# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import hashlib
import re
from typing import Literal, Optional

import msgspec
from mozbuild.schedules import INCLUSIVE_COMPONENTS
from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.attributes import keymatch
from taskgraph.util.keyed_by import evaluate_keyed_by
from taskgraph.util.readonlydict import ReadOnlyDict
from taskgraph.util.schema import Schema, resolve_keyed_by
from taskgraph.util.taskcluster import get_artifact_path
from taskgraph.util.templates import merge

from gecko_taskgraph.transforms.test.variant import TEST_VARIANTS
from gecko_taskgraph.util.perftest import is_external_browser
from gecko_taskgraph.util.platforms import platform_family
from gecko_taskgraph.util.taskcluster import get_index_url

transforms = TransformSequence()


@transforms.add
def limit_platforms(config, tasks):
    for task in tasks:
        if not task["limit-platforms"]:
            yield task
            continue

        limited_platforms = {key: key for key in task["limit-platforms"]}
        if keymatch(limited_platforms, task["test-platform"]):
            yield task


@transforms.add
def handle_suite_category(config, tasks):
    for task in tasks:
        task.setdefault("suite", {})

        if isinstance(task["suite"], str):
            task["suite"] = {"name": task["suite"]}

        suite = task["suite"].setdefault("name", task["test-name"])
        category = task["suite"].setdefault("category", suite)

        task.setdefault("attributes", {})
        task["attributes"]["unittest_suite"] = suite
        task["attributes"]["unittest_category"] = category

        script = task["mozharness"]["script"]
        category_arg = None
        if suite.startswith("test-verify") or suite.startswith("test-coverage"):
            pass
        elif script in ("android_emulator_unittest.py", "android_hardware_unittest.py"):
            category_arg = "--test-suite"
        elif script == "desktop_unittest.py":
            category_arg = f"--{category}-suite"

        if category_arg:
            resolve_keyed_by(
                task,
                "mozharness.extra-options",
                item_name=task["test-name"],
                enforce_single_match=False,
                variant=task["attributes"].get("unittest_variant"),
            )

            task["mozharness"].setdefault("extra-options", [])
            extra = task["mozharness"]["extra-options"]
            if not any(arg.startswith(category_arg) for arg in extra):
                extra.append(f"{category_arg}={suite}")

        # From here on out we only use the suite name.
        task["suite"] = suite

        # in the future we might need to refactor new-test-config to be suite specific
        if "mochitest" in task["suite"] and config.params["try_task_config"].get(
            "new-test-config", False
        ):
            task = merge(
                task, {"mozharness": {"extra-options": ["--restartAfterFailure"]}}
            )
        yield task


@transforms.add
def setup_talos(config, tasks):
    """Add options that are specific to talos jobs (identified by suite=talos)"""
    for task in tasks:
        if task["suite"] != "talos":
            yield task
            continue

        resolve_keyed_by(
            task,
            "mozharness.extra-options",
            item_name=task["test-name"],
            enforce_single_match=False,
            variant=task["attributes"].get("unittest_variant"),
        )

        extra_options = task.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )
        extra_options.append("--use-talos-json")

        if config.params.get("project", None):
            extra_options.append("--project=%s" % config.params["project"])

        if "pdfpaint" in task["try-name"]:
            max_chunks = 10
            for chunk in range(1, max_chunks + 1):
                new_task = copy.deepcopy(task)
                new_task["mozharness"]["extra-options"].append(
                    f"--pdfPaintChunk={chunk}"
                )
                new_task["test-name"] = task["test-name"].replace(
                    "pdfpaint", f"pdfpaint-{chunk}"
                )
                new_task["try-name"] = task["try-name"].replace(
                    "pdfpaint", f"pdfpaint-{chunk}"
                )
                new_task["treeherder-symbol"] = task["treeherder-symbol"].replace(
                    "pdfpaint", f"pdfpaint-{chunk}"
                )
                yield new_task
            continue

        yield task


@transforms.add
def setup_browsertime_flag(config, tasks):
    """Optionally add `--browsertime` flag to Raptor pageload tests."""

    browsertime_flag = config.params["try_task_config"].get("browsertime", False)

    for task in tasks:
        if not browsertime_flag or task["suite"] != "raptor":
            yield task
            continue

        if task["treeherder-symbol"].startswith("Rap"):
            # The Rap group is subdivided as Rap{-fenix,-refbrow(...),
            # so `taskgraph.util.treeherder.replace_group` isn't appropriate.
            task["treeherder-symbol"] = task["treeherder-symbol"].replace(
                "Rap", "Btime", 1
            )

        extra_options = task.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )
        extra_options.append("--browsertime")

        yield task


