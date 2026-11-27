import json
import ntpath
import os
import runpy
import sys
import types
from unittest import mock

import mozunit
import pytest

from talos.xtalos import etlparser, xtalos

RESTORED = etlparser.CEVT_WINDOWS_RESTORED
SHUTDOWN = etlparser.CEVT_XPCOM_SHUTDOWN
TCPIP = "Microsoft-Windows-TCPIP"

HEADER = f"""\
garbage before the header
BeginHeader
P-Start, TimeStamp, Process Name ( PID), ParentPID, Command Line
T-DCStart, TimeStamp, Process Name ( PID), ThreadID, StackBase, Image!Function
T-Start, TimeStamp, Process Name ( PID), ThreadID, StackBase, Image!Function
T-DCEnd, TimeStamp, Process Name ( PID), ThreadID
T-End, TimeStamp, Process Name ( PID), ThreadID
FileIoRead, TimeStamp, Process Name ( PID), ThreadID, Size, FileName
FileIoWrite, TimeStamp, Process Name ( PID), ThreadID, Size, FileName
UnknownEvent/Classic, TimeStamp, Process Name ( PID), ThreadID, EventGuid
{TCPIP}/TcpDataTransferReceive, TimeStamp, Process Name ( PID), ThreadID, etw:ActivityId, NumBytes
{TCPIP}/UdpEndpointSendMessages, TimeStamp, Process Name ( PID), ThreadID, etw:ActivityId, BytesSent
{TCPIP}/TcpConnectTcbProceeding, TimeStamp, Process Name ( PID), ThreadID, etw:ActivityId, NumBytes
EndHeader
this line right after the header is dropped
"""

UUID_NAME = "{12345678-1234-1234-1234-123456789abc}"
BODY = f"""\
P-Start, 1, firefox.exe ( 100), 4, firefox.exe
P-Start, 2, firefox.exe ( 200), 100, firefox.exe
P-Start, 3, firefox.exe ( 300), 100, firefox.exe -contentproc
T-DCStart, 4, firefox.exe ( 200), 11, 0x0, firefox.exe!wmain
T-Start, 5, firefox.exe ( 200), 12, 0x0, ntdll.dll!RtlUserThreadStart
T-Start, 6, firefox.exe ( 300), 13, 0x0, firefox.exe!wmain
T-Start, 6, firefox.exe ( 200), 14, 0x0, xul.dll!Foo
T-End, 7, firefox.exe ( 200), 14
T-DCEnd, 7, firefox.exe ( 200), 99
FileIoRead, 8, firefox.exe ( 200), 11, 0x100, C:\\Users\\me\\AppData\\Profile\\prefs.js
FileIoWrite, 8, firefox.exe ( 200), 11, 0x10, C:\\Users\\me\\AppData\\Profile\\prefs.js
FileIoRead, 8, firefox.exe ( 200), 11, 0x1
FileIoRead, 8, firefox.exe ( 200), 11, 0x2, C:\\Windows\\Prefetch\\FIREFOX.EXE-1A2B.pf
FileIoRead, 8, firefox.exe ( 200), 11, 0x2, C:\\Program Files (x86)\\Mozilla Firefox\\Crash Reports\\InstallTime20240101
FileIoRead, 8, firefox.exe ( 200), 11, 0x2, C:\\Program Files\\Mozilla Firefox\\xul.dll
FileIoRead, 8, firefox.exe ( 200), 11, 0x2, C:\\allowed\\file.txt
FileIoRead, 8, firefox.exe ( 200), 11, 0x4, C:\\Users\\me\\Desktop\\{UUID_NAME}.tmp
FileIoRead, 9, firefox.exe ( 200), 12, 0x20, C:\\Windows\\x.dll
FileIoRead, 9, firefox.exe ( 300), 13, 0x20, C:\\Windows\\ignored.dll
UnknownEvent/Classic, 9, firefox.exe ( 300), 13, {RESTORED}
UnknownEvent/Classic, 10, firefox.exe ( 200), 11, {SHUTDOWN}
UnknownEvent/Classic, 10, firefox.exe ( 200), 11, {RESTORED}
FileIoRead, 11, firefox.exe ( 200), 11, 0x40, C:\\normal.txt
{TCPIP}/TcpDataTransferReceive, 12, firefox.exe ( 200), 11, {{a}}, 50
{TCPIP}/UdpEndpointSendMessages, 12, firefox.exe ( 200), 12, {{b}}, 7
{TCPIP}/UdpEndpointSendMessages, 12, svchost.exe ( 5), 99, {{a}}, 3
{TCPIP}/TcpConnectTcbProceeding, 12, firefox.exe ( 200), 11, {{a}}, 1000
{TCPIP}/TcpDataTransferReceive, 12, svchost.exe ( 5), 77, {{c}}, 1000
UnknownEvent/Classic, 13, firefox.exe ( 200), 11, {SHUTDOWN}
FileIoRead, 14, firefox.exe ( 200), 11, 0x8, C:\\shutdown.txt
"""

