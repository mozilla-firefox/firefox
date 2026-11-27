# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import pathlib
import plistlib
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import mozunit
import pytest
from mozharness.base import script
from mozharness.base.log import CRITICAL
from mozharness.base.python import VirtualenvMixin
from mozharness.mozilla.automation import EXIT_STATUS_DICT, TBPL_RETRY, TBPL_SUCCESS
from mozharness.mozilla.testing import android, raptor, testbase
from mozharness.mozilla.testing.raptor import (
    Raptor,
    RaptorErrorList,
    RaptorOutputParser,
)


@pytest.mark.parametrize("run_local", [False, True])
def test_raptor_package_pins_cover_setup(tmp_path, run_local):
    harness = Raptor.__new__(Raptor)
    harness.config = {"virtualenv_path": str(tmp_path / "venv")}
    harness.clean = False
    harness.run_local = run_local
    harness.browsertime_visualmetrics = False
    harness.raptor_path = str(tmp_path / "raptor")
    harness.query_exe = Mock(return_value=sys.executable)
    harness.platform_name = Mock(return_value="linux")
    harness.query_abs_dirs = Mock(return_value={"abs_test_install_dir": str(tmp_path)})
    harness.register_virtualenv_module = Mock()
    harness.setup_local_ffmpeg = Mock()
    harness.install_module = Mock()
    harness.package_versions = Mock()

    with patch.object(VirtualenvMixin, "create_virtualenv") as create:
        harness.create_virtualenv()

    assert harness.query_python_constraints() == [
        str(tmp_path / "config" / "requirements-raptor.txt")
    ]
    common = str(tmp_path / "raptor" / "requirements-common.txt")
    assert create.call_args.kwargs["requirements"] == [common]
    requirements = "source_requirements.txt" if run_local else "requirements.txt"
    harness.register_virtualenv_module.assert_called_once_with(
        requirements=[str(tmp_path / "raptor" / requirements)]
    )
    harness.install_module.assert_not_called()


@pytest.mark.parametrize("harness", ["raptor", "talos"])
@pytest.mark.parametrize("source", [False, True])
def test_performance_requirements_use_local_mozbase(harness, source):
    testing = Path(__file__).resolve().parents[2]
    filename = "source_requirements.txt" if source else "requirements.txt"
    requirements = testing / harness / filename

    def local_paths(path):
        result = set()
        for line in path.read_text().splitlines():
            if line.startswith("-r "):
                result.update(local_paths(path.parent / line[3:]))
            else:
                requirement = line.removeprefix("--editable ")
                if requirement.startswith("../"):
                    result.add(
                        (requirements.parent / requirement.split(";")[0]).resolve()
                    )
        return result

    expected = {path.parent for path in (testing / "mozbase").glob("*/setup.py")}
    assert expected
    assert expected <= local_paths(requirements)


@pytest.fixture
def make_raptor(tmp_path, monkeypatch):
    monkeypatch.setenv("REQUIRE_GPU", "0")
    for name in (
        "ANDROID_SERIAL",
        "DEVICE_SERIAL",
        "HOST_IP",
        "MOZ_AUTOMATION",
        "MOZ_FETCHES_DIR",
        "MOZ_RAPTOR_COLLECT_PERFSTATS",
        "PERF_FLAGS",
        "PYTHONPATH",
    ):
        monkeypatch.delenv(name, raising=False)

    def make(**config):
        base = {
            "base_work_dir": str(tmp_path),
            "work_dir": "build",
            "log_to_console": False,
        }
        if not config.get("run_local"):
            base["test"] = "speedometer3"
        base.update(config)
        return Raptor(config=base, option_args=[])

    return make


def test_init_stores_production_config_and_resolves_host_env(
    make_raptor, tmp_path, monkeypatch
):
    monkeypatch.setenv("HOST_IP", "10.0.0.1")
    r = make_raptor(
        app="fenix",
        binary_path="org.mozilla.fenix",
        device_serial="serial",
        host="HOST_IP",
        browsertime_node="/node",
    )
    assert r.app == "fenix"
    assert r.binary_path == "org.mozilla.fenix"
    assert r.app_name == "org.mozilla.fenix"
    assert r.device_serial == "serial"
    assert r.host == "10.0.0.1"
    assert r.browsertime_node == "/node"
    assert r.workdir == str(tmp_path / "build")
    assert not r.gecko_profile


def test_init_prefers_device_serial_from_environment(make_raptor, monkeypatch):
    monkeypatch.setenv("ANDROID_SERIAL", "env-serial")
    r = make_raptor(device_serial="serial")
    assert r.device_serial == "env-serial"
    assert r.app == "firefox"


