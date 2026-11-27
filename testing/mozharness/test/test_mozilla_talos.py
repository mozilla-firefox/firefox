# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import json
import os
import sys
from unittest.mock import Mock, patch

import mozunit
import pytest
from mozharness.base.log import CRITICAL, ERROR, INFO, WARNING
from mozharness.base.python import VirtualenvMixin
from mozharness.mozilla.automation import (
    TBPL_FAILURE,
    TBPL_RETRY,
    TBPL_SUCCESS,
    TBPL_WARNING,
)
from mozharness.mozilla.testing import talos, testbase
from mozharness.mozilla.testing.talos import Talos, TalosErrorList, TalosOutputParser

SUITES = {
    "suites": {
        "tp5n": {
            "tests": ["tp5n"],
            "pagesets_name": "tp5n.zip",
            "extra_prefs": ["a.b=1"],
        },
        "damp": {"tests": ["damp"], "pagesets_name": "tp5n.zip"},
        "jetstream": {"tests": ["jetstream"], "benchmark_zip": "jetstream.zip"},
        "webext": {"tests": ["ts_webext"], "webextensions_zip": "webext.zip"},
        "other": {"tests": ["ts_paint", "tart"]},
        "motionmark": {"tests": ["motionmark_htmlsuite"]},
    }
}


@pytest.fixture
def make_talos(tmp_path, monkeypatch):
    monkeypatch.setenv("REQUIRE_GPU", "0")
    monkeypatch.delenv("MOZ_AUTOMATION", raising=False)
    monkeypatch.delenv("PYTHONPATH", raising=False)

    def make(talos_json_config=None, **config):
        base = {
            "base_work_dir": str(tmp_path),
            "work_dir": "build",
            "log_to_console": False,
        }
        base.update(config)
        t = Talos(config=base, option_args=[])
        if talos_json_config is not None:
            t.talos_json_config = copy.deepcopy(talos_json_config)
        return t

    return make


def write_suites(tmp_path):
    talos_json = tmp_path / "talos" / "talos.json"
    talos_json.parent.mkdir(parents=True, exist_ok=True)
    talos_json.write_text(json.dumps(SUITES))
    return talos_json


def test_init_defaults_set_abs_dirs_and_disable_profiler(make_talos, tmp_path):
    t = make_talos()
    dirs = t.query_abs_dirs()
    assert dirs["abs_work_dir"] == str(tmp_path / "build")
    assert dirs["abs_blob_upload_dir"] == str(tmp_path / "build" / "blobber_upload_dir")
    assert dirs["abs_test_install_dir"] == str(tmp_path / "build" / "tests")
    assert t.query_abs_dirs() is dirs
    assert t.workdir == dirs["abs_work_dir"]
    assert not t.gecko_profile
    assert t.query_gecko_profile_options() == []


def test_init_enables_profiler_from_settings(make_talos):
    t = make_talos(
        gecko_profile_interval=2.5,
        talos_extra_options=["--gecko-profile-entries", "1000", "--foo"],
    )
    assert t.gecko_profile
    assert t.gecko_profile_interval == 2.5
    assert t.gecko_profile_entries == "1000"
    assert t.query_gecko_profile_options() == [
        "--gecko-profile",
        "--gecko-profile-interval",
        "2.5",
        "--gecko-profile-entries",
        "1000",
    ]


def test_init_gecko_profile_from_extra_options(make_talos):
    t = make_talos(
        talos_extra_options=["--gecko-profile", "--gecko-profile-threads", "GeckoMain"]
    )
    assert t.gecko_profile
    assert t.query_gecko_profile_options() == [
        "--gecko-profile",
        "--gecko-profile-threads",
        "GeckoMain",
    ]


def test_query_talos_json_config_reads_file(make_talos, tmp_path):
    t = make_talos()
    t.talos_path = str(write_suites(tmp_path).parent)
    config = t.query_talos_json_config()
    assert config == SUITES
    assert t.talos_json == os.path.join(t.talos_path, "talos.json")
    assert t.query_talos_json_config() is config


