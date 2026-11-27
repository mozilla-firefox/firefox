import csv
import json
import os
import runpy
import sys
from unittest import mock

import mozunit
import pytest

import talos.xtalos.etlparser  # noqa: F401
from talos import xtalos as xtalos_package

XTALOS_SCRIPT_DIR_FIRST_ON_PATH = os.path.dirname(xtalos_package.__file__)

with mock.patch.object(sys, "path", [XTALOS_SCRIPT_DIR_FIRST_ON_PATH, *sys.path]):
    with mock.patch.dict(sys.modules):
        import parse_xperf
        import start_xperf

HEADER = """\
BeginHeader
P-Start, TimeStamp, Process Name ( PID), ParentPID, Command Line
UnknownEvent/Classic, TimeStamp, Process Name ( PID), ThreadID, EventGuid
EndHeader
dropped
"""


class TextXPerfFile(parse_xperf.XPerfFile):
    def load(self):
        self.csv_fd = open(self.csvfile)
        self.data = self.filter_xperf_header(
            csv.reader(
                self.csv_fd,
                delimiter=",",
                quotechar='"',
                quoting=csv.QUOTE_NONE,
                skipinitialspace=True,
            )
        )
        return True


@pytest.fixture(autouse=True)
def reset_cached_indices():
    classes = [parse_xperf.ProcessStart, parse_xperf.SessionStoreWindowRestored]
    names = ["timestamp_index", "guid_index", "process_index", "cmd_line_index"]

    def reset():
        for cls in classes:
            for name in names:
                for klass in cls.__mro__:
                    if name in vars(klass):
                        setattr(klass, name, None)

    reset()
    yield
    reset()


@pytest.fixture
def xperf_exe(tmp_path):
    path = tmp_path / "xperf.exe"
    path.write_text("")
    return str(path)


def write_config(tmp_path, **values):
    path = tmp_path / "bcontroller.json"
    path.write_text(json.dumps(values))
    return str(path)


def test_start_runs_xperf_with_kernel_and_user_providers(capsys):
    with mock.patch.object(start_xperf.subprocess, "call") as call:
        start_xperf.start(
            "xperf.exe",
            ["PROC_THREAD", "LOADER"],
            ["FileRead"],
            ["Prov"],
            "t.etl",
            True,
        )
    call.assert_called_once_with([
        "xperf.exe",
        "-on",
        "PROC_THREAD+LOADER",
        "-stackwalk",
        "FileRead",
        "-MaxBuffers",
        "1024",
        "-BufferSize",
        "1024",
        "-f",
        "t.etl.kernel",
        "-start",
        "talos_ses",
        "-on",
        "Prov",
        "-MaxBuffers",
        "1024",
        "-BufferSize",
        "1024",
        "-f",
        "t.etl.user",
    ])
    assert "executing 'xperf.exe -on PROC_THREAD+LOADER" in capsys.readouterr().out


def test_start_from_config_passes_config_values_to_start(tmp_path, xperf_exe):
    config = write_config(
        tmp_path,
        xperf_path=xperf_exe,
        xperf_providers=["A"],
        xperf_user_providers=["B"],
        xperf_stackwalk=["C"],
        etl_filename="t.etl",
    )
    with mock.patch.object(start_xperf, "start") as start:
        start_xperf.start_from_config(config_file=config, debug=True)
    start.assert_called_once_with(
        xperf_path=xperf_exe,
        xperf_providers=["A"],
        xperf_user_providers=["B"],
        xperf_stackwalk=["C"],
        etl_filename="t.etl",
        debug=True,
    )


def test_start_from_config_raises_for_missing_or_nonexistent_xperf(tmp_path):
    with pytest.raises(start_xperf.xtalos.XTalosError, match="xperf_path not given"):
        start_xperf.start_from_config()
    with pytest.raises(start_xperf.xtalos.XTalosError, match="does not exist"):
        start_xperf.start_from_config(
            xperf_path=str(tmp_path / "nope"),
            xperf_providers=["A"],
            xperf_user_providers=["B"],
            xperf_stackwalk=["C"],
            etl_filename="t.etl",
        )


def test_start_main_parses_arguments_and_requires_providers(
    tmp_path, xperf_exe, capsys
):
    with mock.patch.object(start_xperf, "start") as start:
        start_xperf.main([
            "-x",
            xperf_exe,
            "-r",
            "A",
            "--user-providers",
            "B",
            "-s",
            "C",
            "-d",
            "3",
        ])
    assert start.call_args.kwargs["etl_filename"] == "test.etl"
    assert start.call_args.kwargs["debug"] is True

    with pytest.raises(SystemExit):
        start_xperf.main(["-x", xperf_exe])
    assert "No xperf providers given" in capsys.readouterr().err


def test_running_start_xperf_as_script_exits_on_invalid_path(tmp_path):
    argv = ["start_xperf.py", "-x", str(tmp_path / "nope"), "-r", "A"]
    with mock.patch.object(sys, "argv", argv):
        with mock.patch.object(
            sys, "path", [XTALOS_SCRIPT_DIR_FIRST_ON_PATH, *sys.path]
        ):
            with mock.patch.dict(sys.modules):
                with pytest.raises(SystemExit):
                    runpy.run_path(start_xperf.__file__, run_name="__main__")


