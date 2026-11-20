# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import glob
import itertools
import json
import os
import re
import shlex
import subprocess
import sys
import xml.etree.ElementTree as ET

import mozpack.path as mozpath
from mozlint import result
from mozpack.files import FileFinder

# The Gradle target invocations are serialized with a simple locking file scheme.  It's fine for
# them to take a while, since the first will compile all the Java, etc, and then perform
# potentially expensive static analyses.
GRADLE_LOCK_MAX_WAIT_SECONDS = 20 * 60

EXCLUSION_FILES = [
    os.path.join("tools", "rewriting", "Generated.txt"),
    os.path.join("tools", "rewriting", "ThirdPartyPaths.txt"),
]

DEFAULT_LINT_REPORTS = ("lint-results-debug.sarif", "lint-results-geckoDebug.sarif")
DEFAULT_LINT_MODULES = ("*", "*/*", "*/*/*")


def setup(root, **setupargs):
    if setupargs.get("substs", {}).get("MOZ_BUILD_APP") != "mobile/android":
        return -1

    if "topobjdir" not in setupargs:
        setupargs["log"].debug(
            f"Skipping {setupargs['name']}: a configured Android build is required!"
        )
        return -1

    return 0


def gradle(log, topsrcdir=None, topobjdir=None, tasks=[], extra_args=[], verbose=True):
    sys.path.insert(0, os.path.join(topsrcdir, "mobile", "android"))
    from gradle import gradle_lock

    with gradle_lock(topobjdir, max_wait_seconds=GRADLE_LOCK_MAX_WAIT_SECONDS):
        # The android-lint parameter can be used by gradle tasks to run special
        # logic when they are run for a lint using
        #   project.hasProperty('android-lint')
        cmd_args = (
            [
                sys.executable,
                os.path.join(topsrcdir, "mach"),
                "gradle",
                "--verbose",
                "-Pandroid-lint",
                "--",
            ]
            + tasks
            + extra_args
        )

        cmd = " ".join(shlex.quote(arg) for arg in cmd_args)
        log.debug(cmd)

        # Gradle and mozprocess do not get along well, so we use subprocess
        # directly.
        proc = subprocess.Popen(cmd_args, cwd=topsrcdir)
        status = None
        # Leave it to the subprocess to handle Ctrl+C. If it terminates as a result
        # of Ctrl+C, proc.wait() will return a status code, and, we get out of the
        # loop. If it doesn't, like e.g. gdb, we continue waiting.
        while status is None:
            try:
                status = proc.wait()
            except KeyboardInterrupt:
                pass

        try:
            proc.wait()
        except KeyboardInterrupt:
            proc.kill()
            raise

        return proc.returncode


def format(_paths, config, fix=None, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    if fix:
        tasks = lintargs["substs"]["GRADLE_ANDROID_FORMAT_LINT_FIX_TASKS"]
    else:
        tasks = lintargs["substs"]["GRADLE_ANDROID_FORMAT_LINT_CHECK_TASKS"]

    ret = gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=tasks,
        extra_args=lintargs.get("extra_args") or [],
    )

    message = "Formatting error, please run ./mach lint -l android-format --fix"
    results = []
    for path in lintargs["substs"]["GRADLE_ANDROID_FORMAT_LINT_FOLDERS"]:
        results.extend(
            parse_spotless_results(
                config,
                path,
                "spotlessJava",
                "java",
                "spotless-java",
                message,
                topsrcdir=topsrcdir,
                topobjdir=topobjdir,
            )
        )
        results.extend(
            parse_spotless_results(
                config,
                path,
                "spotlessKotlin",
                "kt",
                "spotless-kt",
                message,
                topsrcdir=topsrcdir,
                topobjdir=topobjdir,
            )
        )

    if len(results) == 0 and ret != 0:
        # spotless seems to hit unfixed error.
        err = {
            "rule": "spotless",
            "path": "",
            "lineno": 0,
            "column": 0,
            "message": "Unexpected error",
            "level": "error",
        }
        results.append(result.from_config(config, **err))

    # If --fix was passed, we just report the number of files that were changed
    if fix:
        return {"results": [], "fixed": len(results)}
    return results


