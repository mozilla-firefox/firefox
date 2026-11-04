# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import time

import pytest

try:
    from mozsystemmonitor.resourcemonitor import FORWARD_ENV, SystemResourceMonitor
except ImportError:
    SystemResourceMonitor = None


def pytest_addoption(parser):
    group = parser.getgroup("mozunit")
    group.addoption(
        "--run-slow", action="store_true", default=False, help="run slow tests"
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "slow: mark test as slow to run")
    # Tests may unset FORWARD_ENV, so read it before collecting them.
    if SystemResourceMonitor and (path := os.environ.get(FORWARD_ENV)):
        config.pluginmanager.register(ResourceMarkers(path), "mozunit-resource")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--run-slow"):
        # --run-slow given in cli: do not skip slow tests
        return
    skip_slow = pytest.mark.skip(reason="need --run-slow option to run")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)


class ResourceMarkers:
    """Forward a marker for each test to the resource monitor of a parent
    process, e.g. run-task's."""

    def __init__(self, path):
        self.path = path
        self.reports = {}

    @pytest.hookimpl(hookwrapper=True)
    def pytest_runtest_protocol(self, item, nextitem):
        start = time.monotonic()
        yield
        end = time.monotonic()
        data = {"type": "Test", "test": item.nodeid, "name": item.name}
        data.update(self._status(self.reports.pop(item.nodeid, [])))
        SystemResourceMonitor.forward_marker(
            "test", start, end, data, forward_to=self.path
        )

    def pytest_runtest_logreport(self, report):
        self.reports.setdefault(report.nodeid, []).append(report)

    @staticmethod
    def _status(reports):
        for report in reports:
            if report.failed:
                if report.when != "call":
                    return {"status": "ERROR", "color": "red"}
                return {"status": "FAIL", "expected": "PASS", "color": "orange"}
        for report in reports:
            if report.skipped:
                if hasattr(report, "wasxfail"):
                    return {"status": "FAIL", "color": "green"}
                return {
                    "status": "SKIP",
                    "color": "yellow",
                    "message": str(report.longrepr[-1]),
                }
            if hasattr(report, "wasxfail"):
                return {"status": "PASS", "expected": "FAIL", "color": "orange"}
        return {"status": "PASS", "color": "green"}
