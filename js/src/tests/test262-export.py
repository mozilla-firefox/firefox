#!/usr/bin/env python3
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import math
import os
import re
import shutil
import sys
import traceback
from datetime import date
from typing import Any, Optional

import yaml

# Skip all common files used to support tests for jstests
# These files are listed in the README.txt
SUPPORT_FILES = set(
    [
        "browser.js",
        "shell.js",
        "template.js",
        "user.js",
        "js-test-driver-begin.js",
        "js-test-driver-end.js",
    ]
)


# Run once per subdirectory
def findAndCopyIncludes(dirPath: str, baseDir: str, includeDir: str) -> "list[str]":
    relPath = os.path.relpath(dirPath, baseDir)
    includes: list[str] = ["sm/non262.js"]
    os.makedirs(os.path.join(includeDir, "sm"), exist_ok=True)

    # Recurse down all folders in the relative path until
    # we reach the base directory of shell.js include files.
    # Each directory will have a shell.js file to copy.
    while relPath:
        # find the shell.js
        shellFile = os.path.join(baseDir, relPath, "shell.js")

        # if the file isn't excluded, exists, and is not empty, include in includes
        if (
            not any(relPath == f"non262/{path}" for path in UNSUPPORTED_PATHS)
            and os.path.exists(shellFile)
            and os.path.getsize(shellFile) > 0
        ):
            with open(shellFile, "rb") as f:
                testSource = f.read()

            if b"// SKIP test262 export" not in testSource:
                # create new shell.js file name
                includeFileName = "sm/" + relPath.replace("/", "-") + "-shell.js"
                includes.append(includeFileName)

                includesPath = os.path.join(includeDir, includeFileName)
                shutil.copyfile(shellFile, includesPath)

        relPath = os.path.split(relPath)[0]

    return includes


UNSUPPORTED_CODE: "list[bytes]" = [
    b"// SKIP test262 export",
    b"inTimeZone(",
    b"getTimeZone(",
    b"setTimeZone(",
    b"getAvailableLocalesOf(",
    b"uneval(",
    b"Debugger",
    b"SpecialPowers",
    b"evalcx(",
    b"evaluate(",
    b"drainJobQueue(",
    b"getPromiseResult(",
    b"assertEventuallyEq(",
    b"assertEventuallyThrows(",
    b"settlePromiseNow(",
    b"setPromiseRejectionTrackerCallback",
    b"displayName(",
    b"InternalError",
    b"toSource(",
    b"toSource.call(",
    b"isRope(",
    b"isSameCompartment(",
    b"isCCW",
    b"nukeCCW",
    b"representativeStringArray(",
    b"largeArrayBufferSupported(",
    b"helperThreadCount(",
    b"serialize(",
    b"deserialize(",
    b"clone_object_check",
    b"grayRoot(",
    b"blackRoot(",
    b"gczeal",
    b"getSelfHostedValue(",
    b"oomTest(",
    b"assertLineAndColumn(",
    b"wrapWithProto(",
    b"Reflect.parse(",
    b"relazifyFunctions(",
    b"ignoreUnhandledRejections",
    b".lineNumber",
    b"expectExitCode",
    b"loadRelativeToScript",
    b"XorShiftGenerator",
]


def skipTest(source: bytes) -> Optional[bytes]:
    if b"This Source Code Form is subject to the terms of the Mozilla Public" in source:
        return b"MPL license"
    for c in UNSUPPORTED_CODE:
        if c in source:
            return c

    return None


MODELINE_PATTERN = re.compile(rb"/(/|\*) -\*- .* -\*-( \*/)?[\r\n]+")

UNSUPPORTED_FEATURES = [
    "async-iterator-helpers",
    "Iterator.range",
]

UNSUPPORTED_PATHS = [
    "Intl",
    "Temporal/Intl",
    "reflect-parse",
    "extensions/empty.txt",
    "extensions/file-mapped-arraybuffers.txt",
    "module/bug1693261-async.mjs",
    "module/bug1693261-c1.mjs",
    "module/bug1693261-c2.mjs",
    "module/bug1693261-x.mjs",
]