def fenix_format(_paths, config, fix=None, **lintargs):
    return report_gradlew(
        config,
        fix,
        os.path.join("mobile", "android", "fenix"),
        project_name="fenix",
        lint_tasks=[":fenix:lintDebug"],
        lint_modules=("app",),
        disable_android_components_tasks=True,
        **lintargs,
    )


def ac_format(_paths, config, fix=None, **lintargs):
    return report_gradlew(
        config,
        fix,
        os.path.join("mobile", "android", "android-components"),
        project_name="android-components",
        lint_tasks=[":android-components:lint"],
        **lintargs,
    )


def focus_format(_paths, config, fix=None, **lintargs):
    return report_gradlew(
        config,
        fix,
        os.path.join("mobile", "android", "focus-android"),
        project_name="focus-android",
        lint_tasks=[":focus-android:lint"],
        lint_modules=("app",),
        lint_reports=("lint-results-focusDebug.sarif",),
        disable_android_components_tasks=True,
        **lintargs,
    )


def report_gradlew(
    config,
    fix,
    subdir,
    project_name,
    lint_tasks=[],
    lint_reports=DEFAULT_LINT_REPORTS,
    lint_modules=DEFAULT_LINT_MODULES,
    disable_android_components_tasks=False,
    **lintargs,
):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    if fix:
        ktfmt_task = f":{project_name}:ktfmtFormat"
    else:
        ktfmt_task = f":{project_name}:ktfmtCheck"
    tasks = [ktfmt_task, f":{project_name}:detekt"] + list(lint_tasks)

    extra_args = lintargs.get("extra_args") or []
    if disable_android_components_tasks:
        extra_args = extra_args + ["-PdisableAndroidComponentsTasks=true"]

    ret = gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=tasks,
        extra_args=extra_args + ["--continue"],
    )

    reports = os.path.join(topsrcdir, subdir, "build", "reports")
    results = []

    excludes = []
    for path in EXCLUSION_FILES:
        with open(os.path.join(topsrcdir, path)) as fh:
            for f in fh.readlines():
                if "*" in f:
                    excludes.extend(glob.glob(f.strip()))
                elif f.startswith(subdir):
                    excludes.append(f.strip())

    detekt_report = None
    if os.path.exists(os.path.join(reports, "detekt", "detekt.xml")):
        detekt_report = os.path.join(reports, "detekt", "detekt.xml")
    elif os.path.join(
        topobjdir, "gradle", "build", subdir, "reports", "detekt", "detekt.xml"
    ):
        detekt_report = os.path.join(
            topobjdir, "gradle", "build", subdir, "reports", "detekt", "detekt.xml"
        )
    else:
        print(f"Could not read detekt report: '{detekt_report}'")
        pass

    try:
        tree = ET.parse(open(detekt_report))
        root = tree.getroot()

        for file in root.findall("file"):
            name = file.get("name")
            if is_excluded_file(topsrcdir, excludes, name):
                continue
            for error in file:
                err = {
                    "rule": error.get("source"),
                    "path": name,
                    "lineno": int(error.get("line") or 0),
                    "column": int(error.get("column") or 0),
                    "message": error.get("message"),
                    "level": "error",
                }
                results.append(result.from_config(config, **err))
    except FileNotFoundError:
        print(f"Could not read detekt report: '{detekt_report}'")
        pass

    if not fix:
        results.extend(
            parse_ktfmt_results(
                config,
                subdir,
                topsrcdir=topsrcdir,
                topobjdir=topobjdir,
            )
        )

    results.extend(
        parse_lint_report(config, subdir, lint_reports, lint_modules, **lintargs)
    )

    if ret != 0 and not results:
        # One of the tasks failed without emitting anything we could parse.
        err = {
            "level": "error",
            "rule": "build-failure",
            "message": f"Build Failed running {tasks} - Please check logs for more information",
            "path": os.path.join(topsrcdir, subdir),
            "lineno": 0,
        }
        results.append(result.from_config(config, **err))

    return results


