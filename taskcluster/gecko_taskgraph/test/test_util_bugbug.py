# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import os
import sys
import threading
import time

import mozunit
import pytest
from taskgraph.util import json

from gecko_taskgraph.util import bugbug
from gecko_taskgraph.util.bugbug import (
    BUGBUG_BASE_URL,
    PUSH_SCHEDULES_ENV,
    BugbugTimeoutException,
    get_session,
    push_perf_risk,
    push_schedules,
    start_push_schedules,
)


@pytest.mark.skipif(not hasattr(os, "fork"), reason="requires os.fork")
def test_forked_child_gets_fresh_session():
    session = get_session()
    pid = os.fork()
    if pid == 0:
        # Always exit, so that the child never returns into pytest.
        code = 1
        try:
            code = 0 if get_session() is not session else 1
        finally:
            os._exit(code)
    _, status = os.waitpid(pid, 0)
    assert os.waitstatus_to_exitcode(status) == 0
    assert get_session() is session


def test_group_translation(responses):
    push_schedules.cache_clear()
    branch = ("integration/autoland",)
    rev = "abcdef"
    query = f"/push/{branch}/{rev}/schedules"
    url = BUGBUG_BASE_URL + query

    responses.add(
        responses.GET,
        url,
        json={
            "groups": {
                "dom/indexedDB": 1,
                "testing/web-platform/tests/IndexedDB": 1,
                "testing/web-platform/mozilla/tests/IndexedDB": 1,
            },
            "config_groups": {
                "dom/indexedDB": ["label1", "label2"],
                "testing/web-platform/tests/IndexedDB": ["label3"],
                "testing/web-platform/mozilla/tests/IndexedDB": ["label4"],
            },
        },
        status=200,
    )

    assert push_schedules.cache_info().currsize == 0
    data = push_schedules(branch, rev)
    print(data)
    assert sorted(data["groups"]) == [
        "/IndexedDB",
        "/_mozilla/IndexedDB",
        "dom/indexedDB",
    ]
    assert data["config_groups"] == {
        "dom/indexedDB": ["label1", "label2"],
        "/IndexedDB": ["label3"],
        "/_mozilla/IndexedDB": ["label4"],
    }
    assert push_schedules.cache_info().currsize == 1

    # Value is cached.
    responses.reset()
    push_schedules(branch, rev)
    assert push_schedules.cache_info().currsize == 1


def test_run_push_schedules_query_success(responses, tmp_path):
    branch = "autoland"
    rev = "abcdef"
    url = BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"
    path = str(tmp_path / "result.json")

    responses.add(responses.GET, url, json={"groups": {"dom/indexedDB": 1}})

    bugbug._run_push_schedules_query(branch, rev, path)
    assert bugbug.read_push_schedules_data(path, branch, rev) == {
        "groups": {"dom/indexedDB": 1}
    }
    assert bugbug.read_push_schedules_data(path, branch, "012345") is None


def test_run_push_schedules_query_failure(responses, tmp_path):
    branch = "autoland"
    rev = "abcdef"
    url = BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"
    path = str(tmp_path / "result.json")

    responses.add(responses.GET, url, status=404)

    bugbug._run_push_schedules_query(branch, rev, path)
    with open(path) as fh:
        assert "error" in json.load(fh)
    assert bugbug.read_push_schedules_data(path, branch, rev) is None


def test_start_push_schedules(tmp_path, monkeypatch):
    path = str(tmp_path / "result.json")
    popen_args = []

    def fake_popen(args, **kwargs):
        popen_args.append((args, kwargs))
        return "proc"

    monkeypatch.setattr(bugbug.subprocess, "Popen", fake_popen)
    assert start_push_schedules("autoland", "abcdef", path) == "proc"
    [(args, kwargs)] = popen_args
    assert args[1:] == ["-m", "gecko_taskgraph.util.bugbug", "autoland", "abcdef", path]
    assert kwargs["env"]["PYTHONPATH"].split(os.pathsep) == sys.path


