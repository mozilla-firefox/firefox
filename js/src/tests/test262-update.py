#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import os
import shutil
import sys
import tempfile
from functools import partial
from itertools import chain
from operator import itemgetter

# Skip all tests which use features not supported in SpiderMonkey.
UNSUPPORTED_FEATURES = set([
    "tail-call-optimization",
    "import-defer",
    "nonextensible-applies-to-private",  # Bug 1991478
    "ShadowRealm",
])
FEATURE_CHECK_NEEDED = {
    "Atomics": "!this.hasOwnProperty('Atomics')",
    "SharedArrayBuffer": "!this.hasOwnProperty('SharedArrayBuffer')",
    "Temporal": "!this.hasOwnProperty('Temporal')",
    "decorators": "!(this.hasOwnProperty('getBuildConfiguration')&&getBuildConfiguration('decorators'))",  # Bug 1435869
    "Atomics.pause": "!this.hasOwnProperty('Atomics')||!Atomics.pause",  # Bug 1918717
    "Error.isError": "!Error.isError",  # Bug 1923733
    "iterator-sequencing": "!Iterator.concat",  # Bug 1923732
    "iterator-includes": "!Iterator.prototype.includes",  # Bug 2025779
    "Iterator.prototype.join": "!Iterator.prototype.join",  # Bug 2003341
    "iterator-chunking": "!Iterator.prototype.chunks",  # Bug 1997187
    "immutable-arraybuffer": "!ArrayBuffer.prototype.sliceToImmutable",  # Bug 1952253
    "await-dictionary": "!Promise.allKeyed",
    "source-phase-imports": "!(this.hasOwnProperty('getBuildConfiguration')&&getBuildConfiguration('source-phase-imports'))",
    "source-phase-imports-module-source": "!(this.hasOwnProperty('wasmIsSupported')&&wasmIsSupported())",
    "Intl.Locale-info": "!this.hasOwnProperty('Intl')||!this.Intl.Locale.prototype.hasOwnProperty('firstDayOfWeek')",
}
RELEASE_OR_BETA = set([
    "legacy-regexp",
    "import-bytes",
])
SHELL_OPTIONS = {
    "symbols-as-weakmap-keys": "--enable-symbols-as-weakmap-keys",
    "iterator-sequencing": "--enable-iterator-sequencing",
    "iterator-includes": "--enable-iterator-includes",
    "Iterator.prototype.join": "--enable-iterator-join",
    "iterator-chunking": "--enable-iterator-chunking",
    "Atomics.waitAsync": "--setpref=atomics_wait_async",
    "immutable-arraybuffer": "--enable-arraybuffer-immutable",
    "import-bytes": "--enable-import-bytes",
    "await-dictionary": "--enable-promise-allkeyed",
    "source-phase-imports": "--enable-source-phase-imports",
    "source-phase-imports-module-source": "--enable-source-phase-imports-test262-module-source",
    "Intl.Locale-info": "--enable-intl-locale-info",
    "regexp-buffer-boundaries": "--enable-regexp-buffer-boundaries",
}

INCLUDE_FEATURE_DETECTED_OPTIONAL_SHELL_OPTIONS = {}


@contextlib.contextmanager
def TemporaryDirectory():
    tmpDir = tempfile.mkdtemp()
    try:
        yield tmpDir
    finally:
        shutil.rmtree(tmpDir)


def loadTest262Parser(test262Dir):
    """
    Loads the test262 test record parser.
    """
    import importlib.machinery
    import importlib.util

    packagingDir = os.path.join(test262Dir, "tools", "packaging")
    moduleName = "parseTestRecord"

    # Create a FileFinder to load Python source files.
    loader_details = (
        importlib.machinery.SourceFileLoader,
        importlib.machinery.SOURCE_SUFFIXES,
    )
    finder = importlib.machinery.FileFinder(packagingDir, loader_details)

    # Find the module spec.
    spec = finder.find_spec(moduleName)
    if spec is None:
        raise RuntimeError("Can't find parseTestRecord module")

    # Create and execute the module.
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    # Return the executed module
    return module


