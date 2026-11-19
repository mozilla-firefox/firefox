# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import sys
import tempfile
import unittest
from io import StringIO
from unittest import mock

import mozpack.path as mozpath
from buildconfig import topobjdir, topsrcdir
from mach.logging import LoggingManager
from mozfile.mozfile import NamedTemporaryFile
from mozunit import main

from mozbuild.backend.configenvironment import ConfigEnvironment
from mozbuild.base import (
    BadEnvironmentException,
    MachCommandBase,
    MozbuildObject,
    PathArgument,
    split_job_flags,
)
from mozbuild.test.common import prepare_tmp_topsrcdir
from mozbuild.util import cpu_count

curdir = os.path.dirname(__file__)
log_manager = LoggingManager()


class TestMozbuildObject(unittest.TestCase):
    def setUp(self):
        self._old_cwd = os.getcwd()
        self._old_env = dict(os.environ)
        os.environ.pop("MOZCONFIG", None)
        os.environ.pop("MOZ_OBJDIR", None)

    def tearDown(self):
        os.chdir(self._old_cwd)
        os.environ.clear()
        os.environ.update(self._old_env)

    def get_base(self, topobjdir=None):
        return MozbuildObject(topsrcdir, None, log_manager, topobjdir=topobjdir)

    def test_objdir_config_guess(self):
        base = self.get_base()

        with NamedTemporaryFile(mode="wt") as mozconfig:
            os.environ["MOZCONFIG"] = mozconfig.name

            self.assertIsNotNone(base.topobjdir)
            self.assertEqual(len(base.topobjdir.split()), 1)
            config_guess = base.resolve_config_guess()
            self.assertTrue(base.topobjdir.endswith(config_guess))
            self.assertTrue(os.path.isabs(base.topobjdir))
            self.assertTrue(base.topobjdir.startswith(base.topsrcdir))

    def test_objdir_trailing_slash(self):
        """Trailing slashes in topobjdir should be removed."""
        base = self.get_base()

        with NamedTemporaryFile(mode="wt") as mozconfig:
            mozconfig.write("mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/foo/")
            mozconfig.flush()
            os.environ["MOZCONFIG"] = mozconfig.name

            self.assertEqual(base.topobjdir, mozpath.join(base.topsrcdir, "foo"))
            self.assertTrue(base.topobjdir.endswith("foo"))

    def test_objdir_config_status(self):
        """Ensure @CONFIG_GUESS@ is handled when loading mozconfig."""
        base = self.get_base()
        guess = base.resolve_config_guess()

        # There may be symlinks involved, so we use real paths to ensure
        # path consistency.
        d = os.path.realpath(tempfile.mkdtemp())
        try:
            mozconfig = os.path.join(d, "mozconfig")
            with open(mozconfig, "w") as fh:
                fh.write("mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/foo/@CONFIG_GUESS@")
            print("Wrote mozconfig %s" % mozconfig)

            topobjdir = os.path.join(d, "foo", guess)
            os.makedirs(topobjdir)

            # Create a fake topsrcdir.
            prepare_tmp_topsrcdir(d)

            mozinfo = os.path.join(topobjdir, "mozinfo.json")
            with open(mozinfo, "w") as fh:
                json.dump(
                    dict(
                        topsrcdir=d,
                        mozconfig=mozconfig,
                    ),
                    fh,
                )

            os.environ["MOZCONFIG"] = mozconfig
            os.chdir(topobjdir)

            obj = MozbuildObject.from_environment(detect_virtualenv_mozinfo=False)

            self.assertEqual(obj.topobjdir, mozpath.normsep(topobjdir))
        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def test_relative_objdir(self):
        """Relative defined objdirs are loaded properly."""
        d = os.path.realpath(tempfile.mkdtemp())
        try:
            mozconfig = os.path.join(d, "mozconfig")
            with open(mozconfig, "w") as fh:
                fh.write("mk_add_options MOZ_OBJDIR=./objdir")

            topobjdir = mozpath.join(d, "objdir")
            os.mkdir(topobjdir)

            mozinfo = os.path.join(topobjdir, "mozinfo.json")
            with open(mozinfo, "w") as fh:
                json.dump(
                    dict(
                        topsrcdir=d,
                        mozconfig=mozconfig,
                    ),
                    fh,
                )

            os.environ["MOZCONFIG"] = mozconfig
            child = os.path.join(topobjdir, "foo", "bar")
            os.makedirs(child)
            os.chdir(child)

            obj = MozbuildObject.from_environment(detect_virtualenv_mozinfo=False)

            self.assertEqual(obj.topobjdir, topobjdir)

        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    @unittest.skipIf(
        not hasattr(os, "symlink") or os.name == "nt", "symlinks not available."
    )
    def test_symlink_objdir(self):
        """Objdir that is a symlink is loaded properly."""
        d = os.path.realpath(tempfile.mkdtemp())
        try:
            topobjdir_real = os.path.join(d, "objdir")
            topobjdir_link = os.path.join(d, "objlink")

            os.mkdir(topobjdir_real)
            os.symlink(topobjdir_real, topobjdir_link)

            mozconfig = os.path.join(d, "mozconfig")
            with open(mozconfig, "w") as fh:
                fh.write("mk_add_options MOZ_OBJDIR=%s" % topobjdir_link)

            mozinfo = os.path.join(topobjdir_real, "mozinfo.json")
            with open(mozinfo, "w") as fh:
                json.dump(
                    dict(
                        topsrcdir=d,
                        mozconfig=mozconfig,
                    ),
                    fh,
                )

            os.chdir(topobjdir_link)
            obj = MozbuildObject.from_environment(detect_virtualenv_mozinfo=False)
            self.assertEqual(obj.topobjdir, topobjdir_real)

            os.chdir(topobjdir_real)
            obj = MozbuildObject.from_environment(detect_virtualenv_mozinfo=False)
            self.assertEqual(obj.topobjdir, topobjdir_real)

        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def _mach_context_in_tmpdir(self, d, log_manager=None, handler=None):
        """Lay out a throwaway srcdir/objdir under d and return a mach context."""
        topobjdir = os.path.join(d, "objdir")
        os.makedirs(topobjdir)

        topsrcdir = os.path.join(d, "srcdir")
        prepare_tmp_topsrcdir(topsrcdir)

        mozinfo = os.path.join(topobjdir, "mozinfo.json")
        with open(mozinfo, "w") as fh:
            json.dump(
                dict(
                    topsrcdir=topsrcdir,
                ),
                fh,
            )

        os.chdir(topobjdir)

        class MockMachContext:
            pass

        context = MockMachContext()
        context.cwd = topobjdir
        context.topdir = topsrcdir
        context.settings = None
        context.log_manager = log_manager
        context.handler = handler
        context.detect_virtualenv_mozinfo = False

        return context

    def test_mach_command_base_inside_objdir(self):
        """Ensure a MachCommandBase constructed from inside the objdir works."""

        d = os.path.realpath(tempfile.mkdtemp())

        try:
            context = self._mach_context_in_tmpdir(d)

            o = MachCommandBase(context, None)

            self.assertEqual(o.topobjdir, mozpath.normsep(context.cwd))
            self.assertEqual(o.topsrcdir, mozpath.normsep(context.topdir))

        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def test_auto_log_without_tty_for_coding_agent(self):
        """A coding agent gets a log even though its stdout is not a tty."""

        mock.patch("mozbuild.base.os.isatty", lambda _: False).start()
        mock.patch("mozbuild.base.is_running_under_coding_agent", lambda: True).start()
        self.addCleanup(mock.patch.stopall)

        d = os.path.realpath(tempfile.mkdtemp())
        log_manager = LoggingManager()

        try:
            handler = mock.Mock()
            handler.name = "dummy-command"
            context = self._mach_context_in_tmpdir(d, log_manager, handler)

            obj = MachCommandBase(context, None)

            self.assertIsNotNone(obj.logfile)
            self.assertTrue(obj.logfile.endswith(".log"))
            self.assertTrue(os.path.exists(obj.logfile))

        finally:
            # StreamHandler.close() leaves the stream open; Windows won't rmtree it.
            for log_handler in log_manager.file_handlers:
                log_handler.stream.close()
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def test_objdir_is_srcdir_rejected(self):
        """Ensure the srcdir configurations are rejected."""
        d = os.path.realpath(tempfile.mkdtemp())

        try:
            # The easiest way to do this is to create a mozinfo.json with data
            # that will never happen.
            mozinfo = os.path.join(d, "mozinfo.json")
            with open(mozinfo, "w") as fh:
                json.dump({"topsrcdir": d}, fh)

            os.chdir(d)

            with self.assertRaises(BadEnvironmentException):
                MozbuildObject.from_environment(detect_virtualenv_mozinfo=False)

        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def test_objdir_mismatch(self):
        """Ensure MachCommandBase throwing on objdir mismatch."""
        d = os.path.realpath(tempfile.mkdtemp())

        try:
            real_topobjdir = os.path.join(d, "real-objdir")
            os.makedirs(real_topobjdir)

            topobjdir = os.path.join(d, "objdir")
            os.makedirs(topobjdir)

            topsrcdir = os.path.join(d, "srcdir")
            prepare_tmp_topsrcdir(topsrcdir)

            mozconfig = os.path.join(d, "mozconfig")
            with open(mozconfig, "w") as fh:
                fh.write(
                    "mk_add_options MOZ_OBJDIR=%s" % real_topobjdir.replace("\\", "/")
                )

            mozinfo = os.path.join(topobjdir, "mozinfo.json")
            with open(mozinfo, "w") as fh:
                json.dump(
                    dict(
                        topsrcdir=topsrcdir,
                        mozconfig=mozconfig,
                    ),
                    fh,
                )

            os.chdir(topobjdir)

            class MockMachContext:
                pass

            context = MockMachContext()
            context.cwd = topobjdir
            context.topdir = topsrcdir
            context.settings = None
            context.log_manager = None
            context.detect_virtualenv_mozinfo = False

            stdout = sys.stdout
            sys.stdout = StringIO()
            try:
                with self.assertRaises(SystemExit):
                    MachCommandBase(context, None)

                self.assertTrue(
                    sys.stdout.getvalue().startswith(
                        "Ambiguous object directory detected."
                    )
                )
            finally:
                sys.stdout = stdout

        finally:
            os.chdir(self._old_cwd)
            shutil.rmtree(d)

    def test_config_environment(self):
        d = os.path.realpath(tempfile.mkdtemp())

        try:
            with open(os.path.join(d, "config.status"), "w") as fh:
                fh.write("# coding=utf-8\n")
                fh.write("from __future__ import unicode_literals\n")
                fh.write("topobjdir = '%s'\n" % mozpath.normsep(d))
                fh.write("topsrcdir = '%s'\n" % topsrcdir)
                fh.write("mozconfig = None\n")
                fh.write("defines = { 'FOO': 'foo' }\n")
                fh.write("substs = { 'QUX': 'qux' }\n")
                fh.write(
                    "__all__ = ['topobjdir', 'topsrcdir', 'defines', "
                    "'substs', 'mozconfig']"
                )

            base = self.get_base(topobjdir=d)

            ce = base.config_environment
            self.assertIsInstance(ce, ConfigEnvironment)

            self.assertEqual(base.defines, ce.defines)
            self.assertEqual(base.substs, ce.substs)

            self.assertEqual(base.defines, {"FOO": "foo"})
            self.assertEqual(
                base.substs,
                {
                    "ACDEFINES": "-DFOO=foo",
                    "ALLEMPTYSUBSTS": "",
                    "ALLSUBSTS": "ACDEFINES = -DFOO=foo\nQUX = qux",
                    "QUX": "qux",
                },
            )
        finally:
            shutil.rmtree(d)

    def test_get_binary_path(self):
        base = self.get_base(topobjdir=topobjdir)

        platform = sys.platform

        # We should ideally use the config.status from the build. Let's install
        # a fake one.
        substs = [
            ("MOZ_APP_NAME", "awesomeapp"),
            ("MOZ_BUILD_APP", "awesomeapp"),
        ]
        if sys.platform.startswith("darwin"):
            substs.append(("OS_ARCH", "Darwin"))
            substs.append(("BIN_SUFFIX", ""))
            substs.append(("MOZ_MACBUNDLE_NAME", "Nightly.app"))
        elif sys.platform.startswith(("win32", "cygwin")):
            substs.append(("OS_ARCH", "WINNT"))
            substs.append(("BIN_SUFFIX", ".exe"))
        else:
            substs.append(("OS_ARCH", "something"))
            substs.append(("BIN_SUFFIX", ""))

        base._config_environment = ConfigEnvironment(
            base.topsrcdir, base.topobjdir, substs=substs
        )

        p = base.get_binary_path("xpcshell", False)
        if platform.startswith("darwin"):
            self.assertTrue(p.endswith("Contents/MacOS/xpcshell"))
        elif platform.startswith(("win32", "cygwin")):
            self.assertTrue(p.endswith("xpcshell.exe"))
        else:
            self.assertTrue(p.endswith("dist/bin/xpcshell"))

        p = base.get_binary_path(validate_exists=False)
        if platform.startswith("darwin"):
            self.assertTrue(p.endswith("Contents/MacOS/awesomeapp"))
        elif platform.startswith(("win32", "cygwin")):
            self.assertTrue(p.endswith("awesomeapp.exe"))
        else:
            self.assertTrue(p.endswith("dist/bin/awesomeapp"))

        p = base.get_binary_path(validate_exists=False, where="staged-package")
        if platform.startswith("darwin"):
            self.assertTrue(
                p.endswith("awesomeapp/Nightly.app/Contents/MacOS/awesomeapp")
            )
        elif platform.startswith(("win32", "cygwin")):
            self.assertTrue(p.endswith("awesomeapp\\awesomeapp.exe"))
        else:
            self.assertTrue(p.endswith("awesomeapp/awesomeapp"))

        self.assertRaises(Exception, base.get_binary_path, where="somewhere")

        p = base.get_binary_path("foobar", validate_exists=False)
        if platform.startswith("win32"):
            self.assertTrue(p.endswith("foobar.exe"))
        else:
            self.assertTrue(p.endswith("foobar"))


