# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ctypes
import importlib
import sys
from unittest import mock

import mozunit
import pytest

from talos.utils import TalosError

PDH_MORE_DATA = -2147481646


class FakePdh:
    def __init__(self, failing_object_names=(), child_paths=(), values=None):
        self.failing_object_names = set(failing_object_names)
        self.child_paths = list(child_paths)
        self.values = values or {}
        self.handles = {}
        self.open_query_result = 0
        self.add_counter_results = {}
        self.make_path_second_result = 0
        self.collected = []
        self.enum_calls = 0

    def PdhEnumObjectsA(self, *args):
        self.enum_calls += 1
        return 0

    def PdhMakeCounterPathA(self, elements, buffer, size, flags):
        element = elements.contents
        if element.szObjectName in self.failing_object_names:
            return 1
        path = b"\\%s(%s)\\%s" % (
            element.szObjectName,
            element.szInstanceName,
            element.szCounterName,
        )
        if size.contents.value == 0:
            size.contents.value = len(path) + 1
            return PDH_MORE_DATA
        buffer.value = path
        return self.make_path_second_result

    def PdhOpenQuery(self, source, user_data, query):
        query._obj.value = 1000
        return self.open_query_result

    def PdhAddCounterA(self, query, path, user_data, counter):
        result = self.add_counter_results.get(path, 0)
        if result == 0:
            counter._obj.value = 2000 + len(self.handles)
            self.handles[counter._obj.value] = path
        return result

    def PdhCollectQueryData(self, query):
        self.collected.append(query)
        return 0

    def PdhGetFormattedCounterValue(self, counter, fmt, counter_type, value):
        path = self.handles.get(counter.value)
        if path not in self.values:
            return 1
        value._obj.union.longValue = self.values[path]
        return 0

    def PdhExpandCounterPathA(self, wildcard, buffer, size):
        data = b"".join(p + b"\0" for p in self.child_paths) + b"\0"
        if not self.child_paths:
            return 0
        if size.contents.value == 0:
            size.contents.value = len(data)
            return PDH_MORE_DATA
        buffer.value = data
        return 0


@pytest.fixture
def win32():
    windll = mock.MagicMock()
    with mock.patch.object(ctypes, "windll", windll, create=True):
        with mock.patch.dict(sys.modules):
            sys.modules.pop("talos.cmanager_win32", None)
            module = importlib.import_module("talos.cmanager_win32")
            yield module


def make_manager(module, pdh, counters, process_name="firefox"):
    with mock.patch.object(module, "pdh", pdh):
        return module.WinCounterManager(process_name, mock.Mock(), counters)


def test_module_uses_windll_pdh(win32):
    assert win32.pdh is ctypes.windll.pdh


def test_register_counters_skips_main_rss_and_mainthread_io(win32):
    manager = make_manager(win32, FakePdh(), [])
    manager.registerCounters(["Main_RSS", " mainthread_io ", "Working Set"])

    assert manager.registeredCounters == {b"Working Set": []}


def test_init_registers_process_counter_paths_for_requested_counters(win32):
    pdh = FakePdh()
    manager = make_manager(win32, pdh, ["Private Bytes", "Main_RSS"])

    assert list(manager.registeredCounters) == [b"Private Bytes"]
    query, counters = manager.registeredCounters[b"Private Bytes"]
    assert query.value == 1000
    assert [path for _, path in counters] == [b"\\process(firefox)\\Private Bytes"]
    assert pdh.enum_calls == 2


def test_init_uses_memory_counter_path_when_process_object_fails(win32):
    pdh = FakePdh(failing_object_names=[b"process"])
    manager = make_manager(win32, pdh, ["Available Bytes"])

    _, counters = manager.registeredCounters[b"Available Bytes"]
    assert [path for _, path in counters] == [b"\\Memory(firefox)\\Available Bytes"]


