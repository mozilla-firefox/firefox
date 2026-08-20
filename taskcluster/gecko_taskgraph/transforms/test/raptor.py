# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


from typing import Optional

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.copy import deepcopy
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from taskgraph.util.treeherder import join_symbol, split_symbol

from gecko_taskgraph.transforms.test import TestDescriptionSchema
from gecko_taskgraph.util.perftest import is_external_browser

transforms = TransformSequence()
task_transforms = TransformSequence()

SP3_CRITICAL_TESTS = [
    "test-windows11-64-24h2-shippable/opt-browsertime-benchmark-firefox-speedometer3",
    "test-linux2404-64-shippable/opt-browsertime-benchmark-firefox-speedometer3",
    "test-macosx1500-aarch64-shippable/opt-browsertime-benchmark-firefox-speedometer3",
    "test-android-hw-a55-14-0-aarch64-shippable/opt-browsertime-benchmark-speedometer3-mobile-fenix",
]

# Tests that run on the Speedometer 3 harness, and therefore share its native
# profiling setup, its power measurements and its longer run times.
SPEEDOMETER_3_HARNESS_TEST_NAMES = ("speedometer3", "speedometer-experimental")


def uses_speedometer_3_harness(test):
    return any(
        name in test.get("test-name", "") for name in SPEEDOMETER_3_HARNESS_TEST_NAMES
    )


class RaptorSchema(Schema, kw_only=True):
    activity: Optional[optionally_keyed_by("app", str, use_msgspec=True)] = None  # type: ignore
    apps: Optional[  # type: ignore
        optionally_keyed_by("test-platform", "subtest", list[str], use_msgspec=True)
    ] = None
    binary_path: Optional[optionally_keyed_by("app", str, use_msgspec=True)] = None  # type: ignore
    run_visual_metrics: Optional[  # type: ignore
        optionally_keyed_by("app", "test-platform", bool, use_msgspec=True)
    ] = None
    subtests: Optional[  # type: ignore
        optionally_keyed_by(
            "app", "test-platform", "variant", list[object], use_msgspec=True
        )
    ] = None
    test: Optional[str] = None
    test_url_param: Optional[  # type: ignore
        optionally_keyed_by("subtest", "test-platform", str, use_msgspec=True)
    ] = None
    lull_schedule: Optional[  # type: ignore
        optionally_keyed_by("subtest", "test-platform", str, use_msgspec=True)
    ] = None
    network_conditions: Optional[  # type: ignore
        optionally_keyed_by("subtest", list[object], use_msgspec=True)
    ] = None


class RaptorDescriptionSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    # Raptor specific configs.
    raptor: Optional[RaptorSchema] = None
    # Configs defined in the 'test_description_schema'.
    max_run_time: Optional[  # type: ignore
        optionally_keyed_by(
            "app",
            "subtest",
            "test-platform",
            TestDescriptionSchema.__annotations__["max_run_time"],
            use_msgspec=True,
        )
    ] = None
    run_on_projects: Optional[  # type: ignore
        optionally_keyed_by(
            "app",
            "test-name",
            "raptor.test",
            "subtest",
            "variant",
            TestDescriptionSchema.__annotations__["run_on_projects"],
            use_msgspec=True,
        )
    ] = None
    variants: TestDescriptionSchema.__annotations__["variants"] = None
    target: Optional[  # type: ignore
        optionally_keyed_by(
            "app", TestDescriptionSchema.__annotations__["target"], use_msgspec=True
        )
    ] = None
    tier: Optional[  # type: ignore
        optionally_keyed_by(
            "app",
            "raptor.test",
            "subtest",
            "variant",
            TestDescriptionSchema.__annotations__["tier"],
            use_msgspec=True,
        )
    ] = None
    test_name: TestDescriptionSchema.__annotations__["test_name"]  # noqa: F821
    test_platform: TestDescriptionSchema.__annotations__["test_platform"]  # noqa: F821
    require_signed_extensions: TestDescriptionSchema.__annotations__[
        "require_signed_extensions"  # noqa: F821
    ]
    treeherder_symbol: TestDescriptionSchema.__annotations__["treeherder_symbol"]  # noqa: F821


transforms.add_validate(RaptorDescriptionSchema)


