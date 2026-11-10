# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import pytest
import yaml
from mozunit import main
from pytest_taskgraph import make_graph, make_task
from taskgraph import create
from taskgraph.util import json
from taskgraph.util.taskcluster import _task_definitions_cache

from gecko_taskgraph import decision
from gecko_taskgraph.actions import trigger_action_callback
from gecko_taskgraph.actions.confirm_failure import confirm_modifier

ROOT_URL = "https://taskcluster.example.com"


@pytest.fixture(autouse=True)
def mock_root_url(monkeypatch):
    monkeypatch.delenv("TASKCLUSTER_PROXY_URL", raising=False)
    monkeypatch.setenv("TASKCLUSTER_ROOT_URL", ROOT_URL)


@pytest.fixture(autouse=True)
def clear_caches():
    yield
    _task_definitions_cache.cache.clear()


@pytest.fixture
def artifact_dir(monkeypatch, tmp_path):
    artifact_dir = tmp_path / "artifacts"
    monkeypatch.setattr(decision, "ARTIFACTS_DIR", str(artifact_dir))
    return artifact_dir


@pytest.fixture
def get_artifact(artifact_dir):
    def inner(artifact_name):
        return json.loads((artifact_dir / artifact_name).read_text())

    return inner


@pytest.fixture
def run_action(mocker, monkeypatch, parameters, graph_config):
    monkeypatch.setattr(create, "testing", True)
    mocker.patch("gecko_taskgraph.actions.registry.sanity_check_task_scope")

    def inner(name, params=None, **kwargs):
        if params:
            parameters.update(params)

        kwargs.setdefault("task_group_id", "gid")
        kwargs.setdefault("task_id", "tid")
        kwargs.setdefault("input", None)
        ret = trigger_action_callback(
            callback=name,
            parameters=parameters,
            root=graph_config.root_dir,
            **kwargs,
        )
        return ret

    return inner


def test_cancel(responses, run_action):
    task_id = "abc"

    responses.post(f"{ROOT_URL}/api/queue/v1/task/{task_id}/cancel", status=200)

    run_action("cancel", task_id=task_id, input={"task_id": task_id})


def test_cancel_all(monkeypatch, responses, run_action):
    group_id = "abc"

    # Validate action task doesn't cancel itself.
    monkeypatch.setenv("TASK_ID", group_id)

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task-group/{group_id}/list",
        status=200,
        json={
            "tasks": [
                {"status": {"taskId": group_id, "state": "running"}},
                {"status": {"taskId": "a", "state": "running"}},
                {"status": {"taskId": "b", "state": "completed"}},
                {"status": {"taskId": "c", "state": "pending"}},
                {"status": {"taskId": "d", "state": "unscheduled"}},
            ]
        },
    )

    responses.post(f"{ROOT_URL}/api/queue/v1/task/a/cancel", status=200)
    responses.post(f"{ROOT_URL}/api/queue/v1/task/c/cancel", status=200)
    responses.post(f"{ROOT_URL}/api/queue/v1/task/d/cancel", status=200)

    run_action(
        "cancel-all",
        task_group_id=group_id,
        input={"task_group_id": group_id},
    )


def test_rebuild_cached_tasks(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="foo", attributes={"cached_task": True}, task_def={"name": "foo"}
        ),
        make_task(label="bar", task_def={"name": "bar"}),
    )
    m = mocker.patch(
        "gecko_taskgraph.actions.rebuild_cached_tasks.fetch_graph_and_labels"
    )
    m.return_value = (
        "gid",
        graph,
        {label: "tid" for label in graph.tasks.keys()},
        None,
    )

    run_action("rebuild-cached-tasks")
    to_run = get_artifact("to-run.json")
    assert "foo" in to_run
    assert "bar" not in to_run


