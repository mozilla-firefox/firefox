# Address Sanitizer

## What is Address Sanitizer?

Address Sanitizer (ASan) is a fast memory error detector that detects
use-after-free and out-of-bound bugs in C/C++ programs. It uses a
compile-time instrumentation to check all reads and writes during the
execution. In addition, the runtime part replaces the `malloc` and
`free` functions to check dynamically allocated memory. More
information on how ASan works can be found on [the Address Sanitizer
wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer).

A [meta bug called asan-maintenance](https://bugzilla.mozilla.org/show_bug.cgi?id=asan-maintenance)
is maintained to keep track of all the bugs found with ASan.

## Downloading artifact builds

For Linux and Windows users, the easiest way to get Firefox builds with
Address Sanitizer is to download a continuous integration asan build of
mozilla-central (updated at least daily):

- mozilla-central optimized builds:
  [linux](https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.linux64-asan-opt/artifacts/public/build/target.tar.xz)
  |
  [windows](https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.win64-asan-opt/artifacts/public/build/target.zip)
  (recommended for testing)
- mozilla-central debug builds:
  [linux](https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.linux64-asan-debug/artifacts/public/build/target.tar.xz)
  |
  [windows](https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.win64-asan-debug/artifacts/public/build/target.zip)
  (recommended for debugging if the optimized builds don't do the job)

The fuzzing team also offers a tool called `fuzzfetch` to download these and many
other CI builds. It makes downloading and unpacking these builds much easier and
can be used not just for fuzzing but for all purposes that require a CI build download.

You can install `fuzzfetch` from
[Github](https://github.com/MozillaSecurity/fuzzfetch) or
[via pip](https://pypi.org/project/fuzzfetch/).

Afterwards, you can run e.g.

```
$ python -m fuzzfetch --asan -n firefox-asan
```

to get the optimized Linux ASan build mentioned above unpacked into a directory called `firefox-asan`.
The `--debug` and `--os` switches can be used to get the other variants listed above.

## Creating Try builds

If for some reason you can't use the pre-built binaries mentioned in the
previous section (e.g. you want a non-Linux build or you need to test a
patch), you can either build Firefox yourself (see the following
section) or use the {doc}`try server </tools/try/index>` to
create the customized build for you. Pushing to try requires L1 commit
access. If you don't have this access yet you can request access (see
[Becoming A Mozilla
Committer](https://www.mozilla.org/about/governance/policies/commit/)
and [Mozilla Commit Access
Policy](https://www.mozilla.org/about/governance/policies/commit/access-policy/)
for the requirements).

The tree contains [several mozconfig files for creating asan
builds](https://searchfox.org/mozilla-central/search?q=&case=true&path=browser%2Fconfig%2Fmozconfigs%2F*%2F*asan*)
(the "nightly-asan" files create release builds, whereas the
"debug-asan" files create debug+opt builds). For Linux builds, the
appropriate configuration file is used by the `linux64-asan` target.
If you want to create a macOS or Windows build, you'll need to copy the
appropriate configuration file over the regular debug configuration
before pushing to try. For example:

```
cp browser/config/mozconfigs/macosx64/debug-asan browser/config/mozconfigs/macosx64/debug
```

You can then [push to Try in the usual
way](/tools/try/index.md)
and, once the build is complete, download the appropriate build
artifact.

## Creating local builds on Windows

On Windows, ASan is supported only in 64-bit builds.

Run `mach bootstrap` to get an updated clang-cl in your
`~/.mozbuild` directory, then use the following
{doc}`mozconfig </setup/configuring_build_options>`:

```
ac_add_options --enable-address-sanitizer
ac_add_options --disable-jemalloc
```

If you launch an ASan build under WinDbg, you may see spurious
first-chance Access Violation exceptions. These come from ASan creating
shadow memory pages on demand, and can be ignored. Run `sxi av` to
ignore these exceptions. (You will still catch second-chance Access
Violation exceptions if you actually crash.)

LeakSanitizer (LSan) is not supported on Windows.

## Creating local builds on Linux or Mac

### Build prerequisites

#### LLVM/Clang

The ASan instrumentation is implemented as an LLVM pass and integrated
into Clang. Any clang version that is capable of compiling Firefox has
everything needed to do an ASAN build.

### Building Firefox

#### Getting the source

Using that or any later revision, all you need to do is to {doc}`get yourself
a clone of the Firefox repository </contributing/contribution_quickref>`.

#### Adjusting the build configuration

Create the build configuration file `mozconfig` with the following
content in your Firefox source directory:

```
# Combined .mozconfig file for ASan on Linux+Mac

mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/objdir-ff-asan

# Enable ASan specific code and build workarounds
ac_add_options --enable-address-sanitizer

# These three are required by ASan
ac_add_options --disable-jemalloc
ac_add_options --disable-crashreporter
ac_add_options --disable-elf-hack

# Keep symbols to symbolize ASan traces later
ac_add_options --disable-install-strip

# Settings for an opt build (preferred)
# The -gline-tables-only ensures that all the necessary debug information for ASan
# is present, but the rest is stripped so the resulting binaries are smaller.
ac_add_options --enable-debug-symbols=-gline-tables-only

# Settings for a debug+opt build
#ac_add_options --enable-debug

# MacOSX only: Uncomment and adjust this path to match your SDK
# ac_add_options --with-macos-sdk=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.8.sdk
```

You may also need this, as seen in
`browser/config/mozconfigs/linux64/nightly-asan` (the config file used
for Address Sanitizer builds used for automated testing):

```
# ASan specific options on Linux
ac_add_options --enable-valgrind
```

#### Starting the build process

Now you start the build process using the regular `./mach build`
command.

#### Starting Firefox

After the build has completed, `./mach run` with the usual options for
running in a debugger (`gdb`, `lldb`, `rr`, etc.) work fine, as do
the `--disable-e10s` and other options.

#### Building only the JavaScript shell

If you want to build only the JavaScript shell instead of doing a full
Firefox build, the build script below will probably help you to do so.
Execute this script in the `js/src/` subdirectory and pass a directory
name as the first parameter. The build will then be created in a new
subdirectory with that name.

```
#! /bin/sh

if [ -z $1 ] ; then
     echo "usage: $0 <dirname>"
elif [ -d $1 ] ; then
     echo "directory $1 already exists"
else
     mkdir $1
     cd $1
     CC="clang" \
     CXX="clang++" \
     CFLAGS="-fsanitize=address" \
     CXXFLAGS="-fsanitize=address" \
     LDFLAGS="-fsanitize=address" \
     ../configure --enable-debug --enable-optimize --enable-address-sanitizer --disable-jemalloc
fi
```

### Getting Symbols in Address Sanitizer Traces

By default, ASan traces are unsymbolized and only print the
binary/library and a memory offset instead. In order to get more useful
traces, containing symbols, there are two approaches.

#### Using the LLVM Symbolizer (recommended)

LLVM ships with a symbolizer binary that ASan will readily use to
immediately output symbolized traces. To use it, just set the
environment variable `ASAN_SYMBOLIZER_PATH` to reflect the location of
your `llvm-symbolizer` binary, before running the process. This
program is usually included in an LLVM distribution. Stacks without
symbols can also be post-processed, see below.

::::{warning}

:::{note}
**Warning:** On OS X, the content sandbox prevents the symbolizer
from running. To use llvm-symbolizer on ASan output from a
content process, the content sandbox must be disabled. This can be
done by setting `MOZ_DISABLE_CONTENT_SANDBOX=1` in your run
environment. Setting this in .mozconfig has no effect.
:::
::::

#### Post-Processing Traces with asan_symbolize.py

Instead of using the llvm-symbolizer binary, you can also pipe the
output through the `asan_symbolize.py` script, shipped with LLVM
(`$LLVM_HOME/projects/compiler-rt/lib/asan/scripts/asan_symbolize.py`),
often included in LLVM distributions. The disadvantage is that the
script will need to use `addr2line` to get the symbols, which means
that every library will have to be loaded into memory
(including\`\`libxul\`\`, which takes a bit).

However, in certain situations it makes sense to use this script. For
example, if you have/received an unsymbolized trace, then you can still
use the script to turn it into a symbolized trace, given that you can
get the original binaries that produced the unsymbolized trace. In order
for the script to work in such cases, you need to ensure that the paths
in the trace point to the actual binaries, or change the paths
accordingly.

Since the output of the `asan_symbolize.py` script is still mangled,
you might want to pipe the output also through `c++filt` afterwards.

### Troubleshooting / Known problems

#### Cannot specify -o when generating multiple output files

If you get the error
"`cannot specify -o when generating multiple output files"` from
clang, disable `elf-hack` in your `mozconfig` to work around the
issue:

```
ac_add_options --disable-elf-hack
```

#### Optimized build

Since [an issue with -O2/-Os and
ASan](https://github.com/google/sanitizers/issues/20)
has been resolved, the regular optimizations used by Firefox should work
without any problems. The optimized build has only a barely noticeable
speed penalty and seems to be even faster than regular debug builds.

#### No "AddressSanitizer: **libc** interceptors initialized" shows after running ./mach run

```
$ ASAN_OPTIONS=verbosity=2 ./mach run
```

Use the above command instead

#### "An admin user name and password" is required to enter Developer Mode

Please enable **Developer** **mode** by:

```
$ /usr/sbin/DevToolsSecurity -enable
Developer mode is now enabled.
```

## Debugging issues that ASan finds

When ASan discovers an issue it will simply print an error message and
exit the app. To stop the app in a debugger before ASan exits it, set a
breakpoint on `__asan::ReportGenericError`. For more info on using
ASan and debugging issues that it uncovers, see the page [Address
sanitizer and a
debugger](https://github.com/google/sanitizers/wiki/AddressSanitizerAndDebugger)
page on the upstream wiki.

`__asan_describe_address(pointer)` issued at the debugger prompt or
even directly in the code allows outputting lots of information about
this memory address (thread and stack of allocation, of deallocation,
whether or not it is a bit outside a known buffer, thread and stack of
allocation of this buffer, etc.). This can be useful to understand where
some buffer that is not aligned was allocated, when doing SIMD work, for
example.

[rr](https://rr-project.org/) (Linux x86 only) works great with ASan
and combined, this combo allows doing some very powerful debugging
strategies.

## LeakSanitizer

LeakSanitizer (LSan) is a special execution mode for regular ASan. It
takes advantage of how ASan tracks the set of live blocks at any given
point to print out the allocation stack of any block that is still alive
at shutdown, but is not reachable from the stack, according to a
conservative scan. This is very useful for detecting leaks of things
such as `char*` that do not participate in the usual Gecko shutdown
leak detection. LSan is supported on x86_64 Linux and OS X.

LSan is enabled by default in ASan builds, as of more recent versions of
Clang. To make an ASan build not run LSan, set the environment variable
`ASAN_OPTIONS` to `detect_leaks=0` (or add it as an entry to a
`:`-separated list if it is already set to something). If you want to
enable it when it is not for some reason, set it to 1 instead of 0. If
LSan is enabled and you are using a non-debug build, you will also want
to set the environment variable `MOZ_CC_RUN_DURING_SHUTDOWN=1`, to
ensure that we run shutdown GCs and CCs to avoid spurious leaks.

If an object that is reported by LSan is intentionally never freed, a
symbol can be added to `build/sanitizers/lsan_suppressions.txt` to get
LSan to ignore it.

For some more information on LSan, see the [Leak Sanitizer wiki
page](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer).

A [meta bug called lsan](https://bugzilla.mozilla.org/show_bug.cgi?id=lsan)
is maintained to keep track of all the bugs found with LSan.

## Frequently Asked Questions about ASan

### How does ASan work exactly?

More information on how ASan works can be found on [the Address Sanitizer wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer).
