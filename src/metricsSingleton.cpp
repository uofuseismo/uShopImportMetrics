#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#ifndef NDEBUG
#include <cassert>
#endif
#include "uShopImportMetrics/metricsSingleton.hpp"
#include "uShopImportMetrics/packet.hpp"
#include "uShopImportMetrics/streamIdentifier.hpp"

using namespace UShopImportMetrics;

std::string UShopImportMetrics::toKeyName(
     const UShopImportMetrics::StreamIdentifier &identifier)
{
     auto network = identifier.getNetwork();
     auto station = identifier.getStation();
     auto channel = identifier.getChannel();
     auto locationCode = identifier.getLocationCode();

     auto result = network + "_"
                 + station + "_"
                 + channel;
     if (!locationCode.empty()){result = result + "_" + locationCode;}
     std::transform(result.begin(), result.end(), result.begin(), ::tolower);
     return result;
}

std::string UShopImportMetrics::toKeyName(
     const UShopImportMetrics::Packet &packet)
{
     return toKeyName(packet.getStreamIdentifier());
}

void UShopImportMetrics::initializeMetricsSingleton()
{
    MetricsSingleton::getInstance();
}

///--------------------------------------------------------------------------///
///                             WindowedMetrics                              ///
///--------------------------------------------------------------------------///


struct UShopImportMetrics::WindowedMetrics
{
    WindowedMetrics() = default;
    explicit WindowedMetrics(const std::chrono::seconds &inputUpdateInterval) :
        updateInterval(inputUpdateInterval)
    {
#ifndef NDEBUG
        assert(updateInterval.count() > 0);
#endif
        windowedAverageLatency.store(
            static_cast<double> (updateInterval.count()));
    }

    void setUpdateInterval(const std::chrono::seconds &inputUpdateInterval)
    {
        if (inputUpdateInterval.count() < 1)
        {
            throw std::runtime_error("Invalid update interval");
        }
        updateInterval = inputUpdateInterval;
        windowedAverageLatency.store(
            static_cast<double> (updateInterval.count()));
    }

    void update(const UShopImportMetrics::Packet &packet,
                const std::chrono::microseconds &packetLatency)
    {
        auto nSamples = packet.getNumberOfSamples();
        if (nSamples < 1){return;}
        // Get data
        auto [packetSum, packetSum2]
           = UShopImportMetrics::computeSumAndSumSquared(packet);
        // Update sums
        {
        const std::scoped_lock lock{mMutex};
        sum = sum + packetSum;
        sumSquared = sumSquared + packetSum2;
        samplesCount = samplesCount + nSamples;
        packetsCount = packetsCount + 1;
        sumLatency = sumLatency + packetLatency;
        }
    }

    [[nodiscard]] bool updateAndReset(const std::chrono::microseconds &now)
    {
        bool wasUpdated{false};
        if (now >= lastUpdate + updateInterval)
        {
            lastUpdate = now;
            double averageLatency
               = static_cast<double> (updateInterval.count());
            double averageCounts{0};
            double stdCounts{0};
            {
            const std::scoped_lock lock{mMutex};
            if (samplesCount > 0)
            {
                double besselCorrection{1};
                if (samplesCount > 1)
                {
                    besselCorrection
                        = static_cast<double> (samplesCount)
                         /static_cast<double> (samplesCount - 1);
                }
                averageLatency
                     = (static_cast<double> (sumLatency.count())*1.e-6)
                      /static_cast<double> (packetsCount);
                averageCounts = sum/static_cast<double> (samplesCount);
                // Var[x] = E[x^2] - E[x]^2
                const double varianceOfCounts
                     = sumSquared/static_cast<double> (samplesCount)
                     - averageCounts*averageCounts;
                stdCounts = besselCorrection
                           *std::sqrt(std::max(0.0, varianceOfCounts));
            }
            // Reset sums
            sumLatency = std::chrono::microseconds{0};
            sum = 0;
            sumSquared = 0;
            samplesCount = 0;
            packetsCount = 0;
            }
            // Update 
            windowedAverageLatency.store(averageLatency);
            windowedAverageCounts.store(averageCounts);
            windowedStdCounts.store(stdCounts);
            // Note this was updated
            wasUpdated = true;
        }
        return wasUpdated;
    }