def test_add_new_jobs(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(label="foo", task_def={"name": "foo"}),
        make_task(label="bar", task_def={"name": "bar"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.add_new_jobs.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("add-new-jobs", input={"tasks": ["foo"], "times": 1})

    to_run = get_artifact("to-run.json")
    assert "foo" in to_run
    assert "bar" not in to_run


def test_add_talos(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="test-linux-talos",
            attributes={"talos_try_name": "talos"},
            task_def={"name": "test-linux-talos"},
        ),
        make_task(label="build", task_def={"name": "build"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.add_talos.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)
    mocker.patch("gecko_taskgraph.actions.add_talos.standard_filter", return_value=True)

    run_action("run-all-talos", input={"times": 1})

    to_run = get_artifact("to-run.json")
    assert "test-linux-talos" in to_run
    assert "build" not in to_run


def test_purge_caches(responses, run_action):
    task_id = "abc"
    task_def = {
        "payload": {"cache": {"cache1": "path1"}},
        "provisionerId": "proj-gecko",
        "workerType": "linux",
    }

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    responses.post(
        f"{ROOT_URL}/api/purge-cache/v1/purge-cache/proj-gecko%2Flinux",
        status=200,
        json={},
    )

    run_action("purge-cache", task_id=task_id)


def test_openh264(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="openh264-build", kind="openh264", task_def={"name": "openh264-build"}
        ),
        make_task(label="build", kind="build", task_def={"name": "build"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.openh264.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("openh264")

    to_run = get_artifact("to-run.json")
    assert "openh264-build" in to_run
    assert "build" not in to_run


def test_googleplay(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="push-android-google-fenix-nightly",
            kind="push-android",
            attributes={"build-type": "fenix-nightly", "target-store": "google"},
            task_def={"name": "push-android-google-fenix-nightly"},
        ),
        make_task(
            label="push-android-samsung-fenix-release",
            kind="push-android",
            attributes={"build-type": "fenix-release", "target-store": "samsung"},
            task_def={"name": "push-android-samsung-fenix-release"},
        ),
        make_task(label="build", kind="build", task_def={"name": "build"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.googleplay.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("googleplay", params={"project": "mozilla-central"})

    to_run = get_artifact("to-run.json")
    assert "push-android-google-fenix-nightly" in to_run
    assert "push-android-samsung-fenix-release" not in to_run
    assert "build" not in to_run


def test_raptor_extra_options(mocker, responses, run_action, get_artifact):
    task_id = "tid"
    task_def = {
        "metadata": {"name": "test-raptor"},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "rap", "groupName": "Raptor"}},
    }
    graph = make_graph(make_task(label="test-raptor", task_def=task_def))

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    m = mocker.patch(
        "gecko_taskgraph.actions.raptor_extra_options.fetch_graph_and_labels"
    )
    m.return_value = ("gid", graph, {"test-raptor": "tid"}, None)

    run_action(
        "raptor-extra-options", task_id=task_id, input={"extra_options": "verbose"}
    )

    to_run = get_artifact("to-run.json")
    assert "test-raptor" in to_run


def test_run_missing_tests(mocker, responses, run_action, get_artifact):
    graph = make_graph(
        make_task(label="test-foo", kind="test", task_def={"name": "test-foo"}),
        make_task(label="test-bar", kind="test", task_def={"name": "test-bar"}),
        make_task(label="build", kind="build", task_def={"name": "build"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.run_missing_tests.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {"test-foo": "tid1"}, None)

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/gid/artifacts/public%2Ftarget-tasks.json",
        status=303,
        json={"url": f"{ROOT_URL}/artifacts/target-tasks.json"},
    )
    responses.get(
        f"{ROOT_URL}/artifacts/target-tasks.json",
        status=200,
        json={"test-foo": {}, "test-bar": {}},
    )

    run_action("run-missing-tests")

    to_run = get_artifact("to-run.json")
    assert "test-bar" in to_run
    assert "test-foo" not in to_run
    assert "build" not in to_run


def test_scriptworker_canary(mocker, run_action, graph_config):
    m = mocker.patch("gecko_taskgraph.actions.scriptworker_canary.taskgraph_decision")

    run_action("scriptworker-canary", input={"scriptworkers": ["balrog", "shipit"]})

    m.assert_called_once()
    args, kwargs = m.call_args
    assert args[0] == {"root": graph_config.root_dir}
    assert kwargs["parameters"]["target_tasks_method"] == "scriptworker_canary"
    assert kwargs["parameters"]["try_task_config"] == {
        "scriptworker-canary-workers": ["balrog", "shipit"]
    }
    assert kwargs["parameters"]["tasks_for"] == "action"


def test_merge_automation(mocker, run_action, graph_config):
    m = mocker.patch("gecko_taskgraph.actions.merge_automation.taskgraph_decision")

    run_action(
        "merge-automation",
        params={"project": "mozilla-central"},
        input={"behavior": "bump-main", "merge-automation-id": 123},
    )

    m.assert_called_once()
    args, kwargs = m.call_args
    assert args[0] == {"root": graph_config.root_dir}
    assert kwargs["parameters"]["target_tasks_method"] == "merge_automation"
    assert kwargs["parameters"]["merge_config"] == {
        "force-dry-run": False,
        "behavior": "bump-main",
        "merge-automation-id": 123,
    }
    assert kwargs["parameters"]["tasks_for"] == "action"


def test_retrigger(mocker, responses, run_action, get_artifact):
    task_def = {
        "metadata": {"name": "test-task"},
        "payload": {},
    }
    graph = make_graph(
        make_task(label="test-task", attributes={"retrigger": True}, task_def=task_def)
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/tid",
        status=200,
        json=task_def,
    )
    m = mocker.patch("gecko_taskgraph.actions.retrigger.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("retrigger", input={"force": True})

    to_run = get_artifact("to-run.json")
    assert "test-task" in to_run


def test_retrigger_custom(mocker, responses, run_action, capsys):
    task_def = {
        "metadata": {"name": "test-mochitest"},
        "payload": {"command": ["run"], "env": {}},
        "tags": {"test-type": "mochitest"},
        "extra": {"treeherder": {"symbol": "M"}},
    }
    graph = make_graph(make_task(label="test-mochitest", task_def=task_def))

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/tid",
        status=200,
        json=task_def,
    )
    m = mocker.patch("gecko_taskgraph.actions.retrigger_custom.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {"test-mochitest": "dtid"}, None)

    run_action("retrigger-custom", input={"path": "test/path"})

    captured = capsys.readouterr()
    assert "test-mochitest" in captured.out
    assert "--no-run-tests" in captured.out
    assert "test/path" in captured.out
    assert "M-custom" in captured.out


def test_create_interactive(mocker, responses, monkeypatch, run_action, get_artifact):
    monkeypatch.setenv("TASK_ID", "action-task-id")
    task_def = {
        "metadata": {"name": "test-task"},
        "payload": {
            "env": {},
            "maxRunTime": 3600,
            "cache": {},
            "artifacts": {},
        },
        "scopes": [],
        "extra": {"treeherder": {"symbol": "T"}},
    }
    graph = make_graph(make_task(label="test-task", task_def=task_def))

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/tid",
        status=200,
        json=task_def,
    )
    m = mocker.patch(
        "gecko_taskgraph.actions.create_interactive.fetch_graph_and_labels"
    )
    m.return_value = ("gid", graph, {}, None)

    run_action("create-interactive", input={"notify": "test@example.com"})

    to_run = get_artifact("to-run.json")
    assert "test-task" in to_run


def test_backfill_task(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(label="test-task", task_def={"name": "test-task"}),
    )
    m = mocker.patch("gecko_taskgraph.actions.backfill.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)
    mocker.patch("gecko_taskgraph.actions.backfill.combine_task_graph_files")

    run_action(
        "backfill-task",
        input={"label": "test-task", "revision": "abc123", "symbol": "T"},
    )

    to_run = get_artifact("to-run-0.json")
    assert "test-task" in to_run


def test_backfill_task_chunk_not_found(mocker, run_action, get_artifact):
    """When the original chunk doesn't exist in the target push, the backfill
    should still apply the original manifests via MOZHARNESS_TEST_PATHS."""
    task_def = {
        "name": "test-linux-wpt-1",
        "payload": {"env": {}},
        "metadata": {"name": "test-linux-wpt-1"},
        "extra": {"treeherder": {"symbol": "wpt1", "groupSymbol": "W"}},
        "tags": {},
    }
    graph = make_graph(
        make_task(label="test-linux-wpt-1", task_def=task_def),
    )
    m = mocker.patch("gecko_taskgraph.actions.backfill.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)
    mocker.patch("gecko_taskgraph.actions.backfill.combine_task_graph_files")

    original_manifests = {"web-platform-tests": ["/css/test1.html", "/dom/test2.html"]}
    run_action(
        "backfill-task",
        input={
            "label": "test-linux-wpt-9",
            "revision": "abc123",
            "symbol": "wpt9",
            "test_manifests": original_manifests,
        },
    )

    to_run = get_artifact("to-run-0.json")
    assert "test-linux-wpt-1" in to_run

    task_graph = get_artifact("task-graph-0.json")
    task_data = list(task_graph.values())[0]
    task_def = task_data.get("task", task_data)
    assert task_def["payload"]["env"]["MOZHARNESS_TEST_PATHS"] == json.dumps(
        original_manifests
    )
    assert task_def["metadata"]["name"] == "test-linux-wpt-9"
    assert task_def["extra"]["treeherder"]["symbol"] == "wpt9-abc123-bk"


def test_confirm_failures(mocker, responses, run_action, get_artifact):
    task_id = "test-task-id"
    task_def = {
        "metadata": {"name": "test-mochitest"},
        "extra": {"suite": "mochitest"},
        "payload": {"command": ["run-tests"], "env": {}},
    }
    graph = make_graph(
        make_task(
            label="test-mochitest-cf",
            task_def={
                "name": "test-mochitest-cf",
                "payload": {"command": ["run-tests"], "env": {}},
                "metadata": {"name": "test-mochitest-cf"},
                "tags": {},
            },
        ),
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/artifacts",
        status=200,
        json={
            "artifacts": [
                {"name": "public/logs/live_backing.log"},
                {"name": "public/logs/errorsummary.log"},
            ]
        },
    )

    errorsummary_content = b"\n".join([
        b'{"test": "dom/tests/test_example.html", "status": "FAIL", "expected": "PASS", "group": "dom/tests"}',
        b'{"test": "dom/tests/test_another.html", "status": "FAIL", "expected": "PASS", "group": "dom/tests"}',
    ])
    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/artifacts/public%2Flogs%2Ferrorsummary.log",
        status=303,
        json={"url": f"{ROOT_URL}/artifacts/errorsummary.log"},
    )
    responses.get(
        f"{ROOT_URL}/artifacts/errorsummary.log",
        status=200,
        body=errorsummary_content,
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    m = mocker.patch("gecko_taskgraph.actions.confirm_failure.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("confirm-failures", task_id=task_id)

    to_run = get_artifact("to-run.json")
    assert "test-mochitest-cf" in to_run


def test_confirm_failures_retrigger(mocker, responses, run_action):
    task_id = "test-task-id"
    task_def = {
        "metadata": {"name": "test-mochitest"},
        "extra": {"suite": "mochitest"},
    }
    graph = make_graph(
        make_task(
            label="test-mochitest",
            attributes={"retrigger": True},
            task_def={"name": "test-mochitest"},
        ),
        make_task(label="test-mochitest-cf", task_def={"name": "test-mochitest-cf"}),
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/artifacts",
        status=200,
        json={"artifacts": [{"name": "public/logs/live_backing.log"}]},
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    m = mocker.patch("gecko_taskgraph.actions.confirm_failure.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)
    retrigger_mock = mocker.patch(
        "gecko_taskgraph.actions.confirm_failure.retrigger_action"
    )

    run_action("confirm-failures", task_id=task_id)

    retrigger_mock.assert_called_once()


def test_confirm_modifier_tags_partial_manifests():
    task = make_task(
        label="test-mochitest-cf",
        task_def={
            "payload": {"command": ["run-tests"], "env": {}},
            "metadata": {"name": "test-mochitest-cf"},
            "tags": {},
        },
    )

    confirm_modifier(
        task,
        input={
            "label": "test-mochitest-cf",
            "suite": "mochitest-plain",
            "test_path": "dom/tests/test_example.html",
            "test_group": "dom/tests/mochitest.toml",
        },
    )

    assert task.task["tags"]["action"] == "confirm-failure"
    assert task.task["tags"]["manifests"] == "partial"


def test_rerun(mocker, responses, run_action):
    task_id = "tid"
    task_def = {"metadata": {"name": "test-task"}}

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/status",
        status=200,
        json={"status": {"state": "failed"}},
    )
    responses.post(f"{ROOT_URL}/api/queue/v1/task/{task_id}/rerun", status=200)

    graph = make_graph(make_task(label="test-task", task_def=task_def))
    m = mocker.patch("gecko_taskgraph.actions.retrigger.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {"test-task": [task_id]}, {"test-task": [task_id]})

    run_action("rerun", task_id=task_id)


def test_retrigger_decision(responses, run_action, capsys):
    task_def = {
        "taskGroupId": "tgid",
        "schedulerId": "scheduler",
        "provisionerId": "provisioner",
        "workerType": "worker",
        "created": "2024-01-01T00:00:00.000Z",
        "deadline": "2024-01-01T01:00:00.000Z",
        "expires": "2024-01-02T00:00:00.000Z",
        "metadata": {"name": "decision-task"},
        "payload": {},
        "tags": {},
        "extra": {},
    }

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/tid",
        status=200,
        json=task_def,
    )

    run_action("retrigger-decision", params={"level": "1"})

    captured = capsys.readouterr()
    assert "decision-task" in captured.out
    assert "gecko-level-1" in captured.out
    assert "retrigger-decision-task" in captured.out


def test_retrigger_multiple(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="test-task",
            attributes={"retrigger": True},
            task_def={"name": "test-task"},
        ),
    )

    m = mocker.patch("gecko_taskgraph.actions.retrigger.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, {"test-task": ["tid"]})

    run_action(
        "retrigger-multiple",
        input={"requests": [{"tasks": ["test-task"], "times": 2}]},
    )

    to_run = get_artifact("to-run.json")
    assert "test-task" in to_run


def test_retrigger_multiple_rerun(mocker, responses, run_action):
    task_id = "rerun-task-id"
    graph = make_graph(
        make_task(
            label="test-task",
            attributes={"retrigger": False},
            task_def={"name": "test-task"},
        ),
    )

    m = mocker.patch("gecko_taskgraph.actions.retrigger.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, {"test-task": [task_id]})

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/status",
        status=200,
        json={"status": {"state": "failed"}},
    )
    responses.post(f"{ROOT_URL}/api/queue/v1/task/{task_id}/rerun", status=200)

    run_action(
        "retrigger-multiple",
        input={"requests": [{"tasks": ["test-task"], "times": 2}]},
    )


def test_add_all_browsertime(mocker, run_action, get_artifact):
    graph = make_graph(
        make_task(
            label="raptor-browsertime",
            kind="test",
            attributes={
                "raptor_try_name": "browsertime-firefox",
                "test_platform": "linux64-shippable-qr/opt",
                "run_on_projects": ["mozilla-central"],
            },
            task_def={"name": "raptor-browsertime", "extra": {"suite": "raptor"}},
        ),
        make_task(label="build", kind="build", task_def={"name": "build"}),
    )

    m = mocker.patch("gecko_taskgraph.actions.backfill.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action("add-all-browsertime", params={"project": "mozilla-central"})

    to_run = get_artifact("to-run.json")
    assert "raptor-browsertime" in to_run
    assert "build" not in to_run


@pytest.mark.parametrize(
    "test_platform,node_toolchain,has_node",
    (
        pytest.param(
            "linux2404-64-shippable/opt", "linux64-node-24", False, id="linux"
        ),
        pytest.param(
            "macosx1500-aarch64-shippable/opt",
            "macosx64-aarch64-node-24",
            False,
            id="macosx-aarch64",
        ),
        pytest.param(
            "macosx1470-64-shippable/opt", "macosx64-node-22", False, id="macosx"
        ),
        pytest.param(
            "windows11-64-24h2-shippable/opt",
            "win64-node-24",
            True,
            id="windows-existing-node",
        ),
        # Backfilled pushes may predate the current node version.
        pytest.param(
            "linux2404-64-shippable/opt",
            "linux64-node-22",
            False,
            id="linux-older-graph",
        ),
    ),
)
def test_gecko_profile(
    mocker,
    responses,
    run_action,
    get_artifact,
    test_platform,
    node_toolchain,
    has_node,
):
    task_id = "tid"
    fetches = []
    if has_node:
        fetches.append({
            "artifact": "public/build/node.tar.zst",
            "extract": True,
            "task": f"<toolchain-{node_toolchain}>",
        })
    task_def = {
        "metadata": {"name": "test-raptor"},
        "payload": {
            "command": [["run-tests"]],
            "env": {"MOZ_FETCHES": {"task-reference": json.dumps(fetches)}},
        },
        "extra": {
            "suite": "raptor",
            "treeherder": {"symbol": "R", "groupName": "Raptor"},
        },
    }
    toolchain_platform = node_toolchain.rsplit("-node-", 1)[0]
    toolchain_labels = ["profiler-node-tools", f"{toolchain_platform}-samply"]
    tasks = [
        make_task(
            label="test-raptor",
            kind="test",
            attributes={"unittest_suite": "raptor", "test_platform": test_platform},
            task_def={
                "name": "test-raptor",
                "deadline": "",
                "payload": {
                    "command": [["run-tests"]],
                    "env": {"MOZ_FETCHES": {"task-reference": json.dumps(fetches)}},
                },
                "extra": {
                    "suite": "raptor",
                    "treeherder": {"symbol": "R", "groupName": "Raptor"},
                },
            },
        )
    ]
    tasks.extend(
        make_task(
            label=f"toolchain-{name}",
            kind="toolchain",
            task_def={"name": f"toolchain-{name}"},
        )
        for name in toolchain_labels
    )
    node_label = f"toolchain-{node_toolchain}"
    tasks.append(
        make_task(
            label=node_label,
            kind="toolchain",
            attributes={"toolchain-alias": [f"{toolchain_platform}-node"]},
            task_def={"name": node_label},
        )
    )
    if has_node:
        tasks.append(("test-raptor", node_label, node_label))
    graph = make_graph(*tasks)

    responses.get(
        "http://hg.example.com/json-pushes?version=2&startID=99&endID=100",
        status=200,
        json={"pushes": {"100": {"changesets": ["abc123"]}}},
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )

    responses.get(
        f"{ROOT_URL}/api/index/v1/task/gecko.v2.some-project.pushlog-id.100.decision/artifacts/public%2Fparameters.yml",
        status=303,
        headers={"Location": f"{ROOT_URL}/artifacts/gecko-profile-params.yml"},
    )
    responses.get(
        f"{ROOT_URL}/artifacts/gecko-profile-params.yml",
        status=200,
        body=yaml.dump({"pushlog_id": "100", "project": "autoland", "level": "1"}),
        content_type="application/x-yaml",
    )
    m = mocker.patch("gecko_taskgraph.actions.gecko_profile.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {"test-raptor": "tid"}, None)
    mocker.patch("gecko_taskgraph.actions.gecko_profile.combine_task_graph_files")

    run_action(
        "geckoprofile",
        task_id=task_id,
        params={"pushlog_id": "100", "head_repository": "http://hg.example.com"},
        input={"depth": 1, "gecko_profile_interval": 5},
    )

    to_run = get_artifact("to-run-100.json")
    assert "test-raptor" in to_run

    result_graph = get_artifact("task-graph-100.json")
    labels = {task_id: task["label"] for task_id, task in result_graph.items()}
    result = next(t for t in result_graph.values() if t["label"] == "test-raptor")
    node_fetches = [
        labels[f["task"]]
        for f in json.loads(result["task"]["payload"]["env"]["MOZ_FETCHES"])
        if f["artifact"] == "public/build/node.tar.zst"
    ]
    assert node_fetches == [node_label]


def test_side_by_side(mocker, responses, run_action, get_artifact):
    task_id = "tid"
    task_def = {
        "metadata": {"name": "linux/opt-browsertime-tp6"},
        "extra": {"treeherder": {"symbol": "tp6"}},
        "payload": {"command": [["run"], ["perf-test {test_name}"]]},
    }
    graph = make_graph(
        make_task(
            label="perftest-linux-side-by-side",
            task_def={
                "name": "perftest-linux-side-by-side",
                "payload": {"command": [["run"], ["perf-test {test_name}"]]},
                "extra": {"treeherder": {"symbol": "sxs"}},
                "metadata": {"name": "perftest-linux-side-by-side"},
            },
        )
    )

    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}",
        status=200,
        json=task_def,
    )
    m = mocker.patch("gecko_taskgraph.actions.side_by_side.fetch_graph_and_labels")
    m.return_value = ("gid", graph, {}, None)

    run_action(
        "side-by-side",
        task_id=task_id,
        params={"head_rev": "newrev123", "pushlog_id": "100"},
        input={"revision": "baserev456", "project": "autoland"},
    )

    to_run = get_artifact("to-run.json")
    assert "perftest-linux-side-by-side" in to_run


DECISION_PARAMETERS = {
    "base_repository": "http://hg.example.com",
    "head_repository": "http://hg.example.com",
    "head_rev": "abcdef",
    "project": "try",
    "level": "1",
    "pushlog_id": "100",
    "release_product": "firefox",
    "release_type": "nightly",
}


PARTNER_DECISION_PARAMETERS = DECISION_PARAMETERS | {"release_type": "release"}

# Only en-US: fix_partner_config() intersects locales with l10n-changesets.json,
# whose contents vary by branch and get trimmed altogether by `mach try release`.
# en-US is the one locale it always appends.
PARTNER_CONFIG = {
    "release-partner-repack": {
        "acme": {"acme-001": {"locales": ["en-US"], "platforms": ["win64-shippable"]}}
    }
}


def mock_artifact(responses, task_id, name, **kwargs):
    responses.get(
        f"{ROOT_URL}/api/queue/v1/task/{task_id}/artifacts/public%2F{name}",
        status=303,
        json={"url": f"{ROOT_URL}/artifacts/{task_id}-{name}"},
    )
    responses.get(f"{ROOT_URL}/artifacts/{task_id}-{name}", status=200, **kwargs)


def mock_parameters_artifact(responses, task_id, parameters):
    mock_artifact(
        responses,
        task_id,
        "parameters.yml",
        body=yaml.dump(parameters),
        content_type="application/x-yaml",
    )


def mock_full_task_graph_artifact(responses, task_id, graph=None):
    mock_artifact(responses, task_id, "full-task-graph.json", json=graph or {})


def mock_empty_task_group(responses, action_task_id):
    responses.get(
        f"{ROOT_URL}/api/queue/v1/task-group/{action_task_id}/list",
        status=200,
        json={
            "tasks": [
                {"status": {"taskId": action_task_id, "state": "running"}},
            ]
        },
    )


def test_release_promotion(
    mocker, monkeypatch, responses, run_action, parameters, graph_config
):
    m = mocker.patch("gecko_taskgraph.actions.release_promotion.taskgraph_decision")

    action_task_id = "action-task-id"
    monkeypatch.setenv("TASK_ID", action_task_id)

    responses.get(
        f"{ROOT_URL}/api/index/v1/task/gecko.v2.try.revision.abcdef.taskgraph.decision",
        status=200,
        json={"taskId": "decision-task-id"},
    )

    mock_parameters_artifact(responses, "decision-task-id", DECISION_PARAMETERS)
    mock_full_task_graph_artifact(responses, "decision-task-id")
    mock_empty_task_group(responses, action_task_id)

    mocker.patch(
        "gecko_taskgraph.actions.release_promotion.find_existing_tasks_from_previous_kinds",
        return_value={},
    )

    run_action(
        "release-promotion",
        params={
            "project": "try",
            "level": "1",
        },
        input={
            "release_promotion_flavor": "promote_firefox",
            "build_number": 1,
            "version": "",
            "partial_updates": {},
            "release_enable_partner_repack": False,
            "release_enable_partner_attribution": False,
            "release_enable_emefree": False,
        },
    )

    m.assert_called_once()
    args, kwargs = m.call_args
    assert args[0] == {"root": graph_config.root_dir}
    assert kwargs["parameters"]["target_tasks_method"] == "promote_desktop"


def run_partner_release_promotion(
    mocker,
    monkeypatch,
    responses,
    run_action,
    flavor,
    previous_graph_ids,
    previous_parameters,
):
    taskgraph_decision = mocker.patch(
        "gecko_taskgraph.actions.release_promotion.taskgraph_decision"
    )
    mocker.patch("gecko_taskgraph.actions.release_promotion.get_token")
    get_partner_config = mocker.patch(
        "gecko_taskgraph.actions.release_promotion.get_partner_config",
        return_value=PARTNER_CONFIG,
    )
    mocker.patch(
        "gecko_taskgraph.actions.release_promotion.find_existing_tasks_from_previous_kinds",
        return_value={},
    )

    action_task_id = "action-task-id"
    monkeypatch.setenv("TASK_ID", action_task_id)
    mock_empty_task_group(responses, action_task_id)

    for graph_id in previous_graph_ids:
        mock_full_task_graph_artifact(responses, graph_id)
    # Only the graphs the action is expected to read: `responses` fails the test
    # on both an unregistered request and a registered one that never fires.
    for graph_id, extra_parameters in previous_parameters.items():
        mock_parameters_artifact(
            responses, graph_id, PARTNER_DECISION_PARAMETERS | extra_parameters
        )

    run_action(
        "release-promotion",
        params={"project": "try", "level": "1"},
        input={
            "release_promotion_flavor": flavor,
            "build_number": 1,
            "version": "",
            "partial_updates": {},
            "previous_graph_ids": previous_graph_ids,
            "release_enable_partner_repack": True,
            "release_enable_partner_attribution": False,
            "release_enable_emefree": False,
        },
    )

    taskgraph_decision.assert_called_once()
    _, kwargs = taskgraph_decision.call_args
    return get_partner_config, kwargs["parameters"]


def test_release_promotion_reuses_previous_partner_config(
    mocker, monkeypatch, responses, run_action, parameters
):
    get_partner_config, decision_parameters = run_partner_release_promotion(
        mocker,
        monkeypatch,
        responses,
        run_action,
        flavor="push_firefox",
        previous_graph_ids=["decision-task-id", "promote-task-id"],
        previous_parameters={
            "decision-task-id": {},
            "promote-task-id": {"release_partner_config": PARTNER_CONFIG},
        },
    )

    get_partner_config.assert_not_called()
    assert decision_parameters["release_partner_config"] == PARTNER_CONFIG


def test_release_promotion_looks_up_partner_config_without_a_previous_one(
    mocker, monkeypatch, responses, run_action, parameters
):
    get_partner_config, decision_parameters = run_partner_release_promotion(
        mocker,
        monkeypatch,
        responses,
        run_action,
        flavor="promote_firefox",
        previous_graph_ids=["decision-task-id"],
        previous_parameters={"decision-task-id": {}},
    )

    get_partner_config.assert_called_once()
    assert decision_parameters["release_partner_config"] == PARTNER_CONFIG


def test_release_promotion_partner_repack_flavor_refreshes_partner_config(
    mocker, monkeypatch, responses, run_action, parameters
):
    # No mock for the promote graph's parameters.yml: this flavor must not read
    # it, and `responses` refuses any request it does not recognise.
    get_partner_config, _ = run_partner_release_promotion(
        mocker,
        monkeypatch,
        responses,
        run_action,
        flavor="promote_firefox_partner_repack",
        previous_graph_ids=["decision-task-id", "promote-task-id"],
        previous_parameters={"decision-task-id": {}},
    )

    get_partner_config.assert_called_once()


def test_backfill_standard(mocker, run_action):
    task_id = "tid"
    task_def = {
        "metadata": {"name": "raptor-browsertime-firefox-tp6"},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "tp6"}},
    }
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_task_definition",
        return_value=task_def,
    )

    pushes = ["200", "201", "202"]
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_pushes_from_params_input",
        return_value=pushes,
    )
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_decision_task_id",
        side_effect=lambda project, push_id: f"decision-{push_id}",
    )
    trigger_mock = mocker.patch("gecko_taskgraph.actions.backfill.trigger_action")

    run_action(
        "backfill",
        task_id=task_id,
        input={"depth": 25, "retrigger": False},
    )

    trigger_mock.assert_has_calls(
        [
            mocker.call(
                action_name="backfill-task",
                decision_task_id="decision-200",
                input=mocker.ANY,
            ),
            mocker.call(
                action_name="backfill-task",
                decision_task_id="decision-201",
                input=mocker.ANY,
            ),
            mocker.call(
                action_name="backfill-task",
                decision_task_id="decision-202",
                input=mocker.ANY,
            ),
        ],
        any_order=False,
    )
    assert trigger_mock.call_count == 3


def test_backfill_sliced(mocker, run_action):
    task_id = "tid"
    label = "raptor-browsertime-firefox-tp6"
    task_def = {
        "metadata": {"name": label},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "tp6"}},
    }
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_task_definition",
        return_value=task_def,
    )

    pushes = [str(i) for i in range(100, 125)]
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_pushes_in_gap",
        return_value=pushes,
    )
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_decision_task_id",
        side_effect=lambda project, push_id: f"decision-{push_id}",
    )
    trigger_mock = mocker.patch("gecko_taskgraph.actions.backfill.trigger_action")

    run_action(
        "backfill",
        task_id=task_id,
        input={"depth": 25, "retrigger": False, "slices": 3},
    )

    # slices=3 triggers only the 3 exact pivot pushes: n/4, 2n/4, 3n/4
    # n=25, so pivot indices are 6, 12, 18 -> push IDs 106, 112, 118
    assert trigger_mock.call_count == 3
    called_pids = []
    for call in trigger_mock.call_args_list:
        kwargs = call.kwargs
        assert kwargs["action_name"] == "backfill-task"
        decision = kwargs["decision_task_id"]  # decision-<pid>
        called_pids.append(decision.split("-")[1])

    # ensure no duplicate pushes are triggered
    assert len(called_pids) == len(set(called_pids))
    assert set(called_pids) == {"106", "112", "118"}


def test_backfill_sliced_small_gap_falls_back_to_standard(mocker, run_action):
    task_id = "tid"
    label = "raptor-browsertime-firefox-tp6"
    task_def = {
        "metadata": {"name": label},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "tp6"}},
    }
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_task_definition",
        return_value=task_def,
    )

    # gap=6 (< 7) should trigger all 6 pushes via standard mode
    pushes = [str(i) for i in range(100, 106)]
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_pushes_in_gap",
        return_value=pushes,
    )
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_decision_task_id",
        side_effect=lambda project, push_id: f"decision-{push_id}",
    )
    trigger_mock = mocker.patch("gecko_taskgraph.actions.backfill.trigger_action")

    run_action(
        "backfill",
        task_id=task_id,
        input={"depth": 25, "retrigger": False, "slices": 3},
    )

    # standard mode should be triggered for all gap 6 pushes
    assert trigger_mock.call_count == 6
    called_pids = []
    for call in trigger_mock.call_args_list:
        kwargs = call.kwargs
        assert kwargs["action_name"] == "backfill-task"
        decision = kwargs["decision_task_id"]  # decision-<pid>
        called_pids.append(decision.split("-")[1])

    # ensure no duplicate pushes are triggered
    assert len(called_pids) == len(set(called_pids))
    assert set(called_pids) == {str(i) for i in range(100, 106)}


