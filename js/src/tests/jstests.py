#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
The JS Shell Test Harness.

See the adjacent README.txt for more details.
"""

import math
import os
import platform
import posixpath
import re
import shlex
import sys
import tempfile
from contextlib import contextmanager
from copy import (
    copy,
    deepcopy,
)
from datetime import datetime
from itertools import chain
from os.path import abspath, dirname, isfile, realpath
from subprocess import call, list2cmdline

from lib.adaptor import xdr_annotate
from lib.progressbar import ProgressBar
from lib.results import ResultsSink, TestOutput
from lib.tempfile import TemporaryDirectory
from lib.tests import (
    RefTestCase,
    change_env,
    get_cpu_count,
    get_environment_overlay,
    get_jitflags,
)

if sys.platform.startswith("linux") or sys.platform.startswith("darwin"):
    from lib.tasks_unix import run_all_tests
else:
    from lib.tasks_win import run_all_tests

here = dirname(abspath(__file__))


@contextmanager
def changedir(dirname):
    pwd = os.getcwd()
    os.chdir(dirname)
    try:
        yield
    finally:
        os.chdir(pwd)


class PathOptions:
    def __init__(self, topsrc_dir, location, requested_paths, excluded_paths):
        self.topsrc_dir = topsrc_dir
        self.test_prefix = (
            os.path.abspath(os.path.relpath(location, topsrc_dir)) + os.sep
        )
        self.requested_paths = requested_paths
        self.excluded_files, self.excluded_dirs = PathOptions._split_files_and_dirs(
            location, excluded_paths
        )

    @staticmethod
    def _split_files_and_dirs(location, paths):
        """Split up a set of paths into files and directories"""
        files, dirs = set(), set()
        for path in paths:
            fullpath = os.path.join(location, path)
            if path.endswith("/"):
                dirs.add(path[:-1])
            elif os.path.isdir(fullpath):
                dirs.add(path)
            elif os.path.exists(fullpath):
                files.add(path)

        return files, dirs

    def should_run(self, filename):
        # If any tests are requested by name, skip tests that do not match.
        if self.requested_paths and not any(
            req in (self.test_prefix + filename) for req in self.requested_paths
        ):
            return False

        # Skip excluded tests.
        if filename in self.excluded_files:
            return False

        for dir in self.excluded_dirs:
            if filename.startswith(dir + "/"):
                return False

        return True


def parse_args():
    """
    Parse command line arguments.
    Returns a tuple of: (options, js_shell, requested_paths, excluded_paths)
        options :object: The raw OptionParser output.
        js_shell :str: The absolute location of the shell to test with.
        requested_paths :set<str>: Test paths specially requested on the CLI.
        excluded_paths :set<str>: Test paths specifically excluded by the CLI.
    """
    from argparse import ArgumentParser

    op = ArgumentParser(
        description="Run jstests JS shell tests",
        epilog="Shell output format: [ pass | fail | timeout | skip ] progress | time",
    )
    op.add_argument(
        "--xul-info",
        dest="xul_info_src",
        help="config data for xulRuntime (avoids search for config/autoconf.mk)",
    )

    harness_og = op.add_argument_group("Harness Controls", "Control how tests are run.")
    harness_og.add_argument(
        "-j",
        "--worker-count",
        type=int,
        default=max(1, get_cpu_count()),
        help="Number of tests to run in parallel (default %(default)s)",
    )
    harness_og.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=150.0,
        help="Set maximum time a test is allows to run (in seconds).",
    )
    harness_og.add_argument(
        "--show-slow",
        action="store_true",
        help="Show tests taking longer than a minimum time (in seconds).",
    )
    harness_og.add_argument(
        "--slow-test-threshold",
        type=float,
        default=5.0,
        help="Time in seconds a test can take until it is"
        "considered slow (default %(default)s).",
    )
    harness_og.add_argument(
        "-a",
        "--args",
        dest="shell_args",
        default=[],
        action="append",
        help="Extra args to pass to the JS shell.",
    )
    harness_og.add_argument(
        "--feature-args",
        dest="feature_args",
        default=[],
        action="append",
        help="Extra args to pass to the JS shell even when feature-testing.",
    )
    harness_og.add_argument(
        "--jitflags",
        dest="jitflags",
        default="none",
        type=str,
        help="IonMonkey option combinations. One of all,"
        " debug, ion, and none (default %(default)s).",
    )
    harness_og.add_argument(
        "--tbpl",
        action="store_true",
        help="Runs each test in all configurations tbpl tests.",
    )
    harness_og.add_argument(
        "--tbpl-debug",
        action="store_true",
        help="Runs each test in some faster configurations tbpl tests.",
    )
    harness_og.add_argument(
        "-g", "--debug", action="store_true", help="Run a test in debugger."
    )
    harness_og.add_argument(
        "--debugger", default="gdb -q --args", help="Debugger command."
    )
    harness_og.add_argument(
        "-J", "--jorendb", action="store_true", help="Run under JS debugger."
    )
    harness_og.add_argument(
        "--passthrough",
        action="store_true",
        help="Run tests with stdin/stdout attached to caller.",
    )
    harness_og.add_argument(
        "--test-reflect-stringify",
        dest="test_reflect_stringify",
        help="instead of running tests, use them to test the "
        "Reflect.stringify code in specified file",
    )
    harness_og.add_argument(
        "--valgrind", action="store_true", help="Run tests in valgrind."
    )
    harness_og.add_argument(
        "--valgrind-args", default="", help="Extra args to pass to valgrind."
    )
    harness_og.add_argument(
        "--rr",
        action="store_true",
        help="Run tests under RR record-and-replay debugger.",
    )
    harness_og.add_argument(
        "-C",
        "--check-output",
        action="store_true",
        help="Run tests to check output for different jit-flags",
    )
    harness_og.add_argument(
        "--remote", action="store_true", help="Run tests on a remote device"
    )
    harness_og.add_argument(
        "--deviceIP",
        action="store",
        type=str,
        dest="device_ip",
        help="IP address of remote device to test",
    )
    harness_og.add_argument(
        "--devicePort",
        action="store",
        type=int,
        dest="device_port",
        default=20701,
        help="port of remote device to test",
    )
    harness_og.add_argument(
        "--deviceSerial",
        action="store",
        type=str,
        dest="device_serial",
        default=None,
        help="ADB device serial number of remote device to test",
    )
    harness_og.add_argument(
        "--remoteTestRoot",
        dest="remote_test_root",
        action="store",
        type=str,
        default="/data/local/tmp/test_root",
        help="The remote directory to use as test root (e.g. %(default)s)",
    )
    harness_og.add_argument(
        "--localLib",
        dest="local_lib",
        action="store",
        type=str,
        help="The location of libraries to push -- preferably stripped",
    )
    harness_og.add_argument(
        "--no-xdr",
        dest="use_xdr",
        action="store_false",
        help="Whether to disable caching of self-hosted parsed content in XDR format.",
    )

    input_og = op.add_argument_group("Inputs", "Change what tests are run.")
    input_og.add_argument(
        "-f",
        "--file",
        dest="test_file",
        action="append",
        help="Get tests from the given file.",
    )
    input_og.add_argument(
        "-x",
        "--exclude-file",
        action="append",
        help="Exclude tests from the given file.",
    )
    input_og.add_argument(
        "--wpt",
        dest="wpt",
        choices=["enabled", "disabled", "if-running-everything"],
        default="if-running-everything",
        help="Enable or disable shell web-platform-tests "
        "(default: enable if no test paths are specified).",
    )
    input_og.add_argument(
        "--include",
        action="append",
        dest="requested_paths",
        default=[],
        help="Include the given test file or directory.",
    )
    input_og.add_argument(
        "--exclude",
        action="append",
        dest="excluded_paths",
        default=[],
        help="Exclude the given test file or directory.",
    )
    input_og.add_argument(
        "-d",
        "--exclude-random",
        dest="random",
        action="store_false",
        help='Exclude tests marked as "random."',
    )
    input_og.add_argument(
        "--run-skipped", action="store_true", help='Run tests marked as "skip."'
    )
    input_og.add_argument(
        "--run-only-skipped",
        action="store_true",
        help='Run only tests marked as "skip."',
    )
    input_og.add_argument(
        "--run-slow-tests",
        action="store_true",
        help='Do not skip tests marked as "slow."',
    )
    input_og.add_argument(
        "--no-extensions",
        action="store_true",
        help="Run only tests conforming to the ECMAScript 5 standard.",
    )
    input_og.add_argument(
        "--repeat", type=int, default=1, help="Repeat tests the given number of times."
    )

    output_og = op.add_argument_group("Output", "Modify the harness and tests output.")
    output_og.add_argument(
        "-s",
        "--show-cmd",
        action="store_true",
        help="Show exact commandline used to run each test.",
    )
    output_og.add_argument(
        "-o",
        "--show-output",
        action="store_true",
        help="Print each test's output to the file given by --output-file.",
    )
    output_og.add_argument(
        "-F",
        "--failed-only",
        action="store_true",
        help="If a --show-* option is given, only print output for failed tests.",
    )
    output_og.add_argument(
        "--no-show-failed",
        action="store_true",
        help="Don't print output for failed tests (no-op with --show-output).",
    )
    output_og.add_argument(
        "-O",
        "--output-file",
        help="Write all output to the given file (default: stdout).",
    )
    output_og.add_argument(
        "--failure-file", help="Write all not-passed tests to the given file."
    )
    output_og.add_argument(
        "--no-progress",
        dest="hide_progress",
        action="store_true",
        help="Do not show the progress bar.",
    )
    output_og.add_argument(
        "--tinderbox",
        dest="format",
        action="store_const",
        const="automation",
        help="Use automation-parseable output format.",
    )
    output_og.add_argument(
        "--format",
        dest="format",
        default="none",
        choices=["automation", "none"],
        help="Output format. Either automation or none (default %(default)s).",
    )
    output_og.add_argument(
        "--log-wptreport",
        dest="wptreport",
        action="store",
        help="Path to write a Web Platform Tests report (wptreport)",
    )
    output_og.add_argument(
        "--this-chunk", type=int, default=1, help="The test chunk to run."
    )
    output_og.add_argument(
        "--total-chunks", type=int, default=1, help="The total number of test chunks."
    )

    special_og = op.add_argument_group(
        "Special", "Special modes that do not run tests."
    )
    special_og.add_argument(
        "--make-manifests",
        metavar="BASE_TEST_PATH",
        help="Generate reftest manifest files.",
    )

    op.add_argument("--js-shell", metavar="JS_SHELL", help="JS shell to run tests with")
    op.add_argument(
        "-z", "--gc-zeal", help="GC zeal mode to use when running the shell"
    )

    options, args = op.parse_known_args()

    # Need a shell unless in a special mode.
    if not options.make_manifests:
        if not args:
            op.error("missing JS_SHELL argument")
        options.js_shell = os.path.abspath(args.pop(0))

    requested_paths = set(args)

    # Valgrind, gdb, and rr are mutually exclusive.
    if sum(map(bool, (options.valgrind, options.debug, options.rr))) > 1:
        op.error("--valgrind, --debug, and --rr are mutually exclusive.")

    # Fill the debugger field, as needed.
    if options.debug:
        if options.debugger == "lldb":
            debugger_prefix = ["lldb", "--"]
        else:
            debugger_prefix = options.debugger.split()
    else:
        debugger_prefix = []

    if options.valgrind:
        debugger_prefix = ["valgrind"] + options.valgrind_args.split()
        if os.uname()[0] == "Darwin":
            debugger_prefix.append("--dsymutil=yes")
        options.show_output = True
    if options.rr:
        debugger_prefix = ["rr", "record"]

    js_cmd_args = split_extra_shell_args(options.shell_args + options.feature_args)
    if options.jorendb:
        options.passthrough = True
        options.hide_progress = True
        options.worker_count = 1
        debugger_path = realpath(
            os.path.join(
                abspath(dirname(abspath(__file__))),
                "..",
                "..",
                "examples",
                "jorendb.js",
            )
        )
        js_cmd_args.extend(["-d", "-f", debugger_path, "--"])
    prefix = RefTestCase.build_js_cmd_prefix(
        options.js_shell, js_cmd_args, debugger_prefix
    )

    # If files with lists of tests to run were specified, add them to the
    # requested tests set.
    if options.test_file:
        for test_file in options.test_file:
            requested_paths |= set([
                line.strip() for line in open(test_file).readlines()
            ])

    excluded_paths = set(options.excluded_paths)

    # If files with lists of tests to exclude were specified, add them to the
    # excluded tests set.
    if options.exclude_file:
        for filename in options.exclude_file:
            with open(filename) as fp:
                for line in fp:
                    if line.startswith("#"):
                        continue
                    line = line.strip()
                    if not line:
                        continue
                    excluded_paths.add(line)

    # Handle output redirection, if requested and relevant.
    options.output_fp = sys.stdout
    if options.output_file:
        if not options.show_cmd:
            options.show_output = True
        try:
            options.output_fp = open(options.output_file, "w")
        except OSError as ex:
            raise SystemExit("Failed to open output file: " + str(ex))

    # Hide the progress bar if it will get in the way of other output.
    options.hide_progress = (
        options.format == "automation"
        or not ProgressBar.conservative_isatty()
        or options.hide_progress
    )

    return (options, prefix, requested_paths, excluded_paths)


def load_wpt_tests(xul_tester, requested_paths, excluded_paths, update_manifest=True):
    """Return a list of `RefTestCase` objects for the jsshell testharness.js
    tests filtered by the given paths and debug-ness."""
    repo_root = abspath(os.path.join(here, "..", "..", ".."))
    wp = os.path.join(repo_root, "testing", "web-platform")
    wpt = os.path.join(wp, "tests")

    sys_paths = [
        "python/mozterm",
        "python/mozboot",
        "testing/mozbase/mozcrash",
        "testing/mozbase/mozdevice",
        "testing/mozbase/mozfile",
        "testing/mozbase/mozinfo",
        "testing/mozbase/mozleak",
        "testing/mozbase/mozlog",
        "testing/mozbase/mozprocess",
        "testing/mozbase/mozprofile",
        "testing/mozbase/mozrunner",
        "testing/mozbase/mozversion",
        "testing/web-platform/",
        "testing/web-platform/tests",
        "testing/web-platform/tests/tools",
        "testing/web-platform/tests/tools/third_party/html5lib",
        "testing/web-platform/tests/tools/third_party/webencodings",
        "testing/web-platform/tests/tools/wptrunner",
        "testing/web-platform/tests/tools/wptserve",
        "third_party/python/requests",
    ]
    abs_sys_paths = [os.path.join(repo_root, path) for path in sys_paths]

    failed = False
    for path in abs_sys_paths:
        if not os.path.isdir(path):
            failed = True
            print("Could not add '%s' to the path")
    if failed:
        return []

    sys.path[0:0] = abs_sys_paths

    import manifestupdate
    from wptrunner import products, testloader, wptcommandline, wptlogging, wpttest

    manifest_root = tempfile.gettempdir()
    (maybe_dist, maybe_bin) = os.path.split(os.path.dirname(xul_tester.js_bin))
    if maybe_bin == "bin":
        (maybe_root, maybe_dist) = os.path.split(maybe_dist)
        if maybe_dist == "dist":
            if os.path.exists(os.path.join(maybe_root, "_tests")):
                # Assume this is a gecko objdir.
                manifest_root = maybe_root

    logger = wptlogging.setup({}, {})

    test_manifests = manifestupdate.run(
        repo_root, manifest_root, logger, update=update_manifest
    )

    kwargs = vars(wptcommandline.create_parser().parse_args([]))
    kwargs.update({
        "config": os.path.join(
            manifest_root, "_tests", "web-platform", "wptrunner.local.ini"
        ),
        "gecko_e10s": False,
        "product": "firefox",
        "verify": False,
        "wasm": xul_tester.test("wasmIsSupported()"),
    })
    wptcommandline.set_from_config(kwargs)

    def filter_jsshell_tests(it):
        for item_type, path, tests in it:
            tests = set(item for item in tests if item.jsshell)
            if tests:
                yield item_type, path, tests

    run_info_extras = products.Product.from_product_name("firefox").run_info_extras(
        logger, **kwargs
    )
    run_info = wpttest.get_run_info(
        kwargs["run_info"],
        "firefox",
        debug=xul_tester.test("isDebugBuild"),
        extras=run_info_extras,
    )
    release_or_beta = xul_tester.test("getBuildConfiguration('release_or_beta')")
    run_info["release_or_beta"] = release_or_beta
    run_info["nightly_build"] = not release_or_beta
    # Still referenced by WPT metadata (wasm/jsapi/memory/constructor.any.js.ini).
    run_info["early_beta_or_earlier"] = not release_or_beta

    path_filter = testloader.TestFilter(
        test_manifests, include=requested_paths, exclude=excluded_paths
    )
    subsuites = testloader.load_subsuites(logger, run_info, None, set())
    loader = testloader.TestLoader(
        logger,
        test_manifests,
        ["testharness"],
        run_info,
        subsuites=subsuites,
        manifest_filters=[path_filter, filter_jsshell_tests],
    )

    extra_helper_paths = [
        os.path.join(here, "web-platform-test-shims.js"),
        os.path.join(wpt, "resources", "testharness.js"),
        os.path.join(here, "testharnessreport.js"),
    ]

    pref_prefix = "javascript.options."
    recognized_prefs = set([
        "wasm_js_promise_integration",
        "experimental.wasm_esm_integration",
    ])
    nightly_only_prefs = set([
        "experimental.wasm_esm_integration",
    ])

    def resolve(test_path, script):
        if script.startswith("/"):
            return os.path.join(wpt, script[1:])

        return os.path.join(wpt, os.path.dirname(test_path), script)

    tests = []
    for test in loader.tests[""]["testharness"]:
        test_path = os.path.relpath(test.path, wpt)
        scripts = [resolve(test_path, s) for s in test.scripts]
        extra_helper_paths_for_test = extra_helper_paths + scripts

        # We must create at least one test with the default options, along with
        # one test for each option given in a test-also annotation.
        variants = [None]
        flags = []
        for m in test.itermeta():
            # Search for prefs to enable that we recognize
            for pref in m.prefs:
                pref_value = m.prefs[pref]
                if not pref.startswith(pref_prefix):
                    continue
                short_pref = pref.replace(pref_prefix, "")
                if not short_pref in recognized_prefs:
                    continue
                if release_or_beta and short_pref in nightly_only_prefs:
                    continue
                flags.append("--setpref=" + short_pref + "=" + pref_value)

            if m.has_key("test-also"):  # NOQA: W601
                variants += m.get("test-also").split()
        for variant in variants:
            test_case = RefTestCase(
                wpt,
                test_path,
                extra_helper_paths=extra_helper_paths_for_test[:],
                wpt=test,
            )
            if variant:
                test_case.options.append(variant)
            for flag in flags:
                test_case.options.append(flag)
            tests.append(test_case)
    return tests


def split_extra_shell_args(args):
    result = []
    for option in args:
        result.extend(shlex.split(option))
    return result


def load_tests(options, requested_paths, excluded_paths):
    """
    Returns a tuple: (test_count, test_gen)
        test_count: [int] Number of tests that will be in test_gen
        test_gen: [iterable<Test>] Tests found that should be run.
    """
    from lib import manifest

    if options.js_shell is None:
        xul_tester = manifest.NullXULInfoTester()
    else:
        if options.xul_info_src is None:
            xul_info = manifest.XULInfo.create(options.js_shell)
        else:
            xul_abi, xul_os, xul_debug = options.xul_info_src.split(r":")
            xul_debug = xul_debug.lower() == "true"
            xul_info = manifest.XULInfo(xul_abi, xul_os, xul_debug)
        feature_args = split_extra_shell_args(options.feature_args)
        xul_tester = manifest.XULInfoTester(xul_info, options, feature_args)

    test_dir = dirname(abspath(__file__))

    # Given this file is js/src/tests/jstests.py, let's compute
    # the base path directory as test_dir/../../../
    #
    # This has precedent in WPT loading and jorendb stuff
    # despite being surprising :)
    topsrc_dir = os.path.normpath(os.path.join(test_dir, "..", "..", ".."))

    path_options = PathOptions(topsrc_dir, test_dir, requested_paths, excluded_paths)
    test_count = manifest.count_tests(test_dir, path_options)
    test_gen = manifest.load_reftests(test_dir, path_options, xul_tester)

    # WPT tests are already run in the browser in their own harness.
    wpt_enabled = options.wpt == "enabled" or (
        options.wpt == "if-running-everything"
        and len(requested_paths) == 0
        and not options.make_manifests
    )
    if wpt_enabled:
        wpt_tests = load_wpt_tests(xul_tester, requested_paths, excluded_paths)
        test_count += len(wpt_tests)
        test_gen = chain(test_gen, wpt_tests)

    if options.test_reflect_stringify is not None:

        def trs_gen(tests):
            for test in tests:
                test.test_reflect_stringify = options.test_reflect_stringify
                # Even if the test is not normally expected to pass, we still
                # expect reflect-stringify to be able to handle it.
                test.expect = True
                test.random = False
                test.slow = False
                yield test

        test_gen = trs_gen(test_gen)

    if options.make_manifests:
        manifest.make_manifests(options.make_manifests, test_gen)
        sys.exit()

    # Create a new test list. Apply each TBPL configuration to every test.
    flags_list = None
    if options.tbpl:
        flags_list = get_jitflags("all")
    elif options.tbpl_debug:
        flags_list = get_jitflags("debug")
    else:
        flags_list = get_jitflags(options.jitflags, none=None)

    if flags_list:

        def flag_gen(tests):
            for test in tests:
                for jitflags in flags_list:
                    tmp_test = copy(test)
                    tmp_test.jitflags = copy(test.jitflags)
                    tmp_test.jitflags.extend(jitflags)
                    yield tmp_test

        test_count = test_count * len(flags_list)
        test_gen = flag_gen(test_gen)

    if options.test_file:
        paths = set()
        for test_file in options.test_file:
            paths |= set([line.strip() for line in open(test_file).readlines()])
        test_gen = (_ for _ in test_gen if _.path in paths)

    if options.no_extensions:
        pattern = os.sep + "extensions" + os.sep
        test_gen = (_ for _ in test_gen if pattern not in _.path)

    if not options.random:
        test_gen = (_ for _ in test_gen if not _.random)

    if options.run_only_skipped:
        options.run_skipped = True
        test_gen = (_ for _ in test_gen if not _.enable)

    if not options.run_slow_tests:
        test_gen = (_ for _ in test_gen if not _.slow)

    if options.repeat:
        test_gen = (deepcopy(test) for test in test_gen for _ in range(options.repeat))
        test_count *= options.repeat

    return test_count, test_gen


def main():
    options, prefix, requested_paths, excluded_paths = parse_args()
    if options.js_shell is not None and not (
        isfile(options.js_shell) and os.access(options.js_shell, os.X_OK)
    ):
        if (
            platform.system() != "Windows"
            or isfile(options.js_shell)
            or not isfile(options.js_shell + ".exe")
            or not os.access(options.js_shell + ".exe", os.X_OK)
        ):
            print("Could not find executable shell: " + options.js_shell)
            return 1

    test_count, test_gen = load_tests(options, requested_paths, excluded_paths)
    test_environment = get_environment_overlay(options.js_shell, options.gc_zeal)

    if test_count == 0:
        print("no tests selected")
        return 1

    test_dir = dirname(abspath(__file__))

    if options.debug:
        if test_count > 1:
            print(
                "Multiple tests match command line arguments, debugger can only run one"
            )
            for tc in test_gen:
                print(f"    {tc.path}")
            return 2

        with changedir(test_dir), change_env(
            test_environment
        ), TemporaryDirectory() as tempdir:
            cmd = next(test_gen).get_command(prefix, tempdir)
            if options.show_cmd:
                print(list2cmdline(cmd))
            call(cmd)
        return 0

    # The test_gen generator is converted into a list in
    # run_all_tests. Go ahead and do it here so we can apply
    # chunking.
    #
    # If chunking is enabled, determine which tests are part of this chunk.
    # This code was adapted from testing/mochitest/runtestsremote.py.
    if options.total_chunks > 1:
        tests_per_chunk = math.ceil(test_count / float(options.total_chunks))
        start = int(round((options.this_chunk - 1) * tests_per_chunk))
        end = int(round(options.this_chunk * tests_per_chunk))
        test_gen = list(test_gen)[start:end]

    if options.remote:
        results = ResultsSink("jstests", options, test_count)
        try:
            from lib.remote import init_device, init_remote_dir

            device = init_device(options)
            tempdir = posixpath.join(options.remote_test_root, "tmp")
            jtd_tests = posixpath.join(options.remote_test_root, "tests", "tests")
            init_remote_dir(device, jtd_tests)
            device.push(test_dir, jtd_tests, timeout=600)
            device.chmod(jtd_tests, recursive=True)
            prefix[0] = options.js_shell
            if options.use_xdr:
                test_gen = xdr_annotate(test_gen, options)
            for test in test_gen:
                out = run_test_remote(test, device, prefix, tempdir, options)
                results.push(out)
            results.finish(True)
        except KeyboardInterrupt:
            results.finish(False)

        return 0 if results.all_passed() else 1

    with changedir(test_dir), change_env(
        test_environment
    ), TemporaryDirectory() as tempdir:
        results = ResultsSink("jstests", options, test_count)
        try:
            for out in run_all_tests(test_gen, prefix, tempdir, results.pb, options):
                results.push(out)
            results.finish(True)
        except KeyboardInterrupt:
            results.finish(False)

        return 0 if results.all_passed() else 1

    return 0


def run_test_remote(test, device, prefix, tempdir, options):
    from mozdevice import ADBDevice, ADBProcessError

    cmd = test.get_command(prefix, tempdir)
    test_root_parent = os.path.dirname(test.root)
    jtd_tests = posixpath.join(options.remote_test_root, "tests")
    cmd = [_.replace(test_root_parent, jtd_tests) for _ in cmd]

    env = {"TZ": "PST8PDT", "LD_LIBRARY_PATH": os.path.dirname(prefix[0])}

    adb_cmd = ADBDevice._escape_command_line(cmd)
    start = datetime.now()
    try:
        # Allow ADBError or ADBTimeoutError to terminate the test run,
        # but handle ADBProcessError in order to support the use of
        # non-zero exit codes in the JavaScript shell tests.
        out = device.shell_output(
            adb_cmd, env=env, cwd=options.remote_test_root, timeout=int(options.timeout)
        )
        returncode = 0
    except ADBProcessError as e:
        # Treat ignorable intermittent adb communication errors as
        # skipped tests.
        out = str(e.adb_process.stdout)
        returncode = e.adb_process.exitcode
        re_ignore = re.compile(r"error: (closed|device .* not found)")
        if returncode == 1 and re_ignore.search(out):
            print(f"Skipping {test.path} due to ignorable adb error {out}")
            test.skip_if_cond = "true"
            returncode = test.SKIPPED_EXIT_STATUS

    elapsed = (datetime.now() - start).total_seconds()

    # We can't distinguish between stdout and stderr so we pass
    # the same buffer to both.
    return TestOutput(test, cmd, out, out, returncode, elapsed, False)


if __name__ == "__main__":
    sys.exit(main())