    double getWindowedAverageLatency() const
    {
        return windowedAverageLatency.load();
    }

    double getWindowedAverageCounts() const
    {
        return windowedAverageCounts.load();
    }

    double getWindowedStdCounts() const
    {
        return windowedStdCounts.load();
    }

    mutable std::mutex mMutex;
    std::chrono::seconds updateInterval{UPDATE_INTERVAL_SECONDS};
    std::chrono::microseconds lastUpdate
    {
        std::chrono::duration_cast<std::chrono::microseconds>
        ((std::chrono::high_resolution_clock::now()).time_since_epoch())
    };
    std::chrono::microseconds sumLatency{0};
    std::atomic<double> windowedAverageLatency
    {
        static_cast<double> (updateInterval.count())
    };
    std::atomic<double> windowedAverageCounts{0};
    std::atomic<double> windowedStdCounts{0};
    double sum{0};
    double sumSquared{0};
    int64_t samplesCount{0};
    int64_t packetsCount{0};
};


///--------------------------------------------------------------------------///
///                                Singleton                                 ///
///--------------------------------------------------------------------------///

MetricsSingleton &MetricsSingleton::getInstance()
{
    std::mutex mutex;
    const std::scoped_lock lock{mutex};
    static MetricsSingleton instance;
    return instance;
}

void MetricsSingleton::setUpdateInterval(const std::chrono::seconds &interval)
{
    if (interval.count() <= 0)
    {
        throw std::invalid_argument("Update interval must be positive");
    }
    mUpdateInterval = interval;
}

void MetricsSingleton::tabulateMetrics(const UShopImportMetrics::Packet &packet)
{
    const auto key = toKeyName(packet); // Throws
    // If it made it this far then we update the total packets received
    incrementTotalPacketsCounter(key);
    // Okay check the times
    const int nSamples = packet.getNumberOfSamples();
    if (nSamples <= 0)
    {
        throw std::invalid_argument("Empty packet for " + key);
    }

    // I really don't need an absurd amount of resolution and would
    // rather be resistant to overflow so microseconds are fine.
    const auto startTimeMicroSeconds = packet.getStartTime();
    const auto endTimeMicroSeconds = packet.getEndTime(); // Throws

    const auto now
        = std::chrono::duration_cast<std::chrono::microseconds>
          ((std::chrono::high_resolution_clock::now()).time_since_epoch());
    auto validStartTimeMuS = now - mMaximumLatency;
    auto validEndTimeMuS = now + mMaximumFutureTime;
    // Future
    if (endTimeMicroSeconds > validEndTimeMuS)
    {
        incrementFuturePacketsCounter(key);
        return;
    }
    // Historical
    else if (startTimeMicroSeconds < validStartTimeMuS)
    {
        incrementExpiredPacketsCounter(key);
        return;
    }
    // This is a typical good packet, tabulate metrics
    const auto latency
        = std::max(std::chrono::microseconds {0},
                   now - std::chrono::microseconds{endTimeMicroSeconds} );
    incrementReceivedPacketsCounter(key);
    {
    const std::lock_guard<std::mutex> lock(mMutex);
    auto idx = mWindowedMetricsMap.find(key);
    if (idx == mWindowedMetricsMap.end())
    {
        auto metrics = std::make_unique<WindowedMetrics> (mUpdateInterval);
        metrics->update(packet, latency);
        mWindowedMetricsMap.insert( std::pair{key, std::move(metrics)} );
    }
    else
    {
        idx->second->update(packet, latency);
    }
    }
}


