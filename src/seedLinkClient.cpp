#include <chrono>
#include <string>
#include <array>
#include <cstdint>
#include <libmseed.h>
#include <libslink.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "uShopImportMetrics/seedLinkClient.hpp"
#include "uShopImportMetrics/seedLinkClientOptions.hpp"
#include "uShopImportMetrics/packet.hpp"
#include "uShopImportMetrics/streamIdentifier.hpp"
#include "uShopImportMetrics/streamSelector.hpp"
#include "uShopImportMetrics/version.hpp"

using namespace UShopImportMetrics;

namespace
{

/// @brief Unpacks a miniSEED record.
[[nodiscard]]
std::vector<Packet>
    miniSEEDToDataPackets(char *msRecord, const int bufferSize)
{
    std::vector<Packet> dataPackets;
    auto bufferLength = static_cast<uint64_t> (bufferSize);
    uint64_t offset{0};
    // Iterate through the consumed buffer
    while (bufferLength - offset > MINRECLEN)
    {   
        // Convert every packet in the buffer
        constexpr int8_t verbose{0};
        const uint32_t flags{MSF_UNPACKDATA};
        Packet dataPacket;
        MS3Record *miniSEEDRecord{nullptr};
        auto returnCode = msr3_parse(msRecord + offset,
                                     static_cast<uint64_t> (bufferSize) - offset,
                                     &miniSEEDRecord, flags,
                                     verbose);
        if (returnCode == MS_NOERROR && miniSEEDRecord)
        {
            // SNCL
            constexpr size_t MAX_CHAR_LENGTH{64};
            std::array<char, MAX_CHAR_LENGTH> networkWork;
            std::array<char, MAX_CHAR_LENGTH> stationWork;
            std::array<char, MAX_CHAR_LENGTH> channelWork;
            std::array<char, MAX_CHAR_LENGTH> locationWork;
            std::fill(networkWork.begin(),  networkWork.end(), '\0');
            std::fill(stationWork.begin(),  stationWork.end(), '\0');
            std::fill(channelWork.begin(),  channelWork.end(), '\0'); 
            std::fill(locationWork.begin(), locationWork.end(), '\0');
            returnCode = ms_sid2nslc(miniSEEDRecord->sid,
                                     networkWork.data(),  //networkWork.size(),
                                     stationWork.data(),  //stationWork.size(),
                                     locationWork.data(), //locationWork.size(),
                                     channelWork.data()); //channelWork.size());
            if (returnCode == MS_NOERROR)
            {
                const std::string network(networkWork.data());
                const std::string station(stationWork.data());
                const std::string channel(channelWork.data());
                StreamIdentifier identifier;
                identifier.setNetwork(network);
                identifier.setStation(station);
                identifier.setChannel(channel);
                if (locationWork[0] == '\0')
                {
                    identifier.setLocationCode("--");
                }
                else
                {
                    const std::string locationCode(locationWork.data());
                    identifier.setLocationCode(locationCode);
                }
                dataPacket.setStreamIdentifier(std::move(identifier));
            }
            else
            {
                msr3_free(&miniSEEDRecord);
                throw std::runtime_error("Failed to unpack SNCL");
            }
            // Sampling rate
            dataPacket.setSamplingRate(miniSEEDRecord->samprate);
            // Start time
            std::chrono::microseconds startTime
            {
                static_cast<int64_t>
                    (std::round(miniSEEDRecord->starttime)*1.e-3)
            };
            dataPacket.setStartTime(startTime);
            // Data
            auto nSamples = static_cast<int> (miniSEEDRecord->numsamples);
            if (nSamples > 0)
            {
                if (miniSEEDRecord->sampletype == 'i')
                {
                    const auto data
                        = reinterpret_cast<const int *>
                          (miniSEEDRecord->datasamples);
                    dataPacket.setData(nSamples, data);
                }
                else if (miniSEEDRecord->sampletype == 'f')
                {
                    const auto data
                       = reinterpret_cast<const float *>
                          (miniSEEDRecord->datasamples);
                    dataPacket.setData(nSamples, data);
                }
                else if (miniSEEDRecord->sampletype == 'd')
                {
                    const auto data
                        = reinterpret_cast<const double *>
                          (miniSEEDRecord->datasamples);
                    dataPacket.setData(nSamples, data);
                }
                else
                {
                    msr3_free(&miniSEEDRecord);
                    throw std::runtime_error("Unhandled sample type");
                }
            } // End check on nSamples
            dataPackets.push_back(std::move(dataPacket));
            offset = offset + miniSEEDRecord->reclen;
            msr3_free(&miniSEEDRecord);
        }
        else
        {
            if (returnCode != MS_NOERROR)
            {
                if (miniSEEDRecord){msr3_free(&miniSEEDRecord);}
                throw std::runtime_error("libmseed error detected");
            }
            msr3_free(&miniSEEDRecord);
            throw std::runtime_error(
                 "Insufficient data.  Number of additional bytes estimated is "
                + std::to_string(returnCode));
        }
    }
    return dataPackets;
}

}

