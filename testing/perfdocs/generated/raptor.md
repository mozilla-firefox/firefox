# Raptor

Raptor is a performance-testing framework for running browser pageload and browser benchmark tests. Raptor is cross-browser compatible and is currently running in production on Firefox Desktop, Firefox Android GeckoView, Fenix, Reference Browser, Chromium, Chrome, Chrome for Android, Safari, and Safari Technology Preview.

- Contact: Dave Hunt [:davehunt]
- Source code: {searchfox}`testing/raptor`
- Good first bugs: <https://codetribute.mozilla.org/projects/automation?project%3DRaptor>

Raptor currently supports three test types: 1) page-load performance tests, 2) standard benchmark-performance tests, and 3) "scenario"-based tests, such as power, CPU, and memory-usage measurements on Android and Desktop.

Locally, Raptor can be invoked with the following command:

```
./mach raptor
```

```{toctree}
:titlesonly: true

browsertime
debugging
contributing
test-list
webextension
raptor-metrics
```

```{contents}
:depth: 2
:local: true
```

## Raptor tests

The following documents all testing we have for Raptor.

### Benchmarks

Standard benchmarks are third-party tests (i.e. Speedometer) that we have integrated into Raptor to run per-commit in our production CI. To update any of these benchmarks, see [Updating Benchmark Tests](browsertime.md#updating-benchmark-tests).


(ares6-b)=

::::{dropdown} ares6
:class-container: anchor-id-ares6-b

* **Command to Run Locally**

  ```
  ./mach raptor -t ares6
  ```

**Owner**: :jandem and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/ares6.toml#16`
* **lower is better**: true
* **page cycles**: 4
* **page timeout**: 270000
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/ARES-6/index.html?raptor>
* **type**: benchmark
* **unit**: ms
::::

(assorted-dom-b)=

::::{dropdown} assorted-dom
:class-container: anchor-id-assorted-dom-b

* **Command to Run Locally**

  ```
  ./mach raptor -t assorted-dom
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/assorted-dom.toml#16`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/assorted-dom
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **screen capture**: true
* **sparse checkout**: true
* **test url**: <http://\<host\>:\<port\>/assorted/driver.html?raptor>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-assorted-dom**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-assorted-dom**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(jetstream2-b)=

::::{dropdown} jetstream2
:class-container: anchor-id-jetstream2-b

* **Command to Run Locally**

  ```
  ./mach raptor -t jetstream2
  ```

**Owner**: :jandem and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, chrome-m, cstm-car-m
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/jetstream2-mobile.toml#17`
* **lower is better**: false
* **page cycles**: 4
* **page timeout**: 2000000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/JetStream2
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: score
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: score
**Owner**: :jandem and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/jetstream2.toml#17`
* **lower is better**: false
* **page cycles**: 4
* **page timeout**: 2000000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/JetStream2
* **repository revision**: 6acd3f58f12dbd71cf4743666cad305862681630
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: score
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: score
::::

(jetstream3-b)=

::::{dropdown} jetstream3
:class-container: anchor-id-jetstream3-b

* **Command to Run Locally**

  ```
  ./mach raptor -t jetstream3
  ```

**Owner**: :jandem and spidermonkey team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, safari-tp, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **expose chrome trace**: true
* **fetch path**: pgo-extended-corpus/JetStream
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/jetstream3-desktop.toml#17`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 2000000
* **repository**: https://github.com/webkit/jetstream
* **repository revision**: 3967678fa8ab98d847ab33cf3728dba726fa854b
* **subtest alert threshold**: 5.0
* **subtest lower is better**: true
* **subtest unit**: ms
* **suite name**: JetStream3.0
* **support class**: jetstream3.py
* **test script**: jetstream3.js
* **test url**: <http://\<host\>:\<port\>/>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-tp-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: :jandem and spidermonkey team

* **alert threshold**: 2.0
* **apps**: chrome-m, cstm-car-m, fenix
* **expected**: pass
* **expose browser profiler**: true
* **fetch path**: pgo-extended-corpus/JetStream
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/jetstream3-mobile.toml#22`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 2000000
* **preferences**: dom.max_script_run_time=0
* **repository**: https://github.com/webkit/jetstream
* **repository revision**: 3967678fa8ab98d847ab33cf3728dba726fa854b
* **subtest alert threshold**: 5.0
* **subtest lower is better**: true
* **subtest unit**: ms
* **suite name**: JetStream3.0
* **support class**: jetstream3.py
* **test script**: jetstream3.js
* **test url**: <http://\<host\>:\<port\>/>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-tp-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(matrix-react-bench-b)=

::::{dropdown} matrix-react-bench
:class-container: anchor-id-matrix-react-bench-b

* **Command to Run Locally**

  ```
  ./mach raptor -t matrix-react-bench
  ```

**Owner**: :jandem and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/matrix-react-bench.toml#17`
* **lower is better**: true
* **page cycles**: 30
* **page timeout**: 2000000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/matrix-react-bench
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/matrix_demo.html>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-matrix-react-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(media-capabilities-b)=

::::{dropdown} media-capabilities
:class-container: anchor-id-media-capabilities-b

* **Command to Run Locally**

  ```
  ./mach raptor -t media-capabilities
  ```

**Owner**: :az and Media Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **browser cycles**: 5
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/media-capabilities.toml#12`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 360000
* **preferences**: privacy.reduceTimerPrecision=false privacy.reduceTimerPrecision.unconditional=false
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: media_capabilities.py
* **test url**: <http://\<host\>:\<port\>/media-capabilities/index.html?raptor>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(motionmark-1-3-b)=

::::{dropdown} motionmark-1-3
:class-container: anchor-id-motionmark-1-3-b

* **Command to Run Locally**

  ```
  ./mach raptor -t motionmark-1-3
  ```

**Owner**: Graphics Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **browser cycles**: 1
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **expose chrome trace**: true
* **fetch path**: motionmark
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-desktop.toml#13`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 600000
* **repository**: https://github.com/webkit/motionmark
* **repository revision**: be2a5fea89b6ef411b053ebeb95a6302b3dc0ecb
* **subtest lower is better**: false
* **subtest unit**: score
* **suite name**: MotionMark
* **support class**: motionmark-1-3.py
* **test script**: motionmark-1-3.js
* **test url**: <http://\<host\>:\<port\>/MotionMark/developer.html?warmup-length=2000&warmup-frame-count=30&first-frame-minimum-length=0&test-interval=30&display=minimal&tiles=big&controller=ramp&system-frame-rate=60&frame-rate=60&time-measurement=performance>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: Graphics Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, chrome-m, cstm-car-m
* **browser cycles**: 1
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **fetch path**: motionmark
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-mobile.toml#12`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 600000
* **repository**: https://github.com/webkit/motionmark
* **repository revision**: be2a5fea89b6ef411b053ebeb95a6302b3dc0ecb
* **subtest lower is better**: false
* **subtest unit**: score
* **suite name**: MotionMark
* **support class**: motionmark-1-3.py
* **test script**: motionmark-1-3.js
* **test url**: <http://\<host\>:\<port\>/MotionMark/developer.html?warmup-length=2000&warmup-frame-count=30&first-frame-minimum-length=0&test-interval=30&display=minimal&tiles=big&controller=ramp&system-frame-rate=60&frame-rate=60&time-measurement=performance>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(motionmark-htmlsuite-1-3-b)=

::::{dropdown} motionmark-htmlsuite-1-3
:class-container: anchor-id-motionmark-htmlsuite-1-3-b

* **Command to Run Locally**

  ```
  ./mach raptor -t motionmark-htmlsuite-1-3
  ```

**Owner**: Graphics Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **browser cycles**: 1
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **expose chrome trace**: true
* **fetch path**: motionmark
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-desktop.toml#32`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 600000
* **repository**: https://github.com/webkit/motionmark
* **repository revision**: be2a5fea89b6ef411b053ebeb95a6302b3dc0ecb
* **subtest lower is better**: false
* **subtest unit**: score
* **suite name**: HTML suite
* **support class**: motionmark-1-3.py
* **test script**: motionmark-1-3.js
* **test url**: <http://\<host\>:\<port\>/MotionMark/developer.html?warmup-length=2000&warmup-frame-count=30&first-frame-minimum-length=0&test-interval=30&display=minimal&tiles=big&controller=ramp&system-frame-rate=60&frame-rate=60&time-measurement=performance>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: Graphics Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, chrome-m, cstm-car-m
* **browser cycles**: 1
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **fetch path**: motionmark
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-mobile.toml#31`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 600000
* **repository**: https://github.com/webkit/motionmark
* **repository revision**: be2a5fea89b6ef411b053ebeb95a6302b3dc0ecb
* **subtest lower is better**: false
* **subtest unit**: score
* **suite name**: HTML suite
* **support class**: motionmark-1-3.py
* **test script**: motionmark-1-3.js
* **test url**: <http://\<host\>:\<port\>/MotionMark/developer.html?warmup-length=2000&warmup-frame-count=30&first-frame-minimum-length=0&test-interval=30&display=minimal&tiles=big&controller=ramp&system-frame-rate=60&frame-rate=60&time-measurement=performance>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(speedometer-experimental-b)=

::::{dropdown} speedometer-experimental
:class-container: anchor-id-speedometer-experimental-b

* **Command to Run Locally**

  ```
  ./mach raptor -t speedometer-experimental
  ```

**Owner**: Performance Team

**Description**: Runs the experimental Speedometer workloads from a branch of a Speedometer fork on selected desktop platforms.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, safari-tp, custom-car
* **benchmark port**: 62764
* **browser cycles**: 5
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **fetch path**: Speedometer
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **host from parent**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-desktop.toml#33`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 600000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **repository**: https://github.com/mozilla/Speedometer
* **repository revision**: 11aa6278897ad5fe33c28df9099604dc07a90352
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: speedometer3.py
* **test script**: speedometer3.js
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&tags=sp4>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer-experimental**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer-experimental-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(speedometer2-b)=

::::{dropdown} speedometer2
:class-container: anchor-id-speedometer2-b

* **Command to Run Locally**

  ```
  ./mach raptor -t speedometer2
  ```

**Owner**: SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, safari-tp, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-desktop.toml#19`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 600000
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/Speedometer/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, refbrow, chrome-m, cstm-car-m
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks,java
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-mobile.toml#19`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 420000
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/Speedometer/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(speedometer3-b)=

::::{dropdown} speedometer3
:class-container: anchor-id-speedometer3-b

* **Command to Run Locally**

  ```
  ./mach raptor -t speedometer3
  ```

**Owner**: Performance Team

* **alert threshold**: 0.5
* **apps**: firefox, chrome, safari, safari-tp, custom-car
* **benchmark port**: 62763
* **browser cycles**: 5
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-desktop.toml#21`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 600000
* **subtest alert threshold**: 2.0
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: speedometer3.py
* **test script**: speedometer3.js
* **test url**: <http://\<host\>:\<port\>/Speedometer3/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-nightlyasrelease/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-nightlyasrelease/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-nightlyasrelease/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-tp-speedometer3**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: Performance Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, refbrow, chrome-m, cstm-car-m
* **benchmark port**: 62763
* **browser cycles**: 5
* **cold**: true
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks,java
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-mobile.toml#21`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 420000
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: speedometer3.py
* **test script**: speedometer3.js
* **test url**: <http://\<host\>:\<port\>/Speedometer3/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-nightlyasrelease/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-nightlyasrelease/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-nightlyasrelease/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-fenix-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-geckoview-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-safari-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-tp-speedometer3**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-chrome-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-speedometer3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-native-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(stylebench-b)=

::::{dropdown} stylebench
:class-container: anchor-id-stylebench-b

* **Command to Run Locally**

  ```
  ./mach raptor -t stylebench
  ```

**Owner**: :emilio and Layout Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/stylebench.toml#16`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 140000
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/StyleBench/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-stylebench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-stylebench**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(sunspider-b)=

::::{dropdown} sunspider
:class-container: anchor-id-sunspider-b

* **Command to Run Locally**

  ```
  ./mach raptor -t sunspider
  ```

**Owner**: :jandem and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/sunspider.toml#10`
* **lower is better**: true
* **page cycles**: 5
* **page timeout**: 55000
* **test url**: <http://\<host\>:\<port\>/SunSpider/sunspider-1.0.1/sunspider-1.0.1/driver.html?raptor>
* **type**: benchmark
* **unit**: ms
::::

(twitch-animation-b)=

::::{dropdown} twitch-animation
:class-container: anchor-id-twitch-animation-b

* **Command to Run Locally**

  ```
  ./mach raptor -t twitch-animation
  ```

**Owner**: :jrmuizel

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/twitch-animation.toml#18`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2000000
* **perfstats**: true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/twitch-animation
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **subtest lower is better**: true
* **subtest unit**: ms
* **test url**: <http://\<host\>:\<port\>/index.html>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-twitch-animation**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(unity-webgl-b)=

::::{dropdown} unity-webgl
:class-container: anchor-id-unity-webgl-b

* **Command to Run Locally**

  ```
  ./mach raptor -t unity-webgl
  ```

**Owner**: :jgilbert and Graphics(gfx) Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/unity-webgl-desktop.toml#17`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 420000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/unity-webgl
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: score
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-refbrow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-refbrow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


**Owner**: :jgilbert and Graphics(gfx) Team

* **alert threshold**: 2.0
* **apps**: geckoview, refbrow, fenix, chrome-m
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/unity-webgl-mobile.toml#16`
* **lower is better**: false
* **page cycles**: 1
* **page timeout**: 420000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/unity-webgl
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: score
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-chrome-m**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-refbrow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-refbrow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-unity-webgl**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-unity-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-godot-b)=

