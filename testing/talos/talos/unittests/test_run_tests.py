# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import types
from unittest import mock

import mozunit
import pytest

sys.modules.setdefault(
    "xdg", types.SimpleNamespace(xdg_config_home=lambda: os.path.expanduser("~"))
)

from talos import cmanager_base, utils  # noqa: E402

sys.modules.setdefault("cmanager_base", cmanager_base)

from talos import run_tests  # noqa: E402
from talos.config import ConfigurationError  # noqa: E402
from talos.utils import TalosError, TalosRegression  # noqa: E402

VERSION_INFO = {
    "application_name": "Firefox",
    "application_version": "100.0",
    "application_buildid": "2026",
    "application_repository": "https://hg/repo",
    "application_changeset": "abc",
}


def test_use_base_test_defaults_fills_missing_keys_without_overriding():
    tests = [{"a": 1}, {}]
    assert run_tests.useBaseTestDefaults({"a": 2, "b": None}, tests) == [
        {"a": 1, "b": ""},
        {"a": 2, "b": ""},
    ]


def tp_test(**kwargs):
    test = {
        "name": "tp5o",
        "tpcycles": 1,
        "tpmanifest": "m",
        "gecko_profile": False,
        "preferences": {},
    }
    test.update(kwargs)
    return test


@pytest.mark.parametrize("cycles", [0, 1000, "1"])
def test_set_tp_preferences_bad_cycles(cycles):
    with pytest.raises(TalosError, match="pageloader cycles"):
        run_tests.set_tp_preferences(tp_test(tpcycles=cycles), {})


def test_set_tp_preferences_missing_manifest():
    test = tp_test()
    del test["tpmanifest"]
    with pytest.raises(TalosError, match="tpmanifest not found"):
        run_tests.set_tp_preferences(test, {})


def test_set_tp_preferences_sets_and_removes_prefs():
    test = tp_test(
        tpchrome=True,
        tppagecycles=0,
        preferences={"talos.tphero": True, "talos.tppagecycles": 3},
    )
    run_tests.set_tp_preferences(test, {})
    assert test["preferences"] == {
        "talos.tpchrome": True,
        "talos.tpcycles": 1,
        "talos.tpmanifest": "m",
    }


@pytest.mark.parametrize(
    "name,expected", [("tp5o_scroll", (1, 1, 1)), ("tp5o", (2, 2, 2))]
)
def test_set_tp_preferences_gecko_profile_reduces_cycles(name, expected):
    test = tp_test(name=name, gecko_profile=True, tpcycles=5, tppagecycles=5, cycles=5)
    with mock.patch.object(run_tests, "LOG", new=mock.MagicMock()):
        run_tests.set_tp_preferences(test, {})
    assert (test["tppagecycles"], test["tpcycles"], test["cycles"]) == expected


def test_setup_webserver_creates_httpd_for_host_and_port():
    with mock.patch.object(run_tests, "LOG", new=mock.MagicMock()), mock.patch.object(
        run_tests.server, "WebTestHttpd"
    ) as httpd:
        assert run_tests.setup_webserver("127.0.0.1:8080") is httpd.return_value
    httpd.assert_called_once_with(host="127.0.0.1", port=8080, doc_root=run_tests.here)


@pytest.mark.parametrize(
    "test,config,expected",
    [
        ({}, {}, None),
        ({"pine": False}, {"project": "pine"}, "Broken on the pine branch"),
        ({"pine": False}, {"project": "autoland"}, None),
        ({"pine": True}, {"project": "pine"}, None),
    ],
)
def test_skip_test_only_for_pine_branch_when_test_disables_pine(test, config, expected):
    assert run_tests.skip_test(test, config) == expected


def results_obj(pages):
    inner = types.SimpleNamespace(
        results=[{"index": 0, "page": p, "runs": r} for p, r in pages]
    )
    return types.SimpleNamespace(results=[inner], test_config={"name": "orig"})