@transforms.add
def handle_artifact_prefix(config, tasks):
    """Handle translating `artifact_prefix` appropriately"""
    for task in tasks:
        if task["build-attributes"].get("artifact_prefix"):
            task.setdefault("attributes", {}).setdefault(
                "artifact_prefix", task["build-attributes"]["artifact_prefix"]
            )
        yield task


@transforms.add
def set_treeherder_machine_platform(config, tasks):
    """Set the appropriate task.extra.treeherder.machine.platform"""
    translation = {
        # Linux64 build platform for asan is specified differently to
        # treeherder.
        "macosx1400-64/opt": "osx-1300/opt",
        "macosx1400-64-shippable/opt": "osx-1400-shippable/opt",
        "macosx1500-64/opt": "osx-1500/opt",
        "macosx1500-64-shippable/opt": "osx-1500-shippable/opt",
        "win64-asan/opt": "windows11-64-25h2/asan",
    }
    for task in tasks:
        # For most desktop platforms, the above table is not used for "regular"
        # builds, so we'll always pick the test platform here.
        # On macOS though, the regular builds are in the table.  This causes a
        # conflict in `verify_task_graph_symbol` once you add a new test
        # platform based on regular macOS builds, such as for QR.
        # Since it's unclear if the regular macOS builds can be removed from
        # the table, workaround the issue for QR.
        if "android" in task["test-platform"] and "pgo/opt" in task["test-platform"]:
            platform_new = task["test-platform"].replace("-pgo/opt", "/pgo")
            task["treeherder-machine-platform"] = platform_new
        elif "android-em-" in task["test-platform"]:
            task["treeherder-machine-platform"] = task["test-platform"]
        elif "android-hw" in task["test-platform"]:
            task["treeherder-machine-platform"] = task["test-platform"]

        if "-qr" in task["test-platform"]:
            task["treeherder-machine-platform"] = task["test-platform"]
        else:
            task["treeherder-machine-platform"] = translation.get(
                task["build-platform"], task["test-platform"]
            )
        yield task


@transforms.add
def set_download_symbols(config, tasks):
    """In general, we download symbols immediately for debug builds, but only
    on demand for everything else. ASAN builds shouldn't download
    symbols since they don't product symbol zips see bug 1283879"""
    for task in tasks:
        if task["test-platform"].split("/")[-1] == "debug":
            task["mozharness"]["download-symbols"] = True
        elif "asan" in task["build-platform"] or "tsan" in task["build-platform"]:
            if "download-symbols" in task["mozharness"]:
                del task["mozharness"]["download-symbols"]
        else:
            task["mozharness"]["download-symbols"] = "ondemand"
        yield task


@transforms.add
def handle_keyed_by(config, tasks):
    """Resolve fields that can be keyed by platform, etc."""
    fields = [
        "instance-size",
        "docker-image",
        "max-run-time",
        "chunks",
        "default-chunks",
        "suite",
        "run-on-projects",
        "os-groups",
        "run-as-administrator",
        "workdir",
        "worker-type",
        "virtualization",
        "fetches.fetch",
        "fetches.toolchain",
        "target",
        "webrender-run-on-projects",
        "mozharness.extra-options",
        "mozharness.requires-signed-builds",
        "build-signing-label",
        "dependencies",
    ]
    for task in tasks:
        for field in fields:
            resolve_keyed_by(
                task,
                field,
                item_name=task["test-name"],
                enforce_single_match=False,
                project=config.params["project"],
                variant=task["attributes"].get("unittest_variant"),
            )
        yield task


@transforms.add
def setup_raptor_external_browser_platforms(config, tasks):
    for task in tasks:
        if task["suite"] != "raptor":
            yield task
            continue

        if is_external_browser(task["try-name"]):
            if "win" in task["build-label"]:
                task["build-platform"] = "windows2012-64/opt"
                task["build-label"] = "build-win64/opt"
            else:
                task["build-platform"] = "linux64/opt"
                task["build-label"] = "build-linux64/opt"

        yield task


@transforms.add
def set_target(config, tasks):
    for task in tasks:
        build_platform = task["build-platform"]
        target = None
        if "target" in task:
            target = task["target"]
        if not target:
            if build_platform.startswith("macosx"):
                target = "target.dmg"
            elif build_platform.startswith("android"):
                target = "target.apk"
            elif build_platform.startswith("win"):
                target = "target.zip"
            else:
                target = "target.tar.xz"

        if isinstance(target, dict):
            if "index" in target:
                # TODO Remove hardcoded mobile artifact prefix
                index_url = get_index_url(target["index"])
                installer_url = "{}/artifacts/public/{}".format(
                    index_url, target["name"]
                )
                task["mozharness"]["installer-url"] = installer_url
            else:
                task["mozharness"]["installer-url"] = (
                    f"<{target['upstream-task']}/{target['name']}>"
                )
        else:
            task["mozharness"]["build-artifact-name"] = get_artifact_path(task, target)

        yield task


