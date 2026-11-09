# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json
import logging

import requests
from taskcluster.exceptions import TaskclusterRestFailure
from taskgraph.taskgraph import TaskGraph
from taskgraph.util.taskcluster import get_artifact_from_index, get_task_definition

from .registry import register_callback_action
from .util import combine_task_graph_files, create_tasks, fetch_graph_and_labels

PUSHLOG_TMPL = "{}/json-pushes?version=2&startID={}&endID={}"
INDEX_TMPL = "gecko.v2.{}.pushlog-id.{}.decision"
SIMPLEPERF_COMPATIBLE_TESTS = [
    "-homeview-",
    "-applink-",
    "-restore-",
    "-background-resource-",
    "-foreground-resource-",
]
RESOURCE_TESTS = ["-background-resource-", "-foreground-resource-"]

SIMPLEPERF_DEPENDENCY = {
    "artifact": "project/gecko/android-simpleperf/android-simpleperf.tar.zst",
    "extract": True,
    "task": "<toolchain-linux64-android-simpleperf-linux-repack>",
}
SAMPLY_DEPENDENCY = {
    "artifact": "public/build/samply.tar.zst",
    "extract": True,
    "task": "<toolchain-linux64-samply>",
}
PROFILER_NODE_TOOLS_DEPENDENCY = {
    "artifact": "public/build/profiler-node-tools.tar.zst",
    "extract": True,
    "task": "<toolchain-profiler-node-tools>",
}
SYMBOLS_DEPENDENCY = {
    "artifact": "public/build/target.crashreporter-symbols.zip",
    "extract": False,
    "task": "<build-android-aarch64-shippable/opt>",
}
BUILD_SYMBOLS_DEPENDENCY = {
    "artifact": "public/build/target.crashreporter-symbols.zip",
    "extract": False,
    "task": "<build>",
}

DEPENDANCY_TO_ADD_FOR_TASK_REFERENCE = [
    SIMPLEPERF_DEPENDENCY,
    SAMPLY_DEPENDENCY,
    PROFILER_NODE_TOOLS_DEPENDENCY,
    SYMBOLS_DEPENDENCY,
]
dependencies_to_add_dict = {
    "build-android-aarch64-shippable/opt": "build-android-aarch64-shippable/opt",
    "toolchain-profiler-node-tools": "toolchain-profiler-node-tools",
    "toolchain-linux64-android-simpleperf-linux-repack": "toolchain-linux64-android-simpleperf-linux-repack",
    "toolchain-linux64-samply": "toolchain-linux64-samply",
}


logger = logging.getLogger(__name__)


