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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-safari-assorted-dom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream2-chrome-m-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-cstm-car-m-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream2-fenix-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


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
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream2-chrome-m-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-cstm-car-m-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream2-fenix-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-fenix-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream2-geckoview-jetstream2-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-custom-car-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-chrome-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-jetstream2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-jetstream3-fenix-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-jetstream3-chrome-m-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-cstm-car-m-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-jetstream3-fenix-jetstream3-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-jetstream3-fenix-jetstream3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-jetstream3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-jetstream3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-firefox-media-capabilities**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,CanvasWorkers,TextureUpdate
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,CanvasWorkers,TextureUpdate
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-motionmark-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,CanvasWorkers,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-desktop.toml#29`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-htmlsuite-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **gecko profile features**: stackwalk,js,java,processcpu,memory
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,SwComposite,RenderBackend,SceneBuilder,WrWorker,CanvasWorkers,TextureUpdate
* **host from parent**: false
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/motionmark-1-3-mobile.toml#28`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-motionmark-1-3-chrome-m-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-cstm-car-m-motionmark-htmlsuite-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-motionmark-1-3-fenix-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-motionmark-1-3-geckoview-motionmark-htmlsuite-1-3-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-motionmark-htmlsuite-1-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-motionmark-htmlsuite-1-3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **gather cpuTime**: true
* **gecko profile features**: stackwalk,js,processcpu,nomarkerstacks
* **gecko profile interval**: 1
* **gecko profile threads**: GeckoMain,Compositor,Renderer,TaskController,StyleThread
* **host from parent**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/speedometer-desktop.toml#31`
* **lower is better**: false
* **page cycles**: 5
* **page timeout**: 600000
* **preferences**: media.autoplay.default=0 media.autoplay.blocking_policy=0 media.allowed-to-play.enabled=true media.block-autoplay-until-in-foreground=false
* **repository**: https://github.com/mozilla/Speedometer
* **repository revision**: 89435e5dfb97cf793516449370f14b2176b4aed7
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: speedometer3.py
* **test script**: speedometer3.js
* **test url**: <http://\<host\>:\<port\>/index.html?raptor&tags=experimental>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer2-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-cstm-car-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer2-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
* - **browsertime-benchmark-safari-speedometer2**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

* **alert threshold**: 2.0
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
* **subtest lower is better**: true
* **subtest unit**: ms
* **support class**: speedometer3.py
* **test script**: speedometer3.js
* **test url**: <http://\<host\>:\<port\>/Speedometer3/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **test-android-em-14-arm64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
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

:::{list-table} **test-android-em-14-arm64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-speedometer3-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-speedometer3-mobile-cstm-car-m-nofis**
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
* - **browsertime-benchmark-speedometer3-mobile-fenix-nofis**
  - ✅
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
* - **browsertime-benchmark-speedometer3-mobile-geckoview-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ✅
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* - **browsertime-benchmark-firefox-speedometer3-native-profiling-no-nv**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-firefox-speedometer3-no-nv**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-safari-stylebench**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/benchmarks/twitch-animation.toml#16`
* **lower is better**: true
* **page cycles**: 1
* **page timeout**: 2000000
* **perfstats**: false
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-unity-webgl-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview-nofis**
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


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **subtest lower is better**: false
* **subtest unit**: score
* **test url**: <http://\<host\>:\<port\>/index.html?raptor>
* **type**: benchmark
* **unit**: score
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-benchmark-unity-webgl-mobile-chrome-m-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-fenix-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-benchmark-unity-webgl-mobile-fenix-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-benchmark-unity-webgl-mobile-geckoview-nofis**
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


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-wasm-safari-wasm-godot**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-benchmark-safari-webaudio**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-av1-sfr-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-av1-sfr-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-h264-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-h264-720p60-nofis**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-mobile-geckoview-youtube-playback-h264-sfr-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-mobile-geckoview-youtube-playback-h264-sfr-nofis**
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


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-hfr-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-hfr-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-mobile-geckoview-youtube-playback-hfr-nofis**
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


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-p6-13-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-s24-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-power-mobile-chrome-m-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-fenix-youtube-playback-vp9-720p60-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-power-mobile-geckoview-youtube-playback-vp9-720p60-nofis**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-vp9-sfr-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-mobile-fenix-youtube-playback-vp9-sfr-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-hev3-connection-m-chrome-m-baseline-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-dns-multi-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-dns-multi-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-dns-native-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-dns-native-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3off-dns-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3off-dns-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3off-doh-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3off-doh-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3on-dns-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3on-dns-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3on-doh-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-hev3-connection-m-fenix-he3on-doh-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-cloudf-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-cloudf-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-multi-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-multi-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-next-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-next-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-w-clou-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-w-clou-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-w-next-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-trr-performance-m-fenix-trr-w-next-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-upload-safari-upload**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-upload-safari-upload-h3**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-q-i420**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-av1-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-av1-rt-i420**
  - ✅
  - ✅
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-q-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd**
  - ✅
  - ✅
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-q-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-q-sd-cam**
  - ✅
  - ✅
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-rt-sd**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd**
  - ✅
  - ✅
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

:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-h264-rt-sd-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-h264-rt-sd-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-q-i420**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp8-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp8-rt-i420**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-q**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-q-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-q-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-q-i420**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-rt**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-rt-cam**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-cam**
  - ✅
  - ✅
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-webcodecs-safari-ve-vp9-rt-i420**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-webcodecs-firefox-ve-vp9-rt-i420**
  - ✅
  - ✅
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-av1-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-av1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-av1-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-video-playback-latency-mobile-fenix-vpl-av1-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-av1-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-h264-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-h264**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-h264-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-video-playback-latency-mobile-fenix-vpl-h264-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-h264-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
* - **browsertime-video-playback-latency-safari-vpl-h264**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-video-playback-latency-mobile-chrome-m-vpl-vp9-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-vp9**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-fenix-vpl-vp9-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-video-playback-latency-mobile-fenix-vpl-vp9-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-video-playback-latency-mobile-geckoview-vpl-vp9-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-amazon**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-amazon**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-webextensions-firefox-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-bing-search**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-bing-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-bing-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-buzzfeed**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-buzzfeed**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-buzzfeed**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-sheriffed-firefox-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-ebay**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-ebay**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-ebay**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-ebay**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-espn**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-espn**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-expedia**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-expedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-expedia**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(facebook-d)=

::::{dropdown} facebook
:class-container: anchor-id-facebook-d

* **Command to Run Locally**

  ```
  ./mach raptor -t facebook
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#59`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-facebook.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.facebook.com/marketplace/?ref=bookmark>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.facebook.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-facebook**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-facebook**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-facebook**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#65`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-fandom**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-fandom**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-fandom**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#70`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-docs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-docs**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-docs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-google-docs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(google-mail-d)=

::::{dropdown} google-mail
:class-container: anchor-id-google-mail-d

* **Command to Run Locally**

  ```
  ./mach raptor -t google-mail
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#76`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-google-mail.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://mail.google.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-google-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-mail**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#81`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-search**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-google-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-google-search**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#87`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-slides**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-google-slides**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-google-slides**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#93`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-imdb**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-imdb**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-imdb**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#98`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-imgur**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-imgur**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-imgur**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(instagram-d)=

::::{dropdown} instagram
:class-container: anchor-id-instagram-d

* **Command to Run Locally**

  ```
  ./mach raptor -t instagram
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#110`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-darwin-firefox-instagram.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.instagram.com/nobelprize_org/>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.instagram.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(linkedin-d)=

::::{dropdown} linkedin
:class-container: anchor-id-linkedin-d

* **Command to Run Locally**

  ```
  ./mach raptor -t linkedin
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#115`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-linux-firefox-linkedin.manifest
* **playback version**: 8.1.1
* **secondary url**: <https://www.linkedin.com/in/thommy-harris-hk-385723106/>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.linkedin.com/feed/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-linkedin**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-linkedin**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-linkedin**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-linkedin**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#121`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-microsoft**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-microsoft**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-microsoft**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-microsoft**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(netflix-d)=

::::{dropdown} netflix
:class-container: anchor-id-netflix-d

