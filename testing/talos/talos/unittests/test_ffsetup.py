# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import types
from unittest import mock

import mozunit
import pytest

sys.modules.setdefault(
    "xdg", types.SimpleNamespace(xdg_config_home=lambda: os.path.expanduser("~"))
)

from talos import ffsetup  # noqa: E402
from talos.utils import TalosError  # noqa: E402


@pytest.fixture(autouse=True)
def log():
    with mock.patch.object(ffsetup, "LOG") as log:
        yield log


@pytest.fixture
def clean_env(tmp_path):
    env = {
        k: v
        for k, v in os.environ.items()
        if k
        not in (
            "MOZ_AUTOMATION",
            "MOZ_DEVELOPER_REPO_DIR",
            "MOZ_UPLOAD_DIR",
            "GCOV_PREFIX",
            "JS_CODE_COVERAGE_OUTPUT_DIR",
        )
    }
    with mock.patch.dict(os.environ, env, clear=True):
        yield


@pytest.fixture
def browser_config(tmp_path):
    return {
        "env": {"NO_EM_RESTART": 1},
        "symbols_path": None,
        "browser_path": os.path.join(str(tmp_path), "app", "firefox"),
        "extensions": ["${talos}/talos-powers"],
        "webserver": "127.0.0.1:8000",
        "extra_args": ["--no-remote"],
        "init_url": "http://127.0.0.1:8000/getInfo.html",
        "preferences": {"browser.pref": "${webserver}/page"},
    }


@pytest.fixture
def test_config():
    return {
        "name": "ts_paint",
        "profile": None,
        "profile_path": "${talos}/base_profile",
        "extensions": None,
        "preferences": {},
    }


@pytest.fixture
def setup(clean_env, tmp_path, browser_config, test_config):
    tmp_dir = tmp_path / "setup"
    tmp_dir.mkdir()
    with mock.patch.object(ffsetup.tempfile, "mkdtemp", return_value=str(tmp_dir)):
        yield ffsetup.FFSetup(browser_config, test_config)


@pytest.fixture
def profile_data(tmp_path):
    repo = tmp_path / "repo"
    profiles = repo / "testing" / "profiles"
    profiles.mkdir(parents=True)
    (profiles / "profiles.json").write_text(json.dumps({"talos": ["common", "perf"]}))
    with mock.patch.dict(os.environ, {"MOZ_DEVELOPER_REPO_DIR": str(repo)}):
        yield str(profiles)


@pytest.fixture
def profile():
    profile = mock.MagicMock()
    with mock.patch.object(ffsetup.Profile, "clone", return_value=profile) as clone:
        profile.clone = clone
        yield profile


def test_init_uses_temporary_directory(setup, tmp_path):
    assert setup._tmp_dir == str(tmp_path / "setup")
    assert setup.profile_dir == os.path.join(str(tmp_path / "setup"), "profile")
    assert setup.env is None
    assert setup.gecko_profile is None
    assert setup.debug_mode is False


def test_init_in_automation_uses_task_workdir(
    clean_env, tmp_path, browser_config, test_config
):
    env = {"MOZ_AUTOMATION": "1", "TASK_WORKDIR": str(tmp_path)}
    with mock.patch.dict(os.environ, env):
        setup = ffsetup.FFSetup(browser_config, test_config)

    assert setup._tmp_dir == os.path.join(str(tmp_path), "temp")
    assert setup.profile_dir == os.path.join(str(tmp_path), "temp", "profile")


def test_init_debug_mode(setup, browser_config, test_config):
    browser_config["debug"] = True
    with mock.patch.object(ffsetup.tempfile, "mkdtemp", return_value=setup._tmp_dir):
        assert ffsetup.FFSetup(browser_config, test_config).debug_mode is True


def test_profile_data_dir(setup, tmp_path):
    assert setup.profile_data_dir == os.path.join(ffsetup.here, "profile_data")

    with mock.patch.dict(os.environ, {"MOZ_DEVELOPER_REPO_DIR": str(tmp_path)}):
        assert setup.profile_data_dir == os.path.join(
            str(tmp_path), "testing", "profiles"
        )


def test_init_env_without_symbols(setup, browser_config):
    setup._init_env()

    assert setup.env["NO_EM_RESTART"] == "1"
    assert setup.env["MOZ_CRASHREPORTER_NO_REPORT"] == "1"
    assert setup.env["MOZ_CRASHREPORTER_DISABLE"] == "1"
    assert "MOZ_CRASHREPORTER" not in setup.env
    assert setup.env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "1"
    assert setup.env["LD_LIBRARY_PATH"] == os.path.dirname(
        browser_config["browser_path"]
    )


