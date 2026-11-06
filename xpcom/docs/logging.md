# Gecko Logging

A minimal logging framework is provided for use in core Gecko code,
written in C++ and enabled for all builds and is thread-safe.
It can be accessed via C++, JavaScript or Rust.

This page covers enabling logging for particular logging module, configuring
the logging output, and how to use the logging facilities in native code.

## Enabling and configuring logging

### Caveat: sandboxing when logging to a file

Sandboxed content processes (on all OSes) cannot write to files on disk, so it
is recommended to log to the terminal, possibly by redirecting the output to a
file.

If the sandbox has been disabled and/or the logging statement are coming
from the parent process, `MOZ_LOG_FILE` will work as expected. Otherwise,
logging to the terminal works as expected on macOS and Linux on desktop.

On Windows, you can still see child process messages by using DOS (not the
`MOZ_LOG_FILE` variable defined below) to redirect output to a file. For
example: `MOZ_LOG=CameraChild:5 mach run >& my_log_file.txt` will include
debug messages from the camera's child actor that lives in a (sandboxed) content
process.

Another way to do this and have output in the terminal when developing is by
redirecting `stderr` to `stdout` and then `stdout` to another process,
for example like so:

```
MOZ_LOG=cubeb:4 ./mach run 2>&1 | tee
```

### Logging to the Firefox Profiler

