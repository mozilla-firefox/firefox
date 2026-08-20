#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import functools
import json
import os
import pathlib
import re
import sys

import mozcrash
import mozinfo
import mozprocess
from mozfile import load_source
from mozlog import commandline
from mozrunner.utils import get_stack_fixer_function
from suites import get_gtest_suites, suite_filters

HERE = os.path.abspath(os.path.dirname(__file__))

log = None


class GTests:
    # Time (seconds) in which process will be killed if it produces no output.
    TEST_PROC_NO_OUTPUT_TIMEOUT = 300

    def gtest_timeout_value(self):
        # Time (seconds) to wait for test process to complete
        if mozinfo.info["tsan"]:
            return 3600
        else:
            return 2400

    def merge_perfherder_data(self, perfherder_data):
        grouped = {}

        for data in perfherder_data:
            framework_name = data.get("framework", {}).get("name")
            suites_by_name = grouped.setdefault(framework_name, {})
            for suite in data.get("suites", []):
                suite_name = suite.get("name")
                suite_data = suites_by_name.setdefault(
                    suite_name, {"name": suite_name, "subtests": []}
                )
                suite_data["subtests"].extend(suite.get("subtests", []))

        results = {}
        for framework_name, suites in grouped.items():
            results[framework_name] = {
                "framework": {"name": framework_name},
                "suites": list(suites.values()),
            }

        return results

    def run_gtest(
        self,
        prog,
        xre_path,
        cwd,
        symbols_path=None,
        utility_path=None,
        enable_inc_origin_init=False,
        filter_set=None,
        combine_suites=False,
    ):
        """
        Run a C++ unit test program.

        Arguments:
        * prog: The path to the test program to run.
        * env: The environment to use for running the program.
        * cwd: The directory to run tests from (support files will be found
               in this direcotry).
        * symbols_path: A path to a directory containing Breakpad-formatted
                        symbol files for producing stack traces on crash.
        * utility_path: A path to a directory containing utility programs.
                        currently used to locate a stack fixer to provide
                        symbols symbols for assertion stacks.
        * combine_suites: Run one program with all suites, rather than running
                          each suite as a separate program invocation.

        Return True if the program(s) exit with a zero status, False otherwise.
        """
        self.xre_path = xre_path
        env = self.build_environment(enable_inc_origin_init, filter_set)
        perfherder_data = []
        PERFHERDER_MATCHER = re.compile(r"PERFHERDER_DATA:\s*(\{.*\})\s*$")
        log.info("Running gtest")

        if cwd and not os.path.isdir(cwd):
            os.makedirs(cwd)

        stack_fixer = None
        if utility_path:
            stack_fixer = get_stack_fixer_function(utility_path, symbols_path)

        def output_line_handler(proc, line):
            if stack_fixer:
                print(stack_fixer(line))
            else:
                print(line)

            match = PERFHERDER_MATCHER.search(line)
            if match:
                data = json.loads(match.group(1))
                perfherder_data.append(data)

        args = [prog, "-unittest", "--gtest_death_test_style=threadsafe"]

        def run_test_process(test_name, env):
            def timeout_handler(timeout, context, proc):
                timeout_handler.timed_out = True
                log.error(f"{test_name} | timed out after {timeout} seconds{context}")
                mozcrash.kill_and_get_minidump(proc.pid, cwd, utility_path)

            timeout_handler.timed_out = False

            proc = mozprocess.run_and_wait(
                args,
                cwd=cwd,
                env=env,
                output_line_handler=output_line_handler,
                timeout=self.gtest_timeout_value(),
                timeout_handler=functools.partial(
                    timeout_handler, self.gtest_timeout_value(), ""
                ),
                output_timeout=GTests.TEST_PROC_NO_OUTPUT_TIMEOUT,
                output_timeout_handler=functools.partial(
                    timeout_handler,
                    GTests.TEST_PROC_NO_OUTPUT_TIMEOUT,
                    " without output",
                ),
            )
            returncode = proc.returncode

            log.info(f"{test_name} | process wait complete, returncode={returncode}")
            if mozcrash.check_for_crashes(cwd, symbols_path, test_name=test_name):
                # mozcrash will output the log failure line for us.
                return False
            if timeout_handler.timed_out:
                return False
            result = returncode == 0
            if not result:
                log.error(f"{test_name} | test failed with return code {returncode}")
            return result

        if combine_suites:
            result = run_test_process("gtest", env)
        else:
            suites = get_gtest_suites(args, cwd, env)
            # Some suites have side-effects like emitting PERFHERDER_DATA, so
            # don't short-circuit on the first failure.
            results = [
                run_test_process(f"gtest.{filt.suite}", filt(env))
                for filt in suite_filters(suites)
            ]
            result = all(results)

        if perfherder_data and "MOZ_AUTOMATION" in os.environ:
            upload_dir = pathlib.Path(os.getenv("MOZ_UPLOAD_DIR"))
            upload_dir.mkdir(parents=True, exist_ok=True)
            merged_perfherder_data = self.merge_perfherder_data(perfherder_data)
            for framework_name, data in merged_perfherder_data.items():
                file_name = (
                    "perfherder-data-gtest.json"
                    if len(merged_perfherder_data) == 1
                    else f"perfherder-data-gtest-{framework_name}.json"
                )
                out_path = upload_dir / file_name
                with out_path.open("w", encoding="utf-8") as f:
                    json.dump(data, f)

        return result

    def build_core_environment(self, env={}):
        """
        Add environment variables likely to be used across all platforms, including remote systems.
        """
        env["MOZ_XRE_DIR"] = self.xre_path
        env["MOZ_GMP_PATH"] = os.pathsep.join(
            os.path.join(self.xre_path, p, "1.0")
            for p in ("gmp-fake", "gmp-fakeopenh264")
        )
        env["XPCOM_DEBUG_BREAK"] = "stack-and-abort"
        env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
        env["MOZ_CRASHREPORTER"] = "1"
        env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "1"
        env["MOZ_RUN_GTEST"] = "1"

        if not mozinfo.has_sandbox:
            # Bug 1082193 - This is horrible. Our linux build boxes run CentOS 6,
            # which is too old to support sandboxing. Disable sandbox for gtests
            # on machines which don't support sandboxing until they can be
            # upgraded, or gtests are run on test machines instead.
            env["MOZ_DISABLE_GMP_SANDBOX"] = "1"

        return env

    def build_environment(self, enable_inc_origin_init, filter_set):
        """
        Create and return a dictionary of all the appropriate env variables
        and values. On a remote system, we overload this to set different
        values and are missing things like os.environ and PATH.
        """
        if not os.path.isdir(self.xre_path):
            raise Exception("xre_path does not exist: %s", self.xre_path)
        env = dict(os.environ)
        env = self.build_core_environment(env)
        # Some gtests spawn a Python helper via the PYTHON env var (e.g.
        # DesktopLauncherDownloaderTest launches `python -m http.server`).
        # Point it at the interpreter running the harness rather than
        # inheriting a bare `python3` that ShellExecuteExW on Windows would
        # resolve to the WindowsApps alias.
        env["PYTHON"] = sys.executable
        env["PERFHERDER_ALERTING_ENABLED"] = "1"
        pathvar = ""
        if mozinfo.os == "linux":
            pathvar = "LD_LIBRARY_PATH"
            # disable alerts for unstable tests (Bug 1369807)
            del env["PERFHERDER_ALERTING_ENABLED"]
        elif mozinfo.os == "mac":
            pathvar = "DYLD_LIBRARY_PATH"
        elif mozinfo.os == "win":
            pathvar = "PATH"
        if pathvar:
            if pathvar in env:
                env[pathvar] = "%s%s%s" % (self.xre_path, os.pathsep, env[pathvar])
            else:
                env[pathvar] = self.xre_path

        symbolizer_path = None
        if mozinfo.info["asan"]:
            symbolizer_path = "ASAN_SYMBOLIZER_PATH"
        elif mozinfo.info["tsan"]:
            symbolizer_path = "TSAN_SYMBOLIZER_PATH"

        if symbolizer_path is not None:
            # Use llvm-symbolizer for ASan/TSan if available/required
            if symbolizer_path in env and os.path.isfile(env[symbolizer_path]):
                llvmsym = env[symbolizer_path]
            else:
                llvmsym = os.path.join(
                    self.xre_path, "llvm-symbolizer" + mozinfo.info["bin_suffix"]
                )
            if os.path.isfile(llvmsym):
                env[symbolizer_path] = llvmsym
                log.info("Using LLVM symbolizer at %s" % llvmsym)
            else:
                log.info("Failed to find LLVM symbolizer at %s" % llvmsym)

        # webrender needs gfx.webrender.all=true, gtest doesn't use prefs
        env["MOZ_WEBRENDER"] = "1"
        env["MOZ_ACCELERATED"] = "1"

        if enable_inc_origin_init:
            env["MOZ_ENABLE_INC_ORIGIN_INIT"] = "1"
        else:
            env["MOZ_ENABLE_INC_ORIGIN_INIT"] = "0"

        if filter_set is not None:
            filter_sets_mod_path = os.path.join(HERE, "gtest_filter_sets.py")
            load_source("gtest_filter_sets", filter_sets_mod_path)

            import gtest_filter_sets

            gtest_filter_for_filter_set = gtest_filter_sets.get(filter_set)
            if gtest_filter_for_filter_set:
                env["GTEST_FILTER"] = gtest_filter_for_filter_set
                log.info("Using gtest filter for %s" % filter_set)
            else:
                log.info("Failed to get gtest filter for %s" % filter_set)

        return env