def convertTestFile(source: bytes, includes: "list[str]") -> Optional[bytes]:
    """
    Convert a jstest test to a compatible Test262 test file.
    """

    source = MODELINE_PATTERN.sub(b"", source)

    # Extract the reftest data from the source
    source, reftest = parseHeader(source)

    # Add copyright, if needed.
    copyright, source = insertCopyrightLines(source)

    # Extract the frontmatter data from the source
    frontmatter, source = extractMeta(source)

    source, addincludes = translateHelpers(source)
    includes = includes + addincludes

    meta = computeMeta(source, reftest, frontmatter, includes)

    if "features" in meta and any(f in UNSUPPORTED_FEATURES for f in meta["features"]):
        return None

    source = insertMeta(source, meta)

    source = convertReportCompare(source)

    return copyright + source


## parseHeader


class ReftestEntry:
    def __init__(
        self,
        features: "list[str]",
        error: Optional[str],
        module: bool,
        info: Optional[str],
    ):
        self.features: list[str] = features
        self.error: Optional[str] = error
        self.module: bool = module
        self.info: Optional[str] = info


def featureFromReftest(reftest: str) -> Optional[str]:
    if reftest == "Iterator":
        return "iterator-helpers"
    if reftest == "AsyncIterator":
        return "async-iterator-helpers"
    if reftest == "Temporal":
        return "Temporal"
    if reftest in ("Intl", "addIntlExtras"):
        return None
    raise Exception(f"Unexpected feature {reftest}")


def fetchReftestEntries(reftest: str) -> ReftestEntry:
    """
    Collects and stores the entries from the reftest header.
    """

    # TODO: fails, slow, skip, random, random-if

    features: list[str] = []
    error: Optional[str] = None
    comments: Optional[str] = None
    module: bool = False

    # should capture conditions to skip
    matchesSkip = re.search(r"skip-if\((.*)\)", reftest)
    if matchesSkip:
        matches = matchesSkip.group(1).split("||")
        for match in matches:
            # captures a features list
            dependsOnProp = re.search(
                r"!this.hasOwnProperty\([\'\"](.*?)[\'\"]\)", match
            )
            if dependsOnProp:
                if feature := featureFromReftest(dependsOnProp.group(1)):
                    features.append(feature)
            else:
                print("# Can't parse the following skip-if rule: %s" % match)

    # should capture the expected error
    matchesError = re.search(r"error:\s*(\w*)", reftest)
    if matchesError:
        # The metadata from the reftests won't say if it's a runtime or an
        # early error. This specification is required for the frontmatter tags.
        error = matchesError.group(1)

    # just tells if it's a module
    matchesModule = re.search(r"\bmodule\b", reftest)
    if matchesModule:
        module = True

    # captures any comments
    matchesComments = re.search(r" -- (.*)", reftest)
    if matchesComments:
        comments = matchesComments.group(1)

    return ReftestEntry(features=features, error=error, module=module, info=comments)


def parseHeader(source: bytes) -> "tuple[bytes, Optional[ReftestEntry]]":
    """
    Parse the source to return it with the extracted the header
    """
    from lib.manifest import TEST_HEADER_PATTERN_INLINE

    # Bail early if we do not start with a single comment.
    if not source.startswith(b"//"):
        return (source, None)

    # Extract the token.
    part, _, rest = source.partition(b"\n")
    part = part.decode("utf-8")
    matches = TEST_HEADER_PATTERN_INLINE.match(part)

    if matches and matches.group(0):
        reftest = matches.group(0)

        # Remove the found header from the source;
        # Fetch and return the reftest entries
        return (rest, fetchReftestEntries(reftest))

    return (source, None)


## insertCopyrightLines