def test_init_local_parses_cmd_line_args(make_raptor):
    r = make_raptor(
        run_local=True,
        raptor_cmd_line_args=[
            "--app=geckoview",
            "-i",
            "android.intent.action.MAIN",
            "--activity",
            "GeckoViewActivity",
            "--geckoProfile",
            "--browsertime-geckodriver",
            "/cmdline/geckodriver",
        ],
        browsertime_geckodriver="/config/geckodriver",
    )
    assert r.app == "geckoview"
    assert r.intent == "android.intent.action.MAIN"
    assert r.activity == "GeckoViewActivity"
    assert r.gecko_profile
    assert getattr(r, "browsertime_geckodriver", None) is None


@pytest.mark.parametrize("config", [{}, {"raptor_cmd_line_args": ["--cold"]}])
def test_init_local_defaults_to_firefox(make_raptor, config):
    r = make_raptor(run_local=True, **config)
    assert r.app == "firefox"
    assert not hasattr(r, "intent")


def test_query_gecko_profile_options(make_raptor):
    r = make_raptor(
        gecko_profile=True,
        gecko_profile_interval=1.5,
        gecko_profile_entries=100,
        gecko_profile_features="js,stackwalk",
        gecko_profile_threads="GeckoMain",
    )
    assert r.query_gecko_profile_options() == [
        "--gecko-profile",
        "--gecko-profile-interval",
        "1.5",
        "--gecko-profile-entries",
        "100",
        "--gecko-profile-features",
        "js,stackwalk",
        "--gecko-profile-threads",
        "GeckoMain",
    ]
    assert make_raptor(gecko_profile=True).query_gecko_profile_options() == [
        "--gecko-profile"
    ]
    assert make_raptor(extra_profiler_run=True).query_gecko_profile_options() == [
        "--extra-profiler-run"
    ]
    assert make_raptor().query_gecko_profile_options() == []


def test_query_abs_dirs_caches_blobber_and_test_dirs_without_sdk(make_raptor, tmp_path):
    r = make_raptor()
    dirs = r.query_abs_dirs()
    assert dirs["abs_blob_upload_dir"] == str(tmp_path / "build" / "blobber_upload_dir")
    assert dirs["abs_test_install_dir"] == str(tmp_path / "build" / "tests")
    assert "abs_sdk_dir" not in dirs
    assert r.query_abs_dirs() is dirs


@pytest.mark.parametrize("fetches", [False, True])
def test_query_abs_dirs_emulator_places_sdk_and_avds_dirs(
    make_raptor, tmp_path, monkeypatch, fetches
):
    root = tmp_path / "build"
    if fetches:
        root = tmp_path / "fetches"
        monkeypatch.setenv("MOZ_FETCHES_DIR", str(root))
    r = make_raptor(emulator_avd_name="avd", avds_dir_name="avds")
    dirs = r.query_abs_dirs()
    assert dirs["abs_sdk_dir"] == str(root / "android-sdk-linux")
    assert dirs["abs_avds_dir"] == str(root / "avds")


def test_install_chrome_android_installs_existing_package(make_raptor):
    r = make_raptor(app="chrome-m")
    r._device = Mock()
    r.install_chrome_android()
    r._device.shell_output.assert_called_once_with(
        "cmd package install-existing com.android.chrome"
    )


@pytest.mark.parametrize(
    "config",
    [
        {"app": "firefox"},
        {"run_local": True, "raptor_cmd_line_args": ["--app", "chrome-m"]},
    ],
)
def test_install_chrome_android_skipped(make_raptor, config):
    r = make_raptor(**config)
    r._device = Mock()
    r.install_chrome_android()
    r._device.shell_output.assert_not_called()


def test_install_chromium_android(make_raptor, tmp_path, monkeypatch):
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    r = make_raptor(app="cstm-car-m")
    r._device = Mock()
    r.install_chromium_android()
    r._device.install_app.assert_called_once_with(
        str(tmp_path / "chromium" / "apks" / "ChromePublic.apk")
    )


@pytest.mark.parametrize(
    "config",
    [
        {"app": "firefox"},
        {"run_local": True, "raptor_cmd_line_args": ["--app", "cstm-car-m"]},
    ],
)
def test_install_chromium_android_skipped(make_raptor, config):
    r = make_raptor(**config)
    r._device = Mock()
    r.install_chromium_android()
    r._device.install_app.assert_not_called()