class gtestOptions(argparse.ArgumentParser):
    def __init__(self):
        super().__init__()
        commandline.add_logging_group(self)

        self.add_argument(
            "--cwd",
            dest="cwd",
            default=os.getcwd(),
            help="absolute path to directory from which to run the binary",
        )
        self.add_argument(
            "--xre-path",
            dest="xre_path",
            default=None,
            help="absolute path to directory containing XRE (probably xulrunner)",
        )
        self.add_argument(
            "--symbols-path",
            dest="symbols_path",
            default=None,
            help="absolute path to directory containing breakpad "
            "symbols, or the URL of a zip file containing "
            "symbols",
        )
        self.add_argument(
            "--utility-path",
            dest="utility_path",
            default=None,
            help="path to a directory containing utility program binaries",
        )
        self.add_argument(
            "--enable-inc-origin-init",
            action="store_true",
            dest="enable_inc_origin_init",
            default=False,
            help="enabling of incremental origin initialization in Gecko",
        )
        self.add_argument(
            "--filter-set",
            dest="filter_set",
            default=None,
            help="predefined gtest filter",
        )
        self.add_argument(
            "--combine-suites",
            action="store_true",
            dest="combine_suites",
            default=False,
            help="test all suites in one program invocation",
        )
        self.add_argument("args", nargs=argparse.REMAINDER)


