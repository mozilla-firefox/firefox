#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
import copy
import gzip
import json
import os
import sys
from datetime import datetime, timedelta

# load modules from parent dir
sys.path.insert(1, os.path.dirname(sys.path[0]))

import mozinfo
from mozharness.base.errors import BaseErrorList
from mozharness.base.log import INFO
from mozharness.base.script import PreScriptAction
from mozharness.base.vcs.vcsbase import MercurialScript
from mozharness.mozilla.automation import TBPL_RETRY
from mozharness.mozilla.structuredlog import StructuredOutputParser
from mozharness.mozilla.testing.android import AndroidMixin
from mozharness.mozilla.testing.codecoverage import (
    CodeCoverageMixin,
    code_coverage_config_options,
)
from mozharness.mozilla.testing.errors import WptHarnessErrorList
from mozharness.mozilla.testing.testbase import TestingMixin, testing_config_options


def _is_harness_or_mozilla_entry(name):
    # Keep the manifests, the harness (tests/tools, resources, common) and
    # web-platform/mozilla/; skip the other upstream tests and their metadata.
    # Files under four path components deep (e.g. meta/MANIFEST.json) are kept.
    parts = name.split("/")
    if len(parts) < 4 or parts[0] != "web-platform":
        return True
    if parts[1] == "meta":
        return False
    return parts[1] != "tests" or parts[2] in ("tools", "resources", "common")


def _outside_test262(name):
    return not name.startswith("web-platform/tests/third_party/test262/")


