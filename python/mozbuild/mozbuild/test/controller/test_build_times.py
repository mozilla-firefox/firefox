# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json

import mozunit
import pytest
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor

from mozbuild.controller.building import BuildMonitor


def make_build_monitor(tmp_path, forward_path):
    monitor = BuildMonitor.__new__(BuildMonitor)
    monitor._topobjdir = str(tmp_path / "obj")
    monitor.resources = SystemResourceMonitor(forward_to=str(forward_path))
    return monitor


@pytest.fixture
def forward_path(tmp_path):
    path = tmp_path / "forward.jsonl"
    path.touch()
    return path


def test_write_build_times(tmp_path, forward_path, monkeypatch):
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    monitor = make_build_monitor(tmp_path, forward_path)
    resources = monitor.resources
    resources.start()
    assert resources.forwarding
    resources.begin_phase("configure")
    resources.finish_phase("configure")
    resources.begin_phase("compile")
    resources.finish_phase("compile")
    resources.stop()

    monitor.record_usage()

    with open(monitor.build_times_path) as fh:
        build_times = json.load(fh)
    assert set(build_times["phases"]) == {"configure", "compile", "teardown"}
    assert all(t >= 0 for t in build_times["phases"].values())
    assert build_times["duration"] >= sum(build_times["phases"].values())


def test_write_build_times_not_automation(tmp_path, forward_path, monkeypatch):
    monkeypatch.delenv("MOZ_AUTOMATION", raising=False)
    monitor = make_build_monitor(tmp_path, forward_path)
    monitor.resources.start()
    monitor.resources.stop()

    monitor.record_usage()

    assert not (tmp_path / "obj").exists()


def test_write_build_times_not_started(tmp_path, monkeypatch):
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    monitor = make_build_monitor(tmp_path, tmp_path / "missing" / "forward.jsonl")
    with pytest.warns(UserWarning):
        monitor.resources.start()
    monitor.resources.stop()

    monitor._write_build_times()

    assert not (tmp_path / "obj").exists()


if __name__ == "__main__":
    mozunit.main()
