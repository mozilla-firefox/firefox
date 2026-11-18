# Code coverage

## What is Code Coverage?

**Code coverage** essentially measures how often certain lines are hit,
branches taken or conditions met in a program, given some test that you
run on it.

There are two very important things to keep in mind when talking about
code coverage:

- If a certain branch of code is not hit at all while running tests,
  then those tests will never be able to find a bug in this particular
  piece of the code.
- If a certain branch of code is executed (even very often), this still
  is not a clear indication of the *quality of a test*. It could be
  that a test exercises the code but does not actually check that the
  code performs *correctly*.

As a conclusion, we can use code coverage to find areas that need (more)
tests, but we cannot use it to confirm that certain areas are well
tested.

## Firefox Code Coverage reports

We automatically run code coverage builds and tests on all
mozilla-central runs, for Linux and Windows. C/C++, Rust and JavaScript
are supported.

The generated reports can be found on Searchfox. The
reports can be filtered by platform and/or test suite.

We also generate a report of all totally uncovered files, which can be
found at <https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/project.relman.code-coverage.production.cron.latest/artifacts/public/zero_coverage_report.json>. You can use this to find
areas of code that should be tested, or code that is no longer used
(dead code, which could be removed).

## C/C++ Code Coverage on Firefox

There are several ways to get C/C++ coverage information for
Firefox, including creating your own coverage builds. The next
sections describe the available options.

### Generate Code Coverage report from a try build (or any other CI build)

To spin a code coverage build, you need to select the linux64-ccov
platform (use --full when using the fuzzy selector to get the ccov
builds to show up).

E.g. for a try build:

```shell
./mach try fuzzy -q 'linux64-ccov'
```

There are two options now, you can either generate the report locally or
use a one-click loaner.

#### Generate report using a one-click loaner

Select the B job on Treeherder and get a one-click loaner.

In the loaner, download and execute the script
<https://github.com/mozilla/code-coverage/blob/master/report/firefox_code_coverage/codecoverage.py>:

```shell
wget https://raw.githubusercontent.com/mozilla/code-coverage/master/report/firefox_code_coverage/codecoverage.py
python codecoverage.py
```

This command will automatically generate a HTML report of the code
coverage information in the **report** subdirectory in your current
working directory.

#### Generate report locally

Given a treeherder linux64-ccov build (with its branch, e.g.
\`mozilla-central\` or \`try\`, and revision, the tip commit hash of your
push), run the following command:

```shell
./mach coverage-report --branch BRANCH --revision REVISION
```

This command will automatically download code coverage artifacts from
the treeherder build and generate an HTML report of the code coverage
information. The report will be stored in the **ccov-report** subdirectory in
your current working directory.

### Creating your own Coverage Build

On Linux, Windows and Mac OS X it is straightforward to generate an
instrumented build using GCC or Clang. Adding the following lines to
your `.mozconfig` file should be sufficient:

```shell
# Enable code coverage
ac_add_options --enable-coverage

# Needed for e10s:
# With the sandbox, content processes can't write updated coverage counters in the gcda files.
ac_add_options --disable-sandbox
```

Some additional options might be needed, check the code-coverage
mozconfigs used on CI to be sure:
browser/config/mozconfigs/linux64/code-coverage,
browser/config/mozconfigs/win64/code-coverage,
browser/config/mozconfigs/macosx64/code-coverage.

Make sure you are not running with {doc}`artifact build </contributing/build/artifact_builds>`
enabled, as it can prevent coverage artifacts from being created.

You can then create your build as usual. Once the build is complete, you
can run any tests/tools you would like to run and the coverage data gets
automatically written to special files. In order to view/process this
data, we recommend using the
[grcov](https://github.com/mozilla/grcov) tool, a tool to manage and
visualize gcov results. You can also use the same process explained
earlier for CI builds.

### Debugging Failing Tests on the Try Server

When code coverage is run through a push to try, all the data that is
created is ingested by ActiveData and processed into a different data
format for analysis. Anytime a code coverage run generates \*.gcda and
\*.gcno files, ActiveData starts working. Now, sometimes, a test will
permanently fail when it is running on a build that is instrumented with
GCOV. To debug these issues without overloading ActiveData with garbage
coverage data, open the file
{searchfox}`taskcluster/gecko_taskgraph/transforms/test/__init__.py <taskcluster/gecko_taskgraph/transforms/test/__init__.py#516>`
and add the following line,

```python
test["mozharness"].setdefault("extra-options", []).append("--disable-ccov-upload")
```

right after this line of code:

```python
test["mozharness"].setdefault("extra-options", []).append("--code-coverage")
```

Now when you push to try to debug some failing tests, or anything else,
there will not be any code coverage artifacts uploaded from the build
machines or from the test machines.

## JavaScript Code Coverage on Firefox

### Generate Per Test Code Coverage Locally

To generate the LCov files containing coverage information locally,
simply add an extra environment variable called `JS_CODE_COVERAGE_OUTPUT_DIR`
which accepts a directory as it's input and stores the resulting data in that
directory. For example, to collect code coverage for the entire Mochitest suite:

```shell
JS_CODE_COVERAGE_OUTPUT_DIR=/PATH/TO/COVERAGE/DIR/ ./mach test path/to/test/folder/or/file
```

Unfortunately, these lcov files aren't usable as-is because the JS files
are going to be referenced by their runtime URLs (chrome://... resource://...)
instead of local source file path.
You should first run the following command to retrieve a map of URLs to files:

```shell
./mach build-backend -b ChromeMap
```

This will create a `chrome-map.json` file in your local build objdir.

And then this other command to process the LCov files and convert URLs to paths

```shell
./mach python -m mozbuild.codecoverage.lcov_rewriter --chrome-map-path /YOUR/OBJ-DIR/chrome-map.json /PATH/TO/COVERAGE/DIR/*.info --output-file coverage.info
```

This will aggregate all the JSON files into a unique one called `coverage.info`.

Finally, you can either use your IDE of preference and load the lcov files via some extension.
Or use the following last command and cli rust tool (`grcov`) to generate an HTML report.

```shell
# Either install grcov via cargo
cargo install grcov

# -or- find the existing build in your "mozbuild" folder, typically in:
~/.mozbuild/grcov/grcov
# "./mach environment" can help you find this mozbuild folder (this is the "state directory")

grcov coverage.info -t html --source-dir /PATH/TO/FIREFOX/CHECKOUT/
```

You can then open html/index.html in Firefox and browse through all the results nicely.
