#ifndef OTEL_METRICS_HPP
#define OTEL_METRICS_HPP
#include <iostream>
#include <chrono>
#include <string>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#ifdef WITH_OTLP_GRPC
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h>
#endif
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_context.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include "uShopImportMetrics/metricsSingleton.hpp"

namespace
{

bool metricsInitialized{false};

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

void initializeOTelHTTP(
    const std::string &exporterURL,
    const std::chrono::milliseconds &exportInterval = std::chrono::seconds {5},
    const std::chrono::milliseconds &exportTimeout = std::chrono::milliseconds {500})
{
    if (exporterURL.empty()){return;}
    namespace otel = opentelemetry;
    otel::exporter::otlp::OtlpHttpMetricExporterOptions exporterOptions;
    exporterOptions.url = exporterURL;
    //exporterOptions.console_debug = debug != "" && debug != "0" && debug != "no";
    exporterOptions.content_type
        = otel::exporter::otlp::HttpRequestContentType::kBinary;

    auto exporter
        = otel::exporter::otlp::OtlpHttpMetricExporterFactory::Create(
             exporterOptions);

    // Initialize and set the global MeterProvider
    otel::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis = exportInterval;
    readerOptions.export_timeout_millis = exportTimeout;
    auto reader
        = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = otel::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = otel::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));
    std::shared_ptr<otel::metrics::MeterProvider>
        provider(std::move(metricsProvider));

    otel::sdk::metrics::Provider::SetMeterProvider(provider);
    metricsInitialized = true;
}

void cleanupOTelMetrics()
{
    if (metricsInitialized)
    {
        std::shared_ptr<opentelemetry::metrics::MeterProvider> none;
        opentelemetry::sdk::metrics::Provider::SetMeterProvider(none);
        metricsInitialized = false;
    }
}

void observeValidPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            const auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getReceivedPacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }   
}

void observeFuturePacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            const auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getFuturePacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }   
}

void observeExpiredPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            const auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getExpiredPacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }
}

void observeTotalPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            const auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getTotalPacketsCounters();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

void observeWindowedAverageLatency(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            const auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getWindowedAverageLatencies();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

void observeWindowedAverageCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {   
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getWindowedAverageCounts();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

void observeWindowedStdCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            auto &instance
                = UShopImportMetrics::MetricsSingleton::getInstance();
            auto map = instance.getWindowedStdCounts();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

}
#endif
