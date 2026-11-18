# Profiling on Android with `simpleperf`

`simpleperf` is an Android profiler which, unlike the Gecko profiler, can
profile all threads and works for non-Firefox apps.

To use `simpleperf`, your phone needs to be connected to a desktop machine. The
desktop machine can be Windows, macOS, or Linux.

## Installation

You need both `simpleperf` and [`samply`](https://github.com/mstange/samply).
`simpleperf` for profiling and `samply` for converting into the Firefox profiler
format.

`simpleperf` is in the Android NDK. It’s at `ndk-path/simpleperf/`.
Make sure your Android NDK is somewhat recent. `r26c` seems to work well.
If you have a Firefox checkout, you can run `./mach bootstrap` in it,
pick `4. GeckoView/Firefox for Android`, accept all the licenses, and it
will download the NDK to `~/.mozbuild/android-ndk-<version>`.

To install `samply`, follow the installation instructions at
[https://github.com/mstange/samply?tab=readme-ov-file\#installation](https://github.com/mstange/samply?tab=readme-ov-file#installation).

## Usage

**Step 1**: Open a terminal window and go into the simpleperf directory, for example:

```
cd ~/.mozbuild/android-ndk-r26c/simpleperf/
```

**Step 2**: Record a profile with `simpleperf`, while your phone is connected to the
desktop machine so that `adb` can see it: `./app_profiler.py -p org.mozilla.fenix`

By default this profiles for 10 seconds. You can interact with the Firefox app
during these 10 seconds and what you're doing should make it into the profile.

If everything goes smoothly, there will be a `perf.data` file in the current
directory once profiling is done.

**Step 3**: Import the `perf.data` file into the Firefox Profiler using
`samply`.

```
samply import perf.data --breakpad-symbol-server https://symbols.mozilla.org/try/
```

And that’s it! This should open a browser with the profile data. Example:
[https://share.firefox.dev/4bXQnKv](https://share.firefox.dev/4bXQnKv)

The `--breakpad-symbol-server` argument is needed when you profile official
Firefox Release / Nightly builds, in order to get Firefox C++ / Rust symbols. If
you’re profiling a local build (non-artifact), you need to tell samply about your
object directory, with `--symbol-dir gecko-android-objdir/dist/bin`

## Advanced Usage

### Profiling with off-cpu samples

To see stacks when threads are blocked or sleeping, run this command instead for step 2:

```
./app_profiler.py -p org.mozilla.fenix -r "-g --duration 10 -f 1000 --trace-offcpu -e cpu-clock:u"
```

### Profiling with frame pointer unwinding (when you want deep C++ stacks and don’t need Java stacks)

This command replaces “-g” with “--callgraph fp”. This will give you deeper
stacks and unwind successfully through JavaScript JIT code, but it will not
unwind Java stacks. Unfortunately there currently isn’t any way to get deep
stacks and Java stacks at the same time.

```
./app_profiler.py -p org.mozilla.fenix -r "--call-graph fp --duration 10 -f 1000 --trace-offcpu -e cpu-clock:u"
```

### Profiling on rooted phones

The steps above give you a profile of a single app, but the app has to mark
itself as “profileable”, otherwise you cannot get profiles on non-rooted
devices. (Debuggable apps are always profileable but also have extra startup
overhead which distorts profiles.) [Fenix is
profileable](https://searchfox.org/mozilla-central/rev/51f395e7d26987bb2bf5201a96f53a3559c43943/mobile/android/fenix/app/src/main/AndroidManifest.xml#68-70),
both Nightly and Firefox Release.

If you have a rooted device, you can run simpleperf through `adb shell su`.
This is a lot more powerful:

- You can profile all apps, even apps which don’t mark themselves as
“profileable”. For example, Chrome from the Play Store is not marked as
profileable.
- You can profile all processes system-wide.
  - Importantly, this is the only way to profile processes which are created
    after profiling starts. See the “Limitations” section at the end of this
    document.
- You can get kernel stacks with symbols.

The `app_profiler.py` script unfortunately
[cannot](https://github.com/android/ndk/issues/2027) run simpleperf as root;
instead, we’ll use `adb shell` and `adb pull` to perform its work manually.
Assuming you’ve run `./app_profiler.py` once (so that it has pushed the
simpleperf binary to `/data/local/tmp/`), and assuming you have `su` available,
the following should work:

```
adb shell su -c "/data/local/tmp/simpleperf record -g --duration 30 -f 1000 --trace-offcpu -e cpu-clock -a -o /data/local/tmp/su-perf.data"
adb pull /data/local/tmp/su-perf.data
samply import su-perf.data --breakpad-symbol-server https://symbols.mozilla.org/
```

You can also run the following commands to improve profiling results:

```
# Allow getting kernel symbols even when simpleperf is not running as root (when using “-e cpu-clock” rather than the default “-e cpu-clock:u”):
adb shell su -c "echo 0 > /proc/sys/kernel/kptr_restrict"
# Increase the stack depth limit, to unwind even deeper stacks:
adb shell su -c "echo 200 > /proc/sys/kernel/perf_event_max_stack"
# Prevent the kernel from throttling the max sampling rate:
adb shell su -c "sysctl -w kernel.perf_cpu_time_max_percent=0"
```

Android forgets these modifications when the phone shuts down. These commands
need to be re-run every time the phone is restarted.

Here’s the command for using frame pointer unwinding as root:

```
adb shell su -c "/data/local/tmp/simpleperf record --call-graph fp --duration 10 -f 1000 --trace-offcpu -e cpu-clock -a -o /data/local/tmp/su-perf.data"
```

### Profiling with JavaScript stacks

To get JavaScript stacks from Firefox, you need to set a bunch of environment
variables during startup. The easiest way to do this is for GeckoView-example,
with the help of `./mach run`.

#### Profiling GeckoView-example with JavaScript stacks

Start GeckoView-example with environment variables like this:

```
./mach run --no-install --setenv MOZ_USE_PERFORMANCE_MARKER_FILE=1 --setenv MOZ_PERFORMANCE_MARKER_DIR=/storage/emulated/0/Android/data/org.mozilla.geckoview_example/files --setenv PERF_SPEW_DIR=/storage/emulated/0/Android/data/org.mozilla.geckoview_example/files --setenv IONPERF=func --setenv JIT_OPTION_onlyInlineSelfHosted=true
```

Then profile as described under “Profiling with frame pointer unwinding”. The
`IONPERF=func` environment variable will cause Gecko to create jitdump files
named `jit-<pid>.dump`. After profiling, pull the jitdump and marker files from
the phone like this:

```
adb shell find /storage/emulated/0/Android/data/org.mozilla.geckoview_example/files '\( -name  jit-* -or -name marker-* \)' -print0 | xargs -0 -I {} adb pull '{}'
```

Then run `samply import` as before. If the jitdump files are stored in the same
directory as the `perf.data` file, samply will find them. It knows to look for
them based on the mmap events in the `perf.data` file. This will give you a
profile that contains JavaScript stacks. Example:
[https://share.firefox.dev/3BBZT9N](https://share.firefox.dev/3BBZT9N)

Unfortunately you cannot currently get JavaScript and Java stacks at the same
time. Either you use dwarf unwinding, and get Java but no JS stacks, or you use
frame pointer unwinding, and get JS but no Java stacks. `simpleperf`’s dwarf
unwinding doesn’t appear to fall back to framepointers for our JS JIT code.

#### Profiling Fenix with JavaScript stacks on a rooted phone

Profiling Fenix with JS stacks is a bit more complicated than profiling
GeckoView-example with JS stacks, just because it’s harder to set the
environment variables. The commands below worked for me, with a rooted phone and
Firefox Nightly from the Play Store installed:

```
echo "env:\n  PERF_SPEW_DIR: /storage/emulated/0/Android/data/org.mozilla.fenix/files\n  IONPERF: func\n  JIT_OPTION_onlyInlineSelfHosted: true\n" > org.mozilla.fenix-geckoview-config.yaml
adb push org.mozilla.fenix-geckoview-config.yaml /data/local/tmp/
adb shell am set-debug-app --persistent org.mozilla.fenix
adb shell su -c "/data/local/tmp/simpleperf record --call-graph fp --duration 10 -f 1000 --trace-offcpu -e cpu-clock -a -o /data/local/tmp/su-perf.data"
# Run workload.
# ... then:
adb pull /data/local/tmp/su-perf.data
adb shell find /storage/emulated/0/Android/data/org.mozilla.fenix/files '\( -name  jit-* -or -name marker-* \)' -print0 | xargs -0 -I {} adb pull '{}'
adb shell am clear-debug-app
samply import su-perf.data --breakpad-symbol-server https://symbols.mozilla.org/
```

## Limitations

`simpleperf` does not follow subprocesses! Specifically, when you profile in
“app” mode, i.e. using `./app_profiler.py -p org.mozilla.fenix [...]`, then
simpleperf will check which processes belonging to that app are running at the
beginning of profiling, and only profile those processes. It will not notice new
processes that appear during profiling. If no matching process is running at the
start of profiling, simpleperf will [wait for the first matching process to
appear](https://cs.android.com/android/platform/superproject/main/+/main:system/extras/simpleperf/environment.cpp;l=504-549;drc=2bd1c1b20871bcf4ef4660beaa218f2c2bce4630),
and then profile just that first process.

To see processes which are created during profiling, you need to have a rooted
phone and use system-wide profiling.

This means that profiling Firefox startup with `simpleperf` isn’t very usable on
non-rooted phones, unless you are only interested in the parent process.

To work around this limitation we could conceivably pre-launch a bunch of child
processes, sleep for a bit, start `simpleperf`, and then use the pre-launched
processes whenever we need one.