LICENSE_PATTERN = re.compile(
    rb"// Copyright( \([C]\))? (\w+) .+\. {1,2}All rights reserved\.[\r\n]{1,2}"
    + rb"("
    + rb"// This code is governed by the( BSD)? license found in the LICENSE file\."
    + rb"|"
    + rb"// See LICENSE for details."
    + rb"|"
    + rb"// Use of this source code is governed by a BSD-style license that can be[\r\n]{1,2}"
    + rb"// found in the LICENSE file\."
    + rb"|"
    + rb"// See LICENSE or https://github\.com/tc39/test262/blob/HEAD/LICENSE"
    + rb")[\r\n]{1,2}",
    re.IGNORECASE,
)

PD_PATTERN1 = re.compile(
    rb"/\*[\r\n]{1,2}"
    + rb" \* Any copyright is dedicated to the Public Domain\.[\r\n]{1,2}"
    + rb" \* (http://creativecommons\.org/licenses/publicdomain/|https://creativecommons\.org/publicdomain/zero/1\.0/)[\r\n]{1,2}"
    + rb"( \* Contributors?:"
    + rb"(( [^\r\n]*[\r\n]{1,2})|"
    + rb"([\r\n]{1,2}( \* [^\r\n]*[\r\n]{1,2})+)))?"
    + rb" \*/[\r\n]{1,2}",
    re.IGNORECASE,
)

PD_PATTERN2 = re.compile(
    rb"// Any copyright is dedicated to the Public Domain\.[\r\n]{1,2}"
    + rb"// (http://creativecommons\.org/licenses/publicdomain/|https://creativecommons\.org/publicdomain/zero/1\.0/)[\r\n]{1,2}"
    + rb"(// Contributors?: [^\r\n]*[\r\n]{1,2})?",
    re.IGNORECASE,
)

PD_PATTERN3 = re.compile(
    rb"/\* Any copyright is dedicated to the Public Domain\.[\r\n]{1,2}"
    + rb" \* (http://creativecommons\.org/licenses/publicdomain/|https://creativecommons\.org/publicdomain/zero/1\.0/) \*/[\r\n]{1,2}",
    re.IGNORECASE,
)


BSD_TEMPLATE = (
    b"""\
// Copyright (C) %d Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

"""
    % date.today().year
)

PD_TEMPLATE = b"""\
/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/licenses/publicdomain/
 */

"""


def insertCopyrightLines(source: bytes) -> "tuple[bytes, bytes]":
    """
    Insert the copyright lines into the file.
    """
    if match := LICENSE_PATTERN.search(source):
        start, end = match.span()
        return source[start:end], source[:start] + source[end:]

    if (
        match := PD_PATTERN1.search(source)
        or PD_PATTERN2.search(source)
        or PD_PATTERN3.search(source)
    ):
        start, end = match.span()
        return PD_TEMPLATE, source[:start] + source[end:]

    return BSD_TEMPLATE, source


## extractMeta

FRONTMATTER_WRAPPER_PATTERN = re.compile(
    rb"/\*\---\n([\s]*)((?:\s|\S)*)[\n\s*]---\*/[\r\n]{1,2}", flags=re.DOTALL
)


def extractMeta(source: bytes) -> "tuple[dict[str, Any], bytes]":
    """
    Capture the frontmatter metadata as yaml if it exists.
    Returns a new dict if it doesn't.
    """

    match = FRONTMATTER_WRAPPER_PATTERN.search(source)
    if not match:
        return {}, source

    indent, frontmatter_lines = match.groups()

    unindented = re.sub(b"^%s" % indent, b"", frontmatter_lines)

    yamlresult = yaml.safe_load(unindented)
    if isinstance(yamlresult, str):
        result = {"info": yamlresult}
    else:
        result = yamlresult
    start, end = match.span()
    return result, source[:start] + source[end:]


## computeMeta