def test_init_env_with_symbols(setup, browser_config):
    browser_config["symbols_path"] = "/symbols"
    setup._init_env()

    assert setup.env["MOZ_CRASHREPORTER"] == "1"
    assert "MOZ_CRASHREPORTER_DISABLE" not in setup.env


def test_init_profile_clones_merges_prefs_and_installs_addons(
    setup, profile, profile_data, test_config, tmp_path
):
    test_config["extensions"] = ["${talos}/pageloader"]
    test_config["preferences"] = {"test.int": "5", "test.bool": "true"}

    setup._init_profile()

    clone_args, clone_kwargs = profile.clone.call_args
    assert clone_args == (
        os.path.normpath(test_config["profile_path"]),
        setup.profile_dir,
    )
    assert clone_kwargs["restore"] is False

    interpolation = {"webserver": "http://127.0.0.1:8000"}
    assert profile.merge.call_args_list == [
        mock.call(os.path.join(profile_data, "common"), interpolation=interpolation),
        mock.call(os.path.join(profile_data, "perf"), interpolation=interpolation),
    ]
    profile.set_preferences.assert_called_once_with({
        "browser.pref": "http://127.0.0.1:8000/page",
        "test.int": 5,
        "test.bool": True,
    })
    profile.addons.install.assert_called_once_with([
        "${talos}/talos-powers",
        "${talos}/pageloader",
    ])


def test_init_profile_feedback_logs_subdirectories(
    setup, profile, profile_data, test_config, log
):
    setup._init_profile()

    feedback = profile.clone.call_args[1]["ignore"]
    profile_path = os.path.normpath(test_config["profile_path"])
    log.info.reset_mock()

    assert feedback(profile_path, ["prefs.js"]) == []
    log.info.assert_not_called()
    assert feedback(profile_path + "/extensions", []) == []
    log.info.assert_called_once_with("=> extensions")


def test_init_profile_keeps_full_webserver_url(
    setup, profile, profile_data, browser_config
):
    browser_config["webserver"] = "https://example.com"
    setup._init_profile()

    assert profile.merge.call_args[1]["interpolation"] == {
        "webserver": "https://example.com"
    }


def test_init_profile_downloads_heavy_profile(
    setup, profile, profile_data, test_config, tmp_path
):
    test_config["profile"] = "simple"
    downloaded = str(tmp_path / "simple")

    with mock.patch.object(
        ffsetup.heavy, "download_profile", return_value=downloaded
    ) as download:
        setup._init_profile()

    download.assert_called_once_with("simple")
    assert test_config["profile_path"] == downloaded
    assert profile.clone.call_args[0][0] == os.path.normpath(downloaded)


def test_init_profile_installs_webextensions(
    setup, profile, profile_data, test_config, tmp_path
):
    folder = tmp_path / "webexts"
    folder.mkdir()
    for name in ("one.xpi", "two.xpi", "readme.txt"):
        (folder / name).write_text("")
    single = tmp_path / "single.xpi"
    single.write_text("")
    test_config["webextensions_folder"] = str(folder)
    test_config["webextensions"] = str(single)

    setup._init_profile()

    installed = [c[0][0] for c in profile.addons.install.call_args_list[1:]]
    assert sorted(installed) == sorted([
        str(folder / "one.xpi"),
        str(folder / "two.xpi"),
        str(single),
    ])


def test_init_profile_skips_missing_or_non_xpi_webextensions(
    setup, profile, profile_data, test_config, tmp_path
):
    test_config["webextensions"] = str(tmp_path / "missing.xpi")
    setup._init_profile()
    assert profile.addons.install.call_count == 1

    not_xpi = tmp_path / "ext.zip"
    not_xpi.write_text("")
    test_config["webextensions"] = str(not_xpi)
    setup._init_profile()
    assert profile.addons.install.call_count == 2


def test_init_profile_converts_webextension_path_on_windows(
    setup, profile, profile_data, test_config
):
    test_config["webextensions"] = "c:/exts/ext.xpi"

    with mock.patch.object(ffsetup.mozinfo, "os", "win"):
        with mock.patch.object(ffsetup.os.path, "exists", return_value=True):
            setup._init_profile()

    profile.addons.install.assert_called_with("c:\\exts\\ext.xpi")


