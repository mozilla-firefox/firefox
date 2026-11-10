# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import os
import time

import mozunit
import pytest

from gecko_taskgraph.util.bugbug import (
    BUGBUG_BASE_URL,
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
    monkeypatch.setattr(time, "sleep", lambda i: None)
    url = BUGBUG_BASE_URL + "/perfregressionpredictor/predict/push/autoland/abcdef"

    responses.add(responses.GET, url, json={"ready": False}, status=202)

    with pytest.raises(BugbugTimeoutException):
        push_perf_risk("autoland", "abcdef")


if __name__ == "__main__":
    mozunit.main()