@transforms.add
def set_defaults(config, tests):
    for test in tests:
        test.setdefault("raptor", {}).setdefault("run-visual-metrics", False)
        yield test


@transforms.add
def split_apps(config, tests):
    app_symbols = {
        "chrome": "ChR",
        "chrome-m": "ChR",
        "fenix": "fenix",
        "refbrow": "refbrow",
        "safari": "Saf",
        "safari-tp": "STP",
        "custom-car": "CaR",
        "cstm-car-m": "CaR",
    }

    for test in tests:
        apps = test["raptor"].pop("apps", None)
        if not apps:
            yield test
            continue

        for app in apps:
            # Ignore variants for non-Firefox or non-mobile applications.
            if app not in [
                "firefox",
                "geckoview",
                "fenix",
                "chrome-m",
                "cstm-car-m",
            ] and test["attributes"].get("unittest_variant"):
                continue

            atest = deepcopy(test)
            suffix = f"-{app}"
            atest["app"] = app
            atest["description"] += f" on {app.capitalize()}"

            name = atest["test-name"] + suffix
            atest["test-name"] = name
            atest["try-name"] = name

            if app in app_symbols:
                group, symbol = split_symbol(atest["treeherder-symbol"])
                group += f"-{app_symbols[app]}"
                atest["treeherder-symbol"] = join_symbol(group, symbol)

            yield atest


@transforms.add
def handle_keyed_by_prereqs(config, tests):
    """
    Only resolve keys for prerequisite fields here since the
    these keyed-by options might have keyed-by fields
    as well.
    """
    for test in tests:
        resolve_keyed_by(
            test,
            "raptor.subtests",
            item_name=test["test-name"],
            variant=test["attributes"].get("unittest_variant"),
        )
        yield test


@transforms.add
def split_raptor_subtests(config, tests):
    for test in tests:
        # For tests that have 'subtests' listed, we want to create a separate
        # test job for every subtest (i.e. split out each page-load URL into its own job)
        subtests = test["raptor"].pop("subtests", None)
        if not subtests:
            if all(
                p not in test["test-platform"] for p in ("macosx1400", "macosx1500")
            ):
                yield test
            continue

        for chunk_number, subtest in enumerate(subtests):
            # Create new test job
            chunked = deepcopy(test)
            chunked["chunk-number"] = 1 + chunk_number
            chunked["subtest"] = subtest
            chunked["subtest-symbol"] = subtest
            if isinstance(chunked["subtest"], list):
                chunked["subtest"] = subtest[0]
                chunked["subtest-symbol"] = subtest[1]
            chunked = resolve_keyed_by(
                chunked, "tier", chunked["subtest"], defer=["variant"]
            )
            yield chunked


@transforms.add
def handle_keyed_by(config, tests):
    fields = [
        "raptor.test-url-param",
        "raptor.run-visual-metrics",
        "raptor.activity",
        "raptor.binary-path",
        "raptor.lull-schedule",
        "raptor.network-conditions",
        "limit-platforms",
        "fetches.fetch",
        "fetches.toolchain",
        "max-run-time",
        "run-on-projects",
        "target",
        "tier",
        "mozharness.extra-options",
    ]
    for test in tests:
        for field in fields:
            resolve_keyed_by(
                test, field, item_name=test["test-name"], defer=["variant"]
            )
        yield test


@transforms.add
def handle_network_conditions(config, tests):
    for test in tests:
        conditions = test["raptor"].pop("network-conditions", None)
        if not conditions:
            yield test
            continue

        for condition in conditions:
            new_test = deepcopy(test)
            network_type, packet_loss_rate = condition

            new_test.pop("chunk-number")
            subtest = new_test.pop("subtest")
            new_test["raptor"]["test"] = subtest

            group, _ = split_symbol(new_test["treeherder-symbol"])
            new_group = f"{group}-{network_type}"
            subtest_symbol = f"{new_test['subtest-symbol']}-{packet_loss_rate}"
            new_test["treeherder-symbol"] = join_symbol(new_group, subtest_symbol)

            mozharness = new_test.setdefault("mozharness", {})
            extra_options = mozharness.setdefault("extra-options", [])

            extra_options.extend([
                f"--browsertime-arg=network_type={network_type}",
                f"--browsertime-arg=pkt_loss_rate={packet_loss_rate}",
            ])

            new_test["test-name"] += f"-{subtest}-{network_type}-{packet_loss_rate}"
            new_test["try-name"] += f"-{subtest}-{network_type}-{packet_loss_rate}"
            new_test["description"] += (
                f" on {subtest} with {network_type} network type and "
                f" {packet_loss_rate} loss rate"
            )

            yield new_test

        yield test


