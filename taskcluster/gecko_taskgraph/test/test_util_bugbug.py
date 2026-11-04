# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


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
    push_schedules,
    start_push_schedules,
)


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


def test_start_push_schedules_success(responses):
    branch = "autoland"
    rev = "abcdef"
    url = BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"

    responses.add(responses.GET, url, json={"groups": {"dom/indexedDB": 1}})

    future = start_push_schedules(branch, rev)
    assert future.result(timeout=10) == {"groups": {"dom/indexedDB": 1}}


def test_start_push_schedules_failure(responses):
    branch = "autoland"
    rev = "abcdef"
    url = BUGBUG_BASE_URL + f"/push/{branch}/{rev}/schedules"

    responses.add(responses.GET, url, status=404)

    future = start_push_schedules(branch, rev)
    with pytest.raises(Exception):
        future.result(timeout=10)


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


@pytest.mark.parametrize(
    "status,expected",
    (
        pytest.param(200, {"data": {"groups": {"dom/indexedDB": 1}}}, id="success"),
        pytest.param(404, None, id="failure"),
    ),
)
def test_start_push_schedules_shares_result(responses, tmp_path, status, expected):
    url = BUGBUG_BASE_URL + "/push/autoland/abcdef/schedules"
    responses.add(
        responses.GET, url, json={"groups": {"dom/indexedDB": 1}}, status=status
    )
    path = tmp_path / "result.json"

    future = start_push_schedules("autoland", "abcdef", str(path))
    try:
        future.result(timeout=10)
    except Exception:
        pass
    for _ in range(100):
        if path.exists():
            break
        threading.Event().wait(0.05)
    result = json.loads(path.read_text())
    assert (result["branch"], result["rev"]) == ("autoland", "abcdef")
    if expected:
        assert result["data"] == expected["data"]
    else:
        assert "error" in result


if __name__ == "__main__":
    mozunit.main()