@pytest.fixture
def applications(tmp_path, monkeypatch):
    def fake_path(first, *rest):
        return pathlib.Path(tmp_path, "Applications", *rest)

    monkeypatch.setattr(raptor, "pathlib", SimpleNamespace(Path=fake_path))
    return tmp_path / "Applications"


def write_plist(applications, app_name, data):
    path = applications / f"{app_name}.app" / "Contents" / "version.plist"
    path.parent.mkdir(parents=True)
    path.write_bytes(plistlib.dumps(data))


def test_log_safari_version(make_raptor, applications):
    write_plist(
        applications,
        "Safari",
        {"CFBundleShortVersionString": "18.1", "CFBundleVersion": "20619"},
    )
    r = make_raptor(app="safari")
    r.info = Mock()
    r.warning = Mock()
    r.install_safari_technology_preview()
    r.info.assert_called_once_with(
        "Safari version: 18.1 (CFBundleShortVersionString), 20619 (CFBundleVersion)"
    )
    r.warning.assert_not_called()


def test_log_safari_version_missing_keys(make_raptor, applications):
    write_plist(applications, "Safari", {"CFBundleVersion": "20619"})
    r = make_raptor(app="safari")
    r.warning = Mock()
    r._log_safari_version()
    r.warning.assert_called_once_with(
        "Unable to read Safari version: missing CFBundleShortVersionString"
    )


def test_install_safari_technology_preview(make_raptor, applications):
    r = make_raptor(app="safari-tp")
    r.warning = Mock()
    with patch("mozprocess.run_and_wait") as run:
        r.install_safari_technology_preview()
    run.assert_called_once_with([
        sys.executable,
        "/usr/local/bin/install_safari_softwareupdate_updates.py",
    ])
    message = r.warning.call_args.args[0]
    assert message.startswith("Unable to read Safari Technology Preview version from ")


@pytest.mark.parametrize(
    "config",
    [
        {"app": "firefox"},
        {"run_local": True, "raptor_cmd_line_args": ["--app", "safari"]},
    ],
)
def test_install_safari_technology_preview_skipped(make_raptor, config):
    r = make_raptor(**config)
    r._log_safari_version = Mock()
    r.install_safari_technology_preview()
    r._log_safari_version.assert_not_called()


def test_install_chromium_distribution_not_required(make_raptor):
    r = make_raptor(app="firefox")
    r.install_chromium_distribution()
    assert r.chromium_dist_path is None


CHROME_WIN = "C:\\%s\\Google\\Chrome\\Application\\chrome.exe"


@pytest.mark.parametrize(
    "platform,existing,expected",
    [
        ("win64", [CHROME_WIN % "Progra~1"], CHROME_WIN % "Progra~1"),
        ("win64", [CHROME_WIN % "Progra~2"], CHROME_WIN % "Progra~2"),
        ("linux64", ["/usr/bin/google-chrome"], "/usr/bin/google-chrome"),
        (
            "macosx64",
            ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"],
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        ),
        ("linux64", [], "/usr/bin/google-chrome"),
    ],
)
def test_install_chromium_distribution_chrome(
    make_raptor, platform, existing, expected
):
    r = make_raptor(app="chrome")
    r.platform_name = Mock(return_value=platform)
    r.error = Mock()
    with patch.object(raptor.os.path, "exists", side_effect=lambda p: p in existing):
        r.install_chromium_distribution()
    assert r.chromium_dist_path == expected
    assert r.error.called == (not existing)


def test_install_chromium_distribution_local(make_raptor):
    r = make_raptor(run_local=True, raptor_cmd_line_args=["--app=custom-car"])
    r.install_chromium_distribution()
    assert r.chromium_dist_path is None


@pytest.mark.parametrize(
    "platform,parts",
    [
        ("macosx64", ["chromium", "Chromium.app", "Contents", "MacOS", "chromium"]),
        ("linux64", ["chromium", "Default", "chrome"]),
        ("win64", ["chromium", "Default", "chrome.exe"]),
    ],
)
@pytest.mark.parametrize("installed", [False, True])
def test_install_chromium_distribution_custom_car(
    make_raptor, tmp_path, monkeypatch, platform, parts, installed
):
    fetches = tmp_path / "fetches"
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(fetches))
    binary = fetches.joinpath(*parts)
    if installed:
        binary.parent.mkdir(parents=True)
        binary.write_text("")
    r = make_raptor(app="custom-car")
    r.platform_name = Mock(return_value=platform)
    r.info = Mock()
    r.install_chromium_distribution()
    assert r.chromium_dist_dest == str(fetches)
    assert r.chromium_dist_path == str(binary)
    last = r.info.call_args.args[0]
    if installed:
        assert last == f"Successfully installed custom-car to: {binary}"
    else:
        assert last == "Abort: failed to install custom-car"


