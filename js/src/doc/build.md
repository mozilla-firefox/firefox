# Building and testing SpiderMonkey

**The first step is to run our “bootstrap” script to help ensure you have the
right build tools for your operating system. This will also help you get a copy
of the source code. You do not need to run the “mach build” command just yet
though.**

- {doc}`/setup/linux_build`
- {doc}`/setup/windows_build`
- {doc}`/setup/macos_build`

This guide shows you how to build SpiderMonkey using `mach`, which is
Mozilla's multipurpose build tool. This replaces old guides that advised
running the "configure" script directly.

These instructions assume you have a clone of the
[Firefox repository](https://github.com/mozilla-firefox/firefox) and are
interested in building the JS shell.

## Developer (debug) build

For developing and debugging SpiderMonkey itself, it is best to have
both a debug build (for everyday debugging) and an optimized build (for
performance testing), in separate build directories. We'll start by
covering how to create a debug build.

### Setting up a MOZCONFIG

First, we will create a `MOZCONFIG` file. This file describes the characteristics
of the build you'd like `mach` to create. Since it is likely you will have a
couple of `MOZCONFIGs`, a directory like `$HOME/mozconfigs` is a useful thing to
have.

A basic `MOZCONFIG` file for doing a debug build, put into `$HOME/mozconfigs/debug` looks like this

```text
# Build only the JS shell
ac_add_options --enable-project=js

# Enable the debugging tools: Assertions, debug only code etc.
ac_add_options --enable-debug

# Enable optimizations as well so that the test suite runs much faster. If
# you are having trouble using a debugger, you should disable optimization.
ac_add_options --enable-optimize

# Use a dedicated objdir for SpiderMonkey debug builds to avoid
# conflicting with Firefox build with default configuration.
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-debug-@CONFIG_GUESS@
```

To activate a particular `MOZCONFIG`, set the environment variable:

```text
export MOZCONFIG=$HOME/mozconfigs/debug
```

### Building

Once you have activated a `MOZCONFIG` by setting the environment variable
you can then ask `mach`, located in the top directory of your checkout,
to do your build:

```console
$ cd <path to mozilla-central>
$ ./mach build
```

:::{note}
If you are on Mac and baldrdash fails to compile with something similar to

```
/usr/local/Cellar/llvm/7.0.1/lib/clang/7.0.1/include/inttypes.h:30:15: fatal error: 'inttypes.h' file not found
```

This is because, starting from Mojave, headers are no longer
installed in `/usr/include`. Refer the [release
notes](https://developer.apple.com/documentation/xcode_release_notes/xcode_10_release_notes) under
Command Line Tools -> New Features

The release notes also states that this compatibility package will no longer be provided in the near
future, so the build system on macOS will have to be adapted to look for headers in the SDK

Until then, the following should help,

```
open /Library/Developer/CommandLineTools/Packages/macOS_SDK_headers_for_macOS_10.14.pk
```
:::

Once you have successfully built the shell, you can run it using `mach run`.

## Testing

Once built, you can then use `mach` to run the `jit-tests`:

```console
$ ./mach jit-test
```

Similarly you can use also run `jstests`. These include a local,
intermittently updated, copy of all [test262](https://github.com/tc39/test262/)
tests.

```console
$ ./mach jstests
```

See {doc}`Running Automated JavaScript Tests<test>` for more details.

## Optimized Builds

To switch to an optimized build, such as for performance testing, one need only
have an optimized build `MOZCONFIG`, and then activate it. An example
`$HOME/mozconfigs/optimized` `MOZCONFIG` looks like this:

```text
# Build only the JS shell
ac_add_options --enable-project=js

# Enable optimization for speed
ac_add_options --enable-optimize

# Disable debug checks to better match a release build of Firefox.
ac_add_options --disable-debug

# Use a separate objdir for optimized builds to allow easy
# switching between optimized and debug builds while developing.
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-opt-@CONFIG_GUESS@
```

## SpiderMonkey on Android aarch64

### Building SpiderMonkey on Android

- First, run `mach bootstrap` and answer `GeckoView/Firefox for Android` when
  asked which project you want to build. This will download a recent Android
  NDK, make sure all the build dependencies required to compile on Android are
  present, etc.
- Make sure that `$MOZBUILD_DIR/android-sdk-linux/platform-tools` is present in
  your `PATH` environment. You can do this by running the following line in a
  shell, or adding it to a shell profile init file:

```console
$ export PATH="$PATH:~/.mozbuild/android-sdk-linux/platform-tools"
```

- Create a typical `mozconfig` file for compiling SpiderMonkey, as outlined in
  the {ref}`mozconfig` documentation, and include the following
  line:

```console
$ ac_add_options --target=aarch64-linux-android
```

- Then compile as usual with `mach build` with this `MOZCONFIG` file.

The compiled shell can be installed to `/data/local/tmp` on the device
using `./mach install`.

### Running jit-tests on Android

- Plug your Android device to the machine which compiled the shell for aarch64
  as described above, or make sure it is on the same subnetwork as the host. It
  should appear in the list of devices seen by `adb`:

```console
$ adb devices
```

This command should show you a device ID with the name of the device. If it
doesn't, make sure that you have enabled Developer options on your device, as
well as [enabled USB debugging on the device](https://developer.android.com/studio/debug/dev-options).

- Run `mach jit-test --remote {JIT_TEST_ARGS}` with the android-aarch64
  `MOZCONFIG` file. This will upload the JS shell and its dependencies to the
  Android device, in a temporary directory (`/data/local/tmp/test_root/bin` as
  of 2020-09-02). Then it will start running the jit-test suite.

### Debugging jit-tests on Android

Debugging on Android uses the GDB remote debugging protocol, so we'll set up a
GDB server on the Android device, that is going to be controlled remotely by
the host machine.

- Upload the `gdbserver` precompiled binary from the NDK from the host machine
  to the Android device, using this command on the host:

```console
$ adb push \
    ~/.mozbuild/android-ndk-r23c/prebuilt/android-arm64/gdbserver/gdbserver \
    /data/local/tmp/test_root/bin
```

- Make sure that the `ncurses5` library is installed on the host. On
  Debian-like distros, this can be done with `sudo apt install -y libncurses5`.
- Set up port forwarding for the GDB port, from the Android device to the host,
  so we can connect to a local port from the host, without needing to find what
  the IP address of the Android device is:

```console
$ adb forward tcp:5039 tcp:5039
```

- Start `gdbserver` on the phone, passing the JS shell command line arguments
  to gdbserver:

```console
$ adb shell export LD_LIBRARY_PATH=/data/local/tmp/test_root/bin '&&' /data/local/tmp/test_root/bin/gdbserver :5039 /data/local/tmp/test_root/bin/js /path/to/test.js
```

:::{note}
Note this will make the gdbserver listen on the 5039 port on all the
network interfaces. In particular, the gdbserver will be reachable from
every other devices on the same networks as your phone. Since the gdbserver
protocol is unsafe, it is strongly recommended to double-check that the
gdbserver process has properly terminated when exiting the shell, and to
not run it more than needed.
:::

:::{note}
You can find the full command line that the `jit_test.py` script is
using by giving it the `-s` parameter, and copy/paste it as the final
argument to the gdbserver invocation above.
:::

- On the host, start the precompiled NDK version of GDB that matches your host
  architecture, passing it the path to the shell compiled with `mach` above:

```console
$ ~/.mozbuild/android-ndk-r23c/prebuilt/linux-x86_64/bin/gdb /path/to/objdir-aarch64-linux-android/dist/bin/js
```

- Then connect remotely to the GDB server that's listening on the Android
  device:

```console
$(gdb) target remote :5039
$(gdb) continue
```
