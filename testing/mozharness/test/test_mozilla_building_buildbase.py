import json
import os
import tempfile
import unittest
from unittest import mock

import mozunit
from mozharness.base.log import LogMixin
from mozharness.base.script import ScriptMixin
from mozharness.mozilla.building.buildbase import (
    BuildScript,
    MozconfigPathError,
    get_mozconfig_path,
)


class FakeLogger:
    def log_message(self, *args, **kwargs):
        pass


class FakeScriptMixin(LogMixin, ScriptMixin):
    def __init__(self):
        self.script_obj = self
        self.log_obj = FakeLogger()


class TestMozconfigPath(unittest.TestCase):
    """
    Tests for :func:`get_mozconfig_path`.
    """

    def test_path(self):
        """
        Passing just ``src_mozconfig`` gives that file in ``abs_src_dir``.
        """
        script = FakeScriptMixin()

        abs_src_path = get_mozconfig_path(
            script,
            config={"src_mozconfig": "path/to/mozconfig"},
            dirs={"abs_src_dir": "/src"},
        )
        self.assertEqual(abs_src_path, "/src/path/to/mozconfig")

    def test_composite(self):
        """
        Passing ``app_name``, ``mozconfig_platform``, and ``mozconfig_variant``
        find the file in the ``config/mozconfigs`` subdirectory of that app
        directory.
        """
        script = FakeScriptMixin()

        config = {
            "app_name": "the-app",
            "mozconfig_variant": "variant",
            "mozconfig_platform": "platform9000",
        }
        abs_src_path = get_mozconfig_path(
            script,
            config=config,
            dirs={"abs_src_dir": "/src"},
        )
        self.assertEqual(
            abs_src_path,
            "/src/the-app/config/mozconfigs/platform9000/variant",
        )

    def test_errors(self):
        script = FakeScriptMixin()

        configs = [
            # Not specifying any parts of a mozconfig path
            {},
            # Specifying src_mozconfig with some or all of a composite
            # mozconfig path
            {
                "src_mozconfig": "path",
                "app_name": "app",
                "mozconfig_platform": "platform",
                "mozconfig_variant": "variant",
            },
            {
                "src_mozconfig": "path",
                "mozconfig_platform": "platform",
                "mozconfig_variant": "variant",
            },
            {
                "src_mozconfig": "path",
                "app_name": "app",
                "mozconfig_variant": "variant",
            },
            {
                "src_mozconfig": "path",
                "app_name": "app",
                "mozconfig_platform": "platform",
            },
            # Specifying only some parts of a compsite mozconfig path
            {"mozconfig_platform": "platform", "mozconfig_variant": "variant"},
            {"app_name": "app", "mozconfig_variant": "variant"},
            {"app_name": "app", "mozconfig_platform": "platform"},
            {"app_name": "app"},
            {"mozconfig_variant": "variant"},
            {"mozconfig_platform": "platform"},
        ]

        for config in configs:
            with self.assertRaises(MozconfigPathError):
                get_mozconfig_path(script, config=config, dirs={})


class TestLoadBuildResources(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.obj_dir = os.path.join(tmp.name, "obj-build")
        self.upload_path = os.path.join(tmp.name, "upload")
        os.makedirs(os.path.join(self.obj_dir, ".mozbuild"))
        os.makedirs(self.upload_path)

        self.script = mock.Mock(
            config={
                "profile_build_resources_path": "%(upload_path)s/profile_build_resources.json"
            }
        )
        self.script.query_abs_dirs.return_value = {
            "abs_obj_dir": self.obj_dir,
            "upload_path": self.upload_path,
        }
        self.script.perfherder_resource_options.return_value = ["opt"]
        self.script._load_build_times = BuildScript._load_build_times.__get__(
            self.script
        )

    def _write(self, path, data):
        with open(path, "w") as fh:
            json.dump(data, fh)

    def _load(self):
        with mock.patch.dict(os.environ, {"USE_SCCACHE": "0"}):
            return BuildScript._load_build_resources(self.script)

    def _check(self, data):
        self.assertEqual(data["name"], "build times")
        self.assertEqual(data["value"], 12.5)
        self.assertEqual(data["extraOptions"], ["opt"])
        self.assertTrue(data["shouldAlert"])
        self.assertEqual(
            data["subtests"],
            [
                {"name": "configure", "value": 2.5},
                {"name": "compile", "value": 9.75},
            ],
        )

    def test_build_times(self):
        self._write(
            os.path.join(self.obj_dir, ".mozbuild", "build_times.json"),
            {"duration": 12.5, "phases": {"configure": 2.5, "compile": 9.75}},
        )
        self._check(self._load())

    def test_profile(self):
        self._write(
            os.path.join(self.upload_path, "profile_build_resources.json"),
            {
                "meta": {"profilingEndTime": 12500},
                "threads": [
                    {
                        "markers": {
                            "data": [
                                {"type": "Phase", "phase": "configure"},
                                {"type": "Text", "text": "ignored"},
                                {"type": "Phase", "phase": "compile"},
                            ],
                            "startTime": [0, 100, 2500],
                            "endTime": [2500, 200, 12250],
                        }
                    }
                ],
            },
        )
        self._check(self._load())

    def test_invalid_profile(self):
        self._write(os.path.join(self.upload_path, "profile_build_resources.json"), {})
        self.assertIsNone(self._load())

    def test_missing(self):
        self.assertIsNone(self._load())


if __name__ == "__main__":
    mozunit.main()
