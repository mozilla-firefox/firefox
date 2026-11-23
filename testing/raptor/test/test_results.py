# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import sys
import tarfile
from unittest import mock

import mozunit
import pytest

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

import results
from results import (
    BrowsertimeResultsHandler,
    MissingResultsError,
    PerftestResultsHandler,
    RaptorResultsHandler,
)


def make_raw_result(
    url="https://www.example.com/page",
    extras=None,
    timings=None,
    browser=None,
    **extra_fields,
):
    raw = {
        "info": {"browsertime": {"version": "21.0.0"}, "url": url},
        "browserScripts": [
            {
                "timings": {"firstPaint": 100} if timings is None else timings,
            }
        ],
        "extras": [{}] if extras is None else extras,
        "android": {},
    }
    if browser is not None:
        raw["browserScripts"][0]["browser"] = browser
    raw.update(extra_fields)
    return raw


def parse_args(**overrides):
    args = {
        "page_cycles": 1,
        "cold": False,
        "browser_cycles": 1,
        "measure": None,
        "page_count": [],
        "test_name": "test-site",
        "accept_zero_vismet": False,
        "load_existing": False,
        "test_summary": "pageload",
        "subtest_name_filters": "",
        "handle_custom_data": False,
        "support_class": None,
        "submetric_summary_method": None,
    }
    args.update(overrides)
    return args


def make_test(**overrides):
    test = {
        "name": "test-site",
        "type": "pageload",
        "page_cycles": 1,
        "cold": False,
        "browser_cycles": 1,
        "unit": "ms",
        "lower_is_better": True,
        "alert_threshold": 2.0,
    }
    test.update(overrides)
    return test


@pytest.fixture(autouse=True)
def mock_log(monkeypatch):
    fake = mock.MagicMock()
    monkeypatch.setattr(results, "LOG", fake)
    return fake


@pytest.fixture
def bt_handler(tmp_path):
    def inner(**config):
        config.setdefault("app", "firefox")
        return BrowsertimeResultsHandler(
            config, root_results_dir=str(tmp_path / "browsertime-results")
        )

    return inner


@pytest.fixture
def mock_bt_output(monkeypatch):
    output_cls = mock.MagicMock()
    output = output_cls.return_value
    output.output.return_value = (True, 1)
    output.summarized_results = {"suites": [{"extraOptions": ["fission"]}]}
    monkeypatch.setattr(results, "BrowsertimeOutput", output_cls)
    return output_cls


def write_bt_json(handler, test, data):
    test_dir = handler.result_dir_for_test(test)
    os.makedirs(test_dir, exist_ok=True)
    path = os.path.join(test_dir, "browsertime.json")
    with open(path, "w") as f:
        json.dump(data, f)
    return path


def test_base_class_is_abstract():
    """Check that PerftestResultsHandler cannot be instantiated directly."""
    with pytest.raises(TypeError, match="abstract"):
        PerftestResultsHandler(app="firefox")


@pytest.mark.parametrize(
    "config,expected",
    [
        ({}, ["fission", "webrender"]),
        ({"fission": False}, ["webrender"]),
        ({"conditioned_profile": "settled"}, ["fission", "webrender"]),
        (
            {"conditioned_profile": "artifact:settled-youtube"},
            ["condprof-settled-youtube", "fission", "webrender"],
        ),
        (
            {
                "live_sites": True,
                "gecko_profile": True,
                "cold": True,
                "test_bytecode_cache": True,
                "simpleperf": True,
                "etw_profile": True,
                "samply_profile": True,
                "perf_profile": True,
            },
            [
                "fission",
                "live",
                "gecko-profile",
                "cold",
                "bytecode-cached",
                "simpleperf",
                "etw-profile",
                "samply-profile",
                "perf-profile",
                "webrender",
            ],
        ),
        ({"app": "chrome", "cold": True}, ["cold"]),
        ({"app": "Chrome-M", "test_bytecode_cache": True}, []),
    ],
)
def test_build_extra_options_defaults(config, expected):
    """Check that build_extra_options maps handler flags to the expected options."""
    config.setdefault("app", "firefox")
    handler = RaptorResultsHandler(**config)
    assert handler.build_extra_options() == expected


def test_build_extra_options_with_modifiers():
    """Check that only enabled explicit modifiers become extra options."""
    handler = RaptorResultsHandler(app="firefox")
    modifiers = [(True, "fission"), (False, "cold"), (True, "live")]
    assert handler.build_extra_options(modifiers) == ["fission", "live"]


def test_build_extra_options_with_modifiers_non_firefox():
    """Check that Firefox-only modifiers are dropped for non-Firefox browsers."""
    handler = RaptorResultsHandler(app="safari")
    modifiers = [(True, "fission"), (True, "webrender"), (True, "cold")]
    assert handler.build_extra_options(modifiers) == ["cold"]


def test_build_extra_options_unknown_modifier():
    """Check that an unknown modifier name raises an error."""
    handler = RaptorResultsHandler(app="firefox")
    with pytest.raises(Exception, match="Unknown test modifier bogus"):
        handler.build_extra_options([(True, "bogus")])


