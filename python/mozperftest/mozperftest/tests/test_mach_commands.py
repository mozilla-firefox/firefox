#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from unittest import mock

import mozunit
from mach.registrar import Registrar

Registrar.categories = {"testing": []}
Registrar.commands_by_category = {"testing": set()}

from mozbuild.base import MachCommandBase  # noqa

import mozperftest.mach_commands  # noqa
from mozperftest.environment import MachEnvironment  # noqa
from mozperftest.tests.support import EXAMPLE_TEST, ROOT, running_on_try  # noqa
from mozperftest.utils import silence, temporary_env  # noqa

ITERATION_HOOKS = Path(__file__).parent / "data" / "hooks_iteration.py"
STATE_HOOKS = Path(__file__).parent / "data" / "hooks_state.py"
SCRIPT_PACKAGES_HOOKS = Path(__file__).parent / "data" / "hooks_script_packages.py"


@contextmanager
def _shell_test_declaring_hooks(hooks):
    """Yields a custom-script test that points at its own hooks file."""
    with tempfile.TemporaryDirectory() as tmp_dir:
        script = Path(tmp_dir, "perftest_hooked.sh")
        options = json.dumps({"default": {"hooks": str(hooks)}})
        script.write_text(
            "# Name: hooked-script-test\n"
            "# Owner: Perftest Team\n"
            "# Description: Custom script test that brings its own hooks.\n"
            f"# Options: {options}\n"
        )
        yield script


class _TestMachEnvironment(MachEnvironment):
    def __init__(self, mach_cmd, flavor="desktop-browser", hooks=None, **kwargs):
        MachEnvironment.__init__(self, mach_cmd, flavor, hooks, **kwargs)
        self.runs = 0

    def run(self, metadata):
        self.runs += 1
        return metadata

    def __enter__(self):
        pass

    def __exit__(self, type, value, traceback):
        pass


@contextmanager
def _get_command(command=mozperftest.mach_commands.run_perftest):
    from mozbuild.base import MozbuildObject

    from mozperftest.argparser import PerftestArgumentParser

    config = MozbuildObject.from_environment()

    class context:
        topdir = config.topobjdir
        cwd = os.getcwd()
        settings = {}
        log_manager = mock.Mock()
        state_dir = tempfile.mkdtemp()

    # used to make arguments passed by the test as
    # being set by the user.
    def _run_perftest(func):
        def _run(command_context, **kwargs):
            parser.set_by_user = list(kwargs.keys())
            return func(command_context, **kwargs)

        return _run

    try:
        command_context = MachCommandBase(context())

        if command == mozperftest.mach_commands.run_perftest:
            parser = PerftestArgumentParser()
            command = _run_perftest(command)

        with mock.patch("mozperftest.mach_commands.get_parser", new=lambda: parser):
            yield command, command_context
    finally:
        shutil.rmtree(context.state_dir)


@contextmanager
def _get_tools_command(tool="side-by-side"):
    from mozbuild.base import MozbuildObject

    config = MozbuildObject.from_environment()

    class context:
        topdir = config.topobjdir
        cwd = os.getcwd()
        settings = {}
        log_manager = mock.Mock()
        state_dir = tempfile.mkdtemp()

    # used to make arguments passed by the test as
    # being set by the user.
    def _run_tool(func):
        def _run(command_context, **kwargs):
            parser.set_by_user = list(kwargs.keys())
            return func(command_context, **kwargs)

        return _run

    try:
        command_context = MachCommandBase(context())

        command = _run_tool(mozperftest.mach_commands.run_side_by_side)
        parser = mozperftest.mach_commands.get_perftest_tools_parser(tool)

        with mock.patch(
            "mozperftest.mach_commands.get_perftest_tools_parser", new=lambda: parser
        ):
            yield command, command_context
    finally:
        shutil.rmtree(context.state_dir)


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
def test_command(mocked_func):
    with _get_command() as (cmd, command_context), silence(command_context):
        cmd(command_context, tests=[EXAMPLE_TEST], flavor="desktop-browser")


@mock.patch("mozperftest.MachEnvironment")
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
def test_command_iterations(venv, env):
    kwargs = {
        "tests": [EXAMPLE_TEST],
        "hooks": ITERATION_HOOKS,
        "flavor": "desktop-browser",
    }
    with _get_command() as (cmd, command_context), silence(command_context):
        cmd(command_context, **kwargs)
        # the hook changes the iteration value to 5.
        # each iteration generates 5 calls, so we want to see 25
        assert len(env.mock_calls) == 25


