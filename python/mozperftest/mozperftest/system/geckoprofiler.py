# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import gzip
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mozperftest.layers import Layer
from mozperftest.utils import (
    archive_files,
    extract_tgz_and_find_files,
    get_adb_device_or_emu,
)


class GeckoProfilerError(Exception):
    """Base class for Gecko profiler-related exceptions."""

    pass


class GeckoProfilerAlreadyActiveError(GeckoProfilerError):
    """Raised when attempting to start profiling while it's already active."""

    pass


class GeckoProfilerNotActiveError(GeckoProfilerError):
    """Raised when attempting to stop profiling when it's not active."""

    pass


GECKOVIEW_CONFIG_PATH_PREFIX = "/data/local/tmp"

DEFAULT_GECKOPROFILER_OPTS = {
    "interval": 5,
    "features": "js,stackwalk,screenshots,ipcmessages,java,memory",
    "filters": "GeckoMain,Compositor,Renderer,IPDL Background,socket",
}


class GeckoProfilerController:
    """Controller that starts and stops profiling on device."""

    _package_id = None

    @classmethod
    def set_package_id(cls, package_id: str):
        cls._package_id = package_id

    def __init__(self):
        self.device = get_adb_device_or_emu()
        self.profiling_active = False
        self.package_id = None
        self.config_filename = None

    def _resolve_package_id(self):
        pkg = self._package_id or os.environ.get("BROWSER_BINARY")
        if not pkg:
            raise GeckoProfilerError("Package id not set for GeckoProfiler")
        return pkg

    def start(self, geckoprofiler_opts=None):
        """Create a temporary geckoview-config.yaml on device and enable startup profiling."""
        if geckoprofiler_opts is None:
            geckoprofiler_opts = DEFAULT_GECKOPROFILER_OPTS
        else:
            opts = DEFAULT_GECKOPROFILER_OPTS.copy()
            opts.update(geckoprofiler_opts)
            geckoprofiler_opts = opts

        assert GeckoProfiler.is_enabled()
        if self.profiling_active:
            raise GeckoProfilerAlreadyActiveError("Gecko profiler already active")

        self.package_id = self._resolve_package_id()
        self.config_filename = f"{self.package_id}-geckoview-config.yaml"

        config_content = f"""env:
  MOZ_PROFILER_STARTUP: 1
  MOZ_PROFILER_STARTUP_INTERVAL: {geckoprofiler_opts["interval"]}
  MOZ_PROFILER_STARTUP_FEATURES: {geckoprofiler_opts["features"]}
  MOZ_PROFILER_STARTUP_FILTERS: {geckoprofiler_opts["filters"]}
""".encode()

        with tempfile.NamedTemporaryFile(delete=False) as config_file:
            config_file.write(config_content)
            config_path = config_file.name

        device_config_path = f"{GECKOVIEW_CONFIG_PATH_PREFIX}/{self.config_filename}"

        subprocess.run(
            ["adb", "push", config_path, device_config_path],
            capture_output=True,
            text=True,
            check=True,
        )

        subprocess.run(
            [
                "adb",
                "shell",
                "am",
                "set-debug-app",
                "--persistent",
                self.package_id,
            ],
            capture_output=True,
            text=True,
            check=True,
        )

        self.profiling_active = True

    def stop(self, output_path, index):
        assert GeckoProfiler.is_enabled()
        if not self.profiling_active:
            raise GeckoProfilerNotActiveError("No active profiling session found")

        # Use content provider to stop profiling and stream raw profile data
        # This command blocks until the profile data is fully streamed
        output_filename = f"profile-{index}.json"
        local_output_path = Path(output_path) / output_filename

        with open(local_output_path, "wb") as output_file:
            result = subprocess.run(
                [
                    "adb",
                    "shell",
                    "content",
                    "read",
                    "--uri",
                    f"content://{self.package_id}.profiler/stop-and-upload",
                ],
                stdout=output_file,
                stderr=subprocess.PIPE,
                check=False,
            )

        # Always reset state, even if the command failed
        self.profiling_active = False

        if result.returncode != 0:
            print(
                f"Warning: Failed to stop profiler via content provider (exit code {result.returncode})"
            )
            if result.stderr:
                print(f"Error details:\n{result.stderr.decode()}")
            return None

        with open(local_output_path, "rb") as f:
            file_data = f.read()

        compressed_size = len(file_data)
        try:
            decompressed_data = gzip.decompress(file_data)
            print(
                f"Geckoprofile gzipped size ({compressed_size} bytes -> {len(decompressed_data)} bytes)"
            )
            with open(local_output_path, "wb") as output_file:
                output_file.write(decompressed_data)
        except gzip.BadGzipFile:
            print(f"Profile data is not compressed ({compressed_size} bytes)")

        file_size = local_output_path.stat().st_size
        print(f"Profile saved to: {local_output_path} ({file_size} bytes)")
        return local_output_path


class GeckoProfiler(Layer):
    name = "geckoprofiler"
    activated = False

    def __init__(self, env, mach_cmd):
        super().__init__(env, mach_cmd)
        self.device = get_adb_device_or_emu()
        self.output_dir = None
        self.test_name = None

    @staticmethod
    def is_enabled():
        return os.environ.get("MOZPERFTEST_GECKOPROFILE", "0") == "1"

    @staticmethod
    def get_controller():
        return GeckoProfilerController()

    def _archive_profiles(self):
        """Collect Gecko profiles and add to archive."""

        if not self.output_dir:
            self.info("No output directory set, skipping profile archiving")
            return

        patterns = ["profile-*.json"]
        self.info(f"geckoview output_dir {self.output_dir} and test {self.test_name}")

        profiles, search_dir, work_dir = extract_tgz_and_find_files(
            self.output_dir, self.test_name, patterns
        )

        search_dir = search_dir / self.test_name

        try:
            if profiles:
                # Profiles are streamed directly from device and ready to use
                archive_files(
                    profiles,
                    self.output_dir,
                    f"profile_{self.test_name}",
                    sort_key=lambda p: (p.parent.name, int(p.stem.split("-")[-1])),
                    base_dir=search_dir,
                )
                self.info("Archived gecko profiles")
        finally:
            if work_dir:
                shutil.rmtree(work_dir)

    def _cleanup(self):
        """Cleanup step, called during setup and teardown

        Remove the config files, clear debug app, and unset env flag.
        """
        self.device.shell(
            f"rm -f {GECKOVIEW_CONFIG_PATH_PREFIX}/*-geckoview-config.yaml"
        )
        self.device.shell("am clear-debug-app")
        os.environ.pop("MOZPERFTEST_GECKOPROFILE", None)
        GeckoProfilerController._package_id = None

    def setup(self):
        self._cleanup()
        os.environ["MOZPERFTEST_GECKOPROFILE"] = "1"

    def teardown(self):
        """Cleanup on teardown and add profiles to shared archive."""
        # Collect gecko profiles and add them to a zip file
        self._archive_profiles()
        self._cleanup()

    def run(self, metadata):
        """Run the geckoprofiler layer.

        The run step of the geckoprofiler layer is a no-op since the expectation is that
        the start/stop controls are manually called through the ProfilerMediator.
        """

        if not self.env.get_layer("android"):
            raise GeckoProfilerError(
                "GeckoProfiler is only supported on Android. Please enable the android layer."
            )
        metadata.add_extra_options(["gecko-profile"])
        pkg = getattr(metadata, "binary", None)
        if pkg:
            GeckoProfilerController.set_package_id(pkg)
        output = self.get_arg("output", self.output_dir)
        self.output_dir = Path(output) if output else None
        self.test_name = metadata.script["name"]
        return metadata
