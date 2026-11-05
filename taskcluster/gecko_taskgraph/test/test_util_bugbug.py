# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import mozunit
import pytest

from gecko_taskgraph.util.bugbug import (
    BUGBUG_BASE_URL,
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


if __name__ == "__main__":
    mozunit.main()
