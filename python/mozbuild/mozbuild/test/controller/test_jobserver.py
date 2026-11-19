# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import mozunit

from mozbuild.controller import jobserver
from mozbuild.controller.building import BuildDriver


class TestJobserver(unittest.TestCase):
    def _make_version(self, stdout):
        with mock.patch.object(
            jobserver.subprocess, "check_output", return_value=stdout
        ):
            return jobserver.make_version("make")

    def test_make_version(self):
        self.assertEqual(self._make_version("GNU Make 4.4.1\nBuilt for x\n"), (4, 4))

    def test_make_version_not_gnu(self):
        self.assertIsNone(self._make_version("bmake 1.0\n"))

    def test_make_version_unparsable(self):
        self.assertIsNone(self._make_version("GNU Make 4.x\n"))

    def test_make_version_empty(self):
        self.assertIsNone(self._make_version(""))

    def test_make_version_missing(self):
        with mock.patch.object(
            jobserver.subprocess, "check_output", side_effect=FileNotFoundError
        ):
            self.assertIsNone(jobserver.make_version("make"))

    def _jobserver(self, jobs, version=(4, 4), makeflags=""):
        with mock.patch.object(
            jobserver, "make_version", return_value=version
        ), mock.patch.dict(os.environ, {"MAKEFLAGS": makeflags}):
            with jobserver.jobserver(jobs, "make") as flags:
                return flags, self._tokens(flags)

    def _tokens(self, flags):
        if "--jobserver-auth=" not in flags:
            return None
        auth = flags.split("--jobserver-auth=")[1].split()[0]
        if sys.platform == "win32":
            import ctypes
            from ctypes import wintypes

            kernel32 = ctypes.WinDLL("kernel32")
            kernel32.OpenSemaphoreW.restype = wintypes.HANDLE
            kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
            kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
            handle = kernel32.OpenSemaphoreW(0x1F0003, False, auth)
            self.assertTrue(handle)
            try:
                tokens = 0
                while kernel32.WaitForSingleObject(handle, 0) == 0:
                    tokens += 1
                return tokens
            finally:
                kernel32.CloseHandle(handle)
        if auth.startswith("fifo:"):
            fd = os.open(auth[len("fifo:") :], os.O_RDONLY | os.O_NONBLOCK)
            try:
                return len(os.read(fd, 64))
            finally:
                os.close(fd)
        read, _ = (int(fd) for fd in auth.split(","))
        self.assertTrue(os.get_inheritable(read))
        return len(os.read(read, 64))

    def test_jobserver(self):
        flags, tokens = self._jobserver(4)
        self.assertEqual(tokens, 3)
        if sys.platform == "win32":
            self.assertTrue(flags.startswith("-j4 --jobserver-auth=gmake_semaphore_"))
        else:
            self.assertTrue(flags.startswith("-j4 --jobserver-auth=fifo:"))
            self.assertFalse(os.path.exists(flags.split("fifo:")[1]))

    @unittest.skipUnless(sys.platform == "win32", "Only Windows uses a semaphore")
    def test_jobserver_semaphore_unique(self):
        self.assertNotEqual(self._jobserver(4)[0], self._jobserver(4)[0])

    @unittest.skipIf(sys.platform == "win32", "Windows uses a semaphore")
    def test_jobserver_pipe(self):
        flags, tokens = self._jobserver(4, version=(4, 3))
        self.assertRegex(flags, r"^-j4 --jobserver-auth=\d+,\d+$")
        self.assertEqual(tokens, 3)

    def test_jobserver_keeps_makeflags(self):
        flags, _ = self._jobserver(4, makeflags="--no-print-directory")
        self.assertTrue(flags.startswith("--no-print-directory -j4 --jobserver-auth="))

    def test_jobserver_keeps_assignments(self):
        flags, _ = self._jobserver(4, makeflags=" -- FOO=1")
        self.assertRegex(flags, r"^-j4 --jobserver-auth=\S+ -- FOO=1$")

    def test_jobserver_single_job(self):
        self.assertEqual(self._jobserver(1), ("-j1", None))

    def test_jobserver_old_make(self):
        self.assertEqual(self._jobserver(4, version=(4, 1)), ("-j4", None))

    def test_jobserver_not_gnu_make(self):
        self.assertEqual(self._jobserver(4, version=None), ("-j4", None))

    def test_jobserver_setup_failure(self):
        with mock.patch.object(
            jobserver, "_semaphore", side_effect=OSError
        ), mock.patch.object(jobserver, "_fifo", side_effect=OSError):
            self.assertEqual(self._jobserver(4), ("-j4", None))

    def test_jobserver_fifo_whitespace(self):
        with tempfile.TemporaryDirectory(prefix="job server") as tmp:
            with mock.patch.object(jobserver.tempfile, "tempdir", tmp):
                with self.assertRaisesRegex(OSError, "contains whitespace"):
                    with jobserver._fifo(4):
                        pass
            self.assertEqual(os.listdir(tmp), [])

    def test_jobserver_make(self):
        make = shutil.which("mozmake") or shutil.which("make")
        if not make or (jobserver.make_version(make) or (0,)) < (4, 2):
            self.skipTest("GNU Make 4.2 or newer is not available")
        with tempfile.TemporaryDirectory() as tmp:
            makefile = os.path.join(tmp, "Makefile")
            with open(makefile, "w", encoding="utf-8") as fh:
                fh.write(
                    "define SUBMAKE\n"
                    "+@$(MAKE) -f $(firstword $(MAKEFILE_LIST)) $(1)\n"
                    "\n"
                    "endef\n"
                    "all:\n"
                    "\t$(foreach target,sub,$(call SUBMAKE,$(target)))\n"
                    "sub:\n"
                    "\t@echo $(MAKEFLAGS)\n"
                )
            with mock.patch.dict(os.environ, {"MAKEFLAGS": ""}):
                with jobserver.jobserver(4, make) as flags:
                    result = subprocess.run(
                        [make, "-f", makefile],
                        env={**os.environ, "MAKEFLAGS": flags},
                        capture_output=True,
                        text=True,
                        close_fds=False,
                        check=False,
                    )
        self.assertIn(flags.split(" ")[1], result.stdout)
        self.assertEqual(result.stderr, "")

    def test_jobserver_inherited(self):
        for makeflags in (
            "s -j8 --jobserver-auth=3,4",
            "s -j 8 --jobserver-auth=3,4",
            "s --jobs=8 --jobserver-auth=3,4",
            "s -j --jobserver-fds=3,4",
        ):
            flags, tokens = self._jobserver(4, makeflags=makeflags)
            self.assertRegex(flags, r"^s -j4 --jobserver-auth=\S+$")
            self.assertNotIn("3,4", flags)
            self.assertEqual(tokens, 3)


class TestRunClientMk(unittest.TestCase):
    def test_run_client_mk_drops_job_flags(self):
        with tempfile.TemporaryDirectory() as tmp:
            driver = mock.Mock(
                topsrcdir="/src",
                topobjdir=tmp,
                mozconfig={
                    "make_extra": [],
                    "make_flags": ["-j8", "--output-sync=target", "-j", "4"],
                    "path": None,
                },
            )
            BuildDriver._run_client_mk(driver)
            with open(os.path.join(tmp, ".mozconfig-client-mk")) as fh:
                lines = fh.read().splitlines()
        self.assertEqual(lines[0], "MOZ_MAKE_FLAGS=--output-sync=target")


if __name__ == "__main__":
    mozunit.main()
