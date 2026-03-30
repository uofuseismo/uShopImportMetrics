#ifndef USHOP_IMPORT_METRICS_SEED_LINK_CLIENT_HPP
#define USHOP_IMPORT_METRICS_SEED_LINK_CLIENT_HPP
#include <memory>
#include <functional>
#include <filesystem>
#include <future>
#include <spdlog/spdlog.h>
namespace UShopImportMetrics
{
 class Packet;
 class StreamSelector;
 class SEEDLinkClientOptions;
}
namespace UShopImportMetrics
{
/// @class SEEDLinkClient
/// @brief The SEEDLink client is a long-running thread that scrapes data
///        from the SEEDLink server and propagates those packets to the
///        next phase of processing.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class SEEDLinkClient
{
public:
    /// @brief Constructor.
    /// @param[in] getPacketCallback  The callback function that allows for the
    ///                               propagation of packets to the next phase
    ///                               of processing.
    /// @param[in] options  Options that influence the behavior of the SEEDLink
    ///                     client.
    SEEDLinkClient(const SEEDLinkClientOptions &options,
                   const std::function<void (Packet &&)> &getPacketCallback,
                   std::shared_ptr<spdlog::logger> logger);
    
    /// @result True indicates the client is initialized.
    [[nodiscard]] bool isInitialized() const noexcept;
    /// @brief Starts the acquisition.
    [[nodiscard]] std::future<void> start();
    /// @brief Stops the acquisition.
    void stop();
    
    /// @brief Destructor.
    ~SEEDLinkClient();

    SEEDLinkClient() = delete;
    SEEDLinkClient(const SEEDLinkClient &) = delete;
    SEEDLinkClient(SEEDLinkClient &&) noexcept = delete;
    SEEDLinkClient& operator=(const SEEDLinkClient &) = delete;
    SEEDLinkClient& operator=(SEEDLinkClient &&) noexcept = delete;
private:
    class SEEDLinkClientImpl;
    std::unique_ptr<SEEDLinkClientImpl> pImpl;
};
}
#endif