def tryParseTestFile(test262parser, source, testName):
    """
    Returns the result of test262parser.parseTestRecord() or None if a parser
    error occured.

    See <https://github.com/tc39/test262/blob/main/INTERPRETING.md> for an
    overview of the returned test attributes.
    """
    try:
        return test262parser.parseTestRecord(source, testName)
    except Exception as err:
        print("Error '%s' in file: %s" % (err, testName), file=sys.stderr)
        print("Please report this error to the test262 GitHub repository!")
        return None


def createRefTestEntry(options, skip, skipIf, error, isModule, isAsync):
    """
    Returns the |reftest| tuple (terms, comments) from the input arguments. Or a
    tuple of empty strings if no reftest entry is required.
    """

    terms = []
    comments = []

    if options:
        terms.extend(options)

    if skip:
        terms.append("skip")
        comments.extend(skip)

    if skipIf:
        terms.append("skip-if(" + "||".join([cond for (cond, _) in skipIf]) + ")")
        comments.extend([comment for (_, comment) in skipIf])

    if error:
        terms.append("error:" + error)

    if isModule:
        terms.append("module")

    if isAsync:
        terms.append("async")

    return (" ".join(terms), ", ".join(comments))


def createRefTestLine(terms, comments):
    """
    Creates the |reftest| line using the given terms and comments.
    """

    refTest = terms
    if comments:
        refTest += " -- " + comments
    return refTest


def createSource(testSource, refTest, prologue, epilogue):
    """
    Returns the post-processed source for |testSource|.
    """

    source = []

    # Add the |reftest| line.
    if refTest:
        source.append(b"// |reftest| " + refTest.encode("utf-8"))

    # Prepend any directives if present.
    if prologue:
        source.append(prologue.encode("utf-8"))

    source.append(testSource)

    # Append the test epilogue, i.e. the call to "reportCompare".
    # TODO: Does this conflict with raw tests?
    if epilogue:
        source.append(epilogue.encode("utf-8"))
        source.append(b"")

    return b"\n".join(source)


def writeTestFile(test262OutDir, testFileName, source):
    """
    Writes the test source to |test262OutDir|.
    """

    with open(os.path.join(test262OutDir, testFileName), "wb") as output:
        output.write(source)


def addSuffixToFileName(fileName, suffix):
    (filePath, ext) = os.path.splitext(fileName)
    return filePath + suffix + ext


def writeShellAndBrowserFiles(
    test262OutDir, harnessDir, includesMap, localIncludesMap, relPath
):
    """
    Generate the shell.js and browser.js files for the test harness.
    """

    # Find all includes from parent directories.
    def findParentIncludes():
        parentIncludes = set()
        current = relPath
        while current:
            (parent, child) = os.path.split(current)
            if parent in includesMap:
                parentIncludes.update(includesMap[parent])
            current = parent
        return parentIncludes

    # Find all includes, skipping includes already present in parent directories.
    def findIncludes():
        parentIncludes = findParentIncludes()
        for include in includesMap[relPath]:
            if include not in parentIncludes:
                yield include

    def readIncludeFile(filePath):
        with open(filePath, "rb") as includeFile:
            return b"// file: %s\n%s" % (
                os.path.basename(filePath).encode("utf-8"),
                includeFile.read(),
            )

    localIncludes = localIncludesMap[relPath] if relPath in localIncludesMap else []

    # Concatenate all includes files.
    includeSource = b"\n".join(
        map(
            readIncludeFile,
            chain(
                # The requested include files.
                map(partial(os.path.join, harnessDir), sorted(findIncludes())),
                # And additional local include files.
                map(partial(os.path.join, os.getcwd()), sorted(localIncludes)),
            ),
        )
    )

    # Write the concatenated include sources to shell.js.
    with open(os.path.join(test262OutDir, relPath, "shell.js"), "wb") as shellFile:
        if includeSource:
            shellFile.write(b"// GENERATED, DO NOT EDIT\n")
            shellFile.write(includeSource)

    # The browser.js file is always empty for test262 tests.
    with open(os.path.join(test262OutDir, relPath, "browser.js"), "wb") as browserFile:
        browserFile.write(b"")