@transforms.add
def split_page_load_by_url(config, tests):
    for test in tests:
        # `chunk-number` and 'subtest' only exists when the task had a
        # definition for `subtests`
        chunk_number = test.pop("chunk-number", None)
        subtest = test.get(
            "subtest"
        )  # don't pop as some tasks need this value after splitting variants
        subtest_symbol = test.pop("subtest-symbol", None)

        if not chunk_number or not subtest:
            yield test
            continue

        if len(subtest_symbol) > 10 and "ytp" not in subtest_symbol:
            raise Exception(
                f"Treeherder symbol {subtest_symbol} is larger than 10 char! "
                "Please use a different symbol."
            )

        if test["test-name"].startswith("browsertime-"):
            test["raptor"]["test"] = subtest

            # Remove youtube-playback in the test name to avoid duplication
            test["test-name"] = test["test-name"].replace("youtube-playback-", "")
        else:
            # Use full test name if running on webextension
            test["raptor"]["test"] = "raptor-tp6-" + subtest + "-{}".format(test["app"])

        # Only run the subtest/single URL
        test["test-name"] += f"-{subtest}"
        test["try-name"] += f"-{subtest}"

        # Set treeherder symbol and description
        group, _ = split_symbol(test["treeherder-symbol"])
        test["treeherder-symbol"] = join_symbol(group, subtest_symbol)
        test["description"] += f" on {subtest}"

        yield test


@transforms.add
def modify_extra_options(config, tests):
    for test in tests:
        test_name = test.get("test-name", None)

        if "first-install" in test_name:
            # First-install tests should never use conditioned profiles
            extra_options = test.setdefault("mozharness", {}).setdefault(
                "extra-options", []
            )

            for i, opt in enumerate(extra_options):
                if "conditioned-profile" in opt:
                    if i:
                        extra_options.pop(i)
                    break

        if "-widevine" in test_name:
            extra_options = test.setdefault("mozharness", {}).setdefault(
                "extra-options", []
            )
            for i, opt in enumerate(extra_options):
                if "--conditioned-profile=settled" in opt:
                    if i:
                        extra_options[i] += "-youtube"
                    break

        if "unity-webgl" in test_name:
            # Disable the extra-profiler-run for unity-webgl tests.
            extra_options = test.setdefault("mozharness", {}).setdefault(
                "extra-options", []
            )
            for i, opt in enumerate(extra_options):
                if "extra-profiler-run" in opt:
                    if i:
                        extra_options.pop(i)
                    break

        if "jetstream" in test_name and test.get("app", "") in ("chrome", "custom-car"):
            # Bug 1996836 - Disable jetstream 2/3 extra profile runs
            extra_options = test.setdefault("mozharness", {}).setdefault(
                "extra-options", []
            )
            for i, opt in enumerate(extra_options):
                if "extra-profiler-run" in opt:
                    extra_options.pop(i)
                    break

        yield test