def test_split_and_merge_paths(make_talos):
    t = make_talos()
    assert t.split_path(os.path.join("a", "b", "c")) == ["a", "b", "c"]
    absolute = t.split_path(os.path.abspath(os.path.join("x", "y")))
    assert absolute[-2:] == ["x", "y"]
    assert os.path.isabs(absolute[0])
    assert t.merge_paths(["a", "b", "c"], ["..", "..", "d"]) == ["a", "d"]
    assert t.make_talos_domain("www.example.com") == "www.example.com-talos"


def test_replace_relative_iframe_paths(make_talos, tmp_path):
    t = make_talos()
    site = tmp_path / "talos" / "fis" / "tp5n" / "example.com" / "www.example.com"
    site.mkdir(parents=True)
    page = site / "index.html"
    page.write_text(
        '<iframe src="../../foo.com/www.foo.com/frame.html"></iframe>',
        encoding="utf-8",
    )
    latin = site / "latin.html"
    latin.write_bytes(
        b'\xe9<iframe src="../../bar.com/www.bar.com/frame.html"></iframe>'
    )
    other = site / "style.css"
    other.write_text('iframe src="../../foo.com/x.html"')

    for name in ("index.html", "latin.html", "style.css"):
        t.replace_relative_iframe_paths(str(site), name)

    assert page.read_text(encoding="utf-8") == (
        '<iframe src="http://www.foo.com-talos/fis/tp5n/foo.com/www.foo.com/'
        'frame.html"></iframe>'
    )
    assert latin.read_bytes() == (
        b'\xe9<iframe src="http://www.bar.com-talos/fis/tp5n/bar.com/www.bar.com/'
        b'frame.html"></iframe>'
    )
    assert other.read_text() == 'iframe src="../../foo.com/x.html"'


@pytest.mark.parametrize(
    "suite,pagesets,benchmark,webext",
    [
        ("tp5n", "tp5n.zip", None, None),
        ("jetstream", None, "jetstream.zip", None),
        ("webext", None, None, "webext.zip"),
    ],
)
def test_query_suite_artifacts(make_talos, suite, pagesets, benchmark, webext):
    t = make_talos(talos_json_config=SUITES)
    t.suite = suite
    assert t.query_pagesets_name() == pagesets
    assert t.query_benchmark_zip() == benchmark
    assert t.query_webextensions_zip() == webext
    assert t.pagesets_name_manifest == "tp5n-pageset.manifest"
    assert t.benchmark_zip_manifest == "jetstream-benchmark.manifest"
    assert t.webextensions_zip_manifest == "webextensions.manifest"
    t.talos_json_config = {"suites": {suite: {}}}
    assert t.query_pagesets_name() == pagesets
    assert t.query_benchmark_zip() == benchmark
    assert t.query_webextensions_zip() == webext


def test_query_suite_artifacts_without_suite(make_talos):
    t = make_talos(talos_json_config=SUITES)
    t.suite = None
    assert t.query_pagesets_name() is None
    assert t.query_benchmark_zip() is None
    assert t.query_webextensions_zip() is None
    assert t.query_suite_extra_prefs() == []
    t.validate_suite()


@pytest.mark.parametrize("flag", ["-a", "--activeTests"])
def test_get_suite_from_test(make_talos, flag):
    t = make_talos(talos_json_config=SUITES, talos_extra_options=[flag, "tart"])
    assert t.get_suite_from_test() == "other"


@pytest.mark.parametrize(
    "extra_options", [["-a", "unknown"], ["--activeTests"], ["-a"]]
)
def test_get_suite_from_test_invalid(make_talos, extra_options):
    t = make_talos(talos_json_config=SUITES, talos_extra_options=extra_options)
    with pytest.raises(SystemExit):
        t.get_suite_from_test()


def test_get_suite_from_test_without_config(make_talos):
    t = make_talos(talos_extra_options=["-a", "tart"])
    t.query_talos_json_config = Mock(return_value={})
    with pytest.raises(SystemExit):
        t.get_suite_from_test()


def test_suite_extra_prefs_and_validation(make_talos):
    t = make_talos(talos_json_config=SUITES)
    t.suite = "tp5n"
    assert list(t.query_suite_extra_prefs()) == ["a.b=1"]
    t.validate_suite()
    t.suite = "other"
    assert t.query_suite_extra_prefs() == []
    t.suite = "missing"
    with pytest.raises(SystemExit):
        t.validate_suite()