def pathStartsWith(path, *args):
    prefix = os.path.join(*args)
    return os.path.commonprefix([path, prefix]) == prefix


def convertTestFile(test262parser, testSource, testName, includeSet, strictTests):
    """
    Convert a test262 test to a compatible jstests test file.
    """

    # The test record dictionary, its contents are explained in depth at
    # <https://github.com/tc39/test262/blob/main/INTERPRETING.md>.
    testRec = tryParseTestFile(test262parser, testSource.decode("utf-8"), testName)

    # jsreftest meta data
    refTestOptions = []
    refTestSkip = []
    refTestSkipIf = []

    # Skip all files which contain YAML errors.
    if testRec is None:
        refTestSkip.append("has YAML errors")
        testRec = dict()

    # onlyStrict is set when the test must only be run in strict mode.
    onlyStrict = "onlyStrict" in testRec

    # noStrict is set when the test must not be run in strict mode.
    noStrict = "noStrict" in testRec

    # The "raw" attribute is used in the default test262 runner to prevent
    # prepending additional content (use-strict directive, harness files)
    # before the actual test source code.
    raw = "raw" in testRec

    # Negative tests have additional meta-data to specify the error type and
    # when the error is issued (runtime error or early parse error). We're
    # currently ignoring the error phase attribute.
    # testRec["negative"] == {type=<error name>, phase=parse|resolution|runtime}
    isNegative = "negative" in testRec
    assert not isNegative or type(testRec["negative"]) is dict
    errorType = testRec["negative"]["type"] if isNegative else None

    # Async tests are marked with the "async" attribute.
    isAsync = "async" in testRec

    # Test262 tests cannot be both "negative" and "async".  (In principle a
    # negative async test is permitted when the error phase is not "parse" or
    # the error type is not SyntaxError, but no such tests exist now.)
    # assert not (isNegative and isAsync), (
    #     "Can't have both async and negative attributes: %s" % testName
    # )
    # TODO: Print a warning instead of asserting until
    # <https://github.com/tc39/test262/issues/4638> is fixed.
    if isNegative and isAsync:
        print(
            f"bad isnegative + async: {testName} (https://github.com/tc39/test262/issues/4638)"
        )

    # Only async tests may use the $DONE or asyncTest function. However,
    # negative parse tests may "use" the $DONE (or asyncTest) function (of
    # course they don't actually use it!) without specifying the "async"
    # attribute. Otherwise, neither $DONE nor asyncTest must appear in the test.
    #
    # Some "harness" tests redefine $DONE, so skip this check when the test file
    # is in the "harness" directory.
    assert (
        (b"$DONE" not in testSource and b"asyncTest" not in testSource)
        or isAsync
        or isNegative
        or testName.split(os.path.sep)[0] == "harness"
    ), "Missing async attribute in: %s" % testName

    # When the "module" attribute is set, the source code is module code.
    isModule = "module" in testRec

    # CanBlockIsFalse is set when the test expects that the implementation
    # cannot block on the main thread.
    if "CanBlockIsFalse" in testRec:
        refTestSkipIf.append(("xulRuntime.shell", "shell can block main thread"))

    # CanBlockIsTrue is set when the test expects that the implementation
    # can block on the main thread.
    if "CanBlockIsTrue" in testRec:
        refTestSkipIf.append(("!xulRuntime.shell", "browser cannot block main thread"))

    # Skip tests with unsupported features.
    if "features" in testRec:
        unsupported = [f for f in testRec["features"] if f in UNSUPPORTED_FEATURES]
        if unsupported:
            refTestSkip.append("%s is not supported" % ",".join(unsupported))
        else:
            releaseOrBeta = [f for f in testRec["features"] if f in RELEASE_OR_BETA]
            if releaseOrBeta:
                refTestSkipIf.append((
                    "release_or_beta",
                    "%s is not released yet" % ",".join(releaseOrBeta),
                ))

            featureCheckNeeded = [
                f for f in testRec["features"] if f in FEATURE_CHECK_NEEDED
            ]
            if featureCheckNeeded:
                refTestSkipIf.append((
                    "||".join([FEATURE_CHECK_NEEDED[f] for f in featureCheckNeeded]),
                    "%s is not enabled unconditionally" % ",".join(featureCheckNeeded),
                ))

            if (
                "Atomics" in testRec["features"]
                and "SharedArrayBuffer" in testRec["features"]
            ):
                refTestSkipIf.append((
                    "(this.hasOwnProperty('getBuildConfiguration')"
                    "&&getBuildConfiguration('arm64-simulator'))",
                    "ARM64 Simulator cannot emulate atomics",
                ))

            shellOptions = {
                SHELL_OPTIONS[f] for f in testRec["features"] if f in SHELL_OPTIONS
            }
            if shellOptions:
                refTestSkipIf.append(("!xulRuntime.shell", "requires shell-options"))
                refTestOptions.extend(
                    f"shell-option({opt})" for opt in sorted(shellOptions)
                )

    # Optional shell options. Some tests use feature detection for additional
    # test coverage. We want to get this extra coverage without having to skip
    # these tests in browser builds.
    if "includes" in testRec:
        optionalShellOptions = (
            SHELL_OPTIONS[INCLUDE_FEATURE_DETECTED_OPTIONAL_SHELL_OPTIONS[include]]
            for include in testRec["includes"]
            if include in INCLUDE_FEATURE_DETECTED_OPTIONAL_SHELL_OPTIONS
        )
        refTestOptions.extend(
            f"shell-option({opt})" for opt in sorted(optionalShellOptions)
        )

    # Includes for every test file in a directory is collected in a single
    # shell.js file per directory level. This is done to avoid adding all
    # test harness files to the top level shell.js file.
    if "includes" in testRec:
        assert not raw, "Raw test with includes: %s" % testName
        includeSet.update(testRec["includes"])

    # Add reportCompare() after all positive, synchronous tests.
    if not isNegative and not isAsync:
        testEpilogue = "reportCompare(0, 0);"
    else:
        testEpilogue = ""

    if raw:
        refTestOptions.append("test262-raw")

    (terms, comments) = createRefTestEntry(
        refTestOptions, refTestSkip, refTestSkipIf, errorType, isModule, isAsync
    )
    if raw:
        refTest = ""
        externRefTest = (terms, comments)
    else:
        refTest = createRefTestLine(terms, comments)
        externRefTest = None

    # Don't write a strict-mode variant for raw or module files.
    noStrictVariant = raw or isModule
    assert not (noStrictVariant and (onlyStrict or noStrict)), (
        "Unexpected onlyStrict or noStrict attribute: %s" % testName
    )

    # Write non-strict mode test.
    if noStrictVariant or noStrict or not onlyStrict:
        testPrologue = ""
        nonStrictSource = createSource(testSource, refTest, testPrologue, testEpilogue)
        testFileName = testName
        yield (testFileName, nonStrictSource, externRefTest)

    # Write strict mode test.
    if not noStrictVariant and (onlyStrict or (not noStrict and strictTests)):
        testPrologue = "'use strict';"
        strictSource = createSource(testSource, refTest, testPrologue, testEpilogue)
        testFileName = testName
        if not noStrict:
            testFileName = addSuffixToFileName(testFileName, "-strict")
        yield (testFileName, strictSource, externRefTest)