EXPECTED_THREAD_STATS = {
    "main, startup, file_read_ops, 7",
    "main, startup, file_read_bytes, 269",
    "main, startup, file_write_ops, 1",
    "main, startup, file_write_bytes, 16",
    "main, startup, file_io_bytes, 285",
    "nonmain, startup, file_read_ops, 1",
    "nonmain, startup, file_read_bytes, 32",
    "nonmain, startup, file_io_bytes, 32",
    "main, normal, file_read_ops, 1",
    "main, normal, file_read_bytes, 64",
    "main, normal, file_io_bytes, 64",
    "main, normal, net_recv_bytes, 50",
    "main, normal, net_send_bytes, 3",
    "main, normal, net_io_bytes, 53",
    "nonmain, normal, net_send_bytes, 7",
    "nonmain, normal, net_io_bytes, 7",
    "main, shutdown, file_read_ops, 1",
    "main, shutdown, file_read_bytes, 8",
    "main, shutdown, file_io_bytes, 8",
}

DESKTOP_ERROR = (
    "File '{desktop}\\{uuid}.tmp' (normalized from "
    f"'C:\\Users\\me\\Desktop\\{UUID_NAME}.tmp') was accessed and we were "
    "not expecting it."
)

OUTPUT_HEADER = "filename, tid, stage, readcount, readbytes, writecount, writebytes"


@pytest.fixture(autouse=True)
def reset_globals():
    def reset():
        etlparser.gThreads.clear()
        etlparser.gConnectionIDs.clear()
        etlparser.gHeaders.clear()
        etlparser.gBrowserPID = None

    reset()
    yield
    reset()