class FakeMozbuildObject(MozbuildObject):
    def __init__(self, make_flags=None, make_extra=None):
        super().__init__(topsrcdir, None, log_manager)
        self._mozconfig = {"make_flags": make_flags, "make_extra": make_extra}

    @property
    def mozconfig(self):
        return self._mozconfig

    @property
    def substs(self):
        return {}


class TestRunMake(unittest.TestCase):
    def _args(self, append_env, makeflags=""):
        base = FakeMozbuildObject()
        with mock.patch.object(
            FakeMozbuildObject,
            "substs",
            new_callable=mock.PropertyMock,
            return_value={"GMAKE": "make"},
        ), mock.patch.object(base, "_ensure_objdir_exists"), mock.patch.object(
            base, "_run_command_in_objdir"
        ) as run, mock.patch.dict(os.environ, {"MAKEFLAGS": makeflags}):
            base._run_make(target="export", num_jobs=8, append_env=append_env)
        return run.call_args.kwargs["args"]

    def test_run_make_jobs(self):
        self.assertEqual(self._args(None), ["make", "-j8", "-s", "-w", "export"])

    def test_run_make_makeflags(self):
        self.assertEqual(
            self._args({"MAKEFLAGS": "-j4"}), ["make", "-s", "-w", "export"]
        )

    def test_run_make_inherited_makeflags(self):
        self.assertEqual(
            self._args(None, "-j4 --jobserver-auth=3,4"),
            ["make", "-j8", "-s", "-w", "export"],
        )


