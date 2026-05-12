#include <stdlib.h>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <queue>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <optional>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/common.h>
#include <spdlog/sinks/daily_file_sink.h>
//NOLINTNEXTLINE(misc-include-cleaner)
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
//#include <boost/algorithm/string/classification.hpp>
//#include <boost/algorithm/string/split.hpp>
//#include <opentelemetry/metrics/meter_provider.h>
//#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/metrics/provider.h>
#include "uShopImportMetrics/waveRing.hpp"
#include "uShopImportMetrics/version.hpp"
#include "uShopImportMetrics/packet.hpp"
#include "uShopImportMetrics/metricsSingleton.hpp"
#include "otelMetrics.hpp"

//import Metrics;

using namespace UShopImportMetrics;

namespace
{

volatile std::sig_atomic_t mSignalStatus;

/*
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    validPacketsReceivedCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    futurePacketsReceivedCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    expiredPacketsReceivedCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    totalPacketsReceivedCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    windowedAverageLatencyGauge;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    windowedAverageCountsGauge;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    windowedStdCountsGauge;
*/

std::atomic<bool> mInterrupted{false};

struct OTelHTTPMetricsOptions
{
    std::string url{"localhost:4318"};
    std::chrono::milliseconds exportInterval{5000};
    std::chrono::milliseconds exportTimeOut{500};
    std::string suffix{"/v1/metrics"};
};

//NOLINTBEGIN(misc-include-cleaner)
std::string getOTelCollectorURL(boost::property_tree::ptree &propertyTree,
                                const std::string &section)
//NOLINTEND(misc-include-cleaner)
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
    WaveRingOptions waveRingOptions;
    std::filesystem::path logDirectory{"./"};
    std::string applicationName{"uEarthwormPacketMetrics"};
    std::string otelAttributes;
    std::chrono::seconds windowedMetricsUpdateInterval{120};
    std::chrono::seconds printSummary{std::chrono::minutes {15}};
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

        // Application name
        applicationName
            = propertyTree.get<std::string> ("General.applicationName",
                                             applicationName);
        if (applicationName.empty())
        {
            applicationName = "uEarthwormPacketMetrics";
        }   
        verbosity
            = propertyTree.get<int> ("General.verbosity", verbosity);


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

        // Earthworm
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
        }
        // Make a ring name to use as the otel attribute
        std::transform(applicationName.begin(),
                       applicationName.end(),
                       applicationName.begin(), 
                       ::tolower);
        auto ringNameLower = waveRingOptions.ringName;
        std::transform(ringNameLower.begin(), ringNameLower.end(),
                       ringNameLower.begin(), ::tolower);
        if (!ringNameLower.empty())
        {
            otelAttributes = "ring=" + ringNameLower;
        }
        otelAttributes
            = propertyTree.get<std::string> (
                 "OTelMetricsOptions.resourceAttributes", otelAttributes);
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
        //NOLINTBEGIN(misc-include-cleaner)
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, desc), vm);
        //NOLINTEND(misc-include-cleaner)
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

///--------------------------------------------------------------------------///

