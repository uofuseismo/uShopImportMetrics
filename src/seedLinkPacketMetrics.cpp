#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <cctype>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/daily_file_sink.h>
//NOLINTNEXTLINE(misc-include-cleaner)
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/common.h>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <opentelemetry/metrics/provider.h>
#include "uShopImportMetrics/version.hpp"
#include "uShopImportMetrics/metricsSingleton.hpp"
#include "uShopImportMetrics/seedLinkClient.hpp"
#include "uShopImportMetrics/seedLinkClientOptions.hpp"
#include "uShopImportMetrics/streamSelector.hpp"
#include "uShopImportMetrics/packet.hpp"
#include "otelMetrics.hpp"

#define APPLICATION_NAME "uSEEDLinkPacketMetrics"

using namespace UShopImportMetrics;

namespace
{

volatile std::sig_atomic_t mSignalStatus;
std::atomic_bool mInterrupted{false};

}

namespace
{

struct OTelHTTPMetricsOptions
{
    std::string url{"localhost:4318"};
    std::chrono::milliseconds exportInterval{5000};
    std::chrono::milliseconds exportTimeOut{500};
    std::string suffix{"/v1/metrics"};
};

//NOLINTNEXTLINE(misc-include-cleaner)
std::string getOTelCollectorURL(boost::property_tree::ptree &propertyTree,
                                const std::string &section)
{
    std::string result;
    const std::string otelCollectorHost
        = propertyTree.get<std::string> (section + ".host", "");
    const uint16_t otelCollectorPort
        = propertyTree.get<uint16_t> (section + ".port", 4218);
    if (!otelCollectorHost.empty())
    {
        result = otelCollectorHost + ":"
               + std::to_string(otelCollectorPort);
    }
    return result;
}

struct ProgramOptions
{
    OTelHTTPMetricsOptions otelHTTPMetricsOptions;
    SEEDLinkClientOptions seedLinkOptions;
    std::filesystem::path logDirectory{"./"};
    std::string applicationName{APPLICATION_NAME};
    std::string otelAttributes;
    std::chrono::seconds windowedMetricsUpdateInterval{120};
    std::chrono::seconds printSummary{std::chrono::minutes {15}};
    int verbosity{3};
    size_t maximumQueueSize{4096};
    bool consoleLog{true};
    bool exportMetrics{false};

    void readInitializationFile(const std::filesystem::path &iniFile)
    {
        if (!std::filesystem::exists(iniFile))
        {
            throw std::runtime_error(std::string {iniFile} + " does not exist");
        }   

        boost::property_tree::ptree propertyTree;
        boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);

        // Application name
        applicationName
            = propertyTree.get<std::string> ("General.applicationName",
                                             applicationName);
        if (applicationName.empty())
        {   
            applicationName = APPLICATION_NAME;
        }   
        // Chatty-ness
        verbosity
            = propertyTree.get<int> ("General.verbosity", verbosity);
        // Log directory
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


        // Metrics
        otelHTTPMetricsOptions.url
            = getOTelCollectorURL(propertyTree, "OTelHTTPMetricsOptions");
        otelHTTPMetricsOptions.suffix
            = propertyTree.get<std::string> ("OTelHTTPMetricsOptions.suffix",
                                             "/v1/metrics");
        if (!otelHTTPMetricsOptions.url.empty())
        {
            if (!otelHTTPMetricsOptions.suffix.empty())
            {
                if (!otelHTTPMetricsOptions.url.ends_with("/") &&
                    !otelHTTPMetricsOptions.suffix.starts_with("/"))
                {
                    otelHTTPMetricsOptions.suffix = "/" 
                                                + otelHTTPMetricsOptions.suffix;
                }
                otelHTTPMetricsOptions.url = otelHTTPMetricsOptions.url
                                           + otelHTTPMetricsOptions.suffix;
            }
        }
        if (!otelHTTPMetricsOptions.url.empty())
        {
            exportMetrics = true;
            auto updateInterval
                = static_cast<int> (windowedMetricsUpdateInterval.count());
            updateInterval
                = propertyTree.get<int> (
                     "OTelMetricsOptions.windowedMetricsUpdateIntervalInSeconds",
                     updateInterval);
            if (updateInterval <= 0)
            {
                throw std::invalid_argument("Metrics update interval must be non-negative");
            }
            windowedMetricsUpdateInterval
                 = std::chrono::seconds {updateInterval};
            const auto otelAttributesDataSource
                 = propertyTree.get<std::string> (
                      "OTelMetricsOptions.dataSourceAttribute",
                      "");
            if (!otelAttributesDataSource.empty())
            {
                if (!otelAttributes.empty()){otelAttributes += ",";}
                otelAttributes = "source=" + otelAttributesDataSource;
            }
            const auto otelAttributesDomain
                = propertyTree.get<std::string> (
                     "OTelMetricsOptions.domain", "");
            if (!otelAttributesDomain.empty())
            {
                if (!otelAttributes.empty()){otelAttributes += ",";}
                otelAttributes = otelAttributes + "domain=" + otelAttributesDomain;
            }
        }
 