def translateHelpers(source: bytes) -> "tuple[bytes, list[str]]":
    """
    Translate SpiderMonkey helper methods that have standard variants in test262.
    This also returns a list of includes that are needed to use these variants in test262, if any.
    """

    includes: list[str] = []
    source, n = re.subn(rb"\bassertDeepEq\b", b"assert.deepEqual", source)
    if n:
        includes.append("deepEqual.js")

    source, n = re.subn(rb"\bassertEqArray\b", b"assert.compareArray", source)
    if n:
        includes.append("compareArray.js")

    source = re.sub(rb"\bdetachArrayBuffer\b", b"$262.detachArrayBuffer", source)
    source = re.sub(rb"\bnewGlobal\b", b"createNewGlobal", source)
    source = re.sub(rb"\bassertEq\b", b"assert.sameValue", source)

    return (source, includes)


def mergeMeta(
    reftest: "Optional[ReftestEntry]",
    frontmatter: "dict[str, Any]",
    includes: "list[str]",
) -> "dict[str, Any]":
    """
    Merge the metadata from reftest and an existing frontmatter and populate
    required frontmatter fields properly.
    """

    # Merge the meta from reftest to the frontmatter

    # Add the shell specific includes
    if includes:
        frontmatter["includes"] = frontmatter.get("includes", []) + list(includes)

    flags: list[str] = frontmatter.get("flags", [])
    if "noStrict" not in flags and "onlyStrict" not in flags:
        frontmatter.setdefault("flags", []).append("noStrict")

    if not reftest:
        return frontmatter

    frontmatter.setdefault("features", []).extend(reftest.features)

    # Only add the module flag if the value from reftest is truish
    if reftest.module:
        frontmatter.setdefault("flags", []).append("module")
        if "noStrict" in frontmatter["flags"]:
            frontmatter["flags"].remove("noStrict")
        if "onlyStrict" in frontmatter["flags"]:
            frontmatter["flags"].remove("onlyStrict")

    # Add any comments to the info tag
    if reftest.info:
        info = reftest.info
        # Open some space in an existing info text
        if "info" in frontmatter and frontmatter["info"]:
            frontmatter["info"] += "\n\n%s" % info
        else:
            frontmatter["info"] = info

    # Set the negative flags
    if reftest.error:
        error = reftest.error
        if "negative" not in frontmatter:
            frontmatter["negative"] = {
                # This code is assuming error tags are parse errors, but they
                # might be runtime errors as well.
                # From this point, this code can also print a warning asking to
                # specify the error phase in the generated code or fill the
                # phase with an empty string.
                "phase": "parse",
                "type": error,
            }
        # Print a warning if the errors don't match
        elif frontmatter["negative"].get("type") != error:
            print(
                "Warning: The reftest error doesn't match the existing "
                + "frontmatter error. %s != %s"
                % (error, frontmatter["negative"]["type"])
            )

    return frontmatter


def cleanupMeta(meta: "dict[str, Any]") -> "dict[str, Any]":
    """
    Clean up all the frontmatter meta tags. This is not a lint tool, just a
    simple cleanup to remove trailing spaces and duplicate entries from lists.
    """

    # Trim values on each string tag
    for tag in ("description", "esid", "es5id", "es6id", "info", "author"):
        if tag in meta:
            if not meta[tag]:
                del meta[tag]
            else:
                meta[tag] = meta[tag].strip()
                if not len(meta[tag]):
                    del meta[tag]

    # Populate required tags
    for tag in ("description", "esid"):
        meta.setdefault(tag, "pending")

    # Remove duplicate entries on each list tag
    for tag in ("features", "flags", "includes"):
        if tag in meta:
            if not meta[tag]:
                del meta[tag]
            else:
                # We need the list back for the yaml dump
                meta[tag] = sorted(set(meta[tag]), reverse=tag == "includes")
                if not len(meta[tag]):
                    del meta[tag]

    if "negative" in meta:
        # If the negative tag exists, phase needs to be present and set
        if meta["negative"].get("phase") not in ["parse", "resolution", "runtime"]:
            print(
                "Warning: the negative.phase is not properly set.\n"
                + "Ref https://github.com/tc39/test262/blob/main/INTERPRETING.md#negative"
            )
        # If the negative tag exists, type is required
        if "type" not in meta["negative"]:
            print(
                "Warning: the negative.type is not set.\n"
                + "Ref https://github.com/tc39/test262/blob/main/INTERPRETING.md#negative"
            )

    return meta


