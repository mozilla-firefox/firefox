# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import itertools
import os
import sys
import zipfile
from pathlib import Path
from subprocess import CompletedProcess
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import raptor_profiling
import simpleperf
from simpleperf import SimpleperfProfile


@pytest.fixture(autouse=True)
def env():
    with mock.patch.dict(os.environ):
        os.environ.pop("MOZ_AUTOMATION", None)
        os.environ.pop("MOZ_FETCHES_DIR", None)
        yield os.environ


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    fake = mock.MagicMock()
    monkeypatch.setattr(simpleperf, "LOG", fake)
    return fake


@pytest.fixture(autouse=True)
def temp_dirs(tmp_path, monkeypatch):
    counter = itertools.count()

    def fake_mkdtemp(*args, **kwargs):
        path = tmp_path / f"tmp{next(counter)}"
        path.mkdir()
        return str(path)

    monkeypatch.setattr(raptor_profiling.tempfile, "mkdtemp", fake_mkdtemp)


@pytest.fixture
def upload_dir(tmp_path):
    path = tmp_path / "upload"
    path.mkdir()
    return path


@pytest.fixture
def fetches(tmp_path, env):
    path = tmp_path / "fetches"
    path.mkdir()
    env["MOZ_AUTOMATION"] = "1"
    env["MOZ_FETCHES_DIR"] = str(path)
    return path


@pytest.fixture
def run(monkeypatch):
    calls = []
    state = {"stdout": "", "stderr": "", "returncode": 0, "write": True}

    def fake_run(cmd, **kwargs):
        calls.append((cmd, kwargs))
        if state["write"]:
            Path(cmd[cmd.index("-o") + 1]).write_text('{"meta": {}}')
        return CompletedProcess(
            cmd, state["returncode"], stdout=state["stdout"], stderr=state["stderr"]
        )

    monkeypatch.setattr(simpleperf.subprocess, "run", fake_run)
    fake_run.calls = calls
    fake_run.state = state
    return fake_run


def log_messages(fake, level):
    return [c.args[0] for c in getattr(fake, level).call_args_list]


def make_profiler(upload_dir, raptor_config=None, test_config=None):
    if test_config is None:
        test_config = {"name": "speedometer3"}
    return SimpleperfProfile(str(upload_dir), raptor_config or {}, test_config)


def make_local_tools(profiler, tmp_path):
    profiler.breakpad_symbol_dir = tmp_path / "symbols"
    profiler.breakpad_symbol_dir.mkdir()
    profiler.samply_path = tmp_path / "samply"
    profiler.samply_path.write_bytes(b"")


def make_perf_data(profiler, name="cycle-1"):
    folder = profiler.dest_dir / name
    folder.mkdir(parents=True)
    perf = folder / "perf.data"
    perf.write_bytes(b"data")
    return perf


def test_init_local(upload_dir):
    """Check that a local run has no symbols or samply and writes under the test name."""
    profiler = make_profiler(upload_dir)
    assert profiler.upload_dir == upload_dir
    assert profiler.breakpad_symbol_dir is None
    assert profiler.samply_path is None
    assert profiler.dest_dir == upload_dir / "browsertime-results" / "speedometer3"
    assert Path(profiler.temp_profile_dir).is_dir()


def test_init_default_name(upload_dir):
    """Check that the results directory falls back to simpleperf without a test name."""
    profiler = make_profiler(upload_dir, test_config={})
    assert profiler.dest_dir == upload_dir / "browsertime-results" / "simpleperf"


def test_init_automation(upload_dir, fetches):
    """Check that automation takes symbols and samply from MOZ_FETCHES_DIR."""
    profiler = make_profiler(upload_dir)
    assert profiler.breakpad_symbol_dir == fetches / "target.crashreporter-symbols"
    assert profiler.samply_path == fetches / "samply" / "samply"


def test_symbolicate_no_symbol_dir(upload_dir, mock_log, run):
    """Check that symbolication is skipped when no symbols directory is set."""
    make_profiler(upload_dir).symbolicate()
    assert log_messages(mock_log, "info") == [
        "symbols directory not set, skipping symbolication"
    ]
    assert run.calls == []


def test_symbolicate_no_samply_path(upload_dir, tmp_path, mock_log, run):
    """Check that symbolication is skipped when no samply path is set."""
    profiler = make_profiler(upload_dir)
    profiler.breakpad_symbol_dir = tmp_path
    profiler.symbolicate()
    assert log_messages(mock_log, "info") == ["samply not set, skipping symbolication"]
    assert run.calls == []


def test_symbolicate_samply_missing(upload_dir, fetches, mock_log, run):
    """Check that symbolication is skipped when the samply binary is missing."""
    make_profiler(upload_dir).symbolicate()
    assert log_messages(mock_log, "info") == [
        "samply not found, skipping symbolication"
    ]
    assert run.calls == []


