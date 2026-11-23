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
    APPS,
    _PrintTests,
    absolute_binary_argv,
    create_parser,
    default_desktop_binary,
    parse_args,
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


@pytest.fixture
def fake_binary(filedir):
    return os.path.join(filedir, "fake_binary.exe")


def _parse(*extra):
    parser = create_parser()
    return parser, parser.parse_args(["--test", "test-page-1"] + list(extra))


def _assert_parser_error(capsys, parser, args, message):
    with pytest.raises(SystemExit) as exc:
        verify_options(parser, args)
    assert exc.value.code == 2
    assert message in capsys.readouterr().err


def test_verify_options_android_default_binary(capsys):
    """Check that Android apps get their default binary, activity and intent."""
    parser, args = _parse("--app", "fenix")
    verify_options(parser, args)
    assert args.binary == "org.mozilla.fenix"
    assert args.activity == APPS["fenix"]["default_activity"]
    assert args.intent == APPS["fenix"]["default_intent"]
    assert "Using org.mozilla.fenix as default binary" in capsys.readouterr().out


def test_verify_options_chrome_android_keeps_no_binary():
    """Check that chrome-m is allowed to run without a binary."""
    parser, args = _parse(
        "--app", "chrome-m", "--browsertime-chromedriver", "/path/chromedriver"
    )
    verify_options(parser, args)
    assert args.binary is None


def test_verify_options_desktop_default_binary(fake_binary):
    """Check that a desktop app without --binary uses the detected install path."""
    parser, args = _parse("--app", "chrome")
    with mock.patch("cmdline.default_desktop_binary", return_value=fake_binary):
        verify_options(parser, args)
    assert args.binary == fake_binary


def test_verify_options_no_binary_found(capsys):
    """Check that a missing binary with no detected install is a parser error."""
    parser, args = _parse("--app", "safari")
    with mock.patch("cmdline.default_desktop_binary", return_value=None):
        _assert_parser_error(
            capsys,
            parser,
            args,
            "Couldn't find an installed safari, pass --binary <path/to/binary>",
        )


def test_verify_options_debug_mode_in_ci(capsys, fake_binary):
    """Check that --debug-mode is rejected when not running locally."""
    parser, args = _parse("--binary", fake_binary, "--debug-mode")
    _assert_parser_error(capsys, parser, args, "Cannot run debug mode in CI")


def test_verify_options_debug_mode_locally(fake_binary):
    """Check that --debug-mode is accepted with --run-local."""
    parser, args = _parse("--binary", fake_binary, "--debug-mode", "--run-local")
    verify_options(parser, args)
    assert args.debug_mode


def test_verify_options_visualmetrics_enables_video(fake_binary):
    """Check that requesting visual metrics also turns on browsertime video."""
    parser, args = _parse("--binary", fake_binary, "--browsertime-visualmetrics")
    assert not args.browsertime_video
    verify_options(parser, args)
    assert args.browsertime_video


def test_verify_options_chrome_android_requires_chromedriver(capsys):
    """Check that chrome-m requires --browsertime-chromedriver."""
    parser, args = _parse("--app", "chrome-m")
    _assert_parser_error(
        capsys,
        parser,
        args,
        "--browsertime-chromedriver path is required for android chrome tests",
    )


def test_verify_options_chimera_rejects_page_cycles(capsys, fake_binary):
    """Check that chimera mode rejects page cycles other than 2."""
    parser, args = _parse("--binary", fake_binary, "--chimera", "--page-cycles", "3")
    _assert_parser_error(
        capsys,
        parser,
        args,
        "--page-cycles must either not be set, or must be equal to 2 in chimera mode",
    )


