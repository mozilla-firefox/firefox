# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit

from talos.cmanager_base import CounterManager


def counter_a(pid):
    return 1


def counter_b(pid):
    return 2


class FakeCounterManager(CounterManager):
    counterDict = {"A": counter_a, "B": counter_b}


def test_new_manager_has_no_counters():
    manager = FakeCounterManager()

    assert manager.allCounters == {}
    assert manager.registeredCounters == {}


def test_load_counters_collects_all_counters():
    manager = FakeCounterManager()
    manager._loadCounters()

    assert manager.allCounters == {"A": counter_a, "B": counter_b}


def test_register_counters_ignores_unknown_counters():
    manager = FakeCounterManager()
    manager._loadCounters()
    manager.registerCounters(["A", "unknown"])

    assert manager.registeredCounters == {"A": [counter_a, []]}


def test_register_counters_before_load_registers_nothing():
    manager = FakeCounterManager()
    manager.registerCounters(["A"])

    assert manager.registeredCounters == {}


def test_base_get_counter_value_returns_none():
    manager = FakeCounterManager()
    manager._loadCounters()
    manager.registerCounters(["A"])

    assert manager.getCounterValue("A") is None


if __name__ == "__main__":
    mozunit.main()