@transforms.add
def setup_browsertime(config, tasks):
    """Configure browsertime dependencies for Raptor pageload tests that have
    `--browsertime` extra option."""

    for task in tasks:
        # We need to make non-trivial changes to various fetches, and our
        # `by-test-platform` may not be "compatible" with existing
        # `by-test-platform` filters.  Therefore we do everything after
        # `handle_keyed_by` so that existing fields have been resolved down to
        # simple lists.  But we use the `by-test-platform` machinery to express
        # filters so that when the time comes to move browsertime into YAML
        # files, the transition is straight-forward.
        extra_options = task.get("mozharness", {}).get("extra-options", [])

        if task["suite"] != "raptor":
            yield task
            continue

        ts = {
            "by-test-platform": {
                "android.*": ["browsertime", "linux64-geckodriver", "linux64-node"],
                "linux.*": ["browsertime", "linux64-geckodriver", "linux64-node"],
                "macosx1470.*": [
                    "browsertime",
                    "macosx64-geckodriver",
                    "macosx64-node",
                ],
                "macosx1400.*": [
                    "browsertime",
                    "macosx64-geckodriver",
                    "macosx64-aarch64-node",
                ],
                "macosx1500.*": [
                    "browsertime",
                    "macosx64-geckodriver",
                    "macosx64-aarch64-node",
                ],
                "windows.*aarch64.*": [
                    "browsertime",
                    "win32-geckodriver",
                    "win32-node",
                ],
                "windows.*-64.*": ["browsertime", "win64-geckodriver", "win64-node"],
            },
        }

        task.setdefault("fetches", {}).setdefault("toolchain", []).extend(
            evaluate_keyed_by(ts, "fetches.toolchain", task)
        )

        fs = {
            "by-test-platform": {
                "android.*": ["linux64-ffmpeg-7.1"],
                "linux.*": ["linux64-ffmpeg-7.1"],
                "macosx1470.*": ["mac64-ffmpeg-7.1"],
                "macosx1400.*": ["mac64-ffmpeg-7.1"],
                "macosx1500.*": ["mac64-ffmpeg-7.1"],
                "windows.*aarch64.*": ["win64-ffmpeg-7.1"],
                "windows.*-64.*": ["win64-ffmpeg-7.1"],
            },
        }

        cd_fetches = {
            "android.*": [
                "linux64-cft-cd-backup",
                "linux64-cft-cd-stable",
                "linux64-cft-cd-beta",
            ],
            "linux.*": [
                "linux64-cft-cd-backup",
                "linux64-cft-cd-stable",
                "linux64-cft-cd-beta",
            ],
            "macosx1470.*": [
                "mac-cft-cd-backup",
                "mac-cft-cd-stable",
                "mac-cft-cd-beta",
            ],
            "macosx1400.*": [
                "mac-cft-cd-arm-backup",
                "mac-cft-cd-arm-stable",
                "mac-cft-cd-arm-beta",
            ],
            "macosx1500.*": [
                "mac-cft-cd-arm-backup",
                "mac-cft-cd-arm-stable",
                "mac-cft-cd-arm-beta",
            ],
            "windows.*-64.*": [
                "win64-cft-cd-backup",
                "win64-cft-cd-stable",
                "win64-cft-cd-beta",
            ],
        }

        chromium_fetches = {
            "linux.*": ["linux64-cft-cd-canary"],
            "macosx1400.*": ["mac-cft-cd-arm-canary"],
            "macosx1470.*": ["mac-cft-cd-canary"],
            "macosx1500.*": ["mac-cft-cd-arm-canary"],
            "windows.*-64.*": ["win64-cft-cd-canary"],
            "android.*": ["linux64-cft-cd-canary"],
        }

        cd_extracted_name = {
            "windows": "{}chromedriver.exe",
            "mac": "{}chromedriver",
            "default": "{}chromedriver",
        }

        if "--app=chrome" in extra_options or "--app=chrome-m" in extra_options:
            # Only add the chromedriver fetches when chrome is running
            for platform in cd_fetches:
                fs["by-test-platform"][platform].extend(cd_fetches[platform])
        if "--app=custom-car" in extra_options or "--app=cstm-car-m" in extra_options:
            for platform in chromium_fetches:
                fs["by-test-platform"][platform].extend(chromium_fetches[platform])

            # The Chrome-for-Testing chromedrivers are repackaged into the following
            # platform specific archives. The versions will always be compatible as
            # these are fetched from the `Canary` channel.
            cd_extracted_name = {
                "windows": "cft-chromedriver-win64/chromedriver.exe",
                "mac": "cft-chromedriver-mac/chromedriver",
                "default": "cft-chromedriver-linux/chromedriver",
            }

        # Disable the Raptor install step
        if "--app=chrome-m" in extra_options or "--app=cstm-car-m" in extra_options:
            extra_options.append("--no-install")

        task.setdefault("fetches", {}).setdefault("fetch", []).extend(
            evaluate_keyed_by(fs, "fetches.fetch", task)
        )

        extra_options.extend((
            "--browsertime-browsertimejs",
            "$MOZ_FETCHES_DIR/browsertime/node_modules/browsertime/bin/browsertime.js",
        ))  # noqa: E501

        eos = {
            "by-test-platform": {
                "windows.*": [
                    "--browsertime-node",
                    "$MOZ_FETCHES_DIR/node/node.exe",
                    "--browsertime-geckodriver",
                    "$MOZ_FETCHES_DIR/geckodriver.exe",
                    "--browsertime-chromedriver",
                    "$MOZ_FETCHES_DIR/" + cd_extracted_name["windows"],
                    "--browsertime-ffmpeg",
                    "$MOZ_FETCHES_DIR/ffmpeg-n7.1-latest-win64-gpl-shared-7.1/bin/ffmpeg.exe",
                ],
                "macosx.*": [
                    "--browsertime-node",
                    "$MOZ_FETCHES_DIR/node/bin/node",
                    "--browsertime-geckodriver",
                    "$MOZ_FETCHES_DIR/geckodriver",
                    "--browsertime-chromedriver",
                    "$MOZ_FETCHES_DIR/" + cd_extracted_name["mac"],
                    "--browsertime-ffmpeg",
                    "$MOZ_FETCHES_DIR/ffmpeg-7.1/bin/ffmpeg",
                ],
                "default": [
                    "--browsertime-node",
                    "$MOZ_FETCHES_DIR/node/bin/node",
                    "--browsertime-geckodriver",
                    "$MOZ_FETCHES_DIR/geckodriver",
                    "--browsertime-chromedriver",
                    "$MOZ_FETCHES_DIR/" + cd_extracted_name["default"],
                    "--browsertime-ffmpeg",
                    "$MOZ_FETCHES_DIR/ffmpeg-n7.1-linux64-gpl-7.1/bin/ffmpeg",
                ],
            }
        }

        extra_options.extend(evaluate_keyed_by(eos, "mozharness.extra-options", task))

        yield task