When a log statement is logged on a thread and the [Firefox Profiler](https://profiler.firefox.com) is profiling that thread, the log statements is
recorded as a profiler marker.

This allows getting logs alongside profiler markers and lots of performance
and contextual information, in a way that doesn't require disabling the
sandbox, and works across all processes.

The profile can be downloaded and shared e.g. via Bugzilla or email, or
uploaded, and the logging statements will be visible either in the marker chart
or the marker table.

While it is possible to manually configure logging module and start the profiler
with the right set of threads to profile, `about:logging` makes this task a lot
simpler and error-proof.

### The `MOZ_LOG` syntax

Logging is configured using a special but simple syntax: which module should be
enabled, at which level, and what logging options should be enabled or disabled.

The syntax is a list of terms, separated by commas. There are two types of
terms:

- A log module and its level, separated by a colon (`:`), such as
  `example_module:5` to enable the module `example_module` at logging level
  `5` (verbose). This [searchfox query](https://searchfox.org/mozilla-central/search?q=LazyLogModule+.*%5C%28%22&path=&case=true&regexp=true)
  returns the complete list of modules available.
- A special string in the following table, to configure the logging behaviour.
  Some configuration switch take an integer parameter, in which case it's
  separated from the string by a colon (`:`). Most switches only apply in a
  specific output context, noted in the **Context** column.

| Special module name | Context  | Action                                                                                                                                                                                                              |
| ------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| append              | File     | Append new logs to existing log file.                                                                                                                                                                               |
| sync                | File     | Print each log synchronously, this is useful to check behavior in real time or get logs immediately before crash.                                                                                                   |
| raw                 | File     | Print exactly what has been specified in the format string, without the process/thread/timestamp, etc. prefix.                                                                                                      |
| timestamp           | File     | Insert timestamp at start of each log line.                                                                                                                                                                         |
| rotate:**N**        | File     | This limits the produced log files' size. Only most recent **N megabytes** of log datais saved. We rotate four log files with .0, .1, .2, .3 extensions. Note: this optiondisables 'append' and forces 'timestamp'. |
| maxsize:**N**       | File     | Limit the log to **N** MB. Only work in append mode.                                                                                                                                                                |
| prependheader       | File     | Prepend a simple header while distinguishing logging. Useful in append mode.                                                                                                                                        |
| profilerstacks      | Profiler | When profiling with the Firefox Profiler and log modules are enabled, capture the callstack for each log statement.                                                                                                 |
| jsstacks            | Any      | Enables logging JavaScript stack traces when logging JavaScript console messages. (this may be extended outside of only console messages)                                                                           |

For `rotate` and `maxsize`, **N** is clamped to 2047 MB, and 0 or a negative
value disables the option.

This syntax is used for most methods of enabling logging.

### Enabling Logging

Enabling logging can be done in a variety of ways:

- via environment variables
- via command line switches
- using `about:config` preferences
- using `about:logging`

The first two allow logging from the start of the application and are also
useful in case of a crash (when `sync` output is requested, this can also be
done with `about:config` as well to a certain extent). The last two
allow enabling and disabling logging at runtime and don't require using the
command-line.

By default all logging output is disabled.

#### Enabling logging using `about:logging`

`about:logging` allows enabling logging by entering a `MOZ_LOG` string in the
text input, and validating.

Options allow logging to a file or using the Firefox Profiler, that can be
started and stopped right from the page.

Logging presets for common scenarios are available in a drop-down. They can be
associated with a profiler preset.

It is possible, via URL parameters, to select a particular logging
configuration, or to override certain parameters in a preset. This is useful to
ask a user to gather logs efficiently without having to fiddle with prefs and/or
environment variable.

URL parameters are described in the following table:

| Parameter         | Description                                                                           |
| ----------------- | ------------------------------------------------------------------------------------- |
| `preset`          | a [logging preset](https://searchfox.org/mozilla-central/search?q=gLoggingPresets)    |
| `logging-preset`  | alias for `preset`                                                                    |
| `modules`         | a string in `MOZ_LOG` syntax                                                          |
| `module`          | alias for `modules`                                                                   |
| `threads`         | a list of threads to profile, overrides what a profiler preset would have picked      |
| `thread`          | alias for `threads`                                                                   |
| `output`          | either `profiler` or `file`                                                           |
| `output-type`     | alias for `output`                                                                    |
| `profiler-preset` | a [profiler preset](https://searchfox.org/mozilla-central/search?q=%40type+{Presets}) |

If a preset is selected, then `threads` or `modules` can be used to override the
profiled threads or logging modules enabled, but keeping other aspects of the
preset. If no preset is selected, then a generic profiling preset is used,
`firefox-platform`. For example:

```
about:logging?output=profiler&preset=media-playback&modules=cubeb:4,AudioSinkWrapper:4:AudioSink:4
```

will profile the threads in the `Media` profiler preset, but will only log
specific log modules (instead of the [long list](https://searchfox.org/mozilla-central/search?q="media-playback"&path=toolkit%2Fcontent%2FaboutLogging.mjs)
in the `media-playback` preset). In addition, it disallows logging to a file.

#### Enabling logging using environment variables

On UNIX, setting and environment variable can be done in a variety of ways

```
set MOZ_LOG="example_logger:3"
export MOZ_LOG="example_logger:3"
MOZ_LOG="example_logger:3" ./mach run
```

In the Windows Command Prompt (`cmd.exe`), don't use quotes:

```
set MOZ_LOG=example_logger:3
```

If you want this on GeckoView example, use the following adb command to launch process:

```
adb shell am start -n org.mozilla.geckoview_example/.GeckoViewActivity --es env0 "MOZ_LOG=example_logger:3"
```

There are special module names to change logging behavior. You can specify one or more special module names without logging level.

For example, if you want to specify `sync`, `timestamp` and `rotate`:

```
set MOZ_LOG="example_logger:3,timestamp,sync,rotate:10"
```

Enabling logging usually outputs the logging statements to the terminal. To
have the logs written to a file instead (one file per process), the environment
variable `MOZ_LOG_FILE` can be used. Logs will be written at this path
(either relative or absolute), suffixed by a process type and its PID.
`MOZ_LOG` files are text files and have the extension `.moz_log`.

For example, setting:

```
set MOZ_LOG_FILE="firefox-logs"
```

can create a number of files like so:

```
firefox-log-main.96353.moz_log
firefox-log-child.96354.moz_log
```

respectively for a parent process of PID 96353 and a child process of PID
96354\.

#### Enabling logging using command-line flags

The `MOZ_LOG` syntax can be used with the command line switch on the same
name, and specifying a file with `MOZ_LOG_FILE` works in the same way:

```
./mach run -MOZ_LOG=timestamp,rotate:200,example_module:5 -MOZ_LOG_FILE=%TEMP%\firefox-logs
```

will enable verbose (`5`) logging for the module `example_module`, with
timestamp prepended to each line, rotate the logs with 4 files of each 50MB
(for a total of 200MB), and write the output to the temporary directory on
Windows, with name starting with `firefox-logs`.

#### Enabling logging using preferences

To adjust the logging after Firefox has started, you can set prefs under the
`logging.` prefix. For example, setting `logging.foo` to `3` will set the log
module `foo` to start logging at level 3.

A number of special prefs can be set as well, described in the table below:

| Preference name                   | Preference | Preference value              | Description                                                                                        |
| --------------------------------- | ---------- | ----------------------------- | -------------------------------------------------------------------------------------------------- |
| `logging.config.clear_on_startup` | bool       | --                            | Whether to clear all prefs under `logging.`                                                        |
| `logging.config.LOG_FILE`         | string     | A path (relative or absolute) | The path to which the log files will be written.                                                   |
| `logging.config.add_timestamp`    | bool       | --                            | Whether to prefix all lines by a timestamp.                                                        |
| `logging.config.sync`             | bool       | --                            | Whether to flush the stream after each log statements.                                             |
| `logging.config.profilerstacks`   | bool       | --                            | When logging to the Firefox Profiler, whether to include the call stack in each logging statement. |

### Enabling logging in Rust code

We're gradually adding more Rust code to Gecko, and Rust crates typically use a
different approach to logging. Many Rust libraries use the [log](https://docs.rs/log) crate to log messages, which works together with
[env_logger](https://docs.rs/env_logger) at the application level to control
what's actually printed via `RUST_LOG`.

You can set an overall logging level, though it could be quite verbose:

```
set RUST_LOG="debug"
```

You can also target individual modules by path:

```
set RUST_LOG="style::style_resolver=debug"
```

:::{note}
For Linux/MacOS users, you need to use `export` rather than `set`.
:::

:::{note}
Sometimes it can be useful to only log child processes and ignore the parent

: process. In Firefox 57 and later, you can use `RUST_LOG_CHILD` instead of
  `RUST_LOG` to specify log settings that will only apply to child processes.
:::

The `log` crate lists the available [log levels](https://docs.rs/log/0.3.8/log/enum.LogLevel.html):

| Log Level | Purpose                                                             |
| --------- | ------------------------------------------------------------------- |
| error     | Designates very serious errors.                                     |
| warn      | Designates hazardous situations.                                    |
| info      | Designates useful information.                                      |
| debug     | Designates lower priority information.                              |
| trace     | Designates very low priority, often extremely verbose, information. |

It is common for debug and trace to be disabled at compile time in release builds, so you may need a debug build if you want logs from those levels.

Check the [env_logger](https://docs.rs/env_logger) docs for more details on logging options.

Additionally, a mapping from `RUST_LOG` is available. When using the `MOZ_LOG`
syntax, it is possible to enable logging in rust crate using a similar syntax:

```
MOZ_LOG=rust_crate_name::*:4
```

will enable `debug` logging for all log statements in the crate
`rust_crate_name`.

`*` can be replaced by a series of modules if more specificity is needed:

```
MOZ_LOG=rust_crate_name::module::submodule:4
```

will enable `debug` logging for all log statements in the sub-module
`submodule` of the module `module` of the crate `rust_crate_name`.

A table mapping Rust log levels to `MOZ_LOG` log level is available below:

| Rust log level | MOZ_LOG level | Numerical value |
| -------------- | ------------- | --------------- |
| off            | Disabled      | 0               |
| error          | Error         | 1               |
| warn           | Warning       | 2               |
| info           | Info          | 3               |
| debug          | Debug         | 4               |
| trace          | Verbose       | 5               |

### Enabling logging on Android, interleaved with system logs (`logcat`)

While logging to the Firefox Profiler works it's sometimes useful to have
system logs (`adb logcat`) interleaved with application logging. With a
device (or emulator) that `adb devices` sees, it's possible to set
environment variables like so, for e.g. `GeckoView_example`:

```sh
adb shell am start -n org.mozilla.geckoview_example/.GeckoViewActivity --es env0 MOZ_LOG=MediaDemuxer:4
```

It is then possible to see the logging statements like so, to display all logs,
including `MOZ_LOG`:

```sh
adb logcat
```

and to only see `MOZ_LOG` like so:

```sh
adb logcat Gecko:V '*:S'
```

This expression means: print log module `Gecko` from log level `Verbose`
(lowest level, this means that all levels are printed), and filter out (`S`
for silence) all other logging (`*`, be careful to quote it or escape it
appropriately, it so that it's not expanded by the shell).

While interactive with e.g. `GeckoView` code, it can be useful to specify
more logging tags like so:

```sh
adb logcat GeckoViewActivity:V Gecko:V '*:S'
```

### Enabling logging on Android, using the Firefox Profiler

Set the logging modules using `about:config` (this requires a Nightly build)
using the instructions outlined above, and start the profile using an
appropriate profiling preset to profile the correct threads using the instructions
written in Firefox Profiler documentation's [dedicated page](https://profiler.firefox.com/docs/#/./guide-profiling-android-directly-on-device).

[Bug 1803607](https://bugzilla.mozilla.org/show_bug.cgi?id=1803607) tracks
improving the logging experience on mobile.

## Working with `MOZ_LOG` in the code

### Declaring a Log Module

`LazyLogModule` defers the creation the backing `LogModule` in a thread-safe manner and is the preferred method to declare a log module. Multiple `LazyLogModules` with the same name can be declared, all will share the same backing `LogModule`. This makes it much simpler to share a log module across multiple translation units. `LazyLogLodule` provides a conversion operator to `LogModule*` and is suitable for passing into the logging macros detailed below.

Note: Log module names can only contain specific characters. The first character must be a lowercase or uppercase ASCII char, underscore, dash, or dot. Subsequent characters may be any of those, or an ASCII digit.

```cpp
#include "mozilla/Logging.h"

static mozilla::LazyLogModule sFooLog("foo");
```

### Logging interface

A basic interface is provided in the form of 2 macros and an enum class.

```{eval-rst}
+----------------------------------------+----------------------------------------------------------------------------+
| MOZ_LOG(module, level, message)        | Outputs the given message if the module has the given log level enabled:   |
|                                        |                                                                            |
|                                        | *   module: The log module to use.                                         |
|                                        | *   level: The log level of the message.                                   |
|                                        | *   message: A printf-style message to output. Must be enclosed in         |
|                                        |     parentheses.                                                           |
+----------------------------------------+----------------------------------------------------------------------------+
| MOZ_LOG_FMT(module, level, message)    | Outputs the given message if the module has the given log level enabled:   |
|                                        |                                                                            |
|                                        | *   module: The log module to use.                                         |
|                                        | *   level: The log level of the message.                                   |
|                                        | *   message: An {fmt} style message to output.                             |
+----------------------------------------+----------------------------------------------------------------------------+
| MOZ_LOG_TEST(module, level)            | Checks if the module has the given level enabled:                          |
|                                        |                                                                            |
|                                        | *    module: The log module to use.                                        |
|                                        | *    level: The output level of the message.                               |
+----------------------------------------+----------------------------------------------------------------------------+
```

| Log Level | Numeric Value | Purpose                                                                                                       |
| --------- | ------------- | ------------------------------------------------------------------------------------------------------------- |
| Disabled  | 0             | Indicates logging is disabled. This should not be used directly in code.                                      |
| Error     | 1             | An error occurred, generally something you would consider asserting in a debug build.                         |
| Warning   | 2             | A warning often indicates an unexpected state.                                                                |
| Info      | 3             | An informational message, often indicates the current program state.                                          |
| Debug     | 4             | A debug message, useful for debugging but too verbose to be turned on normally.                               |
| Verbose   | 5             | A message that will be printed a lot, useful for debugging program flow and will probably impact performance. |

### Example Usage

```cpp
#include "mozilla/Logging.h"

using mozilla::LogLevel;

static mozilla::LazyLogModule sLogger("example_logger");

static void DoStuff()
{
  MOZ_LOG(sLogger, LogLevel::Info, ("Doing stuff."));

  int i = 0;
  int start = Time::NowMS();
  MOZ_LOG(sLogger, LogLevel::Debug, ("Starting loop."));
  while (i++ &lt; 10) {
    MOZ_LOG(sLogger, LogLevel::Verbose, ("i = %d", i));
  }

  // Only calculate the elapsed time if the Warning level is enabled.
  if (MOZ_LOG_TEST(sLogger, LogLevel::Warning)) {
    int elapsed = Time::NowMS() - start;
    if (elapsed &gt; 1000) {
      MOZ_LOG(sLogger, LogLevel::Warning, ("Loop took %dms!", elapsed));
    }
  }

  if (i != 10) {
    MOZ_LOG(sLogger, LogLevel::Error, ("i should be 10!"));
  }
}
```

## Logging from JavaScript via the `console` API

Any call made to a `console` API from JavaScript will be logged through the
`MOZ_LOG` pipeline.

- Web Pages as well as privileged context using `console` API expose to
  JavaScript will automatically generate MOZ_LOG messages under the `console`
  module name.
- Privileged context can use a specific module name by instantiating their own
  console object:
  `const logger = console.createInstance({ logModule: "module-name" })`,
  `logModule` value will be used as the MOZ_LOG module name.

More info about `console.createInstance` is available on the
[JavaScript Logging page](/toolkit/javascript-logging.md)

When using the `console` API, the console methods calls will be visible
in the Developer Tools, as well as through MOZ_LOG stdout, file or profiler
outputs.

```javascript
// The following two logs can be visible through MOZ_LOG by using:
// MOZ_LOG=console:5

// Both call will be logged through "console" module name.
// Any console API call from privileged or content page will be logged.
console.log("Doing stuff.");

console.error("Error happened");

// The following two other logs can be visible through MOZ_LOG by using:
// MOZ_LOG=example_logger:5

// From a privileged context, you can instantiate your own console object
// with a specific module name, here "example_logger":
const logger = console.createInstance({ logModule: "example_logger" });

logger.warn("something failed");

logger.debug("some debug info");
```

To avoid unnecessarily computing expensive log-only strings, use
`console.shouldLog()` to check if a given log level would actually be logged
based on either the `maxLogLevel{Pref}` setting from `createInstance` or
the current `MOZ_LOG` environment variable.

```javascript
const logger = console.createInstance({
  logModule: "example_logger",
  maxLogLevel: "Warn",
});

if (logger.shouldLog("Debug")) {
  logger.debug("Expensive computation:", computeExpensiveDebugInfo());
}
```

## Logging from Java

In GeckoView, the Java code can log messages using the `org.mozilla.gecko.MozLog` class.

```java
import org.mozilla.gecko.MozLog;

public class Example {
    public void doStuff() {
        final String MODULE = "GeckoSample";
        MozLog.d(MODULE, "Doing stuff");
        MozLog.w(MODULE, "Warning");
        MozLog.e(MODULE, "Error happened");
    }
}
```

## Logging web page errors and warnings

Any error or warning message sent to the `nsConsoleService` C++ class can be
logged via the `PageMessages` module name.

These messages are typically emitted via `nsContentUtils::ReportToConsole*()`
or `nsContentUtils::LogMessageToConsole()` methods.

They includes any JavaScript exception and most DOM API warnings and errors.

### Console API levels

| Console API Method                     | MOZ_LOG Level |
| -------------------------------------- | ------------- |
| console.error() console.assert()       | 1 (Error)     |
| console.warn()                         | 2 (Warning)   |
| All other methods, but console.debug() | 3 (Info)      |
| console.debug()                        | 4 (Debug      |
