# Running Tests for the new about:debugging

## Tests overview

Tests are located in `devtools/client/aboutdebugging/test`. There are two subfolders, `browser` and `xpcshell`. `browser` contains our [browser chrome mochitests](https://firefox-source-docs.mozilla.org/testing/browser-chrome/). Most of our tests are browser mochitests. `xpcshell` contains our [xpc-shell unit tests](https://firefox-source-docs.mozilla.org/testing/xpcshell/). At the moment of writing we only have one.

## Test coverage

You can get some code coverage information at https://codecov.io/gh/mozilla/gecko-dev/tree/master/devtools/client/aboutdebugging/src . The service is sometimes very slow, be patient! You might have to reload the page several times to get a result.

## Running tests

To run tests, you can use `./mach test {path}`. The path argument can be:
- relative/absolute path to a single test file: will run only this test
- relative/absolute path to a folder: will run all tests in the folder
- just a string: will match all the tests that contain this string

A few examples below:

```
# Run browser_aboutdebugging_addons_manifest_url.js only

./mach test devtools/client/aboutdebugging/test/browser/browser_aboutdebugging_addons_manifest_url.js

# or

./mach test browser_aboutdebugging_addons_manifest_url.js
```

```
# Run all aboutdebugging tests

./mach test devtools/client/aboutdebugging/test/browser/

# or (this works because all our tests start with "browser_aboutdebugging...")

./mach test browser_aboutdebugging
```

Having consistent names for our tests can be very helpful to quickly run subset of tests:
```
# Run all sidebar tests (will just run all the tests that start with browser_aboutdebugging_sidebar)

./mach test browser_aboutdebugging_sidebar
```

## Troubleshooting

### Fix the error "ADB process is already running"

Some tests for about:debugging rely on starting and stopping the ADB (android debug bridge) process. However if the process is already running on your machine, the tests have no way to proceed and will fail with the message:

```
Error: The ADB process is already running on this machine, it should be stopped before running this test
```

In this case try to kill the process named `adb` in your process manager. If the adb process keeps coming back up, there must be an application that spawns the process. It might be a Firefox instance. Stop all your Firefox instances, then kill the `adb` process again and restart Firefox. (Note that in theory we should always stop adb correctly, but it seems there are still scenarios where this doesn't happen).

### Pause a test

If a test is not behaving as expected, it can be helpful to pause it at a certain step to have the time to investigate. You can add an await such as:

```
await new Promise(r => setTimeout(r, TIME)); // eg, replace TIME by 60000 to wait for 1 minute
```

Note that if you really need to wait for a long time, tests will timeout after some time and shutdown automatically. To avoid that, call `requestLongerTimeout(N);` somewhere in your test. `requestLongerTimeout()` takes an integer factor that is a multiplier for the the default 45 seconds timeout. So a factor of 2 means: Wait for at last 90s.

### Attach a JS debugger to mochitests

You can set debug tests with the DevTools debugger by passing the `--jsdebugger` argument to your tests.

At the moment, you need to use `./mach mochitest` instead of `./mach test`, because of [Bug 1519369](https://bugzilla.mozilla.org/show_bug.cgi?id=1519369). This command is less flexible than `./mach test` so you will need to absolutely pass a relative path here.

```
./mach mochitest relative/path/to/test.js --jsdebugger
```

This will open a browser toolbox, with the debugger selected, before starting your test. Feel free to browse the files in the debugger and to add breakpoints. However your file is most likely not loaded yet, so the best is usually to add `debugger` statements in your code directly.

This time the tests will wait for you to click on the "Browser chrome test" window to start. Do not be fooled by the "Run all tests" button on this window, clicking anywhere in the window will actually start the tests.

## Other Tips

### Headless mode

Headless mode allows to run tests without opening a Firefox window and therefore blocking your computer.

```
./mach test browser_aboutdebugging --headless
```

### Memory leaks and Debug mode

Running tests in debug mode is simply done by using a debug build (build with `ac_add_options --enable-debug`). The added value of debug mode is that it will also assert leaks. It can be very useful to run tests in debug mode if you modified things related to event listeners for instance, and you are not sure if you are cleanly removing all the listeners.

### Test verify mode

The test-verify mode - shortened as "TV" on our continuous integration platforms - will run a single test in a loop with some different flavors. The intent is to make it easier to catch intermittents. If you added or modified a test significantly, it is usually a good idea, to run it in test-verify mode at least once.

```
# Keeping the --headless argument, because the tests can be pretty slow
./mach test browser_aboutdebugging_addons_manifest_url.js --headless --test-verify
```

## Try server

You can push your local changesets to our remote continuous integration server, try. This is useful if you made some significant changes and you would like to make sure nothing is broken in the whole DevTools tests suite, on any platform.

There are many topics to cover here, but none are specific to about:debugging. Here are a few pointers:
- [Try overview](https://firefox-source-docs.mozilla.org/tools/try/index.html)
- [Selectors integrated with ./mach](https://firefox-source-docs.mozilla.org/tools/try/selectors/index.html)
- [Try syntax selector](https://firefox-source-docs.mozilla.org/tools/try/selectors/syntax.html)
- [Try fuzzy selector](https://firefox-source-docs.mozilla.org/tools/try/selectors/fuzzy.html)

Below is an example of pushing to try using the try syntax selector. As the documentation says, this syntax is obscure and can be difficult to remember, but it is still widely used by Firefox developers.

```
./mach try -b do -p linux64 -u xpcshell,mochitest-dt,mochitest-chrome --artifact
```

Refer to the [try syntax documentation](https://firefox-source-docs.mozilla.org/tools/try/selectors/syntax.html) to learn what the various parameters mean.

Note that you need committer access level 1 in order to push to try.
