# Vendoring Puppeteer

As mentioned in the chapter on [Testing], we run the full [Puppeteer
test suite] on try.  These tests are vendored in central under
_remote/test/puppeteer/_ and we have a script to pull in upstream changes.

We periodically perform a manual two-way sync. Below is an outline of the
process interspersed with some tips.

## Check for not-yet upstreamed changes

Before vendoring a new Puppeteer release make sure that there are no Puppeteer
specific changes in the Firefox repository that haven't been upstreamed yet
since the last vendor happened. Run the following command and check the listed
bugs or related upstream code to verify:

```shell
% git log remote/test/puppeteer
```

If an upstream pull request is needed please see their [contributing.md].
Typically, the changes we push to Puppeteer include unskipping newly passing
unit tests for Firefox along with minor fixes to the tests or
to Firefox-specific browser-fetching and launch code.

Be sure to [run tests against both Chromium and Firefox] in the Puppeteer
repo. You can specify your local Firefox build when you do so:

```shell
% BINARY=<path-to-objdir-binary> npm run test:firefox
% BINARY=<path-to-objdir-binary> npm run test:firefox:bidi
```

## Prepare the Puppeteer Repository

Clone the Puppeteer git repository and checkout the release tag you want
to vendor into the Firefox repository.

```shell
% git checkout tags/puppeteer-%version%
```

You might want to [install the project] at this point and make sure unit tests pass.
Check the project's `package.json` for relevant testing commands.

## Update the Puppeteer code in the Firefox repository

You can run the following mach command to copy over the Puppeteer branch you
just prepared. The mach command has flags to specify a local or remote
repository as well as a commit:

```shell
% ./mach remote vendor-puppeteer --commitish puppeteer-%version% [--repository %path%]
```

By default, this command also installs the newly-pulled Puppeteer package in
order to generate a new `package-lock.json` file for the purpose of pinning
Puppeteer dependencies for our CI. There is a `--no-install` option if you want
to skip this step; for example, if you want to run installation separately at
a later point.

Validate that newly created files and folders are required to be tracked by
version control. If that is not the case then update the top-level
`remote/.gitignore` file for those paths.

### Apply recurring patches

Due to some discrepancies between the Firefox repository and the upstream puppeteer
repository, some patches need to be manually applied every time we vendor a new
version.

All patches should be located under `remote/test/puppeteer-patches`, and can be
applied with `git apply`:

```shell
% git apply -3 remote/test/puppeteer-patches/skip-aria-test.patch
% git commit -a -m "Bug XXXXXXX - [puppeteer] Skip test \"\$\$eval should handle many elements\" which is causing the error summary log to overflow"
% git apply -3 remote/test/puppeteer-patches/skip-mozilla-ci-incompatible-tests.patch
% git commit -a -m "Bug XXXXXXX - [puppeteer] Disable all puppeteer tests incompatible with Mozilla CI"
```

### Validate that the new code works

Use `./mach puppeteer-test` (see [Testing]) to run Puppeteer tests against both
Chromium and Firefox in headless mode. Again, only running a subset of tests
against Firefox is fine -- at this point you just want to check that the
typescript compiles and the browser binaries are launched successfully.

If something at this stage fails, you might want to check changes in
`remote/test/puppeteer/package.json` and update `remote/mach_commands.py`
with new npm scripts.

### Verify the expectation meta data

Next, you want to make sure that the expectation meta data is correct. Check
changes in [TestExpectations.json]. If there are
newly skipped tests for Firefox, you might need to update these expectations.
To do this, run the Puppeteer test job on try (see [Testing]). If these tests
are specific for Chrome or time out, we want to keep them skipped, if they fail
we want to have `FAIL` status for all platforms in the expectation meta data.
You can see, if the meta data needs to be updated, at the end of the log file.
Nightly-specific overrides to the test expectations are stored in [CanaryTestExpectations.json].

Examine the job logs and make sure the run didn't get interrupted early by a
crash or a hang, especially if you see a lot of `TEST-UNEXPECTED-MISSING` in
the Treeherder Failure Summary. You might have to fix some new bug in the unit
tests. This is the fun part.

Some tests can also unexpectedly pass. Make sure it's correct, and if needed
update the expectation data by following the instructions at the end of the
log file.

### Submit the code changes

Once you are happy with the metadata and are ready to submit the sync patch
up for review, run the Puppeteer test job on try again with `--rebuild 10`
to check for stability.

[Testing]: Testing.md
[Puppeteer test suite]: https://github.com/GoogleChrome/puppeteer/tree/master/test
[install the project]: https://github.com/puppeteer/puppeteer/blob/main/docs/contributing.md#getting-started
[run tests against both Chromium and Firefox]: https://github.com/puppeteer/puppeteer/blob/main/test/README.md#running-tests
[TestExpectations.json]: https://searchfox.org/firefox-main/source/remote/test/puppeteer/test/TestExpectations.json
[CanaryTestExpectations.json]: https://searchfox.org/firefox-main/source/remote/test/puppeteer/test/CanaryTestExpectations.json
[contributing.md]: https://github.com/puppeteer/puppeteer/blob/main/docs/contributing.md
