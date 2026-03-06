#include <iostream>
#include <csignal>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <filesystem>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "uShopImportMetrics/waveRing.hpp"
#include "uShopImportMetrics/version.hpp"
#include "uShopImportMetrics/packet.hpp"

import Metrics;

using namespace UShopImportMetrics;

namespace
{

std::atomic<bool> mInterrupted{false};

struct ProgramOptions
{
    WaveRingOptions waveRingOptions;
    std::filesystem::path logDirectory{"./"};
    std::string applicationName{"uEarthwormPacketMetrics"};
    int verbosity{3};
    size_t maximumQueueSize{4096};
    bool consoleLog{true};
    bool exportMetrics{false};

    explicit ProgramOptions(const std::filesystem::path &iniFile)
    {
        if (!std::filesystem::exists(iniFile))
        {
            throw std::runtime_error(std::string {iniFile} + " does not exist");
        }

        boost::property_tree::ptree propertyTree;
        boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);

        auto moduleName
            = propertyTree.get<std::string> ("Earthworm.moduleName",
                                             "MOD_EARTHWORM_METRICS");
        if (moduleName.empty())
        {
            throw std::invalid_argument("Earthworm.moduleName not specified");
        }
        auto ringName
            = propertyTree.get<std::string> ("Earthworm.ringName", "WAVE_RING");
        if (ringName.empty())
        {
            throw std::invalid_argument("Earthworm.ringName not specified");
        }
        waveRingOptions.moduleName = moduleName;
        waveRingOptions.ringName = ringName;

        auto logDirectoryName
            = propertyTree.get_optional<std::string> ("General.logDirectory");
        if (logDirectoryName)
        {
            if (logDirectoryName->empty()){*logDirectoryName = "./";}
            logDirectory = *logDirectoryName;
            if (!std::filesystem::exists(logDirectory))
            {
                std::filesystem::create_directories(logDirectory);
            }
            if (!std::filesystem::exists(logDirectory))
            {
                throw std::runtime_error("Could not create log directory: "
                                       + logDirectory.string());
            }
            consoleLog = false;
        }
        else
        {
            consoleLog = true;
        }

        maximumQueueSize
            = propertyTree.get<size_t> ("General.maximumQueueSize",
                                        maximumQueueSize);
        if (maximumQueueSize == 0)
        {
            throw std::invalid_argument(
               "General.maximumQueueSize must be positive");
        }

    }

    [[nodiscard]] static std::optional<std::filesystem::path>
        parseCommandLineOptions(int argc, char *argv[])
    {
        std::string iniFile;
        boost::program_options::options_description desc(
R"""(
The uEarthwormPacketMetrics scrapes TraceBuf2 packets from an Earthworm ring and
attempts to compute metrics such as latency, average counts, etc. per stream.

    uEarthwormPacketMetrics --ini=metrics.ini

Allowed options)""");
        desc.add_options()
            ("help", "Produces this help message")
            ("ini",  boost::program_options::value<std::string> (), 
                     "Defines the initialization file for this executable")
            ("version", "Displays the version number");
        boost::program_options::variables_map vm; 
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, desc), vm);
        boost::program_options::notify(vm);
        if (vm.count("help"))
        {
            std::cout << desc << std::endl;
            return std::nullopt;
        }
        else if (vm.count("version"))
        {
            std::cout << Version::getVersion() << std::endl;
            return std::nullopt;
        }
        else if (vm.count("ini"))
        {
            iniFile = vm["ini"].as<std::string>();
            if (!std::filesystem::exists(iniFile))
            {
                throw std::runtime_error("Initialization file: " + iniFile
                                       + " does not exist");
            }
        }
        else
        {
            throw std::runtime_error("Initialization file was not set");
        }
        return std::make_optional<std::filesystem::path> (iniFile);
    }
};

///--------------------------------------------------------------------------///