@pytest.fixture
def workdir(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    with open("test.etl.csv", "w", encoding="cp1252") as f:
        f.write(HEADER + BODY)
    with open("test.etl", "w") as f:
        f.write("etl")
    with open("allowlist.txt", "w") as f:
        f.write("# comment\nC:\\allowed\\file.txt\n")
    os.mkdir("app")
    with open("app" + "\\dependentlibs.list", "w") as f:
        f.write("xul.dll\n")
    return tmp_path


def run_etlparser(**kwargs):
    args = dict(
        xperf_path="xperf.exe",
        etl_filename="test.etl",
        processID=100,
        approot="app",
        outputFile="out.csv",
        allowlist_file="allowlist.txt",
        error_filename="errors.txt",
    )
    args.update(kwargs)
    with mock.patch.object(etlparser.subprocess, "call") as call:
        etlparser.etlparser(**args)
    return call


def read_lines(path):
    with open(path) as f:
        return f.read().splitlines()


def output_rows(path):
    lines = read_lines(path)
    assert lines[0] == OUTPUT_HEADER
    return {tuple(line.split(", ")[:3]) for line in lines[1:]}


def test_etlparser_writes_filtered_rows_thread_stats_and_errors(workdir, capsys):
    call = run_etlparser()

    assert call.call_args_list == [
        mock.call([
            "xperf.exe",
            "-merge",
            "test.etl.user",
            "test.etl.kernel",
            "test.etl",
        ]),
        mock.call(["xperf.exe", "-i", "test.etl", "-o", "test.etl.csv"]),
    ]
    assert etlparser.getBrowserPID() == 200
    assert etlparser.gThreads == {"11": "main", "12": "nonmain"}

    stats = read_lines("out_thread_stats.csv")
    assert stats[0] == "thread, stage, counter, value"
    assert set(stats[1:]) == EXPECTED_THREAD_STATS
    assert stats[1:] == sorted(stats[1:])

    profile = "C:\\Users\\me\\AppData\\Profile\\prefs.js"
    desktop = f"C:\\Users\\me\\Desktop\\{UUID_NAME}.tmp"
    assert output_rows("out.csv") == {
        (profile, "11 (main)", "startup"),
        ("C:\\Windows\\Prefetch\\FIREFOX.EXE-1A2B.pf", "11 (main)", "startup"),
        (
            "C:\\Program Files (x86)\\Mozilla Firefox\\Crash Reports\\InstallTime20240101",
            "11 (main)",
            "startup",
        ),
        ("C:\\Program Files\\Mozilla Firefox\\xul.dll", "11 (main)", "startup"),
        (desktop, "11 (main)", "startup"),
    }

    errors = read_lines("errors.txt")
    assert len(errors) == 1
    assert errors[0].startswith(DESKTOP_ERROR)
    out = capsys.readouterr().out
    assert "TEST-UNEXPECTED-FAIL : xperf: " + DESKTOP_ERROR in out


def test_etlparser_all_stages_and_threads(workdir):
    run_etlparser(all_stages=True, all_threads=True, allowlist_file=None)

    rows = output_rows("out.csv")
    assert ("C:\\Windows\\x.dll", "12", "startup") in rows
    assert ("C:\\normal.txt", "11 (main)", "normal") in rows
    assert ("C:\\shutdown.txt", "11 (main)", "shutdown") in rows
    assert ("C:\\allowed\\file.txt", "11 (main)", "startup") in rows
    assert ("C:\\Windows\\ignored.dll", "13", "startup") not in rows

    errors = read_lines("errors.txt")
    assert any("'c:\\allowed\\file.txt'" in e for e in errors)
    assert any("'c:\\normal.txt'" in e for e in errors)
    assert any("'c:\\windows\\x.dll'" in e for e in errors)


def test_etlparser_debug_uploads(workdir, capsys, monkeypatch):
    upload_dir = workdir / "upload"
    upload_dir.mkdir()
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(upload_dir))

    run_etlparser(debug=True)

    rows = output_rows("out.csv")
    assert ("C:\\Windows\\x.dll", "12", "startup") in rows
    assert ("C:\\normal.txt", "11 (main)", "normal") in rows
    assert ("C:\\shutdown.txt", "11 (main)", "shutdown") not in rows
    assert ("C:\\allowed\\file.txt", "11 (main)", "startup") not in rows
    assert sorted(os.listdir(upload_dir)) == [
        "out.csv.log",
        "out_thread_stats.csv.log",
        "test.etl.csv.log",
        "test.etl.log",
    ]
    out = capsys.readouterr().out
    assert "executing 'xperf.exe -merge" in out
    assert "uploading raw file test.etl.csv via blobber" in out


def test_etlparser_to_stdout_without_errors(workdir, capsys):
    with open("test.etl.csv", "w", encoding="cp1252") as f:
        f.write(HEADER + "P-Start, 1, firefox.exe ( 200), 100, firefox.exe\n")

    run_etlparser(outputFile=None, error_filename=None, approot=None)

    out = capsys.readouterr().out
    assert "thread, stage, counter, value\n" in out
    assert OUTPUT_HEADER in out
    assert "TEST-UNEXPECTED-FAIL" not in out
    assert not os.path.exists("errors.txt")