::::{dropdown} wasm-godot
:class-container: anchor-id-wasm-godot-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-godot
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-godot.toml#11`
* **lower is better**: true
* **newtab per cycle**: true
* **page cycles**: 5
* **page timeout**: 120000
* **test url**: <http://localhost:\<port\>/wasm-godot/index.html>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-chrome-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-chrome-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-godot-baseline-b)=

::::{dropdown} wasm-godot-baseline
:class-container: anchor-id-wasm-godot-baseline-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-godot-baseline
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-godot-baseline.toml#14`
* **lower is better**: true
* **newtab per cycle**: true
* **page cycles**: 5
* **page timeout**: 120000
* **preferences**: javascript.options.wasm_baselinejit=true javascript.options.wasm_optimizingjit=false
* **test url**: <http://localhost:\<port\>/wasm-godot/index.html>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-godot-optimizing-b)=

::::{dropdown} wasm-godot-optimizing
:class-container: anchor-id-wasm-godot-optimizing-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-godot-optimizing
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-godot-optimizing.toml#14`
* **lower is better**: true
* **newtab per cycle**: true
* **page cycles**: 5
* **page timeout**: 120000
* **preferences**: javascript.options.wasm_baselinejit=false javascript.options.wasm_optimizingjit=true
* **test url**: <http://localhost:\<port\>/wasm-godot/index.html>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-godot-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-misc-b)=

::::{dropdown} wasm-misc
:class-container: anchor-id-wasm-misc-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-misc
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-misc.toml#15`
* **lower is better**: true
* **page cycles**: 5
* **page timeout**: 1200000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/wasm-misc
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-chrome-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-chrome-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-misc-baseline-b)=

::::{dropdown} wasm-misc-baseline
:class-container: anchor-id-wasm-misc-baseline-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-misc-baseline
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-misc-baseline.toml#14`
* **lower is better**: true
* **page cycles**: 5
* **page timeout**: 1200000
* **preferences**: javascript.options.wasm_baselinejit=true javascript.options.wasm_optimizingjit=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/wasm-misc
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-baseline**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wasm-misc-optimizing-b)=

::::{dropdown} wasm-misc-optimizing
:class-container: anchor-id-wasm-misc-optimizing-b

* **Command to Run Locally**

  ```
  ./mach raptor -t wasm-misc-optimizing
  ```

**Owner**: :lth and SpiderMonkey Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/wasm-misc-optimizing.toml#14`
* **lower is better**: true
* **page cycles**: 5
* **page timeout**: 1200000
* **preferences**: javascript.options.wasm_baselinejit=false javascript.options.wasm_optimizingjit=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/wasm-misc
* **repository revision**: 61332db584026b73e37066d717a162825408c36b
* **sparse checkout**: true
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-wasm-firefox-wasm-misc-optimizing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(webaudio-b)=

::::{dropdown} webaudio
:class-container: anchor-id-webaudio-b

* **Command to Run Locally**

  ```
  ./mach raptor -t webaudio
  ```

**Owner**: :padenot and Media Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/webaudio.toml#10`
* **lower is better**: true
* **page cycles**: 5
* **page timeout**: 360000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **test url**: <http://\<host\>:\<port\>/webaudio/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-webaudio**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-webaudio**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-av1-sfr-b)=

::::{dropdown} youtube-playback-av1-sfr
:class-container: anchor-id-youtube-playback-av1-sfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-av1-sfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#24`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-av1-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-av1-sfr**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-av1-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-av1-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-av1-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-av1-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-1080p30-b)=

::::{dropdown} youtube-playback-h264-1080p30
:class-container: anchor-id-youtube-playback-h264-1080p30-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-1080p30
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#28`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-h264-test&tests=18&raptor=true&muted=true&command=run&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-1080p60-b)=

::::{dropdown} youtube-playback-h264-1080p60
:class-container: anchor-id-youtube-playback-h264-1080p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-1080p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#34`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&raptor=true&tests=46&muted=true&command=run&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-720p60-b)=

::::{dropdown} youtube-playback-h264-720p60
:class-container: anchor-id-youtube-playback-h264-720p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-720p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, geckoview, fenix, refbrow, chrome-m
* **browsertime args**: --pageCompleteWaitTime=10 --pageCompleteCheckPollTimeout=10 --browsertime.benchmark_wait_time=500
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#82`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **support class**: browsertime_benchmark.py
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&tests=38&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-full-1080p30-b)=

::::{dropdown} youtube-playback-h264-full-1080p30
:class-container: anchor-id-youtube-playback-h264-full-1080p30-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-full-1080p30
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#40`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false full-screen-api.allow-trusted-requests-only=false full-screen-api.warning.timeout=0
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-h264-test&tests=18&raptor=true&muted=true&command=run&fullscreen=true&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-full-1080p60-b)=

::::{dropdown} youtube-playback-h264-full-1080p60
:class-container: anchor-id-youtube-playback-h264-full-1080p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-full-1080p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#55`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false full-screen-api.allow-trusted-requests-only=false full-screen-api.warning.timeout=0
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&raptor=true&tests=46&muted=true&command=run&fullscreen=true&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-h264-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-h264-sfr-b)=

::::{dropdown} youtube-playback-h264-sfr
:class-container: anchor-id-youtube-playback-h264-sfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-h264-sfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#70`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-h264-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-geckoview-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-refbrow-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-geckoview-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-refbrow-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-hfr-b)=

::::{dropdown} youtube-playback-hfr
:class-container: anchor-id-youtube-playback-hfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-hfr
  ```

**Owner**: PerfTest Team

* **alert on**: [H2641080p60fps@1X_dropped_frames](raptor-metrics.md#youtube-playback-metrics), [H264720p60fps@1X_dropped_frames](raptor-metrics.md#youtube-playback-metrics)
* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#74`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-hfr**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-refbrow-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-refbrow-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-v9-1080p30-b)=

::::{dropdown} youtube-playback-v9-1080p30
:class-container: anchor-id-youtube-playback-v9-1080p30-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-v9-1080p30
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#96`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-vp9-test&raptor=true&tests=18&muted=true&command=run&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-v9-1080p60-b)=

::::{dropdown} youtube-playback-v9-1080p60
:class-container: anchor-id-youtube-playback-v9-1080p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-v9-1080p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#102`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&raptor=true&tests=14&muted=true&command=run&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-v9-full-1080p30-b)=

::::{dropdown} youtube-playback-v9-full-1080p30
:class-container: anchor-id-youtube-playback-v9-full-1080p30-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-v9-full-1080p30
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#108`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false full-screen-api.allow-trusted-requests-only=false full-screen-api.warning.timeout=0
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-vp9-test&raptor=true&tests=18&muted=true&command=run&fullscreen=true&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p30**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-v9-full-1080p60-b)=

::::{dropdown} youtube-playback-v9-full-1080p60
:class-container: anchor-id-youtube-playback-v9-full-1080p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-v9-full-1080p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **expose browser profiler**: true
* **gather cpuTime**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#123`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false full-screen-api.allow-trusted-requests-only=false full-screen-api.warning.timeout=0
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&raptor=true&tests=14&muted=true&command=run&fullscreen=true&exclude=1,2>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-firefox-youtube-playback-v9-full-1080p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-vp9-720p60-b)=

::::{dropdown} youtube-playback-vp9-720p60
:class-container: anchor-id-youtube-playback-vp9-720p60-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-vp9-720p60
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, chrome, geckoview, fenix, refbrow, chrome-m
* **browsertime args**: --pageCompleteWaitTime=10 --pageCompleteCheckPollTimeout=10 --browsertime.benchmark_wait_time=500
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#89`
* **lower is better**: true
* **page cycles**: 20
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **support class**: browsertime_benchmark.py
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-hfr-test&tests=6&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-s24-14-0-aarch64-shippable/opt <hardware-samsung-s24>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-vp9-sfr-b)=

::::{dropdown} youtube-playback-vp9-sfr
:class-container: anchor-id-youtube-playback-vp9-sfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-vp9-sfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#138`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-sfr-vp9-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-vp9-sfr**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-mobile-fenix-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-widevine-h264-sfr-b)=

::::{dropdown} youtube-playback-widevine-h264-sfr
:class-container: anchor-id-youtube-playback-widevine-h264-sfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-widevine-h264-sfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#142`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false media.eme.enabled=true media.gmp-manager.updateEnabled=true media.eme.require-app-approval=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-widevine-sfr-h264-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-h264-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-widevine-hfr-b)=

::::{dropdown} youtube-playback-widevine-hfr
:class-container: anchor-id-youtube-playback-widevine-hfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-widevine-hfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#156`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false media.eme.enabled=true media.gmp-manager.updateEnabled=true media.eme.require-app-approval=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-widevine-hfr-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-playback-widevine-vp9-sfr-b)=

::::{dropdown} youtube-playback-widevine-vp9-sfr
:class-container: anchor-id-youtube-playback-widevine-vp9-sfr-b

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-playback-widevine-vp9-sfr
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: firefox, geckoview, fenix, refbrow, chrome
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1000
* **gecko profile threads**: MediaPlayback
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/youtube-playback.toml#170`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2700000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false media.eme.enabled=true media.gmp-manager.updateEnabled=true media.eme.require-app-approval=false
* **subtest lower is better**: true
* **subtest unit**: score
* **test url**: <https://prod.youtube-test.prod.webservices.mozgcp.net/main.html?test_type=playbackperf-widevine-sfr-vp9-test&raptor=true&exclude=1,2&muted=true&command=run>
* **type**: benchmark
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-firefox-youtube-playback-widevine-vp9-sfr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Custom

Browsertime tests that use a custom pageload test script. These use the pageload type, but may have other intentions.

(addMab1-c)=

::::{dropdown} addMab1
:class-container: anchor-id-addMab1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMab1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small ArrayBuffers to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=ArrayBuffer --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#50`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMab1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMab1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMab1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addMabN-c)=

::::{dropdown} addMabN
:class-container: anchor-id-addMabN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMabN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small ArrayBuffers to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=ArrayBuffer --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#54`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMabN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMabN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMabN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addMar1-c)=

::::{dropdown} addMar1
:class-container: anchor-id-addMar1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMar1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small Arrays to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=Array --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#58`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMar1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMar1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMar1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addMarN-c)=

::::{dropdown} addMarN
:class-container: anchor-id-addMarN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMarN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small Arrays to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=Array --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#62`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMarN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMarN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMarN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addMbl1-c)=

::::{dropdown} addMbl1
:class-container: anchor-id-addMbl1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMbl1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small Blobs to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=Blob --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#66`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMbl1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMbl1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMbl1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addMblN-c)=

::::{dropdown} addMblN
:class-container: anchor-id-addMblN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addMblN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send many small Blobs to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=16384 --browsertime.chunk_size=1024 --browsertime.buffer_type=Blob --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#70`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMblN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addMblN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addMblN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkAB1-c)=

::::{dropdown} addkAB1
:class-container: anchor-id-addkAB1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkAB1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large ArrayBuffers to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=ArrayBuffer --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#74`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkAB1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkAB1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAB1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkABN-c)=

::::{dropdown} addkABN
:class-container: anchor-id-addkABN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkABN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large ArrayBuffers to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=ArrayBuffer --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#78`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkABN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkABN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkABN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkAR1-c)=

::::{dropdown} addkAR1
:class-container: anchor-id-addkAR1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkAR1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large JS Arrays to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=Array --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#82`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkAR1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkAR1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkAR1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkARN-c)=

::::{dropdown} addkARN
:class-container: anchor-id-addkARN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkARN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large JS Arrays to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=Array --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#86`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkARN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkARN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkARN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkBL1-c)=

::::{dropdown} addkBL1
:class-container: anchor-id-addkBL1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkBL1
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large Blobs to IndexedDB in one transaction

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=Blob --browsertime.atomic=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#90`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkBL1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkBL1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBL1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(addkBLN-c)=

::::{dropdown} addkBLN
:class-container: anchor-id-addkBLN-c

* **Command to Run Locally**

  ```
  ./mach raptor -t addkBLN
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Use add API to send a couple of large Blobs to IndexedDB independently

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=128 --browsertime.chunk_size=1048576 --browsertime.buffer_type=Blob --browsertime.atomic=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#94`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_write.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkBLN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-addkBLN**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-addkBLN**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(baseline-c)=