def convertFixtureFile(fixtureSource, fixtureName):
    """
    Convert a test262 fixture file to a compatible jstests test file.
    """

    # jsreftest meta data
    refTestOptions = []
    refTestSkip = ["not a test file"]
    refTestSkipIf = []
    errorType = None
    isModule = False
    isAsync = False

    (terms, comments) = createRefTestEntry(
        refTestOptions, refTestSkip, refTestSkipIf, errorType, isModule, isAsync
    )

    source = createSource(fixtureSource, "", "", "")
    externRefTest = (terms, comments)
    yield (fixtureName, source, externRefTest)


def process_test262(test262Dir, test262OutDir, strictTests, externManifests):
    """
    Process all test262 files and converts them into jstests compatible tests.
    """

    harnessDir = os.path.join(test262Dir, "harness")
    testDir = os.path.join(test262Dir, "test")
    test262parser = loadTest262Parser(test262Dir)

    # Map of test262 subdirectories to the set of include files required for
    # tests in that subdirectory. The includes for all tests in a subdirectory
    # are merged into a single shell.js.
    # map<dirname, set<includeFiles>>
    includesMap = {}

    # Additional local includes keyed by test262 directory names. The include
    # files in this map must be located in the js/src/tests directory.
    # map<dirname, list<includeFiles>>
    localIncludesMap = {}

    # The root directory contains required harness files and test262-host.js.
    includesMap[""] = set(["sta.js", "assert.js"])
    localIncludesMap[""] = ["test262-host.js"]

    # Also add files known to be used by many tests to the root shell.js file.
    includesMap[""].update(["propertyHelper.js", "compareArray.js"])

    # Write the root shell.js file.
    writeShellAndBrowserFiles(
        test262OutDir, harnessDir, includesMap, localIncludesMap, ""
    )

    # Additional explicit includes inserted at well-chosen locations to reduce
    # code duplication in shell.js files.
    explicitIncludes = {}
    explicitIncludes[os.path.join("built-ins", "Atomics")] = [
        "testAtomics.js",
        "testTypedArray.js",
    ]
    explicitIncludes[os.path.join("built-ins", "DataView")] = [
        "byteConversionValues.js"
    ]
    explicitIncludes[os.path.join("built-ins", "Promise")] = ["promiseHelper.js"]
    explicitIncludes[os.path.join("built-ins", "Temporal")] = ["temporalHelpers.js"]
    explicitIncludes[os.path.join("built-ins", "TypedArray")] = [
        "byteConversionValues.js",
        "detachArrayBuffer.js",
        "nans.js",
    ]
    explicitIncludes[os.path.join("built-ins", "TypedArrays")] = [
        "detachArrayBuffer.js"
    ]

    # We can't include "sm/non262.js", because it conflicts with our test harness,
    # but some definitions from "sm/non262.js" are still needed.
    localIncludesMap[os.path.join("staging", "sm")] = ["test262-non262.js"]

    # Process all test directories recursively.
    for dirPath, dirNames, fileNames in os.walk(testDir):
        relPath = os.path.relpath(dirPath, testDir)
        if relPath == ".":
            continue

        # Skip creating a "prs" directory if it already exists
        if relPath not in ("prs", "local") and not os.path.exists(
            os.path.join(test262OutDir, relPath)
        ):
            os.makedirs(os.path.join(test262OutDir, relPath))

        includeSet = set()
        includesMap[relPath] = includeSet

        if relPath in explicitIncludes:
            includeSet.update(explicitIncludes[relPath])

        # Convert each test file.
        for fileName in fileNames:
            filePath = os.path.join(dirPath, fileName)
            testName = os.path.relpath(filePath, testDir)

            # Copy non-test files as is.
            (_, fileExt) = os.path.splitext(fileName)
            if fileExt != ".js":
                shutil.copyfile(filePath, os.path.join(test262OutDir, testName))
                continue

            # Files ending with "_FIXTURE.js" are fixture files:
            # https://github.com/tc39/test262/blob/main/INTERPRETING.md#modules
            isFixtureFile = fileName.endswith("_FIXTURE.js")

            # Read the original test source and preprocess it for the jstests harness.
            with open(filePath, "rb") as testFile:
                testSource = testFile.read()

            if isFixtureFile:
                convert = convertFixtureFile(testSource, testName)
            else:
                convert = convertTestFile(
                    test262parser,
                    testSource,
                    testName,
                    includeSet,
                    strictTests,
                )

            for newFileName, newSource, externRefTest in convert:
                writeTestFile(test262OutDir, newFileName, newSource)

                if externRefTest is not None:
                    externManifests.append({
                        "name": newFileName,
                        "reftest": externRefTest,
                    })

        # Remove "sm/non262.js" because it overwrites our test harness with stub
        # functions.
        includeSet.discard("sm/non262.js")

        # Add shell.js and browers.js files for the current directory.
        writeShellAndBrowserFiles(
            test262OutDir, harnessDir, includesMap, localIncludesMap, relPath
        )