def test_etlparser_errors_without_error_file(workdir, capsys):
    run_etlparser(error_filename=None, approot=None)
    assert "TEST-UNEXPECTED-FAIL" in capsys.readouterr().out
    assert not os.path.exists("errors.txt")


def test_upload_file_prints_nothing_without_upload_dir(workdir, monkeypatch, capsys):
    etlparser.uploadFile("test.etl")
    assert capsys.readouterr().out == ""


class _Rows:
    def __init__(self, items):
        self.items = list(items)

    def __iter__(self):
        return self

    def __next__(self):
        if not self.items:
            raise StopIteration
        item = self.items.pop(0)
        if isinstance(item, Exception):
            raise item
        return item


def test_filter_out_header_skips_bad_rows():
    rows = _Rows([
        ["x"],
        ["BeginHeader"],
        UnicodeDecodeError("cp1252", b"\x81", 0, 1, "undefined"),
        ["P-Start", "TimeStamp"],
        etlparser.csv.Error("bad"),
        [],
        ["EndHeader"],
        ["dropped"],
        ["P-Start", "1"],
    ])
    assert list(etlparser.filterOutHeader(rows)) == [["P-Start", "1"]]
    assert etlparser.gHeaders == {"P-Start": ["P-Start", "TimeStamp"]}


def test_get_index_uses_fallback_name_and_returns_none_if_missing():
    etlparser.gHeaders["Evt"] = ["Evt", "A", "B"]
    assert etlparser.getIndex("Evt", "B") == 2
    assert etlparser.getIndex("Evt", "Z", "A") == 1
    assert etlparser.getIndex("Evt", "Z") is None


def test_file_summary_ignores_events_without_filename():
    etlparser.gHeaders["FileIoRead"] = ["FileIoRead", "TimeStamp", "P", "ThreadID"]
    files = {}
    etlparser.fileSummary(["FileIoRead", "1", "p", "11"], 0, files)
    etlparser.fileSummary(["T-Start", "1", "p", "11"], 0, files)
    assert files == {}


def test_track_thread_net_io_without_slash_raises():
    etlparser.gHeaders[TCPIP] = [TCPIP, "TimeStamp", "P", "ThreadID", "etw:ActivityId"]
    etlparser.gThreads["11"] = "main"
    with pytest.raises(xtalos.XTalosError, match="Could not find a regular"):
        etlparser.trackThreadNetIO([TCPIP, "1", "p", "11", "{a}"], {}, 0)


def test_update_stage_only_moves_forward():
    etlparser.gHeaders["UnknownEvent/Classic"] = ["UnknownEvent/Classic", "EventGuid"]
    row = lambda guid: ["UnknownEvent/Classic", guid]  # noqa: E731
    assert etlparser.updateStage(row(RESTORED), 0) == 1
    assert etlparser.updateStage(row(SHUTDOWN), 0) == 0
    assert etlparser.updateStage(row(SHUTDOWN), 1) == 2
    assert etlparser.updateStage(row(RESTORED), 2) == 2


def test_etl2csv_runs_merge_and_convert_and_returns_csv_name(capsys):
    with mock.patch.object(etlparser.subprocess, "call") as call:
        assert etlparser.etl2csv("xperf.exe", "a.etl") == "a.etl.csv"
    assert call.call_count == 2
    assert capsys.readouterr().out == ""


def test_load_allowlist_parses_entries_and_expands_env_vars(
    tmp_path, monkeypatch, capsys
):
    assert etlparser.loadAllowlist(None) is None
    assert etlparser.loadAllowlist(str(tmp_path / "missing.txt")) is None
    assert "xperf allowlist" in capsys.readouterr().out

    monkeypatch.setenv("XT_TEST_DIR", "C:\\expanded")
    path = tmp_path / "allow.txt"
    path.write_text(
        "# a comment\n"
        "C:\\file.txt\n"
        "  C:\\dir\\*  \n"
        "C:\\tree\\*\\*\n"
        "${XT_TEST_DIR}\\f.txt\n"
    )
    files, dirs, recur = etlparser.loadAllowlist(str(path))
    assert files == {"C:\\file.txt", "C:\\expanded\\f.txt"}
    assert dirs == {"C:\\dir"}
    assert recur == {"C:\\tree"}


