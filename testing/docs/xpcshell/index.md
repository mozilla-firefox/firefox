# XPCShell tests

xpcshell tests are quick-to-run tests, that are generally used to write
unit tests. They do not have access to the full browser chrome like
`browser chrome tests`, and so have much
lower overhead. They are typical run by using `./mach xpcshell-test`
which initiates a new `xpcshell` session with
the xpcshell testing harness. Anything available to the XPCOM layer
(through scriptable interfaces) can be tested with xpcshell. See
`Mozilla automated testing` and `pages
tagged "automated testing"` for more
information.

## Introducing xpcshell testing

xpcshell test filenames must start with `test_`.

### Creating a new test directory

If you need to create a new test directory, then follow the steps here.
The test runner needs to know about the existence of the tests and how
to configure them through the use of the `xpcshell.toml` manifest file.

First add a `XPCSHELL_TESTS_MANIFESTS += ['xpcshell.toml']` declaration
(with the correct relative `xpcshell.toml` path) to the `moz.build`
file located in or above the directory.

Then create an empty `xpcshell.toml` file to tell the build system
about the individual tests, and provide any additional configuration
options.

### Creating a new test in an existing directory

If you're creating a new test in an existing directory, you can simply
run:

```bash
./mach addtest --suite xpcshell path/to/test/test_example.js
git add path/to/test/test_example.js
```

This will automatically create the test file and add it to
`xpcshell.toml`, the second line adds it to your commit.

The test file contains an empty test which will give you an idea of how
to write a test. There are plenty more examples throughout
mozilla-central.

## Running tests

To run the test, execute it by running the `mach` command from the
root of the Gecko source code directory.

```bash
# Run a single test:
./mach xpcshell-test path/to/tests/test_example.js

# Test an entire test suite in a folder:
./mach xpcshell-test path/to/tests/

# Or run any type of test, including both xpcshell and browser chrome tests:
./mach test path/to/tests/test_example.js
```

The test is executed by the testing harness. It will call in turn:

- `run_test` (if it exists).
- Any functions added with `add_task` or `add_test` in the order
  they were defined in the file.

See also the notes below around `add_task` and `add_test`.

## xpcshell Testing API

xpcshell tests have access to the following functions. They are defined
in
{searchfox}`testing/xpcshell/head.js <testing/xpcshell/head.js>`
and
{searchfox}`testing/modules/Assert.sys.mjs <testing/modules/Assert.sys.mjs>`.

### Assertions

- `Assert.ok(truthyOrFalsy[, message])`
- `Assert.equal(actual, expected[, message])`
- `Assert.notEqual(actual, expected[, message])`
- `Assert.deepEqual(actual, expected[, message])`
- `Assert.notDeepEqual(actual, expected[, message])`
- `Assert.strictEqual(actual, expected[, message])`
- `Assert.notStrictEqual(actual, expected[, message])`
- `Assert.rejects(actual, expected[, message])`
- `Assert.greater(actual, expected[, message])`
- `Assert.greaterOrEqual(actual, expected[, message])`
- `Assert.less(actual, expected[, message])`
- `Assert.lessOrEqual(actual, expected[, message])`

