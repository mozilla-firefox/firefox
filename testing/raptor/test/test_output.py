# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import filters
import output
from output import BrowsertimeOutput, PerftestOutput, RaptorOutput


def make_output(
    cls=RaptorOutput,
    results=None,
    supporting_data=None,
    subtest_alert_on=None,
    app="firefox",
    extra_summary_methods=None,
):
    return cls(
        results if results is not None else [],
        supporting_data,
        subtest_alert_on,
        app,
        extra_summary_methods=extra_summary_methods or [],
    )


def raptor_test(**kwargs):
    test = {
        "name": "raptor-tp6-test",
        "type": "pageload",
        "extra_options": [],
        "lower_is_better": True,
        "unit": "ms",
        "alert_threshold": 2,
        "subtest_unit": "ms",
        "subtest_lower_is_better": True,
        "cold": False,
        "measurements": {},
    }
    test.update(kwargs)
    return test


def bench_test(name, measurements, **kwargs):
    return raptor_test(
        name=name,
        type="benchmark",
        unit="score",
        lower_is_better=False,
        measurements=measurements,
        **kwargs,
    )


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    logger = mock.Mock()
    monkeypatch.setattr(output, "LOG", logger)
    return logger


@pytest.fixture
def linux(monkeypatch):
    monkeypatch.setattr(output.platform, "system", lambda: "Linux")
    monkeypatch.setattr(output.platform, "release", lambda: "6.1.0")


@pytest.fixture
def in_tmp(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    return tmp_path


def test_perftest_output_is_abstract():
    """Check that PerftestOutput cannot be instantiated without summarize."""
    with pytest.raises(TypeError, match="abstract"):
        PerftestOutput([], None, None, "firefox")


def test_init_and_browser_meta(linux):
    """Check that the constructor stores its inputs and set_browser_meta records the browser."""
    out = make_output(results=["r"], supporting_data=["s"], subtest_alert_on=["fcp"])
    assert out.results == ["r"]
    assert out.supporting_data == ["s"]
    assert out.subtest_alert_on == ["fcp"]
    assert out.summarized_results == {}
    assert out.summarized_supporting_data == []
    assert out.summarized_screenshots == []
    assert out.os_name == "Linux"
    assert out.browser_name is None

    out.set_browser_meta("firefox", "130.0")
    assert (out.browser_name, out.browser_version) == ("firefox", "130.0")


@pytest.mark.parametrize(
    "system,expected",
    [("Windows", "10.0.19045"), ("Darwin", "14.5"), ("Linux", "6.1.0")],
)
def test_set_platform_version(monkeypatch, system, expected):
    """Check that _set_platform_version picks the right platform call for each OS."""
    monkeypatch.setattr(
        output.platform, "uname", lambda: mock.Mock(version="10.0.19045")
    )
    monkeypatch.setattr(output.platform, "mac_ver", lambda: ("14.5", "", ""))
    monkeypatch.setattr(output.platform, "release", lambda: "6.1.0")
    out = make_output()
    out.os_name = system
    out._set_platform_version()
    assert out.os_platform_version == expected


def test_summarize_supporting_data_none():
    """Check that summarize_supporting_data does nothing without supporting data."""
    out = make_output()
    assert out.summarize_supporting_data() is None
    assert out.summarized_supporting_data == []


def test_summarize_supporting_data_android():
    """Check that Android supporting data is split by type into perfherder suites."""
    supporting = [
        {
            "type": "power",
            "test": "raptor-speedometer-geckoview",
            "unit": "mAh",
            "values": {
                "cpu": {"values": 10, "lowerIsBetter": False, "unit": "mWh"},
                "wifi": {"values": 5, "alertThreshold": 5.0, "shouldAlert": True},
                "screen": 2,
            },
        },
        {
            "type": "cpu",
            "test": "raptor-speedometer-geckoview",
            "unit": "%",
            "suite-suffix-type": False,
            "summarize-values": False,
            "values": {"total": {"values": 50, "subtest-prefix-type": False}},
        },
    ]
    results = [
        {"name": "other", "extra_options": ["nope"]},
        {"name": "raptor-speedometer-geckoview", "extra_options": ["fission"]},
    ]
    out = make_output(results=results, supporting_data=supporting, app="geckoview")
    out.set_browser_meta("geckoview", "131.0")
    out.summarize_supporting_data()

    assert len(out.summarized_supporting_data) == 2
    power, cpu = out.summarized_supporting_data
    assert power["framework"] == {"name": "raptor"}
    assert power["application"] == {"name": "geckoview", "version": "131.0"}
    assert "os" not in power

    suite = power["suites"][0]
    assert suite["name"] == "raptor-speedometer-geckoview-power"
    assert suite["type"] == "power"
    assert suite["lowerIsBetter"] is True
    assert suite["unit"] == "mAh"
    assert suite["alertThreshold"] == 2.0
    assert suite["extraOptions"] == ["fission"]
    assert suite["value"] == 17
    assert suite["subtests"] == [
        {
            "name": "power-cpu",
            "value": 10,
            "lowerIsBetter": False,
            "alertThreshold": 2.0,
            "unit": "mWh",
        },
        {
            "name": "power-wifi",
            "value": 5,
            "lowerIsBetter": True,
            "alertThreshold": 5.0,
            "unit": "mAh",
            "shouldAlert": True,
        },
        {
            "name": "power-screen",
            "value": 2,
            "lowerIsBetter": True,
            "alertThreshold": 2.0,
            "unit": "mAh",
        },
    ]

    cpu_suite = cpu["suites"][0]
    assert cpu_suite["name"] == "raptor-speedometer-geckoview"
    assert "value" not in cpu_suite
    assert "unit" not in cpu_suite
    assert cpu_suite["subtests"][0]["name"] == "total"


def test_summarize_supporting_data_desktop_without_platform_version(linux):
    """Check that desktop supporting data gets the OS name when no platform version is known."""
    supporting = [
        {"type": "power", "test": "t", "unit": "KB", "values": {"a": 1, "b": 2}}
    ]
    out = make_output(supporting_data=supporting)
    out.set_browser_meta("firefox", None)
    with mock.patch.object(RaptorOutput, "_set_platform_version"):
        out.summarize_supporting_data()
    data = out.summarized_supporting_data[0]
    assert data["application"] == {"name": "firefox"}
    assert data["os"] == {"name": "Linux"}
    assert data["suites"][0]["value"] == 3
    assert "extraOptions" not in data["suites"][0]


def summarized(name="raptor-tp6-test.alias", **suite_kwargs):
    suite = {"name": name, "type": "pageload", "extraOptions": [], "subtests": []}
    suite.update(suite_kwargs)
    return {"framework": {"name": "raptor"}, "suites": [suite]}


def test_output_no_results(in_tmp):
    """Check that output fails and writes nothing when there are no results."""
    out = make_output()
    assert out.output(["raptor-tp6-test"]) == (False, 0)
    assert not (in_tmp / "raptor.json").exists()
    assert not (in_tmp / "screenshots.html").exists()


def test_output_screenshots_only(in_tmp):
    """Check that output still writes screenshots when there are no results."""
    out = make_output()
    out.summarize_screenshots([
        {"test_name": "t", "page_cycle": 1, "screenshot": "data:x"}
    ])
    assert out.output(["t"]) == (False, 0)
    assert "data:x" in (in_tmp / "screenshots.html").read_text()


def test_output_writes_results_to_upload_dir(tmp_path, monkeypatch, linux):
    """Check that output writes raptor.json next to MOZ_UPLOAD_DIR with browser and OS info."""
    upload = tmp_path / "upload"
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(upload))
    out = make_output()
    out.summarized_results = summarized(extraOptions=["gecko-profile"])
    out.set_browser_meta("firefox", "130.0")

    assert out.output(["raptor-tp6-test"]) == (True, 1)

    data = json.loads((tmp_path / "raptor.json").read_text())
    assert data["suites"][0]["shouldAlert"] is False
    assert data["application"] == {"name": "firefox", "version": "130.0"}
    assert data["os"] == {"name": "Linux", "platform_version": "6.1.0"}