def make_runner(wait):
    runner_cls = mock.Mock()
    runner = runner_cls.return_value
    runner.process_handler.pid = 1234
    runner.process_handler.wait.side_effect = wait
    return runner_cls, runner


def test_run_profile_starts_runner_and_logs_process_exit(setup, browser_config, log):
    setup._init_env()
    runner_cls, runner = make_runner([0])

    with mock.patch.object(ffsetup.mozrunner, "runners", {"firefox": runner_cls}):
        with mock.patch.dict(ffsetup.mozinfo.info, {"appname": "firefox"}):
            setup._run_profile()

    runner_cls.assert_called_once_with(
        profile=setup.profile_dir,
        binary=browser_config["browser_path"],
        cmdargs=["--no-remote", browser_config["init_url"]],
        env=setup.env,
    )
    runner.start.assert_called_once_with(outputTimeout=30)
    log.process_start.assert_called_once()
    log.process_exit.assert_called_once_with(1234, 0)
    assert browser_config["extra_args"] == ["--no-remote"]


def test_run_profile_uses_default_runner(setup):
    setup._init_env()
    runner_cls, runner = make_runner([0])

    with mock.patch.object(ffsetup.mozrunner, "runners", {}):
        with mock.patch.object(ffsetup.mozrunner, "Runner", runner_cls):
            setup._run_profile()

    runner.start.assert_called_once()


def test_run_profile_kills_browser_on_error(setup):
    setup._init_env()
    runner_cls, runner = make_runner(RuntimeError("timeout"))

    with mock.patch.object(ffsetup.mozrunner, "runners", {}):
        with mock.patch.object(ffsetup.mozrunner, "Runner", runner_cls):
            with pytest.raises(TalosError, match="Browser Failed to close"):
                setup._run_profile()

    runner.process_handler.kill.assert_called_once_with()


def test_init_gecko_profile_logs_critical_without_upload_dir(setup, test_config, log):
    test_config["gecko_profile"] = True
    setup._init_gecko_profile()

    assert setup.gecko_profile is None
    log.critical.assert_called_once()


def test_init_gecko_profile_disabled(setup, tmp_path, log):
    with mock.patch.dict(os.environ, {"MOZ_UPLOAD_DIR": str(tmp_path)}):
        setup._init_gecko_profile()

    assert setup.gecko_profile is None
    log.critical.assert_not_called()


def test_init_gecko_profile_creates_profile_and_updates_env(
    setup, test_config, tmp_path
):
    test_config["gecko_profile"] = True
    setup._init_env()

    with mock.patch.dict(os.environ, {"MOZ_UPLOAD_DIR": str(tmp_path)}):
        with mock.patch.object(ffsetup, "GeckoProfile") as gecko_profile:
            setup._init_gecko_profile()

    gecko_profile.assert_called_once_with(str(tmp_path), test_config)
    assert setup.gecko_profile is gecko_profile.return_value
    setup.gecko_profile.update_env.assert_called_once_with(setup.env)


def test_clean_removes_tmp_dir_and_gecko_profile(setup):
    os.makedirs(setup.profile_dir)
    setup.gecko_profile = mock.Mock()

    setup.clean()

    assert not os.path.exists(setup._tmp_dir)
    setup.gecko_profile.clean.assert_called_once_with()


def test_clean_logs_removal_errors(setup, log):
    with mock.patch.object(ffsetup.mozfile, "remove", side_effect=OSError("busy")):
        setup.clean()

    assert log.info.call_count == 2


def make_ccov_tree(root):
    for rel in ("a.gcda", os.path.join("sub", "b.gcda"), os.path.join("sub", "c.txt")):
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("")
    archived = root / "gcda-archive" / "old" / "d.gcda"
    archived.parent.mkdir(parents=True)
    archived.write_text("")
    return archived


def test_ccov_does_nothing_in_develop_mode(setup, browser_config, tmp_path):
    browser_config["develop"] = True
    make_ccov_tree(tmp_path / "gcov")

    with mock.patch.dict(os.environ, {"GCOV_PREFIX": str(tmp_path / "gcov")}):
        setup.collect_or_clean_ccov(clean=True)

    assert (tmp_path / "gcov" / "a.gcda").exists()