def test_add_counter_raises_talos_error_on_each_pdh_failure(win32):
    pdh = FakePdh(failing_object_names=[b"bad"])
    manager = make_manager(win32, pdh, [])

    with mock.patch.object(win32, "pdh", pdh):
        with pytest.raises(TalosError, match="Could not create counter path"):
            manager._addCounter(b"firefox", b"bad", b"RSS")

        pdh.make_path_second_result = 1
        with pytest.raises(TalosError, match="Could not create counter path"):
            manager._addCounter(b"firefox", b"process", b"RSS")
        pdh.make_path_second_result = 0

        pdh.open_query_result = 1
        with pytest.raises(TalosError, match="Could not open win32 counter query"):
            manager._addCounter(b"firefox", b"process", b"RSS")
        pdh.open_query_result = 0

        pdh.add_counter_results[b"\\process(firefox)\\RSS"] = 1
        with pytest.raises(TalosError, match="Could not add win32 counter"):
            manager._addCounter(b"firefox", b"process", b"RSS")


def test_get_counter_value_sums_counters(win32):
    path = b"\\process(firefox)\\Private Bytes"
    pdh = FakePdh(values={path: 1234})
    manager = make_manager(win32, pdh, ["Private Bytes"])

    with mock.patch.object(win32, "pdh", pdh):
        assert manager.getCounterValue("Private Bytes") == 1234
    assert pdh.collected == [manager.registeredCounters[b"Private Bytes"][0]]


def test_get_counter_value_ignores_counters_without_value(win32):
    pdh = FakePdh()
    manager = make_manager(win32, pdh, ["Private Bytes"])

    with mock.patch.object(win32, "pdh", pdh):
        assert manager.getCounterValue("Private Bytes") == 0


def test_get_counter_value_unknown_or_unset_counter(win32):
    manager = make_manager(win32, FakePdh(), [])
    manager.registerCounters(["Working Set"])

    assert manager.getCounterValue("Unknown") is None
    assert manager.getCounterValue("Working Set") is None


def test_update_counter_paths_adds_new_child_counters(win32):
    pdh = FakePdh()
    manager = make_manager(win32, pdh, ["RSS"])
    query = manager.registeredCounters[b"RSS"][0]
    known = manager.registeredCounters[b"RSS"][1][0][1]
    pdh.add_counter_results["child-bad"] = 1

    expanded = [known, "child-1", "child-bad", "child-2"]
    with mock.patch.object(win32, "pdh", pdh):
        with mock.patch.object(
            win32, "_getExpandedCounterPaths", return_value=expanded
        ) as expand:
            manager._updateCounterPathsForChildProcesses(b"RSS")

    expand.assert_called_once_with(b"plugin-container", b"RSS")
    paths = [path for _, path in manager.registeredCounters[b"RSS"][1]]
    assert paths == [known, "child-1", "child-2"]
    assert pdh.collected == [query]


def test_update_counter_paths_without_new_children(win32):
    pdh = FakePdh()
    manager = make_manager(win32, pdh, ["RSS"])
    known = manager.registeredCounters[b"RSS"][1][0][1]

    with mock.patch.object(win32, "pdh", pdh):
        with mock.patch.object(win32, "_getExpandedCounterPaths", return_value=[known]):
            manager._updateCounterPathsForChildProcesses(b"RSS")

    assert len(manager.registeredCounters[b"RSS"][1]) == 1
    assert pdh.collected == []


def test_get_expanded_counter_paths_no_match(win32):
    with mock.patch.object(win32, "pdh", FakePdh()):
        assert win32._getExpandedCounterPaths(b"plugin-container", b"RSS") == []


def test_get_expanded_counter_paths_second_call_fails(win32):
    pdh = FakePdh(child_paths=[b"a"])
    pdh.PdhExpandCounterPathA = mock.Mock(side_effect=[PDH_MORE_DATA, 1])

    with mock.patch.object(win32, "pdh", pdh):
        assert win32._getExpandedCounterPaths(b"plugin-container", b"RSS") == []


@pytest.mark.xfail(
    reason="_getExpandedCounterPaths compares str(bytes) with '\\0', so it "
    "never splits the path list",
    strict=True,
)
def test_get_expanded_counter_paths_splits_paths(win32):
    paths = [b"\\process(a)\\RSS", b"\\process(b)\\RSS"]

    with mock.patch.object(win32, "pdh", FakePdh(child_paths=paths)):
        result = win32._getExpandedCounterPaths(b"plugin-container", b"RSS")

    assert [p.encode() if isinstance(p, str) else p for p in result] == paths


if __name__ == "__main__":
    mozunit.main()