def test_output_missing_test_name_fails(in_tmp, mock_log):
    """Check that output reports failure when a suite matches no requested test name."""
    out = make_output(app="geckoview")
    out.summarized_results = summarized(name="raptor-other.alias")
    assert out.output(["raptor-tp6-test"]) == (False, 1)
    mock_log.error.assert_called_once_with(
        "no summarized raptor results found for raptor-other"
    )
    data = json.loads((in_tmp / "raptor.json").read_text())
    assert "os" not in data
    assert "application" not in data
    assert "shouldAlert" not in data["suites"][0]


def test_output_scenario_not_posted(in_tmp):
    """Check that scenario results are saved but not posted to perfherder."""
    out = make_output(app="fenix")
    out.summarized_results = summarized(type="scenario")
    assert out.output(["raptor-tp6-test"]) == (True, 0)
    assert (in_tmp / "raptor.json").exists()


def test_output_with_supporting_data_not_posted(in_tmp):
    """Check that regular results are not posted when supporting data exists."""
    out = make_output(app="fenix")
    out.summarized_results = summarized()
    out.summarized_supporting_data = [{"suites": [{"type": "power"}]}]
    assert out.output(["raptor-tp6-test"]) == (True, 0)


def test_output_supporting_data_empty():
    """Check that output_supporting_data fails when nothing was summarized."""
    assert make_output().output_supporting_data(["t"]) == (False, 0)


def test_output_supporting_data_upload_dir(tmp_path, monkeypatch):
    """Check that each supporting data type is written to its own JSON file."""
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(tmp_path / "upload"))
    out = make_output()
    out.summarized_supporting_data = [
        {"suites": [{"type": "power", "value": 1}]},
        {"suites": [{"type": "cpu", "value": 2}]},
    ]
    assert out.output_supporting_data(["t"]) == (True, 2)
    power = json.loads((tmp_path / "raptor-power.json").read_text())
    assert power["suites"][0]["value"] == 1
    assert (tmp_path / "raptor-cpu.json").exists()


def test_output_supporting_data_empty_upload_dir_uses_cwd(in_tmp, monkeypatch):
    """Check that an empty MOZ_UPLOAD_DIR makes supporting data go to the cwd."""
    monkeypatch.setenv("MOZ_UPLOAD_DIR", "")
    out = make_output()
    out.summarized_supporting_data = [{"suites": [{"type": "power"}]}]
    assert out.output_supporting_data(["t"]) == (True, 1)
    assert (in_tmp / "raptor-power.json").exists()


