# Talos

Talos is a cross-platform Python performance testing framework that is specifically for
Firefox on desktop. New performance tests should be added to the newer framework
[MozPerftest](/testing/perfdocs/mozperftest.md) unless there are limitations
there (highly unlikely) that make it absolutely necessary to add them to Talos. Talos is
named after the [bronze automaton from Greek myth](https://en.wikipedia.org/wiki/Talos).

```{contents}
:depth: 1
:local: true
```

Talos tests are run in a similar manner to xpcshell and mochitests. They are started via
the command {code}`mach talos-test`. A {searchfox}`python script <testing/talos>`
then launches Firefox, which runs the tests via JavaScript special powers. The test timing
information is recorded in a text log file, e.g. {code}`browser_output.txt`, and then processed
into the {searchfox}`JSON format supported by Perfherder <testing/performance/common/performance-artifact-schema.json>`.

Talos bugs can be filed in [Testing::Talos](https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=Talos).

Talos infrastructure is still mostly documented [on the Mozilla Wiki](https://wiki.mozilla.org/TestEngineering/Performance/Talos).
In addition, there are plans to surface all of the individual tests using PerfDocs.
This work is tracked in [Bug 1674220](https://bugzilla.mozilla.org/show_bug.cgi?id=1674220).

Examples of current Talos runs can be [found in Treeherder by searching for "Talos"](https://treeherder.mozilla.org/jobs?repo=autoland&searchStr=Talos).
If none are immediately available, then scroll to the bottom of the page and load more test
runs. The tests all share a group symbol starting with a {code}`T`, for example
{code}`T(c d damp g1)` or {code}`T-gli(webgl)`.

## Running Talos Locally

Running tests locally is most likely only useful for debugging what is going on in a test,
as the test output is only reported as raw JSON. The CLI is documented via:

```bash
./mach talos-test --help
```

To quickly try out the {code}`./mach talos-test` command, the following can be run to do a
single run of the DevTools' simple netmonitor test.

```bash
# Run the "simple.netmonitor" test very quickly with 1 cycle, and 1 page cycle.
./mach talos-test --activeTests damp --subtests simple.netmonitor --cycles 1 --tppagecycles 1
```

The {code}`--print-suites` and {code}`--print-tests` are two helpful command flags to
figure out what suites and tests are available to run.

```bash
# Print out the suites:
./mach talos-test --print-suites

# Available suites:
#  bcv                          (basic_compositor_video)
#  chromez                      (about_preferences_basic:tresize)
#  dromaeojs                    (dromaeo_css:kraken)
# ...

# Run all of the tests in the "bcv" test suite:
./mach talos-test --suite bcv

# Print out the tests:
./mach talos-test --print-tests

# Available tests:
# ================
#
# a11yr
# -----
# This test ensures basic a11y tables and permutations do not cause
# performance regressions.
#
# ...

# Run the tests in "a11yr" listed above
./mach talos-test --activeTests a11yr
```

## Running Talos on Try

Talos runs can be generated through the mach try fuzzy finder:

```bash
./mach try fuzzy
```

The following is an example output at the time of this writing. Refine the query for the
platform and test suites of your choosing.

```
| test-windows10-64-qr/opt-talos-bcv-swr-e10s
| test-linux64-shippable/opt-talos-webgl-e10s
| test-linux64-shippable/opt-talos-other-e10s
| test-linux64-shippable-qr/opt-talos-g5-e10s
| test-linux64-shippable-qr/opt-talos-g4-e10s
| test-linux64-shippable-qr/opt-talos-g3-e10s
| test-linux64-shippable-qr/opt-talos-g1-e10s
| test-windows10-64/opt-talos-webgl-gli-e10s
| test-linux64-shippable/opt-talos-tp5o-e10s
| test-linux64-shippable/opt-talos-svgr-e10s
| test-linux64-shippable/opt-talos-damp-e10s
> test-windows7-32/opt-talos-webgl-gli-e10s
| test-linux64-shippable/opt-talos-bcv-e10s
| test-linux64-shippable/opt-talos-g5-e10s
| test-linux64-shippable/opt-talos-g4-e10s
| test-linux64-shippable/opt-talos-g3-e10s
| test-linux64-shippable/opt-talos-g1-e10s
| test-linux64-qr/opt-talos-bcv-swr-e10s

  For more shortcuts, see mach help try fuzzy and man fzf
  select: <tab>, accept: <enter>, cancel: <ctrl-c>, select-all: <ctrl-a>, cursor-up: <up>, cursor-down: <down>
  1379/2967
> talos
```

## At a glance

- Tests are defined in
  {searchfox}`testing/talos/talos/test.py`
- Treeherder abbreviations are defined in
  {searchfox}`taskcluster/kinds/test/talos.yml`
- Suites are defined for production in
  {searchfox}`testing/talos/talos.json`

## Test lifecycle

- Taskcluster schedules {searchfox}`talos jobs <taskcluster/kinds/test/talos.yml>`

- Taskcluster runs a Talos job on a hardware machine when one is
  available - this is bootstrapped by
  {searchfox}`mozharness <testing/mozharness/mozharness/mozilla/testing/talos.py>`

  - mozharness downloads the build, talos.zip (found in
    {searchfox}`talos.json <testing/talos/talos.json>`),
    and creates a virtualenv for running the test.
  - mozharness [configures the test and runs
    it](https://wiki.mozilla.org/TestEngineering/Performance/Talos/Running#How_Talos_is_Run_in_Production)
  - After the test is completed the data is uploaded to
    [Perfherder](https://treeherder.mozilla.org/perf.html#/graphs)

- Treeherder displays a green (all OK) status and has a link to
  [Perfherder](https://treeherder.mozilla.org/perf.html#/graphs)

- 13 pushes later,
  [analyze_talos.py](http://hg.mozilla.org/graphs/file/tip/server/analysis/analyze_talos.py)
  is ran which compares your push to the previous 12 pushes and next 12
  pushes to look for a
  [regression](https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#Regressions)

  - If a regression is found, it will be posted on [Perfherder
    Alerts](https://treeherder.mozilla.org/perf.html#/alerts)

## Test types

There are two different species of Talos tests:

- [Startup]: Start up the browser and wait for either the load event or the paint event and exit, measuring the time
- [Page load]: Load a manifest of pages

In addition we have some variations on existing tests:

- [Heavy]: Run tests with the heavy user profile instead of a blank one
- [WebExtension]: Run tests with a WebExtension to see the perf impacts extension have
- [Real-world WebExtensions]: Run tests with a set of 5 popular real-world WebExtensions installed and enabled.

Different tests measure different things:

- [Paint]: These measure events from the browser like moz_after_paint, etc.
- [ASAP]: These tests go really fast and typically measure how many frames we can render in a time window
- [Benchmarks]: These are benchmarks that measure specific items and report a summarized score

### Startup

[Startup
tests](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/startup_test)
launch Firefox and measure the time to the onload or paint events. We
run this in a series of cycles (default is 20) to generate a full set of
data. Tests that currently are startup tests are:

- [ts_paint](#ts_paint)
- [tpaint]
- [tresize](#tresize)
- [sessionrestore](#sessionrestore)
- [sessionrestore_no_auto_restore](#sessionrestore_no_auto_restore)
- [sessionrestore_many_windows](#sessionrestore_many_windows)

### Page load

Many of the Talos tests use the page loader to load a manifest of pages.
These are tests that load a specific page and measure the time it takes
to load the page, scroll the page, draw the page etc. In order to run a
page load test, you need a manifest of pages to run. The manifest is
simply a list of URLs of pages to load, separated by carriage returns,
e.g.:

```none
https://www.mozilla.org
https://www.mozilla.com
```

Example:
[svgx.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/svgx/svgx.manifest)

Manifests may also specify that a test computes its own data by
prepending a `%` in front of the line:

```none
% https://www.mozilla.org
% https://www.mozilla.com
```

Example:
[v8.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/v8_7/v8.manifest)

The file you created should be referenced in your test config inside of
[test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l607).
For example, open test.py, and look for the line referring to the test
you want to run:

```python
tpmanifest = "${talos}/page_load_test/svgx/svgx.manifest"
tpcycles = 1  # run a single cycle
tppagecycles = 25  # load each page 25 times before moving onto the next page
```

### Heavy

All our testing is done with empty blank profiles, this is not ideal for
finding issues for end users. We recently undertook a task to create a
daily update to a profile so it is modern and relevant. It browses a
variety of web pages, and have history and cache to give us a more
realistic scenario.

The toolchain is documented on
[github](https://github.com/tarekziade/heavy-profile) and was added
to Talos in [bug
1407398](https://bugzilla.mozilla.org/show_bug.cgi?id=1407398).

Currently we have issues with this on windows (takes too long to unpack
the files from the profile), so we have turned this off there. Our goal
is to run this on basic pageload and startup tests.

### WebExtension

WebExtensions are what Firefox has switched to and there are different
code paths and APIs used vs addons. Historically we don't test with
addons (other than our test addons) and are missing out on common
slowdowns. In 2017 we started running some startup and basic pageload
tests with a WebExtension in the profile ([bug
1398974](https://bugzilla.mozilla.org/show_bug.cgi?id=1398974)). We
have updated the Extension to be more real world and will continue to do
that.

### Real-world WebExtensions

We've added a variation on our test suite that automatically downloads,
installs and enables 5 popular WebExtensions. This is used to measure
things like the impact of real-world WebExtensions on start-up time.

Currently, the following extensions are installed:

- Adblock Plus (3.5.2)
- Cisco Webex Extension (1.4.0)
- Easy Screenshot (3.67)
- NoScript (10.6.3)
- Video DownloadHelper (7.3.6)

Note that these add-ons and versions are "pinned" by being held in a
compressed file that's hosted in an archive by our test infrastructure
and downloaded at test runtime. To update the add-ons in this set, one
must provide a new ZIP file to someone on the test automation team. See
[this comment in
Bugzilla](https://bugzilla.mozilla.org/show_bug.cgi?id=1575089#c3).

### Paint

Paint tests are measuring the time to receive both the
[MozAfterPaint](https://developer.mozilla.org/en-US/docs/Web/Events/MozAfterPaint)
and OnLoad event instead of just the OnLoad event. Most tests now look
for this unless they are an ASAP test, or an internal benchmark

### ASAP

We have a variety of tests which we now run in ASAP mode where we render
as fast as possible (disabling vsync and letting the rendering iterate
as fast as it can using \`requestAnimationFrame\`). In fact we have
replaced some original tests with the 'x' versions to make them measure.
We do this with RequestAnimationFrame().

ASAP tests are:

- [basic_compositor_video](#basic_compositor_video)
- [displaylist_mutate](#displaylist_mutate)
- [glterrain](#glterrain)
- [rasterflood_svg](#rasterflood_svg)
- [rasterflood_gradient](#rasterflood_gradient)
- [tsvgx](#tsvgx)
- [tscrollx](#tscrollx)
- [tp5o_scroll](#tp5o_scroll)
- [tabswitch](#tabswitch)
- [tart](#tart)

### Benchmarks

Many tests have internal benchmarks which we report as accurately as
possible. These are the exceptions to the general rule of calculating
the suite score as a geometric mean of the subtest values (which are
median values of the raw data from the subtests).

Tests which are imported benchmarks are:

- [ARES6](#ares6)
- [dromaeo](#dromaeo)
- [JetStream](#jetstream)
- [kraken](#kraken)
- motionmark ([animometer](#motionmark_animometer),
  [htmlsuite](#motionmark_htmlsuite), [webgl](#motionmark_webgl))
- [stylebench](#stylebench)

### Row major vs. column major

To get more stable numbers, tests are run multiple times. There are two
ways that we do this: row major and column major. Row major means each
test is run multiple times and then we move to the next test (and run it
multiple times). Column major means that each test is run once one after
the other and then the whole sequence of tests is run again.

More background information about these approaches can be found in Joel
Maher's [Reducing the Noise in
Talos](https://elvis314.wordpress.com/2012/03/12/reducing-the-noise-in-talos/)
blog post.

## Page sets

We run our tests 100% offline, but serve pages via a webserver. Knowing
this we need to store and make available the offline pages we use for
testing.

### tp5pages

Some tests make use of a set of 50 "real world" pages, known as the tp5n
set. These pages are not part of the Talos repository, but without them
the tests which use them won't run.

- To add these pages to your local setup, download the latest tp5n zip
  from [tooltool](https://mozilla-releng.net/tooltool/), and extract
  it such that `tp5n` ends up as `testing/talos/talos/tests/tp5n`.
  You can also obtain it by running a Talos test locally to get the zip
  into `testing/talos/talos/tests/`, i.e `./mach talos-test --suite damp`
- see also [tp5o](#tp5o).

## Talos Tests

For the sample commands found below, note that the capitalization used is important. Without the exact spelling, the test won't be found when running locally.

(ARES6)=

::::{dropdown} ARES6
:class-container: anchor-id-ARES6

* **Command to Run Locally**

  ```
  ./mach talos-test -a ARES6
  ```

* contact: :jandem and SpiderMonkey Team
* source: [ARES-6](https://searchfox.org/firefox-main/source/third_party/webkit/PerformanceTests/ARES-6)
* type: [Page load]
* data: 6 cycles of the entire benchmark
  * [geometric mean](https://searchfox.org/firefox-main/source/testing/talos/talos/output.py#259) self reported from the benchmark
* **Lower is better**
* unit: geometric mean / benchmark score
* lower_is_better: True
* tpmanifest: ${talos}/tests/ares6/ares6.manifest
* tppagecycles: 1
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(JetStream)=

::::{dropdown} JetStream
:class-container: anchor-id-JetStream

* **Command to Run Locally**

  ```
  ./mach talos-test -a JetStream
  ```

* contact: :jandem and SpiderMonkey Team
* source: [jetstream.manifest](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/jetstream/jetstream.manifest) and jetstream.zip from tooltool
* type: [Page load]
* measuring: JavaScript performance
* reporting: geometric mean from the benchmark
* data: internal benchmark
  * suite: [geometric
        mean](https://searchfox.org/firefox-main/source/testing/talos/talos/output.py#259)
        provided by the benchmark
* description:
      | This is the [JetStream](http://browserbench.org/JetStream/in-depth.html)
        javascript benchmark taken verbatim and slightly modified to fit into
        our pageloader extension and Talos harness.
* tpmanifest: ${talos}/tests/jetstream/jetstream.manifest
* tppagecycles: 1
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(a11yr)=

::::{dropdown} a11yr
:class-container: anchor-id-a11yr

* **Command to Run Locally**

  ```
  ./mach talos-test -a a11yr
  ```

* contact: :jamie and accessibility team
* source: [a11y.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/a11y)
* type: [Page load]
* measuring: ???
* data: we load 2 pages 25 times each, collect 2 sets of 25 data points
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 24; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l627)
  * suite: [geometric mean] of the 2 subtest results.
* reporting: test time in ms (lower is better)
* description:
      | This test ensures basic a11y tables and permutations do not cause performance regressions.
* **Example Data**

  ```
  0;dhtml.html;1584;1637;1643;1665;1741;1529;1647;1645;1692;1647;1542;1750;1654;1649;1541;1656;1674;1645;1645;1740;1558;1652;1654;1656;1654 |
  1;tablemutation.html;398;385;389;391;387;387;385;387;388;385;384;31746;386;387;384;387;389;387;387;387;388;391;386;387;388 |
  ```

* a11y: True
* alert_threshold: 5.0
* preferences: {'dom.send_after_paint_to_content': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/a11y/a11y.manifest
* tpmozafterpaint: True
* tppagecycles: 25
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(about_preferences_basic)=

::::{dropdown} about_preferences_basic
:class-container: anchor-id-about_preferences_basic

* **Command to Run Locally**

  ```
  ./mach talos-test -a about_preferences_basic
  ```

* contact: :jaws and :gijs
* source: [about_preferences_basic.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/about-preferences/about_preferences_basic.manifest)
* type: [Page load]
* measuring: first-non-blank-paint
* data: We load 5 urls 1 time each, and repeat for 25 cycles; collecting 25 sets of 5 data points
* summarization:
  * subtest: [ignore first] five data points, then take the [median] of the rest; [source: test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l627)
  * suite: [geometric mean] of the the subtest results.
* reporting: test time in ms (lower is better)
* description:
      | This test measures the performance of the Firefox about:preferences
        page. This test is a little different than other pageload tests in that
        we are loading one page (about:preferences) but also testing the loading
        of that same page's subcategories/panels (i.e. about:preferences#home).

      When simply changing the page's panel/category, that doesn't cause a new
      onload event as expected; therefore we had to introduce loading the
      'about:blank' page in between each page category; that forces the entire
      page to reload with the specified category panel activated.

      For that reason, when new panels/categories are added to the
      'about:preferences' page, it can be expected that a performance
      regression may be introduced, even if a subtest hasn't been added for
      that new page category yet.

      This test should only ever have 1 pagecycle consisting of the main
      about-preferences page and each category separated by an about:blank
      between. Then repeats are achieved by using 25 cycles (instead of
      pagecycles).
* **Example Data**

  ```
  0;preferences;346;141;143;150;136;143;153;140;154;156;143;154;146;147;151;166;140;146;140;144;144;156;154;150;140
  2;preferences#search;164;142;133;141;141;141;142;140;131;146;131;140;131;131;139;142;140;144;146;143;143;142;142;137;143
  3;preferences#privacy;179;159;166;177;173;153;148;154;168;155;164;155;152;157;149;155;156;186;149;156;160;151;158;168;157
  4;preferences#sync;148;156;140;137;159;139;143;145;138;130;145;142;141;133;146;141;147;143;146;146;139;144;142;151;156
  5;preferences#home;141;111;130;131;138;128;133;122;138;138;131;139;139;132;133;141;143;139;138;135;136;128;134;140;135
  ```

* fnbpaint: True
* gecko_profile_interval: 1
* lower_is_better: True
* pine: False
* tpcycles: 25
* tpmanifest: ${talos}/tests/about-preferences/about_preferences_basic.manifest
* tppagecycles: 1
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
  - ✅
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
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(basic_compositor_video)=

::::{dropdown} basic_compositor_video
:class-container: anchor-id-basic_compositor_video

* **Command to Run Locally**

  ```
  ./mach talos-test -a basic_compositor_video
  ```

* contact: :b0bh00d, :jeffm, and gfx
* source: [video](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/video)
* type: [Page load]
* data: 12 cycles of the entire benchmark, each subtest will have 12 data points (see below)
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 11; [source: test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l522)
  * suite: [geometric mean] of the 24 subtest results.
* **Lower is better**
* **Example Data**

  ```
  ;0;240p.120fps.mp4_scale_fullscreen_startup;11.112;11.071;11.196;11.157;11.195;11.240;11.196;11.155;11.237;11.074;11.154;11.282
  ;1;240p.120fps.mp4_scale_fullscreen_inclip;10.995;11.114;11.052;10.991;10.876;11.115;10.995;10.991;10.997;10.994;10.992;10.993
  ;2;240p.120fps.mp4_scale_1_startup;1.686;1.690;1.694;1.683;1.689;1.692;1.686;1.692;1.689;1.704;1.684;1.686
  ;3;240p.120fps.mp4_scale_1_inclip;1.666;1.666;1.666;1.668;1.667;1.669;1.667;1.668;1.668;1.667;1.667;1.669
  ;4;240p.120fps.mp4_scale_1.1_startup;1.677;1.672;1.673;1.677;1.673;1.677;1.672;1.677;1.677;1.671;1.676;1.679
  ;5;240p.120fps.mp4_scale_1.1_inclip;1.667;1.668;1.666;1.667;1.667;1.668;1.667;1.667;1.667;1.667;1.668;1.668
  ;6;240p.120fps.mp4_scale_2_startup;1.927;1.908;1.947;1.946;1.902;1.932;1.916;1.936;1.921;1.896;1.908;1.894
  ;7;240p.120fps.mp4_scale_2_inclip;1.911;1.901;1.896;1.917;1.897;1.921;1.907;1.944;1.904;1.912;1.936;1.913
  ;8;480p.60fps.webm_scale_fullscreen_startup;11.675;11.587;11.539;11.454;11.723;11.410;11.629;11.410;11.454;11.498;11.540;11.540
  ;9;480p.60fps.webm_scale_fullscreen_inclip;11.304;11.238;11.370;11.300;11.364;11.368;11.237;11.238;11.434;11.238;11.304;11.368
  ;10;480p.60fps.webm_scale_1_startup;3.386;3.360;3.391;3.376;3.387;3.402;3.371;3.371;3.356;3.383;3.376;3.356
  ;11;480p.60fps.webm_scale_1_inclip;3.334;3.334;3.329;3.334;3.334;3.334;3.334;3.334;3.334;3.335;3.334;3.334
  ;12;480p.60fps.webm_scale_1.1_startup;3.363;3.363;3.368;3.356;3.356;3.379;3.364;3.360;3.360;3.356;3.363;3.356
  ;13;480p.60fps.webm_scale_1.1_inclip;3.329;3.334;3.329;3.334;3.333;3.334;3.334;3.334;3.340;3.335;3.329;3.335
  ;14;480p.60fps.webm_scale_2_startup;4.960;4.880;4.847;4.959;4.802;4.863;4.824;4.926;4.847;4.785;4.870;4.855
  ;15;480p.60fps.webm_scale_2_inclip;4.903;4.786;4.892;4.903;4.822;4.832;4.798;4.857;4.808;4.856;4.926;4.741
  ;16;1080p.60fps.mp4_scale_fullscreen_startup;14.638;14.495;14.496;14.710;14.781;14.853;14.639;14.637;14.707;14.637;14.711;14.636
  ;17;1080p.60fps.mp4_scale_fullscreen_inclip;13.795;13.798;13.893;13.702;13.799;13.607;13.798;13.705;13.896;13.896;13.896;14.088
  ;18;1080p.60fps.mp4_scale_1_startup;6.995;6.851;6.930;6.820;6.915;6.805;6.898;6.866;6.852;6.850;6.803;6.851
  ;19;1080p.60fps.mp4_scale_1_inclip;6.560;6.625;6.713;6.601;6.645;6.496;6.624;6.538;6.539;6.497;6.580;6.558
  ;20;1080p.60fps.mp4_scale_1.1_startup;7.354;7.230;7.195;7.300;7.266;7.283;7.196;7.249;7.230;7.230;7.212;7.264
  ;21;1080p.60fps.mp4_scale_1.1_inclip;6.969;7.222;7.018;6.993;7.045;6.970;6.970;6.807;7.118;6.969;6.997;6.972
  ;22;1080p.60fps.mp4_scale_2_startup;6.963;6.947;6.914;6.929;6.979;7.010;7.010245327102808;6.914;6.961;7.028;7.012;6.914
  ;23;1080p.60fps.mp4_scale_2_inclip;6.757;6.694;6.672;6.669;6.737;6.831;6.716;6.715;6.832;6.670;6.672;6.759
  ```

* gecko_profile_interval: 1
* lower_is_better: True
* preferences: {'full-screen-api.allow-trusted-requests-only': False, 'layers.acceleration.force-enabled': False, 'layers.acceleration.disabled': True, 'gfx.webrender.software': True, 'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'full-screen-api.warning.timeout': 500, 'media.ruin-av-sync.enabled': True}
* timeout: 10000
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/video/video.manifest
* tppagecycles: 12
* unit: ms/frame
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-bcv**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-bcv-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-bcv-swr**
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
* - **talos-bcv**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-bcv-swr**
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
* - **talos-bcv**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-bcv-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-bcv-swr**
  - ✅
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
* - **talos-bcv**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-bcv-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-bcv-swr**
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
* - **talos-bcv**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-bcv-swr**
  - ✅
  - ✅
  - ❌
  - ❌
:::


::::

(canvas2dvideo)=

::::{dropdown} canvas2dvideo
:class-container: anchor-id-canvas2dvideo

* **Command to Run Locally**

  ```
  ./mach talos-test -a canvas2dvideo
  ```

* contact: :aosmond and gfx
* source: [canvas2dvideo](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/canvas2d/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: Canvas2D video texture update with 1080p video. Measures mean tick time across 100 ticks.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l522)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean tick time across 100 ticks: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test playbacks a video file and ask Canvas2D to draw video frames as
        Canvas2D textures for 100 ticks. It collects the mean tick time across 100
        ticks to measure how much time it will spend for a video texture upload
        to be a Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore
        the first found. Lower results are better.
* gecko_profile_extra_threads: CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/canvas2d/canvas2dvideo.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(cpstartup)=

::::{dropdown} cpstartup
:class-container: anchor-id-cpstartup

* **Command to Run Locally**

  ```
  ./mach talos-test -a cpstartup
  ```

* contact: :mconley, Firefox Desktop Front-end team, Gijs, fqueze, and dthayer
* measuring: Time from opening a new tab (which creates a new content process) to having that new content process be ready to load URLs.
* source: [cpstartup](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/cpstartup)
* type: [Page load]
* bug: [bug 1336389](https://bugzilla.mozilla.org/show_bug.cgi?id=1336389)
* data: 20 cycles of the entire benchmark
* **Lower is better**
* **Example Data**

  ```
  0;content-process-startup;877;737;687;688;802;697;794;685;694;688;794;669;699;684;690;849;687;873;694;689
  ```

* extensions: ['${talos}/pageloader', '${talos}/tests/cpstartup/extension']
* preferences: {'browser.link.open_newwindow': 3, 'browser.link.open_newwindow.restriction': 2}
* timeout: 600
* tploadnocache: True
* tpmanifest: ${talos}/tests/cpstartup/cpstartup.manifest
* tppagecycles: 20
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(cross_origin_pageload)=

::::{dropdown} cross_origin_pageload
:class-container: anchor-id-cross_origin_pageload

* **Command to Run Locally**

  ```
  ./mach talos-test -a cross_origin_pageload
  ```

* contact: :sefeng, :jesup, and perf eng team
* measuring: The time it takes to load a page which has 20 cross origin iframes
* source: [cross_origin_pageload](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/cross_origin_pageload)
* type: [Page load]
* bug: [bug 1701989](https://bugzilla.mozilla.org/show_bug.cgi?id=1701989)
* data: 10 cycles of the entire benchmark
* **Lower is better**
* **Example Data**

  ```
  0;/index.html;194.42;154.12;141.38;145.88;136.92;147.64;152.54;138.02;145.5;143.62
  ```

* extensions: ['${talos}/pageloader']
* preferences: {'dom.ipc.processPrelaunch.fission.number': 30}
* timeout: 100
* tploadnocache: True
* tpmanifest: ${talos}/tests/cross_origin_pageload/cross_origin_pageload.manifest
* tppagecycles: 10
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(damp)=

::::{dropdown} damp
:class-container: anchor-id-damp

* **Command to Run Locally**

  ```
  ./mach talos-test -a damp
  ```

* contact: :ochameau and devtools team
* source: [damp](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/devtools)
* type: [Page load]
* measuring: Developer Tools toolbox performance. Split in test suites covering different DevTools areas (inspector, webconsole, other).
* reporting: intervals in ms (lower is better)
* see below for details
* data: there are 36 reported subtests from DAMP which we load 25 times, resulting in 36 sets of 25 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 24 data points; [source: test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l356)
  * suite: No value for the suite, only individual subtests are relevant.
* description:
      | To run this locally, you'll need to pull down the [tp5 page
        set](#page-sets) and run it in a local web server. See the [tp5
        section](#tp5).
* **Example Data**

  ```
  0;simple.webconsole.open.DAMP;1198.86;354.38;314.44;337.32;344.73;339.05;345.55;358.37;314.89;353.73;324.02;339.45;304.63;335.50;316.69;341.05;353.45;353.73;342.28;344.63;357.62;375.18;326.08;363.10;357.30
  1;simple.webconsole.reload.DAMP;44.60;41.21;25.62;29.85;38.10;42.29;38.25;40.14;26.95;39.24;40.32;34.67;34.64;44.88;32.51;42.09;28.04;43.05;40.62;36.56;42.44;44.11;38.69;29.10;42.00
  2;simple.webconsole.close.DAMP;27.26;26.97;25.45;27.82;25.98;26.05;38.00;26.89;24.90;26.61;24.90;27.22;26.95;25.18;24.24;25.60;28.91;26.90;25.57;26.04;26.79;27.33;25.76;26.47;27.43
  3;simple.inspector.open.DAMP;507.80;442.03;424.93;444.62;412.94;451.18;441.39;435.83;441.27;460.69;440.93;413.13;418.73;443.41;413.93;447.34;434.69;459.24;453.60;412.58;445.41;466.34;441.89;417.59;428.82
  4;simple.inspector.reload.DAMP;169.45;165.11;163.93;181.12;167.86;164.67;170.34;173.12;165.24;180.59;176.72;187.42;170.14;190.35;176.59;155.00;151.66;174.40;169.46;163.85;190.93;217.00;186.25;181.31;161.13
  5;simple.inspector.close.DAMP;44.40;42.28;42.71;47.21;41.74;41.24;42.94;43.73;48.24;43.04;48.61;42.49;45.93;41.36;43.83;42.43;41.81;43.93;41.38;40.98;49.76;50.86;43.49;48.99;44.02
  6;simple.jsdebugger.open.DAMP;642.59;464.02;540.62;445.46;471.09;466.57;466.70;511.91;424.12;480.70;448.37;477.51;488.99;437.97;442.32;459.03;421.54;467.99;472.78;440.27;431.47;454.76;436.86;453.61;485.59
  7;simple.jsdebugger.reload.DAMP;51.65;55.46;225.46;53.32;58.78;53.23;54.39;51.59;55.46;48.03;50.70;46.34;230.94;53.71;54.23;53.01;61.03;51.23;51.45;293.01;56.93;51.44;59.85;63.35;57.44
  8;simple.jsdebugger.close.DAMP;29.12;30.76;40.34;32.09;31.26;32.30;33.95;31.89;29.68;31.39;32.09;30.36;44.63;32.33;30.16;32.43;30.89;30.85;31.99;49.86;30.94;44.63;32.54;29.79;33.15
  9;simple.styleeditor.open.DAMP;758.54;896.93;821.17;1026.24;887.14;867.39;927.86;962.80;740.40;919.39;741.01;925.21;807.39;1051.47;729.04;1095.78;755.03;888.70;900.52;810.30;1090.09;869.72;737.44;893.16;927.72
  10;simple.styleeditor.reload.DAMP;57.32;178.13;59.23;60.82;71.45;78.86;74.35;60.11;66.43;77.41;61.96;69.22;65.97;45.53;67.88;74.76;124.61;60.01;36.66;59.24;65.01;165.68;34.61;69.02;71.42
  11;simple.styleeditor.close.DAMP;28.28;56.50;36.18;30.00;36.32;34.85;35.33;36.24;25.45;36.72;26.53;36.90;28.88;30.94;26.56;31.34;47.79;30.90;30.52;27.95;30.75;56.28;26.76;30.25;37.42
  12;simple.performance.open.DAMP;444.28;357.87;331.17;335.16;585.71;402.99;504.58;466.95;272.98;427.54;345.60;441.53;319.99;327.91;312.94;349.79;399.51;465.60;418.42;295.14;362.06;363.11;445.71;634.96;500.83
  13;simple.performance.reload.DAMP;38.07;33.44;35.99;70.57;64.04;106.47;148.31;29.60;68.47;28.95;148.46;75.92;32.15;93.72;36.17;44.13;75.11;154.76;98.28;75.16;29.39;36.68;113.16;64.05;135.60
  14;simple.performance.close.DAMP;23.98;25.49;24.19;24.61;27.56;40.33;33.85;25.13;22.62;25.28;41.84;25.09;26.39;25.20;23.76;25.44;25.92;30.40;40.77;25.41;24.57;26.15;43.65;28.54;30.16
  15;simple.netmonitor.open.DAMP;438.85;350.64;318.04;329.12;341.91;352.33;344.05;334.15;514.57;327.95;471.50;334.55;344.94;364.39;727.56;374.48;339.45;344.31;345.61;329.78;325.74;334.74;350.36;342.85;344.64
  16;simple.netmonitor.reload.DAMP;59.68;47.50;69.37;61.18;76.89;83.22;68.11;81.24;56.15;68.20;32.41;81.22;81.62;44.30;39.52;29.60;86.07;71.18;76.32;79.93;79.63;82.15;83.58;87.04;82.97
  17;simple.netmonitor.close.DAMP;38.42;39.32;52.56;43.37;48.08;40.62;51.12;41.11;59.54;43.29;41.72;40.85;51.61;49.61;51.39;44.91;40.36;41.10;45.43;42.15;42.63;40.69;41.21;44.04;41.95
  18;complicated.webconsole.open.DAMP;589.97;505.93;480.71;530.93;460.60;479.63;485.33;489.08;605.28;457.12;463.95;493.28;680.05;478.72;504.47;578.69;488.66;485.34;504.94;460.67;548.38;474.98;470.33;471.34;464.58
  19;complicated.webconsole.reload.DAMP;2707.20;2700.17;2596.02;2728.09;2905.51;2716.65;2657.68;2707.74;2567.86;2726.36;2650.92;2839.14;2620.34;2718.36;2595.22;2686.28;2703.48;2609.75;2686.41;2577.93;2634.47;2745.56;2655.89;2540.09;2649.18
  20;complicated.webconsole.close.DAMP;623.56;570.80;636.63;502.49;565.83;537.93;525.46;565.78;532.90;562.66;525.42;490.88;611.99;486.45;528.60;505.35;480.55;500.75;532.75;480.91;488.69;548.77;535.31;477.92;519.84
  21;complicated.inspector.open.DAMP;1233.26;753.57;742.74;953.11;653.29;692.66;653.75;767.02;840.68;707.56;713.95;685.79;690.21;1020.47;685.67;721.69;1063.72;695.55;702.15;760.91;853.14;660.12;729.16;1044.86;724.34
  22;complicated.inspector.reload.DAMP;2384.90;2436.35;2356.11;2436.58;2372.96;2558.86;2543.76;2351.03;2411.95;2358.04;2413.27;2339.85;2373.11;2338.94;2418.88;2360.87;2349.09;2498.96;2577.73;2445.07;2354.88;2424.90;2696.10;2362.39;2493.29
  23;complicated.inspector.close.DAMP;541.96;509.38;476.91;456.48;545.48;634.04;603.10;488.09;599.20;480.45;617.93;420.39;514.92;439.99;727.41;469.04;458.59;539.74;611.55;725.03;473.36;484.60;481.46;458.93;554.76
  24;complicated.jsdebugger.open.DAMP;644.97;578.41;542.23;595.94;704.80;603.08;689.18;552.99;597.23;584.17;682.14;758.16;791.71;738.43;640.30;809.26;704.85;601.32;696.10;683.44;796.34;657.25;631.89;739.96;641.82
  25;complicated.jsdebugger.reload.DAMP;2676.82;2650.84;2687.78;2401.23;3421.32;2450.91;2464.13;2286.40;2399.40;2415.97;2481.48;2827.69;2652.03;2554.63;2631.36;2443.83;2564.73;2466.22;2597.57;2552.73;2539.42;2481.21;2319.50;2539.00;2576.43
  26;complicated.jsdebugger.close.DAMP;795.68;616.48;598.88;536.77;435.02;635.61;558.67;841.64;613.48;886.60;581.38;580.96;571.40;605.34;671.00;882.02;619.01;579.63;643.05;656.78;699.64;928.99;549.76;560.96;676.32
  27;complicated.styleeditor.open.DAMP;2327.30;2494.19;2190.29;2205.60;2198.11;2509.01;2189.79;2532.05;2178.03;2207.75;2224.96;2665.84;2294.40;2645.44;2661.41;2364.60;2395.36;2582.72;2872.03;2679.29;2561.24;2330.11;2580.16;2510.36;2860.83
  28;complicated.styleeditor.reload.DAMP;2218.46;2335.18;2284.20;2345.05;2286.98;2453.47;2506.97;2661.19;2529.51;2289.78;2564.15;2608.24;2270.77;2362.17;2287.31;2300.19;2331.56;2300.86;2239.27;2231.33;2476.14;2286.28;2583.24;2540.29;2259.67
  29;complicated.styleeditor.close.DAMP;302.67;343.10;313.15;305.60;317.92;328.44;350.70;370.12;339.77;308.72;312.71;320.63;305.52;316.69;324.92;306.60;313.65;312.17;326.26;321.45;334.56;307.38;312.95;350.94;339.36
  30;complicated.performance.open.DAMP;477.99;537.96;564.85;515.05;502.03;515.58;492.80;689.06;448.76;588.91;509.76;485.39;548.17;479.14;638.67;535.86;541.61;611.52;554.72;665.37;694.04;470.60;746.16;547.85;700.02
  31;complicated.performance.reload.DAMP;2258.31;2345.74;2509.24;2579.71;2367.94;2365.94;2260.86;2324.23;2579.01;2412.63;2540.38;2069.80;2534.91;2443.48;2193.01;2442.99;2422.42;2475.35;2076.48;2092.95;2444.53;2353.86;2154.28;2354.61;2104.82
  32;complicated.performance.close.DAMP;334.44;516.66;432.49;341.29;309.30;365.20;332.16;311.42;370.81;301.81;381.13;299.39;317.60;314.10;372.44;314.76;306.24;349.85;382.08;352.53;309.40;298.44;314.10;315.44;405.22
  33;complicated.netmonitor.open.DAMP;469.70;597.87;468.36;823.09;696.39;477.19;487.78;495.92;587.89;471.48;555.02;507.45;883.33;522.15;756.86;713.64;593.82;715.13;477.15;717.85;586.79;556.97;631.43;629.55;581.16
  34;complicated.netmonitor.reload.DAMP;4033.55;3577.36;3655.61;3721.24;3874.29;3977.92;3778.62;3825.60;3984.65;3707.91;3985.24;3565.21;3702.40;3956.70;3627.14;3916.11;3929.11;3934.06;3590.60;3628.39;3618.84;3579.52;3953.04;3781.01;3682.69
  35;complicated.netmonitor.close.DAMP;1042.98;920.21;928.19;940.38;950.25;1043.61;1078.16;1077.38;1132.91;1095.05;1176.31;1256.83;1143.14;1234.61;1248.97;1242.29;1378.63;1312.74;1371.48;1373.15;1544.55;1422.51;1549.48;1616.55;1506.58
  ```

* cycles: 5
* extensions: ['${talos}/pageloader', '${talos}/tests/devtools/addon']
* gecko_profile_extra_threads: DOM Worker
* gecko_profile_interval: 10
* linux_counters: None
* mac_counters: None
* perfherder_framework: devtools
* preferences: {'devtools.memory.enabled': True}
* subtest_alerts: True
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/devtools/damp.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-damp-inspector**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-webconsole**
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
* - **talos-damp-inspector**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-damp-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-damp-webconsole**
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
* - **talos-damp-inspector**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-webconsole**
  - ✅
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
* - **talos-damp-inspector**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-damp-webconsole**
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
* - **talos-damp-inspector**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-damp-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-damp-webconsole**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(displaylist_mutate)=

::::{dropdown} displaylist_mutate
:class-container: anchor-id-displaylist_mutate

* **Command to Run Locally**

  ```
  ./mach talos-test -a displaylist_mutate
  ```

* contact: :miko and gfx
* source: [displaylist_mutate.html](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/layout/benchmarks/displaylist_mutate.html)
* type: [Page load]
* data: we load the displaylist_mutate.html page five times, measuring pageload each time, generating 5 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source: test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l986)
* description:
      | This measures the amount of time it takes to render a page after
        changing its display list. The page has a large number of display list
        items (10,000), and mutates one every frame. The goal of the test is to
        make displaylist construction a bottleneck, rather than painting or
        other factors, and thus improvements or regressions to displaylist
        construction will be visible. The test runs in ASAP mode to maximize
        framerate, and the result is how quickly the test was able to mutate and
        re-paint 600 items, one during each frame.
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/layout/displaylist_mutate.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(dromaeo)=

::::{dropdown} dromaeo
:class-container: anchor-id-dromaeo

* **Command to Run Locally**

  ```
  ./mach talos-test -a dromaeo
  ```

* description:
      | Dromaeo suite of tests for JavaScript performance testing. See the
        [Dromaeo wiki](https://wiki.mozilla.org/Dromaeo) for more
        information.

      This suite is divided into several sub-suites.

      Each sub-suite is divided into tests, and each test is divided into
      sub-tests. Each sub-test takes some (in theory) fixed piece of work and
      measures how many times that piece of work can be performed in one
      second. The score for a test is then the geometric mean of the
      runs/second numbers for its sub-tests. The score for a sub-suite is the
      geometric mean of the scores for its tests.
::::

(dromaeo_css)=

::::{dropdown} dromaeo_css
:class-container: anchor-id-dromaeo_css

* **Command to Run Locally**

  ```
  ./mach talos-test -a dromaeo_css
  ```

* contact: :emilio, and css/layout team
* source: [css.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/dromaeo)
* type: [Page load]
* reporting: speed in test runs per second (higher is better)
* data: Dromaeo has 6 subtests which run internal benchmarks, each benchmark reports about 180 raw data points each
* summarization:
  * subtest:
        Dromaeo is a custom benchmark which has a lot of micro tests
        inside each subtest, because of this we use a custom [dromaeo
        filter](https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#dromaeo)
        to summarize the subtest. Each micro test produces 5 data points and
        for each 5 data points we take the mean, leaving 36 data points to
        represent the subtest (assuming 180 points). These 36 micro test
        means, are then run through a geometric_mean to produce a single
        number for the dromaeo subtest. [source:
        filter.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l527)
  * suite: [geometric mean] of the 6 subtest results.
* description:
      | Each page in the manifest is part of the dromaeo css benchmark. Each
        page measures the performance of searching the DOM for nodes matching
        various CSS selectors, using different libraries for the selector
        implementation (jQuery, Dojo, Mootools, ExtJS, Prototype, and Yahoo UI).
* **Example Data**

  ```
  0;dojo.html;2209.83;2269.68;2275.47;2278.83;2279.81;4224.43;4344.96;4346.74;4428.69;4459.82;4392.80;4396.38;4412.54;4414.34;4415.62;3909.94;4027.96;4069.08;4099.63;4099.94;4017.70;4018.96;4054.25;4068.74;4081.31;3825.10;3984.20;4053.23;4074.59;4106.63;3893.88;3971.80;4031.15;4046.68;4048.31;3978.24;4010.16;4046.66;4051.68;4056.37;4189.50;4287.98;4390.98;4449.89;4450.20;4536.23;4557.82;4588.40;4662.58;4664.42;4675.51;4693.13;4743.72;4758.12;4764.67;4138.00;4251.60;4346.22;4410.12;4417.23;4677.53;4702.48;4714.62;4802.59;4805.33;4445.07;4539.91;4598.93;4605.45;4618.79;4434.40;4543.09;4618.56;4683.98;4689.51;4485.26;4496.75;4511.23;4600.86;4602.08;4567.52;4608.33;4615.56;4619.31;4622.79;3469.44;3544.11;3605.80;3647.74;3658.56;3101.88;3126.41;3147.73;3159.92;3170.73;3672.28;3686.40;3730.74;3748.89;3753.59;4411.71;4521.50;4633.98;4702.72;4708.76;3626.62;3646.71;3713.07;3713.13;3718.91;3846.17;3846.25;3913.61;3914.63;3916.22;3982.88;4112.98;4132.26;4194.92;4201.54;4472.64;4575.22;4644.74;4645.42;4665.51;4120.13;4142.88;4171.29;4208.43;4211.03;4405.36;4517.89;4537.50;4637.77;4644.28;4548.25;4581.20;4614.54;4658.42;4671.09;4452.78;4460.09;4494.06;4521.30;4522.37;4252.81;4350.72;4364.93;4441.40;4492.78;4251.34;4346.70;4355.00;4358.89;4365.72;4494.64;4511.03;4582.11;4591.79;4592.36;4207.54;4308.94;4309.14;4406.71;4474.46
  1;ext.html;479.65;486.21;489.61;492.94;495.81;24454.14;33580.33;34089.15;34182.83;34186.15;34690.83;35050.30;35051.30;35071.65;35099.82;5758.22;5872.32;6389.62;6525.38;6555.57;8303.96;8532.96;8540.91;8544.00;8571.49;8360.79;8408.79;8432.96;8447.28;8447.83;5817.71;5932.67;8371.83;8389.20;8643.44;7983.80;8073.27;8073.84;8076.48;8078.15;24596.00;32518.84;32787.34;32830.51;32861.00;2220.87;2853.84;3333.53;3345.17;3445.47;24785.75;24971.75;25044.25;25707.61;25799.00;2464.69;2481.89;2527.57;2534.65;2534.92;217793.00;219347.90;219495.00;220059.00;297168.00;40556.19;53062.47;54275.73;54276.00;54440.37;50636.75;50833.49;50983.49;51028.49;51032.74;10746.36;10972.45;11450.37;11692.18;11797.76;8402.58;8415.79;8418.66;8426.75;8428.16;16768.75;16896.00;16925.24;16945.58;17018.15;7047.68;7263.13;7313.16;7337.38;7383.22;713.88;723.72;751.47;861.35;931.00;25454.36;25644.90;25801.87;25992.61;25995.00;819.89;851.23;852.00;886.59;909.89;14325.79;15064.92;15240.39;15431.23;15510.61;452382.00;458194.00;458707.00;459226.00;459601.00;45699.54;46244.54;46270.54;46271.54;46319.00;1073.94;1080.66;1083.35;1085.84;1087.74;26622.33;27807.58;27856.72;28040.58;28217.86;37229.81;37683.81;37710.81;37746.62;37749.81;220386.00;222903.00;240808.00;247394.00;247578.00;25567.00;25568.49;25610.74;25650.74;25710.23;26466.21;28718.71;36175.64;36529.27;36556.00;26676.00;30757.69;31965.84;34521.83;34622.65;32791.18;32884.00;33194.83;33720.16;34192.66;32150.36;32520.02;32851.18;32947.18;33128.01;29472.85;30214.09;30708.54;30999.23;32879.51;23822.88;23978.28;24358.88;24470.88;24515.51
  2;jquery.html;285.42;288.57;292.66;293.75;294.14;10313.00;10688.20;13659.11;13968.65;14003.93;13488.39;13967.51;13980.79;14545.13;15059.77;4361.37;4488.35;4489.44;4492.24;4496.69;3314.32;3445.07;4412.51;5020.75;5216.66;5113.49;5136.56;5141.31;5143.87;5156.28;5055.95;5135.02;5138.64;5215.82;5226.48;4550.98;4551.59;4553.07;4557.77;4559.16;18339.63;18731.53;18738.63;18741.16;18806.15;1474.99;1538.31;1557.52;1703.67;1772.16;12209.94;12335.44;12358.32;12516.50;12651.94;1520.94;1522.62;1541.37;1584.71;1642.50;57533.00;59169.41;59436.11;59758.70;59872.40;8669.13;8789.34;8994.37;9016.05;9064.95;11047.39;11058.39;11063.78;11077.89;11082.78;6401.81;6426.26;6504.35;6518.25;6529.61;6250.22;6280.65;6304.59;6318.91;6328.72;5144.28;5228.40;5236.21;5271.26;5273.79;1398.91;1450.05;1456.39;1494.66;1519.42;727.85;766.62;844.35;858.49;904.87;9912.55;10249.54;14936.71;16566.50;16685.00;378.04;381.34;381.44;385.67;387.23;5362.60;5392.78;5397.14;5497.12;5514.83;213309.00;318297.00;320682.00;322681.00;322707.00;56357.44;67892.66;68329.66;68463.32;69506.00;418.91;424.49;425.19;425.28;426.40;9363.39;9559.95;9644.00;9737.07;9752.80;33170.83;33677.33;34950.83;35607.47;35765.82;44079.34;44588.55;45396.00;46309.00;46427.30;6302.87;6586.51;6607.08;6637.44;6642.17;9776.17;9790.46;9931.90;10391.79;10392.43;8739.26;8838.38;8870.20;8911.50;8955.15;8422.83;8786.21;8914.00;9135.82;9145.36;8945.28;9028.37;9035.23;9116.64;9137.86;6433.90;6688.73;6822.11;6830.08;6833.90;8575.23;8599.87;8610.91;8655.65;9123.91
  3;mootools.html;1161.69;1333.31;1425.89;1500.37;1557.37;6706.93;7648.46;8020.04;8031.36;8049.64;7861.80;7972.40;7978.12;7993.32;7993.88;1838.83;1862.93;1864.11;1866.28;1866.71;1909.93;1921.83;1928.53;1954.07;1969.98;1808.68;1820.01;1821.30;1825.92;1826.91;1849.07;1904.99;1908.26;1911.24;1912.50;1856.86;1871.78;1873.72;1878.54;1929.57;6506.67;6752.73;7799.22;7830.41;7855.18;4117.18;4262.42;4267.30;4268.27;4269.62;2720.56;2795.36;2840.08;2840.79;2842.62;699.12;703.75;774.36;791.73;798.18;11096.22;11126.39;11132.72;11147.16;11157.44;3934.33;4067.37;4140.94;4149.75;4150.38;9042.82;9077.46;9083.55;9084.41;9086.41;4431.47;4432.84;4437.33;4438.40;4440.44;3935.67;3937.31;3937.43;3940.53;3976.68;3247.17;3307.90;3319.90;3323.32;3330.60;1001.90;1016.87;1021.12;1021.67;1022.05;1016.34;1019.09;1036.62;1056.81;1057.76;7345.56;7348.56;7391.89;7393.85;7406.30;374.27;392.53;394.73;397.28;398.26;5588.58;5653.21;5655.07;5659.15;5660.66;9775.41;9860.51;9938.40;9959.85;9968.45;9733.42;9904.31;9953.05;9960.55;9967.20;6399.26;6580.11;7245.93;7336.96;7386.78;7162.00;7245.49;7249.38;7250.75;7304.63;8458.24;8583.40;8651.57;8717.39;8742.39;8896.42;8904.60;8927.96;8960.73;8961.82;7483.48;7747.77;7763.46;7766.34;7773.07;7784.00;7821.41;7827.18;7849.18;7855.49;7012.16;7141.57;7250.09;7253.13;7335.89;6977.97;7015.51;7042.40;7204.35;7237.20;7160.46;7293.23;7321.27;7321.82;7331.16;6268.69;6324.11;6325.78;6328.56;6342.40;6554.54;6625.30;6646.00;6650.30;6674.90
  4;prototype.html;237.05;251.94;256.61;259.65;263.52;4488.53;4676.88;4745.24;4745.50;4748.81;4648.47;4660.21;4666.58;4671.88;4677.32;3602.84;3611.40;3613.69;3615.69;3619.15;3604.41;3619.44;3623.24;3627.66;3628.11;3526.59;3589.35;3615.93;3616.35;3622.80;3624.69;3626.84;3628.47;3631.22;3632.15;3184.76;3186.11;3187.16;3187.78;3189.35;4353.43;4466.46;4482.57;4616.72;4617.88;4012.18;4034.84;4047.07;4047.82;4055.29;4815.11;4815.21;4816.11;4817.08;4820.40;3300.31;3345.18;3369.55;3420.98;3447.97;5026.99;5033.82;5034.50;5034.95;5038.97;3516.72;3520.79;3520.95;3521.81;3523.47;3565.29;3574.23;3574.37;3575.82;3578.37;4045.19;4053.51;4056.76;4058.76;4059.00;4714.67;4868.66;4869.66;4873.54;4878.29;1278.20;1300.92;1301.13;1301.17;1302.47;868.94;871.16;878.50;883.40;884.85;3874.71;3878.44;3881.61;3882.67;3886.92;4959.83;4968.45;4969.50;4971.38;4972.30;3862.69;3870.15;3871.79;3873.83;3878.07;2690.15;2711.66;2714.42;2715.39;2715.89;4349.04;4349.63;4351.33;4353.59;4355.46;4950.95;5101.08;5107.69;5120.21;5120.39;4336.63;4360.76;4361.96;4362.28;4365.43;4928.75;4939.41;4939.56;4943.95;4966.78;4869.03;4886.24;4888.85;4889.14;4895.76;4362.39;4362.78;4363.96;4365.00;4365.08;3408.00;3470.03;3476.37;3546.65;3547.34;4905.73;4926.21;4926.70;4926.93;4929.43;4682.88;4694.91;4696.30;4697.06;4699.69;4688.86;4691.25;4691.46;4698.37;4699.41;4628.07;4631.31;4633.42;4634.00;4636.00;4699.44;4796.02;4808.83;4809.95;4813.52;4719.10;4720.41;4722.95;4723.03;4723.53
  5;yui.html;569.72;602.22;627.02;647.49;692.84;9978.30;10117.54;10121.70;10129.75;10137.24;9278.68;9291.44;9349.00;9370.53;9375.86;475.79;481.92;606.51;607.42;618.73;617.68;618.89;623.30;626.58;631.85;501.81;649.76;653.22;655.69;656.71;510.62;645.56;657.42;657.88;658.39;475.53;476.77;476.80;476.92;476.96;9895.16;9976.15;9988.25;9989.85;9996.40;9483.15;9545.75;9676.37;9808.51;10360.22;8331.29;8397.87;8538.06;8714.69;8803.78;2748.93;2800.93;2802.59;2857.33;2864.46;33757.16;33804.83;33859.32;33931.00;33991.32;7818.65;7846.92;7892.09;8170.55;8217.75;13691.38;13692.86;13693.25;13698.73;13706.66;5378.70;5517.83;5615.86;5616.16;5624.00;2985.63;3002.97;3003.07;3037.73;3038.87;2459.10;2502.52;2504.91;2507.07;2507.26;396.62;405.78;411.43;412.03;412.56;543.45;550.75;568.50;578.59;592.25;6762.21;6901.72;6984.27;7064.22;7122.29;454.78;519.40;539.29;543.96;566.16;3235.39;3266.13;3453.26;3498.79;3518.54;39079.22;39722.80;41350.59;41422.38;41540.17;34435.14;34606.31;34623.31;34661.00;34672.48;29449.12;29530.11;30507.24;31938.52;31961.52;7449.33;7524.62;7629.73;7712.96;7796.42;22917.43;23319.00;23441.41;23582.88;23583.53;29780.40;30272.55;31761.00;31765.84;31839.36;6112.45;6218.35;6476.68;6603.54;6793.66;10385.79;10471.69;10518.53;10552.74;10644.95;9563.52;9571.33;9617.09;9946.35;9976.80;9406.11;9518.48;9806.46;10102.44;10173.19;9482.43;9550.28;9878.21;9902.90;9951.45;8343.17;8511.00;8606.00;8750.21;8869.29;8234.96;8462.70;8473.49;8499.58;8808.91
  ```

* gecko_profile_interval: 2
* tpmanifest: ${talos}/tests/dromaeo/css.manifest
* unit: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
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
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(dromaeo_dom)=

::::{dropdown} dromaeo_dom
:class-container: anchor-id-dromaeo_dom

* **Command to Run Locally**

  ```
  ./mach talos-test -a dromaeo_dom
  ```

* contact: :peterv and dom team
* source: [dom.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/dromaeo)
* type: [Page load]
* data: see Dromaeo DOM
* reporting: speed in test runs per second (higher is better)
* description:
      | Each page in the manifest is part of the dromaeo dom benchmark. These
        are the specific areas that Dromaeo DOM covers:

  * **DOM Attributes**:
       Measures performance of getting and setting a DOM attribute, both via
       ``getAttribute`` and via a reflecting DOM property. Also throws in some
       expando getting/setting for good measure.

   * **DOM Modification**:
       Measures performance of various things that modify the DOM tree:
       creating element and text nodes and inserting them into the DOM.

   * **DOM Query**:
       Measures performance of various methods of looking for nodes in the DOM:
       ``getElementById``, ``getElementsByTagName``, and so forth.

   * **DOM Traversal**:
       Measures performance of various accessors (``childNodes``,
       ``firstChild``, etc) that would be used when doing a walk over the DOM
       tree.

     Please see [dromaeo_css](#dromaeo_css) for examples of data.
* gecko_profile_interval: 2
* tpmanifest: ${talos}/tests/dromaeo/dom.manifest
* unit: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g3-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g3-swr**
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
* - **talos-g3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g3-swr**
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
* - **talos-g3-profiling**
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
* - **talos-g3-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(glterrain)=

::::{dropdown} glterrain
:class-container: anchor-id-glterrain

* **Command to Run Locally**

  ```
  ./mach talos-test -a glterrain
  ```

* contact: :jgilbert and gfx
* source: [glterrain](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/webgl/benchmarks/terrain)
* type: [Page load]
* data: we load the perftest.html page (which generates 4 metrics to track) 25 times, resulting in 4 sets of 25 data points
* summarization: Measures average frames interval while animating a simple WebGL scene
  * subtest: [ignore first] data point, then take the [median] of the remaining 24; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l381)
  * suite: [geometric mean] of the 4 subtest results.
* description:
      | This tests animates a simple WebGL scene (static textured landscape, one
        moving light source, rotating viewport) and measure the frames
        throughput (expressed as average interval) over 100 frames. It runs in
        ASAP mode (vsync off) and measures the same scene 4 times (for all
        combination of antialiasing and alpha. It reports the results as 4
        values) one for each combination. Lower results are better.
* **Example Data**

  ```
  0;0.WebGL-terrain-alpha-no-AA-no;19.8189;20.57185;20.5069;21.09645;20.40045;20.89025;20.34285;20.8525;20.45845;20.6499;19.94505;20.05285;20.316049;19.46745;19.46135;20.63865;20.4789;19.97015;19.9546;20.40365;20.74385;20.828649;20.78295;20.51685;20.97069
  1;1.WebGL-terrain-alpha-no-AA-yes;23.0464;23.5234;23.34595;23.40609;22.54349;22.0554;22.7933;23.00685;23.023649;22.51255;23.25975;23.65819;22.572249;22.9195;22.44325;22.95015;23.3567;23.02089;22.1459;23.04545;23.09235;23.40855;23.3296;23.18849;23.273249
  2;2.WebGL-terrain-alpha-yes-AA-no;24.01795;23.889449;24.2683;24.34649;23.0562;24.02275;23.54819;24.1874;23.93545;23.53629;23.305149;23.62459;24.01589;24.06405;24.143449;23.998549;24.08205;24.26989;24.0736;24.2346;24.01145;23.7817;23.90785;24.7118;24.2834
  3;3.WebGL-terrain-alpha-yes-AA-yes;25.91375;25.87005;25.64875;25.15615;25.5475;24.497449;24.56385;25.57529;25.54889;26.31559;24.143949;25.09895;24.75049;25.2087;25.52385;25.9017;25.4439;24.3495;25.9269;25.734449;26.4126;25.547449;25.667249;25.679349;25.9565
  ```

* gecko_profile_interval: 10
* linux_counters: None
* mac_counters: None
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/webgl/glterrain.manifest
* tpmozafterpaint: False
* tppagecycles: 25
* unit: frame interval
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(glvideo)=

::::{dropdown} glvideo
:class-container: anchor-id-glvideo

* **Command to Run Locally**

  ```
  ./mach talos-test -a glvideo
  ```

* contact: :jgilbert and gfx
* source: [glvideo](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/webgl/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: WebGL video texture update with 1080p video. Measures mean tick time across 100 ticks.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l522)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean tick time across 100 ticks: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test playbacks a video file and ask WebGL to draw video frames as
        WebGL textures for 100 ticks. It collects the mean tick time across 100
        ticks to measure how much time it will spend for a video texture upload
        to be a WebGL texture (gl.texImage2D). We run it for 5 times and ignore
        the first found. Lower results are better.
* gecko_profile_extra_threads: CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/webgl/glvideo.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(kraken)=

::::{dropdown} kraken
:class-container: anchor-id-kraken

* **Command to Run Locally**

  ```
  ./mach talos-test -a kraken
  ```

* contact: :sdetar, jandem, and SpiderMonkey Team
* source: [kraken.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/kraken)
* type: [Page load]
* measuring: JavaScript performance
* reporting: Total time for all tests, in ms (lower is better)
* data: there are 14 subtests in kraken, each subtest is an internal benchmark and generates 10 data points, or 14 sets of 10 data points.
* summarization:
  * subtest: For all of the 10 data points, we take the
        [mean](https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#mean)
        to report a single number.
  * suite: [geometric mean] of the 14 subtest results.
* description:
      | This is the [Kraken](https://wiki.mozilla.org/Kraken) javascript
        benchmark taken verbatim and slightly modified to fit into our
        pageloader extension and Talos harness.
* **Example Data**

  ```
  0;ai-astar;100;95;98;102;101;99;97;98;98;102
  1;audio-beat-detection;147;147;191;173;145;139;186;143;183;140
  2;audio-dft;161;156;158;157;160;158;160;160;159;158
  3;audio-fft;82;83;83;154;83;83;82;83;160;82
  4;audio-oscillator;96;96;141;95;95;95;129;96;95;134
  5;imaging-gaussian-blur;116;115;116;115;115;115;115;115;117;116
  6;imaging-darkroom;166;164;166;165;166;166;165;165;165;166
  7;imaging-desaturate;87;87;87;87;88;87;88;87;87;87
  8;json-parse-financial;75;77;77;76;77;76;77;76;77;77
  9;json-stringify-tinderbox;79;79;80;79;78;79;79;78;79;79
  10;stanford-crypto-aes;98;97;96;98;98;98;98;98;113;95
  11;stanford-crypto-ccm;130;138;130;127;137;134;134;132;147;129
  12;stanford-crypto-pbkdf2;176;187;183;183;176;174;181;187;175;173
  13;stanford-crypto-sha256-iterative;86;85;83;84;86;85;85;86;83;83
  ```

* gecko_profile_interval: 1
* preferences: {'dom.send_after_paint_to_content': False}
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/kraken/kraken.manifest
* tpmozafterpaint: False
* tppagecycles: 1
* unit: score
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
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
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-dromaeojs-profiling**
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
* - **talos-dromaeojs**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(motionmark_animometer)=

::::{dropdown} motionmark_animometer
:class-container: anchor-id-motionmark_animometer

* **Command to Run Locally**

  ```
  ./mach talos-test -a motionmark_animometer
  ```

* contact: :b0bh00d, :jeffm, and gfx
* source: [source](https://searchfox.org/firefox-main/source/third_party/webkit/PerformanceTests/MotionMark) [manifests](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/motionmark)
* type: [Page load]
* measuring: benchmark measuring the time to animate complex scenes
* summarization:
  * subtest: FPS from the subtest, each subtest is run for 15 seconds,
        repeat this 5 times and report the median value
  * suite: we take a geometric mean of all the subtests (9 for
        animometer, 11 for html suite)
* tpmanifest: ${talos}/tests/motionmark/animometer.manifest
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(motionmark_htmlsuite)=

::::{dropdown} motionmark_htmlsuite
:class-container: anchor-id-motionmark_htmlsuite

* **Command to Run Locally**

  ```
  ./mach talos-test -a motionmark_htmlsuite
  ```

* contact: :jrmuizel and graphics(gfx) team
* tpmanifest: ${talos}/tests/motionmark/htmlsuite.manifest
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
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
* - **talos-motionmark-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(motionmark_webgl)=

::::{dropdown} motionmark_webgl
:class-container: anchor-id-motionmark_webgl

* **Command to Run Locally**

  ```
  ./mach talos-test -a motionmark_webgl
  ```

* contact: :jgilbert and gfx
* source: [source](https://searchfox.org/firefox-main/source/third_party/webkit/PerformanceTests/MotionMark) [manifest](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/motionmark/webgl.manifest)
* type: [Page load]
* measuring: Draw call performance in WebGL
* summarization:
  * subtest: FPS from the subtest, each subtest is run once for 15
        seconds, report the average FPS over that time.
  * suite: identical to subtest
* timeout: 600
* tpmanifest: ${talos}/tests/motionmark/webgl.manifest
* unit: fps
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_main_2d_av1)=

::::{dropdown} offscreencanvas_webcodecs_main_2d_av1
:class-container: anchor-id-offscreencanvas_webcodecs_main_2d_av1

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_2d_av1
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_2d_av1](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p AV1 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l843)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a AV1 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_av1.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_main_2d_h264)=

::::{dropdown} offscreencanvas_webcodecs_main_2d_h264
:class-container: anchor-id-offscreencanvas_webcodecs_main_2d_h264

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_2d_h264
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_2d_h264](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p H264 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a H264 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_h264.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
::::

(offscreencanvas_webcodecs_main_2d_vp9)=

::::{dropdown} offscreencanvas_webcodecs_main_2d_vp9
:class-container: anchor-id-offscreencanvas_webcodecs_main_2d_vp9

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_2d_vp9
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_2d_vp9](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p VP9 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l819)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a VP9 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_2d_vp9.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_main_webgl_av1)=

::::{dropdown} offscreencanvas_webcodecs_main_webgl_av1
:class-container: anchor-id-offscreencanvas_webcodecs_main_webgl_av1

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_webgl_av1
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_webgl_av1](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p AV1 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a AV1 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_av1.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_main_webgl_h264)=

::::{dropdown} offscreencanvas_webcodecs_main_webgl_h264
:class-container: anchor-id-offscreencanvas_webcodecs_main_webgl_h264

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_webgl_h264
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_webgl_h264](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p H264 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a H264 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_h264.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_main_webgl_vp9)=

::::{dropdown} offscreencanvas_webcodecs_main_webgl_vp9
:class-container: anchor-id-offscreencanvas_webcodecs_main_webgl_vp9

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_main_webgl_vp9
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_main_webgl_vp9](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p VP9 video on the main thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a VP9 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on the main thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_main_webgl_vp9.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_worker_2d_av1)=

::::{dropdown} offscreencanvas_webcodecs_worker_2d_av1
:class-container: anchor-id-offscreencanvas_webcodecs_worker_2d_av1

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_2d_av1
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_2d_av1](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p AV1 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a AV1 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on a DOM worker thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_av1.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_worker_2d_h264)=

::::{dropdown} offscreencanvas_webcodecs_worker_2d_h264
:class-container: anchor-id-offscreencanvas_webcodecs_worker_2d_h264

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_2d_h264
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_2d_h264](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p H264 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a H264 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on a DOM worker thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_h264.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
::::

(offscreencanvas_webcodecs_worker_2d_vp9)=

::::{dropdown} offscreencanvas_webcodecs_worker_2d_vp9
:class-container: anchor-id-offscreencanvas_webcodecs_worker_2d_vp9

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_2d_vp9
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_2d_vp9](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas 2D video texture update with WebCodecs and 1080p VP9 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a VP9 video file using WebCodecs and ask Canvas2D to draw video
        frames as Canvas2D textures for 100 frames on a DOM worker thread. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        Canvas2D texture (ctx.drawImage). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,CanvasWorker,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_2d_vp9.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_worker_webgl_av1)=

::::{dropdown} offscreencanvas_webcodecs_worker_webgl_av1
:class-container: anchor-id-offscreencanvas_webcodecs_worker_webgl_av1

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_webgl_av1
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_webgl_av1](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p AV1 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a AV1 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on a DOM worker. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_av1.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_worker_webgl_h264)=

::::{dropdown} offscreencanvas_webcodecs_worker_webgl_h264
:class-container: anchor-id-offscreencanvas_webcodecs_worker_webgl_h264

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_webgl_h264
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_webgl_h264](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p H264 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a H264 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on a DOM worker. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_h264.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(offscreencanvas_webcodecs_worker_webgl_vp9)=

::::{dropdown} offscreencanvas_webcodecs_worker_webgl_vp9
:class-container: anchor-id-offscreencanvas_webcodecs_worker_webgl_vp9

* **Command to Run Locally**

  ```
  ./mach talos-test -a offscreencanvas_webcodecs_worker_webgl_vp9
  ```

* contact: :aosmond and gfx
* source: [offscreencanvas_webcodecs_worker_webgl_vp9](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/offscreencanvas/benchmarks/video)
* type: [Page load]
* data: 5 cycles of the entire benchmark, each subtest will have 5 data points (see below)
* summarization: OffscreenCanvas WebGL video texture update with WebCodecs and 1080p VP9 video on a DOM worker thread. Measures mean frame time across 100 frames.
  * subtest: [ignore first] data point, then take the [median] of the remaining 4; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l795)
  * suite: [geometric mean] of the 4 subtest results.
* **Lower is better**
* **Example Data**

  ```
  0;Mean frame time across 100 frames: ;54.6916;49.0534;51.21645;51.239650000000005;52.44295
  ```

* description:
      | This test extracts frames from a VP9 video file using WebCodecs and ask WebGL to draw video
        frames as WebGL textures for 100 frames on a DOM worker. It collects the mean frame time
        across 100 frames to measure how much time it will spend for a video texture upload to be a
        WebGL texture (gl.texImage2D). We run it for 5 times and ignore the first found. Lower
        results are better.
* gecko_profile_extra_threads: DOM Worker,CanvasRenderer,MediaSupervisor
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/offscreencanvas/offscreencanvas_webcodecs_worker_webgl_vp9.manifest
* tpmozafterpaint: False
* tppagecycles: 5
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-webgl-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
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
* - **talos-webgl**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-webgl-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(pdfpaint)=

::::{dropdown} pdfpaint
:class-container: anchor-id-pdfpaint

* **Command to Run Locally**

  ```
  ./mach talos-test -a pdfpaint
  ```

* contact: :calixte and CI and Quality Tools team
* source:
* type: [Page load]
* reporting: time from *performance.timing.navigationStart* to *pagerendered* event in ms (lower is better)
* data: loads a PDF 5 times
* description:
      | Runs through a set of chunks. Each chunk runs 100 PDFs with 5 iterations each.
        If --pdfPaintChunk is not used when running the test locally, all PDFs will be tested
        by default with only 1 cycle each. The PDFs that are run are found in a private Mozilla
        repository. Locally, the artifact from the toolchain task in CI needs to be downloaded and
        installed manually to run this test. Running the test locally with outdated/non-existent PDFs will
        provide a link to the latest artifact that needs to be downloaded and installed.
* alert_threshold: 6.0
* pdfpaint: True
* subtest_alerts: True
* timeout: 2000
* tpmanifest: ${talos}/tests/pdfpaint/pdfpaint.manifest
* tppagecycles: 1
* tptimeout: 60000
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-pdfpaint-1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-1-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-10**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-10-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-2**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-2-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-3-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-4-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-5-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-6**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-6-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-7**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-7-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-8**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-8-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-9**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-9-swr**
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
* - **talos-pdfpaint-1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-1-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-10**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-10-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-2-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-3-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-5-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-6**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-6-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-7**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-7-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-8**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-8-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-9-swr**
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
* - **talos-pdfpaint-1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-1-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-10**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-10-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-2**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-2-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-3-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-5-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-6**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-6-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-7**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-7-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-8**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-8-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-9**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-9-swr**
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
* - **talos-pdfpaint-1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-1-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-10**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-10-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-2**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-2-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-3**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-3-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-4-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-5-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-6**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-6-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-7**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-7-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-8**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-8-swr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-9**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-pdfpaint-9-swr**
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
* - **talos-pdfpaint-1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-1-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-10**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-10-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-2**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-2-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-3**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-3-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-5-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-6**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-6-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-7**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-7-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-8**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-8-swr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-9**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-pdfpaint-9-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(perf_reftest)=

::::{dropdown} perf_reftest
:class-container: anchor-id-perf_reftest

* **Command to Run Locally**

  ```
  ./mach talos-test -a perf_reftest
  ```

* contact: :emilio and css/layout team
* source: [perf-reftest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/perf-reftest)
* type: [Page load]
* reporting: intervals in ms (lower is better)
* data: each test loads 25 times
* summarization:
  * subtest: [ignore first] 5 data points, then take the [median] of the remaining 20 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l734)
  * suite: identical to subtest
* description:
      | **Important note:** This test now requires an 'opt' build. If the
        perf-reftest is ran on a non-opt build, it will time out (more
        specifically on innertext-1.html, and possibly others in the future).

      Style system performance test suite. The perf-reftest suite is a unique
      Talos suite where each subtest loads two different test pages: a 'base'
      page (i.e. bloom_basic) and a 'reference' page (i.e. bloom_basic_ref),
      and then compares each of the page load times against eachother to
      determine the variance.

      Talos runs each of the two pages as if they are stand-alone tests, and
      then calculates and reports the variance; the test output 'replicates'
      reported from bloom_basic are actually the comparisons between the
      'base' and 'reference' pages for each page load cycle. The suite
      contains multiple subtests, each of which contains a base page and a
      reference page.

      If you wish to see the individual 'base' and 'reference' page results
      instead of just the reported difference, the 'base_replicates' and
      'ref_replicates' can be found in the PERFHERDER_DATA log file output,
      and in the 'local.json' Talos output file when running Talos locally. In
      production, both of the page replicates are also archived in the
      perfherder-data.json file. The perfherder-data.json file is archived
      after each run in production, and can be found on the Treeherder Job
      Details tab when the perf-reftest job symbol is selected.

      This test suite was ported over from the [style-perf-tests](https://github.com/heycam/style-perf-tests).
* **Example Data**

  ```
  "replicates": [1.185, 1.69, 1.22, 0.36, 11.26, 3.835, 3.315, 1.355, 3.185, 2.485, 2.2, 1.01, 0.9, 1.22, 1.9,
        0.285, 1.52, 0.31, 2.58, 0.725, 2.31, 2.67, 3.295, 1.57, 0.3], "value": 1.7349999999999999, "unit": "ms",
  "base_replicates": [166.94000000000003, 165.16, 165.64000000000001, 165.04000000000002, 167.355, 165.175,
        165.325, 165.11, 164.175, 164.78, 165.555, 165.885, 166.83499999999998, 165.76500000000001, 164.375, 166.825,
        167.13, 166.425, 169.22500000000002, 164.955, 165.335, 164.45000000000002, 164.85500000000002, 165.005, 166.035]}],
  "ref_replicates": [165.755, 166.85000000000002, 166.85999999999999, 165.4, 178.615, 169.01, 168.64, 166.465,
        167.36, 167.265, 167.75500000000002, 166.895, 167.735, 166.985, 166.275, 166.54000000000002, 165.61, 166.115,
        166.64499999999998, 165.68, 167.64499999999998, 167.12, 168.15, 166.575, 166.335],
  ```

* alert_threshold: 5.0
* base_vs_ref: True
* gecko_profile_interval: 1
* lower_is_better: True
* subtest_alerts: True
* tpcycles: 1
* tpmanifest: ${talos}/tests/perf-reftest/perf_reftest.manifest
* tppagecycles: 10
* tptimeout: 30000
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-perf-reftest**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-perf-reftest-swr**
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
* - **talos-perf-reftest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-perf-reftest-swr**
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
* - **talos-perf-reftest**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-perf-reftest-swr**
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
* - **talos-perf-reftest**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-perf-reftest-swr**
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
* - **talos-perf-reftest**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-perf-reftest-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(perf_reftest_singletons)=

::::{dropdown} perf_reftest_singletons
:class-container: anchor-id-perf_reftest_singletons

* **Command to Run Locally**

  ```
  ./mach talos-test -a perf_reftest_singletons
  ```

* contact: :emilio and Layout team
* source: [perf-reftest-singletons](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/perf-reftest-singletons)
* type: [Page load]
* reporting: intervals in ms (lower is better)
* data: each test loads 25 times
* summarization:
  * subtest: [ignore first] 5 data points, then take the [median] of the remaining 20 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l734)
  * suite: identical to subtest
* description:
      | Individual style system performance tests. The perf-reftest-singletons
        suite runs the perf-reftest 'base' pages (i.e. bloom_basic) test
        individually, and reports the values for that single test page alone,
        NOT the comparison of two different pages. There are multiple subtests
        in this suite, each just containing the base page on its own.

      This test suite was ported over from the [style-perf-tests](https://github.com/heycam/style-perf-tests).
* **Example Data**

  ```
  bloombasic.html;88.34000000000003;88.66499999999999;94.815;92.60500000000002;95.30000000000001;
  98.80000000000001;91.975;87.73500000000001;86.925;86.965;93.00500000000001;98.93;87.45000000000002;
  87.14500000000001;92.78500000000001;86.96499999999999;98.32000000000001;97.485;90.67000000000002;
  86.72500000000001;95.665;100.67;101.095;94.32;91.87
  ```

* alert_threshold: 5.0
* gecko_profile_interval: 1
* lower_is_better: True
* subtest_alerts: True
* suite_should_alert: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/perf-reftest-singletons/perf_reftest_singletons.manifest
* tppagecycles: 15
* tptimeout: 30000
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-perf-reftest-singletons**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-singletons-profiling**
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
* - **talos-perf-reftest-singletons**
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
* - **talos-perf-reftest-singletons**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-singletons-profiling**
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
* - **talos-perf-reftest-singletons**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-perf-reftest-singletons-profiling**
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
* - **talos-perf-reftest-singletons**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(rasterflood_gradient)=

::::{dropdown} rasterflood_gradient
:class-container: anchor-id-rasterflood_gradient

* **Command to Run Locally**

  ```
  ./mach talos-test -a rasterflood_gradient
  ```

* contact: :jrmuizel, :jimm, and gfx
* source: [rasterflood_gradient.html](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/gfx/benchmarks/rasterflood_gradient.html)
* type: [Page load]
* data: we load the rasterflood_gradient.html page ten times, computing a score each time, generating 10 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 9; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l986)
* description:
      | This page animates some complex gradient patterns in a
        requestAnimationFrame callback. However, it also churns the CPU during
        each callback, spinning an empty loop for 14ms each frame. The intent is
        that, if we consider the rasterization costs to be 0, then the animation
        should run close to 60fps. Otherwise it will lag. Since rasterization
        costs are not 0, the lower we can get them, the faster the test will
        run. The test runs in ASAP mode to maximize framerate.

      The test runs for 10 seconds, and the resulting score is how many frames
      we were able to render during that time. Higher is better. Improvements
      (or regressions) to general painting performance or gradient rendering
      will affect this benchmark.
* gecko_profile_interval: 2
* linux_counters: None
* lower_is_better: False
* mac_counters: None
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/gfx/rasterflood_gradient.manifest
* tpmozafterpaint: False
* tppagecycles: 10
* unit: score
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(rasterflood_svg)=

::::{dropdown} rasterflood_svg
:class-container: anchor-id-rasterflood_svg

* **Command to Run Locally**

  ```
  ./mach talos-test -a rasterflood_svg
  ```

* contact: :jrmuizel, :jimm, and gfx
* source: [rasterflood_svg.html](https://searchfox.org/firefox-main/source/testing/talos/talos/tests/gfx/benchmarks/rasterflood_svg.html)
* type: [Page load]
* data: we load the rasterflood_svg.html page ten times, measuring pageload each time, generating 10 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 9; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l986)
* description:
      | This page animates some complex SVG patterns in a requestAnimationFrame
        callback. However, it also churns the CPU during each callback, spinning
        an empty loop for 14ms each frame. The intent is that, if we consider
        the rasterization costs to be 0, then the animation should run close to
        60fps. Otherwise it will lag. Since rasterization costs are not 0, the
        lower we can get them, the faster the test will run. The test runs in
        ASAP mode to maximize framerate. The result is how quickly the browser
        is able to render 600 frames of the animation.

      Improvements (or regressions) to general painting performance or SVG are
      likely to affect this benchmark.
* gecko_profile_interval: 2
* linux_counters: None
* mac_counters: None
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/gfx/rasterflood_svg.manifest
* tpmozafterpaint: False
* tppagecycles: 10
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g4-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
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
* - **talos-g4**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g4-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(sessionrestore)=

::::{dropdown} sessionrestore
:class-container: anchor-id-sessionrestore

* **Command to Run Locally**

  ```
  ./mach talos-test -a sessionrestore
  ```

* contact: :dale, :dao, :farre, session restore module owners/peers, and DOM team
* source: [talos/sessionrestore](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/startup_test/sessionrestore)
* bug: [bug 936630](https://bugzilla.mozilla.org/show_bug.cgi?id=936630),
     [bug 1331937](https://bugzilla.mozilla.org/show_bug.cgi?id=1331937),
     [bug 1531520](https://bugzilla.mozilla.org/show_bug.cgi?id=1531520)
* type: Startup_
* measuring: time spent reading and restoring the session.
* reporting: interval in ms (lower is better).
* data: we load the session restore index page 10 times to collect 1 set of 10 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 9 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l305)
  * suite: identical to subtest
* description:
      | Three tests measure the time spent reading and restoring the session
        from a valid sessionstore.js. Time is counted from the *process start*
        until the *sessionRestored* event.

      In *sessionrestore*, this is tested with a configuration that requires
      the session to be restored. In *sessionrestore_no_auto_restore*, this is
      tested with a configuration that requires the session to not be
      restored. Both of the above tests use a sessionstore.js file that
      contains one window and roughly 89 tabs. In
      *sessionrestore_many_windows*, this is tested with a sessionstore.js
      that contains 3 windows and 130 tabs. The first window contains 50 tabs,
      80 remaning tabs are divided equally between the second and the third
      window.
* **Example Data**

  ```
  [2362.0, 2147.0, 2171.0, 2134.0, 2116.0, 2145.0, 2141.0, 2141.0, 2136.0, 2080.0]
  ```

* cycles: 10
* extensions: ['${talos}/startup_test/sessionrestore/addon']
* gecko_profile_startup: True
* pine: False
* preferences: {'browser.startup.page': 3}
* profile_path: ${talos}/startup_test/sessionrestore/profile
* reinstall: ['sessionstore.jsonlz4', 'sessionstore.js', 'sessionCheckpoints.json']
* timeout: 900
* unit: ms
* url: about:home
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(sessionrestore_many_windows)=

::::{dropdown} sessionrestore_many_windows
:class-container: anchor-id-sessionrestore_many_windows

* **Command to Run Locally**

  ```
  ./mach talos-test -a sessionrestore_many_windows
  ```

* See [sessionrestore](#sessionrestore).
* profile_path: ${talos}/startup_test/sessionrestore/profile-manywindows
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-sessionrestore-many-windows**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-swr**
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
* - **talos-sessionrestore-many-windows**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-swr**
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
* - **talos-sessionrestore-many-windows**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-swr**
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
* - **talos-sessionrestore-many-windows**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-swr**
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
* - **talos-sessionrestore-many-windows**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-sessionrestore-many-windows-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(sessionrestore_no_auto_restore)=

::::{dropdown} sessionrestore_no_auto_restore
:class-container: anchor-id-sessionrestore_no_auto_restore

* **Command to Run Locally**

  ```
  ./mach talos-test -a sessionrestore_no_auto_restore
  ```

* See [sessionrestore](#sessionrestore).
* preferences: {'browser.startup.page': 1, 'talos.sessionrestore.norestore': True}
* timeout: 300
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(startup_about_home_paint)=

::::{dropdown} startup_about_home_paint
:class-container: anchor-id-startup_about_home_paint

* **Command to Run Locally**

  ```
  ./mach talos-test -a startup_about_home_paint
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* source: [addon](https://hg.mozilla.org/mozilla-central/file/tip/testing/talos/talos/startup_test/startup_about_home_paint/addon/)
* type: Startup_
* measuring: The time from process start to the point where the about:home page reports that it has painted the Top Sites.
* data: we load restart the browser 20 times, and collect a timestamp for each run.
* reporting: test time in ms (lower is better)
* **Example Data**

  ```
  [1503.0, 1497.0, 1523.0, 1536.0, 1511.0, 1485.0, 1594.0, 1580.0, 1531.0, 1471.0, 1502.0, 1520.0, 1488.0, 1533.0, 1531.0, 1502.0, 1486.0, 1489.0, 1487.0, 1475.0]
  ```

* cycles: 20
* extensions: ['${talos}/startup_test/startup_about_home_paint/addon']
* pine: False
* preferences: {'browser.startup.homepage.abouthome_cache.enabled': False}
* timeout: 600
* tpmanifest: ${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest
* url: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(startup_about_home_paint_cached)=

::::{dropdown} startup_about_home_paint_cached
:class-container: anchor-id-startup_about_home_paint_cached

* **Command to Run Locally**

  ```
  ./mach talos-test -a startup_about_home_paint_cached
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* See [startup_about_home_paint](#startup_about_home_paint).
* description:
      | Tests loading about:home on startup with the about:home startup cache enabled.
* cycles: 20
* extensions: ['${talos}/startup_test/startup_about_home_paint/addon']
* pine: False
* preferences: {'browser.startup.homepage.abouthome_cache.enabled': True}
* tpmanifest: ${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest
* url: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(startup_about_home_paint_realworld_webextensions)=

::::{dropdown} startup_about_home_paint_realworld_webextensions
:class-container: anchor-id-startup_about_home_paint_realworld_webextensions

* **Command to Run Locally**

  ```
  ./mach talos-test -a startup_about_home_paint_realworld_webextensions
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* source: [addon](https://hg.mozilla.org/mozilla-central/file/tip/testing/talos/talos/startup_test/startup_about_home_paint/addon/)
* type: Startup_, [Real-world WebExtensions]
* measuring: The time from process start to the point where the about:home page reports that it has painted the Top Sites when 5 popular, real-world WebExtensions are installed and enabled.
* data: we install the 5 real-world WebExtensions, then load and restart the browser 20 times, and collect a timestamp for each run.
* reporting: test time in ms (lower is better)
* **Example Data**

  ```
  [1503.0, 1497.0, 1523.0, 1536.0, 1511.0, 1485.0, 1594.0, 1580.0, 1531.0, 1471.0, 1502.0, 1520.0, 1488.0, 1533.0, 1531.0, 1502.0, 1486.0, 1489.0, 1487.0, 1475.0]
  ```

* cycles: 20
* extensions: ['${talos}/startup_test/startup_about_home_paint/addon', '${talos}/getinfooffline']
* pine: False
* preferences: {'browser.startup.homepage.abouthome_cache.enabled': False}
* tpmanifest: ${talos}/startup_test/startup_about_home_paint/startup_about_home_paint.manifest
* url: None
* webextensions_folder: ${talos}/webextensions
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-realworld-webextensions**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-realworld-webextensions-profiling**
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
* - **talos-realworld-webextensions**
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
* - **talos-realworld-webextensions**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-realworld-webextensions-profiling**
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
* - **talos-realworld-webextensions**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-realworld-webextensions-profiling**
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
* - **talos-realworld-webextensions**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(stylebench)=

::::{dropdown} stylebench
:class-container: anchor-id-stylebench

* **Command to Run Locally**

  ```
  ./mach talos-test -a stylebench
  ```

* contact: :emilio and Layout team
* source: [stylebench.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/stylebench/stylebench.manifest)
* type: [Page load]
* measuring: speed of dynamic style recalculation
* reporting: runs/minute score
* tpmanifest: ${talos}/tests/stylebench/stylebench.manifest
::::

(tabpaint)=

::::{dropdown} tabpaint
:class-container: anchor-id-tabpaint

* **Command to Run Locally**

  ```
  ./mach talos-test -a tabpaint
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* source: [tabpaint](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/tabpaint)
* bug: https://bugzilla.mozilla.org/show_bug.cgi?id=1253382
* type: [Page load]
* measuring:
  * The time it takes to paint the content of a newly opened tab when
        the tab is opened from the parent (ex: by hitting Ctrl-T)
  * The time it takes to paint the content of a newly opened tab when
        the tab is opened from content (ex: by clicking on a target="_blank" link)
* **NOT** measuring:
  * The time it takes to animate the tabs. That's the responsibility
        of the TART test. tabpaint is strictly concerned with the painting of the web content.
* data: we load the tabpaint trigger page 20 times, each run produces
     two values (the time it takes to paint content when opened from the
     parent, and the time it takes to paint content when opened from
     content), resulting in 2 sets of 20 data points.
* **Example Data**

  ```
  0;tabpaint-from-parent;105;76;66;64;64;69;65;63;70;68;64;60;65;63;54;61;64;67;61;64
  1;tabpaint-from-content;129;68;72;72;70;78;86;85;82;79;120;92;76;80;74;82;76;89;77;85
  ```

* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 19 data points
  * suite: geometric_mean(subtests)
* extensions: ['${talos}/tests/tabpaint', '${talos}/pageloader']
* preferences: {'browser.link.open_newwindow': 3, 'browser.link.open_newwindow.restriction': 2, 'browser.newtab.preload': False}
* timeout: 600
* tploadnocache: True
* tpmanifest: ${talos}/tests/tabpaint/tabpaint.manifest
* tppagecycles: 20
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tabswitch)=

::::{dropdown} tabswitch
:class-container: anchor-id-tabswitch

* **Command to Run Locally**

  ```
  ./mach talos-test -a tabswitch
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* source: [tabswitch](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/tabswitch)
* bug: https://bugzilla.mozilla.org/show_bug.cgi?id=1166132
* type: [Page load]
* measuring:
  * The time between switching a tab and the first paint to the content area
* reporting:
* data: we load 50 web pages 5 times each, resulting in 50 sets of 5 data points.
* **To run it locally**, you'd need [tp5n.zip](#page-sets).
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 4 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l305)
  * suite: [geometric mean] of the 50 subtest results.
* **Example Data**

  ```
  0;amazon.com/www.amazon.com/Kindle-Wireless-Reader-Wifi-Graphite/dp/B002Y27P3M/507846.html;66.34;54.15;53.08;55.79;49.12
  1;cgi.ebay.com/cgi.ebay.com/ALL-NEW-KINDLE-3-eBOOK-WIRELESS-READING-DEVICE-W-WIFI-/130496077314@pt=LH_DefaultDomain_0&hash=item1e622c1e02.html;50.85;46.57;39.51;36.71;36.47
  2;163.com/www.163.com/index.html;95.05;80.80;76.09;69.29;68.96
  3;mail.ru/mail.ru/index.html;66.21;52.04;56.33;55.11;45.61
  4;bbc.co.uk/www.bbc.co.uk/news/index.html;35.80;44.59;48.02;45.71;42.58
  5;store.apple.com/store.apple.com/us@mco=Nzc1MjMwNA.html;52.98;49.45;58.47;56.79;61.29
  6;imdb.com/www.imdb.com/title/tt1099212/index.html;46.51;55.12;46.22;50.60;47.63
  7;cnn.com/www.cnn.com/index.html;43.02;50.77;43.88;49.70;50.02
  8;sohu.com/www.sohu.com/index.html;74.03;62.89;63.30;67.71;89.35
  9;youku.com/www.youku.com/index.html;43.98;52.69;45.80;63.00;57.02
  10;ifeng.com/ifeng.com/index.html;88.01;87.54;104.47;94.93;113.91
  11;tudou.com/www.tudou.com/index.html;45.32;48.10;46.03;39.26;58.38
  12;chemistry.about.com/chemistry.about.com/index.html;38.24;37.07;39.59;39.48;39.60
  13;beatonna.livejournal.com/beatonna.livejournal.com/index.html;35.59;50.75;36.17;48.49;52.61
  14;rakuten.co.jp/www.rakuten.co.jp/index.html;90.28;71.95;62.66;60.33;67.76
  15;uol.com.br/www.uol.com.br/index.html;42.89;48.05;53.77;40.02;42.41
  16;thepiratebay.org/thepiratebay.org/top/201.html;40.46;42.46;47.63;57.66;45.49
  17;page.renren.com/page.renren.com/index.html;47.61;66.78;47.91;62.78;47.19
  18;chinaz.com/chinaz.com/index.html;50.34;58.17;118.43;55.47;63.80
  19;globo.com/www.globo.com/index.html;41.34;38.52;42.82;53.14;45.20
  20;spiegel.de/www.spiegel.de/index.html;33.60;34.34;36.25;36.18;47.04
  21;dailymotion.com/www.dailymotion.com/us.html;37.68;36.13;39.52;37.15;42.79
  22;goo.ne.jp/goo.ne.jp/index.html;50.74;47.30;63.04;58.41;58.96
  23;stackoverflow.com/stackoverflow.com/questions/184618/what-is-the-best-comment-in-source-code-you-have-ever-encountered.html;44.66;44.40;43.39;47.38;57.65
  24;ezinearticles.com/ezinearticles.com/index.html@Migraine-Ocular---The-Eye-Migraines&id=4684133.html;37.38;45.01;40.29;36.26;39.28
  25;huffingtonpost.com/www.huffingtonpost.com/index.html;39.57;43.35;55.01;44.13;58.28
  26;media.photobucket.com/media.photobucket.com/image/funny%20gif/findstuff22/Best%20Images/Funny/funny-gif1.jpg@o=1.html;39.77;42.46;75.54;42.38;47.72
  27;imgur.com/imgur.com/gallery/index.html;34.72;37.37;46.74;40.93;37.08
  28;reddit.com/www.reddit.com/index.html;42.47;39.89;51.54;51.51;41.68
  29;noimpactman.typepad.com/noimpactman.typepad.com/index.html;54.28;47.40;52.38;52.15;50.97
  30;myspace.com/www.myspace.com/albumart.html;48.97;64.12;61.66;48.32;68.53
  31;mashable.com/mashable.com/index.html;36.76;40.95;35.30;53.86;42.76
  32;dailymail.co.uk/www.dailymail.co.uk/ushome/index.html;42.06;40.64;44.24;37.32;61.35
  33;whois.domaintools.com/whois.domaintools.com/mozilla.com.html;34.73;35.23;39.25;48.24;35.72
  34;indiatimes.com/www.indiatimes.com/index.html;52.67;55.51;46.29;52.69;58.82
  35;reuters.com/www.reuters.com/index.html;32.92;33.08;36.95;39.23;39.27
  36;xinhuanet.com/xinhuanet.com/index.html;125.85;102.56;138.89;130.34;147.45
  37;56.com/www.56.com/index.html;63.89;75.00;61.45;62.20;58.67
  38;bild.de/www.bild.de/index.html;35.61;43.74;34.79;33.45;31.83
  39;guardian.co.uk/www.guardian.co.uk/index.html;38.91;55.93;62.34;42.63;45.99
  40;naver.com/www.naver.com/index.html;78.10;89.07;127.67;75.18;109.32
  41;yelp.com/www.yelp.com/biz/alexanders-steakhouse-cupertino.html;42.54;46.92;39.19;49.82;50.43
  42;wsj.com/online.wsj.com/home-page.html;46.43;55.51;44.16;81.79;48.78
  43;google.com/www.google.com/search@q=mozilla.html;35.62;36.71;44.47;45.00;40.22
  44;xunlei.com/xunlei.com/index.html;67.57;60.69;83.83;85.53;85.08
  45;aljazeera.net/aljazeera.net/portal.html;65.03;51.84;73.29;64.77;69.70
  46;w3.org/www.w3.org/standards/webdesign/htmlcss.html;53.57;58.50;72.98;66.95;55.62
  47;homeway.com.cn/www.hexun.com/index.html;105.59;117.32;108.95;116.10;70.32
  48;youtube.com/www.youtube.com/music.html;40.53;41.48;59.67;40.81;40.07
  49;people.com.cn/people.com.cn/index.html;96.49;103.64;115.12;66.05;117.84
  ```

* extensions: ['${talos}/tests/tabswitch', '${talos}/pageloader']
* preferences: {'addon.test.tabswitch.urlfile': '${talos}/tests/tp5o.html', 'addon.test.tabswitch.webserver': '${webserver}', 'addon.test.tabswitch.maxurls': -1}
* timeout: 1200
* tploadnocache: True
* tpmanifest: ${talos}/tests/tabswitch/tabswitch.manifest
* tppagecycles: 5
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-tabswitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-swr**
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
* - **talos-tabswitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-swr**
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
* - **talos-tabswitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-swr**
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
* - **talos-tabswitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-swr**
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
* - **talos-tabswitch**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tabswitch-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tart)=

::::{dropdown} tart
:class-container: anchor-id-tart

* **Command to Run Locally**

  ```
  ./mach talos-test -a tart
  ```

* contact: :mconley, Firefox Desktop Front-end team, :gijs, :fqueze, and :dthayer
* source: [tart](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/tart)
* type: [Page load]
* measuring: Desktop Firefox UI animation speed and smoothness
* reporting: intervals in ms (lower is better)
* see below for details
* data: there are 30 reported subtests from TART which we load 25 times, resulting in 30 sets of 25 data points.
* summarization:
  * subtest: [ignore first] data point, then take the [median] of the remaining 24 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l305)
  * suite: [geometric mean] of the 30 subtest results.
* TART is the **Tab Animation Regression Test**.
* TART tests tab animation on these cases:
* Simple: single new tab of about:blank open/close without affecting (shrinking/expanding) other tabs.
* icon: same as above with favicons and long title instead of about:blank.
* Newtab: newtab open with thumbnails preview
* without affecting other tabs, with and without preload.
* Fade: opens a tab, then measures fadeout/fadein (tab animation without the overhead of opening/closing a tab).
  * Case 1 is tested with DPI scaling of 1.
  * Case 2 is tested with DPI scaling of 1.0 and 2.0.
  * Case 3 is tested with the default scaling of the test system.
  * Case 4 is tested with DPI scaling of 2.0 with the "icon" tab (favicon and long title).
* Each animation produces 3 test results:
  * error: difference between the designated duration and the actual completion duration from the trigger.
  * half: average frame interval over the 2nd half of the animation.
  * all: average frame interval over all recorded intervals.
  * And the run logs also include the explicit intervals from which these 3 values were derived.
* **Example Data**

  ```
  0;simple-open-DPI1.half.TART;2.35;2.42;2.63;2.47;2.71;2.38;2.37;2.41;2.48;2.70;2.44;2.41;2.51;2.43;2.41;2.56;2.76;2.49;2.36;2.40;2.70;2.53;2.35;2.46;2.47
  1;simple-open-DPI1.all.TART;2.80;2.95;3.12;2.92;3.46;2.87;2.92;2.99;2.89;3.24;2.94;2.95;3.25;2.92;3.02;3.00;3.21;3.31;2.84;2.87;3.10;3.13;3.10;2.94;2.95
  2;simple-open-DPI1.error.TART;33.60;36.33;35.93;35.97;38.17;34.77;36.00;35.01;36.25;36.22;35.24;35.76;36.64;36.31;34.74;34.40;34.34;41.48;35.04;34.83;34.27;34.04;34.37;35.22;36.52
  3;simple-close-DPI1.half.TART;1.95;1.88;1.91;1.94;2.00;1.97;1.88;1.76;1.84;2.18;1.99;1.83;2.04;1.93;1.81;1.77;1.79;1.90;1.82;1.84;1.78;1.75;1.76;1.89;1.81
  4;simple-close-DPI1.all.TART;2.19;2.08;2.07;2.32;2.65;2.32;2.26;1.96;2.02;2.26;2.05;2.16;2.19;2.11;2.04;1.98;2.05;2.02;2.01;2.11;1.97;1.97;2.05;2.01;2.12
  5;simple-close-DPI1.error.TART;21.35;23.87;22.60;22.02;22.97;22.35;22.15;22.79;21.81;21.90;22.26;22.58;23.15;22.43;22.76;23.36;21.86;22.70;22.96;22.70;22.28;22.03;21.78;22.33;22.23
  6;icon-open-DPI1.half.TART;2.42;2.33;2.50;2.58;2.36;2.51;2.60;2.35;2.52;2.51;2.59;2.34;3.29;2.63;2.46;2.57;2.53;2.50;2.39;2.51;2.44;2.66;2.72;2.36;2.52
  7;icon-open-DPI1.all.TART;3.12;2.94;3.42;3.23;3.10;3.21;3.33;3.14;3.24;3.32;3.46;2.90;3.65;3.19;3.27;3.47;3.32;3.13;2.95;3.23;3.21;3.33;3.47;3.15;3.32
  8;icon-open-DPI1.error.TART;38.39;37.96;37.03;38.85;37.03;37.17;37.19;37.56;36.67;36.33;36.89;36.85;37.54;38.46;35.38;37.52;36.68;36.48;36.03;35.71;37.12;37.08;37.74;38.09;35.85
  9;icon-close-DPI1.half.TART;1.94;1.93;1.79;1.89;1.83;1.83;1.90;1.89;1.75;1.76;1.77;1.74;1.81;1.86;1.95;1.99;1.73;1.83;1.97;1.80;1.94;1.84;2.01;1.88;2.03
  10;icon-close-DPI1.all.TART;2.14;2.14;1.98;2.03;2.02;2.25;2.29;2.13;1.97;2.01;1.94;2.01;1.99;2.05;2.11;2.09;2.02;2.02;2.12;2.02;2.20;2.11;2.19;2.07;2.27
  11;icon-close-DPI1.error.TART;24.51;25.03;25.17;24.54;23.86;23.70;24.02;23.61;24.10;24.53;23.92;23.75;23.73;23.78;23.42;25.40;24.21;24.55;23.96;24.96;24.41;24.96;24.16;24.20;23.65
  12;icon-open-DPI2.half.TART;2.60;2.60;2.60;2.53;2.51;2.53;2.59;2.43;2.66;2.60;2.47;2.61;2.64;2.43;2.49;2.63;2.61;2.60;2.52;3.06;2.65;2.74;2.69;2.44;2.43
  13;icon-open-DPI2.all.TART;3.45;3.22;3.38;3.47;3.10;3.31;3.47;3.13;3.37;3.14;3.28;3.20;3.40;3.15;3.14;3.23;3.41;3.16;3.26;3.55;3.29;3.49;3.44;3.11;3.22
  14;icon-open-DPI2.error.TART;40.20;37.86;37.53;41.46;37.03;38.77;37.48;36.97;37.28;37.72;36.09;36.71;38.89;38.21;37.37;38.91;36.79;36.10;37.60;36.99;37.56;35.76;38.92;37.46;37.52
  15;icon-close-DPI2.half.TART;2.27;1.97;1.83;1.86;2.08;1.88;1.80;1.90;1.77;1.89;2.06;1.89;1.89;1.86;2.01;1.79;1.78;1.83;1.89;1.85;1.74;1.82;1.84;1.81;1.79
  16;icon-close-DPI2.all.TART;2.33;2.13;2.18;2.03;2.33;2.03;1.95;2.06;1.96;2.13;2.25;2.10;2.13;2.03;2.18;2.00;2.05;2.01;2.08;2.05;1.96;2.04;2.10;2.04;2.08
  17;icon-close-DPI2.error.TART;22.99;23.02;24.32;23.58;23.05;23.34;22.92;23.22;22.90;23.33;23.33;23.05;22.80;23.45;24.05;22.39;22.14;22.97;22.85;22.13;22.89;22.98;23.69;22.99;23.08
  18;iconFade-close-DPI2.half.TART;2.14;1.84;1.78;1.84;1.66;2.07;1.81;3.82;1.68;1.85;1.62;2.54;2.06;1.85;2.17;1.80;1.71;1.67;2.11;1.73;2.94;2.14;1.93;1.72;2.05
  19;iconFade-close-DPI2.all.TART;2.17;1.76;1.80;1.89;1.70;1.93;1.80;3.38;1.78;1.90;1.70;2.50;1.94;1.81;2.29;1.82;1.79;1.76;2.23;1.80;2.85;2.06;1.84;1.83;2.09
  20;iconFade-close-DPI2.error.TART;4.67;4.11;3.69;4.51;4.46;3.88;4.54;3.68;4.56;3.82;4.32;4.87;4.42;3.72;3.72;4.54;4.93;4.46;4.64;3.39;4.09;3.28;3.58;4.11;3.80
  21;iconFade-open-DPI2.half.TART;2.37;2.61;2.37;2.62;2.54;2.84;2.57;2.44;4.33;2.57;2.59;2.67;2.58;2.48;2.38;2.39;2.50;2.39;2.50;2.57;2.52;2.55;2.47;2.69;2.41
  22;iconFade-open-DPI2.all.TART;2.45;2.64;2.39;2.60;2.57;2.60;2.61;2.59;3.14;2.55;2.54;2.66;2.57;2.48;2.47;2.46;2.55;2.45;2.51;2.61;2.54;2.58;2.50;2.54;2.40
  23;iconFade-open-DPI2.error.TART;3.64;4.67;4.31;5.79;6.43;3.64;4.82;8.68;5.78;4.38;3.80;3.98;4.64;653.63;4.63;3.76;4.23;5.01;5.48;4.99;3.48;5.10;5.02;6.14;5.58
  24;newtab-open-preload-no.half.TART;5.02;2.90;3.06;3.03;2.94;2.94;3.08;3.12;3.60;3.19;2.82;2.96;3.67;7.85;2.79;3.12;3.18;2.92;2.86;2.96;2.96;3.00;2.90;2.97;2.94
  25;newtab-open-preload-no.all.TART;7.11;4.66;5.03;4.68;4.50;4.58;4.76;4.76;5.67;4.96;4.36;4.51;5.21;11.16;4.38;4.69;4.77;4.45;4.45;4.70;4.51;4.61;4.54;4.69;4.60
  26;newtab-open-preload-no.error.TART;40.82;40.85;37.38;37.40;36.30;36.47;36.89;37.63;37.12;38.65;36.73;36.95;36.11;38.59;37.39;37.77;37.93;37.54;37.46;38.29;36.58;38.25;38.32;37.92;36.93
  27;newtab-open-preload-yes.half.TART;3.14;2.96;2.97;8.37;2.98;3.00;2.96;3.05;3.12;3.48;3.07;3.23;3.05;2.88;2.92;3.06;2.90;3.01;3.19;2.90;3.18;3.11;3.04;3.16;3.21
  28;newtab-open-preload-yes.all.TART;5.10;4.60;4.63;8.94;5.01;4.69;4.63;4.67;4.93;5.43;4.78;5.12;4.77;4.65;4.50;4.78;4.75;4.63;4.76;4.45;4.86;4.88;4.69;4.86;4.92
  29;newtab-open-preload-yes.error.TART;35.90;37.24;38.57;40.60;36.04;38.12;38.78;36.73;36.91;36.69;38.12;36.69;37.79;35.80;36.11;38.01;36.59;38.85;37.14;37.30;38.02;38.95;37.64;37.86;36.43
  ```

* extensions: ['${talos}/pageloader', '${talos}/tests/tart/addon']
* gecko_profile_interval: 10
* linux_counters: None
* mac_counters: None
* pine: False
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* tpcycles: 1
* tploadnocache: True
* tpmanifest: ${talos}/tests/tart/tart.manifest
* tpmozafterpaint: False
* tppagecycles: 25
* unit: ms
* win_counters: None
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tp5)=

::::{dropdown} tp5
:class-container: anchor-id-tp5

* **Command to Run Locally**

  ```
  ./mach talos-test -a tp5
  ```

* description:
      | Note that the tp5 test no longer exists (only talos-tp5o) though many
        tests still make use of this pageset. Here, we provide an overview of
        the tp5 pageset and some information about how data using the tp5
        pageset might be used in various suites.
::::

(tp5n)=

::::{dropdown} tp5n
:class-container: anchor-id-tp5n

* **Command to Run Locally**

  ```
  ./mach talos-test -a tp5n
  ```

* contact: perftest team
* description:
      | The tp5 is an updated web page test set to 100 pages from April 8th, 2011. Effort was made for the pages to no longer be splash screens/login pages/home pages but to be pages that better reflect the actual content of the site in question.
* cleanup: ${talos}/xtalos/parse_xperf.py -c ${talos}/bcontroller.json
* cycles: 1
* linux_counters: []
* mac_counters: []
* mainthread: True
* multidomain: True
* preferences: {'extensions.enabledScopes': 5, 'talos.logfile': 'browser_output.txt'}
* resolution: 20
* setup: ${talos}/xtalos/start_xperf.py -c ${talos}/bcontroller.json
* timeout: 1800
* tpcycles: 1
* tpmanifest: ${talos}/fis/tp5n/tp5n.manifest
* tpmozafterpaint: True
* tppagecycles: 1
* tptimeout: 10000
* unit: ms
* win_counters: []
* xperf_counters: ['main_startup_fileio', 'main_startup_netio', 'main_normal_fileio', 'main_normal_netio', 'nonmain_startup_fileio', 'nonmain_normal_fileio', 'nonmain_normal_netio', 'mainthread_readcount', 'mainthread_readbytes', 'mainthread_writecount', 'mainthread_writebytes', 'time_to_session_store_window_restored_ms']
* xperf_providers: ['PROC_THREAD', 'LOADER', 'HARD_FAULTS', 'FILENAME', 'FILE_IO', 'FILE_IO_INIT']
* xperf_stackwalk: ['FileCreate', 'FileRead', 'FileWrite', 'FileFlush', 'FileClose']
* xperf_user_providers: ['Mozilla Generic Provider', 'Microsoft-Windows-TCPIP']
* **Test Task**:

:::{list-table} **{ref}`test-windows11-64-25h2-shippable/opt <hardware-azure-standard-f8alds-v7>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-xperf**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-xperf-swr**
  - ✅
  - ✅
  - ❌
  - ❌
:::


:::{list-table} **{ref}`test-windows11-64-25h2/opt <hardware-azure-standard-f8alds-v7>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-xperf**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-xperf-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tp5o)=

::::{dropdown} tp5o
:class-container: anchor-id-tp5o

* **Command to Run Locally**

  ```
  ./mach talos-test -a tp5o
  ```

* contact: :davehunt, and perftest team
* source: [tp5n.zip](#page-sets)
* type: [Page load]
* data: we load each of the 51 tp5o pages 25 times, resulting in 51 sets of 25 data points.
* **To run it locally**, you'd need [tp5n.zip](#page-sets).
* summarization: tp5 with limited pageset (48 pages as others have too much noise)
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 20; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l449)
  * suite: [geometric mean] of the 51 subtest results.
* description:
      | Tests the time it takes Firefox to load the [tp5 web page test
        set](#page-sets). The web set was culled from the Alexa top 500 April
        8th, 2011 and consists of 100 pages in tp5n and 51 in tp5o. Some suites
        use a subset of these, i.e. 48/51 tests to reduce noise
* check with the
        owner of the test suite which uses the pageset to check if this
        difference exists there.

        Here are the broad steps we use to create the test set:

        #. Take the Alexa top 500 sites list
        #. Remove all sites with questionable or explicit content
        #. Remove duplicate site (for ex. many Google search front pages)
        #. Manually select to keep interesting pages (such as pages in different locales)
        #. Select a more representative page from any site presenting a simple search/login/etc. page
        #. Deal with Windows 255 char limit for cached pages
        #. Limit test set to top 100 pages

        Note that the above steps did not eliminate all outside network access
        so we had to take further action to scrub all the pages so that there
        are 0 outside network accesses (this is done so that the tp test is as
        deterministic measurement of our rendering/layout/paint process as
        possible).
* **Example Data**

  ```
  0;163.com/www.163.com/index.html;1035;512;542;519;505;514;551;513;554;793;487;528;528;498;503;530;527;490;521;535;521;496;498;564;520
  1;56.com/www.56.com/index.html;1081;583;580;577;597;580;623;558;572;592;598;580;564;583;596;600;579;580;566;573;566;581;571;600;586
  2;aljazeera.net/aljazeera.net/portal.html;688;347;401;382;346;362;347;372;337;345;365;337;380;338;355;360;356;366;367;352;350;366;346;375;382
  3;amazon.com/www.amazon.com/Kindle-Wireless-Reader-Wifi-Graphite/dp/B002Y27P3M/507846.html;1392;878;901;852;886;867;877;864;862;877;866;888;3308;870;863;869;873;850;851;850;857;873;869;860;855
  4;bbc.co.uk/www.bbc.co.uk/news/index.html;455;271;272;271;279;289;276;285;277;291;281;286;278;286;274;285;276;285;287;286;276;288;279;272;278
  5;beatonna.livejournal.com/beatonna.livejournal.com/index.html;290;123;123;129;120;121;124;125;119;125;120;150;121;147;121;121;113;121;119;122;117;112;127;117;139
  6;bild.de/www.bild.de/index.html;1314;937;946;931;922;918;920;937;934;930;947;928;936;933;933;928;930;941;951;946;947;938;925;939;938
  7;cgi.ebay.com/cgi.ebay.com/ALL-NEW-KINDLE-3-eBOOK-WIRELESS-READING-DEVICE-W-WIFI-/130496077314@pt=LH_DefaultDomain_0&hash=item1e622c1e02.html;495;324;330;328;321;308;315;308;321;313;327;330;317;339;333;322;312;370;336;327;310;312;312;355;330
  8;chemistry.about.com/chemistry.about.com/index.html;238;156;156;154;158;161;152;151;152;167;179;152;154;156;161;161;157;167;151;167;154;149;178;153;160
  9;chinaz.com/chinaz.com/index.html;347;223;255;234;245;233;264;234;244;228;260;224;258;223;280;220;243;225;251;226;258;232;258;232;247
  10;cnn.com/www.cnn.com/index.html;551;384;436;394;391;375;371;407;371;374;398;372;368;388;376;380;386;377;363;383;384;370;388;381;374
  11;dailymail.co.uk/www.dailymail.co.uk/ushome/index.html;984;606;551;561;545;542;576;564;543;560;566;557;561;544;545;576;548;539;568;567;557;560;545;544;578
  12;dailymotion.com/www.dailymotion.com/us.html;473;271;286;272;285;288;290;290;280;268;286;269;287;275;289;282;293;287;304;261;289;284;281;277;286
  13;digg.com/digg.com/news/story/New_logo_for_Mozilla_Firefox_browser.html;410;321;304;303;322;300;319;321;320;306;323;313;312;305;312;338;317;338;301;325;297;302;309;305;300
  14;ezinearticles.com/ezinearticles.com/index.html@Migraine-Ocular---The-Eye-Migraines&id=4684133.html;234;177;163;163;186;176;185;175;167;156;162;199;163;190;173;181;175;178;165;159;182;170;183;169;158
  15;globo.com/www.globo.com/index.html;684;468;466;485;482;445;433;467;467;450;487;466;440;484;444;451;511;443;429;469;468;430;485;459;447
  16;google.com/www.google.com/search@q=mozilla.html;150;100;102;101;97;104;99;116;107;100;98;137;102;102;99;106;98;112;100;102;105;104;107;96;100
  17;goo.ne.jp/goo.ne.jp/index.html;328;125;132;132;143;121;122;120;132;145;166;139;144;125;128;152;128;145;130;132;154;126;142;133;139
  18;guardian.co.uk/www.guardian.co.uk/index.html;462;311;296;322;309;305;303;288;301;308;301;304;319;309;295;305;294;308;304;322;310;314;302;303;292
  19;homeway.com.cn/www.hexun.com/index.html;584;456;396;357;417;374;376;406;363;392;400;378;378;402;390;373;398;393;366;385;383;361;418;386;351
  20;huffingtonpost.com/www.huffingtonpost.com/index.html;811;609;575;596;568;585;589;571;568;600;571;588;585;570;574;616;576;564;598;594;589;590;572;572;612
  21;ifeng.com/ifeng.com/index.html;829;438;478;462;448;465;469;470;429;463;465;432;482;444;476;453;460;476;461;484;467;510;447;477;443
  22;imdb.com/www.imdb.com/title/tt1099212/index.html;476;337;358;332;414;379;344;420;354;363;387;345;358;371;341;385;359;379;353;349;392;349;358;378;347
  23;imgur.com/imgur.com/gallery/index.html;419;205;224;231;207;222;206;231;204;219;209;210;210;208;202;215;203;210;203;212;218;219;202;224;230
  24;indiatimes.com/www.indiatimes.com/index.html;530;339;348;384;376;381;353;350;403;333;356;393;350;328;393;329;389;346;394;349;382;332;409;327;354
  25;mail.ru/mail.ru/index.html;500;256;308;251;271;270;270;246;273;252;279;249;301;252;251;256;271;246;267;254;265;248;277;247;275
  26;mashable.com/mashable.com/index.html;699;497;439;508;440;448;512;446;431;500;445;427;495;455;458;499;440;432;522;443;447;488;445;461;489
  27;media.photobucket.com/media.photobucket.com/image/funny%20gif/findstuff22/Best%20Images/Funny/funny-gif1.jpg@o=1.html;294;203;195;189;213;186;195;186;204;188;190;220;204;202;195;204;192;204;191;187;204;199;191;192;211
  28;myspace.com/www.myspace.com/albumart.html;595;446;455;420;433;425;416;429;452;411;435;439;389;434;418;402;422;426;396;438;423;391;434;438;395
  29;naver.com/www.naver.com/index.html;626;368;338;386;342;377;371;352;379;348;362;357;359;354;386;338;394;330;326;372;345;392;336;336;368
  30;noimpactman.typepad.com/noimpactman.typepad.com/index.html;431;333;288;292;285;313;277;289;282;292;276;293;270;294;289;281;275;302;285;290;280;285;278;284;283
  31;page.renren.com/page.renren.com/index.html;373;232;228;228;213;227;224;227;226;216;234;226;230;212;213;221;224;230;212;218;217;221;212;222;230
  32;people.com.cn/people.com.cn/index.html;579;318;305;339;307;341;325;326;307;309;315;314;318;317;321;309;307;299;312;313;305;326;318;384;310
  33;rakuten.co.jp/www.rakuten.co.jp/index.html;717;385;371;388;381;348;394;358;396;368;343;386;348;388;393;388;360;339;398;357;392;378;395;386;367
  34;reddit.com/www.reddit.com/index.html;340;254;248;255;241;241;248;275;251;250;250;252;243;274;240;269;254;249;242;257;271;253;243;278;252
  35;reuters.com/www.reuters.com/index.html;513;404;355;358;379;343;354;385;379;354;418;363;342;412;355;351;402;375;354;400;362;355;380;373;336
  36;slideshare.net/www.slideshare.net/jameswillamor/lolcats-in-popular-culture-a-historical-perspective.html;397;279;270;283;285;283;291;286;289;284;275;281;288;284;280;279;290;301;290;270;292;282;289;267;278
  37;sohu.com/www.sohu.com/index.html;727;414;479;414;431;485;418;440;488;431;432;464;442;407;488;435;416;465;445;414;480;416;403;463;429
  38;spiegel.de/www.spiegel.de/index.html;543;430;391;380;440;387;375;430;380;397;415;383;434;420;384;399;421;392;384;418;388;380;427;403;392
  39;stackoverflow.com/stackoverflow.com/questions/184618/what-is-the-best-comment-in-source-code-you-have-ever-encountered.html;503;377;356;438;370;388;409;367;366;407;375;363;393;360;363;396;376;391;426;363;378;408;400;359;408
  40;store.apple.com/store.apple.com/us@mco=Nzc1MjMwNA.html;488;327;344;343;333;329;328;348;361;342;362;332;389;333;382;331;382;343;405;343;326;325;329;323;340
  41;thepiratebay.org/thepiratebay.org/top/201.html;412;274;317;260;256;269;266;261;258;289;245;284;256;277;251;302;276;307;268;268;247;285;260;271;257
  42;tudou.com/www.tudou.com/index.html;522;304;281;283;287;285;288;307;279;288;282;303;292;288;290;287;311;271;279;274;294;272;290;269;290
  43;uol.com.br/www.uol.com.br/index.html;668;387;450;411;395;452;386;431;452;394;385;436;413;414;440;401;412;439;408;430;426;415;382;433;387
  44;w3.org/www.w3.org/standards/webdesign/htmlcss.html;225;143;129;151;181;141;147;137;159;179;142;153;136;139;191;140;151;143;141;181;140;154;142;143;183
  45;wsj.com/online.wsj.com/home-page.html;634;466;512;463;467;507;461;432;492;494;491;507;466;477;495;455;451;495;461;463;494;468;444;497;442
  46;xinhuanet.com/xinhuanet.com/index.html;991;663;727;659;647;639;644;656;666;658;670;648;676;653;652;654;641;636;664;668;655;657;646;674;633
  47;xunlei.com/xunlei.com/index.html;802;625;624;636;641;652;659;642;623;635;628;606;667;688;683;694;672;640;628;620;653;626;633;654;643
  48;yelp.com/www.yelp.com/biz/alexanders-steakhouse-cupertino.html;752;475;502;472;477;512;489;478;501;472;454;517;487;474;521;467;450;513;491;464;536;507;455;511;481
  49;youku.com/www.youku.com/index.html;844;448;498;441;417;497;478;439;467;436;447;465;438;461;466;446;452;496;457;446;486;449;467;499;442
  50;youtube.com/www.youtube.com/music.html;443;338;253;289;238;296;254;290;242;302;237;290;253;305;253;293;251;311;244;293;255;291;246;316;249
  ```

* cycles: 1
* gecko_profile_interval: 2
* linux_counters: ['XRes']
* mac_counters: []
* mainthread: False
* multidomain: True
* timeout: 1800
* tpcycles: 1
* tpmanifest: ${talos}/fis/tp5n/tp5o.manifest
* tpmozafterpaint: True
* tppagecycles: 25
* tptimeout: 5000
* unit: ms
* win_counters: ['% Processor Time']
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-tp5o**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-tp5o-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tp5o-swr**
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
* - **talos-tp5o**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tp5o-swr**
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
* - **talos-tp5o**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-tp5o-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tp5o-swr**
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
* - **talos-tp5o**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-tp5o-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tp5o-swr**
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
* - **talos-tp5o**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-tp5o-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tp5o_scroll)=

::::{dropdown} tp5o_scroll
:class-container: anchor-id-tp5o_scroll

* **Command to Run Locally**

  ```
  ./mach talos-test -a tp5o_scroll
  ```

* contact: :botond, :tnikkel, :hiro, and layout team
* source: [tp5n.zip](#page-sets)
* type: [Page load]
* data: we load each of the 51 tp5o pages 12 times, resulting in 51 sets of 12 data points.
* **To run it locally**, you'd need [tp5n.zip](#page-sets).
* summarization: Measures average frames interval while scrolling the pages of the tp5o set
  * subtest: [ignore first] data point, then take the [median] of the remaining 11; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l470)
  * suite: [geometric mean] of the 51 subtest results.
* description:
      | This test is identical to tscrollx, but it scrolls the 50 pages of the
        tp5o set (rather than 6 synthetic pages which tscrollx scrolls). There
        are two variants for each test page. The "regular" variant waits 500ms
        after the page load event fires, then iterates 100 scroll steps of 10px
        each (or until the bottom of the page is reached
* whichever comes
        first), then reports the average frame interval. The "CSSOM" variant is
        similar, but uses APZ's smooth scrolling mechanism to do compositor
        scrolling instead of main-thread scrolling. So it just requests the
        final scroll destination and the compositor handles the scrolling and
        reports frame intervals.
* **Example Data**

  ```
  0;163.com/www.163.com/index.html;9.73;8.61;7.37;8.17;7.58;7.29;6.88;7.45;6.91;6.61;8.47;7.12
  1;56.com/www.56.com/index.html;10.85;10.24;10.75;10.30;10.23;10.10;10.31;10.06;11.10;10.06;9.56;10.30
  2;aljazeera.net/aljazeera.net/portal.html;9.23;7.15;7.50;7.26;7.73;7.05;7.14;7.66;7.23;7.93;7.26;7.18
  3;amazon.com/www.amazon.com/Kindle-Wireless-Reader-Wifi-Graphite/dp/B002Y27P3M/507846.html;7.14;6.87;7.18;6.31;6.93;6.71;6.37;7.00;6.59;5.37;7.31;6.13
  4;bbc.co.uk/www.bbc.co.uk/news/index.html;7.39;6.33;6.22;7.66;6.67;7.77;6.91;7.74;7.08;6.36;6.03;7.12
  5;beatonna.livejournal.com/beatonna.livejournal.com/index.html;5.79;5.79;5.68;5.46;5.55;5.48;5.69;5.83;5.88;5.97;5.93;5.88
  6;bild.de/www.bild.de/index.html;8.65;7.63;7.17;6.36;7.44;7.68;8.63;6.71;8.34;7.15;7.82;7.70
  7;cgi.ebay.com/cgi.ebay.com/ALL-NEW-KINDLE-3-eBOOK-WIRELESS-READING-DEVICE-W-WIFI-/130496077314@pt=LH_DefaultDomain_0&hash=item1e622c1e02.html;7.12;6.81;7.22;6.98;7.05;5.68;7.15;6.54;7.31;7.18;7.82;7.77
  8;chemistry.about.com/chemistry.about.com/index.html;6.76;6.17;6.41;6.88;5.67;5.47;6.83;6.28;6.16;6.81;6.21;6.75
  9;chinaz.com/chinaz.com/index.html;10.72;7.99;7.33;7.10;7.85;8.62;8.39;6.72;6.26;6.65;8.14;7.78
  10;cnn.com/www.cnn.com/index.html;7.73;6.80;6.08;8.27;9.24;7.81;7.69;7.05;8.17;7.70;7.90;6.81
  11;dailymail.co.uk/www.dailymail.co.uk/ushome/index.html;6.37;8.28;7.19;8.00;8.09;7.43;6.90;7.24;7.77;7.29;7.38;6.14
  12;dailymotion.com/www.dailymotion.com/us.html;9.53;9.80;9.29;9.03;9.10;8.64;8.62;8.71;8.77;9.81;9.64;8.96
  13;digg.com/digg.com/news/story/New_logo_for_Mozilla_Firefox_browser.html;7.72;7.06;7.60;5.67;6.85;7.32;7.80;5.98;8.27;6.68;7.52;8.39
  14;ezinearticles.com/ezinearticles.com/index.html@Migraine-Ocular---The-Eye-Migraines&id=4684133.html;7.14;7.11;8.09;7.17;6.87;7.12;7.65;7.74;7.26;7.36;6.91;6.95
  15;globo.com/www.globo.com/index.html;6.71;7.91;5.83;7.34;7.75;8.00;7.73;7.85;7.03;6.42;8.43;8.11
  16;google.com/www.google.com/search@q=mozilla.html;6.49;6.23;7.96;6.39;7.23;8.19;7.35;7.39;6.94;7.24;7.55;7.62
  17;goo.ne.jp/goo.ne.jp/index.html;8.56;7.18;7.15;7.03;6.85;7.62;7.66;6.99;7.84;7.51;7.23;7.18
  18;guardian.co.uk/www.guardian.co.uk/index.html;7.32;7.62;8.18;7.62;7.83;8.08;7.60;8.17;8.47;7.54;7.92;8.09
  19;homeway.com.cn/www.hexun.com/index.html;10.18;8.75;8.83;8.64;8.98;8.07;7.76;9.29;8.05;7.55;8.91;7.78
  20;huffingtonpost.com/www.huffingtonpost.com/index.html;8.38;7.17;7.03;6.83;6.49;6.47;6.69;7.08;6.81;7.29;7.13;7.70
  21;ifeng.com/ifeng.com/index.html;12.45;8.65;8.75;7.56;8.26;7.71;8.04;7.45;7.83;7.14;8.38;7.68
  22;imdb.com/www.imdb.com/title/tt1099212/index.html;8.53;5.65;6.94;7.18;6.10;7.57;6.26;8.34;8.16;7.29;7.62;8.51
  23;imgur.com/imgur.com/gallery/index.html;8.10;7.20;7.50;7.88;7.27;6.97;8.13;7.14;7.59;7.39;8.01;8.82
  24;indiatimes.com/www.indiatimes.com/index.html;8.00;6.74;7.37;8.52;7.03;8.45;7.08;8.47;9.26;7.89;7.17;6.74
  25;mail.ru/mail.ru/index.html;7.64;9.50;9.47;7.03;6.45;6.24;8.03;6.72;7.18;6.39;6.25;6.25
  26;mashable.com/mashable.com/index.html;7.97;8.03;6.10;7.80;7.91;7.26;7.49;7.45;7.60;7.08;7.63;7.36
  27;media.photobucket.com/media.photobucket.com/image/funny%20gif/findstuff22/Best%20Images/Funny/funny-gif1.jpg@o=1.html;290.00;195.00;217.00;199.00;204.00;196.00;198.00;206.00;209.00;208.00;192.00;196.00
  28;myspace.com/www.myspace.com/albumart.html;14.40;13.45;13.29;13.62;13.42;14.15;13.86;14.34;14.69;14.10;13.82;14.13
  29;naver.com/www.naver.com/index.html;9.15;8.31;9.40;9.89;7.29;8.43;8.87;8.77;8.96;8.24;8.16;8.21
  30;noimpactman.typepad.com/noimpactman.typepad.com/index.html;7.27;7.14;7.70;7.86;7.43;6.95;7.30;7.58;7.51;7.95;7.43;7.05
  31;page.renren.com/page.renren.com/index.html;7.94;8.13;6.76;7.77;6.93;6.60;7.62;7.61;6.88;7.56;7.55;7.48
  32;people.com.cn/people.com.cn/index.html;11.92;9.22;8.49;8.55;8.34;8.49;6.91;9.92;8.69;8.63;7.69;9.34
  33;rakuten.co.jp/www.rakuten.co.jp/index.html;11.10;7.13;8.68;7.85;8.37;7.91;6.74;8.27;8.55;8.93;7.15;9.02
  34;reddit.com/www.reddit.com/index.html;6.38;7.95;6.84;7.04;6.96;7.15;8.05;7.71;8.13;7.13;6.60;7.53
  35;reuters.com/www.reuters.com/index.html;7.51;7.25;6.60;6.98;7.41;6.45;7.61;7.46;6.11;7.15;7.05;6.94
  36;slideshare.net/www.slideshare.net/jameswillamor/lolcats-in-popular-culture-a-historical-perspective.html;7.20;6.32;6.80;6.87;6.29;6.45;7.18;6.92;6.57;7.41;7.08;6.51
  37;sohu.com/www.sohu.com/index.html;11.72;9.64;8.85;7.12;7.96;9.14;7.76;8.19;7.14;7.68;8.08;7.24
  38;spiegel.de/www.spiegel.de/index.html;7.24;7.30;6.64;7.01;6.74;6.70;6.36;6.84;7.86;7.08;7.12;7.40
  39;stackoverflow.com/stackoverflow.com/questions/184618/what-is-the-best-comment-in-source-code-you-have-ever-encountered.html;7.39;5.88;7.22;6.51;7.12;6.51;6.46;6.53;6.63;6.54;6.48;6.80
  40;store.apple.com/store.apple.com/us@mco=Nzc1MjMwNA.html;6.23;7.17;7.39;8.98;7.99;8.03;9.12;8.37;8.56;7.61;8.06;7.55
  41;thepiratebay.org/thepiratebay.org/top/201.html;9.08;8.93;8.09;7.49;7.30;7.80;7.54;7.65;7.91;7.53;8.37;8.04
  42;tudou.com/www.tudou.com/index.html;10.06;9.38;8.68;7.37;8.57;9.11;8.20;7.91;8.78;9.64;8.11;8.47
  43;uol.com.br/www.uol.com.br/index.html;9.04;9.49;9.48;9.31;8.68;8.41;9.16;8.91;9.49;8.37;9.77;8.73
  44;w3.org/www.w3.org/standards/webdesign/htmlcss.html;6.62;5.98;6.87;6.47;7.22;6.05;6.42;6.50;7.47;7.18;5.82;7.11
  45;wsj.com/online.wsj.com/home-page.html;7.49;8.57;6.84;8.12;7.60;7.24;8.16;8.22;6.81;8.28;8.11;8.58
  46;xinhuanet.com/xinhuanet.com/index.html;13.66;9.21;10.09;9.56;8.99;10.29;10.24;8.91;11.23;10.82;9.64;10.11
  47;xunlei.com/xunlei.com/index.html;8.99;8.16;8.82;8.37;7.01;8.48;7.98;8.69;8.10;8.10;7.10;6.41
  48;yelp.com/www.yelp.com/biz/alexanders-steakhouse-cupertino.html;8.18;7.45;7.01;8.14;7.12;7.82;8.24;7.13;7.00;6.41;6.85;7.31
  49;youku.com/www.youku.com/index.html;12.21;10.29;10.37;10.34;8.40;9.82;9.23;9.91;9.64;9.91;8.90;10.23
  50;youtube.com/www.youtube.com/music.html;9.90;9.06;9.29;9.17;8.85;8.77;9.83;9.21;9.29;10.09;9.69;8.64
  ```

* **Possible regression causes**: Some examples of things that cause regressions in this test are
  * Increased displayport size (which causes a larger display list to be built)
  * Slowdown in the building of display list
  * Slowdown in rasterization of content
  * Slowdown in composite times
* gecko_profile_interval: 2
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': True, 'apz.paint_skipping.enabled': False, 'layout.css.scroll-snap.spring-constant': "'10'", 'layout.css.scroll-behavior.same-physics-as-user-input': False, 'toolkit.framesRecording.bufferSize': 10000}
* tpcycles: 1
* tpmanifest: ${talos}/tests/tp5n/tp5o.manifest
* tpmozafterpaint: False
* tppagecycles: 12
* tpscrolltest: True
* unit: 1/FPS
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g1-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g1-swr**
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
* - **talos-g1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g1-swr**
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
* - **talos-g1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g1-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g1-swr**
  - ✅
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
* - **talos-g1**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g1-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g1-swr**
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
* - **talos-g1**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g1-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tp5o_webext)=

::::{dropdown} tp5o_webext
:class-container: anchor-id-tp5o_webext

* **Command to Run Locally**

  ```
  ./mach talos-test -a tp5o_webext
  ```

* contact: :mixedpuppy and webextension team
* preferences: {'xpinstall.signatures.required': False}
* webextensions: ${talos}/webextensions/dummy/dummy.xpi
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tresize)=

::::{dropdown} tresize
:class-container: anchor-id-tresize

* **Command to Run Locally**

  ```
  ./mach talos-test -a tresize
  ```

* contact: :gcp and platform integration
* source: [tresize-test.html](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/startup_test/tresize/addon/content/tresize-test.html)
* type: Startup_
* measuring: Time to do XUL resize, in ms (lower is better).
* data: we run the tresize test page 20 times, resulting in 1 set of 20 data points.
* summarization:
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 15 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l205)
  * suite: same as subtest result
* description:
      | A purer form of paint measurement than tpaint. This test opens a single
        window positioned at 10,10 and sized to 300,300, then resizes the window
        outward \|max\| times measuring the amount of time it takes to repaint
        each resize. Dumps the resulting dataset and average to stdout or
        logfile.

      In [bug
      1102479](https://bugzilla.mozilla.org/show_bug.cgi?id=1102479)
      tresize was rewritten to work in e10s mode which involved a full rewrite
      of the test.
* **Example Data**

  ```
  [23.2565333333333, 23.763383333333362, 22.58369999999999, 22.802766666666653, 22.304050000000025, 23.010383333333326, 22.865466666666677, 24.233716666666705, 24.110983333333365, 22.21390000000004, 23.910333333333316, 23.409816666666647, 19.873049999999992, 21.103966666666686, 20.389749999999978, 20.777349999999984, 20.326283333333365, 22.341616666666667, 20.29813333333336, 20.769600000000104]
  ```

* **Possible regression causes**
  * slowdown in the paint pipeline
  * resizes also trigger a rendering flush so bugs in the flushing code can manifest as regressions
  * introduction of more spurious MozAfterPaint events
* see [bug 1471961](https://bugzilla.mozilla.org/show_bug.cgi?id=1471961)
* extensions: ['${talos}/pageloader', '${talos}/tests/tresize/addon']
* gecko_profile_interval: 2
* timeout: 900
* tpmanifest: ${talos}/tests/tresize/tresize.manifest
* tpmozafterpaint: True
* tppagecycles: 20
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
  - ✅
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
* - **talos-chrome**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-chrome-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
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
* - **talos-chrome**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-chrome-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ts_paint)=

::::{dropdown} ts_paint
:class-container: anchor-id-ts_paint

* **Command to Run Locally**

  ```
  ./mach talos-test -a ts_paint
  ```

* contact: :mconley, Firefox Desktop Front-end team,
* source: [tspaint_test.html](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/startup_test/tspaint_test.html)
* Perfomatic: "Ts, Paint"
* type: Startup_
* data: 20 times we start the browser and time how long it takes to
     paint the startup test page, resulting in 1 set of 20 data points.
* summarization:
  * subtest: identical to suite
  * suite: [ignore first] data point, then take the [median] of the remaining 19 data points; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l135)
* description:
      | Starts the browser to display tspaint_test.html with the start time in
        the url, waits for [MozAfterPaint and onLoad](#paint) to fire, then
        records the end time and calculates the time to startup.
* **Example Data**

  ```
  [1666.0, 1195.0, 1139.0, 1198.0, 1248.0, 1224.0, 1213.0, 1194.0, 1229.0, 1196.0, 1191.0, 1230.0, 1247.0, 1169.0, 1217.0, 1184.0, 1196.0, 1192.0, 1224.0, 1192.0]
  ```

* **Possible regression causes**
  * (and/or maybe tpaint?) will regress if a new element is added to the
        browser window (e.g. browser.xul) and it's frame gets created. Fix
        this by ensuring it's display:none by default.
* cycles: 20
* gecko_profile_startup: True
* mainthread: False
* timeout: 150
* tpmozafterpaint: True
* unit: ms
* url: startup_test/tspaint_test.html
* win7_counters: []
* xperf_counters: []
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(ts_paint_heavy)=

::::{dropdown} ts_paint_heavy
:class-container: anchor-id-ts_paint_heavy

* **Command to Run Locally**

  ```
  ./mach talos-test -a ts_paint_heavy
  ```

* [ts_paint](#ts_paint) test run against a heavy user profile.
* contact: :mconley, Firefox Desktop Front-end team,
* profile: simple
::::

(ts_paint_webext)=

::::{dropdown} ts_paint_webext
:class-container: anchor-id-ts_paint_webext

* **Command to Run Locally**

  ```
  ./mach talos-test -a ts_paint_webext
  ```

* contact: :mconley, Firefox Desktop Front-end team,
* preferences: {'xpinstall.signatures.required': False}
* webextensions: ${talos}/webextensions/dummy/dummy.xpi
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-g5-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
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
* - **talos-g5**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-g5-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tscrollx)=

::::{dropdown} tscrollx
:class-container: anchor-id-tscrollx

* **Command to Run Locally**

  ```
  ./mach talos-test -a tscrollx
  ```

* contact: :jrmuizel and gfx
* source: [scroll.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/scroll)
* type: [Page load]
* measuring: Scroll performance
* reporting: Average frame interval (1 ÷ fps). Lower is better.
* data: we load 6 pages 25 times each, collecting 6 sets of 25 data points
* summarization: [Replacing tscroll,tsvg with tscrollx,tsvgx](https://groups.google.com/d/topic/mozilla.dev.platform/RICw5SJhNMo/discussion)
  * subtest: [ignore first] data point, then take the [median] of the remaining 24; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l623)
  * suite: [geometric mean] of the 6 subtest results.
* description:
      | This test scrolls several pages where each represent a different known
        "hard" case to scroll (\* needinfo), and measures the average frames
        interval (1/FPS) on each. The ASAP test (tscrollx) iterates in unlimited
        frame-rate mode thus reflecting the maximum scroll throughput per page.
        To turn on ASAP mode, we set these preferences:

      ``preferences = {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1}``

      See also [tp5o_scroll](#tp5o_scroll) which has relevant information for this test.
* **Example Data**

  ```
  0;tiled.html;5.41;5.57;5.34;5.64;5.53;5.48;5.44;5.49;5.50;5.50;5.49;5.66;5.50;5.37;5.57;5.54;5.46;5.31;5.41;5.57;5.50;5.52;5.71;5.31;5.44
  fixed.html;10.404609053497941;10.47;10.66;10.45;10.73;10.79;10.64;10.64;10.82;10.43;10.92;10.47;10.47;10.64;10.74;10.67;10.40;10.83;10.77;10.54;10.38;10.70;10.44;10.38;10.56
  downscale.html;5.493209876543211;5.27;5.50;5.50;5.51;5.46;5.58;5.58;5.51;5.49;5.49;5.47;9.09;5.56;5.61;5.50;5.47;5.59;5.47;5.49;5.60;5.61;5.58;5.40;5.43
  downscale.html;10.676522633744854;10.82;10.79;10.41;10.75;10.91;10.52;10.61;10.50;10.55;10.80;10.17;10.68;10.41;10.42;10.41;10.58;10.28;10.56;10.66;10.68;10.47;10.60;10.61;10.26
  4;iframe.svg;13.82;14.87;14.78;14.35;14.73;14.50;14.15;14.46;14.80;14.48;15.10;14.93;14.77;14.52;14.08;15.01;14.58;14.52;15.23;14.35;14.72;14.28;14.30;14.27;14.96
  5;reader.htm;10.72;10.62;10.23;10.48;10.42;10.64;10.40;10.40;10.14;10.60;10.51;10.36;10.57;10.41;10.52;10.75;10.19;10.72;10.44;9.75;10.49;10.07;10.54;10.46;10.44
  ```

* gecko_profile_interval: 1
* pine: False
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': True, 'apz.paint_skipping.enabled': False, 'layout.css.scroll-snap.spring-constant': "'10'", 'layout.css.scroll-behavior.same-physics-as-user-input': False, 'toolkit.framesRecording.bufferSize': 10000}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/scroll/scroll.manifest
* tpmozafterpaint: False
* tppagecycles: 25
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tscrollx_paint_skip)=

::::{dropdown} tscrollx_paint_skip
:class-container: anchor-id-tscrollx_paint_skip

* **Command to Run Locally**

  ```
  ./mach talos-test -a tscrollx_paint_skip
  ```

* TODO: Add a description. See this bug: https://bugzilla.mozilla.org/show_bug.cgi?id=2036344
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': True, 'apz.paint_skipping.enabled': True, 'layout.css.scroll-snap.spring-constant': "'10'", 'layout.css.scroll-behavior.same-physics-as-user-input': False, 'toolkit.framesRecording.bufferSize': 10000}
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tsvg_static)=

::::{dropdown} tsvg_static
:class-container: anchor-id-tsvg_static

* **Command to Run Locally**

  ```
  ./mach talos-test -a tsvg_static
  ```

* contact: :jwatt, :dholbert
* source: [svg_static](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/svg_static/)
* type: [Page load]
* data: we load the 5 svg pages 25 times, resulting in 5 sets of 25 data points
* summarization: An svg-only number that measures SVG rendering performance of some complex (but static) SVG content.
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 20; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l623)
  * suite: [geometric mean] of the 5 subtest results.
* **Example Data**

  ```
  0;gearflowers.svg;262;184;184;198;197;187;181;186;177;192;196;194;194;186;195;190;237;193;188;182;188;196;191;194;184
  1;composite-scale.svg;69;52;48;49;57;51;52;87;52;49;49;51;58;53;64;57;49;65;67;58;53;59;56;68;50
  2;composite-scale-opacity.svg;72;53;91;54;51;58;60;46;51;57;59;58;66;70;57;61;47;51;76;65;52;65;64;64;63
  3;composite-scale-rotate.svg;70;76;89;62;62;78;57;77;79;82;74;56;61;79;73;64;75;74;81;82;76;58;77;61;62
  4;composite-scale-rotate-opacity.svg;91;60;67;84;62;66;78;69;65;68;62;73;68;63;64;71;79;77;63;80;85;65;82;76;81
  ```

* gecko_profile_interval: 1
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/svg_static/svg_static.manifest
* tpmozafterpaint: True
* tppagecycles: 25
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tsvgm)=

::::{dropdown} tsvgm
:class-container: anchor-id-tsvgm

* **Command to Run Locally**

  ```
  ./mach talos-test -a tsvgm
  ```

* An svg-only number that measures SVG rendering performance for dynamic content only.
* contact: :jwatt, :dholbert
* add test details
* gecko_profile_interval: 10
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False}
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/svgx/svgm.manifest
* tpmozafterpaint: False
* tppagecycles: 7
* unit: ms
::::

(tsvgr_opacity)=

::::{dropdown} tsvgr_opacity
:class-container: anchor-id-tsvgr_opacity

* **Command to Run Locally**

  ```
  ./mach talos-test -a tsvgr_opacity
  ```

* contact: :jwatt, :dholbert
* source: [svg_opacity.manifest](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/svg_opacity/svg_opacity.manifest)
* type: [Page load]
* data: we load the 2 svg opacity pages 25 times, resulting in 2 sets of 25 data points
* summarization: [Row Major](#row-major-vs-column-major) and 25 cycles/page.
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 20; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l623)
  * suite: [geometric mean] of the 2 subtest results.
* description:
      | Renders many semi-transparent, partially overlapping SVG rectangles, and
        measures time to completion of this rendering.

      Note that this test also tends to reflect changes in network efficiency
      and navigation bar rendering issues.
* Most of the page load tests measure from before the location is
     changed, until onload + mozafterpaint, therefore any changes in
     chrome performance from the location change, or network performance
     (the pages load from a local web server) would affect page load
     times. SVG opacity is rather quick by itself, so any such
     chrome/network/etc performance changes would affect this test more
     than other page load tests (relatively, in percentages).
* **Example Data**

  ```
  0;big-optimizable-group-opacity-2500.svg;170;171;205;249;249;244;192;252;192;431;182;250;189;249;151;168;209;194;247;250;193;250;255;247;247
  1;small-group-opacity-2500.svg;585;436;387;441;512;438;440;380;443;391;450;386;459;383;445;388;450;436;485;443;383;438;528;444;441
  ```

* gecko_profile_interval: 1
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/svg_opacity/svg_opacity.manifest
* tpmozafterpaint: True
* tppagecycles: 25
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(tsvgx)=

::::{dropdown} tsvgx
:class-container: anchor-id-tsvgx

* **Command to Run Locally**

  ```
  ./mach talos-test -a tsvgx
  ```

* contact: :jwatt, :dholbert
* source: [svgx](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/svgx)
* type: [Page load]
* data: we load the 7 svg pages 25 times, resulting in 7 sets of 25 data points
* summarization: [Replacing tscroll,tsvg with tscrollx,tsvgx](https://groups.google.com/d/topic/mozilla.dev.platform/RICw5SJhNMo/discussion)
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 20; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l623)
  * suite: [geometric mean] of the 7 subtest results.
* description:
      | An svg-only number that measures SVG rendering performance, with
        animations or iterations of rendering. This is an ASAP test --i.e. it
        iterates in unlimited frame-rate mode thus reflecting the maximum
        rendering throughput of each test. The reported value is the overall
        duration the sequence/animation took to complete. To turn on ASAP mode,
        we set these preferences:

      ``preferences = {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1}``
* **Example Data**

  ```
  0;hixie-001.xml;562;555;508;521;522;520;499;510;492;514;502;504;500;521;510;506;511;505;495;517;520;512;503;504;502
  1;hixie-002.xml;510;613;536;530;536;522;498;505;500;504;498;529;498;509;493;512;501;506;504;499;496;505;508;511;503
  2;hixie-003.xml;339;248;242;261;250;241;240;248;258;244;235;240;247;248;239;247;241;245;242;245;251;239;241;240;237
  3;hixie-004.xml;709;540;538;536;540;536;552;539;535;535;543;533;536;535;545;537;537;537;537;539;538;535;536;538;536
  4;hixie-005.xml;3096;3086;3003;3809;3213;3323;3143;3313;3192;3203;3225;3048;3069;3101;3189;3251;3172;3122;3266;3183;3159;3076;3014;3237;3100
  5;hixie-006.xml;5586;5668;5565;5666;5668;5728;5886;5534;5484;5607;5678;5577;5745;5753;5532;5585;5506;5516;5648;5778;5894;5994;5794;5852;5810
  6;hixie-007.xml;1823;1743;1739;1743;1744;1787;1802;1788;1782;1766;1787;1750;1748;1788;1766;1779;1767;1794;1758;1768;1781;1773;1765;1798;1805
  ```

* **Possible regression causes**
  * Did you change the dimensions of the content area? Even a little? The
        tsvgx test seems to be sensitive to changes like this. See [bug
        1375479](https://bugzilla.mozilla.org/show_bug.cgi?id=1375479),
        for example. Usually, these sorts of "regressions" aren't real
        regressions
* they just mean that we need to re-baseline our
        expectations from the test.
* gecko_profile_interval: 10
* preferences: {'layout.frame_rate': 0, 'docshell.event_starvation_delay_hint': 1, 'dom.send_after_paint_to_content': False, 'places.history.enabled': False}
* timeout: 600
* tpchrome: False
* tpcycles: 1
* tpmanifest: ${talos}/tests/svgx/svgx.manifest
* tpmozafterpaint: False
* tppagecycles: 25
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-svgr-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
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
* - **talos-svgr**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-svgr-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(twinopen)=

::::{dropdown} twinopen
:class-container: anchor-id-twinopen

* **Command to Run Locally**

  ```
  ./mach talos-test -a twinopen
  ```

* contact: :gcp and platform integration
* source: [twinopen](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/tests/twinopen)
* type: Startup_
* data: we open a new browser window 20 times, resulting in 1 set of 20 data points.
* summarization: Time from calling OpenBrowserWindow until the chrome of the new window has [painted](https://developer.mozilla.org/en-US/docs/Web/Events#MozAfterPaint).
  * subtest: [ignore first] **5** data points, then take the [median] of the remaining 15; [source:
        test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l190)
  * suite: identical to subtest
* description:
      | Tests the amount of time it takes the open a new window from a currently
        open browser. This test does not include startup time. Multiple test
        windows are opened in succession, results reported are the average
        amount of time required to create and display a window in the running
        instance of the browser. (Measures ctrl-n performance.)
* **Example Data**

  ```
  [209.219, 222.180, 225.299, 225.970, 228.090, 229.450, 230.625, 236.315, 239.804, 242.795, 244.5, 244.770, 250.524, 251.785, 253.074, 255.349, 264.729, 266.014, 269.399, 326.190]
  ```

* extensions: ['${talos}/pageloader', '${talos}/tests/twinopen']
* gecko_profile_interval: 1
* preferences: {'browser.startup.homepage': 'about:blank'}
* timeout: 300
* tpmanifest: ${talos}/tests/twinopen/twinopen.manifest
* tpmozafterpaint: True
* tppagecycles: 20
* unit: ms
* **Test Task**:

:::{list-table} **{ref}`test-linux2404-64-shippable/opt <hardware-hpe-moonshot>`**
:widths: 30 15 15 15 15
:header-rows: 1

* - **Test Name**
  - mozilla-central
  - autoland
  - mozilla-release
  - mozilla-beta
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ✅
  - ✅
  - ❌
  - ❌
* - **talos-other-profiling**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
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
* - **talos-other**
  - ❌
  - ❌
  - ❌
  - ❌
* - **talos-other-swr**
  - ❌
  - ❌
  - ❌
  - ❌
:::


::::

(v8_7)=

::::{dropdown} v8_7
:class-container: anchor-id-v8_7

* **Command to Run Locally**

  ```
  ./mach talos-test -a v8_7
  ```

* description:
      | This is the V8 (version 7) javascript benchmark taken verbatim and slightly modified
        to fit into our pageloader extension and Talos harness. The previous version of this
        test is V8 version 5 which was run on selective branches and operating systems.
* contact: No longer being maintained by any team/individual
* gecko_profile_interval: 1
* lower_is_better: False
* preferences: {'dom.send_after_paint_to_content': False}
* resolution: 20
* tpcycles: 1
* tpmanifest: ${talos}/tests/v8_7/v8.manifest
* tpmozafterpaint: False
* unit: score
::::



## Extra Talos Tests

```{contents}
:depth: 1
:local: true
```

### File IO

File IO is tested using the tp5 test set in the [xperf]
test.

#### Possible regression causes

- **nonmain_startup_fileio opt (with or without e10s) windows7-32** –
  [bug
  1274018](https://bugzilla.mozilla.org/show_bug.cgi?id=1274018)
  This test seems to consistently report a higher result for
  mozilla-central compared to Try even for an identical revision due to
  extension signing checks. In other words, if you are comparing Try
  and Mozilla-Central you may see a false-positive regression on
  perfherder. Graphs:
  [non-e10s](https://treeherder.mozilla.org/perf.html#/graphs?timerange=604800&series=%5Bmozilla-central,e5f5eaa174ef22fdd6b6e150e8c450aa827c2ff6,1,1%5D&series=%5Btry,e5f5eaa174ef22fdd6b6e150e8c450aa827c2ff6,1,1%5D)
  [e10s](https://treeherder.mozilla.org/perf.html#/graphs?series=%5B%22mozilla-central%22,%222f3af3833d55ff371ecf01c41aeee1939ef3a782%22,1,1%5D&series=%5B%22try%22,%222f3af3833d55ff371ecf01c41aeee1939ef3a782%22,1,1%5D&timerange=604800)

### Xres (X Resource Monitoring)

A memory metric tracked during tp5 test runs. This metric is sampled
every 20 seconds. This metric is collected on linux only.

[xres man page](https://linux.die.net/man/3/xres).

### % CPU

Cpu usage tracked during tp5 test runs. This metric is sampled every 20
seconds. This metric is collected on windows only.

### tpaint

:::{warning}
This test no longer exists
:::

- contact: :davidb

- source:
  [tpaint-window.html](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/startup_test/tpaint.html)

- type: [Startup]

- data: we load the tpaint test window 20 times, resulting in 1 set of
  20 data points.

- summarization:

  - subtest: [ignore first] **5** data points, then take the [median] of the remaining 15; [source:
    test.py](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/test.py#l190)
  - suite: identical to subtest

```{eval-rst}
+-----------------+---------------------------------------------------+
| Talos test name | Description                                       |
+-----------------+---------------------------------------------------+
| tpaint          | twinopen but measuring the time after we receive  |
|                 | the `MozAfterPaint and OnLoad event <#paint>`__.  |
+-----------------+---------------------------------------------------+
```

Tests the amount of time it takes the open a new window. This test does
not include startup time. Multiple test windows are opened in
succession, results reported are the average amount of time required to
create and display a window in the running instance of the browser.
(Measures ctrl-n performance.)

**Example Data**

```none
[209.219, 222.180, 225.299, 225.970, 228.090, 229.450, 230.625, 236.315, 239.804, 242.795, 244.5, 244.770, 250.524, 251.785, 253.074, 255.349, 264.729, 266.014, 269.399, 326.190]
```

#### Possible regression causes

- None listed yet. If you fix a regression for this test and have some
  tips to share, this is a good place for them.

### xperf

- contact: perftest team
- source: [xperf
  instrumentation](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/xtalos)
- type: [Page load] (tp5n) / [Startup]
- measuring: IO counters from windows (currently, only startup IO is in
  scope)
- reporting: Summary of read/write counters for disk, network (lower is
  better)

These tests only run on windows builds. See [this active-data
query](https://activedata.allizom.org/tools/query.html#query_id=zqlX+2Jn)
for an updated set of platforms that xperf can be found on. If the query
is not found, use the following on the query page:

```javascript
{
    "from":"task",
    "groupby":["run.name","build.platform"],
    "limit":2000,
    "where":{"regex":{"run.name":".*xperf.*"}}
}
```

Talos will turn orange for 'x' jobs on windows 7 if your changeset
accesses files which are not predefined in the
[allowlist](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/xtalos/xperf_allowlist.json)
during startup; specifically, before the
"[sessionstore-windows-restored](https://hg.mozilla.org/mozilla-central/file/0eebc33d8593/toolkit/components/startup/nsAppStartup.cpp#l631)"
Firefox event. If your job turns orange, you will see a list of files in
Treeherder (or in the log file) which have been accessed unexpectedly
(similar to this):

```none
TEST-UNEXPECTED-FAIL : xperf: File '{profile}\secmod.db' was accessed and we were not expecting it. DiskReadCount: 6, DiskWriteCount: 0, DiskReadBytes: 16904, DiskWriteBytes: 0
TEST-UNEXPECTED-FAIL : xperf: File '{profile}\cert8.db' was accessed and we were not expecting it. DiskReadCount: 4, DiskWriteCount: 0, DiskReadBytes: 33288, DiskWriteBytes: 0
TEST-UNEXPECTED-FAIL : xperf: File 'c:\$logfile' was accessed and we were not expecting it. DiskReadCount: 0, DiskWriteCount: 2, DiskReadBytes: 0, DiskWriteBytes: 32768
TEST-UNEXPECTED-FAIL : xperf: File '{profile}\secmod.db' was accessed and we were not expecting it. DiskReadCount: 6, DiskWriteCount: 0, DiskReadBytes: 16904, DiskWriteBytes: 0
TEST-UNEXPECTED-FAIL : xperf: File '{profile}\cert8.db' was accessed and we were not expecting it. DiskReadCount: 4, DiskWriteCount: 0, DiskReadBytes: 33288, DiskWriteBytes: 0
TEST-UNEXPECTED-FAIL : xperf: File 'c:\$logfile' was accessed and we were not expecting it. DiskReadCount: 0, DiskWriteCount: 2, DiskReadBytes: 0, DiskWriteBytes: 32768
```

In the case that these files are expected to be accessed during startup
by your changeset, then we can add them to the
[allowlist](https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=Talos).

Xperf runs tp5 while collecting xperf metrics for disk IO and network
IO. The providers we listen for are:

- ['PROC_THREAD', 'LOADER', 'HARD_FAULTS', 'FILENAME', 'FILE_IO',
  'FILE_IO_INIT'](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/xperf.config#l10)

The values we collect during stackwalk are:

- ['FileRead', 'FileWrite',
  'FileFlush'](https://dxr.mozilla.org/mozilla-central/source/testing/talos/talos/xperf.config#l11)

Notes:

- Currently some runs may [return all-zeros and skew the
  results](https://bugzilla.mozilla.org/show_bug.cgi?id=1614805)
- Additionally, these runs don't have dedicated hardware and have a
  large variability. At least 30 runs are likely to be needed to get
  stable statistics (xref [bug
  1616236](https://bugzilla.mozilla.org/show_bug.cgi?id=1616236))

## Build metrics

These are not part of the Talos code, but like Talos they are benchmarks
that record data using the graphserver and are analyzed by the same
scripts for regressions.

### Number of constructors (num_ctors)

This test runs at build time and measures the number of static
initializers in the compiled code. Reducing this number is helpful for
[startup
optimizations](https://blog.mozilla.org/tglek/2010/05/27/startup-backward-constructors/).

- <https://hg.mozilla.org/build/tools/file/348853aee492/buildfarm/utils/count_ctors.py>

  - these are run for linux 32+64 opt and pgo builds.

## Platform microbenchmark

### IsASCII and IsUTF8 gtest microbenchmarks

- contact: :hsivonen
- source:
  [TestStrings.cpp](https://dxr.mozilla.org/mozilla-central/source/xpcom/tests/gtest/TestStrings.cpp)
- type: [Microbench]
- reporting: intervals in ms (lower is better)
- data: each test is run and measured 5 times
- summarization: take the [median] of the 5 data points; [source:
  MozGTestBench.cpp](https://dxr.mozilla.org/mozilla-central/source/testing/gtest/mozilla/MozGTestBench.cpp#43-46)

Test whose name starts with PerfIsASCII test the performance of the
XPCOM string IsASCII function with ASCII inputs if different lengths.

Test whose name starts with PerfIsUTF8 test the performance of the XPCOM
string IsUTF8 function with ASCII inputs if different lengths.

#### Possible regression causes

- The --enable-rust-simd accidentally getting turned off in automation.
- Changes to encoding_rs internals.
- LLVM optimizations regressing between updates to the copy of LLVM
  included in the Rust compiler.

### Microbench

- contact: :bholley
- source:
  [MozGTestBench.cpp](https://dxr.mozilla.org/mozilla-central/source/testing/gtest/mozilla/MozGTestBench.cpp)
- type: Custom GTest micro-benchmarking
- data: Time taken for a GTest function to execute
- summarization: Not a Talos test. This suite is provides a way to add
  low level platform performance regression tests for things that are
  not suited to be tested by Talos.

### PerfStrip Tests

- contact: :davidb
- source:
  <https://dxr.mozilla.org/mozilla-central/source/xpcom/tests/gtest/TestStrings.cpp>
- type: [Microbench]
- reporting: execution time in ms (lower is better) for 100k function
  calls
- data: each test run and measured 5 times
- summarization:

PerfStripWhitespace - call StripWhitespace() on 5 different test cases
20k times (each)

PerfStripCharsWhitespace - call StripChars("ftrn") on 5 different
test cases 20k times (each)

PerfStripCRLF - call StripCRLF() on 5 different test cases 20k times
(each)

PerfStripCharsCRLF() - call StripChars("rn") on 5 different test cases
20k times (each)

### Stylo gtest microbenchmarks

- contact: :bholley, :SimonSapin
- source:
  [gtest](https://dxr.mozilla.org/mozilla-central/source/layout/style/test/gtest)
- type: [Microbench]
- reporting: intervals in ms (lower is better)
- data: each test is run and measured 5 times
- summarization: take the [median] of the 5 data points; [source:
  MozGTestBench.cpp](https://dxr.mozilla.org/mozilla-central/source/testing/gtest/mozilla/MozGTestBench.cpp#43-46)

Servo_StyleSheet_FromUTF8Bytes_Bench parses a sample stylesheet 20 times
with Stylo’s CSS parser that is written in Rust. It starts from an
in-memory UTF-8 string, so that I/O or UTF-16-to-UTF-8 conversion is not
measured.

Gecko_nsCSSParser_ParseSheet_Bench does the same with Gecko’s previous
CSS parser that is written in C++, for comparison.

Servo_DeclarationBlock_SetPropertyById_Bench parses the string "10px"
with Stylo’s CSS parser and sets it as the value of a property in a
declaration block, a million times. This is similar to animations that
are based on JavaScript code modifying Element.style instead of using
CSS @keyframes.

Servo_DeclarationBlock_SetPropertyById_WithInitialSpace_Bench is the
same, but with the string " 10px" with an initial space. That initial
space is less typical of JS animations, but is almost always there in
stylesheets or full declarations like "width: 10px". This microbenchmark
was used to test the effect of some specific code changes. Regressions
here may be acceptable if Servo_StyleSheet_FromUTF8Bytes_Bench is not
affected.

## History of tp tests

### tp

The original tp test created by Mozilla to test browser page load time.
Cycled through 40 pages. The pages were copied from the live web during
November, 2000. Pages were cycled by loading them within the main
browser window from a script that lived in content.

### tp2/tp_js

The same tp test but loading the individual pages into a frame instead
of the main browser window. Still used the old 40 page, year 2000 web
page test set.

### tp3

An update to both the page set and the method by which pages are cycled.
The page set is now 393 pages from December, 2006. The pageloader is
re-built as an extension that is pre-loaded into the browser
chrome/components directories.

### tp4

Updated web page test set to 100 pages from February 2009.

### tp4m

This is a smaller pageset (21 pages) designed for mobile Firefox. This
is a blend of regular and mobile friendly pages.

We landed on this on April 18th, 2011 in [bug
648307](https://bugzilla.mozilla.org/show_bug.cgi?id=648307). This
runs for Android and Maemo mobile builds only.

### tp5

Updated web page test set to 100 pages from April 8th, 2011. Effort was
made for the pages to no longer be splash screens/login pages/home pages
but to be pages that better reflect the actual content of the site in
question. There are two test page data sets for tp5 which are used in
multiple tests (i.e. awsy, xperf, etc.): (i) an optimized data set
called tp5o, and (ii) the standard data set called tp5n.

### tp6

Created June 2017 with recorded pages via mitmproxy using modern google,
amazon, youtube, and facebook. Ideally this will contain more realistic
user accounts that have full content, in addition we would have more
than 4 sites- up to top 10 or maybe top 20.

These were migrated to Raptor between 2018 and 2019.

[geometric mean]: https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#geometric_mean
[ignore first]: https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#ignore_first
[median]: https://wiki.mozilla.org/TestEngineering/Performance/Talos/Data#median