def test_raptor_options_requires_binary(make_raptor):
    r = make_raptor()
    with pytest.raises(SystemExit):
        r.raptor_options()


def test_raptor_options_firefox(make_raptor):
    r = make_raptor(binary_path="/ff")
    assert r.raptor_options() == [
        "--browsertime",
        "--app",
        "firefox",
        "--binary",
        "/ff",
        "--test",
        "speedometer3",
        "--host",
        "127.0.0.1",
    ]


def test_raptor_options_android_production(make_raptor):
    r = make_raptor(app="geckoview", binary_path="org.mozilla.geckoview_example")
    r.installer_path = "/apk/geckoview.apk"
    r.query_package_name = Mock(return_value="org.mozilla.geckoview_example")
    options = r.raptor_options()
    assert options[options.index("--installerpath") + 1] == "/apk/geckoview.apk"
    assert options[options.index("--binary") + 1] == "org.mozilla.geckoview_example"
    assert options[options.index("--app") + 1] == "geckoview"


@pytest.mark.parametrize(
    "app,dist_path,binary",
    [
        ("safari", None, "/Applications/Safari.app/Contents/MacOS/Safari"),
        (
            "safari-tp",
            None,
            "/Applications/Safari Technology Preview.app/Contents/MacOS/"
            "Safari Technology Preview",
        ),
        ("cstm-car-m", None, "org.chromium.chrome"),
        ("chrome", "/chrome", "/chrome"),
        ("custom-car", None, ""),
    ],
)
def test_raptor_options_other_browsers(make_raptor, app, dist_path, binary):
    r = make_raptor(app=app)
    r.chromium_dist_path = dist_path
    options = r.raptor_options()
    assert options[options.index("--binary") + 1] == binary


def test_raptor_options_local_chrome(make_raptor):
    r = make_raptor(
        run_local=True,
        raptor_cmd_line_args=["--app=chrome", "--browsertime-node", "/cmd/node"],
        browsertime_node="/config/node",
    )
    options = r.raptor_options()
    assert "--binary" not in options
    assert options == [
        "--run-local",
        "--app=chrome",
        "--browsertime-node",
        "/cmd/node",
        "--browsertime",
        "--app",
        "chrome",
        "--host",
        "127.0.0.1",
    ]


def test_raptor_options_full(make_raptor, monkeypatch):
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    monkeypatch.setenv("MOZ_RAPTOR_COLLECT_PERFSTATS", "1")
    monkeypatch.setenv("PERF_FLAGS", "flag-a key=value")
    monkeypatch.setenv("BT_DIR", "/bt")
    r = make_raptor(
        binary_path="/ff",
        binary="/override",
        obj_path="/obj",
        mozbuild_path="/mozbuild",
        test_url_params="a=1",
        device_name="a55",
        activity="Main",
        conditioned_profile="settled",
        benchmark_repository="https://example.com/repo",
        benchmark_revision="abc",
        benchmark_branch="main",
        host="10.0.0.2",
        code_coverage=True,
        is_release_build=True,
        live_sites=True,
        chimera=True,
        disable_perf_tuning=True,
        cold=True,
        fission=False,
        verbose=True,
        extra_prefs=["a.b=1"],
        environment=["X=1"],
        install_extensions=["ublock"],
        enable_marionette_trace=True,
        browser_cycles=3,
        test_bytecode_cache=True,
        simpleperf=True,
        etw_profile=True,
        samply_profile=True,
        perf_profile=True,
        extra_summary_methods=["geomean"],
        perfherder_extra_options=["opt"],
        page_timeout=1000,
        post_startup_delay=500,
        power_test=True,
        browsertime_node="$BT_DIR/node",
        browsertime_user_args=["one", "two"],
        browsertime_video=True,
        extra_profiler_run=True,
    )
    r.symbols_path = "/symbols"
    r.browsertime_ffmpeg = "/ffmpeg"
    options = r.raptor_options(args=["--extra"], cycles="5")

    assert options[:3] == ["--extra-profiler-run", "--extra", "--flag-a"]
    for flag in [
        "--code-coverage",
        "--is-release-build",
        "--live-sites",
        "--chimera",
        "--disable-perf-tuning",
        "--cold",
        "--disable-fission",
        "--verbose",
        "--setpref=a.b=1",
        "--setenv=X=1",
        "--enable-marionette-trace",
        "--browser-cycles=3",
        "--test-bytecode-cache",
        "--collect-perfstats",
        "--simpleperf",
        "--etw-profile",
        "--samply-profile",
        "--perf-profile",
        "--extra-summary-methods=geomean",
        "--perfherder-extra-option=opt",
        "--page-timeout=1000",
        "--post-startup-delay=500",
        "--screenshot-on-failure",
        "--power-test",
        "--browsertime-video",
        "--browsertime",
    ]:
        assert flag in options
    pairs = {}
    for i, item in enumerate(options[:-1]):
        if item.startswith("--"):
            pairs.setdefault(item, []).append(options[i + 1])
    assert pairs["--install-extension"] == ["ublock"]
    assert pairs["--browsertime-node"] == [os.path.expandvars("$BT_DIR/node")]
    assert pairs["--browsertime-arg"] == ["one", "two"]
    assert pairs["--browsertime-ffmpeg"] == ["/ffmpeg"]
    expected_kw = {
        "--app": "firefox",
        "--binary": "/override",
        "--test": "speedometer3",
        "--symbolsPath": "/symbols",
        "--obj-path": "/obj",
        "--mozbuild-path": "/mozbuild",
        "--test-url-params": "a=1",
        "--device-name": "a55",
        "--activity": "Main",
        "--conditioned-profile": "settled",
        "--benchmark_repository": "https://example.com/repo",
        "--benchmark_revision": "abc",
        "--benchmark_branch": "main",
        "--cycles": "5",
        "--host": "10.0.0.2",
        "--key": "value",
    }
    for key, value in expected_kw.items():
        assert pairs[key] == [value]