def named(values, name="x"):
    return [[v, name] for v in values]


@pytest.mark.parametrize(
    "testname,vals,unit,expected",
    [
        ("raptor-v8_7-firefox", named([1, 4]), None, 200),
        ("speedometer3", [[1, "a"], [42, "score"]], None, 42),
        ("speedometer-experimental", [[7, "score"]], None, 7),
        ("raptor-kraken-firefox", named([1, 2, 3]), None, 6),
        ("raptor-sunspider", named([1, 2]), None, 3),
        (
            "unity-webgl",
            [[2, "Geometric Mean"], [4, "Geometric Mean"], [99, "x"]],
            None,
            3,
        ),
        ("raptor-webaudio", [[6, "Geometric Mean"], [99, "x"]], None, 6),
        ("assorted-dom", named([1, 4]), None, 2.0),
        ("wasm-misc", [[10, "__total__"], [99, "x"]], None, 10),
        ("wasm-godot", [[5, "first-interactive"], [99, "x"]], None, 5),
        ("youtube-playback", named([1, 2]), None, 1.5),
        ("twitch-animation", [[1, "run"], [4, "run"], [99, "x"]], None, 2.0),
        ("supporting_data", named([1, 2]), None, 3),
        ("supporting_data", named([1, 3]), "%", 2),
        ("supporting_data", [[1, "cpu-avg"], [2, "gpu-avg"], [9, "max"]], "W", 3),
        ("supporting_data", [[1, "cpu-avg"]], "MHz", 1),
        ("supporting_data", named([1, 2]), "KB", 3),
        ("supporting_data", named([1, 2]), "mAh", 3),
        ("supporting_data", named([1, 2]), "mWh", 3),
        ("generic", named([1, 4]), None, 2.0),
        ("generic", named([1.234]), None, 1.23),
    ],
)
def test_construct_summary(testname, vals, unit, expected):
    """Check that construct_summary applies the scoring formula for each test family."""
    result = make_output().construct_summary(vals, testname, unit=unit)
    assert result == pytest.approx(expected)


def test_construct_summary_speedometer():
    """Check that Speedometer scores use every tenth value of the 160 subtests."""
    vals = named([float(i % 10 + 1) for i in range(160)])
    result = make_output().construct_summary(vals, "raptor-speedometer")
    assert result == pytest.approx(60 * 1000 / 10.0 / 3)


def test_construct_summary_stylebench():
    """Check that StyleBench scores use the per-test totals of the 546 entries."""
    vals = named([2.0] * 546)
    result = make_output().construct_summary(vals, "raptor-stylebench")
    assert result == pytest.approx(60 * 1000 / 2.0 / 3)


@pytest.mark.parametrize(
    "testname,vals,unit,exc,match",
    [
        ("speedometer3", [[1, "a"]], None, Exception, "Unable to find score"),
        ("raptor-speedometer", named([1]), None, Exception, "160 subtests"),
        ("raptor-stylebench", named([1]), None, Exception, "StyleBench requires"),
        ("supporting_data", [[1, "max"]], "W", Exception, "No average"),
        ("supporting_data", named([1]), "furlongs", NotImplementedError, "furlongs"),
    ],
)
def test_construct_summary_errors(testname, vals, unit, exc, match):
    """Check that construct_summary raises on malformed benchmark data or unknown units."""
    with pytest.raises(exc, match=match):
        make_output().construct_summary(vals, testname, unit=unit)


def unknown_test(measurements, **kwargs):
    test = {
        "name": "custom",
        "measurements": measurements,
        "subtest_unit": "ms",
        "alert_threshold": 2,
        "subtest_lower_is_better": True,
    }
    test.update(kwargs)
    return test


def test_parse_unknown_requires_dict():
    """Check that parseUnknown rejects measurements that are not a dictionary."""
    with pytest.raises(Exception, match="Expected a dictionary"):
        make_output().parseUnknown(unknown_test([1, 2]))


def test_parse_unknown_flattens_and_skips_blocklist():
    """Check that parseUnknown flattens nested metrics and skips summary keys."""
    test = unknown_test({
        "custom": [
            {"a": {"b": 1.23456, "c": [2, 4]}, "mean": 9},
            {"a": {"b": 3.001, "c": [6]}, "median": 9},
        ]
    })
    subtests, vals = make_output().parseUnknown(test)
    assert [s["name"] for s in subtests] == ["a/c", "a/b"]
    by_name = {s["name"]: s for s in subtests}
    assert by_name["a/b"]["replicates"] == [1.235, 3.001]
    assert by_name["a/b"]["value"] == pytest.approx(2.118)
    assert by_name["a/c"]["replicates"] == [2, 4, 6]
    assert by_name["a/c"]["value"] == 4
    assert by_name["a/c"]["unit"] == "ms"
    assert by_name["a/c"]["alertThreshold"] == 2.0
    assert by_name["a/c"]["lowerIsBetter"] is True
    assert vals == [[4, "a/c"], [pytest.approx(2.118), "a/b"]]


