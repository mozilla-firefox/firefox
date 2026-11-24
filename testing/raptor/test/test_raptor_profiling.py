# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gzip
import json
import os
import sys
import tarfile
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
from raptor_profiling import (
    RaptorProfiling,
    _profile_archive_path,
    _profile_archive_paths,
)


@pytest.fixture(autouse=True)
def mock_log():
    with mock.patch("raptor_profiling.LOG") as log:
        yield log


class FakeProfiler(RaptorProfiling):
    profile_entry_string = "geckoProfiles"

    def __init__(self, *args, extra_profiler_run=False, **kwargs):
        super().__init__(*args, **kwargs)
        self._is_extra_profiler_run = extra_profiler_run


@pytest.fixture
def make_profiler(tmp_path):
    created = []

    def _make(raptor_config=None, test_config=None, **kwargs):
        upload_dir = tmp_path / "upload"
        upload_dir.mkdir(exist_ok=True)
        profiler = FakeProfiler(
            upload_dir, raptor_config or {}, test_config or {}, **kwargs
        )
        created.append(profiler)
        return profiler

    yield _make
    for profiler in created:
        profiler.clean()


def write_browsertime_json(path, profiles, key="geckoProfiles"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps([{"files": {key: profiles}}]))


def bt_config(result_dir, **kwargs):
    config = {"browsertime": True, "browsertime_result_dir": str(result_dir)}
    config.update(kwargs)
    return config


@pytest.mark.parametrize(
    "test_config,profile_type,expected",
    [
        (
            {"name": "amazon"},
            "cold",
            os.path.join("amazon-pageload-cold", "browser-cycle-1.json.gz"),
        ),
        (
            {"name": "amazon", "type": "pageload"},
            "warm",
            os.path.join("amazon-pageload-warm", "page-cycle-1.json.gz"),
        ),
        (
            {"name": "sp3", "type": "benchmark"},
            "benchmark",
            os.path.join("sp3-benchmark", "iteration-1.json.gz"),
        ),
        (
            {"name": "sc", "type": "scenario"},
            "cold",
            os.path.join("sc-scenario", "iteration-1.json.gz"),
        ),
    ],
)
def test_profile_archive_path(test_config, profile_type, expected):
    """Check that the archive folder and file names follow the test and cycle type."""
    profile_info = {"path": "/a/b/geckoProfile-1.json.gz", "type": profile_type}
    assert _profile_archive_path(test_config, profile_info) == expected


def test_profile_archive_path_suffix_replaces_extension():
    """Check that a suffix replaces the collected profile's extension."""
    profile_info = {"path": "/a/b/geckoProfile-3.json.gz", "type": "warm"}
    assert _profile_archive_path({"name": "t"}, profile_info, ".json") == (
        os.path.join("t-pageload-warm", "page-cycle-3.json")
    )


def test_profile_archive_paths_without_collisions():
    """Check that unique archive paths are kept as-is with per-profile suffixes."""
    profiles = [
        {"path": "/r/a/data/geckoProfile-1.json.gz", "type": "warm"},
        {"path": "/r/a/data/geckoProfile-2.json.gz", "type": "warm"},
    ]
    assert _profile_archive_paths({"name": "t"}, profiles, [".json", None]) == [
        os.path.join("t-pageload-warm", "page-cycle-1.json"),
        os.path.join("t-pageload-warm", "page-cycle-2.json.gz"),
    ]