::::{dropdown} baseline
:class-container: anchor-id-baseline-c

* **Command to Run Locally**

  ```
  ./mach raptor -t baseline
  ```

**Owner**: Network Team

**Description**: Cross-browser baseline: measures connection time and protocol to an HTTP/3 site (cloudflare.com). Used on Chrome, where the Firefox-only Happy Eyeballs v3 and DoH preferences do not apply.

* **alert threshold**: 2.0
* **apps**: chrome,chrome-m
* **browser cycles**: 3
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-hev3-connection.toml#27`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 75000
* **preferences**: network.connectivity-service.enabled=true
* **test script**: hev3_connection.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-chrome-m-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-chrome-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-chrome-baseline**
  - ✅
  - ❌
  - ❌
  - ❌
:::


::::

(browsertime-c)=

::::{dropdown} browsertime
:class-container: anchor-id-browsertime-c

* **Command to Run Locally**

  ```
  ./mach raptor -t browsertime
  ```

**Owner**: PerfTest Team

**Description**: Used to run vanilla Browsertime tests through Raptor. For example: `./mach raptor -t browsertime --browsertime-arg url=https://www.sitespeed.io --browsertime-arg iterations=3`

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time)
* **alert threshold**: 2.0
* **apps**: firefox
* **browser cycles**: 1
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-custom.toml#17`
* **lower is better**: true
* **measure**: fnbpaint, fcp, dcf, loadtime
* **page cycles**: 1
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: null.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test script**: None
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
::::

(busydoh-c)=

::::{dropdown} busydoh
:class-container: anchor-id-busydoh-c

* **Command to Run Locally**

  ```
  ./mach raptor -t busydoh
  ```

**Owner**: Network Team

**Description**: Distribution of DoH lookup times while a local page replays the request timeline recorded from detroitnews.com. The page, its hosts and the DoH resolver are all served by a local test server.

* **alert threshold**: 2.0
* **apps**: firefox, fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.site=detroitnews
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-doh-busy-site.toml#29`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 120000
* **preferences**: network.connectivity-service.enabled=true
* **support class**: doh_busy_site.py
* **test script**: doh_busy_site.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-m-fenix-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-p6-13-0-aarch64-shippable/opt <hardware-google-pixel-6>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-m-fenix-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-firefox-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-firefox-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-firefox-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-doh-busy-site-firefox-busydoh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(connect-c)=

::::{dropdown} connect
:class-container: anchor-id-connect-c

* **Command to Run Locally**

  ```
  ./mach raptor -t connect
  ```

**Owner**: Network Team

**Description**: Verifies that connection time on a pageload is zero when a speculative connection has first been made

* **alert change type**: absolute
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change)
* **alert threshold**: 10.0
* **apps**: firefox
* **browser cycles**: 3
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculative.toml#18`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: speculative-connect.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculative-firefox-connect**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(constant-regression-c)=

::::{dropdown} constant-regression
:class-container: anchor-id-constant-regression-c

* **Command to Run Locally**

  ```
  ./mach raptor -t constant-regression
  ```

**Owner**: PerfTest Team

**Description**: Generates a constant value that can be changed to induce a regression.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, geckoview, fenix
* **browser cycles**: 1
* **browsertime args**: --browsertime.constant_value=1500
* **custom data**: true
* **expected**: pass
* **fore window**: 1
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-regression-test.toml#23`
* **lower is better**: true
* **max back window**: 3
* **min back window**: 2
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest unit**: ms
* **test script**: constant_regression_test.js
* **test summary**: flatten
* **test url**: <None>
* **type**: pageload
* **unit**: score
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-regression-tests-firefox-constant-regression**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-regression-tests-firefox-constant-regression**
  - ❌
  - ✅
  - ❌
  - ❌
:::


::::

(dns-multi-c)=

::::{dropdown} dns-multi
:class-container: anchor-id-dns-multi-c

* **Command to Run Locally**

  ```
  ./mach raptor -t dns-multi
  ```

**Owner**: Network Team

**Description**: Tests multi-domain DNS lookup and pageload performance via native DNS.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:0
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#16`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_multi_domain_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-dns-multi**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(dns-native-c)=

::::{dropdown} dns-native
:class-container: anchor-id-dns-native-c

* **Command to Run Locally**

  ```
  ./mach raptor -t dns-native
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time via native DNS.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:0
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#51`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-dns-native**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-dns-native**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(getkeyrng-c)=

::::{dropdown} getkeyrng
:class-container: anchor-id-getkeyrng-c

* **Command to Run Locally**

  ```
  ./mach raptor -t getkeyrng
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures the speed of key-range based reads of IndexedDB

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#98`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **support class**: browsertime_pageload.py
* **test script**: indexeddb_getkeyrange.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-getkeyrng**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(h2-download-c)=

::::{dropdown} h2-download
:class-container: anchor-id-h2-download-c

* **Command to Run Locally**

  ```
  ./mach raptor -t h2-download
  ```

**Owner**: Network Team

**Description**: Measures HTTP/2 download throughput with a local server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, custom-car
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h2_download --browsertime.iterations=10 --firefox.preference=network.http.http3.enable:false
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-network-bench.toml#38`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **support class**: network_bench.py
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h2-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-custom-car-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(h2-upload-c)=

::::{dropdown} h2-upload
:class-container: anchor-id-h2-upload-c

* **Command to Run Locally**

  ```
  ./mach raptor -t h2-upload
  ```

**Owner**: Network Team

**Description**: Measures HTTP/2 file upload throughput with a local server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, custom-car
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h2_upload --browsertime.iterations=10 --firefox.preference=network.http.http3.enable:false
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-network-bench.toml#34`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **support class**: network_bench.py
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h2-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-custom-car-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h2-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h2-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(h3-download-c)=

::::{dropdown} h3-download
:class-container: anchor-id-h3-download-c

* **Command to Run Locally**

  ```
  ./mach raptor -t h3-download
  ```

**Owner**: Network Team

**Description**: Measures HTTP/3 download throughput with a local server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, custom-car
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h3_download --browsertime.iterations=10 --firefox.preference=network.http.http3.enable:true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-network-bench.toml#28`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **support class**: network_bench.py
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h3-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-custom-car-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-download**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-download-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(h3-upload-c)=

::::{dropdown} h3-upload
:class-container: anchor-id-h3-upload-c

* **Command to Run Locally**

  ```
  ./mach raptor -t h3-upload
  ```

**Owner**: Network Team

**Description**: Measures HTTP/3 file upload throughput with a local server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, custom-car
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h3_upload --browsertime.iterations=10 --firefox.preference=network.http.http3.enable:true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-network-bench.toml#24`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **support class**: network_bench.py
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h3-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-custom-car-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-chrome-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-chrome-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-custom-car-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-network-bench-firefox-h3-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-100M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-10M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-1M_400ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_40ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-network-bench-firefox-h3-upload-300M_80ms-0**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(he3off-dns-c)=

::::{dropdown} he3off-dns
:class-container: anchor-id-he3off-dns-c

* **Command to Run Locally**

  ```
  ./mach raptor -t he3off-dns
  ```

**Owner**: Network Team

**Description**: Measures connection time and protocol to an HTTP/3 site (cloudflare.com) with Happy Eyeballs v3 disabled, using native DNS.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --firefox.preference network.http.happy_eyeballs_enabled:false --firefox.preference network.trr.mode:0
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-hev3-connection.toml#35`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 75000
* **preferences**: network.connectivity-service.enabled=true
* **test script**: hev3_connection.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3off-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(he3off-doh-c)=

::::{dropdown} he3off-doh
:class-container: anchor-id-he3off-doh-c

* **Command to Run Locally**

  ```
  ./mach raptor -t he3off-doh
  ```

**Owner**: Network Team

**Description**: Measures connection time and protocol to an HTTP/3 site (cloudflare.com) with Happy Eyeballs v3 disabled, using DoH (TRR mode 2, CIRA).

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --firefox.preference network.http.happy_eyeballs_enabled:false --firefox.preference network.trr.mode:2 --firefox.preference network.trr.uri:https://private.canadianshield.cira.ca/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-hev3-connection.toml#42`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 75000
* **preferences**: network.connectivity-service.enabled=true
* **test script**: hev3_connection.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3off-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3off-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(he3on-dns-c)=

::::{dropdown} he3on-dns
:class-container: anchor-id-he3on-dns-c

* **Command to Run Locally**

  ```
  ./mach raptor -t he3on-dns
  ```

**Owner**: Network Team

**Description**: Measures connection time and protocol to an HTTP/3 site (cloudflare.com) with Happy Eyeballs v3 enabled, using native DNS.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --firefox.preference network.http.happy_eyeballs_enabled:true --firefox.preference network.trr.mode:0
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-hev3-connection.toml#49`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 75000
* **preferences**: network.connectivity-service.enabled=true
* **test script**: hev3_connection.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3on-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-dns**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(he3on-doh-c)=

::::{dropdown} he3on-doh
:class-container: anchor-id-he3on-doh-c

* **Command to Run Locally**

  ```
  ./mach raptor -t he3on-doh
  ```

**Owner**: Network Team

**Description**: Measures connection time and protocol to an HTTP/3 site (cloudflare.com) with Happy Eyeballs v3 enabled, using DoH (TRR mode 2, CIRA).

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --firefox.preference network.http.happy_eyeballs_enabled:true --firefox.preference network.trr.mode:2 --firefox.preference network.trr.uri:https://private.canadianshield.cira.ca/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,Socket Thread
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-hev3-connection.toml#56`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 75000
* **preferences**: network.connectivity-service.enabled=true
* **test script**: hev3_connection.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3on-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-fenix-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-firefox-he3on-doh**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idb-open-few-par-c)=

::::{dropdown} idb-open-few-par
:class-container: anchor-id-idb-open-few-par-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idb-open-few-par
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures opening a few IndexedDB databases in parallel

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=1000 --browsertime.parallel=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#36`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_open.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idb-open-few-seq-c)=

::::{dropdown} idb-open-few-seq
:class-container: anchor-id-idb-open-few-seq-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idb-open-few-seq
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures opening a few IndexedDB databases in sequence

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=1000 --browsertime.parallel=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#40`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_open.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-few-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idb-open-many-par-c)=

::::{dropdown} idb-open-many-par
:class-container: anchor-id-idb-open-many-par-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idb-open-many-par
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures opening many IndexedDB databases in parallel

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=10000 --browsertime.parallel=1
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#28`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_open.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-par**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idb-open-many-seq-c)=

::::{dropdown} idb-open-many-seq
:class-container: anchor-id-idb-open-many-seq-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idb-open-many-seq
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures opening many IndexedDB databases in sequence

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=10000 --browsertime.parallel=0
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#32`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_open.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idb-open-many-seq**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idbCurNext-c)=

::::{dropdown} idbCurNext
:class-container: anchor-id-idbCurNext-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idbCurNext
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures IDBIndex cursor iteration via continue() over many rows sharing an index value

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=50000 --browsertime.cursor_mode=continue
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#109`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_cursor.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurNext**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idbCurPKey-c)=

::::{dropdown} idbCurPKey
:class-container: anchor-id-idbCurPKey-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idbCurPKey
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures IDBIndex cursor iteration via continuePrimaryKey() over many rows sharing an index value

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=50000 --browsertime.cursor_mode=continuePrimaryKey
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#113`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_cursor.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPKey**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(idbCurPrev-c)=

::::{dropdown} idbCurPrev
:class-container: anchor-id-idbCurPrev-c

* **Command to Run Locally**

  ```
  ./mach raptor -t idbCurPrev
  ```

**Owner**: DOM Lifecycle, Workers and Storage Team

**Description**: Measures IDBIndex cursor iteration via continue() in the prev direction over many rows sharing an index value

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=50000 --browsertime.cursor_mode=continue --browsertime.direction=prev
* **custom data**: true
* **expected**: pass
* **gecko profile entries**: 131072000
* **gecko profile features**: js,stackwalk,memory
* **gecko profile threads**: GeckoMain,DOM Worker,IndexedDB
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-indexeddb.toml#117`
* **lower is better**: true
* **measure**: cpuTime
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-example.manifest
* **subtest unit**: ms
* **test script**: indexeddb_cursor.js
* **test summary**: flatten
* **test url**: <https://www.example.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-chrome-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-indexeddb-firefox-idbCurPrev**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(media-seek-c)=

::::{dropdown} media-seek
:class-container: anchor-id-media-seek-c

* **Command to Run Locally**

  ```
  ./mach raptor -t media-seek
  ```

**Owner**: Media Team

**Description**: Measures buffered video seek latency (time from a currentTime seek to the seeked event) with the decoder warm, and also cold on Firefox, where the decoder can be forced to be released on pause.

