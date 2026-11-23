# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import plistlib
import sys
import types
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import perftest
from perftest import Perftest, PerftestAndroid, PerftestDesktop


class _Concrete:
    def run_test_setup(self, test):
        super().run_test_setup(test)

    def run_test(self, test, timeout):
        self.ran.append((test["name"], timeout))

    def run_test_teardown(self, test):
        self.torn_down.append(test["name"])

    def set_browser_test_prefs(self, prefs=None):
        self.prefs_set.append(prefs)

    def check_for_crashes(self):
        pass


class DesktopTest(_Concrete, PerftestDesktop):
    pass


class AndroidTest(_Concrete, PerftestAndroid):
    pass


def bare(cls=DesktopTest, **config):
    inst = object.__new__(cls)
    inst.config = {"app": "firefox", "binary": "/path/to/firefox", **config}
    inst._dirs_to_remove = []
    inst._test_names = []
    inst.page_count = []
    inst.playback = None
    inst.benchmark = None
    inst.gecko_profiler = None
    inst.chrome_trace = None
    inst.simpleperf_profiler = None
    inst.etw_profiler = None
    inst.samply_profiler = None
    inst.perf_profiler = None
    inst.run_local = config.get("run_local", False)
    inst.debug_mode = False
    inst.results_handler = mock.Mock(existing_results=None)
    inst.crashes = 0
    inst.ran = []
    inst.torn_down = []
    inst.prefs_set = []
    return inst


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    monkeypatch.setattr(perftest, "LOG", mock.Mock())


@pytest.fixture
def fake_condprof(monkeypatch):
    class ProfileNotFoundError(Exception):
        pass

    pkg = types.ModuleType("condprof")
    client = types.ModuleType("condprof.client")
    client.ProfileNotFoundError = ProfileNotFoundError
    client.get_profile = mock.Mock()
    util = types.ModuleType("condprof.util")
    util.get_current_platform = mock.Mock(return_value="macosx64-firefox")
    util.download_file = mock.Mock(side_effect=lambda url, **kw: f"dl/{url}")
    runner_mod = types.ModuleType("condprof.runner")
    runner_mod.Runner = mock.Mock()
    patch_mod = types.ModuleType("condprof.patch")
    for name, mod in (
        ("client", client),
        ("util", util),
        ("runner", runner_mod),
        ("patch", patch_mod),
    ):
        setattr(pkg, name, mod)
        monkeypatch.setitem(sys.modules, f"condprof.{name}", mod)
    monkeypatch.setitem(sys.modules, "condprof", pkg)
    return pkg


def make(cls=DesktopTest, **kwargs):
    kwargs.setdefault("app", "firefox")
    kwargs.setdefault("binary", "/path/to/firefox")
    results_handler = mock.Mock()
    meta = ("Firefox", "120.0")
    with mock.patch.object(cls, "build_browser_profile"):
        with mock.patch.object(cls, "get_browser_meta", return_value=meta):
            inst = cls(results_handler_class=results_handler, **kwargs)
    return inst, results_handler


def test_init_records_browser_meta_and_defaults():
    """Check that init stores the browser meta and passes it to the results handler."""
    inst, handler_cls = make(project="try")
    assert inst.browser_name == "Firefox"
    assert inst.browser_version == "120.0"
    handler_cls.return_value.add_browser_meta.assert_called_once_with(
        "firefox", "120.0"
    )
    assert handler_cls.call_args.kwargs["project"] == "try"
    assert inst.config["install_extensions"] == []
    assert inst.profile_class == "firefox"
    assert inst.crashes == 0


@pytest.mark.parametrize(
    "platform, processor, binary, expected",
    [
        ("win", "aarch64", "/path/to/firefox", None),
        ("win", "x86_64", "org.mozilla.reference.browser.raptor", None),
        ("win", "x86_64", "/path/to/firefox", "settled"),
    ],
)
def test_init_disables_condprof_on_unsupported(
    monkeypatch, platform, processor, binary, expected
):
    """Check that conditioned profiles are dropped on win aarch64 and the reference browser."""
    monkeypatch.setattr(perftest.mozinfo, "os", platform)
    monkeypatch.setattr(perftest.mozinfo, "processor", processor)
    inst, _ = make(binary=binary, conditioned_profile="settled")
    assert inst.config["conditioned_profile"] == expected


@pytest.mark.parametrize("app", ["chrome", "chrome-m", "custom-car", "cstm-car-m"])
def test_init_sets_logger_app_for_chromium(app):
    """Check that init tags the logger with the app name for chromium-based apps."""
    with mock.patch.object(perftest.LOG, "set_app") as set_app:
        make(app=app)
    set_app.assert_called_once_with(app)


def test_init_debug_mode_requires_run_local():
    """Check that debug mode is ignored and the default delay used when not running locally."""
    inst, _ = make(run_local=False, debug_mode=True)
    assert inst.debug_mode is False
    assert inst.post_startup_delay == perftest.POST_DELAY_DEFAULT


def test_android_init_mobile_delay():
    """Check that Android apps without a conditioned profile get the mobile startup delay."""
    inst, _ = make(AndroidTest, app="geckoview", binary="org.mozilla.geckoview")
    assert inst.post_startup_delay == perftest.POST_DELAY_MOBILE


