# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy
import errno
import os
import subprocess
import sys
import tempfile
import unittest

import six
from buildconfig import topobjdir, topsrcdir
from mozpack import path as mozpath
from six import StringIO, string_types

from mozbuild.configure import ConfigureSandbox
from mozbuild.util import ReadOnlyNamespace, memoized_property


def fake_short_path(path):
    if sys.platform.startswith("win"):
        return "/".join(
            p.split(" ", 1)[0] + "~1" if " " in p else p for p in mozpath.split(path)
        )
    return path


def ensure_exe_extension(path):
    if sys.platform.startswith("win"):
        return path + ".exe"
    return path


class ConfigureTestVFS:
    def __init__(self, paths):
        self._paths = set(mozpath.abspath(p) for p in paths)

    def _real_file(self, path):
        return mozpath.basedir(path, [topsrcdir, topobjdir, tempfile.gettempdir()])

    def exists(self, path):
        if path in self._paths:
            return True
        if self._real_file(path):
            return os.path.exists(path)
        return False

    def isfile(self, path):
        path = mozpath.abspath(path)
        if path in self._paths:
            return True
        if self._real_file(path):
            return os.path.isfile(path)
        return False

    def expanduser(self, path):
        return os.path.expanduser(path)

    def isdir(self, path):
        path = mozpath.abspath(path)
        if any(mozpath.basedir(mozpath.dirname(p), [path]) for p in self._paths):
            return True
        if self._real_file(path):
            return os.path.isdir(path)
        return False

    def getsize(self, path):
        if not self._real_file(path):
            raise FileNotFoundError(path)
        return os.path.getsize(path)


class ConfigureTestSandbox(ConfigureSandbox):
    """Wrapper around the ConfigureSandbox for testing purposes.

    Its arguments are the same as ConfigureSandbox, except for the additional
    `paths` argument, which is a dict where the keys are file paths and the
    values are either None or a function that will be called when the sandbox
    calls an implemented function from subprocess with the key as command.
    When the command is CONFIG_SHELL, the function for the path of the script
    that follows will be called.

    The API for those functions is:
        retcode, stdout, stderr = func(stdin, args)

    This class is only meant to implement the minimal things to make
    moz.configure testing possible. As such, it takes shortcuts.
    """

    def __init__(self, paths, config, environ, *args, **kwargs):
        self._search_path = environ.get("PATH", "").split(os.pathsep)

        self._subprocess_paths = {mozpath.abspath(k): v for k, v in paths.items() if v}

        paths = list(paths)

        environ = copy.copy(environ)
        if "CONFIG_SHELL" not in environ:
            environ["CONFIG_SHELL"] = mozpath.abspath("/bin/sh")
            self._subprocess_paths[environ["CONFIG_SHELL"]] = self.shell
            paths.append(environ["CONFIG_SHELL"])
        self._subprocess_paths[mozpath.join(topsrcdir, "build/win32/vswhere.exe")] = (
            self.vswhere
        )

        vfs = ConfigureTestVFS(paths)

        os_path = {k: getattr(vfs, k) for k in dir(vfs) if not k.startswith("_")}

        os_path.update(self.OS.path.__dict__)

        os_contents = {}
        exec("from os import *", {}, os_contents)
        os_contents["path"] = ReadOnlyNamespace(**os_path)
        os_contents["environ"] = dict(environ)
        self.imported_os = ReadOnlyNamespace(**os_contents)

        super(ConfigureTestSandbox, self).__init__(config, environ, *args, **kwargs)

    @memoized_property
    def _wrapped_mozfile(self):
        return ReadOnlyNamespace(which=self.which)

    @memoized_property
    def _wrapped_os(self):
        return self.imported_os

    @memoized_property
    def _wrapped_subprocess(self):
        return ReadOnlyNamespace(
            CalledProcessError=subprocess.CalledProcessError,
            check_output=self.check_output,
            run=self.subprocess_run,
            PIPE=subprocess.PIPE,
            STDOUT=subprocess.STDOUT,
            Popen=self.Popen,
        )

    @memoized_property
    def _wrapped_ctypes(self):
        class CTypesFunc:
            def __init__(self, func):
                self._func = func

            def __call__(self, *args, **kwargs):
                return self._func(*args, **kwargs)

        return ReadOnlyNamespace(
            create_unicode_buffer=self.create_unicode_buffer,
            windll=ReadOnlyNamespace(
                kernel32=ReadOnlyNamespace(
                    GetShortPathNameW=CTypesFunc(self.GetShortPathNameW)
                )
            ),
            wintypes=ReadOnlyNamespace(LPCWSTR=0, LPWSTR=1, DWORD=2),
        )

    @memoized_property
    def _wrapped__winreg(self):
        def OpenKey(*args, **kwargs):
            raise OSError()

        return ReadOnlyNamespace(HKEY_LOCAL_MACHINE=0, OpenKey=OpenKey)

    def create_unicode_buffer(self, *args, **kwargs):
        class Buffer:
            def __init__(self):
                self.value = ""

        return Buffer()

    def GetShortPathNameW(self, path_in, path_out, length):
        path_out.value = fake_short_path(path_in)
        return length

    def which(self, command, mode=None, path=None, exts=None):
        if isinstance(path, string_types):
            path = path.split(os.pathsep)

        for parent in path or self._search_path:
            c = mozpath.abspath(mozpath.join(parent, command))
            for candidate in (c, ensure_exe_extension(c)):
                if self.imported_os.path.exists(candidate):
                    return candidate
        return None

    def Popen(self, args, stdin=None, stdout=None, stderr=None, **kargs):
        program = self.which(args[0])
        if not program:
            raise OSError(errno.ENOENT, "File not found")

        func = self._subprocess_paths.get(program)
        cwd = kargs.get("cwd")
        if cwd and func.__code__.co_argcount == 3:
            retcode, stdout, stderr = func(stdin, args[1:], cwd)
        else:
            retcode, stdout, stderr = func(stdin, args[1:])

        class Process:
            def communicate(self, stdin=None):
                return stdout, stderr

            def wait(self):
                return retcode

        return Process()

    def check_output(self, args, **kwargs):
        proc = self.Popen(args, **kwargs)
        stdout, stderr = proc.communicate()
        retcode = proc.wait()
        if retcode:
            raise subprocess.CalledProcessError(retcode, args, stdout)
        return stdout

    def subprocess_run(self, args, **kwargs):
        proc = self.Popen(args, **kwargs)
        stdout, stderr = proc.communicate()
        retcode = proc.wait()
        if kwargs.get("check") and retcode:
            raise subprocess.CalledProcessError(retcode, args, stdout)
        return ReadOnlyNamespace(
            args=args,
            returncode=retcode,
            stdout=stdout,
            stderr=stderr,
        )

    def shell(self, stdin, args):
        script = mozpath.abspath(args[0])
        if script in self._subprocess_paths:
            return self._subprocess_paths[script](stdin, args[1:])
        return 127, "", "File not found"

    def vswhere(self, stdin, args):
        return 0, "[]", ""

    def get_config(self, name):
        # Like the loop in ConfigureSandbox.run, but only execute the code
        # associated with the given config item.
        for func, args in self._execution_queue:
            if (
                func == self._resolve_and_set
                and args[0] is self._config
                and args[1] == name
            ):
                func(*args)
                return self._config.get(name)