def test_talos_options_requires_binary(make_talos):
    t = make_talos()
    with pytest.raises(SystemExit):
        t.talos_options()


def test_talos_options_minimal(make_talos):
    t = make_talos(binary_path="/path/firefox", talos_json_config=SUITES)
    t.suite = None
    assert t.talos_options() == ["--executablePath", "/path/firefox"]


def test_talos_options_full(make_talos, monkeypatch):
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    t = make_talos(
        binary_path="/path/firefox.exe",
        suite="tp5n",
        title="machine",
        project="try",
        pdfpaint_chunk=2,
        talos_extra_options=["--extra"],
        code_coverage=True,
        extra_prefs=["fission.autostart=false"],
        gecko_profile=True,
        gecko_profile_interval=1,
        talos_json_config={"suites": {"tp5n": {"extra_prefs": ["a.b=1"]}}},
    )
    t.suite = "tp5n"
    t.symbols_path = "/symbols"
    options = t.talos_options(
        args=["--arg"], activeTests=["ts_paint", "tart"], cycles="3"
    )
    assert options == [
        "--executablePath",
        "/path/firefox",
        "--suite",
        "tp5n",
        "--title",
        "machine",
        "--symbolsPath",
        "/symbols",
        "--project",
        "try",
        "--pdfPaintChunk",
        "2",
        "--activeTests",
        "ts_paint:tart",
        "--cycles",
        "3",
        "--gecko-profile",
        "--gecko-profile-interval",
        "1",
        "--arg",
        "--extra",
        "--code-coverage",
        "--screenshot-on-failure",
        "--setpref=a.b=1",
        "--setpref=fission.autostart=false",
        "--disable-fission",
    ]


def test_talos_options_disable_fission_flag(make_talos):
    t = make_talos(
        binary_path="/path/firefox",
        fission=False,
        screenshot_on_failure=True,
        talos_json_config=SUITES,
    )
    t.suite = "other"
    options = t.talos_options(activeTests="ts_paint")
    assert options == [
        "--executablePath",
        "/path/firefox",
        "--activeTests",
        "ts_paint",
        "--screenshot-on-failure",
        "--disable-fission",
    ]


