# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import argparse
import json
import logging
import os
import pathlib
import sys
import tempfile
import time
import traceback
from functools import partial

from mach.decorators import Command, CommandArgument, SubCommand
from mach.util import strtobool
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor


def setup_logging(command_context, quiet=False, verbose=True):
    """
    Set up Python logging for all loggers, sending results to stderr (so
    that command output can be redirected easily) and adding the typical
    mach timestamp.
    """
    # remove the old terminal handler
    old = command_context.log_manager.replace_terminal_handler(None)

    # re-add it, with level and fh set appropriately
    if not quiet:
        level = logging.DEBUG if verbose else logging.INFO
        command_context.log_manager.add_terminal_logging(
            fh=sys.stderr,
            level=level,
            write_interval=old.formatter.write_interval,
            write_times=old.formatter.write_times,
        )
        logging.getLogger("taskcluster").setLevel(logging.INFO)

    # all of the taskgraph logging is unstructured logging
    command_context.log_manager.enable_unstructured()


def get_taskgraph_command_parser(name):
    """Given a command name, obtain its argument parser.

    Args:
        name (str): Name of the command.

    Returns:
        ArgumentParser: An ArgumentParser instance.
    """
    from gecko_taskgraph.main import commands as taskgraph_commands

    command = taskgraph_commands[name]
    parser = argparse.ArgumentParser()
    for arg in command.func.args:
        parser.add_argument(*arg[0], **arg[1])

    parser.set_defaults(func=command.func, **command.defaults)
    return parser


def get_taskgraph_decision_parser():
    parser = get_taskgraph_command_parser("decision")

    extra_args = [
        (
            ["--optimize-target-tasks"],
            {
                "type": lambda flag: bool(strtobool(flag)),
                "nargs": "?",
                "const": "true",
                "help": "If specified, this indicates whether the target "
                "tasks are eligible for optimization. Otherwise, the default "
                "for the project is used.",
            },
        ),
        (
            ["--include-push-tasks"],
            {
                "action": "store_true",
                "help": "Whether tasks from the on-push graph should be re-used "
                "in this graph. This allows cron graphs to avoid rebuilding "
                "jobs that were built on-push.",
            },
        ),
        (
            ["--rebuild-kind"],
            {
                "dest": "rebuild_kinds",
                "action": "append",
                "default": argparse.SUPPRESS,
                "help": "Kinds that should not be re-used from the on-push graph.",
            },
        ),
    ]
    for arg in extra_args:
        parser.add_argument(*arg[0], **arg[1])

    return parser


@Command(
    "taskgraph",
    category="ci",
    description="Manipulate TaskCluster task graphs defined in-tree",
    virtualenv_name="taskgraph",
)
def taskgraph_command(command_context):
    """The taskgraph subcommands all relate to the generation of task graphs
    for Gecko continuous integration.  A task graph is a set of tasks linked
    by dependencies: for example, a binary must be built before it is tested,
    and that build may further depend on various toolchains, libraries, etc.
    """


@SubCommand(
    "taskgraph",
    "kind-graph",
    description="Generate a graph of the relationship between taskgraph kinds",
    parser=partial(get_taskgraph_command_parser, "kind-graph"),
)
def taskgraph_kind_graph(command_context, **options):
    from gecko_taskgraph.main import commands as taskgraph_commands

    try:
        setup_logging(command_context)
        return taskgraph_commands["kind-graph"].func(options)
    except Exception:
        traceback.print_exc()
        sys.exit(1)


@SubCommand(
    "taskgraph",
    "tasks",
    description="Show all tasks in the taskgraph",
    parser=partial(get_taskgraph_command_parser, "tasks"),
)
def taskgraph_tasks(command_context, **options):
    return run_show_taskgraph(command_context, **options)


@SubCommand(
    "taskgraph",
    "full",
    description="Show the full taskgraph",
    parser=partial(get_taskgraph_command_parser, "full"),
)
def taskgraph_full(command_context, **options):
    return run_show_taskgraph(command_context, **options)


@SubCommand(
    "taskgraph",
    "target",
    description="Show the target task set",
    parser=partial(get_taskgraph_command_parser, "target"),
)
def taskgraph_target(command_context, **options):
    return run_show_taskgraph(command_context, **options)


@SubCommand(
    "taskgraph",
    "target-graph",
    description="Show the target taskgraph",
    parser=partial(get_taskgraph_command_parser, "target-graph"),
)
def taskgraph_target_graph(command_context, **options):
    return run_show_taskgraph(command_context, **options)