def test_backfill_sliced_empty_gap(mocker, run_action):
    task_id = "tid"
    label = "raptor-browsertime-firefox-tp6"
    task_def = {
        "metadata": {"name": label},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "tp6"}},
    }
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_task_definition",
        return_value=task_def,
    )

    # no missing pushes found
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_pushes_in_gap",
        return_value=[],
    )
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_decision_task_id",
        side_effect=lambda project, push_id: f"decision-{push_id}",
    )
    trigger_mock = mocker.patch("gecko_taskgraph.actions.backfill.trigger_action")

    run_action(
        "backfill",
        task_id=task_id,
        input={"depth": 25, "retrigger": False, "slices": 3},
    )

    # No pushes should be triggered gap is 0 (no missing pushes found)
    assert trigger_mock.call_count == 0


def test_backfill_sliced_boundary_gap(mocker, run_action):
    task_id = "tid"
    label = "raptor-browsertime-firefox-tp6"
    task_def = {
        "metadata": {"name": label},
        "payload": {"env": {}},
        "extra": {"treeherder": {"symbol": "tp6"}},
    }
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_task_definition",
        return_value=task_def,
    )

    # gap=7 (>= 7) should use sliced mode
    pushes = [str(i) for i in range(100, 107)]
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_pushes_in_gap",
        return_value=pushes,
    )
    mocker.patch(
        "gecko_taskgraph.actions.backfill.get_decision_task_id",
        side_effect=lambda project, push_id: f"decision-{push_id}",
    )
    trigger_mock = mocker.patch("gecko_taskgraph.actions.backfill.trigger_action")

    run_action(
        "backfill",
        task_id=task_id,
        input={"depth": 25, "retrigger": False, "slices": 3},
    )

    # slices=3 triggers only the 3 exact pivot pushes: n/4, 2n/4, 3n/4
    # n=7, so pivot indices are 1, 3, 5 -> push IDs 101, 103, 105
    assert trigger_mock.call_count == 3
    called_pids = []
    for call in trigger_mock.call_args_list:
        kwargs = call.kwargs
        assert kwargs["action_name"] == "backfill-task"
        decision = kwargs["decision_task_id"]  # decision-<pid>
        called_pids.append(decision.split("-")[1])

    # ensure no duplicate pushes are triggered
    assert len(called_pids) == len(set(called_pids))
    assert set(called_pids) == {"101", "103", "105"}


