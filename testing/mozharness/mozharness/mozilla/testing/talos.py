#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
"""
run talos tests in a virtualenv
"""

import copy
import json
import multiprocessing
import os
import pprint
import re
import shutil
import subprocess
import sys

from mozsystemmonitor.resourcemonitor import SystemResourceMonitor

import mozharness
from mozharness.base.config import parse_config_file
from mozharness.base.errors import PythonErrorList
from mozharness.base.log import CRITICAL, DEBUG, ERROR, INFO, WARNING, OutputParser
from mozharness.base.python import Python3Virtualenv
from mozharness.base.vcs.vcsbase import MercurialScript
from mozharness.mozilla.automation import (
    TBPL_FAILURE,
    TBPL_RETRY,
    TBPL_SUCCESS,
    TBPL_WARNING,
    TBPL_WORST_LEVEL_TUPLE,
)
from mozharness.mozilla.testing.codecoverage import (
    CodeCoverageMixin,
    code_coverage_config_options,
)
from mozharness.mozilla.testing.errors import TinderBoxPrintRe
from mozharness.mozilla.testing.testbase import TestingMixin, testing_config_options
from mozharness.mozilla.tooltool import TooltoolMixin

scripts_path = os.path.abspath(os.path.dirname(os.path.dirname(mozharness.__file__)))
external_tools_path = os.path.join(scripts_path, "external_tools")

TalosErrorList = PythonErrorList + [
    {"regex": re.compile(r"""run-as: Package '.*' is unknown"""), "level": DEBUG},
    {"substr": r"""FAIL: Graph server unreachable""", "level": CRITICAL},
    {"substr": r"""FAIL: Busted:""", "level": CRITICAL},
    {"substr": r"""FAIL: failed to cleanup""", "level": ERROR},
    {"substr": r"""erfConfigurator.py: Unknown error""", "level": CRITICAL},
    {"substr": r"""talosError""", "level": CRITICAL},
    {
        "regex": re.compile(r"""No machine_name called '.*' can be found"""),
        "level": CRITICAL,
    },
    {
        "substr": r"""No such file or directory: 'browser_output.txt'""",
        "level": CRITICAL,
        "explanation": "Most likely the browser failed to launch, or the test was otherwise "
        "unsuccessful in even starting.",
    },
]

GeckoProfilerSettings = (
    "gecko_profile_interval",
    "gecko_profile_entries",
    "gecko_profile_features",
    "gecko_profile_threads",
)

# TODO: check for running processes on script invocation


class TalosOutputParser(OutputParser):
    minidump_regex = re.compile(
        r'''talosError: "error executing: '(\S+) (\S+) (\S+)'"'''
    )
    RE_PERF_DATA = re.compile(r".*PERFHERDER_DATA:\s+(\{.*\})")
    worst_tbpl_status = TBPL_SUCCESS

    def __init__(self, **kwargs):
        super(TalosOutputParser, self).__init__(**kwargs)
        self.minidump_output = None
        self.found_perf_data = []

    def update_worst_log_and_tbpl_levels(self, log_level, tbpl_level):
        self.worst_log_level = self.worst_level(log_level, self.worst_log_level)
        self.worst_tbpl_status = self.worst_level(
            tbpl_level, self.worst_tbpl_status, levels=TBPL_WORST_LEVEL_TUPLE
        )

    def parse_single_line(self, line):
        """In Talos land, every line that starts with RETURN: needs to be
        printed with a TinderboxPrint:"""
        if line.startswith("RETURN:"):
            line.replace("RETURN:", "TinderboxPrint:")
        m = self.minidump_regex.search(line)
        if m:
            self.minidump_output = (m.group(1), m.group(2), m.group(3))

        m = self.RE_PERF_DATA.match(line)
        if m:
            self.found_perf_data.append(m.group(1))

        # now let's check if we should retry
        harness_retry_re = TinderBoxPrintRe["harness_error"]["retry_regex"]
        if harness_retry_re.search(line):
            self.critical(" %s" % line)
            self.update_worst_log_and_tbpl_levels(CRITICAL, TBPL_RETRY)
            return  # skip base parse_single_line

        if line.startswith("SUITE-START "):
            SystemResourceMonitor.begin_marker("suite", "")
        elif line.startswith("SUITE-END "):
            SystemResourceMonitor.end_marker("suite", "")
        elif line.startswith("TEST-"):
            part = line.split(" | ")
            if part[0] == "TEST-START":
                SystemResourceMonitor.begin_marker("test", part[1])
            elif part[0] in ("TEST-OK", "TEST-UNEXPECTED-ERROR"):
                SystemResourceMonitor.end_marker("test", part[1])
        elif line.startswith("Running cycle ") or line.startswith("PROCESS-CRASH "):
            SystemResourceMonitor.record_event(line)

        super(TalosOutputParser, self).parse_single_line(line)