@transforms.add
def add_extra_options(config, tests):
    for test in tests:
        mozharness = test.setdefault("mozharness", {})
        if test.get("app", "") == "chrome-m":
            mozharness["tooltool-downloads"] = "internal"

        extra_options = mozharness.setdefault("extra-options", [])

        # Adding device name if we're on android
        test_platform = test["test-platform"]
        if test_platform.startswith("android-hw-a55"):
            extra_options.append("--device-name=a55")
        elif test_platform.startswith("android-hw-p6"):
            extra_options.append("--device-name=p6_aarch64")
        elif test_platform.startswith("android-hw-s24"):
            extra_options.append("--device-name=s24_aarch64")

        if test["raptor"].pop("run-visual-metrics", False):
            extra_options.append("--browsertime-video")
            extra_options.append("--browsertime-visualmetrics")
            test["attributes"]["run-visual-metrics"] = True

        if "app" in test:
            extra_options.append(
                "--app={}".format(test["app"])
            )  # don't pop as some tasks need this value after splitting variants

        if "activity" in test["raptor"]:
            extra_options.append("--activity={}".format(test["raptor"].pop("activity")))

        if "binary-path" in test["raptor"]:
            extra_options.append(
                "--binary-path={}".format(test["raptor"].pop("binary-path"))
            )

        if "test" in test["raptor"]:
            extra_options.append("--test={}".format(test["raptor"].pop("test")))

        if test["require-signed-extensions"]:
            extra_options.append("--is-release-build")

        if "test-url-param" in test["raptor"]:
            param = test["raptor"].pop("test-url-param")
            if not param == []:
                extra_options.append(
                    "--test-url-params={}".format(param.replace(" ", ""))
                )

        if (
            ("android-hw-p6" in test_platform or "android-hw-s24" in test_platform)
            and "speedometer2-" not in test["test-name"]
            # Bug 1943674 resolve why --power-test causes permafails on certain mobile platforms and browsers
        ):
            # Bug 2037511 Temporarily disable power-test option for tp6m on a55s
            if "--power-test" not in extra_options:
                extra_options.append("--power-test")
        elif "windows" in test_platform and (
            uses_speedometer_3_harness(test) or "tp6" in test["test-name"]
        ):
            extra_options.append("--power-test")

        extra_options.append("--project={}".format(config.params.get("project")))

        yield test


@transforms.add
def modify_mozharness_configs(config, tests):
    for test in tests:
        if not is_external_browser(test["app"]):
            yield test
            continue

        test_platform = test["test-platform"]
        mozharness = test.setdefault("mozharness", {})
        if "mac" in test_platform:
            mozharness["config"] = ["raptor/mac_external_browser_config.py"]
        elif "windows" in test_platform:
            mozharness["config"] = ["raptor/windows_external_browser_config.py"]
        elif "linux" in test_platform:
            mozharness["config"] = ["raptor/linux_external_browser_config.py"]
        elif "android" in test_platform:
            test["target"] = "target.tar.xz"
            mozharness["config"] = ["raptor/android_hw_external_browser_config.py"]

        yield test


@transforms.add
def handle_lull_schedule(config, tests):
    # Setup lull schedule attribute here since the attributes
    # can't have any keyed by settings
    for test in tests:
        if "lull-schedule" in test["raptor"]:
            lull_schedule = test["raptor"].pop("lull-schedule")
            if lull_schedule:
                test.setdefault("attributes", {})["lull-schedule"] = lull_schedule
        yield test


@transforms.add
def apply_raptor_device_optimization(config, tests):
    # Bug 1919389
    # For now, only change the back stop optimization strategy for A55 devices
    for test in tests:
        if test["test-platform"].startswith("android-hw-a55"):
            test["optimization"] = {"skip-unless-backstop": None}
        yield test


@task_transforms.add
def add_scopes_and_proxy(config, tasks):
    for task in tasks:
        task.setdefault("worker", {})["taskcluster-proxy"] = True
        task.setdefault("scopes", []).append(
            "secrets:get:project/perftest/gecko/level-{level}/perftest-login"
        )
        yield task


@task_transforms.add
def setup_lull_schedule(config, tasks):
    for task in tasks:
        attrs = task.setdefault("attributes", {})
        if attrs.get("lull-schedule", None) is not None:
            # Move the lull schedule attribute into the extras
            # so that it can be accessible through mozci
            lull_schedule = attrs.pop("lull-schedule")
            task.setdefault("extra", {})["lull-schedule"] = lull_schedule
        yield task


@task_transforms.add
def setup_autoland_retriggers(config, tasks):

    def _allow_task_duplicates(label):
        if "android" in label:
            return False
        if any(sp3_test in label for sp3_test in SP3_CRITICAL_TESTS):
            return True
        return False

    for task in tasks:
        attrs = task.setdefault("attributes", {})
        if config.params["project"] == "autoland" and _allow_task_duplicates(
            task["label"]
        ):
            attrs["task_duplicates"] = 12
        yield task


@task_transforms.add
def setup_internal_artifacts(config, tasks):
    for task in tasks:
        if (
            task["worker"]["os"] == "linux-bitbar"
            or task["worker"]["os"] == "linux-lambda"
        ):
            task["worker"].setdefault("artifacts", []).append({
                "name": "perftest",
                "path": "workspace/build/perftest",
                "type": "directory",
            })
        else:
            task["worker"].setdefault("artifacts", []).append({
                "name": "perftest",
                "path": "build/perftest",
                "type": "directory",
            })
        yield task