* **alert on**: [seekedColdLatency](raptor-metrics.md#seek-latency), [seekedWarmLatency](raptor-metrics.md#seek-latency)
* **alert threshold**: 5.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.iterations=5
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,js,cpu,screenshots
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,MediaDecoderStateMachine,MediaSupervisor,MediaPDecoder,RemVidChild,RemVidParent
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-seek.toml#35`
* **lower is better**: true
* **measure**: seekedColdLatency, seekedWarmLatency
* **page cycles**: 5
* **page timeout**: 120000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.autoplay.ask-permission=false media.dormant-on-pause-timeout-ms=0
* **subtest unit**: ms
* **support class**: media_seek.py
* **test script**: media_seek.js
* **test summary**: flatten
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-chrome-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-seek-firefox-media-seek**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-chrome-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-seek-firefox-media-seek**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-chrome-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-seek-firefox-media-seek**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-seek-firefox-media-seek**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(mp-1080p30-av1-hw-c)=

::::{dropdown} mp-1080p30-av1-hw
:class-container: anchor-id-mp-1080p30-av1-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-av1-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 AV1 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. This is the cross-browser track.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#48`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=av1_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-1080p30-av1-sw-c)=

::::{dropdown} mp-1080p30-av1-sw
:class-container: anchor-id-mp-1080p30-av1-sw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-av1-sw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 AV1 playback with software decode forced, alongside the frame rate that shows whether playback kept up. Firefox only, since the forcing preference is not applied to other browsers.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback-sw.toml#39`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.enabled=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=av1_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-1080p30-h264-hw-c)=

::::{dropdown} mp-1080p30-h264-hw
:class-container: anchor-id-mp-1080p30-h264-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-h264-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 H264 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. This is the cross-browser track.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#52`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=h264_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-1080p30-h264-sw-c)=

::::{dropdown} mp-1080p30-h264-sw
:class-container: anchor-id-mp-1080p30-h264-sw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-h264-sw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 H264 playback with software decode forced, alongside the frame rate that shows whether playback kept up. Firefox only, since the forcing preference is not applied to other browsers.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback-sw.toml#43`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.enabled=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=h264_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-1080p30-h264-sw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(mp-1080p30-vp9-hw-c)=

::::{dropdown} mp-1080p30-vp9-hw
:class-container: anchor-id-mp-1080p30-vp9-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-vp9-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 VP9 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. This is the cross-browser track.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#56`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=vp9_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-1080p30-vp9-sw-c)=

::::{dropdown} mp-1080p30-vp9-sw
:class-container: anchor-id-mp-1080p30-vp9-sw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-1080p30-vp9-sw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 1080p30 VP9 playback with software decode forced, alongside the frame rate that shows whether playback kept up. Firefox only, since the forcing preference is not applied to other browsers.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback-sw.toml#47`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.enabled=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=vp9_1080p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-2160p30-av1-hw-c)=

::::{dropdown} mp-2160p30-av1-hw
:class-container: anchor-id-mp-2160p30-av1-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-2160p30-av1-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 2160p30 AV1 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. Four times the pixels of the 1080p subtests, so playback accounts for more of the energy measured.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#60`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=av1_2160p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-2160p30-h264-hw-c)=

::::{dropdown} mp-2160p30-h264-hw
:class-container: anchor-id-mp-2160p30-h264-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-2160p30-h264-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 2160p30 H264 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. Four times the pixels of the 1080p subtests, so playback accounts for more of the energy measured.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#64`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=h264_2160p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-2160p30-h264-hw**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(mp-2160p30-vp9-hw-c)=

::::{dropdown} mp-2160p30-vp9-hw
:class-container: anchor-id-mp-2160p30-vp9-hw-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-2160p30-vp9-hw
  ```

**Owner**: Media Team

**Description**: Measures the power used by sustained 2160p30 VP9 playback with hardware decode forced, alongside the frame rate that shows whether playback kept up. Four times the pixels of the 1080p subtests, so playback accounts for more of the energy measured.

* **alert on**: [powerUsage_cpu_package](raptor-metrics.md#power-usage)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,MediaDecoderStateMachine,MediaPDecoder
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#68`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu, presentedFps
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: false
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?clip=vp9_2160p30&driver=single&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
::::

(mp-idle-c)=

::::{dropdown} mp-idle
:class-container: anchor-id-mp-idle-c

* **Command to Run Locally**

  ```
  ./mach raptor -t mp-idle
  ```

**Owner**: Media Team

**Description**: Measures the power used over the same interval with nothing playing, as the baseline the media playback subtests are read against.

* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **benchmark webserver**: true
* **browser cycles**: 1
* **custom data**: true
* **expected**: pass
* **gecko profile features**: stackwalk,processcpu,power
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-media-playback.toml#72`
* **lower is better**: true
* **measure**: powerUsage_cpu_package, powerUsage_cpu_cores, powerUsage_gpu
* **page cycles**: 15
* **page timeout**: 60000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.block-autoplay-until-in-foreground=false media.suspend-background-video.enabled=false media.hardware-video-decoding.force-enabled=true
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/media-playback
* **repository revision**: 7046a4bd167a082989802856aaa86b77e84e939a
* **sparse checkout**: true
* **subtest lower is better**: true
* **subtest unit**: uWh
* **support class**: media_playback.py
* **test script**: media_playback.js
* **test url**: <http://\<host\>:\<port\>/index.html?driver=idle&raptor=1>
* **type**: pageload
* **unit**: uWh
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-chrome-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-media-playback-firefox-mp-idle**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-media-playback-firefox-mp-idle**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-conservative-c)=

::::{dropdown} prefetch-conservative
:class-container: anchor-id-prefetch-conservative-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-conservative
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test (conservative eagerness): the inline speculationrules script prefetches the target on pointerdown, which is held so the prefetch can complete before release navigates, letting a prefetch-capable browser serve the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.eagerness=conservative
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#29`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-eager-c)=

::::{dropdown} prefetch-eager
:class-container: anchor-id-prefetch-eager-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-eager
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test (eager eagerness): the inline speculationrules script prefetches the target on the slightest hover, then the link is clicked so a prefetch-capable browser serves the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.eagerness=eager
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#23`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-eager**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-immediate-c)=

::::{dropdown} prefetch-immediate
:class-container: anchor-id-prefetch-immediate-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-immediate
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test (immediate eagerness): the inline speculationrules script prefetches the target as soon as it is parsed, then the link is clicked so a prefetch-capable browser serves the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.eagerness=immediate
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#20`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-list-conservative-c)=

::::{dropdown} prefetch-list-conservative
:class-container: anchor-id-prefetch-list-conservative-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-list-conservative
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test using a list-source rule (conservative eagerness): the inline speculationrules script lists the target URLs explicitly; matching links are present so the prefetch can be triggered on pointerdown, which is held so the prefetch can complete before release navigates (list-rule eagerness is browser-defined).

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.source=list --browsertime.eagerness=conservative
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#41`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-list-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-conservative**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-list-eager-c)=

::::{dropdown} prefetch-list-eager
:class-container: anchor-id-prefetch-list-eager-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-list-eager
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test using a list-source rule (eager eagerness): the inline speculationrules script lists the target URLs explicitly; matching links are present so the prefetch can be triggered on the slightest hover (list-rule eagerness is browser-defined), then the link is clicked to serve the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.source=list --browsertime.eagerness=eager
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#35`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-list-eager**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-eager**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-list-immediate-c)=

::::{dropdown} prefetch-list-immediate
:class-container: anchor-id-prefetch-list-immediate-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-list-immediate
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test using a list-source rule (immediate eagerness): the inline speculationrules script lists the target URLs explicitly, so a prefetch-capable browser prefetches them as soon as the rules are parsed, then the link is clicked to serve the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.source=list --browsertime.eagerness=immediate
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#32`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-list-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-immediate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-list-inflight-c)=

::::{dropdown} prefetch-list-inflight
:class-container: anchor-id-prefetch-list-inflight-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-list-inflight
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test using a list-source rule (moderate eagerness, in-flight activation): the inline speculationrules script lists the target URLs explicitly; ~200 ms of sustained hover triggers the prefetch and the link is clicked partway through the target's server stall, so the navigation starts while the prefetch is still in flight and must wait for it to complete rather than refetching the page.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.source=list --browsertime.eagerness=moderate --browsertime.dwell_ms=250
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#57`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-list-inflight**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-inflight**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-list-moderate-c)=

::::{dropdown} prefetch-list-moderate
:class-container: anchor-id-prefetch-list-moderate-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-list-moderate
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test using a list-source rule (moderate eagerness): the inline speculationrules script lists the target URLs explicitly; matching links are present so the prefetch can be triggered after ~200 ms of sustained hover (list-rule eagerness is browser-defined), then the link is clicked to serve the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.source=list --browsertime.eagerness=moderate
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#38`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-list-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-list-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(prefetch-moderate-c)=

::::{dropdown} prefetch-moderate
:class-container: anchor-id-prefetch-moderate-c

* **Command to Run Locally**

  ```
  ./mach raptor -t prefetch-moderate
  ```

**Owner**: Network Team, Performance Team

**Description**: Speculation Rules prefetch test (moderate eagerness): the inline speculationrules script prefetches the target after ~200 ms of sustained hover, then the link is clicked so a prefetch-capable browser serves the navigation from the prefetch cache.

* **alert threshold**: 10.0
* **apps**: firefox, chrome
* **browser cycles**: 1
* **browsertime args**: --browsertime.eagerness=moderate
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-speculation-rules.toml#26`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **support class**: speculation_rules.py
* **test script**: speculation-rules-prefetch.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-chrome-prefetch-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-speculation-rules-firefox-prefetch-moderate**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(process-switch-c)=

::::{dropdown} process-switch
:class-container: anchor-id-process-switch-c

* **Command to Run Locally**

  ```
  ./mach raptor -t process-switch
  ```

**Owner**: PerfTest Team

**Description**: Measures process switch time

* **accept zero vismet**: true
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **browser cycles**: 25
* **browsertime args**: --pageCompleteWaitTime=1000 --pageCompleteCheckInactivity=true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-process-switch.toml#20`
* **lower is better**: true
* **measure**: fcp, loadtime, ContentfulSpeedIndex, PerceptualSpeedIndex, SpeedIndex, FirstVisualChange, LastVisualChange
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-proc-switch.manifest
* **playback version**: 5.1.1
* **support class**: browsertime_pageload.py
* **test script**: process_switch.js
* **test url**: <https://mozilla.seanfeng.dev/files/red.html,https://mozilla.pettay.fi/moztests/blue.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-custom-firefox-process-switch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(sample-python-support-c)=

::::{dropdown} sample-python-support
:class-container: anchor-id-sample-python-support-c

* **Command to Run Locally**

  ```
  ./mach raptor -t sample-python-support
  ```

**Owner**: PerfTest Team

**Description**: A sample test that uses a python support file to modify the test command.

* **alert threshold**: 2.0
* **apps**: firefox
* **browser cycles**: 1
* **browsertime args**: --browsertime.constant_value={replace-with-constant-value}
* **custom data**: true
* **expected**: pass
* **fore window**: 1
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-sample-python-support.toml#22`
* **lower is better**: true
* **max back window**: 3
* **min back window**: 2
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest unit**: ms
* **support class**: sample_python_support.py
* **test script**: constant_regression_test.js
* **test summary**: flatten
* **test url**: <None>
* **type**: pageload
* **unit**: score
* **use live sites**: true
::::

(throttled-c)=

::::{dropdown} throttled
:class-container: anchor-id-throttled-c

* **Command to Run Locally**

  ```
  ./mach raptor -t throttled
  ```

**Owner**: Network Team

**Description**: Pageload test using a throttled network environment

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox
* **browser cycles**: 3
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-throttled-pageload.toml#17`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: throttled_pageload.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-throttled-firefox-throttled**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tp6-bench-c)=

::::{dropdown} tp6-bench
:class-container: anchor-id-tp6-bench-c

* **Command to Run Locally**

  ```
  ./mach raptor -t tp6-bench
  ```

**Owner**: PerfTest Team

**Description**: A pageload benchmark test that quickly runs through a selection of the pages available to produce a single overall metric for fcp, lcp, and loadtime. The metrics from all pages get summarized with the geomean, and mean.


* **alert threshold**: 2.0
* **apps**: firefox, chrome, chromium, safari
* **browser cycles**: 1
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-tp6-bench.toml#19`
* **lower is better**: true
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **playback**: mitmproxy
* **playback pageset manifest**: None
* **playback version**: 8.1.1
* **subtest unit**: ms
* **support class**: browsertime_tp6_bench.py
* **test script**: browsertime_tp6_bench.js
* **test summary**: flatten
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-pageload-benchmark-firefox-tp6-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-cira-c)=

::::{dropdown} trr-cira
:class-container: anchor-id-trr-cira-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-cira
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a CIRA TRR server.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://private.canadianshield.cira.ca/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#23`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-cloudf-c)=

::::{dropdown} trr-cloudf
:class-container: anchor-id-trr-cloudf-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-cloudf
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a Cloudflare TRR server.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://mozilla.cloudflare-dns.com/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#30`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-cloudf**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-cloudf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-comcas-c)=