def _bhr_graph():
    return make_graph(
        make_task(
            label="bhr-aggregate-cron",
            task_def={
                "payload": {
                    "env": {
                        "BHR_AGGREGATE_DATE_OFFSET_DAYS": "4",
                        "BHR_AGGREGATE_SAMPLE_SIZE": "0.5",
                    }
                },
                "extra": {"treeherder": {"symbol": "BHR"}},
                "routes": [
                    "index.gecko.v2.mozilla-central.latest.firefox.bhr-aggregate",
                    "index.gecko.v2.mozilla-central.pushdate.2026.09.02.latest"
                    ".firefox.bhr-aggregate",
                    "tc-treeherder.v2.mozilla-central.abcdef",
                ],
            },
        ),
    )


@pytest.fixture
def run_bhr_action(mocker, run_action, get_artifact):
    def inner(action_input):
        graph = _bhr_graph()
        m = mocker.patch("gecko_taskgraph.actions.bhr_aggregate.fetch_graph_and_labels")
        m.return_value = (
            "gid",
            graph,
            {label: "tid" for label in graph.tasks.keys()},
            None,
        )
        run_action("bhr-aggregate", input=action_input)
        # task-graph.json is keyed by taskid, so find our task by its label.
        task_graph = get_artifact("task-graph.json")
        entries = [e for e in task_graph.values() if e["label"] == "bhr-aggregate-cron"]
        assert len(entries) == 1
        return entries[0]["task"]

    return inner