def insertMeta(source: bytes, frontmatter: "dict[str, Any]") -> bytes:
    """
    Insert the formatted frontmatter into the file, use the current existing
    space if any
    """
    lines: list[bytes] = []

    lines.append(b"/*---")

    for key, value in frontmatter.items():
        if key in ("description", "info"):
            lines.append(b"%s: |" % key.encode("ascii"))
            lines.append(
                yaml.dump(
                    value,
                    encoding="utf8",
                    default_style="|",
                    default_flow_style=False,
                    allow_unicode=True,
                )
                .strip()
                .replace(b"|-\n", b"")
            )
        elif key in ["flags", "includes", "features"]:
            lines.append(
                b"%s: " % key.encode("ascii")
                + yaml.dump(
                    value, encoding="utf8", default_flow_style=True, width=math.inf
                ).strip()
            )
        else:
            lines.append(
                yaml.dump(
                    {key: value}, encoding="utf8", default_flow_style=False
                ).strip()
            )

    lines.append(b"---*/\n")
    source = b"\n".join(lines) + source

    if frontmatter.get("negative", {}).get("phase", "") == "parse":
        source += b"$DONOTEVALUATE();\n"

    return source


def computeMeta(
    source: bytes,
    reftest: "Optional[ReftestEntry]",
    frontmatter: "dict[str, Any]",
    includes: "list[str]",
) -> "dict[str, Any]":
    """
    Captures the reftest meta and a pre-existing meta if any and merge them
    into a single dict.
    """

    if source.startswith((b'"use strict"', b"'use strict'")):
        frontmatter.setdefault("flags", []).append("onlyStrict")

    if b"createIsHTMLDDA" in source:
        frontmatter.setdefault("features", []).append("IsHTMLDDA")

    # Merge the reftest and frontmatter
    merged = mergeMeta(reftest, frontmatter, includes)

    # Cleanup the metadata
    return cleanupMeta(merged)


## convertReportCompare


def convertReportCompare(source: bytes) -> bytes:
    """
    Captures all the reportCompare and convert them accordingly.

    Cases with reportCompare calls where the arguments are the same and one of
    0, true, or null, will be discarded as they are not necessary for Test262.

    Otherwise, reportCompare will be replaced with assert.sameValue, as the
    equivalent in Test262
    """

    def replaceFn(matchobj: "re.Match[bytes]") -> bytes:
        actual: bytes = matchobj.group(4)
        expected: bytes = matchobj.group(5)

        if actual == expected and actual in [b"0", b"true", b"null"]:
            return b""

        return matchobj.group()

    newSource = re.sub(
        rb".*(if \(typeof reportCompare ===? (\"|')function(\"|')\)\s*)?reportCompare\s*\(\s*(\w*)\s*,\s*(\w*)\s*(,\s*\S*)?\s*\)\s*;*\s*",
        replaceFn,
        source,
    )

    return re.sub(rb"\breportCompare\b", b"assert.sameValue", newSource)