def update_mozinfo():
    """walk up directories to find mozinfo.json update the info"""
    path = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
    dirs = set()
    while path != os.path.expanduser("~"):
        if path in dirs:
            break
        dirs.add(path)
        path = os.path.split(path)[0]
    mozinfo.find_and_update_from_json(*dirs)


def main():
    global log
    parser = gtestOptions()
    options = parser.parse_args()
    log = commandline.setup_logging("gtest", options, {"raw": sys.stdout})
    args = options.args
    if not args:
        print("Usage: %s <binary>" % sys.argv[0])
        sys.exit(1)
    if not options.xre_path:
        print("Error: --xre-path is required")
        sys.exit(1)
    if not options.utility_path:
        print("Warning: --utility-path is required to process assertion stacks")

    update_mozinfo()
    prog = os.path.abspath(args[0])
    options.xre_path = os.path.abspath(options.xre_path)
    tester = GTests()
    try:
        result = tester.run_gtest(
            prog,
            options.xre_path,
            options.cwd,
            symbols_path=options.symbols_path,
            utility_path=options.utility_path,
            enable_inc_origin_init=options.enable_inc_origin_init,
            filter_set=options.filter_set,
            combine_suites=options.combine_suites,
        )
    except Exception as e:
        log.error(str(e))
        result = False
    exit_code = 0 if result else 1
    log.info("rungtests.py exits with code %s" % exit_code)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
