# Automated Testing

You've just written a feature and (hopefully!) want to test it. Or you've
decided that an existing feature doesn't have enough tests and want to contribute
some. But where do you start? You've looked around and found references to things
like "xpcshell" or "web-platform-tests" or "talos". What code, features or
platforms do they all test? Where do their feature sets overlap? In short, where
should your new tests go? This document is a starting point for those who want
to start to learn about Mozilla's automated testing tools and procedures. Below
you'll find a short summary of each framework we use, and some questions to help
you pick the framework(s) you need for your purposes.

If you still have questions, ask on [Matrix](https://wiki.mozilla.org/Matrix)
or on the relevant bug.

## Firefox Production

These tests are found within the
[Firefox repository](https://github.com/mozilla-firefox/firefox),
along with the product code.

They are run when a changeset is pushed
to the [`main` branch](https://github.com/mozilla-firefox/firefox/tree/main),
[autoland](https://github.com/mozilla-firefox/firefox/tree/autoland), or
{doc}`try </tools/try/index>`, with the results showing up on
[Treeherder](https://treeherder.mozilla.org/). Not all tests will be run on
every changeset; alogrithms are put in place to run the most likely failures,
with all tests being run on a regular basis.

They can also be run on local builds.
Note: Most of the mobile tests run on emulators, but some of the tests
(notably, performance tests) run on hardware devices.
We try to avoid running mobile tests on hardware devices unnecessarily.
In Treeherder, tests with names that start with "hw-" run on hardware.

### Linting

Lint tests help to ensure better quality, less error-prone code by
analysing the code with a linter.

```{eval-rst}
.. csv-table:: Linters
   :header-rows: 1

   "Treeherder Symbol", "Name", "Platforms", "What is Tested"
   "``ES``", ":doc:`ESLint </code-quality/lint/linters/eslint>`", "All", "JavaScript is analyzed for correctness."
   "``ES-build``", ":doc:`eslint-build </code-quality/lint/linters/eslint>`", "All", "Extended javascript analysis that uses build artifacts."
   "``mocha(EPM)``", ":doc:`ESLint-plugin-mozilla </code-quality/lint/linters/eslint-plugin-mozilla>`", "Desktop", "The ESLint plugin rules."
   "``f8``", ":doc:`flake8 </code-quality/lint/linters/ruff>`", "All", "Python analyzed for style and correctness."
   "``stylelint``", ":doc:`Stylelint </code-quality/lint/linters/stylelint>`", "All", "CSS is analyzed for correctness."
   "``W``", ":doc:`wpt lint </web-platform/index>`", "Desktop", "web-platform-tests analyzed for style and manifest correctness"
   "``A``", ":doc:`Spotless </code-quality/lint/linters/android-format>`", "Android", "Java is analyzed for style and correctness."
```

(functional-testing)=

### Functional testing

```{eval-rst}
.. csv-table:: Automated Test Suites
   :header-rows: 2

   "Treeherder Symbol", "Name", "Platform", "Process", "Environment", "", "Privilege", "What is Tested"
   "", "", "", "", "Shell", "Browser Profile", "",
   "``R(J)``", "JS Reftest", "Desktop", "N/A", "JSShell", "N/A", "N/A", "The JavaScript engine's implementation of the JavaScript language."
   "``R(C)``", ":doc:`Crashtest </web-platform/index>`", "All", "Child", "Content", "Yes", "Low", "That pages load without crashing, asserting, or leaking."
   "``R(R)``", ":doc:`Reftest </web-platform/index>`", "All", "Child", "Content", "Yes", "Low", "That pages are rendered (and thus also layed out) correctly."
   "``GTest``", ":doc:`GTest </gtest/index>`", "All", "N/A", "Terminal", "N/A", "N/A", "Code that is not exposed to JavaScript."
   "``X``", ":doc:`xpcshell </testing/xpcshell/index>`", "All", "Parent, Allow", "XPCShell", "Allow", "High", "Low-level code exposed to JavaScript, such as XPCOM components."
   "``M(a11y)``", "Accessibility (mochitest-a11y)", "Desktop", "Child", "Content", "Yes", "?", "Accessibility interfaces."
   "``M(1), M(2), M(...)``", ":doc:`Mochitest plain </testing/mochitest-plain/index>`", "All", "Child", "Content", "Yes", "Low, Allow", "Features exposed to JavaScript in web content, like DOM and other Web APIs, where the APIs do not require elevated permissions to test."
   "``M(c1/c2/c3)``", ":doc:`Mochitest chrome </testing/chrome-tests/index>`", "All", "Child, Allow", "Content", "Yes", "High", "Code requiring UI or JavaScript interactions with privileged objects."
   "``M(bc)``", ":doc:`Mochitest browser-chrome </testing/mochitest-plain/index>`", "All", "Parent, Allow", "Browser", "Yes", "High", "How the browser UI interacts with itself and with content."
   "``M(remote)``", "Mochitest Remote Protocol", "All", "Parent, Allow", "Browser", "Yes", "High", "Firefox Remote Protocol (Implements parts of Chrome dev-tools protocol). Based on Mochitest browser-chrome."
   "``SM(...), SM(pkg)``", "`SpiderMonkey automation <https://wiki.mozilla.org/Javascript:Automation_Builds>`__", "Desktop", "N/A", "JSShell", "N/A", "Low", "SpiderMonkey engine shell tests and JSAPI tests."
   "``W``", ":doc:`web-platform-tests </web-platform/index>`", "Desktop", "Child", "Content", "Yes", "Low", "Standardized features exposed to ECMAScript in web content; tests are shared with other vendors."
   "``Wr``", "`web-platform-tests <https://web-platform-tests.org/writing-tests/reftests.html>`__", "All", "Child", "Content", "Yes", "Low", "Layout and graphic correctness for standardized features; tests are shared with other vendors."
   "``Mn``", ":doc:`Marionette </remote/marionette/Testing>`", "Desktop", "?", "Content, Browser", "?", "High", "Large out-of-process function integration tests and tests that do communication with multiple remote Gecko processes."
   "``Fxfn``", ":doc:`Firefox UI Tests </remote/Testing>`", "Desktop", "?", "Content, Browser", "Yes", "High", "Integration tests with a focus on the user interface and localization."
   "``tt(c)``", ":doc:`telemetry-tests-client </toolkit/components/telemetry/internals/integration_tests/index>`", "Desktop", "N/A", "Content, Browser", "Yes", "High", "Integration tests for the Firefox Telemetry client."
   "``TV``", ":doc:`Test Verification (test-verify) </testing/test-verification/index>`", "All", "Depends on test harness", "?", "?", "?", "Uses other test harnesses - mochitest, reftest, xpcshell - to perform extra testing on new/modified tests."
   "``TVw``", ":doc:`Test Verification for wpt (test-verify-wpt) </testing/test-verification/index>`", "Desktop", "Child", "?", "?", "?", "Uses wpt test harnesses to perform extra testing on new/modified web-platform tests."
   "``WR(wrench)``", ":doc:`WebRender standalone tests </testing/webrender/index>`", "All", "N/A", "Terminal", "N/A", "N/A", "WebRender rust code (as a standalone module, with Gecko integration)."
```

Note: there are preference-based variations of the previous testing suites.
For example, mochitests on Treeherder can have `gli`, `swr`, `spi`,
`nofis`, `a11y-checks`, `spi-nw-1proc`, and many others. Another
example is GTest, which can use `GTest-1proc`. To learn more about
these variations, you can mouse hover over these items to read a
description of what these abbreviations mean.

(table-key)=

#### Table key

Symbol

: Abbreviation for the test suite used by
  [Treeherder](https://treeherder.mozilla.org/). The first letter
  generally indicates which of the test harnesses is used to execute
  the test. The letter in parentheses identifies the actual test suite.

Name

: Common name used when referring to the test suite.

Platform

: Most test suites are supported only on a subset of the available
  plaforms and operating systems. Unless otherwise noted:

- **Desktop** tests run on Windows, Mac OS X, and Linux.
- **Mobile** tests run on Android emulators or remotely on Android
  devices.

Process
: - When **Parent** is indicated, the test file will always run in the
    parent process, even when the browser is running in Electrolysis
    (e10s) mode.

- When **Child** is indicated, the test file will run in the child
  process when the browser is running in Electrolysis (e10s) mode.
- The **Allow** label indicates that the test has access to
  mechanisms to run code in the other process.

Environment
: - The **JSShell** and **XPCShell** environments are limited
    JavaScript execution environments with no windows or user
    interface (note however that XPCShell tests on Android are run
    within a browser window.)

- The **Content** indication means that the test is run inside a
  content page loaded by a browser window.
- The **Browser** indication means that the test is loaded in the
  JavaScript context of a browser XUL window.
- The **Browser Profile** column indicates whether a browser profile
  is loaded when the test starts. The **Allow** label means that the
  test can optionally load a profile using a special command.

Privilege

: Indicates whether the tests normally run with low (content) or high
  (chrome) JavaScript privileges. The **Allow** label means that the
  test can optionally run code in a privileged environment using a
  special command.

(performance-testing)=

### Performance testing

There are many test harnesses used to test performance.
{doc}`For more information on the various performance harnesses,
check out the perf docs. </testing/perfdocs/index>`

(so-which-should-i-use)=

## So which should I use?

Generally, you should pick the lowest-level framework that you can. If
you are testing JavaScript but don't need a window, use XPCShell or even
JSShell. If you're testing page layout, try to use
[web-platform-test reftest.](https://web-platform-tests.org/writing-tests/reftests.html)
The advantage in lower level testing is that you don't drag in a lot of
other components that might have their own problems, so you can home in
quickly on any bugs in what you are specifically testing.

Here's a series of questions to ask about your work when you want to
write some tests.

(is-it-low-level-code)=

### Is it low-level code?

If the functionality is exposed to JavaScript, and you don't need a
window, consider {doc}`XPCShell </testing/xpcshell/index>`. If not,
you'll probably have to use {doc}`GTest </gtest/index>`, which can
test pretty much anything. In general, this should be your
last option for a new test, unless you have to test something that is
not exposed to JavaScript.

(does-it-cause-a-crash)=

### Does it cause a crash?

If you've found pages that crash Firefox, add a
{doc}`crashtest </web-platform/index>` to
make sure future versions don't experience this crash (assertion or
leak) again. Note that this may lead to more tests once the core
problem is found.

(is-it-a-layoutgraphics-feature)=

### Is it a layout/graphics feature?

{doc}`Reftest </layout/Reftest>` is your best bet, if possible.

(do-you-need-to-verify-performance)=

### Do you need to verify performance?

{doc}`Use an appropriate performance test suite from this list </testing/perfdocs/index>`.

(are-you-testing-ui-features)=

### Are you testing UI features?

Try one of the flavors of
{doc}`mochitest </testing/mochitest-plain/index>`, or
{doc}`Marionette </remote/marionette/index>` if the application also needs to be
restarted, or tested with localized builds.

(are-you-testing-mobileandroid)=

### Are you testing Mobile/Android?

If you are testing GeckoView, you will need to need to use
{doc}`JUnit integration tests </mobile/android/geckoview/contributor/junit>`.

There are some specific features that
{doc}`Mochitest </testing/mochitest-plain/index>` or
{doc}`Reftest </layout/Reftest>` can cover. Browser-chrome tests do not run on
Android. If you want to test performance, {doc}`Raptor </testing/perfdocs/raptor>` will
be a good choice.

(are-you-doing-none-of-the-above)=

### Are you doing none of the above?

- To get your tests running in continuous integration, try
  {doc}`web-platform-tests </web-platform/index>`, or
  {doc}`Mochitest </testing/mochitest-plain/index>`, or,
  if higher privileges are required, try
  {doc}`Mochitest browser chrome tests </testing/mochitest-plain/index>`.
- For Desktop Firefox, or if you just want to see the future of Gecko
  testing, look into the on-going
  {doc}`Marionette </remote/marionette/Testing>` project.

(need-to-get-more-data-out-of-your-tests)=

## Need to get more data out of your tests?

Most test jobs now expose an environment variable named
`MOZ_UPLOAD_DIR`. If this variable is set during automated test runs,
you can drop additional files into this directory, and they will be
uploaded to a web server when the test finishes. The URLs to retrieve
the files will be output in the test log.

Passing `MOZ_RECORD_TEST=1` as an environment variable when running some
tests (e.g. mochitests) on Linux Desktop, macOS or Windows will trigger a recording of the
desktop. This works on try as well, in which case the video
file will be uploaded as an artifact and available in the
`Artifacts and Debugging Tools` panel on Treeherder.

For browser chrome mochitests, passing `MOZ_DEVTOOLS_TEST_SCOPES=1` as an
environment variable will record all variables and arguments available in
the scope of the test when any assert fails. On try, each failed assert will generate
a JSON file named `scope-variables-[...].json` which will be uploaded as a
test artifact. When using the feature locally, set MOZ_UPLOAD_DIR to a local
folder where the JSON files should be saved. Note that Firefox opens JSON files
with the built-in DevTools JSON viewer.

(need-to-set-preferences-for-test-suites)=

## Need to set preferences for test-suites?

First ask yourself if these prefs need to be enabled for all tests or
just a subset of tests (e.g to enable a feature).

(setting-prefs-that-only-apply-to-certain-tests)=

### Setting prefs that only apply to certain tests

If the answer is the latter, try to set the pref as local to the tests
that need it as possible. Here are some options:

- If the test runs in chrome scope (e.g mochitest chrome or
  browser-chrome), you can use
  {searchfox}`Services.prefs <modules/libpref/nsIPrefBranch.idl>`
  to set the prefs in your test's setup function. Be sure to reset the
  pref back to its original value during teardown!

- Mochitest plain tests can use
  {doc}`SpecialPowers </testing/mochitest-plain/faq>`
  to set prefs.

- All variants of mochitest can set prefs in their manifests. For
  example, to set a pref for all tests in a manifest:

  ```
  [DEFAULT]
  prefs =
    my.awesome.pref=foo,
    my.other.awesome.pref=bar,
  [test_foo.js]
  [test_bar.js]
  ```

- All variants of reftest can also set prefs in their
  {doc}`manifests </layout/Reftest>`.

- All variants of web-platform-tests can also {doc}`set prefs in their
  manifests </web-platform/index>`.

(setting-prefs-that-apply-to-the-entire-suite)=

### Setting prefs that apply to the entire suite

Most test suites define prefs in user.js files that live under
{searchfox}`testing/profiles`.
Each directory is a profile that contains a `user.js` file with a
number of prefs defined in it. Test suites will then merge one or more
of these basic profiles into their own profile at runtime. To see which
profiles apply to which test suites, you can inspect
{searchfox}`testing/profiles/profiles.json`.
Profiles at the beginning of the list get overridden by profiles at the
end of the list.

Because this system makes it hard to get an overall view of which
profiles are set for any given test suite, a handy `profile` utility
was created:

```
$ cd testing/profiles
$ ./profile -- --help
usage: profile [-h] {diff,sort,show,rm} ...
$ ./profile show mochitest          # prints all prefs that will be set in mochitest
$ ./profile diff mochitest reftest  # prints differences between the mochitest and reftest suites
```

:::{container} blockIndicator note
**Note:** JS engine tests do not use testing/profiles yet, instead
{searchfox}`set prefs here <js/src/tests/user.js>`.
:::

## Adding New Context to Skip Conditions

Often when standing up new test configurations, it's necessary to add new keys
that can be used in `skip-if` annotations.

```{toctree}
manifest-sandbox
```

## Other Topics

```{toctree}
mochia
tree-closure
```
