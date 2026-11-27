#!/usr/bin/env python

"""Tests for how ADBDevice.pidof() filters the pids pidof returns by the
command line it reads back for each of them.

The device is faked: shell_output() returns canned text keyed by the
command's first word, so these tests cover only the parsing and filtering in
Python. They do not check that the shell commands run correctly on a device,
including when they are wrapped in su.
"""

from unittest import mock

import mozunit
import pytest
from mozdevice import ADBError

APP = "org.mozilla.geckoview.test_runner:xpcshell0"


@pytest.fixture
def device(mock_adb_object, monkeypatch):
    mock_adb_object._have_pidof = True
    mock_adb_object._have_flaky_pidof = False
    mock_adb_object._logger = mock.MagicMock()
    mock_adb_object.outputs = {}

    def shell_output(self, cmd, timeout=None, **kwargs):
        output = self.outputs[cmd.split(" ")[0]]
        if isinstance(output, Exception):
            raise output
        return output

    monkeypatch.setattr(type(mock_adb_object), "shell_output", shell_output)
    return mock_adb_object


def test_pidof_keeps_pids_with_the_name(device):
    device.outputs["pidof"] = "1234\n"
    device.outputs["sh"] = f"pid=1234\n{APP}\x00"
    assert device.pidof(APP) == [1234]


def test_pidof_drops_pids_now_running_under_another_name(device):
    device.outputs["pidof"] = "1234 5678\n"
    device.outputs["sh"] = (
        "pid=1234\norg.mozilla.geckoview.test_runner:xpcshell1\x00\n"
        f"pid=5678\n{APP}\x00"
    )
    assert device.pidof(APP) == [5678]


def test_pidof_drops_pids_that_exited(device):
    device.outputs["pidof"] = "1234\n"
    device.outputs["sh"] = "pid=1234"
    assert device.pidof(APP) == []


def test_pidof_drops_exited_pid_between_live_ones(device):
    device.outputs["pidof"] = "1234 5678 9012\n"
    device.outputs["sh"] = f"pid=1234\n{APP}\x00\npid=5678\n\npid=9012\n{APP}\x00"
    assert device.pidof(APP) == [1234, 9012]


def test_pidof_keeps_pids_when_names_cannot_be_checked(device):
    device.outputs["pidof"] = "1234\n"
    device.outputs["sh"] = ADBError("cmdline check failed")
    assert device.pidof(APP) == [1234]
    device._logger.warning.assert_called_once()


if __name__ == "__main__":
    mozunit.main()