def get_mobile_project(task):
    """Returns the mobile project of the specified task or None."""

    if not task["build-platform"].startswith("android"):
        return

    mobile_projects = ("fenix", "geckoview", "refbrow", "chrome-m", "cstm-car-m")

    for name in mobile_projects:
        if name in task["test-name"]:
            return name

    target = None
    if "target" in task:
        resolve_keyed_by(
            task, "target", item_name=task["test-name"], enforce_single_match=False
        )
        target = task["target"]
    if target:
        if isinstance(target, dict):
            target = target["name"]

        for name in mobile_projects:
            if name in target:
                return name

    return None


@transforms.add
def disable_wpt_timeouts_on_autoland(config, tasks):
    """do not run web-platform-tests that are expected TIMEOUT on autoland"""
    for task in tasks:
        if (
            "web-platform-tests" in task["test-name"]
            and config.params["project"] == "autoland"
        ):
            resolve_keyed_by(
                task,
                "mozharness.extra-options",
                item_name=task["test-name"],
                enforce_single_match=False,
                variant=task["attributes"].get("unittest_variant"),
            )

            task["mozharness"].setdefault("extra-options", []).append("--skip-timeout")
        yield task


@transforms.add
def enable_code_coverage(config, tasks):
    """Enable code coverage for the ccov build-platforms"""
    for task in tasks:
        if "ccov" in task["build-platform"]:
            task.setdefault("attributes", {})["ccov"] = True

            # Do not run tests on fuzzing builds
            if "fuzzing" in task["build-platform"]:
                task["run-on-projects"] = []
                continue

            # Skip this transform for android code coverage builds.
            if "android" in task["build-platform"]:
                task.setdefault("fetches", {}).setdefault("toolchain", []).append(
                    "linux64-grcov"
                )
                task["mozharness"].setdefault("extra-options", []).append(
                    "--java-code-coverage"
                )
                yield task
                continue
            task["mozharness"].setdefault("extra-options", []).append("--code-coverage")
            task["instance-size"] = "xlarge-noscratch"
            if "jittest" in task["test-name"]:
                task["instance-size"] = "xlarge"

            # Temporarily disable Mac tests on mozilla-central
            if "mac" in task["build-platform"]:
                task["run-on-projects"] = []

            # Ensure we always run on the projects defined by the build, unless the test
            # is try only or shouldn't run at all.
            if task["run-on-projects"] not in [[]]:
                task["run-on-projects"] = "built-projects"

            # Ensure we don't optimize test suites out.
            # We always want to run all test suites for coverage purposes.
            task.pop("schedules-component", None)
            task.pop("when", None)
            task["optimization"] = None

            # Add a toolchain and a fetch task for the grcov binary.
            if any(p in task["build-platform"] for p in ("linux", "osx", "win")):
                task.setdefault("fetches", {})
                task["fetches"].setdefault("fetch", [])
                task["fetches"].setdefault("toolchain", [])
                task["fetches"].setdefault("build", [])

            if "linux" in task["build-platform"]:
                task["fetches"]["toolchain"].append("linux64-grcov")
            elif "osx" in task["build-platform"]:
                task["fetches"]["toolchain"].append("macosx64-grcov")
            elif "win" in task["build-platform"]:
                task["fetches"]["toolchain"].append("win64-grcov")

            task["fetches"]["build"].append({"artifact": "target.mozinfo.json"})

        yield task