@register_callback_action(
    title="GeckoProfile",
    name="geckoprofile",
    symbol="Gp",
    description=(
        "Take the label of the current task, "
        "and trigger the task with that label "
        "on previous pushes in the same project "
        "while adding the --gecko-profile cmd arg. "
        "Plus optional overrides for threads, "
        "features, and sampling interval."
    ),
    order=200,
    context=[
        {"test-type": "talos"},
        {"test-type": "raptor"},
        {"test-type": "mozperftest"},
    ],
    schema={
        "type": "object",
        "properties": {
            "depth": {
                "type": "integer",
                "default": 1,
                "minimum": 1,
                "maximum": 10,
                "title": "Depth",
                "description": "How many pushes to backfill the profiling task on.",
            },
            "gecko_profile_interval": {
                "type": "number",
                "default": None,
                "title": "Sampling interval (ms)",
                "description": "How often to sample the profiler (in ms).",
            },
            "gecko_profile_features": {
                "type": "string",
                "default": "",
                "title": "Features",
                "description": "Comma-separated Gecko profiler features. "
                "Example: js,stackwalk,screenshots,memory",
            },
            "gecko_profile_threads": {
                "type": "string",
                "default": "",
                "title": "Threads",
                "description": "Comma-separated thread names to profile. "
                "Example: GeckoMain,Compositor,Renderer",
            },
        },
    },
    available=lambda parameters: True,
)
def geckoprofile_action(parameters, graph_config, input, task_group_id, task_id):
    task = get_task_definition(task_id)
    label = task["metadata"]["name"]
    pushes = []
    depth = input.get("depth", 1)
    end_id = int(parameters["pushlog_id"])

    while True:
        start_id = max(end_id - depth, 0)
        pushlog_url = PUSHLOG_TMPL.format(
            parameters["head_repository"], start_id, end_id
        )
        r = requests.get(pushlog_url)
        r.raise_for_status()
        pushes = pushes + list(r.json()["pushes"].keys())
        if len(pushes) >= depth:
            break

        end_id = start_id - 1
        start_id -= depth
        if start_id < 0:
            break

    pushes = sorted(pushes)[-depth:]
    backfill_pushes = []

    for push in pushes:
        try:
            push_params = get_artifact_from_index(
                INDEX_TMPL.format(parameters["project"], push), "public/parameters.yml"
            )
            push_decision_task_id, full_task_graph, label_to_taskid, _ = (
                fetch_graph_and_labels(push_params, graph_config)
            )
        except TaskclusterRestFailure as e:
            logger.info(f"Skipping {push} due to missing index artifacts! Error: {e}")
            continue

        if label in full_task_graph.tasks.keys():

            def modifier(task):
                if task.label != label:
                    return task

                interval = input.get("gecko_profile_interval")
                features = input.get("gecko_profile_features")
                threads = input.get("gecko_profile_threads")

                task_kind = task.kind
                env = task.task["payload"]["env"]
                perf_flags = env.get("PERF_FLAGS", "")
                test_suite = task.attributes.get("unittest_suite")
                profiling_command_flags = ["--gecko-profile"]

                if task_kind == "perftest":
                    is_resource_test = any(test in label for test in RESOURCE_TESTS)
                    if is_resource_test:
                        env["PERF_FLAGS"] = (
                            perf_flags
                            + " simpleperf simpleperf-path=$MOZ_FETCHES_DIR/android-simpleperf"
                        ).strip()
                        profiling_command_flags = []
                    # Add "gecko-profile" to PERF_FLAGS if missing and then add remaining
                    # Gecko Profiler customizations via MOZ_PROFILER_STARTUP_* env overrides.
                    elif "gecko-profile" not in perf_flags:
                        env["PERF_FLAGS"] = (perf_flags + " gecko-profile").strip()

                    if interval is not None:
                        env["MOZ_PROFILER_STARTUP_INTERVAL"] = str(interval)
                    if features is not None:
                        env["MOZ_PROFILER_STARTUP_FEATURES"] = features
                    if threads is not None:
                        env["MOZ_PROFILER_STARTUP_FILTERS"] = threads
                    if not is_resource_test and any(
                        test in label for test in SIMPLEPERF_COMPATIBLE_TESTS
                    ):
                        #  We will need to unify the options for enabling gecko_profiling across test harnesses, see bug 2000281
                        env["PERF_FLAGS"] = (
                            perf_flags
                            + " ".join([
                                "simpleperf",
                                "simpleperf-path=$MOZ_FETCHES_DIR/android-simpleperf",
                                "geckoprofiler",
                            ])
                        ).strip()

                elif test_suite == "raptor":
                    # Use PERF_FLAGS env to cusomize profiler settings.
                    raptor_flags = []
                    if interval is not None:
                        raptor_flags.append(f"gecko-profile-interval={interval}")
                    if features is not None:
                        raptor_flags.append(f"gecko-profile-features={features}")
                    if threads is not None:
                        raptor_flags.append(f"gecko-profile-threads={threads}")

                    env["PERF_FLAGS"] = (
                        perf_flags + " " + " ".join(raptor_flags)
                    ).strip()

                elif test_suite == "talos":
                    # Pass everything through the command directly
                    # Bug 1979192 will modify Talos to make use of PERF_FLAGS.
                    if interval is not None:
                        profiling_command_flags.append(
                            f"--gecko-profile-interval={interval}"
                        )
                    if features is not None:
                        profiling_command_flags.append(
                            f"--gecko-profile-features={features}"
                        )
                    if threads is not None:
                        profiling_command_flags.append(
                            f"--gecko-profile-threads={threads}"
                        )

                if "command" in task.task["payload"]:
                    cmd = task.task["payload"]["command"]
                    task.task["payload"]["command"] = add_args_to_perf_command(
                        cmd, profiling_command_flags
                    )

                # A single profile is enough, don't inherit the retrigger count
                # of the task being profiled.
                task.attributes["task_duplicates"] = 1

                task.task["extra"]["treeherder"]["symbol"] += "-p"
                task.task["extra"]["treeherder"]["groupName"] += " (profiling)"
                return task

            # When Treeherder's "Generate performance profile" button is pressed,
            # add dependencies for processing raw Simpleperf or Gecko profiles
            # The Samply dependency is needed to serve symbols and the profile and
            # profiler-node-tools dependency provides profiler-edit, a node tool
            # that performs the profile symbolication.
            if any(test in label for test in SIMPLEPERF_COMPATIBLE_TESTS):
                full_task_graph = full_task_graph.to_json()
                for key, value in dependencies_to_add_dict.items():
                    full_task_graph[label]["dependencies"][key] = value
                full_task_graph = TaskGraph.from_json(full_task_graph)[1]

                full_task_graph[label].task["scopes"].append(
                    "queue:get-artifact:project/gecko/android-simpleperf/*"
                )

                task_reference_full_taskgraph = json.loads(
                    full_task_graph.tasks[label].task["payload"]["env"]["MOZ_FETCHES"][
                        "task-reference"
                    ]
                )
                task_reference_full_taskgraph.extend(
                    DEPENDANCY_TO_ADD_FOR_TASK_REFERENCE
                )
                full_task_graph.tasks[label].task["payload"]["env"]["MOZ_FETCHES"][
                    "task-reference"
                ] = json.dumps(task_reference_full_taskgraph)
            else:
                test_platform = full_task_graph.tasks[label].attributes.get(
                    "test_platform", ""
                )
                if "macosx" in test_platform and "aarch64" in test_platform:
                    toolchain_platform = "macosx64-aarch64"
                elif "macosx" in test_platform:
                    toolchain_platform = "macosx64"
                elif "win" in test_platform:
                    toolchain_platform = "win64"
                else:
                    toolchain_platform = "linux64"
                samply_platform_toolchain = f"toolchain-{toolchain_platform}-samply"
                node_toolchain = find_node_toolchain(
                    full_task_graph, toolchain_platform
                )

                samply_platform_dependency = {
                    "artifact": "public/build/samply.tar.zst",
                    "extract": True,
                    "task": f"<{samply_platform_toolchain}>",
                }

                full_task_graph = full_task_graph.to_json()
                dependencies = full_task_graph[label]["dependencies"]
                dependencies["toolchain-profiler-node-tools"] = (
                    "toolchain-profiler-node-tools"
                )
                dependencies[samply_platform_toolchain] = samply_platform_toolchain

                # Add node as there are non-Raptor frameworks that support
                # Gecko profiling (e.g. Talos).
                has_node = node_toolchain in dependencies
                if not has_node:
                    dependencies[node_toolchain] = node_toolchain

                full_task_graph = TaskGraph.from_json(full_task_graph)[1]
                task_reference_full_taskgraph = json.loads(
                    full_task_graph.tasks[label].task["payload"]["env"]["MOZ_FETCHES"][
                        "task-reference"
                    ]
                )
                if not has_node:
                    task_reference_full_taskgraph.append({
                        "artifact": "public/build/node.tar.zst",
                        "extract": True,
                        "task": f"<{node_toolchain}>",
                    })
                task_reference_full_taskgraph.extend([
                    PROFILER_NODE_TOOLS_DEPENDENCY,
                    samply_platform_dependency,
                ])
                if "build" in dependencies and not any(
                    fetch["artifact"].endswith("target.crashreporter-symbols.zip")
                    for fetch in task_reference_full_taskgraph
                ):
                    task_reference_full_taskgraph.append(BUILD_SYMBOLS_DEPENDENCY)
                full_task_graph.tasks[label].task["payload"]["env"]["MOZ_FETCHES"][
                    "task-reference"
                ] = json.dumps(task_reference_full_taskgraph)

            create_tasks(
                graph_config,
                [label],
                full_task_graph,
                label_to_taskid,
                push_params,
                push_decision_task_id,
                push,
                modifier=modifier,
            )
            backfill_pushes.append(push)
        else:
            logger.info(f"Could not find {label} on {push}. Skipping.")
    combine_task_graph_files(backfill_pushes)


