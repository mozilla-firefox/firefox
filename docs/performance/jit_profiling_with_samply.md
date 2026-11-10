# JIT profiling with `samply`

TL;DR: On Linux and macOS, if you have a local Firefox or Spidermonkey shell
build with default options, then you can use
[`samply`](https://github.com/mstange/samply) to get profiles like [this shell
profile](https://share.firefox.dev/3xMCwI7) or [this browser
profile](https://share.firefox.dev/4aRb4Hw) (but with working source and
assembly views) by running the following commands:

- Firefox:

```
samply record PERF_SPEW_DIR=/tmp IONPERF=src MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_USE_PERFORMANCE_MARKER_FILE=1 JIT_OPTION_enableICFramePointers=true JIT_OPTION_onlyInlineSelfHosted=true JIT_OPTION_emitInterpreterEntryTrampoline=true python3 ./mach run
```

- JS shell:

```
samply record PERF_SPEW_DIR=/tmp IONPERF=src ~/code/obj-shell/dist/bin/js --enable-ic-frame-pointers --only-inline-selfhosted index.js
```

## Motivation

For JIT profiling, the Gecko profiler currently has some shortcomings:

- It's not available in the JS shell.
- It doesn't label all JIT frames correctly, such as IC frames or trampolines.
- It doesn't allow accessing the assembly code of jitted functions.
- It doesn't allow accessing the JavaScript source code or IR code.

[`samply`](https://github.com/mstange/samply) is an alternative to the Gecko
profiler which can address these shortcomings, at least on Linux and macOS,
until these features are available in the Gecko profiler. It does this by
leveraging Spidermonkey's support for Jitdump, and it presents profiles in the
familiar Firefox Profiler UI.

Additionally, on Linux, the Linux tool `perf` can be used to get even more
control. For example, `perf` can sample performance counters such as cache
misses, instructions executed, or page faults. It can also unwind with frame
pointers whereas samply currently always uses DWARF unwinding.

To record and view a profile, you have three options:

1. `samply record`: easiest to use, Firefox Profiler UI
2. `perf record` + `samply import perf.data`: more control + Firefox Profiler
   UI
3. `perf record` + `perf report` / `perf annotate`: if you're already familiar
   with perf's UI and don't mind it

## Setup

To install samply, follow [the installation instructions in `samply`'s README
file](https://github.com/mstange/samply?tab=readme-ov-file#samply).

To install `perf` (optional), install `linux-perf-tools` with your distribution's
package manager.

## Environment Variables and flags

Environment variables that must be defined for perf JIT profiling:

- `PERF_SPEW_DIR`: Location of jitdump output files. Making this directory a tmpfs
filesystem could help reduce overhead.
- `IONPERF`: Valid options include: `func`, `src`, `ir`, `ir-ops`.
  - `IONPERF=func` will disable all annotation and only function names will be
  available. It is the fastest option.
  - `IONPERF=ir` will enable IR annotation.
  - `IONPERF=ir-ops` will enable IR annotation with operand support. **Requires
  --enable-jitspew** and adds additional overhead to "ir".
  - `IONPERF=src` will enable source code annotation. With samply, this works
  well in most cases. When using `perf annotate`, it only works if perf can read
  the source file locally, so it only really works well in the JS shell.

The following environment variables and flags are useful, too:

`JIT_OPTION_onlyInlineSelfHosted=true` and `--only-inline-selfhosted` (for the
browser and the shell, respectively) make it so that all function calls show up
in the profile, by disabling inlining. In the future, we hope to have inlining
information in Jitdump so that we can see inlined JS function without disabling
inlining.

`MOZ_DISABLE_CONTENT_SANDBOX=1` is needed when profiling the browser, so that
content processes can write the Jitdump file to the disk.

`MOZ_USE_PERFORMANCE_MARKER_FILE=1` can be used to get PerformanceUserTiming
markers into the profile, which is useful when profiling Speedometer.

## Usage

`samply record` launches a process, records it, and waits for it to finish. Once
the process has shut down, it opens the profile.

So, with the browser, you use it as follows:

 1. `samply record [env-vars] python3 ./mach run [flags]`
 2. Run the workload you want to profile in your Firefox build.
 3. Shut down Firefox.
 4. Wait for the profile to appear.
 5. Interact with the profiler.
 6. When done, press Ctrl+C on the terminal to stop the samply server.

And with the JS shell:

 1. `samply record [env-vars] obj/dist/bin/js [flags] index.js`
 2. Wait for the shell to finish running index.js
 3. Wait for the profile to appear.
 4. Interact with the profiler.
 5. When done, press Ctrl+C on the terminal to stop the samply server.

## Hints

- On macOS, `samply` cannot record shell scripts or system applications because

  of signing restrictions. So `samply record ./mach run` will not work. But if
  `python3` is installed from homebrew, then `samply record python3` works. This is
  why I've been writing `samply record python3 ./mach run` rather than `samply
  record ./mach run` above.

- Get more samples: In the source view and especially in the assembly view,
  you'll often want more samples than what you get from a single run.
  - In the browser, try running your workload multiple times.
  - In the shell, run your workload multiple times by using `samply record
    --iteration-count 10 --reuse-threads`.