def test_symbolicate_symbol_dir_missing(upload_dir, tmp_path, mock_log, run):
    """Check that symbolication is skipped when the symbols directory is missing."""
    profiler = make_profiler(upload_dir)
    profiler.breakpad_symbol_dir = tmp_path / "nope"
    profiler.samply_path = tmp_path / "samply"
    profiler.samply_path.write_bytes(b"")
    make_perf_data(profiler)
    profiler.symbolicate()
    assert log_messages(mock_log, "info") == [
        "symbols directory not found, skipping symbolication"
    ]
    assert run.calls == []


def test_symbolicate_no_perf_files(upload_dir, tmp_path, mock_log, run):
    """Check that an error is logged when no perf.data file exists."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    profiler.symbolicate()
    assert log_messages(mock_log, "error") == [
        f"perf.data not found at {profiler.dest_dir}, skipping symbolication"
    ]
    assert run.calls == []


def test_symbolicate_local_command_line(upload_dir, tmp_path, mock_log, run):
    """Check that samply import gets the perf file, output, symbols and aux dir."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    perf = make_perf_data(profiler)
    profiler.symbolicate()

    profile = perf.parent / "cycle-1.json"
    assert len(run.calls) == 1
    cmd, kwargs = run.calls[0]
    assert cmd[:3] == [str(profiler.samply_path), "import", str(perf)]
    options = dict(zip(cmd, cmd[1:]))
    assert options["-o"] == str(profile)
    assert options["--breakpad-symbol-dir"] == str(profiler.breakpad_symbol_dir)
    assert options["--aux-file-dir"] == str(perf.parent)
    assert options["--name"] == "org.mozilla.fenix"
    assert kwargs["check"] is False
    assert profile.exists()
    assert not perf.exists()
    assert log_messages(mock_log, "error") == []
    assert log_messages(mock_log, "info") == [
        f"Profile converted: {profile} ({profile.stat().st_size} bytes)"
    ]
    assert not (upload_dir / "profile_speedometer3.zip").exists()


def test_symbolicate_uses_binary_name(upload_dir, tmp_path, run):
    """Check that the configured binary is passed to samply as the name."""
    profiler = make_profiler(
        upload_dir, raptor_config={"binary": "org.mozilla.geckoview_example"}
    )
    make_local_tools(profiler, tmp_path)
    make_perf_data(profiler)
    profiler.symbolicate()
    assert run.calls[0][0][-2:] == ["--name", "org.mozilla.geckoview_example"]


def test_symbolicate_multiple_and_nested(upload_dir, tmp_path, run):
    """Check that every perf.data, including nested ones, is converted."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    perf_a = make_perf_data(profiler, "a")
    perf_b = make_perf_data(profiler, os.path.join("b", "nested"))
    profiler.symbolicate()
    inputs = sorted(cmd[2] for cmd, _ in run.calls)
    assert inputs == sorted([str(perf_a), str(perf_b)])
    assert (perf_a.parent / "a.json").exists()
    assert (perf_b.parent / "nested.json").exists()


def test_symbolicate_logs_output_and_failure(upload_dir, tmp_path, mock_log, run):
    """Check that samply output, exit code and a missing profile are logged."""
    run.state.update(stdout="out1\nout2", stderr="err1", returncode=3, write=False)
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    perf = make_perf_data(profiler)
    profiler.symbolicate()

    profile = perf.parent / "cycle-1.json"
    assert log_messages(mock_log, "info") == [
        "samply stdout: out1",
        "samply stdout: out2",
        "samply stderr: err1",
    ]
    assert log_messages(mock_log, "error") == [
        "samply exited with code 3",
        f"samply did not produce a profile at {profile}",
    ]
    assert not perf.exists()


def test_symbolicate_cleans_markers_and_jitdumps(upload_dir, tmp_path, run):
    """Check that perf, marker and jitdump files are removed but others kept."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    perf = make_perf_data(profiler)
    folder = perf.parent
    (folder / "marker-1234.txt").write_text("m")
    (folder / "jit").mkdir()
    (folder / "jit" / "jit-5678.dump").write_bytes(b"j")
    (folder / "keep.txt").write_text("k")
    profiler.symbolicate()
    assert sorted(p.name for p in folder.rglob("*") if p.is_file()) == [
        "cycle-1.json",
        "keep.txt",
    ]


