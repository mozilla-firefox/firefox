# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from gecko_taskgraph.transforms.perftest import setup_gecko_profile_from_try_config


def applink_job():
    return {
        "name": "android-hw-a55-aarch64-shippable-startup-fenix-newssite-applink-startup",
        "run": {"command": "./mach perftest applink"},
        "treeherder": {"symbol": "perftest-fenix(newssite-applink)"},
    }


@pytest.mark.parametrize("resource", ["background", "foreground"])
def test_resource_profile_from_try_config(run_transform, resource):
    job = {
        "name": f"hw-a55-{resource}-resource-fenix",
        "run": {"command": f"./mach perftest {resource}-resource"},
        "treeherder": {"symbol": f"perftest-fenix({resource}-rsrc)"},
    }
    params = {"try_task_config": {"gecko-profile": True}, "target_tasks_method": "try"}
    [task] = list(
        run_transform(setup_gecko_profile_from_try_config, [job], params=params)
    )

    assert "--simpleperf" in task["run"]["command"]
    assert "--geckoprofiler" not in task["run"]["command"]
    assert "linux64-android-simpleperf-linux-repack" in task["fetches"]["toolchain"]
    assert task["treeherder"]["symbol"].endswith("-p)")


@pytest.mark.parametrize(
    "params,expected",
    (
        pytest.param(
            {"try_task_config": {}, "target_tasks_method": "perftest-fenix-startup"},
            False,
            id="no-profiling",
        ),
        pytest.param(
            {"try_task_config": {"gecko-profile": True}, "target_tasks_method": "try"},
            True,
            id="try-gecko-profile",
        ),
        pytest.param(
            {
                "try_task_config": {},
                "target_tasks_method": "perftest-applink-profiling",
            },
            True,
            id="profiling-target-tasks-method",
        ),
    ),
)
def test_setup_gecko_profile_from_try_config(run_transform, params, expected):
    tasks = list(
        run_transform(
            setup_gecko_profile_from_try_config, [applink_job()], params=params
        )
    )

    assert len(tasks) == 1
    command = tasks[0]["run"]["command"]
    assert ("--simpleperf" in command) == expected
    assert ("--geckoprofiler" in command) == expected
    assert (tasks[0]["treeherder"]["symbol"].endswith("-p)")) == expected


if __name__ == "__main__":
    main()
