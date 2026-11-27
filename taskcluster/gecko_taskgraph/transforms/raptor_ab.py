# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Add tasks that profile Firefox and Chromium-as-Release back to back on one
machine, so that the two profiles can be compared without the machine to
machine differences that separate tasks suffer from.

Each such task is derived from a Windows Firefox native profiling task and
takes the Chromium-as-Release side's arguments and toolchain fetches from the
matching custom-car task. The two are only known together once every task's
command is final, so this has to run on the whole kind after the job
transforms. The task runs ``raptor_ab_script.py``, which runs both sides.
"""

import json
from copy import deepcopy

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.treeherder import join_symbol, split_symbol

from gecko_taskgraph.util.perftest import use_raptor_ab_script

transforms = TransformSequence()

FIREFOX_APP = "-firefox-"
CAR_APP = "-custom-car-"
LABEL_SUFFIX = "-vs-custom-car"


def car_label_for(task):
    attributes = task["attributes"]
    if (
        not attributes.get("supports_ab_profiles")
        or attributes.get("unittest_variant")
        or not attributes.get("test_platform", "").startswith("windows")
        or task["label"].count(FIREFOX_APP) != 1
    ):
        return None
    return task["label"].replace(FIREFOX_APP, CAR_APP)


def raptor_arguments(task):
    """The arguments the task passes to raptor_script.py.

    Windows commands are a single string, joined with spaces and without
    quoting, so splitting on whitespace recovers the words.
    """
    command = task["worker"]["command"]
    if len(command) != 1 or not isinstance(command[0], str):
        raise Exception(
            f"Expected one Windows command for {task['label']}: {command!r}"
        )
    words = command[0].split()
    script = next(
        (i for i, word in enumerate(words) if word.endswith("raptor_script.py")), None
    )
    if script is None:
        raise Exception(f"Cannot find raptor_script.py in {task['label']}'s command")
    return words[script + 1 :]


def fetches(task):
    return json.loads(task["worker"]["env"]["MOZ_FETCHES"]["task-reference"])


def make_vs_car_task(firefox_task, car_task):
    task = deepcopy(firefox_task)
    task["label"] += LABEL_SUFFIX
    task["run-on-projects"] = []

    attributes = task["attributes"]
    attributes["ab_profiles"] = True
    # Keep the ab-profiles action from running on this task, which would
    # replace its AB configuration.
    del attributes["supports_ab_profiles"]
    attributes["raptor_try_name"] += LABEL_SUFFIX

    firefox_fetches = fetches(task)
    car_fetches = [f for f in fetches(car_task) if f not in firefox_fetches]
    task["worker"]["env"]["MOZ_FETCHES"] = {
        "task-reference": json.dumps(firefox_fetches + car_fetches)
    }
    for fetch in car_fetches:
        dependency = fetch["task"].strip("<>")
        task["dependencies"][dependency] = car_task["dependencies"][dependency]

    config = {
        "order": ["base", "new"],
        "runs": {
            "base": {
                "browser": "custom-car",
                "description": "Chromium-as-Release",
                "arguments": raptor_arguments(car_task),
            },
            "new": {"browser": "firefox", "description": "Firefox"},
        },
    }
    task["worker"]["env"]["RAPTOR_AB_CONFIG"] = json.dumps(config)
    task["worker"]["command"] = use_raptor_ab_script(task["worker"]["command"])

    # The Firefox task's allowance already has headroom over how long one run
    # takes, so doubling it covers the second run.
    task["worker"]["max-run-time"] *= 2

    group, symbol = split_symbol(task["treeherder"]["symbol"])
    task["treeherder"]["symbol"] = join_symbol(
        group, f"{symbol.removesuffix('-p')}-car-ab"
    )
    task["description"] = (
        "AB profiles of Chromium-as-Release and Firefox on one machine. "
        + task["description"]
    )
    return task


@transforms.add
def add_vs_car_tasks(config, tasks):
    tasks = list(tasks)
    by_label = {task["label"]: task for task in tasks}
    # Derive the new tasks before yielding any task: the transforms after this
    # one modify the yielded tasks in place, for example adding their routes.
    vs_car_tasks = [
        make_vs_car_task(task, by_label[car_label])
        for task in tasks
        if (car_label := car_label_for(task)) in by_label
    ]
    yield from tasks
    yield from vs_car_tasks
