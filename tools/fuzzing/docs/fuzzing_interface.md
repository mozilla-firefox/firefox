# Fuzzing Interface

The fuzzing interface is glue code living in the Firefox repository in order to
make it easier for developers and security researchers to test C/C++
code with either [libFuzzer](https://llvm.org/docs/LibFuzzer.html) or
[afl-fuzz](http://lcamtuf.coredump.cx/afl/).

These fuzzing tools, are based on *compile-time instrumentation* to measure
things like branch coverage and more advanced heuristics per fuzzing test.
Doing so allows these tools to progress through code with little to no custom
logic/knowledge implemented in the fuzzer itself. Usually, the only thing
these tools need is a code "shim" that provides the entry point for the fuzzer
to the code to be tested. We call this additional code a *fuzzing target* and
the rest of this manual describes how to implement and work with these targets.

As for the tools used with these targets, we currently recommend the use of
libFuzzer over afl-fuzz, as the latter is no longer maintained while libFuzzer
is being actively developed. Furthermore, libFuzzer has some advanced
instrumentation features (e.g. value profiling to deal with complicated
comparisons in code), making it overall more effective.

## What can be tested?

The interface can be used to test all C/C++ code that either ends up in
`libxul` (more precisely, the gtest version of `libxul`) **or** is
part of the JS engine.

Note that this is not the right testing approach for testing the full
browser as a whole. It is rather meant for component-based testing
(especially as some components cannot be easily separated out of the
full build).

:::{note}
**Note:** If you are working on the JS engine (trying to reproduce a
bug or seeking to develop a new fuzzing target), then please also read
the {ref}`JS Engine Specifics Section <js-engine-specifics>` at the end
of this documentation, as the JS engine offers additional options for
implementing and running fuzzing targets.
:::

## Reproducing bugs for existing fuzzing targets

If you are working on a bug that involves an existing fuzzing interface target,
you have two options for reproducing the issue:

### Using existing builds

We have several fuzzing builds in CI that you can simply download. We recommend
using `fuzzfetch` for this purpose, as it makes downloading and unpacking
these builds much easier.

You can install `fuzzfetch` from
[Github](https://github.com/MozillaSecurity/fuzzfetch) or
[via pip](https://pypi.org/project/fuzzfetch/).

Afterwards, you can run

```
$ python -m fuzzfetch -a --fuzzing --target firefox gtest -n firefox-fuzzing
```

to fetch the latest optimized build. Alternatively, we offer non-ASan debug builds
which you can download using

```
$ python -m fuzzfetch -d --fuzzing --target firefox gtest -n firefox-fuzzing
```

In both commands, `firefox-fuzzing` indicates the name of the directory that
will be created for the download.

Afterwards, you can reproduce the bug using

```
$ FUZZER=TargetName firefox-fuzzing/firefox test.bin
```

assuming that `TargetName` is the name of the fuzzing target specified in the
bug you are working on and `test.bin` is the attached testcase.

:::{note}
**Note:** You should not export the `FUZZER` variable permanently
in your shell, especially if you plan to do local builds. If the `FUZZER`
variable is exported, it will affect the build process.
:::

If the CI builds don't meet your requirements and you need a local build instead,
you can follow the steps below to create one:

(local-build-requirements-and-flags)=

### Local build requirements and flags

You will need a Linux environment with a recent Clang. Using the Clang downloaded
by `./mach bootstrap` or a newer version is recommended.

The only build flag required to enable the fuzzing targets is `--enable-fuzzing`,
so adding

```
ac_add_options --enable-fuzzing
```

to your `.mozconfig` is already sufficient for producing a fuzzing build.
However, for improved crash handling capabilities and to detect additional errors,
it is strongly recommended to combine libFuzzer with {doc}`AddressSanitizer </tools/sanitizer/asan>`
at least for optimized builds and bugs requiring ASan to reproduce at all
(e.g. you are working on a bug where ASan reports a memory safety violation
of some sort).

Once your build is complete, if you want to run gtests, you **must** additionally run

```
$ ./mach gtest dontruntests
```

to force the gtest libxul to be built.

If you get the error `error while loading shared libraries: libxul.so: cannot
open shared object file: No such file or directory`, you need to explicitly
set `LD_LIBRARY_PATH` to your build directory `obj-dir` before the command
to invoke the fuzzing. For example an IPC fuzzing invocation:

```
$ LD_LIBRARY_PATH=/path/to/obj-dir/dist/bin/ MOZ_FUZZ_TESTFILE=/path/to/test.bin NYX_FUZZER="IPC_Generic" /path/to/obj-dir/dist/bin/firefox /path/to/testcase.html
```

:::{note}
**Note:** If you modify any code, please ensure that you run **both** build
commands to ensure that the gtest libxul is also rebuilt. It is a common mistake
to only run `./mach build` and miss the second command.
:::

Once these steps are complete, you can reproduce the bug locally using the same
steps as described above for the downloaded builds.

## Developing new fuzzing targets

Developing a new fuzzing target using the fuzzing interface only requires a few steps.

### Determine if the fuzzing interface is the right tool

The fuzzing interface is not suitable for every kind of testing. In particular
if your testing requires the full browser to be running, then you might want to
look into other testing methods.

The interface uses the `ScopedXPCOM` implementation to provide an environment
in which XPCOM is available and initialized. You can initialize further subsystems
that you might require, but you are responsible yourself for any kind of
initialization steps.

There is (in theory) no limit as to how far you can take browser initialization.
However, the more subsystems are involved, the more problems might occur due to
non-determinism and loss of performance.

If you are unsure if the fuzzing interface is the right approach for you or you
require help in evaluating what could be done for your particular task, please
don't hesitate to {ref}`contact us <fuzzing-contact-us>`.

### Develop the fuzzing code

#### Where to put your fuzzing code

The code using the fuzzing interface usually lives in a separate directory
called `fuzztest` that is on the same level as gtests. If your component
has no gtests, then a subdirectory either in tests or in your main directory
will work. If such a directory does not exist yet in your component, then you
need to create one with a suitable `moz.build`. See {searchfox}`the transport target for an example <dom/media/webrtc/transport/fuzztest/moz.build>`

In order to include the new subdirectory into the build process, you will
also have to modify the toplevel `moz.build` file accordingly. For this
purpose, you should add your directory to `TEST_DIRS` only if `FUZZING_INTERFACES`
is set. See again {searchfox}`the transport target for an example <mozilla-central/rev/de7676288a78b70d2b9927c79493adbf294faad5:media/mtransport/moz.build#18-24>`.

#### How your code should look like

In order to define your fuzzing target `MyTarget`, you only need to implement 2 functions:

1. A one-time initialization function.

   At startup, the fuzzing interface calls this function **once**, so this can
   be used to perform one-time operations like initializing subsystems or parsing
   extra fuzzing options.

   This function is the equivalent of the [LLVMFuzzerInitialize](https://llvm.org/docs/LibFuzzer.html#startup-initialization)
   function and has the same signature. However, with our fuzzing interface,
   it won't be resolved by its name, so it can be defined `static` and called
   whatever you prefer. Note that the function should always `return 0` and
   can (except for the return), remain empty.

   For the sake of this documentation, we assume that you have `static int FuzzingInitMyTarget(int* argc, char*** argv);`

2. The fuzzing iteration function.

   This is where the actual fuzzing happens, and this function is the equivalent
   of [LLVMFuzzerTestOneInput](https://llvm.org/docs/LibFuzzer.html#fuzz-target).
   Again, the difference to the fuzzing interface is that the function won't be
   resolved by its name. In addition, we offer two different possible signatures
   for this function, either

   `static int FuzzingRunMyTarget(const uint8_t* data, size_t size);`

   or

   `static int FuzzingRunMyTarget(nsCOMPtr<nsIInputStream> inputStream);`

   The latter is just a wrapper around the first one for implementations that
   usually work with streams. No matter which of the two signatures you choose
   to work with, the only thing you need to implement inside the function
   is the use of the provided data with your target implementation. This can
   mean to simply feed the data to your target, using the data to drive operations
   on the target API, or a mix of both.

   While doing so, you should avoid altering global state in a permanent way,
   using additional sources of data/randomness or having code run beyond the
   lifetime of the iteration function (e.g. on another thread), for one simple
   reason: Coverage-guided fuzzing tools depend on the **deterministic** nature
   of the iteration function. If the same input to this function does not lead
   to the same execution when run twice (e.g. because the resulting state depends
   on multiple successive calls or because of additional external influences),
   then the tool will not be able to reproduce its fuzzing progress and perform
   badly. Dealing with this restriction can be challenging e.g. when dealing
   with asynchronous targets that run multi-threaded, but can usually be managed
   by synchronizing execution on all threads at the end of the iteration function.
   For implementations accumulating global state, it might be necessary to
   (re)initialize this global state in each iteration, rather than doing it once
   in the initialization function, even if this costs additional performance.

   Note that unlike the vanilla libFuzzer approach, you are allowed to `return 1`
   in this function to indicate that an input is "bad". Doing so will cause
   libFuzzer to discard the input, no matter if it generated new coverage or not.
   This is particularly useful if you have means to internally detect and catch
   bad testcase behavior such as timeouts/excessive resource usage etc. to avoid
   these tests to end up in your corpus.

Once you have implemented the two functions, the only thing remaining is to
register them with the fuzzing interface. For this purpose, we offer two
macros, depending on which iteration function signature you used. If you
stuck to the classic signature using buffer and size, you can simply use

```
#include "FuzzingInterface.h"

// Your includes and code

MOZ_FUZZING_INTERFACE_RAW(FuzzingInitMyTarget, FuzzingRunMyTarget, MyTarget);
```

where `MyTarget` is the name of the target and will be used later to decide
at runtime which target should be used.

If instead you went for the streaming interface, you need a different include,
but the macro invocation is quite similar:

```
#include "FuzzingInterfaceStream.h"

// Your includes and code

MOZ_FUZZING_INTERFACE_STREAM(FuzzingInitMyTarget, FuzzingRunMyTarget, MyTarget);
```

For a live example, see also the {searchfox}`implementation of the STUN fuzzing target <dom/media/webrtc/transport/fuzztest/stun_parser_libfuzz.cpp>`.

### Add instrumentation to the code being tested

libFuzzer requires that the code you are trying to test is instrumented
with special compiler flags. Fortunately, adding these on a per-directory basis
can be done just by including the following directive in each `moz.build`
file that builds code under test:

```
# Add libFuzzer configuration directives
include('/tools/fuzzing/libfuzzer-config.mozbuild')
```

The include already does the appropriate configuration checks to be only
active in fuzzing builds, so you don't have to guard this in any way.

:::{note}
**Note:** This include modifies `CFLAGS` and `CXXFLAGS` accordingly
but this only works for source files defined in this particular
directory. The flags are **not** propagated to subdirectories automatically
and you have to ensure that each directory that builds source files
for your target has the include added to its `moz.build` file.
:::

By keeping the instrumentation limited to the parts that are actually being
tested using this tool, you not only increase the performance but also potentially
reduce the amount of noise that libFuzzer sees.

### Build your code

See the {ref}`Build instructions above <local-build-requirements-and-flags>` for instructions
how to modify your `.mozconfig` to create the appropriate build.

### Running your code and building a corpus

You need to set the following environment variable to enable running the
fuzzing code inside Firefox instead of the regular browser.

- `FUZZER=name`

Where `name` is the name of your fuzzing module that you specified
when calling the `MOZ_FUZZING_INTERFACE_RAW` macro. For the example
above, this would be `MyTarget` or `StunParser` for the live example.

Now when you invoke the firefox binary in your build directory with the
`-help=1` parameter, you should see the regular libFuzzer help. On
Linux for example:

```
$ FUZZER=StunParser obj-asan/dist/bin/firefox -help=1
```

You should see an output similar to this:

```
Running Fuzzer tests...
Usage:

To run fuzzing pass 0 or more directories.
obj-asan/dist/bin/firefox [-flag1=val1 [-flag2=val2 ...] ] [dir1 [dir2 ...] ]

To run individual tests without fuzzing pass 1 or more files:
obj-asan/dist/bin/firefox [-flag1=val1 [-flag2=val2 ...] ] file1 [file2 ...]

Flags: (strictly in form -flag=value)
 verbosity                      1       Verbosity level.
 seed                           0       Random seed. If 0, seed is generated.
 runs                           -1      Number of individual test runs (-1 for infinite runs).
 max_len                        0       Maximum length of the test input. If 0, libFuzzer tries to guess a good value based on the corpus and reports it.
...
```

#### Reproducing a Crash

In order to reproduce a crash from a given test file, simply put the
file as the only argument on the command line, e.g.

```
$ FUZZER=StunParser obj-asan/dist/bin/firefox test.bin
```

This should reproduce the given problem.

#### FuzzManager and libFuzzer

Our FuzzManager project comes with a harness for running libFuzzer with
an optional connection to a FuzzManager server instance. Note that this
connection is not mandatory, even without a server you can make use of
the local harness.

You can find the harness
[here](https://github.com/MozillaSecurity/FuzzManager/tree/master/misc/afl-libfuzzer).

An example invocation for the harness to use with StunParser could look
like this:

```
FUZZER=StunParser python /path/to/afl-libfuzzer-daemon.py --fuzzmanager \
    --stats libfuzzer-stunparser.stats --libfuzzer-auto-reduce-min 500 --libfuzzer-auto-reduce 30 \
    --tool libfuzzer-stunparser --libfuzzer --libfuzzer-instances 6 obj-asan/dist/bin/firefox \
    -max_len=256 -use_value_profile=1 -rss_limit_mb=3000 corpus-stunparser
```

What this does is

- run libFuzzer on the `StunParser` target with 6 parallel instances
  using the corpus in the `corpus-stunparser` directory (with the
  specified libFuzzer options such as `-max_len` and
  `-use_value_profile`)
- automatically reduce the corpus and restart if it grew by 30% (and
  has at least 500 files)
- use FuzzManager (need a local `.fuzzmanagerconf` and a
  `firefox.fuzzmanagerconf` binary configuration as described in the
  FuzzManager manual) and submit crashes as `libfuzzer-stunparser`
  tool
- write statistics to the `libfuzzer-stunparser.stats` file

(js-engine-specifics)=

## JS Engine Specifics

The fuzzing interface can also be used for testing the JS engine, in fact there
are two separate options to implement and run fuzzing targets:

### Implementing in C++

Similar to the fuzzing interface in Firefox, you can implement your target in
entirely C++ with very similar interfaces compared to what was described before.

There are a few minor differences though:

1. All of the fuzzing targets live in `js/src/fuzz-tests`.

2. All of the code is linked into a separate binary called `fuzz-tests`,
   similar to how all JSAPI tests end up in `jsapi-tests`. In order for this
   binary to be built, you must build a JS shell with `--enable-fuzzing`
   **and** `--enable-tests`. Again, this can and should be combined with
   AddressSanitizer for maximum effectiveness. This also means that there is no
   need to (re)build gtests when dealing with a JS fuzzing target and using
   a shell as part of a full browser build.

3. The harness around the JS implementation already provides you with an
   initialized `JSContext` and global object. You can access these in
   your target by declaring

   `extern JS::PersistentRootedObject gGlobal;`

   and

   `extern JSContext* gCx;`

   but there is no obligation for you to use these.

For a live example, see also the {searchfox}`implementation of the StructuredCloneReader target <js/src/fuzz-tests/testStructuredCloneReader.cpp>`.

### Implementing in JS

In addition to the C++ targets, you can also implement targets in JavaScript
using the JavaScript Runtime (JSRT) fuzzing approach. Using this approach is
not only much simpler (since you don't need to know anything about the
JSAPI or engine internals), but it also gives you full access to everything
defined in the JS shell, including handy functions such as `timeout()`.

Of course, this approach also comes with disadvantages: Calling into JS and
performing the fuzzing operations there costs performance. Also, there is more
chance for causing global side-effects or non-determinism compared to a
fairly isolated C++ target.

As a rule of thumb, you should implement the target in JS if

- you don't know C++ and/or how to use the JSAPI (after all, a JS fuzzing target is better than none),
- your target is expected to have lots of hangs/timeouts (you can catch these internally),
- or your target is not isolated enough for a C++ target and/or you need specific JS shell functions.

There is an {searchfox}`example target <js/src/shell/jsrtfuzzing/jsrtfuzzing-example.js>`
in-tree that shows roughly how to implement such a fuzzing target.

To run such a target, you must run the `js` (shell) binary instead of the
`fuzz-tests` binary and point the `FUZZER` variable to the file containing
your fuzzing target, e.g.

```
$ FUZZER=/path/to/jsrtfuzzing-example.js obj-asan/dist/bin/js --fuzzing-safe --no-threads -- <libFuzzer options here>
```

More elaborate targets can be found in {searchfox}`js/src/fuzz-tests/`.

## Troubleshooting

### Fuzzing Interface: Error: No testing callback found

This error means that the fuzzing callback with the name you specified
using the `FUZZER` environment variable could not be found. Reasons
for are typically either a misspelled name or that your code wasn't
built (check your `moz.build` file and build log).

### `mach build` doesn't seem to update my fuzzing code

Keep in mind you always need to run both the `mach build` and
`mach gtest dontruntests` commands in order to update your fuzzing
code. The latter rebuilds the gtest version of `libxul`, containing
your code.
