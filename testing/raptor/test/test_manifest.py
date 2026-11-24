import json
import os
import re
import sys
from unittest.mock import MagicMock, patch
from urllib.parse import parse_qs, urlsplit

import mozinfo
import mozunit
import pytest

# need this so raptor imports work both from /raptor and via mach
here = os.path.abspath(os.path.dirname(__file__))

raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import manifest
from manifest import (
    add_test_url_params,
    get_browser_test_list,
    get_raptor_test_list,
    validate_test_toml,
    write_test_settings_json,
)

# some test details (test TOMLs)
VALID_MANIFESTS = [
    {
        # page load test with local playback
        "alert_on": "fcp",
        "alert_threshold": 2.0,
        "apps": "firefox",
        "lower_is_better": True,
        "manifest": "valid_details_0",
        "measure": ["fnbpaint", "fcp"],
        "page_cycles": 25,
        "playback": "mitmproxy",
        "playback_pageset_manifest": "pageset.manifest",
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
    },
    {
        # test optional settings with None
        "alert_threshold": 2.0,
        "apps": "firefox",
        "lower_is_better": True,
        "manifest": "valid_details_1",
        "measure": "fnbpaint, fcb",
        "page_cycles": 25,
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
        "alert_change_type": None,
        "alert_on": None,
        "playback": None,
    },
    {
        # page load test for geckoview
        "alert_threshold": 2.0,
        "apps": "geckoview",
        "browser_cycles": 10,
        "lower_is_better": False,
        "manifest": "valid_details_2",
        "measure": "fcp",
        "page_cycles": 1,
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "score",
    },
    {
        # benchmark test for chrome
        "alert_threshold": 2.0,
        "apps": "chrome",
        "lower_is_better": False,
        "manifest": "valid_details_1",
        "measure": "fcp",
        "page_cycles": 5,
        "test_url": "http://www.test-url/goes/here",
        "type": "benchmark",
        "unit": "score",
    },
]

INVALID_MANIFESTS = [
    {
        "alert_threshold": 2.0,
        "apps": "firefox",
        "lower_is_better": True,
        "manifest": "invalid_details_0",
        "page_cycles": 25,
        "playback": "mitmproxy",
        "playback_pageset_manifest": "pageset.manifest",
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
    },
    {
        "alert_threshold": 2.0,
        "apps": "chrome",
        "lower_is_better": True,
        "manifest": "invalid_details_1",
        "measure": "fnbpaint, fcp",
        "page_cycles": 25,
        "playback": "mitmproxy",
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
    },
    {
        "alert_on": "nope",
        "alert_threshold": 2.0,
        "apps": "firefox",
        "lower_is_better": True,
        "manifest": "invalid_details_2",
        "measure": "fnbpaint, fcp",
        "page_cycles": 25,
        "playback": "mitmproxy",
        "playback_pageset_manifest": "pageset.manifest",
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
    },
]


@patch("logger.logger.RaptorLogger.info")
@patch("logger.logger.RaptorLogger.critical")
@pytest.mark.parametrize("app", ["firefox", "chrome", "geckoview", "refbrow", "fenix"])
def test_get_browser_test_list(mock_info, mock_critical, app):
    test_list = get_browser_test_list(app, run_local=True)
    assert len(test_list) > 0


@pytest.mark.parametrize("test_details", VALID_MANIFESTS)
def test_validate_test_toml_valid(test_details):
    assert validate_test_toml(test_details)


