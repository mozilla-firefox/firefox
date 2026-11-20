import os
import sys
from unittest import mock

import mozunit
import pytest

# need this so the raptor unit tests can find raptor/raptor classes
here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from argparse import ArgumentParser, Namespace

from cmdline import (
    absolute_binary_argv,
    create_parser,
    default_desktop_binary,
    verify_options,
)


def test_verify_options(filedir):
    args = Namespace(
        app="firefox",
        binary="invalid/path",
        gecko_profile="False",
        page_cycles=1,
        page_timeout=60000,
        debug="True",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    parser = ArgumentParser()

    with pytest.raises(SystemExit):
        verify_options(parser, args)

    args.binary = os.path.join(filedir, "fake_binary.exe")
    verify_options(parser, args)  # assert no exception

    args = Namespace(
        app="geckoview",
        binary="org.mozilla.geckoview_example",
        activity="org.mozilla.geckoview_example.GeckoViewActivity",
        intent="android.intent.action.MAIN",
        gecko_profile="False",
        is_release_build=False,
        host="sophie",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    verify_options(parser, args)  # assert no exception

    args = Namespace(
        app="refbrow",
        binary="org.mozilla.reference.browser",
        activity="org.mozilla.reference.browser.BrowserTestActivity",
        intent="android.intent.action.MAIN",
        gecko_profile="False",
        is_release_build=False,
        host="sophie",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    verify_options(parser, args)  # assert no exception

    args = Namespace(
        app="fenix",
        binary="org.mozilla.fenix.browser",
        activity="org.mozilla.fenix.browser.BrowserPerformanceTestActivity",
        intent="android.intent.action.VIEW",
        gecko_profile="False",
        is_release_build=False,
        host="sophie",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    verify_options(parser, args)  # assert no exception

    args = Namespace(
        app="geckoview",
        binary="org.mozilla.geckoview_example",
        activity="org.mozilla.geckoview_example.GeckoViewActivity",
        intent="android.intent.action.MAIN",
        gecko_profile="False",
        is_release_build=False,
        host="sophie",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    verify_options(parser, args)  # assert no exception

    args = Namespace(
        app="refbrow",
        binary="org.mozilla.reference.browser",
        activity=None,
        intent="android.intent.action.MAIN",
        gecko_profile="False",
        is_release_build=False,
        host="sophie",
        chimera=False,
        browsertime_video=False,
        browsertime_visualmetrics=False,
        fission=True,
        test_bytecode_cache=False,
        webext=False,
        extra_prefs=[],
        benchmark_repository=None,
        benchmark_revision=None,
        benchmark_branch=None,
        post_startup_delay=None,
        power_test=False,
        simpleperf=False,
    )
    parser = ArgumentParser()

    verify_options(parser, args)  # also will work as uses default activity


@mock.patch("perftest.Perftest.build_browser_profile", new=mock.MagicMock())
@pytest.mark.parametrize(
    "args,settings_to_check",
    [
        # Test that post_startup_delay is 30s as expected
        [
            [
                "--test",
                "test-page-1",
                "--binary",
                "invalid/path",
                # This gets set automatically from mach_commands, but is set
                # to False by default in the Perftest class
                "--run-local",
            ],
            [
                ("post_startup_delay", 30000),
                ("run_local", True),
                ("debug_mode", False),
            ],
        ],
        # Test that run_local is false by default
        [
            [
                "--test",
                "test-page-1",
                "--binary",
                "invalid/path",
            ],
            [
                ("post_startup_delay", 30000),
                ("run_local", False),
                ("debug_mode", False),
            ],
        ],
        # Test that debug mode gets set when running locally
        [
            [
                "--test",
                "test-page-1",
                "--binary",
                "invalid/path",
                "--debug-mode",
                "--run-local",
            ],
            [
                ("post_startup_delay", 3000),
                ("run_local", True),
                ("debug_mode", True),
            ],
        ],
        # Test that debug mode doesn't get set when we're not running locally
        [
            [
                "--test",
                "test-page-1",
                "--binary",
                "invalid/path",
                "--debug-mode",
            ],
            [
                ("post_startup_delay", 30000),
                ("run_local", False),
                ("debug_mode", False),
            ],
        ],
    ],
)
def test_perftest_setup_with_args(ConcretePerftest, args, settings_to_check):
    parser = create_parser()
    args = parser.parse_args(args)

    perftest = ConcretePerftest(**vars(args))
    for setting, expected in settings_to_check:
        assert getattr(perftest, setting) == expected


@pytest.mark.parametrize(
    "args, expected",
    [
        [[], []],
        [
            ["--install-extension", "uBlock0@raymondhill.net"],
            ["uBlock0@raymondhill.net"],
        ],
        [
            [
                "--install-extension",
                "uBlock0@raymondhill.net",
                "--install-extension",
                "https://example.com/a.xpi",
            ],
            ["uBlock0@raymondhill.net", "https://example.com/a.xpi"],
        ],
    ],
)
def test_install_extension_arg(args, expected):
    parser = create_parser()
    parsed = parser.parse_args(
        ["--test", "test-page-1", "--binary", "invalid/path"] + args
    )
    assert parsed.install_extensions == expected


@mock.patch("perftest.Perftest.build_browser_profile", new=mock.MagicMock())
def test_install_extension_reaches_config(ConcretePerftest):
    parser = create_parser()
    args = parser.parse_args([
        "--test",
        "test-page-1",
        "--binary",
        "invalid/path",
        "--install-extension",
        "uBlock0@raymondhill.net",
    ])
    perftest = ConcretePerftest(**vars(args))
    assert perftest.config["install_extensions"] == ["uBlock0@raymondhill.net"]


@mock.patch("perftest.Perftest.build_browser_profile", new=mock.MagicMock())
def test_install_extra_extensions_flattens_csv(ConcretePerftest):
    parser = create_parser()
    args = parser.parse_args([
        "--test",
        "test-page-1",
        "--binary",
        "invalid/path",
        # mach try perf forwards multiple extensions as one comma-separated value
        "--install-extension",
        "https://amo/a.xpi,https://amo/b.xpi",
        "--install-extension",
        "https://amo/c.xpi",
    ])
    perftest = ConcretePerftest(**vars(args))
    perftest.profile = mock.MagicMock()
    with mock.patch("condprof.util.download_file", side_effect=lambda url, **kw: url):
        perftest.install_extra_extensions()
    perftest.profile.addons.install.assert_called_once_with([
        "https://amo/a.xpi",
        "https://amo/b.xpi",
        "https://amo/c.xpi",
    ])
    # Each installed addon's details are looked up so its id/version is logged.
    assert perftest.profile.addons.addon_details.call_count == 3


@mock.patch("perftest.Perftest.build_browser_profile", new=mock.MagicMock())
def test_perfherder_extra_option_reaches_extra_options(ConcretePerftest):
    parser = create_parser()
    args = parser.parse_args([
        "--test",
        "test-page-1",
        "--binary",
        "invalid/path",
        "--perfherder-extra-option",
        "ab-base",
        "--perfherder-extra-option",
        "other",
    ])
    perftest = ConcretePerftest(**vars(args))
    extra_options = perftest.results_handler.build_extra_options()
    assert extra_options[-2:] == ["ab-base", "other"]


@pytest.mark.parametrize(
    "args, expected_failure",
    [
        # Test that post_startup_delay is 30s as expected
        [
            {
                "app": "firefox",
                "binary": "org.mozilla.reference.browser",
                "activity": None,
                "intent": "android.intent.action.MAIN",
                "gecko_profile": "False",
                "is_release_build": False,
                "host": "sophie",
                "chimera": False,
                "browsertime_video": False,
                "browsertime_visualmetrics": False,
                "fission": True,
                "test_bytecode_cache": False,
                "webext": False,
                "extra_prefs": [],
                "benchmark_repository": None,
                "benchmark_revision": None,
                "benchmark_branch": None,
                "post_startup_delay": None,
                "power_test": True,
                "simpleperf": False,
            },
            SystemExit,
        ],
    ],
)
def test_verify_options_failure(ConcretePerftest, args, expected_failure):
    args = Namespace(**args)
    parser = ArgumentParser()
    with pytest.raises(expected_failure):
        verify_options(parser, args)


@pytest.mark.parametrize(
    "app,platform,env,which,expected",
    [
        (
            "chrome",
            "darwin",
            {},
            {},
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        ),
        (
            "chrome",
            "win32",
            {"PROGRAMFILES": "C:\\Program Files"},
            {},
            os.path.join(
                "C:\\Program Files", "Google", "Chrome", "Application", "chrome.exe"
            ),
        ),
        (
            "chrome",
            "linux",
            {},
            {"google-chrome-stable": "/opt/bin/google-chrome-stable"},
            "/opt/bin/google-chrome-stable",
        ),
        ("safari", "darwin", {}, {}, "/Applications/Safari.app/Contents/MacOS/Safari"),
        ("safari", "linux", {}, {}, None),
        (
            "safari-tp",
            "darwin",
            {},
            {},
            "/Applications/Safari Technology Preview.app/Contents/MacOS/"
            "Safari Technology Preview",
        ),
        ("safari-tp", "linux", {}, {}, None),
        ("custom-car", "darwin", {}, {}, None),
    ],
)
def test_default_desktop_binary(app, platform, env, which, expected):
    with mock.patch("cmdline.sys.platform", platform):
        with mock.patch.dict(os.environ, env, clear=True):
            with mock.patch("cmdline.shutil.which", side_effect=which.get):
                with mock.patch("cmdline.os.path.isfile", return_value=True):
                    assert default_desktop_binary(app) == expected


def test_default_desktop_binary_not_installed():
    with mock.patch("cmdline.sys.platform", "darwin"):
        with mock.patch("cmdline.os.path.isfile", return_value=False):
            assert default_desktop_binary("chrome") is None


@pytest.mark.parametrize(
    "argv",
    [
        ["-t", "sp3", "--binary", "rel/ff"],
        ["-t", "sp3", "-b", "rel/ff"],
        ["-t", "sp3", "--binary=rel/ff"],
        ["-t", "sp3", "--bin", "rel/ff"],
        ["-t", "sp3", "--bina=rel/ff"],
        ["-t", "sp3", "-brel/ff"],
        ["-t", "sp3"],
    ],
)
def test_absolute_binary_argv(argv):
    result = absolute_binary_argv(argv, "/abs/ff")
    assert result == argv + ["--binary", "/abs/ff"]
    args, _ = create_parser().parse_known_args(result)
    assert args.binary == "/abs/ff"


@pytest.mark.parametrize("binary", [None, "org.mozilla.fenix"])
def test_absolute_binary_argv_unchanged(binary):
    argv = ["-t", "sp3", "--binary", "org.mozilla.fenix"]
    assert absolute_binary_argv(argv, binary) == argv


if __name__ == "__main__":
    mozunit.main()