@SubCommand(
    "taskgraph",
    "optimized",
    description="Show the optimized taskgraph",
    parser=partial(get_taskgraph_command_parser, "optimized"),
)
def taskgraph_optimized(command_context, **options):
    return run_show_taskgraph(command_context, **options)


@SubCommand(
    "taskgraph",
    "morphed",
    description="Show the morphed taskgraph",
    parser=partial(get_taskgraph_command_parser, "morphed"),
)
def taskgraph_morphed(command_context, **options):
    return run_show_taskgraph(command_context, **options)


def run_show_taskgraph(command_context, **options):
    import gecko_taskgraph.main

    # There are cases where we don't want to set up mach logging (e.g logs
    # are being redirected to disk). By monkeypatching the 'setup_logging'
    # function we can let 'taskgraph.main' decide whether or not to log to
    # the terminal.
    gecko_taskgraph.main.setup_logging = partial(
        setup_logging,
        command_context,
        quiet=options["quiet"],
        verbose=options["verbose"],
    )
    show_taskgraph = options.pop("func")
    return show_taskgraph(options)


@SubCommand("taskgraph", "actions", description="Write actions.json to stdout")
@CommandArgument(
    "--root", "-r", help="root of the taskgraph definition relative to topsrcdir"
)
@CommandArgument(
    "--quiet", "-q", action="store_true", help="suppress all logging output"
)
@CommandArgument(
    "--verbose",
    "-v",
    action="store_true",
    help="include debug-level logging output",
)
@CommandArgument(
    "--parameters",
    "-p",
    default="project=mozilla-central",
    help="parameters file (.yml or .json; see `taskcluster/docs/parameters.rst`)`",
)
def taskgraph_actions(command_context, **options):
    import gecko_taskgraph
    import gecko_taskgraph.actions
    from taskgraph.generator import TaskGraphGenerator
    from taskgraph.parameters import parameters_loader

    try:
        setup_logging(
            command_context, quiet=options["quiet"], verbose=options["verbose"]
        )
        parameters = parameters_loader(options["parameters"])

        tgg = TaskGraphGenerator(
            root_dir=options.get("root"),
            parameters=parameters,
        )

        actions = gecko_taskgraph.actions.render_actions_json(
            tgg.parameters,
            tgg.graph_config,
            decision_task_id="DECISION-TASK",
        )
        print(json.dumps(actions, sort_keys=True, indent=2, separators=(",", ": ")))
    except Exception:
        traceback.print_exc()
        sys.exit(1)


@SubCommand(
    "taskgraph",
    "decision",
    description="Run the decision task",
    parser=get_taskgraph_decision_parser,
)
def taskgraph_decision(command_context, **options):
    """Run the decision task: generate a task graph and submit to
    TaskCluster.  This is only meant to be called within decision tasks,
    and requires a great many arguments.  Commands like `mach taskgraph
    optimized` are better suited to use on the command line, and can take
    the parameters file generated by a decision task."""
    from gecko_taskgraph import register as register_gecko_taskgraph
    from gecko_taskgraph.decision import (
        build_decision_perfherder_data,
        get_decision_parameters,
        taskgraph_decision,
    )
    from taskgraph.config import load_graph_config

    try:
        setup_logging(command_context)

        in_automation = os.environ.get("MOZ_AUTOMATION") == "1"
        moz_upload_dir = os.environ.get("MOZ_UPLOAD_DIR")
        monitor = None
        forward_path = None
        if in_automation and moz_upload_dir:
            monitor = SystemResourceMonitor(poll_interval=0.1)
            monitor.start()
            if not monitor.forwarding:
                fd, forward_path = tempfile.mkstemp(
                    prefix="taskgraph-", suffix=".jsonl"
                )
                os.close(fd)
                monitor.accept_forwarded(forward_path)

        try:
            t_params_start = time.monotonic()
            if monitor is not None:
                monitor.begin_phase("parameters")
            graph_config = load_graph_config(options.get("root") or "taskcluster")
            register_gecko_taskgraph(graph_config)
            # Honor the graph_config's own register hook (e.g.
            # comm_taskgraph:register for Thunderbird) before parameters
            # resolve. GraphConfig.register() inserts root_dir into sys.path
            # and then calls the hook, which extends sys.path further and
            # registers comm-specific parameters with the schema.
            register_path = graph_config["taskgraph"].get("register")
            if register_path and register_path != "gecko_taskgraph:register":
                graph_config.register()
            parameters = get_decision_parameters(graph_config, options)
            t_params_end = time.monotonic()
            if monitor is not None:
                monitor.finish_phase("parameters")

            t_taskgraph_start = time.monotonic()
            if monitor is not None:
                monitor.begin_phase("taskgraph")
            ret = taskgraph_decision(options, parameters)
            t_taskgraph_end = time.monotonic()
            if monitor is not None:
                monitor.finish_phase("taskgraph")
        finally:
            if monitor is not None:
                monitor.stop()
            if forward_path:
                os.remove(forward_path)
            if monitor is not None and not monitor.forwarding:
                upload_dir = pathlib.Path(moz_upload_dir)
                profile_path = upload_dir / "profile_build_resources.json"
                with open(profile_path, "w", encoding="utf-8", newline="\n") as f:
                    to_write = json.dumps(monitor.as_profile(), separators=(",", ":"))
                    f.write(to_write)

        if in_automation:
            params_time = t_params_end - t_params_start
            taskgraph_time = t_taskgraph_end - t_taskgraph_start
            perfherder_data = build_decision_perfherder_data(
                graph_config["trust-domain"], params_time, taskgraph_time
            )
            print(
                f"PERFHERDER_DATA: {json.dumps(perfherder_data)}",
                file=sys.stderr,
            )

            if moz_upload_dir:
                upload_dir = pathlib.Path(moz_upload_dir)
                out_path = upload_dir / "perfherder-data-decision.json"
                with out_path.open("w", encoding="utf-8") as f:
                    json.dump(perfherder_data, f)

        return ret
    except Exception:
        traceback.print_exc()
        sys.exit(1)


