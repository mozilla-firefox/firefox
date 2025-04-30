# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import time
from pathlib import Path

from mozdevice import ADBDevice

from mozperftest.layers import Layer


class SimpleperfError(Exception):
    """Base class for Simpleperf-related exceptions."""

    pass


class SimpleperfAlreadyRunningError(SimpleperfError):
    """Raised when attempting to start simpleperf while it's already running."""

    pass


class SimpleperfNotRunningError(SimpleperfError):
    """Raised when attempting to stop simpleperf when it's not running."""

    pass


class SimpleperfExecutionError(SimpleperfError):
    """Raised when simpleperf fails to execute properly."""

    pass


class SimpleperfSystemError(SimpleperfError):
    """Raised when the system is not compatible with Android NDK installation."""

    pass


class SimpleperfBinaryNotFoundError(SimpleperfError):
    """Raised when the simpleperf binary cannot be found at the expected path."""

    pass


"""The default Simpleperf options will collect a 30s system-wide profile that uses DWARF based
   call graph so that we can collect Java stacks.  This requires root access.
"""
DEFAULT_SIMPLEPERF_OPTS = "-g --duration 30 -f 1000 --trace-offcpu -e cpu-clock -a"


class SimpleperfController:
    def __init__(self):
        self.device = ADBDevice()
        self.profiler_process = None

    def start(self, simpleperf_opts):
        """Starts the simpleperf profiler asynchronously if the layer is enabled.

        This method expects that the /data/local/tmp/simpleperf binary has
        already been installed during the setup phase of the layer.

        The simpleperf options can be provided as an argument.  If none are
        provided, we default to system-wide profiling which will require
        root access.
        """
        if simpleperf_opts is None:
            simpleperf_opts = DEFAULT_SIMPLEPERF_OPTS

        assert SimpleperfProfiler.is_enabled()
        if self.profiler_process:
            raise SimpleperfAlreadyRunningError("simpleperf already running")

        cmd = f"/data/local/tmp/simpleperf record {simpleperf_opts} -o /data/local/tmp/perf.data"

        self.profiler_process = subprocess.Popen(
            [
                "adb",
                "shell",
                "su",
                "-c",
                cmd,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Sleep for 1s to let simpleperf settle and begin profiling.
        time.sleep(1)

    def stop(self, output_path, index):
        assert SimpleperfProfiler.is_enabled()
        if not self.profiler_process:
            raise SimpleperfNotRunningError("no profiler process found")

        # Send SIGINT to simpleperf on the device to stop profiling.
        self.device.shell("kill $(pgrep simpleperf)")

        stdout_data, stderr_data = self.profiler_process.communicate()
        if self.profiler_process.returncode != 0:
            print("Error running simpleperf")
            print("output: ", stderr_data.decode())
            raise SimpleperfExecutionError("failed to run simpleperf")
        self.profiler_process = None

        output_path = str(Path(output_path, f"perf-{index}.data"))
        # Pull profiler data directly to the given output path.
        self.device.pull("/data/local/tmp/perf.data", output_path)
        self.device.shell("rm -f /data/local/tmp/perf.data")


class SimpleperfProfiler(Layer):
    name = "simpleperf"
    activated = False
    arguments = {
        "path": {
            "type": str,
            "default": None,
            "help": "Path to the Simpleperf NDK.",
        },
    }

    def __init__(self, env, mach_cmd):
        super(SimpleperfProfiler, self).__init__(env, mach_cmd)
        self.device = ADBDevice()

    @staticmethod
    def is_enabled():
        return os.environ.get("MOZPERFTEST_SIMPLEPERF", "0") == "1"

    @staticmethod
    def get_controller():
        return SimpleperfController()

    def setup_simpleperf_path(self):
        """Sets up and verifies that the simpleperf NDK exists.

        If no simpleperf path is provided, this step will try to install
        the Android NDK locally.
        """
        if self.get_arg("path", None) is None:
            import platform

            from mozboot import android

            os_name = None
            if platform.system() == "Windows":
                os_name = "windows"
            elif platform.system() == "Linux":
                os_name = "linux"
            elif platform.system() == "Darwin":
                os_name = "mac"
            else:
                raise SimpleperfSystemError(
                    "Unknown system in order to install Android NDK"
                )

            android.ensure_android_ndk(os_name)

            self.set_arg("path", str(Path(android.NDK_PATH, "simpleperf")))

        # Make sure the arm64 binary exists in the NDK path.
        binary_path = Path(
            self.get_arg("path"), "bin", "android", "arm64", "simpleperf"
        )
        if not os.path.exists(binary_path):
            raise SimpleperfBinaryNotFoundError(
                f"Cannot find simpleperf binary at {binary_path}"
            )

    def _cleanup(self):
        """Cleanup step, called during setup and teardown.

        Remove any leftover profiles and simpleperf binaries on the device,
        and also undefine the $MOZPERFTEST_SIMPLEPERF environment variable.
        """
        self.device.shell("rm -f /data/local/tmp/perf.data /data/local/tmp/simpleperf")
        os.environ.pop("MOZPERFTEST_SIMPLEPERF", None)

    def setup(self):
        """Setup the simpleperf layer

        First verify that the simpleperf NDK and ARM64 binary exists.
        Next, install the ARM64 simpleperf binary in /data/local/tmp on the device.
        Finally, define $MOZPERFTEST_SIMPLEPERF to indicate layer is active.
        """
        self.setup_simpleperf_path()
        self._cleanup()
        self.device.push(
            Path(self.get_arg("path"), "bin", "android", "arm64", "simpleperf"),
            "/data/local/tmp",
        )
        self.device.shell("chmod a+x /data/local/tmp/simpleperf")
        os.environ["MOZPERFTEST_SIMPLEPERF"] = "1"

    def teardown(self):
        self._cleanup()

    def run(self, metadata):
        """Run the simpleperf layer.

        The run step of the simpleperf layer is a no-op since the expectation is that
        the start/stop controls are manually called through the ProfilerMediator.
        """
        return metadata
