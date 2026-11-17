# PerfStats

PerfStats is a framework for the low-overhead selective collection of internal performance metrics.
The results are accessible through ChromeUtils, Browsertime output, and in select performance tests.

## Adding a new PerfStat

Define the new PerfStat by adding it to [this list](https://searchfox.org/mozilla-central/rev/b1e5f2c7c96be36974262551978d54f457db2cae/tools/performance/PerfStats.h#34-53) in [`PerfStats.h`](https://searchfox.org/mozilla-central/rev/52da19becaa3805e7f64088e91e9dade7dec43c8/tools/performance/PerfStats.h).
Then, in C++ code, wrap execution in an RAII object, e.g.

```
PerfStats::AutoMetricRecording<PerfStats::Metric::MyMetric>()
```

or call the following function manually:

```
PerfStats::RecordMeasurement(PerfStats::Metric::MyMetric, Start, End)
```

For incrementing counters, use the following:

```
PerfStats::RecordMeasurementCount(PerfStats::Metric::MyMetric, incrementCount)
```

[Here's an example of a patch where a new PerfStat was added and used.](https://github.com/mozilla-firefox/firefox/commit/1febdb96a6646021262d1f249f9afb1887bb7ff1)

## Enabling collection

To enable collection, use `ChromeUtils.setPerfStatsFeatures(array<string> metrics)`, where passing an empty array disables all metrics. Pass an array of metric names corresponding to the `Metric` enum values, e.g. `["LayerBuilding", "DisplayListBuilding"]`. To enable all metrics, you may use `ChromeUtils.enableAllPerfStatsFeatures()`.

## Accessing results

Results can be accessed with `ChromeUtils.CollectPerfStats()`.
The Browsertime test framework will sum results across processes and report them in its output.
The raptor-browsertime Windows essential pageload tests also collect all PerfStats.