def find_node_toolchain(full_task_graph, toolchain_platform):
    """
    Return the label of the node toolchain for `toolchain_platform`. The label
    is resolved from the given graph instead of being hardcoded, as backfilled
    pushes may predate a node upgrade.
    """
    alias = f"{toolchain_platform}-node"
    for task in full_task_graph.tasks.values():
        if task.kind != "toolchain":
            continue
        aliases = task.attributes.get("toolchain-alias", [])
        if isinstance(aliases, str):
            aliases = [aliases]
        if alias in aliases:
            return task.label
    raise Exception(f"Could not find the {alias} toolchain in the task graph")


def add_args_to_perf_command(payload_commands, extra_args=()):
    """
    Add custom command line args to a given command.
    args:
      payload_commands: the raw command as seen by taskcluster
      extra_args: array of args we want to inject
    """
    perf_command_idx = -1  # currently, it's the last (or only) command
    perf_command = payload_commands[perf_command_idx]

    command_form = "default"
    if isinstance(perf_command, str):
        # windows has a single command, in long string form
        perf_command = perf_command.split(" ")
        command_form = "string"
    # osx & linux have an array of subarrays

    perf_command.extend(extra_args)

    if command_form == "string":
        # pack it back to list
        perf_command = " ".join(perf_command)

    payload_commands[perf_command_idx] = perf_command

    return payload_commands
