# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import hashlib
import os
import pathlib
import re
import ssl
import subprocess
import sys
import time

from mozperftest.utils import ON_TRY

# Add the python packages installed by mozperftest
sys.path.insert(0, os.environ["PYTHON_PACKAGES"])

import cv2
import numpy as np
from mozdevice import ADBDevice
from mozperftest.profiler import PROFILERS, ProfilingMediator

"""
Homeview:
An error of greater than 0.0002 indicates we have 1 icon, any less than this startup is done
Else(newssite(cvne), shopify (cvne), tab-restore):
An error of greater than 0.001 indicates we have the loading bar present, any less than this startup is done
"""
PERFHERDER_NAMES = {
    "cold_view_nav_end": "applink_startup",
    "mobile_restore": "tab_restore",
    "homeview_startup": "homeview_startup",
}
ACCEPTABLE_THRESHOLD_ERROR = {
    "homeview_startup": 0.0002,
    "cold_view_nav_end": 0.003,
    "mobile_restore": 0.001,
}
BACKGROUND_TABS = [
    "https://www.google.com/search?q=toronto+weather",
    "https://en.m.wikipedia.org/wiki/Anemone_hepatica",
    "https://www.temu.com",
    "https://www.espn.com/nfl/game/_/gameId/401671793/chiefs-falcons",
]
CROP_TOP, CROP_BOTTOM, CROP_LEFT, CROP_RIGHT = (
    100,
    440,
    0,
    20,
)
SUPPORTED_DEVICES = {"SM-A556E": "a55", "Pixel 6": "p6", "SM-S921B": "s24"}
CRITICAL_METRICS = {("newssite_applink_startup", "SM-A55")}
VALID_IMAGES_DIR = "testing/performance/mobile-startup/expected_startup_screenshots"
ERROR_THRESHOLD = 8  # This is the lower bound for the high pass filter to remove noise
MAX_STARTUP_TIME = 25000  # 25000ms = 25 seconds
PROD_CHRM = "chrome-m"
PROD_FENIX = "fenix"

# The certificate served by newssite-setup.sh. Its fingerprint goes into
# cert_override.txt so Fenix trusts the local HTTPS server.
SERVER_CERT = "testing/raptor/browsertime/utils/http2-cert.pem"


class InvalidLastFrame(Exception):
    """If thrown, the difference in images is too high, we suspect a faulty run"""

    pass