class TestSplitJobFlags(unittest.TestCase):
    def test_split_job_flags(self):
        for make_flags, expected in (
            (None, (0, [])),
            ([], (0, [])),
            (["-j8", "-s"], (8, ["-s"])),
            (["-j", "8", "-s"], (8, ["-s"])),
            (["-j", "-s"], (0, ["-s"])),
            (["-j"], (0, [])),
            (["-j", "-j8", "-s"], (8, ["-s"])),
            (["-jFOO", "-j8", "-s"], (8, ["-s"])),
            (["-s", "FOO=bar"], (0, ["-s", "FOO=bar"])),
            (["-j4", "-j8"], (8, [])),
            (["-j8", "-j"], (0, [])),
            (["-j8", "-j", "-s"], (0, ["-s"])),
            (["--jobs=8", "-s"], (8, ["-s"])),
            (["--jobs", "8", "-s"], (8, ["-s"])),
            (["--jobs", "-s"], (0, ["-s"])),
        ):
            with self.subTest(make_flags=make_flags):
                self.assertEqual(split_job_flags(make_flags), expected)


class TestResolveNumJobs(unittest.TestCase):
    def test_explicit_num_jobs_wins(self):
        base = FakeMozbuildObject(
            make_flags=["-j8"], make_extra=["MOZ_PARALLEL_BUILD=4"]
        )
        self.assertEqual(base.resolve_num_jobs(2), 2)

    def test_moz_parallel_build_beats_make_flags(self):
        base = FakeMozbuildObject(
            make_flags=["-j8"], make_extra=["MOZ_PARALLEL_BUILD=4"]
        )
        self.assertEqual(base.resolve_num_jobs(), 4)

    def test_make_flags_beat_heuristic(self):
        base = FakeMozbuildObject(make_flags=["-j8"])
        self.assertEqual(base.resolve_num_jobs(), 8)

    def test_heuristic_when_nothing_configured(self):
        base = FakeMozbuildObject()
        with mock.patch("mozbuild.base.psutil", None):
            self.assertEqual(base.resolve_num_jobs(), cpu_count())


