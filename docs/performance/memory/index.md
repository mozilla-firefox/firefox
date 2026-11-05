# Memory profiling and leak detection

Tools for measuring how much memory Firefox uses, and for tracking down memory
that is never released.

```{toctree}
:caption: Measuring memory usage
:maxdepth: 1

about_colon_memory
memory_reporting
/testing/perfdocs/awsy
bloatview
```

```{toctree}
:caption: Leak detection
:maxdepth: 1

leak_hunting_strategies_and_tips
leaked_until_shutdown
refcount_tracing_and_balancing
gc_and_cc_logs
```

```{toctree}
:caption: Heap profiling with DMD
:maxdepth: 1

dmd
heap_scan_mode
```

## The DevTools Memory panel

The Memory panel is documented in the DevTools user documentation, under
[Memory](/devtools-user/memory/index.md).
