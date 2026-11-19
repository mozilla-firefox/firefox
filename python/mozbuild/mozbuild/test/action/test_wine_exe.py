# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import unittest
from unittest import mock

import mozunit

from mozbuild.action import wine_exe


class TestWineExe(unittest.TestCase):
    def _run(self, argv, cwd, status=0):
        with mock.patch.object(
            wine_exe, "buildconfig", substs={"PATH": "/a:/b"}
        ), mock.patch.object(
            wine_exe.os, "getcwd", return_value=cwd
        ), mock.patch.object(wine_exe.subprocess, "call", return_value=status) as call:
            return wine_exe.main(argv), call

    def test_wine_exe(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = os.path.join(tmp, "src", "a.asm")
            os.makedirs(os.path.dirname(source))
            with open(source, "w"):
                pass
            status, call = self._run(
                ["wine", "ml64.exe", "-Foa.obj", "/c", source],
                os.path.join(tmp, "obj"),
            )
        self.assertEqual(status, 0)
        call.assert_called_once_with(
            ["wine", "ml64.exe", "-Foa.obj", "/c", os.path.join("..", "src", "a.asm")],
            env=mock.ANY,
        )
        self.assertEqual(call.call_args.kwargs["env"]["WINEPATH"], "/a;/b")

    def test_wine_exe_failure(self):
        status, _ = self._run(["wine", "ml64.exe"], "/obj", status=3)
        self.assertEqual(status, 3)


if __name__ == "__main__":
    mozunit.main()
