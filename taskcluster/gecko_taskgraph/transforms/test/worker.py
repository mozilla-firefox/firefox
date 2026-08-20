# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from taskgraph.transforms.base import TransformSequence

# default worker types keyed by instance-size
LINUX_WORKER_TYPES = {
    "large-legacy": "t-linux-docker",
    "large": "t-linux-docker-amd",
    "large-noscratch": "t-linux-docker-noscratch-amd",
    "xlarge": "t-linux-docker-amd",
    "xlarge-noscratch": "t-linux-docker-noscratch-amd",
    "highcpu": "t-linux-docker-16c32gb-amd",
    "default": "t-linux-docker-noscratch-amd",
}

# linux worker types running the in-tree test docker image
LINUX_DOCKER_WORKER_TYPES = set(LINUX_WORKER_TYPES.values()) | {
    "t-linux-wayland",
    "t-linux-xlarge-wayland",
}

# windows worker types keyed by test-platform and virtualization
WINDOWS_WORKER_TYPES = {
    "windows10-64": {  # source-test
        "virtual": "t-win10-64",
        "virtual-with-gpu": "t-win10-64-gpu-s",
        "hardware": "win10-64-2009-hw",
    },
    "windows10-64-shippable-qr": {
        "virtual": "t-win10-64",
        "virtual-with-gpu": "t-win10-64-gpu-s",
        "hardware": "win10-64-2009-hw",
    },
    "windows10-64-2009": {
        "virtual": "win10-64-2009",
        "virtual-with-gpu": "win10-64-2009-gpu",
        "hardware": "win10-64-2009-hw",
    },
    "windows10-64-2009-qr": {
        "virtual": "win10-64-2009",
        "virtual-with-gpu": "win10-64-2009-gpu",
        "hardware": "win10-64-2009-hw",
    },
    "windows10-64-2009-shippable-qr": {
        "virtual": "win10-64-2009",
        "virtual-with-gpu": "win10-64-2009-gpu",
        "hardware": "win10-64-2009-hw",
    },
    "windows11-32-24h2-mingwclang": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-32-24h2": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-32-24h2-shippable": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-64-24h2-hw-ref-shippable": {
        "virtual": "win11-64-24h2-hw-ref",
        "virtual-with-gpu": "win11-64-24h2-hw-ref",
        "hardware": "win11-64-24h2-hw-ref",
    },
    "windows11-64-24h2-hw-ref": {
        "virtual": "win11-64-24h2-hw-ref",
        "virtual-with-gpu": "win11-64-24h2-hw-ref",
        "hardware": "win11-64-24h2-hw-ref",
    },
    "windows11-64-24h2": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
        "hardware": "win11-64-24h2-hw",
    },
    "windows11-64-24h2-artifact": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-64-24h2-ccov": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-64-24h2-devedition": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-64-24h2-shippable": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
        "hardware": "win11-64-24h2-hw",
    },
    "windows11-64-24h2-asan": {
        "virtual": "win11-64-24h2",
        "large": "win11-64-24h2-large",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-64-24h2-mingwclang": {
        "virtual": "win11-64-24h2",
        "virtual-with-gpu": "win11-64-24h2-gpu",
    },
    "windows11-aarch64-24h2": {
        "virtual": "win11-a64-24h2",
    },
    "windows11-aarch64-24h2-devedition": {
        "virtual": "win11-a64-24h2",
    },
    "windows11-aarch64-24h2-shippable": {
        "virtual": "win11-a64-24h2",
    },
    "windows11-32-25h2-mingwclang": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-32-25h2": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-32-25h2-shippable": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-64-25h2": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
        "hardware": "win11-64-24h2-hw",
    },
    "windows11-64-25h2-ccov": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-64-25h2-devedition": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-64-25h2-shippable": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
        "hardware": "win11-64-24h2-hw",
    },
    "windows11-64-25h2-asan": {
        "virtual": "win11-64-25h2",
        "large": "win11-64-25h2-large",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-64-25h2-mingwclang": {
        "virtual": "win11-64-25h2",
        "virtual-with-gpu": "win11-64-25h2-gpu",
    },
    "windows11-aarch64-25h2": {
        "virtual": "win11-a64-25h2",
    },
    "windows11-aarch64-25h2-devedition": {
        "virtual": "win11-a64-25h2",
    },
    "windows11-aarch64-25h2-shippable": {
        "virtual": "win11-a64-25h2",
    },
}

# os x worker types keyed by test-platform
MACOSX_WORKER_TYPES = {
    "macosx1015-64": "t-osx-1015-r8",
    "macosx1470-64": "t-osx-1400-r8",
    "macosx1400-64": "t-osx-1400-m2",
    "macosx1500-64": "t-osx-1500-m4",
    "macosx1500-aarch64": "t-osx-1500-m4",
    "macosx1500-aarch64-vms": "t-osx-1500-m-vms",
}

transforms = TransformSequence()