* **Command to Run Locally**

  ```
  ./mach raptor -t netflix
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#126`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-linux-firefox-netflix.manifest
* **playback version**: 8.1.1
* **secondary url**: <https://www.netflix.com/title/699257>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.netflix.com/title/80117263>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-netflix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-netflix**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-netflix**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(nytimes-d)=

::::{dropdown} nytimes
:class-container: anchor-id-nytimes-d

* **Command to Run Locally**

  ```
  ./mach raptor -t nytimes
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#132`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-nytimes.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.nytimes.com/section/opinion/columnists>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.nytimes.com/2020/02/19/opinion/surprise-medical-bill.html>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-nytimes**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-nytimes**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-nytimes**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(office-d)=

::::{dropdown} office
:class-container: anchor-id-office-d

* **Command to Run Locally**

  ```
  ./mach raptor -t office
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#136`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm7-linux-firefox-office.manifest
* **playback version**: 12.2.1
* **secondary url**: <https://www.office.com/>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.office.com/launch/powerpoint?auth=1>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-office**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-office**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-office**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-office**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-office**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-office**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(outlook-d)=

::::{dropdown} outlook
:class-container: anchor-id-outlook-d

* **Command to Run Locally**

  ```
  ./mach raptor -t outlook
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#140`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-live.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://outlook.live.com/mail/inbox>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-outlook**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-outlook**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-outlook**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(paypal-d)=

::::{dropdown} paypal
:class-container: anchor-id-paypal-d

* **Command to Run Locally**

  ```
  ./mach raptor -t paypal
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#144`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-paypal.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.paypal.com/myaccount/summary/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-paypal**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-paypal**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-paypal**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(pinterest-d)=

::::{dropdown} pinterest
:class-container: anchor-id-pinterest-d

* **Command to Run Locally**

  ```
  ./mach raptor -t pinterest
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#148`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-linux-firefox-pinterest.manifest
* **playback version**: 8.1.1
* **secondary url**: <https://www.pinterest.com/today/best/halloween-costumes-for-your-furry-friends/75787/>
* **support class**: browsertime_pageload.py
* **test url**: <https://pinterest.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-pinterest**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-pinterest**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-pinterest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-pinterest**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#155`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-reddit**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-reddit**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-reddit**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

(tumblr-d)=

::::{dropdown} tumblr
:class-container: anchor-id-tumblr-d

* **Command to Run Locally**

  ```
  ./mach raptor -t tumblr
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#161`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm6-linux-firefox-tumblr.manifest
* **playback version**: 8.1.1
* **secondary url**: <https://www.tumblr.com/tagged/funny+cats?sort=top>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.tumblr.com/dashboard>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-tumblr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-tumblr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-tumblr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(twitch-d)=

::::{dropdown} twitch
:class-container: anchor-id-twitch-d

* **Command to Run Locally**

  ```
  ./mach raptor -t twitch
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#167`
* **lower is better**: true
* **measure**: fcp, loadtime, ContentfulSpeedIndex, PerceptualSpeedIndex, SpeedIndex, FirstVisualChange, LastVisualChange
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm8-linux-firefox-twitch.manifest
* **playback version**: 12.2.1
* **preferences**: media.autoplay.default=5 media.autoplay.ask-permission=true media.autoplay.blocking_policy=1 media.allowed-to-play.enabled=false media.block-autoplay-until-in-foreground=true
* **secondary url**: <https://www.twitch.tv/gmashley>
* **support class**: browsertime_pageload.py
* **test url**: <https://www.twitch.tv/videos/894226211>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-twitch**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-twitch**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-firefox-twitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitch**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#180`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-twitter**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-twitter**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-twitter**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-twitter**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#185`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-wikia**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-wikia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-wikia**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#192`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-wikipedia**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

(yahoo-mail-d)=

::::{dropdown} yahoo-mail
:class-container: anchor-id-yahoo-mail-d