::::{dropdown} trr-comcas
:class-container: anchor-id-trr-comcas-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-comcas
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a Comcast TRR server.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://doh.xfinity.com/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#37`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-comcas**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-multi-c)=

::::{dropdown} trr-multi
:class-container: anchor-id-trr-multi-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-multi
  ```

**Owner**: Network Team

**Description**: Tests multi-domain DNS lookup and pageload performance on a Cloudflare TRR server.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://mozilla.cloudflare-dns.com/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#44`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_multi_domain_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-multi**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-multi**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-next-c)=

::::{dropdown} trr-next
:class-container: anchor-id-trr-next-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-next
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a NextDNS TRR server.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=browser_idle --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://firefox.dns.nextdns.io/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#58`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-next**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-w-cira-c)=

::::{dropdown} trr-w-cira
:class-container: anchor-id-trr-w-cira-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-w-cira
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a CIRA TRR server, with the TRR connection warmed up.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=trr_warm --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://private.canadianshield.cira.ca/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#65`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-cira**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-w-clou-c)=

::::{dropdown} trr-w-clou
:class-container: anchor-id-trr-w-clou-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-w-clou
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a Cloudflare TRR server, with the TRR connection warmed up.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=trr_warm --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://mozilla.cloudflare-dns.com/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#72`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-w-clou**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-clou**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-w-comc-c)=

::::{dropdown} trr-w-comc
:class-container: anchor-id-trr-w-comc-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-w-comc
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a Comcast TRR server, with the TRR connection warmed up.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=trr_warm --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://doh.xfinity.com/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#79`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-comc**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(trr-w-next-c)=

::::{dropdown} trr-w-next
:class-container: anchor-id-trr-w-next-c

* **Command to Run Locally**

  ```
  ./mach raptor -t trr-w-next
  ```

**Owner**: Network Team

**Description**: Tests DNS lookup time on a NextDNS TRR server, with the TRR connection warmed up.

* **alert threshold**: 2.0
* **apps**: firefox,fenix
* **browser cycles**: 3
* **browsertime args**: --browsertime.test_type=trr_warm --firefox.preference network.trr.mode:3 --firefox.preference network.trr.uri:https://firefox.dns.nextdns.io/dns-query
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-trr-performance.toml#86`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 60000
* **test script**: trr_performance.js
* **test url**: <None>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-w-next**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-m-fenix-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-trr-performance-firefox-trr-w-next**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(upload-c)=

::::{dropdown} upload
:class-container: anchor-id-upload-c

* **Command to Run Locally**

  ```
  ./mach raptor -t upload
  ```

**Owner**: Network Team

**Description**: Measures HTTP/2 file upload throughput with a remote server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h2_upload --browsertime.iterations=10 --browsertime.server_url=https://uploadtest-381620.uc.r.appspot.com --firefox.preference=network.http.http3.enable:false --chrome.args disable-quic
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-upload.toml#16`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-chrome-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-upload-firefox-upload**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-chrome-upload**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-upload-firefox-upload**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(upload-h3-c)=

::::{dropdown} upload-h3
:class-container: anchor-id-upload-h3-c

* **Command to Run Locally**

  ```
  ./mach raptor -t upload-h3
  ```

**Owner**: Network Team

**Description**: Measures HTTP/3 file upload throughput with a remote server

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 1
* **browsertime args**: --browsertime.test_type=h3_upload --browsertime.iterations=10 --browsertime.server_url=https://uploadtest-381620.uc.r.appspot.com --firefox.preference=network.http.http3.enable:true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-upload.toml#31`
* **lower is better**: false
* **output timeout**: 2000000
* **page cycles**: 1
* **page timeout**: 1800000
* **subtest lower is better**: false
* **subtest name filters**: tests/,iterations/
* **subtest unit**: mbps
* **test script**: network_bench.js
* **test url**: <None>
* **type**: pageload
* **unit**: mbps
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-chrome-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-upload-firefox-upload-h3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-chrome-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-upload-firefox-upload-h3**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-upload-firefox-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-q-c)=

::::{dropdown} ve-av1-q
:class-container: anchor-id-ve-av1-q-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-q
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p video in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#22`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=av01.0.04M.08&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-q-cam-c)=

::::{dropdown} ve-av1-q-cam
:class-container: anchor-id-ve-av1-q-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-q-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p camera input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#86`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=av01.0.04M.08&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-q-i420-c)=

::::{dropdown} ve-av1-q-i420
:class-container: anchor-id-ve-av1-q-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-q-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p I420 input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#52`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=av01.0.04M.08&latencyModes=quality&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-rt-c)=

::::{dropdown} ve-av1-rt
:class-container: anchor-id-ve-av1-rt-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-rt
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p video in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#19`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=av01.0.04M.08&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-rt-cam-c)=

::::{dropdown} ve-av1-rt-cam
:class-container: anchor-id-ve-av1-rt-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-rt-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p camera input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#73`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=av01.0.04M.08&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-av1-rt-i420-c)=

::::{dropdown} ve-av1-rt-i420
:class-container: anchor-id-ve-av1-rt-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-av1-rt-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures AV1 encoding performance for 1080p I420 input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#49`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=av01.0.04M.08&latencyModes=realtime&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-q-c)=

::::{dropdown} ve-h264-q
:class-container: anchor-id-ve-h264-q-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-q
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p video in quality mode using AVC format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#28`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:avc&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-q-cam-c)=

::::{dropdown} ve-h264-q-cam
:class-container: anchor-id-ve-h264-q-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-q-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p camera input in quality mode using AVC format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#112`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=avc1.42001E:avc&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-q-i420-c)=

::::{dropdown} ve-h264-q-i420
:class-container: anchor-id-ve-h264-q-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-q-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p I420 input in quality mode using AVC format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#58`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:avc&latencyModes=quality&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-q-sd-c)=

::::{dropdown} ve-h264-q-sd
:class-container: anchor-id-ve-h264-q-sd-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-q-sd
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 480p video in quality mode using AVC format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#34`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:avc&latencyModes=quality&width=640&height=480&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-q-sd-cam-c)=

::::{dropdown} ve-h264-q-sd-cam
:class-container: anchor-id-ve-h264-q-sd-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-q-sd-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 480p camera input in quality mode using AVC format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#138`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=avc1.42001E:avc&latencyModes=quality&width=640&height=480&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-rt-c)=

::::{dropdown} ve-h264-rt
:class-container: anchor-id-ve-h264-rt-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-rt
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p video in real-time mode using Annex B format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#25`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:annexb&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-rt-cam-c)=

::::{dropdown} ve-h264-rt-cam
:class-container: anchor-id-ve-h264-rt-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-rt-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p camera input in real-time mode using Annex B format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#99`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=avc1.42001E:annexb&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-rt-i420-c)=

::::{dropdown} ve-h264-rt-i420
:class-container: anchor-id-ve-h264-rt-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-rt-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 1080p I420 input in real-time mode using Annex B format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#55`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:annexb&latencyModes=realtime&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-rt-sd-c)=

::::{dropdown} ve-h264-rt-sd
:class-container: anchor-id-ve-h264-rt-sd-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-rt-sd
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 480p video in real-time mode using Annex B format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#31`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=avc1.42001E:annexb&latencyModes=realtime&width=640&height=480&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-h264-rt-sd-cam-c)=

::::{dropdown} ve-h264-rt-sd-cam
:class-container: anchor-id-ve-h264-rt-sd-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-h264-rt-sd-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures H264-baseline encoding performance for 480p camera input in real-time mode using Annex B format.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#125`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=avc1.42001E:annexb&latencyModes=realtime&width=640&height=480&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-q-c)=

::::{dropdown} ve-vp8-q
:class-container: anchor-id-ve-vp8-q-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-q
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p video in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#40`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp8&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-q-cam-c)=

::::{dropdown} ve-vp8-q-cam
:class-container: anchor-id-ve-vp8-q-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-q-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p camera input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#164`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=vp8&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-q-i420-c)=

::::{dropdown} ve-vp8-q-i420
:class-container: anchor-id-ve-vp8-q-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-q-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p I420 input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#64`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp8&latencyModes=quality&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-rt-c)=

::::{dropdown} ve-vp8-rt
:class-container: anchor-id-ve-vp8-rt-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-rt
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p video in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#37`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp8&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-rt-cam-c)=

::::{dropdown} ve-vp8-rt-cam
:class-container: anchor-id-ve-vp8-rt-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-rt-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p camera input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#151`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=vp8&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp8-rt-i420-c)=

::::{dropdown} ve-vp8-rt-i420
:class-container: anchor-id-ve-vp8-rt-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp8-rt-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP8 encoding performance for 1080p I420 input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#61`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp8&latencyModes=realtime&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-q-c)=

::::{dropdown} ve-vp9-q
:class-container: anchor-id-ve-vp9-q-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-q
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p video in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#46`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp09.00.10.08&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-q-cam-c)=

::::{dropdown} ve-vp9-q-cam
:class-container: anchor-id-ve-vp9-q-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-q-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p camera input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#190`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=vp09.00.10.08&latencyModes=quality&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-q-i420-c)=

::::{dropdown} ve-vp9-q-i420
:class-container: anchor-id-ve-vp9-q-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-q-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p I420 input in quality mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#70`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp09.00.10.08&latencyModes=quality&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-rt-c)=

::::{dropdown} ve-vp9-rt
:class-container: anchor-id-ve-vp9-rt-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-rt
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p video in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#43`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp09.00.10.08&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-rt-cam-c)=

::::{dropdown} ve-vp9-rt-cam
:class-container: anchor-id-ve-vp9-rt-cam-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-rt-cam
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p camera input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **chrome args**: --use-fake-device-for-media-stream --use-fake-ui-for-media-stream
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#177`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **preferences**: media.navigator.permission.disabled=true media.navigator.streams.fake=true media.getusermedia.camera.fake.force=true media.autoplay.default=0
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&sources=camera&codecs=vp09.00.10.08&latencyModes=realtime&width=1920&height=1080&framerate=30>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ve-vp9-rt-i420-c)=

::::{dropdown} ve-vp9-rt-i420
:class-container: anchor-id-ve-vp9-rt-i420-c

* **Command to Run Locally**

  ```
  ./mach raptor -t ve-vp9-rt-i420
  ```

**Owner**: :chunmin and Media Team

**Description**: Measures VP9 encoding performance for 1080p I420 input in real-time mode.

* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-webcodecs.toml#67`
* **lower is better**: true
* **page cycles**: 3
* **page timeout**: 360000
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/webcodecs
* **repository revision**: 8fd150ac2ede429d5a927e25c04159e57760ac81
* **sparse checkout**: true
* **support class**: browsertime_benchmark.py
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&codecs=vp09.00.10.08&latencyModes=realtime&width=1920&height=1080&framerate=30&pixelFormats=I420>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-chrome-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-custom-car-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(vpl-av1-c)=

::::{dropdown} vpl-av1
:class-container: anchor-id-vpl-av1-c

* **Command to Run Locally**

  ```
  ./mach raptor -t vpl-av1
  ```

**Owner**: :aosmond and the Media Team

**Description**: Measures AV1 1080p video playback latency.