def make_zip_fetcher(t, files):
    def fetch(manifest, output_dir, cache):
        assert os.path.isabs(manifest)

    def unzip(archive, dest):
        root = os.path.join(dest, os.path.basename(archive)[: -len(".zip")])
        for name, content in files.items():
            path = os.path.join(root, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(content)

    t.tooltool_fetch = Mock(side_effect=fetch)
    t.unzip = Mock(side_effect=unzip)


def test_populate_webroot_production_pagesets(make_talos, tmp_path):
    t = make_talos(suite="tp5n", tooltool_cache="/cache")
    talos_dir = tmp_path / "build" / "tests" / "talos"
    talos_dir.mkdir(parents=True)
    (talos_dir / "talos.json").write_text(json.dumps(SUITES))
    make_zip_fetcher(
        t,
        {
            os.path.join(
                "example.com", "www.example.com", "index.html"
            ): '<iframe src="../../a.com/www.a.com/f.html"></iframe>'
        },
    )

    t.populate_webroot()

    assert t.suite == "tp5n"
    assert t.talos_path == str(talos_dir)
    tests_dest = str(talos_dir / "talos" / "tests")
    fis_dest = str(talos_dir / "talos" / "fis")
    manifest = str(talos_dir / "tp5n-pageset.manifest")
    assert t.tooltool_fetch.call_args_list == [
        ((manifest,), {"output_dir": tests_dest, "cache": "/cache"}),
        ((manifest,), {"output_dir": fis_dest, "cache": "/cache"}),
    ]
    tests_page = os.path.join(
        tests_dest, "tp5n", "example.com", "www.example.com", "index.html"
    )
    fis_page = os.path.join(
        fis_dest, "tp5n", "example.com", "www.example.com", "index.html"
    )
    with open(tests_page) as f:
        assert "../../a.com" in f.read()
    with open(fis_page) as f:
        assert "http://www.a.com-talos/fis/tp5n/a.com/www.a.com/f.html" in f.read()


def test_populate_webroot_damp_only_uses_fis(make_talos, tmp_path):
    t = make_talos(suite="damp", talos_json_config=SUITES)
    make_zip_fetcher(t, {})
    t.populate_webroot()
    talos_dir = tmp_path / "build" / "tests" / "talos"
    t.tooltool_fetch.assert_called_once_with(
        str(talos_dir / "tp5n-pageset.manifest"),
        output_dir=str(talos_dir / "talos" / "fis"),
        cache=None,
    )


@pytest.mark.parametrize(
    "suite,manifest,dest",
    [
        ("jetstream", "jetstream-benchmark.manifest", "tests"),
        ("webext", "webextensions.manifest", "webextensions"),
    ],
)
def test_populate_webroot_benchmark_and_webextensions(
    make_talos, tmp_path, suite, manifest, dest
):
    t = make_talos(suite=suite, talos_json_config=SUITES)
    make_zip_fetcher(t, {})
    t.populate_webroot()
    talos_dir = tmp_path / "build" / "tests" / "talos"
    t.tooltool_fetch.assert_called_once_with(
        str(talos_dir / manifest),
        output_dir=str(talos_dir / "talos" / dest),
        cache=None,
    )
    t.unzip.assert_called_once()


def test_populate_webroot_archive_already_available(make_talos, tmp_path):
    t = make_talos(suite="jetstream", talos_json_config=SUITES)
    dest = tmp_path / "build" / "tests" / "talos" / "talos" / "tests"
    dest.mkdir(parents=True)
    (dest / "jetstream.zip").write_bytes(b"")
    make_zip_fetcher(t, {})
    t.populate_webroot()
    t.tooltool_fetch.assert_not_called()


def test_populate_webroot_local_no_download(make_talos, tmp_path):
    talos_json = write_suites(tmp_path)
    t = make_talos(
        run_local=True,
        talos_json=str(talos_json),
        talos_extra_options=["--suite", "jetstream", "--no-download"],
    )
    make_zip_fetcher(t, {})
    t.populate_webroot()
    assert t.suite == "jetstream"
    assert t.talos_path == str(talos_json.parent)
    t.tooltool_fetch.assert_not_called()


def test_populate_webroot_local_test_name(make_talos, tmp_path):
    talos_json = write_suites(tmp_path)
    t = make_talos(
        run_local=True,
        talos_json=str(talos_json),
        talos_extra_options=["-a", "motionmark_htmlsuite"],
    )
    t.get_webkit_source = Mock()
    t.populate_webroot()
    assert t.suite == "motionmark"
    t.get_webkit_source.assert_called_once_with()


def test_populate_webroot_local_missing_suite_name(make_talos, tmp_path):
    t = make_talos(
        run_local=True,
        talos_json=str(write_suites(tmp_path)),
        talos_extra_options=["--suite"],
    )
    with pytest.raises(SystemExit):
        t.populate_webroot()


def test_populate_webroot_local_other_options(make_talos, tmp_path):
    t = make_talos(
        run_local=True,
        suite="other",
        talos_json=str(write_suites(tmp_path)),
        talos_extra_options=["--cycles", "1"],
    )
    t.suite = "other"
    t.populate_webroot()
    assert t.suite == "other"


def test_get_webkit_source(make_talos, tmp_path):
    repo = tmp_path / "repo"
    src = repo / "third_party" / "webkit" / "PerformanceTests"
    (src / "MotionMark").mkdir(parents=True)
    (src / "MotionMark" / "index.html").write_text("mm")
    t = make_talos(repo_path=str(repo))
    t.talos_path = str(tmp_path / "talos")
    dest = tmp_path / "talos" / "talos" / "tests" / "webkit" / "PerformanceTests"
    (dest / "stale").mkdir(parents=True)

    t.get_webkit_source()

    assert (dest / "MotionMark" / "index.html").read_text() == "mm"
    assert not (dest / "stale").exists()


def test_get_webkit_source_copy_failure(make_talos, tmp_path):
    t = make_talos(repo_path=str(tmp_path / "missing"))
    t.talos_path = str(tmp_path / "talos")
    t.critical = Mock()
    t.get_webkit_source()
    t.critical.assert_called_once()


def test_download_and_extract_requests_talos_categories(make_talos):
    t = make_talos()
    with patch.object(
        testbase.TestingMixin, "download_and_extract", return_value="ok"
    ) as dl:
        assert t.download_and_extract(extract_dirs=["ignored"]) == "ok"
    dl.assert_called_once_with(
        extract_dirs=[
            "tools/wptserve/*",
            "tools/wpt_third_party/h2/*",
            "tools/wpt_third_party/pywebsocket3/*",
        ],
        suite_categories=["common", "talos"],
    )


def test_query_python_constraints_returns_mozbase_file(make_talos, tmp_path):
    t = make_talos()
    t.talos_path = str(tmp_path / "tests" / "talos")
    assert t.query_python_constraints() == [
        str(tmp_path / "tests" / "config" / "requirements-mozbase.txt")
    ]


@pytest.mark.parametrize(
    "platform,site_packages",
    [
        ("win64", os.path.join("Lib", "site-packages")),
        ("linux64", os.path.join("lib", "python", "site-packages")),
    ],
)
def test_create_virtualenv_reuses_local_venv(
    make_talos, tmp_path, monkeypatch, platform, site_packages
):
    venv = tmp_path / "venv"
    venv.mkdir()
    python = tmp_path / "python"
    python.write_text("")
    t = make_talos(run_local=True, virtualenv_path=str(venv))
    t.query_exe = Mock(return_value=str(python))
    t.platform_name = Mock(return_value=platform)
    t.register_virtualenv_module = Mock()
    monkeypatch.setattr(sys, "path", list(sys.path))
    with patch.object(talos.multiprocessing, "set_executable") as set_exe:
        t.create_virtualenv()
    assert sys.path[-1] == os.path.join(str(venv), site_packages)
    assert set_exe.called == platform.startswith("win")
    t.register_virtualenv_module.assert_not_called()


@pytest.mark.parametrize("run_local", [False, True])
def test_create_virtualenv_registers_requirements_by_run_mode(
    make_talos, tmp_path, run_local
):
    t = make_talos(run_local=run_local, virtualenv_path=str(tmp_path / "venv"))
    t.talos_path = str(tmp_path / "tests" / "talos")
    t.query_exe = Mock(return_value=str(tmp_path / "missing-python"))
    t.platform_name = Mock(return_value="macosx64")
    t.register_virtualenv_module = Mock()
    t.install_module = Mock()
    with patch.object(VirtualenvMixin, "create_virtualenv") as create:
        t.create_virtualenv()
    create.assert_called_once_with()
    config_dir = tmp_path / "build" / "tests" / "config"
    if run_local:
        config_dir = tmp_path / "tests" / "config"
    mozbase = (
        "mozbase_source_requirements.txt" if run_local else ("mozbase_requirements.txt")
    )
    talos_req = "source_requirements.txt" if run_local else "requirements.txt"
    t.register_virtualenv_module.assert_called_once_with(
        requirements=[str(config_dir / mozbase)], editable=True
    )
    t.install_module.assert_called_once_with(
        requirements=[os.path.join(t.talos_path, talos_req)]
    )


@pytest.fixture
def schema(tmp_path):
    path = tmp_path / "schema.json"
    path.write_text(
        json.dumps({"type": "object", "required": ["framework"]}), encoding="utf-8"
    )
    with patch.object(talos, "perfherder_schema_path", return_value=str(path)):
        yield path


def make_parser(t):
    return TalosOutputParser(
        config=t.config, log_obj=t.log_obj, error_list=TalosErrorList
    )


@pytest.mark.parametrize(
    "perf_data,expected",
    [
        ([], TBPL_WARNING),
        (['{"framework": {}}', '{"framework": {}}'], TBPL_WARNING),
        (['{"framework": {}}'], TBPL_SUCCESS),
        (['{"suites": []}'], TBPL_WARNING),
        (["not json"], TBPL_WARNING),
    ],
)
def test_validate_treeherder_data(make_talos, schema, perf_data, expected):
    t = make_talos()
    parser = make_parser(t)
    parser.found_perf_data = perf_data
    t._validate_treeherder_data(parser)
    assert parser.worst_tbpl_status == expected


def test_artifact_perf_data_copies_file_or_logs_critical(make_talos, tmp_path):
    t = make_talos()
    work = tmp_path / "build"
    work.mkdir(parents=True, exist_ok=True)
    (work / "local.json").write_text("{}")
    parser = make_parser(t)
    dest = tmp_path / "out.json"
    t._artifact_perf_data(parser, str(dest))
    assert dest.read_text() == "{}"
    assert parser.worst_tbpl_status == TBPL_SUCCESS

    t._artifact_perf_data(parser, str(tmp_path / "missing" / "out.json"))
    assert parser.worst_tbpl_status == TBPL_FAILURE
    assert parser.worst_log_level == CRITICAL


@pytest.fixture
def runner(make_talos, tmp_path, schema, monkeypatch):
    monkeypatch.setenv("MOZ_FETCHES_DIR", "/fetches")

    def make(lines=(), return_code=0, **config):
        config.setdefault("binary_path", "/path/firefox")
        config.setdefault("suite", "other")
        t = make_talos(**config)
        t.talos_path = str(write_suites(tmp_path).parent)
        t.suite = config["suite"]
        t.query_python_path = Mock(return_value="/venv/bin/python")
        t.query_minidump_stackwalk = Mock(return_value="/stackwalk")
        t.record_status = Mock()
        calls = []

        def run_command(command, **kwargs):
            calls.append((command, kwargs))
            parser = kwargs.get("output_parser")
            if parser:
                for line in lines:
                    parser.parse_single_line(line)
                return return_code
            return 0

        t.run_command = Mock(side_effect=run_command)
        t.calls = calls
        return t

    return make


def test_run_tests_production_success(runner, tmp_path):
    work = tmp_path / "build"
    work.mkdir(parents=True, exist_ok=True)
    (work / "local.json").write_text('{"framework": {}}')
    t = runner(lines=['PERFHERDER_DATA: {"framework": {}}'], repo_path="/repo")
    t.run_tests()

    command, kwargs = t.calls[0]
    upload = str(tmp_path / "build" / "blobber_upload_dir")
    assert command[:2] == [
        "/venv/bin/python",
        str(tmp_path / "talos" / "talos" / "run_tests.py"),
    ]
    assert command[-2:] == [
        "--log-tbpl-level=info",
        "--log-errorsummary=" + os.path.join(upload, "other_errorsummary.log"),
    ]
    env = kwargs["env"]
    assert kwargs["output_timeout"] == 3600
    assert kwargs["cwd"] == str(tmp_path / "build")
    assert env["MOZ_UPLOAD_DIR"] == upload
    assert env["MINIDUMP_STACKWALK"] == "/stackwalk"
    assert env["MOZ_FETCHES_DIR"] == "/fetches"
    assert env["PYTHONPATH"] == t.talos_path
    assert env["MOZ_DEVELOPER_REPO_DIR"] == "/repo"
    assert "MOZ_DEVELOPER_OBJ_DIR" not in env
    assert os.path.isdir(upload)
    with open(os.path.join(upload, "perfherder-data.json")) as f:
        assert json.load(f) == {"framework": {}}
    t.record_status.assert_called_once_with(TBPL_SUCCESS, level=INFO)


def test_run_tests_local_omits_upload_env_and_uses_obj_dir(
    runner, tmp_path, monkeypatch
):
    monkeypatch.setenv("PYTHONPATH", "existing")
    t = runner(
        lines=['PERFHERDER_DATA: {"framework": {}}'],
        run_local=True,
        mozbuild_path="/mozbuild",
        obj_path="/obj",
        talos_output_timeout=10,
    )
    t.run_tests()
    command, kwargs = t.calls[0]
    env = kwargs["env"]
    assert command[-1] == "--log-tbpl-level=info"
    assert kwargs["output_timeout"] == 10
    assert env["MOZBUILD_PATH"] == "/mozbuild"
    assert env["MOZ_DEVELOPER_OBJ_DIR"] == "/obj"
    assert env["PYTHONPATH"] == t.talos_path + os.pathsep + "existing"
    assert "MINIDUMP_STACKWALK" not in env
    assert not os.path.exists(
        tmp_path / "build" / "blobber_upload_dir" / "perfherder-data.json"
    )
    t.record_status.assert_called_once_with(TBPL_SUCCESS, level=INFO)


@pytest.mark.parametrize(
    "return_code,tbpl,level",
    [(1, TBPL_WARNING, WARNING), (4, TBPL_RETRY, WARNING), (2, TBPL_FAILURE, ERROR)],
)
def test_run_tests_failure_codes(runner, return_code, tbpl, level):
    t = runner(return_code=return_code)
    t.run_tests()
    t.record_status.assert_called_once_with(tbpl, level=level)


def test_run_tests_gecko_profile_skips_validation(runner):
    t = runner(gecko_profile=True)
    t._validate_treeherder_data = Mock()
    t.run_tests()
    t._validate_treeherder_data.assert_not_called()
    assert "--gecko-profile" in t.calls[0][0]


def test_run_tests_no_upload_results(runner):
    t = runner()
    t._validate_treeherder_data = Mock()
    t.run_tests(args=["--no-upload-results"])
    t._validate_treeherder_data.assert_not_called()


def test_run_tests_lists_minidumps(runner):
    t = runner(
        lines=["""talosError: "error executing: '/a /b /c'\""""],
        return_code=2,
    )
    t.run_tests()
    assert [c[0] for c in t.calls[1:]] == [
        ["ls", "-l", "/a"],
        ["ls", "-l", "/b"],
        ["ls", "-l", "/c"],
    ]


def test_run_tests_debug_runs_popen_not_run_command(runner):
    t = runner()
    t.return_code = 0
    t._validate_treeherder_data = Mock()
    with patch.object(talos.subprocess, "Popen") as popen:
        t.run_tests(args=["--debugger", "lldb"])
    command = popen.call_args.args[0]
    assert "--debugger" in command
    assert popen.call_args.kwargs["bufsize"] == 0
    popen.return_value.wait.assert_called_once_with()
    t.run_command.assert_not_called()


@pytest.fixture
def monitor():
    with patch.object(talos, "SystemResourceMonitor") as monitor:
        yield monitor


def test_output_parser_markers(make_talos, monitor):
    parser = make_parser(make_talos())
    for line in [
        "SUITE-START | tp5n",
        "TEST-START | tp5n",
        "TEST-OK | tp5n",
        "TEST-START | tart",
        "TEST-UNEXPECTED-ERROR | tart",
        "TEST-INFO | tart",
        "SUITE-END | tp5n",
        "Running cycle 1/3",
        "PROCESS-CRASH | tp5n",
        "RETURN: something",
    ]:
        parser.parse_single_line(line)
    assert monitor.begin_marker.call_args_list == [
        (("suite", ""),),
        (("test", "tp5n"),),
        (("test", "tart"),),
    ]
    assert monitor.end_marker.call_args_list == [
        (("test", "tp5n"),),
        (("test", "tart"),),
        (("suite", ""),),
    ]
    assert monitor.record_event.call_args_list == [
        (("Running cycle 1/3",),),
        (("PROCESS-CRASH | tp5n",),),
    ]
    assert parser.worst_tbpl_status == TBPL_SUCCESS


def test_output_parser_collects_data_minidumps_and_retry(make_talos, monitor):
    parser = make_parser(make_talos())
    parser.parse_single_line('foo PERFHERDER_DATA: {"a": 1}')
    parser.parse_single_line("""talosError: "error executing: '1 2 3'\"""")
    parser.parse_single_line("ADBTimeoutError: device gone")
    assert parser.found_perf_data == ['{"a": 1}']
    assert parser.minidump_output == ("1", "2", "3")
    assert parser.worst_tbpl_status == TBPL_RETRY
    assert parser.worst_log_level == CRITICAL


if __name__ == "__main__":
    mozunit.main()