These assertion methods are provided by
{searchfox}`testing/modules/Assert.sys.mjs <testing/modules/Assert.sys.mjs>`.
It implements the [CommonJS Unit Testing specification version
1.1](http://wiki.commonjs.org/wiki/Unit_Testing/1.1), which
provides a basic, standardized interface for performing in-code
logical assertions with optional, customizable error reporting. It is
*highly* recommended to use these assertion methods, instead of the
ones mentioned below. You can on all these methods remove the
`Assert.` from the beginning of the name, e.g. `ok(true)` rather
than `Assert.ok(true)`, however keeping the `Assert.` prefix may
be seen as more descriptive and easier to spot where the tests are.
`Assert.throws(callback, expectedException[, message])`
`Assert.throws(callback[, message])`
Asserts that the provided callback function throws an exception. The
`expectedException` argument can be an `Error` instance, or a
regular expression matching part of the error message (like in
`Assert.throws(() => a.b, /is not defined/`).
`Assert.rejects(promise, expectedException[, message])`
Asserts that the provided promise is rejected. Note: that this should
be called prefixed with an `await`. The `expectedException`
argument can be an `Error` instance, or a regular expression
matching part of the error message. Example:
`await Assert.rejects(myPromise, /bad response/);`

### Test case registration and execution

`add_task([condition, ]testFunc)`

: Add an asynchronous function or to the list of tests that are to be
  run asynchronously. Whenever the function `await`s a
  [Promise](https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Promise),
  the test runner waits until the promise is resolved or rejected
  before proceeding. Rejected promises are converted into exceptions,
  and resolved promises are converted into values.
  You can optionally specify a condition which causes the test function
  to be skipped; see [Adding conditions through the add_task or
  add_test
  function](#adding-conditions-through-the-add_task-or-add_test-function)
  for details.
  For tests that use `add_task()`, the `run_test()` function is
  optional, but if present, it should also call `run_next_test()` to
  start execution of all asynchronous test functions. The test cases
  must not call `run_next_test()`, it is called automatically when
  the task finishes. See [Async tests](#async-tests), below, for
  more information.

`add_test([condition, ]testFunction)`

: Add a test function to the list of tests that are to be run
  asynchronously.
  You can optionally specify a condition which causes the test function
  to be skipped; see [Adding conditions through the add_task or
  add_test
  function](#adding-conditions-through-the-add_task-or-add_test-function)
  for details.
  Each test function must call `run_next_test()` when it's done. For
  tests that use `add_test()`, `the run_test()` function is
  optional, but if present, it should also call `run_next_test()` to
  start execution of all asynchronous test functions. In most cases,
  you should rather use the more readable variant `add_task()`. See
  [Async tests](#async-tests), below, for more information.

`run_next_test()`

: Run the next test function from the list of asynchronous tests. Each
  test function must call `run_next_test()` when it's done.
  `run_test()` should also call `run_next_test()` to start
  execution of all asynchronous test functions. See [Async
  tests](#async-tests), below, for more information.

**\`\`registerCleanupFunction\`\`**`(callback)`

: Executes the function `callback` after the current JS test file has
  finished running, regardless of whether the tests inside it pass or
  fail. You can use this to clean up anything that might otherwise
  cause problems between test runs.
  If `callback` returns a `Promise`, the test will not finish until
  the promise is fulfilled or rejected (making the termination function
  asynchronous).
  Cleanup functions are called in reverse order of registration.

`do_test_pending()`

: Delay exit of the test until do_test_finished() is called.
  do_test_pending() may be called multiple times, and
  do_test_finished() must be paired with each before the unit test will
  exit.

`do_test_finished()`

: Call this function to inform the test framework that an asynchronous
  operation has completed. If all asynchronous operations have
  completed (i.e., every do_test_pending() has been matched with a
  do_test_finished() in execution), then the unit test will exit.

### Environment

`do_get_file(testdirRelativePath, allowNonexistent)`

: Returns an `nsILocalFile` object representing the given file (or
  directory) in the test directory. For example, if your test is
  unit/test_something.js, and you need to access unit/data/somefile,
  you would call `do_get_file('data/somefile')`. The given path must
  be delimited with forward slashes. You can use this to access
  test-specific auxiliary files if your test requires access to
  external files. Note that you can also use this function to get
  directories.

  :::{note}
  **Note:** If your test needs access to one or more files that
  aren't in the test directory, you should install those files to
  the test directory in the Makefile where you specify
  `XPCSHELL_TESTS`. For an example, see
  `netwerk/test/Makefile.in#117`.
  :::

`do_get_profile()`

: Registers a directory with the profile service and returns an
  `nsILocalFile` object representing that directory. It also makes
  sure that the **profile-change-net-teardown**,
  **profile-change-teardown**, and **profile-before-change** [observer
  notifications](https://developer.mozilla.org/en-US/docs/Observer_Notifications)
  are sent before the test finishes. This is useful if the components
  loaded in the test observe them to do cleanup on shutdown (e.g.,
  places).

  :::{note}
  **Note:** `do_register_cleanup` will perform any cleanup
  operation *before* the profile and the network is shut down by the
  observer notifications.
  :::

`do_get_idle()`

: By default xpcshell tests will disable the idle service, so that idle
  time will always be reported as 0. Calling this function will
  re-enable the service and return a handle to it; the idle time will
  then be correctly requested to the underlying OS. The idle-daily
  notification could be fired when requesting idle service. It is
  suggested to always get the service through this method if the test
  has to use idle.

`do_get_cwd()`

: Returns an `nsILocalFile` object representing the test directory.
  This is the directory containing the test file when it is currently
  being run. Your test can write to this directory as well as read any
  files located alongside your test. Your test should be careful to
  ensure that it will not fail if a file it intends to write already
  exists, however.

`load(testdirRelativePath)`

: Imports the JavaScript file referenced by `testdirRelativePath`
  into the global script context, executing the code inside it. The
  file specified is a file within the test directory. For example, if
  your test is unit/test_something.js and you have another file
  unit/extra_helpers.js, you can load the second file from the first by
  calling `load('extra_helpers.js')`.

### Utility

`do_parse_document(path, type)`

: Parses and returns a DOM document.

`executeSoon(callback)`

: Executes the function `callback` on a later pass through the event
  loop. Use this when you want some code to execute after the current
  function has finished executing, but you don't care about a specific
  time delay. This function will automatically insert a
  `do_test_pending` / `do_test_finished` pair for you.

`do_timeout(delay, fun)`

: Call this function to schedule a timeout. The given function will be
  called with no arguments provided after the specified delay (in
  milliseconds). Note that you must call `do_test_pending` so that
  the test isn't completed before your timer fires, and you must call
  `do_test_finished` when the actions you perform in the timeout
  complete, if you have no other functionality to test. (Note: the
  function argument used to be a string argument to be passed to eval,
  and some older branches support only a string argument or support
  both string and function.)

### Multiprocess communication

`do_send_remote_message(name, optionalData)`

: Asynchronously send a message to all remote processes. Pairs with
  `do_await_remote_message` or equivalent ProcessMessageManager
  listeners.

`do_await_remote_message(name, optionalCallback)`

: Returns a promise that is resolved when the message is received. Must
  be paired with`do_send_remote_message` or equivalent
  ProcessMessageManager calls. If **optionalCallback** is provided, the
  callback must call `do_test_finished`. If optionalData is passed
  to `do_send_remote_message` then that data is the first argument to
  **optionalCallback** or the value to which the promise resolves.

## xpcshell.toml manifest

The manifest controls what tests are included in a test suite, and the
configuration of the tests. It is loaded via the \`moz.build\` property
configuration property.

The following are all of the configuration options for a test suite as
listed under the `[DEFAULT]` section of the manifest.

`tags`

: Tests can be filtered by tags when running multiple tests. The
  command for mach is `./mach xpcshell-test --tag TAGNAME`

`head`

: The relative path to the head JavaScript file, which is run once
  before a test suite is run. The variables declared in the root scope
  are available as globals in the test files. See [Test head and
  support files](#test-head-and-support-files) for more information
  and usage.

`firefox-appdir`

: Set this to "browser" if your tests need access to things in the
  browser/ directory (e.g. additional XPCOM services that live there)

`skip-if` `run-if` `fail-if`

: For this entire test suite, run the tests only if they meet certain
  conditions. See [Adding conditions in the xpcshell.toml
  manifest](#adding-conditions-through-the-add_task-or-add_test-function) for how
  to use these properties.

`support-files`

: Make files available via the `resource://test/[filename]` path to
  the tests. The path can be relative to other directories, but it will
  be served only with the filename. See [Test head and support
  files](#test-head-and-support-files) for more information and
  usage.

`[test_*]`

: Test file names must start with `test_` and are listed in square
  brackets

`requesttimeoutfactor`

: A multiplier applied to the default test timeout. The default timeout for
  xpcshell tests is 30 seconds. Setting `requesttimeoutfactor = 2` will
  increase the timeout to 60 seconds (30 × 2). This can be set either at the
  manifest level (under `[DEFAULT]`) to apply to all tests, or on individual
  test entries to apply only to specific tests.

  **Important:** Slower platforms (such as Android, debug builds, TSan, ASan) already
  have platform-wide timeout factors defined in `taskcluster/kinds/test/xpcshell.yml`
  that apply to all tests running on those platforms. These platform factors are
  multiplied with any manifest-level or test-level factors you specify. Therefore,
  you should **not** set `requesttimeoutfactor` in manifests simply because a
  platform is generally slower—only use it when a specific test needs extra time
  beyond what the platform factor already provides.

  This should be used when tests legitimately require more time due to:

- Specific tests being disproportionately slower on certain platforms (beyond the general platform slowness)
- Complex operations that take longer than the default timeout (e.g., large database operations, extensive network tests)
- Tests that run multiple time-consuming operations sequentially

  You should **not** use this for tests that are slow due to inefficient test code
  or unnecessary waits. Consider profiling and refactoring such tests instead.

  Example usage in a manifest:

  ```toml
  [DEFAULT]
  # Apply a 2x timeout factor to all tests in this manifest
  requesttimeoutfactor = 2

  ["test_slow_on_windows.js"]
  # This test needs 3x timeout (90 seconds) on Windows
  requesttimeoutfactor = 3  # Slow on Windows
  ```

  When a test-level factor is specified, it replaces (not multiplies) the manifest-level
  factor. For example, if the manifest has `requesttimeoutfactor = 2` and a test has
  `requesttimeoutfactor = 3`, the test will use a 3x factor, not 6x.

### Creating a new xpcshell.toml file

When creating a new directory and new xpcshell.toml manifest file, the
following must be added to a moz.build file near that file in the
directory hierarchy:

```bash
XPCSHELL_TESTS_MANIFESTS += ['path/to/xpcshell.toml']
```

Typically, the moz.build containing *XPCSHELL_TESTS_MANIFESTS* is not in
the same directory as *xpcshell.toml*, but rather in a parent directory.
Common directory structures look like:

```bash
feature
├──moz.build
└──tests/xpcshell
   └──xpcshell.toml

# or

feature
├──moz.build
└──tests
   ├──moz.build
   └──xpcshell
      └──xpcshell.toml
```

### Test head and support files

Typically in a test suite, similar setup code and dependencies will need
to be loaded in across each test. This can be done through the test
head, which is the file declared in the `xpcshell.toml` manifest file
under the `head` property. The file itself is typically called
`head.js`. Any variable declared in the test head will be in the
global scope of each test in that test suite.

In addition to the test head, other support files can be declared in the
`xpcshell.toml` manifest file. This is done through the
`support-files` declaration. These files will be made available
through the url `resource://test` plus the name of the file. These
files can then be loaded in using the
`ChromeUtils.importESModule` function
or other loaders. The support files can be located in other directory as
well, and they will be made available by their filename.

```bash
# File structure:

path/to/tests
├──head.js
├──module.mjs
├──moz.build
├──test_example.js
└──xpcshell.toml
```

```text
# xpcshell.toml
[DEFAULT]
head = head.js
support-files =
  ./module.mjs
  ../../some/other/file.js
[test_component_state.js]
```

```js
// head.js
var globalValue = "A global value.";

// Import support-files.
const { foo } = ChromeUtils.importESModule("resource://test/module.mjs");
const { bar } = ChromeUtils.importESModule("resource://test/file.mjs");
```

```js
// test_example.js
function run_test() {
  equal(globalValue, "A global value.", "Declarations in head.js can be accessed");
}
```

## Additional testing considerations

### Async tests

Asynchronous tests (that is, those whose success cannot be determined
until after `run_test` finishes) can be written in a variety of ways.

## Task-based asynchronous tests

The easiest is using the `add_task` helper. `add_task` can take an
asynchronous function as a parameter. `add_task` tests are run
automatically if you don't have a `run_test` function.

```js
add_task(async function test_foo() {
  let foo = await makeFoo(); // makeFoo() returns a Promise<foo>
  equal(foo, expectedFoo, "Should have received the expected object");
});

add_task(async function test_bar() {
  let foo = await makeBar(); // makeBar() returns a Promise<bar>
  Assert.equal(bar, expectedBar, "Should have received the expected object");
});
```

### Callback-based asynchronous tests

You can also use `add_test`, which takes a function and adds it to the
list of asynchronously-run functions. Each function given to
`add_test` must also call `run_next_test` at its end. You should
normally use `add_task` instead of `add_test`, but you may see
`add_test` in existing tests.

```js
add_test(function test_foo() {
  makeFoo(function callback(foo) { // makeFoo invokes a callback<foo> once completed
    equal(foo, expectedFoo);
    run_next_test();
  });
});

add_test(function test_bar() {
  makeBar(function callback(bar) {
    equal(bar, expectedBar);
    run_next_test();
  });
});
```

### Other tests

We can also tell the test harness not to kill the test process once
`run_test()` is finished, but to keep spinning the event loop until
our callbacks have been called and our test has completed. Newer tests
prefer the use of `add_task` rather than this method. This can be
achieved with `do_test_pending()` and `do_test_finished()`:

```js
function run_test() {
  // Tell the harness to keep spinning the event loop at least
  // until the next do_test_finished() call.
  do_test_pending();

  someAsyncProcess(function callback(result) {
    equal(result, expectedResult);

    // Close previous do_test_pending() call.
    do_test_finished();
  });
}
```

### Testing in child processeses

By default xpcshell tests run in the parent process. If you wish to run
test logic in the child, you have several ways to do it:

1. Create a regular test_foo.js test, and then write a wrapper
   test_foo_wrap.js file that uses the `run_test_in_child()` function
   to run an entire script file in the child. This is an easy way to
   arrange for a test to be run twice, once in chrome and then later
   (via the \_wrap.js file) in content. See /network/test/unit_ipc for
   examples. The `run_test_in_child()` function takes a callback, so
   you should be able to call it multiple times with different files, if
   that's useful.
2. For tests that need to run logic in both the parent + child processes
   during a single test run, you may use the poorly documented
   `sendCommand()` function, which takes a code string to be executed
   on the child, and a callback function to be run on the parent when it
   has completed. You will want to first call
   do_load_child_test_harness() to set up a reasonable test environment
   on the child. `sendCommand` returns immediately, so you will
   generally want to use `do_test_pending`/`do_test_finished` with
   it. NOTE: this method of test has not been used much, and your level
   of pain may be significant. Consider option #1 if possible.

See the documentation for `run_test_in_child()` and
`do_load_child_test_harness()` in testing/xpcshell/head.js for more
information.

### Platform-specific tests

Sometimes you might want a test to know what platform it's running on
(to test platform-specific features, or allow different behaviors). Unit
tests are not normally invoked from a Makefile (unlike Mochitests), or
preprocessed (so not #ifdefs), so platform detection with those methods
isn't trivial.

### Runtime detection

Some tests will want to only execute certain portions on specific
platforms. Use
{searchfox}`AppConstants.sys.mjs <mozilla-central/rev/5f0a7ca8968ac5cef8846e1d970ef178b8b76dcc:toolkit/modules/AppConstants.sys.mjs#158>`
for determining the platform, for example:

```js
let { AppConstants } =
  ChromeUtils.importESModule("resource://gre/modules/AppConstants.mjs");

let isMac = AppConstants.platform == "macosx";
```

## Conditionally running a test

There are two different ways to conditional skip a test, either through

### Adding conditions through the `add_task` or `add_test` function

You can use conditionals on individual test functions instead of entire
files. The condition is provided as an optional first parameter passed
into `add_task()` or `add_test()`. The condition is an object which
contains a function named `skip_if()`, which is an [arrow
function](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Functions/Arrow_functions)
returning a boolean value which is **\`\`true\`\`** if the test should be
skipped.

For example, you can provide a test which only runs on Mac OS X like
this:

```js
let { AppConstants } =
  ChromeUtils.importESModule("resource://gre/modules/AppConstants.sys.mjs");

add_task({
  skip_if: () => AppConstants.platform != "mac"
}, async function some_test() {
  // Test code goes here
});
```

Since `AppConstants.platform != "mac"` is `true` only when testing
on Mac OS X, the test will be skipped on all other platforms.

:::{note}
**Note:** Arrow functions are ideal here because if your condition
compares constants, it will already have been evaluated before the
test is even run, meaning your output will not be able to show the
specifics of what the condition is.
:::

### Adding conditions in the xpcshell.toml manifest

Sometimes you may want to add conditions to specify that a test should
be skipped in certain configurations, or that a test is known to fail on
certain platforms. You can do this in xpcshell manifests by adding
annotations below the test file entry in the manifest, for example:

```ini
[test_example.js]
skip-if = os == 'win'
```

This example would skip running `test_example.js` on Windows.

:::{note}
**Note:** Starting with Gecko (Firefox 40 / Thunderbird 40 /
SeaMonkey 2.37), you can use conditionals on individual test
functions instead of on entire files. See [Adding conditions through
the add_task or add_test
function](#adding-conditions-through-the-add_task-or-add_test-function)
above for details.
:::

There are currently four conditionals you can specify:

#### skip-if

`skip-if` tells the harness to skip running this test if the condition
evaluates to true. You should use this only if the test has no meaning
on a certain platform, or causes undue problems like hanging the test
suite for a long time.

##### run-if

`run-if` tells the harness to only run this test if the condition
evaluates to true. It functions as the inverse of `skip-if`.

#### fail-if

`fail-if` tells the harness that this test is expected to fail if the
condition is true. If you add this to a test, make sure you file a bug
on the failure and include the bug number in a comment in the manifest,
like:

```ini
[test_example.js]
# bug xxxxxx
fail-if = os == 'linux'
```

#### run-sequentially

`run-sequentially`basically tells the harness to run the respective
test in isolation. This is required for tests that are not
"thread-safe". You should do all you can to avoid using this option,
since this will kill performance. However, we understand that there are
some cases where this is imperative, so we made this option available.
If you add this to a test, make sure you specify a reason and possibly
even a bug number, like:

```ini
[test_example.js]
run-sequentially = Has to launch Firefox binary, bug 123456.
```

### Manifest conditional expressions

For a more detailed description of the syntax of the conditional
expressions, as well as what variables are available, {doc}`see this
page </mozbase/manifestparser>`.

## Running a specific test only

When working on a specific feature or issue, it is convenient to only
run a specific task from a whole test suite. Use `.only()` for that
purpose:

```js
add_task(async function some_test() {
  // Some test.
});

add_task(async function some_interesting_test() {
// Only this test will be executed.
}).only();
```

## Problems with pending events and shutdown

Events are not processed during test execution if not explicitly
triggered. This sometimes causes issues during shutdown, when code is
run that expects previously created events to have been already
processed. In such cases, this code at the end of a test can help:

```js
let thread = gThreadManager.currentThread;
while (thread.hasPendingEvents())
  thread.processNextEvent(true);
```

## Debugging xpcshell-tests

### Running unit tests under the javascript debugger via `--jsdebugger`

You can specify flags when issuing the `xpcshell-test` command that
will cause your test to stop right before running so you can attach the
{doc}`javascript debugger </devtools-user/tools_toolbox/index>`.

Example:

```bash
$ ./mach xpcshell-test --jsdebugger browser/components/tests/unit/test_browserGlue_pingcentre.js
 0:00.50 INFO Running tests sequentially.
...
 0:00.68 INFO ""
 0:00.68 INFO "*******************************************************************"
 0:00.68 INFO "Waiting for the debugger to connect on port 6000"
 0:00.68 INFO ""
 0:00.68 INFO "To connect the debugger, open a Firefox instance, select 'Connect'"
 0:00.68 INFO "from the Developer menu and specify the port as 6000"
 0:00.68 INFO "*******************************************************************"
 0:00.68 INFO ""
 0:00.71 INFO "Still waiting for debugger to connect..."
...
```

At this stage in a running Firefox instance:

- Go to the three-bar menu, then select `More tools` ->
  `Remote Debugging`
- A new tab is opened. In the Network Location box, enter
  `localhost:6000` and select `Connect`
- You should then get a link to *\`\`Main Process\`\`*, click it and the
  Developer Tools debugger window will open.
- It will be paused at the start of the test, so you can add
  breakpoints, or start running as appropriate.

If you get a message such as:

```
 0:00.62 ERROR Failed to initialize debugging: Error: resource://devtools appears to be inaccessible from the xpcshell environment.
This can usually be resolved by adding:
  firefox-appdir = browser
to the xpcshell.toml manifest.
It is possible for this to alter test behevior by triggering additional browser code to run, so check test behavior after making this change.
```

This is typically a test in core code. You can attempt to add that to
the xpcshell.toml, however as it says, it might affect how the test runs
and cause failures. Generally the firefox-appdir should only be left in
xpcshell.toml for tests that are in the browser/ directory, or are
Firefox-only.

### Running unit tests with the profiler using `--profiler`

Similarly, it's possible to run an xpcshell test with the profiler enabled.
After the test finishes running the profiler interface will be opened
automatically:

```bash
$ ./mach xpcshell-test --profiler browser/components/tests/unit/test_browserGlue_migration_osauth.js
...
 0:00.50 INFO Running tests sequentially.
...
 0:00.88 profiler INFO Symbolicating the performance profile... This could take a couple of minutes.
 0:01.93 profiler INFO Temporarily serving the profile from: http://127.0.0.1:57737/profile_test_browserGlue_migration_osauth.js.json
 0:01.93 profiler INFO Opening the profile: https://profiler.firefox.com/from-url/http%3A%2F%2F127.0.0.1%3A57737%2Fprofile_test_browserGlue_migration_osauth.js.json
...
```

### Running unit tests under a C++ debugger

#### Via `--debugger` and `--debugger-interactive`

You can specify flags when issuing the `xpcshell-test` command that
will launch xpcshell in the specified debugger (implemented in
[bug 382682](https://bugzilla.mozilla.org/show_bug.cgi?id=382682)).
Provide the full path to the debugger, or ensure that the named debugger
is in your system PATH.

Example:

```bash
$ ./mach xpcshell-test --debugger gdb --debugger-interactive netwerk/test/unit/test_resumable_channel.js
# js>_execute_test();
...failure or success messages are printed to the console...
# js>quit();
```

On Windows with the VS debugger:

```bash
./mach xpcshell-test --debugger devenv --debugger-interactive netwerk/test/test_resumable_channel.js
```

Or with WinDBG:

```bash
./mach xpcshell-test --debugger windbg --debugger-interactive netwerk/test/test_resumable_channel.js
```

Or with modern WinDbg (WinDbg Preview as of April 2020):

```bash
./mach xpcshell-test --debugger WinDbgX --debugger-interactive netwerk/test/test_resumable_channel.js
```

#### Debugging xpcshell tests in a child process

To debug the child process, where code is often being run in a project,
set MOZ_DEBUG_CHILD_PROCESS=1 in your environment (or on the command
line) and run the test. You will see the child process emit a printf
with its process ID, then sleep. Attach a debugger to the child's pid,
and when it wakes up you can debug it:

```bash
$ MOZ_DEBUG_CHILD_PROCESS=1 ./mach xpcshell-test test_simple_wrap.js
CHILDCHILDCHILDCHILD
  debug me @13476
```

#### Debug both parent and child processes

Use MOZ_DEBUG_CHILD_PROCESS=1 to attach debuggers to each process. (For
gdb at least, this means running separate copies of gdb, one for each
process.)