@transforms.add
def handle_run_on_projects(config, tasks):
    """Handle translating `built-projects` appropriately"""
    for task in tasks:
        if task["run-on-projects"] == "built-projects":
            task["run-on-projects"] = task["build-attributes"].get(
                "run_on_projects", ["all"]
            )

        if task.pop("built-projects-only", False):
            built_projects = set(
                task["build-attributes"].get("run_on_projects", {"all"})
            )
            run_on_projects = set(task.get("run-on-projects", set()))

            # If 'all' exists in run-on-projects, then the intersection of both
            # is built-projects. Similarly if 'all' exists in built-projects,
            # the intersection is run-on-projects (so do nothing). When neither
            # contains 'all', take the actual set intersection.
            if "all" in run_on_projects:
                task["run-on-projects"] = sorted(built_projects)
            elif "all" not in built_projects:
                task["run-on-projects"] = sorted(run_on_projects & built_projects)
        yield task


@transforms.add
def handle_tier(config, tasks):
    """Set the tier based on policy for all test descriptions that do not
    specify a tier otherwise."""
    for task in tasks:
        if "tier" in task:
            resolve_keyed_by(
                task,
                "tier",
                item_name=task["test-name"],
                variant=task["attributes"].get("unittest_variant"),
                enforce_single_match=False,
            )

        # only override if not set for the test
        if "tier" not in task or task["tier"] == "default":
            if task["test-platform"] in [
                "linux64/opt",
                "linux64/debug",
                "linux64-shippable/opt",
                "linux64-devedition/opt",
                "linux64-asan/opt",
                "linux64-qr/opt",
                "linux64-qr/debug",
                "linux64-shippable-qr/opt",
                "linux2204-64-wayland/debug",
                "linux2204-64-wayland/opt",
                "linux2204-64-wayland-shippable/opt",
                "linux2404-64/opt",
                "linux2404-64/debug",
                "linux2404-64-shippable/opt",
                "linux2404-64-devedition/opt",
                "linux2404-64-asan/opt",
                "linux2404-64-tsan/opt",
                "windows11-32-25h2/debug",
                "windows11-32-25h2/opt",
                "windows11-32-25h2-shippable/opt",
                "windows11-64-24h2-hw-ref/opt",
                "windows11-64-24h2-hw-ref-shippable/opt",
                "windows11-64-24h2/opt",
                "windows11-64-24h2-shippable/opt",
                "windows11-64-25h2/opt",
                "windows11-64-25h2/debug",
                "windows11-64-25h2-shippable/opt",
                "windows11-64-25h2-devedition/opt",
                "windows11-64-25h2-asan/opt",
                "macosx1015-64/opt",
                "macosx1015-64/debug",
                "macosx1015-64-shippable/opt",
                "macosx1015-64-devedition/opt",
                "macosx1015-64-devedition-qr/opt",
                "macosx1015-64-qr/opt",
                "macosx1015-64-shippable-qr/opt",
                "macosx1015-64-qr/debug",
                "macosx1470-64/opt",
                "macosx1470-64/debug",
                "macosx1470-64-shippable/opt",
                "macosx1470-64-devedition/opt",
                "macosx1500-aarch64/opt",
                "macosx1500-aarch64/debug",
                "macosx1500-aarch64-shippable/opt",
                "macosx1500-aarch64-devedition/opt",
                "android-em-14-x86_64-shippable/opt",
                "android-em-14-x86_64/opt",
                "android-em-14-x86_64-shippable-lite/opt",
                "android-em-14-x86_64-lite/opt",
                "android-em-14-x86_64/debug",
                "android-em-14-x86_64/debug-isolated-process",
                "android-em-14-x86-shippable/opt",
                "android-em-14-x86/opt",
            ]:
                task["tier"] = 1
            else:
                task["tier"] = 2

        yield task


@transforms.add
def apply_artifact_build_settings(config, tasks):
    """Artifact build tests are tier 2. On Linux and Windows they run on
    all mozilla-central pushes; on other platforms they are try-only."""
    for task in tasks:
        if "-artifact/" in task["test-platform"]:
            task["tier"] = 2
            if not task["test-platform"].startswith(("linux", "windows")):
                task["run-on-projects"] = []
        yield task


