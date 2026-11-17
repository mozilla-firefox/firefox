# Managing lists of tests

```{eval-rst}
.. py:currentmodule:: manifestparser
```

We don't always want to run all tests, all the time. Sometimes a test
may be broken, in other cases we only want to run a test on a specific
platform or build of Mozilla. To handle these cases (and more), we
created a python library to create and use test "manifests", which
codify this information.

## Update for August 2023: Transition to TOML for manifestparser

As of August 2023, manifestparser will be transitioning from INI format
configuration files to TOML. The new TOML format will better support
future continuous integration automation and has a much more
precise syntax (FFI see [Bug 1821199](https://bugzilla.mozilla.org/show_bug.cgi?id=1821199)).
During the migration period both `*.ini` files and
`*.toml` files will be supported. If an INI config file is specified
(e.g. in `moz.build`) and a TOML file is present, the TOML file will be
used.

### {mod}`manifestparser` --- Create and manage test manifests

manifestparser lets you easily create and use test manifests, to
control which tests are run under what circumstances.

What manifestparser gives you:

- manifests are ordered lists of tests
- tests may have an arbitrary number of key, value pairs
- the parser returns an ordered list of test data structures, which
  are just dicts with some keys. For example, a test with no
  user-specified metadata looks like this:

```text
[{'expected': 'pass',
  'path': '/home/mozilla/mozmill/src/manifestparser/manifestparser/tests/testToolbar/testBackForwardButtons.js',
  'relpath': 'testToolbar/testBackForwardButtons.js',
  'name': 'testBackForwardButtons.js',
  'here': '/home/mozilla/mozmill/src/manifestparser/manifestparser/tests',
  'manifest': '/home/mozilla/mozmill/src/manifestparser/manifestparser/tests/manifest.toml',}]
```

The keys displayed here (path, relpath, name, here, and manifest) are
reserved keys for manifestparser and any consuming APIs. You can add
additional key, value metadata to each test.

## Why have test manifests?

It is desirable to have a unified format for test manifests for testing
[mozilla-central](https://github.com/mozilla-firefox/firefox), etc.

- It is desirable to be able to selectively enable or disable tests based on platform or other conditions. This should be easy to do. Currently, since many of the harnesses just crawl directories, there is no effective way of disabling a test except for removal from mozilla-central
- It is desriable to do this in a universal way so that enabling and disabling tests as well as other tasks are easily accessible to a wider audience than just those intimately familiar with the specific test framework.
- It is desirable to have other metadata on top of the test. For instance, let's say a test is marked as skipped. It would be nice to give the reason why.

Most Mozilla test harnesses work by crawling a directory structure.
While this is straight-forward, manifests offer several practical
advantages:

- ability to turn a test off easily: if a test is broken on m-c
  currently, the only way to turn it off, generally speaking, is just
  removing the test. Often this is undesirable, as if the test should
  be dismissed because other people want to land and it can't be
  investigated in real time (is it a failure? is the test bad? is no
  one around that knows the test?), then backing out a test is at best
  problematic. With a manifest, a test may be disabled without
  removing it from the tree and a bug filed with the appropriate
  reason:

```text
["test_broken.js"]
disabled = "https://bugzilla.mozilla.org/show_bug.cgi?id=123456"
```

- ability to run different (subsets of) tests on different
  platforms. Traditionally, we've done a bit of magic or had the test
  know what platform it would or would not run on. With manifests, you
  can mark what platforms a test will or will not run on and change
  these without changing the test.

```text
["test_works_on_windows_only.js"]
skip-if = ["os != 'win'"]
```

- ability to markup tests with metadata. We have a large, complicated,
  and always changing infrastructure. key, value metadata may be used
  as an annotation to a test and appropriately curated and mined. For
  instance, we could mark certain tests as randomorange with a bug
  number, if it were desirable.
- ability to have sane and well-defined test-runs. You can keep
  different manifests for different test runs and `["include:FILENAME.toml"]`
  (sub)manifests as appropriate to your needs.

## Manifest Format

Manifests are `*.toml` (formerly `*.ini`) files with the section names denoting the path
relative to the manifest:

```text
["foo.js"]
["bar.js"]
["fleem.js"]
```

The sections are read in order. In addition, tests may include
arbitrary key, value metadata to be used by the harness. You may also
have a `[DEFAULT]` section that will give key, value pairs that will
be inherited by each test unless overridden:

```text
[DEFAULT]
type = "restart"

["lilies.js"]
color = "white"

["daffodils.js"]
color = "yellow"
type = "other"
# override type from DEFAULT

["roses.js"]
color = "red"
```

You can also include other manifests:

```text
["include:subdir/anothermanifest.toml"]
```

And reference parent manifests to inherit keys and values from the DEFAULT
section, without adding possible included tests.

```text
["parent:../manifest.toml"]
```

Manifests are included relative to the directory of the manifest with
the `[include:]` directive unless they are absolute paths.

By default you can use '#' as a comment character. Comments can start a
new line, or be inline.

```text
["roses.js"]
# a valid comment
color = "red" # another valid comment
```

Because in TOML all values must be quoted there is no risk of an anchor in
an URL being interpreted as a comment.

```text
["test1.js"]
url = "https://foo.com/bar#baz" # Bug 1234
```

## Manifest Conditional Expressions

The conditional expressions used in manifests are parsed using the *ExpressionParser* class.

```{eval-rst}
.. autoclass:: manifestparser.ExpressionParser
```

Consumers of this module are expected to pass in a value dictionary
for evaluating conditional expressions. A common pattern is to pass
the dictionary from the {mod}`mozinfo` module.

## Data

Manifest Destiny gives tests as a list of dictionaries (in python
terms).

- path: full path to the test
- relpath: relative path starting from the root directory. The root directory
  : is typically the location of the root manifest, or the source
  repository. It can be specified at runtime by passing in `rootdir`
  to `TestManifest`. Defaults to the directory containing the test's
  ancestor manifest.
- name: file name of the test
- here: the parent directory of the manifest
- manifest: the path to the manifest containing the test

This data corresponds to a one-line manifest:

```text
["testToolbar/testBackForwardButtons.js"]
```

If additional key, values were specified, they would be in this dict
as well.

Outside of the reserved keys, the remaining key, values
are up to convention to use. There is a (currently very minimal)
generic integration layer in manifestparser for use of all harnesses,
`manifestparser.TestManifest`.
For instance, if the 'disabled' key is present, you can get the set of
tests without disabled (various other queries are doable as well).

Since the system is convention-based, the harnesses may do whatever
they want with the data. They may ignore it completely, they may use
the provided integration layer, or they may provide their own
integration layer. This should allow whatever sort of logic is
desired. For instance, if in yourtestharness you wanted to run only on
mondays for a certain class of tests:

```text
tests = []
for test in manifests.tests:
    if 'runOnDay' in test:
       if calendar.day_name[calendar.weekday(*datetime.datetime.now().timetuple()[:3])].lower() == test['runOnDay'].lower():
           tests.append(test)
    else:
       tests.append(test)
```

To recap:

- the manifests allow you to specify test data
- the parser gives you this data
- you can use it however you want or process it further as you need

Tests are denoted by sections in an `*.toml` file (see
{searchfox}`testing/mozbase/manifestparser/tests/manifest.toml`
).

Additional manifest files may be included with an `[include:]` directive:

```text
["include:path-to-additional-file-manifest.toml"]
```

The path to included files is relative to the current manifest.

The `[DEFAULT]` section contains variables that all tests inherit from.

Included files will inherit the top-level variables but may override
in their own `[DEFAULT]` section.

## manifestparser Architecture

There is a two- or three-layered approach to the manifestparser
architecture, depending on your needs:

1\. ManifestParser: this is a generic parser for `*.toml` manifests that
facilitates the `[include:]` logic and the inheritance of
metadata. Despite the internal variable being called `self.tests`
(an oversight), this layer has nothing in particular to do with tests.

2\. TestManifest: this is a harness-agnostic integration layer that is
test-specific. TestManifest facilitates `skip-if` logic.

3\. Optionally, a harness will have an integration layer than inherits
from TestManifest if more harness-specific customization is desired at
the manifest level.

See the source code at
{searchfox}`testing/mozbase/manifestparser`
.

## Filtering Manifests

After creating a `TestManifest` object, all manifest files are read and a list
of test objects can be accessed via `TestManifest.tests`. However this list contains
all test objects, whether they should be run or not. Normally they need to be
filtered down only to the set of tests that should be run by the test harness.

To do this, a test harness can call `TestManifest.active_tests`:

```python
tests = manifest.active_tests(exists=True, disabled=True, **tags)
```

By default, `active_tests` runs the filters found in
{attr}`~.DEFAULT_FILTERS`. It also accepts two convenience arguments:

1. `exists`: if True (default), filter out tests that do not exist on the local file system.
2. `disabled`: if True (default), do not filter out tests containing the 'disabled' key
   (which can be set by `skip-if` manually).

This works for simple cases, but there are other built-in filters, or even custom filters
that can be applied to the `TestManifest`. To do so, add the filter to `TestManifest.filters`:

```python
from manifestparser.filters import subsuite
import mozinfo

filters = [subsuite("devtools")]
tests = manifest.active_tests(filters=filters, **mozinfo.info)
```

```{eval-rst}
.. automodule:: manifestparser.filters
    :members:
    :exclude-members: filterlist,InstanceFilter,DEFAULT_FILTERS
```


```{eval-rst}
.. autodata::  manifestparser.filters.DEFAULT_FILTERS
    :annotation:
```

For example, suppose we want to introduce a new key called `timeout-if` that adds a
'timeout' property to a test if a certain condition is True. The syntax in the manifest
files will look like this:

```text
["test_foo.py"]
timeout-if = ["300, os == 'win'"]
```

The value is \<timeout>, \<condition> where condition is the same format as the one in
`skip-if`. In the above case, if os == 'win', a timeout of 300 seconds will be
applied. Otherwise, no timeout will be applied. All we need to do is define the filter
and add it:

```python
from manifestparser.expression import parse
import mozinfo


def timeout_if(tests, values):
    for test in tests:
        if "timeout-if" in test:
            timeout, condition = test["timeout-if"].split(",", 1)
            if parse(condition, **values):
                test["timeout"] = timeout
        yield test


tests = manifest.active_tests(filters=[timeout_if], **mozinfo.info)
```

## CLI

**NOTE:** *The manifestparser CLI is currently being updated to support TOML.*

Run `manifestparser help` for usage information.

To create a manifest from a set of directories:

```text
manifestparser [options] create directory <directory> <...> [create-options]
```

To output a manifest of tests:

```text
manifestparser [options] write manifest <manifest> <...> -tag1 -tag2 --key1=value1 --key2=value2 ...
```

To copy tests and manifests from a source:

```text
manifestparser [options] copy from_manifest to_manifest -tag1 -tag2 `key1=value1 key2=value2 ...
```

To update the tests associated with a manifest from a source
directory:

```text
manifestparser [options] update manifest from_directory -tag1 -tag2 --key1=value1 --key2=value2 ...
```

## Creating Manifests

manifestparser comes with a console script, `manifestparser create`, that
may be used to create a seed manifest structure from a directory of
files. Run `manifestparser help create` for usage information.

## Copying Manifests

To copy tests and manifests from a source:

```text
manifestparser [options] copy from_manifest to_directory -tag1 -tag2 `key1=value1 key2=value2 ...
```

## Updating Tests

To update the tests associated with a manifest from a source
directory:

```text
manifestparser [options] update manifest from_directory -tag1 -tag2 `key1=value1 `key2=value2 ...
```

## Usage example

Here is an example of how to create manifests for a directory tree and
update the tests listed in the manifests from an external source.

## Creating Manifests

Let's say you want to make a series of manifests for a given directory structure containing `.js` test files:

```text
testing/mozmill/tests/firefox/
testing/mozmill/tests/firefox/testAwesomeBar/
testing/mozmill/tests/firefox/testPreferences/
testing/mozmill/tests/firefox/testPrivateBrowsing/
testing/mozmill/tests/firefox/testSessionStore/
testing/mozmill/tests/firefox/testTechnicalTools/
testing/mozmill/tests/firefox/testToolbar/
testing/mozmill/tests/firefox/restartTests
```

You can use `manifestparser create` to do this:

```text
$ manifestparser help create
Usage: manifestparser.py [options] create directory <directory> <...>

     create a manifest from a list of directories

Options:
  -p PATTERN, `pattern=PATTERN
                        glob pattern for files
  -i IGNORE, `ignore=IGNORE
                        directories to ignore
  -w IN_PLACE, --in-place=IN_PLACE
                        Write .ini files in place; filename to write to
```

We only want `.js` files and we want to skip the `restartTests` directory.
We also want to write a manifest per directory, so I use the `--in-place`
option to write the manifests:

```text
manifestparser create . -i restartTests -p '*.js' -w manifest.ini
```

This creates a manifest.ini per directory that we care about with the JS test files:

```text
testing/mozmill/tests/firefox/manifest.ini
testing/mozmill/tests/firefox/testAwesomeBar/manifest.ini
testing/mozmill/tests/firefox/testPreferences/manifest.ini
testing/mozmill/tests/firefox/testPrivateBrowsing/manifest.ini
testing/mozmill/tests/firefox/testSessionStore/manifest.ini
testing/mozmill/tests/firefox/testTechnicalTools/manifest.ini
testing/mozmill/tests/firefox/testToolbar/manifest.ini
```

The top-level `manifest.ini` merely has `[include:]` references to the sub manifests:

```text
[include:testAwesomeBar/manifest.ini]
[include:testPreferences/manifest.ini]
[include:testPrivateBrowsing/manifest.ini]
[include:testSessionStore/manifest.ini]
[include:testTechnicalTools/manifest.ini]
[include:testToolbar/manifest.ini]
```

Each sub-level manifest contains the (`.js`) test files relative to it.

## Updating the tests from manifests

You may need to update tests as given in manifests from a different source directory.
`manifestparser update` was made for just this purpose:

```text
Usage: manifestparser [options] update manifest directory -tag1 -tag2 `key1=value1 --key2=value2 ...

    update the tests as listed in a manifest from a directory
```

To update from a directory of tests in `~/mozmill/src/mozmill-tests/firefox/` run:

```text
manifestparser update manifest.ini ~/mozmill/src/mozmill-tests/firefox/
```

## Tests

manifestparser includes a suite of tests.

`test_manifest.txt` is a doctest that may be helpful in figuring out
how to use the API. Tests are run via `mach python-test testing/mozbase/manifestparser`.

## Using mach manifest skip-fails

The first of the `mach manifest` subcommands is `skip-fails`. This command
can be used to *automatically* edit manifests to skip tests that are failing
as well as file the corresponding bugs for the failures. This is particularly
useful when "greening up" a new platform.

You may verify the proposed changes from `skip-fails` output and examine
any local manifest changes with `git status`.

Be careful, this command will create bugs in Bugzilla. To avoid this, add
`--bugzilla disable` to your command.

Here is the usage:

```text
$ ./mach manifest skip-fails --help
usage: mach [global arguments] manifest skip-fails [command arguments]

Sub Command Arguments:
try_url               Treeherder URL for try (please use quotes)
-b BUGZILLA, --bugzilla BUGZILLA
                        Bugzilla instance (or disable)
-m META_BUG_ID, --meta-bug-id META_BUG_ID
                        Meta Bug id
-s, --turbo           Skip all secondary failures
-t SAVE_TASKS, --save-tasks SAVE_TASKS
                        Save tasks to file
-T USE_TASKS, --use-tasks USE_TASKS
                        Use tasks from file
-f SAVE_FAILURES, --save-failures SAVE_FAILURES
                        Save failures to file
-F USE_FAILURES, --use-failures USE_FAILURES
                        Use failures from file
-M MAX_FAILURES, --max-failures MAX_FAILURES
                        Maximum number of failures to skip (-1 == no limit)
-v, --verbose         Verbose mode
-d, --dry-run         Determine manifest changes, but do not write them
$
```

### `try_url` --- Treeherder URL

This is the url (usually in single quotes) from running tests in try, for example:
'<https://treeherder.mozilla.org/jobs?repo=try&revision=babc28f495ee8af2e4f059e9cbd23e84efab7d0d>'

### `--bugzilla BUGZILLA` --- Bugzilla instance

By default the Bugzilla instance is `bugzilla.allizom.org`, but you may set it on the command
line to another value such as `bugzilla.mozilla.org` (or by setting the environment variable
`BUGZILLA`).

### `--meta-bug-id META_BUG_ID` --- Meta Bug id

Any new bugs that are filed will block (be dependents of) this "meta" bug (optional).

### `--turbo` --- Skip all secondary failures

The default `skip-fails` behavior is to skip only the first failure (for a given label) for each test.
In `turbo` mode, all failures for this manifest + label will skipped.

### `--save-tasks SAVE_TASKS` --- Save tasks to file

This feature is primarily for `skip-fails` development and debugging.
It will save the tasks (downloaded via mozci) to the specified JSON file
(which may be used in a future `--use-tasks` option)

### `--use-tasks USE_TASKS` --- Use tasks from file

This feature is primarily for `skip-fails` development and debugging.
It will uses the tasks from the specified JSON file (instead of downloading them via mozci).
See also `--save-tasks`.

### `--save-failures SAVE_FAILURES` --- Save failures to file

This feature is primarily for `skip-fails` development and debugging.
It will save the failures (calculated from the tasks) to the specified JSON file
(which may be used in a future `--use-failures` option)

### `--use-failures USE_FAILURES` --- Use failures from file

This feature is primarily for `skip-fails` development and debugging.
It will uses the failures from the specified JSON file (instead of downloading them via mozci).
See also `--save-failures`.

### `--max-failures MAX_FAILURES` --- Maximum number of failures to skip

This feature is primarily for `skip-fails` development and debugging.
It will limit the number of failures that are skipped (default is -1 == no limit).

### `--verbose` --- Verbose mode

Increase verbosity of output.

### `--dry-run` --- Dry run

In dry run mode, the manifest changes (and bugs top be filed) are determined, but not written.

## Bugs

Please file any bugs or feature requests at

<https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=ManifestParser>

Or contact in #cia on irc.mozilla.org

## Design Considerations

Contrary to some opinion, manifestparser.py and the associated `*.toml`
format were not magically plucked from the sky but were descended upon
through several design considerations.

- test manifests should be ordered. The current `*.toml` format supports
  this (as did the `*.ini` format)
- the manifest format should be easily human readable/writable And
  programmatically editable. While the `*.ini` format worked for a long
  time the underspecified syntax made it difficult to reliably parse.
  The new `*.toml` format is widely accepted, as a formal syntax as well
  as libraries to read and edit it (e.g. `tomlkit`).
- there should be a single file that may easily be
  transported. Traditionally, test harnesses have lived in
  mozilla-central. This is less true these days and it is increasingly
  likely that more tests will not live in mozilla-central going
  forward. So `manifestparser.py` should be highly consumable. To
  this end, it is a single file, as appropriate to mozilla-central,
  which is also a working python package deployed to PyPI for easy
  installation.

## Historical Reference

Date-ordered list of links about how manifests came to be where they are today:

```
* https://wiki.mozilla.org/Auto-tools/Projects/UniversalManifest
* http://alice.nodelman.net/blog/post/2010/05/
* http://alice.nodelman.net/blog/post/universal-manifest-for-unit-tests-a-proposal/
* https://elvis314.wordpress.com/2010/07/05/improving-personal-hygiene-by-adjusting-mochitests/
* https://elvis314.wordpress.com/2010/07/27/types-of-data-we-care-about-in-a-manifest/
* https://bugzilla.mozilla.org/show_bug.cgi?id=585106
* http://elvis314.wordpress.com/2011/05/20/converting-xpcshell-from-listing-directories-to-a-manifest/
* https://bugzilla.mozilla.org/show_bug.cgi?id=616999
* https://developer.mozilla.org/en/Writing_xpcshell-based_unit_tests#Adding_your_tests_to_the_xpcshell_manifest
* https://bugzilla.mozilla.org/show_bug.cgi?id=1821199
```