        // SEEDLink client options
        auto slinkHost
            = propertyTree.get<std::string> ("SEEDLink.host");
        auto slinkPort
            = propertyTree.get<uint16_t> ("SEEDLink.port", 18000);
        seedLinkOptions.setHost(slinkHost);
        seedLinkOptions.setPort(slinkPort);
        constexpr int maxSelectors{std::numeric_limits<uint16_t>::max()};
        for (int iSelector = 1; iSelector <= maxSelectors; ++iSelector)
        {
            const std::string selectorName{"SEEDLink.data_selector_"
                                         + std::to_string(iSelector)};
            auto selectorString
                = propertyTree.get_optional<std::string> (selectorName);
            if (selectorString)
            {
                std::vector<std::string> splitSelectors;
                boost::split(splitSelectors, *selectorString,
                             boost::is_any_of(",|"));
                for (const auto &thisSplitSelector : splitSelectors)
                {
                    auto selector
                       = StreamSelector::fromString(thisSplitSelector);
                    seedLinkOptions.addStreamSelector(selector);
                }
            } 
        }
                     

    }

    [[nodiscard]] static std::optional<std::filesystem::path>
        parseCommandLineOptions(int argc, char *argv[])
    {   
        std::string iniFile;
        boost::program_options::options_description desc(
R"""(
The uSEEDLinkPacketMetrics scrapes MiniSEED packets from a SEEDLink client and
attempts to compute metrics such as latency, average counts, etc. on a per 
stream basis.  

Example usage:

    uSEEDLinkPacketMetrics --ini=metrics.ini

Allowed options)""");
        desc.add_options()
            ("help", "Produces this help message")
            ("ini",  boost::program_options::value<std::string> (), 
                     "Defines the initialization file for this executable")
            ("version", "Displays the version number");
        boost::program_options::variables_map vm; 
        //NOLINTBEGIN
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, desc), vm);
        //NOLINTEND
        boost::program_options::notify(vm);
        if (vm.count("help"))
        {
            std::cout << desc << "\n";
            return std::nullopt;
        }
        else if (vm.count("version"))
        {
            std::cout << Version::getVersionWithTag() << "\n";
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

}

