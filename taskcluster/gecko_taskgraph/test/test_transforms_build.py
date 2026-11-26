# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from gecko_taskgraph.test.conftest import FakeParameters
from gecko_taskgraph.transforms.build import (
    collapse_unified_builds,
    disable_clang_plugins,
)
from gecko_taskgraph.transforms.build import use_artifact as use_artifact_transform


def unified_build():
    return {
        "name": "macosx64-shippable/opt",
        "attributes": {"shippable": True},
        "index": {"job-name": "macosx64-opt"},
        "worker": {"env": {}, "max-run-time": 1800},
        "mozconfig": None,
        "run": {"job-script": "taskcluster/scripts/misc/unify.sh"},
        "dependencies": {
            "x64": "build-macosx64-x64-shippable/opt",
            "aarch64": "build-macosx64-aarch64-shippable/opt",
        },
        "fetches": {
            "x64": [{"artifact": "target.dmg", "dest": "x64"}],
            "aarch64": [{"artifact": "target.dmg", "dest": "aarch64"}],
        },
    }


def opt_build():
    return {
        "name": "build-linux64/opt",
        "worker": {"env": {}},
    }


def debug_build():
    dbg_build = opt_build()
    platform, _, kind = dbg_build["name"].rpartition("/")
    assert kind == "opt"
    dbg_build["name"] = "/".join((platform, "debug"))
    return dbg_build


def per_arch_build(arch, package_tests=True):
    env = {"MOZ_AUTOMATION_PACKAGE_TESTS": "1"} if package_tests else {}
    return {
        "name": f"macosx64-{arch}-shippable/opt",
        "attributes": {},
        "index": {"job-name": f"macosx64-{arch}-opt"},
        "worker": {"env": env, "max-run-time": 7200},
        "mozconfig": {"variant": "nightly"},
        "run": {"config": ["builds/releng_base_mac_64_cross_builds.py"]},
        "fetches": {"toolchain": ["linux64-clang"]},
    }


def run(run_transform, use_artifact, package_tests=True):
    jobs = [
        per_arch_build("x64", package_tests),
        per_arch_build("aarch64", package_tests),
        unified_build(),
    ]
    params = FakeParameters({
        "try_task_config": {"use-artifact-builds": use_artifact},
    })
    return list(
        run_transform(collapse_unified_builds, jobs, kind="build", params=params)
    )[-1]


def test_collapse_unified_build(run_transform):
    job = run(run_transform, use_artifact=True)
    assert "dependencies" not in job
    assert "job-script" not in job["run"]
    assert job["run"]["config"] == ["builds/releng_base_mac_64_cross_builds.py"]
    assert job["fetches"] == {"toolchain": ["linux64-clang"]}
    assert job["worker"]["env"]["MOZ_AUTOMATION_PACKAGE_TESTS"] == "1"
    assert job["worker"]["max-run-time"] == 7200
    assert job["mozconfig"] == {"variant": "nightly"}


def run_disable_clang_plugins(run_transform):
    jobs = [
        opt_build(),
        debug_build(),
    ]
    return list(run_transform(disable_clang_plugins, jobs, kind="build"))


def test_disable_clang_plugins(run_transform):
    jobs = run_disable_clang_plugins(run_transform)
    for job in jobs:
        worker_env = job["worker"]["env"]
        if job["name"].endswith("/opt"):
            assert "DISABLE_CLANG_PLUGIN" in worker_env, job
        elif job["name"].endswith("/debug"):
            assert "DISABLE_CLANG_PLUGIN" not in worker_env, job
        else:
            assert False, "unsupported configuration for that test"


@pytest.mark.parametrize(
    "use_artifact,package_tests",
    (
        pytest.param(False, True, id="artifact-builds-disabled"),
        pytest.param(True, False, id="tests-not-packaged"),
    ),
)
def test_unified_build_left_alone(run_transform, use_artifact, package_tests):
    job = run(run_transform, use_artifact, package_tests)
    assert job == unified_build()


def linux_build():
    return {
        "name": "linux64/opt",
        "attributes": {},
        "index": {"job-name": "linux64-opt"},
        "treeherder": {"symbol": "B"},
        "worker": {"env": {"MOZ_AUTOMATION_PACKAGE_TESTS": "1"}},
        "use-sccache": True,
        "fetches": {
            "toolchain": [
                "linux64-clang",
                "linux64-rust",
                "linux64-rust-size",
                "linux64-node",
                "linux64-sccache",
                "sysroot-x86_64-linux-gnu",
                "onnxruntime-x86_64-linux-gnu",
            ],
            "fetch": ["windows-rs"],
        },
    }


@pytest.mark.parametrize("use_artifact", (True, False))
def test_use_artifact_toolchains(run_transform, use_artifact):
    params = FakeParameters({
        "try_task_config": {"use-artifact-builds": use_artifact},
    })
    [job] = run_transform(
        use_artifact_transform, [linux_build()], kind="build", params=params
    )
    if use_artifact:
        assert job["use-sccache"] is False
        assert job["fetches"]["toolchain"] == ["linux64-node"]
        assert job["fetches"]["fetch"] == []
        assert job["worker"]["env"]["USE_ARTIFACT"] == "1"
    else:
        assert job == linux_build()


if __name__ == "__main__":
    main()
