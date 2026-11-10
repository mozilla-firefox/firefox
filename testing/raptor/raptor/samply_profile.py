# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import tempfile
import time
import zipfile
from pathlib import Path

from cmdline import SAMPLY_PROFILE_APPS
from logger.logger import RaptorLogger
from raptor_profiling import RaptorProfiling

LOG = RaptorLogger(component="raptor-samply")


class SamplyProfile(RaptorProfiling):
    """Record profiles using Samply on macOS and convert them to
    Firefox Profiler JSON profiles."""

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

    def __init__(
        self, upload_dir, raptor_config, test_config, source_path_mappings=None
    ):
        super().__init__(upload_dir, raptor_config, test_config, source_path_mappings)

        if self.raptor_config.get("app", "") not in SAMPLY_PROFILE_APPS:
            raise RuntimeError(f"Samply profiling only supports: {SAMPLY_PROFILE_APPS}")

        if self.raptor_config.get("platform") != "mac":
            raise RuntimeError("Samply profiling is only supported on macOS")

        self.test_name = test_config.get("name", "test")
        self.upload_dir = Path(self.upload_dir)
        self.local = self.raptor_config.get("run_local")
        self.profile = (
            self.upload_dir / f"profile_samply_{self.test_name}_unprocessed.jslb.gz"
        )
        self.temp_dir = Path(tempfile.mkdtemp())
        self.running = False

        self.original_binary_path = Path(self.raptor_config.get("binary"))
        self.wrapper_binary_path = None
        self.macosx_sdk = None

        if self.local:
            toolchain_dir = Path(
                os.environ.get("MOZBUILD_STATE_PATH", Path.home() / ".mozbuild")
            )
        else:
            toolchain_dir = Path(os.environ.get("MOZ_FETCHES_DIR", ""))

        self.samply_path = toolchain_dir / "samply" / "samply"
        self.clang_path = toolchain_dir / "clang" / "bin" / "clang++"

        if self.clang_path.exists():
            # Opt for the latest MacOSX SDK available if we are using
            # clang++ from mozbuild
            sdk_dirs = sorted(toolchain_dir.glob("MacOSX*.sdk"), reverse=True)
            if sdk_dirs:
                self.macosx_sdk = Path(sdk_dirs[0])
                LOG.info(f"Found MacOSX SDK: {self.macosx_sdk}")
            else:
                LOG.warning(
                    f"Failed to find MacOSX SDK in {toolchain_dir}. clang++ compilation may fail."
                )
        # Attempt to use system clang++ as a fallback locally.
        # It should pre-configured with a system SDK (-isysroot is not needed
        # when compiling).
        else:
            clang_which = shutil.which("clang++")
            if clang_which:
                self.clang_path = Path(clang_which)

        if not self.samply_path.is_file():
            raise FileNotFoundError(
                f"samply not found at {self.samply_path}. {'Run ./mach bootstrap to install.' if self.local else ''}"
            )

        if not self.clang_path.is_file():
            raise FileNotFoundError(
                f"clang++ not found at {self.clang_path}. {'Run ./mach bootstrap to install.' if self.local else ''}"
            )

        self.symbol_dir = self._get_build_symbols()

        if self.symbol_dir and self.symbol_dir.exists():
            # Turn on crash reporter if we have symbols
            os.environ["MOZ_CRASHREPORTER_NO_REPORT"] = "1"
            if self.raptor_config.get("symbols_path"):
                os.environ["MOZ_CRASHREPORTER"] = "1"
            else:
                os.environ["MOZ_CRASHREPORTER_DISABLE"] = "1"
        else:
            LOG.info("Symbol directory not found. Skipping profile symbolication.")

        # Firefox Profiling settings may already be set upstream,
        # so only set them if they don't already exist.
        # Upstream these can be set with --setenv in a transform and
        # Downstream they are passed to browsertime as --firefox.env.
        # Setting them here (instead of a transform) allows these
        # settings to be used in both CI runs and local runs
        if self.raptor_config.get("app", "") == "firefox":
            for key, val in {
                "IONPERF": "func",
                "MOZ_USE_PERFORMANCE_MARKER_FILE": "1",
                # Disabling the content sandbox allows JIT dumps
                # and marker files to be emitted
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
            LOG.debug(f"attribute: {key}={value}")
        for key, value in raptor_config.items():
            LOG.debug(f"raptor config: {key}={value}")
        for key, value in test_config.items():
            LOG.debug(f"test config: {key}={value}")

    def _pkill_process(self, process_name):
        # Based on testing, system pkill seems to be a more reliable
        # way to kill samply processes than .terminate()
        # / .send_signal(signal.SIGINT)
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

    def start(self):
        LOG.info("Killing any existing samply processes")
        self._pkill_process("samply")

        if self.profile.is_file():
            self.profile.unlink()

        LOG.info("Compiling helper binary")
        env_vars_file = self.temp_dir / "env_vars.txt"
        helper_code = self._generate_helper_code(env_vars_file)
        helper_binary = self._compile_binary(
            code=helper_code, binary_name="samply_helper"
        )
        if not helper_binary:
            LOG.error("Failed to compile helper binary")
            return False
        LOG.info(f"helper binary: {helper_binary}")

        symbol_args = []
        if self.symbol_dir and self.symbol_dir.exists():
            symbol_args = [
                "--presymbolicate",
                "--breakpad-symbol-dir",
                str(self.symbol_dir),
                "--breakpad-symbol-server",
                "https://symbols.mozilla.org/",
            ]

        cmd = (
            [
                str(self.samply_path),
                "record",
                "--save-only",
            ]
            + symbol_args
            + [
                "-o",
                str(self.profile),
                str(helper_binary),
            ]
        )

        LOG.info("Profiling helper binary with samply")
        LOG.info(f"Running samply command: {' '.join(cmd)}")
        try:
            samply_process = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
        except Exception as e:
            LOG.error(f"Failed to start samply: {e}")
            return False

        # Check if Samply failed during startup
        try:
            samply_process.wait(timeout=1)
            stdout, stderr = samply_process.communicate()
            LOG.error("Samply process failed to start properly")
            if stdout:
                LOG.error(f"Samply stdout: {stdout}")
            if stderr:
                LOG.error(f"Samply stderr: {stderr}")
            return False
        except subprocess.TimeoutExpired:
            pass

        LOG.info(
            "Extracting DYLD_INSERT_LIBRARIES and SAMPLY_BOOTSTRAP_SERVER_NAME values"
        )
        dyld_value = None
        bootstrap_value = None
        ENV_VAR_WAIT_TIME = 5
        start_wait = time.time()
        while (time.time() - start_wait) < ENV_VAR_WAIT_TIME:
            if env_vars_file.exists():
                with open(env_vars_file) as f:
                    for line in f:
                        clean_line = line.rstrip()
                        if clean_line.startswith("DYLD_INSERT_LIBRARIES="):
                            dyld_value = clean_line.split("=", 1)[1]
                            LOG.debug(f"dyld_value: {dyld_value}")
                        elif clean_line.startswith("SAMPLY_BOOTSTRAP_SERVER_NAME="):
                            bootstrap_value = clean_line.split("=", 1)[1]
                            LOG.debug(f"bootstrap_value: {bootstrap_value}")
                if dyld_value and bootstrap_value:
                    break
            time.sleep(1)

        if not dyld_value or not bootstrap_value:
            LOG.error("Failed to extract env vars")
            LOG.info("Killing samply_helper process")
            self._pkill_process("samply_helper")
            self._pkill_process("samply")
            return False

        LOG.info("Compiling wrapper binary")
        wrapper_code = self._generate_wrapper_code(dyld_value, bootstrap_value)
        wrapper_binary = self._compile_binary(
            code=wrapper_code, binary_name="samply_wrapper"
        )
        if not wrapper_binary:
            LOG.error("Failed to compile wrapper")
            return False
        self.wrapper_binary_path = wrapper_binary
        LOG.info(f"wrapper_binary: {self.wrapper_binary_path}")
        self.raptor_config["binary"] = str(wrapper_binary)

        self.running = True
        LOG.info("Samply profiling started")
        return True

    def _generate_helper_code(self, env_vars_file):
        return f"""
        #include <cstdlib>
        #include <fstream>
        #include <unistd.h>

        int main() {{
            const char* dyld = getenv("DYLD_INSERT_LIBRARIES");
            const char* bootstrap = getenv("SAMPLY_BOOTSTRAP_SERVER_NAME");

            std::ofstream f("{env_vars_file}");

            if (f) {{
                if (dyld) {{
                    f << "DYLD_INSERT_LIBRARIES=" << dyld << "\\n";
                }}
                if (bootstrap) {{
                    f << "SAMPLY_BOOTSTRAP_SERVER_NAME=" << bootstrap << "\\n";
                }}
                f.flush(); // Write to OS filesystem immediately
            }}

            pause();

            return 0;
        }}
        """

    def _generate_wrapper_code(self, dyld_value, bootstrap_value):
        return f"""
        #include <unistd.h>
        #include <stdlib.h>
        #include <cstdio>

        extern char** environ;

        int main(int argc, char* argv[]) {{
            const char* firefox_binary = "{self.original_binary_path}";
            const char* dyld = "{dyld_value}";
            const char* bootstrap = "{bootstrap_value}";

            setenv("DYLD_INSERT_LIBRARIES", dyld, 1);
            setenv("SAMPLY_BOOTSTRAP_SERVER_NAME", bootstrap, 1);

            argv[0] = (char*)firefox_binary;
            execve(firefox_binary, argv, environ);
            return 1;
        }}
        """

    def _compile_binary(self, code=None, binary_name=None):
        if code is None:
            LOG.error("Code for compilation is not provided")
            return None

        if binary_name is None:
            LOG.error("Binary name is not provided")
            return None

        code_file = self.temp_dir / f"{binary_name}.cpp"
        code_file.write_text(code)
        binary = self.temp_dir / f"{binary_name}"

        cmd = [
            str(self.clang_path),
            str(code_file),
            "-o",
            str(binary),
        ]
        if self.macosx_sdk:
            cmd.extend(["-isysroot", str(self.macosx_sdk)])

        LOG.info(f"Running clang++ command: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.stdout:
            LOG.info(f"clang++ stdout: {result.stdout}")
        if result.stderr:
            LOG.info(f"clang++ stderr: {result.stderr}")
        LOG.info(f"clang++ return code: {result.returncode}")
        if result.returncode != 0:
            LOG.error(f"Binary compilation failed: {result.stderr}")
            return None

        if not binary.is_file():
            LOG.error(f"Binary {binary_name} not found after compilation: {binary}")
            return None
        else:
            LOG.info(
                f"{binary_name} successfully compiled: {binary} ({binary.stat().st_size} bytes)"
            )

        return binary

    def stop(self):
        if not self.running:
            LOG.warning("Profiler not running")
            return False

        LOG.info("Killing samply_helper process")
        self._pkill_process("samply_helper")

        MAX_WAIT_TIME = 120
        LOG.info(f"Waiting for profile at: {self.profile}")
        start = time.time()
        profile_found = False
        while (time.time() - start) < MAX_WAIT_TIME:
            if self.profile.exists():
                LOG.info(f"Profile found: {self.profile}")
                profile_found = True
                break
            time.sleep(1)
        if not profile_found:
            LOG.warning(
                f"Profile not found after {MAX_WAIT_TIME} seconds: {self.profile}"
            )

        # Wait until profile is finished being written to
        # to ensure it is not uploaded prematurely
        MAX_WRITE_TIME = 120
        STABLE_COUNT = 5
        if profile_found:
            LOG.info(
                "Waiting for profile to finish writing (checking file size stability)."
            )
            start = time.time()
            stable_count = 0
            last_size = 0
            while (time.time() - start) < MAX_WRITE_TIME:
                current_size = self.profile.stat().st_size
                if current_size == last_size and last_size > 0:
                    stable_count += 1
                    if stable_count >= STABLE_COUNT:
                        LOG.info(f"Profile file size stable at {current_size} bytes")
                        break
                else:
                    stable_count = 0
                last_size = current_size
                LOG.info(f"Current profile size: {current_size} bytes")
                time.sleep(1)

        LOG.info("Killing samply process")
        self._pkill_process("samply")

        if self.profile.is_file():
            LOG.info(
                f"Profile created: {self.profile} ({self.profile.stat().st_size} bytes)"
            )
        else:
            LOG.error(f"Profile was not created at {self.profile}")
            self.running = False
            return False

        self.running = False
        LOG.info("Samply profiling stopped")
        return True

    def clean(self):
        if self.temp_dir.exists():
            LOG.info(f"Removing temp dir: {self.temp_dir}")
            shutil.rmtree(self.temp_dir)
            self.raptor_config["binary"] = self.original_binary_path

        super().clean()
