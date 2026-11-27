# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio

import mozunit
import pytest

from condprof import helpers
from condprof.tests.fakearsenic import FakeSession


@pytest.mark.parametrize(
    "platform,mobile",
    [
        ("linux64", False),
        ("macosx64", False),
        ("fenix", True),
        ("p2_aarch64-geckoview_example", True),
        ("s21-firefox", True),
    ],
)
def test_is_mobile_detects_mobile_platforms(platform, mobile):
    assert helpers.is_mobile(platform) is mobile


def test_tab_switcher_mobile_does_nothing():
    session = FakeSession()
    tabs = helpers.TabSwitcher(session, {"platform": "fenix"})
    asyncio.run(tabs.create_windows())
    asyncio.run(tabs.switch())
    assert session.requests == []
    assert session.switched == []


@pytest.mark.parametrize("max_urls,num_tabs", [(None, 10), (3, 3), (150, 100)])
def test_tab_switcher_creates_tabs(max_urls, num_tabs):
    session = FakeSession()
    options = {"platform": "linux64"}
    if max_urls is not None:
        options["max_urls"] = max_urls
    tabs = helpers.TabSwitcher(session, options)
    asyncio.run(tabs.create_windows())
    assert session.requests == [("/window/new", "POST", {"type": "tab"})] * num_tabs


def test_tab_switcher_cycles():
    session = FakeSession(handles=["a", "b", "c"])
    tabs = helpers.TabSwitcher(session, {})

    async def switch(times):
        for _ in range(times):
            await tabs.switch()

    asyncio.run(switch(4))
    assert session.switched == ["a", "b", "c", "a"]


def test_tab_switcher_without_handles():
    class NoHandles(FakeSession):
        async def get_window_handles(self):
            raise ValueError()

    session = NoHandles()
    tabs = helpers.TabSwitcher(session, {})
    asyncio.run(tabs.switch())
    assert tabs.handles is None
    assert session.switched == []


def test_tab_switcher_ignores_switch_errors():
    class BrokenSwitch(FakeSession):
        async def switch_to_window(self, handle):
            raise ValueError()

    tabs = helpers.TabSwitcher(BrokenSwitch(handles=[1, 2]), {})
    asyncio.run(tabs.switch())
    assert tabs.current == 1


def test_execute_async_script_sets_timeout_and_posts_script():
    session = FakeSession(context="chrome", script_results=[{"result": 0}])
    assert asyncio.run(helpers.execute_async_script(session, "js", 1, 2)) == {
        "result": 0
    }
    assert session.requests == [
        ("/moz/context", "GET", None),
        ("/timeouts", "POST", {"script": helpers._SCRIPT_TIMEOUT}),
        ("/execute/async", "POST", {"script": "js", "args": [1, 2]}),
    ]


def test_execute_async_script_switches_context_back():
    session = FakeSession(context="content", script_results=[ValueError(), "ok"])
    assert asyncio.run(helpers.execute_async_script(session, "js")) == "ok"
    contexts = [data for url, method, data in session.requests if method == "POST"]
    assert contexts[0] == {"context": "chrome"}
    assert contexts[-1] == {"context": "content"}


def test_execute_async_script_gives_up():
    errors = [ValueError("one"), ValueError("two"), ValueError("three")]
    session = FakeSession(script_results=errors)
    result = asyncio.run(helpers.execute_async_script(session, "js"))
    assert result["result"] == 1
    assert result["result_message"] == "three"
    assert result["result_exc"] is errors[2]
    assert result["logs"] == {}


def test_close_extra_windows():
    session = FakeSession(handles=[1, 2, 3])
    asyncio.run(helpers.close_extra_windows(session))
    assert session.switched == [1, 2]
    assert session.requests == [("/window", "DELETE", None)] * 2


if __name__ == "__main__":
    mozunit.main()