@pytest.mark.parametrize(
    "extra,expected_prefs",
    [
        [[], ["fission.autostart=true"]],
        [["--page-cycles", "2"], ["fission.autostart=true"]],
        [
            ["--test-bytecode-cache"],
            [
                "dom.script_loader.bytecode_cache.strategy=-1",
                "fission.autostart=true",
            ],
        ],
    ],
)
def test_verify_options_chimera_defaults(fake_binary, extra, expected_prefs):
    """Check that chimera mode forces cold loads, 2 cycles and the bytecode pref."""
    parser, args = _parse("--binary", fake_binary, "--chimera", *extra)
    verify_options(parser, args)
    assert args.cold
    assert args.page_cycles == 2
    assert args.extra_prefs == expected_prefs


def test_verify_options_bytecode_cache_requires_chimera(capsys, fake_binary):
    """Check that --test-bytecode-cache is rejected without --chimera."""
    parser, args = _parse("--binary", fake_binary, "--test-bytecode-cache")
    _assert_parser_error(
        capsys, parser, args, "--test-bytecode-cache can only be used in --chimera mode"
    )


def test_verify_options_missing_desktop_binary(capsys):
    """Check that a desktop binary path that doesn't exist is a parser error."""
    parser, args = _parse("--binary", "invalid/path")
    _assert_parser_error(capsys, parser, args, "invalid/path does not exist!")


@pytest.mark.parametrize("app", ["chrome", "custom-car"])
def test_verify_options_gecko_profile_on_chromium(capsys, fake_binary, app):
    """Check that --gecko-profile is rejected for Chromium-based desktop apps."""
    parser, args = _parse("--app", app, "--binary", fake_binary, "--gecko-profile")
    _assert_parser_error(
        capsys, parser, args, "Gecko profiling is not supported on Chrome/Chromium!"
    )


@pytest.mark.parametrize(
    "extra,expected_pref,expected_out",
    [
        [[], "fission.autostart=true", "Fission enabled"],
        [["--disable-fission"], "fission.autostart=false", "Fission disabled"],
    ],
)
def test_verify_options_fission_pref(
    capsys, fake_binary, extra, expected_pref, expected_out
):
    """Check that the fission pref is appended to extra prefs and reported."""
    parser, args = _parse("--binary", fake_binary, "--setpref", "a.b=1", *extra)
    verify_options(parser, args)
    assert args.extra_prefs == ["a.b=1", expected_pref]
    assert expected_out in capsys.readouterr().out


def test_verify_options_android_keeps_explicit_activity_and_intent():
    """Check that an explicit Android activity and intent are kept."""
    parser, args = _parse(
        "--app", "geckoview", "--activity", "my.Activity", "--intent", "my.INTENT"
    )
    verify_options(parser, args)
    assert args.activity == "my.Activity"
    assert args.intent == "my.INTENT"


def test_verify_options_benchmark_repository_requires_revision(capsys, fake_binary):
    """Check that a benchmark repository without a revision is a parser error."""
    parser, args = _parse(
        "--binary", fake_binary, "--benchmark-repository", "https://example.com/b"
    )
    _assert_parser_error(
        capsys,
        parser,
        args,
        "When a benchmark repository is provided, a revision is also required.",
    )


def test_verify_options_benchmark_repository_with_revision(fake_binary):
    """Check that a benchmark repository with a revision is accepted."""
    parser, args = _parse(
        "--binary",
        fake_binary,
        "--benchmark-repository",
        "https://example.com/b",
        "--benchmark-revision",
        "abc123",
    )
    verify_options(parser, args)
    assert args.benchmark_revision == "abc123"


def test_verify_options_negative_post_startup_delay(capsys, fake_binary):
    """Check that a negative --post-startup-delay is a parser error."""
    parser, args = _parse("--binary", fake_binary, "--post-startup-delay", "-5")
    _assert_parser_error(
        capsys, parser, args, "--post-startup-delay must be a positive integer (in ms)."
    )


def test_verify_options_positive_post_startup_delay(fake_binary):
    """Check that a positive --post-startup-delay is accepted."""
    parser, args = _parse("--binary", fake_binary, "--post-startup-delay", "5")
    verify_options(parser, args)
    assert args.post_startup_delay == 5


