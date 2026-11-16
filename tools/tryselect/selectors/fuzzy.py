# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import sys
from pathlib import PurePath

from ..cli import BaseTryParser
from ..push import check_working_directory, generate_try_task_config, push_to_try
from ..tasks import filter_tasks_by_paths, filter_tasks_by_worker_type, generate_tasks
from ..util.fzf import (
    FZF_NOT_FOUND,
    PREVIEW_SCRIPT,
    format_header,
    fzf_bootstrap,
    fzf_shortcuts,
    run_fzf,
)


class FuzzyParser(BaseTryParser):
    name = "fuzzy"
    arguments = [
        [
            ["-q", "--query"],
            {
                "metavar": "STR",
                "action": "append",
                "default": [],
                "help": "Use the given query instead of entering the selection "
                "interface. Equivalent to typing <query><ctrl-a><enter> "
                "from the interface. Specifying multiple times schedules "
                "the union of computed tasks.",
            },
        ],
        [
            ["-i", "--interactive"],
            {
                "action": "store_true",
                "default": False,
                "help": "Run fzf interactively, even if --preset or --query is used. "
                "Tasks selected interactively will be unioned with tasks selected "
                "by the --preset/--query flags. If -x/--and is also specified, tasks "
                "selected interactively will instead be intersected with tasks "
                "selected by --preset/--query.",
            },
        ],
        [
            ["-x", "--and"],
            {
                "dest": "intersection",
                "action": "store_true",
                "default": False,
                "help": "When specifying queries on the command line with -q/--query, "
                "use the intersection of tasks rather than the union. This is "
                "especially useful for post filtering presets.",
            },
        ],
        [
            ["-e", "--exact"],
            {
                "action": "store_true",
                "default": False,
                "help": "Enable exact match mode. Terms will use an exact match "
                "by default, and terms prefixed with ' will become fuzzy.",
            },
        ],
        [
            ["-u", "--update"],
            {
                "action": "store_true",
                "default": False,
                "help": "Update fzf before running.",
            },
        ],
        [
            ["--disable-target-task-filter", "--all-tasks"],
            {
                "action": "store_true",
                "default": False,
                "help": "Some tasks run on mozilla-central but are filtered out "
                "of the default list due to resource constraints. This flag "
                "disables this filtering.",
            },
        ],
        [
            ["--include-duplicates"],
            {
                "action": "store_true",
                "default": False,
                "help": "Include tasks copied from other kinds by the duplicate "
                "transforms, such as the -appservices-in-tree tasks.",
            },
        ],
        [
            ["--show-chunk-numbers"],
            {
                "action": "store_true",
                "default": False,
                "help": "Chunk numbers are hidden to simplify the selection. This flag "
                "makes them appear again.",
            },
        ],
    ]
    common_groups = ["push", "task", "preset"]
    task_configs = [
        "artifact",
        "browsertime",
        "chemspill-prio",
        "disable-pgo",
        "do-not-optimize",
        "env",
        "existing-tasks",
        "extensions",
        "gecko-profile",
        "new-test-config",
        "path",
        "target-tasks-method",
        "test-tag",
        "pernosco",
        "pushdate",
        "rebuild",
        "repeat-tests",
        "routes",
        "worker-overrides",
    ]


