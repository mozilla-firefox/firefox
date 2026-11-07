import json
import os
import pathlib
import sys
from unittest import mock

import pytest
from mozlog.structuredlog import StructuredLogger

from mozperftest.metrics.notebook.perftestetl import PerftestETL
from mozperftest.metrics.notebook.perftestnotebook import PerftestNotebook
from mozperftest.system.geckoprofiler import GeckoProfilerController
from mozperftest.tests.support import HERE, get_running_env
from mozperftest.utils import temp_dir


@pytest.fixture
def patched_mozperftest_tools():
    tools_mock = mock.MagicMock(name="tools-mock")
    _module = mock.MagicMock(name="mozperftest_tools")
    _module.SideBySide.return_value = tools_mock

    try:
        sys.modules["mozperftest_tools.side_by_side"] = _module
        yield tools_mock
    finally:
        del sys.modules["mozperftest_tools.side_by_side"]


@pytest.fixture(scope="session", autouse=True)
def data():
    data_1 = {
        "browserScripts": [
            {"timings": {"firstPaint": 101}},
            {"timings": {"firstPaint": 102}},
            {"timings": {"firstPaint": 103}},
        ],
    }

    data_2 = {
        "browserScripts": [
            {"timings": {"firstPaint": 201}},
            {"timings": {"firstPaint": 202}},
            {"timings": {"firstPaint": 203}},
        ],
    }

    data_3 = {
        "browserScripts": [
            {"timings": {"firstPaint": 301}},
            {"timings": {"firstPaint": 302}},
            {"timings": {"firstPaint": 303}},
        ],
    }

    yield {"data_1": data_1, "data_2": data_2, "data_3": data_3}


@pytest.fixture(scope="session", autouse=True)
def standarized_data():
    return {
        "browsertime": [
            {
                "data": [
                    {"value": 1, "xaxis": 1, "file": "file_1"},
                    {"value": 2, "xaxis": 2, "file": "file_2"},
                ],
                "name": "name",
                "subtest": "subtest",
            }
        ]
    }


@pytest.fixture(scope="session", autouse=True)
def files(data):
    # Create a temporary directory.
    with temp_dir() as td:
        tmp_path = pathlib.Path(td)

    dirs = {
        "resources": tmp_path / "resources",
        "output": tmp_path / "output",
    }

    for d in dirs.values():
        d.mkdir(parents=True, exist_ok=True)

    # Create temporary data files for tests.
    def _create_temp_files(path, data):
        path.touch(exist_ok=True)
        path.write_text(data)
        return path.resolve().as_posix()

    resources = {}
    json_1 = dirs["resources"] / "file_1.json"
    resources["file_1"] = _create_temp_files(json_1, json.dumps(data["data_1"]))

    json_2 = dirs["resources"] / "file_2.json"
    resources["file_2"] = _create_temp_files(json_2, json.dumps(data["data_2"]))

    txt_3 = dirs["resources"] / "file_3.txt"
    resources["file_3"] = _create_temp_files(txt_3, str(data["data_3"]))

    output = dirs["output"] / "output.json"

    yield {
        "resources": resources,
        "dirs": dirs,
        "output": output,
    }


@pytest.fixture(scope="session", autouse=True)
def ptetls(files):
    resources, dirs, output = files["resources"], files["dirs"], files["output"]
    _, metadata, _ = get_running_env()
    config = {"output": output}
    file_group_list = {"group_1": list(resources.values())}
    file_group_str = {"group_1": dirs["resources"].resolve().as_posix()}

    yield {
        "ptetl_list": PerftestETL(
            file_groups=file_group_list,
            config=config,
            prefix="PerftestETL",
            logger=metadata,
            sort_files=True,
        ),
        "ptetl_str": PerftestETL(
            file_groups=file_group_str,
            config=config,
            prefix="PerftestETL",
            logger=metadata,
            sort_files=True,
        ),
    }


@pytest.fixture(scope="session", autouse=True)
def ptnb(standarized_data):
    _, metadata, _ = get_running_env()
    return PerftestNotebook(standarized_data, metadata, "PerftestNotebook")


@pytest.fixture(scope="function", autouse=True)
def perftestetl_plugin():
    ret = HERE / "data" / "perftestetl_plugin"

    os.environ["PERFTESTETL_PLUGIN"] = ret.resolve().as_posix()

    yield ret

    del os.environ["PERFTESTETL_PLUGIN"]


@pytest.fixture(autouse=True)
def reset_geckoprofiler_package_id(monkeypatch):
    monkeypatch.setattr(GeckoProfilerController, "_package_id", None)


@pytest.fixture
def set_perf_flags(monkeypatch):
    monkeypatch.setenv("PERF_FLAGS", "gecko-profile")


@pytest.fixture
def structured_logger():
    return StructuredLogger("logger")


@pytest.fixture
def perfdocs_sample():
    from mozperftest.tests.test_perfdocs import (
        DYNAMIC_SAMPLE_CONFIG,
        SAMPLE_CONFIG,
        SAMPLE_INI,
        SAMPLE_METRICS_CONFIG,
        SAMPLE_TEST,
        temp_dir,
        temp_file,
    )

    with temp_dir() as tmpdir:
        suite_dir = pathlib.Path(tmpdir, "suite")
        raptor_dir = pathlib.Path(tmpdir, "raptor")
        raptor_suitedir = pathlib.Path(tmpdir, "raptor", "suite")
        raptor_another_suitedir = pathlib.Path(tmpdir, "raptor", "another_suite")
        perfdocs_dir = pathlib.Path(tmpdir, "perfdocs")

        perfdocs_dir.mkdir(parents=True, exist_ok=True)
        suite_dir.mkdir(parents=True, exist_ok=True)
        raptor_dir.mkdir(parents=True, exist_ok=True)
        raptor_suitedir.mkdir(parents=True, exist_ok=True)
        raptor_another_suitedir.mkdir(parents=True, exist_ok=True)

        with temp_file(
            "perftest.toml", tempdir=suite_dir, content='["perftest_sample.js"]'
        ) as tmpmanifest, temp_file(
            "raptor_example1.ini", tempdir=raptor_suitedir, content=SAMPLE_INI
        ) as tmpexample1manifest, temp_file(
            "raptor_example2.ini", tempdir=raptor_another_suitedir, content=SAMPLE_INI
        ) as tmpexample2manifest, temp_file(
            "perftest_sample.js", tempdir=suite_dir, content=SAMPLE_TEST
        ) as tmptest, temp_file(
            "config.yml", tempdir=perfdocs_dir, content=SAMPLE_CONFIG
        ) as tmpconfig, temp_file(
            "config_2.yml", tempdir=perfdocs_dir, content=DYNAMIC_SAMPLE_CONFIG
        ) as tmpconfig_2, temp_file(
            "config_metrics.yml", tempdir=perfdocs_dir, content=SAMPLE_METRICS_CONFIG
        ) as tmpconfig_metrics, temp_file(
            "index.md",
            tempdir=perfdocs_dir,
            content="{metrics_rst_name}{documentation}",
        ) as tmpindex:
            yield {
                "top_dir": tmpdir,
                "manifest": {"path": tmpmanifest},
                "example1_manifest": tmpexample1manifest,
                "example2_manifest": tmpexample2manifest,
                "test": tmptest,
                "config": tmpconfig,
                "config_2": tmpconfig_2,
                "config_metrics": tmpconfig_metrics,
                "index": tmpindex,
            }
