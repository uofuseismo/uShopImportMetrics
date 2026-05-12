#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <functional>
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
#include "uShopImportMetrics/version.hpp"
#include "uShopImportMetrics/seedLinkClient.hpp"
#include "uShopImportMetrics/seedLinkClientOptions.hpp"
#include "uShopImportMetrics/streamSelector.hpp"
#include "uShopImportMetrics/packet.hpp"

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
        verbosity
            = propertyTree.get<int> ("General.verbosity", verbosity);

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
                     "OTelTTPMetricsOptions.windowedMetricsUpdateIntervalInSeconds",
                     updateInterval);
            if (updateInterval <= 0)
            {
                throw std::invalid_argument("Metrics update interval must be non-negative");
            }
            windowedMetricsUpdateInterval
                 = std::chrono::seconds {updateInterval};
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
attempts to compute metrics such as latency, average counts, etc. per stream.

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
            std::cout << Version::getVersion() << "\n";
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
    std::mutex mImportMutex;
    std::mutex mStopMutex;
    std::queue<Packet> mPacketQueue;
    std::unique_ptr<SEEDLinkClient> mSEEDLinkClient{nullptr};
    size_t mMaximumQueueSize{4096};
    bool mStopRequested{false};
    bool mIsRunning{false};
};

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

    std::unique_ptr<Process> process{nullptr}; 
    try
    {
        process = std::make_unique<Process> (options, logger);
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to initialized process because {}",
                               std::string {e.what()});
    }

    

    return EXIT_SUCCESS;
}