class Process
{
public:
    Process(std::unique_ptr<ProgramOptions> &&options,
            std::shared_ptr<spdlog::logger> logger) :
        mOptions(std::move(options)),
        mLogger(logger)
    {
#ifndef NDEBUG
        assert(mOptions);
        assert(mLogger);
#endif
        if (mOptions->exportMetrics)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Initializing metrics");

        }
        SPDLOG_LOGGER_INFO(mLogger, "Creating earthworm ring reader");
        mMaximumQueueSize = mOptions->maximumQueueSize;
        mRingReader
            = std::make_unique<WaveRing>
                 (mOptions->waveRingOptions, 
                  mAddPacketCallbackFunction,
                  mLogger);
    }

    ~Process()
    {
        stop();
    }

    void addPacketCallback(Packet &&packet)
    {
        int nPacketsSkipped{0};
        {
        std::lock_guard<std::mutex> lock(mImportMutex);
        while (mPacketQueue.size() >= mMaximumQueueSize)
        {
            nPacketsSkipped = nPacketsSkipped + 1;
            mPacketQueue.pop(); // Remove front element
        }
        try
        {
            mPacketQueue.push(std::move(packet));
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                               "Failed to enqueue packet because {}",
                               std::string {e.what()}); 
        }
        }
        if (nPacketsSkipped > 0)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                               "Popped {} packets from queue", nPacketsSkipped);
        }
    }

    void tabulateMetrics()
    {
        auto &metrics
            = UShopImportMetrics::Metrics::MetricsSingleton::getInstance();
        while (mKeepRunning)
        {
            bool gotPacket{false};
            Packet packet;
            {
            std::lock_guard<std::mutex> lock(mImportMutex);
            if (!mPacketQueue.empty())
            {
                gotPacket = true;
                packet = std::move(mPacketQueue.front());
                mPacketQueue.pop();
            }
            }
            // Tabulate metrics if we got a packet
            if (gotPacket)
            {
                try
                {
                    metrics.tabulateMetrics(packet);
                }
                catch (const std::exception &e)
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Failed to tabulate metrics because {}",
                                       std::string {e.what()});
                }
            }
            else
            {
                constexpr std::chrono::milliseconds timeOut{10};
                std::this_thread::sleep_for(timeOut);
            }
        }
    }

    void start()
    {
        mKeepRunning.store(true);
        SPDLOG_LOGGER_INFO(mLogger, "Launching metrics thread");
        mMetricsFuture = std::async(&::Process::tabulateMetrics, this);
        SPDLOG_LOGGER_INFO(mLogger, "Launching Earthworm reader thread");
        mAcquisitionFuture = mRingReader->start();
        handleMainThread();
    }

    void stop()
    {
        mKeepRunning.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds {30});
        if (mRingReader){mRingReader->stop();}
        std::this_thread::sleep_for(std::chrono::milliseconds {10});
        if (mAcquisitionFuture.valid()){mAcquisitionFuture.get();}
        std::this_thread::sleep_for (std::chrono::milliseconds {10});
        if (mMetricsFuture.valid()){mMetricsFuture.get();}
    } 

    void handleMainThread()
    {    
        SPDLOG_LOGGER_DEBUG(mLogger, "Main thread entering waiting loop");
        catchSignals();
        while (!mStopRequested)
        {    
            if (mInterrupted)
            {    
                SPDLOG_LOGGER_INFO(mLogger,
                                   "SIGINT/SIGTERM signal received!");
                mStopRequested = true;
                break;
            }    
            if (!checkFuturesOkay(std::chrono::milliseconds {5}))
            {
                SPDLOG_LOGGER_CRITICAL(mLogger,
                   "Futures exception caught; terminating app");
                 mStopRequested = true;
                break;
            }
            std::unique_lock<std::mutex> lock(mStopMutex);
            mStopCondition.wait_for(lock,
                                    std::chrono::milliseconds {100},
                                    [this]
                                    {
                                          return mStopRequested;
                                    });
            lock.unlock();
        }
        if (mStopRequested)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Stop request received.  Exiting...");
            stop();
        }
    }

    /// True indicates the all the processes are running a-okay.
    [[nodiscard]] bool checkFuturesOkay(const std::chrono::milliseconds &timeOut)
    {     
        bool isOkay{true};
        try
        {
            auto status = mAcquisitionFuture.wait_for(timeOut);
            if (status == std::future_status::ready)
            {
                mAcquisitionFuture.get();
            }
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_CRITICAL(mLogger,
                                   "Fatal error in Earthworm reader: {}",
                                   std::string {e.what()});
            isOkay = false;
        }

        try 
        {   
            auto status = mMetricsFuture.wait_for(timeOut);
            if (status == std::future_status::ready)
            {   
                mMetricsFuture.get();
            }   
        }   
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_CRITICAL(mLogger,
                                   "Fatal error in metrics thread: {}",
                                   std::string {e.what()});
            isOkay = false;
        }

        return isOkay;
    }

    /// Handles sigterm and sigint
    static void signalHandler(const int )
    {    
        mInterrupted = true;
    }    
    static void catchSignals()
    {    
        struct sigaction action;
        action.sa_handler = signalHandler;
        action.sa_flags = 0; 
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT,  &action, NULL);
        sigaction(SIGTERM, &action, NULL);
    }    

    std::unique_ptr<ProgramOptions> mOptions{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::function<void (Packet &&)> 
        mAddPacketCallbackFunction
    {    
        std::bind(&::Process::addPacketCallback, this,
                  std::placeholders::_1)
    };   
    std::mutex mImportMutex;
    std::mutex mStopMutex;
    std::queue<Packet> mPacketQueue;
    std::unique_ptr<WaveRing> mRingReader{nullptr};
    size_t mMaximumQueueSize{4096};
    std::condition_variable mStopCondition;
    std::future<void> mAcquisitionFuture;
    std::future<void> mMetricsFuture;
    std::atomic<bool> mKeepRunning{true};
    bool mStopRequested{false};
};

}