class WebPlatformTest(TestingMixin, MercurialScript, CodeCoverageMixin, AndroidMixin):
    python_constraints = "requirements-marionette.txt"

    config_options = (
        [
            [
                ["--test-type"],
                {
                    "action": "extend",
                    "dest": "test_type",
                    "help": "Specify the test types to run.",
                },
            ],
            [
                ["--disable-e10s"],
                {
                    "action": "store_false",
                    "dest": "e10s",
                    "default": True,
                    "help": "Run without e10s enabled",
                },
            ],
            [
                ["--disable-fission"],
                {
                    "action": "store_true",
                    "dest": "disable_fission",
                    "default": False,
                    "help": "Run without fission enabled",
                },
            ],
            [
                ["--total-chunks"],
                {
                    "action": "store",
                    "dest": "total_chunks",
                    "help": "Number of total chunks",
                },
            ],
            [
                ["--this-chunk"],
                {
                    "action": "store",
                    "dest": "this_chunk",
                    "help": "Number of this chunk",
                },
            ],
            [
                ["--allow-software-gl-layers"],
                {
                    "action": "store_true",
                    "dest": "allow_software_gl_layers",
                    "default": False,
                    "help": "Permits a software GL implementation (such as LLVMPipe) "
                    "to use the GL compositor.",
                },
            ],
            [
                ["--headless"],
                {
                    "action": "store_true",
                    "dest": "headless",
                    "default": False,
                    "help": "Run tests in headless mode.",
                },
            ],
            [
                ["--headless-width"],
                {
                    "action": "store",
                    "dest": "headless_width",
                    "default": "1600",
                    "help": "Specify headless virtual screen width (default: 1600).",
                },
            ],
            [
                ["--headless-height"],
                {
                    "action": "store",
                    "dest": "headless_height",
                    "default": "1200",
                    "help": "Specify headless virtual screen height (default: 1200).",
                },
            ],
            [
                ["--skip-upstream-tests-extraction"],
                {
                    "action": "store_true",
                    "dest": "skip_upstream_tests_extraction",
                    "default": False,
                    "help": "Only extract the harness and the /_mozilla/ tests from the "
                    "web-platform tests archive, for jobs that run no upstream tests.",
                },
            ],
            [
                ["--setpref"],
                {
                    "action": "append",
                    "metavar": "PREF=VALUE",
                    "dest": "extra_prefs",
                    "default": [],
                    "help": "Defines an extra user preference.",
                },
            ],
            [
                ["--skip-implementation-status"],
                {
                    "action": "extend",
                    "dest": "skip_implementation_status",
                    "default": [],
                    "help": "Defines a way to not run a specific implementation status "
                    " (i.e. not implemented).",
                },
            ],
            [
                ["--backlog"],
                {
                    "action": "store_true",
                    "dest": "backlog",
                    "default": False,
                    "help": "Defines if test category is backlog.",
                },
            ],
            [
                ["--skip-timeout"],
                {
                    "action": "store_true",
                    "dest": "skip_timeout",
                    "default": False,
                    "help": "Ignore tests that are expected status of TIMEOUT",
                },
            ],
            [
                ["--skip-crash"],
                {
                    "action": "store_true",
                    "dest": "skip_crash",
                    "default": False,
                    "help": "Ignore tests that are expected status of CRASH",
                },
            ],
            [
                ["--default-exclude"],
                {
                    "action": "store_true",
                    "dest": "default_exclude",
                    "default": False,
                    "help": "Only run the tests explicitly given in arguments",
                },
            ],
            [
                ["--include"],
                {
                    "action": "append",
                    "dest": "include",
                    "default": [],
                    "help": "Add URL prefix to include.",
                },
            ],
            [
                ["--exclude"],
                {
                    "action": "append",
                    "dest": "exclude",
                    "default": [],
                    "help": "Add URL prefix to exclude.",
                },
            ],
            [
                ["--tag"],
                {
                    "action": "append",
                    "dest": "tag",
                    "default": [],
                    "help": "Add test tag (which includes URL prefix) to include.",
                },
            ],
            [
                ["--exclude-tag"],
                {
                    "action": "append",
                    "dest": "exclude_tag",
                    "default": [],
                    "help": "Add test tag (which includes URL prefix) to exclude.",
                },
            ],
            [
                ["--repeat"],
                {
                    "action": "store",
                    "dest": "repeat",
                    "default": 0,
                    "type": int,
                    "help": "Repeat tests (used for confirm-failures) X times.",
                },
            ],
            [
                ["--timeout-multiplier"],
                {
                    "action": "store",
                    "dest": "timeout_multiplier",
                    "type": float,
                    "help": "Sets the timeout multiplier (0.25 for `--backlog` tests by default)",
                },
            ],
            [
                ["--no-update-status-on-crash"],
                {
                    "action": "store_false",
                    "dest": "update_status_on_crash",
                    "default": True,
                    "help": "Sets whether to update the test status if a crash dump is detected",
                },
            ],
            [
                ["--enable-isolated-process"],
                {
                    "action": "store_true",
                    "dest": "isolated_process",
                    "default": False,
                    "help": "Run the tests with content service isolated process enabled.",
                },
            ],
        ]
        + copy.deepcopy(testing_config_options)
        + copy.deepcopy(code_coverage_config_options)
    )

    def __init__(self, require_config_file=True):
        super().__init__(
            config_options=self.config_options,
            all_actions=[
                "clobber",
                "download-and-extract",
                "download-and-process-manifest",
                "create-virtualenv",
                "pull",
                "start-emulator",
                "verify-device",
                "install",
                "run-tests",
            ],
            require_config_file=require_config_file,
            config={"require_test_zip": True, "test_summary": True},
        )

        # Surely this should be in the superclass
        c = self.config
        self.installer_url = c.get("installer_url")
        self.test_url = c.get("test_url")
        self.test_packages_url = c.get("test_packages_url")
        self.installer_path = c.get("installer_path")
        self.binary_path = c.get("binary_path")
        self.repeat = c.get("repeat")
        self.abs_app_dir = None
        self.xre_path = None
        if self.is_emulator:
            self.device_serial = "emulator-5554"

    def query_abs_app_dir(self):
        """We can't set this in advance, because OSX install directories
        change depending on branding and opt/debug.
        """
        if self.abs_app_dir:
            return self.abs_app_dir
        if not self.binary_path:
            self.fatal("Can't determine abs_app_dir (binary_path not set!)")
        self.abs_app_dir = os.path.dirname(self.binary_path)
        return self.abs_app_dir

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super().query_abs_dirs()

        dirs = {}
        dirs["abs_app_install_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "application"
        )
        dirs["abs_test_install_dir"] = os.path.join(abs_dirs["abs_work_dir"], "tests")
        dirs["abs_test_bin_dir"] = os.path.join(dirs["abs_test_install_dir"], "bin")
        dirs["abs_wpttest_dir"] = os.path.join(
            dirs["abs_test_install_dir"], "web-platform"
        )
        dirs["abs_blob_upload_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "blobber_upload_dir"
        )
        dirs["abs_test_extensions_dir"] = os.path.join(
            dirs["abs_test_install_dir"], "extensions"
        )
        work_dir = os.environ.get("MOZ_FETCHES_DIR") or abs_dirs["abs_work_dir"]
        if self.is_android:
            dirs["abs_xre_dir"] = os.path.join(work_dir, "hostutils")
        if self.is_emulator:
            dirs["abs_sdk_dir"] = os.path.join(work_dir, "android-sdk-linux")
            dirs["abs_avds_dir"] = os.path.join(work_dir, "android-device")
            dirs["abs_bundletool_path"] = os.path.join(work_dir, "bundletool.jar")
            # AndroidMixin uses this when launching the emulator. We only want
            # GLES3 if we're running WebRender (default)
            self.use_gles3 = True

        abs_dirs.update(dirs)
        self.abs_dirs = abs_dirs

        return self.abs_dirs

    @PreScriptAction("create-virtualenv")
    def _pre_create_virtualenv(self, action):
        dirs = self.query_abs_dirs()

        requirements = os.path.join(
            dirs["abs_test_install_dir"], "config", "marionette_requirements.txt"
        )

        self.register_virtualenv_module(requirements=[requirements])

        webtransport_requirements = os.path.join(
            dirs["abs_test_install_dir"],
            "config",
            "wpt_ci_requirements.txt",
        )

        self.register_virtualenv_module(requirements=[webtransport_requirements])

    def _query_geckodriver(self):
        path = None
        c = self.config
        dirs = self.query_abs_dirs()
        repl_dict = {}
        repl_dict.update(dirs)
        path = c.get("geckodriver", "geckodriver")
        if path:
            path = path % repl_dict
        return path

    def _query_cmd(self, test_types):
        if not self.binary_path:
            self.fatal("Binary path could not be determined")
            # And exit

        c = self.config
        run_file_name = "runtests.py"

        dirs = self.query_abs_dirs()
        abs_app_dir = self.query_abs_app_dir()
        str_format_values = {
            "binary_path": self.binary_path,
            "test_path": dirs["abs_wpttest_dir"],
            "test_install_path": dirs["abs_test_install_dir"],
            "abs_app_dir": abs_app_dir,
            "abs_work_dir": dirs["abs_work_dir"],
            "xre_path": self.xre_path,
        }

        cmd = [self.query_python_path("python"), "-u"]
        cmd.append(os.path.join(dirs["abs_wpttest_dir"], run_file_name))

        mozinfo.find_and_update_from_json(dirs["abs_test_install_dir"])

        raw_log_file, error_summary_file = self.get_indexed_logs(
            dirs["abs_blob_upload_dir"], "wpt"
        )

        cmd += [
            "--log-raw=-",
            "--log-wptreport=%s"
            % os.path.join(dirs["abs_blob_upload_dir"], "wptreport.json"),
            "--log-errorsummary=%s" % error_summary_file,
            "--symbols-path=%s" % self.symbols_path,
            "--stackwalk-binary=%s" % self.query_minidump_stackwalk(),
            "--stackfix-dir=%s" % os.path.join(dirs["abs_test_install_dir"], "bin"),
            "--no-pause-after-test",
            "--instrument-to-file=%s"
            % os.path.join(dirs["abs_blob_upload_dir"], "wpt_instruments.txt"),
            "--specialpowers-path=%s"
            % os.path.join(
                dirs["abs_test_extensions_dir"], "specialpowers@mozilla.org.xpi"
            ),
            # Ensure that we don't get a Python traceback from handlers that will be
            # added to the log summary
            "--suppress-handler-traceback",
        ]

        if self.repeat > 0:
            # repeat should repeat the original test, so +1 for first run
            cmd.append("--repeat=%s" % (self.repeat + 1))

        # Only the default testharness jobs: the tag-scoped ones run a
        # different test selection that hasn't been measured, and the VM pools
        # have much less memory per guest. The exception is the WebGPU CTS jobs
        # on the Intel workers, which run one test at a time and leave them
        # mostly idle.
        is_parallel_capable_mac = (
            sys.platform.startswith("darwin")
            and list(test_types) == ["testharness"]
            and (
                (
                    mozinfo.info.get("arch") == "aarch64"
                    and not mozinfo.info.get("macos_vm")
                    and not c["tag"]
                )
                or (
                    mozinfo.info.get("arch") == "x86_64"
                    and any(tag in ("webgpu", "webgpu-long") for tag in c["tag"])
                    and not c["backlog"]
                )
            )
        )
        if (
            self.is_android
            or mozinfo.info["tsan"]
            or "wdspec" in test_types
            or (not c["disable_fission"] and not is_parallel_capable_mac)
            # reftest on osx needs to be 1 process
            or ("reftest" in test_types and sys.platform.startswith("darwin"))
        ):
            processes = 1
        elif is_parallel_capable_mac:
            # Verified by experiment on the 10-core/16GB Apple M4 workers
            # (Mac16,10) in 2026-09 (bug 2050488) and for WebGPU on the macOS
            # 14.70 Intel workers in 2026-10 (bug 2078310).
            processes = 3
        else:
            processes = 2
        cmd.append("--processes=%s" % processes)

        if self.is_android:
            cmd += [
                "--device-serial=%s" % self.device_serial,
                "--package-name=%s" % self.query_package_name(),
                "--product=firefox_android",
            ]
        else:
            cmd += ["--binary=%s" % self.binary_path, "--product=firefox"]

        cmd += ["--no-install-fonts"]

        if test_types:
            cmd.append("--test-type")
            cmd.extend(test_types)

        if c["extra_prefs"]:
            cmd.extend([f"--setpref={p}" for p in c["extra_prefs"]])

        if c["disable_fission"]:
            cmd.append("--disable-fission")

        if not c["e10s"]:
            cmd.append("--disable-e10s")

        if c["skip_timeout"]:
            cmd.append("--skip-timeout")

        if c["skip_crash"]:
            cmd.append("--skip-crash")

        if c["default_exclude"]:
            cmd.append("--default-exclude")

        for implementation_status in c["skip_implementation_status"]:
            cmd.append("--skip-implementation-status=%s" % implementation_status)

        # Bug 1643177 - reduce timeout multiplier for web-platform-tests backlog
        if "timeout_multiplier" in c:
            cmd.append("--timeout-multiplier=%s" % c["timeout_multiplier"])
        elif c["backlog"]:
            cmd.append("--timeout-multiplier=0.25")

        if c["update_status_on_crash"]:
            cmd.append("--update-status-on-crash")
        else:
            cmd.append("--no-update-status-on-crash")

        if c["isolated_process"]:
            cmd.append("--enable-isolated-process")

        test_paths = set()
        if not (self.verify_enabled or self.per_test_coverage):
            # mozharness_test_paths is a set of test groups (directories) to run
            # if we have confirm_paths, this is a specific path we want to run and ignore the group
            mozharness_test_paths = json.loads(
                os.environ.get("MOZHARNESS_TEST_PATHS", '""')
            )
            confirm_paths = json.loads(os.environ.get("MOZHARNESS_CONFIRM_PATHS", '""'))

            if mozharness_test_paths:
                if confirm_paths:
                    mozharness_test_paths = confirm_paths

                path = os.path.join(dirs["abs_fetches_dir"], "wpt_tests_by_group.json")

                if not os.path.exists(path):
                    self.critical("Unable to locate web-platform-test groups file.")

                cmd.append(f"--test-groups={path}")

                for key in mozharness_test_paths.keys():
                    if "web-platform" not in key:
                        self.info(f"Ignoring test_paths for {key} harness")
                        continue
                    paths = mozharness_test_paths.get(key, [])
                    for p in paths:
                        if not p.startswith("/"):
                            # Assume this is a filesystem path rather than a test id
                            path = os.path.relpath(p, "testing/web-platform")
                            if ".." in path:
                                self.fatal(f"Invalid WPT path: {path}")
                            path = os.path.join(dirs["abs_wpttest_dir"], path)
                        else:
                            path = p

                        test_paths.add(path)
            else:
                # As per WPT harness, the --run-by-dir flag is incompatible with
                # the --test-groups flag.
                cmd.append("--run-by-dir=%i" % (3 if not mozinfo.info["asan"] else 0))
                for opt in ["total_chunks", "this_chunk"]:
                    val = c.get(opt)
                    if val:
                        cmd.append("--%s=%s" % (opt.replace("_", "-"), val))

        options = list(c.get("options", []))

        if "wdspec" in test_types or "aamtest" in test_types:
            geckodriver_path = self._query_geckodriver()
            if not geckodriver_path or not os.path.isfile(geckodriver_path):
                self.fatal(
                    "Unable to find geckodriver binary "
                    "in common test package: %s" % str(geckodriver_path)
                )
            cmd.append("--webdriver-binary=%s" % geckodriver_path)
            cmd.append("--webdriver-arg=-vv")  # enable trace logs
            cmd.append("--log-raw-unexpectedonly")

        cmd.extend(self.query_options(options, str_format_values=str_format_values))

        for url_prefix in c["include"]:
            cmd.append(f"--include={url_prefix}")
        for url_prefix in c["exclude"]:
            cmd.append(f"--exclude={url_prefix}")
        for tag in c["tag"]:
            cmd.append(f"--tag={tag}")
        for tag in c["exclude_tag"]:
            cmd.append(f"--exclude-tag={tag}")

        if mozinfo.info["os"] == "win":
            # Because of a limit on the length of CLI command line string length in Windows, we
            # should prefer to pass paths by a file instead.
            import tempfile

            with tempfile.NamedTemporaryFile(delete=False) as tmp:
                tmp.write("\n".join(test_paths).encode())
                cmd.append(f"--include-file={tmp.name}")
        else:
            cmd.extend(test_paths)

        return cmd

    def download_unpack(
        self, url, extract_to=".", extract_dirs="*", verbose=False, member_filter=None
    ):
        test_types = self.config.get("test_type")
        if "web-platform" in (url.split("/")[-1]):
            if self.config.get("skip_upstream_tests_extraction"):
                member_filter = _is_harness_or_mozilla_entry
            elif test_types and "test262" not in test_types:
                # test262 is about a quarter of the archive's files.
                member_filter = _outside_test262
        super().download_unpack(url, extract_to, extract_dirs, verbose, member_filter)

    def download_and_extract(self):
        super().download_and_extract(
            extract_dirs=[
                "mach",
                "bin/*",
                "config/*",
                "extensions/*",
                "mozbase/*",
                "marionette/*",
                "tools/*",
                "web-platform/*",
                "mozpack/*",
                "mozbuild/*",
            ],
            suite_categories=["web-platform"],
        )
        dirs = self.query_abs_dirs()
        if self.is_android:
            self.xre_path = dirs["abs_xre_dir"]
        # Make sure that the logging directory exists
        if self.mkdir_p(dirs["abs_blob_upload_dir"]) == -1:
            self.fatal("Could not create blobber upload directory")
            # Exit

    def download_and_process_manifest(self):
        """Downloads the tests-by-manifest JSON mapping generated by the decision task.

        web-platform-tests are chunked in the decision task as of Bug 1608837
        and this means tests are resolved by the TestResolver as part of this process.

        The manifest file contains tests keyed by the groups generated in
        TestResolver.get_wpt_group().

        Upon successful call, a JSON file containing only the web-platform test
        groups are saved in the fetch directory.

        Bug:
            1634554
        """
        dirs = self.query_abs_dirs()
        url = os.environ.get("TESTS_BY_MANIFEST_URL", "")
        if not url:
            self.fatal("TESTS_BY_MANIFEST_URL not defined.")

        artifact_name = url.split("/")[-1]

        # Save file to the MOZ_FETCHES dir.
        self.download_file(
            url, file_name=artifact_name, parent_dir=dirs["abs_fetches_dir"]
        )

        with gzip.open(os.path.join(dirs["abs_fetches_dir"], artifact_name), "r") as f:
            tests_by_manifest = json.loads(f.read())

        # We need to filter out non-web-platform-tests without knowing what the
        # groups are. Fortunately, all web-platform test 'manifests' begin with a
        # forward slash.
        test_groups = {
            key: tests_by_manifest[key]
            for key in tests_by_manifest.keys()
            if key.startswith("/")
        }

        outfile = os.path.join(dirs["abs_fetches_dir"], "wpt_tests_by_group.json")
        with open(outfile, "w+") as f:
            json.dump(test_groups, f, indent=2, sort_keys=True)

    def install(self):
        if self.is_android:
            self.install_android_app(self.installer_path)
        else:
            super().install()

    def _install_fonts(self):
        if self.is_android:
            return
        # Ensure the Ahem font is available
        dirs = self.query_abs_dirs()

        if not sys.platform.startswith("darwin"):
            font_path = os.path.join(os.path.dirname(self.binary_path), "fonts")
        else:
            font_path = os.path.join(
                os.path.dirname(self.binary_path),
                os.pardir,
                "Resources",
                "res",
                "fonts",
            )
        if not os.path.exists(font_path):
            os.makedirs(font_path)
        ahem_src = os.path.join(dirs["abs_wpttest_dir"], "tests", "fonts", "Ahem.ttf")
        ahem_dest = os.path.join(font_path, "Ahem.ttf")
        with open(ahem_src, "rb") as src, open(ahem_dest, "wb") as dest:
            dest.write(src.read())

    def query_gmp_path(self, plugin):
        """Locate a GMP plugin unpacked into the fetches directory.

        Gecko expects each entry on MOZ_GMP_PATH to be the version directory,
        so return <fetches>/<plugin>/<version>, or None on the platforms we
        have nothing to fetch for.
        """
        dirs = self.query_abs_dirs()
        base = os.path.join(dirs["abs_fetches_dir"], plugin)
        versions = sorted(os.listdir(base)) if os.path.isdir(base) else []
        if not versions:
            return None
        if len(versions) > 1:
            self.fatal(
                f"Expected a single {plugin} under {base}, found {', '.join(versions)}."
            )
        return os.path.join(base, versions[0])

    def run_tests(self):
        dirs = self.query_abs_dirs()

        parser = StructuredOutputParser(
            config=self.config,
            log_obj=self.log_obj,
            log_compact=True,
            error_list=BaseErrorList + WptHarnessErrorList,
            allow_crashes=True,
            test_summary_file=self.query_test_summary_file(
                dirs["abs_blob_upload_dir"], self.config
            ),
        )

        env = {"MINIDUMP_SAVE_PATH": dirs["abs_blob_upload_dir"]}
        env["RUST_BACKTRACE"] = "full"

        if self.config["allow_software_gl_layers"]:
            env["MOZ_LAYERS_ALLOW_SOFTWARE_GL"] = "1"
        if self.config["headless"]:
            env["MOZ_HEADLESS"] = "1"
            env["MOZ_HEADLESS_WIDTH"] = self.config["headless_width"]
            env["MOZ_HEADLESS_HEIGHT"] = self.config["headless_height"]

        if self.is_android:
            env["ADB_PATH"] = self.adb_path

        # The real plugins come from fetches where we have them; the fakes
        # stand in everywhere else.
        widevine_path = self.query_gmp_path("gmp-widevinecdm")
        if widevine_path:
            self.info(f"Using the Widevine CDM in {widevine_path}")
            gmp_paths = [widevine_path]
        else:
            gmp_paths = [
                os.path.join(dirs["abs_test_bin_dir"], "plugins", "gmp-fake", "1.0")
            ]
        openh264_path = self.query_gmp_path("gmp-gmpopenh264")
        if openh264_path:
            self.info(f"Using the OpenH264 GMP plugin in {openh264_path}")
            gmp_paths.append(openh264_path)
        else:
            gmp_paths.append(
                os.path.join(
                    dirs["abs_test_bin_dir"], "plugins", "gmp-fakeopenh264", "1.0"
                )
            )
        env["MOZ_GMP_PATH"] = os.pathsep.join(gmp_paths)

        env = self.query_env(partial_env=env, log_level=INFO)

        start_time = datetime.now()
        max_per_test_time = timedelta(minutes=60)
        max_per_test_tests = 10
        if self.per_test_coverage:
            max_per_test_tests = 30
        executed_tests = 0
        executed_too_many_tests = False

        if self.per_test_coverage or self.verify_enabled:
            suites = self.query_per_test_category_suites(None, None)
            if "wdspec" in suites:
                # geckodriver is required for wdspec, but not always available
                geckodriver_path = self._query_geckodriver()
                if not geckodriver_path or not os.path.isfile(geckodriver_path):
                    suites.remove("wdspec")
                    self.info("Skipping 'wdspec' tests - no geckodriver")
        else:
            test_types = self.config.get("test_type", [])
            suites = [None]
        for suite in suites:
            if executed_too_many_tests and not self.per_test_coverage:
                continue

            if suite:
                test_types = [suite]

            summary = {}
            for per_test_args in self.query_args(suite):
                # Make sure baseline code coverage tests are never
                # skipped and that having them run has no influence
                # on the max number of actual tests that are to be run.
                is_baseline_test = (
                    "baselinecoverage" in per_test_args[-1]
                    if self.per_test_coverage
                    else False
                )
                if executed_too_many_tests and not is_baseline_test:
                    continue

                if not is_baseline_test:
                    if (datetime.now() - start_time) > max_per_test_time:
                        # Running tests has run out of time. That is okay! Stop running
                        # them so that a task timeout is not triggered, and so that
                        # (partial) results are made available in a timely manner.
                        self.info(
                            "TinderboxPrint: Running tests took too long: Not all tests "
                            "were executed.<br/>"
                        )
                        return
                    if executed_tests >= max_per_test_tests:
                        # When changesets are merged between trees or many tests are
                        # otherwise updated at once, there probably is not enough time
                        # to run all tests, and attempting to do so may cause other
                        # problems, such as generating too much log output.
                        self.info(
                            "TinderboxPrint: Too many modified tests: Not all tests "
                            "were executed.<br/>"
                        )
                        executed_too_many_tests = True

                    executed_tests = executed_tests + 1

                cmd = self._query_cmd(test_types)
                cmd.extend(per_test_args)

                final_env = copy.copy(env)

                if self.per_test_coverage:
                    final_env.update(self.coverage_env(is_baseline_test))

                try:
                    return_code = self.run_command(
                        cmd,
                        cwd=dirs["abs_work_dir"],
                        output_timeout=1000,
                        output_parser=parser,
                        env=final_env,
                    )

                    if self.per_test_coverage:
                        self.add_per_test_coverage_report(
                            final_env, suite, per_test_args[-1]
                        )

                    tbpl_status, log_level, summary = parser.evaluate_parser(
                        return_code, previous_summary=summary
                    )
                finally:
                    parser.finish()

                self.record_status(tbpl_status, level=log_level)

                if len(per_test_args) > 0:
                    self.log_per_test_status(per_test_args[-1], tbpl_status, log_level)
                    if tbpl_status == TBPL_RETRY:
                        self.info("Per-test run abandoned due to RETRY status")
                        return


# main {{{1
if __name__ == "__main__":
    web_platform_tests = WebPlatformTest()
    web_platform_tests.run_and_exit()
