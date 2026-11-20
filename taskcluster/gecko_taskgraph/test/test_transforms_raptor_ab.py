# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json

import pytest
from mozunit import main

from gecko_taskgraph.transforms.raptor_ab import transforms
from gecko_taskgraph.util.perftest import use_raptor_ab_script

PLATFORM = "windows11-64-24h2-shippable/opt"
FIREFOX_LABEL = (
    f"test-{PLATFORM}-browsertime-benchmark-firefox-speedometer3-native-profiling"
)
CAR_LABEL = (
    f"test-{PLATFORM}-browsertime-benchmark-custom-car-speedometer3-native-profiling"
)
SHARED_FETCH = {"artifact": "public/build/node.tar.zst", "task": "<node>"}
SYMBOLS_FETCH = {
    "artifact": "public/build/target.crashreporter-symbols.zip",
    "task": "<build>",
}
CAR_FETCH = {
    "artifact": "public/build/car_win64.tar.zst",
    "task": "<toolchain-win64-custom-car>",
}


def make_task(label, app_arguments, fetches, dependencies, attributes=None):
    return {
        "label": label,
        "description": "Raptor speedometer3",
        "attributes": {
            "test_platform": PLATFORM,
            "unittest_variant": None,
            "raptor_try_name": label.split("/opt-")[1],
            **(attributes or {}),
        },
        "run-on-projects": ["autoland-only"],
        "dependencies": dependencies,
        "treeherder": {"symbol": "Btime(sp3-p)"},
        "worker": {
            "max-run-time": 4200,
            "env": {"MOZ_FETCHES": {"task-reference": json.dumps(fetches)}},
            "command": [
                "python3 run-task -- python3 -u mozharness\\scripts\\raptor_script.py "
                f"--cfg windows_config.py {app_arguments} "
                "--browsertime-node $MOZ_FETCHES_DIR/node/node.exe"
            ],
        },
    }


def firefox_task(**kwargs):
    return make_task(
        FIREFOX_LABEL,
        "--app=firefox",
        [SHARED_FETCH, SYMBOLS_FETCH],
        {"build": "build-win64-shippable/opt", "node": "toolchain-node"},
        attributes={"supports_ab_profiles": True, **kwargs},
    )


def car_task():
    return make_task(
        CAR_LABEL,
        "--app=custom-car --browsertime-arg=chrome.args=--enable-benchmarking",
        [SHARED_FETCH, CAR_FETCH],
        {
            "build": "build-win64-shippable/opt",
            "node": "toolchain-node",
            "toolchain-win64-custom-car": "toolchain-win64-custom-car",
        },
    )


def test_adds_vs_car_task(run_transform):
    tasks = list(run_transform(transforms, [firefox_task(), car_task()]))
    assert [t["label"] for t in tasks] == [
        FIREFOX_LABEL,
        CAR_LABEL,
        f"{FIREFOX_LABEL}-vs-custom-car",
    ]
    original, _, task = tasks

    assert "supports_ab_profiles" in original["attributes"]
    assert "supports_ab_profiles" not in task["attributes"]
    assert task["attributes"]["raptor_try_name"].endswith(
        "native-profiling-vs-custom-car"
    )
    assert task["run-on-projects"] == []
    assert task["attributes"]["ab_profiles"]
    assert task["treeherder"]["symbol"] == "Btime(sp3-car-ab)"
    assert task["worker"]["max-run-time"] == 8400
    assert task["worker"]["command"] == [
        original["worker"]["command"][0].replace(
            "\\raptor_script.py", "\\raptor_ab_script.py"
        )
    ]

    fetches = json.loads(task["worker"]["env"]["MOZ_FETCHES"]["task-reference"])
    assert fetches == [SHARED_FETCH, SYMBOLS_FETCH, CAR_FETCH]
    assert task["dependencies"]["toolchain-win64-custom-car"] == (
        "toolchain-win64-custom-car"
    )

    config = json.loads(task["worker"]["env"]["RAPTOR_AB_CONFIG"])
    assert config["runs"]["base"] == {
        "browser": "custom-car",
        "description": "Chromium-as-Release",
        "arguments": [
            "--cfg",
            "windows_config.py",
            "--app=custom-car",
            "--browsertime-arg=chrome.args=--enable-benchmarking",
            "--browsertime-node",
            "$MOZ_FETCHES_DIR/node/node.exe",
        ],
    }
    assert config["runs"]["new"]["browser"] == "firefox"
    assert "arguments" not in config["runs"]["new"]


def test_later_transforms_do_not_leak_into_vs_car_task(run_transform):
    tasks = run_transform(transforms, [firefox_task(), car_task()])
    for task in tasks:
        task.setdefault("routes", []).append("tc-treeherder")
    assert task["routes"] == ["tc-treeherder"]


def test_skips_variants(run_transform):
    tasks = list(
        run_transform(
            transforms, [firefox_task(unittest_variant="headless"), car_task()]
        )
    )
    assert len(tasks) == 2


def test_skips_tasks_without_car_partner(run_transform):
    tasks = list(run_transform(transforms, [firefox_task()]))
    assert len(tasks) == 1


def test_use_raptor_ab_script_in_posix_command():
    command = [
        ["chmod", "+x", "run-task"],
        ["./run-task", "--", "python3", "-u", "mozharness/scripts/raptor_script.py"],
    ]
    assert use_raptor_ab_script(command) == [
        ["chmod", "+x", "run-task"],
        ["./run-task", "--", "python3", "-u", "mozharness/scripts/raptor_ab_script.py"],
    ]


def test_use_raptor_ab_script_needs_raptor_script():
    with pytest.raises(Exception, match="found it 0 times"):
        use_raptor_ab_script(["python3 -u mozharness\\scripts\\raptor_ab_script.py"])


if __name__ == "__main__":
    main()
