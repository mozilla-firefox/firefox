# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""A GNU Make jobserver that `BuildDriver` owns.

The protocol is described at
https://www.gnu.org/software/make/manual/html_node/Job-Slots.html. GNU Make
takes a named semaphore on Windows and a pipe elsewhere. With an anonymous pipe,
only recipe lines that Make treats as recursive reach the jobserver, so Make 4.4
and later get a named pipe, which every child can open. The standard library
cannot create a named semaphore on Windows, so `_semaphore` calls
`CreateSemaphoreW` through `ctypes`.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import uuid
from contextlib import ExitStack, contextmanager

from ..base import split_job_flags


def make_version(make):
    try:
        output = subprocess.check_output(
            [make, "--version"], stderr=subprocess.DEVNULL, text=True
        )
    except (subprocess.CalledProcessError, OSError):
        return None
    if m := re.match(r"GNU Make (\d+)\.(\d+)", output):
        return int(m[1]), int(m[2])
    return None


@contextmanager
def _semaphore(jobs):
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateSemaphoreW.restype = wintypes.HANDLE
    kernel32.CreateSemaphoreW.argtypes = (
        ctypes.c_void_p,
        wintypes.LONG,
        wintypes.LONG,
        wintypes.LPCWSTR,
    )
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    name = f"gmake_semaphore_{os.getpid()}_{uuid.uuid4().hex}"
    handle = kernel32.CreateSemaphoreW(None, jobs - 1, jobs - 1, name)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        yield name
    finally:
        kernel32.CloseHandle(handle)


@contextmanager
def _pipe(jobs):
    read, write = os.pipe()
    try:
        os.write(write, b"+" * (jobs - 1))
        os.set_inheritable(read, True)
        os.set_inheritable(write, True)
        yield f"{read},{write}"
    finally:
        os.close(read)
        os.close(write)


@contextmanager
def _fifo(jobs):
    tmpdir = tempfile.mkdtemp(prefix="jobserver")
    path = os.path.join(tmpdir, "fifo")
    try:
        if any(c.isspace() for c in path):
            raise OSError(f"jobserver path {path!r} contains whitespace")
        os.mkfifo(path, 0o600)
        with ExitStack() as stack:
            read = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            stack.callback(os.close, read)
            write = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
            stack.callback(os.close, write)
            os.write(write, b"+" * (jobs - 1))
            yield f"fifo:{path}"
    finally:
        shutil.rmtree(tmpdir)


def _server(version):
    if sys.platform == "win32":
        return _semaphore
    if version >= (4, 4):
        return _fifo
    return _pipe


@contextmanager
def jobserver(jobs, make):
    inherited = os.environ.get("MAKEFLAGS", "")
    version = make_version(make) or (0,)
    flags, sep, assignments = f" {inherited}".partition(" -- ")
    _, flags = split_job_flags(flags.split())
    flags = [
        flag
        for flag in flags
        if not flag.startswith(("--jobserver-auth=", "--jobserver-fds="))
    ]
    flags.append(f"-j{jobs}")
    with ExitStack() as stack:
        if jobs >= 2 and version >= (4, 2):
            try:
                auth = stack.enter_context(_server(version)(jobs))
                flags.append(f"--jobserver-auth={auth}")
            except OSError:
                pass
        yield " ".join(flags) + sep + assignments