@task_transforms.add
def select_tasks_to_lambda(config, tasks):
    """
    all motionmark tests
    speedometer3 test
    unity-webgl test
    all youtube-playback tests (including power)
    all vpl (video-playback-latency) tests
    all pageload tests (ideally fenix/CaR/ChR)
    jetstream2/jetstream3 benchmarks
    background/foreground resource tests (browsertime-power idle/idle-bg)
    trr-* performance tests

    """
    tests_to_run_at_lambdatest = [
        "motionmark-1-3",
        "motionmark-htmlsuite-1-3",
        "speedometer3",
        "unity-webgl",
        "video-playback-latency",
        "youtube-playback-av1-sfr",
        "youtube-playback-hfr",
        "youtube-playback-vp9-sfr",
        "youtube-playback-h264-sfr",
        "youtube-playback-h264-720p60",
        "youtube-playback-vp9-720p60",
        "tp6m",
        "jetstream2",
        "jetstream3",
        "browsertime-power",
        "browsertime-trr-performance",
    ]

    # Bug 2017151 - newly-migrated tests run at tier 2 while stabilizing on LT
    tests_to_force_tier2 = [
        "youtube-playback-h264-sfr",
        "youtube-playback-h264-720p60",
        "youtube-playback-vp9-720p60",
        "jetstream2",
        "jetstream3",
        "browsertime-power",
        "browsertime-trr-performance",
    ]

    def redirect_to_lt(task):
        task["tags"]["os"] = "linux-lambda"
        task["worker"]["os"] = "linux-lambda"
        task["worker-type"] = "t-lambda-perf-a55"
        task["worker"]["env"]["TASKCLUSTER_WORKER_TYPE"] = "t-lambda-perf-a55"
        cmds = []
        for cmd in task["worker"]["command"]:
            # Bug 1981862 - issues with condprof setup @ lambdatest
            cmds.append([
                c.replace(
                    "/builds/taskcluster/script.py",
                    "/home/ltuser/taskcluster/script.py",
                )
                for c in cmd
                if not c.startswith("--conditioned-profile")
            ])
        task["worker"]["command"] = cmds
        task["worker"]["env"]["DISABLE_USB_POWER_METER_RESET"] = "1"
        # Bug 2017151 - newly-migrated tests run at tier 2 while stabilizing on LT
        if any(t in task["label"] for t in tests_to_force_tier2):
            th = task.setdefault("treeherder", {})
            th["tier"] = max(th.get("tier", 1), 2)
        return task

    for task in tasks:
        if not ("android" in task["label"] and "a55" in task["label"]):
            yield task
            continue
        if not any(t in task["label"] for t in tests_to_run_at_lambdatest):
            yield task
            continue
        if task["worker-type"] != "t-bitbar-gw-perf-a55":
            yield task
            continue
        yield redirect_to_lt(task)


@transforms.add
def add_simpleperf(config, tests):
    app_packages = {
        "fenix": "org.mozilla.fenix",
        "geckoview": "org.mozilla.geckoview_example",
    }

    def _setup_simpleperf_profiling(test):
        extra_options = test.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )
        extra_options.extend([
            "--simpleperf",
            "--browsertime-arg=androidSimpleperf=$MOZ_FETCHES_DIR/android-simpleperf",
        ])
        app_data_dir = (
            f"/storage/emulated/0/Android/data/{app_packages[test.get('app')]}/files"
        )
        extra_options.extend([
            "--setenv MOZ_USE_PERFORMANCE_MARKER_FILE=1",
            f"--setenv MOZ_PERFORMANCE_MARKER_DIR={app_data_dir}",
            f"--setenv PERF_SPEW_DIR={app_data_dir}",
            "--setenv IONPERF=func",
            "--setenv JIT_OPTION_onlyInlineSelfHosted=true",
        ])

        fetches = test.setdefault("fetches", {})
        fetches.setdefault("build", []).append({
            "artifact": "target.crashreporter-symbols.zip",
            "extract": False,
        })
        toolchains = [
            "linux64-android-simpleperf-linux-repack",
            "linux64-samply",
        ]
        toolchain_fetches = fetches.setdefault("toolchain", [])
        for toolchain in toolchains:
            if toolchain not in toolchain_fetches:
                toolchain_fetches.append(toolchain)

    for test in tests:
        app = test.get("app")
        if (
            app in app_packages
            and "speedometer3-mobile" in test.get("test-name", "")
            and (
                "no-fission"
                not in (test.get("attributes", {}).get("unittest_variant") or "")
            )
        ):
            np_test = deepcopy(test)
            np_test["test-name"] += "-native-profiling"
            np_test["try-name"] += "-native-profiling"
            _setup_simpleperf_profiling(np_test)

            # On autoland, run a copy of the Speedometer 3 a55 Fenix task
            # with native (Simpleperf) profiling

            is_autoland_job = (
                app == "fenix"
                and "a55" in test.get("test-platform", "")
                and test["attributes"].get("shippable", False)
            )

            if is_autoland_job:
                np_test["run-on-projects"] = ["autoland-only"]
            else:
                np_test["run-on-projects"] = []

            yield np_test

        yield test