* **Command to Run Locally**

  ```
  ./mach raptor -t yahoo-mail
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: false
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#197`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm5-linux-firefox-yahoo-mail.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://mail.yahoo.com/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-custom-car-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-chrome-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-bytecode-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-essential-firefox-yahoo-mail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-yahoo-mail**
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
* **apps**: firefox, chrome, safari, custom-car
* **benchmark page**: true
* **browser cycles**: 25
* **expected**: pass
* **gecko profile interval**: 1
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/desktop/browsertime-tp6.toml#202`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-custom-car-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-youtube**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-youtube**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6-chrome-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-firefox-youtube**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6-live-chrome-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-firefox-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6-live-safari-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-cnn-nav**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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

(facebook-nav-i)=

::::{dropdown} facebook-nav
:class-container: anchor-id-facebook-nav-i

* **Command to Run Locally**

  ```
  ./mach raptor -t facebook-nav
  ```

**Owner**: PerfTest Team

**Description**: Navigates to facebook, then the sub-pages friends, marketplace, groups.

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
* **page timeout**: 90000
* **playback**: mitmproxy
* **playback pageset manifest**: mitm11-windows-firefox-facebook-nav.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test cmds**: ['measure.start', 'landing'] ['navigate', 'https://www.facebook.com/'] ['wait.byTime', '5000'] ['measure.stop', ''] ['measure.start', 'marketplace'] ['navigate', 'https://www.facebook.com/marketplace'] ['wait.byTime', '5000'] ['measure.stop', ''] ['measure.start', 'groups'] ['navigate', 'https://www.facebook.com/groups/discover/'] ['wait.byTime', '5000'] ['measure.stop', ''] ['measure.start', 'friends'] ['navigate', 'https://www.facebook.com/friends/'] ['wait.byTime', '5000'] ['measure.stop', '']
* **test url**: <https://www.facebook.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-linux2404-64-clang-trunk/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-chrome-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-facebook-nav**
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
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#64`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-ama**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#81`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-1**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/interactive/browsertime-responsiveness.toml#103`
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

:::{list-table} **test-linux2404-64-clang-trunk/opt**
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


:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-responsiveness-firefox-reddit-billgates-post-2**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **test-windows11-64-24h2/opt**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-booking-sf-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking-sf-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-booking-sf-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-sf-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-discord-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-discord**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-discord-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-discord-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-discord-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-fashionbeans-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-fashionbeans-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-fashionbeans-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-fashionbeans-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-google-accounts-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-accounts-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-google-accounts-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-accounts-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-imdb-firefox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb-firefox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-imdb-firefox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-firefox-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-medium-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-medium-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-medium-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-medium-article-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-people-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-people-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-people-article-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-people-article-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-reddit-thread-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit-thread-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-reddit-thread-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-thread-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-rumble-fox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-rumble-fox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-rumble-fox-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-rumble-fox-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-stackoverflow-question-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow-question-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-stackoverflow-question-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-question-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-urbandictionary-define-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-urbandictionary-define-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-urbandictionary-define-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-urbandictionary-define-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, firefox, chrome
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-wikia-marvel-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikia-marvel-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-live-fenix-wikia-marvel-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikia-marvel-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-allrecipes**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-allrecipes-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-allrecipes-nofis**
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
* - **browsertime-tp6m-live-fenix-allrecipes-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-allrecipes-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-amazon**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-amazon-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-amazon-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-amazon-search-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search-nofis**
  - ✅
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-amazon-search-nofis**
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
* - **browsertime-tp6m-live-fenix-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-amazon-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-webextensions-fenix-amazon-search-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bild-de**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bild-de-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bild-de-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bild-de-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bing**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bing-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bing-search-restaurants**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-bing-search-restaurants-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-bing-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-bing-search-restaurants-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-booking**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-booking-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-booking-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-booking-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-cnn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-cnn-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-cnn-ampstories**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-cnn-ampstories-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-cnn-ampstories-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-cnn-ampstories-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-dailymail**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-dailymail-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-dailymail-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-dailymail-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-search**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-search-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-ebay-kleinanzeigen-search-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-ebay-kleinanzeigen-search-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-espn**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-espn-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-espn-nofis**
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
* - **browsertime-tp6m-live-fenix-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-espn-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(facebook-m)=