* **alert on**: [estimatedFirstFrameLatency](raptor-metrics.md#estimated-frame-latency-first), [estimatedAnyFrameLatency](raptor-metrics.md#estimated-frame-latency-any)
* **alert threshold**: 2.0
* **apps**: chrome, chrome-m, geckoview, fenix, firefox, safari
* **benchmark webserver**: true
* **browser cycles**: 20
* **expected**: pass
* **gecko profile features**: stackwalk,js,java,processcpu,screenshots,memory
* **gecko profile interval**: 1
* **gecko profile threads**: BackgroundThreadPool,Compositor,decoder,GeckoMain,gmp,graph,grph,InotifyEventThread,IPDL Background,media,ModuleProcessThread,PacerThread,RemVidChild,RenderBackend,Renderer,SwComposite,TextureUpdate,ImageBridgeChld
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-video-playback-latency.toml#37`
* **lower is better**: true
* **measure**: firstFrame, secondFrame, lastFrame, estimatedFirstFrameLatency, estimatedAnyFrameLatency
* **newtab per cycle**: true
* **page cycles**: 20
* **page timeout**: 20000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/video-playback-latency
* **repository revision**: e3d0295c15cfdd6f32e6d4270de0ed5d6c1632cd
* **sparse checkout**: true
* **support class**: video_playback_latency.py
* **test url**: <http://\<host\>:\<port\>/av1_1080p.html>
* **type**: pageload
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-av1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-fenix-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(vpl-h264-c)=

::::{dropdown} vpl-h264
:class-container: anchor-id-vpl-h264-c

* **Command to Run Locally**

  ```
  ./mach raptor -t vpl-h264
  ```

**Owner**: :aosmond and the Media Team

**Description**: Measures H264 1080p video playback latency.

* **alert on**: [estimatedFirstFrameLatency](raptor-metrics.md#estimated-frame-latency-first), [estimatedAnyFrameLatency](raptor-metrics.md#estimated-frame-latency-any)
* **alert threshold**: 2.0
* **apps**: chrome, chrome-m, geckoview, fenix, firefox, safari
* **benchmark webserver**: true
* **browser cycles**: 20
* **expected**: pass
* **gecko profile features**: stackwalk,js,java,processcpu,screenshots,memory
* **gecko profile interval**: 1
* **gecko profile threads**: BackgroundThreadPool,Compositor,decoder,GeckoMain,gmp,graph,grph,InotifyEventThread,IPDL Background,media,ModuleProcessThread,PacerThread,RemVidChild,RenderBackend,Renderer,SwComposite,TextureUpdate,ImageBridgeChld
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-video-playback-latency.toml#31`
* **lower is better**: true
* **measure**: firstFrame, secondFrame, lastFrame, estimatedFirstFrameLatency, estimatedAnyFrameLatency
* **newtab per cycle**: true
* **page cycles**: 20
* **page timeout**: 20000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/video-playback-latency
* **repository revision**: e3d0295c15cfdd6f32e6d4270de0ed5d6c1632cd
* **sparse checkout**: true
* **support class**: video_playback_latency.py
* **test url**: <http://\<host\>:\<port\>/h264_1080p.html>
* **type**: pageload
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-h264**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-fenix-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx2700-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-safari-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(vpl-vp9-c)=

::::{dropdown} vpl-vp9
:class-container: anchor-id-vpl-vp9-c

* **Command to Run Locally**

  ```
  ./mach raptor -t vpl-vp9
  ```

**Owner**: :aosmond and the Media Team

**Description**: Measures VP9 1080p video playback latency.

* **alert on**: [estimatedFirstFrameLatency](raptor-metrics.md#estimated-frame-latency-first), [estimatedAnyFrameLatency](raptor-metrics.md#estimated-frame-latency-any)
* **alert threshold**: 2.0
* **apps**: chrome, chrome-m, geckoview, fenix, firefox, safari
* **benchmark webserver**: true
* **browser cycles**: 20
* **expected**: pass
* **gecko profile features**: stackwalk,js,java,processcpu,screenshots,memory
* **gecko profile interval**: 1
* **gecko profile threads**: BackgroundThreadPool,Compositor,decoder,GeckoMain,gmp,graph,grph,InotifyEventThread,IPDL Background,media,ModuleProcessThread,PacerThread,RemVidChild,RenderBackend,Renderer,SwComposite,TextureUpdate,ImageBridgeChld
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-video-playback-latency.toml#34`
* **lower is better**: true
* **measure**: firstFrame, secondFrame, lastFrame, estimatedFirstFrameLatency, estimatedAnyFrameLatency
* **newtab per cycle**: true
* **page cycles**: 20
* **page timeout**: 20000
* **preferences**: media.autoplay.default=0 media.autoplay.ask-permission=false media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **repository**: https://github.com/mozilla/perf-automation
* **repository path**: benchmarks/video-playback-latency
* **repository revision**: e3d0295c15cfdd6f32e6d4270de0ed5d6c1632cd
* **sparse checkout**: true
* **support class**: video_playback_latency.py
* **test url**: <http://\<host\>:\<port\>/vp9_1080p.html>
* **type**: pageload
* **unit**: ms
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-vp9**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-fenix-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-chrome-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-firefox-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(welcome-c)=

::::{dropdown} welcome
:class-container: anchor-id-welcome-c

* **Command to Run Locally**

  ```
  ./mach raptor -t welcome
  ```

**Owner**: PerfTest Team

**Description**: Measures pageload metrics for the first-install about:welcome page

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/custom/browsertime-welcome.toml#18`
* **lower is better**: true
* **measure**: fcp, loadtime, ContentfulSpeedIndex, PerceptualSpeedIndex, SpeedIndex, FirstVisualChange, LastVisualChange
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-welcome.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test script**: welcome.js
* **test url**: <about:welcome>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-first-install-firefox-welcome**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Desktop

Tests for page-load performance. The links direct to the actual websites that are being tested.

(amazon-d)=

::::{dropdown} amazon
:class-container: anchor-id-amazon-d

* **Command to Run Locally**

  ```
  ./mach raptor -t amazon
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#22`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm12-windows-firefox-amazon.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.amazon.com/Acer-A515-46-R14K-Quad-Core-Processor-Backlit/dp/B08VKNVDDR/ref=sr_1_3?dchild=1&keywords=laptop&qid=1627047187&sr=8-3>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.amazon.com/s?k=laptop&ref=nb_sb_noss_1>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-profiling-firefox-amazon**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-profiling-firefox-amazon**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-profiling-firefox-amazon**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(bing-search-d)=

::::{dropdown} bing-search
:class-container: anchor-id-bing-search-d

* **Command to Run Locally**

  ```
  ./mach raptor -t bing-search
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#28`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-darwin-firefox-bing-search.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.bing.com/search?q=barack+obama>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(buzzfeed-d)=

::::{dropdown} buzzfeed
:class-container: anchor-id-buzzfeed-d

* **Command to Run Locally**

  ```
  ./mach raptor -t buzzfeed
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#32`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-buzzfeed.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.buzzfeed.com/quizzes>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.buzzfeed.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(cnn-d)=

::::{dropdown} cnn
:class-container: anchor-id-cnn-d

* **Command to Run Locally**

  ```
  ./mach raptor -t cnn
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#38`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm12-darwin-firefox-cnn.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.cnn.com/weather>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.cnn.com/2021/03/22/weather/climate-change-warm-waters-lake-michigan/index.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(docomo-d)=

::::{dropdown} docomo
:class-container: anchor-id-docomo-d

* **Command to Run Locally**

  ```
  ./mach raptor -t docomo
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp7/desktop/browsertime-tp7.toml#23`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-docomo.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.docomo.ne.jp/iphone/?icid=CRP_menu_to_CRP_IPH>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.docomo.ne.jp/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-docomo**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ebay-d)=

::::{dropdown} ebay
:class-container: anchor-id-ebay-d

* **Command to Run Locally**

  ```
  ./mach raptor -t ebay
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#43`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-ebay.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.ebay.com/deals>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.ebay.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(espn-d)=

::::{dropdown} espn
:class-container: anchor-id-espn-d

* **Command to Run Locally**

  ```
  ./mach raptor -t espn
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#48`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-espn.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.espn.com/nba/draft/news>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.espn.com/nba/story/_/page/allstarweekend25788027/the-comparison-lebron-james-michael-jordan-their-own-words>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(expedia-d)=

::::{dropdown} expedia
:class-container: anchor-id-expedia-d

* **Command to Run Locally**

  ```
  ./mach raptor -t expedia
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#54`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-darwin-firefox-expedia.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://groups.expedia.com/Group-Rate/?locale=en_US&ol=1>
* **support class**: browsertime_pageload.py
* **test url**: <https://expedia.com/Hotel-Search?destination=New+York%2C+New+York&latLong=40.756680%2C-73.986470&regionId=178293&startDate=&endDate=&rooms=1&_xpid=11905%7C1&adults=2>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(fandom-d)=

::::{dropdown} fandom
:class-container: anchor-id-fandom-d

* **Command to Run Locally**

  ```
  ./mach raptor -t fandom
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#59`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-fandom.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.fandom.com/articles/fallout-76-will-live-and-die-on-the-creativity-of-its-playerbase>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-docs-d)=

::::{dropdown} google-docs
:class-container: anchor-id-google-docs-d

* **Command to Run Locally**

  ```
  ./mach raptor -t google-docs
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#64`
* **lower is better**: true
* **page complete wait time**: 8000
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm12-darwin-firefox-google-docs.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://docs.google.com/document/d/1vUnn0ePU-ynArE1OdxyEHXR2G0sl74ja_st_4OOzlgE/preview>
* **support class**: browsertime_pageload.py
* **test url**: <https://docs.google.com/document/d/1US-07msg12slQtI_xchzYxcKlTs6Fp7WqIc6W5GK5M8/edit?usp=sharing>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-search-d)=

::::{dropdown} google-search
:class-container: anchor-id-google-search-d

* **Command to Run Locally**

  ```
  ./mach raptor -t google-search
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#70`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-google-search.manifest
* **playback version**: 11.0.0
* **support class**: browsertime_pageload.py
* **test url**: <https://www.google.com/search?hl=en&q=barack+obama&cad=h>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-slides-d)=

::::{dropdown} google-slides
:class-container: anchor-id-google-slides-d

* **Command to Run Locally**

  ```
  ./mach raptor -t google-slides
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#76`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-google-slides.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://docs.google.com/presentation/d/e/2PACX-1vRWwPQQFMDkSR4RtYc7HSAGm-hoprespfemIk5SatJxd2o8yrFfrMeAAUkjdfalC5PpjJ9ZKgfsuQ1T/pub?start=false&loop=false&delayms=3000>
* **support class**: browsertime_pageload.py
* **test url**: <https://docs.google.com/presentation/d/1Ici0ceWwpFvmIb3EmKeWSq_vAQdmmdFcWqaiLqUkJng/edit?usp=sharing>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(imdb-d)=

::::{dropdown} imdb
:class-container: anchor-id-imdb-d

* **Command to Run Locally**

  ```
  ./mach raptor -t imdb
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#82`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-imdb.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.imdb.com/title/tt0084967/episodes/?ref_=tt_ov_epl>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.imdb.com/title/tt0084967/?ref_=nv_sr_2>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(imgur-d)=

::::{dropdown} imgur
:class-container: anchor-id-imgur-d

* **Command to Run Locally**

  ```
  ./mach raptor -t imgur
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#87`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-imgur.manifest
* **playback version**: 12.2.1
* **preferences**: media.autoplay.default=5 media.autoplay.ask-permission=true media.autoplay.blocking_policy=1 media.allowed-to-play.enabled=false media.block-autoplay-until-in-foreground=true
* **secondary url**: <https://imgur.com/gallery/firefox-logo-w8fgXyY>
* **support class**: browsertime_pageload.py
* **test url**: <https://imgur.com/gallery/mozilla-performance-testing-logo-sNUtC19>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(microsoft-d)=

::::{dropdown} microsoft
:class-container: anchor-id-microsoft-d

* **Command to Run Locally**

  ```
  ./mach raptor -t microsoft
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#99`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-microsoft.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://support.microsoft.com/en-us>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.microsoft.com/en-us/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(openai-d)=

::::{dropdown} openai
:class-container: anchor-id-openai-d

* **Command to Run Locally**

  ```
  ./mach raptor -t openai
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp7/desktop/browsertime-tp7.toml#27`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-openai.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://openai.com/blog>
* **support class**: browsertime_pageload.py
* **test url**: <https://openai.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-openai**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-openai**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-openai**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-d)=

::::{dropdown} reddit
:class-container: anchor-id-reddit-d

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#104`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-reddit.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.reddit.com/r/technology/>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.reddit.com/r/technology/comments/9sqwyh/we_posed_as_100_senators_to_run_ads_on_facebook/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(samsung-d)=

::::{dropdown} samsung
:class-container: anchor-id-samsung-d

* **Command to Run Locally**

  ```
  ./mach raptor -t samsung
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp7/desktop/browsertime-tp7.toml#32`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-samsung.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.samsung.com/us/mobile>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.samsung.com/us>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-samsung**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tiktok-d)=

::::{dropdown} tiktok
:class-container: anchor-id-tiktok-d

* **Command to Run Locally**

  ```
  ./mach raptor -t tiktok
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp7/desktop/browsertime-tp7.toml#28`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-tiktok.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.tiktok.com/explore>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.tiktok.com/en>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-tiktok**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(twitter-d)=

::::{dropdown} twitter
:class-container: anchor-id-twitter-d

* **Command to Run Locally**

  ```
  ./mach raptor -t twitter
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#110`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-twitter.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://twitter.com/BarackObama>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(weather-d)=

::::{dropdown} weather
:class-container: anchor-id-weather-d

* **Command to Run Locally**

  ```
  ./mach raptor -t weather
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp7/desktop/browsertime-tp7.toml#41`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-weather.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://weather.com/en-CA/weather/monthly/l/43.67%2C-79.42>
* **support class**: browsertime_pageload.py
* **test url**: <https://weather.com/en-CA/weather/today/l/43.67%2C-79.42>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-weather**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-chrome-weather**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp7-firefox-weather**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wikia-d)=

::::{dropdown} wikia
:class-container: anchor-id-wikia-d

* **Command to Run Locally**

  ```
  ./mach raptor -t wikia
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#115`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-wikia.manifest
* **playback version**: 11.0.0
* **secondary url**: <https://marvel.fandom.com/wiki/Celestials>
* **support class**: browsertime_pageload.py
* **test url**: <https://marvel.fandom.com/wiki/Black_Panther>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wikipedia-d)=

::::{dropdown} wikipedia
:class-container: anchor-id-wikipedia-d

