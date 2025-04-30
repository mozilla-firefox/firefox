# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Set up a browser environment before running a test.
"""
import json
import os
import shutil
import tempfile

import mozfile
import mozinfo
import mozrunner
from mozlog import get_proxy_logger
from mozprofile.profile import Profile

from talos import heavy, utils
from talos.gecko_profile import GeckoProfile
from talos.utils import TalosError, run_in_debug_mode

here = os.path.abspath(os.path.dirname(__file__))

LOG = get_proxy_logger()


class FFSetup:
    """
    Initialize the browser environment before running a test.

    This prepares:
     - the environment vars for running the test in the browser,
       available via the instance member *env*.
     - the profile used to run the test, available via the
       instance member *profile_dir*.
     - Gecko profiling, available via the instance member *gecko_profile*
       of type :class:`GeckoProfile` or None if not used.

    Note that the browser will be run once with the profile, to ensure
    this is basically working and negate any performance noise with the
    real test run (installing the profile the first time takes time).

    This class should be used as a context manager::

      with FFSetup(browser_config, test_config) as setup:
          # setup.env is initialized, and setup.profile_dir created
          pass
      # here the profile is removed
    """

    def __init__(self, browser_config, test_config):
        self.browser_config, self.test_config = browser_config, test_config
        self._tmp_dir = tempfile.mkdtemp()
        self.env = None
        # The profile dir must be named 'profile' because of xperf analysis
        # (in etlparser.py). TODO fix that ?
        self.profile_dir = os.path.join(self._tmp_dir, "profile")
        self.gecko_profile = None
        self.debug_mode = run_in_debug_mode(browser_config)

    @property
    def profile_data_dir(self):
        if "MOZ_DEVELOPER_REPO_DIR" in os.environ:
            return os.path.join(
                os.environ["MOZ_DEVELOPER_REPO_DIR"], "testing", "profiles"
            )
        return os.path.join(here, "profile_data")

    def _init_env(self):
        self.env = dict(os.environ)
        for k, v in self.browser_config["env"].items():
            self.env[k] = str(v)
        self.env["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
        if self.browser_config["symbols_path"]:
            self.env["MOZ_CRASHREPORTER"] = "1"
        else:
            self.env["MOZ_CRASHREPORTER_DISABLE"] = "1"

        self.env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] = "1"

        self.env["LD_LIBRARY_PATH"] = os.path.dirname(
            self.browser_config["browser_path"]
        )

    def _init_profile(self):
        extensions = self.browser_config["extensions"][:]
        if self.test_config.get("extensions"):
            extensions.extend(self.test_config["extensions"])

        # downloading a profile instead of using the empty one
        if self.test_config["profile"] is not None:
            path = heavy.download_profile(self.test_config["profile"])
            self.test_config["profile_path"] = path

        profile_path = os.path.normpath(self.test_config["profile_path"])
        LOG.info("Cloning profile located at %s" % profile_path)

        def _feedback(directory, content):
            # Called by shutil.copytree on each visited directory.
            # Used here to display info.
            #
            # Returns the items that should be ignored by
            # shutil.copytree when copying the tree, so always returns
            # an empty list.
            sub = directory.split(profile_path)[-1].lstrip("/")
            if sub:
                LOG.info("=> %s" % sub)
            return []

        profile = Profile.clone(
            profile_path, self.profile_dir, ignore=_feedback, restore=False
        )

        # build pref interpolation context
        webserver = self.browser_config["webserver"]
        if "://" not in webserver:
            webserver = "http://" + webserver

        interpolation = {
            "webserver": webserver,
        }

        # merge base profiles
        with open(os.path.join(self.profile_data_dir, "profiles.json")) as fh:
            base_profiles = json.load(fh)["talos"]

        for name in base_profiles:
            path = os.path.join(self.profile_data_dir, name)
            LOG.info(f"Merging profile: {path}")
            profile.merge(path, interpolation=interpolation)

        # set test preferences
        preferences = self.browser_config.get("preferences", {}).copy()
        if self.test_config.get("preferences"):
            test_prefs = dict(
                [
                    (i, utils.parse_pref(j))
                    for i, j in self.test_config["preferences"].items()
                ]
            )
            preferences.update(test_prefs)

        for name, value in preferences.items():
            if type(value) is str:
                value = utils.interpolate(value, **interpolation)
                preferences[name] = value
        profile.set_preferences(preferences)

        # installing addons
        LOG.info("Installing Add-ons:")
        LOG.info(extensions)
        profile.addons.install(extensions)

        # installing webextensions
        webextensions_to_install = []
        webextensions_folder = self.test_config.get("webextensions_folder", None)
        if isinstance(webextensions_folder, str):
            folder = utils.interpolate(webextensions_folder)
            for file in os.listdir(folder):
                if file.endswith(".xpi"):
                    webextensions_to_install.append(os.path.join(folder, file))

        webextensions = self.test_config.get("webextensions", None)
        if isinstance(webextensions, str):
            webextensions_to_install.append(webextensions)

        if webextensions_to_install is not None:
            LOG.info("Installing Webextensions:")
            for webext in webextensions_to_install:
                filename = utils.interpolate(webext)
                if mozinfo.os == "win":
                    filename = filename.replace("/", "\\")
                if not filename.endswith(".xpi"):
                    continue
                if not os.path.exists(filename):
                    continue
                LOG.info(filename)
                profile.addons.install(filename)

    def _run_profile(self):
        runner_cls = mozrunner.runners.get(
            mozinfo.info.get("appname", "firefox"), mozrunner.Runner
        )

        args = list(self.browser_config["extra_args"])
        args.append(self.browser_config["init_url"])

        runner = runner_cls(
            profile=self.profile_dir,
            binary=self.browser_config["browser_path"],
            cmdargs=args,
            env=self.env,
        )

        runner.start(outputTimeout=30)
        proc = runner.process_handler
        LOG.process_start(
            proc.pid, "%s %s" % (self.browser_config["browser_path"], " ".join(args))
        )

        try:
            exit_code = proc.wait()
        except Exception:
            proc.kill()
            raise TalosError("Browser Failed to close properly during warmup")

        LOG.process_exit(proc.pid, exit_code)

    def _init_gecko_profile(self):
        upload_dir = os.getenv("MOZ_UPLOAD_DIR")
        if self.test_config.get("gecko_profile") and not upload_dir:
            LOG.critical("Profiling ignored because MOZ_UPLOAD_DIR was not" " set")
        if upload_dir and self.test_config.get("gecko_profile"):
            self.gecko_profile = GeckoProfile(
                upload_dir, self.browser_config, self.test_config
            )
            self.gecko_profile.update_env(self.env)

    def clean(self):
        try:
            mozfile.remove(self._tmp_dir)
        except Exception as e:
            LOG.info("Exception while removing profile directory: %s" % self._tmp_dir)
            LOG.info(e)

        if self.gecko_profile:
            self.gecko_profile.clean()

    def collect_or_clean_ccov(self, clean=False):
        # NOTE: Currently only supported when running in production
        if not self.browser_config.get("develop", False):
            # first see if we an find any ccov files at the ccov output dirs
            if clean:
                LOG.info("Cleaning ccov files before starting the talos test")
            else:
                LOG.info(
                    "Collecting ccov files that were generated during the talos test"
                )
            gcov_prefix = os.getenv("GCOV_PREFIX", None)
            js_ccov_dir = os.getenv("JS_CODE_COVERAGE_OUTPUT_DIR", None)
            gcda_archive_folder_name = "gcda-archive"
            _gcda_files_found = []

            for _ccov_env in [gcov_prefix, js_ccov_dir]:
                if _ccov_env is not None:
                    # ccov output dir env vars exist; now search for gcda files to remove
                    _ccov_path = os.path.abspath(_ccov_env)
                    if os.path.exists(_ccov_path):
                        # now walk through and look for gcda files
                        LOG.info("Recursive search for gcda files in: %s" % _ccov_path)
                        for root, dirs, files in os.walk(_ccov_path):
                            for next_file in files:
                                if next_file.endswith(".gcda"):
                                    # don't want to move or delete files in our 'gcda-archive'
                                    if root.find(gcda_archive_folder_name) == -1:
                                        _gcda_files_found.append(
                                            os.path.join(root, next_file)
                                        )
                    else:
                        LOG.info(
                            "The ccov env var path doesn't exist: %s" % str(_ccov_path)
                        )

            # now  clean or collect gcda files accordingly
            if clean:
                # remove ccov data
                LOG.info(
                    "Found %d gcda files to clean. Deleting..."
                    % (len(_gcda_files_found))
                )
                for _gcda in _gcda_files_found:
                    try:
                        mozfile.remove(_gcda)
                    except Exception as e:
                        LOG.info("Exception while removing file: %s" % _gcda)
                        LOG.info(e)
                LOG.info("Finished cleaning ccov gcda files")
            else:
                # copy gcda files to archive folder to be collected later
                gcda_archive_top = os.path.join(
                    gcov_prefix, gcda_archive_folder_name, self.test_config["name"]
                )
                LOG.info(
                    "Found %d gcda files to collect. Moving to gcda archive %s"
                    % (len(_gcda_files_found), str(gcda_archive_top))
                )
                if not os.path.exists(gcda_archive_top):
                    try:
                        os.makedirs(gcda_archive_top)
                    except OSError:
                        LOG.critical(
                            "Unable to make gcda archive folder %s" % gcda_archive_top
                        )
                for _gcda in _gcda_files_found:
                    # want to copy the existing directory strucutre but put it under archive-dir
                    # need to remove preceeding '/' from _gcda file name so can join the path
                    gcda_archive_file = os.path.join(
                        gcov_prefix,
                        gcda_archive_folder_name,
                        self.test_config["name"],
                        _gcda.strip(gcov_prefix + "//"),
                    )
                    gcda_archive_dest = os.path.dirname(gcda_archive_file)

                    # create archive folder, mirroring structure
                    if not os.path.exists(gcda_archive_dest):
                        try:
                            os.makedirs(gcda_archive_dest)
                        except OSError:
                            LOG.critical(
                                "Unable to make archive folder %s" % gcda_archive_dest
                            )
                    # now copy the file there
                    try:
                        shutil.copy(_gcda, gcda_archive_dest)
                    except Exception as e:
                        LOG.info(
                            "Error copying %s to %s"
                            % (str(_gcda), str(gcda_archive_dest))
                        )
                        LOG.info(e)
                LOG.info(
                    "Finished collecting ccov gcda files. Copied to: %s"
                    % gcda_archive_top
                )

    def __enter__(self):
        LOG.info("Initialising browser for %s test..." % self.test_config["name"])
        self._init_env()
        self._init_profile()
        try:
            if not self.debug_mode and not self.test_config["name"].startswith("damp"):
                self._run_profile()
        except BaseException:
            self.clean()
            raise
        self._init_gecko_profile()
        LOG.info("Browser initialized.")
        LOG.info("Fission enabled: %s" % self.browser_config.get("fission", True))
        # remove ccov files before actual tests start
        if self.browser_config.get("code_coverage", False):
            # if the Firefox build was instrumented for ccov, initializing the browser
            # will have caused ccov to output some gcda files; in order to have valid
            # ccov data for the talos test we want to remove these files before starting
            # the actual talos test(s)
            self.collect_or_clean_ccov(clean=True)
        return self

    def __exit__(self, type, value, tb):
        self.clean()