@transforms.add
def apply_raptor_tier_optimization(config, tasks):
    for task in tasks:
        if task["suite"] != "raptor":
            yield task
            continue

        if "regression-tests" in task["test-name"]:
            # Don't optimize the regression tests
            yield task
            continue

        if not task["test-platform"].startswith("android-hw"):
            task["optimization"] = {"skip-unless-expanded": None}
            if task["tier"] > 1:
                task["optimization"] = {"skip-unless-backstop": None}

        if task["attributes"].get("unittest_variant"):
            task["tier"] = max(task["tier"], 2)
        yield task


@transforms.add
def disable_try_only_platforms(config, tasks):
    """Turns off platforms that should only run on try."""
    try_only_platforms = ()
    for task in tasks:
        if any(re.match(k + "$", task["test-platform"]) for k in try_only_platforms):
            task["run-on-projects"] = []
        yield task


@transforms.add
def ensure_spi_disabled_on_all_but_spi(config, tasks):
    for task in tasks:
        variant = task["attributes"].get("unittest_variant") or ""
        has_no_setpref = (
            "gtest",
            "cppunit",
            "jittest",
            "junit",
            "raptor",
            "reftest",
            "web-platform-tests",
        )

        if (
            all(s not in task["suite"] for s in has_no_setpref)
            and "socketprocess" not in variant
        ):
            task["mozharness"]["extra-options"].append(
                "--setpref=media.peerconnection.mtransport_process=false"
            )
            task["mozharness"]["extra-options"].append(
                "--setpref=network.process.enabled=false"
            )

        yield task


class PlatformOSSchema(Schema, kw_only=True):
    """Platform OS configuration schema."""

    name: Literal["android", "linux", "macosx", "windows"]
    version: str
    build: Optional[str] = None


class PlatformSchema(Schema, kw_only=True):
    """Platform configuration schema."""

    arch: Literal["32", "64", "aarch64", "arm7", "x86", "x86_64"]
    os: PlatformOSSchema
    device: Optional[str] = None
    display: Optional[Literal["wayland"]] = None
    machine: Optional[Literal["hw-ref"]] = None


class BuildSchema(Schema, kw_only=True, forbid_unknown_fields=False):
    """Build configuration schema."""

    type: Literal["opt", "debug", "debug-isolated-process"]
    asan: Optional[bool] = None
    ccov: Optional[bool] = None
    clang_trunk: Optional[bool] = None
    devedition: Optional[bool] = None
    lite: Optional[bool] = None
    mingwclang: Optional[bool] = None
    nightlyasrelease: Optional[bool] = None
    shippable: Optional[bool] = None
    tsan: Optional[bool] = None


class TestSettingDescriptionSchema(Schema, kw_only=True, forbid_unknown_fields=False):
    """Schema test settings must conform to. Validated by
    :py:func:`~test.test_mozilla_central.test_test_setting`"""

    _hash: str = msgspec.field(name="_hash")
    platform: PlatformSchema
    build: BuildSchema
    runtime: dict[str, bool]

    def __post_init__(self):
        valid_keys = set(TEST_VARIANTS.keys()) | {"1proc"}
        invalid = set(self.runtime.keys()) - valid_keys
        if invalid:
            raise ValueError(f"Invalid runtime keys: {invalid}")