def fetch_local_changes(inDir, outDir, srcDir, strictTests):
    """
    Fetch the changes from a local clone of Test262.

    1. Get the list of file changes made by the current branch used on Test262 (srcDir).
    2. Copy only the (A)dded, (C)opied, (M)odified, and (R)enamed files to inDir.
    3. inDir is treated like a Test262 checkout, where files will be converted.
    4. Fetches the current branch name to set the outDir.
    5. Processed files will be added to `<outDir>/local/<branchName>`.
    """
    import subprocess

    # TODO: fail if it's in the default branch? or require a branch name?
    # Checks for unstaged or non committed files. A clean branch provides a clean status.
    status = subprocess.check_output(
        ("git -C %s status --porcelain" % srcDir).split(" "), encoding="utf-8"
    )

    if status.strip():
        raise RuntimeError(
            "Please commit files and cleanup the local test262 folder before importing files.\n"
            "Current status: \n%s" % status
        )

    # Captures the branch name to be used on the output
    branchName = subprocess.check_output(
        ("git -C %s rev-parse --abbrev-ref HEAD" % srcDir).split(" "), encoding="utf-8"
    ).split("\n")[0]

    # Fetches the file names to import
    files = subprocess.check_output(
        ("git -C %s diff main --diff-filter=ACMR --name-only" % srcDir).split(" "),
        encoding="utf-8",
    )

    # Fetches the deleted files to print an output log. This can be used to
    # set up the skip list, if necessary.
    deletedFiles = subprocess.check_output(
        ("git -C %s diff main --diff-filter=D --name-only" % srcDir).split(" "),
        encoding="utf-8",
    )

    # Fetches the modified files as well for logging to support maintenance
    # in the skip list.
    modifiedFiles = subprocess.check_output(
        ("git -C %s diff main --diff-filter=M --name-only" % srcDir).split(" "),
        encoding="utf-8",
    )

    # Fetches the renamed files for the same reason, this avoids duplicate
    # tests if running the new local folder and the general imported Test262
    # files.
    renamedFiles = subprocess.check_output(
        ("git -C %s diff main --diff-filter=R --summary" % srcDir).split(" "),
        encoding="utf-8",
    )

    # Print some friendly output
    print("From the branch %s in %s \n" % (branchName, srcDir))
    print("Files being copied to the local folder: \n%s" % files)
    if deletedFiles:
        print(
            "Deleted files (use this list to update the skip list): \n%s" % deletedFiles
        )
    if modifiedFiles:
        print(
            "Modified files (use this list to update the skip list): \n%s"
            % modifiedFiles
        )
    if renamedFiles:
        print("Renamed files (already added with the new names): \n%s" % renamedFiles)

    for f in files.splitlines():
        # Capture the subdirectories names to recreate the file tree
        # TODO: join the file tree with -- instead of multiple subfolders?
        fileTree = os.path.join(inDir, os.path.dirname(f))
        if not os.path.exists(fileTree):
            os.makedirs(fileTree)

        shutil.copyfile(
            os.path.join(srcDir, f), os.path.join(fileTree, os.path.basename(f))
        )

    # Extras from Test262. Copy the current support folders - including the
    # harness - for a proper conversion process
    shutil.copytree(os.path.join(srcDir, "tools"), os.path.join(inDir, "tools"))
    shutil.copytree(os.path.join(srcDir, "harness"), os.path.join(inDir, "harness"))

    # Reset any older directory in the output using the same branch name
    outDir = os.path.join(outDir, "local", branchName)
    if os.path.isdir(outDir):
        shutil.rmtree(outDir)
    os.makedirs(outDir)

    process_test262(inDir, outDir, strictTests, [])