class Talos(
    TestingMixin, MercurialScript, TooltoolMixin, Python3Virtualenv, CodeCoverageMixin
):
    """
    install and run Talos tests
    """

    config_options = (
        [
            [
                ["--use-talos-json"],
                {
                    "action": "store_true",
                    "dest": "use_talos_json",
                    "default": False,
                    "help": "Use talos config from talos.json",
                },
            ],
            [
                ["--suite"],
                {
                    "action": "store",
                    "dest": "suite",
                    "help": "Talos suite to run (from talos json)",
                },
            ],
            [
                ["--system-bits"],
                {
                    "action": "store",
                    "dest": "system_bits",
                    "type": "choice",
                    "default": "32",
                    "choices": ["32", "64"],
                    "help": "Testing 32 or 64 (for talos json plugins)",
                },
            ],
            [
                ["--add-option"],
                {
                    "action": "extend",
                    "dest": "talos_extra_options",
                    "default": None,
                    "help": "extra options to talos",
                },
            ],
            [
                ["--gecko-profile"],
                {
                    "dest": "gecko_profile",
                    "action": "store_true",
                    "default": False,
                    "help": (
                        "Whether to profile the test run and save the profile results. "
                        "Copy paste the parameters used in this profiling run directly from about:profiling in Nightly."
                    ),
                },
            ],
            [
                ["--gecko-profile-interval"],
                {
                    "dest": "gecko_profile_interval",
                    "type": "float",
                    "help": "The interval between samples taken by the profiler (milliseconds)",
                },
            ],
            [
                ["--gecko-profile-entries"],
                {
                    "dest": "gecko_profile_entries",
                    "type": "int",
                    "help": "How many samples to take with the profiler",
                },
            ],
            [
                ["--gecko-profile-features"],
                {
                    "dest": "gecko_profile_features",
                    "type": "str",
                    "default": None,
                    "help": "The features to enable in the profiler (comma-separated)",
                },
            ],
            [
                ["--gecko-profile-threads"],
                {
                    "dest": "gecko_profile_threads",
                    "type": "str",
                    "help": "Comma-separated list of threads to sample.",
                },
            ],
            [
                ["--gecko-profile-extra-threads"],
                {
                    "dest": "gecko_profile_extra_threads",
                    "type": "str",
                    "help": "Comma-separated list of extra threads to add to the default list of threads to profile.",
                },
            ],
            [
                ["--disable-e10s"],
                {
                    "dest": "e10s",
                    "action": "store_false",
                    "default": True,
                    "help": "Run without multiple processes (e10s).",
                },
            ],
            [
                ["--disable-fission"],
                {
                    "action": "store_false",
                    "dest": "fission",
                    "default": True,
                    "help": "Disable Fission (site isolation) in Gecko.",
                },
            ],
            [
                ["--project"],
                {
                    "dest": "project",
                    "type": "str",
                    "help": "The project branch we're running tests on. Used for "
                    "disabling/skipping tests.",
                },
            ],
            [
                ["--setpref"],
                {
                    "action": "append",
                    "metavar": "PREF=VALUE",
                    "dest": "extra_prefs",
                    "default": [],
                    "help": "Set a browser preference. May be used multiple times.",
                },
            ],
            [
                ["--skip-preflight"],
                {
                    "action": "store_true",
                    "dest": "skip_preflight",
                    "default": False,
                    "help": "skip preflight commands to prepare machine.",
                },
            ],
            [
                ["--screenshot-on-failure"],
                {
                    "action": "store_true",
                    "dest": "screenshot_on_failure",
                    "default": False,
                    "help": "Take a screenshot when the test fails.",
                },
            ],
            [
                ["--pdfPaintChunk"],
                {
                    "type": "int",
                    "dest": "pdfpaint_chunk",
                    "default": None,
                    "help": (
                        "Chunk of the pdfpaint test to run (each chunk runs at most 100 pdfs). "
                        "Defaults to None to run all the pdfs at the same time."
                    ),
                },
            ],
        ]
        + testing_config_options
        + copy.deepcopy(code_coverage_config_options)
    )

    def __init__(self, **kwargs):
        kwargs.setdefault("config_options", self.config_options)
        kwargs.setdefault(
            "all_actions",
            [
                "clobber",
                "download-and-extract",
                "populate-webroot",
                "create-virtualenv",
                "install",
                "run-tests",
            ],
        )
        kwargs.setdefault(
            "default_actions",
            [
                "clobber",
                "download-and-extract",
                "populate-webroot",
                "create-virtualenv",
                "install",
                "run-tests",
            ],
        )
        kwargs.setdefault("config", {})
        super(Talos, self).__init__(**kwargs)

        self.workdir = self.query_abs_dirs()["abs_work_dir"]  # convenience

        self.run_local = self.config.get("run_local")
        self.installer_url = self.config.get("installer_url")
        self.test_packages_url = self.config.get("test_packages_url")
        self.talos_json_url = self.config.get("talos_json_url")
        self.talos_json = self.config.get("talos_json")
        self.talos_json_config = self.config.get("talos_json_config")
        self.repo_path = self.config.get("repo_path")
        self.obj_path = self.config.get("obj_path")
        self.tests = None
        extra_opts = self.config.get("talos_extra_options", [])
        self.gecko_profile = (
            self.config.get("gecko_profile") or "--gecko-profile" in extra_opts
        )
        for setting in GeckoProfilerSettings:
            value = self.config.get(setting)
            arg = "--" + setting.replace("_", "-")
            if value is None:
                try:
                    value = extra_opts[extra_opts.index(arg) + 1]
                except ValueError:
                    pass  # Not found
            if value is not None:
                setattr(self, setting, value)
                if not self.gecko_profile:
                    self.warning("enabling Gecko profiler for %s setting!" % setting)
                    self.gecko_profile = True
        self.pagesets_name = None
        self.benchmark_zip = None
        self.webextensions_zip = None

    # We accept some configuration options from the try commit message in the format
    # mozharness: <options>
    # Example try commit message:
    #   mozharness: --gecko-profile try: <stuff>
    def query_gecko_profile_options(self):
        gecko_results = []
        # finally, if gecko_profile is set, we add that to the talos options
        if self.gecko_profile:
            gecko_results.append("--gecko-profile")
            for setting in GeckoProfilerSettings:
                value = getattr(self, setting, None)
                if value:
                    arg = "--" + setting.replace("_", "-")
                    gecko_results.extend([arg, str(value)])
        return gecko_results

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super(Talos, self).query_abs_dirs()
        abs_dirs["abs_blob_upload_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "blobber_upload_dir"
        )
        abs_dirs["abs_test_install_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "tests"
        )
        self.abs_dirs = abs_dirs
        return self.abs_dirs

    def query_talos_json_config(self):
        """Return the talos json config."""
        if self.talos_json_config:
            return self.talos_json_config
        if not self.talos_json:
            self.talos_json = os.path.join(self.talos_path, "talos.json")
        self.talos_json_config = parse_config_file(self.talos_json)
        self.info(pprint.pformat(self.talos_json_config))
        return self.talos_json_config

    def make_talos_domain(self, host):
        return host + "-talos"

    def split_path(self, path):
        result = []
        while True:
            path, folder = os.path.split(path)
            if folder:
                result.append(folder)
                continue
            elif path:
                result.append(path)
            break

        result.reverse()
        return result

    def merge_paths(self, lhs, rhs):
        backtracks = 0
        for subdir in rhs:
            if subdir == "..":
                backtracks += 1
            else:
                break
        return lhs[:-backtracks] + rhs[backtracks:]

    def replace_relative_iframe_paths(self, directory, filename):
        """This will find iframes with relative paths and replace them with
        absolute paths containing domains derived from the original source's
        domain. This helps us better simulate real-world cases for fission
        """
        if not filename.endswith(".html"):
            return

        directory_pieces = self.split_path(directory)
        while directory_pieces and directory_pieces[0] != "fis":
            directory_pieces = directory_pieces[1:]
        path = os.path.join(directory, filename)

        # XXX: ugh, is there a better way to account for multiple encodings than just
        # trying each of them?
        encodings = ["utf-8", "latin-1"]
        iframe_pattern = re.compile(r'(iframe.*")(\.\./.*\.html)"')
        for encoding in encodings:
            try:
                with open(path, encoding=encoding) as f:
                    content = f.read()

                def replace_iframe_src(match):
                    src = match.group(2)
                    split = self.split_path(src)
                    merged = self.merge_paths(directory_pieces, split)
                    host = merged[3]
                    site_origin_hash = self.make_talos_domain(host)
                    new_url = 'http://%s/%s"' % (
                        site_origin_hash,
                        "/".join(merged),  # pylint --py3k: W1649
                    )
                    self.info(
                        "Replacing %s with %s in iframe inside %s"
                        % (match.group(2), new_url, path)
                    )
                    return match.group(1) + new_url

                content = re.sub(iframe_pattern, replace_iframe_src, content)
                with open(path, "w", encoding=encoding) as f:
                    f.write(content)
                break
            except UnicodeDecodeError:
                pass

    def query_pagesets_name(self):
        """Certain suites require external pagesets to be downloaded and
        extracted.
        """
        if self.pagesets_name:
            return self.pagesets_name
        if self.query_talos_json_config() and self.suite is not None:
            self.pagesets_name = self.talos_json_config["suites"][self.suite].get(
                "pagesets_name"
            )
            self.pagesets_name_manifest = "tp5n-pageset.manifest"
            return self.pagesets_name

    def query_benchmark_zip(self):
        """Certain suites require external benchmarks to be downloaded and
        extracted.
        """
        if self.benchmark_zip:
            return self.benchmark_zip
        if self.query_talos_json_config() and self.suite is not None:
            self.benchmark_zip = self.talos_json_config["suites"][self.suite].get(
                "benchmark_zip"
            )
            self.benchmark_zip_manifest = "jetstream-benchmark.manifest"
            return self.benchmark_zip

    def query_webextensions_zip(self):
        """Certain suites require external WebExtension sets to be downloaded and
        extracted.
        """
        if self.webextensions_zip:
            return self.webextensions_zip
        if self.query_talos_json_config() and self.suite is not None:
            self.webextensions_zip = self.talos_json_config["suites"][self.suite].get(
                "webextensions_zip"
            )
            self.webextensions_zip_manifest = "webextensions.manifest"
            return self.webextensions_zip

    def get_suite_from_test(self):
        """Retrieve the talos suite name from a given talos test name."""
        # running locally, single test name provided instead of suite; go through tests and
        # find suite name
        suite_name = None
        if self.query_talos_json_config():
            if "-a" in self.config["talos_extra_options"]:
                test_name_index = self.config["talos_extra_options"].index("-a") + 1
            if "--activeTests" in self.config["talos_extra_options"]:
                test_name_index = (
                    self.config["talos_extra_options"].index("--activeTests") + 1
                )
            if test_name_index < len(self.config["talos_extra_options"]):
                test_name = self.config["talos_extra_options"][test_name_index]
                for talos_suite in self.talos_json_config["suites"]:
                    if test_name in self.talos_json_config["suites"][talos_suite].get(
                        "tests"
                    ):
                        suite_name = talos_suite
            if not suite_name:
                # no suite found to contain the specified test, error out
                self.fatal("Test name is missing or invalid")
        else:
            self.fatal("Talos json config not found, cannot verify suite")
        return suite_name

    def query_suite_extra_prefs(self):
        if self.query_talos_json_config() and self.suite is not None:
            return self.talos_json_config["suites"][self.suite].get("extra_prefs", [])

        return []

    def validate_suite(self):
        """Ensure suite name is a valid talos suite."""
        if self.query_talos_json_config() and self.suite is not None:
            if self.suite not in self.talos_json_config.get("suites"):
                self.fatal(
                    "Suite '%s' is not valid (not found in talos json config)"
                    % self.suite
                )

    def talos_options(self, args=None, **kw):
        """return options to talos"""
        # binary path
        binary_path = self.binary_path or self.config.get("binary_path")
        if not binary_path:
            msg = """Talos requires a path to the binary.  You can specify binary_path or add
            download-and-extract to your action list."""
            self.fatal(msg)

        # talos options
        options = []
        # talos can't gather data if the process name ends with '.exe'
        if binary_path.endswith(".exe"):
            binary_path = binary_path[:-4]
        # options overwritten from **kw
        kw_options = {"executablePath": binary_path}
        if "suite" in self.config:
            kw_options["suite"] = self.config["suite"]
        if self.config.get("title"):
            kw_options["title"] = self.config["title"]
        if self.symbols_path:
            kw_options["symbolsPath"] = self.symbols_path
        if self.config.get("project", None):
            kw_options["project"] = self.config["project"]
        if self.config.get("pdfpaint_chunk", None):
            kw_options["pdfPaintChunk"] = str(self.config["pdfpaint_chunk"])

        kw_options.update(kw)
        # talos expects tests to be in the format (e.g.) 'ts:tp5:tsvg'
        tests = kw_options.get("activeTests")
        if tests and not isinstance(tests, str):
            tests = ":".join(tests)  # Talos expects this format
            kw_options["activeTests"] = tests
        for key, value in kw_options.items():
            options.extend(["--%s" % key, value])
        # configure profiling options
        options.extend(self.query_gecko_profile_options())
        # extra arguments
        if args is not None:
            options += args
        if "talos_extra_options" in self.config:
            options += self.config["talos_extra_options"]
        if self.config.get("code_coverage", False):
            options.extend(["--code-coverage"])
        if (
            self.config.get("screenshot_on_failure", False)
            or os.environ.get("MOZ_AUTOMATION", None) is not None
        ):
            options.extend(["--screenshot-on-failure"])

        # Add extra_prefs defined by individual test suites in talos.json
        extra_prefs = self.query_suite_extra_prefs()
        # Add extra_prefs from the configuration
        if self.config["extra_prefs"]:
            extra_prefs.extend(self.config["extra_prefs"])

        options.extend([f"--setpref={p}" for p in extra_prefs])

        # disabling fission can come from the --disable-fission cmd line argument; or in CI
        # it comes from a taskcluster transform which adds a --setpref for fission.autostart
        if (not self.config["fission"]) or "fission.autostart=false" in self.config[
            "extra_prefs"
        ]:
            options.extend(["--disable-fission"])

        return options

    def populate_webroot(self):
        """Populate the production test machines' webroots"""
        self.talos_path = os.path.join(
            self.query_abs_dirs()["abs_test_install_dir"], "talos"
        )

        # need to determine if talos pageset is required to be downloaded
        if self.config.get("run_local") and "talos_extra_options" in self.config:
            # talos initiated locally, get and verify test/suite from cmd line
            self.talos_path = os.path.dirname(self.talos_json)
            if (
                "-a" in self.config["talos_extra_options"]
                or "--activeTests" in self.config["talos_extra_options"]
            ):
                # test name (-a or --activeTests) specified, find out what suite it is a part of
                self.suite = self.get_suite_from_test()
            elif "--suite" in self.config["talos_extra_options"]:
                # --suite specified, get suite from cmd line and ensure is valid
                suite_name_index = (
                    self.config["talos_extra_options"].index("--suite") + 1
                )
                if suite_name_index < len(self.config["talos_extra_options"]):
                    self.suite = self.config["talos_extra_options"][suite_name_index]
                    self.validate_suite()
                else:
                    self.fatal("Suite name not provided")
        else:
            # talos initiated in production via mozharness
            self.suite = self.config["suite"]

        tooltool_artifacts = []
        src_talos_pageset_dest = os.path.join(self.talos_path, "talos", "tests")
        # unfortunately this path has to be short and can't be descriptive, because
        # on Windows we tend to already push the boundaries of the max path length
        # constraint. This will contain the tp5 pageset, but adjusted to have
        # absolute URLs on iframes for the purposes of better modeling things for
        # fission.
        src_talos_pageset_multidomain_dest = os.path.join(
            self.talos_path, "talos", "fis"
        )
        webextension_dest = os.path.join(self.talos_path, "talos", "webextensions")

        if self.query_pagesets_name():
            tooltool_artifacts.append(
                {
                    "name": self.pagesets_name,
                    "manifest": self.pagesets_name_manifest,
                    "dest": src_talos_pageset_dest,
                }
            )
            tooltool_artifacts.append(
                {
                    "name": self.pagesets_name,
                    "manifest": self.pagesets_name_manifest,
                    "dest": src_talos_pageset_multidomain_dest,
                    "postprocess": self.replace_relative_iframe_paths,
                }
            )

        if self.query_benchmark_zip():
            tooltool_artifacts.append(
                {
                    "name": self.benchmark_zip,
                    "manifest": self.benchmark_zip_manifest,
                    "dest": src_talos_pageset_dest,
                }
            )

        if self.query_webextensions_zip():
            tooltool_artifacts.append(
                {
                    "name": self.webextensions_zip,
                    "manifest": self.webextensions_zip_manifest,
                    "dest": webextension_dest,
                }
            )

        # now that have the suite name, check if artifact is required, if so download it
        # the --no-download option will override this
        for artifact in tooltool_artifacts:
            if "--no-download" not in self.config.get("talos_extra_options", []):
                self.info("Downloading %s with tooltool..." % artifact)

                archive = os.path.join(artifact["dest"], artifact["name"])
                output_dir_path = re.sub(r"\.zip$", "", archive)
                if not os.path.exists(archive):
                    manifest_file = os.path.join(self.talos_path, artifact["manifest"])
                    self.tooltool_fetch(
                        manifest_file,
                        output_dir=artifact["dest"],
                        cache=self.config.get("tooltool_cache"),
                    )
                    unzip = self.query_exe("unzip")
                    unzip_cmd = [unzip, "-q", "-o", archive, "-d", artifact["dest"]]
                    self.run_command(unzip_cmd, halt_on_failure=True)

                    if "postprocess" in artifact:
                        for subdir, dirs, files in os.walk(output_dir_path):
                            for file in files:
                                artifact["postprocess"](subdir, file)
                else:
                    self.info("%s already available" % artifact)

            else:
                self.info(
                    "Not downloading %s because the no-download option was specified"
                    % artifact
                )

        # if running webkit tests locally, need to copy webkit source into talos/tests
        if self.config.get("run_local") and (
            "stylebench" in self.suite or "motionmark" in self.suite
        ):
            self.get_webkit_source()

    def get_webkit_source(self):
        # in production the build system auto copies webkit source into place;
        # but when run locally we need to do this manually, so that talos can find it
        src = os.path.join(self.repo_path, "third_party", "webkit", "PerformanceTests")
        dest = os.path.join(
            self.talos_path, "talos", "tests", "webkit", "PerformanceTests"
        )

        if os.path.exists(dest):
            shutil.rmtree(dest)

        self.info("Copying webkit benchmarks from %s to %s" % (src, dest))
        try:
            shutil.copytree(src, dest)
        except Exception:
            self.critical("Error copying webkit benchmarks from %s to %s" % (src, dest))

    # Action methods. {{{1
    # clobber defined in BaseScript

    def download_and_extract(self, extract_dirs=None, suite_categories=None):
        # Use in-tree wptserve for Python 3.10 compatibility
        extract_dirs = [
            "tools/wptserve/*",
            "tools/wpt_third_party/h2/*",
            "tools/wpt_third_party/pywebsocket3/*",
        ]
        return super(Talos, self).download_and_extract(
            extract_dirs=extract_dirs, suite_categories=["common", "talos"]
        )

    def create_virtualenv(self, **kwargs):
        """VirtualenvMixin.create_virtualenv() assuemes we're using
        self.config['virtualenv_modules']. Since we are installing
        talos from its source, we have to wrap that method here."""
        # if virtualenv already exists, just add to path and don't re-install, need it
        # in path so can import jsonschema later when validating output for perfherder
        _virtualenv_path = self.config.get("virtualenv_path")

        _python_interp = self.query_exe("python")
        if "win" in self.platform_name() and os.path.exists(_python_interp):
            multiprocessing.set_executable(_python_interp)

        if self.run_local and os.path.exists(_virtualenv_path):
            self.info("Virtualenv already exists, skipping creation")

            if "win" in self.platform_name():
                _path = os.path.join(_virtualenv_path, "Lib", "site-packages")
            else:
                _path = os.path.join(
                    _virtualenv_path,
                    "lib",
                    os.path.basename(_python_interp),
                    "site-packages",
                )

            sys.path.append(_path)
            return

        # virtualenv doesn't already exist so create it
        # install mozbase first, so we use in-tree versions
        # Additionally, decide where to pull talos requirements from.
        if not self.run_local:
            mozbase_requirements = os.path.join(
                self.query_abs_dirs()["abs_test_install_dir"],
                "config",
                "mozbase_requirements.txt",
            )
            talos_requirements = os.path.join(self.talos_path, "requirements.txt")
        else:
            mozbase_requirements = os.path.join(
                os.path.dirname(self.talos_path),
                "config",
                "mozbase_source_requirements.txt",
            )
            talos_requirements = os.path.join(
                self.talos_path, "source_requirements.txt"
            )
        self.register_virtualenv_module(
            requirements=[mozbase_requirements],
            editable=True,
        )
        super(Talos, self).create_virtualenv()
        # talos in harness requires what else is
        # listed in talos requirements.txt file.
        self.install_module(requirements=[talos_requirements])

    def _validate_treeherder_data(self, parser):
        # late import is required, because install is done in create_virtualenv
        import jsonschema

        if len(parser.found_perf_data) != 1:
            self.critical(
                "PERFHERDER_DATA was seen %d times, expected 1."
                % len(parser.found_perf_data)
            )
            parser.update_worst_log_and_tbpl_levels(WARNING, TBPL_WARNING)
            return

        schema_path = os.path.join(
            external_tools_path, "performance-artifact-schema.json"
        )
        self.info("Validating PERFHERDER_DATA against %s" % schema_path)
        try:
            with open(schema_path) as f:
                schema = json.load(f)
            data = json.loads(parser.found_perf_data[0])
            jsonschema.validate(data, schema)
        except Exception:
            self.exception("Error while validating PERFHERDER_DATA")
            parser.update_worst_log_and_tbpl_levels(WARNING, TBPL_WARNING)

    def _artifact_perf_data(self, parser, dest):
        src = os.path.join(self.query_abs_dirs()["abs_work_dir"], "local.json")
        try:
            shutil.copyfile(src, dest)
        except Exception:
            self.critical("Error copying results %s to upload dir %s" % (src, dest))
            parser.update_worst_log_and_tbpl_levels(CRITICAL, TBPL_FAILURE)

    def run_tests(self, args=None, **kw):
        """run Talos tests"""

        # get talos options
        options = self.talos_options(args=args, **kw)

        # XXX temporary python version check
        python = self.query_python_path()
        self.run_command([python, "--version"])
        parser = TalosOutputParser(
            config=self.config, log_obj=self.log_obj, error_list=TalosErrorList
        )
        env = {}
        env["MOZ_UPLOAD_DIR"] = self.query_abs_dirs()["abs_blob_upload_dir"]
        if not self.run_local:
            env["MINIDUMP_STACKWALK"] = self.query_minidump_stackwalk()
            env["MOZ_FETCHES_DIR"] = os.environ.get("MOZ_FETCHES_DIR")
        else:
            env["MOZBUILD_PATH"] = self.config.get("mozbuild_path")
        env["MINIDUMP_SAVE_PATH"] = self.query_abs_dirs()["abs_blob_upload_dir"]
        env["RUST_BACKTRACE"] = "full"
        if not os.path.isdir(env["MOZ_UPLOAD_DIR"]):
            self.mkdir_p(env["MOZ_UPLOAD_DIR"])
        env = self.query_env(partial_env=env, log_level=INFO)
        # adjust PYTHONPATH to be able to use talos as a python package
        if "PYTHONPATH" in env:
            env["PYTHONPATH"] = self.talos_path + os.pathsep + env["PYTHONPATH"]
        else:
            env["PYTHONPATH"] = self.talos_path

        if self.repo_path is not None:
            env["MOZ_DEVELOPER_REPO_DIR"] = self.repo_path
        if self.obj_path is not None:
            env["MOZ_DEVELOPER_OBJ_DIR"] = self.obj_path

        # sets a timeout for how long talos should run without output
        output_timeout = self.config.get("talos_output_timeout", 3600)
        # run talos tests
        run_tests = os.path.join(self.talos_path, "talos", "run_tests.py")

        # Dynamically set the log level based on the talos config for consistency
        # throughout the test
        mozlog_opts = [f"--log-tbpl-level={self.config['log_level']}"]

        if not self.run_local and "suite" in self.config:
            fname_pattern = "%s_%%s.log" % self.config["suite"]
            mozlog_opts.append(
                "--log-errorsummary=%s"
                % os.path.join(env["MOZ_UPLOAD_DIR"], fname_pattern % "errorsummary")
            )

        def launch_in_debug_mode(cmdline):
            cmdline = set(cmdline)
            debug_opts = {"--debug", "--debugger", "--debugger_args"}

            return bool(debug_opts.intersection(cmdline))

        command = [python, run_tests] + options + mozlog_opts
        if launch_in_debug_mode(command):
            talos_process = subprocess.Popen(
                command, cwd=self.workdir, env=env, bufsize=0
            )
            talos_process.wait()
        else:
            self.return_code = self.run_command(
                command,
                cwd=self.workdir,
                output_timeout=output_timeout,
                output_parser=parser,
                env=env,
            )
        if parser.minidump_output:
            self.info("Looking at the minidump files for debugging purposes...")
            for item in parser.minidump_output:
                self.run_command(["ls", "-l", item])

        if self.return_code not in [0]:
            # update the worst log level and tbpl status
            log_level = ERROR
            tbpl_level = TBPL_FAILURE
            if self.return_code == 1:
                log_level = WARNING
                tbpl_level = TBPL_WARNING
            if self.return_code == 4:
                log_level = WARNING
                tbpl_level = TBPL_RETRY

            parser.update_worst_log_and_tbpl_levels(log_level, tbpl_level)
        elif "--no-upload-results" not in options:
            if not self.gecko_profile:
                self._validate_treeherder_data(parser)
                if not self.run_local:
                    # copy results to upload dir so they are included as an artifact
                    dest = os.path.join(env["MOZ_UPLOAD_DIR"], "perfherder-data.json")
                    self._artifact_perf_data(parser, dest)

        self.record_status(parser.worst_tbpl_status, level=parser.worst_log_level)