def test_check_allowlist_matches_files_dirs_and_trees():
    allowlist = ({"C:\\file.txt"}, {"C:\\dir"}, {"C:\\tree"})
    with mock.patch.object(etlparser, "os", types.SimpleNamespace(path=ntpath)):
        assert etlparser.checkAllowlist("C:\\file.txt", None) is False
        assert etlparser.checkAllowlist("C:\\file.txt", allowlist) is True
        assert etlparser.checkAllowlist("C:\\dir\\a.txt", allowlist) is True
        assert etlparser.checkAllowlist("C:\\dir\\sub\\a.txt", allowlist) is False
        assert etlparser.checkAllowlist("C:\\tree\\a\\b\\c.txt", allowlist) is True
        assert etlparser.checkAllowlist("C:\\other\\c.txt", allowlist) is False


def write_config(tmp_path, **values):
    path = tmp_path / "config.json"
    path.write_text(json.dumps(values))
    return str(path)


def test_etlparser_from_config_passes_config_values_to_etlparser(tmp_path):
    xperf = tmp_path / "xperf.exe"
    xperf.write_text("")
    config = write_config(
        tmp_path, processID=42, xperf_path=str(xperf), etl_filename="x.etl"
    )
    with mock.patch.object(etlparser, "etlparser") as parse:
        etlparser.etlparser_from_config(config, approot="app")
    parse.assert_called_once_with(
        xperf_path=str(xperf),
        etl_filename="x.etl",
        outputFile="etl_output.csv",
        processID=42,
        approot="app",
        allowlist_file=None,
        error_filename=None,
        all_stages=False,
        all_threads=False,
        configFile=config,
    )


def test_etlparser_from_config_errors(tmp_path):
    with pytest.raises(xtalos.XTalosError, match="No process ID"):
        etlparser.etlparser_from_config(write_config(tmp_path))
    config = write_config(tmp_path, processID=1, xperf_path=str(tmp_path / "nope"))
    with pytest.raises(xtalos.XTalosError, match="does not exist"):
        etlparser.etlparser_from_config(config)


def test_main_passes_parsed_arguments_to_etlparser(tmp_path):
    xperf = tmp_path / "xperf.exe"
    xperf.write_text("")
    argv = ["--pid", "100", "-x", str(xperf), "-o", "o.csv", "-d", "3", "-i", "-t"]
    with mock.patch.object(etlparser, "etlparser") as parse:
        etlparser.main(argv)
    args = parse.call_args.args
    assert args[:2] == (os.path.abspath(str(xperf)), "test.etl")
    assert int(args[2]) == 100
    assert args[3:] == (None, None, "o.csv", None, None, True, True)
    assert parse.call_args.kwargs == {"debug": True}


def test_main_exits_without_pid_or_with_invalid_xperf_path(tmp_path, capsys):
    xperf = tmp_path / "xperf.exe"
    xperf.write_text("")
    with pytest.raises(SystemExit):
        etlparser.main(["-x", str(xperf)])
    assert "No process ID argument given" in capsys.readouterr().err
    with pytest.raises(SystemExit):
        etlparser.main(["--pid", "1", "-x", str(tmp_path / "nope")])
    assert "Unable to verify arguments" in capsys.readouterr().err


def test_running_module_as_script_exits_on_invalid_xperf_path(tmp_path):
    argv = ["etlparser.py", "-x", str(tmp_path / "nope")]
    with mock.patch.object(sys, "argv", argv), pytest.raises(SystemExit):
        runpy.run_path(etlparser.__file__, run_name="__main__")


if __name__ == "__main__":
    mozunit.main()