def fetch_pr_files(inDir, outDir, prNumber, strictTests):
    import requests

    prTestsOutDir = os.path.join(outDir, "prs", prNumber)
    if os.path.isdir(prTestsOutDir):
        print("Removing folder %s" % prTestsOutDir)
        shutil.rmtree(prTestsOutDir)
    os.makedirs(prTestsOutDir)

    # Reuses current Test262 clone's harness and tools folders only, the clone's test/
    # folder can be discarded from here
    shutil.rmtree(os.path.join(inDir, "test"))

    prRequest = requests.get(
        "https://api.github.com/repos/tc39/test262/pulls/%s" % prNumber
    )
    prRequest.raise_for_status()

    pr = prRequest.json()

    if pr["state"] != "open":
        # Closed PR, remove respective files from folder
        return print("PR %s is closed" % prNumber)

    url = "https://api.github.com/repos/tc39/test262/pulls/%s/files" % prNumber
    hasNext = True

    while hasNext:
        files = requests.get(url)
        files.raise_for_status()

        for item in files.json():
            if not item["filename"].startswith("test/"):
                continue

            filename = item["filename"]
            fileStatus = item["status"]

            print("%s %s" % (fileStatus, filename))

            # Do not add deleted files
            if fileStatus == "removed":
                continue

            contents = requests.get(item["raw_url"])
            contents.raise_for_status()

            fileText = contents.text

            filePathDirs = os.path.join(inDir, *filename.split("/")[:-1])

            if not os.path.isdir(filePathDirs):
                os.makedirs(filePathDirs)

            with open(os.path.join(inDir, *filename.split("/")), "wb") as output_file:
                output_file.write(fileText.encode("utf8"))

        hasNext = False

        # Check if the pull request changes are split over multiple pages.
        if "link" in files.headers:
            link = files.headers["link"]

            # The links are comma separated and the entries within a link are separated by a
            # semicolon. For example the first two links entries for PR 3199:
            #
            # https://api.github.com/repos/tc39/test262/pulls/3199/files
            # """
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=2>; rel="next",
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=14>; rel="last"
            # """
            #
            # https://api.github.com/repositories/16147933/pulls/3199/files?page=2
            # """
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=1>; rel="prev",
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=3>; rel="next",
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=14>; rel="last",
            # <https://api.github.com/repositories/16147933/pulls/3199/files?page=1>; rel="first"
            # """

            for pages in link.split(", "):
                (pageUrl, rel) = pages.split("; ")

                assert pageUrl[0] == "<"
                assert pageUrl[-1] == ">"

                # Remove the angle brackets around the URL.
                pageUrl = pageUrl[1:-1]

                # Make sure we only request data from github and not some other place.
                assert pageUrl.startswith("https://api.github.com/")

                # Ensure the relative URL marker has the expected format.
                assert rel in {'rel="prev"', 'rel="next"', 'rel="first"', 'rel="last"'}

                # We only need the URL for the next page.
                if rel == 'rel="next"':
                    url = pageUrl
                    hasNext = True

    process_test262(inDir, prTestsOutDir, strictTests, [])


