# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import sys
from unittest import mock

import mozunit
import pytest

from talos import cmanager_base

CMANAGER_BASE_AS_TOP_LEVEL_MODULE = {"cmanager_base": cmanager_base}

with mock.patch.dict(sys.modules, CMANAGER_BASE_AS_TOP_LEVEL_MODULE):
    from talos import cmanager_linux

XRESTOP_OUTPUT = """\
0 - Firefox ( PID: 2035 ):
    res_base      : 0x1600000
    windows       : 69
    total bytes   : ~4728761
1 - xterm ( PID: ? ):
    windows       : 3
    total bytes   : ~100
2 - Content ( PID: 2040 ):
    total bytes   : 1000
"""

MAPS = {
    "/proc/1/maps": (
        "00400000-00401000 r-xp 00000000 08:01 1 /bin/app\n"
        "00600000-00602000 rw-p 00000000 08:01 1 /bin/app\n"
        "00700000-00704000 rw-s 00000000 00:00 0\n"
    ),
    "/proc/2/maps": "10000000-10010000 rw-p 00000000 00:00 0\n",
    "/proc/1/status": "Name:\tfirefox\nVmRSS:\t    100 kB\n",
    "/proc/2/status": "Name:\tcontent\nVmRSS:\t     20 kB\nThreads:\t1\n",
}


def fake_open(path, *args, **kwargs):
    return io.StringIO(MAPS[path])


def fake_xrestop_process(stdout, returncode=0):
    process = mock.Mock(returncode=returncode)
    process.communicate.return_value = (stdout, "")
    return mock.patch.object(cmanager_linux.subprocess, "Popen", return_value=process)


def test_xrestop_parses_output():
    with fake_xrestop_process(XRESTOP_OUTPUT) as popen:
        result = cmanager_linux.xrestop()

    assert popen.call_args[0][0] == ["xrestop", "-m", "1", "-b"]
    assert result == {
        2035: {
            "index": 0,
            "name": "Firefox",
            "res_base": "0x1600000",
            "windows": "69",
            "total bytes": "~4728761",
        },
        2040: {"index": 2, "name": "Content", "total bytes": "1000"},
    }


def test_xrestop_custom_binary():
    with fake_xrestop_process("") as popen:
        assert cmanager_linux.xrestop(binary="/opt/xrestop") == {}

    assert popen.call_args[0][0][0] == "/opt/xrestop"


def test_xrestop_raises_on_failure():
    with fake_xrestop_process("boom", returncode=1):
        with pytest.raises(Exception, match="Unexpected error executing"):
            cmanager_linux.xrestop()


def test_get_private_bytes_counts_private_writable_mappings():
    with mock.patch("builtins.open", side_effect=fake_open):
        assert cmanager_linux.GetPrivateBytes([1, 2]) == 0x2000 + 0x10000


def test_get_resident_size_sums_pages_of_all_pids():
    with mock.patch("builtins.open", side_effect=fake_open):
        assert cmanager_linux.GetResidentSize([1, 2]) == 120 * 1024


def test_get_xres_sums_known_pids():
    with fake_xrestop_process(XRESTOP_OUTPUT):
        assert cmanager_linux.GetXRes([2035, 2040, 9999]) == 4728761 + 1000


def test_get_xres_invalid_value():
    output = "0 - Firefox ( PID: 2035 ):\n    total bytes   : lots\n"
    with fake_xrestop_process(output):
        with pytest.raises(ValueError):
            cmanager_linux.GetXRes([2035])


def make_process():
    process = mock.Mock(pid=1)
    process.children.return_value = [mock.Mock(pid=2)]
    return process


def test_counter_manager_registers_known_counters():
    manager = cmanager_linux.LinuxCounterManager(
        "firefox", make_process(), ["RSS", "XRes", "Private Bytes", "Unknown"]
    )

    assert set(manager.registeredCounters) == {"RSS", "XRes", "Private Bytes"}


def test_counter_manager_pid_list_returns_process_and_children():
    manager = cmanager_linux.LinuxCounterManager("firefox", make_process(), [])

    assert manager.pidList() == [1, 2]


def test_counter_manager_pid_list_returns_none_when_children_fail(capsys):
    process = mock.Mock(pid=1)
    process.children.side_effect = RuntimeError("gone")
    manager = cmanager_linux.LinuxCounterManager("firefox", process, [])

    assert manager.pidList() is None
    assert "problem updating child PID's" in capsys.readouterr().out


def test_counter_manager_get_counter_value_reads_rss():
    manager = cmanager_linux.LinuxCounterManager("firefox", make_process(), ["RSS"])

    with mock.patch("builtins.open", side_effect=fake_open):
        assert manager.getCounterValue("RSS") == 120 * 1024


def test_counter_manager_get_counter_value_errors_return_none():
    manager = cmanager_linux.LinuxCounterManager("firefox", make_process(), ["RSS"])

    assert manager.getCounterValue("Unknown") is None
    with mock.patch("builtins.open", side_effect=OSError("no /proc")):
        assert manager.getCounterValue("RSS") is None


if __name__ == "__main__":
    mozunit.main()