def test_make_comparison_result_pairs_base_and_ref_pages():
    original = results_obj([
        ("base.html", [10.0, 20.0]),
        ("ref.html", [12.5, 15.0]),
        ("base2.html", [1.0]),
        ("ref2.html", [1.0004]),
    ])
    comparison = run_tests.make_comparison_result(original)
    assert comparison.results[0].results == [
        {
            "index": 0,
            "page": "base.html",
            "runs": [2.5, 5.0],
            "base_runs": [10.0, 20.0],
            "ref_runs": [12.5, 15.0],
        },
        {
            "index": 0,
            "page": "base2.html",
            "runs": [0.0],
            "base_runs": [1.0],
            "ref_runs": [1.0004],
        },
    ]
    assert len(original.results[0].results) == 4


def test_convert_to_separate_test_results_splits_multi_value_by_label():
    multi = results_obj([("NULL", {"ev1": [1], "ev2": [2]})])
    separate = run_tests.convert_to_separate_test_results(
        multi, [{"name": "t1", "label": "ev1"}, {"name": "t2", "label": "ev2"}]
    )
    assert [s.test_config["name"] for s in separate] == ["t1", "t2"]
    assert [s.results[0].results[0]["runs"] for s in separate] == [[1], [2]]
    assert multi.test_config["name"] == "orig"


@pytest.mark.parametrize("exists", [False, True])
def test_view_gecko_profile_from_talos_views_only_existing_archive(tmp_path, exists):
    archive = tmp_path / "profile.zip"
    if exists:
        archive.write_bytes(b"zip")
    with mock.patch.dict(
        os.environ, {"TALOS_LATEST_GECKO_PROFILE_ARCHIVE": str(archive)}
    ), mock.patch.object(run_tests, "LOG", new=mock.MagicMock()), mock.patch.object(
        run_tests, "view_gecko_profile"
    ) as view:
        run_tests.view_gecko_profile_from_talos()
    assert view.called == exists


def test_view_gecko_profile_from_talos_skips_without_env_var():
    with mock.patch.dict(os.environ), mock.patch.object(
        run_tests, "LOG", new=mock.MagicMock()
    ), mock.patch.object(run_tests, "view_gecko_profile") as view:
        os.environ.pop("TALOS_LATEST_GECKO_PROFILE_ARCHIVE", None)
        run_tests.view_gecko_profile_from_talos()
    view.assert_not_called()