def general_update(inDir, outDir, strictTests):
    import subprocess

    restoreLocalTestsDir = False
    restorePrsTestsDir = False
    localTestsOutDir = os.path.join(outDir, "local")
    prsTestsOutDir = os.path.join(outDir, "prs")

    # Stash test262/local and test262/prs. Currently the Test262 repo does not have any
    # top-level subdirectories named "local" or "prs".
    # This prevents these folders from being removed during the update process.
    if os.path.isdir(localTestsOutDir):
        shutil.move(localTestsOutDir, inDir)
        restoreLocalTestsDir = True

    if os.path.isdir(prsTestsOutDir):
        shutil.move(prsTestsOutDir, inDir)
        restorePrsTestsDir = True

    # Create the output directory from scratch.
    if os.path.isdir(outDir):
        shutil.rmtree(outDir)
    os.makedirs(outDir)

    # Copy license file.
    shutil.copyfile(os.path.join(inDir, "LICENSE"), os.path.join(outDir, "LICENSE"))

    # Create the git info file.
    with open(os.path.join(outDir, "GIT-INFO"), "w", encoding="utf-8") as info:
        subprocess.check_call(["git", "-C", inDir, "log", "-1"], stdout=info)

    # Copy the test files.
    externManifests = []
    process_test262(inDir, outDir, strictTests, externManifests)

    # Create the external reftest manifest file.
    with open(os.path.join(outDir, "jstests.list"), "wb") as manifestFile:
        manifestFile.write(b"# GENERATED, DO NOT EDIT\n\n")
        for externManifest in sorted(externManifests, key=itemgetter("name")):
            (terms, comments) = externManifest["reftest"]
            if terms:
                entry = "%s script %s%s\n" % (
                    terms,
                    externManifest["name"],
                    (" # %s" % comments) if comments else "",
                )
                manifestFile.write(entry.encode("utf-8"))

    # Move test262/local back.
    if restoreLocalTestsDir:
        shutil.move(os.path.join(inDir, "local"), outDir)

    # Restore test262/prs if necessary after a general Test262 update.
    if restorePrsTestsDir:
        shutil.move(os.path.join(inDir, "prs"), outDir)


