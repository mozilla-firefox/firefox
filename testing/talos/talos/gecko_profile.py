# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
module to handle Gecko profiling.
"""

import os
import tempfile
import zipfile

import mozfile
from mozgeckoprofiler import symbolicate_profile_file
from mozlog import get_proxy_logger

LOG = get_proxy_logger()


class GeckoProfile:
    """
    Handle Gecko profiling.

    This allow to collect Gecko profiling data and to zip results in one file.
    """

    def __init__(self, upload_dir, test_config):
        self.upload_dir = upload_dir
        self.test_config = test_config

        # Create a temporary directory into which the tests can put
        # their profiles. These files will be assembled into one big
        # zip file later on, which is put into the MOZ_UPLOAD_DIR.
        gecko_profile_dir = tempfile.mkdtemp()

        gecko_profile_interval = test_config.get("gecko_profile_interval", 1)
        # Default number of entries is 128MiB.
        # This value is calculated by dividing the 128MiB of memory by 8 because
        # the profiler uses 8 bytes per entry.
        gecko_profile_entries = test_config.get(
            "gecko_profile_entries", int(128 * 1024 * 1024 / 8)
        )
        gecko_profile_features = test_config.get(
            "gecko_profile_features", "js,stackwalk,screenshots,memory"
        )
        gecko_profile_threads = test_config.get(
            "gecko_profile_threads", "GeckoMain,Compositor,Renderer"
        )

        gecko_profile_extra_threads = test_config.get(
            "gecko_profile_extra_threads", None
        )
        if gecko_profile_extra_threads:
            gecko_profile_threads += "," + gecko_profile_extra_threads

        # Make sure no archive already exists in the location where
        # we plan to output our profiler archive
        # If individual talos is ran (--activeTest) instead of suite (--suite)
        # the "suite" key will be empty and we'll name the profile after
        # the test name
        self.profile_arcname = os.path.join(
            self.upload_dir,
            "profile_{}.zip".format(test_config.get("suite", test_config["name"])),
        )

        # We delete the archive if the current test is the first in the suite
        if test_config.get("is_first_test", False):
            LOG.info(f"Clearing archive {self.profile_arcname}")
            mozfile.remove(self.profile_arcname)

        LOG.info(
            "Activating Gecko Profiling. Temp. profile dir:"
            f" {gecko_profile_dir}, interval: {gecko_profile_interval}, entries: {gecko_profile_entries}"
        )

        self.profiling_info = {
            "gecko_profile_interval": gecko_profile_interval,
            "gecko_profile_entries": gecko_profile_entries,
            "gecko_profile_dir": gecko_profile_dir,
            "gecko_profile_features": gecko_profile_features,
            "gecko_profile_threads": gecko_profile_threads,
        }

    def option(self, name):
        return self.profiling_info["gecko_profile_" + name]

    def update_env(self, env):
        """
        update the given env to update some env vars if required.
        """
        if not self.test_config.get("gecko_profile_startup"):
            return
        # Set environment variables which will cause profiling to
        # start as early as possible. These are consumed by Gecko
        # itself, not by Talos JS code.
        env.update({
            "MOZ_PROFILER_STARTUP": "1",
            "MOZ_PROFILER_STARTUP_INTERVAL": str(self.option("interval")),
            "MOZ_PROFILER_STARTUP_ENTRIES": str(self.option("entries")),
            "MOZ_PROFILER_STARTUP_FEATURES": str(self.option("features")),
            "MOZ_PROFILER_STARTUP_FILTERS": str(self.option("threads")),
        })

    def _save_gecko_profile(self, cycle, profile_path, out_path):
        """Symbolicate profile_path to out_path, returning the path to archive.

        The archive deflates its entries already, so the symbolicated profile
        is written uncompressed rather than gzipped.
        """
        try:
            if symbolicate_profile_file(profile_path, out_path):
                return out_path
        except Exception:
            LOG.critical(
                "Encountered an exception during profile"
                f" symbolication {profile_path} (cycle {cycle})",
                exc_info=True,
            )
        return profile_path

    def symbolicate(self, cycle):
        """
        Symbolicate Gecko profiling data for one cycle.

        :param cycle: the number of the cycle of the test currently run.
        """
        try:
            mode = zipfile.ZIP_DEFLATED
        except NameError:
            mode = zipfile.ZIP_STORED

        gecko_profile_dir = self.option("dir")

        with tempfile.TemporaryDirectory() as sym_dir, zipfile.ZipFile(
            self.profile_arcname, "a", mode
        ) as arc:
            # Collect all individual profiles that the test
            # has put into gecko_profile_dir.
            for index, profile_filename in enumerate(os.listdir(gecko_profile_dir)):
                testname = profile_filename
                if testname.endswith(".profile"):
                    testname = testname[0:-8]
                profile_path = os.path.join(gecko_profile_dir, profile_filename)
                archived_path = self._save_gecko_profile(
                    cycle, profile_path, os.path.join(sym_dir, f"{index}.json")
                )

                # Our zip will contain one directory per subtest,
                # and each subtest directory will contain one or
                # more cycle_i.profile files. For example, with
                # test_config['name'] == 'tscrollx',
                # profile_filename == 'iframe.svg.profile', i == 0,
                # we'll get path_in_zip ==
                # 'profile_tscrollx/iframe.svg/cycle_0.profile'.
                cycle_name = f"cycle_{cycle}.profile"
                path_in_zip = os.path.join(
                    "profile_{}".format(self.test_config["name"]), testname, cycle_name
                )
                LOG.info(
                    f"Adding profile {path_in_zip} to archive {self.profile_arcname}"
                )
                try:
                    arc.write(archived_path, path_in_zip)
                except Exception:
                    LOG.exception(
                        f"Failed to copy profile {profile_path} as {path_in_zip} to"
                        f" archive {self.profile_arcname}"
                    )
            # save the latest gecko profile archive to an env var, so later on
            # it can be viewed automatically via the view-gecko-profile tool
            os.environ["TALOS_LATEST_GECKO_PROFILE_ARCHIVE"] = self.profile_arcname

    def clean(self):
        """
        Clean up temp folders created with the instance creation.
        """
        mozfile.remove(self.option("dir"))