def test_profile_archive_paths_collisions_use_alias_and_dedup():
    """Check that colliding archive paths get an alias folder, numbered when repeated."""
    profiles = [
        {"path": os.path.join("r", "siteA", "data", "geckoProfile-1.json.gz")},
        {"path": os.path.join("r", "siteB", "data", "geckoProfile-1.json.gz")},
        {"path": os.path.join("s", "siteA", "data", "geckoProfile-1.json.gz")},
        {"path": os.path.join("t", "siteA", "data", "geckoProfile-1.json.gz")},
    ]
    for p in profiles:
        p["type"] = "benchmark"
    folder = "t-benchmark"
    assert _profile_archive_paths({"name": "t", "type": "benchmark"}, profiles) == [
        os.path.join(folder, "siteA", "iteration-1.json.gz"),
        os.path.join(folder, "siteB", "iteration-1.json.gz"),
        os.path.join(folder, "siteA-2", "iteration-1.json.gz"),
        os.path.join(folder, "siteA-3", "iteration-1.json.gz"),
    ]


def test_open_profile_file_plain_and_gzip(tmp_path, make_profiler):
    """Check that both plain and gzipped JSON profiles are loaded."""
    profiler = make_profiler()
    data = {"meta": {"version": 1}}
    plain = tmp_path / "profile.json"
    plain.write_text(json.dumps(data), encoding="utf-8")
    gz = tmp_path / "profile.json.gz"
    with gzip.open(gz, "wt") as f:
        json.dump(data, f)
    assert profiler._open_profile_file(str(plain)) == data
    assert profiler._open_profile_file(str(gz)) == data


def test_init_creates_temp_dir_and_clean_removes_it(make_profiler):
    """Check that the temp profile dir is created on init and removed by clean()."""
    profiler = make_profiler()
    temp_dir = Path(profiler.temp_profile_dir)
    assert temp_dir.is_dir()
    profiler.clean()
    assert not temp_dir.exists()
    profiler.clean()


def test_collect_profiles_from_tarball(tmp_path, make_profiler):
    """Check that profiles listed in the fixture's browsertime.json are collected."""
    with tarfile.open(os.path.join(here, "geckoProfileTest.tar")) as f:
        f.extractall(path=tmp_path)
    result_dir = tmp_path / "amazon"
    profiler = make_profiler(bt_config(result_dir), {"name": "amazon"})
    assert profiler.collect_profiles() == [
        {
            "path": os.path.join(
                str(result_dir),
                "pages/www_amazon_com/s/query-2f96b7ee/data/geckoProfile-1.json.gz",
            ),
            "type": "warm",
        }
    ]


@pytest.mark.parametrize(
    "raptor_extra,test_config,expected_type",
    [
        ({}, {}, "warm"),
        ({"cold": True}, {}, "cold"),
        ({}, {"cold": True}, "cold"),
        ({}, {"type": "benchmark"}, "benchmark"),
        ({}, {"type": "scenario"}, "scenario"),
    ],
)
def test_collect_profiles_test_type(
    tmp_path, make_profiler, raptor_extra, test_config, expected_type
):
    """Check that collected profiles are typed cold, warm, benchmark or scenario."""
    write_browsertime_json(tmp_path / "browsertime.json", ["p-1.json.gz"])
    profiler = make_profiler(bt_config(tmp_path, **raptor_extra), test_config)
    assert profiler.collect_profiles() == [
        {"path": os.path.join(str(tmp_path), "p-1.json.gz"), "type": expected_type}
    ]


def test_collect_profiles_extra_profiler_run_uses_profiling_dir(
    tmp_path, make_profiler
):
    """Check that an extra profiler run reads results from the profiling subdir."""
    write_browsertime_json(tmp_path / "browsertime.json", ["main.json.gz"])
    write_browsertime_json(
        tmp_path / "profiling" / "browsertime.json", ["extra.json.gz"]
    )
    profiler = make_profiler(bt_config(tmp_path), extra_profiler_run=True)
    assert profiler.collect_profiles() == [
        {
            "path": os.path.join(str(tmp_path), "profiling", "extra.json.gz"),
            "type": "warm",
        }
    ]


def test_collect_profiles_missing_result_dir(tmp_path, make_profiler, mock_log):
    """Check that a missing result directory yields no profiles."""
    profiler = make_profiler(bt_config(tmp_path / "missing"))
    assert profiler.collect_profiles() == []
    mock_log.info.assert_called_with("Could not find the result directory.")