def parse_spotless_results(
    config, project_dir, spotless_task, extension, rule, message, topsrcdir, topobjdir
):
    """Parse all files spotless reported need formatting"""
    results = []
    build_dir = os.path.join(topobjdir, "gradle", "build", project_dir)
    for outdir in ("spotless-clean", "spotless-lints"):
        folder = os.path.join(build_dir, outdir, spotless_task)
        for filename in glob.iglob(f"{folder}/**/*.{extension}", recursive=True):
            err = {
                "rule": rule,
                "path": os.path.join(
                    topsrcdir, project_dir, mozpath.relpath(filename, folder)
                ),
                "lineno": 0,
                "column": 0,
                "message": message,
                "level": "error",
            }
            results.append(result.from_config(config, **err))
    return results


def parse_ktfmt_results(config, subdir, topsrcdir=None, topobjdir=None):
    results = []
    # :fenix and :focus-android are rooted at <subdir>/app, :android-components
    # at <subdir> itself.
    for project_dir in (subdir, os.path.join(subdir, "app")):
        results.extend(
            parse_spotless_results(
                config,
                project_dir,
                "spotlessKotlin",
                "kt",
                "ktfmt",
                "Formatting error, please run ./mach lint --fix or ./mach gradle ktfmtFormat",
                topsrcdir=topsrcdir,
                topobjdir=topobjdir,
            )
        )
    return results


def is_excluded_file(topsrcdir, excludes, file):
    for path in excludes:
        if file.startswith(os.path.join(topsrcdir, path)):
            return True
    return False


def api_lint(_paths, config, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=lintargs["substs"]["GRADLE_ANDROID_API_LINT_TASKS"],
        extra_args=lintargs.get("extra_args") or [],
    )

    folder = lintargs["substs"]["GRADLE_ANDROID_GECKOVIEW_APILINT_FOLDER"]

    results = []

    with open(os.path.join(topobjdir, folder, "apilint-result.json")) as f:
        issues = json.load(f)

        for rule in ("compat_failures", "failures"):
            for r in issues[rule]:
                err = {
                    "rule": r["rule"] if rule == "failures" else "compat_failures",
                    "path": r["file"],
                    "lineno": int(r["line"]),
                    "column": int(r.get("column") or 0),
                    "message": r["msg"],
                    "level": "error" if r["error"] else "warning",
                }
                results.append(result.from_config(config, **err))

        for r in issues["api_changes"]:
            err = {
                "rule": "api_changes",
                "path": r["file"],
                "lineno": int(r["line"]),
                "column": int(r.get("column") or 0),
                "message": "Unexpected api change. Please run ./mach gradle {} for more "
                "information".format(
                    " ".join(lintargs["substs"]["GRADLE_ANDROID_API_LINT_TASKS"])
                ),
            }
            results.append(result.from_config(config, **err))

    return results


def javadoc(_paths, config, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=lintargs["substs"]["GRADLE_ANDROID_GECKOVIEW_DOCS_TASKS"],
        extra_args=lintargs.get("extra_args") or [],
    )

    output_files = lintargs["substs"]["GRADLE_ANDROID_GECKOVIEW_DOCS_OUTPUT_FILES"]

    results = []

    for output_file in output_files:
        with open(os.path.join(topobjdir, output_file)) as f:
            # Like: '[{"path":"/absolute/path/to/topsrcdir/mobile/android/geckoview/src/main/java/org/mozilla/geckoview/ContentBlocking.java","lineno":"462","level":"warning","message":"no @return"}]'.  # NOQA: E501
            issues = json.load(f)

            for issue in issues:
                # We want all warnings to be errors for linting purposes.
                # This ensures javadoc warnings can no longer be ignored.
                issue["level"] = "error"
                results.append(result.from_config(config, **issue))

    return results


