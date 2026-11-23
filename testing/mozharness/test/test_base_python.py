import os
import unittest
from unittest.mock import Mock, patch

import mozunit
import pytest
from mozharness.base import python

here = os.path.dirname(os.path.abspath(__file__))


class TestVirtualenvMixin(unittest.TestCase):
    def test_package_versions(self):
        example = os.path.join(here, "pip-freeze.example.txt")
        output = open(example).read()
        mixin = python.VirtualenvMixin()
        packages = mixin.package_versions(output)

        # from the file
        expected = {
            "MakeItSo": "0.2.6",
            "PyYAML": "3.10",
            "Tempita": "0.5.1",
            "WebOb": "1.2b3",
            "coverage": "3.5.1",
            "logilab-astng": "0.23.1",
            "logilab-common": "0.57.1",
            "mozdevice": "0.2",
            "mozhttpd": "0.3",
            "mozinfo": "0.3.3",
            "nose": "1.1.2",
            "pyflakes": "0.5.0",
            "pylint": "0.25.1",
            "virtualenv": "1.7.1.2",
            "wsgiref": "0.1.2",
        }

        self.assertEqual(packages, expected)


@pytest.mark.parametrize("uv", [None, "/tools/uv"])
@pytest.mark.parametrize("offline", [False, True])
def test_install_module_package_sources(tmp_path, uv, offline):
    config = {
        options["dest"]: options["default"]
        for _, options in python.virtualenv_config_options
        if "default" in options
    }
    if offline:
        config.update(pip_index=False, find_links=[str(tmp_path)])
    harness = Mock(config=config)
    harness.query_python_constraints.return_value = [str(tmp_path / "constraints.txt")]
    harness.query_abs_dirs.return_value = {"abs_work_dir": str(tmp_path)}
    harness.query_env.return_value = {}
    harness.query_virtualenv_path.return_value = str(tmp_path / "venv")
    harness.query_python_path.return_value = str(tmp_path / "venv/bin/python")
    with patch.object(python, "get_uv_executable", return_value=uv), patch(
        "urllib.request.urlopen", side_effect=AssertionError("Unexpected source probe")
    ):
        python.VirtualenvMixin.install_module(harness, module="six==1.16.0")
    command = harness.retry.call_args.kwargs["args"][0]
    assert ("--no-index" in command) == offline
    assert ("--find-links" in command) == offline
    if offline:
        assert command[command.index("--find-links") + 1] == str(tmp_path)
    assert not any("pypi.pub.build.mozilla.org" in arg for arg in command)
    assert command[command.index("--constraint") + 1] == str(
        tmp_path / "constraints.txt"
    )
    assert command[-1] == "six==1.16.0"


@pytest.mark.parametrize("uv", [None, "/tools/uv"])
def test_create_virtualenv_seed_wheels(tmp_path, uv):
    wheels = tmp_path / "src" / "third_party" / "python" / "_venv" / "wheels"
    wheels.mkdir(parents=True)
    for name in ("pip-26.3", "setuptools-82.0.1", "wheel-0.45.1"):
        (wheels / f"{name}-py3-none-any.whl").touch()
    harness = Mock(config={"virtualenv_path": "venv"}, _virtualenv_modules=[])
    harness.query_abs_dirs.return_value = {
        "abs_work_dir": str(tmp_path),
        "abs_src_dir": str(tmp_path / "src"),
    }
    harness.query_virtualenv_path.return_value = str(tmp_path / "venv")
    harness.query_python_path.return_value = str(tmp_path / "venv/bin/python")
    harness._is_windows.return_value = False
    harness._is_darwin.return_value = False
    with patch.object(python, "get_uv_executable", return_value=uv):
        python.VirtualenvMixin.create_virtualenv(harness)
    seed = [
        call.args[0]
        for call in harness.run_command.call_args_list
        if "--only-binary" in call.args[0]
    ][0]
    seeded = [os.path.basename(arg) for arg in seed if arg.endswith(".whl")]
    expected = ["setuptools-82.0.1-py3-none-any.whl", "wheel-0.45.1-py3-none-any.whl"]
    if not uv:
        expected.insert(0, "pip-26.3-py3-none-any.whl")
    assert seeded == expected


@pytest.mark.parametrize(
    "filename", ["requirements-mozbase.txt", "requirements-marionette.txt"]
)
def test_testing_constraints_path(tmp_path, filename):
    from mozharness.mozilla.testing.testbase import TestingMixin

    harness = TestingMixin.__new__(TestingMixin)
    harness.python_constraints = filename
    harness.query_abs_dirs = Mock(return_value={"abs_test_install_dir": str(tmp_path)})
    assert harness.query_python_constraints() == [str(tmp_path / "config" / filename)]
    assert python.VirtualenvMixin().query_python_constraints() == []


if __name__ == "__main__":
    mozunit.main()