@transforms.add
def set_test_setting(config, tasks):
    """A test ``setting`` is the set of configuration that uniquely
    distinguishes a test task from other tasks that run the same suite
    (ignoring chunks).

    There are three different types of information that make up a setting:

    1. Platform - Information describing the underlying platform tests run on,
    e.g, OS, CPU architecture, etc.

    2. Build - Information describing the build being tested, e.g build type,
    ccov, asan/tsan, etc.

    3. Runtime - Information describing which runtime parameters are enabled,
    e.g, prefs, environment variables, etc.

    This transform adds a ``test-setting`` object to the ``extra`` portion of
    all test tasks, of the form:

    .. code-block::

        {
            "platform": { ... },
            "build": { ... },
            "runtime": { ... }
        }

    This information could be derived from the label, but consuming this
    object is less brittle.
    """
    # Some attributes have a dash in them which complicates parsing. Ensure we
    # don't split them up.
    # TODO Rename these so they don't have a dash.
    dash_attrs = [
        "clang-trunk",
        "hw-ref",
    ]
    dash_token = "%D%"
    platform_re = re.compile(r"(\D+)(\d*)")

    for task in tasks:
        setting = {
            "platform": {
                "os": {},
            },
            "build": {},
            "runtime": {},
        }

        # parse platform and build information out of 'test-platform'
        platform, build_type = task["test-platform"].split("/", 1)

        # ensure dashed attributes don't get split up
        for attr in dash_attrs:
            if attr in platform:
                platform = platform.replace(attr, attr.replace("-", dash_token))

        parts = platform.split("-")

        # restore dashes now that split is finished
        for i, part in enumerate(parts):
            if dash_token in part:
                parts[i] = part.replace(dash_token, "-")

        match = platform_re.match(parts.pop(0))
        assert match
        os_name, os_version = match.groups()

        device = machine = os_build = display = None
        if os_name == "android":
            device = parts.pop(0)
            if device == "hw":
                device = parts.pop(0)
            else:
                device = "emulator"

            os_version = parts.pop(0)
            if parts[0].isdigit():
                os_version = f"{os_version}.{parts.pop(0)}"

            if parts[0] == "android":
                parts.pop(0)

            arch = parts.pop(0)

        else:
            arch = parts.pop(0)
            if parts and (parts[0].isdigit() or parts[0] in ["24h2", "25h2"]):
                os_build = parts.pop(0)

            if parts and parts[0] == "hw-ref":
                machine = parts.pop(0)

            if parts and parts[0] == "wayland":
                display = parts.pop(0)

            if parts and parts[0] == "aarch64":
                arch = parts.pop(0)

        # It's not always possible to glean the exact architecture used from
        # the task, so sometimes this will just be set to "32" or "64".
        setting["platform"]["arch"] = arch
        setting["platform"]["os"] = {
            "name": os_name,
            "version": os_version,
        }

        if os_build:
            setting["platform"]["os"]["build"] = os_build

        if device:
            setting["platform"]["device"] = device

        if machine:
            setting["platform"]["machine"] = machine

        if display:
            setting["platform"]["display"] = display

        # parse remaining parts as build attributes
        setting["build"]["type"] = build_type
        while parts:
            attr = parts.pop(0)
            if attr == "qr":
                # all tasks are webrender now, no need to store it
                continue

            setting["build"][attr] = True

        unittest_variant = task["attributes"].get("unittest_variant")
        if unittest_variant:
            for variant in unittest_variant.split("+"):
                setting["runtime"][variant] = True

        # add a hash of the setting object for easy comparisons
        setting["_hash"] = hashlib.sha256(
            json.dumps(setting, sort_keys=True).encode("utf-8")
        ).hexdigest()[:12]

        task["test-setting"] = ReadOnlyDict(**setting)
        yield task


@transforms.add
def allow_software_gl_layers(config, tasks):
    """
    Handle the "allow-software-gl-layers" property for platforms where it
    applies.
    """
    for task in tasks:
        if task.get("allow-software-gl-layers"):
            # This should be set always once bug 1296086 is resolved.
            task["mozharness"].setdefault("extra-options", []).append(
                "--allow-software-gl-layers"
            )

        yield task


@transforms.add
def enable_webrender(config, tasks):
    """
    Handle the "webrender" property by passing a flag to mozharness if it is
    enabled.
    """
    for task in tasks:
        # TODO: this was all conditionally in enable_webrender- do we still need this?
        extra_options = task["mozharness"].setdefault("extra-options", [])
        # We only want to 'setpref' on tests that have a profile
        if not task["attributes"]["unittest_category"] in [
            "cppunittest",
            "geckoview-junit",
            "gtest",
            "jittest",
            "raptor",
        ]:
            extra_options.append("--setpref=layers.d3d11.enable-blacklist=false")

        yield task


@transforms.add
def set_schedules_for_webrender_android(config, tasks):
    """android-hw has limited resources, we need webrender on phones"""
    for task in tasks:
        if task["suite"] in ["crashtest", "reftest"] and task[
            "test-platform"
        ].startswith("android-hw"):
            task["schedules-component"] = "android-hw-gfx"
        yield task


@transforms.add
def set_retry_exit_status(config, tasks):
    """Set the retry exit status to TBPL_RETRY, the value returned by mozharness
    scripts to indicate a transient failure that should be retried."""
    for task in tasks:
        task["retry-exit-status"] = [4]
        yield task


@transforms.add
def set_profile(config, tasks):
    """Set profiling mode for tests."""
    ttconfig = config.params["try_task_config"]
    profile = ttconfig.get("gecko-profile", False)
    settings = (
        "gecko-profile-interval",
        "gecko-profile-entries",
        "gecko-profile-threads",
        "gecko-profile-features",
    )

    for task in tasks:
        if profile and task["suite"] in ["talos", "raptor"]:
            extras = task["mozharness"]["extra-options"]
            extras.append("--gecko-profile")
            for setting in settings:
                value = ttconfig.get(setting)
                if value is not None:
                    # These values can contain spaces (eg the "DOM Worker"
                    # thread) and the command is constructed in different,
                    # incompatible ways on different platforms.

                    if task["test-platform"].startswith("win"):
                        # Double quotes for Windows (single won't work).
                        extras.append("--" + setting + '="' + str(value) + '"')
                    else:
                        # Other platforms keep things as separate values,
                        # rather than joining with spaces.
                        extras.append("--" + setting + "=" + str(value))

        yield task