def lint(_paths, config, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=lintargs["substs"]["GRADLE_ANDROID_LINT_TASKS"],
        extra_args=(lintargs.get("extra_args") or []) + ["--continue"],
    )

    gradle_build = os.path.join(topobjdir, "gradle/build/mobile/android")

    results = []

    def collect(path):
        tree = ET.parse(open(path))
        root = tree.getroot()
        for issue in root.findall("issue"):
            location = issue[0]
            if "third_party" in location.get("file") or "thirdparty" in location.get(
                "file"
            ):
                continue
            err = {
                "level": issue.get("severity").lower(),
                "rule": issue.get("id"),
                "message": issue.get("message"),
                "path": location.get("file"),
                "lineno": int(location.get("line") or 0),
            }
            results.append(result.from_config(config, **err))

    collect(
        os.path.join(
            gradle_build,
            "geckoview/reports",
            "lint-results-{}.xml".format(
                lintargs["substs"]["GRADLE_ANDROID_GECKOVIEW_VARIANT_NAME"]
            ),
        )
    )

    # The included Gradle plugin builds carry the AndroidX lint-gradle checks and
    # each emit their own lint report.
    for plugin in (
        "gradle/plugins/conventions",
        "android-components/plugins/dependencies",
        "android-components/plugins/publicsuffixlist",
        "fenix/plugins/apksize",
    ):
        report = os.path.join(gradle_build, plugin, "reports/lint-results.xml")
        if os.path.exists(report):
            collect(report)

    return results


def parse_lint_report(config, subdir, lint_reports, lint_modules, **lintargs):
    topsrcdir = lintargs["root"]

    build = os.path.join(lintargs["topobjdir"], "gradle", "build", subdir)

    excludes = []
    for path in EXCLUSION_FILES:
        with open(os.path.join(topsrcdir, path)) as fh:
            for f in fh.readlines():
                if "*" in f:
                    excludes.extend(glob.glob(f.strip()))
                elif f.startswith(subdir):
                    excludes.append(f.strip())

    try:
        files = [
            report
            for module in lint_modules
            for reports in glob.glob(
                os.path.join(glob.escape(build), module, "reports")
            )
            for name in lint_reports
            if os.path.exists(report := os.path.join(reports, name))
        ]
        if not files:
            raise FileNotFoundError(build)

        results = []
        for file in files:
            with open(file) as fh:
                data = json.load(fh).get("runs", [{}])[0]

            issues = data.get("results", [])
            rules = data.get("tool", {}).get("driver", {}).get("rules", [])

            for issue in issues:
                dir = os.path.join(topsrcdir)
                if subdir == os.path.join("mobile", "android", "android-components"):
                    dir = os.path.join(topsrcdir, subdir)
                name = os.path.join(
                    dir,
                    issue
                    .get("locations", [{}])[0]
                    .get("physicalLocation", {})
                    .get("artifactLocation", {})
                    .get("uri"),
                )

                if is_excluded_file(topsrcdir, excludes, name) and not "/res/" in name:
                    continue

                level = "error"
                if "level" in issue:
                    level = issue["level"]
                elif "ruleIndex" in issue and len(rules) > issue["ruleIndex"]:
                    rule_level = (
                        rules[issue["ruleIndex"]]
                        .get("defaultConfiguration", {})
                        .get("level")
                    )
                    if rule_level:
                        level = rule_level

                err = {
                    "rule": issue.get("ruleId"),
                    "path": name,
                    "lineno": issue
                    .get("locations", [{}])[0]
                    .get("physicalLocation", {})
                    .get("region", {})
                    .get("startLine"),
                    "column": issue
                    .get("locations", [{}])[0]
                    .get("physicalLocation", {})
                    .get("region", {})
                    .get("startColumn"),
                    "message": issue.get("message", {}).get("text"),
                    "level": level,
                }
                results.append(result.from_config(config, **err))

        return results
    except FileNotFoundError:
        err = {
            "level": "error",
            "rule": "build-failure",
            "message": f"Lint reports were not generated for {subdir} - Please check logs for more information",
            "path": os.path.join(topsrcdir, subdir),
            "lineno": 0,
        }
        return [result.from_config(config, **err)]


def _parse_checkstyle_output(config, topsrcdir=None, report_path=None):
    tree = ET.parse(open(report_path))
    root = tree.getroot()

    for file in root.findall("file"):
        for error in file.findall("error"):
            # Like <error column="42" line="22" message="Name 'mPorts' must match pattern 'xm[A-Z][A-Za-z]*$'." severity="error" source="com.puppycrawl.tools.checkstyle.checks.naming.MemberNameCheck" />.  # NOQA: E501
            err = {
                "level": "error",
                "rule": error.get("source"),
                "message": error.get("message"),
                "path": file.get("name"),
                "lineno": int(error.get("line") or 0),
                "column": int(error.get("column") or 0),
            }
            yield result.from_config(config, **err)