class ImageAnalzer:
    def __init__(self, browser, test, test_url, profilers):
        if test == "homeview_startup":
            self.metric_name = PERFHERDER_NAMES[test]
        else:
            self.metric_name = (
                "shopify_" if "shopify" in test_url else "newssite_"
            ) + PERFHERDER_NAMES[test]
        self.video = None
        self.browser = browser
        self.test = test
        self.acceptable_error = ACCEPTABLE_THRESHOLD_ERROR[test]
        self.test_url = test_url
        self.width = 0
        self.height = 0
        self.video_name = ""
        self.package_name = os.environ["BROWSER_BINARY"]
        self.device = ADBDevice()
        self.profiler = ProfilingMediator(profilers)
        self.cpu_data = {"total": {"time": []}}
        if self.browser == PROD_FENIX:
            self.package_and_activity = (
                "org.mozilla.fenix/org.mozilla.fenix.IntentReceiverActivity"
            )
        elif self.browser == PROD_CHRM:
            self.package_and_activity = (
                "com.android.chrome/com.google.android.apps.chrome.IntentDispatcher"
            )
        else:
            raise Exception("Bad browser name")
        self.nav_start_command = f"am start-activity -W -n {self.package_and_activity} -a android.intent.action.VIEW -d "
        self.view_intent_command = (
            f"am start-activity -W -n {self.package_and_activity} -a "
            f"android.intent.action.VIEW"
        )
        self.device.shell("mkdir -p /sdcard/Download")
        self.device.shell("settings put global window_animation_scale 1")
        self.device.shell("settings put global transition_animation_scale 1")
        self.device.shell("settings put global animator_duration_scale 1")
        a11y_enabled = self.device.shell_output(
            "settings get secure accessibility_enabled"
        )
        a11y_services = self.device.shell_output(
            "settings get secure enabled_accessibility_services"
        )
        print(
            f"A11Y STATE: accessibility_enabled={a11y_enabled}, services={a11y_services}"
        )
        print("A11Y: Clearing enabled_accessibility_services and accessibility_enabled")
        self.device.shell('settings put secure enabled_accessibility_services ""')
        self.device.shell("settings put secure accessibility_enabled 0")
        a11y_enabled = self.device.shell_output(
            "settings get secure accessibility_enabled"
        )
        a11y_services = self.device.shell_output(
            "settings get secure enabled_accessibility_services"
        )
        print(
            f"A11Y STATE AFTER CLEAR: accessibility_enabled={a11y_enabled}, services={a11y_services}"
        )
        self.device.disable_notifications("com.topjohnwu.magisk")
        self.device_model = self.device.shell_output("getprop ro.product.model")

    def app_setup(self):
        if ON_TRY:
            self.device.shell(f"pm clear {self.package_name}")
            # Bug 2019204 - Clear Security Hub package to prevent 'not responding' dialogs
            self.device.shell("pm clear com.google.android.apps.security.securityhub")
        time.sleep(3)
        self.skip_onboarding()
        self.device.enable_notifications(
            self.package_name
        )  # enabling notifications for android
        if self.test != "homeview_startup":
            self.create_background_tabs()
        self.device.shell(f"am force-stop {self.package_name}")
        # Extra delay needed to avoid shutdown thread active during startup
        time.sleep(3)
        if self.test_url.startswith("https"):
            self._add_cert_override()

    def _add_cert_override(self):
        """Write cert_override.txt to the Fenix profile.

        This makes Firefox accept the test server's TLS certificate
        without needing enterprise_roots (which loads certs asynchronously
        and can race with the first TLS connection).
        """
        if self.browser != PROD_FENIX:
            return

        data_dir = f"/data/data/{self.package_name}/files/mozilla"
        try:
            entries = self.device.shell_output(f"ls {data_dir}").strip().split()
        except Exception:
            print(f"Profile directory {data_dir} not found, skipping cert override")
            return
        profiles = [e for e in entries if ".default" in e]
        if not profiles:
            print(f"No .default profile under {data_dir}, skipping cert override")
            return

        profile_dir = f"{data_dir}/{profiles[0]}"
        der = ssl.PEM_cert_to_DER_cert(pathlib.Path(SERVER_CERT).read_text())
        fingerprint = hashlib.sha256(der).digest().hex(":").upper()
        # cert_override.txt format: host:port, hash algorithm OID, fingerprint.
        # OID.2.16.840.1.101.3.4.2.1 is SHA-256.
        override_line = f"localhost:8000\tOID.2.16.840.1.101.3.4.2.1\t{fingerprint}\t"
        self.device.shell_output(
            f"echo '{override_line}' > {profile_dir}/cert_override.txt"
        )

    def skip_onboarding(self):
        # Skip onboarding for chrome and fenix
        if self.browser == PROD_CHRM:
            self.device.shell_output(
                'echo "chrome --no-default-browser-check --no-first-run '
                '--disable-fre" > /data/local/tmp/chrome-command-line '
            )
            self.device.shell("am set-debug-app --persistent com.android.chrome")
        elif self.browser == PROD_FENIX:
            self.device.shell(
                "am start-activity -W -a android.intent.action.MAIN --ez "
                "performancetest true -n org.mozilla.fenix/org.mozilla.fenix.App"
            )

    def create_background_tabs(self):
        # Add background tabs that allow us to see the impact of having background tabs open
        # when we do the cold applink startup test. This makes the test workload more realistic
        # and will also help catch regressions that affect per-open-tab startup work.
        for website in BACKGROUND_TABS:
            self.device.shell(self.nav_start_command + website)
            time.sleep(3)
        if self.test == "mobile_restore":
            self.load_page_to_test_startup()

    def get_video(self, run):
        self.video_name = f"vid{run}_{self.browser}.mp4"
        video_location = f"/sdcard/Download/{self.video_name}"

        # Bug 1927548 - Recording command doesn't use mozdevice shell because the mozdevice shell
        # outputs an adbprocess obj whose adbprocess.proc.kill() does not work when called
        recording = subprocess.Popen([
            "adb",
            "shell",
            "screenrecord",
            "--bugreport",
            video_location,
        ])

        # Start Profilers if enabled.
        self.profiler.start()

        if self.test == "cold_view_nav_end":
            self.load_page_to_test_startup()
        elif self.test in ["mobile_restore", "homeview_startup"]:
            self.open_browser_with_view_intent()

        # Stop Profilers if enabled.
        self.profiler.stop(os.environ["TESTING_DIR"], run)

        self.process_cpu_info(run)
        recording.kill()
        time.sleep(5)
        self.device.command_output([
            "pull",
            "-a",
            video_location,
            os.environ["TESTING_DIR"],
        ])

        time.sleep(4)
        video_location = str(pathlib.Path(os.environ["TESTING_DIR"], self.video_name))
        self.video = cv2.VideoCapture(video_location)
        self.width = self.video.get(cv2.CAP_PROP_FRAME_WIDTH)
        self.height = self.video.get(cv2.CAP_PROP_FRAME_HEIGHT)
        self.device.shell(f"am force-stop {self.package_name}")

    def get_image(self, frame_position, cropped=True, bw=True):
        self.video.set(cv2.CAP_PROP_POS_FRAMES, frame_position)
        ret, frame = self.video.read()
        if bw:
            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        if not ret:
            raise Exception("Frame not read")
        # We crop out the top 100 pixels in each image as when we have --bug-report in the
        # screen-recording command it displays a timestamp which interferes with the image comparisons
        # We crop out the bottom pixels to remove the fading in of the OS navigation
        # controls, and the chrome-m command-line-flags popup
        # We crop out the right 20 pixels to remove the scroll bar as it interferes with startup accuracy
        if cropped:
            return frame[
                CROP_TOP : int(self.height) - CROP_BOTTOM,
                CROP_LEFT : int(self.width) - CROP_RIGHT,
            ]
        return frame

    def error(self, img1, img2):
        h = img1.shape[0]
        w = img1.shape[1]
        diff = cv2.absdiff(img1, img2)
        threshold_diff = cv2.threshold(diff, ERROR_THRESHOLD, 255, cv2.THRESH_BINARY)[1]
        err = np.sum(threshold_diff**2)
        mse = err / (float(h * w))
        return mse

    def get_page_loaded_time(self, iteration):
        """
        Returns the index of the frame where the main image on the shopify demo page is displayed
        for the first time.
        Specifically, we find the index of the first frame whose image is within an error of 20
        compared to the final frame, via binary search. The binary search assumes that the error
        compared to the final frame decreases monotonically in the captured frames.
        """
        final_frame_index = self.video.get(cv2.CAP_PROP_FRAME_COUNT) - 1
        final_frame = self.get_image(final_frame_index)
        compare_to_end_frame = final_frame_index
        diff = 0

        while diff <= self.acceptable_error:
            compare_to_end_frame -= 1
            if compare_to_end_frame < 0:
                raise Exception(
                    "Could not find the initial pageload frame, all possible images compared"
                )
            diff = self.error(self.get_image(compare_to_end_frame), final_frame)

        compare_to_end_frame += 1
        save_image_location = pathlib.Path(
            os.environ["TESTING_DIR"],
            f"iter_{iteration}_startup_done.png",
        )
        cv2.imwrite(
            save_image_location,
            self.get_image(compare_to_end_frame, cropped=False, bw=False),
        )
        return compare_to_end_frame

    def get_time_from_frame_num(self, frame_num):
        self.video.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
        self.video.read()
        video_timestamp = self.video.get(cv2.CAP_PROP_POS_MSEC)
        if video_timestamp > MAX_STARTUP_TIME:
            raise ValueError(
                f"Startup time of {video_timestamp / 1000}s exceeds max time of {MAX_STARTUP_TIME / 1000}s"
            )
        return video_timestamp

    def load_page_to_test_startup(self):
        # Navigate to the page we want to use for testing startup
        self.device.shell(self.nav_start_command + self.test_url)
        time.sleep(5)

    def open_browser_with_view_intent(self):
        self.device.shell(self.view_intent_command)
        time.sleep(5)

    def process_cpu_info(self, run):
        cpu_info = self.device.shell_output(
            f"ps -A -o name=,cpu=,time+=,%cpu= | grep {self.package_name}"
        ).split("\n")
        total_time_seconds = tab_processes_time = 0
        for process in cpu_info:
            process_name = re.search(r"([\w\d_.:]+)\s", process).group(1)
            time = re.search(r"\s(\d+):(\d+).(\d+)\s", process)
            time_seconds = (
                10 * int(time.group(3))
                + 1000 * int(time.group(2))
                + 60 * 1000 * int(time.group(1))
            )
            total_time_seconds += time_seconds
            # Fenix process names may be tagged with "sandboxed_" (see bug
            # 2079728) and "_disable_art_image_" (see bug 2005825) but that
            # should be ignored for the process names used here.
            if "org.mozilla.fenix" in process_name:
                process_name = process_name.replace(":sandboxed_", ":").replace(
                    "_disable_art_image_", ""
                )

            if "org.mozilla.fenix:tab" in process_name:
                process_name = "org.mozilla.fenix:tab"
            if (
                "com.android.chrome" in process_name
                and "sandboxed_process" in process_name
            ):
                process_name = "com.android.chrome:sandboxed_process"

            if process_name not in self.cpu_data.keys():
                self.cpu_data[process_name] = {}
                self.cpu_data[process_name]["time"] = []

            if "org.mozilla.fenix:tab" == process_name:
                tab_processes_time += time_seconds
                continue
            self.cpu_data[process_name]["time"] += [time_seconds]

        if tab_processes_time:
            self.cpu_data["org.mozilla.fenix:tab"]["time"] += [tab_processes_time]
        self.cpu_data["total"]["time"] += [total_time_seconds]

    def alert_severity(self):
        """Severity of the alerts produced by this test's main metric."""
        if self.browser != PROD_FENIX:
            return "normal"
        for metric, model in CRITICAL_METRICS:
            if metric == self.metric_name and model in self.device_model:
                return "critical"
        return "normal"

    def perfmetrics_cpu_data_ingesting(self):
        for process in self.cpu_data.keys():
            print(
                'perfMetrics: {"values": '
                + str(self.cpu_data[process]["time"])
                + ', "name": "'
                + process
                + f'-cpu-time", "shouldAlert": true, "suite": "{self.metric_name}_submetrics"}}'
            )

    def validate_end_frame(self, frame_to_check):
        if SUPPORTED_DEVICES.get(self.device_model, False):
            device = SUPPORTED_DEVICES.get(self.device_model)
            filename = f"{self.browser}-{self.test}"
            if self.test == "cold_view_nav_end":
                if "shopify" in self.test_url:
                    filename += "-shopify"
                elif "localhost" in self.test_url:
                    filename += "-newssite"
            filename += f"-{device}.png"
            validated_image = cv2.imread(str(pathlib.Path(VALID_IMAGES_DIR, filename)))
            cropped_image = validated_image[
                CROP_TOP : int(self.height) - CROP_BOTTOM,
                CROP_LEFT : int(self.width) - CROP_RIGHT,
            ]
            cropped_image = cv2.cvtColor(cropped_image, cv2.COLOR_BGR2GRAY)
            diff = self.error(self.get_image(frame_to_check), cropped_image)
            print(f"Error we found in images: {diff}")
            if diff > 0.5:
                raise InvalidLastFrame(
                    "Difference in Images is too high, suspected faulty run"
                )

    def run_test(self, iteration):
        self.app_setup()
        self.get_video(iteration)
        return self.get_page_loaded_time(iteration)


