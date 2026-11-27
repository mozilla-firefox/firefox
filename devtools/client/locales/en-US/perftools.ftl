# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### These strings are used in DevTools’ performance-new panel, about:profiling, and
### the remote profiling panel. There are additional profiler strings in the appmenu.ftl
### file that are used for the profiler popup.

perftools-intro-title = Profiler Settings
perftools-intro-description =
  Recordings launch profiler.firefox.com in a new tab. All data is stored
  locally, but you can choose to upload it for sharing.

## All of the headings for the various sections.

perftools-heading-settings = Full Settings
perftools-heading-buffer = Buffer Settings
perftools-heading-features = Features
perftools-heading-features-default = Features (Recommended on by default)
perftools-heading-features-disabled = Disabled Features
perftools-heading-features-experimental = Experimental
perftools-heading-threads = Threads
perftools-heading-threads-jvm = JVM Threads
perftools-heading-local-build = Local build

##

perftools-description-intro =
  Recordings launch <a>profiler.firefox.com</a> in a new tab. All data is stored
  locally, but you can choose to upload it for sharing.
perftools-description-local-build =
  If you’re profiling a build that you have compiled yourself, on this
  machine, please add your build’s objdir to the list below so that
  it can be used to look up symbol information.
perftools-pick-local-build-directory = Pick build directory

## The controls for the interval at which the profiler samples the code.

perftools-range-interval-label = Sampling interval:
perftools-range-interval-milliseconds = {NUMBER($interval, maxFractionalUnits: 2)} ms

## Generic memory units that can be used in various places, eg for the buffer size.

# Byte
perftools-memory-unit-b = { NUMBER($num, maxFractionalUnits: 2) } B
# Kibibyte
perftools-memory-unit-kib = { NUMBER($num, maxFractionalUnits: 2) } KiB
# Mebibyte
perftools-memory-unit-mib = { NUMBER($num, maxFractionalUnits: 2) } MiB
# Gibibyte
perftools-memory-unit-gib = { NUMBER($num, maxFractionalUnits: 2) } GiB
# Tebibyte
perftools-memory-unit-tib = { NUMBER($num, maxFractionalUnits: 2) } TiB
# Pebibyte
perftools-memory-unit-pib = { NUMBER($num, maxFractionalUnits: 2) } PiB
# Exbibyte
perftools-memory-unit-eib = { NUMBER($num, maxFractionalUnits: 2) } EiB
# Zebibyte
perftools-memory-unit-zib = { NUMBER($num, maxFractionalUnits: 2) } ZiB
# Yobibyte
perftools-memory-unit-yib = { NUMBER($num, maxFractionalUnits: 2) } YiB

##

# The size of the memory buffer used to store things in the profiler.
perftools-range-entries-label = Buffer size:

perftools-custom-threads-label = Add custom threads by name:

perftools-devtools-interval-label = Interval:
perftools-devtools-threads-label = Threads:
perftools-devtools-settings-label = Settings

## Various statuses that affect the current state of profiling, not typically displayed.

perftools-status-recording-stopped-by-another-tool = The recording was stopped by another tool.
perftools-status-restart-required = The browser must be restarted to enable this feature.

## These are shown briefly when the user is waiting for the profiler to respond.

perftools-request-to-stop-profiler = Stopping recording
perftools-request-to-get-profile-and-stop-profiler = Capturing profile

##

perftools-button-start-recording = Start recording
perftools-button-capture-recording = Capture recording
perftools-button-cancel-recording = Cancel recording
perftools-button-save-settings = Save settings and go back
perftools-button-restart = Restart
perftools-button-add-directory = Add a directory
perftools-button-remove-directory = Remove selected
perftools-button-edit-settings = Edit Settings…

## More actions menu

perftools-menu-more-actions-button =
  .title = More actions
perftools-menu-more-actions-restart-with-profiling = Restart { -brand-shorter-name } with startup profiling enabled
perftools-menu-more-actions-copy-for-startup = Copy environment variables for startup profiling
perftools-menu-more-actions-copy-for-perf-tests = Copy parameters for performance tests

## These messages are descriptions of the threads that can be enabled for the profiler.

perftools-thread-gecko-main =
  .title = The main processes for both the parent process, and content processes