def test_stop_runs_xperf_stop_for_talos_session(capsys):
    with mock.patch.object(parse_xperf.subprocess, "call") as call:
        parse_xperf.stop("xperf.exe", debug=True)
    call.assert_called_once_with(["xperf.exe", "-stop", "-stop", "talos_ses"])
    assert "executing 'xperf.exe -stop -stop talos_ses'" in capsys.readouterr().out


def test_session_restore_analysis_writes_window_restored_time(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    with open("in.csv", "w") as f:
        f.write(
            HEADER
            + "P-Start, 1000, firefox.exe ( 10), 1, firefox.exe\n"
            + "UnknownEvent/Classic, 4250, firefox.exe ( 10), 2, "
            + parse_xperf.etlparser.CEVT_WINDOWS_RESTORED
            + "\n"
        )
    with mock.patch.object(parse_xperf, "XPerfFile", TextXPerfFile):
        parse_xperf.run_session_restore_analysis(
            csv_filename="in.csv", outputFile="etl_output.csv", debug=True
        )
    with open("etl_output_session_restore_stats.csv") as f:
        assert f.read() == "time_to_session_store_window_restored_ms\n3.250\n"
    assert os.path.exists("in.csv")


def test_session_restore_analysis_without_restore(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    with open("in.csv", "w") as f:
        f.write(HEADER + "P-Start, 1000, other.exe ( 10), 1, other.exe\n")
    with mock.patch.object(parse_xperf, "XPerfFile", TextXPerfFile):
        parse_xperf.run_session_restore_analysis(
            csv_filename="in.csv", outputFile="out.csv"
        )
    with open("out_session_restore_stats.csv") as f:
        assert f.read() == "time_to_session_store_window_restored_ms\n"


def test_session_restore_analysis_requires_args():
    with pytest.raises(parse_xperf.xtalos.XTalosError, match="csv_filename required"):
        parse_xperf.run_session_restore_analysis(outputFile="o.csv")
    with pytest.raises(parse_xperf.xtalos.XTalosError, match="outputFile required"):
        parse_xperf.run_session_restore_analysis(csv_filename="i.csv")


def stop_kwargs(xperf_exe):
    return dict(
        xperf_path=xperf_exe,
        etl_filename="t.etl",
        approot="app",
        error_filename="err.txt",
        processID=5,
        outputFile="etl_output.csv",
    )


@pytest.mark.parametrize("debug", [False, True])
def test_stop_from_config_stops_xperf_then_parses_and_analyzes(
    tmp_path, monkeypatch, xperf_exe, debug
):
    monkeypatch.chdir(tmp_path)
    config = write_config(tmp_path, etl_filename="t.etl")
    with open("t.etl.csv", "w") as f:
        f.write(HEADER)
    with mock.patch.object(parse_xperf, "stop") as stop:
        with mock.patch.object(parse_xperf.etlparser, "etlparser_from_config") as parse:
            with mock.patch.object(
                parse_xperf, "run_session_restore_analysis"
            ) as analyze:
                parse_xperf.stop_from_config(
                    config_file=config, debug=debug, **stop_kwargs(xperf_exe)
                )
    stop.assert_called_once_with(xperf_path=xperf_exe, debug=debug)
    parse.assert_called_once_with(
        config, approot="app", error_filename="err.txt", processID=5
    )
    assert analyze.call_args.kwargs["csv_filename"] == "t.etl.csv"
    assert analyze.call_args.kwargs["outputFile"] == "etl_output.csv"
    assert os.path.exists("t.etl.csv") is debug


def test_stop_from_config_raises_for_missing_or_nonexistent_xperf(tmp_path):
    with pytest.raises(parse_xperf.xtalos.XTalosError, match="xperf_path not given"):
        parse_xperf.stop_from_config()
    with pytest.raises(parse_xperf.xtalos.XTalosError, match="does not exist"):
        parse_xperf.stop_from_config(
            xperf_path=str(tmp_path / "nope"), etl_filename="t.etl"
        )


def test_parse_main_passes_config_and_debug_to_stop_from_config(
    tmp_path, xperf_exe, capsys
):
    config = write_config(tmp_path, xperf_path=xperf_exe)
    with mock.patch.object(parse_xperf, "stop_from_config") as stop_from_config:
        parse_xperf.main(["-c", config, "-d", "3"])
    kwargs = stop_from_config.call_args.kwargs
    assert kwargs["config_file"] == config
    assert kwargs["debug"] is True

    with pytest.raises(SystemExit):
        parse_xperf.main(["-x", str(tmp_path / "nope")])
    assert "does not exist" in capsys.readouterr().err


def test_running_parse_xperf_as_script_exits_on_invalid_path(tmp_path):
    argv = ["parse_xperf.py", "-x", str(tmp_path / "nope")]
    with mock.patch.object(sys, "argv", argv):
        with mock.patch.object(
            sys, "path", [XTALOS_SCRIPT_DIR_FIRST_ON_PATH, *sys.path]
        ):
            with mock.patch.dict(sys.modules):
                with pytest.raises(SystemExit):
                    runpy.run_path(parse_xperf.__file__, run_name="__main__")


if __name__ == "__main__":
    mozunit.main()