def test_metadata_images_timeouts_and_supporting_data():
    """Check that browser meta, images, timeouts and supporting data are recorded."""
    handler = RaptorResultsHandler(app="firefox")
    handler.add_browser_meta("firefox", "120.0")
    assert (handler.browser_name, handler.browser_version) == ("firefox", "120.0")

    handler.add_image("data:image/png", "test-site", 2)
    assert handler.images == [
        {"screenshot": "data:image/png", "test_name": "test-site", "page_cycle": 2}
    ]

    handler.add_page_timeout("test-site", "http://a", 1, None)
    handler.add_page_timeout(
        "test-site", "http://b", 3, {"fcp": True, "loadtime": False, "dcf": True}
    )
    assert handler.page_timeout_list == [
        {"test_name": "test-site", "url": "http://a", "page_cycle": 1},
        {
            "test_name": "test-site",
            "url": "http://b",
            "page_cycle": 3,
            "pending_metrics": "fcp, dcf",
        },
    ]

    assert handler.supporting_data is None
    handler.add_supporting_data({"type": "power", "values": {}})
    handler.add_supporting_data({"type": "cpu", "values": {}})
    assert [d["type"] for d in handler.supporting_data] == ["power", "cpu"]

    assert handler.result_dir() is None
    handler.use_existing_results("/some/dir")
    assert handler.existing_results == "/some/dir"


@pytest.mark.parametrize(
    "summarized,supporting,expected",
    [
        ({}, None, 1),
        ({"suites": [{"type": "pageload"}]}, None, 1),
        ({"suites": [{"type": "scenario"}]}, None, None),
        (
            {},
            [{"suites": [{"type": "power"}]}, {"suites": [{"type": "scenario"}]}],
            None,
        ),
    ],
)
def test_get_expected_perfherder(summarized, supporting, expected):
    """Check that scenario results disable the expected PERFHERDER_DATA count."""
    output = mock.Mock(
        summarized_results=summarized, summarized_supporting_data=supporting
    )
    handler = RaptorResultsHandler(app="firefox")
    assert handler._get_expected_perfherder(output) == expected


@pytest.fixture
def schema_path(tmp_path, monkeypatch):
    path = tmp_path / "schema.json"
    path.write_text(
        json.dumps({
            "type": "object",
            "properties": {
                "suites": {"type": "array"},
                "framework": {"type": "object"},
            },
            "required": ["suites"],
        })
    )
    monkeypatch.setenv("PERFHERDER_SCHEMA_PATH", str(path))
    return path


def test_validate_treeherder_data_skips_scenario():
    """Check that validation passes without a schema for scenario results."""
    output = mock.Mock(
        summarized_results={"suites": [{"type": "scenario"}]},
        summarized_supporting_data=None,
    )
    handler = RaptorResultsHandler(app="firefox")
    assert handler._validate_treeherder_data(output, 0) is True


def test_validate_treeherder_data_wrong_count(mock_log):
    """Check that validation fails when PERFHERDER_DATA was output the wrong number of times."""
    output = mock.Mock(summarized_results={}, summarized_supporting_data=None)
    handler = RaptorResultsHandler(app="firefox")
    assert handler._validate_treeherder_data(output, 2) is False
    mock_log.critical.assert_called_once_with(
        "PERFHERDER_DATA was seen 2 times, expected 1."
    )


def test_validate_treeherder_data_valid(schema_path):
    """Check that summarized results matching the schema validate."""
    output = mock.Mock(
        summarized_results={"suites": [{"type": "pageload"}]},
        summarized_supporting_data=None,
    )
    handler = RaptorResultsHandler(app="firefox")
    assert handler._validate_treeherder_data(output, 1) is True


def test_validate_treeherder_data_uses_supporting_data(schema_path):
    """Check that supporting data is validated when there are no summarized results."""
    output = mock.Mock(
        summarized_results={},
        summarized_supporting_data=[{"suites": [{"type": "power"}]}],
    )
    handler = RaptorResultsHandler(app="firefox")
    assert handler._validate_treeherder_data(output, 1) is True


def test_validate_treeherder_data_invalid(schema_path):
    """Check that results violating the schema fail validation."""
    output = mock.Mock(
        summarized_results={"suites": [{"type": "pageload"}], "framework": "x"},
        summarized_supporting_data=None,
    )
    handler = RaptorResultsHandler(app="firefox")
    assert handler._validate_treeherder_data(output, 1) is False


def test_raptor_add():
    """Check that RaptorResultsHandler.add tags results with condprof, fission and live."""
    handler = RaptorResultsHandler(
        app="firefox", conditioned_profile="settled", live_sites=True
    )
    result = {"extra_options": ["existing"]}
    handler.add(result)
    assert result["extra_options"] == [
        "existing",
        "condprof-settled",
        "fission",
        "live",
    ]
    assert result["tags"] == ["live"]
    assert handler.results == [result]

    handler = RaptorResultsHandler(app="chrome")
    result = {}
    handler.add(result)
    assert result == {"extra_options": []}