def run(
    metrics,
    update=False,
    query=None,
    intersect_query=None,
    full=False,
    parameters=None,
    try_config_params=None,
    save_query=False,
    stage_changes=False,
    dry_run=False,
    write_task_config=False,
    message="{msg}",
    test_paths=None,
    test_tag=None,
    exact=False,
    closed_tree=False,
    disable_target_task_filter=False,
    include_duplicates=False,
    push_to_vcs=False,
    show_chunk_numbers=False,
    new_test_config=False,
    **kwargs,
):
    metrics.mach_try.fzf_bootstrap_duration.start()
    from gecko_taskgraph.target_tasks import filter_by_uncommon_try_tasks

    fzf = fzf_bootstrap(update)
    metrics.mach_try.fzf_bootstrap_duration.stop()

    if not fzf:
        print(FZF_NOT_FOUND)
        return 1

    push = not stage_changes and not dry_run and not write_task_config
    check_working_directory(push)

    target_tasks_method = None
    if try_config_params and "target_tasks_method" in try_config_params:
        target_tasks_method = try_config_params.pop("target_tasks_method")

    metrics.mach_try.taskgraph_generation_duration.start()
    tg = generate_tasks(
        parameters,
        full=full,
        disable_target_task_filter=disable_target_task_filter,
        target_tasks_method=target_tasks_method,
    )
    all_tasks = tg.tasks
    metrics.mach_try.taskgraph_generation_duration.stop()

    metrics.mach_try.task_filtering_duration.start()

    if not full and not disable_target_task_filter:
        all_tasks = {
            task_name: task
            for task_name, task in all_tasks.items()
            if filter_by_uncommon_try_tasks(task_name)
        }

    if not include_duplicates:
        all_tasks = {
            task_name: task
            for task_name, task in all_tasks.items()
            if "duplicate-of" not in task.attributes
        }

    if try_config_params.get("try_task_config", {}).get("worker-types", []):
        all_tasks = filter_tasks_by_worker_type(all_tasks, try_config_params)
        if not all_tasks:
            metrics.mach_try.task_filtering_duration.stop()
            return 1

    if test_paths or test_tag:
        all_tasks = filter_tasks_by_paths(all_tasks, test_paths, tag=test_tag)
        if not all_tasks:
            metrics.mach_try.task_filtering_duration.stop()
            return 1

    metrics.mach_try.task_filtering_duration.stop()

    key_shortcuts = [k + ":" + v for k, v in fzf_shortcuts.items()]
    base_cmd = [
        fzf,
        "-m",
        "--bind",
        ",".join(key_shortcuts),
        "--header",
        format_header(),
        "--preview-window=right:30%",
        "--print-query",
        "--preview",
        f'{str(PurePath(sys.executable))} {PREVIEW_SCRIPT} -t "{{+f}}"',
    ]

    if exact:
        base_cmd.append("--exact")

    selected = set()
    queries = []

    def get_tasks(query_arg=None, candidate_tasks=all_tasks):
        cmd = base_cmd[:]
        if query_arg and query_arg != "INTERACTIVE":
            cmd.extend(["-f", query_arg])

        if not show_chunk_numbers:
            fzf_tasks = set(task.chunk_pattern for task in candidate_tasks.values())
        else:
            fzf_tasks = set(candidate_tasks.keys())

        metrics.mach_try.interactive_duration.start()
        query_str, tasks = run_fzf(cmd, sorted(fzf_tasks))
        metrics.mach_try.interactive_duration.stop()

        queries.append(query_str)
        return set(tasks)

    for q in query or []:
        selected |= get_tasks(q)

    for q in intersect_query or []:
        if not selected:
            selected |= get_tasks(q)
        else:
            selected &= get_tasks(
                q,
                {
                    task_name: task
                    for task_name, task in all_tasks.items()
                    if task_name in selected or task.chunk_pattern in selected
                },
            )

    if not queries:
        selected = get_tasks()

    if not selected:
        print("no tasks selected")
        return

    if save_query:
        return queries

    # build commit message
    if "preset_id" in kwargs:
        msg = "Fuzzy (preset: {})".format(kwargs.get("preset_id"))
    else:
        msg = "Fuzzy"
    args = [f"query={q}" for q in queries]
    if test_paths:
        args.append("paths={}".format(":".join(test_paths)))
    if args:
        msg = "{} {}".format(msg, "&".join(args))

    metrics.mach_try.task_config_generation_duration.start()
    try_task_config = generate_try_task_config(
        "fuzzy", selected, params=try_config_params
    )
    metrics.mach_try.task_config_generation_duration.stop()

    return push_to_try(
        "fuzzy",
        message.format(msg=msg),
        metrics,
        try_task_config=try_task_config,
        stage_changes=stage_changes,
        dry_run=dry_run,
        write_task_config=write_task_config,
        closed_tree=closed_tree,
        push_to_vcs=push_to_vcs,
    )