@mock.patch("mozperftest.MachEnvironment")
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
def test_script_declared_hooks_can_install_packages(venv, env):
    with _shell_test_declaring_hooks(
        SCRIPT_PACKAGES_HOOKS
    ) as script, tempfile.TemporaryDirectory() as marker_dir:
        marker = Path(marker_dir, "marker")
        kwargs = {"tests": [str(script)], "flavor": "custom-script"}
        with _get_command() as (cmd, command_context), silence(command_context):
            with temporary_env(HOOK_MARKER=str(marker)):
                cmd(command_context, **kwargs)

        # the test's own hooks file ran before_iterations, with a virtualenv
        assert marker.read_text() == "True"


@mock.patch("mozperftest.MachEnvironment")
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
def test_hooks_state(venv, env):
    kwargs = {
        "tests": [EXAMPLE_TEST],
        "hooks": STATE_HOOKS,
        "flavor": "desktop-browser",
    }
    with _get_command() as (cmd, command_context), silence(command_context):
        cmd(command_context, **kwargs)


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
def test_doc_flavor(mocked_func):
    with _get_command() as (cmd, command_context), silence(command_context):
        cmd(command_context, tests=[EXAMPLE_TEST], flavor="doc")


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.run_script")
def test_test_runner(*mocked):
    from mozperftest.mach_commands import run_tests

    with running_on_try(False), _get_command(run_tests) as (cmd, command_context):
        cmd(command_context, tests=[EXAMPLE_TEST], verbose=True)


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.run_python_script")
def test_test_runner_on_try(*mocked):
    from mozperftest.mach_commands import run_tests

    # simulating on try to run the paths parser
    with running_on_try(), _get_command(run_tests) as (cmd, command_context):
        cmd(command_context, tests=[EXAMPLE_TEST])


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.run_script")
def test_test_runner_coverage(*mocked):
    from mozperftest.mach_commands import run_tests

    # simulating with coverage not installed
    with running_on_try(False), _get_command(run_tests) as (cmd, command_context):
        old = list(sys.meta_path)
        sys.meta_path = []
        try:
            cmd(command_context, tests=[EXAMPLE_TEST])
        finally:
            sys.meta_path = old


@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.checkout_python_script")
def test_test_runner_low_coverage(checkout, *mocked):
    from mozperftest.mach_commands import run_tests

    checkout.side_effect = lambda venv, module, args, **kw: args != ["report"]
    with running_on_try(False), _get_command(run_tests) as (cmd, command_context):
        with mock.patch.object(command_context, "log") as log:
            assert cmd(command_context, tests=[], raptor=True) == 1

    assert "Coverage is too low" in log.call_args[0][3]


@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.checkout_python_script", return_value=False)
def test_test_runner_failing_tests(*mocked):
    from mozperftest.mach_commands import run_tests

    with running_on_try(False), _get_command(run_tests) as (cmd, command_context):
        with mock.patch.object(command_context, "log") as log:
            assert cmd(command_context, tests=[EXAMPLE_TEST], skip_linters=True) == 1

    assert "Some tests failed" in log.call_args[0][3]


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.runner.run_tests")
def test_help_nothing_selected(*kwargs):
    from mozperftest.runner import run_tests

    with _get_command() as (cmd, command_context), silence():
        cmd(command_context, tests=[])

    run_tests.assert_not_called()


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.runner.run_tests")
def test_help_help_selected(*kwargs):
    from mozperftest.runner import run_tests

    with _get_command() as (cmd, command_context), silence():
        cmd(command_context, tests=["help"])

    run_tests.assert_not_called()


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.run_python_script")
@mock.patch("mozperftest.utils.install_package")
def test_side_by_side(mock1, mock2, mock3, patched_mozperftest_tools):
    with mock.patch(
        "mozperftest.utils.create_path", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._create_artifacts_dir", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._save_params", return_value="fake_path"
    ) as _:
        with _get_tools_command() as (cmd, command_context), silence(command_context):
            cmd(command_context)
    patched_mozperftest_tools.run.assert_called()


@mock.patch("mozperftest.MachEnvironment", new=_TestMachEnvironment)
@mock.patch("mozbuild.base.MachCommandBase.activate_virtualenv")
@mock.patch("mozperftest.utils.run_python_script")
@mock.patch("mozperftest.utils.install_package")
def test_change_detector(mock1, mock2, mock3, patched_mozperftest_tools):
    with mock.patch(
        "mozperftest.utils.create_path", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._create_artifacts_dir", return_value="fake_path"
    ) as _, mock.patch(
        "mozperftest.runner._save_params", return_value="fake_path"
    ) as _:
        with _get_tools_command(tool="change-detector") as (
            cmd,
            command_context,
        ), silence(command_context):
            cmd(command_context)
    patched_mozperftest_tools.run.assert_called()


if __name__ == "__main__":
    mozunit.main()