@transforms.add
def add_etw_profile(config, tests):

    def _setup_etw_profiling(test):

        extra_options = test.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )

        extra_options.extend([
            "--etw-profile",
            "--setenv ETW_ENABLED=1",
            "--setenv JIT_OPTION_enableICFramePointers=true",
            "--setenv JIT_OPTION_onlyInlineSelfHosted=true",
            "--setenv JIT_OPTION_emitInterpreterEntryTrampoline=true",
        ])

        if test.get("app") in ["chrome", "custom-car"]:
            extra_options.extend([
                "--browsertime-arg=chrome.args=--enable-features=EnableEtwExports",
                "--browsertime-arg=chrome.args=--enable-benchmarking",
                "--browsertime-arg=chrome.args=--js-flags=--perf-prof",
                "--browsertime-arg=chrome.args=--js-flags=--enable-etw-stack-walking",
                "--browsertime-arg=chrome.args=--js-flags=--interpreted-frames-native-stack",
                "--browsertime-arg=chrome.args=--js-flags=--no-turbo-inlining",
                "--browsertime-arg=chrome.args=--js-flags=--no-compact-code-space",
            ])

        if uses_speedometer_3_harness(test):
            test["max-run-time"] = 4200  # seconds
            if "--extra-profiler-run" in extra_options:
                extra_options.remove("--extra-profiler-run")

        fetches = test.setdefault("fetches", {})

        toolchain_fetches = fetches.setdefault("toolchain", [])
        for toolchain in ("win64-samply", "profiler-node-tools"):
            if toolchain not in toolchain_fetches:
                toolchain_fetches.append(toolchain)

        if not is_external_browser(test["app"]):
            fetches.setdefault("build", []).append({
                "artifact": "target.crashreporter-symbols.zip",
                "extract": False,
            })

    for test in tests:
        if "win" in test.get("test-platform", "") and uses_speedometer_3_harness(test):
            np_test = deepcopy(test)
            np_test["test-name"] += "-native-profiling"
            np_test["try-name"] += "-native-profiling"
            _setup_etw_profiling(np_test)

            run_on_projects = test.get("run-on-projects", [])
            if "autoland" in run_on_projects or "trunk" in run_on_projects:
                # On Autoland, run duplicates of the following Windows tasks with native profiling:
                # - Sp3 on Firefox Windows 11 24H2 Shippable (trunk)
                # - Sp3 on Firefox Windows 11 24H2 Ref HW Shippable (trunk)
                # - Sp3 on Firefox Windows 11 24H2 NightlyAsRelease (autoland)
                np_test["run-on-projects"] = ["autoland-only"]
            else:
                np_test["run-on-projects"] = []

            yield np_test

        yield test