class BaseConfigureTest(unittest.TestCase):
    HOST = "x86_64-pc-linux-gnu"

    def setUp(self):
        self._cwd = os.getcwd()
        os.chdir(topobjdir)

    def tearDown(self):
        os.chdir(self._cwd)

    def config_guess(self, stdin, args):
        return 0, self.HOST, ""

    def config_sub(self, stdin, args):
        return 0, args[0], ""

    def get_sandbox(
        self,
        paths,
        config,
        args=[],
        environ={},
        mozconfig="",
        out=None,
        logger=None,
        cls=ConfigureTestSandbox,
    ):
        kwargs = {}
        if logger:
            kwargs["logger"] = logger
        else:
            if not out:
                out = StringIO()
            kwargs["stdout"] = out
            kwargs["stderr"] = out

        if hasattr(self, "TARGET"):
            target = ["--target=%s" % self.TARGET]
        else:
            target = []

        if mozconfig:
            fh, mozconfig_path = tempfile.mkstemp(text=True)
            os.write(fh, six.ensure_binary(mozconfig))
            os.close(fh)
        else:
            mozconfig_path = os.path.join(
                os.path.dirname(__file__), "data", "empty_mozconfig"
            )

        try:
            environ = dict(
                environ,
                MOZCONFIG=mozconfig_path,
            )

            paths = dict(paths)
            autoconf_dir = mozpath.join(topsrcdir, "build", "autoconf")
            paths[mozpath.join(autoconf_dir, "config.guess")] = self.config_guess
            paths[mozpath.join(autoconf_dir, "config.sub")] = self.config_sub

            sandbox = cls(
                paths, config, environ, ["configure"] + target + args, **kwargs
            )
            sandbox.include_file(os.path.join(topsrcdir, "moz.configure"))

            return sandbox
        finally:
            if mozconfig:
                os.remove(mozconfig_path)