/// Store windowed metrics and reset for next window
void MetricsSingleton::updateAndResetWindowedMetrics()
{
    const auto now
        = std::chrono::duration_cast<std::chrono::microseconds>
          ((std::chrono::high_resolution_clock::now()).time_since_epoch());
    const std::lock_guard<std::mutex> lock(mMutex);
    for (auto &item : mWindowedMetricsMap)
    {
        auto updated = item.second->updateAndReset(now);
        if (updated)
        {
            auto averageLatency = item.second->getWindowedAverageLatency();
            auto averageCounts = item.second->getWindowedAverageCounts();
            auto stdCounts = item.second->getWindowedStdCounts();
            // Take advantage of our mutex
            mAverageLatencyMap.insert_or_assign(item.first, averageLatency);
            mAverageCountsMap.insert_or_assign(item.first, averageCounts);
            mStdCountsMap.insert_or_assign(item.first, stdCounts);
        }
    }
}

/// Average latency
std::map<std::string, double> 
MetricsSingleton::getWindowedAverageLatencies() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mAverageLatencyMap;
}

/// Average counts 
std::map<std::string, double> 
MetricsSingleton::getWindowedAverageCounts() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mAverageCountsMap;
}

/// Std counts
std::map<std::string, double> 
MetricsSingleton::getWindowedStdCounts() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mStdCountsMap;
}

/// Received packets
void MetricsSingleton::incrementReceivedPacketsCounter(const std::string &key)
{
    const std::lock_guard<std::mutex> lock(mMutex);
    auto idx = mReceivedPacketsCounterMap.find(key);
    if (idx == mReceivedPacketsCounterMap.end())
    {
        mReceivedPacketsCounterMap.insert( std::pair {key, 1} );
    }
    else
    {
        idx->second = idx->second + 1;
    }
}

std::map<std::string, int64_t> 
MetricsSingleton::getReceivedPacketsCounters() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mReceivedPacketsCounterMap;
}

/// Future counter
void MetricsSingleton::incrementFuturePacketsCounter(const std::string &key)
{
    const std::lock_guard<std::mutex> lock(mMutex);
    auto idx = mFuturePacketsCounterMap.find(key);
    if (idx == mFuturePacketsCounterMap.end())
    {
        mFuturePacketsCounterMap.insert( std::pair {key, 1} );
    }
    else
    {
        idx->second = idx->second + 1;
    }
}

std::map<std::string, int64_t> 
MetricsSingleton::getFuturePacketsCounters() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mFuturePacketsCounterMap;
}

/// Expired counter
void MetricsSingleton::incrementExpiredPacketsCounter(const std::string &key)
{
    const std::lock_guard<std::mutex> lock(mMutex);
    auto idx = mExpiredPacketsCounterMap.find(key);
    if (idx == mExpiredPacketsCounterMap.end())
    {
        mExpiredPacketsCounterMap.insert( std::pair {key, 1} );
    }
    else
    {
        idx->second = idx->second + 1;
    }
}

std::map<std::string, int64_t> 
MetricsSingleton::getExpiredPacketsCounters() const
{
   const std::lock_guard<std::mutex> lock(mMutex);
   return mExpiredPacketsCounterMap;
}

/// Total packets counter
void MetricsSingleton::incrementTotalPacketsCounter(const std::string &key)
{
    const std::lock_guard<std::mutex> lock(mMutex);
    auto idx = mTotalPacketsCounterMap.find(key);
    if (idx == mTotalPacketsCounterMap.end())
    {
        mTotalPacketsCounterMap.insert( std::pair {key, 1} );
    }
    else
    {
        idx->second = idx->second + 1;
    }
}

std::map<std::string, int64_t> MetricsSingleton::getTotalPacketsCounters() const
{
    const std::lock_guard<std::mutex> lock(mMutex);
    return mTotalPacketsCounterMap;
}

/// Received packets
void MetricsSingleton::incrementReceivedPacketsCounter()
{
    mReceivedPacketsCounter.fetch_add(1);
}

int64_t MetricsSingleton::getReceivedPacketsCount() const noexcept
{
    return mReceivedPacketsCounter.load();
}