@pytest.mark.parametrize("method", ["mean", "geomean"])
def test_parse_unknown_custom_data_summary_methods(method):
    """Check that parseUnknown handles pre-flattened custom data with each summary method."""
    test = unknown_test(
        {"x": 2, "y": [4, 4], "geomean": 1},
        custom_data=True,
        submetric_summary_method=method,
    )
    subtests, vals = make_output().parseUnknown(test)
    by_name = {s["name"]: s for s in subtests}
    assert test["measurements"] == {"custom": [{"x": 2, "y": [4, 4], "geomean": 1}]}
    assert by_name["x"]["replicates"] == [2]
    assert by_name["y"]["value"] == 4
    assert set(by_name) == {"x", "y"}


def bench(measurements, name="raptor-bench", **kwargs):
    return unknown_test(measurements, name=name, **kwargs)


def test_parse_speedometer():
    """Check that parseSpeedometerOutput merges replicates per subtest and takes the median."""
    test = bench({"speedometer": [[{"a": [1, 2, 3]}], [{"a": [4], "b": [1.23456]}]]})
    subtests, vals = make_output().parseSpeedometerOutput(test)
    assert [s["name"] for s in subtests] == ["b", "a"]
    assert subtests[1]["replicates"] == [1, 2, 3, 4]
    assert subtests[1]["value"] == 2.5
    assert subtests[1]["shouldAlert"] is True
    assert subtests[0]["replicates"] == [1.235]
    assert vals == [[1.235, "b"], [2.5, "a"]]


def test_parse_ares6():
    """Check that parseAresSixOutput takes the mean of each subtest's replicates."""
    test = bench({
        "ares6": [
            [{"Air_firstIteration": [1, 3], "Basic_steadyState": [10]}],
            [{"Air_firstIteration": [5]}],
        ]
    })
    subtests, vals = make_output().parseAresSixOutput(test)
    assert subtests[0]["name"] == "Air_firstIteration"
    assert subtests[0]["replicates"] == [1.0, 3.0, 5.0]
    assert subtests[0]["value"] == 3.0
    assert vals == [[10.0, "Basic_steadyState"], [3.0, "Air_firstIteration"]]


def motionmark_cycle(**subs):
    return [
        {
            "suite": {
                name: {
                    "frameLength": {"average": v},
                    "complexity": {"bootstrap": {"median": v * 10 if v else v}},
                }
                for name, v in subs.items()
            }
        }
    ]


def test_parse_motionmark_frame_length():
    """Check that parseMotionmarkOutput uses the frameLength average for non-ramp tests."""
    test = bench(
        {"motionmark": [motionmark_cycle(a=1.11111, b=2), motionmark_cycle(a=3)]},
        name="raptor-motionmark-htmlsuite",
    )
    subtests, vals = make_output().parseMotionmarkOutput(test)
    assert [s["name"] for s in subtests] == ["b", "a"]
    assert subtests[1]["replicates"] == [1.111, 3.0]
    assert vals == [[2.0, "b"], [pytest.approx(2.0555), "a"]]


def test_parse_motionmark_ramp_uses_complexity():
    """Check that parseMotionmarkOutput uses the bootstrap median for ramp tests."""
    test = bench({"motionmark": [motionmark_cycle(a=2)]}, name="raptor-motionmark-ramp")
    subtests, _ = make_output().parseMotionmarkOutput(test)
    assert subtests[0]["replicates"] == [20.0]


def test_parse_youtube_playback(mock_log):
    """Check that the YouTube parser emits decoded, dropped and percent-dropped subtests."""
    test = bench({
        "youtube-playback-h264": [
            [
                {
                    "PlaybackPerf.H264.1080p30@1X": {
                        "droppedFrames": 2,
                        "decodedFrames": 100,
                    },
                    "H264.720p30@1X": {"droppedFrames": 0, "decodedFrames": 0},
                }
            ],
            [
                {
                    "PlaybackPerf.H264.1080p30@1X": {
                        "droppedFrames": 4,
                        "decodedFrames": 100,
                    }
                }
            ],
        ]
    })
    out = make_output(subtest_alert_on=["H264.1080p30@1X_dropped_frames"])
    subtests, vals = out.parseYoutubePlaybackPerformanceOutput(test)
    by_name = {s["name"]: s for s in subtests}

    dropped = by_name["H264.1080p30@1X_dropped_frames"]
    assert dropped["replicates"] == [2, 4]
    assert dropped["value"] == 3.0
    assert dropped["shouldAlert"] is True
    assert by_name["H264.1080p30@1X_decoded_frames"]["lowerIsBetter"] is False
    assert "shouldAlert" not in by_name["H264.1080p30@1X_decoded_frames"]
    assert by_name["H264.1080p30@1X_%_dropped_frames"]["value"] == 3.0
    assert by_name["H264.720p30@1X_%_dropped_frames"]["value"] == 100.0
    assert any(
        "H264.720p30@1X test Failed" in c.args[0]
        for c in mock_log.warning.call_args_list
    )
    assert sorted(vals) == [
        [0.0, "H264.720p30@1X_dropped_frames"],
        [3.0, "H264.1080p30@1X_dropped_frames"],
    ]


def test_parse_youtube_playback_no_measurements():
    """Check that the YouTube parser raises when no playback measurements exist."""
    with pytest.raises(Exception, match="No measurements found"):
        make_output().parseYoutubePlaybackPerformanceOutput(bench({"other": []}))