def test_verify_options_simpleperf_unsupported_app(capsys, fake_binary):
    """Check that --simpleperf is rejected for apps that don't support it."""
    parser, args = _parse("--binary", fake_binary, "--simpleperf")
    _assert_parser_error(
        capsys, parser, args, "--simpleperf is only available in: fenix, geckoview"
    )


def test_verify_options_simpleperf_with_gecko_profile(capsys):
    """Check that --simpleperf cannot be combined with --gecko-profile."""
    parser, args = _parse("--app", "fenix", "--simpleperf", "--gecko-profile")
    _assert_parser_error(
        capsys, parser, args, "--simpleperf cannot be used with --gecko-profile."
    )


def test_verify_options_simpleperf_on_android():
    """Check that --simpleperf is accepted on a supported Android app."""
    parser, args = _parse("--app", "geckoview", "--simpleperf")
    verify_options(parser, args)
    assert args.simpleperf


def test_parse_args_host_from_environment(fake_binary):
    """Check that --host HOST_IP is replaced by the HOST_IP environment variable."""
    with mock.patch.dict(os.environ, {"HOST_IP": "10.0.0.7"}):
        args = parse_args(["--test", "t", "--binary", fake_binary, "--host", "HOST_IP"])
    assert args.host == "10.0.0.7"


def test_parse_args_runs_verification(capsys):
    """Check that parse_args runs verify_options on the parsed arguments."""
    with pytest.raises(SystemExit):
        parse_args(["--test", "t", "--binary", "invalid/path"])
    assert "invalid/path does not exist!" in capsys.readouterr().err


def test_parse_args_default_host(fake_binary):
    """Check that parse_args defaults the host to 127.0.0.1."""
    args = parse_args(["--test", "t", "--binary", fake_binary])
    assert args.host == "127.0.0.1"


def test_print_tests(capsys):
    """Check that --print-tests lists suites and pageload subtests per app."""
    tests_by_app = {
        "firefox": [
            {
                "name": "speedometer",
                "manifest": "/m/benchmarks.toml",
                "type": "benchmark",
            },
            {
                "name": "amazon",
                "manifest": "/m/tp6.toml",
                "type": "pageload",
                "measure": "fcp\nloadtime",
            },
            {"name": "bing", "manifest": "/m/tp6.toml", "type": "pageload"},
            {"manifest": "/m/unnamed.toml", "type": "pageload"},
            {"name": "untyped", "manifest": "/m/misc.toml"},
        ],
        "fenix": [
            {"name": "youtube", "manifest": "/m/tp6m.toml", "type": "pageload"},
        ],
    }

    def fake_manifest(*args, **kwargs):
        manifest = mock.MagicMock()
        manifest.active_tests.side_effect = lambda **kw: tests_by_app.get(kw["app"], [])
        return manifest

    parser = create_parser()
    with mock.patch("manifestparser.TestManifest", side_effect=fake_manifest):
        with pytest.raises(SystemExit) as exc:
            parser.parse_args(["--print-tests"])
    assert exc.value.code == 0
    out = capsys.readouterr().out
    assert "Raptor Tests Available for Firefox Desktop\n" in out
    assert (
        "Raptor Tests Available for Firefox Android Fenix Browser (--app=fenix)" in out
    )
    assert "Google Chrome" not in out
    assert "\nbenchmarks\n  type: benchmark\n\n" in out
    assert (
        "\ntp6\n  type: pageload\n  subtests:\n    amazon (fcp, loadtime)\n    bing\n"
        in out
    )
    assert "\nmisc\n  type: None\n" in out
    assert "unnamed" not in out
    assert "\ntp6m\n  type: pageload\n  subtests:\n    youtube\n" in out
    assert out.endswith("\nDone.\n")


def test_print_tests_filter_app():
    """Check that filter_app only yields tests whose apps include the requested app."""
    action = _PrintTests(option_strings=["--print-tests"])
    tests = [{"apps": "firefox, chrome"}, {"apps": "fenix"}]
    assert list(action.filter_app(tests, {"app": "fenix"})) == [tests[1]]


if __name__ == "__main__":
    mozunit.main()