def test_bhr_aggregate_pins_date_and_sample_size(run_bhr_action):
    task = run_bhr_action({"date": "20260401", "sample_size": 0.02})
    env = task["payload"]["env"]
    assert env["BHR_AGGREGATE_DATE"] == "20260401"
    assert env["BHR_AGGREGATE_SAMPLE_SIZE"] == "0.02"
    # The offset stays, but the script prefers an explicit date over it.
    assert env["BHR_AGGREGATE_DATE_OFFSET_DAYS"] == "4"
    assert task["extra"]["treeherder"]["symbol"] == "BHR-custom"


def test_bhr_aggregate_empty_input_keeps_cron_defaults(run_bhr_action):
    task = run_bhr_action({})
    env = task["payload"]["env"]
    assert "BHR_AGGREGATE_DATE" not in env
    assert env["BHR_AGGREGATE_SAMPLE_SIZE"] == "0.5"


def test_bhr_aggregate_accepts_either_field_alone(run_bhr_action):
    env = run_bhr_action({"sample_size": 0.001})["payload"]["env"]
    assert "BHR_AGGREGATE_DATE" not in env
    assert env["BHR_AGGREGATE_SAMPLE_SIZE"] == "0.001"

    env = run_bhr_action({"date": "20260401"})["payload"]["env"]
    assert env["BHR_AGGREGATE_DATE"] == "20260401"
    assert env["BHR_AGGREGATE_SAMPLE_SIZE"] == "0.5"


