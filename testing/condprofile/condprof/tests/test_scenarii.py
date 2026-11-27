# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import importlib

import mozunit
import pytest

from condprof.tests import fakearsenic

from condprof.tests.fakearsenic import FakeSession  # isort: skip
from condprof.scenarii import scenarii  # isort: skip

full = importlib.import_module("condprof.scenarii.full")


@pytest.fixture
def sleeps(monkeypatch):
    durations = []
    real_sleep = asyncio.sleep

    async def fake_sleep(duration):
        durations.append(duration)
        await real_sleep(0)

    monkeypatch.setattr(asyncio, "sleep", fake_sleep)
    return durations


def test_registry_contains_expected_scenario_names():
    assert sorted(scenarii) == ["full", "settled", "settled-youtube", "settled2"]


@pytest.mark.parametrize("options,expected", [({}, [30]), ({"sleep": 2}, [2])])
def test_settled_sleeps_default_or_requested_duration(sleeps, options, expected):
    assert asyncio.run(scenarii["settled"](FakeSession(), options)) == {}
    assert sleeps == expected


def test_settled2_retries_visit_after_timeout_then_sleeps(sleeps):
    session = FakeSession()
    session.get_errors = [asyncio.TimeoutError(), None]
    assert asyncio.run(scenarii["settled2"](session, {})) == {}
    assert session.visited == ["https://www.mozilla.org/en-US/"]
    assert sleeps == [300]


def test_settled2_gives_up_after_repeated_timeouts(sleeps):
    session = FakeSession()
    session.get_errors = [asyncio.TimeoutError()] * 3
    assert asyncio.run(scenarii["settled2"](session, {"sleep": 1})) == {}
    assert session.visited == []
    assert sleeps == [1]


@pytest.mark.parametrize("options,expected", [({}, [5, 30]), ({"sleep": 1}, [1, 1])])
def test_settled_youtube_visits_test_video_page_then_sleeps(sleeps, options, expected):
    session = FakeSession()
    assert asyncio.run(scenarii["settled-youtube"](session, options)) == {}
    assert len(session.visited) == 1
    assert session.visited[0].startswith("https://yttest.prod.mozaws.net/")
    assert sleeps == expected


def test_builder_builds_urls_from_words_with_default_limits():
    builder = full.Builder({})
    words = [w.strip() for w in builder.words if not w.strip().startswith("#")]
    assert len(builder.urls) > full.MAX_URLS
    for url, word in builder.urls:
        assert word in words
        assert "{}" not in url
    assert builder.max_urls == full.MAX_URLS
    assert builder.max_bookmarks == full.MAX_BOOKMARKS
    assert builder.bookmark_frequency == full.BOOKMARK_FREQUENCY
    assert (builder.username, builder.password) == (None, None)


def test_builder_mobile_caps_urls():
    assert full.Builder({"platform": "fenix", "max_urls": 50}).max_urls == 20
    assert full.Builder({"platform": "linux64", "max_urls": 50}).max_urls == 50


def _bookmarks(session):
    return [data for url, method, data in session.requests if url == "/execute/async"]


def test_full_visits_urls_bookmarks_periodically_and_switches_tabs(sleeps):
    session = FakeSession(handles=[0, 1])
    options = {"platform": "linux64", "max_urls": 4, "bookmark_frequency": 2}
    metadata = asyncio.run(scenarii["full"](session, options))
    assert metadata == {"visited_url": 4}
    assert len(session.visited) == 4
    bookmarks = _bookmarks(session)
    assert [data["args"][0] for data in bookmarks] == [
        session.visited[0],
        session.visited[2],
    ]
    assert all(data["args"][2] == full.MAX_BOOKMARKS for data in bookmarks)
    assert session.switched == [0, 1, 0]


def test_full_mobile_skips_bookmarks_and_tabs(sleeps):
    session = FakeSession()
    metadata = asyncio.run(
        scenarii["full"](session, {"platform": "fenix", "max_urls": 2})
    )
    assert metadata == {"visited_url": 2}
    assert _bookmarks(session) == []
    assert session.switched == []


def test_full_retries_failed_visits(sleeps):
    session = FakeSession()
    session.get_errors = [
        asyncio.TimeoutError(),
        fakearsenic.UnknownError(),
        fakearsenic.UnknownArsenicError(),
        None,
    ]
    metadata = asyncio.run(
        scenarii["full"](session, {"platform": "fenix", "max_urls": 2})
    )
    assert metadata == {"visited_url": 1}
    assert sleeps == [1.0, 1.0, 1.0]


def test_full_ignores_tab_switch_timeout(sleeps, monkeypatch):
    async def switch(self):
        raise asyncio.TimeoutError()

    monkeypatch.setattr(full.TabSwitcher, "switch", switch)
    session = FakeSession()
    options = {"platform": "linux64", "max_urls": 2}
    assert asyncio.run(scenarii["full"](session, options)) == {"visited_url": 2}
    assert session.switched == []


@pytest.mark.parametrize(
    "script_res,expected",
    [
        (
            {"logs": {"sync": ["l"]}, "result": 2, "result_message": "bad"},
            {"logs": {"sync": ["l"]}, "result": 2, "result_message": "bad"},
        ),
        (None, {"logs": {}, "result": 0, "result_message": "SUCCESS"}),
    ],
)
def test_builder_sync_returns_script_result_or_success_defaults(script_res, expected):
    builder = full.Builder({})
    assert asyncio.run(builder.sync(FakeSession(), {})) is None
    builder.syncing = True
    builder.username, builder.password = "bob", "s3cret"
    session = FakeSession(script_results=[script_res])
    assert asyncio.run(builder.sync(session, {})) == expected
    (args,) = [
        data["args"] for url, _, data in session.requests if url == "/execute/async"
    ]
    assert args == ["bob", "s3cret", "https://accounts.stage.mozaws.net"]


if __name__ == "__main__":
    mozunit.main()