@pytest.mark.parametrize("run_local", [False, True])
def test_populate_webroot_sets_raptor_path_by_run_mode(
    make_raptor, tmp_path, run_local
):
    r = make_raptor(run_local=run_local, repo_path=str(tmp_path / "src"))
    r.populate_webroot()
    if run_local:
        assert r.raptor_path == str(tmp_path / "src" / "testing" / "raptor")
    else:
        assert r.raptor_path == str(tmp_path / "build" / "tests" / "raptor")


def test_clobber_recreates_upload_dir(make_raptor, tmp_path):
    r = make_raptor()
    r.mkdir_p = Mock(side_effect=os.makedirs)
    with patch.object(script.BaseScript, "clobber") as clobber:
        r.clobber()
        r.clobber()
    assert clobber.call_count == 2
    r.mkdir_p.assert_called_once_with(str(tmp_path / "build" / "blobber_upload_dir"))


@pytest.mark.parametrize("fails", [False, True])
def test_install_android_app_captures_logcat(make_raptor, fails):
    r = make_raptor()
    r.logcat_start = Mock()
    r.logcat_stop = Mock()
    side_effect = RuntimeError("install failed") if fails else None
    with patch.object(
        android.AndroidMixin, "install_android_app", side_effect=side_effect
    ) as install:
        if fails:
            with pytest.raises(RuntimeError):
                r.install_android_app("app.apk", replace=True)
        else:
            r.install_android_app("app.apk", replace=True)
    install.assert_called_once_with("app.apk", replace=True)
    r.logcat_start.assert_called_once_with()
    r.logcat_stop.assert_called_once_with()


def test_download_and_extract_requests_raptor_categories(make_raptor):
    r = make_raptor()
    with patch.object(
        testbase.TestingMixin, "download_and_extract", return_value="ok"
    ) as dl:
        assert r.download_and_extract() == "ok"
    dl.assert_called_once_with(
        extract_dirs=[
            "bin/*",
            "tools/wptserve/*",
            "tools/wpt_third_party/h2/*",
            "tools/wpt_third_party/pywebsocket3/*",
        ],
        suite_categories=["common", "condprof", "raptor"],
    )


def prepare_virtualenv(r, tmp_path, platform="linux64"):
    python = tmp_path / "python"
    python.write_text("")
    r.raptor_path = str(tmp_path / "raptor")
    r.query_exe = Mock(return_value=str(python))
    r.platform_name = Mock(return_value=platform)
    r.register_virtualenv_module = Mock()
    r.setup_local_ffmpeg = Mock()