perftools-thread-compositor =
  .title = Composites together different painted elements on the page
perftools-thread-dom-worker =
  .title = This handles both web workers and service workers
perftools-thread-renderer =
  .title = When WebRender is enabled, the thread that executes OpenGL calls
perftools-thread-render-backend =
  .title = The WebRender RenderBackend thread
perftools-thread-timer =
  .title = The thread handling timers (setTimeout, setInterval, nsITimer)
perftools-thread-style-thread =
  .title = Style computation is split into multiple threads
pref-thread-stream-trans =
  .title = Network stream transport
perftools-thread-socket-thread =
  .title = The thread where networking code runs any blocking socket calls
perftools-thread-jxl-img-decode =
  .title = JPEG XL image decoding threads
perftools-thread-dns-resolver =
  .title = DNS resolution happens on this thread
perftools-thread-task-controller =
  .title = TaskController thread pool threads
perftools-thread-jvm-gecko =
  .title = The main Gecko JVM thread
perftools-thread-jvm-nimbus =
  .title = The main threads for the Nimbus experiments SDK
perftools-thread-jvm-default-dispatcher =
  .title = The Default dispatcher for the Kotlin coroutines library
perftools-thread-jvm-glean =
  .title = The main threads for the Glean telemetry SDK
perftools-thread-jvm-arch-disk-io =
  .title = The IO dispatcher for the Kotlin coroutines library
perftools-thread-jvm-pool =
  .title = Threads created in an unnamed thread pool

##

perftools-record-all-registered-threads =
  Bypass selections above and record all registered threads

perftools-tools-threads-input-label =
  .title = These thread names are a comma separated list that is used to enable profiling of the threads in the profiler. The name needs to be only a partial match of the thread name to be included. It is whitespace sensitive.

## Onboarding UI labels. These labels are displayed in the new performance panel UI, when
## devtools.performance.new-panel-onboarding preference is true.

perftools-onboarding-message = <b>New</b>: { -profiler-brand-name } is now integrated into Developer Tools. <a>Learn more</a> about this powerful new tool.

perftools-onboarding-close-button =
  .aria-label = Close the onboarding message

## Profiler presets

# Presets and their l10n IDs are defined in the file
# devtools/client/performance-new/shared/background.sys.mjs
# The same labels and descriptions are also defined in appmenu.ftl.

perftools-presets-web-developer-label = Web Developer
perftools-presets-web-developer-description = Recommended preset for most web app debugging, with low overhead.

perftools-presets-firefox-label = { -brand-shorter-name }
perftools-presets-firefox-description = Recommended preset for profiling { -brand-shorter-name }.

perftools-presets-benchmark-label = Benchmark
perftools-presets-benchmark-description = Preset for investigating { -brand-shorter-name } performance on benchmarks such as Speedometer, with low overhead.

perftools-presets-graphics-label = Graphics
perftools-presets-graphics-description = Preset for investigating graphics bugs in { -brand-shorter-name }.

perftools-presets-media-label = Media
perftools-presets-media-description2 = Preset for investigating audio and video bugs in { -brand-shorter-name }.

perftools-presets-ml-label = Machine Learning
perftools-presets-ml-description2 = Preset for investigating machine learning bugs in { -brand-shorter-name }.

perftools-presets-networking-label = Networking
perftools-presets-networking-description = Preset for investigating networking bugs in { -brand-shorter-name }.

perftools-presets-networking-with-logs-label = Networking with Logs
perftools-presets-networking-with-logs-description = Preset for investigating networking bugs in { -brand-shorter-name }, including networking logs. These logs may contain sensitive information such as the URLs you visit.

# "Power" is used in the sense of energy (electricity used by the computer).
perftools-presets-power-label = Power
perftools-presets-power-description = Preset for investigating power use bugs in { -brand-shorter-name }, with low overhead.

perftools-presets-debug-label = Debug
perftools-presets-debug-description = Preset for debugging in { -brand-shorter-name }. High overhead, do not use for performance work but use for focusing on understanding browser behavior.

perftools-presets-web-compat-label = Web Compat
perftools-presets-web-compat-description = Recommended preset for debugging web compatibility issues in websites, rather than tracking performance.

perftools-presets-custom-label = Custom

##