@pytest.fixture
def env(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    with mock.patch.object(
        run_tests, "LOG", new=mock.MagicMock()
    ) as log, mock.patch.object(
        run_tests.mozversion, "get_version", return_value=dict(VERSION_INFO)
    ) as get_version, mock.patch.object(
        run_tests, "setup_webserver"
    ) as webserver, mock.patch.object(
        run_tests, "TTest"
    ) as ttest_cls, mock.patch.object(
        run_tests.TalosResults, "output"
    ) as output, mock.patch.object(run_tests.mozinfo, "os", "linux"), mock.patch.object(
        run_tests, "view_gecko_profile_from_talos"
    ) as view, mock.patch.dict(os.environ):
        for key in (
            "DISABLE_PROFILE_LAUNCH",
            "MOZ_DEVELOPER_OBJ_DIR",
            "MOZ_UPLOAD_DIR",
        ):
            os.environ.pop(key, None)
        ttest_cls.return_value.runTest.return_value = "result"
        yield mock.Mock(
            log=log,
            get_version=get_version,
            webserver=webserver,
            runTest=ttest_cls.return_value.runTest,
            output=output,
            view=view,
            config={
                "tests": [
                    {
                        "name": "t1",
                        "url": "http://x",
                        "setup": "",
                        "cleanup": "${talos}/cleanup.py",
                        "preferences": {},
                    }
                ],
                "gecko_profile": False,
            },
            browser_config={
                "subtests": None,
                "preferences": {},
                "webserver": "127.0.0.1:8000",
                "develop": True,
                "process": "",
                "browser_path": os.path.join("dir", "firefox"),
                "extensions": ["${talos}/ext"],
                "bcontroller_config": "${talos}/bc.json",
                "no_upload_results": False,
            },
        )


def run(env):
    return run_tests.run_tests(env.config, env.browser_config)


def test_run_tests_configures_browser_and_test_then_reports_success(env, capsys):
    assert run(env) == 0
    bc = env.browser_config
    test = env.config["tests"][0]
    assert test["is_first_test"] is True
    assert test["profile"] is None
    assert test["cleanup"] == utils.here + "/cleanup.py"
    assert bc["extra_args"] == []
    assert bc["process"] == "firefox"
    assert bc["extensions"] == [utils.here + "/ext"]
    assert bc["bcontroller_config"] == utils.here + "/bc.json"
    assert bc["browser_name"] == "Firefox"
    assert bc["browser_version"] == "100.0"
    assert bc["buildid"] == "2026"
    assert bc["repository"] == "https://hg/repo"
    assert bc["sourcestamp"] == "abc"
    assert bc["preferences"]["network.proxy.type"] == 2
    assert "PROXY 127.0.0.1:8000" in bc["preferences"]["network.proxy.autoconfig_url"]
    env.runTest.assert_called_once_with(bc, test, utility_path=None)
    env.webserver.assert_called_once_with("127.0.0.1:8000")
    env.webserver.return_value.start.assert_called_once_with()
    env.webserver.return_value.stop.assert_called_once_with()
    env.output.assert_called_once_with({"output_urls": [os.path.abspath("local.json")]})
    assert "Thanks for running Talos locally" in capsys.readouterr().out
    env.log.test_end.assert_called_once_with("t1", status="OK")


def test_run_tests_adds_extra_options_for_fission_and_gecko_profile(env):
    added = []
    with mock.patch.object(
        run_tests.TalosResults, "add_extra_option", side_effect=added.append
    ):
        run(env)
    assert added == ["e10s", "stylo", "fission", "webrender"]

    added.clear()
    env.config["gecko_profile"] = True
    env.browser_config.update(
        fission=False, preferences={"gfx.webrender.software": True}, subtests="a"
    )
    os.environ["DISABLE_PROFILE_LAUNCH"] = "1"
    with mock.patch.object(
        run_tests.TalosResults, "add_extra_option", side_effect=added.append
    ):
        run(env)
    assert added == ["e10s", "stylo", "gecko-profile", "webrender-sw"]
    prefs = env.browser_config["preferences"]
    assert prefs["fission.autostart"] is False
    assert prefs["talos.subtests"] == "a"
    env.view.assert_not_called()


def test_run_tests_adds_windows_wait_and_no_deelevate_args(env):
    with mock.patch.object(run_tests.mozinfo, "os", "win"):
        run(env)
    assert env.browser_config["extra_args"] == ["-wait-for-browser", "-no-deelevate"]


def test_run_tests_production_results_url(env):
    env.browser_config.update(develop=False, process="ff")
    env.config.update(suite="other", code_coverage=True)
    assert run(env) == 0
    env.output.assert_called_once_with({"output_urls": ["local.json"]})
    assert env.browser_config["process"] == "ff"
    assert env.browser_config["code_coverage"] is True
    assert env.config["tests"][0]["suite"] == "other"


def test_run_tests_code_coverage_requires_production(env):
    env.config["code_coverage"] = True
    with pytest.raises(TalosError, match="only supported in production"):
        run(env)


def test_run_tests_missing_changeset_develop(env):
    del env.get_version.return_value["application_changeset"]
    run(env)
    assert env.browser_config["repository"] == "develop"
    assert env.browser_config["sourcestamp"] == "develop"


def test_run_tests_missing_changeset_production(env, capsys):
    env.browser_config["develop"] = False
    del env.get_version.return_value["application_repository"]
    with pytest.raises(SystemExit) as exc:
        run(env)
    assert exc.value.code == 1
    assert "unable to find changeset" in capsys.readouterr().out


def test_run_tests_accepts_testdate_config(env):
    env.config["testdate"] = "Mon, 01 Jan 2024 00:00:00 GMT"
    assert run(env) == 0


def test_run_tests_no_upload(env):
    env.browser_config["no_upload_results"] = True
    assert run(env) == 0
    env.output.assert_not_called()


def test_run_tests_gecko_profile_develop_opens_profiler(env):
    env.config["gecko_profile"] = True
    assert run(env) == 0
    env.view.assert_called_once_with()


def test_run_tests_skipped_test(env):
    env.config["project"] = "pine"
    env.config["tests"][0]["pine"] = False
    assert run(env) == 2
    env.runTest.assert_not_called()
    assert env.log.test_end.call_args.kwargs["status"] == "SKIP"
    env.log.error.assert_called_once_with("No tests ran")


def test_run_tests_prepares_pageloader_manifest_and_preferences(env):
    test = env.config["tests"][0]
    test.update(
        url="",
        tpmanifest="${talos}/tests/x.manifest",
        tpcycles=1,
        gecko_profile=False,
        fnbpaint=True,
        extensions=["${talos}/a"],
    )
    run(env)
    assert test["url"] is None
    assert test["extensions"] == [utils.here + "/a"]
    assert test["tpmanifest"].startswith("file:")
    assert test["tpmanifest"].endswith("x.manifest")
    prefs = test["preferences"]
    assert prefs["talos.tpmanifest"] == test["tpmanifest"]
    assert prefs["talos.fnbpaint"] is True
    assert prefs["dom.performance.time_to_non_blank_paint.enabled"] is True


def test_run_tests_screenshot_obj_dir(env):
    env.config["screenshot_on_failure"] = True
    os.environ["MOZ_DEVELOPER_OBJ_DIR"] = "obj"
    run(env)
    assert env.runTest.call_args.kwargs["utility_path"] == os.path.join(
        "obj", "dist", "bin"
    )


def test_run_tests_screenshot_upload_dir(env, tmp_path):
    env.config["screenshot_on_failure"] = True
    os.environ["MOZ_UPLOAD_DIR"] = str(tmp_path / "build" / "upload")
    run(env)
    assert str(env.runTest.call_args.kwargs["utility_path"]) == str(
        tmp_path / "build" / "tests" / "bin"
    )


def test_run_tests_splits_multi_value_results_by_event_map(env):
    env.config["tests"][0].update(
        firstpaint=True, testeventmap=[{"name": "a", "label": "ev"}]
    )
    multi = results_obj([("NULL", {"ev": [1]})])
    env.runTest.return_value = multi
    added = []
    with mock.patch.object(run_tests.TalosResults, "add", side_effect=added.append):
        run(env)
    assert [r.test_config["name"] for r in added] == ["a"]


def test_run_tests_multi_value_without_event_map(env):
    env.config["tests"][0]["userready"] = True
    assert run(env) == 2
    assert "testeventmap" in env.log.test_end.call_args.kwargs["message"]


def test_run_tests_multi_value_no_results(env):
    env.config["tests"][0].update(firstpaint=True, testeventmap=[])
    env.runTest.return_value = None
    assert run(env) == 2
    assert "no results returned" in env.log.test_end.call_args.kwargs["message"]


def test_run_tests_base_vs_ref_adds_comparison_results(env):
    env.config["tests"][0]["base_vs_ref"] = True
    env.runTest.return_value = results_obj([("b", [2.0]), ("r", [1.0])])
    added = []
    with mock.patch.object(run_tests.TalosResults, "add", side_effect=added.append):
        run(env)
    assert added[0].results[0].results[0]["runs"] == [1.0]


def test_run_tests_returns_one_and_logs_failure_on_regression(env):
    env.runTest.side_effect = TalosRegression("regressed")
    assert run(env) == 1
    env.log.error.assert_called_once_with("Detected a regression for t1")
    assert env.log.test_end.call_args.kwargs["status"] == "FAIL"
    env.webserver.return_value.stop.assert_called_once_with()


def test_run_tests_internal_error(env):
    env.runTest.side_effect = RuntimeError("broken")
    assert run(env) == 2
    assert env.log.test_end.call_args.kwargs["status"] == "ERROR"
    env.log.suite_end.assert_called_once_with()


def test_main_configuration_error():
    with mock.patch.object(
        run_tests, "get_configs", side_effect=ConfigurationError("bad")
    ):
        with pytest.raises(SystemExit) as exc:
            run_tests.main()
    assert exc.value.code == "ERROR: bad"


def test_main_exits_with_run_tests_status():
    with mock.patch.object(
        run_tests, "get_configs", return_value=("c", "b")
    ), mock.patch.object(run_tests, "run_tests", return_value=0) as run_mock:
        with pytest.raises(SystemExit) as exc:
            run_tests.main()
    assert exc.value.code == 0
    run_mock.assert_called_once_with("c", "b")


if __name__ == "__main__":
    mozunit.main()