def test_simple_properties():
    """Check is_localhost, android_external_storage and get_page_timeout_list."""
    inst = bare(host="localhost")
    assert inst.is_localhost
    inst.config["host"] = "10.0.0.2"
    assert not inst.is_localhost
    assert inst.android_external_storage == "/sdcard/test_root/"
    assert inst.get_page_timeout_list() is inst.results_handler.page_timeout_list


def test_temp_dirs_removed_by_clean_up(tmp_path, monkeypatch):
    """Check that clean_up removes temp dirs and tolerates ones already gone."""
    monkeypatch.setattr(perftest.tempfile, "mkdtemp", lambda: str(tmp_path / "t1"))
    inst = bare(run_local=True, clean=False)
    temp = inst._get_temp_dir()
    os.makedirs(temp)
    inst._dirs_to_remove.append(str(tmp_path / "already-gone"))
    inst.clean_up()
    assert not os.path.exists(temp)
    assert inst._dirs_to_remove == []


def test_conditioned_profile_copy_skips_lock(tmp_path, monkeypatch):
    """Check that conditioned_profile_copy copies the profile without its lock file."""
    src = tmp_path / "src"
    src.mkdir()
    (src / "prefs.js").write_text("x")
    (src / "lock").write_text("")
    monkeypatch.setattr(perftest.tempfile, "mkdtemp", lambda: str(tmp_path / "tmp"))
    inst = bare()
    inst.conditioned_profile_dir = str(src)
    copy = inst.conditioned_profile_copy
    assert copy == str(tmp_path / "tmp" / "profile")
    assert sorted(os.listdir(copy)) == ["prefs.js"]
    assert inst._dirs_to_remove == [str(tmp_path / "tmp")]


@pytest.mark.parametrize(
    "scenario, expected_prepare",
    [
        ("settled-youtube", ("settled-youtube", "youtube")),
        ("settled-webext", ("settled", "webext")),
        ("settled", ("settled", "default")),
    ],
)
def test_build_conditioned_profile(
    fake_condprof, monkeypatch, scenario, expected_prepare
):
    """Check that build_conditioned_profile runs the condprof scenario and customization."""
    loop = mock.Mock()
    monkeypatch.setattr("asyncio.get_event_loop", lambda: loop)
    runner = fake_condprof.runner.Runner.return_value
    runner.env.profile = "/built/profile"
    inst = bare(conditioned_profile=scenario, device_name="a55")
    inst.browsertime = True
    inst.browsertime_geckodriver = "/gd"
    with mock.patch.object(
        DesktopTest, "conditioned_profile_copy", new_callable=mock.PropertyMock
    ) as copy:
        copy.return_value = "/copy"
        assert inst.build_conditioned_profile() == "/copy"
    kwargs = fake_condprof.runner.Runner.call_args.kwargs
    assert kwargs["geckodriver"] == "/gd"
    assert kwargs["firefox"] == "/path/to/firefox"
    assert kwargs["device_name"] == "a55"
    assert kwargs["remote_test_root"] == "/sdcard/test_root/"
    runner.prepare.assert_called_once_with(*expected_prepare)
    runner.one_run.assert_called_once_with(*expected_prepare)
    loop.run_until_complete.assert_called_once_with(runner.one_run.return_value)
    assert inst.conditioned_profile_dir == "/built/profile"