@SubCommand(
    "taskgraph",
    "action-callback",
    description="Run action callback used by action tasks",
    parser=partial(get_taskgraph_command_parser, "action-callback"),
)
def action_callback(command_context, **options):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)
    taskgraph_commands["action-callback"].func(options)


@SubCommand(
    "taskgraph",
    "test-action-callback",
    description="Run an action callback in a testing mode",
    parser=partial(get_taskgraph_command_parser, "test-action-callback"),
)
def test_action_callback(command_context, **options):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)

    if not options["parameters"]:
        options["parameters"] = "project=mozilla-central"

    taskgraph_commands["test-action-callback"].func(options)


@SubCommand(
    "taskgraph",
    "load-image",
    description="Load a pre-built Docker image. Note that you need to "
    "have docker installed and running for this to work.",
    parser=partial(get_taskgraph_command_parser, "load-image"),
)
def load_image(command_context, **kwargs):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)
    taskgraph_commands["load-image"].func(kwargs)


@SubCommand(
    "taskgraph",
    "build-image",
    description="Build a Docker image",
    parser=partial(get_taskgraph_command_parser, "build-image"),
)
def build_image(command_context, **kwargs):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)
    try:
        taskgraph_commands["build-image"].func(kwargs)
    except Exception:
        traceback.print_exc()
        sys.exit(1)


@SubCommand(
    "taskgraph",
    "image-digest",
    description="Print the digest of the image of this name based on the "
    "current contents of the tree.",
    parser=partial(get_taskgraph_command_parser, "build-image"),
)
def image_digest(command_context, **kwargs):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)
    taskgraph_commands["image-digest"].func(kwargs)


@SubCommand(
    "taskgraph",
    "load-task",
    description="Loads a pre-built Docker image and drops you into a container with "
    "the same environment variables and run-task setup as the specified task. "
    "The task's payload.command will be replaced with 'bash'. You need to have "
    "docker installed and running for this to work.",
    parser=partial(get_taskgraph_command_parser, "load-task"),
)
def load_task(command_context, **kwargs):
    from gecko_taskgraph.main import commands as taskgraph_commands

    setup_logging(command_context)
    taskgraph_commands["load-task"].func(kwargs)


@Command(
    "release-history",
    category="ci",
    description="Query balrog for release history used by enable partials generation",
    virtualenv_name="try",
)
@CommandArgument(
    "-b",
    "--branch",
    help="The gecko project branch used in balrog, such as "
    "mozilla-central, release, maple",
)
@CommandArgument(
    "--product", default="Firefox", help="The product identifier, such as 'Firefox'"
)
def generate_partials_builds(command_context, product, branch):
    from gecko_taskgraph.util.partials import populate_release_history

    try:
        import yaml

        release_history = {"release_history": populate_release_history(product, branch)}
        print(
            yaml.safe_dump(
                release_history, allow_unicode=True, default_flow_style=False
            )
        )
    except Exception:
        traceback.print_exc()
        sys.exit(1)