def test_read_push_schedules_data_missing(tmp_path):
    path = str(tmp_path / "result.json")
    assert bugbug.read_push_schedules_data(path, "autoland", "abcdef") is None


def test_push_schedules_timeout(responses, monkeypatch):
    push_schedules.cache_clear()
    monkeypatch.setattr(bugbug, "RETRY_TIMEOUT", 0.5)
    monkeypatch.setattr(bugbug, "RETRY_INTERVAL", 0.1)
    url = BUGBUG_BASE_URL + "/push/autoland/abcdef/schedules"
    responses.add(responses.GET, url, json={}, status=202)

    start = time.monotonic()
    with pytest.raises(BugbugTimeoutException):
        push_schedules("autoland", "abcdef")
    assert 0.4 <= time.monotonic() - start < 1
    push_schedules.cache_clear()


@pytest.fixture
def result_path(tmp_path, monkeypatch):
    push_schedules.cache_clear()
    path = tmp_path / "bugbug-push-schedules.json"
    monkeypatch.setenv(PUSH_SCHEDULES_ENV, str(path))
    monkeypatch.setattr(bugbug, "RESULT_POLL_INTERVAL", 0.01)
    yield path
    push_schedules.cache_clear()


def write_result(path, **result):
    bugbug._write_push_schedules_result(
        str(path), {"branch": "autoland", "rev": "abcdef", **result}
    )


def test_push_schedules_shared_result(responses, result_path):
    write_result(result_path, data={"groups": {"dom/indexedDB": 1}})
    assert push_schedules("autoland", "abcdef") == {"groups": {"dom/indexedDB": 1}}


def test_push_schedules_waits_for_shared_result(responses, result_path):
    timer = threading.Timer(0.2, write_result, (result_path,), {"data": {"groups": {}}})
    timer.start()
    try:
        assert push_schedules("autoland", "abcdef") == {"groups": {}}
    finally:
        timer.join()


def test_push_schedules_shared_timeout(responses, result_path):
    write_result(result_path, timeout="Timed out")
    with pytest.raises(BugbugTimeoutException):
        push_schedules("autoland", "abcdef")


@pytest.mark.parametrize(
    "result",
    (
        pytest.param({"error": "HTTPError()"}, id="error"),
        pytest.param({"rev": "012345", "data": {"groups": {}}}, id="other push"),
    ),
)
def test_push_schedules_shared_result_fallback(responses, result_path, result):
    write_result(result_path, **result)
    url = BUGBUG_BASE_URL + "/push/autoland/abcdef/schedules"
    responses.add(responses.GET, url, json={"groups": {"dom/indexedDB": 1}})
    assert push_schedules("autoland", "abcdef") == {"groups": {"dom/indexedDB": 1}}


def test_push_perf_risk(monkeypatch, responses):
    push_perf_risk.cache_clear()
    monkeypatch.setattr(time, "sleep", lambda i: None)
    url = BUGBUG_BASE_URL + "/perfregressionpredictor/predict/push/autoland/abcdef"
    prediction = {"risk_score": 0.2, "commits": [{"risk_score": 0.2}]}

    responses.add(responses.GET, url, json={"ready": False}, status=202)
    responses.add(responses.GET, url, json=prediction, status=200)

    assert push_perf_risk("autoland", "abcdef") == prediction
    assert len(responses.calls) == 2


def test_push_perf_risk_timeout(monkeypatch, responses):
    push_perf_risk.cache_clear()
    monkeypatch.setattr(bugbug, "RETRY_TIMEOUT", 0)
    url = BUGBUG_BASE_URL + "/perfregressionpredictor/predict/push/autoland/abcdef"

    responses.add(responses.GET, url, json={"ready": False}, status=202)

    with pytest.raises(BugbugTimeoutException):
        push_perf_risk("autoland", "abcdef")


if __name__ == "__main__":
    mozunit.main()
