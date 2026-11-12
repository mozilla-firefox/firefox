# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Tests for the android_taskgraph job handlers and transforms.
"""

import dataclasses
import os

import pytest
from android_taskgraph import job
from android_taskgraph.transforms import build_android_app, push_android_app
from android_taskgraph.worker_types import build_push_apk_payload
from mozunit import main
from taskgraph.config import load_graph_config
from taskgraph.task import Task
from taskgraph.transforms.base import TransformConfig

from gecko_taskgraph import GECKO
from gecko_taskgraph.test.conftest import FakeParameters

here = os.path.abspath(os.path.dirname(__file__))


@pytest.fixture(scope="module")
def config():
    graph_config = load_graph_config(os.path.join(GECKO, "taskcluster"))
    params = FakeParameters({
        "base_repository": "http://hg.example.com",
        "head_repository": "http://hg.example.com",
        "head_rev": "abcdef",
        "level": 1,
        "moz_build_date": "20260917000000",
        "project": "example",
    })
    return TransformConfig(
        "android_test", here, {}, params, {}, graph_config, write_artifacts=False
    )


def test_gradlew_uploads_build_metrics(config, monkeypatch):
    monkeypatch.setattr(job, "configure_taskdesc_for_run", lambda *args: None)
    task = {
        "run": {
            "using": "gradlew",
            "gradlew": ["clean", "assembleDebug"],
            "workdir": "/builds/worker",
        },
        "worker": {"implementation": "docker-worker"},
    }
    taskdesc = {"dependencies": {}}

    job.configure_gradlew(config, task, taskdesc)

    worker = taskdesc["worker"]
    assert worker["env"]["GRADLEW_ARGS"] == "clean assembleDebug"
    assert worker["env"]["BUILD_METRICS_DIR"] == job.BUILD_METRICS_DIR
    assert worker["env"]["GRADLE_FLAGS"] == "--offline --daemon"
    assert worker["env"]["GRADLE_USER_HOME"] == "/builds/worker/gradle-user-home"
    assert {
        "type": "directory",
        "name": "public/build/build-metrics",
        "path": job.BUILD_METRICS_DIR,
    } in worker["artifacts"]


@pytest.mark.parametrize(
    "kind,mozconfig,archs",
    [
        ("build", "android-x86_64/nightly", None),
        (
            "build-fat-aar",
            "android-arm/nightly-android-lints",
            "armeabi-v7a,arm64-v8a,x86_64",
        ),
    ],
)
def test_gradlew_geckoview_dependency(config, monkeypatch, kind, mozconfig, archs):
    monkeypatch.setattr(job, "configure_taskdesc_for_run", lambda *args: None)
    geckoview = Task(
        kind=kind,
        label="geckoview",
        attributes={},
        task={
            "payload": {
                "env": {
                    "MOZCONFIG": "mobile/android/config/mozconfigs/android-x86_64/nightly"
                }
            }
        },
    )
    config = dataclasses.replace(
        config, kind_dependencies_tasks={"geckoview": geckoview}
    )
    task = {
        "run": {
            "using": "gradlew",
            "gradlew": ["assembleDebug"],
            "workdir": "/builds/worker",
        },
        "worker": {"implementation": "docker-worker"},
    }
    taskdesc = {"dependencies": {"geckoview": "geckoview"}}

    job.configure_gradlew(config, task, taskdesc)

    env = taskdesc["worker"]["env"]
    assert env["MOZCONFIG"] == (
        "/builds/worker/checkouts/gecko/mobile/android/config/mozconfigs/" + mozconfig
    )
    assert env.get("MOZ_ANDROID_FAT_AAR_ARCHITECTURES") == archs


def test_track_build_times_labels_the_perfherder_series(config):
    tasks = [
        {
            "name": "fenix-debug",
            "run": {
                "gradle-build-type": "debug",
                "gradle-build-name": "debug",
                "track-build-times": True,
            },
        },
        {
            "name": "fenix-debug",
            "run": {
                "gradle-build-type": "debug",
                "gradle-build-name": "debug",
                "gradle-package-command": "bundle",
                "track-build-times": True,
            },
        },
        {
            "name": "fenix-nightly-firebase",
            "run": {"gradle-build-type": "nightly", "gradle-build-name": "nightly"},
        },
    ]

    tracked, bundle, untracked = build_android_app.build_gradle_command(config, tasks)

    assert tracked["run"]["gradlew"] == [
        "clean",
        "assembleDebug",
        "-PbuildMetricsPerfherderOptions=fenix-debug",
    ]
    assert "track-build-times" not in tracked["run"]
    assert bundle["run"]["gradlew"] == [
        "clean",
        "bundleDebug",
        "-PbuildMetricsPerfherderOptions=fenix-debug-bundle",
    ]
    assert untracked["run"]["gradlew"] == ["clean", "assembleNightly"]


@pytest.mark.parametrize(
    "gradle_build,build_type,build_name,expected_path",
    [
        ("fenix", "nightly", "nightly", "fenix/app/outputs/mapping/nightly"),
        ("fenix", "debug", "debug", "fenix/app/outputs/mapping/debug"),
        (
            "focus",
            "release",
            "focusRelease",
            "focus-android/app/outputs/mapping/focusRelease",
        ),
    ],
)
def test_mapping_published_for_each_variant(
    config, gradle_build, build_type, build_name, expected_path
):
    source_project_name = "fenix" if gradle_build == "fenix" else "focus-android"
    task = {
        "attributes": {},
        "run": {
            "gradle-build": gradle_build,
            "gradle-build-type": build_type,
            "gradle-build-name": build_name,
        },
        "source-project-name": source_project_name,
        "apk-artifact-template": {
            "type": "file",
            "name": "public/build/target.{abi}.apk",
            "path": "/{fileName}",
        },
    }

    [task] = build_android_app.add_artifacts(config, [task])

    assert {
        "type": "file",
        "name": "public/build/mapping.txt",
        "path": "/builds/worker/workspace/obj-build/gradle/build/mobile/android/"
        f"{expected_path}/mapping.txt",
    } in task["worker"]["artifacts"]


@pytest.mark.parametrize("store", ["samsung", "huawei", "vivo"])
@pytest.mark.parametrize("release_eta", ["2025-04-29T13:00:00.000Z", None, ""])
@pytest.mark.parametrize("level", ["1", "3"])
def test_push_android_scheduled_submit(
    run_transform, config, caplog, store, release_eta, level
):
    task = {
        "name": "fenix-release",
        "label": f"push-android-{store}-fenix-release",
        "attributes": {"build-type": "fenix-release"},
        "worker": {
            "target-store": store,
            "submit": {"by-level": {"3": True, "default": False}},
            "channel": "fenix-release",
            "commit": True,
            "product": "fenix",
            "dep": False,
            "upstream-artifacts": [],
        },
    }
    params = FakeParameters({"level": level, "release_eta": release_eta})
    [result] = run_transform(push_android_app.transforms, task, params=params)
    task_def = {"tags": {}, "scopes": []}
    build_push_apk_payload(config, result, task_def)
    payload = task_def["payload"]
    assert payload["submit"] == bool(level == "3" and release_eta)
    if level == "3" and release_eta:
        assert payload["scheduled_release_date"] == release_eta
    else:
        assert "scheduled_release_date" not in payload
    assert "rollout_percentage" not in payload
    if level == "3" and not release_eta:
        assert task["label"] in caplog.text
        assert "upload only" in caplog.text
    else:
        assert not caplog.records


def test_push_android_google_unchanged(run_transform, config):
    task = {
        "name": "fenix-release",
        "label": "push-android-google-fenix-release",
        "attributes": {"build-type": "fenix-release"},
        "worker": {
            "target-store": "google",
            "certificate-alias": "fenix-release",
            "channel": "fenix-release",
            "commit": True,
            "product": "fenix",
            "dep": False,
            "upstream-artifacts": [],
        },
    }
    params = FakeParameters({"level": "3", "release_eta": "2025-04-29T13:00:00.000Z"})
    [result] = run_transform(push_android_app.transforms, task, params=params)
    task_def = {"tags": {}, "scopes": []}
    build_push_apk_payload(config, result, task_def)
    assert task_def["payload"] == {
        "target_store": "google",
        "certificate_alias": "fenix-release",
        "channel": "fenix-release",
        "commit": True,
        "upstreamArtifacts": [],
    }


if __name__ == "__main__":
    main()