def test_symbolicate_cleans_up_when_samply_raises(upload_dir, tmp_path, monkeypatch):
    """Check that perf and marker files are removed even when samply raises."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    perf = make_perf_data(profiler)
    marker = perf.parent / "marker-1.txt"
    marker.write_text("m")
    monkeypatch.setattr(
        simpleperf.subprocess, "run", mock.Mock(side_effect=OSError("boom"))
    )
    with pytest.raises(OSError, match="boom"):
        profiler.symbolicate()
    assert not perf.exists()
    assert not marker.exists()


def make_automation_tools(fetches):
    samply = fetches / "samply" / "samply"
    samply.parent.mkdir()
    samply.write_bytes(b"")
    symbols = fetches / "target.crashreporter-symbols"
    with zipfile.ZipFile(f"{symbols}.zip", "w") as zipf:
        zipf.writestr("libxul.so/ABC/libxul.so.sym", "MODULE")
    return symbols


def test_symbolicate_automation_extracts_and_archives(
    upload_dir, fetches, mock_log, run
):
    """Check that automation extracts symbols and archives profiles into a zip."""
    symbols = make_automation_tools(fetches)
    profiler = make_profiler(upload_dir)
    perf_a = make_perf_data(profiler, "a")
    perf_b = make_perf_data(profiler, "b")
    profiler.symbolicate()

    assert (symbols / "libxul.so" / "ABC" / "libxul.so.sym").read_text() == "MODULE"
    assert run.calls[0][0][0] == str(fetches / "samply" / "samply")
    archive = upload_dir / "profile_speedometer3.zip"
    with zipfile.ZipFile(archive) as zipf:
        assert sorted(zipf.namelist()) == ["simpleperf/a.json", "simpleperf/b.json"]
        assert zipf.read("simpleperf/a.json") == b'{"meta": {}}'
        assert all(i.compress_type == zipfile.ZIP_DEFLATED for i in zipf.infolist())
    assert not (perf_a.parent / "a.json").exists()
    assert not (perf_b.parent / "b.json").exists()
    assert not perf_a.exists()
    assert log_messages(mock_log, "info")[-1] == (
        f"Profiles archived to: {archive} ({archive.stat().st_size} bytes)"
    )
    assert log_messages(mock_log, "error") == []


def test_symbolicate_automation_appends_to_existing_archive(upload_dir, fetches, run):
    """Check that profiles are appended to an existing profile archive."""
    make_automation_tools(fetches)
    archive = upload_dir / "profile_speedometer3.zip"
    with zipfile.ZipFile(archive, "w") as zipf:
        zipf.writestr("gecko/profile.json", "{}")
    profiler = make_profiler(upload_dir)
    make_perf_data(profiler)
    profiler.symbolicate()
    with zipfile.ZipFile(archive) as zipf:
        assert sorted(zipf.namelist()) == [
            "gecko/profile.json",
            "simpleperf/cycle-1.json",
        ]


def test_symbolicate_automation_without_symbol_zip(upload_dir, fetches, mock_log, run):
    """Check that automation skips symbolication when no symbols zip exists."""
    samply = fetches / "samply" / "samply"
    samply.parent.mkdir()
    samply.write_bytes(b"")
    profiler = make_profiler(upload_dir)
    profiler.symbolicate()
    assert log_messages(mock_log, "info") == [
        "symbols directory not found, skipping symbolication"
    ]


def test_symbolicate_automation_archive_failure(upload_dir, fetches, mock_log, run):
    """Check that a failure is logged when no profile reaches the archive."""
    make_automation_tools(fetches)
    run.state["write"] = False
    profiler = make_profiler(upload_dir)
    make_perf_data(profiler)
    profiler.symbolicate()
    archive = upload_dir / "profile_speedometer3.zip"
    assert not archive.exists()
    assert log_messages(mock_log, "error")[-1] == (
        f"Failed to archive profiles to {archive}"
    )


def test_clean_removes_symbols_and_temp_dir(upload_dir, tmp_path):
    """Check that clean removes the symbols and temporary profile directories."""
    profiler = make_profiler(upload_dir)
    make_local_tools(profiler, tmp_path)
    (profiler.breakpad_symbol_dir / "sub").mkdir()
    temp_dir = Path(profiler.temp_profile_dir)
    profiler.clean()
    assert not profiler.breakpad_symbol_dir.exists()
    assert not temp_dir.exists()


def test_clean_without_symbols(upload_dir, tmp_path):
    """Check that clean works with no or a missing symbols directory."""
    profiler = make_profiler(upload_dir)
    temp_dir = Path(profiler.temp_profile_dir)
    profiler.clean()
    assert not temp_dir.exists()

    profiler = make_profiler(upload_dir)
    profiler.breakpad_symbol_dir = tmp_path / "missing"
    profiler.clean()
    assert not Path(profiler.temp_profile_dir).exists()


if __name__ == "__main__":
    mozunit.main()