class Process
{
public:
    Process
    (
        ProgramOptions &options,
        std::shared_ptr<spdlog::logger> logger
    ) :
        mOptions(options),
        mLogger(std::move(logger))
    {
        mMaximumQueueSize = mOptions.maximumQueueSize;
        if (mLogger == nullptr)
        {
            //NOLINTNEXTLINE(misc-include-cleaner)
            mLogger = spdlog::stdout_color_mt("ProcessConsole");
        }
        mSEEDLinkClient
            = std::make_unique<SEEDLinkClient> (
                mOptions.seedLinkOptions,
                mAddPacketCallbackFunction,
                mLogger);
#ifndef NDEBUG
        assert(mSEEDLinkClient != nullptr);
#endif
        if (mOptions.exportMetrics)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Initializing metrics");
            auto &metrics
                = UShopImportMetrics::MetricsSingleton::getInstance();
            metrics.setUpdateInterval(mOptions.windowedMetricsUpdateInterval);
            // Need a provider from which to get a meter.  This is initialized
            // once and should last the duration of the application.
            auto provider 
                = opentelemetry::metrics::Provider::GetMeterProvider();
    
            // Meter will be bound to application (library, module, class, etc.)
            // so as to identify who is genreating these metrics.
            auto meter = provider->GetMeter(mOptions.applicationName, "1.2.0");

            // Valid (good) packets
            validPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "seedlink.import_packet_metrics.client.packets.valid",
                    "Number of valid data packets received by SEEDLink client.",
                    "{packets}");
            validPacketsReceivedCounter->AddCallback(
                ::observeValidPacketsReceived, nullptr);
            // Future packets
            futurePacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                   "seedlink.import_packet_metrics.client.packets.future",
                   "Number of future packets received by SEEDLink client.",
                   "{packets}");
            futurePacketsReceivedCounter->AddCallback(
                ::observeFuturePacketsReceived, nullptr);
            // Expired packets
            expiredPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "seedlink.import_packet_metrics.client.packets.expired",
                    "Number of expired packets received by SEEDLink client.",
                    "{packets}");
            expiredPacketsReceivedCounter->AddCallback(
                ::observeExpiredPacketsReceived, nullptr);
            // Total packets received
            totalPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "seedlink.import_packet_metrics.client.packets.all",
                    "Total number of packets received by SEEDLink client.  This includes future and expired packets.",
                    "{packets}");
            totalPacketsReceivedCounter->AddCallback(
                ::observeTotalPacketsReceived, nullptr);
            // Windowed average latency
            windowedAverageLatencyGauge
                = meter->CreateDoubleObservableGauge(
                    "seedlink.import_packet_metrics.client.windowed.latency.average",
                    "The windowed average latency of packets received by SEEDLink client.",
                    "{s}");
            windowedAverageLatencyGauge->AddCallback(
                ::observeWindowedAverageLatency, nullptr);
            // Windowed average counts
            windowedAverageCountsGauge
                = meter->CreateDoubleObservableGauge(
                    "seedlink.import_packet_metrics.client.windowed.counts.average",
                    "The windowed average number of counts received by SEEDLink client.",
                    "{counts}");
            windowedAverageCountsGauge->AddCallback(
                ::observeWindowedAverageCounts, nullptr);
            // Windowed std of counts
            windowedStdCountsGauge
                = meter->CreateDoubleObservableGauge(
                    "seedlink.import_packet_metrics.client.windowed.counts.standard_deviaton",
                    "The windowed standard deviation of counts of packets received by SEEDLink client.",
                    "{counts}");
            windowedStdCountsGauge->AddCallback(
                ::observeWindowedStdCounts, nullptr);
        }
    }

    ~Process()
    {
        stop();
    }

    void start()
    {
        mKeepRunning.store(true);
        SPDLOG_LOGGER_INFO(mLogger, "Launching metrics thread");
        mMetricsFuture = std::async(&Process::tabulateMetrics, this);
        SPDLOG_LOGGER_INFO(mLogger, "Launching SEEDLink reader thread");
        mAcquisitionFuture = mSEEDLinkClient->start();
        handleMainThread();
    }

    void stop()
    {   
        constexpr std::chrono::milliseconds pause{10};
        mKeepRunning.store(false);
        if (mSEEDLinkClient){mSEEDLinkClient->stop();}
        std::this_thread::sleep_for(pause);
        if (mAcquisitionFuture.valid()){mAcquisitionFuture.get();}
        std::this_thread::sleep_for(pause);
        if (mMetricsFuture.valid()){mMetricsFuture.get();}
    }

    void addPacketCallback(Packet &&packet)
    {
        int nPacketsSkipped{0};
        {
        const std::lock_guard<std::mutex> lock(mImportMutex);
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
            = UShopImportMetrics::MetricsSingleton::getInstance();
        auto lastUpdate
            = std::chrono::duration_cast<std::chrono::microseconds>
              ((std::chrono::high_resolution_clock::now()).time_since_epoch());
        while (mKeepRunning)
        {
            bool gotPacket{false};
            Packet packet;
            {
            const std::lock_guard<std::mutex> lock(mImportMutex);
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
                    metrics.updateAndResetWindowedMetrics();
                    lastUpdate
                       = std::chrono::duration_cast<std::chrono::microseconds>
                         ((std::chrono::high_resolution_clock::now())
                           .time_since_epoch());
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
                const auto now
                    = std::chrono::duration_cast<std::chrono::microseconds>
                      ((std::chrono::high_resolution_clock::now())
                        .time_since_epoch());
                // If we keep missing we periodically want to flush information
                if (now > lastUpdate + mOptions.windowedMetricsUpdateInterval)
                {
                    metrics.updateAndResetWindowedMetrics();
                }
                std::this_thread::sleep_for(timeOut);
            }
        }
    }

    void handleMainThread()
    {
        SPDLOG_LOGGER_DEBUG(mLogger, "Main thread entering waiting loop");
        stdCatchSignals();
        while (!mStopRequested)
        {
            if (mInterrupted)
            {
                SPDLOG_LOGGER_INFO(mLogger,
                                   "SIGINT/SIGTERM signal received!");
                mStopRequested = true;
                break;
            }
            constexpr std::chrono::milliseconds waitFor{5};
            if (!checkFuturesOkay(waitFor))
            {
                SPDLOG_LOGGER_CRITICAL(mLogger,
                   "Futures exception caught; terminating app");
                mStopRequested = true;
                break;
            }
            constexpr std::chrono::milliseconds pauseFor{100};
            std::unique_lock<std::mutex> lock(mStopMutex);
            mStopCondition.wait_for(lock,
                                    pauseFor,
                                    [this]
                                    {
                                          return mStopRequested;
                                    });
            lock.unlock();
        }
        if (mStopRequested)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, 
                                "Stop request received.  Exiting...");
            stop();
        }
    }

    /// True indicates the all the processes are running a-okay.
    [[nodiscard]] 
    bool checkFuturesOkay(const std::chrono::milliseconds &timeOut)
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

    void stdCatchSignals()
    {
        std::signal(SIGINT,  Process::stdSignalHandler);
        std::signal(SIGTERM, Process::stdSignalHandler);
    }

    static void stdSignalHandler(const int signal)
    {
        mSignalStatus = signal;
        mInterrupted = true;
    }
    ProgramOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::function<void (Packet &&)> 
        mAddPacketCallbackFunction
    {    
        std::bind(&::Process::addPacketCallback, this,
                  std::placeholders::_1)
    };   
    std::unique_ptr<SEEDLinkClient> mSEEDLinkClient{nullptr};
    std::queue<Packet> mPacketQueue;
    std::mutex mImportMutex;
    std::mutex mStopMutex;
    std::condition_variable mStopCondition;
    std::future<void> mAcquisitionFuture;
    std::future<void> mMetricsFuture;
    std::atomic<bool> mKeepRunning{true};
    size_t mMaximumQueueSize{4096};
    bool mStopRequested{false};
};

