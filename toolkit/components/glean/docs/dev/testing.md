# Testing Glean

```{admonition} This documentation is about testing FOG itself
This document contains information about how FOG tests itself,
how to add new tests, how and what to log, and stuff like that.
If you're interested in learning how to test instrumentation you added,
you'll want to read
[the instrumetnation testing docs](../user/instrumentation_tests) instead.
```

Given the multiple API languages, processes, and dependencies,
testing FOG is a matter of choosing the right tool for the situation.

## One Big Command

To run all the things, here's the tl;dr:

`./mach build && ./mach lint -W -w -o --fix
&& ./mach rusttests && ./mach gtest "FOG*"
&& python3 ./mach python-test toolkit/components/glean/tests/pytest
&& ./mach test toolkit/components/glean/tests/xpcshell
&& ./mach telemetry-tests-client --tag client --gecko-log "-"
&& ./mach test toolkit/components/glean/tests/browser --headless
`

## Logging

An often-overlooked first line of testing is "what do the logs say?".
To turn on logging for FOG, use any of the following:
* Run Firefox with `RUST_LOG="fog_control,firefox_on_glean,glean_core"`.
    * On some platforms this will use terminal colours to indicate log level.
* Run Firefox with `MOZ_LOG="timestamp,sync,glean::*:5,firefox_on_glean::*:5,fog_control::*:5,glean_core::*:5"`.
* Set the following prefs:
    * `logging.config.timestamp` to `true`
    * `logging.config.sync` to `true`
    * `logging.fog_control::*` to `5`
    * `logging.firefox_on_glean::*` to `5`
    * `logging.glean::*` to `5`
    * `logging.glean_core::*` to `5`
    * `logging.config.clear_on_startup` to `false` (or all these prefs will be cleared on startup)

For more information on logging in Gecko, see the
[Gecko Logging docs](/xpcom/logging).

User-destined logs (of the "You did something wrong" variety) might output to the
[Browser Console](/devtools-user/browser_console/index)
if they originate in JS land. Open via
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>J</kbd>, or
<kbd>Cmd</kbd>+<kbd>Shift</kbd>+<kbd>J</kbd>.

```{admonition} Note
At present, Rust logging in non-main processes just doesn't work.
```

### What to log, and to where?

FOG covers a lot a ground (languages, layers):
where you are determines what logging you have available.

Here are some common situtations for logging:

#### JS to C++

