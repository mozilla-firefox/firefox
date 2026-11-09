# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
This transform passes options from `mach perftest` to the corresponding task.
"""

from datetime import date, timedelta
from typing import Optional, Union

from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.copy import deepcopy
from taskgraph.util.schema import Schema, optionally_keyed_by, resolve_keyed_by
from taskgraph.util.treeherder import join_symbol, split_symbol

transforms = TransformSequence()

# Target tasks methods that only select tasks meant to be profiled.
PROFILING_TARGET_TASKS_METHODS = {"perftest-applink-profiling"}


class PerftestDescriptionSchema(Schema, forbid_unknown_fields=False, kw_only=True):
    # The test names and the symbols to use for them: [test-symbol, test-path]
    perftest: Optional[list[list[str]]] = None
    # Metrics to gather for the test. These will be merged
    # with options specified through perftest-perfherder-global
    perftest_metrics: Optional[
        optionally_keyed_by(
            "perftest",
            Union[
                list[str],
                dict[str, Union[None, dict[str, Union[None, str, list[str]]]]],
            ],
            use_msgspec=True,
        )
    ] = None
    # Perfherder data options that will be applied to
    # all metrics gathered.
    perftest_perfherder_global: Optional[
        optionally_keyed_by(
            "perftest",
            dict[str, Union[None, str, list[str]]],
            use_msgspec=True,
        )
    ] = None
    # Extra options to add to the test's command
    perftest_extra_options: Optional[
        optionally_keyed_by("perftest", list[str], use_msgspec=True)
    ] = None
    # Variants of the test to make based on extra browsertime
    # arguments. Expecting:
    #    [variant-suffix, options-to-use]
    # If variant-suffix is `null` then the options will be added
    # to the existing task. Otherwise, a new variant is created
    # with the given suffix and with its options replaced.
    perftest_btime_variants: Optional[
        optionally_keyed_by("perftest", list[list[Optional[str]]], use_msgspec=True)
    ] = None


transforms.add_validate(PerftestDescriptionSchema)


@transforms.add
def split_tests(config, jobs):
    for job in jobs:
        if job.get("perftest") is None:
            yield job
            continue

        for test_symbol, test_name in job.pop("perftest"):
            job_new = deepcopy(job)

            job_new["perftest"] = test_symbol
            job_new["name"] += "-" + test_symbol
            job_new["treeherder"]["symbol"] = job["treeherder"]["symbol"].format(
                symbol=test_symbol
            )
            job_new["run"]["command"] = job["run"]["command"].replace(
                "{perftest_testname}", test_name
            )

            yield job_new


@transforms.add
def handle_keyed_by_perftest(config, jobs):
    fields = ["perftest-metrics", "perftest-extra-options", "perftest-btime-variants"]
    for job in jobs:
        if job.get("perftest") is None:
            yield job
            continue

        for field in fields:
            resolve_keyed_by(job, field, item_name=job["name"])

        job.pop("perftest")
        yield job


@transforms.add
def parse_perftest_metrics(config, jobs):
    """Parse the metrics into a dictionary immediately.

    This way we can modify the extraOptions field (and others) entry through the
    transforms that come later. The metrics aren't formatted until the end of the
    transforms.
    """
    for job in jobs:
        if job.get("perftest-metrics") is None:
            yield job
            continue
        perftest_metrics = job.pop("perftest-metrics")

        # If perftest metrics is a string, split it up first
        if isinstance(perftest_metrics, list):
            new_metrics_info = [{"name": metric} for metric in perftest_metrics]
        else:
            new_metrics_info = []
            for metric, options in perftest_metrics.items():
                entry = {"name": metric}
                entry.update(options)
                new_metrics_info.append(entry)

        job["perftest-metrics"] = new_metrics_info
        yield job


@transforms.add
def split_perftest_variants(config, jobs):
    for job in jobs:
        if job.get("variants") is None:
            yield job
            continue

        for variant in job.pop("variants"):
            job_new = deepcopy(job)

            group, symbol = split_symbol(job_new["treeherder"]["symbol"])
            group += "-" + variant
            job_new["treeherder"]["symbol"] = join_symbol(group, symbol)
            job_new["name"] += "-" + variant
            job_new.setdefault("perftest-perfherder-global", {}).setdefault(
                "extraOptions", []
            ).append(variant)
            job_new[variant] = True

            yield job_new

        yield job


@transforms.add
def split_btime_variants(config, jobs):
    for job in jobs:
        if job.get("perftest-btime-variants") is None:
            yield job
            continue

        variants = job.pop("perftest-btime-variants")
        if not variants:
            yield job
            continue

        yield_existing = False
        for suffix, options in variants:
            if suffix is None:
                # Append options to the existing job
                job.setdefault("perftest-btime-variants", []).append(options)
                yield_existing = True
            else:
                job_new = deepcopy(job)
                group, symbol = split_symbol(job_new["treeherder"]["symbol"])
                symbol += "-" + suffix
                job_new["treeherder"]["symbol"] = join_symbol(group, symbol)
                job_new["name"] += "-" + suffix
                job_new.setdefault("perftest-perfherder-global", {}).setdefault(
                    "extraOptions", []
                ).append(suffix)
                # Replace the existing options with the new ones
                job_new["perftest-btime-variants"] = [options]
                yield job_new

        # The existing job has been modified so we should also return it
        if yield_existing:
            yield job


@transforms.add
def setup_http3_tests(config, jobs):
    for job in jobs:
        if job.get("http3") is None or not job.pop("http3"):
            yield job
            continue
        job.setdefault("perftest-btime-variants", []).append(
            "firefox.preference=network.http.http3.enable:true"
        )
        yield job


@transforms.add
def setup_perftest_metrics(config, jobs):
    for job in jobs:
        if job.get("perftest-metrics") is None:
            yield job
            continue
        perftest_metrics = job.pop("perftest-metrics")

        # Options to apply to each metric
        global_options = job.pop("perftest-perfherder-global", {})
        for metric_info in perftest_metrics:
            for opt, val in global_options.items():
                if isinstance(val, list) and opt in metric_info:
                    metric_info[opt].extend(val)
                elif not (isinstance(val, list) and len(val) == 0):
                    metric_info[opt] = val

        quote_escape = '\\"'
        if "win" in job.get("platform", ""):
            # Escaping is a bit different on windows platforms
            quote_escape = '\\\\\\"'

        job["run"]["command"] = job["run"]["command"].replace(
            "{perftest_metrics}",
            " ".join([
                ",".join([
                    ":".join([
                        option,
                        str(value).replace(" ", "").replace("'", quote_escape),
                    ])
                    for option, value in metric_info.items()
                ])
                for metric_info in perftest_metrics
            ]),
        )

        yield job


@transforms.add
def setup_perftest_browsertime_variants(config, jobs):
    for job in jobs:
        if job.get("perftest-btime-variants") is None:
            yield job
            continue

        job["run"]["command"] += " --browsertime-extra-options %s" % ",".join([
            opt.strip() for opt in job.pop("perftest-btime-variants")
        ])

        yield job


@transforms.add
def setup_perftest_extra_options(config, jobs):
    for job in jobs:
        if job.get("perftest-extra-options") is None:
            yield job
            continue
        job["run"]["command"] += " " + " ".join(job.pop("perftest-extra-options"))
        yield job


@transforms.add
def pass_perftest_options(config, jobs):
    for job in jobs:
        env = job.setdefault("worker", {}).setdefault("env", {})
        env["PERFTEST_OPTIONS"] = json.dumps(
            config.params["try_task_config"].get("perftest-options")
        )
        yield job


@transforms.add
def setup_gecko_profile_from_try_config(config, jobs):
    """Apply gecko-profile settings when --gecko-profile is used with ./mach try fuzzy,
    or when a cron selects tasks through a profiling target tasks method.

    This mimics the logic from the gecko_profile action but applies it during
    task generation instead of as a post-hoc action.
    """
    gecko_profile = (
        config.params.get("try_task_config", {}).get("gecko-profile", False)
        or config.params.get("target_tasks_method") in PROFILING_TARGET_TASKS_METHODS
    )
    resource_tests = ["-background-resource-", "-foreground-resource-"]
    simpleperf_compatible_tests = [
        "-homeview-",
        "-applink-",
        "-restore-",
        *resource_tests,
    ]

    for job in jobs:
        # For simpleperf-compatible tests, add simpleperf support
        if (
            any(test in job["name"] for test in simpleperf_compatible_tests)
            and gecko_profile
        ):
            # Append simpleperf flags directly to the command
            # This avoids conflicts with try_task_config_env overwriting PERF_FLAGS
            simpleperf_args = [
                "--simpleperf",
                "--simpleperf-path",
                "$MOZ_FETCHES_DIR/android-simpleperf",
            ]
            if not any(test in job["name"] for test in resource_tests):
                simpleperf_args.append("--geckoprofiler")
            job["run"]["command"] += " " + " ".join(simpleperf_args)

            # Add required toolchain dependencies
            fetches = job.setdefault("fetches", {})
            fetch_toolchains = fetches.setdefault("toolchain", [])

            simpleperf_deps = [
                "linux64-android-simpleperf-linux-repack",
                "linux64-samply",
                "profiler-node-tools",
            ]
            for dep in simpleperf_deps:
                if dep not in fetch_toolchains:
                    fetch_toolchains.append(dep)

            # Add build dependency for symbols
            dependencies = job.setdefault("dependencies", {})
            if "android-aarch64-shippable" not in dependencies:
                dependencies["android-aarch64-shippable"] = (
                    "build-android-aarch64-shippable/opt"
                )

            # Add symbols artifact fetch
            fetches.setdefault("android-aarch64-shippable", []).append({
                "artifact": "target.crashreporter-symbols.zip",
                "extract": False,
            })

            # Add scope for android-simpleperf artifact
            scopes = job.setdefault("scopes", [])
            simpleperf_scope = "queue:get-artifact:project/gecko/android-simpleperf/*"
            if simpleperf_scope not in scopes:
                scopes.append(simpleperf_scope)

            # Update treeherder symbol to indicate profiling
            job["treeherder"]["symbol"] = job["treeherder"]["symbol"].replace(
                ")", "-p)"
            )

        yield job


@transforms.add
def setup_perftest_test_date(config, jobs):
    for job in jobs:
        if (
            job.get("attributes", {}).get("batch", False)
            and "--test-date" not in job["run"]["command"]
        ):
            yesterday = (date.today() - timedelta(1)).strftime("%Y.%m.%d")
            job["run"]["command"] += " --test-date %s" % yesterday
        yield job


@transforms.add
def setup_regression_detector(config, jobs):
    for job in jobs:
        if "change-detector" in job.get("name"):
            tasks_to_analyze = []
            for task in config.params["try_task_config"].get("tasks", []):
                # Explicitly skip these tasks since they're
                # part of the mozperftest tasks
                if "side-by-side" in task:
                    continue
                if "change-detector" in task:
                    continue

                # Select these tasks
                if "browsertime" in task:
                    tasks_to_analyze.append(task)
                elif "talos" in task:
                    tasks_to_analyze.append(task)
                elif "awsy" in task:
                    tasks_to_analyze.append(task)
                elif "perftest" in task:
                    tasks_to_analyze.append(task)

            if len(tasks_to_analyze) == 0:
                yield job
                continue

            # Make the change detector task depend on the tasks to analyze.
            # This prevents the task from running until all data is available
            # within the current push.
            job["soft-dependencies"] = tasks_to_analyze
            job["requires"] = "all-completed"

            new_project = config.params["project"]
            if (
                "try" in config.params["project"]
                or config.params["try_mode"] == "try_select"
            ):
                new_project = "try"

            base_project = None
            if (
                config.params
                .get("try_task_config", {})
                .get("env", {})
                .get("PERF_BASE_REVISION", None)
                is not None
            ):
                task_names = " --task-name ".join(tasks_to_analyze)
                base_revision = config.params["try_task_config"]["env"][
                    "PERF_BASE_REVISION"
                ]
                base_project = new_project

                # Add all the required information to the task
                job["run"]["command"] = job["run"]["command"].format(
                    task_name=task_names,
                    base_revision=base_revision,
                    base_branch=base_project,
                    new_branch=new_project,
                    new_revision=config.params["head_rev"],
                )

        yield job


@transforms.add
def apply_perftest_tier_optimization(config, jobs):
    for job in jobs:
        job["optimization"] = {"perf-cadence-backstop": None}
        job["treeherder"]["tier"] = max(job["treeherder"]["tier"], 2)
        yield job


@transforms.add
def set_perftest_attributes(config, jobs):
    for job in jobs:
        attributes = job.setdefault("attributes", {})
        attributes["perftest_name"] = job["name"]
        yield job


@transforms.add
def setup_autoland_retriggers(config, jobs):

    def _allow_task_duplicates(label):
        if "hw-a55-aarch64-shippable-startup-fenix" in label:
            return True
        return False

    for job in jobs:
        attrs = job.setdefault("attributes", {})
        if config.params["project"] == "autoland" and _allow_task_duplicates(
            job["name"]
        ):
            attrs["task_duplicates"] = 4
        yield job


@transforms.add
def hide_cmd_exe_window_on_windows(config, jobs):
    for job in jobs:
        platform = job.get("platform", "")
        platforms = [platform] if isinstance(platform, str) else platform
        if any(p.startswith("windows") for p in platforms):
            worker = job.setdefault("worker", {})
            worker["hide-cmd-window"] = True
        yield job