@pytest.mark.parametrize(
    "supporting,results_list,timeouts,gecko_profile,expected",
    [
        (None, [{}], [], False, True),
        ([{"type": "power"}], [{}], [], False, True),
        ([{"type": "power"}], [], [], False, True),
        (None, [{}], [{"url": "x"}], False, False),
        (None, [{}], [], True, True),
    ],
)
def test_raptor_summarize_and_output(
    monkeypatch, supporting, results_list, timeouts, gecko_profile, expected
):
    """Check that Raptor summarizing drives RaptorOutput and reports overall success."""
    output_cls = mock.MagicMock()
    output = output_cls.return_value
    output.output.return_value = (True, 1)
    output.output_supporting_data.return_value = (True, 0)
    monkeypatch.setattr(results, "RaptorOutput", output_cls)

    handler = RaptorResultsHandler(app="firefox", gecko_profile=gecko_profile)
    handler.results = results_list
    handler.supporting_data = supporting
    handler.page_timeout_list = timeouts
    handler.add_browser_meta("firefox", "120")
    validate = mock.Mock(return_value=True)
    monkeypatch.setattr(handler, "_validate_treeherder_data", validate)

    assert (
        handler.summarize_and_output({"subtest_alert_on": ["fcp"]}, [], ["t"])
        is expected
    )
    output_cls.assert_called_once_with(results_list, supporting, ["fcp"], "firefox")
    output.set_browser_meta.assert_called_once_with("firefox", "120")
    output.summarize.assert_called_once_with(["t"])
    output.combine_browser_cycles.assert_called_once()
    output.summarize_screenshots.assert_called_once_with([])
    has_sup = supporting is not None and len(results_list) != 0
    assert output.summarize_supporting_data.called is has_sup
    if gecko_profile:
        validate.assert_not_called()
    else:
        validate.assert_called_once_with(output, 1)


def test_raptor_summarize_and_output_failures(monkeypatch):
    """Check that a supporting-data output failure makes summarizing fail."""
    output_cls = mock.MagicMock()
    output = output_cls.return_value
    output.output.return_value = (True, 1)
    output.output_supporting_data.return_value = (False, 1)
    monkeypatch.setattr(results, "RaptorOutput", output_cls)

    handler = RaptorResultsHandler(app="firefox")
    handler.results = [{}]
    handler.supporting_data = [{"type": "power"}]
    validate = mock.Mock(return_value=True)
    monkeypatch.setattr(handler, "_validate_treeherder_data", validate)
    assert handler.summarize_and_output({}, [], ["t"]) is False
    validate.assert_called_once_with(output, 2)


def test_browsertime_handler_init_creates_root(tmp_path, bt_handler):
    """Check that the browsertime handler creates its root results directory."""
    handler = bt_handler()
    assert os.path.isdir(handler.result_dir())
    assert handler.result_dir() == str(tmp_path / "browsertime-results")
    assert handler.failed_vismets == []
    assert handler.add({"anything": 1}) is None
    BrowsertimeResultsHandler({"app": "firefox"}, root_results_dir=handler.result_dir())


def test_result_dirs_for_test(tmp_path, bt_handler):
    """Check that per-test result and profiling directories are built and removed."""
    handler = bt_handler()
    test = {"name": "amazon"}
    assert handler.result_dir_for_test(test) == os.path.join(
        handler.result_dir(), "amazon"
    )

    os.makedirs(handler.result_dir_for_test(test))
    profiling = handler.result_dir_for_test_profiling(test)
    assert profiling == os.path.join(handler.result_dir(), "amazon", "profiling")
    assert os.path.isdir(profiling)
    assert handler.result_dir_for_test_profiling(test) == profiling

    removed = handler.remove_result_dir_for_test(test)
    assert removed == os.path.join(handler.result_dir(), "amazon")
    assert not os.path.exists(removed)
    assert handler.remove_result_dir_for_test(test) == removed

    handler.use_existing_results(str(tmp_path / "old"))
    assert handler.result_dir_for_test(test) == os.path.join(
        str(tmp_path / "old"), "amazon"
    )


def test_build_tags(bt_handler):
    """Check that interactive tests get the interactive tag."""
    handler = bt_handler()
    assert handler.build_tags() == []
    assert handler.build_tags(test={"interactive": True}) == ["interactive"]
    assert handler.build_tags(test={"interactive": False}) == []


def test_extract_vmetrics(bt_handler):
    """Check that visual metrics jobs point at a posix path under browsertime-results."""
    handler = bt_handler()
    bt_json = os.path.join("root", "browsertime-results", "amazon", "browsertime.json")
    assert handler._extract_vmetrics(
        "amazon",
        bt_json,
        json_name="cold-browsertime.json",
        tags=["interactive"],
        extra_options=["cold"],
        accept_zero_vismet=True,
    ) == {
        "browsertime_json_path": "browsertime-results/amazon/cold-browsertime.json",
        "test_name": "amazon",
        "tags": ["interactive"],
        "extra_options": ["cold"],
        "accept_zero_vismet": True,
    }


