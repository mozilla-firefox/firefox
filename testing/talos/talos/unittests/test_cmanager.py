# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib
import sys
import threading
from unittest import mock

import mozunit
import pytest

from talos import cmanager_base

CMANAGER_BASE_AS_TOP_LEVEL_MODULE = {"cmanager_base": cmanager_base}

with mock.patch.dict(sys.modules, CMANAGER_BASE_AS_TOP_LEVEL_MODULE):
    from talos import cmanager


class FakeCounterManager:
    owner = None
    values = {}
    created = []

    def __init__(self, process_name, process, counters):
        self.created.append((process_name, process, counters))
        self.iterators = {c: iter(v) for c, v in self.values.items()}

    def getCounterValue(self, counter):
        try:
            return next(self.iterators[counter])
        except StopIteration:
            self.owner._stop.set()
            return None


@pytest.fixture
def fake_manager():
    FakeCounterManager.created = []
    with mock.patch.object(cmanager, "DefaultCounterManager", FakeCounterManager):
        yield FakeCounterManager


def test_counter_management_requires_counters():
    with pytest.raises(AssertionError):
        cmanager.CounterManagement("firefox", [], 1)


def test_collects_counter_values_and_drops_none_samples(fake_manager):
    management = cmanager.CounterManagement("firefox", ["RSS", "XRes"], 0.001)
    fake_manager.owner = management
    fake_manager.values = {"RSS": [10, None, 30], "XRes": [1, 2, 3, 4]}
    process = mock.Mock(pid=1)

    management.start(process)
    management._thread.join(timeout=30)
    management.stop()

    assert fake_manager.created == [("firefox", process, ["RSS", "XRes"])]
    results = management.results()
    assert results["RSS"] == [10, 30]
    assert results["XRes"] == [1, 2, 3, 4]


def test_results_requires_stopped_thread(fake_manager):
    release = threading.Event()

    class BlockingManager:
        def __init__(self, *args):
            pass

        def getCounterValue(self, counter):
            release.wait()

    management = cmanager.CounterManagement("firefox", ["RSS"], 0.001)
    with mock.patch.object(cmanager, "DefaultCounterManager", BlockingManager):
        management.start(mock.Mock())
        try:
            with pytest.raises(AssertionError):
                management.results()
        finally:
            release.set()
            management.stop()

    assert management.results() == {"RSS": []}


def reload_cmanager():
    with mock.patch.dict(sys.modules, CMANAGER_BASE_AS_TOP_LEVEL_MODULE):
        sys.modules["talos.cmanager"] = cmanager
        importlib.reload(cmanager)


@pytest.mark.parametrize(
    "os_name,module_name,class_name",
    [
        ("linux", "talos.cmanager_linux", "LinuxCounterManager"),
        ("win", "talos.cmanager_win32", "WinCounterManager"),
        ("mac", "talos.cmanager_mac", "MacCounterManager"),
    ],
)
def test_default_counter_manager_depends_on_os(os_name, module_name, class_name):
    fake_class = type(class_name, (), {})
    fake_module = mock.Mock(**{class_name: fake_class})
    try:
        with mock.patch.dict(sys.modules, {module_name: fake_module}):
            with mock.patch("mozinfo.os", os_name):
                reload_cmanager()
                assert cmanager.DefaultCounterManager is fake_class
    finally:
        reload_cmanager()


if __name__ == "__main__":
    mozunit.main()