@pytest.mark.parametrize(
    "platform,site_packages",
    [
        ("win64", os.path.join("Lib", "site-packages")),
        ("linux64", os.path.join("lib", "python", "site-packages")),
    ],
)
def test_create_virtualenv_reuses_local_venv(
    make_raptor, tmp_path, monkeypatch, platform, site_packages
):
    venv = tmp_path / "venv"
    venv.mkdir()
    r = make_raptor(run_local=True, virtualenv_path=str(venv))
    prepare_virtualenv(r, tmp_path, platform)
    monkeypatch.setattr(sys, "path", list(sys.path))
    with patch.object(raptor.multiprocessing, "set_executable") as set_exe:
        r.create_virtualenv()
    assert sys.path[-1] == os.path.join(str(venv), site_packages)
    assert set_exe.called == platform.startswith("win")
    r.setup_local_ffmpeg.assert_called_once_with()
    r.register_virtualenv_module.assert_not_called()


def test_create_virtualenv_clean_removes_venv(make_raptor, tmp_path):
    venv = tmp_path / "venv"
    (venv / "bin").mkdir(parents=True)
    r = make_raptor(clean=True, virtualenv_path=str(venv))
    prepare_virtualenv(r, tmp_path)
    with patch.object(VirtualenvMixin, "create_virtualenv") as create:
        r.create_virtualenv()
    assert not venv.exists()
    assert create.call_args.kwargs["modules"] == ["pip>=1.5"]


@pytest.mark.parametrize(
    "minor,numpy",
    [(11, "numpy==1.23.5"), (12, "numpy==2.2.3")],
)
def test_create_virtualenv_visual_metrics(
    make_raptor, tmp_path, monkeypatch, minor, numpy
):
    r = make_raptor(
        browsertime_visualmetrics=True, virtualenv_path=str(tmp_path / "venv")
    )
    prepare_virtualenv(r, tmp_path)
    monkeypatch.setattr(
        raptor, "sys", SimpleNamespace(version_info=SimpleNamespace(minor=minor))
    )
    with patch.object(VirtualenvMixin, "create_virtualenv") as create:
        r.create_virtualenv()
    modules = create.call_args.kwargs["modules"]
    assert modules[0] == "pip>=1.5"
    assert numpy in modules
    assert len(modules) == 6
    r.setup_local_ffmpeg.assert_not_called()


def test_setup_local_ffmpeg_already_on_path(make_raptor, monkeypatch):
    monkeypatch.setenv("PATH", os.pathsep.join(["/usr/bin", "/opt/ffmpeg/bin"]))
    r = make_raptor()
    r.platform_name = Mock()
    r.setup_local_ffmpeg()
    r.platform_name.assert_not_called()


@pytest.mark.parametrize(
    "platform,parts",
    [
        ("macosx64", ["ffmpeg-7.1"]),
        ("linux64", ["ffmpeg-master-latest-linux64-gpl-shared"]),
        ("win64", ["ffmpeg-n7.1-latest-win64-gpl-shared-7.1", "bin"]),
    ],
)
def test_setup_local_ffmpeg_raises_then_adds_dir_to_path(
    make_raptor, tmp_path, monkeypatch, platform, parts
):
    monkeypatch.setenv("PATH", "/usr/bin")
    r = make_raptor(mozbuild_path=str(tmp_path))
    r.platform_name = Mock(return_value=platform)
    expected = tmp_path.joinpath("browsertime", *parts)

    with pytest.raises(Exception, match="No local ffmpeg binary found"):
        r.setup_local_ffmpeg()

    expected.mkdir(parents=True)
    r.setup_local_ffmpeg()
    assert r.browsertime_ffmpeg == str(expected)
    assert os.environ["PATH"] == "/usr/bin" + os.pathsep + str(expected)


@pytest.fixture
def android_raptor(make_raptor):
    def make(app, **config):
        r = make_raptor(app=app, binary_path=f"org.mozilla.{app}", **config)
        r._device = Mock()
        r._device.confirm_clear_app_data.return_value = True
        r.installer_path = f"/apk/{app}.apk"
        r.install_android_app = Mock()
        return r

    return make


def test_install_fenix_clears_data_and_installs_baseline_profile(android_raptor):
    r = android_raptor("fenix")
    r.install()
    r._device.shell_output.assert_called_once_with("pm clear org.mozilla.fenix")
    r._device.uninstall_app.assert_called_once_with("org.mozilla.fenix")
    r._device.install_app_baseline_profile.assert_called_once_with("/apk/fenix.apk")
    r.install_android_app.assert_not_called()


def test_install_geckoview_continues_when_clear_fails(android_raptor):
    r = android_raptor("geckoview")
    r._device.shell_output.side_effect = RuntimeError("not installed")
    r.info = Mock()
    r.install()
    assert "failed (app may not be installed yet)" in r.info.call_args_list[0].args[0]
    r.install_android_app.assert_called_once_with("/apk/geckoview.apk")


