# web-platform-tests

[web-platform-tests](http://web-platform-tests.org) is a
cross-browser testsuite. Writing tests as web-platform-tests helps
ensure that browsers all implement the same behaviour for web-platform
features.

## Upstream Documentation

This documentation covers the integration of web-platform-tests into
the Firefox tree. For documentation on writing tests, see
[web-platform-tests.org](http://web-platform-tests.org). In particular
the following documents cover common test-writing needs:

- [Javascript Tests (testharness.js)](https://web-platform-tests.org/writing-tests/testharness.html)

  - [testharness.js API](https://web-platform-tests.org/writing-tests/testharness-api.html)
  - [testdriver.js API](https://web-platform-tests.org/writing-tests/testdriver.html) -
    features for writing tests that require special privileges
    e.g. user-initiated input events.
  - [Message Channels](https://web-platform-tests.org/writing-tests/channels.html) -
    features for communicating between different globals (including
    those in different browsing context groups / processes).

- [Reftests](https://web-platform-tests.org/writing-tests/reftests.html)

- [Crashtests](https://web-platform-tests.org/writing-tests/crashtest.html)

- [Server features](https://web-platform-tests.org/writing-tests/server-features.html) -
  e.g. multiple origins, substitutions, server-side Python scripts.

## Running Tests

Tests can be run using `mach`:

```
mach wpt
```

To run only certain tests, pass these as additional arguments to the
command. For example to include all tests in the dom directory:

```
mach wpt testing/web-platform/tests/dom
```

Tests may also be passed by id; this is the path plus any query or
fragment part of a URL and is suitable for copying directly from logs
e.g. on treeherder:

```
mach wpt /web-nfc/idlharness.https.window.html
```

A single file can produce multiple tests, so passing test ids rather
than paths is sometimes necessary to run exactly one test.

The testsuite contains a mix of various test types including
Javascript (`testharness`) tests, reftests and wdspec tests. To limit
the type of tests that get run, use `--test-type=<type>` e.g.
`--test-type=reftest` for reftests.

Note that if only a single testharness test is run the browser will
stay open by default (matching the behaviour of mochitest). To prevent
this pass `--no-pause-after-test` to `mach wpt`.

When the source tree is configured for building android, tests will
also be run on Android, by default using a local emulator.

### Running Tests In Other Browsers

web-platform-tests is cross browser, and the runner is compatible with
multiple browsers. Therefore it's possible to check the behaviour of
tests in other browsers. By default Chrome, Edge and Servo are
supported. In order to run the tests in these browsers use the
`--product` argument to wptrunner:

```
mach wpt --product chrome dom/historical.html
```

By default these browsers run without expectation metadata, but it can
be added in the `testing/web-platform/products/<product>`
directory. To run with the same metadata as for Firefox (so that
differences are reported as unexpected results), pass `--meta
testing/web-platform/meta` to the mach command.

Results from the upstream CI for many browser, including Chrome and
Safari, are available on [wpt.fyi](https://wpt.fyi). There is also a
[gecko dashboard](https://jgraham.github.io/wptdash/) which by default
shows tests that are failing in Gecko but not in Chrome and Safari,
organised by bug component, based on the wpt.fyi data.

## Directories

Under `testing/web-platform` are the following directories:

`tests/`

: An automatically-updated import of the web-platform-tests
  repository. Any changes to this directory are automatically
  converted into pull requests against the upstream repository, and
  merged if they pass CI.

`meta/`

: Gecko-specific metadata including expected test results
  and configuration e.g. prefs that are set when running the
  test. This is explained in the following section.

`mozilla/tests`

: Tests that will not be upstreamed and may
  make use of Mozilla-specific features. They can access
  the `SpecialPowers` APIs.

`mozilla/meta`

: Metadata for the Mozilla-specific tests.

## Metadata

In order to separate out the shared testsuite from Firefox-specific
metadata about the tests, all the metadata is stored in separate
ini-like files in the `meta/` sub-directory.

There is one metadata file per test file with associated
gecko-specific data. The metadata file of a test has the same relative
path as the test file and has the the suffix `.ini` e.g. for the
test in `testing/web-platform/tests/example/example.html`, the
corresponding metadata file is
`testing/web-platform/meta/example/example.html.ini`.

The format of these files is similar to `ini` files, but with a couple
of important differences; sections can be nested using indentation,
and only `:` is permitted as a key-value separator. For example:

```
[filename.html]
    [Subtest 1 name]
        key: value

    [Subtest 2 name]
        key: [list, value]
```

For cases where a single file generates multiple tests (e.g. variants
or `.any.js` tests), the metadata file has one top-level section for
each test, for example:

```
[test.any.html]
    [Subtest name]
        key: value

[test.any.worker.html]
    [Subtest name]
        key: other-value
```

Values can be made conditional using a Python-like conditional syntax:

```
[filename.html]
    key:
        if os == "linux": linux-value
        default-value
```

The available variables for the conditions are those provided by
{doc}`mozinfo </mozbase/mozinfo>`, plus
some additional [wpt-specific values](https://searchfox.org/mozilla-central/search?q=def%20run_info_extras&path=testing%2Fweb-platform%2Ftests%2Ftools%2Fwptrunner%2Fwptrunner%2Fbrowsers%2Ffirefox.py&case=false&regexp=false).

For more information on manifest files, see the [wptrunner
documentation](https://web-platform-tests.org/tools/wptrunner/docs/expectation.html)

### Expectation Data

All tests that don't pass in our CI have expectation data stored in
the metadata file, under the key `expected`. For example the
expectation file for a test with one failing subtest and one erroring
subtest might look like:

```
[filename.html]
    [Subtest name for failing test]
        expected: FAIL

    [Subtest name for erroring test]
        expected: ERROR
```

Expectations can be made configuration-specific using the conditional syntax:

```
[filename.html]
    expected:
        if os == "linux" and bits == 32: TIMEOUT
        if os == "win": ERROR
        FAIL
```

Tests that are intermittent may be marked with multiple statuses using
a list of possibilities e.g. for a test that usually passes, but
intermittently fails:

```
[filename.html]
    [Subtest name for intermittent test]
        expected: [PASS, FAIL]
```

### Auto-generating Expectation Data

After changing some code it may be necessary to update the expectation
data for the relevant tests. This can of course be done manually, but
tools are available to automate much of the process.

First one must run the tests that have changed status, and save the
raw log output to a file:

```
mach wpt /url/of/test.html --log-wptreport wptreport.json
```

Then the `wpt-update` command may be run using this log data to update
the expectation files:

```
mach wpt-update wptreport.json
```

CI runs also produce `wptreport.json` files that can be downloaded
as artifacts. When tests are run across multiple platforms, and all
the wptreport files are processed together, the tooling will set the
appropriate conditions for any platform-specific results:

```
mach wpt-update logs/*.json
```

For complete runs the `--full` flag will cause metadata to be
removed when a) the test was updated and b) there is a condition that
didn't match any of the configuration in the input files.

When tests are run more than once `--update-intermittent` flag will
cause conflicting results to be marked as intermittents (otherwise the
data is not updated in the case of conflicts).

### Disabling Tests

Tests are disabled using the same manifest files used to set
expectation values. For example, if a test is unstable on Windows, it
can be disabled using an ini file with the contents:

```
[filename.html]
    disabled:
        if os == "win": https://bugzilla.mozilla.org/show_bug.cgi?id=1234567
```

For intermittents it's generally preferable to give the test multiple
expectations rather than disable it.

### Fuzzy Reftests

Reftests where the test doesn't exactly match the reference can be
marked as fuzzy. If the difference is inherent to the test, it should
be encoded in a [meta element](https://web-platform-tests.org/writing-tests/reftests.html#fuzzy-matching),
but where it's a Gecko-specific difference it can be added to the
metadata file, using the same syntax:

```
[filename.html]
    fuzzy: maxDifference=10-15;totalPixels=200-300
```

In this case we expect between 200 and 300 pixels, inclusive, to be
different, and the maximum difference in any RGB colour channel to be
between 10 and 15.

### Enabling Prefs

Some tests require specific prefs to be enabled before running. These
prefs can be set in the expectation data using a `prefs` key with a
comma-separated list of `pref.name:value` items:

```
[filename.html]
    prefs: [dom.serviceWorkers.enabled:true,
            dom.serviceWorkers.exemptFromPerDomainMax:true,
            dom.caches.enabled:true]
```

### Disabling Leak Checks

When a test is imported that leaks, it may be necessary to temporarily
disable leak checking for that test in order to allow the import to
proceed. This works in basically the same way as disabling a test, but
with the key `leaks`:

```
[filename.html]
    leaks:
        if os == "linux": https://bugzilla.mozilla.org/show_bug.cgi?id=1234567
```

### Per-Directory Metadata

Occasionally it is useful to set metadata for an entire directory of
tests e.g. to disable then all, or to enable prefs for every test. In
that case it is possible to create a `__dir__.ini` file in the
metadata directory corresponding to the tests for which you want to
set this metadata e.g. to disable all the tests in
`tests/feature/unsupported/`, one might create
`meta/feature/unsupported/__dir__.ini` with the contents:

```
disabled: Feature is unsupported
```

Settings set in this way are inherited into sub-directories. It is
possible to unset a value that has been set in a parent using the
special token `@Reset` (usually used with prefs), or to force a value
to true or false using `@True` and `@False`. For example to enable
the tests in `meta/feature/unsupported/subfeature-supported` one might
create an ini file
`meta/feature/unsupported/subfeature-supported/__dir__.ini` like:

```
disabled: @False
```

### Setting Metadata for Release Branches

Run info properties can be used to set metadata for release branches
that differs from nightly (e.g. for when a feature depends on prefs
that are only set on nightly), for example:

```
[filename.html]
  expected:
    if release_or_beta: FAIL
```

Note that in general the automatic metadata update will work better if
the nonstandard configuration is used explicitly in the conditional,
and placed at the top of the set of conditions, i.e. the following
would cause problems later:

```
[filename.html]
  expected:
    if nightly_build: PASS
    FAIL
```

This is because on import the automatic metadata updates are run
against the results of nightly builds, and we remove any existing
conditions that match all the input runs to avoid building up stale
configuration options.

## Test Manifest

web-platform-tests use a large auto-generated JSON file as their
manifest. This stores data about the type of tests, their references,
if any, and their timeout, gathered by inspecting the filenames and
the contents of the test files. It is not necessary to manually add
new tests to the manifest; it is automatically kept up to date when
running `mach wpt`.

## Synchronization with Upstream

Tests are automatically synchronized with upstream using the [wpt-sync
bot](https://github.com/mozilla/wpt-sync). This performs the following tasks:

- Creates upstream PRs for changes in
  `testing/web-platform/tests` once they land on autoland, and
  automatically merges them after they reach the `main` branch.
- Runs merged upstream PRs through gecko CI to generate updated
  expectation metadata.
- Updates the copy of web-platform-tests in the gecko tree with
  changes from upstream, and the expectation metadata required to make
  CI jobs pass.

The nature of a two-way sync means that occasional merge conflicts and
other problems. If something isn't in sync with upstream in the way
you expect, please ask on [#interop](https://chat.mozilla.org/#/room/#interop:mozilla.org) on matritx.

## wpt-serve

Sometimes, it's preferable to run the WPT's web server on its own, and point different browsers to the test files.

./mach wpt-serve

can be used for this, after a short setup:

On Unix, one can run:

./wpt make-hosts-file | sudo tee -a /etc/hosts

from the root of the WPT checkout, present at `testing/web-platform/tests/`.

On Windows, from an administrator `mozilla-build` shell, one can run:

./wpt make-hosts-file >> /c/Windows/System32/drivers/etc/host

from the WPT checkout.

Most of the time, browsing to <http://localhost:8000> will allow
running the test, although some tests have special requirements, such as running
on a specific domain or running using https.
