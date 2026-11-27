# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import mozunit
import pytest

from talos.talosconfig import generateTalosConfig, writeConfigFile

BCONTROLLER_KEYS = {
    "command",
    "child_process",
    "process",
    "browser_wait",
    "test_timeout",
    "browser_path",
    "error_filename",
}


@pytest.fixture
def browser_config(tmp_path):
    return {
        "child_process": "plugin-container",
        "process": "firefox",
        "browser_wait": 5,
        "test_timeout": 1200,
        "browser_path": os.path.join(str(tmp_path), "app", "firefox"),
        "error_filename": "errors.txt",
        "bcontroller_config": str(tmp_path / "bcontroller.json"),
        "buildid": "20240101",
        "sourcestamp": "abcdef",
        "repository": "https://hg.mozilla.org/mozilla-central",
        "title": "qm-pxp01",
        "unrelated": "not written",
    }


def read_config(browser_config):
    with open(browser_config["bcontroller_config"]) as f:
        return json.load(f)


def test_write_config_file_selected_keys():
    assert json.loads(writeConfigFile({"a": 1, "b": 2}, ["b"])) == {"b": 2}


def test_write_config_file_all_keys():
    assert json.loads(writeConfigFile({"a": 1, "b": 2}, None)) == {"a": 1, "b": 2}
    assert json.loads(writeConfigFile({"a": 1}, [])) == {"a": 1}


def test_write_config_file_missing_key():
    with pytest.raises(KeyError):
        writeConfigFile({"a": 1}, ["b"])


def test_generate_config_writes_command_and_controller_keys(browser_config):
    content = generateTalosConfig(["firefox", "-profile", "p"], browser_config, {})

    written = read_config(browser_config)
    assert json.loads(content) == written
    assert set(written) == BCONTROLLER_KEYS
    assert written["command"] == "firefox -profile p"
    assert browser_config["command"] == "firefox -profile p"


def test_generate_config_with_xperf_path(browser_config):
    browser_config["xperf_path"] = "c:/xperf.exe"

    generateTalosConfig(["firefox"], browser_config, {"name": "tp5n"})

    written = read_config(browser_config)
    assert set(written) == BCONTROLLER_KEYS | {
        "xperf_path",
        "buildid",
        "sourcestamp",
        "repository",
        "title",
        "testname",
    }
    assert written["testname"] == "tp5n"


def test_generate_config_with_xperf_path_without_test_name(browser_config):
    browser_config["xperf_path"] = "c:/xperf.exe"

    generateTalosConfig(["firefox"], browser_config, {})

    assert "testname" not in read_config(browser_config)


def test_generate_config_with_xperf_providers(browser_config, capsys):
    test_config = {
        "xperf_providers": ["PROC_THREAD"],
        "xperf_user_providers": ["Mozilla Generic Provider"],
        "xperf_stackwalk": ["FileCreate"],
    }

    generateTalosConfig(["firefox"], browser_config, test_config, pid=42)

    written = read_config(browser_config)
    assert written["xperf_providers"] == ["PROC_THREAD"]
    assert written["xperf_user_providers"] == ["Mozilla Generic Provider"]
    assert written["xperf_stackwalk"] == ["FileCreate"]
    assert written["processID"] == 42
    assert written["approot"] == os.path.dirname(browser_config["browser_path"])
    assert "extending with xperf!" in capsys.readouterr().out


def test_generate_config_needs_all_xperf_provider_keys(browser_config):
    test_config = {"xperf_providers": ["PROC_THREAD"], "xperf_stackwalk": []}

    generateTalosConfig(["firefox"], browser_config, test_config, pid=42)

    assert set(read_config(browser_config)) == BCONTROLLER_KEYS


if __name__ == "__main__":
    mozunit.main()