def test_parse_unity_webgl():
    """Check that parseUnityWebGLOutput decodes JSON cycles into median subtests."""
    cycle = json.dumps([
        {"benchmark": "Mandelbrot GPU", "result": 10},
        {"benchmark": "Instantiate", "result": 2},
    ])
    test = bench({"unity-webgl": [[cycle], [cycle]]})
    subtests, vals = make_output().parseUnityWebGLOutput(test)
    assert [s["name"] for s in subtests] == ["Mandelbrot GPU", "Instantiate"]
    assert subtests[0]["replicates"] == [10, 10]
    assert vals == [[10, "Mandelbrot GPU"], [2, "Instantiate"]]


def test_parse_webaudio():
    """Check that parseWebaudioOutput turns durations into median subtests."""
    cycle = json.dumps([{"name": "Empty", "duration": 26.12345}])
    test = bench({
        "webaudio": [[cycle], [json.dumps([{"name": "Empty", "duration": 30}])]]
    })
    subtests, vals = make_output().parseWebaudioOutput(test)
    assert subtests[0]["replicates"] == [26.123, 30.0]
    assert vals == [[pytest.approx(28.0615), "Empty"]]


@pytest.mark.parametrize(
    "key,method",
    [("wasm-godot", "parseWASMGodotOutput"), ("wasm-misc", "parseWASMMiscOutput")],
)
def test_parse_wasm(key, method):
    """Check that the wasm parsers collect times per named subtest."""
    test = bench({
        key: [
            [[{"name": "validate", "time": 1}, {"name": "__total__", "time": 10}]],
            [[{"name": "validate", "time": 3}]],
        ]
    })
    subtests, vals = getattr(make_output(), method)(test)
    assert [s["name"] for s in subtests] == ["validate", "__total__"]
    assert subtests[0]["replicates"] == [1, 3]
    assert vals == [[2.0, "validate"], [10, "__total__"]]


@pytest.mark.parametrize(
    "key,method",
    [("sunspider", "parseSunspiderOutput"), ("jetstream2", "parseJetstreamTwoOutput")],
)
def test_parse_mean_benchmarks(key, method):
    """Check that the Sunspider and JetStream2 parsers take the mean of replicates."""
    test = bench({key: [[{"a": [1.0001, 2]}], [{"a": [3], "b": [4]}]]})
    subtests, vals = getattr(make_output(), method)(test)
    assert [s["name"] for s in subtests] == ["b", "a"]
    assert subtests[1]["replicates"] == [1.0, 2.0, 3.0]
    assert vals == [[4.0, "b"], [2.0, "a"]]


def test_parse_assorted_dom():
    """Check that parseAssortedDomOutput only reports the total in the summary values."""
    test = bench({
        "assorted-dom": [[{"test": 5.8, "total": 30}], [{"test": 6.0, "total": 31.555}]]
    })
    subtests, vals = make_output().parseAssortedDomOutput(test)
    assert [s["name"] for s in subtests] == ["total", "test"]
    assert subtests[0]["value"] == 30.78
    assert subtests[1]["value"] == 5.9
    assert vals == [[30.78, "total"]]


def test_parse_matrix_react_bench():
    """Check that parseMatrixReactBenchOutput groups values by iteration count."""
    test = bench({"matrix-react-bench": [[[1, 10], [10, 50]], [[1, 20]]]})
    subtests, vals = make_output().parseMatrixReactBenchOutput(test)
    assert [s["name"] for s in subtests] == ["10-iterations", "1-iterations"]
    assert subtests[1]["replicates"] == [10, 20]
    assert vals == [[50.0, "10-iterations"], [15.0, "1-iterations"]]


def test_parse_twitch_animation():
    """Check that parseTwitchAnimationOutput keeps only the run and perfstat metrics."""
    test = bench({
        "twitch-animation": [[10], [20]],
        "perfstat-Compositing": [5, 7],
        "ignored": [1],
    })
    subtests, vals = make_output().parseTwitchAnimationOutput(test)
    assert [s["name"] for s in subtests] == ["run", "perfstat-Compositing"]
    assert subtests[0]["replicates"] == [10, 20]
    assert vals == [[15.0, "run"], [6.0, "perfstat-Compositing"]]


def test_raptor_summarize_no_results():
    """Check that RaptorOutput.summarize leaves results empty when there are none."""
    out = make_output()
    assert out.summarize(["t"]) is None
    assert out.summarized_results == {}


def test_raptor_summarize_warm_pageload():
    """Check that warm pageloads drop the first value, skip empty ttfi and set alert flags."""
    test = raptor_test(
        tags=["live"],
        measurements={
            "fcp": [1000, 10, 20, 30],
            "loadtime": [1000, 5, 7],
            "ttfi": [-1, -1, -1],
        },
    )
    out = make_output(results=[test], subtest_alert_on=["fcp"])
    out.summarize(["raptor-tp6-test"])

    suite = out.summarized_results["suites"][0]
    assert out.summarized_results["framework"] == {"name": "raptor"}
    assert suite["tags"] == ["live", "pageload", "warm"]
    assert "cold" not in suite
    assert "value" not in suite
    assert [s["name"] for s in suite["subtests"]] == ["fcp", "loadtime"]
    fcp, loadtime = suite["subtests"]
    assert fcp["value"] == 20
    assert fcp["replicates"] == [1000, 10, 20, 30]
    assert fcp["shouldAlert"] is True
    assert loadtime["value"] == 6.0
    assert loadtime["shouldAlert"] is False