* **Command to Run Locally**

  ```
  ./mach raptor -t wikipedia
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#122`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-wikipedia.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://en.wikipedia.org/wiki/Joe_Biden>
* **support class**: browsertime_pageload.py
* **test url**: <https://en.wikipedia.org/wiki/Barack_Obama>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-d)=

::::{dropdown} youtube
:class-container: anchor-id-youtube-d

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#127`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-youtube.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.youtube.com/watch?v=JrdEMERq8MA>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.youtube.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Interactive

Browsertime tests that interact with the webpage. Includes responsiveness tests as they make use of this support for navigation. This type of test allows the specification of browsertime commands through the test manifest.

(cnn-nav-i)=

::::{dropdown} cnn-nav
:class-container: anchor-id-cnn-nav-i

* **Command to Run Locally**

  ```
  ./mach raptor -t cnn-nav
  ```

**Owner**: PerfTest Team

**Description**: Navigates to cnn main page, then to the world sub-page.

* **accept zero vismet**: true
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 10
* **expected**: pass
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#25`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-cnn-nav.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test cmds**: ['measure.start', 'landing'] ['navigate', 'https://www.cnn.com'] ['wait.byTime', '4000'] ['measure.stop', ''] ['measure.start', 'world'] ['click.byXpathAndWait', '/html/body/div[5]/div/div/header/div/div[1]/div/div[2]/nav/ul/li[2]/a'] ['wait.byTime', '1000'] ['measure.stop', '']
* **test url**: <https://www.cnn.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-billgates-ama-i)=

::::{dropdown} reddit-billgates-ama
:class-container: anchor-id-reddit-billgates-ama-i

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit-billgates-ama
  ```

**Owner**: PerfTest Team

**Description**: Navigates from the Bill Gates AMA to the Reddit members section.

* **accept zero vismet**: true
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 10
* **expected**: pass
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#40`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 240000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-windows-firefox-reddit-billgates-ama.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test cmds**: ['measure.start', 'billg-ama'] ['navigate', 'https://www.reddit.com/r/IAmA/comments/m8n4vt/im_bill_gates_cochair_of_the_bill_and_melinda/'] ['wait.byTime', '5000'] ['measure.stop', ''] ['measure.start', 'members'] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div[3]/div[2]/div/div[1]/div/div[4]/div[1]/div'] ['wait.byTime', '1000'] ['measure.stop', '']
* **test url**: <https://www.reddit.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-billgates-post-1-i)=

::::{dropdown} reddit-billgates-post-1
:class-container: anchor-id-reddit-billgates-post-1-i

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit-billgates-post-1
  ```

**Owner**: PerfTest Team

**Description**: Navigates the `thisisbillgates` user starting at the main user page, then to the posts, comments, hot, and top sections.

* **accept zero vismet**: true
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 10
* **expected**: pass
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#57`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 90000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-windows-firefox-reddit-billgates-post.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test cmds**: ['measure.start', 'billg'] ['navigate', 'https://www.reddit.com/user/thisisbillgates/'] ['wait.byTime', '500'] ['measure.stop', ''] ['measure.start', 'posts'] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div/div/div[2]/div[2]/div/div/div/a[2]'] ['wait.byTime', '500'] ['measure.stop', ''] ['measure.start', 'comments'] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div/div/div[2]/div[2]/div/div/div/a[3]'] ['wait.byTime', '500'] ['measure.stop', ''] ['wait.byTime', '500']
* **test url**: <https://www.reddit.com/user/thisisbillgates/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-billgates-post-2-i)=

::::{dropdown} reddit-billgates-post-2
:class-container: anchor-id-reddit-billgates-post-2-i

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit-billgates-post-2
  ```

**Owner**: PerfTest Team

**Description**: Navigates the `thisisbillgates` user starting at the main user page, then to the posts, comments, hot, and top sections.

* **accept zero vismet**: true
* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari
* **browser cycles**: 10
* **expected**: pass
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#79`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 90000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-windows-firefox-reddit-billgates-post.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test cmds**: ['measure.start', 'billg'] ['navigate', 'https://www.reddit.com/user/thisisbillgates/'] ['wait.byTime', '500'] ['measure.stop', ''] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div/div/div[2]/div[2]/div/div/div/a[3]'] ['wait.byTime', '500'] ['measure.start', 'hot'] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div/div/div[2]/div[4]/div[1]/div[1]/div[2]/a[2]'] ['wait.byTime', '500'] ['measure.stop', ''] ['measure.start', 'top'] ['click.byXpathAndWait', '/html/body/div[1]/div/div[2]/div[2]/div/div/div/div[2]/div[4]/div[1]/div[1]/div[2]/a[3]'] ['wait.byTime', '500'] ['measure.stop', ''] ['wait.byTime', '500']
* **test url**: <https://www.reddit.com/user/thisisbillgates/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-clang-trunk/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64-shippable/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1470-64/opt <hardware-mac-mini-r8>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Live

A set of test pages that are run as live sites instead of recorded versions. These tests are available on all browsers, on all platforms.

(booking-sf-l)=

::::{dropdown} booking-sf
:class-container: anchor-id-booking-sf-l

* **Command to Run Locally**

  ```
  ./mach raptor -t booking-sf
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#17`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://www.booking.com/hotel/us/edwardian-san-francisco.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(discord-l)=

::::{dropdown} discord
:class-container: anchor-id-discord-l

* **Command to Run Locally**

  ```
  ./mach raptor -t discord
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#20`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://discordapp.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-discord**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-discord**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(fashionbeans-l)=

::::{dropdown} fashionbeans
:class-container: anchor-id-fashionbeans-l

* **Command to Run Locally**

  ```
  ./mach raptor -t fashionbeans
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#23`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://fashionbeans.com/article/coolest-menswear-stores-in-the-world>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-accounts-l)=

::::{dropdown} google-accounts
:class-container: anchor-id-google-accounts-l

* **Command to Run Locally**

  ```
  ./mach raptor -t google-accounts
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#26`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://accounts.google.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(imdb-firefox-l)=

::::{dropdown} imdb-firefox
:class-container: anchor-id-imdb-firefox-l

* **Command to Run Locally**

  ```
  ./mach raptor -t imdb-firefox
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#29`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://m.imdb.com/title/tt0083943/>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(medium-article-l)=

::::{dropdown} medium-article
:class-container: anchor-id-medium-article-l

* **Command to Run Locally**

  ```
  ./mach raptor -t medium-article
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#32`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://medium.com/s/coincidences-are-a-lie/could-america-have-also-been-the-birthplace-of-impressionism-cb3d31a2e22d>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(people-article-l)=

::::{dropdown} people-article
:class-container: anchor-id-people-article-l

* **Command to Run Locally**

  ```
  ./mach raptor -t people-article
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#35`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://people.com/amp-stories/royal-a-to-z/>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-thread-l)=

::::{dropdown} reddit-thread
:class-container: anchor-id-reddit-thread-l

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit-thread
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#38`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://www.reddit.com/r/firefox/comments/7dkq03/its_been_a_while/>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(rumble-fox-l)=

::::{dropdown} rumble-fox
:class-container: anchor-id-rumble-fox-l

* **Command to Run Locally**

  ```
  ./mach raptor -t rumble-fox
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#41`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://rumble.com/v3c44t-foxes-jumping-on-my-trampoline.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(stackoverflow-question-l)=

::::{dropdown} stackoverflow-question
:class-container: anchor-id-stackoverflow-question-l

* **Command to Run Locally**

  ```
  ./mach raptor -t stackoverflow-question
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#44`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://stackoverflow.com/questions/927358/how-do-i-undo-the-most-recent-commits-in-git>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(urbandictionary-define-l)=

::::{dropdown} urbandictionary-define
:class-container: anchor-id-urbandictionary-define-l

* **Command to Run Locally**

  ```
  ./mach raptor -t urbandictionary-define
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#47`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://urbandictionary.com/define.php?term=awesome%20sauce>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wikia-marvel-l)=

::::{dropdown} wikia-marvel
:class-container: anchor-id-wikia-marvel-l

* **Command to Run Locally**

  ```
  ./mach raptor -t wikia-marvel
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, firefox
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/live/browsertime-live.toml#50`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **support class**: browsertime_pageload.py
* **test url**: <https://marvel.wikia.com/wiki/Black_Panther>
* **type**: pageload
* **unit**: ms
* **use live sites**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Mobile

Page-load performance test suite on Android. The links direct to the actual websites that are being tested.

(allrecipes-m)=

::::{dropdown} allrecipes
:class-container: anchor-id-allrecipes-m

* **Command to Run Locally**

  ```
  ./mach raptor -t allrecipes
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#22`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm8-android-gve-allrecipes.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.allrecipes.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-allrecipes**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(amazon-m)=

::::{dropdown} amazon
:class-container: anchor-id-amazon-m

* **Command to Run Locally**

  ```
  ./mach raptor -t amazon
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#27`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-amazon.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.amazon.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-amazon**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-amazon**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(amazon-search-m)=

::::{dropdown} amazon-search
:class-container: anchor-id-amazon-search-m

* **Command to Run Locally**

  ```
  ./mach raptor -t amazon-search
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#30`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-amazon-search.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.amazon.com/s/ref=nb_sb_noss_2/139-6317191-5622045?url=search-alias%3Daps&field-keywords=mobile+phone>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-amazon-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(bild-de-m)=

::::{dropdown} bild-de
:class-container: anchor-id-bild-de-m

* **Command to Run Locally**

  ```
  ./mach raptor -t bild-de
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#33`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm8-android-fenix-bild-de.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.bild.de/sport/american-football/nfl/super-bowl-rekordquote-fuer-die-nfl-87139668.bild.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bild-de**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bild-de**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(bing-m)=

::::{dropdown} bing
:class-container: anchor-id-bing-m

* **Command to Run Locally**

  ```
  ./mach raptor -t bing
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#38`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-bing.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.bing.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bing**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bing**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bing**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(bing-search-restaurants-m)=

::::{dropdown} bing-search-restaurants
:class-container: anchor-id-bing-search-restaurants-m

* **Command to Run Locally**

  ```
  ./mach raptor -t bing-search-restaurants
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#41`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-bing-search-restaurants.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.bing.com/search?q=restaurants+in+exton+pa+19341>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bing-search-restaurants**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-bing-search-restaurants**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(booking-m)=

::::{dropdown} booking
:class-container: anchor-id-booking-m

* **Command to Run Locally**

  ```
  ./mach raptor -t booking
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#44`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-booking.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.booking.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-booking**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-booking**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-booking**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-booking**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(cnn-m)=

::::{dropdown} cnn
:class-container: anchor-id-cnn-m

* **Command to Run Locally**

  ```
  ./mach raptor -t cnn
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#47`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-cnn.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://cnn.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-cnn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-cnn**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(cnn-ampstories-m)=

::::{dropdown} cnn-ampstories
:class-container: anchor-id-cnn-ampstories-m

* **Command to Run Locally**

  ```
  ./mach raptor -t cnn-ampstories
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#50`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-cnn-ampstories.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://cnn.com/ampstories/us/why-hurricane-michael-is-a-monster-unlike-any-other>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-cnn-ampstories**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-cnn-ampstories**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(dailymail-m)=

::::{dropdown} dailymail
:class-container: anchor-id-dailymail-m

* **Command to Run Locally**

  ```
  ./mach raptor -t dailymail
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#53`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-dailymail.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.dailymail.co.uk/sciencetech/article-9749081/Experts-say-Hubble-repair-despite-NASA-insisting-multiple-options-fix.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-dailymail**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-dailymail**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ebay-kleinanzeigen-m)=

::::{dropdown} ebay-kleinanzeigen
:class-container: anchor-id-ebay-kleinanzeigen-m

* **Command to Run Locally**

  ```
  ./mach raptor -t ebay-kleinanzeigen
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#56`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-ebay-kleinanzeigen.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.ebay-kleinanzeigen.de>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ebay-kleinanzeigen-search-m)=

::::{dropdown} ebay-kleinanzeigen-search
:class-container: anchor-id-ebay-kleinanzeigen-search-m

* **Command to Run Locally**

  ```
  ./mach raptor -t ebay-kleinanzeigen-search
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#59`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-ebay-kleinanzeigen-search.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.ebay-kleinanzeigen.de/s-anzeigen/auf-zeit-wg-berlin/zimmer/c199-l3331>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-search**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-search**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(espn-m)=

::::{dropdown} espn
:class-container: anchor-id-espn-m

* **Command to Run Locally**

  ```
  ./mach raptor -t espn
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#62`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-espn.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <http://www.espn.com/nba/story/_/page/allstarweekend25788027/the-comparison-lebron-james-michael-jordan-their-own-words>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-espn**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-m)=

::::{dropdown} google
:class-container: anchor-id-google-m

* **Command to Run Locally**

  ```
  ./mach raptor -t google
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#65`
* **login**: true
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-g5-fenix-google.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.google.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-google**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-google**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-maps-m)=

::::{dropdown} google-maps
:class-container: anchor-id-google-maps-m