def test_install_raises_when_clear_app_data_declined(android_raptor):
    r = android_raptor("geckoview")
    r._device.confirm_clear_app_data.return_value = False
    with pytest.raises(Exception, match="Declined to clear app data"):
        r.install()
    r._device.uninstall_app.assert_not_called()


def test_install_with_no_install_only_clears_app_data(android_raptor):
    r = android_raptor("geckoview", no_install=True)
    r.install()
    r._device.shell_output.assert_called_once_with("pm clear org.mozilla.geckoview")
    r._device.uninstall_app.assert_not_called()


@pytest.mark.parametrize("app", ["chrome-m", "firefox"])
def test_install_uses_testing_mixin(make_raptor, app):
    r = make_raptor(app=app)
    r._device = Mock()
    with patch.object(testbase.TestingMixin, "install") as install:
        r.install()
    install.assert_called_once_with()
    assert r._device.shell_output.called == (app == "chrome-m")


def test_artifact_perf_data_copies_file_or_logs_critical(make_raptor, tmp_path):
    r = make_raptor()
    src = tmp_path / "raptor.json"
    src.write_text("{}")
    dest = tmp_path / "upload" / "perfherder-data.json"
    r._artifact_perf_data(str(src), str(dest))
    assert dest.read_text() == "{}"

    r.critical = Mock()
    r._artifact_perf_data(str(tmp_path / "missing.json"), str(dest))
    r.critical.assert_called_once()


@pytest.fixture
def runner(make_raptor, tmp_path):
    def make(lines=(), return_code=0, **config):
        config.setdefault("binary_path", "/ff")
        r = make_raptor(**config)
        r.raptor_path = str(tmp_path / "raptor")
        r.query_python_path = Mock(return_value="/venv/bin/python")
        r.query_minidump_stackwalk = Mock(return_value="/stackwalk")
        r.logcat_start = Mock()
        r.logcat_stop = Mock()
        calls = []

        def run_command(command, **kwargs):
            calls.append((command, kwargs))
            parser = kwargs.get("output_parser")
            if parser:
                for line in lines:
                    parser.parse_single_line(line)
                return return_code
            return 0

        r.run_command = Mock(side_effect=run_command)
        r.calls = calls
        return r

    return make


def test_run_tests_production_sets_env_and_uploads_perfherder(runner, tmp_path):
    work = tmp_path / "build"
    (work / "application" / "firefox").mkdir(parents=True)
    (work / "tests" / "bin").mkdir(parents=True)
    (work / "tests" / "bin" / "xpcshell.exe").write_text("xpcshell")
    (work / "raptor.json").write_text("main")
    (work / "raptor-os-baseline-power.json").write_text("power")
    (work / "other.json").write_text("other")
    (work / "screenshots.html").write_text("shots")
    r = runner(
        suite="raptor",
        repo_path="/repo",
        obj_path="/obj",
        mozbuild_path="/mozbuild",
        is_release_build=True,
    )
    r.run_tests()

    assert r.calls[0][0] == ["/venv/bin/python", "--version"]
    command, kwargs = r.calls[1]
    upload = work / "blobber_upload_dir"
    assert command[:2] == [
        "/venv/bin/python",
        str(tmp_path / "raptor" / "raptor" / "raptor.py"),
    ]
    assert command[-2:] == [
        "--log-tbpl-level=info",
        "--log-errorsummary=" + str(upload / "speedometer3_errorsummary.log"),
    ]
    env = kwargs["env"]
    assert kwargs["output_timeout"] == 3600
    assert env["MINIDUMP_STACKWALK"] == "/stackwalk"
    assert env["PYTHONPATH"] == r.raptor_path
    assert env["XPCSHELL_PATH"] == str(
        work / "application" / "firefox" / "xpcshell.exe"
    )
    assert env["MOZ_INTERNAL_UPLOAD_DIR"] == str(work / "perftest")
    assert env["MOZ_DISABLE_NONLOCAL_CONNECTIONS"] == "1"
    assert env["MOZ_DEVELOPER_REPO_DIR"] == "/repo"
    assert env["MOZ_DEVELOPER_OBJ_DIR"] == "/obj"
    assert env["MOZ_MOZBUILD_DIR"] == "/mozbuild"
    assert (work / "perftest").is_dir()
    assert (work / "application" / "firefox" / "xpcshell.exe").read_text() == (
        "xpcshell"
    )
    assert (upload / "perfherder-data.json").read_text() == "main"
    assert (upload / "perfherder-data-os-baseline-power.json").read_text() == "power"
    assert (upload / "screenshots.html").read_text() == "shots"
    assert not (upload / "perfherder-data-other.json").exists()
    assert r.return_code == 0
    r.logcat_start.assert_not_called()