def update_test262(args):
    import subprocess

    url = args.url
    branch = args.branch
    revision = args.revision
    outDir = args.out
    prNumber = args.pull
    srcDir = args.local

    if not os.path.isabs(outDir):
        outDir = os.path.join(os.getcwd(), outDir)

    strictTests = args.strict

    # Download the requested branch in a temporary directory.
    with TemporaryDirectory() as inDir:
        # If it's a local import, skip the git clone parts.
        if srcDir:
            return fetch_local_changes(inDir, outDir, srcDir, strictTests)

        if revision == "HEAD":
            subprocess.check_call([
                "git",
                "clone",
                "--depth=1",
                "--branch=%s" % branch,
                url,
                inDir,
            ])
        else:
            subprocess.check_call([
                "git",
                "clone",
                "--single-branch",
                "--branch=%s" % branch,
                url,
                inDir,
            ])
            subprocess.check_call(["git", "-C", inDir, "reset", "--hard", revision])

        # If a PR number is provided, fetches only the new and modified files
        # from that PR. It also creates a new folder for that PR or replaces if
        # it already exists, without updating the regular Test262 tests.
        if prNumber:
            return fetch_pr_files(inDir, outDir, prNumber, strictTests)

        # Without a PR or a local import, follows through a regular copy.
        general_update(inDir, outDir, strictTests)


if __name__ == "__main__":
    import argparse

    # This script must be run from js/src/tests to work correctly.
    if "/".join(os.path.normpath(os.getcwd()).split(os.sep)[-3:]) != "js/src/tests":
        raise RuntimeError("%s must be run from js/src/tests" % sys.argv[0])

    parser = argparse.ArgumentParser(description="Update the test262 test suite.")
    parser.add_argument(
        "--url",
        default="https://github.com/tc39/test262.git",
        help="URL to git repository (default: %(default)s)",
    )
    parser.add_argument(
        "--branch", default="main", help="Git branch (default: %(default)s)"
    )
    parser.add_argument(
        "--revision", default="HEAD", help="Git revision (default: %(default)s)"
    )
    parser.add_argument(
        "--out",
        default="test262",
        help="Output directory. Any existing directory will be removed!"
        "(default: %(default)s)",
    )
    parser.add_argument(
        "--pull", help="Import contents from a Pull Request specified by its number"
    )
    parser.add_argument(
        "--local",
        help="Import new and modified contents from a local folder, a new folder "
        "will be created on local/branch_name",
    )
    parser.add_argument(
        "--strict",
        default=False,
        action="store_true",
        help="Generate additional strict mode tests. Not enabled by default.",
    )
    parser.set_defaults(func=update_test262)
    args = parser.parse_args()
    args.func(args)
