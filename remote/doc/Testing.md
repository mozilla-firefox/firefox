# Remote protocol testing

The Remote Protocol has unit- and functional tests located under different folders:

* Marionette: `remote/marionette/`.
* Shared Modules: `remote/shared/`
* WebDriver BiDi: `remote/webdriver-bidi/`

You may want to run all the tests under a particular subfolder locally like this:

```shell
% ./mach test remote
```

## Unit tests

Because tests are run in parallel and [xpcshell] itself is quite
chatty, it can sometimes be useful to run the tests in sequence:

```shell
% ./mach xpcshell-test --sequential remote/shared/test/xpcshell
```

The unit tests will appear as part of the `X` (for _xpcshell_) jobs
on Treeherder.

[xpcshell]: /testing/xpcshell/index.md

## Browser Chrome Mochitests

We also have a set of functional browser-chrome mochitests located
under several components, ie. _remote/shared/messagehandler/test/browser_:

```shell
% ./mach mochitest remote/shared/messagehandler/test/browser/browser_*
```

The functional tests will appear under the `M` (for _mochitest_)
category in the `remote` jobs on Treeherder.

As the functional tests will sporadically pop up new Firefox
application windows, a helpful tip is to run them in headless
mode:

```shell
% ./mach mochitest --headless remote/shared/messagehandler/test/browser
```

The `--headless` flag is equivalent to setting the `MOZ_HEADLESS`
environment variable.  You can additionally use `MOZ_HEADLESS_WIDTH`
and `MOZ_HEADLESS_HEIGHT` to control the dimensions of the virtual
display.

The `add_task()` function used for writing asynchronous tests is
replaced to provide some additional test setup and teardown useful
for writing tests against the Remote Agent and the targets.

## Puppeteer tests

In addition to our own Firefox-specific tests, we run the upstream
[Puppeteer test suite] against our implementation to [track progress]
towards achieving full [Puppeteer support] in Firefox when using the
WebDriver BiDi protocol. The tests are written in the behavior-driven
testing framework [Mocha].

Puppeteer tests are vendored under _remote/test/puppeteer/_ and are
run locally like this:

```shell
% ./mach puppeteer-test
```

You can also run them against Chrome as:

```shell
% ./mach puppeteer-test --product=chrome
```

By default the mach command will automatically install Puppeteer but that's
only needed for the very first time, or when a new Puppeteer release has been
vendored in. To skip the install step use the `--no-install` option.

To run only some specific tests from the whole test suite the appropriate
test files have to be updated first. To select specific tests or test
groups within a file define [exclusive tests] by adding the `.only` suffix
like `it.only()` or `describe.only()`.

It is also possible, similar to how it works in CI, to run tests in chunks:

```shell
% ./mach puppeteer-test --this-chunk=1 --total-chunks=2
```

More customizations for [Mocha] can be found in its own documentation.

Test expectation metadata is collected in [TestExpectations.json]
via log parsing and a custom Mocha reporter under
_remote/test/puppeteer/json-mocha-reporter.cjs_. For Nightly, overrides to these expectations
are stored in [CanaryTestExpectations.json].

Check the upstream [Puppeteer test suite] documentation for instructions on
how to skip tests, run only one test or a subsuite of tests.

## Testing on Try

To schedule all the Remote Protocol tests on try, you can use the
`remote-protocol` [try preset]:

```shell
% ./mach try --preset remote-protocol
```

This preset will schedule tests related to the Remote Protocol component on
various platforms. You can reduce the number of tasks by filtering on platforms
(e.g. linux) or build type (e.g. opt):

```shell
% ./mach try --preset remote-protocol -xq "'linux 'opt"
```

But you can also schedule tests by selecting relevant jobs yourself:

```shell
% ./mach try fuzzy
```

[Puppeteer test suite]: https://github.com/puppeteer/puppeteer/blob/master/test/README.md
[Puppeteer support]: https://bugzilla.mozilla.org/show_bug.cgi?id=puppeteer
[Mocha]: https://mochajs.org/
[exclusive tests]: https://mochajs.org/#exclusive-tests
[track progress]: https://puppeteer.github.io/ispuppeteerfirefoxready/
[try preset]: /tools/try/presets.md
[TestExpectations.json]: https://searchfox.org/firefox-main/source/remote/test/puppeteer/test/TestExpectations.json
[CanaryTestExpectations.json]: https://searchfox.org/firefox-main/source/remote/test/puppeteer/test/CanaryTestExpectations.json