def test_collect_profiles_no_result_json(tmp_path, make_profiler):
    """Check that a result dir without browsertime JSONs raises."""
    (tmp_path / "other.json").write_text("[]")
    profiler = make_profiler(bt_config(tmp_path))
    with pytest.raises(Exception, match="Could not find any browsertime result"):
        profiler.collect_profiles()


def test_collect_profiles_no_result_json_extra_run(tmp_path, make_profiler):
    """Check that an extra profiler run without browsertime JSONs yields nothing."""
    (tmp_path / "profiling").mkdir()
    profiler = make_profiler(bt_config(tmp_path), extra_profiler_run=True)
    assert profiler.collect_profiles() == []


def test_collect_profiles_chimera(tmp_path, make_profiler):
    """Check that chimera runs collect cold and warm profiles from their own JSONs."""
    write_browsertime_json(tmp_path / "browsertime.json", ["main.json.gz"])
    write_browsertime_json(tmp_path / "cold-browsertime.json", ["c-1.json.gz"])
    write_browsertime_json(
        tmp_path / "warm-browsertime.json", ["w-1.json.gz", "w-2.json.gz"]
    )
    profiler = make_profiler(bt_config(tmp_path, chimera=True))
    root = str(tmp_path)
    assert profiler.collect_profiles() == [
        {"path": os.path.join(root, "c-1.json.gz"), "type": "cold"},
        {"path": os.path.join(root, "w-1.json.gz"), "type": "warm"},
        {"path": os.path.join(root, "w-2.json.gz"), "type": "warm"},
    ]


def test_collect_profiles_chimera_missing_split(tmp_path, make_profiler):
    """Check that a chimera run missing the warm JSON raises."""
    write_browsertime_json(tmp_path / "cold-browsertime.json", ["c-1.json.gz"])
    profiler = make_profiler(bt_config(tmp_path, chimera=True))
    with pytest.raises(Exception, match="chimera mode"):
        profiler.collect_profiles()


def test_collect_profiles_chimera_missing_split_extra_run(tmp_path, make_profiler):
    """Check that an extra chimera run missing the cold JSON yields nothing."""
    write_browsertime_json(
        tmp_path / "profiling" / "warm-browsertime.json", ["w-1.json.gz"]
    )
    profiler = make_profiler(bt_config(tmp_path, chimera=True), extra_profiler_run=True)
    assert profiler.collect_profiles() == []


@pytest.mark.parametrize(
    "extra_run,log_method,message",
    [
        (False, "error", "Failed to find profiles."),
        (True, "info", "Failed to find profiles for extra profiler run."),
    ],
)
def test_collect_profiles_entry_without_profiles(
    tmp_path, make_profiler, mock_log, extra_run, log_method, message
):
    """Check that entries without profile files are logged and skipped."""
    bt_dir = tmp_path / "profiling" if extra_run else tmp_path
    write_browsertime_json(bt_dir / "browsertime.json", ["x"], key="screenshots")
    profiler = make_profiler(bt_config(tmp_path), extra_profiler_run=extra_run)
    assert profiler.collect_profiles() == []
    getattr(mock_log, log_method).assert_any_call(message)


def test_collect_profiles_webext_temp_dir(make_profiler):
    """Check that non-browsertime runs collect profiles from the temp dir."""
    profiler = make_profiler({}, {"type": "benchmark"})
    Path(profiler.temp_profile_dir, "profile-1.json").touch()
    assert profiler.collect_profiles() == [
        {
            "path": os.path.join(profiler.temp_profile_dir, "profile-1.json"),
            "type": "benchmark",
        }
    ]