///--------------------------------------------------------------------------///

int main(int argc, char *argv[])
{
    std::filesystem::path iniFile;
    try
    {
        auto result = ProgramOptions::parseCommandLineOptions(argc, argv);
        if (result == std::nullopt){return EXIT_SUCCESS;} // Help
        iniFile = *result;      
    }
    catch (const std::exception &e)
    {
        spdlog::critical(std::string {e.what()});
        return EXIT_FAILURE;
    }

    std::unique_ptr<ProgramOptions> options{nullptr};
    try
    {
        options = std::make_unique<ProgramOptions> (iniFile);
    }
    catch (const std::exception &e)
    {
        spdlog::critical(std::string {e.what()});
        return EXIT_FAILURE;
    } 

    // Setup logger
    std::shared_ptr<spdlog::logger> logger{nullptr};
    if (options->consoleLog)
    {
        logger = spdlog::stdout_color_mt(options->applicationName + "-console");
    }
    else
    {
        logger
            = spdlog::daily_logger_mt(options->applicationName,
                 options->logDirectory/"uEarthwormPacketMetrics.log",
                 0, 0);
    }
    logger->set_level(spdlog::level::err);
    if (options->verbosity > 2)
    {
        logger->set_level(spdlog::level::debug);
    }
    else if (options->verbosity == 2)
    {
        logger->set_level(spdlog::level::info);
    }
    else if (options->verbosity == 1)
    {
        logger->set_level(spdlog::level::warn);
    }

    // Initialize metrics
    UShopImportMetrics::Metrics::initializeMetricsSingleton();

    try
    {
        SPDLOG_LOGGER_INFO(logger, "Initializing main process...");
        auto process = std::make_unique<Process> (std::move(options), logger);
        SPDLOG_LOGGER_INFO(logger, "Starting metrics calculator...");
        process->start();
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Metrics module failed with {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}