def test_bhr_aggregate_publishes_under_the_build_date(run_bhr_action):
    task = run_bhr_action({"date": "20260802"})
    assert task["routes"] == [
        "tc-treeherder.v2.mozilla-central.abcdef",
        "index.gecko.v2.mozilla-central.bhr-aggregate.build.20260802",
    ]


def test_bhr_aggregate_never_takes_the_crons_index_routes(run_bhr_action):
    # Without a date there is no build date to publish under, so the run is
    # reachable by task id alone rather than displacing the day's real run.
    task = run_bhr_action({"sample_size": 0.01})
    assert task["routes"] == ["tc-treeherder.v2.mozilla-central.abcdef"]


def test_bhr_aggregate_leaves_the_timeseries_alone(run_bhr_action):
    for action_input in ({"date": "20260802"}, {"sample_size": 0.01}, {}):
        env = run_bhr_action(action_input)["payload"]["env"]
        assert env["BHR_SKIP_TIMESERIES"] == "1"


def test_bhr_aggregate_refill_keeps_the_crons_routes_and_roll_up(run_bhr_action):
    task = run_bhr_action({"refill_dates": ["20260816", "20260817"]})
    env = task["payload"]["env"]
    assert env["BHR_TIMESERIES_REFILL_DATES"] == "20260816,20260817"
    assert "BHR_SKIP_TIMESERIES" not in env
    # It replaces the day's run rather than sitting beside it, so it publishes
    # where the dashboard and the next cron run look.
    assert (
        "index.gecko.v2.mozilla-central.latest.firefox.bhr-aggregate" in task["routes"]
    )
    assert task["extra"]["treeherder"]["symbol"] == "BHR-custom"


def test_bhr_aggregate_refuses_a_pinned_date_with_a_refill(run_bhr_action):
    with pytest.raises(Exception, match="cannot be combined"):
        run_bhr_action({"date": "20260802", "refill_dates": ["20260816"]})


if __name__ == "__main__":
    main()