::::{dropdown} facebook
:class-container: anchor-id-facebook-m

* **Command to Run Locally**

  ```
  ./mach raptor -t facebook
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#65`
* **login**: true
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-g5-fenix-facebook.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.facebook.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-facebook-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(facebook-cristiano-m)=

::::{dropdown} facebook-cristiano
:class-container: anchor-id-facebook-cristiano-m

* **Command to Run Locally**

  ```
  ./mach raptor -t facebook-cristiano
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#70`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm6-android-fenix-facebook-cristiano.manifest
* **playback version**: 8.1.1
* **support class**: browsertime_pageload.py
* **test url**: <https://m.facebook.com/Cristiano>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-chrome-m-facebook-cristiano-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook-cristiano**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook-cristiano-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-cristiano**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-cristiano-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-live-fenix-facebook-cristiano**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-facebook-cristiano-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-cristiano**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-facebook-cristiano-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#73`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-google**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-google-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-google-nofis**
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
* - **browsertime-tp6m-live-fenix-google-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#78`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-google-maps**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-google-maps-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-maps-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-maps-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#81`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-google-search-restaurants**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-google-search-restaurants-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-google-search-restaurants-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-google-search-restaurants-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#86`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-imdb**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-imdb-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-imdb-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-imdb-nofis**
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

(instagram-m)=

::::{dropdown} instagram
:class-container: anchor-id-instagram-m

* **Command to Run Locally**

  ```
  ./mach raptor -t instagram
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#89`
* **login**: true
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm8-android-gve-instagram.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.instagram.com>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-instagram**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-instagram-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-instagram**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-instagram**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-instagram-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-instagram**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#95`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-microsoft-support**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-microsoft-support-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-microsoft-support-nofis**
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
* - **browsertime-tp6m-live-fenix-microsoft-support-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-microsoft-support-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#98`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-reddit**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-reddit-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-reddit-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-reddit-nofis**
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

(sina-m)=

::::{dropdown} sina
:class-container: anchor-id-sina-m

* **Command to Run Locally**

  ```
  ./mach raptor -t sina
  ```

**Owner**: PerfTest Team

