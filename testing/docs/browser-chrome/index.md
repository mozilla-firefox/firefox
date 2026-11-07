Browser chrome mochitests
=========================

Browser chrome mochitests are mochitests that run in the context of the desktop
Firefox browser window. The test files are named `browser_something.js` by
convention, and in addition to mochitest assertions supports the
[CommonJS standard assertions](http://wiki.commonjs.org/wiki/Unit_Testing/1.1),
like [nodejs' assert module](https://nodejs.org/api/assert.html#assert) but
implemented in [`Assert.sys.mjs`](../assert.md).

These tests are used to test UI-related behaviour in Firefox for
Desktop. They do not run on Android. If you're testing internal code that
does not directly interact with the user interface,
[xpcshell tests](../xpcshell/index.md) are probably a better fit for your needs.


Running the tests
-----------------

You can run individual tests locally using the standard `./mach test` command:
`./mach test path/to/browser_test.js`. You can omit the path if the filename
is unique. You can also run entire directories, or specific test manifests:

```
./mach test path/to/browser.toml
```

You can also use the more specific `./mach mochitest` command in the same way.
Using `./mach mochitest --help` will give you an exhaustive overview of useful
other available flags relating to running, debugging and evaluating tests.

For both commands, you can use the `--verify` flag to run the test under
[test verification](../test-verification/index.md). This helps flush out
intermittent issues with the test.


On our infrastructure, these tests run in the mochitest-browser-chrome jobs.
There, they run on a per-manifest basis (so for most manifests, more than one
test will run while the browser stays open).

The tests also get run in `verify` mode in the `test-verify` jobs, whenever
the test itself is changed.

Note that these tests use "real" focus and input, so you'll need to not touch
your machine while running them. You can run them with the `--headless`
flag to avoid this, but some tests may break in this mode.
A headless run reads fixed look-and-feel values on Linux and the host's own on
macOS and Windows, as [Look and feel](/widget/look-and-feel.md#headless)
describes.


Adding new tests
----------------

You can use the standard `./mach addtest path/to/new/browser_test.js` command
to generate a new browser test, and add it to the relevant manifest, if tests
already exist in that directory. This automatically creates a test file using
the right template for you, and adds it to the manifest.

If there are no tests in the directory yet (for example, for an entirely new
feature and directory) you will need to:

1. create an empty `browser.toml` file
2. add it to `BROWSER_CHROME_MANIFESTS` collection from a `moz.build` file.
3. then run the `./mach addtest` command as before.

In terms of the contents of the test, please see [Writing new browser
mochitests](writing.md).

Debugging tests
---------------

The `./mach test` and `./mach mochitest` commands support a `--jsdebugger`
flag which will open the browser toolbox. If you add the
[`debugger;` keyword](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Statements/debugger)
in your test, the debugger will pause there.

Alternatively, you can set breakpoints using the debugger yourself. If you want
to pause the debugger before running the test, you can use the `--no-autorun`
flag. It's also useful to be able to switch to the Network Monitor panel before
the tests start.

Alternatively, if you want to pause the debugger on failure, you can use
`--debug-on-failure`.

For more details, see [Avoiding intermittent tests](../intermittent/index.md).

Profiling tests
---------------
The `./mach test` and `./mach mochitest` commands also support a `--profiler`
flag that will start the Firefox Profiler before running tests.

It's possible to get a profile also on try with `--profiler`:

```
./mach try fuzzy --profiler <test directory>
```

The profile will be uploaded in case of a failure only, so make sure to add
something such as `ok(false, "force profile upload");` in your test to force the
failure.


Reference material
------------------

- [Assert module](../assert.md)
- [TestUtils module](../testutils.md)
- [BrowserTestUtils module](browsertestutils.md)
- [SimpleTest utilities](../simpletest.md)
- [EventUtils utilities](../eventutils.md)

```{toctree}
:hidden:
:maxdepth: 1

browsertestutils
writing
../assert
../testutils
../simpletest
../eventutils
```