def test_archive_adds_existing_profiles_and_skips_missing(
    tmp_path, make_profiler, mock_log
):
    """Check that archive() zips existing profiles and skips missing ones."""
    profiler = make_profiler(test_config={"name": "sp3"})
    good = tmp_path / "profile_a.jslb.gz"
    good.write_bytes(b"data")
    missing = tmp_path / "missing.jslb.gz"
    archive = profiler.archive([good, str(missing)])
    assert archive == profiler.upload_dir / "profiles_sp3.zip"
    with zipfile.ZipFile(archive) as zf:
        assert zf.namelist() == ["profile_a.jslb.gz"]
        assert zf.read("profile_a.jslb.gz") == b"data"
    mock_log.warning.assert_called_once()


def test_archive_without_profiles_raises(make_profiler):
    """Check that archive() raises when given no profiles."""
    with pytest.raises(ValueError, match="No profile"):
        make_profiler().archive([])


@pytest.fixture
def toolchain_dir(tmp_path, monkeypatch):
    toolchain_dir = tmp_path / "toolchains"
    for tool in [
        Path("node", "node.exe"),
        Path("node", "bin", "node"),
        Path("profiler-node-tools", "profiler-edit.js"),
    ]:
        (toolchain_dir / tool).parent.mkdir(parents=True, exist_ok=True)
        (toolchain_dir / tool).touch()
    monkeypatch.setenv("MOZBUILD_STATE_PATH", str(toolchain_dir))
    return toolchain_dir


def test_source_path_mappings_are_passed_to_profiler_edit(tmp_path, toolchain_dir):
    source_path_mappings = [
        {"urlPrefix": "http://127.0.0.1:62763/", "localPath": str(tmp_path)}
    ]
    upload_dir = tmp_path / "upload"
    upload_dir.mkdir()
    profiler = RaptorProfiling(
        upload_dir,
        {"run_local": True},
        {"name": "speedometer3"},
        source_path_mappings,
    )
    profiler.profile = upload_dir / "profile_unprocessed.jslb.gz"
    profiler.profile.touch()

    profiler_edit_runs = []

    def run_profiler_edit(cmd, **kwargs):
        mapping_path = Path(cmd[cmd.index("--map-source-paths") + 1])
        profiler_edit_runs.append(json.loads(mapping_path.read_text()))
        Path(cmd[cmd.index("-o") + 1]).touch()
        return CompletedProcess(cmd, 0, stdout="", stderr="")

    with mock.patch("raptor_profiling.subprocess.run", side_effect=run_profiler_edit):
        profiles = profiler.post_process_profiles()
    profiler.clean()

    assert profiles
    assert profiler_edit_runs == [{"mappings": source_path_mappings}] * len(profiles)


def make_post_process_profiler(make_profiler, test_config=None):
    profiler = make_profiler({"run_local": True}, test_config or {"name": "sp3"})
    profiler.profile = profiler.upload_dir / "profile_unprocessed.jslb.gz"
    profiler.profile.write_bytes(b"x")
    return profiler


def fake_run(cmd, **kwargs):
    Path(cmd[cmd.index("-o") + 1]).write_bytes(b"out")
    return CompletedProcess(cmd, 0, stdout="out1\nout2", stderr="err1")