def checkstyle(_paths, config, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=lintargs["substs"]["GRADLE_ANDROID_CHECKSTYLE_TASKS"],
        extra_args=lintargs.get("extra_args") or [],
    )

    results = []

    for relative_path in lintargs["substs"]["GRADLE_ANDROID_CHECKSTYLE_OUTPUT_FILES"]:
        report_path = os.path.join(lintargs["topobjdir"], relative_path)
        results.extend(
            _parse_checkstyle_output(
                config, topsrcdir=lintargs["root"], report_path=report_path
            )
        )

    return results


def _parse_android_test_results(config, topsrcdir=None, report_dir=None):
    # A brute force way to turn a Java FQN into a path on disk.  Assumes Java
    # and Kotlin sources are in mobile/android for performance and simplicity.
    sourcepath_finder = FileFinder(os.path.join(topsrcdir, "mobile", "android"))

    finder = FileFinder(report_dir)
    reports = list(finder.find("TEST-*.xml"))
    if not reports:
        raise RuntimeError(f"No reports found under {report_dir}")

    for report, _ in reports:
        tree = ET.parse(open(os.path.join(finder.base, report)))
        root = tree.getroot()

        class_name = root.get(
            "name"
        )  # Like 'org.mozilla.gecko.permissions.TestPermissions'.
        path = (
            "**/" + class_name.replace(".", "/") + ".*"
        )  # Like '**/org/mozilla/gecko/permissions/TestPermissions.*'.  # NOQA: E501

        for testcase in root.findall("testcase"):
            function_name = testcase.get("name")

            # Schema cribbed from http://llg.cubic.org/docs/junit/.
            for unexpected in itertools.chain(
                testcase.findall("error"), testcase.findall("failure")
            ):
                sourcepaths = list(sourcepath_finder.find(path))
                if not sourcepaths:
                    raise RuntimeError(f"No sourcepath found for class {class_name}")

                for sourcepath, _ in sourcepaths:
                    lineno = 0
                    message = unexpected.get("message")
                    # Turn '... at org.mozilla.gecko.permissions.TestPermissions.testMultipleRequestsAreQueuedAndDispatchedSequentially(TestPermissions.java:118)' into 118.  # NOQA: E501
                    pattern = r"at {class_name}\.{function_name}\(.*:(\d+)\)"
                    pattern = pattern.format(
                        class_name=class_name, function_name=function_name
                    )
                    match = re.search(pattern, message)
                    if match:
                        lineno = int(match.group(1))
                    else:
                        msg = f"No source line found for {class_name}.{function_name}"
                        raise RuntimeError(msg)

                    err = {
                        "level": "error",
                        "rule": unexpected.get("type"),
                        "message": message,
                        "path": os.path.join(
                            topsrcdir, "mobile", "android", sourcepath
                        ),
                        "lineno": lineno,
                    }
                    yield result.from_config(config, **err)


def test(_paths, config, **lintargs):
    topsrcdir = lintargs["root"]
    topobjdir = lintargs["topobjdir"]

    gradle(
        lintargs["log"],
        topsrcdir=topsrcdir,
        topobjdir=topobjdir,
        tasks=lintargs["substs"]["GRADLE_ANDROID_TEST_TASKS"],
        extra_args=lintargs.get("extra_args") or [],
    )

    results = []

    def capitalize(s):
        # Can't use str.capitalize because it lower cases trailing letters.
        return (s[0].upper() + s[1:]) if s else ""

    pairs = [("geckoview", lintargs["substs"]["GRADLE_ANDROID_GECKOVIEW_VARIANT_NAME"])]
    for project, variant in pairs:
        report_dir = os.path.join(
            lintargs["topobjdir"],
            f"gradle/build/mobile/android/{project}/test-results/test{capitalize(variant)}UnitTest",
        )
    results.extend(
        _parse_android_test_results(
            config, topsrcdir=lintargs["root"], report_dir=report_dir
        )
    )

    return results
