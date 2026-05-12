#ifndef USHOP_IMPORT_METRICS_METRICS_SINGLETON_HPP
#define USHOP_IMPORT_METRICS_METRICS_SINGLETON_HPP
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <memory>
#define UPDATE_INTERVAL_SECONDS 120
#define MAXIMUM_LATENCY_DAYS    180

namespace UShopImportMetrics
{
 class Packet;
 class StreamIdentifier;
 struct WindowedMetrics;
}
namespace UShopImportMetrics
{
class MetricsSingleton
{
public:
    /// @result An instance of the metrics singleton.
    [[maybe_unused]] static MetricsSingleton &getInstance();
    /// @brief Sets the update interval for the time window-based metrics.
    void setUpdateInterval(const std::chrono::seconds &interval);
    /// @brief Updates the metrics for this packet.
    void tabulateMetrics(const UShopImportMetrics::Packet &packet);
    /// @brief Updates the time window-based metrics and resets for
    ///        the next window.
    void updateAndResetWindowedMetrics();
    /// @result Average latency for each stream.
    [[nodiscard]] std::map<std::string, double> getWindowedAverageLatencies() const;
    /// @result Average counts for each stream.
    [[nodiscard]] std::map<std::string, double> getWindowedAverageCounts() const;
    /// @result Standard deviation of counts for each stream.
    [[nodiscard]]  std::map<std::string, double> getWindowedStdCounts() const;
    /// @brief Increments the number of received packets for the stream.
    void incrementReceivedPacketsCounter(const std::string &key);
    /// @result The number of received packets counters for each stream. 
    [[nodiscard]] std::map<std::string, int64_t> getReceivedPacketsCounters() const;

    /// @brief Increments the number of future packets for the stream.
    void incrementFuturePacketsCounter(const std::string &key);
    /// @result The number of future packets for each stream.    
    [[nodiscard]] std::map<std::string, int64_t> getFuturePacketsCounters() const;

    /// @brief Increments the expired counter for the stream.
    void incrementExpiredPacketsCounter(const std::string &key);
    /// @result The number of expired packets for each stream.
    [[nodiscard]] std::map<std::string, int64_t> getExpiredPacketsCounters() const;

    /// @brief Increments the total number of packets for the stream (good and bad.)
    void incrementTotalPacketsCounter(const std::string &key);
    /// @result The total number of packets for each stream.
    [[nodiscard]] std::map<std::string, int64_t> getTotalPacketsCounters() const;

    /// @brief Increments the total number of received packets.
    void incrementReceivedPacketsCounter();
    /// @result The total number of received packets.
    [[nodiscard]] int64_t getReceivedPacketsCount() const noexcept;

private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
    mutable std::mutex mMutex;
    std::map<std::string, int64_t> mReceivedPacketsCounterMap;
    std::map<std::string, int64_t> mExpiredPacketsCounterMap;
    std::map<std::string, int64_t> mFuturePacketsCounterMap;
    std::map<std::string, int64_t> mTotalPacketsCounterMap;
    std::map<std::string, double> mAverageLatencyMap;
    std::map<std::string, double> mAverageCountsMap;
    std::map<std::string, double> mStdCountsMap;
    std::map<std::string, std::unique_ptr<WindowedMetrics>> mWindowedMetricsMap;
    std::atomic<int64_t> mReceivedPacketsCounter{0};
    std::chrono::seconds mUpdateInterval{UPDATE_INTERVAL_SECONDS};
    std::chrono::microseconds mMaximumLatency{std::chrono::days {MAXIMUM_LATENCY_DAYS}};
    std::chrono::microseconds mMaximumFutureTime{0};
};
/// @brief Call this as the beginning of the main application to initialize the metrics singleton.
void initializeMetricsSingleton();
/// @brief Convenience function to convert a stream identifier to a key name.
[[nodiscard]] 
std::string toKeyName(const UShopImportMetrics::StreamIdentifier &identifier);
/// @brief Convenience function to extract stream identifier from a packet
///        and convert it to a key name.
[[nodiscard]]
std::string toKeyName(const UShopImportMetrics::Packet &packet);

}
#endif