@patch("logger.logger.RaptorLogger.info")
@patch("logger.logger.RaptorLogger.critical")
@patch("logger.logger.RaptorLogger.error")
@pytest.mark.parametrize("test_details", INVALID_MANIFESTS)
def test_validate_test_toml_invalid(mock_info, mock_critical, mock_error, test_details):
    assert not (validate_test_toml(test_details))


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_firefox(mock_info, create_args):
    args = create_args(browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 4

    subtests = ["test-page-1", "test-page-2", "test-page-3", "test-page-4"]

    for next_subtest in test_list:
        assert next_subtest["name"] in subtests


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_chrome(mock_info, create_args):
    args = create_args(app="chrome", test="speedometer2", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "speedometer2"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_geckoview(mock_info, create_args):
    args = create_args(app="geckoview", test="unity-webgl", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "unity-webgl"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_gecko_profiling_enabled(mock_info, create_args):
    args = create_args(test="amazon", gecko_profile=True, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["gecko_profile"] is True
    assert test_list[0].get("gecko_profile_interval") == "1"
    assert test_list[0].get("gecko_profile_threads") is None
    assert test_list[0].get("gecko_profile_features") is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_gecko_profiling_enabled_args_override(
    mock_info, create_args
):
    args = create_args(
        test="amazon",
        gecko_profile=True,
        gecko_profile_entries=42,
        gecko_profile_interval=100,
        gecko_profile_threads="Foo",
        gecko_profile_features="Mood,UserNetWorth",
        browser_cycles=1,
    )

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["gecko_profile"] is True
    assert test_list[0]["gecko_profile_entries"] == "42"
    assert test_list[0]["gecko_profile_interval"] == "100"
    assert test_list[0]["gecko_profile_threads"] == "Foo"
    assert test_list[0]["gecko_profile_features"] == "Mood,UserNetWorth"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_gecko_profiling_enabled_extra_args_override(
    mock_info, create_args
):
    args = create_args(
        test="amazon",
        gecko_profile=True,
        gecko_profile_entries=42,
        gecko_profile_interval=100,
        gecko_profile_extra_threads=["Foo", "Oof"],
        gecko_profile_threads="String,Rope",
        browser_cycles=1,
    )

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["gecko_profile"] is True
    assert test_list[0]["gecko_profile_entries"] == "42"
    assert test_list[0]["gecko_profile_interval"] == "100"
    assert test_list[0]["gecko_profile_threads"] == "String,Rope,Foo,Oof"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_gecko_profiling_disabled(mock_info, create_args):
    args = create_args(
        test="amazon",
        gecko_profile=False,
        gecko_profile_entries=42,
        gecko_profile_interval=100,
        gecko_profile_threads=["Foo"],
        gecko_profile_features=["Temperature"],
        browser_cycles=1,
    )

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0].get("gecko_profile") is None
    assert test_list[0].get("gecko_profile_entries") is None
    assert test_list[0].get("gecko_profile_interval") is None
    assert test_list[0].get("gecko_profile_threads") is None
    assert test_list[0].get("gecko_profile_features") is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_gecko_profiling_disabled_args_override(
    mock_info, create_args
):
    args = create_args(
        test="amazon",
        gecko_profile=False,
        gecko_profile_entries=42,
        gecko_profile_interval=100,
        gecko_profile_threads=["Foo"],
        gecko_profile_features=["Temperature"],
        browser_cycles=1,
    )

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0].get("gecko_profile") is None
    assert test_list[0].get("gecko_profile_entries") is None
    assert test_list[0].get("gecko_profile_interval") is None
    assert test_list[0].get("gecko_profile_threads") is None
    assert test_list[0].get("gecko_profile_features") is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_extra_profiler_run_enabled(mock_info, create_args):
    args = create_args(test="amazon", extra_profiler_run=True, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["extra_profiler_run"] is True


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_extra_profiler_run_disabled(mock_info, create_args):
    args = create_args(test="amazon", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0].get("extra_profiler_run") is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_extra_profiler_run_enabled_chrome(mock_info, create_args):
    args = create_args(
        app="chrome", test="unity-webgl", extra_profiler_run=True, browser_cycles=1
    )

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "unity-webgl"
    assert test_list[0]["extra_profiler_run"] is True


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_extra_profiler_run_disabled_chrome(
    mock_info, create_args
):
    args = create_args(app="chrome", test="unity-webgl", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "unity-webgl"
    assert test_list[0].get("extra_profiler_run") is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_debug_mode(mock_info, create_args):
    args = create_args(test="amazon", debug_mode=True, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["debug_mode"] is True
    assert test_list[0]["page_cycles"] == 2


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_using_live_sites(mock_info, create_args):
    args = create_args(test="amazon", live_sites=True, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["use_live_sites"]
    assert test_list[0]["playback"] is None


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_using_collect_perfstats(mock_info, create_args):
    args = create_args(test="amazon", collect_perfstats=True, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["perfstats"] == "true"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_override_page_cycles(mock_info, create_args):
    args = create_args(test="amazon", page_cycles=99, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["page_cycles"] == 99


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_override_page_timeout(mock_info, create_args):
    args = create_args(test="amazon", page_timeout=9999, browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    assert test_list[0]["page_timeout"] == 9999


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_add_test_url_params(mock_info, create_args):
    args = create_args(test="amazon", test_url_params="c=4", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "amazon"
    query_params = parse_qs(urlsplit(test_list[0]["test_url"]).query)
    assert query_params.get("c") == ["4"]


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_refbrow(mock_info, create_args):
    args = create_args(app="refbrow", test="speedometer2", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "speedometer2"


@patch("logger.logger.RaptorLogger.info")
def test_get_raptor_test_list_fenix(mock_info, create_args):
    args = create_args(app="fenix", test="speedometer2", browser_cycles=1)

    test_list = get_raptor_test_list(args, mozinfo.os)
    assert len(test_list) == 1
    assert test_list[0]["name"] == "speedometer2"


def test_add_test_url_params_with_single_extra_param():
    initial_test_url = "http://test.com?a=1&b=2"
    extra_params = "c=3"

    result = add_test_url_params(initial_test_url, extra_params)

    expected_params = {"a": ["1"], "b": ["2"], "c": ["3"]}
    actual_params = parse_qs(urlsplit(result).query)
    assert actual_params == expected_params


def test_add_test_url_params_with_multiple_extra_param():
    initial_test_url = "http://test.com?a=1&b=2"
    extra_params = "c=3&d=4"

    result = add_test_url_params(initial_test_url, extra_params)

    expected_params = {"a": ["1"], "b": ["2"], "c": ["3"], "d": ["4"]}
    actual_params = parse_qs(urlsplit(result).query)
    assert actual_params == expected_params


def test_add_test_url_params_without_params_in_url():
    initial_test_url = "http://test.com"
    extra_params = "c=3"

    result = add_test_url_params(initial_test_url, extra_params)

    expected_params = {"c": ["3"]}
    actual_params = parse_qs(urlsplit(result).query)
    assert actual_params == expected_params


def test_add_test_url_params_overwrites_single_param():
    initial_test_url = "http://test.com?a=1&b=2"
    extra_params = "b=3"

    result = add_test_url_params(initial_test_url, extra_params)

    expected_params = {"a": ["1"], "b": ["3"]}
    actual_params = parse_qs(urlsplit(result).query)
    assert actual_params == expected_params


def test_add_test_url_params_overwrites_multiple_param():
    initial_test_url = "http://test.com?a=1&b=2&c=3"
    extra_params = "c=4&b=5"

    result = add_test_url_params(initial_test_url, extra_params)

    expected_params = {"a": ["1"], "b": ["5"], "c": ["4"]}
    actual_params = parse_qs(urlsplit(result).query)
    assert actual_params == expected_params


def _base_details(**kwargs):
    details = {
        "alert_threshold": 2.0,
        "apps": "firefox",
        "lower_is_better": "true",
        "manifest": "synthetic.toml",
        "measure": ["fcp"],
        "name": "synthetic",
        "page_cycles": 5,
        "test_url": "http://www.test-url/goes/here",
        "type": "pageload",
        "unit": "ms",
    }
    details.update(kwargs)
    return details


@patch("manifest.LOG")
def test_validate_test_toml_sets_default_page_timeout(mock_log):
    """Check that a missing page_timeout defaults to 30000 ms."""
    details = _base_details()

    assert validate_test_toml(details)
    assert details["page_timeout"] == 30000


@patch("manifest.LOG")
def test_validate_test_toml_youtube_playback_default_measure(mock_log):
    """Check that youtube-playback tests without measure use the full measure list for alert_on."""
    details = _base_details(
        name="raptor-youtube-playback-h264",
        type="benchmark",
        alert_on="H264.1080p30@1X_dropped_frames, H264.1080p30@1X_dropped_frames",
    )
    del details["measure"]

    assert validate_test_toml(details)
    assert details["measure"] == manifest.YOUTUBE_PLAYBACK_MEASURE
    assert details["alert_on"] == ["H264.1080p30@1X_dropped_frames"]


@patch("manifest.LOG")
def test_validate_test_toml_alert_on_wildcard_and_newlines(mock_log):
    """Check that alert_on wildcards and newlines expand to sorted measure names."""
    details = _base_details(
        measure=["fcp", "loadtime", "fnbpaint"], alert_on="f*\nloadtime"
    )

    assert validate_test_toml(details)
    assert details["alert_on"] == ["fcp", "fnbpaint", "loadtime"]


@patch("manifest.LOG")
def test_validate_test_toml_repository_requires_revision(mock_log):
    """Check that a repository without repository_revision is rejected."""
    details = _base_details(type="benchmark", repository="https://repo")

    assert not validate_test_toml(details)
    mock_log.error.assert_called_once_with(
        "`repository_revision` is required when a `repository` is defined."
    )


@patch("manifest.LOG")
def test_validate_test_toml_repository_only_for_benchmarks(mock_log):
    """Check that a repository on a non-benchmark pageload test is rejected."""
    details = _base_details(repository="https://repo", repository_revision="abc")

    assert not validate_test_toml(details)
    mock_log.error.assert_called_once_with(
        "`repository` is only available for benchmark test types."
    )


@pytest.mark.parametrize(
    "extra",
    [
        {"type": "benchmark"},
        {"benchmark_webserver": True},
    ],
)
@patch("manifest.LOG")
def test_validate_test_toml_repository_allowed(mock_log, extra):
    """Check that a repository is accepted for benchmarks and benchmark webservers."""
    details = _base_details(
        repository="https://repo", repository_revision="abc", **extra
    )

    assert validate_test_toml(details)
    mock_log.error.assert_not_called()


def _settings_details(**kwargs):
    details = _base_details(
        cold=False, expected_browser_cycles=1, page_timeout=30000, **kwargs
    )
    return details


def _write_and_load(tmp_path, details, oskey="linux"):
    args = MagicMock(host="127.0.0.1")
    with patch("manifest.tests_dir", str(tmp_path)):
        write_test_settings_json(args, details, oskey)
    with open(os.path.join(str(tmp_path), "json", details["name"] + ".json")) as fh:
        return json.load(fh)["raptor-options"]


def test_write_test_settings_json_defaults(tmp_path):
    """Check that unit and lower_is_better defaults are written to the settings json."""
    details = _settings_details(measure=["fcp", "loadtime"])
    del details["unit"]
    del details["lower_is_better"]

    options = _write_and_load(tmp_path, details)

    assert options["page_cycles"] == 5
    assert options["host"] == "127.0.0.1"
    assert options["measure"] == {"fcp": True, "loadtime": True}
    assert options["unit"] == "ms"
    assert options["lower_is_better"] is True
    assert options["subtest_unit"] == "ms"
    assert options["subtest_lower_is_better"] is True
    assert options["alert_threshold"] == 2.0
    assert "alert_on" not in options
    assert "gecko_profile" not in options


def test_write_test_settings_json_optional_fields(tmp_path):
    """Check that optional test fields are copied into the settings json."""
    details = _settings_details(
        measure=["fcp", "hero"],
        hero="hero1, hero2",
        alert_on=["fcp"],
        alert_change_type="absolute",
        screen_capture=True,
        newtab_per_cycle=True,
        subtest_unit="score",
        subtest_lower_is_better=False,
        extra_profiler_run=True,
        test_url="http://host/{platform}/index.html",
    )

    options = _write_and_load(tmp_path, details, oskey="linux")

    assert options["measure"] == {"fcp": True, "hero": ["hero1", "hero2"]}
    assert options["alert_on"] == ["fcp"]
    assert options["alert_change_type"] == "absolute"
    assert options["screen_capture"] is True
    assert options["newtab_per_cycle"] is True
    assert options["subtest_unit"] == "score"
    assert options["subtest_lower_is_better"] is False
    assert options["extra_profiler_run"] is True
    assert options["test_url"] == "http://host/linux64/index.html"
    assert details["test_url"] == "http://host/linux64/index.html"


def test_write_test_settings_json_gecko_profile(tmp_path):
    """Check that gecko profiling writes default entries, interval and merged threads."""
    details = _settings_details(
        gecko_profile=True,
        gecko_profile_threads="Foo,,Bar",
        gecko_profile_features="js,stackwalk",
    )

    options = _write_and_load(tmp_path, details)

    assert options["gecko_profile"] is True
    assert options["gecko_profile_entries"] == 1000000
    assert options["gecko_profile_interval"] == 1.0
    assert sorted(options["gecko_profile_threads"].split(",")) == [
        "Bar",
        "Compositor",
        "Foo",
        "GeckoMain",
        "Renderer",
    ]
    assert options["gecko_profile_features"] == "js,stackwalk"


@pytest.mark.parametrize(
    "extra,expected_background",
    [({}, False), ({"background_test": True}, True)],
)
def test_write_test_settings_json_scenario(tmp_path, extra, expected_background):
    """Check that scenario tests write scenario_time and background_test."""
    details = _settings_details(type="scenario", scenario_time=1000, **extra)

    options = _write_and_load(tmp_path, details)

    assert "measure" not in options
    assert options["scenario_time"] == 1000
    assert options["background_test"] is expected_background


def test_write_test_settings_json_existing_dir(tmp_path):
    """Check that settings are written when the json directory already exists."""
    os.mkdir(os.path.join(str(tmp_path), "json"))
    details = _settings_details(type="benchmark")

    options = _write_and_load(tmp_path, details)

    assert options["type"] == "benchmark"
    assert "measure" not in options


@patch("manifest.LOG")
def test_write_test_settings_json_write_error(mock_log, tmp_path):
    """Check that an OSError while writing the settings json is logged."""
    details = _settings_details()
    args = MagicMock(host=None)

    with patch("manifest.tests_dir", str(tmp_path)):
        with patch("builtins.open", side_effect=OSError("boom")):
            write_test_settings_json(args, details, "linux")

    mock_log.info.assert_called_once_with(
        "abort: exception writing test settings json!"
    )


def _entry(**kwargs):
    kwargs.setdefault("measure", "fcp")
    return _base_details(**kwargs)


def _run_test_list(tmp_path, args, available):
    with patch("manifest.LOG"):
        with patch("manifest.tests_dir", str(tmp_path)):
            with patch("manifest.get_browser_test_list", return_value=available):
                return get_raptor_test_list(args, "linux")


def test_get_raptor_test_list_duplicate_names(tmp_path, create_args):
    """Check that two tests with the same name raise an error naming both manifests."""
    args = create_args(test="dup", browser_cycles=None)
    available = [
        _entry(name="dup", manifest="a.toml"),
        _entry(name="dup", manifest="b.toml"),
    ]

    with pytest.raises(
        Exception,
        match=re.escape(
            "Too many tests found with the same test name `dup` for this app. "
            "Found in these manifests: ['a.toml', 'b.toml']"
        ),
    ):
        _run_test_list(tmp_path, args, available)


def test_get_raptor_test_list_manifest_match(tmp_path, create_args):
    """Check that a manifest file name selects all of its subtests."""
    args = create_args(test="suite", browser_cycles=None)
    available = [
        _entry(name="one", manifest=os.path.join("dir", "suite.toml")),
        _entry(name="two", manifest=os.path.join("dir", "suite.toml")),
        _entry(name="three", manifest=os.path.join("dir", "other.toml")),
    ]

    test_list = _run_test_list(tmp_path, args, available)

    assert [t["name"] for t in test_list] == ["one", "two"]
    assert sorted(os.listdir(os.path.join(str(tmp_path), "json"))) == [
        "one.json",
        "two.json",
    ]


@pytest.mark.parametrize("app", ["chrome", "custom-car"])
def test_get_raptor_test_list_no_perfstats_for_chrome(tmp_path, create_args, app):
    """Check that perfstats are not enabled for Chrome-based apps."""
    args = create_args(
        app=app, test="synthetic", collect_perfstats=True, browser_cycles=None
    )

    test_list = _run_test_list(tmp_path, args, [_entry()])

    assert "perfstats" not in test_list[0]


@pytest.mark.parametrize("chimera,expected_page_cycles", [(False, 1), (True, 2)])
def test_get_raptor_test_list_extra_profiler_run_chimera(
    tmp_path, create_args, chimera, expected_page_cycles
):
    """Check that extra-profiler-run sets its page cycles to 2 only in chimera mode."""
    args = create_args(
        test="synthetic",
        extra_profiler_run=True,
        gecko_profile=False,
        chimera=chimera,
        browser_cycles=None,
    )

    test_list = _run_test_list(tmp_path, args, [_entry()])

    assert test_list[0]["extra_profiler_run"] is True
    assert test_list[0]["extra_profiler_run_browser_cycles"] == 1
    assert test_list[0]["extra_profiler_run_page_cycles"] == expected_page_cycles


def test_get_raptor_test_list_extra_profiler_run_unsupported_app(tmp_path, create_args):
    """Check that extra-profiler-run is disabled for apps without profiler support."""
    args = create_args(
        app="safari", test="synthetic", extra_profiler_run=True, browser_cycles=None
    )

    test_list = _run_test_list(
        tmp_path,
        args,
        [_entry(apps="safari", gecko_profile_entries="10")],
    )

    assert args.extra_profiler_run is False
    assert "extra_profiler_run" not in test_list[0]
    assert "gecko_profile_entries" not in test_list[0]


def test_get_raptor_test_list_gecko_profile_caps_cycles(tmp_path, create_args):
    """Check that gecko profiling caps browser cycles at 3 for cold tests."""
    args = create_args(test="synthetic", gecko_profile=True, browser_cycles=None)

    test_list = _run_test_list(
        tmp_path,
        args,
        [_entry(page_cycles="25", browser_cycles="10", cold="true")],
    )

    assert test_list[0]["gecko_profile"] is True
    assert test_list[0]["browser_cycles"] == 3
    assert test_list[0]["expected_browser_cycles"] == 3
    assert test_list[0]["page_cycles"] == 1
    assert test_list[0]["cold"] is True


@pytest.mark.parametrize("chimera,expected_page_cycles", [(False, 1), (True, 2)])
def test_get_raptor_test_list_cold(
    tmp_path, create_args, chimera, expected_page_cycles
):
    """Check that cold mode uses browser cycles and sets page cycles per chimera."""
    args = create_args(
        test="synthetic",
        gecko_profile=False,
        extra_profiler_run=False,
        cold=True,
        chimera=chimera,
        browser_cycles=7,
    )

    test_list = _run_test_list(tmp_path, args, [_entry(page_cycles="25")])

    assert test_list[0]["cold"] is True
    assert test_list[0]["browser_cycles"] == 7
    assert test_list[0]["expected_browser_cycles"] == 7
    assert test_list[0]["page_cycles"] == expected_page_cycles
    assert test_list[0]["browser_cycle"] == 1


def test_get_raptor_test_list_warm_resets_browser_cycles(tmp_path, create_args):
    """Check that warm mode forces a single browser cycle."""
    args = create_args(test="synthetic", cold=False, chimera=False, browser_cycles=5)

    test_list = _run_test_list(tmp_path, args, [_entry()])

    assert test_list[0]["cold"] is False
    assert test_list[0]["browser_cycles"] == 1
    assert test_list[0]["expected_browser_cycles"] == 1


def test_get_raptor_test_list_live_sites_youtube_and_hero(tmp_path, create_args):
    """Check that live sites rename youtube tests, scale the timeout and drop hero."""
    args = create_args(
        test="raptor-youtube-playback-h264",
        live_sites=True,
        cold=False,
        browser_cycles=None,
    )
    available = [
        _entry(
            name="raptor-youtube-playback-h264",
            measure="fcp, hero",
            hero="hero1",
            page_timeout="1000",
            playback="mitmproxy",
        )
    ]

    test_list = _run_test_list(tmp_path, args, available)

    assert test_list[0]["name"] == "raptor-youtube-playback-h264-live"
    assert test_list[0]["use_live_sites"] is True
    assert test_list[0]["playback"] is None
    assert test_list[0]["page_timeout"] == 1000 * manifest.LIVE_SITE_TIMEOUT_MULTIPLIER
    assert test_list[0]["measure"] == ["fcp"]
    assert "hero" not in test_list[0]
    assert os.path.exists(
        os.path.join(str(tmp_path), "json", "raptor-youtube-playback-h264-live.json")
    )


def test_get_raptor_test_list_playback_pageset_transformed(tmp_path, create_args):
    """Check that the {subtest} placeholder in the playback pageset is replaced."""
    args = create_args(test="synthetic", live_sites=False, browser_cycles=None)
    available = [
        _entry(
            playback="mitmproxy",
            playback_pageset_manifest="mitm-linux-firefox-{subtest}.manifest",
        )
    ]

    test_list = _run_test_list(tmp_path, args, available)

    assert (
        test_list[0]["playback_pageset_manifest"]
        == "mitm-linux-firefox-synthetic.manifest"
    )


def test_get_raptor_test_list_browsertime_manifest_requires_browsertime(
    tmp_path, create_args
):
    """Check that browsertime manifests fail without --browsertime."""
    args = create_args(test="synthetic", browsertime=False, browser_cycles=None)
    available = [_entry(manifest=os.path.join("browsertime", "synthetic.toml"))]

    with pytest.raises(
        Exception, match="^synthetic test can only be run with --browsertime$"
    ):
        _run_test_list(tmp_path, args, available)


def test_get_raptor_test_list_browsertime_default_measure(tmp_path, create_args):
    """Check that browsertime pageload tests without measure get a default list."""
    args = create_args(test="synthetic", browsertime=True, browser_cycles=None)
    details = _entry()
    del details["measure"]

    test_list = _run_test_list(tmp_path, args, [details])

    measure = test_list[0]["measure"]
    assert isinstance(measure, list)
    assert {"fcp", "loadtime", "largestContentfulPaint"} <= set(measure)


def test_get_raptor_test_list_support_class(tmp_path, create_args):
    """Check that a support_class is imported, instantiated and set up."""
    args = create_args(test="synthetic", browser_cycles=None)
    support_instance = MagicMock()
    support_cls = MagicMock(return_value=support_instance)

    with patch(
        "manifest.import_support_class", return_value=support_cls
    ) as mock_import:
        test_list = _run_test_list(tmp_path, args, [_entry(support_class="custom.py")])

    imported_path = mock_import.call_args[0][0]
    assert imported_path.name == "custom.py"
    assert imported_path.parent.name == "support-scripts"
    assert test_list[0]["support_class"] is support_instance
    support_instance.setup_test.assert_called_once_with(test_list[0], args)


def test_get_raptor_test_list_invalid_test_last_removed(tmp_path, create_args):
    """Check that an invalid test at the end of the list is removed."""
    args = create_args(test="suite", browser_cycles=None)
    valid = _entry(name="good", manifest="suite.toml")
    invalid = _entry(name="bad", manifest="suite.toml")
    del invalid["unit"]

    test_list = _run_test_list(tmp_path, args, [valid, invalid])

    assert [t["name"] for t in test_list] == ["good"]
    assert os.listdir(os.path.join(str(tmp_path), "json")) == ["good.json"]


if __name__ == "__main__":
    mozunit.main()