@transforms.add
def add_gecko_profile_symbolication_deps(config, tasks):
    """Add symbolication dependencies when profiling raptor, talos, or mochitest tests"""

    try_task_config = config.params.get("try_task_config", {})
    gecko_profile_from_try = try_task_config.get("gecko-profile", False)
    startup_profile = try_task_config.get("env", {}).get("MOZ_PROFILER_STARTUP") == "1"

    for task in tasks:
        extra_options = task.get("mozharness", {}).get("extra-options", [])
        has_gecko_profile_option = any(
            "--gecko-profile" in option for option in extra_options
        )
        gecko_profile = gecko_profile_from_try or has_gecko_profile_option

        if (gecko_profile and task["suite"] in ["talos", "raptor"]) or (
            startup_profile and "mochitest" in task["suite"]
        ):
            fetches = task.setdefault("fetches", {})
            fetch_toolchains = fetches.setdefault("toolchain", [])

            if "profiler-node-tools" not in fetch_toolchains:
                fetch_toolchains.append("profiler-node-tools")

            test_platform = task["test-platform"]

            if "macosx" in test_platform and "aarch64" in test_platform:
                samply_toolchain = "macosx64-aarch64-samply"
            elif "macosx" in test_platform:
                samply_toolchain = "macosx64-samply"
            elif "win" in test_platform:
                samply_toolchain = "win64-samply"
            else:
                samply_toolchain = "linux64-samply"

            if samply_toolchain not in fetch_toolchains:
                fetch_toolchains.append(samply_toolchain)

            # Add node as a dependency for talos and mochitest tasks if needed.
            # node is used to run profiler-edit, our profile symbolication tool
            if task["suite"] == "talos" or "mochitest" in task["suite"]:
                if "macosx" in test_platform and "aarch64" in test_platform:
                    node_toolchain = "macosx64-aarch64-node"
                elif "macosx" in test_platform:
                    node_toolchain = "macosx64-node"
                elif "win" in test_platform:
                    node_toolchain = "win64-node"
                else:
                    node_toolchain = "linux64-node"

                if node_toolchain not in fetch_toolchains:
                    fetch_toolchains.append(node_toolchain)

        yield task


@transforms.add
def set_tag(config, tasks):
    """Set test for a specific tag."""
    tag = None
    for task in tasks:
        if tag:
            task["mozharness"]["extra-options"].extend(["--tag", tag])
        yield task


@transforms.add
def set_test_type(config, tasks):
    types = ["mochitest", "reftest", "talos", "raptor", "geckoview-junit", "gtest"]
    for task in tasks:
        for test_type in types:
            if test_type in task["suite"] and "web-platform" not in task["suite"]:
                task.setdefault("tags", {})["test-type"] = test_type
        yield task


@transforms.add
def set_schedules_components(config, tasks):
    for task in tasks:
        if "optimization" in task or "when" in task:
            yield task
            continue

        category = task["attributes"]["unittest_category"]
        schedules = task.get("schedules-component", category)
        if isinstance(schedules, str):
            schedules = [schedules]

        schedules = set(schedules)
        if schedules & set(INCLUSIVE_COMPONENTS):
            # if this is an "inclusive" test, then all files which might
            # cause it to run are annotated with SCHEDULES in moz.build,
            # so do not include the platform or any other components here
            task["schedules-component"] = sorted(schedules)
            yield task
            continue

        schedules.add(category)
        schedules.add(platform_family(task["build-platform"]))
        schedules.add("firefox")

        task["schedules-component"] = sorted(schedules)
        yield task


@transforms.add
def enable_parallel_marking_in_tsan_tests(config, tasks):
    """Enable parallel marking in TSAN tests"""
    skip_list = ["cppunittest", "gtest"]
    for task in tasks:
        if "-tsan-" in task["test-platform"]:
            if task["suite"] not in skip_list:
                extra_options = task["mozharness"].setdefault("extra-options", [])
                extra_options.append(
                    "--setpref=javascript.options.mem.gc_parallel_marking=true"
                )

        yield task


@transforms.add
def set_webgpu_ignore_blocklist(config, tasks):
    """
    Ignore the WebGPU blocklist on Linux because CI's Mesa is old

    See <https://bugzilla.mozilla.org/show_bug.cgi?id=1985348>
    """
    for task in tasks:
        if "web-platform-tests-webgpu" in task["test-name"] and task[
            "test-platform"
        ].startswith("linux"):
            extra_options = task["mozharness"].setdefault("extra-options", [])
            extra_options.append("--setpref=gfx.webgpu.ignore-blocklist=true")

        yield task