def test_raptor_summarize_cold_scenario_has_summary():
    """Check that cold scenario suites keep cycle info and get a summary value."""
    test = raptor_test(
        type="scenario",
        cold=True,
        browser_cycle="2",
        expected_browser_cycles="3",
        measurements={"a": [1, 1], "b": [4, 4]},
    )
    out = make_output(results=[test])
    out.summarize(["raptor-tp6-test"])
    suite = out.summarized_results["suites"][0]
    assert suite["cold"] is True
    assert suite["browser_cycle"] == 2
    assert suite["expected_browser_cycles"] == 3
    assert suite["tags"] == ["cold", "scenario"]
    assert suite["value"] == 2.0
    assert all("shouldAlert" not in s for s in suite["subtests"])


def test_raptor_summarize_ttfi_kept_when_valid():
    """Check that ttfi is kept and negative values are ignored when valid values exist."""
    test = raptor_test(
        cold=True,
        browser_cycle=1,
        expected_browser_cycles=1,
        measurements={"ttfi": [-1, 100, 200]},
    )
    out = make_output(results=[test])
    out.summarize(["t"])
    assert out.summarized_results["suites"][0]["subtests"][0]["value"] == 150.0


def test_raptor_summarize_benchmark_real_parser():
    """Check that a Raptor benchmark is parsed and scored end to end."""
    test = bench_test(
        "raptor-sunspider-firefox",
        {"sunspider": [[{"a": [1, 3], "b": [4]}]]},
    )
    out = make_output(results=[test])
    out.summarize(["raptor-sunspider-firefox"])
    suite = out.summarized_results["suites"][0]
    assert suite["tags"] == ["benchmark", "warm"]
    assert suite["value"] == 6.0
    assert [s["name"] for s in suite["subtests"]] == ["a", "b"]


@pytest.mark.parametrize(
    "key,method",
    [
        ("youtube-playback-vp9", "parseYoutubePlaybackPerformanceOutput"),
        ("assorted-dom", "parseAssortedDomOutput"),
        ("ares6", "parseAresSixOutput"),
        ("jetstream2", "parseJetstreamTwoOutput"),
        ("motionmark", "parseMotionmarkOutput"),
        ("speedometer", "parseSpeedometerOutput"),
        ("sunspider", "parseSunspiderOutput"),
        ("unity-webgl", "parseUnityWebGLOutput"),
        ("wasm-godot", "parseWASMGodotOutput"),
        ("wasm-misc", "parseWASMMiscOutput"),
        ("webaudio", "parseWebaudioOutput"),
    ],
)
def test_raptor_summarize_benchmark_dispatch(key, method):
    """Check that RaptorOutput.summarize sends each benchmark to its parser."""
    test = bench_test("raptor-bench", {key: []})
    parsed = ([{"name": "only", "value": 1}], [[1, "only"]])
    out = make_output(results=[test])
    with mock.patch.object(PerftestOutput, method, return_value=parsed) as parser:
        out.summarize(["raptor-bench"])
    parser.assert_called_once_with(test)
    suite = out.summarized_results["suites"][0]
    assert suite["subtests"] == [{"name": "only", "value": 1}]
    assert "value" not in suite


def test_raptor_summarize_unsupported_type():
    """Check that RaptorOutput.summarize bails out on an unsupported test type."""
    out = make_output(results=[raptor_test(type="bogus")])
    assert out.summarize(["t"]) is None
    assert out.summarized_results == {}


def test_raptor_summarize_sorts_suites():
    """Check that RaptorOutput.summarize sorts suites by name."""
    tests = [
        raptor_test(name="b", measurements={"x": [1]}),
        raptor_test(name="a", measurements={"x": [1]}),
    ]
    out = make_output(results=tests)
    out.summarize(["a", "b"])
    assert [s["name"] for s in out.summarized_results["suites"]] == ["a", "b"]


def test_combine_browser_cycles_no_results():
    """Check that combine_browser_cycles does nothing without results."""
    out = make_output()
    assert out.combine_browser_cycles() is None


def cold_suite(name, cycle, subtests, expected=2):
    return {
        "name": name,
        "cold": True,
        "browser_cycle": cycle,
        "expected_browser_cycles": expected,
        "type": "pageload",
        "subtests": [{"name": n, "replicates": list(r)} for n, r in subtests.items()],
    }


def test_combine_browser_cycles():
    """Check that cold browser cycles are merged into one suite per test."""
    out = make_output(results=["placeholder"])
    warm = {"name": "warm", "subtests": []}
    single = cold_suite("single", 1, {"fcp": [5]}, expected=1)
    out.summarized_results = {
        "suites": [
            warm,
            cold_suite("cold", 1, {"fcp": [1], "dcf": [4]}),
            single,
            cold_suite("cold", 2, {"fcp": [3], "ttfi": [9]}),
            cold_suite("solo", 1, {"fcp": [7]}),
        ]
    }
    out.combine_browser_cycles()

    suites = {s["name"]: s for s in out.summarized_results["suites"]}
    assert set(suites) == {"warm", "single", "cold", "solo"}
    assert suites["warm"] is warm
    assert suites["single"] is single

    combined = suites["cold"]
    for key in ("cold", "browser_cycle", "expected_browser_cycles"):
        assert key not in combined
    subs = {s["name"]: s for s in combined["subtests"]}
    assert subs["fcp"]["replicates"] == [1, 3]
    assert subs["fcp"]["value"] == 2.0
    assert subs["dcf"]["value"] == 4
    assert subs["ttfi"]["value"] == 9
    assert combined["value"] == pytest.approx(
        round(filters.geometric_mean([2, 4, 9]), 2)
    )

    assert suites["solo"]["subtests"][0]["value"] == 7
    assert "value" not in suites["solo"]


