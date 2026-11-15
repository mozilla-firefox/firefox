(test-manifests)=

# Test Manifests

Many test suites have their test metadata defined in files called
**test manifests**.

Test manifests are divided into two flavors: {ref}`manifestparser-manifests`
and {ref}`reftest-manifests`.

## Naming Convention

The build system does not enforce file naming for test manifest files.
However, the following convention is used.

mochitest.toml

: For the *plain* flavor of mochitests.

chrome.toml

: For the *chrome* flavor of mochitests.

browser.toml

: For the *browser chrome* flavor of mochitests.

a11y.toml

: For the *a11y* flavor of mochitests.

xpcshell.toml

: For *xpcshell* tests.

(manifestparser-manifests)=

## ManifestParser Manifests

ManifestParser manifests are essentially toml files that conform to a basic
set of assumptions.

The {doc}`reference documentation </mozbase/manifestparser>`
for manifestparser manifests describes the basic format of test manifests.

In summary, manifests are toml files with section names describing test files:

```
["test_foo.js"]
["test_bar.js"]
```

Keys under sections can hold metadata about each test:

```
["test_foo.js"]
skip-if = ["os == 'win'"]
["test_foo.js"]
skip-if = ["os == 'linux' && debug"]
["test_baz.js"]
fail-if = [
  "os == 'mac'",
  "os == 'android'",
]
```

There is a special **DEFAULT** section whose keys/metadata apply to all
sections/tests:

```
[DEFAULT]
property = value

["test_foo.js"]
```

In the above example, **test_foo.js** inherits the metadata **property = value**
from the **DEFAULT** section.

### Recognized Metadata

Test manifests can define some common keys/metadata to influence behavior.
Those keys are as follows:

head

: List of files that will be executed before the test file. (Used in
  xpcshell tests.)

tail

: List of files that will be executed after the test file. (Used in
  xpcshell tests.)

support-files

: List of additional files required to run tests. This is typically
  defined in the **DEFAULT** section.

  Unlike other file lists, *support-files* supports a globbing mechanism
  to facilitate pulling in many files with minimal typing. This globbing
  mechanism is activated if an entry in this value contains a `*`
  character. A single `*` will wildcard match all files in a directory.
  A double `**` will descend into child directories. For example,
  `data/*` will match `data/foo` but not `data/subdir/bar` where
  `data/**` will match `data/foo` and `data/subdir/bar`.

  Support files starting with `/` are placed in a root directory, rather
  than a location determined by the manifest location. For mochitests,
  this allows for the placement of files at the server root. The source
  file is selected from the base name (e.g., `foo` for `/path/foo`).
  Files starting with `/` cannot be selected using globbing.

  Some support files are used by tests across multiple directories. In
  this case, a test depending on a support file from another directory
  must note that dependency with the path to the required support file
  in its own **support-files** entry. These use a syntax where paths
  starting with `!/` will indicate the beginning of the path to a
  shared support file starting from the root of the srcdir. For example,
  if a manifest at `dom/base/test/mochitest.toml` has a support file,
  `dom/base/test/server-script.sjs`, and a mochitest in
  `dom/workers/test` depends on that support file, the test manifest
  at `dom/workers/test/mochitest.toml` must include
  `!/dom/base/test/server-script.sjs` in its **support-files** entry.

generated-files

: List of files that are generated as part of the build and don't exist in
  the source tree.

  The build system assumes that each manifest file, test file, and file
  listed in **head**, **tail**, and **support-files** is static and
  provided by the source tree (and not automatically generated as part
  of the build). This variable tells the build system not to make this
  assumption.

  This variable will likely go away sometime once all generated files are
  accounted for in the build config.

  If a generated file is not listed in this key, a clobber build will
  likely fail.

dupe-manifest

: Record that this manifest duplicates another manifest.

  The common scenario is two manifest files will include a shared
  manifest file via the `["include:file"]` special section. The build
  system enforces that each test file is only provided by a single
  manifest. Having this key present bypasses that check.

  The value of this key is ignored.

skip-if

: Skip this test if the specified condition is true.
  See {ref}`manifest-filter-language`.

  Conditions can be specified on multiple lines, where each line is implicitly
  joined by a logical OR (`||`). This makes it easier to add comments to
  distinct failures. For example:

  ```{eval-rst}
  .. parsed-literal::

     ["test_foo.js"]
     skip-if = [
         "os == 'mac' && fission",  # bug 123 - fails on fission
         "os == 'windows' && debug",  # bug 456 - hits an assertion
     ]
  ```

fail-if

: Expect test failure if the specified condition is true.
  See {ref}`manifest-filter-language`.

  Conditions can be specified on multiple lines (see `skip-if`).

run-sequentially

: Run test sequentially if the specified condition is true.
  See {ref}`manifest-filter-language`.

  To always run a test sequentially, use `run-sequentially = ["true"]`.

scheme

: Changes the scheme and domain from which the test runs. (Only used in mochitest suites)

  There are two possible values:
  : - `http` (default): The test will run from <http://mochi.test:8888>
    - `https`: The test will run from <https://example.com:443>

(manifest-filter-language)=

### Manifest Filter Language

Some manifest keys accept a special filter syntax as their values. These
values are essentially boolean expressions that are evaluated at test
execution time.

The expressions can reference a well-defined set of variables, such as
`os` and `debug`. These variables are populated from the
`mozinfo.json` file. For the full list of available variables, see
the {ref}`mozinfo documentation <mozinfo-attributes>`.

See
[the source](https://searchfox.org/firefox-main/source/testing/mozbase/manifestparser/manifestparser/manifestparser.py) for the full documentation of the
expression syntax until it is documented here.


```{eval-rst}
.. todo::

   Document manifest filter language.
```

(manifest-file-installation)=

### File Installation

Files referenced by manifests are automatically installed into the object
directory into paths defined in
{py:func}`mozbuild.frontend.emitter.TreeMetadataEmitter._process_test_manifest`.

Relative paths resolving to parent directory (e.g.
`support-files = ../foo.txt` have special behavior.

For `support-files`, the file will be installed to the default destination
for that manifest. Only the file's base name is used to construct the final
path: directories are irrelevant. Files starting with `/` are an exception,
these are installed relative to the root of the destination; the base name is
instead used to select the file..

For all other entry types, the file installation is skipped.

(reftest-manifests)=

## Reftest Manifests

See [MDN](https://developer.mozilla.org/en-US/docs/Creating_reftest-based_unit_tests).
