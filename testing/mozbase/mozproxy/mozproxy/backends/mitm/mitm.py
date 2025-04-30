# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import platform
import signal
import socket
import sys
import time

import mozinfo
from mozprocess import ProcessHandler

from mozproxy.backends.base import Playback
from mozproxy.recordings import RecordingFile
from mozproxy.utils import (
    LOG,
    download_file_from_url,
    get_available_port,
    tooltool_download,
    transform_platform,
)

here = os.path.dirname(__file__)
mitm_folder = os.path.dirname(os.path.realpath(__file__))

# maximal allowed runtime of a mitmproxy command
MITMDUMP_COMMAND_TIMEOUT = 30


class Mitmproxy(Playback):
    def __init__(self, config):
        self.config = config

        self.host = (
            "127.0.0.1" if "localhost" in self.config["host"] else self.config["host"]
        )
        self.port = None
        self.mitmproxy_proc = None
        self.mitmdump_path = None
        self.mitmdump_path_dir = None
        self.record_mode = config.get("record", False)
        self.recording = None
        self.playback_files = []

        self.browser_path = ""
        if config.get("binary", None):
            self.browser_path = os.path.normpath(config.get("binary"))

        self.policies_dir = None
        self.ignore_mitmdump_exit_failure = config.get(
            "ignore_mitmdump_exit_failure", False
        )

        if self.record_mode:
            if "recording_file" not in self.config:
                LOG.error(
                    "recording_file value was not provided. Proxy service wont' start "
                )
                raise Exception("Please provide a playback_files list.")

            if not isinstance(self.config.get("recording_file"), str):
                LOG.error("recording_file argument type is not str!")
                raise Exception("recording_file argument type invalid!")

            if not os.path.splitext(self.config.get("recording_file"))[1] == ".zip":
                LOG.error(
                    "Recording file type (%s) should be a zip. "
                    "Please provide a valid file type!"
                    % self.config.get("recording_file")
                )
                raise Exception("Recording file type should be a zip")

            if os.path.exists(self.config.get("recording_file")):
                LOG.error(
                    "Recording file (%s) already exists."
                    "Please provide a valid file path!"
                    % self.config.get("recording_file")
                )
                raise Exception("Recording file already exists.")

            if self.config.get("playback_files", False):
                LOG.error("Record mode is True and playback_files where provided!")
                raise Exception("playback_files specified during record!")

        if self.config.get("playback_version") is None:
            LOG.error(
                "mitmproxy was not provided with a 'playback_version' "
                "Please provide a valid playback version"
            )
            raise Exception("playback_version not specified!")

        # mozproxy_dir is where we will download all mitmproxy required files
        # when running locally it comes from obj_path via mozharness/mach
        if self.config.get("obj_path") is not None:
            self.mozproxy_dir = self.config.get("obj_path")
        else:
            # in production it is ../tasks/task_N/build/, in production that dir
            # is not available as an envvar, however MOZ_UPLOAD_DIR is set as
            # ../tasks/task_N/build/blobber_upload_dir so take that and go up 1 level
            self.mozproxy_dir = os.path.dirname(
                os.path.dirname(os.environ["MOZ_UPLOAD_DIR"])
            )

        self.mozproxy_dir = os.path.join(self.mozproxy_dir, "testing", "mozproxy")
        self.upload_dir = os.environ.get("MOZ_UPLOAD_DIR", self.mozproxy_dir)

        LOG.info(
            "mozproxy_dir used for mitmproxy downloads and exe files: %s"
            % self.mozproxy_dir
        )
        # setting up the MOZPROXY_DIR env variable so custom scripts know
        # where to get the data
        os.environ["MOZPROXY_DIR"] = self.mozproxy_dir

        LOG.info("Playback tool: %s" % self.config["playback_tool"])
        LOG.info("Playback tool version: %s" % self.config["playback_version"])

    def generate_mitmdump_path(self):
        mitmdump_path_tail = ["mitmdump"]
        if self.config["playback_version"] == "11.0.0" and sys.platform == "darwin":
            # For MacOS newer versions have a different folder structure.
            # Prepend this new path
            mitmdump_path_tail = [
                "mitmproxy.app",
                "Contents",
                "MacOS",
            ] + mitmdump_path_tail

        # mitmproxy is unpacked here
        self.mitmdump_path_dir = os.path.normpath(
            os.path.join(
                self.mozproxy_dir,
                "mitmdump-%s" % self.config["playback_version"],
            )
        )
        self.mitmdump_path = os.path.normpath(
            os.path.join(self.mitmdump_path_dir, *mitmdump_path_tail)
        )

    def download_mitm_bin(self):
        # Download and setup mitm binaries

        manifest = os.path.join(
            here,
            "manifests",
            "mitmproxy-rel-bin-%s-{platform}.manifest"
            % self.config["playback_version"],
        )
        transformed_manifest = transform_platform(
            manifest,
            self.config["platform"],
            platform.processor(),
            self.config["playback_version"],
        )

        # generate the mitmdump_path
        self.generate_mitmdump_path()

        # Check if mitmproxy bin exists
        if os.path.exists(self.mitmdump_path):
            LOG.info("mitmproxy binary already exists. Skipping download")
        else:
            # Download and unpack mitmproxy binary
            download_path = self.mitmdump_path_dir
            LOG.info("create mitmproxy %s dir" % self.config["playback_version"])
            if not os.path.exists(download_path):
                os.makedirs(download_path)

            LOG.info("downloading mitmproxy binary")
            tooltool_download(
                transformed_manifest, self.config["run_local"], download_path
            )

    def download_manifest_file(self, manifest_path):
        # Manifest File
        # we use one pageset for all platforms
        LOG.info("downloading mitmproxy pageset")

        tooltool_download(manifest_path, self.config["run_local"], self.mozproxy_dir)

        with open(manifest_path) as manifest_file:
            manifest = json.load(manifest_file)
            for file in manifest:
                zip_path = os.path.join(self.mozproxy_dir, file["filename"])
                LOG.info("Adding %s to recording list" % zip_path)
                self.playback_files.append(RecordingFile(zip_path))

    def download_playback_files(self):
        # Detect type of file from playback_files and download accordingly
        if "playback_files" not in self.config:
            LOG.error(
                "playback_files value was not provided. Proxy service wont' start "
            )
            raise Exception("Please provide a playback_files list.")

        if not isinstance(self.config["playback_files"], list):
            LOG.error("playback_files should be a list")
            raise Exception("playback_files should be a list")

        for playback_file in self.config["playback_files"]:
            if playback_file.startswith("https://") and "mozilla.com" in playback_file:
                # URL provided
                dest = os.path.join(self.mozproxy_dir, os.path.basename(playback_file))
                download_file_from_url(playback_file, self.mozproxy_dir, extract=False)
                # Add Downloaded file to playback_files list
                LOG.info("Adding %s to recording list" % dest)
                self.playback_files.append(RecordingFile(dest))
                continue

            if not os.path.exists(playback_file):
                LOG.error(
                    "Zip or manifest file path (%s) does not exist. Please provide a valid path!"
                    % playback_file
                )
                raise Exception("Zip or manifest file path does not exist")

            if os.path.splitext(playback_file)[1] == ".zip":
                # zip file path provided
                LOG.info("Adding %s to recording list" % playback_file)
                self.playback_files.append(RecordingFile(playback_file))
            elif os.path.splitext(playback_file)[1] == ".manifest":
                # manifest file path provided
                self.download_manifest_file(playback_file)

    def download(self):
        """Download and unpack mitmproxy binary and pageset using tooltool"""
        if not os.path.exists(self.mozproxy_dir):
            os.makedirs(self.mozproxy_dir)

        self.download_mitm_bin()

        if self.record_mode:
            self.recording = RecordingFile(self.config["recording_file"])
        else:
            self.download_playback_files()

    def stop(self):
        LOG.info("Mitmproxy stop!!")
        self.stop_mitmproxy_playback()
        if self.record_mode:
            LOG.info("Record mode ON. Generating zip file ")
            self.recording.generate_zip_file()

    def wait(self, timeout=1):
        """Wait until the mitmproxy process has terminated."""
        # We wait using this method to allow Windows to respond to the Ctrl+Break
        # signal so that we can exit cleanly from the command-line driver.
        while True:
            returncode = self.mitmproxy_proc.wait(timeout)
            if returncode is not None:
                return returncode

    def start(self):
        # go ahead and download and setup mitmproxy
        self.download()

        # mitmproxy must be started before setup, so that the CA cert is available
        self.start_mitmproxy(self.mitmdump_path, self.browser_path)

        # In case the setup fails, we want to stop the process before raising.
        try:
            self.setup()
        except Exception:
            try:
                self.stop()
            except Exception:
                LOG.error("MitmProxy failed to STOP.", exc_info=True)
            LOG.error("Setup of MitmProxy failed.", exc_info=True)
            raise

    def start_mitmproxy(self, mitmdump_path, browser_path):
        """Startup mitmproxy and replay the specified flow file"""
        if self.mitmproxy_proc is not None:
            raise Exception("Proxy already started.")
        self.port = get_available_port()

        LOG.info("mitmdump path: %s" % mitmdump_path)
        LOG.info("browser path: %s" % browser_path)

        # mitmproxy needs some DLL's that are a part of Firefox itself, so add to path
        env = os.environ.copy()
        env["PATH"] = os.path.dirname(browser_path) + os.pathsep + env["PATH"]
        command = [mitmdump_path]

        if self.config.get("verbose", False):
            # Generate mitmproxy verbose logs
            command.extend(["-v"])
        # add proxy host and port options
        command.extend(["--listen-host", self.host, "--listen-port", str(self.port)])

        # record mode
        if self.record_mode:
            # generate recording script paths

            command.extend(
                [
                    "--save-stream-file",
                    os.path.normpath(self.recording.recording_path),
                    "--set",
                    "websocket=false",
                ]
            )
            if "inject_deterministic" in self.config.keys():
                command.extend(
                    [
                        "--scripts",
                        os.path.join(mitm_folder, "scripts", "inject-deterministic.py"),
                    ]
                )
            self.recording.set_metadata(
                "proxy_version", self.config["playback_version"]
            )
        else:
            # playback mode
            if len(self.playback_files) > 0:
                if self.config["playback_version"] in ["8.1.1", "11.0.0"]:
                    command.extend(
                        [
                            "--set",
                            "websocket=false",
                            "--set",
                            "connection_strategy=lazy",
                            "--set",
                            "alt_server_replay_nopop=true",
                            "--set",
                            "alt_server_replay_kill_extra=true",
                            "--set",
                            "alt_server_replay_order_reversed=true",
                            "--set",
                            "tls_version_client_min=TLS1_2",
                            "--set",
                            "alt_server_replay={}".format(
                                ",".join(
                                    [
                                        os.path.normpath(playback_file.recording_path)
                                        for playback_file in self.playback_files
                                    ]
                                )
                            ),
                            "--scripts",
                            os.path.normpath(
                                os.path.join(
                                    mitm_folder, "scripts", "alt-serverplayback.py"
                                )
                            ),
                        ]
                    )
                elif self.config["playback_version"] in [
                    "4.0.4",
                    "5.1.1",
                    "6.0.2",
                ]:
                    command.extend(
                        [
                            "--set",
                            "upstream_cert=false",
                            "--set",
                            "upload_dir=" + os.path.normpath(self.upload_dir),
                            "--set",
                            "websocket=false",
                            "--set",
                            "server_replay_files={}".format(
                                ",".join(
                                    [
                                        os.path.normpath(playback_file.recording_path)
                                        for playback_file in self.playback_files
                                    ]
                                )
                            ),
                            "--scripts",
                            os.path.normpath(
                                os.path.join(
                                    mitm_folder, "scripts", "alternate-server-replay.py"
                                )
                            ),
                        ]
                    )
                else:
                    raise Exception("Mitmproxy version is unknown!")

            else:
                raise Exception(
                    "Mitmproxy can't start playback! Playback settings missing."
                )

        # mitmproxy needs some DLL's that are a part of Firefox itself, so add to path
        env = os.environ.copy()
        if not os.path.dirname(self.browser_path) in env["PATH"]:
            env["PATH"] = os.path.dirname(self.browser_path) + os.pathsep + env["PATH"]

        LOG.info("Starting mitmproxy playback using env path: %s" % env["PATH"])
        LOG.info("Starting mitmproxy playback using command: %s" % " ".join(command))
        # to turn off mitmproxy log output, use these params for Popen:
        # Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.mitmproxy_proc = ProcessHandler(
            command,
            logfile=os.path.join(self.upload_dir, "mitmproxy.log"),
            env=env,
            storeOutput=False,
        )
        self.mitmproxy_proc.run()
        end_time = time.time() + MITMDUMP_COMMAND_TIMEOUT

        ready = False
        while time.time() < end_time:
            ready = self.check_proxy(host=self.host, port=self.port)
            if ready:
                LOG.info(
                    "Mitmproxy playback successfully started on %s:%d as pid %d"
                    % (self.host, self.port, self.mitmproxy_proc.pid)
                )
                return
            time.sleep(0.25)

        # cannot continue as we won't be able to playback the pages
        LOG.error("Aborting: Mitmproxy process did not startup")
        self.stop_mitmproxy_playback()
        sys.exit(1)  # XXX why do we need to do that? a raise is not enough?

    def stop_mitmproxy_playback(self):
        """Stop the mitproxy server playback"""
        if self.mitmproxy_proc is None or self.mitmproxy_proc.poll() is not None:
            return
        LOG.info(
            "Stopping mitmproxy playback, killing process %d" % self.mitmproxy_proc.pid
        )
        # On Windows, mozprocess brutally kills mitmproxy with TerminateJobObject
        # The process has no chance to gracefully shutdown.
        # Here, we send the process a break event to give it a chance to wrapup.
        # See the signal handler in the alternate-server-replay-4.0.4.py script
        if mozinfo.os == "win":
            LOG.info("Sending CTRL_BREAK_EVENT to mitmproxy")
            os.kill(self.mitmproxy_proc.pid, signal.CTRL_BREAK_EVENT)
            time.sleep(2)

        exit_code = self.mitmproxy_proc.kill()
        self.mitmproxy_proc = None

        if exit_code != 0:
            if exit_code is None:
                LOG.error("Failed to kill the mitmproxy playback process")
                return

            if mozinfo.os == "win":
                from mozprocess.winprocess import (  # noqa
                    ERROR_CONTROL_C_EXIT,
                    ERROR_CONTROL_C_EXIT_DECIMAL,
                )

                if exit_code in [ERROR_CONTROL_C_EXIT, ERROR_CONTROL_C_EXIT_DECIMAL]:
                    LOG.info(
                        "Successfully killed the mitmproxy playback process"
                        " with exit code %d" % exit_code
                    )
                    return
            log_func = LOG.error
            if self.ignore_mitmdump_exit_failure:
                log_func = LOG.info
            log_func("Mitmproxy exited with error code %d" % exit_code)
        else:
            LOG.info("Successfully killed the mitmproxy playback process")

    def check_proxy(self, host, port):
        """Check that mitmproxy process is working by doing a socket call using the proxy settings
        :param host:  Host of the proxy server
        :param port: Port of the proxy server
        :return: True if the proxy service is working
        """
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.connect((host, port))
            s.shutdown(socket.SHUT_RDWR)
            s.close()
            return True
        except OSError:
            return False