def test_summarize_screenshots():
    """Check that summarize_screenshots builds an HTML table row per screenshot."""
    out = make_output()
    out.summarize_screenshots([])
    assert out.summarized_screenshots == []

    out.summarize_screenshots([
        {"test_name": "amazon", "page_cycle": 1, "screenshot": "a.png"},
        {"test_name": "amazon", "page_cycle": 2, "screenshot": "b.png"},
    ])
    assert len(out.summarized_screenshots) == 4
    assert out.summarized_screenshots[0].startswith("<!DOCTYPE html>")
    assert 'src="b.png"' in out.summarized_screenshots[2]
    assert 'alt="amazon 2"' in out.summarized_screenshots[2]
    assert out.summarized_screenshots[-1] == "</table></body> </html>"


def bt_test(**kwargs):
    test = raptor_test(name="browsertime-tp6", type="pageload")
    test.update(kwargs)
    return test


def test_browsertime_summarize_no_results():
    """Check that BrowsertimeOutput.summarize leaves results empty when there are none."""
    out = make_output(BrowsertimeOutput)
    assert out.summarize(["t"]) is None
    assert out.summarized_results == {}


def test_browsertime_summarize_pageload():
    """Check that browsertime pageload results merge, get alert windows and extra summaries."""
    tests = [
        bt_test(
            tags=["live"],
            extra_options=["fission"],
            min_back_window="3",
            max_back_window=None,
            fore_window=5,
            alert_change_type="absolute",
            measurements={"fcp": [1, 4], "loadtime": [2], "empty": []},
        ),
        bt_test(
            extra_options=["fission"],
            alert_change_type="relative",
            measurements={"fcp": [16], "cpuTime": [3], "powerUsage": [8]},
        ),
    ]
    out = make_output(
        BrowsertimeOutput,
        results=tests,
        subtest_alert_on=["fcp"],
        extra_summary_methods=["median", "mean"],
    )
    out.summarize(["browsertime-tp6"])

    results = out.summarized_results
    assert results["framework"] == {"name": "browsertime"}
    assert len(results["suites"]) == 1
    suite = results["suites"][0]
    assert suite["tags"] == ["live", "fission"]
    assert suite["minBackWindow"] == 3
    assert suite["foreWindow"] == 5
    assert "maxBackWindow" not in suite
    assert suite["alertChangeType"] == "absolute"
    assert "shouldAlert" not in suite
    assert "value" not in suite

    subs = {s["name"]: s for s in suite["subtests"]}
    assert "empty" not in subs
    assert [s["name"] for s in suite["subtests"]] == sorted(subs)
    fcp = subs["fcp"]
    assert fcp["replicates"] == [1, 4, 16]
    assert fcp["value"] == 4.0
    assert fcp["shouldAlert"] is True
    assert fcp["minBackWindow"] == 3
    assert fcp["orderedTrials"] is True
    assert subs["fcp (median)"]["value"] == 4
    assert "fcp (mean)" in subs
    assert subs["loadtime"]["shouldAlert"] is False
    assert subs["cpuTime"]["unit"] == "ms"
    assert subs["powerUsage"]["unit"] == "uWh"
    assert subs["loadtime"]["unit"] == "ms"


def test_browsertime_summarize_alt_method_failure_is_ignored(monkeypatch):
    """Check that a failing extra summary method is skipped without breaking the suite."""
    monkeypatch.setattr(output.filters, "median", mock.Mock(side_effect=ValueError))
    out = make_output(
        BrowsertimeOutput,
        results=[bt_test(measurements={"fcp": [2, 8]})],
        extra_summary_methods=["median"],
    )
    out.summarize(["t"])
    names = [s["name"] for s in out.summarized_results["suites"][0]["subtests"]]
    assert names == ["fcp"]


def test_browsertime_summarize_chrome_disables_alerts():
    """Check that Chrome results never alert at suite or subtest level."""
    out = make_output(
        BrowsertimeOutput,
        results=[bt_test(type="scenario", measurements={"fcp": [2], "lcp": [8]})],
        subtest_alert_on=["fcp"],
        app="chrome",
    )
    out.summarize(["t"])
    suite = out.summarized_results["suites"][0]
    assert suite["shouldAlert"] is False
    assert all(s["shouldAlert"] is False for s in suite["subtests"])
    assert suite["value"] == 4.0


def test_browsertime_summarize_power():
    """Check that power tests use the mean per subtest and the sum for the suite."""
    out = make_output(
        BrowsertimeOutput,
        results=[bt_test(type="power", measurements={"a": [1, 3], "b": [10]})],
    )
    out.summarize(["t"])
    suite = out.summarized_results["suites"][0]
    subs = {s["name"]: s["value"] for s in suite["subtests"]}
    assert subs == {"a": 2.0, "b": 10.0}
    assert suite["value"] == 12.0


