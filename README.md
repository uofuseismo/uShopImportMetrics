# About

The following modules generate stream-based metrics to monitor telemetered data packets in the UUSS import.  

## uEarthwormPacketMetrics

This module attaches to an Earthworm ring and generates the following metrics

  | Metric | Meaning | Per Channel |
  |---|---|---|
  |earthworm.ring_packet_metrics.client.packets.valid | Number of packets that are sensibly timed and contain data. | x |
  |earthworm.ring_packet_metrics.client.packets.future | Number of packets that contain samples whose time exceed now.  This indicates a timing error. | x |
  |earthworm.ring_packet_metrics.packets.expired | Number of packets that contain samples older than 2 months.  This indicates a timing error. | x |
  |earthworm.ring_packet_metrics.packets.all | Total number of packets (valid + future + expired + incorrectly formatted). | x |
  |earthworm.ring_packet_metrics.windowed.latency.average | The average latency (now - last sample) of packets measured over a 2 minute window. |x|
  |earthworm.ring_packet_metrics.windowed.counts.average | The average counts of the samples in each packet measured over a 2 minute window. |x|
  |earthworm.ring_packet_metrics.windowed.counts.standard_deviaton | The standard deviation of the samples in each packet measured over a 2 minute window. |x|

Metrics are exported to the [OpenTelemetryCollector](https://opentelemetry.io/docs/collector/) running on the same node.  

Usage requires an initialization file, say metrics.ini, of the form

    [General]
    # This should be unique and identifiable as it will also be used in the OTEL_SERVICE_NAME
    applicationName=appName
    # The verbosity (1) Critical only, (2) Errors + Critical, (3) Info + Errors + Critical, (4) Debug + Info + Errors + Critical
    verbosity=3
    # The program will create a daily log file in the following directory.  If this is not set the the program will log to stdout.
    logDirectory=/path/to/log/files

    [Earthworm]
    # The earthworm module name set in Earthworm's global config
    moduleName=MOD_EARTHWORM_METRICS
    # The earthworm ring from which to scrape TraceBuf2 packets
    ringName=WAVE_RING
    

    [OTelTTPMetricsOptions]
    # The name of the host running the OTel collector
    url=localhost
    # The suffix to append to the host name.  Changing this is not recommended.
    #suffix=/v1/metrics
    # Nominally, the windowed metrics are computed every 2 minutes but this can be adjusted.
    windowedMetricsUpdateIntervalInSeconds=120
    # The default values for the OTEL_RESOURCE_ATTRIBUTES will be lifted from this. 
    #resourceAttributes=

The program would then be run from the command line or through the Earthworm stagmgr with something like

    uEarthwormPacketMetrics --ini=metrics.ini
    