@pytest.mark.parametrize(
    "rel,profiling_job,has_video,expected",
    [
        ("a/browsertime.json", True, False, ["browsertime-profiler"]),
        ("a/profiling/browsertime.json", False, False, ["browsertime-profiler"]),
        ("a/video/1-original.mp4", False, True, ["browsertime-videos-original"]),
        ("a/video/1.mp4", False, True, ["browsertime-videos-annotated"]),
        ("a/browsertime.json", False, False, ["browsertime-results"]),
        (
            "a/browsertime.json",
            False,
            True,
            [
                "browsertime-results",
                "browsertime-videos-annotated",
                "browsertime-videos-original",
            ],
        ),
        ("a/screenshot.png", False, True, ["browsertime-results-extra"]),
    ],
)
def test_determine_target_subfolders(
    tmp_path, bt_handler, rel, profiling_job, has_video, expected
):
    """Check that each artifact type is routed to the right archive folders."""
    handler = bt_handler()
    path = tmp_path.joinpath(*rel.split("/"))
    assert (
        handler._determine_target_subfolders(path, profiling_job, has_video) == expected
    )


def _make_artifacts(handler):
    root = handler.result_dir()
    files = {
        "amazon/browsertime.json": "{}",
        "amazon/pages/video/1.mp4": "annotated",
        "amazon/pages/video/1-original.mp4": "original",
        "amazon/pages/screenshot.png": "png",
        "amazon/profiling/profile.json": "{}",
    }
    for rel, content in files.items():
        path = os.path.join(root, *rel.split("/"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(content)
    os.makedirs(os.path.join(root, "amazon", "InteractiveRunner.html"))


def _tar_names(path):
    with tarfile.open(str(path)) as tar:
        return sorted(m.name for m in tar.getmembers() if m.isfile())


def test_archive_raptor_artifacts(tmp_path, bt_handler):
    """Check that artifacts are split into per-type archives and folders cleaned up."""
    handler = bt_handler()
    _make_artifacts(handler)
    (tmp_path / "temp-browsertime-results").mkdir()
    (tmp_path / "browsertime-results.tgz").write_text("stale")

    handler.archive_raptor_artifacts(is_profiling_job=False)

    assert sorted(p.name for p in tmp_path.iterdir()) == [
        "browsertime-profiler.tgz",
        "browsertime-results-extra.tgz",
        "browsertime-results.tgz",
        "browsertime-videos-annotated.tgz",
        "browsertime-videos-original.tgz",
    ]
    assert _tar_names(tmp_path / "browsertime-results.tgz") == [
        "browsertime-results/amazon/browsertime.json"
    ]
    assert _tar_names(tmp_path / "browsertime-videos-annotated.tgz") == [
        "browsertime-videos-annotated/amazon/browsertime.json",
        "browsertime-videos-annotated/amazon/pages/video/1.mp4",
    ]
    assert _tar_names(tmp_path / "browsertime-videos-original.tgz") == [
        "browsertime-videos-original/amazon/browsertime.json",
        "browsertime-videos-original/amazon/pages/video/1-original.mp4",
    ]
    assert _tar_names(tmp_path / "browsertime-results-extra.tgz") == [
        "browsertime-results-extra/amazon/pages/screenshot.png"
    ]
    assert _tar_names(tmp_path / "browsertime-profiler.tgz") == [
        "browsertime-profiler/amazon/profiling/profile.json"
    ]


def test_archive_raptor_artifacts_profiling_job(tmp_path, bt_handler):
    """Check that a profiling job archives every artifact into the profiler archive."""
    handler = bt_handler()
    _make_artifacts(handler)

    handler.archive_raptor_artifacts(is_profiling_job=True)

    assert [p.name for p in tmp_path.iterdir()] == ["browsertime-profiler.tgz"]
    assert len(_tar_names(tmp_path / "browsertime-profiler.tgz")) == 5


@pytest.mark.parametrize(
    "rel,expected",
    [
        ("pages/example_com/data/video/1.mp4", "pages/example_com/warm/data"),
        (
            "pages/example_com/query-abc/data/video/1.mp4",
            "pages/example_com/warm/query-abc/data",
        ),
    ],
)
def test_label_video_folder(tmp_path, bt_handler, rel, expected):
    """Check that video data folders are moved under a cold or warm label."""
    handler = bt_handler()
    parts = rel.split("/")
    video = tmp_path.joinpath(*parts)
    video.parent.mkdir(parents=True)
    video.write_text("v")
    result_data = {"files": {"video": [os.path.join(*parts)]}}

    handler._label_video_folder(result_data, str(tmp_path))

    new_parts = expected.split("/") + ["video", "1.mp4"]
    assert result_data["files"]["video"] == [os.path.join(*new_parts)]
    assert tmp_path.joinpath(*new_parts).read_text() == "v"
    assert not video.exists()


def test_label_video_folder_missing_source(tmp_path, bt_handler):
    """Check that file paths are relabelled even when the source folder is missing."""
    handler = bt_handler()
    result_data = {"files": {"video": [os.path.join("p", "data", "1.mp4")]}}
    base = tmp_path / "base"
    base.mkdir()
    handler._label_video_folder(result_data, str(base), kind="cold")
    assert result_data["files"]["video"] == [os.path.join("p", "cold", "data", "1.mp4")]
    assert list(base.iterdir()) == []


@pytest.mark.parametrize(
    "raw,overrides,message",
    [
        ([], {"cold": True}, "Missing results for all cold"),
        ([], {"handle_custom_data": True}, "Missing results for all cold"),
        ([make_raw_result()], {"page_cycles": 2}, "at least 1 warm page-cycle"),
        (
            [dict(make_raw_result(), browserScripts=[])],
            {},
            "produced no measurements",
        ),
        ([make_raw_result(timings=None)], {}, None),
    ],
)
def test_parse_browsertime_json_missing_results(bt_handler, raw, overrides, message):
    """Check that missing or empty browsertime cycles raise MissingResultsError."""
    handler = bt_handler()
    if message is None:
        raw[0]["browserScripts"][0]["timings"] = None
        message = "missing all timings"
    with pytest.raises(MissingResultsError, match=message):
        handler.parse_browsertime_json(raw, **parse_args(**overrides))


def test_parse_browsertime_json_requires_support_class_or_extras(bt_handler):
    """Check that results without extras or a support class are rejected."""
    handler = bt_handler()
    with pytest.raises(Exception, match="browsertime_pageload.py"):
        handler.parse_browsertime_json([make_raw_result()], **parse_args())


def test_parse_browsertime_json_load_existing_ignores_cycle_count(bt_handler):
    """Check that loading existing results skips the page-cycle count check."""
    handler = bt_handler()
    raw = [make_raw_result(extras=[{"timings": {"fcp": 10}}])] * 3
    parsed = handler.parse_browsertime_json(
        raw, **parse_args(page_cycles=1, load_existing=True)
    )
    assert len(parsed) == 3


def test_parse_browsertime_json_extras(bt_handler):
    """Check that extras, perfstats, cpu and power values are parsed per cycle."""
    handler = bt_handler(perfstats=True)
    raw = [
        make_raw_result(
            url=f"https://www.example.com/page{i}",
            extras=[{"metrics": {"fcp": 10 + i, "lcp": 20 + i}}],
            browser={"userAgent": "UA"},
            geckoPerfStats=[{"Compositing": 5, "MajorGC": 7}, {"Compositing": 6}],
            cpu=[1.5, 2.5],
            android={"power": [{"powerUsage": 1234567}, {"powerUsage": 2000000}]},
        )
        for i in range(2)
    ]
    parsed = handler.parse_browsertime_json(
        raw,
        **parse_args(
            page_cycles=2,
            page_count=[1, 2],
            submetric_summary_method="geomean",
        ),
        gather_cpuTime=True,
    )
    assert parsed == [
        {
            "bt_ver": "21.0.0",
            "browser": {"userAgent": "UA"},
            "url": (f"https://www.example.com.{i + 1}/page{i},",),
            "name": f"test-site.{i + 1}",
            "measurements": {
                "fcp": [10 + i],
                "lcp": [20 + i],
                "perfstat-Compositing": [5, 6],
                "perfstat-MajorGC": [7],
                "cpuTime": [1.5, 2.5],
                "powerUsage": [1.23, 2.0],
            },
            "statistics": {},
            "submetric_summary_method": "geomean",
        }
        for i in range(2)
    ]


def test_parse_browsertime_json_no_cpu_for_chrome(bt_handler):
    """Check that cpu time is not gathered for Chrome."""
    handler = bt_handler(app="chrome")
    raw = [make_raw_result(extras=[{"m": {"fcp": 1}}], cpu=[3.0])]
    parsed = handler.parse_browsertime_json(raw, **parse_args(), gather_cpuTime=True)
    assert parsed[0]["measurements"] == {"fcp": [1]}
    assert parsed[0]["browser"] is None
    assert "submetric_summary_method" not in parsed[0]


def test_parse_browsertime_json_custom_data(bt_handler):
    """Check that custom data keeps numeric values and applies name filters."""
    handler = bt_handler()
    raw = [
        make_raw_result(
            extras=[
                {
                    "benchmark": {
                        "prefix-score": 10,
                        "prefix-list": [1, 2.5],
                        "label": "text",
                        "nested": {"a": 1},
                    }
                },
                {"benchmark": {"prefix-score": 11}},
            ]
        )
    ]
    parsed = handler.parse_browsertime_json(
        raw,
        **parse_args(handle_custom_data=True, subtest_name_filters="prefix-,unused"),
    )
    assert parsed[0]["custom_data"] is True
    assert parsed[0]["measurements"] == {
        "score": [10, 11],
        "list": [1, 2.5],
    }


def test_parse_browsertime_json_custom_data_flatten(bt_handler):
    """Check that flatten summaries turn nested custom data into path names."""
    handler = bt_handler()
    raw = [
        make_raw_result(
            extras=[{"benchmark": {"suite": {"sub1": [1, 2], "sub2": 3}, "x": 0}}]
        )
    ]
    parsed = handler.parse_browsertime_json(
        raw, **parse_args(handle_custom_data=True, test_summary="flatten")
    )
    assert parsed[0]["measurements"] == {
        "suite/sub1": [1, 2],
        "suite/sub2": [3],
        "x": [0],
    }


def test_parse_browsertime_json_support_class(bt_handler):
    """Check that a support class handles each result and sees the last one."""
    handler = bt_handler()
    support = mock.Mock()

    def handle_result(bt_result, raw_result, conversion=None, last_result=False):
        bt_result["measurements"]["fcp"] = [raw_result["info"]["url"]]
        bt_result["last"] = last_result

    support.handle_result.side_effect = handle_result
    raw = [
        make_raw_result(url="https://a.com/x", extras=[]),
        make_raw_result(url="https://b.com/y", extras=[]),
    ]
    parsed = handler.parse_browsertime_json(
        raw, **parse_args(cold=True, support_class=support)
    )
    assert [(p["measurements"]["fcp"], p["last"]) for p in parsed] == [
        (["https://a.com/x"], False),
        (["https://b.com/y"], True),
    ]
    assert all(p["custom_data"] for p in parsed)
    assert support.save_data.call_count == 2
    conversion = support.handle_result.call_args.kwargs["conversion"]
    assert ("loadtime", "loadEventEnd") in conversion


def test_summarize_and_output_missing_results(bt_handler, mock_bt_output):
    """Check that summarizing fails when browsertime.json is missing."""
    handler = bt_handler()
    assert handler.summarize_and_output({}, [make_test()], ["test-site"]) is False
    mock_bt_output.assert_not_called()


def test_summarize_and_output_invalid_json(bt_handler, mock_bt_output):
    """Check that an unreadable browsertime.json is re-raised."""
    handler = bt_handler()
    test = make_test()
    path = write_bt_json(handler, test, [])
    with open(path, "w") as f:
        f.write("{not json")
    with pytest.raises(json.JSONDecodeError):
        handler.summarize_and_output({}, [test], ["test-site"])


def test_summarize_and_output_pageload(bt_handler, mock_bt_output, monkeypatch):
    """Check that a pageload result is completed and a visual metrics job written."""
    handler = bt_handler(simpleperf=False)
    handler.add_browser_meta("firefox", "120.0")
    validate = mock.Mock(return_value=True)
    monkeypatch.setattr(handler, "_validate_treeherder_data", validate)
    test = make_test(
        interactive=True,
        accept_zero_vismet=True,
        min_back_window=3,
        alert_change_type="absolute",
    )
    write_bt_json(handler, test, [make_raw_result(extras=[{"t": {"fcp": 5}}])])

    assert (
        handler.summarize_and_output(
            {"subtest_alert_on": ["fcp"]}, [test], ["test-site"]
        )
        is True
    )

    assert len(handler.results) == 1
    result = handler.results[0]
    assert result["type"] == "pageload"
    assert result["measurements"] == {"fcp": [5]}
    assert result["unit"] == "ms"
    assert result["alert_threshold"] == 2.0
    assert result["min_back_window"] == 3
    assert result["max_back_window"] is None
    assert result["alert_change_type"] == "absolute"
    assert result["subtest_lower_is_better"] is True
    assert result["subtest_unit"] == "ms"
    assert result["extra_options"] == ["fission", "webrender"]
    assert result["tags"] == ["interactive"]
    assert result["support_class"] is None
    assert result["simpleperf"] is False

    mock_bt_output.assert_called_once_with(
        handler.results, None, ["fcp"], "firefox", []
    )
    output = mock_bt_output.return_value
    output.set_browser_meta.assert_called_once_with("firefox", "120.0")
    validate.assert_called_once_with(output, 1)

    with open(os.path.join(handler.result_dir(), "jobs.json")) as f:
        jobs = json.load(f)
    assert jobs == {
        "jobs": [
            {
                "browsertime_json_path": "browsertime-results/test-site/browsertime.json",
                "test_name": "test-site",
                "tags": ["interactive"],
                "extra_options": ["fission", "webrender"],
                "accept_zero_vismet": True,
            }
        ],
        "application": {"name": "firefox", "version": "120.0"},
        "extra_options": ["fission"],
    }


def test_summarize_and_output_run_local_no_jobs(bt_handler, mock_bt_output):
    """Check that local custom-data runs produce results but no jobs.json."""
    handler = bt_handler(gecko_profile=True)
    test = make_test(custom_data="true", subtest_unit="score")
    write_bt_json(handler, test, [make_raw_result(extras=[{"b": {"s": [1, 2]}}])])

    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is True
    assert not os.path.exists(os.path.join(handler.result_dir(), "jobs.json"))
    result = handler.results[0]
    assert result["type"] == "pageload"
    assert result["subtest_unit"] == "score"
    assert result["custom_data"] is True
    assert result["measurements"] == {"s": [1, 2]}
    assert result["extra_options"] == ["fission", "gecko-profile", "webrender"]


def test_summarize_and_output_existing_results(tmp_path, bt_handler, mock_bt_output):
    """Check that results are read from the existing results directory."""
    handler = bt_handler(gecko_profile=True)
    handler.use_existing_results(str(tmp_path / "old"))
    test = make_test(page_cycles=5)
    write_bt_json(handler, test, [make_raw_result(extras=[{"t": {"fcp": 1}}])])
    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is True
    assert handler.results[0]["measurements"] == {"fcp": [1]}


def test_summarize_and_output_power(bt_handler, mock_bt_output):
    """Check that power results are typed as power and support class failure propagates."""
    handler = bt_handler(gecko_profile=True)
    test = make_test()
    support = mock.Mock()
    support.report_test_success.return_value = False

    def handle_result(bt_result, raw_result, **kwargs):
        bt_result["power_data"] = True
        bt_result["measurements"]["cpu"] = [1.0]

    support.handle_result.side_effect = handle_result
    test["support_class"] = support
    write_bt_json(handler, test, [make_raw_result(extras=[])])

    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is False
    result = handler.results[0]
    assert result["type"] == "power"
    assert result["unit"] == "mAh"
    assert result["subtest_unit"] == "mAh"
    assert result["extra_options"][-1] == "power"
    assert result["support_class"] is support
    support.report_test_success.assert_called_once()


def test_summarize_and_output_benchmark_merges_results(bt_handler, mock_bt_output):
    """Check that benchmark cycles merge into one result, skipping power entries."""
    handler = bt_handler(gecko_profile=True)
    test = make_test(type="benchmark", cold=True, gather_cpuTime=True)
    write_bt_json(
        handler,
        test,
        [
            make_raw_result(extras=[{"b": {"score": 1}}], cpu=[9]),
            make_raw_result(extras=[{"b": {"score": 2}}], cpu=[8]),
        ],
    )
    handler.results.append({"name": "test-site", "power_data": True})

    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is True
    assert len(handler.results) == 2
    result = handler.results[1]
    assert result["type"] == "benchmark"
    assert result["gather_cpuTime"] is True
    assert result["measurements"] == {"score": [1, 2], "cpuTime": [9, 8]}


def test_summarize_and_output_benchmark_simpleperf(bt_handler, mock_bt_output):
    """Check that simpleperf benchmark cycles merge, adding new measurements."""
    handler = bt_handler(gecko_profile=True)
    test = make_test(type="benchmark", cold=True, simpleperf=True)
    write_bt_json(
        handler,
        test,
        [
            make_raw_result(extras=[{"b": {"score": 1}}]),
            make_raw_result(extras=[{"b": {"score": 2, "new": 3}}]),
        ],
    )

    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is True
    assert len(handler.results) == 1
    assert handler.results[0]["measurements"] == {"score": [1, 2], "new": [3]}
    assert handler.results[0]["simpleperf"] is True


def test_summarize_and_output_failed_vismets(bt_handler, mock_bt_output, mock_log):
    """Check that recorded visual metric failures make summarizing fail."""
    handler = bt_handler(gecko_profile=True)
    handler.failed_vismets.append("test-site")
    test = make_test()
    write_bt_json(handler, test, [make_raw_result(extras=[{"t": {"fcp": 1}}])])
    assert handler.summarize_and_output({"run_local": True}, [test], ["t"]) is False
    assert "visual metrics" in mock_log.critical.call_args.args[0]


def _chimera_results():
    return [
        {
            "info": {
                "browsertime": {"version": "21"},
                "url": "https://example.com/?run=1",
            },
            "browserScripts": [{"timings": {}}],
            "extras": [{"t": {"fcp": 1}}],
            "android": {},
            "files": {"video": [os.path.join("pages", "data", "video", "1.mp4")]},
        },
        {
            "info": {
                "browsertime": {"version": "21"},
                "url": "https://example.com/?run=2",
            },
            "browserScripts": [{"timings": {}}],
            "extras": [{"t": {"fcp": 2}}],
            "android": {},
            "files": {"video": [os.path.join("pages", "data", "video", "2.mp4")]},
        },
    ]


def test_summarize_and_output_chimera(bt_handler, mock_bt_output, monkeypatch):
    """Check that chimera runs split into cold and warm files, results and jobs."""
    handler = bt_handler(chimera=True, cold=True)
    monkeypatch.setattr(handler, "_validate_treeherder_data", lambda *a: True)
    test = make_test(page_cycles=2)
    path = write_bt_json(handler, test, _chimera_results())
    test_dir = os.path.dirname(path)
    video_dir = os.path.join(test_dir, "pages", "data", "video")
    os.makedirs(video_dir)
    for name in ("1.mp4", "2.mp4"):
        with open(os.path.join(video_dir, name), "w") as f:
            f.write(name)

    profiling_dir = os.path.join(test_dir, "profiling")
    os.makedirs(profiling_dir)
    with open(os.path.join(profiling_dir, "browsertime.json"), "w") as f:
        f.write("{broken")

    assert (
        handler.summarize_and_output({"extra_profiler_run": True}, [test], ["t"])
        is True
    )

    with open(os.path.join(test_dir, "cold-browsertime.json")) as f:
        cold = json.load(f)
    with open(os.path.join(test_dir, "warm-browsertime.json")) as f:
        warm = json.load(f)
    assert cold[0]["files"]["video"] == [
        os.path.join("pages", "cold", "data", "video", "1.mp4")
    ]
    assert warm[0]["files"]["video"] == [
        os.path.join("pages", "warm", "data", "video", "2.mp4")
    ]
    with open(path) as f:
        rewritten = json.load(f)
    assert rewritten[1]["files"] == warm[0]["files"]

    assert [r["extra_options"] for r in handler.results] == [
        ["fission", "cold", "webrender"],
        ["fission", "webrender", "warm"],
    ]

    with open(os.path.join(handler.result_dir(), "jobs.json")) as f:
        jobs = json.load(f)["jobs"]
    assert [(j["browsertime_json_path"], j["extra_options"]) for j in jobs] == [
        (
            "browsertime-results/test-site/cold-browsertime.json",
            ["fission", "cold", "webrender"],
        ),
        (
            "browsertime-results/test-site/warm-browsertime.json",
            ["fission", "webrender", "warm"],
        ),
    ]


def test_summarize_and_output_chimera_profiler_split(bt_handler, mock_bt_output):
    """Check that chimera runs also split the extra profiler run results."""
    handler = bt_handler(chimera=True, cold=True, gecko_profile=True)
    test = make_test(page_cycles=2)
    path = write_bt_json(handler, test, _chimera_results())
    profiling_dir = os.path.join(os.path.dirname(path), "profiling")
    os.makedirs(profiling_dir)
    with open(os.path.join(profiling_dir, "browsertime.json"), "w") as f:
        json.dump(_chimera_results(), f)

    assert (
        handler.summarize_and_output(
            {"run_local": True, "extra_profiler_run": True}, [test], ["t"]
        )
        is True
    )
    assert sorted(os.listdir(profiling_dir)) == [
        "browsertime.json",
        "cold-browsertime.json",
        "warm-browsertime.json",
    ]


PERFHERDER_DATA = {"framework": {"name": "browsertime"}, "suites": []}
LABEL = "test-windows11-64-24h2-shippable/opt-browsertime-benchmark-firefox-sp3"


@pytest.fixture
def ci_env(monkeypatch):
    monkeypatch.setenv("RAPTOR_TASK_LABEL", LABEL)
    monkeypatch.setenv(
        "GECKO_HEAD_REPOSITORY", "https://hg.mozilla.org/integration/autoland/"
    )


@pytest.fixture
def output():
    return mock.Mock(summarized_results=PERFHERDER_DATA)


@pytest.fixture
def xtreme_regression():
    with mock.patch("results.XtremeRegression") as xtreme_regression:
        xtreme_regression.return_value.detect.return_value = [
            {"suite": "speedometer3", "test": None, "exceeds_variance": True},
            {"suite": "speedometer3", "test": "a", "exceeds_variance": False},
            {"suite": "speedometer3", "test": "b", "exceeds_variance": None},
        ]
        yield xtreme_regression


def test_detect_xtreme_regressions(ci_env, output, xtreme_regression):
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_called_once_with(
        "windows11-64-24h2-shippable", revision_repository="autoland"
    )
    xtreme_regression.return_value.detect.assert_called_once_with(PERFHERDER_DATA)
    assert handler.xtreme_regressions == [
        {"suite": "speedometer3", "test": None, "exceeds_variance": True}
    ]


def test_detect_xtreme_regressions_outside_of_ci(
    monkeypatch, output, xtreme_regression
):
    monkeypatch.delenv("RAPTOR_TASK_LABEL", raising=False)
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_not_called()
    assert handler.xtreme_regressions == []


def test_detect_xtreme_regressions_outside_of_autoland(
    ci_env, monkeypatch, output, xtreme_regression
):
    monkeypatch.setenv("GECKO_HEAD_REPOSITORY", "https://hg.mozilla.org/try/")
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    xtreme_regression.assert_not_called()
    assert handler.xtreme_regressions == []


def test_detect_xtreme_regressions_without_perfherder_data(
    ci_env, output, xtreme_regression
):
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 0)

    xtreme_regression.assert_not_called()


def test_detect_xtreme_regressions_failure(ci_env, output, xtreme_regression):
    xtreme_regression.return_value.detect.side_effect = ConnectionError(
        "Treeherder is down"
    )
    handler = RaptorResultsHandler()
    handler._detect_xtreme_regressions(output, 1)

    assert handler.xtreme_regressions == []


if __name__ == "__main__":
    mozunit.main()
