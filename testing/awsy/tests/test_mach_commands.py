# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import importlib.util
import json
import os
import sys
from unittest import mock

import mozunit
import pytest
from awsy import MAX_TABS
from mach.registrar import Registrar
from marionette_harness import runtests
from mozbuild.base import BinaryNotFoundException
from mozlog.structured import commandline

AWSY_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_mach_commands():
    spec = importlib.util.spec_from_file_location(
        "awsy_mach_commands", os.path.join(AWSY_DIR, "mach_commands.py")
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.multiple(
        Registrar,
        categories={"testing": []},
        commands_by_category={"testing": set()},
        command_handlers={},
    ):
        spec.loader.exec_module(module)
    return module


mach_commands = load_mach_commands()


@pytest.fixture
def harness(monkeypatch):
    harness = mock.Mock()
    harness.return_value.run.return_value = 0
    monkeypatch.setattr(runtests, "MarionetteHarness", harness)
    monkeypatch.setattr(commandline, "setup_logging", mock.Mock(return_value="log"))
    return harness


@pytest.fixture
def parser(monkeypatch):
    parser = mock.Mock()
    monkeypatch.setattr(mach_commands, "setup_awsy_argument_parser", lambda: parser)
    return parser


@pytest.fixture
def context(tmp_path):
    context = mock.Mock()
    context.topsrcdir = str(tmp_path / "src")
    context.topobjdir = str(tmp_path / "obj")
    return context


@pytest.fixture(autouse=True)
def clean_state(monkeypatch):
    monkeypatch.delenv("DMD", raising=False)
    monkeypatch.setattr(sys, "path", list(sys.path))


def make_kwargs(**overrides):
    kwargs = {
        "base": False,
        "quick": False,
        "dmd": False,
        "testvars": None,
        "prefs_files": None,
        "webRootDir": None,
        "pageManifest": None,
        "resultsDir": None,
        "entities": None,
        "iterations": None,
        "perTabPause": None,
        "settleWaitTime": None,
        "maxTabs": None,
    }
    kwargs.update(overrides)
    return kwargs


def harness_args(harness):
    (runner,), call_kwargs = harness.call_args
    assert runner is runtests.MarionetteTestRunner
    return call_kwargs["args"]


def read_runtime_testvars(path):
    with open(path) as f:
        return json.load(f)


def test_setup_awsy_argument_parser_returns_marionette_arguments_parser():
    parser = mach_commands.setup_awsy_argument_parser()
    assert isinstance(parser, runtests.MarionetteArguments)
    args = parser.parse_args(["--log-mach-level", "debug", "test.py"])
    assert args.tests == ["test.py"]
    assert args.log_mach_level == "debug"


@pytest.mark.parametrize(
    "base,test_file,testvars_file,prefs_file",
    [
        (False, "test_memory_usage.py", "tp6-testvars.json", "tp6-prefs.json"),
        (True, "test_base_memory_usage.py", "base-testvars.json", "base-prefs.json"),
    ],
)
def test_run_awsy_default_and_base_runs_pass_expected_harness_arguments(
    context, harness, parser, base, test_file, testvars_file, prefs_file
):
    ret = mach_commands.run_awsy(
        context, [], binary="/bin/firefox", **make_kwargs(base=base)
    )

    assert ret == 0
    args = harness_args(harness)
    src = os.path.join(context.topsrcdir, "testing", "awsy")
    tests_dir = os.path.join(context.topobjdir, "_tests", "awsy")
    runtime_path = os.path.join(tests_dir, "runtime-testvars.json")
    assert args["tests"] == [os.path.join(src, "awsy", test_file)]
    assert args["binary"] == "/bin/firefox"
    assert args["testvars"] == [os.path.join(src, "conf", testvars_file), runtime_path]
    assert args["prefs_files"] == [os.path.join(src, "conf", prefs_file)]
    assert args["logger"] == "log"
    assert read_runtime_testvars(runtime_path) == {
        "webRootDir": os.path.join(tests_dir, "html"),
        "resultsDir": os.path.join(tests_dir, "results"),
        "dmd": False,
        "bin": "/bin/firefox",
        "run_local": True,
    }
    assert os.path.isdir(os.path.join(tests_dir, "results"))
    parser.verify_usage.assert_called_once()
    assert parser.verify_usage.call_args[0][0].tests == args["tests"]
    assert "DMD" not in os.environ


def test_run_awsy_explicit_options_override_paths_and_runtime_testvars(
    context, harness, parser, tmp_path
):
    web_root = tmp_path / "custom" / "html"
    results = tmp_path / "out"
    results.mkdir()
    web_root.parent.mkdir()
    testvars = [str(tmp_path / "my-testvars.json")]
    kwargs = make_kwargs(
        webRootDir=str(web_root),
        resultsDir=str(results),
        pageManifest="pages.yml",
        entities=2,
        iterations=3,
        perTabPause=4,
        settleWaitTime=5,
        maxTabs=6,
        testvars=testvars,
        prefs_files=["my-prefs.json"],
    )

    mach_commands.run_awsy(context, ["my_test.py"], binary="/bin/ff", **kwargs)

    args = harness_args(harness)
    runtime_path = str(tmp_path / "custom" / "runtime-testvars.json")
    assert args["tests"] == ["my_test.py"]
    assert args["testvars"] == [testvars[0], runtime_path]
    assert args["prefs_files"] == ["my-prefs.json"]
    assert read_runtime_testvars(runtime_path) == {
        "webRootDir": str(web_root),
        "pageManifest": "pages.yml",
        "resultsDir": str(results),
        "entities": 2,
        "iterations": 3,
        "perTabPause": 4,
        "settleWaitTime": 5,
        "maxTabs": 6,
        "dmd": False,
        "bin": "/bin/ff",
        "run_local": True,
    }


def test_run_awsy_quick_overrides_entities_and_timing_options(context, harness, parser):
    mach_commands.run_awsy(
        context, [], binary="/bin/ff", **make_kwargs(quick=True, entities=50)
    )
    runtime = read_runtime_testvars(harness_args(harness)["testvars"][-1])
    assert (
        runtime["entities"],
        runtime["iterations"],
        runtime["perTabPause"],
        runtime["settleWaitTime"],
    ) == (3, 1, 1, 1)


def test_run_awsy_returns_1_when_harness_fails(context, harness, parser):
    harness.return_value.run.return_value = 2
    assert mach_commands.run_awsy(context, [], binary="/b", **make_kwargs()) == 1


@pytest.mark.parametrize("os_name", ["linux", "win", "mac"])
def test_run_awsy_dmd_sets_env_and_path_and_warns_about_sandbox_on_windows(
    context, harness, parser, monkeypatch, tmp_path, os_name
):
    monkeypatch.setattr(mach_commands.mozinfo, "os", os_name)
    bin_dir = str(tmp_path / "bin")
    binary = os.path.join(bin_dir, "firefox")

    mach_commands.run_awsy(context, [], binary=binary, **make_kwargs(dmd=True))

    assert os.environ["DMD"] == "1"
    args = harness_args(harness)
    runtime = read_runtime_testvars(args["testvars"][-1])
    assert runtime["dmd"] is True
    if os_name == "mac":
        assert sys.path[-1] == os.path.join(bin_dir, "../Resources/")
    else:
        assert sys.path[-1] == bin_dir
    if os_name == "win":
        context.log.assert_called_once()
        assert "security.sandbox.content.level" in context.log.call_args[0][3]
    else:
        context.log.assert_not_called()


def test_run_awsy_dmd_keeps_existing_env_and_path_entry(
    context, harness, parser, monkeypatch
):
    monkeypatch.setattr(mach_commands.mozinfo, "os", "linux")
    monkeypatch.setenv("DMD", "custom")
    sys.path.append("/bin")
    path_len = len(sys.path)

    mach_commands.run_awsy(context, [], binary="/bin/firefox", **make_kwargs(dmd=True))

    assert os.environ["DMD"] == "custom"
    assert len(sys.path) == path_len


@pytest.fixture
def run_awsy(monkeypatch):
    run_awsy = mock.Mock(return_value=0)
    monkeypatch.setattr(mach_commands, "run_awsy", run_awsy)
    return run_awsy


@pytest.fixture
def is_firefox(monkeypatch):
    is_firefox = mock.Mock(return_value=True)
    monkeypatch.setattr(mach_commands.conditions, "is_firefox", is_firefox)
    return is_firefox


def test_run_awsy_test_looks_up_binary_and_prefers_test_objects(
    context, run_awsy, is_firefox
):
    context.get_binary_path.return_value = "/obj/firefox"

    ret = mach_commands.run_awsy_test(
        context, ["a.py"], test_objects=[{"file_relpath": "b.py"}]
    )

    assert ret == 0
    context.get_binary_path.assert_called_once_with("app")
    run_awsy.assert_called_once_with(
        context, ["b.py"], logger_name="Awsy Tests", binary="/obj/firefox"
    )


def test_run_awsy_test_uses_explicit_binary_without_lookup(
    context, run_awsy, is_firefox
):
    mach_commands.run_awsy_test(context, ["a.py"], binary="/my/firefox")
    context.get_binary_path.assert_not_called()
    run_awsy.assert_called_once_with(
        context, ["a.py"], logger_name="Awsy Tests", binary="/my/firefox"
    )


def test_run_awsy_test_skips_binary_lookup_when_not_firefox(
    context, run_awsy, is_firefox
):
    is_firefox.return_value = False
    mach_commands.run_awsy_test(context, [])
    context.get_binary_path.assert_not_called()
    run_awsy.assert_called_once_with(context, [], logger_name="Awsy Tests")


def test_run_awsy_test_returns_1_when_binary_not_found(context, run_awsy, is_firefox):
    context.get_binary_path.side_effect = BinaryNotFoundException("/obj/firefox")

    assert mach_commands.run_awsy_test(context, []) == 1

    run_awsy.assert_not_called()
    assert context.log.call_count == 2
    assert "/obj/firefox" in context.log.call_args_list[0][0][2]["error"]


def test_awsy_test_command_registers_name_group_and_arguments():
    command = mach_commands.run_awsy_test._mach_command
    assert command.name == "awsy-test"
    assert command.argument_group_names == ["AWSY"]
    dests = {kw["dest"] for _, kw in command.arguments}
    assert {"webRootDir", "resultsDir", "maxTabs", "dmd", "base"} <= dests
    help_ = next(kw["help"] for _, kw in command.arguments if kw["dest"] == "maxTabs")
    assert str(MAX_TABS) in help_


if __name__ == "__main__":
    mozunit.main()