* **alert on**: [fcp](raptor-metrics.md#first-paint), [loadtime](raptor-metrics.md#load-time), [ContentfulSpeedIndex](raptor-metrics.md#contentful-speed-index), [PerceptualSpeedIndex](raptor-metrics.md#perceptual-speed-index), [SpeedIndex](raptor-metrics.md#speed-index), [FirstVisualChange](raptor-metrics.md#first-visual-change), [LastVisualChange](raptor-metrics.md#last-visual-change), [largestContentfulPaint](raptor-metrics.md#largest-contentful-paint)
* **alert threshold**: 2.0
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#101`
* **lower is better**: true
* **page cycles**: 25
* **page timeout**: 60000
* **playback**: mitmproxy-android
* **playback pageset manifest**: mitm8-android-fenix-sina.manifest
* **playback version**: 12.2.1
* **support class**: browsertime_pageload.py
* **test url**: <https://www.sina.com.cn/>
* **type**: pageload
* **unit**: ms
* **use live sites**: false
* **Test Task**:

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-sina-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-sina-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-sina**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-sina-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-sina**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-sina-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-sina**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-fenix-sina**
  - ✅
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-sina-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-sina**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-sina-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-refbrow-sina**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#106`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-stackoverflow**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-stackoverflow-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-stackoverflow-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-stackoverflow-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#109`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-wikipedia**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-wikipedia-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-wikipedia-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-wikipedia-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#112`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-chrome-m-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-cstm-car-m-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-youtube**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-fenix-youtube-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-nofis**
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


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-fenix-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-geckoview-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-nofis**
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
* **apps**: geckoview, fenix, refbrow, chrome-m, cstm-car-m
* **benchmark page**: true
* **browser cycles**: 15
* **expected**: pass
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/tp6/mobile/browsertime-tp6m.toml#115`
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

:::{list-table} **test-android-hw-a55-14-0-aarch64-shippable/opt**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **browsertime-tp6m-essential-chrome-m-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-cstm-car-m-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-youtube-watch**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-fenix-youtube-watch-nofis**
  - ✅
  - ✅
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-refbrow-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-chrome-m-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-fenix-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
:::


:::{list-table} **test-android-hw-a55-14-0-aarch64/opt**
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
* - **browsertime-tp6m-essential-fenix-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-essential-geckoview-youtube-watch-nofis**
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
* - **browsertime-tp6m-live-fenix-youtube-watch-nofis**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **browsertime-tp6m-live-geckoview-youtube-watch-nofis**
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

**Description**: Runs the nav-bench composite across Amazon, BBC, DuckDuckGo, Reddit, and Wikipedia in a single combined session.


* **accept zero vismet**: false
* **alert on**: [SpeedIndex](raptor-metrics.md#speed-index)
* **alert threshold**: 5.0
* **apps**: firefox, chrome
* **browser cycles**: 10
* **browsertime visualmetrics**: true
* **cold**: true
* **custom data**: true
* **expected**: pass
* **gecko profile interval**: 1
* **interactive**: true
* **link searchfox**: {searchfox}`testing/raptor/raptor/tests/nav-bench/browsertime-nav-bench.toml#55`
* **lower is better**: false
* **measure**: SpeedIndex
* **page cycles**: 1
* **page timeout**: 600000
* **playback**: mitmproxy
* **playback manifests dir**: nav-bench
* **playback pageset manifest**: mitm12-darwin-firefox-amazon-nav.manifest,mitm12-darwin-firefox-bbc-nav.manifest,mitm12-darwin-firefox-duckduckgo-nav.manifest,mitm12-darwin-firefox-reddit-nav.manifest,mitm12-darwin-firefox-wikipedia-nav.manifest
* **playback version**: 12.2.1
* **preferences**: fission.bfcacheInParent=false browser.sessionhistory.max_total_viewers=0 network.http.http3.enable=false network.http.altsvc.enabled=false network.dns.forceResolve=192.0.2.1
* **support class**: nav_bench.py
* **test cmds**: ['wait.byTime', '30000'] ['measure.start', 'amazon-nav-load'] ['navigate', 'https://www.amazon.ca/s?k=laptop&ref=nb_sb_noss_1'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'bbc-nav-load'] ['navigate', 'https://www.bbc.com/news/'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'bbc-nav-subnav'] ['click.bySelectorAndWait', 'a[href*="bbcindepth"]'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'duckduckgo-nav-load'] ['navigate', 'https://duckduckgo.com/?q=mandelbrot+set'] ['wait.byTime', '2000'] ['measure.stop', ''] ['measure.start', 'duckduckgo-nav-subnav'] ['click.bySelectorAndWait', 'nav ul li:nth-of-type(2) > a'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'reddit-nav-load'] ['navigate', 'https://www.reddit.com/r/IAmA/comments/m8n4vt/im_bill_gates_cochair_of_the_bill_and_melinda/'] ['wait.byTime', '3000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000'] ['measure.start', 'wikipedia-nav-load'] ['navigate', 'https://en.wikipedia.org/wiki/Barack_Obama'] ['wait.byTime', '2000'] ['measure.stop', ''] ['navigate', 'about:blank'] ['wait.byTime', '1000']
* **test url**: <about:blank>
* **type**: pageload
* **unit**: score
* **use live sites**: false
* **verbose**: true
* **Test Task**:

:::{list-table} **test-linux2404-64-nightlyasrelease/opt**
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


:::{list-table} **test-linux2404-64-shippable/opt**
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


:::{list-table} **test-linux2404-64/opt**
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


:::{list-table} **test-macosx1470-64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1470-64-shippable/opt**
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


:::{list-table} **test-macosx1470-64/opt**
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


:::{list-table} **test-macosx1500-aarch64-nightlyasrelease/opt**
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


:::{list-table} **test-macosx1500-aarch64-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2-hw-ref/opt**
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


:::{list-table} **test-windows11-64-24h2-nightlyasrelease/opt**
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


:::{list-table} **test-windows11-64-24h2-shippable/opt**
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


:::{list-table} **test-windows11-64-24h2/opt**
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


