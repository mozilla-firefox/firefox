# Performance

This page explains how to optimize the performance of the Firefox code base.

The [test documentation](/testing/perfdocs/index.md)
explains how to test for performance in Firefox.
The [profiler documentation](/tools/profiler/index.md)
explains how to use the Gecko profiler.

```{toctree}
:hidden:
:glob:

*
memory/index
build_metrics/build_metrics
platform_microbenchmarks/platform_microbenchmarks
```

## General Performance references

* Tips on generating valid performance metrics by [benchmarking](Benchmarking.md)
* [GPU Performance](GPU_performance.md) Tips for reducing impacts on browser performance in front-end code.
* [Automated Performance testing and Sheriffing](automated_performance_testing_and_sheriffing.md) Information on automated performance testing and sheriffing at Mozilla.
* [Performance best practices for Firefox front-end engineers](bestpractices.md) Tips for reducing impacts on browser performance in front-end code.
* [Reporting a performance problem](reporting_a_performance_problem.md) A user friendly guide to reporting a performance problem. A development environment is not required.
* [Scroll Linked Effects](scroll-linked_effects.md) Information on scroll-linked effects, their effect on performance, related tools, and possible mitigation techniques.

## Memory profiling and leak detection tools

* The [Developer Tools Memory panel](/devtools-user/memory/index.md) supports taking heap snapshots, diffing them, computing dominator trees to surface "heavy retainers", and recording allocation stacks.
* [About:memory](memory/about_colon_memory.md) about:memory is the easiest-to-use tool for measuring memory usage in Mozilla code, and is the best place to start. It also lets you do other memory-related operations like trigger GC and CC, dump GC & CC logs, and dump DMD reports. about:memory is built on top of Firefox's [memory reporting](memory/memory_reporting.md) infrastructure.
* [DMD](memory/dmd.md) is a tool that identifies shortcomings in about:memory's measurements, and can also do multiple kinds of general heap profiling.
* [AWSY](/testing/perfdocs/awsy.md) (are we slim yet?) is a memory usage and regression tracker.
* [Bloatview](memory/bloatview.md) prints per-class statistics on allocations and refcounts, and provides gross numbers on the amount of memory being leaked broken down by class. It is used as part of Mozilla's continuous integration testing.
* [Refcount Tracing and Balancing](memory/refcount_tracing_and_balancing.md) are ways to track down leaks caused by incorrect uses of reference counting. They are slow and not particular easy to use, and thus most suitable for use by expert developers.
* [GC and CC Logs](memory/gc_and_cc_logs.md)
* [LogAlloc](https://searchfox.org/firefox-main/source/memory/replace/logalloc/README) is a tool that dumps a log of memory allocations in Gecko. That log can then be replayed against Firefox's default memory allocator independently or through another replace-malloc library, allowing the testing of other allocators under the exact same workload.
* [See also the documentation on Leak-hunting strategies and tips.](memory/leak_hunting_strategies_and_tips.md)

## Profiling and performance tools

* [JIT Profiling with perf](jit_profiling_with_perf.md) Using perf to collect JIT profiles.
* [JIT Profiling with samply](jit_profiling_with_samply.md) Using samply to collect JIT profiles
* [Profiling with simpleperf](profiling_with_simpleperf.md) `simpleperf` is the Android equivalent of Linux's `perf`
* [Profiling with Instruments](profiling_with_instruments.md) How to use Apple's Instruments tool to profile Mozilla code.
* [Profiling with xperf](profiling_with_xperf.md) How to use Microsoft's Xperf tool to profile Mozilla code.
* [Profiling with Concurrency Visualizer](profiling_with_concurrency_visualizer.md) How to use Visual Studio's Concurrency Visualizer tool to profile Mozilla code.
* [Profiling with Zoom](profiling_with_zoom.md) Zoom is a profiler for Linux done by the people who made Shark.
* {doc}`Adding a new telemetry probe </toolkit/components/telemetry/start/adding-a-new-probe>` Information on how to add a new measurement to the Telemetry performance-reporting system

## Power Profiling

* [An overview of power profiling](power_profiling_overview.md). It includes details about hardware, what can be measured, and recommended approaches. It should be the starting point for anybody new to power profiling.
* **(Mac, Linux)** [tools/power/rapl](tools_power_rapl.md) is a command-line utility in the Mozilla codebase that uses the Intel RAPL interface to gather direct power estimates for the package, cores, GPU and memory.
* **(Mac-only)** [powermetrics](powermetrics.md) is a command-line utility that gathers and displays a wide range of global and per-process measurements, including CPU usage, GPU usage, and various wakeups frequencies.
* **(All-platforms)** [TimerFirings](timerfirings_logging.md) logging is a built-in logging mechanism that prints data on every time fired.
* **(Mac-only)** [Activity Monitor and top](activity_monitor_and_top.md) The battery status menu, Activity Monitor and top are three related Mac tools that have major flaws but often consulted by users, and so are worth understanding.
* **(Windows, Mac and Linux)** [Intel Power Gadget](intel_power_gadget.md) Intel Power Gadget provides real-time graphs for package and processor RAPL estimates. It also provides an API through which those estimates can be obtained.
* **(Linux only)** [perf](perf.md) perf is a powerful command-line utility that can measure many different things, including energy estimates and high-context measurements of things such as wakeups.
* **(Linux-only)** [turbostat](turbostat.md) is a command-line utility that gathers and displays various power-related measurements, with a focus on per-CPU measurements such as frequencies and C-states.
* **(Linux-only)** [powertop](https://01.org/powertop) is an interactive command-line utility that gathers and displays various power-related measurements.

## Performance Metrics

* [PerfStats](perfstats.md) - A framework for low-overhead collection of internal performance metrics.