@transforms.add
def add_samply_profile(config, tests):
    def _setup_samply_profiling(test):
        extra_options = test.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )

        if uses_speedometer_3_harness(test):
            test["max-run-time"] = 4200  # seconds
            if "--extra-profiler-run" in extra_options:
                extra_options.remove("--extra-profiler-run")

        extra_options.extend([
            "--samply-profile",
        ])

        fetches = test.setdefault("fetches", {})
        toolchain = fetches.setdefault("toolchain", [])

        if "macos" in test.get("test-platform", ""):
            if "aarch64" in test.get("test-platform", ""):
                toolchains = [
                    "macosx64-aarch64-clang",
                    "macosx64-sdk-toolchain",
                    "macosx64-aarch64-samply",
                    "profiler-node-tools",
                ]
            else:
                toolchains = [
                    "macosx64-clang",
                    "macosx64-sdk-toolchain",
                    "macosx64-samply",
                    "profiler-node-tools",
                ]

            for tool in toolchains:
                if tool not in toolchain:
                    toolchain.append(tool)

        fetches.setdefault("build", []).append({
            "artifact": "target.crashreporter-symbols.zip",
            "extract": False,
        })

    for test in tests:
        if (
            uses_speedometer_3_harness(test)
            and "macos" in test.get("test-platform", "")
            and test.get("app") in ["firefox"]
        ):
            np_test = deepcopy(test)
            np_test["test-name"] += "-native-profiling"
            np_test["try-name"] += "-native-profiling"
            _setup_samply_profiling(np_test)

            run_on_projects = test.get("run-on-projects", [])
            if "autoland" in run_on_projects or "trunk" in run_on_projects:
                # On Autoland, run duplicates of the following macOS tasks with native profiling:
                # - Sp3 on Firefox macOS x86_64 Shippable base variant (trunk)
                # - Sp3 on Firefox macOS AArch64 Shippable  base variant (trunk)
                # - Sp3 on Firefox macOS x86_64 NightlyAsRelease base variant (autoland)
                np_test["run-on-projects"] = ["autoland-only"]
            else:
                np_test["run-on-projects"] = []

            yield np_test

        yield test


@transforms.add
def add_perf_profile(config, tests):
    def _setup_perf_profiling(test):
        extra_options = test.setdefault("mozharness", {}).setdefault(
            "extra-options", []
        )

        if uses_speedometer_3_harness(test):
            test["max-run-time"] = 4200  # seconds
            if "--extra-profiler-run" in extra_options:
                extra_options.remove("--extra-profiler-run")

        extra_options.extend([
            "--perf-profile",
        ])

        fetches = test.setdefault("fetches", {})
        toolchain = fetches.setdefault("toolchain", [])

        if "linux" in test.get("test-platform", ""):
            toolchains = ["linux64-samply", "profiler-node-tools"]
            for tool in toolchains:
                if tool not in toolchain:
                    toolchain.append(tool)

        fetches.setdefault("build", []).append({
            "artifact": "target.crashreporter-symbols.zip",
            "extract": False,
        })

    for test in tests:
        if (
            uses_speedometer_3_harness(test)
            and "linux" in test.get("test-platform", "")
            and test.get("app") == "firefox"
        ):
            np_test = deepcopy(test)
            np_test["test-name"] += "-native-profiling"
            np_test["try-name"] += "-native-profiling"
            _setup_perf_profiling(np_test)

            run_on_projects = test.get("run-on-projects", [])
            if "autoland" in run_on_projects or "trunk" in run_on_projects:
                # On Autoland, run duplicates of the following Linux tasks with native profiling:
                # - Sp3 on Firefox Linux Shippable base variant (trunk)
                # - Sp3 on Firefox Linux NightlyAsRelease base variant (autoland)
                np_test["run-on-projects"] = ["autoland-only"]
            else:
                np_test["run-on-projects"] = []

            yield np_test

        yield test


@transforms.add
def handle_native_profiling_symbol(config, tests):
    for test in tests:
        extra_options = test.get("mozharness", {}).get("extra-options", [])

        native_profiling_args = [
            "--simpleperf",
            "--etw-profile",
            "--samply-profile",
            "--perf-profile",
        ]

        if any(arg in extra_options for arg in native_profiling_args):
            group, symbol = split_symbol(test["treeherder-symbol"])
            test["treeherder-symbol"] = join_symbol(group, f"{symbol}-p")
        yield test


@task_transforms.add
def setup_native_profiling_autoland_retriggers(config, tasks):
    for task in tasks:
        if config.params["project"] == "autoland":
            attrs = task.setdefault("attributes", {})
            raptor_try_name = attrs.get("raptor_try_name", "")
            if "native-profiling" in raptor_try_name and "task_duplicates" in attrs:
                attrs["task_duplicates"] = 1
        yield task
