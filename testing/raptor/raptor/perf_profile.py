# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import signal
import subprocess
import tempfile
import zipfile
from pathlib import Path

from cmdline import PERF_PROFILE_APPS
from logger.logger import RaptorLogger
from raptor_profiling import RaptorProfiling

LOG = RaptorLogger(component="raptor-perf")

SAMPLING_FREQUENCY = 1000  # Hz
SAMPLY_TIMEOUT = 900
PERF_STOP_TIMEOUT = 300
PERF_TERM_TIMEOUT = 60


class PerfProfile(RaptorProfiling):
    """Collect system-wide perf traces and convert them to
    Firefox Profiler JSON profiles using Samply."""

    def __init__(
        self, upload_dir, raptor_config, test_config, source_path_mappings=None
    ):
        super().__init__(upload_dir, raptor_config, test_config, source_path_mappings)

        if self.raptor_config.get("app", "") not in PERF_PROFILE_APPS:
            raise RuntimeError(f"Perf profiling only supports: {PERF_PROFILE_APPS}")

        if self.raptor_config.get("platform") != "linux":
            raise RuntimeError("Perf profiling is only supported on Linux")

        self.test_name = test_config.get("name", "test")
        self.local = self.raptor_config.get("run_local")
        self.upload_dir = Path(self.upload_dir)
        self.temp_dir = Path(tempfile.mkdtemp())
        self.profile = (
            self.upload_dir / f"profile_perf_{self.test_name}_unprocessed.jslb.gz"
        )

        self.perf_data_path = self.temp_dir / f"perf-{self.test_name}.data"
        self.perf_process = None
        self.perf_stderr = None
        self.running = False

        if self.local:
            toolchain_dir = Path(
                os.environ.get("MOZBUILD_STATE_PATH", Path.home() / ".mozbuild")
            )
        else:
            toolchain_dir = Path(os.environ.get("MOZ_FETCHES_DIR", ""))

        self.samply_path = toolchain_dir / "samply" / "samply"

        if not self.samply_path.is_file():
            raise FileNotFoundError(
                f"samply not found at {self.samply_path}. {'Run ./mach bootstrap to install.' if self.local else ''}"
            )

        self.symbol_dir = self._get_build_symbols()

        if self.symbol_dir and self.symbol_dir.exists():
            os.environ["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
            if self.raptor_config.get("symbols_path"):
                os.environ["MOZ_CRASHREPORTER"] = "1"
            else:
                os.environ["MOZ_CRASHREPORTER_DISABLE"] = "1"
        else:
            LOG.info("Symbol directory not found. Skipping profile symbolication.")

        # Firefox Profiling settings may already be set upstream,
        # so only set them if they don't already exist.
        if self.raptor_config.get("app", "") == "firefox":
            for key, val in {
                "IONPERF": "func",
                "MOZ_USE_PERFORMANCE_MARKER_FILE": "1",
                "MOZ_DISABLE_CONTENT_SANDBOX": "1",
                "JIT_OPTION_enableICFramePointers": "true",
                "JIT_OPTION_onlyInlineSelfHosted": "true",
                "JIT_OPTION_emitInterpreterEntryTrampoline": "true",
                "PERF_SPEW_DIR": f"{self.temp_dir}",
                "MOZ_PERFORMANCE_MARKER_DIR": f"{self.temp_dir}",
            }.items():
                raptor_config.setdefault("environment", {}).setdefault(key, val)

        LOG.info("Initialization successful.")

        for key, value in self.__dict__.items():
            LOG.debug(f"{key}: {value}")
        for key, value in raptor_config.items():
            LOG.debug(f"{key}: {value}")
        for key, value in test_config.items():
            LOG.debug(f"{key}: {value}")

    def _get_build_symbols(self):

        if not self.local:
            symbol_extract_dir = self.temp_dir / "symbols"
            symbol_zip = (
                Path(os.environ["MOZ_FETCHES_DIR"]) / "target.crashreporter-symbols.zip"
            )
            if not symbol_zip.is_file():
                LOG.warning(f"Symbol zip not found at {symbol_zip}")
                return None
            try:
                with zipfile.ZipFile(symbol_zip, "r") as zipf:
                    zipf.extractall(symbol_extract_dir)
            except Exception as e:
                LOG.warning(f"Failed to extract {symbol_zip}: {e}")
                return None
            return symbol_extract_dir

        symbols_path = self.raptor_config.get("symbols_path")
        if symbols_path and Path(symbols_path).is_dir():
            return Path(symbols_path)
        elif "MOZ_DEVELOPER_OBJ_DIR" in os.environ:
            sym_dir = Path(
                os.environ["MOZ_DEVELOPER_OBJ_DIR"], "dist", "crashreporter-symbols"
            )
            if sym_dir.is_dir():
                return sym_dir
            LOG.warning(
                f"Symbol directory not found at {sym_dir}. {'Try running ./mach buildsymbols' if self.local else ''}"
            )
            return None
        else:
            LOG.warning(
                f"No symbol directory found. Set --symbolsPath or MOZ_DEVELOPER_OBJ_DIR. {'Try running ./mach buildsymbols' if self.local else ''}"
            )
            return None

    def _pkill_process(self, process_name):
        cmd = ["pkill", process_name]
        LOG.info(f"Running pkill command: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, check=False)
        LOG.info(f"pkill return code: {result.returncode}")
        if result.returncode == 1:
            LOG.info(f"No {process_name} processes to kill")
        if result.stdout:
            LOG.info(f"pkill stdout: {result.stdout}")
        if result.stderr:
            LOG.info(f"pkill stderr: {result.stderr}")

    def _log_perf_output(self):
        if self.perf_stderr is not None:
            self.perf_stderr.seek(0, os.SEEK_END)
            self.perf_stderr.seek(max(0, self.perf_stderr.tell() - 65536))
            output = self.perf_stderr.read().decode("utf-8", errors="replace")
            if output:
                LOG.info(f"Perf output: {output}")
            self.perf_stderr.close()
            self.perf_stderr = None

    def start(self):
        if self.local:
            self._pkill_process("perf")
            subprocess.run(["sudo", "-v"], check=False)
            # Build perf record command with system-wide profiling
            cmd = [
                "sudo",
                "-n",  # Run non-interactively without password prompt
                "perf",
                "record",
                "-a",  # Profile system-wide
                "-g",  # Record call graphs (stack traces)
                "-k",  # Use monotonic clock, same clock as marker file and jitdump timestamps
                "mono",
                "-F",  # Sampling frequency
                str(SAMPLING_FREQUENCY),  # Hz
                "-o",
                str(self.perf_data_path),
            ]
        else:
            # The CI wrapper runs the same perf record options as above
            cmd = ["sudo", "-n", "/usr/local/bin/record-system-perf"]

        self.perf_stderr = tempfile.TemporaryFile()
        try:
            # The task opens the destination; the privileged wrapper only returns bytes.
            with self.perf_data_path.open("wb") as output:
                LOG.info(f"Running perf command: {' '.join(cmd)}")
                self.perf_process = subprocess.Popen(
                    cmd,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL if self.local else output,
                    stderr=self.perf_stderr,
                )
        except Exception:
            self._log_perf_output()
            raise

        try:
            self.perf_process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            self.running = True
            LOG.info("Perf profiling started")
            return True

        self.perf_process.stdin.close()
        self._log_perf_output()
        LOG.error("Perf process failed to start properly")
        return False

    def _terminate_perf(self):
        # sudo relays SIGTERM to the wrapper, which finalizes and exits;
        # SIGKILL would only kill sudo and orphan the root-owned recorder.
        self.perf_process.terminate()
        try:
            self.perf_process.wait(timeout=PERF_TERM_TIMEOUT)
        except subprocess.TimeoutExpired:
            LOG.error("Perf ignored SIGTERM. Force killing")
            self.perf_process.kill()
            self.perf_process.wait(timeout=10)

    def stop(self):
        if not self.running or self.perf_process is None:
            LOG.warning("Perf is not running")
            return False

        try:
            if self.local:
                self.perf_process.send_signal(signal.SIGINT)
            # EOF asks the wrapper to finalize and deliver the recording.
            self.perf_process.stdin.close()
            self.perf_process.wait(timeout=PERF_STOP_TIMEOUT)
        except subprocess.TimeoutExpired:
            LOG.error("Perf did not finish within the stop deadline")
            self._terminate_perf()
            self.perf_data_path.unlink(missing_ok=True)
            return False
        finally:
            self.running = False
            self._log_perf_output()

        if self.perf_process.returncode != 0:
            LOG.error(f"Perf exited with status {self.perf_process.returncode}")
            self.perf_data_path.unlink(missing_ok=True)
            return False
        if not self.perf_data_path.is_file() or not self.perf_data_path.stat().st_size:
            LOG.error(f"Perf recording is missing or empty: {self.perf_data_path}")
            self.perf_data_path.unlink(missing_ok=True)
            return False

        LOG.info(
            f"perf.data successfully written: {self.perf_data_path} ({self.perf_data_path.stat().st_size} bytes)"
        )
        return True

    def symbolicate(self):

        if not self.perf_data_path.is_file():
            LOG.error(f"Symbolication failed. {self.perf_data_path} does not exist.")
            return

        # Convert perf.data to a symbolicated Firefox Profiler JSON profile using Samply
        # Output to temp directory first, move to upload directory if successful
        temp_profile = self.temp_dir / self.profile.name
        cmd = [
            str(self.samply_path),
            "import",
            str(self.perf_data_path),
            "--save-only",
            "-o",
            str(temp_profile),
            "--presymbolicate",
            "--breakpad-symbol-server",
            "https://symbols.mozilla.org/",
        ]

        if self.symbol_dir and self.symbol_dir.exists():
            LOG.info(f"Adding breakpad-symbol-dir: {self.symbol_dir}")
            cmd += [
                "--breakpad-symbol-dir",
                str(self.symbol_dir),
            ]

        LOG.info(f"Running Samply command: {' '.join(cmd)}")
        result = subprocess.run(
            cmd, capture_output=True, text=True, check=False, timeout=SAMPLY_TIMEOUT
        )
        for line in result.stdout.splitlines():
            LOG.info(f"Samply stdout: {line}")
        for line in result.stderr.splitlines():
            LOG.info(f"Samply stderr: {line}")
        LOG.info(f"Samply return code: {result.returncode}")
        if result.returncode != 0:
            LOG.error("Samply failed to symbolicate profile")
            return

        if not temp_profile.exists():
            LOG.error(f"Samply did not produce symbolicated profile at: {temp_profile}")
            return

        LOG.info(f"Moving profile from {temp_profile} to {self.profile}")
        temp_profile.replace(self.profile)
        LOG.info(
            f"Profile uploaded successfully: {self.profile} ({self.profile.stat().st_size} bytes)"
        )

    def clean(self):
        if self.temp_dir.exists():
            LOG.info(f"Removing temp directory: {self.temp_dir}")
            shutil.rmtree(self.temp_dir)

        super().clean()