def test_run_tests_local_omits_upload_env_and_uses_obj_xpcshell(
    runner, tmp_path, monkeypatch
):
    monkeypatch.setenv("PYTHONPATH", "existing")
    r = runner(run_local=True, obj_path="/obj", raptor_output_timeout=5)
    r.run_tests()
    command, kwargs = r.calls[1]
    env = kwargs["env"]
    assert command[-1] == "--log-tbpl-level=info"
    assert kwargs["output_timeout"] == 5
    assert "MINIDUMP_STACKWALK" not in env
    assert "MOZ_INTERNAL_UPLOAD_DIR" not in env
    assert env["XPCSHELL_PATH"] == os.path.join("/obj", "dist", "bin", "xpcshell.exe")
    assert env["PYTHONPATH"] == r.raptor_path + os.pathsep + "existing"
    assert not (
        tmp_path / "build" / "blobber_upload_dir" / "perfherder-data.json"
    ).exists()


def test_run_tests_power_profile_wraps_in_powerprofilesctl(runner):
    r = runner(run_local=True, power_profile=True)
    r.run_tests()
    command = r.calls[1][0]
    assert command[:3] == ["powerprofilesctl", "launch", "--profile"]
    separator = command.index("raptor-power-profile")
    assert command[separator + 1 :][:2] == [
        "/venv/bin/python",
        os.path.join(r.raptor_path, "raptor", "raptor.py"),
    ]


def test_run_tests_debug_runs_popen_not_run_command(runner):
    r = runner(run_local=True)
    with patch.object(raptor.subprocess, "Popen") as popen:
        r.run_tests(args=["--debug"])
    assert "--debug" in popen.call_args.args[0]
    popen.return_value.wait.assert_called_once_with()
    assert len(r.calls) == 1


def test_run_tests_android_collects_logcat(runner):
    r = runner(run_local=True, raptor_cmd_line_args=["--app=geckoview"])
    r.run_tests()
    r.logcat_start.assert_called_once_with()
    r.logcat_stop.assert_called_once_with()


def test_run_tests_lists_minidumps(runner, tmp_path):
    r = runner(lines=["""raptorError: "error executing: '/a /b /c'\""""])
    r.run_tests()
    assert [c[0] for c in r.calls[2:]] == [
        ["ls", "-l", "/a"],
        ["ls", "-l", "/b"],
        ["ls", "-l", "/c"],
    ]
    assert not (
        tmp_path / "build" / "blobber_upload_dir" / "perfherder-data.json"
    ).exists()


def test_run_tests_log_failure_overrides_return_code(runner):
    r = runner(run_local=True, lines=["INFRA-ERROR: device lost"])
    r.run_tests()
    assert r.return_code == EXIT_STATUS_DICT[TBPL_RETRY]


@pytest.fixture
def monitor():
    with patch.object(raptor, "SystemResourceMonitor") as monitor:
        yield monitor


def make_raptor_parser(r):
    return RaptorOutputParser(
        config=r.config, log_obj=r.log_obj, error_list=RaptorErrorList
    )


def test_output_parser_records_markers_perf_data_and_minidumps(make_raptor, monitor):
    parser = make_raptor_parser(make_raptor())
    for line in [
        "raptor-browsertime Info: BEGIN: speedometer3",
        "raptor-browsertime Info: END: speedometer3",
        "raptor-browsertime Info: cycle 1",
        'PERFHERDER_DATA: {"framework": {}}',
        """raptorError: "error executing: '1 2 3'\"""",
        "plain line",
    ]:
        parser.parse_single_line(line)
    monitor.begin_marker.assert_called_once_with("test", "speedometer3")
    monitor.end_marker.assert_called_once_with("test", "speedometer3")
    monitor.record_event.assert_called_once_with("cycle 1")
    assert parser.found_perf_data == ['{"framework": {}}']
    assert parser.minidump_output == ("1", "2", "3")
    assert parser.tbpl_status == TBPL_SUCCESS


def test_output_parser_adb_error_sets_retry_and_critical(make_raptor, monitor):
    parser = make_raptor_parser(make_raptor())
    parser.parse_single_line("ADBError: device offline")
    assert parser.tbpl_status == TBPL_RETRY
    assert parser.worst_log_level == CRITICAL


if __name__ == "__main__":
    mozunit.main()