class Process
{
public:
    Process(std::unique_ptr<ProgramOptions> &&options,
            std::shared_ptr<spdlog::logger> logger) :
        mOptions(std::move(options)),
        mLogger(std::move(logger))
    {
#ifndef NDEBUG
        assert(mOptions);
        assert(mLogger);
#endif
        SPDLOG_LOGGER_INFO(mLogger, "Creating earthworm ring reader");
        mMaximumQueueSize = mOptions->maximumQueueSize;
        mRingReader
            = std::make_unique<WaveRing>
                 (mOptions->waveRingOptions, 
                  mAddPacketCallbackFunction,
                  mLogger);

        if (mOptions->exportMetrics)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Initializing metrics");
            auto &metrics
                = UShopImportMetrics::MetricsSingleton::getInstance();
            metrics.setUpdateInterval(mOptions->windowedMetricsUpdateInterval);
            // Need a provider from which to get a meter.  This is initialized
            // once and should last the duration of the application.
            auto provider 
                = opentelemetry::metrics::Provider::GetMeterProvider();
    
            // Meter will be bound to application (library, module, class, etc.)
            // so as to identify who is genreating these metrics.
            auto meter = provider->GetMeter(mOptions->applicationName, "1.2.0");

            // Valid (good) packets
            validPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "earthworm.ring_packet_metrics.packets.valid",
                    "Number of valid data packets read from Earthworm ring.",
                    "{packets}");
            validPacketsReceivedCounter->AddCallback(
                ::observeValidPacketsReceived, nullptr);
            // Future packets
            futurePacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                   "earthworm.ring_packet_metrics.packets.future",
                   "Number of future packets read from Earthworm ring.",
                   "{packets}");
            futurePacketsReceivedCounter->AddCallback(
                ::observeFuturePacketsReceived, nullptr);
            // Expired packets
            expiredPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "earthworm.ring_packet_metrics.packets.expired",
                    "Number of expired packets read from Earthworm ring.",
                    "{packets}");
            expiredPacketsReceivedCounter->AddCallback(
                ::observeExpiredPacketsReceived, nullptr);
            // Total packets received
            totalPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "earthworm.ring_packet_metrics.packets.all",
                    "Total number of packets read from Earthworm ring.  This includes future and expired packets.",
                    "{packets}");
            totalPacketsReceivedCounter->AddCallback(
                ::observeTotalPacketsReceived, nullptr);
            // Windowed average latency
            windowedAverageLatencyGauge
                = meter->CreateDoubleObservableGauge(
                    "earthworm.ring_packet_metrics.windowed.latency.average",
                    "The windowed average latency of packets read from Earthworm ring.",
                    "{s}");
            windowedAverageLatencyGauge->AddCallback(
                ::observeWindowedAverageLatency, nullptr);
            // Windowed average counts
            windowedAverageCountsGauge
                = meter->CreateDoubleObservableGauge(
                    "earthworm.ring_packet_metrics.windowed.counts.average",
                    "The windowed average number of counts read from Earthworm ring.",
                    "{counts}");
            windowedAverageCountsGauge->AddCallback(
                ::observeWindowedAverageCounts, nullptr);
            // Windowed std of counts
            windowedStdCountsGauge
                = meter->CreateDoubleObservableGauge(
                    "earthworm.ring_packet_metrics.windowed.counts.standard_deviaton",
                    "The windowed standard deviation of counts read from Earthworm ring.",
                    "{counts}");
            windowedStdCountsGauge->AddCallback(
                ::observeWindowedStdCounts, nullptr);
        }
    }

    ~Process()
    {
        stop();
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
                if (now > lastUpdate + mOptions->windowedMetricsUpdateInterval)
                {
                    metrics.updateAndResetWindowedMetrics();
                }
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
    UShopImportMetrics::initializeMetricsSingleton();
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
        //NOLINTNEXTLINE(misc-include-cleaner)
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
    if (std::getenv("OTEL_SERVICE_NAME") == nullptr)
    {
        SPDLOG_LOGGER_INFO(logger,
                           "Setting OTEL_SERVICE_NAME to {}",
                           options->applicationName); 
        constexpr int overwrite{1};
        setenv("OTEL_SERVICE_NAME",
               options->applicationName.c_str(),
               overwrite);
    }
    if (std::getenv("OTEL_RESOURCE_ATTRIBUTES") == nullptr)
    {
        if (!options->otelAttributes.empty())
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Setting OTEL_RESOURCE_ATTRIBUTES to {}",
                               options->otelAttributes);
            constexpr int overwrite{1};
            setenv("OTEL_RESOURCE_ATTRIBUTES",
                   options->otelAttributes.c_str(),
                   overwrite);
        }
    }
    if (options->exportMetrics)
    {
        ::initializeOTelHTTP(
            options->otelHTTPMetricsOptions.url,
            options->otelHTTPMetricsOptions.exportInterval,
            options->otelHTTPMetricsOptions.exportTimeOut);
    }

    try
    {
        SPDLOG_LOGGER_INFO(logger, "Initializing main process...");
        auto process = std::make_unique<Process> (std::move(options), logger);
        SPDLOG_LOGGER_INFO(logger, "Starting metrics calculator...");
        process->start();
        ::cleanupOTelMetrics();
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Metrics module failed with {}",
                               std::string {e.what()});
        ::cleanupOTelMetrics();
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}


