# AWSY

Are We Slim Yet project (commonly known as AWSY) tracks memory usage across builds.
It uses the same infrastructure as [about:memory](/performance/memory/about_colon_memory.md)
to measure memory usage, either with the recorded tp6 pageset or with blank tabs
(base memory usage). AWSY runs in TaskCluster on all branches and platforms, and its results
are posted to [Perfherder](https://treeherder.mozilla.org/perfherder/) where regressions are
tracked automatically.

As new processes are added to Firefox we want to make sure their memory usage is also tracked
by AWSY. To this end we request that memory reporting be integrated into any new process
before it is enabled on Nightly.

On treeherder, the AWSY builds are listed in subgroups of `SY`.

AWSY tests consist of two types: TP6 and Base Memory Usage.

{documentation}

## Running AWSY Locally

Running tests locally is most likely only useful for debugging what is going on in a test,
as the test output is only reported as raw JSON. The CLI is documented via:

```bash
./mach awsy-test --help
```

### TP6 tests

```bash
./mach awsy-test
```

### Base Memory Usage tests

```bash
./mach awsy-test --base
```

## Running AWSY on Try

AWSY runs can be generated through the mach try fuzzy finder:

```bash
./mach try fuzzy
```

A query for "awsy" will return all AWSY tests. TP6 and Base test names will contain `tp6` and `base`, respectively.

The following documents all tests we currently run for AWSY.
*The following content was migrated from* <https://wiki.mozilla.org/AWSY/Tests> *and will be updated to clarify TP6 tests vs Base tests:* <https://bugzilla.mozilla.org/show_bug.cgi?id=1714600>

## TP6 Tests

Running `./mach awsy-test` by default will run TP6 tests.

### Explicit Memory

- This is memory explicitly reported by a memory reporter. It includes all the memory allocated via explicit calls to heap allocation functions (such as malloc and new), and some (only that covered by memory reporters) of the memory allocated via explicit calls to non-heap allocation functions (such as mmap and VirtualAlloc).

**Possible regression causes**

- A regression in this usually means a new feature is using or retaining more memory and should be looked at. These are easier to diagnose because we can compare memory reports.

See the [about:memory documentation](/performance/memory/about_colon_memory.md#explicit-allocations) for more details.

### Heap Unclassified

*to do: add test definition*

### Images

- This is a subset of the "explicit" measurement that focuses on memory used to render images.

**Possible regression causes**

- A regression in this can indicate leaks or poor memory usage in the image subsystem. In the past this was a persistent problem.

### JS

*to do: add test definition*

### Resident Memory

- This is a higher level measurement provided by the operating system. We sum the "resident" memory ([RSS](https://en.wikipedia.org/wiki/Resident_set_size)) with the [resident-unique](https://en.wikipedia.org/wiki/Unique_set_size) memory of the content processes. It's pretty noisy and large so it's not very useful in detecting smaller regressions.

**Possible regression causes**

- Regressions in this often track regressions in explicit and heap unclassified. If we see a regression in resident, but not in other reports this can indicate we are leaking untracked memory (perhaps through shared memory, graphics allocations, file handles, etc).

## Base Content Tests

- An updated test focused on supporting Fission. This measures the base overhead of an empty content process. It tracks resident unique, heap unclassified, JS, and explicit memory metrics as well as storing full memory reports as artifacts. The median value for each metric is used from across all content processes. It has much lower thresholds for alerting and is recorded in [Perfherder](https://wiki.mozilla.org/EngineeringProductivity/Projects/Perfherder).

### Base Content Explicit

**Possible regression causes**

A change has caused more JavaScript to load at startup or into blank pages.

- **Common solution**: Lazily load any new modules you rely on.
- **Common solution**: Split your code out to only load what is minimally needed initially.

You modified the JS engine and it's using more memory.

- **Common solution**: Attempt to reduce your object size for the common case, these tend to add up!

You implemented a new feature in JavaScript.

- **Common solution**: Write the majority (or all of it) in compiled code (C++/Rust). This will reduce overhead and generally improve performance.

### Base Content Heap Unclassified

- The "heap-unclassified" value represents heap-allocated memory that is not measured by any memory reporter. This is typically 10-20% of "explicit".

**Possible regression causes**

- A regression in this can indicate that we're leaking memory or that additional memory reporters should be added.
- An improvement can indicate that leaks have been fixed or that we added new memory reporters.

See the [about:memory documentation](/performance/memory/about_colon_memory.md#explicit-allocations) for more details.

### Base Content JS

- This is the "js-main-runtime/" value in `about:memory` which is all the memory attributed to the javascript engine.

**Possible regression causes**

- A regression in this number can indicate leaks in the JS engine, optimizations that take performance into consideration at the expense of more memory, or problems with the garbage collector.

### Base Content Resident Unique Memory

- This is the "resident-unique" value in `about:memory`, also known as the unique set size ([USS](https://en.wikipedia.org/wiki/Unique_set_size)). It is the memory mapped by a content process that is present in physical memory and not shared with any other process, which is roughly the amount of RAM we'd expect to be freed if that process was closed. Read-only data such as shared libraries is shared between processes, so it is not counted here.
- Since we can have dozens of content processes with Fission, this measures how much a single new content process adds to the total memory used by the system. The median value across all content processes is reported.

**Possible regression causes**

- A regression in this usually means more memory is allocated or touched in every content process, for example by loading more code or data at content process startup. It often tracks regressions in Base Content Explicit, Base Content Heap Unclassified, or Base Content JS. A regression here that isn't visible in those can indicate memory that isn't covered by memory reporters, such as graphics allocations or memory that used to be shared between processes.