@transforms.add
def set_worker_type(config, tasks):
    """Set the worker type based on the test platform."""
    for task in tasks:
        # during the taskcluster migration, this is a bit tortured, but it
        # will get simpler eventually!
        test_platform = task["test-platform"]
        if task.get("worker-type", "default") != "default":
            # This test already has its worker type defined, so just use that (yields below)
            # Unless the value is set to "default", in that case ignore it.
            pass
        elif test_platform.startswith("macosx1015-64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1015-64"]
        elif test_platform.startswith("macosx1470-64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1470-64"]
        elif test_platform.startswith("macosx1400-64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1400-64"]
        elif test_platform.startswith("macosx1400-aarch64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1400-aarch64"]
        elif test_platform.startswith("macosx1500-aarch64-vms"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1500-aarch64-vms"]
        elif test_platform.startswith("macosx1500-aarch64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1500-aarch64"]
        elif test_platform.startswith("macosx1500-64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1500-64"]
        elif test_platform.startswith("win"):
            # figure out what platform the job needs to run on
            if task["virtualization"] == "hardware":
                # some jobs like talos and reftest run on real h/w
                if test_platform.startswith("windows11-64-24h2-hw-ref"):
                    win_worker_type_platform = WINDOWS_WORKER_TYPES[
                        "windows11-64-24h2-hw-ref"
                    ]
                elif test_platform.startswith("windows10-64"):
                    win_worker_type_platform = WINDOWS_WORKER_TYPES["windows10-64"]
                elif test_platform.startswith("windows11-64-24h2"):
                    win_worker_type_platform = WINDOWS_WORKER_TYPES["windows11-64-24h2"]
                elif test_platform.startswith("windows11-64-25h2"):
                    win_worker_type_platform = WINDOWS_WORKER_TYPES["windows11-64-25h2"]
                else:
                    raise Exception(f"Unknown worker type for {test_platform}")
            else:
                # the other jobs run on a vm which may or may not be a win10 vm
                win_worker_type_platform = WINDOWS_WORKER_TYPES[
                    test_platform.split("/")[0]
                ]
                if task[
                    "virtualization"
                ] == "virtual-with-gpu" and test_platform.startswith("windows1"):
                    # add in `--requires-gpu` to the mozharness options
                    task["mozharness"]["extra-options"].append("--requires-gpu")

            # now we have the right platform set the worker type accordingly
            if task["instance-size"].startswith("large") and test_platform.startswith(
                "windows11-"
            ):
                if test_platform.startswith("windows11-64-25h2"):
                    task["worker-type"] = "win11-64-25h2-large"
                else:
                    task["worker-type"] = "win11-64-24h2-large"
            else:
                task["worker-type"] = win_worker_type_platform[task["virtualization"]]
        elif test_platform.startswith("android-hw-p6"):
            if task["suite"] != "raptor":
                task["worker-type"] = "t-bitbar-gw-unit-p6"
            else:
                task["worker-type"] = "t-bitbar-gw-perf-p6"
        elif test_platform.startswith("android-hw-s24"):
            if task["suite"] != "raptor":
                task["worker-type"] = "t-bitbar-gw-unit-s24"
            else:
                task["worker-type"] = "t-bitbar-gw-perf-s24"
        elif test_platform.startswith("android-hw-a55"):
            if task["suite"] != "raptor":
                task["worker-type"] = "t-lambda-perf-a55"
            else:
                task["worker-type"] = "t-bitbar-gw-perf-a55"
        elif test_platform.startswith("android-em-14-arm64"):
            task["worker-type"] = MACOSX_WORKER_TYPES["macosx1500-aarch64"]
        elif test_platform.startswith("android-em-"):
            task["worker-type"] = "t-linux-kvm"
        elif test_platform.startswith("linux") or test_platform.startswith("android"):
            if "wayland" in test_platform:
                if task["instance-size"].startswith("xlarge"):
                    task["worker-type"] = "t-linux-xlarge-wayland"
                else:
                    task["worker-type"] = "t-linux-wayland"
            elif task.get("suite", "") in ["talos", "raptor"] and not task[
                "build-platform"
            ].startswith("linux64-ccov"):
                if test_platform.startswith("linux2404"):
                    if "browsertime-network-bench" in task.get("test-name"):
                        task["worker-type"] = "t-linux-netperf-2404"
                    else:
                        task["worker-type"] = "t-linux-talos-2404"
                elif "browsertime-network-bench" in task.get("test-name"):
                    task["worker-type"] = "t-linux-netperf-1804"
                else:
                    task["worker-type"] = "t-linux-talos-1804"
            else:
                task["worker-type"] = LINUX_WORKER_TYPES[task["instance-size"]]
        else:
            raise Exception(f"unknown test_platform {test_platform}")

        yield task


@transforms.add
def set_wayland_env(config, tasks):
    for task in tasks:
        if "wayland" not in task["test-platform"]:
            yield task
            continue

        env = task.setdefault("worker", {}).setdefault("env", {})
        env["MOZ_ENABLE_WAYLAND"] = "1"
        env["WAYLAND_DISPLAY"] = "wayland-0"
        env["NEED_GNOME_KEYRING"] = "true"
        yield task


@transforms.add
def set_screenshots_font_antialiasing(config, tasks):
    """The Linux test image disables font antialiasing image-wide to match
    releng's setup; mozscreenshots wants real antialiased text for its
    comparison screenshots, so opt this task back in without touching the
    other tasks sharing that image.

    The config file only exists in the in-tree Linux docker image, so only
    tasks running there can point FONTCONFIG_FILE at it: anywhere else this
    would point fontconfig at a missing file rather than merely losing
    antialiasing."""
    for task in tasks:
        if (
            task["test-name"] == "mochitest-browser-screenshots"
            and task["worker-type"] in LINUX_DOCKER_WORKER_TYPES
        ):
            env = task.setdefault("worker", {}).setdefault("env", {})
            env["FONTCONFIG_FILE"] = "/builds/worker/.fonts-aa.conf"
        yield task


@transforms.add
def hide_cmd_exe_window_on_windows(config, tasks):
    for task in tasks:
        if task["test-platform"].startswith("win") and task["suite"] in (
            "raptor",
            "talos",
            "awsy",
        ):
            worker = task.setdefault("worker", {})
            worker["hide-cmd-window"] = True
        yield task