///--------------------------------------------------------------------------///

class SEEDLinkClient::SEEDLinkClientImpl
{
public:
    explicit SEEDLinkClientImpl(
        const SEEDLinkClientOptions &options,
        const std::function<void (Packet &&)> &callback,
        std::shared_ptr<spdlog::logger> logger) :
        mAddPacketCallback(callback),
        mOptions(options),
        mLogger(logger)
    {
        if (mLogger == nullptr)
        {
            mLogger = spdlog::stdout_color_mt("SEEDLinkConsole");
        }
        initialize(options);
    }        
    /// Destructor
    ~SEEDLinkClientImpl()
    {
        stop();
        disconnect();
    }
    /// Terminate the SEED link client connection
    void disconnect()
    {   
        if (mSEEDLinkConnection != nullptr)
        {
            if (mSEEDLinkConnection->link != -1)
            {
                SPDLOG_LOGGER_DEBUG(mLogger, "Disconnecting SEEDLink...");
                sl_disconnect(mSEEDLinkConnection);
            }
            /*
            if (mUseStateFile)
            {
                SPDLOG_LOGGER_INFO(mLogger,
                                  "Saving state prior to disconnect");
                sl_savestate(mSEEDLinkConnection, mStateFile.c_str());
            }
            */
            SPDLOG_LOGGER_DEBUG(mLogger, "Freeing SEEDLink structure...");
            sl_freeslcd(mSEEDLinkConnection);
            mSEEDLinkConnection = nullptr;
        }
    }
    /// Sends a terminate command to the SEEDLink connection
    void terminate()
    {
        if (mSEEDLinkConnection != nullptr)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Issuing terminate command to poller");
            sl_terminate(mSEEDLinkConnection);
        }
    }
    /// Starts the service
    [[nodiscard]] std::future<void> start()
    {
        stop(); // Ensure module is stopped
        if (!mInitialized)
        {
            throw std::runtime_error("SEEDLink client not initialized");
        }
        setRunning(true);
        SPDLOG_LOGGER_DEBUG(mLogger, "Starting the SEEDLink polling thread...");
        mSEEDLinkConnection->terminate = 0;
        auto result = std::async(&SEEDLinkClientImpl::packetToCallback, this);
        return result;
    }
    /// Toggles this as running or not running
    void setRunning(const bool running)
    {
        // Terminate the session
        if (!running && mKeepRunning)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Issuing terminate command");
            terminate();
        }
        // Tell the scraping thread to quit if it hasn't already given up
        // because it received a terminate request
        mKeepRunning = running;
    }
    /// Stops the service
    void stop()
    {
        setRunning(false); // Issues terminate command
    }
    /// Initialize
    void initialize(const SEEDLinkClientOptions &options)
    {   
        mHaveOptions = false;
        disconnect();
        mInitialized = false;
        // Create a new instance
        mSEEDLinkConnection
            = sl_initslcd(mClientName.c_str(),
                          UShopImportMetrics::Version::getVersion().c_str());
        if (!mSEEDLinkConnection)
        {                   
            throw std::runtime_error("Failed to create client handle");
        }
        // Set the connection string            
        auto host = options.getHost();
        auto port = options.getPort();
        auto seedLinkAddress = host +  ":" + std::to_string(port);
        SPDLOG_LOGGER_INFO(mLogger, 
                           "Will connect to SEEDLink server at {}",
                           seedLinkAddress);
        if (sl_set_serveraddress(
               mSEEDLinkConnection, seedLinkAddress.c_str()) != 0)
        {                                  
            throw std::invalid_argument("Failed to set server address "
                                      + seedLinkAddress);
        }   
        // Set the record size and state file
        //mSEEDRecordSize = options.getSEEDRecordSize();
        /*
        if (options.hasStateFile())
        {   
            mStateFile = options.getStateFile();
            if (options.deleteStateFileOnStart())
            {
                if (std::filesystem::exists(mStateFile))
                {
                    if (!std::filesystem::remove(mStateFile))
                    {
                        SPDLOG_LOGGER_WARN(mLogger,
                            "Failed to remove state file {}",
                            mStateFile);
                    }
                }
            }
            mStateFileUpdateInterval = options.getStateFileUpdateInterval();
            mUseStateFile = true;
            mDeleteStateFileOnStop = options.deleteStateFileOnStop();
        }
        */
        // If there are selectors then try to use them
        constexpr uint64_t sequenceNumber{SL_UNSETSEQUENCE}; // Start at next data
        const char *timeStamp{nullptr};
        auto streamSelectors = options.getStreamSelectors();
        for (const auto &selector : streamSelectors)
        {
            try
            {
                auto network = selector.getNetwork();
                auto station = selector.getStation();
                auto stationID = network + "_" + station;
                auto streamSelector = selector.getSelector();
                SPDLOG_LOGGER_INFO(mLogger, "Adding SEEDLink selector: {} {}",
                                   stationID, streamSelector);
                auto returnCode = sl_add_stream(mSEEDLinkConnection,
                                                stationID.c_str(),
                                                streamSelector.c_str(),
                                                sequenceNumber,
                                                timeStamp);
                if (returnCode != 0)
                {
                    throw std::runtime_error("Failed to add selector: "
                                           + network + " "
                                           + station + " "
                                           + streamSelector);
                }
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_WARN(mLogger,
                    "Could not add selector because {}",
                    std::string {e.what()});
            }
        }
        // Configure uni-station mode if no streams were specified
        if (mSEEDLinkConnection->streams == nullptr)
        {
            const char *selectors{nullptr};
            auto returnCode = sl_set_allstation_params(mSEEDLinkConnection,
                                                       selectors,
                                                       sequenceNumber,
                                                       timeStamp);
            if (returnCode != 0)
            {
                SPDLOG_LOGGER_ERROR(mLogger,
                    "Could not set SEEDLink uni-station mode");
                throw std::runtime_error(
                    "Failed to create a SEEDLink uni-station client");
            }
        }
        // Preferentially do not block so our thread can check for other
        // commands.
        constexpr bool nonBlock{true};
        if (sl_set_blockingmode(mSEEDLinkConnection, nonBlock) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set non-blocking mode");
        }
#ifndef NDEBUG
        assert(mSEEDLinkConnection->noblock == 1);
#endif
        constexpr bool closeConnection{false};
        if (sl_set_dialupmode(mSEEDLinkConnection, closeConnection) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set keep-alive connection");
        }
#ifndef NDEBUG
        assert(mSEEDLinkConnection->dialup == 0);
#endif
        // Time out and reconnect delay
        auto networkTimeOut
            = static_cast<int> (options.getNetworkTimeOut().count());
        if (sl_set_idletimeout(mSEEDLinkConnection, networkTimeOut) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set idle connection time");
        }
        auto reconnectDelay
            = static_cast<int> (options.getNetworkReconnectDelay().count());
        if (sl_set_reconnectdelay(mSEEDLinkConnection, reconnectDelay) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set reconnect delay");
        }
        // Check this worked
#ifndef NDEBUG
        std::string slSite(512, '\0');
        std::string slServerID(512, '\0');
        auto returnCode = sl_ping(mSEEDLinkConnection,
                                  slServerID.data(),
                                  slSite.data());
        if (returnCode != 0)
        {
            if (returnCode ==-1)
            {
                SPDLOG_LOGGER_WARN(mLogger, "Invalid ping response");
            }
            else
            {
                SPDLOG_LOGGER_ERROR(mLogger, "Could not connect to server");
                throw std::runtime_error("Failed to connect");
            }
        }
        else
        {
            SPDLOG_LOGGER_INFO(mLogger,
               "SEEDLink ping successfully returned server {} (site {})",
               slServerID, slSite);
        }
#endif
        // All-good
        mOptions = options;
        mInitialized = true;
        mHaveOptions = true;
    }
    /// Scrapes the packets and puts them to the callback
    void packetToCallback()
    {
        constexpr std::chrono::milliseconds timeToSleep{50};
        mConnected = true;
        // Recover state
        /*
        if (mUseStateFile)
        {
            if (!sl_recoverstate(mSEEDLinkConnection, mStateFile.c_str()))
            {
                 throw std::runtime_error("Failed to recover state");
            }
        }
        */
        // Now start scraping
        //sl_printslcd(mSEEDLinkConnection); // Useful for debugging
        const SLpacketinfo *seedLinkPacketInfo{nullptr};
        std::array<char, SL_RECV_BUFFER_SIZE> seedLinkBuffer;
        const auto seedLinkBufferSize
            = static_cast<uint32_t> (seedLinkBuffer.size());
        //int updateStateFile{1};
        SPDLOG_LOGGER_DEBUG(mLogger,
                            "Thread entering SEEDLink polling loop...");
        while (mKeepRunning)
        {
            // Attempt to collect data but then immediately return.
            auto returnValue = sl_collect(mSEEDLinkConnection,
                                          &seedLinkPacketInfo,
                                          seedLinkBuffer.data(),
                                          seedLinkBufferSize);
            // Deal with packet
            if (returnValue == SLPACKET)
            {
                // I really only care about data packets
                if (seedLinkPacketInfo->payloadformat == SLPAYLOAD_MSEED2 ||
                    seedLinkPacketInfo->payloadformat == SLPAYLOAD_MSEED3)
                {
                    auto payloadLength = seedLinkPacketInfo->payloadlength;
                    try
                    {
                        auto packets
                            = ::miniSEEDToDataPackets(seedLinkBuffer.data(),
                                                      payloadLength);
                        if (packets.empty())
                        {
                            SPDLOG_LOGGER_WARN(mLogger,
                                               "No mseed packets unpacked");
                        }
                        else if (packets.size() > 1)
                        {
                            SPDLOG_LOGGER_WARN(mLogger,
                                             "Multiple mseed packets received");
                        }
                        for (auto &packet : packets)
                        {
                            try
                            {
                                mAddPacketCallback( std::move(packet) );
                            }
                            catch (const std::exception &e)
                            {
                                SPDLOG_LOGGER_WARN(mLogger,
                                    "Failed to propagate packet because {}",
                                    std::string {e.what()});
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        SPDLOG_LOGGER_WARN(mLogger,
                           "Skipping packet.  Unpacking failed with {}",
                           std::string(e.what()));
                    }
                    /*
                    if (mUseStateFile)
                    {
                        if (updateStateFile > mStateFileUpdateInterval)
                        {
                            sl_savestate(mSEEDLinkConnection,
                                         mStateFile.c_str());
                            updateStateFile = 0;
                        }
                        updateStateFile = updateStateFile + 1;
                    }
                    */
                }
            }
            else if (returnValue == SLTOOLARGE)
            {
                SPDLOG_LOGGER_WARN(mLogger,
                    "Payload length {} exceeds {}; skipping",
                    seedLinkPacketInfo->payloadlength,
                    seedLinkBufferSize);
                continue;
            }
            else if (returnValue == SLNOPACKET)
            {
                SPDLOG_LOGGER_DEBUG(mLogger, "No data from sl_collect");
                std::this_thread::sleep_for(timeToSleep);
                continue;
            }
            else if (returnValue == SLTERMINATE)
            {
                SPDLOG_LOGGER_INFO(mLogger,
                                   "SEEDLink terminate request received");
                mConnected = false;
                break;
            }
            else
            {
                SPDLOG_LOGGER_WARN(mLogger,
                     "Unhandled SEEDLink return value: {}",
                     returnValue);
                continue;
            }
        } // Loop on keep running
        // Purge state file
        /*
        if (mUseStateFile && mDeleteStateFileOnStop)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Purging state file {}", mStateFile);
            if (std::filesystem::exists(mStateFile))
            {
                if (!std::filesystem::remove(mStateFile))
                {
                    throw std::runtime_error("Failed to purge state file " 
                                           + mStateFile);
                }
            }
        }
        */
        if (mKeepRunning)
        {
            SPDLOG_LOGGER_CRITICAL(mLogger,
                                   "Premature end of SEEDLink import");
            throw std::runtime_error("Premature end of SEEDLink import");
        }
        SPDLOG_LOGGER_INFO(mLogger, "Thread leaving SEEDLink polling loop");
        mConnected = false;
    }
//private:
    std::function
    <   
        void(Packet &&) 
    > mAddPacketCallback;
    SEEDLinkClientOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr}; 
    std::string mClientName{"uShopImportMetricsSEEDLinkPacketImporter"};
    SLCD *mSEEDLinkConnection{nullptr};
    //std::string mStateFile;
    std::atomic<bool> mKeepRunning{true};
    std::atomic<bool> mConnected{false};
    //int mStateFileUpdateInterval{100};
    //int mSEEDRecordSize{512};
    bool mHaveOptions{false};
    //bool mUseStateFile{false};
    //bool mDeleteStateFileOnStop{false};
    bool mInitialized{false};
};


/// Constructor
SEEDLinkClient::SEEDLinkClient(
    const SEEDLinkClientOptions &options,
    const std::function<void (Packet &&)> &callback,
    std::shared_ptr<spdlog::logger> logger) :
    pImpl(std::make_unique<SEEDLinkClientImpl> (options, callback, logger))
{
    //pImpl->initialize(options);
}

/// Initialized?
bool SEEDLinkClient::isInitialized() const noexcept
{
    return pImpl->mInitialized;
}

/// Start the client
std::future<void> SEEDLinkClient::start()
{
    if (!isInitialized())
    {   
        throw std::runtime_error("SEEDLink client not initialized");
    }   
    return pImpl->start();
}

/// Stop the client
void SEEDLinkClient::stop()
{
    pImpl->stop();
}


SEEDLinkClient::~SEEDLinkClient() = default;