def test_post_process_profiles_commands(toolchain_dir, make_profiler, mock_log):
    """Check that profiler-edit runs once per output with the expected flags."""
    profiler = make_post_process_profiler(
        make_profiler, {"name": "sp3", "browser_cycles": 3}
    )
    unprocessed = profiler.profile
    with mock.patch(
        "raptor_profiling.subprocess.run", side_effect=fake_run
    ) as run, mock.patch("raptor_profiling.platform.system", return_value="Linux"):
        profiles = profiler.post_process_profiles()

    upload = profiler.upload_dir
    assert profiles == [
        upload / "profile_sp3_compact.jslb.gz",
        upload / "profile_sp3_raw_all_processes.jslb.gz",
        upload / "profile_sp3_all_processes.jslb.gz",
    ]
    assert not unprocessed.exists()
    assert profiler.profile is None

    cmds = [c.args[0] for c in run.call_args_list]
    labels_toml = str(
        Path(raptor_profiling.__file__).resolve().parent
        / "browser_function_labels.toml"
    )
    for cmd in cmds:
        assert cmd[0] == str(toolchain_dir / "node" / "bin" / "node")
        assert cmd[2] == str(toolchain_dir / "profiler-node-tools" / "profiler-edit.js")
        assert cmd[cmd.index("-i") + 1] == str(unprocessed)
    compact, raw, labelled = cmds
    assert "--only-keep-threads-with-markers-matching=-async,-sync" in compact
    assert "--merge-non-overlapping-threads-by-name" in compact
    assert compact[compact.index("--insert-label-frames") + 1] == labels_toml
    assert compact[compact.index("--set-name") + 1] == (
        "Sp3 3x (with labels, combined main threads)"
    )
    assert "--insert-label-frames" not in raw
    assert "--merge-non-overlapping-threads-by-name" not in raw
    assert raw[raw.index("--set-name") + 1] == "Sp3 3x (all processes)"
    assert "--insert-label-frames" in labelled
    assert "--merge-non-overlapping-threads-by-name" not in labelled
    mock_log.info.assert_any_call("profiler-edit stdout: out2")
    mock_log.info.assert_any_call("profiler-edit stderr: err1")


def test_post_process_profiles_uses_fetches_dir_and_windows_node(
    tmp_path, toolchain_dir, make_profiler, monkeypatch
):
    """Check that CI runs use MOZ_FETCHES_DIR and node.exe on Windows."""
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(toolchain_dir))
    profiler = make_post_process_profiler(make_profiler)
    profiler.raptor_config = {}
    with mock.patch(
        "raptor_profiling.subprocess.run", side_effect=fake_run
    ) as run, mock.patch("raptor_profiling.platform.system", return_value="Windows"):
        profiler.post_process_profiles()
    assert run.call_args.args[0][0] == str(toolchain_dir / "node" / "node.exe")


@pytest.mark.parametrize(
    "missing,message",
    [
        (Path("node", "bin", "node"), "Node executable not found"),
        (Path("profiler-node-tools", "profiler-edit.js"), "profiler-edit not found"),
    ],
)
def test_post_process_profiles_missing_tools(
    toolchain_dir, make_profiler, missing, message
):
    """Check that a missing node or profiler-edit raises before running anything."""
    (toolchain_dir / missing).unlink()
    profiler = make_post_process_profiler(make_profiler)
    with mock.patch(
        "raptor_profiling.platform.system", return_value="Linux"
    ), mock.patch("raptor_profiling.subprocess.run") as run:
        with pytest.raises(FileNotFoundError, match=message):
            profiler.post_process_profiles()
    run.assert_not_called()


@pytest.mark.parametrize(
    "result,message",
    [
        (CompletedProcess([], 2, stdout="", stderr="boom"), "exited with code 2"),
        (CompletedProcess([], 0, stdout="", stderr=""), "did not produce a profile"),
    ],
)
def test_post_process_profiles_profiler_edit_failures(
    toolchain_dir, make_profiler, result, message
):
    """Check that a failing or output-less profiler-edit run raises."""
    profiler = make_post_process_profiler(make_profiler)
    with mock.patch("raptor_profiling.subprocess.run", return_value=result):
        with pytest.raises(RuntimeError, match=message):
            profiler.post_process_profiles()
    assert profiler.profile.exists()


def test_post_process_profiles_missing_unprocessed_profile(
    toolchain_dir, make_profiler
):
    """Check that a missing unprocessed profile raises FileNotFoundError."""
    profiler = make_post_process_profiler(make_profiler)
    profiler.profile.unlink()
    with pytest.raises(FileNotFoundError, match="Unprocessed profile not found"):
        profiler.post_process_profiles()


if __name__ == "__main__":
    mozunit.main()
