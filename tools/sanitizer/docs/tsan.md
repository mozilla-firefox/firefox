# Thread Sanitizer

## What is Thread Sanitizer?

Thread Sanitizer (TSan) is a fast data race detector for C/C++ and Rust
programs. It uses a compile-time instrumentation to check all non-race-free
memory access at runtime. Unlike other tools, it understands compiler-builtin
atomics and synchronization and therefore provides very accurate results
with no false positives (except if unsupported synchronization primitives
like inline assembly or memory fences are used). More information on how
TSan works can be found on [the Thread Sanitizer wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm).

A [meta bug called tsan](https://bugzilla.mozilla.org/show_bug.cgi?id=tsan)
is maintained to keep track of all the bugs found with TSan.

A [blog post on hacks.mozilla.org](https://hacks.mozilla.org/2021/04/eliminating-data-races-in-firefox-a-technical-report/) describes this project.

Note that unlike other sanitizers, TSan is currently **only supported on Linux**.

## Downloading artifact builds

The easiest way to get Firefox builds with Thread Sanitizer is to download a
continuous integration TSan build of mozilla-central (updated at least daily):

- mozilla-central optimized builds:
  [linux](https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/gecko.v2.mozilla-central.latest.firefox.linux64-tsan-opt/artifacts/public/build/target.tar.xz)

The fuzzing team also offers a tool called `fuzzfetch` to download this and many
other CI builds. It makes downloading and unpacking these builds much easier and
can be used not just for fuzzing but for all purposes that require a CI build download.

You can install `fuzzfetch` from
[Github](https://github.com/MozillaSecurity/fuzzfetch) or
[via pip](https://pypi.org/project/fuzzfetch/).

Afterwards, you can run

```
$ python -m fuzzfetch --tsan -n firefox-tsan
```

to get the build mentioned above unpacked into a directory called `firefox-tsan`.

## Creating Try builds

If for some reason you can't use the pre-built binaries mentioned in the
previous section (e.g. you need to test a patch), you can either build
Firefox yourself (see the following section) or use the {doc}`try server </tools/try/index>`
to create the customized build for you. Pushing to try requires L1 commit
access. If you don't have this access yet you can request access (see
[Becoming A Mozilla
Committer](https://www.mozilla.org/about/governance/policies/commit/)
and [Mozilla Commit Access
Policy](https://www.mozilla.org/about/governance/policies/commit/access-policy/)
for the requirements).

Using `mach try fuzzy --full` you can select the `build-linux64-tsan/opt` job
and related tests (if required).

## Creating local builds on Linux

### Build prerequisites

#### LLVM/Clang/Rust

The TSan instrumentation is implemented as an LLVM pass and integrated
into Clang. We strongly recommend that you use the Clang version supplied
as part of the `mach bootstrap` process, as we backported several required
fixes for TSan on Firefox.

Sanitizer support in Rust is genuinely experimental,
so our build system only works with a specially patched version of Rust
that we build in our CI. To install that specific version (or update to a newer
version), run the following in the root of your Firefox checkout:

```
./mach artifact toolchain --from-build linux64-rust-dev
rm -rf ~/.mozbuild/rustc-sanitizers
mv rustc ~/.mozbuild/rustc-sanitizers
rustup toolchain link gecko-sanitizers ~/.mozbuild/rustc-sanitizers
rustup override set gecko-sanitizers
```

`mach artifact` will always download the `linux64-rust-dev` toolchain associated
with the current mozilla central commit you have checked out. The toolchain should
mostly behave like a normal rust nightly but we don't recommend using it for anything
other than building gecko, just in case. Also note that
`~/.mozbuild/rustc-sanitizers` is just a reasonable default location -- feel
free to "install" the toolchain wherever you please.

### Building Firefox

#### Getting the source

Using that or any later revision, all you need to do is to {doc}`get yourself
a clone of the Firefox repository </contributing/contribution_quickref>`.

#### Adjusting the build configuration

Create the build configuration file `mozconfig` with the following
content in your Firefox source directory:

```
# Combined .mozconfig file for TSan on Linux+Mac

mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/objdir-ff-tsan

# Enable ASan specific code and build workarounds
ac_add_options --enable-thread-sanitizer

# This ensures that we also instrument Rust code.
export RUSTFLAGS="-Zsanitizer=thread"

# rustfmt is currently missing in Rust nightly
unset RUSTFMT

# Current Rust Nightly has warnings
ac_add_options --disable-warnings-as-errors

# These are required by TSan
ac_add_options --disable-jemalloc
ac_add_options --disable-crashreporter
ac_add_options --disable-elf-hack
ac_add_options --disable-profiling

# The Thread Sanitizer is not compatible with sandboxing
# (see bug 1182565)
ac_add_options --disable-sandbox

# Keep symbols to symbolize TSan traces later
ac_add_options --disable-install-strip

# The -gline-tables-only ensures that all the necessary debug information for ASan
# is present, but the rest is stripped so the resulting binaries are smaller.
ac_add_options --enable-debug-symbols=-gline-tables-only

# Settings for a debug+opt build
#ac_add_options --enable-debug
```

#### Starting the build process

Now you start the build process using the regular `./mach build`
command.

#### Starting Firefox

After the build has completed, `./mach run` with the usual options for
running in a debugger (`gdb`, `lldb`, `rr`, etc.) work fine, as do
the `--disable-e10s` and other options.

While running Firefox, ensure that it's not in safe mode since it might cause
some tsan failures during startup. You can use a different profile or add
`--temp-profile` to use a temporary one.

Firefox might crash on startup if you have an NVIDIA GPU with proprietary
drivers. To fix this, disable the graphics acceleration by changing the following
prefs:

- `gfx.x11-egl.force-disabled=true`
- `gfx.webrender.software.opengl=true`
- `layers.acceleration.disabled=true`

You can either do this by passing these prefs to your `./mach run` command
like this: `./mach run --setpref "gfx.x11-egl.force-disabled=true" --setpref "gfx.webrender.software.opengl=true" --setpref "layers.acceleration.disabled=true"`
or you can add them to your `machrc` file. Learn more about mach settings
{ref}`here<mach-settings>`.

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
     CC="/path/to/mozbuild/clang" \
     CXX="/path/to/mozbuild/clang++" \
     ../configure --enable-debug-symbols=-gline-tables-only --enable-thread-sanitizer --disable-jemalloc
fi
```

## Thread Sanitizer and Symbols

Unlike Address Sanitizer, TSan requires in-process symbolizing to work
properly in the first place, as any kind of runtime suppressions will
otherwise not work.

Hence, it is required that you have a copy of `llvm-symbolizer` either
in your `PATH` or pointed to by the `TSAN_SYMBOLIZER_PATH` environment
variable. This binary is included in your local mozbuild directory, obtained
by `./mach bootstrap`.

## Runtime Suppressions

TSan has the ability to suppress race reports at runtime. This can be used to
silence a race while a fix is developed as well as to permanently silence a
(benign) race that cannot be fixed.

:::{warning}
**Warning**: Many races *look* benign but are indeed not. Please read
the {ref}`FAQ section <frequently-asked-questions-about-tsan>` carefully
and think twice before attempting to suppress a race.
:::

The runtime Suppression list is directly baked into Firefox at compile-time and
located at {searchfox}`build/sanitizers/TsanOptions.cpp`.

:::{warning}
**Important**: When adding a suppression, always make sure to include
the bug number. If the suppression is supposed to be permanent, please
add the string `permanent` in the same line as the bug number.
:::

:::{warning}
**Important**: When adding a suppression for a *data race*, always make
sure to include a stack frame from **each** of the two race stacks.
Adding only one suppression for one stack can cause intermittent failures
that are later on hard to track. One exception to this rule is when suppressing
races on global variables. In that case, a single race entry with the name of
the variable is sufficient.
:::

## Troubleshooting / Known Problems

### Known Sources of False Positives

TSan has a number of things that can cause false positives, namely:

- The use of memory fences (e.g. Rust Arc)
- The use of inline assembly for synchronization
- Uninstrumented code (e.g. external libraries) using compiler-builtins for synchronization
- A lock order inversion involving only a single thread can cause a false positive deadlock
  report (see also <https://github.com/google/sanitizers/issues/488>).

If none of these four items are involved, you should *never* assume that TSan is reporting
a false positive to you without consulting TSan peers. It is very easy to misjudge a race
to be a false positive because races can be highly complex and totally non-obvious due to
compiler optimizations and the nature of parallel code.

### Intermittent Broken Stacks

If you intermittently see race reports where one stack is missing with a `failed to restore the stack`
message, this can indicate that a suppression is partially covering the race you are seeing.

Any race where only one of the two stacks is matched by a runtime suppression will show up
if that particular stack fails to symbolize for some reason. The usual solution is to search
the suppressions for potential candidates and disable them temporarily to check if your race
report now becomes mostly consistent.

However, there are other reasons for broken TSan stacks, in particular if they are not intermittent.
See also the `history_size` parameter in the [TSan flags](https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags).

### Intermittent Race Reports

Unfortunately, the TSan algorithm does not guarantee, that a race is detected 100% of the
time. Intermittent failures with TSan are (to a certain degree) to be expected and the races
involved should be filed and fixed to solve the problem.

(frequently-asked-questions-about-tsan)=

## Frequently Asked Questions about TSan

### Why fix data races?

Data races are undefined behavior and can cause crashes as well as correctness issues.
Compiler optimizations can cause racy code to have unpredictable and hard-to-reproduce behavior.

At Mozilla, we have already seen several dangerous races, causing random
[use-after-free crashes](https://bugzilla.mozilla.org/show_bug.cgi?id=1580288),
[intermittent test failures](https://bugzilla.mozilla.org/show_bug.cgi?id=1602009),
[hangs](https://bugzilla.mozilla.org/show_bug.cgi?id=1607008),
[performance issues](https://bugzilla.mozilla.org/show_bug.cgi?id=1615045) and
[intermittent asserts](https://bugzilla.mozilla.org/show_bug.cgi?id=1601940). Such problems do
not only decrease the quality of our code and user experience, but they also waste countless hours
of developer time.

Since it is very hard to judge if a particular race could cause such a situation, we
have decided to fix all data races wherever possible, since doing so is often cheaper
than analyzing a race.

### My race is benign, can we ignore it?

While it is possible to add a runtime suppression to ignore the race, we *strongly* encourage
you to not do so, for two reasons:

1. Each suppressed race decreases the overall performance of the TSan build, as the race
   has to be symbolized each time when it occurs. Since TSan is already in itself a slow
   build, we need to keep the amount of suppressed races as low as possible.
2. Deciding if a race is truly benign is surprisingly hard. We recommend to read
   [this blog post](http://software.intel.com/en-us/blogs/2013/01/06/benign-data-races-what-could-possibly-go-wrong)
   and `this paper <https://www.usenix.org/legacy/events/hotpar11/tech/final_files/Boehm.pdf>`
   on the effects of seemingly benign races.

Valid reasons to suppress a confirmed benign race include performance problems arising from
fixing the race or cases where fixing the race would require an unreasonable amount of work.

Note that the use of atomics usually does not have the bad performance impact that developers
tend to associate with it. If you assume that e.g. using atomics for synchronization will
cause performance regressions, we suggest to perform a benchmark to confirm this. In many
cases, the difference is not measurable.

### How does TSan work exactly?

More information on how TSan works can be found on [the Thread Sanitizer wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm).