def test_ccov_clean_removes_gcda_files_but_keeps_others(setup, tmp_path, log):
    gcov = tmp_path / "gcov"
    js = tmp_path / "jsccov"
    archived = make_ccov_tree(gcov)
    js.mkdir()
    (js / "e.gcda").write_text("")
    env = {"GCOV_PREFIX": str(gcov), "JS_CODE_COVERAGE_OUTPUT_DIR": str(js)}

    with mock.patch.dict(os.environ, env):
        setup.collect_or_clean_ccov(clean=True)

    assert not (gcov / "a.gcda").exists()
    assert not (gcov / "sub" / "b.gcda").exists()
    assert not (js / "e.gcda").exists()
    assert (gcov / "sub" / "c.txt").exists()
    assert archived.exists()


def test_ccov_clean_missing_dir_and_remove_errors(setup, tmp_path, log):
    gcov = tmp_path / "gcov"
    make_ccov_tree(gcov)
    env = {
        "GCOV_PREFIX": str(gcov),
        "JS_CODE_COVERAGE_OUTPUT_DIR": str(tmp_path / "missing"),
    }

    with mock.patch.dict(os.environ, env):
        with mock.patch.object(ffsetup.mozfile, "remove", side_effect=OSError):
            setup.collect_or_clean_ccov(clean=True)

    messages = " ".join(str(c[0][0]) for c in log.info.call_args_list)
    assert "doesn't exist" in messages
    assert "Exception while removing file" in messages
    assert (gcov / "a.gcda").exists()


def test_ccov_collect_copies_files_to_archive(setup, tmp_path):
    gcov = tmp_path / "gcov"
    make_ccov_tree(gcov)

    with mock.patch.dict(os.environ, {"GCOV_PREFIX": str(gcov)}):
        setup.collect_or_clean_ccov()

    archive_top = gcov / "gcda-archive" / "ts_paint"
    collected = sorted(p.name for p in archive_top.rglob("*.gcda"))
    assert collected == ["a.gcda", "b.gcda"]
    assert (gcov / "a.gcda").exists()


def test_ccov_collect_logs_errors(setup, tmp_path, log):
    gcov = tmp_path / "gcov"
    make_ccov_tree(gcov)

    with mock.patch.dict(os.environ, {"GCOV_PREFIX": str(gcov)}):
        with mock.patch.object(ffsetup.os, "makedirs", side_effect=OSError):
            with mock.patch.object(ffsetup.shutil, "copy", side_effect=OSError):
                setup.collect_or_clean_ccov()

    assert log.critical.call_count == 3
    messages = " ".join(str(c[0][0]) for c in log.info.call_args_list)
    assert "Error copying" in messages


def enter_mocks(setup):
    return mock.patch.multiple(
        setup,
        _init_env=mock.DEFAULT,
        _init_profile=mock.DEFAULT,
        _run_profile=mock.DEFAULT,
        _init_gecko_profile=mock.DEFAULT,
        collect_or_clean_ccov=mock.DEFAULT,
        clean=mock.DEFAULT,
    )


def test_context_manager_runs_setup_steps_and_cleans_on_exit(setup):
    with enter_mocks(setup) as mocks:
        with setup as entered:
            assert entered is setup
            mocks["clean"].assert_not_called()

    for name in ("_init_env", "_init_profile", "_run_profile", "_init_gecko_profile"):
        mocks[name].assert_called_once_with()
    mocks["collect_or_clean_ccov"].assert_not_called()
    mocks["clean"].assert_called_once_with()


def test_context_manager_cleans_ccov_when_code_coverage_enabled(setup, browser_config):
    browser_config["code_coverage"] = True
    with enter_mocks(setup) as mocks:
        with setup:
            pass

    mocks["collect_or_clean_ccov"].assert_called_once_with(clean=True)


def test_context_manager_skips_warmup_for_damp_and_debug(setup, test_config):
    test_config["name"] = "damp"
    with enter_mocks(setup) as mocks:
        with setup:
            pass
    mocks["_run_profile"].assert_not_called()

    test_config["name"] = "ts_paint"
    setup.debug_mode = True
    with enter_mocks(setup) as mocks:
        with setup:
            pass
    mocks["_run_profile"].assert_not_called()


def test_context_manager_cleans_when_warmup_fails(setup):
    with enter_mocks(setup) as mocks:
        mocks["_run_profile"].side_effect = TalosError("warmup")
        with pytest.raises(TalosError):
            setup.__enter__()

    mocks["clean"].assert_called_once_with()
    mocks["_init_gecko_profile"].assert_not_called()


if __name__ == "__main__":
    mozunit.main()