class TestPathArgument(unittest.TestCase):
    def test_path_argument(self):
        # Absolute path
        p = PathArgument("/obj/foo", "/src", "/obj", "/src")
        self.assertEqual(p.relpath(), "foo")
        self.assertEqual(p.srcdir_path(), "/src/foo")
        self.assertEqual(p.objdir_path(), "/obj/foo")

        # Relative path within srcdir
        p = PathArgument("foo", "/src", "/obj", "/src")
        self.assertEqual(p.relpath(), "foo")
        self.assertEqual(p.srcdir_path(), "/src/foo")
        self.assertEqual(p.objdir_path(), "/obj/foo")

        # Relative path within subdirectory
        p = PathArgument("bar", "/src", "/obj", "/src/foo")
        self.assertEqual(p.relpath(), "foo/bar")
        self.assertEqual(p.srcdir_path(), "/src/foo/bar")
        self.assertEqual(p.objdir_path(), "/obj/foo/bar")

        # Relative path within objdir
        p = PathArgument("foo", "/src", "/obj", "/obj")
        self.assertEqual(p.relpath(), "foo")
        self.assertEqual(p.srcdir_path(), "/src/foo")
        self.assertEqual(p.objdir_path(), "/obj/foo")

        # "." path
        p = PathArgument(".", "/src", "/obj", "/src/foo")
        self.assertEqual(p.relpath(), "foo")
        self.assertEqual(p.srcdir_path(), "/src/foo")
        self.assertEqual(p.objdir_path(), "/obj/foo")

        # Nested src/obj directories
        p = PathArgument("bar", "/src", "/src/obj", "/src/obj/foo")
        self.assertEqual(p.relpath(), "foo/bar")
        self.assertEqual(p.srcdir_path(), "/src/foo/bar")
        self.assertEqual(p.objdir_path(), "/src/obj/foo/bar")


if __name__ == "__main__":
    main()