def exportTest262(
    outDir: str, providedSrcs: "list[str]", includeShell: bool, baseDir: str
):
    # Create the output directory from scratch.
    print(f"Generating output in {os.path.abspath(outDir)}")
    if os.path.isdir(outDir):
        shutil.rmtree(outDir)

    # only make the includes directory if requested
    includeDir = os.path.join(outDir, "harness-includes")
    if includeShell:
        os.makedirs(includeDir)

    skipped = 0
    skippedDirs = 0

    # Go through each source path
    for providedSrc in providedSrcs:
        src = os.path.abspath(providedSrc)
        if not os.path.isdir(src):
            print(f"Did not find directory {src}")
        # the basename of the path will be used in case multiple "src" arguments
        # are passed in to create an output directory for each "src".
        basename = os.path.basename(src)

        # Process all test directories recursively.
        for dirPath, dirNames, fileNames in os.walk(src):
            # we need to make and get the unique set of includes for this filepath
            includes = []
            if includeShell:
                includes = findAndCopyIncludes(dirPath, baseDir, includeDir)

            relPath = os.path.relpath(dirPath, src)
            fullRelPath = os.path.join(basename, relPath)

            if relPath in UNSUPPORTED_PATHS:
                print("SKIPPED unsupported path %s" % relPath)
                # Prevent recursing into subdirectories
                del dirNames[:]
                skippedDirs += 1
                continue

            # Make new test subdirectory to seperate from includes
            currentOutDir = os.path.join(outDir, "tests", fullRelPath)

            # This also creates the own outDir folder
            if not os.path.exists(currentOutDir):
                os.makedirs(currentOutDir)

            for fileName in fileNames:
                # Skip browser.js files
                if fileName == "browser.js" or fileName == "shell.js":
                    continue

                if fileName.endswith("~"):
                    continue

                filePath = os.path.join(dirPath, fileName)
                testName = os.path.join(
                    fullRelPath, fileName
                )  # captures folder(s)+filename

                # This is hacky, but we're unlikely to add anything new
                # that needs to be skipped this way.
                if relPath + "/" + fileName in UNSUPPORTED_PATHS:
                    print("SKIPPED %s" % testName)
                    skipped += 1
                    continue

                # Copy non-test files as is.
                if "_FIXTURE" in fileName or os.path.splitext(fileName)[1] != ".js":
                    shutil.copyfile(filePath, os.path.join(currentOutDir, fileName))
                    print("C %s" % testName)
                    continue

                # Read the original test source and preprocess it for Test262
                with open(filePath, "rb") as testFile:
                    testSource = testFile.read()

                if not testSource:
                    print("SKIPPED %s" % testName)
                    skipped += 1
                    continue

                skip = skipTest(testSource)
                if skip is not None:
                    print(
                        f"SKIPPED {testName} because file contains {skip.decode('ascii')}"
                    )
                    skipped += 1
                    continue

                try:
                    newSource = convertTestFile(testSource, includes)
                    if newSource is None:
                        print(f"SKIPPED {testName} due to disabled features")
                        skipped += 1
                        continue
                except Exception as e:
                    print(f"SKIPPED {testName} due to error {e}")
                    traceback.print_exc(file=sys.stdout)
                    skipped += 1
                    continue

                with open(os.path.join(currentOutDir, fileName), "wb") as output:
                    output.write(newSource)

                print("SAVED %s" % testName)

    print(f"Skipped {skipped} tests and {skippedDirs} test directories")


if __name__ == "__main__":
    import argparse

    # This script must be run from js/src/tests to work correctly.
    if "/".join(os.path.normpath(os.getcwd()).split(os.sep)[-3:]) != "js/src/tests":
        raise RuntimeError("%s must be run from js/src/tests" % sys.argv[0])

    parser = argparse.ArgumentParser(
        description="Export tests to match Test262 file compliance."
    )
    parser.add_argument(
        "--out",
        default="test262/export",
        help="Output directory. Any existing directory will be removed! "
        "(default: %(default)s)",
    )
    parser.add_argument(
        "--exportshellincludes",
        action="store_true",
        help="Optionally export shell.js files as includes in exported tests. "
        "Only use for testing, do not use for exporting to test262 (test262 tests "
        "should have as few dependencies as possible).",
    )
    parser.add_argument(
        "src", nargs="+", help="Source folder with test files to export"
    )
    args = parser.parse_args()
    exportTest262(
        os.path.abspath(args.out), args.src, args.exportshellincludes, os.getcwd()
    )