If your logging is aimed at the user using the JS API
(e.g. because the type provided isn't convertable to the necessary C++ type)
then use the Browser Console via
[FOG's Common's `LogToBrowserConsole`](https://searchfox.org/mozilla-central/rev/d107bc8aeadcc816ba85cb21c1a6a1aac1d4ef9f/toolkit/components/glean/bindings/private/Common.cpp#19).

#### C++

If you are in C++ and didn't come from JS, use `MOZ_LOG` with module `fog`.

#### Rust

Use the logging macros from `log`, e.g. `log::info!` or `log::error!`.
Remember that, no matter the log level, `log::debug!` and `log::trace!`
[will not appear in non-debug builds](/testing-rust-code/index.md#gecko-logging)

If you are logging due to a situation caused by and fixable by a developer using the API,
use `log::error!(...)`. Otherwise, use a quieter level.

## `about:glean`

`about:glean` is a page in a running Firefox that allows you to
[debug the Glean SDK](https://mozilla.github.io/glean/book/user/debugging/index.html)
in Firefox Desktop.
It does this through the displayed user interface (just follow the instructions).

## Linting

To keep in accordance with Mozilla's various and several Coding Styles,
we rely on `mach lint`.

To lint the code in the "usual" way, automatically fixing where possible, run:
`./mach lint -W -w -o --fix`

This should keep you from checking in code that will automatically be backed out.

## Rust

Not all of our Rust code can be tested in a single fashion, unfortunately.

### Using `rusttests` (Treeherder symbol `Br` (a build task))

If the crate you're testing has no Gecko symbols you can write standard
[Rust tests](https://doc.rust-lang.org/book/ch11-01-writing-tests.html).

This supports both unit tests
(inline in the file under test) and integration tests
(in the `tests/` folder in the crate root).
Metric type tests are currently written as unit tests inline in the file,
as they require access to the metric ID, which should only be exposed in tests.

To run FOG's `rusttests` suite use `mach rusttests`

If the crate uses only a few Gecko symbols, they may use the
`with_gecko` feature to conditionally use them.
This allows the crate to test its non-Gecko-adjacent code using Rust tests.
(You will need to cover the Gecko-adjacent code via another means.)

**Note:** Some FOG rusttests panic on purpose. They print stack traces to stdout.
If the rusttests fail and you see a stack trace,
double-check it isn't from a purposefully-panicking test.

**Note:** If a test fails, it is very likely they'll poison the test lock.
This will cause all subsequent tests that attempt to take the test lock
(which is all of them)
to also fail due to `PoisonError`s. They can be safely ignored.

### Using `gtest` (Treeherder symbol `GTest` (a build task))

Because Gecko symbols aren't built for the
`rusttests` build,
any test that is written for code that uses Gecko symbols should be written as a
[`gtest`](https://github.com/google/googletest)
in `toolkit/components/glean/tests/gtest/`.
You can write the actual test code in Rust.
It needs to be accompanied by a C++ GTest that calls a C FFI-exported Rust function.
See [Testing & Debugging Rust Code](/testing-rust-code/index.md) for more.
See {searchfox}`toolkit/components/glean/tests/gtest/TestFog.cpp`
and {searchfox}`toolkit/components/glean/tests/gtest/test.rs`
for an example.

By necessity these can only be integration tests against the compiled crate.

**Note:** When adding a new test file, don't forget to add it to
`toolkit/components/glean/tests/gtest/moz.build` and use the
`FOGFixture` fixture
(e.g. `TEST_F(FOGFixture, YourTestName) { ... }`).

To run FOG's Rust `gtest` suite use `mach gtest FOG.*`

## Python (Treeherder symbol `py3(fp)` aka `source-test-python-fog`)

The [Glean Parser](https://github.com/mozilla/glean_parser/)
has been augmented to generate FOG-specific APIs for Glean metrics.
This augmentation is tested by running:

`mach test toolkit/components/glean/tests/pytest`

These tests require Python 3+.
If your default Python is Python 2, you may need to instead run:

`python3 mach python-test toolkit/components/glean/tests/pytest`

These tests check the code generator output against known good file contents.
If you change the code generator the files will need an update.
Run the test suite with the `UPDATE_EXPECT` environment variable set to do that automatically:

`UPDATE_EXPECT=1 mach test toolkit/components/glean/tests/pytest`

## C++ (Treeherder symbol `GTest` (a build task))

To test the C++ parts of FOG's implementation
(like metric types)
you should use `gtest`.
FOG's `gtest` tests are in
{searchfox}`gtest/ <toolkit/components/glean/tests/gtest/>`.

You can either add a test case to an existing file or add a new file.
If you add a new file, remember to add it to the
{searchfox}`moz.build <toolkit/components/glean/tests/gtest/moz.build>`
or the test runner won't be able to find it.

All tests should use the `FOGFixture` fixture where possible.
If not possible, prefix your test name with `FOG` so that all tests are run with
`./mach gtest FOG*`.

## JS (Treeherder symbol `X(Xn)` for some number `n`)

To test the JS parts of FOG's implementation
(like metric types)
you should use `xpcshell`.
FOG's `xpcshell` tests are in
{searchfox}`xpcshell/ <toolkit/components/glean/tests/xpcshell>`.

You can either add a test case to an existing file or add a new file.
If you add a new file, remember to add it to
{searchfox}`toolkit/components/glean/tests/xpcshell/xpcshell.toml`
or the test runner will not be able to find it.

To run FOG's JS tests, run:
`./mach test toolkit/components/glean/tests/xpcshell`

## Non-content-process multiprocess (Browser Chrome Mochitests with Treeherder symbol `M(bcN)` for some number `N`)

To test e.g. the GPU process support you need a full Firefox browser:
xpcshell doesn't have the flexibility.
To test that and have access to privileged JS (i.e. `Glean` and `FOG` APIs),
we use browser-chrome-flavoured mochitests you can find in
{searchfox}`browser/ <toolkit/components/glean/tests/browser>`.

If you need to add a new test file, remember to add it to
{searchfox}`toolkit/components/glean/tests/browser/browser.toml`
or the test runner will not be able to find it.

To run FOG's browser chrome tests, run:
`./mach test toolkit/components/glean/tests/browser`

## Integration (Marionette, borrowing `telemetry-tests-client` Treeherder symbol `tt(c)`)

To test pings (See [bug 1681742](https://bugzilla.mozilla.org/show_bug.cgi?id=1681742))
or anything that requires one or more full browsers running,
we use the `telemetry-tests-client` suite in
{searchfox}`toolkit/components/telemetry/tests/marionette/tests/client/`.

For more information on this suite, look to
[the telemetry-tests-client documentation](/toolkit/components/telemetry/internals/integration_tests/index.md)
and
[Marionette's Documentation](/remote/marionette/Testing.md).

To run these integration tests, run:
`./mach telemetry-tests-client --tag client`

There is no FOG-specific manifest tag, so this runs the complete client suite.
To narrow the run to FOG test files, use:
`./mach telemetry-tests-client toolkit/components/telemetry/tests/marionette/tests/client/test_fog*`

Passing test files directly bypasses manifest skip conditions. See the suite
documentation linked above for details and other runner options.

## Artifact-build-only failures -- testing JOG

In the event of artifact-build-only failures, you may need to test
the runtime metric registration subsystem, [JOG](jog).

[Its documentation](jog) has detailed instructions on how to go about doing this.