def test_build_conditioned_profile_release_build_toggles_env(
    fake_condprof, monkeypatch
):
    """Check that release builds allow non-local connections only while building."""
    monkeypatch.setattr("asyncio.get_event_loop", mock.Mock())
    monkeypatch.setenv("MOZ_DISABLE_NONLOCAL_CONNECTIONS", "unset")
    seen = []
    fake_condprof.runner.Runner.return_value.prepare.side_effect = lambda *a: (
        seen.append(os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"])
    )
    monkeypatch.setattr(sys, "platform", "win32")
    inst = bare(conditioned_profile="settled", is_release_build=True)
    inst.browsertime = True
    with mock.patch.object(
        DesktopTest, "conditioned_profile_copy", new_callable=mock.PropertyMock
    ):
        inst.build_conditioned_profile()
    assert seen == ["0"]
    assert os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "1"
    assert fake_condprof.runner.Runner.call_args.kwargs["geckodriver"] == (
        "geckodriver.exe"
    )


def test_build_conditioned_profile_requires_browsertime(fake_condprof):
    """Check that build_conditioned_profile refuses to run without browsertime."""
    inst = bare(conditioned_profile="settled")
    inst.browsertime = False
    with pytest.raises(Exception, match="only supported when using Browsertime"):
        inst.build_conditioned_profile()


def test_get_conditioned_profile_reuses_existing_dir(fake_condprof):
    """Check that an already fetched conditioned profile is copied, not downloaded again."""
    inst = bare(conditioned_profile="artifact:settled")
    inst.conditioned_profile_dir = "/existing"
    with mock.patch.object(
        DesktopTest, "conditioned_profile_copy", new_callable=mock.PropertyMock
    ) as copy:
        copy.return_value = "/copy"
        assert inst.get_conditioned_profile() == "/copy"
    fake_condprof.client.get_profile.assert_not_called()


def test_get_conditioned_profile_builds_non_artifact(fake_condprof):
    """Check that a non-artifact scenario is built locally instead of downloaded."""
    inst = bare(conditioned_profile="settled")
    inst.conditioned_profile_dir = None
    with mock.patch.object(
        DesktopTest, "build_conditioned_profile", return_value="/built"
    ):
        assert inst.get_conditioned_profile() == "/built"


@pytest.fixture
def condprof_inst(tmp_path, monkeypatch):
    def inner(cls=DesktopTest, **config):
        monkeypatch.setattr(
            perftest.tempfile, "mkdtemp", lambda: str(tmp_path / "download")
        )
        inst = bare(cls, conditioned_profile="artifact:settled", **config)
        inst.conditioned_profile_dir = None
        return inst

    return inner


def _mock_copy():
    return mock.patch.object(
        Perftest,
        "conditioned_profile_copy",
        new_callable=mock.PropertyMock,
        return_value="/copy",
    )


def test_get_conditioned_profile_desktop_artifact(
    fake_condprof, condprof_inst, tmp_path
):
    """Check that desktop artifact profiles are fetched for the current platform and repo."""
    target = tmp_path / "download" / "profile-dir"
    target.mkdir(parents=True)
    fake_condprof.client.get_profile.return_value = str(target)
    inst = condprof_inst(project="autoland")
    with _mock_copy():
        assert inst.get_conditioned_profile() == "/copy"
    fake_condprof.client.get_profile.assert_called_once_with(
        str(tmp_path / "download"), "macosx64-firefox", "settled", repo="autoland"
    )
    assert inst.conditioned_profile_dir == str(target)


@pytest.mark.parametrize(
    "device_name, project, expected_platform, fallback_repo",
    [
        (None, "mozilla-central", "g5-fenix", "try"),
        ("a55", "try", "a55-fenix", "mozilla-central"),
    ],
)
def test_get_conditioned_profile_android_falls_back_to_other_repo(
    fake_condprof,
    condprof_inst,
    tmp_path,
    device_name,
    project,
    expected_platform,
    fallback_repo,
):
    """Check that Android profile lookups retry on the alternate repo when not found."""
    target = tmp_path / "target"
    target.mkdir()
    fake_condprof.client.get_profile.side_effect = [
        fake_condprof.client.ProfileNotFoundError(),
        str(target),
    ]
    inst = condprof_inst(
        AndroidTest,
        app="fenix",
        binary="org.mozilla.fenix",
        device_name=device_name,
        project=project,
    )
    with _mock_copy():
        inst.get_conditioned_profile()
    calls = fake_condprof.client.get_profile.call_args_list
    assert [c.args[1] for c in calls] == [expected_platform, expected_platform]
    assert [c.kwargs["repo"] for c in calls] == [project, fallback_repo]


def test_get_conditioned_profile_other_errors_are_fatal(fake_condprof, condprof_inst):
    """Check that unexpected get_profile errors are re-raised without a retry."""
    fake_condprof.client.get_profile.side_effect = ValueError("boom")
    inst = condprof_inst(project="try")
    with mock.patch.object(perftest.traceback, "print_exc"):
        with pytest.raises(ValueError, match="boom"):
            inst.get_conditioned_profile()
    assert fake_condprof.client.get_profile.call_count == 1


@pytest.fixture
def profile_data(tmp_path, monkeypatch):
    data = tmp_path / "testing" / "profiles"
    data.mkdir(parents=True)
    (data / "profiles.json").write_text(json.dumps({"raptor": ["common", "perf"]}))
    monkeypatch.setenv("MOZ_DEVELOPER_REPO_DIR", str(tmp_path))
    return data


def test_build_browser_profile_non_firefox():
    """Check that non-Firefox apps get no mozprofile profile."""
    inst = bare(app="chrome")
    with mock.patch.object(perftest, "create_profile") as create:
        inst.build_browser_profile()
    assert inst.profile is None
    create.assert_not_called()


def test_build_browser_profile_firefox(profile_data):
    """Check that Firefox profiles merge base profiles and apply extra prefs."""
    inst = bare(conditioned_profile=None, extra_prefs={"a.b": 1})
    inst.profile_class = "firefox"
    with mock.patch.object(perftest, "create_profile") as create:
        create.return_value.profile = "/profile"
        inst.build_browser_profile()
    create.assert_called_once_with("firefox")
    profile = create.return_value
    assert profile.merge.call_args_list == [
        mock.call(str(profile_data / "common")),
        mock.call(str(profile_data / "perf")),
    ]
    profile.set_preferences.assert_called_once_with({"a.b": 1})
    assert inst.config["local_profile_dir"] == "/profile"


def test_build_browser_profile_conditioned_android(profile_data):
    """Check that Android Firefox profiles use the condprof and merge raptor-android."""
    inst = bare(
        AndroidTest,
        app="fenix",
        conditioned_profile="artifact:settled",
        extra_prefs={},
    )
    inst.profile_class = "firefox"
    inst.get_conditioned_profile = mock.Mock(return_value="/condprof")
    with mock.patch.object(perftest, "create_profile") as create:
        inst.build_browser_profile()
    create.assert_called_once_with("firefox", profile="/condprof")
    merged = [c.args[0] for c in create.return_value.merge.call_args_list]
    assert merged[-1] == str(profile_data / "raptor-android")
    assert len(merged) == 3


def test_install_extra_extensions_noop_without_entries():
    """Check that empty extension entries leave the profile untouched."""
    inst = bare(install_extensions=["", ","])
    inst.profile = mock.Mock()
    inst.install_extra_extensions()
    inst.profile.set_preferences.assert_not_called()
    inst.profile.addons.install.assert_not_called()


def test_install_extra_extensions(fake_condprof, tmp_path):
    """Check that local, URL and AMO extensions are resolved and installed."""
    local_xpi = tmp_path / "local.xpi"
    local_xpi.write_text("")
    inst = bare(
        install_extensions=[
            f"{local_xpi},https://example.com/a.xpi",
            "ublock@example",
        ]
    )
    inst.profile = mock.Mock()
    inst.profile.addons.addon_details.return_value = {"id": "x", "version": "1"}
    with mock.patch.object(
        perftest, "resolve_amo_addon", return_value="https://amo/u.xpi"
    ) as resolve:
        inst.install_extra_extensions()
    resolve.assert_called_once_with("ublock@example")
    prefs = inst.profile.set_preferences.call_args.args[0]
    assert prefs["extensions.autoDisableScopes"] == 0
    inst.profile.addons.install.assert_called_once_with([
        str(local_xpi),
        "dl/https://example.com/a.xpi",
        "dl/https://amo/u.xpi",
    ])
    assert fake_condprof.util.download_file.call_args.kwargs == {
        "mozfetches_subdir": "firefox-addons"
    }
    assert inst.profile.addons.addon_details.call_count == 3


def test_profile_data_dir(monkeypatch, tmp_path):
    """Check the profile_data_dir lookup order: repo env var, mozbuild, then bundled."""
    inst = bare()
    monkeypatch.setenv("MOZ_DEVELOPER_REPO_DIR", str(tmp_path))
    assert inst.profile_data_dir == os.path.join(str(tmp_path), "testing", "profiles")
    monkeypatch.delenv("MOZ_DEVELOPER_REPO_DIR")
    monkeypatch.setattr(perftest, "build", mock.Mock(topsrcdir="/src"))
    assert inst.profile_data_dir == os.path.join("/src", "testing", "profiles")
    monkeypatch.setattr(perftest, "build", None)
    assert inst.profile_data_dir == os.path.join(perftest.here, "profile_data")


def test_artifact_dir(monkeypatch, tmp_path):
    """Check artifact_dir for CI, MOZ_UPLOAD_DIR and local runs."""
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MOZ_DEVELOPER_REPO_DIR", raising=False)
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    inst = bare(run_local=False)
    assert inst.artifact_dir == os.getcwd()
    monkeypatch.setenv("MOZ_UPLOAD_DIR", "/upload")
    assert inst.artifact_dir == "/upload"
    inst.config["run_local"] = True
    assert inst.artifact_dir == perftest.here
    monkeypatch.setenv("MOZ_DEVELOPER_REPO_DIR", "/repo")
    assert inst.artifact_dir == os.path.join("/repo", "testing", "mozharness", "build")


def test_run_test_setup_starts_playback_and_skips_prefs_for_chrome():
    """Check that setup starts playback and skips Firefox prefs for Chrome."""
    inst = bare(app="chrome")
    with mock.patch.object(DesktopTest, "start_playback") as start:
        inst.run_test_setup({
            "name": "t",
            "playback": "mitmproxy",
            "preferences": {"a": 1},
        })
    start.assert_called_once()
    assert inst.prefs_set == []


def test_run_test_setup_firefox_prefs_without_playback():
    """Check that setup applies test prefs for Firefox when no playback is used."""
    inst = bare()
    inst.run_test_setup({"name": "t", "preferences": {"a": 1}})
    assert inst.prefs_set == [{"a": 1}]
    assert inst.config["subtest_alert_on"] is None


@pytest.mark.parametrize(
    "host, expected_proxy",
    [
        ("127.0.0.1", "--proxy-server=127.0.0.1:8080"),
        ("192.168.1.5", "--proxy-server=192.168.1.5:8080"),
    ],
)
def test_playback_chrome_args_proxy(host, expected_proxy):
    """Check the proxy-mode Chrome args, including the remote host substitution."""
    inst = bare(host=host)
    inst.playback = mock.Mock(playback_mode="proxy", host="127.0.0.1", port=8080)
    assert inst.playback_chrome_args() == [
        expected_proxy,
        "--proxy-bypass-list=localhost;127.0.0.1",
        "--ignore-certificate-errors",
    ]


def test_run_tests_runs_each_test_and_cleans_up():
    """Check that run_tests runs, tears down and processes every test, then cleans up."""
    inst = bare()
    tests = [{"name": "a", "page_timeout": "100"}, {"name": "b", "page_timeout": 5}]
    pr = inst.process_results = mock.Mock(return_value="res")
    clean = inst.clean_up = mock.Mock()
    assert inst.run_tests(tests) == "res"
    assert inst.ran == [("a", 100), ("b", 5)]
    assert inst.torn_down == ["a", "b"]
    pr.assert_called_once_with(tests)
    clean.assert_called_once()


def test_run_tests_skips_when_existing_results():
    """Check that run_tests only processes results when existing results are given."""
    inst = bare()
    inst.results_handler.existing_results = "/results"
    pr = inst.process_results = mock.Mock()
    inst.clean_up = mock.Mock()
    inst.run_tests([{"name": "a", "page_timeout": 1}])
    assert inst.ran == []
    pr.assert_called_once()


@pytest.mark.parametrize("crashes, critical_calls", [(0, 1), (2, 0)])
def test_run_tests_runtime_error_exits(crashes, critical_calls):
    """Check that a test RuntimeError exits with 1 and logs only when nothing crashed."""
    inst = bare()
    inst.crashes = crashes
    inst.run_test = mock.Mock(side_effect=RuntimeError("t/o"))
    check = inst.check_for_crashes = mock.Mock()
    clean = inst.clean_up = mock.Mock()
    with pytest.raises(SystemExit) as exc:
        inst.run_tests([{"name": "a", "page_timeout": 1}])
    assert exc.value.code == 1
    check.assert_called_once()
    assert perftest.LOG.critical.call_count == critical_calls
    assert inst.torn_down == ["a"]
    clean.assert_called_once()


def test_process_results_local_with_gecko_profiler(monkeypatch):
    """Check that local results go to raptor.json and gecko profiles are symbolicated."""
    monkeypatch.delenv("MOZ_DEVELOPER_REPO_DIR", raising=False)
    inst = bare(run_local=True, gecko_profile=True)
    inst._test_names = ["a"]
    inst.page_count = [3]
    inst.gecko_profiler = mock.Mock()
    inst.results_handler.summarize_and_output.return_value = True
    assert inst.process_results(["t"]) is True
    assert inst.config["raptor_json_path"] == os.path.join(perftest.here, "raptor.json")
    assert inst.config["page_count"] == [3]
    inst.results_handler.summarize_and_output.assert_called_once_with(
        inst.config, ["t"], ["a"]
    )
    inst.gecko_profiler.symbolicate.assert_called_once()
    inst.gecko_profiler.clean.assert_called_once()


def test_process_results_ci_with_chrome_trace_and_simpleperf(monkeypatch, tmp_path):
    """Check that CI results go to local.json and chrome trace and simpleperf finish."""
    monkeypatch.chdir(tmp_path)
    inst = bare(
        app="chrome", run_local=False, gecko_profile=False, extra_profiler_run=True
    )
    inst.config["simpleperf"] = True
    inst.chrome_trace = mock.Mock()
    inst.simpleperf_profiler = mock.Mock()
    inst.process_results([])
    assert inst.config["raptor_json_path"] == os.path.join(os.getcwd(), "local.json")
    inst.chrome_trace.output_trace.assert_called_once()
    inst.chrome_trace.clean.assert_called_once()
    inst.simpleperf_profiler.symbolicate.assert_called_once()
    inst.simpleperf_profiler.clean.assert_called_once()


def test_process_results_no_profilers():
    """Check that process_results works when no profiler is enabled."""
    inst = bare(run_local=False, gecko_profile=False, app="safari")
    inst.process_results([])
    assert inst.gecko_profiler is None


@pytest.fixture
def fake_home(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("USERPROFILE", str(tmp_path))
    mitm = tmp_path / ".mitmproxy"
    mitm.mkdir()
    return mitm


@pytest.mark.parametrize(
    "run_local, clean, removed",
    [(False, False, True), (True, True, True), (True, False, False)],
)
def test_clean_up_mitmproxy(fake_home, run_local, clean, removed):
    """Check that ~/.mitmproxy is removed in CI or with --clean but kept locally."""
    inst = bare(clean=clean)
    inst.run_local = run_local
    inst.clean_up_mitmproxy()
    assert fake_home.exists() is not removed


def test_clean_up_keeps_last_five_images(fake_home, tmp_path, monkeypatch):
    """Check that clean_up in CI keeps at most five images of each type."""
    artifacts = tmp_path / "artifacts"
    (artifacts / "sub").mkdir(parents=True)
    for i in range(7):
        (artifacts / "sub" / f"shot{i}.png").write_text("")
    (artifacts / "only.jpg").write_text("")
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(artifacts))
    inst = bare(clean=False)
    inst.results_handler.result_dir.return_value = "/results"
    inst.clean_up()
    assert len(list(artifacts.rglob("*.png"))) == 5
    assert (artifacts / "only.jpg").exists()
    assert not fake_home.exists()


def test_delete_proxy_settings_from_profile(tmp_path):
    """Check that network.proxy prefs are removed from user.js."""
    (tmp_path / "user.js").write_text(
        'user_pref("network.proxy.type", 1);\n'
        'user_pref("browser.foo", true);\n'
        'user_pref("network.proxy.http", "x");\n'
    )
    inst = bare()
    inst.profile = mock.Mock(profile=str(tmp_path))
    inst.delete_proxy_settings_from_profile()
    assert (tmp_path / "user.js").read_text() == 'user_pref("browser.foo", true);\n'


def test_start_playback(monkeypatch):
    """Check that start_playback configures and starts the playback tool."""
    inst = bare(verbose=False)
    playback = mock.Mock()
    monkeypatch.setattr(perftest, "get_playback", mock.Mock(return_value=playback))
    with mock.patch.object(DesktopTest, "clean_up_mitmproxy") as clean:
        inst.start_playback({
            "name": "amazon",
            "playback": "mitmproxy",
            "playback_pageset_manifest": "a.manifest,b.manifest",
            "verbose": "true",
        })
    pb_dir = os.path.join(perftest.here, "tooltool-manifests", "playback")
    assert inst.config["playback_files"] == [
        os.path.join(pb_dir, "a.manifest"),
        os.path.join(pb_dir, "b.manifest"),
    ]
    assert inst.config["playback_tool"] == "mitmproxy"
    assert inst.config["playback_mode"] == "proxy"
    assert inst.config["playback_version"] == "8.1.1"
    assert inst.config["verbose"] is True
    assert inst.config["test_name"] == "amazon"
    clean.assert_called_once()
    perftest.get_playback.assert_called_once_with(inst.config)
    assert inst.playback is playback
    playback.start.assert_called_once()


def test_start_playback_custom_manifest_dir(monkeypatch):
    """Check that start_playback honours the manifest dir, mode and version overrides."""
    inst = bare()
    monkeypatch.setattr(perftest, "get_playback", mock.Mock())
    with mock.patch.object(DesktopTest, "clean_up_mitmproxy"):
        inst.start_playback({
            "playback": "mitmproxy",
            "playback_mode": "direct",
            "playback_version": "11.0",
            "playback_manifests_dir": "other",
            "playback_pageset_manifest": "c.manifest",
        })
    assert inst.config["playback_files"] == [
        os.path.join(perftest.here, "tooltool-manifests", "other", "c.manifest")
    ]
    assert inst.config["playback_mode"] == "direct"
    assert inst.config["playback_version"] == "11.0"
    assert inst.config["verbose"] is False


def test_get_source_path_mappings():
    """Check that source path mappings come from the benchmark when there is one."""
    inst = bare()
    assert inst._get_source_path_mappings() is None
    inst.benchmark = mock.Mock()
    inst.benchmark.get_source_path_mapping.return_value = {"a": "b"}
    assert inst._get_source_path_mappings() == [{"a": "b"}]


@pytest.mark.parametrize(
    "method, attr, cls_name, os_name, extra_args",
    [
        ("_init_gecko_profiling", "gecko_profiler", "GeckoProfile", None, True),
        ("_init_etw_profiling", "etw_profiler", "ETWProfile", "win", True),
        ("_init_samply_profiling", "samply_profiler", "SamplyProfile", "mac", True),
        ("_init_chrome_trace", "chrome_trace", "ChromeTrace", None, False),
        (
            "_init_simpleperf_profiling",
            "simpleperf_profiler",
            "SimpleperfProfile",
            None,
            False,
        ),
        ("_init_perf_profiling", "perf_profiler", "PerfProfile", "linux", True),
    ],
)
def test_init_profilers(monkeypatch, method, attr, cls_name, os_name, extra_args):
    """Check that each profiler is created only when MOZ_UPLOAD_DIR is set."""
    if os_name:
        monkeypatch.setattr(perftest.mozinfo, "os", os_name)
    profiler_cls = mock.Mock()
    monkeypatch.setattr(perftest, cls_name, profiler_cls)
    inst = bare()
    test = {"name": "t"}

    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    getattr(inst, method)(test)
    assert getattr(inst, attr) is None
    profiler_cls.assert_not_called()

    monkeypatch.setenv("MOZ_UPLOAD_DIR", "/upload")
    getattr(inst, method)(test)
    assert getattr(inst, attr) is profiler_cls.return_value
    expected = ("/upload", inst.config, test)
    if extra_args:
        expected += (None,)
    profiler_cls.assert_called_once_with(*expected)


@pytest.mark.parametrize(
    "method, cls_name, os_name",
    [
        ("_init_etw_profiling", "ETWProfile", "mac"),
        ("_init_samply_profiling", "SamplyProfile", "linux"),
        ("_init_perf_profiling", "PerfProfile", "win"),
    ],
)
def test_init_profilers_unsupported_os(monkeypatch, method, cls_name, os_name):
    """Check that OS-specific profilers are skipped on unsupported platforms."""
    monkeypatch.setattr(perftest.mozinfo, "os", os_name)
    monkeypatch.setenv("MOZ_UPLOAD_DIR", "/upload")
    profiler_cls = mock.Mock()
    monkeypatch.setattr(perftest, cls_name, profiler_cls)
    getattr(bare(), method)({"name": "t"})
    profiler_cls.assert_not_called()


def test_non_local_connections_env(monkeypatch):
    """Check that the non-local connection helpers set the env var."""
    monkeypatch.setenv("MOZ_DISABLE_NONLOCAL_CONNECTIONS", "x")
    inst = bare()
    inst.enable_non_local_connections()
    assert os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "0"
    inst.disable_non_local_connections()
    assert os.environ["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "1"


@pytest.mark.parametrize("cls", [DesktopTest, AndroidTest])
def test_setup_chrome_args_not_implemented(cls):
    """Check that the mixins leave setup_chrome_args unimplemented."""
    with pytest.raises(NotImplementedError):
        bare(cls).setup_chrome_args({})


def test_android_get_browser_meta_firefox(monkeypatch):
    """Check that Android Firefox meta comes from mozversion on the installer."""
    get_version = mock.Mock(
        return_value={"application_name": "fenix", "application_version": "130.0"}
    )
    monkeypatch.setattr(perftest.mozversion, "get_version", get_version)
    inst = bare(AndroidTest, app="fenix")
    inst.firefox_android_apps = perftest.FIREFOX_ANDROID_APPS
    inst.installerpath = "/fenix.apk"
    assert inst.get_browser_meta() == ("fenix", "130.0")
    get_version.assert_called_once_with(binary="/fenix.apk")


def test_android_get_browser_meta_firefox_mozversion_fails(monkeypatch):
    """Check that a mozversion failure on Android Firefox yields no meta."""
    monkeypatch.setattr(
        perftest.mozversion, "get_version", mock.Mock(side_effect=OSError("no"))
    )
    inst = bare(AndroidTest, app="geckoview")
    inst.firefox_android_apps = perftest.FIREFOX_ANDROID_APPS
    inst.installerpath = None
    assert inst.get_browser_meta() == (None, None)


@pytest.mark.parametrize(
    "app, binary, expected_package",
    [
        ("chrome-m", "x", "com.android.chrome"),
        ("cstm-car-m", "x", "org.chromium.chrome"),
        ("other", "com.example.browser", "com.example.browser"),
    ],
)
def test_android_get_browser_meta_via_dumpsys(
    monkeypatch, app, binary, expected_package
):
    """Check that non-Firefox Android versions come from the first dumpsys versionName."""
    import mozdevice

    device = mock.Mock()
    device.shell_output.return_value = (
        "Packages:\n    versionCode=1\n    versionName=131.0.6778.39\n"
        "    versionName=99.0\n"
    )
    monkeypatch.setattr(mozdevice, "ADBDeviceFactory", mock.Mock(return_value=device))
    inst = bare(AndroidTest, app=app, binary=binary)
    inst.firefox_android_apps = perftest.FIREFOX_ANDROID_APPS
    assert inst.get_browser_meta() == (app, "131.0.6778.39")
    device.shell_output.assert_called_once_with(f"dumpsys package {expected_package}")


def test_android_get_browser_meta_dumpsys_no_version(monkeypatch):
    """Check that a missing dumpsys versionName raises."""
    import mozdevice

    device = mock.Mock()
    device.shell_output.return_value = "Unable to find package\n"
    monkeypatch.setattr(mozdevice, "ADBDeviceFactory", mock.Mock(return_value=device))
    inst = bare(AndroidTest, app="chrome-m")
    inst.firefox_android_apps = perftest.FIREFOX_ANDROID_APPS
    with pytest.raises(Exception, match="Could not determine version for apk"):
        inst.get_browser_meta()


def test_android_set_reverse_ports_benchmark_and_proxy():
    """Check that playback and benchmark ports are reverse-forwarded on localhost."""
    inst = bare(AndroidTest, host="127.0.0.1")
    inst.device = mock.Mock()
    inst.playback = mock.Mock(playback_mode="proxy", port=8080)
    inst.benchmark = mock.Mock(port="5555")
    inst.set_reverse_ports({})
    assert inst.device.create_socket_connection.call_args_list == [
        mock.call("reverse", "tcp:8080", "tcp:8080"),
        mock.call("reverse", "tcp:5555", "tcp:5555"),
    ]


def test_android_set_reverse_ports_remote_host():
    """Check that no ports are reverse-forwarded for a remote host."""
    inst = bare(AndroidTest, host="10.0.0.2")
    inst.device = mock.Mock()
    inst.playback = mock.Mock(port=8080)
    inst.set_reverse_ports({})
    inst.device.create_socket_connection.assert_not_called()


@pytest.mark.parametrize("confirmed", [True, False])
def test_android_clear_app_data(confirmed):
    """Check that app data is cleared only when the device confirms."""
    inst = bare(AndroidTest, binary="org.mozilla.fenix")
    inst.device = mock.Mock()
    inst.device.confirm_clear_app_data.return_value = confirmed
    if confirmed:
        inst.clear_app_data()
        inst.device.shell.assert_called_once_with("pm clear org.mozilla.fenix")
    else:
        with pytest.raises(Exception, match="Declined to clear app data"):
            inst.clear_app_data()
        inst.device.shell.assert_not_called()


def test_android_set_debug_app_flag():
    """Check that the persistent debug-app flag is set for the binary."""
    inst = bare(AndroidTest, binary="org.mozilla.fenix")
    inst.device = mock.Mock()
    inst.set_debug_app_flag()
    inst.device.shell.assert_called_once_with(
        "am set-debug-app --persistent org.mozilla.fenix"
    )


def test_android_copy_profile_to_device():
    """Check that the profile is replaced on the device and made accessible."""
    inst = bare(AndroidTest, binary="org.mozilla.fenix")
    inst.device = mock.Mock()
    inst.profile = mock.Mock(profile="/local/profile")
    inst.remote_profile = "/sdcard/profile"
    inst.copy_profile_to_device()
    inst.device.rm.assert_called_once_with(
        "/sdcard/profile", force=True, recursive=True
    )
    inst.device.push.assert_called_once_with("/local/profile", "/sdcard/profile")
    inst.device.chmod.assert_called_once_with("/sdcard/profile", recursive=True)


def test_android_copy_profile_to_device_errors():
    """Check that a missing app or a failed push raises."""
    inst = bare(AndroidTest, binary="org.mozilla.fenix")
    inst.device = mock.Mock()
    inst.device.is_app_installed.return_value = False
    with pytest.raises(Exception, match="org.mozilla.fenix is not installed"):
        inst.copy_profile_to_device()

    inst.device.is_app_installed.return_value = True
    inst.device.push.side_effect = OSError("adb gone")
    inst.profile = mock.Mock(profile="/p")
    inst.remote_profile = "/r"
    with pytest.raises(OSError, match="adb gone"):
        inst.copy_profile_to_device()


def test_android_turn_on_app_proxy():
    """Check that the Android proxy prefs point at the playback server."""
    inst = bare(AndroidTest, host="192.168.1.5")
    inst.playback = mock.Mock(host="192.168.1.5", port=8080)
    inst.profile = mock.Mock()
    inst.turn_on_android_app_proxy()
    inst.profile.set_preferences.assert_called_once_with({
        "network.proxy.type": 1,
        "network.proxy.http": "192.168.1.5",
        "network.proxy.http_port": 8080,
        "network.proxy.ssl": "192.168.1.5",
        "network.proxy.ssl_port": 8080,
        "network.proxy.no_proxies_on": "192.168.1.5",
    })


def test_desktop_chrome_args_debug_and_extra_args():
    """Check that desktop Chrome args include devtools in debug mode and test args."""
    inst = bare(app="chrome")
    inst.debug_mode = True
    args = inst.desktop_chrome_args({"chrome_args": "  --foo\n\n --bar=1 \n"})
    assert args == [
        "--use-mock-keychain",
        "--no-default-browser-check",
        "--auto-open-devtools-for-tabs",
        "--foo",
        "--bar=1",
    ]


@pytest.fixture
def no_mozversion(monkeypatch):
    monkeypatch.setattr(
        perftest.mozversion, "get_version", mock.Mock(side_effect=OSError("nope"))
    )


def test_desktop_get_browser_meta_mozversion(monkeypatch):
    """Check that desktop meta comes from mozversion when it works."""
    monkeypatch.setattr(
        perftest.mozversion,
        "get_version",
        mock.Mock(
            return_value={
                "application_name": "Firefox",
                "application_version": "121.0",
            }
        ),
    )
    assert bare().get_browser_meta() == ("Firefox", "121.0")


def _write_plist(path, data):
    with open(path, "wb") as fh:
        plistlib.dump(data, fh)


@pytest.mark.parametrize(
    "app, files, expected",
    [
        (
            "chrome",
            {"Info.plist": {"CFBundleShortVersionString": "120.0.1"}},
            "120.0.1",
        ),
        (
            "safari",
            {
                "version.plist": {
                    "CFBundleShortVersionString": "17.1",
                    "CFBundleVersion": "19616",
                }
            },
            "17.1 (19616)",
        ),
        ("safari-tp", {"version.plist": {"CFBundleVersion": "19616"}}, "19616"),
    ],
)
def test_desktop_get_browser_meta_mac_fallback(
    no_mozversion, tmp_path, app, files, expected
):
    """Check that the mac fallback reads the version from the app plist."""
    contents = tmp_path / "App.app" / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    for name, data in files.items():
        _write_plist(contents / name, data)
    inst = bare(app=app, platform="mac", binary=str(contents / "MacOS" / "app"))
    assert inst.get_browser_meta() == (app, expected)


def test_desktop_get_browser_meta_linux_fallback(no_mozversion, monkeypatch):
    """Check that the Linux fallback parses the binary's --version output."""
    run = mock.Mock(return_value=mock.Mock(stdout="Google Chrome 120.0.6099.71 \n"))
    monkeypatch.setattr(perftest.subprocess, "run", run)
    inst = bare(app="chrome", platform="linux", binary="/usr/bin/chrome")
    assert inst.get_browser_meta() == ("chrome", "120.0.6099.71")
    assert run.call_args.args[0] == ["/usr/bin/chrome", "--version"]


@pytest.mark.parametrize(
    "output, expected",
    [("120.0.6099.71\r\n", ("chrome", "120.0.6099.71")), ("", (None, None))],
)
def test_desktop_get_browser_meta_windows_fallback(
    no_mozversion, monkeypatch, output, expected
):
    """Check that the Windows fallback reads the file version via PowerShell."""
    check_output = mock.Mock(return_value=output)
    monkeypatch.setattr(perftest.subprocess, "check_output", check_output)
    inst = bare(app="chrome", platform="win", binary=r"C:\chrome.exe")
    assert inst.get_browser_meta() == expected
    cmd = check_output.call_args.args[0]
    assert cmd[:2] == ["powershell", "-Command"]
    assert r'"C:\chrome.exe"' in cmd[2]


if __name__ == "__main__":
    mozunit.main()