def test_browsertime_summarize_conflicting_extra_options():
    """Check that same-named tests with different extra options get separate suites."""
    tests = [
        bt_test(extra_options=["a"], measurements={"fcp": [1]}),
        bt_test(extra_options=["a", "b"], measurements={"fcp": [2]}),
        bt_test(extra_options=[], measurements={"fcp": [3]}),
        bt_test(extra_options=["b", "a"], measurements={"fcp": [4]}),
    ]
    out = make_output(BrowsertimeOutput, results=tests)
    out.summarize(["t"])
    suites = out.summarized_results["suites"]
    assert len(suites) == 3
    by_options = {tuple(s["extraOptions"]): s for s in suites}
    assert by_options[("a",)]["subtests"][0]["replicates"] == [1]
    assert by_options[("a", "b")]["subtests"][0]["replicates"] == [2, 4]
    assert by_options[()]["subtests"][0]["replicates"] == [3]
    assert all(s["name"] == "browsertime-tp6" for s in suites)


def test_browsertime_summarize_support_class():
    """Check that a support class summarizes the test and suites instead of the default."""
    support = mock.Mock()

    def summarize_test(test, suite):
        suite["subtests"] = [{"name": "custom", "value": 1, "replicates": [1]}]
        suite["replicates"] = [1]

    support.summarize_test.side_effect = summarize_test
    test = bt_test(support_class=support, measurements={"fcp": [1]})
    out = make_output(BrowsertimeOutput, results=[test])
    out.summarize(["t"])

    suites = out.summarized_results["suites"]
    support.summarize_test.assert_called_once()
    support.summarize_suites.assert_called_once_with(suites)
    assert suites[0]["orderedTrials"] is True
    assert suites[0]["subtests"] == [
        {"name": "custom", "value": 1, "replicates": [1], "orderedTrials": True}
    ]


def test_browsertime_summarize_benchmark_extras():
    """Check that benchmarks get cpuTime and powerUsage subtests and a score."""
    test = bt_test(
        name="browsertime-sunspider",
        type="benchmark",
        gather_cpuTime=True,
        measurements={
            "sunspider": [[{"a": [1, 3], "b": [4]}]],
            "cpuTime": [4, 9],
            "powerUsage": [1, 2],
        },
    )
    out = make_output(BrowsertimeOutput, results=[test])
    out.summarize(["t"])
    suite = out.summarized_results["suites"][0]
    subs = {s["name"]: s for s in suite["subtests"]}
    assert subs["cpuTime"]["value"] == 6.0
    assert subs["cpuTime"]["unit"] == "ms"
    assert subs["powerUsage"]["value"] == 1.5
    assert subs["a"]["value"] == 2.0
    assert suite["value"] == 6.0
    assert all(s["orderedTrials"] for s in suite["subtests"])


def test_browsertime_summarize_benchmark_unknown_parser():
    """Check that unknown browsertime benchmarks fall back to parseUnknown."""
    test = bt_test(
        name="browsertime-custom",
        type="benchmark",
        measurements={"custom": [{"x": [1, 3]}]},
    )
    out = make_output(BrowsertimeOutput, results=[test])
    out.summarize(["t"])
    suite = out.summarized_results["suites"][0]
    assert suite["subtests"][0]["name"] == "x"
    assert suite["subtests"][0]["value"] == 2.0
    assert "value" not in suite


def test_browsertime_summarize_benchmark_without_metrics_raises():
    """Check that summarize raises when a benchmark parser finds no metrics."""
    test = bt_test(name="browsertime-custom", type="benchmark", measurements={})
    out = make_output(BrowsertimeOutput, results=[test])
    with mock.patch.object(PerftestOutput, "parseUnknown", return_value=(None, [])):
        with pytest.raises(Exception, match="No benchmark metrics"):
            out.summarize(["t"])


@pytest.mark.parametrize(
    "name,key,method",
    [
        ("bt", "speedometer", "parseSpeedometerOutput"),
        ("bt-ares6", "x", "parseAresSixOutput"),
        ("bt", "motionmark", "parseMotionmarkOutput"),
        ("bt-youtube-playback", "x", "parseYoutubePlaybackPerformanceOutput"),
        ("bt-unity-webgl", "x", "parseUnityWebGLOutput"),
        ("bt", "webaudio", "parseWebaudioOutput"),
        ("bt", "wasm-godot", "parseWASMGodotOutput"),
        ("bt", "wasm-misc", "parseWASMMiscOutput"),
        ("bt", "sunspider", "parseSunspiderOutput"),
        ("bt", "assorted-dom", "parseAssortedDomOutput"),
        ("bt", "jetstream2", "parseJetstreamTwoOutput"),
        ("bt-matrix-react-bench", "x", "parseMatrixReactBenchOutput"),
        ("bt-twitch-animation", "x", "parseTwitchAnimationOutput"),
    ],
)
def test_browsertime_summarize_benchmark_dispatch(name, key, method):
    """Check that BrowsertimeOutput.summarize sends each benchmark to its parser."""
    test = bt_test(name=name, type="benchmark", measurements={key: []})
    parsed = ([{"name": "only", "value": 1, "replicates": [1]}], [[1, "only"]])
    out = make_output(BrowsertimeOutput, results=[test])
    with mock.patch.object(PerftestOutput, method, return_value=parsed) as parser:
        out.summarize(["t"])
    parser.assert_called_once_with(test)
    assert out.summarized_results["suites"][0]["subtests"][0]["name"] == "only"


if __name__ == "__main__":
    mozunit.main()
