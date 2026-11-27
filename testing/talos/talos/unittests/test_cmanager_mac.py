# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import mozunit
import pytest

from talos import cmanager_mac

PS_OUTPUT = ["  PID      VSZ    RSS\n", " 3210    75964    920\n"]


def fake_popen(lines):
    handle = mock.MagicMock()
    handle.stdout.readlines.return_value = lines
    return mock.patch.object(cmanager_mac.subprocess, "Popen", return_value=handle)


def test_get_process_data_runs_ps_and_parses_columns():
    with fake_popen(PS_OUTPUT) as popen:
        assert cmanager_mac.GetProcessData(3210) == ["3210", "75964", "920"]

    command = popen.call_args[0][0]
    assert command == ["ps -o pid,vsize,rss -p3210"]
    assert popen.call_args[1]["shell"] is True
    popen.return_value.wait.assert_called_once_with()


def test_get_process_data_other_pid_returns_none():
    with fake_popen(PS_OUTPUT):
        assert cmanager_mac.GetProcessData(1) is None


def test_get_process_data_reraises_popen_errors():
    with mock.patch.object(
        cmanager_mac.subprocess, "Popen", side_effect=OSError("no ps")
    ):
        with pytest.raises(OSError):
            cmanager_mac.GetProcessData(3210)


def test_get_private_bytes_and_resident_size_convert_kilobytes_to_bytes():
    with fake_popen(PS_OUTPUT):
        assert cmanager_mac.GetPrivateBytes(3210) == 75964 * 1024
        assert cmanager_mac.GetResidentSize(3210) == 920 * 1024


def test_counter_manager_registers_known_counters():
    process = mock.Mock(pid=3210)
    manager = cmanager_mac.MacCounterManager(
        "firefox", process, ["RSS", "Private Bytes", "XRes"]
    )

    assert manager.pid == 3210
    assert set(manager.registeredCounters) == {"RSS", "Private Bytes"}


def test_counter_manager_get_counter_value_reads_rss_from_ps():
    process = mock.Mock(pid=3210)
    manager = cmanager_mac.MacCounterManager("firefox", process, ["RSS"])

    with fake_popen(PS_OUTPUT):
        assert manager.getCounterValue("RSS") == 920 * 1024


def test_counter_manager_unregistered_counter_returns_none(capsys):
    process = mock.Mock(pid=3210)
    manager = cmanager_mac.MacCounterManager("firefox", process, ["RSS"])

    assert manager.getCounterValue("Private Bytes") is None
    assert "not registered" in capsys.readouterr().out


def test_counter_manager_collection_error_returns_none(capsys):
    process = mock.Mock(pid=3210)
    manager = cmanager_mac.MacCounterManager("firefox", process, ["RSS"])

    with fake_popen(PS_OUTPUT[:1]):
        assert manager.getCounterValue("RSS") is None
    assert "Error in collecting counter: RSS, pid: 3210" in capsys.readouterr().out


if __name__ == "__main__":
    mozunit.main()