///--------------------------------------------------------------------------///

int main(int argc, char *argv[])
{
    // Get this done early
    UShopImportMetrics::initializeMetricsSingleton();
 
    // Okay, now get the command line options
    std::filesystem::path iniFile;
    try
    {
        auto result = ProgramOptions::parseCommandLineOptions(argc, argv);
        if (result == std::nullopt){return EXIT_SUCCESS;} // Help
        iniFile = *result;
    }
    catch (const std::exception &e)
    {
        //NOLINTNEXTLINE(misc-include-cleaner)
        auto logger = spdlog::stdout_color_st("Console");
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to launch because {}", 
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    ProgramOptions options;
    try
    {
        options.readInitializationFile(iniFile);
    }
    catch (const std::exception &e)
    {
        auto logger = spdlog::stdout_color_st("Console");
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to read ini file because {}", 
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    // Setup logger
    std::shared_ptr<spdlog::logger> logger{nullptr};
    if (options.consoleLog)
    {   
        //NOLINTNEXTLINE(misc-include-cleaner)
        logger = spdlog::stdout_color_mt(options.applicationName + "-console");
    }   
    else
    {   
        logger
            = spdlog::daily_logger_mt(options.applicationName,
                 options.logDirectory/"uSEEDLinkMetrics.log",
                 0, 0); 
    }   
    logger->set_level(spdlog::level::err);
    if (options.verbosity > 2)
    {   
        logger->set_level(spdlog::level::debug);
    }   
    else if (options.verbosity == 2)
    {   
        logger->set_level(spdlog::level::info);
    }
    else if (options.verbosity == 1)
    {
        logger->set_level(spdlog::level::warn);
    }

    // Initialize metrics
    if (std::getenv("OTEL_SERVICE_NAME") == nullptr)
    {
        auto serviceName = options.applicationName;
        if (serviceName.empty()){serviceName = APPLICATION_NAME;}
        std::transform(serviceName.begin(),
                       serviceName.end(),
                       serviceName.begin(),
                       ::tolower);
        SPDLOG_LOGGER_INFO(logger,
                           "Setting OTEL_SERVICE_NAME to {}",
                           serviceName);
        constexpr int overwrite{1};
        setenv("OTEL_SERVICE_NAME", serviceName.c_str(), overwrite);
    }
    if (std::getenv("OTEL_RESOURCE_ATTRIBUTES") == nullptr)
    {
        if (!options.otelAttributes.empty())
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Setting OTEL_RESOURCE_ATTRIBUTES to {}",
                               options.otelAttributes);
            constexpr int overwrite{1};
            setenv("OTEL_RESOURCE_ATTRIBUTES",
                   options.otelAttributes.c_str(),
                   overwrite);
        }
    }
    if (options.exportMetrics)
    {   
        try
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Configuring OTel to send metrics to {}",
                               options.otelHTTPMetricsOptions.url);
            ::initializeOTelHTTP(options.otelHTTPMetricsOptions.url,
                                 options.otelHTTPMetricsOptions.exportInterval,
                                 options.otelHTTPMetricsOptions.exportTimeOut);
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_CRITICAL(logger,
                                   "Failed to initialize OTel because {}",
                                   std::string {e.what()});
            return EXIT_FAILURE;
        }
    }   

    std::unique_ptr<Process> process{nullptr}; 
    try
    {
        SPDLOG_LOGGER_INFO(logger, "Initializing main process class...");
        process = std::make_unique<Process> (options, logger);
    }
    catch (const std::exception &e)
    {
        ::cleanupOTelMetrics();
        SPDLOG_LOGGER_CRITICAL(logger,
                          "Failed to initialized main process class because {}",
                          std::string {e.what()});
        return EXIT_FAILURE;
    }

    try
    {
        SPDLOG_LOGGER_INFO(logger, "Starting main process...");
        process->start();
    }
    catch (const std::exception &e)
    {
        ::cleanupOTelMetrics();
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Metrics tabulator failed because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