* **Command to Run Locally**

  ```
  ./mach raptor -t google-maps
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#70`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-google-maps.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.google.com/maps?force=pwa>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-google-maps**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-google-maps**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(google-search-restaurants-m)=

::::{dropdown} google-search-restaurants
:class-container: anchor-id-google-search-restaurants-m

* **Command to Run Locally**

  ```
  ./mach raptor -t google-search-restaurants
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#73`
* **login**: true
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-g5-fenix-google-search-restaurants.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.google.com/search?q=restaurants+near+me>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-google-search-restaurants**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-google-search-restaurants**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(imdb-m)=

::::{dropdown} imdb
:class-container: anchor-id-imdb-m

* **Command to Run Locally**

  ```
  ./mach raptor -t imdb
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#78`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-imdb.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.imdb.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-imdb**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-imdb**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(microsoft-support-m)=

::::{dropdown} microsoft-support
:class-container: anchor-id-microsoft-support-m

* **Command to Run Locally**

  ```
  ./mach raptor -t microsoft-support
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#81`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-microsoft-support.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://support.microsoft.com/en-us>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-microsoft-support**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(reddit-m)=

::::{dropdown} reddit
:class-container: anchor-id-reddit-m

* **Command to Run Locally**

  ```
  ./mach raptor -t reddit
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#84`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-reddit.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.reddit.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-reddit**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-reddit**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(stackoverflow-m)=

::::{dropdown} stackoverflow
:class-container: anchor-id-stackoverflow-m

* **Command to Run Locally**

  ```
  ./mach raptor -t stackoverflow
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#87`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-stackoverflow.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://stackoverflow.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-stackoverflow**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-stackoverflow**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(wikipedia-m)=

::::{dropdown} wikipedia
:class-container: anchor-id-wikipedia-m

* **Command to Run Locally**

  ```
  ./mach raptor -t wikipedia
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#90`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-wikipedia.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://en.m.wikipedia.org/wiki/Main_Page>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-wikipedia**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-wikipedia**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-m)=

::::{dropdown} youtube
:class-container: anchor-id-youtube-m

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#93`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-youtube.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.youtube.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-youtube**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-youtube**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(youtube-watch-m)=

::::{dropdown} youtube-watch
:class-container: anchor-id-youtube-watch-m

* **Command to Run Locally**

  ```
  ./mach raptor -t youtube-watch
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#96`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-youtube-watch.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.youtube.com/watch?v=COU5T-Wafa4>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-youtube-watch**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-fenix-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Nav-bench

A composite benchmark that measures page loading and sub-navigation performance across a set of representative sites. Not all sites perform sub-navigation; some are measured for loading only. Produces a single score (higher is better) derived from the geomean of SpeedIndex across all sites.


(nav-bench-n)=

::::{dropdown} nav-bench
:class-container: anchor-id-nav-bench-n

* **Command to Run Locally**

  ```
  ./mach raptor -t nav-bench
  ```

**Owner**: PerfTest Team

**Description**: Runs the nav-bench composite across Amazon, BBC, DuckDuckGo, Facebook, Google, Google Docs, Reddit, Wikipedia, and Yahoo in a single combined session.


* **accept zero vismet**: false
* **alert on**: [SpeedIndex](raptor-metrics.md#speed-index)
* **alert threshold**: 2.0
* **apps**: firefox, chrome
* **browser cycles**: 5
* **browsertime visualmetrics**: true
* **cold**: true
* **custom data**: true
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/nav-bench/browsertime-nav-bench.toml#60`
* **lower is better**: false
* **measure**: SpeedIndex
* **page cycles**: 1
* **page timeout**: 600000
* **playback**: mitmproxy
* **playback manifests dir**: nav-bench
* **playback mode**: direct
* **playback pageset manifest**: mitm12-darwin-firefox-amazon-nav.manifest,mitm12-darwin-firefox-bbc-nav.manifest,mitm12-darwin-firefox-duckduckgo-nav.manifest,mitm12-darwin-firefox-facebook-nav.manifest,mitm12-darwin-firefox-google-docs-nav.manifest,mitm12-darwin-firefox-google-nav.manifest,mitm12-darwin-firefox-reddit-nav.manifest,mitm12-darwin-firefox-wikipedia-nav.manifest,mitm12-darwin-firefox-yahoo-nav.manifest
* **playback version**: 12.2.1
* **preferences**: fission.bfcacheInParent=false browser.sessionhistory.max_total_viewers=0 network.http.http3.enable=false network.http.altsvc.enabled=false
* **support class**: nav_bench.py
* **test cmds**: ['wait.byTime', '30000'] ['measure.start', 'google-nav-load'] ['navigate', 'https://www.google.com/'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'facebook-nav-load'] ['navigate', 'https://www.facebook.com/Cristiano'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'yahoo-nav-load'] ['navigate', 'https://ca.yahoo.com/'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'amazon-nav-load'] ['navigate', 'https://www.amazon.ca/s?k=laptop&ref=nb_sb_noss_1'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'bbc-nav-load'] ['navigate', 'https://www.bbc.com/news/'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'bbc-nav-subnav'] ['click.bySelectorAndWait', 'a[href*="bbcindepth"]'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'duckduckgo-nav-load'] ['navigate', 'https://duckduckgo.com/?q=mandelbrot+set'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'duckduckgo-nav-subnav'] ['click.bySelectorAndWait', 'nav ul li:nth-of-type(2) > a'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'reddit-nav-load'] ['navigate', 'https://www.reddit.com/r/IAmA/comments/m8n4vt/im_bill_gates_cochair_of_the_bill_and_melinda/'] ['wait.byTime', '3000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'google-docs-nav-load'] ['navigate', 'https://docs.google.com/document/d/1US-07msg12slQtI_xchzYxcKlTs6Fp7WqIc6W5GK5M8/edit?usp=sharing'] ['wait.byTime', '3000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'wikipedia-nav-load'] ['navigate', 'https://en.wikipedia.org/wiki/Barack_Obama'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'google-docs-nav-warm'] ['navigate', 'https://docs.google.com/document/d/1US-07msg12slQtI_xchzYxcKlTs6Fp7WqIc6W5GK5M8/edit?usp=sharing'] ['wait.byTime', '3000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000']
* **test url**: <about:blank>
* **type**: pageload
* **unit**: score
* **use live sites**: false
* **verbose**: true
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-nightlyasrelease/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-linux2404-64/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-nightlyasrelease/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-macosx1500-aarch64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref-shippable/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-hw-ref/opt <hardware-nuc12-windows11-reference>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-nightlyasrelease/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2-shippable/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-24h2/opt <hardware-nuc13-windows11>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-firefox-nav-bench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(nav-bench-m-n)=

::::{dropdown} nav-bench-m
:class-container: anchor-id-nav-bench-m-n

* **Command to Run Locally**

  ```
  ./mach raptor -t nav-bench-m
  ```

**Owner**: PerfTest Team

**Description**: Fenix navigation benchmark across Amazon, Wikipedia, DuckDuckGo, and BBC in a single combined session.


* **accept zero vismet**: false
* **alert on**: [SpeedIndex](raptor-metrics.md#speed-index)
* **alert threshold**: 5.0
* **apps**: fenix
* **browser cycles**: 5
* **browsertime visualmetrics**: true
* **cold**: true
* **custom data**: true
* **expected**: pass
* **expose browser profiler**: true
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/nav-bench/browsertime-nav-bench-m.toml#44`
* **lower is better**: false
* **measure**: SpeedIndex
* **page cycles**: 1
* **page timeout**: 600000
* **playback**: mitmproxy-android
* **playback manifests dir**: nav-bench
* **playback mode**: direct
* **playback pageset manifest**: mitm12-android-gve-amazon-nav.manifest,mitm12-android-gve-wikipedia-nav.manifest,mitm12-android-gve-duckduckgo-nav.manifest,mitm12-android-gve-bbc-nav.manifest
* **playback version**: 12.2.1
* **preferences**: fission.bfcacheInParent=false browser.sessionhistory.max_total_viewers=0 network.http.http3.enable=false network.http.altsvc.enabled=false
* **support class**: nav_bench.py
* **test cmds**: ['wait.byTime', '30000'] ['measure.start', 'amazon-nav-load'] ['navigate', 'https://www.amazon.ca/s?k=laptop'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'amazon-nav-subnav'] ['click.bySelectorAndWait', 'a[href*="/dp/"]'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'wikipedia-nav-load'] ['navigate', 'https://en.m.wikipedia.org/wiki/Barack_Obama'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'wikipedia-nav-subnav'] ['click.bySelectorAndWait', 'a[href*="/wiki/Honolulu"]'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'duckduckgo-nav-load'] ['navigate', 'https://duckduckgo.com/?q=mandelbrot+set'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'duckduckgo-nav-subnav'] ['click.bySelectorAndWait', 'a[href*="ia=images"]'] ['wait.byTime', '3000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'bbc-nav-load'] ['navigate', 'https://www.bbc.com/news/'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000']
* **test url**: <about:blank>
* **type**: pageload
* **unit**: score
* **use live sites**: false
* **verbose**: true
* **Test Task**:

:::{list-table} **{ref}`test-android-em-14-arm64-shippable/opt <hardware-mac-mini-m4>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-m-fenix-nav-bench-m**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-nightlyasrelease/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-m-fenix-nav-bench-m**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64-shippable/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-m-fenix-nav-bench-m**
  - ❌
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-android-hw-a55-14-0-aarch64/opt <hardware-samsung-a55>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-nav-bench-m-fenix-nav-bench-m**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::


### Scenario

Tests that perform a specific action (a scenario), i.e. idle application, idle application in background, etc.

(idle-s)=

::::{dropdown} idle
:class-container: anchor-id-idle-s

* **Command to Run Locally**

  ```
  ./mach raptor -t idle
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, refbrow
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/scenario/idle.toml#15`
* **lower is better**: true
* **measure**: fakeMeasure
* **page cycles**: 1
* **page timeout**: 1320000
* **scenario time**: 1200000
* **support class**: browsertime_pageload.py
* **test url**: <about:blank>
* **type**: scenario
* **unit**: scenarioComplete
::::

(idle-bg-s)=

::::{dropdown} idle-bg
:class-container: anchor-id-idle-bg-s

* **Command to Run Locally**

  ```
  ./mach raptor -t idle-bg
  ```

**Owner**: PerfTest Team

* **alert threshold**: 2.0
* **apps**: fenix, geckoview, refbrow
* **browsertime args**: --browsertime.scenario_time=60000 --browsertime.background_app=false
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/scenario/idle.toml#17`
* **lower is better**: true
* **measure**: fakeMeasure
* **page cycles**: 1
* **page timeout**: 1320000
* **scenario time**: 1200000
* **support class**: browsertime_pageload.py
* **test url**: <about:blank>
* **type**: scenario
* **unit**: scenarioComplete
::::


### Unittests

These tests aren't used in standard testing, they are only used in the Raptor unit tests (they are similar to raptor-tp6 tests).

(test-page-1-u)=

::::{dropdown} test-page-1
:class-container: anchor-id-test-page-1-u

* **Command to Run Locally**

  ```
  ./mach raptor -t test-page-1
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time)
* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **gecko profile entries**: 14000000
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/unittests/browsertime-tp6-unittest.toml#17`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-test-page-1.manifest
* **playback version**: 12.2.1
* **test url**: <https://www.amazon.com/s?k=laptop&ref=nb_sb_noss_1>
* **type**: pageload
* **unit**: ms
::::

(test-page-2-u)=

::::{dropdown} test-page-2
:class-container: anchor-id-test-page-2-u

* **Command to Run Locally**

  ```
  ./mach raptor -t test-page-2
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time)
* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **gecko profile entries**: 14000000
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/unittests/browsertime-tp6-unittest.toml#20`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-test-page-2.manifest
* **playback version**: 12.2.1
* **test url**: <https://www.facebook.com>
* **type**: pageload
* **unit**: ms
::::

(test-page-3-u)=

::::{dropdown} test-page-3
:class-container: anchor-id-test-page-3-u

* **Command to Run Locally**

  ```
  ./mach raptor -t test-page-3
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time)
* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **gecko profile entries**: 14000000
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/unittests/browsertime-tp6-unittest.toml#23`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-test-page-3.manifest
* **playback version**: 12.2.1
* **test url**: <https://www.google.com/search?hl=en&q=barack+obama&cad=h>
* **type**: pageload
* **unit**: ms
::::

(test-page-4-u)=

::::{dropdown} test-page-4
:class-container: anchor-id-test-page-4-u

* **Command to Run Locally**

  ```
  ./mach raptor -t test-page-4
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time)
* **alert threshold**: 2.0
* **apps**: firefox
* **expected**: pass
* **gecko profile entries**: 14000000
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/unittests/browsertime-tp6-unittest.toml#26`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-test-page-4.manifest
* **playback version**: 12.2.1
* **test url**: <https://www.youtube.com>
* **type**: pageload
* **unit**: ms
::::