def get_profiler_combinations():
    """Returns a list of profiler combinations based on which one are enabled.
    If multiple profilers are enabled, returns each profile,then all enabled profilers together
    """
    enabled = [name for name, cls in PROFILERS.items() if cls.is_enabled()]

    if len(enabled) > 1:
        return [[p] for p in enabled] + [enabled]
    return [enabled] if enabled else []


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise Exception("Didn't pass the args properly :(")
    start_video_timestamp = []
    browser = sys.argv[1]
    test = sys.argv[2]
    test_url = sys.argv[3]

    base_testing_dir = os.environ["TESTING_DIR"]
    profiler_combinations = get_profiler_combinations()
    iterations = 10
    if not profiler_combinations:
        profiler_combinations = [[]]
    for profilers in profiler_combinations:
        if profilers:
            subdir_name = "-".join(profilers)
            output_path = pathlib.Path(base_testing_dir) / subdir_name
            output_path.mkdir(parents=True, exist_ok=True)
            os.environ["TESTING_DIR"] = str(output_path)
            iterations = 5
        else:
            os.environ["TESTING_DIR"] = base_testing_dir

        ImageObject = ImageAnalzer(browser, test, test_url, profilers)
        for iteration in range(iterations):
            nav_done_frame = ImageObject.run_test(iteration)
            try:
                ImageObject.validate_end_frame(nav_done_frame)
            except InvalidLastFrame:
                print("Something went wrong, retrying image validation")
                nav_done_frame = ImageObject.run_test(iteration)
                ImageObject.validate_end_frame(nav_done_frame)
            start_video_timestamp += [
                ImageObject.get_time_from_frame_num(nav_done_frame)
            ]
    print(
        'perfMetrics: {"values": '
        + str(start_video_timestamp)
        + ', "name": "'
        + PERFHERDER_NAMES[test]
        + '", "shouldAlert": true, "alertSeverity": "'
        + ImageObject.alert_severity()
        + '"}'
    )

    print(
        'perfMetrics: {"values": '
        + str(start_video_timestamp)
        + ', "name": "'
        + ImageObject.metric_name
        + f'", "shouldAlert": true, "suite": "{ImageObject.metric_name}_submetrics"}}'
    )

    ImageObject.perfmetrics_cpu_data_ingesting()
