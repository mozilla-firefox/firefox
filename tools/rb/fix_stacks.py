#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This script uses `fix-stacks` to post-process the entries produced by
# MozFormatCodeAddress().

import atexit
import os
import platform
import re
import sys
import threading
from subprocess import PIPE, Popen

# Matches lines produced by MozFormatCodeAddress(), e.g.
# `#01: ???[tests/example +0x43a0]`.
line_re = re.compile(r"#\d+: .+\[.+ \+0x[0-9A-Fa-f]+\]")

fix_stacks = None
# Serializes requests to `fix_stacks`, which harnesses share across threads.
fix_stacks_lock = threading.Lock()


def autobootstrap():
    import buildconfig
    from mozbuild.configure import ConfigureSandbox

    sandbox = ConfigureSandbox(
        {},
        argv=[
            "configure",
            "--help",
            "--host={}".format(buildconfig.substs["HOST_ALIAS"]),
        ],
    )
    moz_configure = os.path.join(buildconfig.topsrcdir, "build", "moz.configure")
    sandbox.include_file(os.path.join(moz_configure, "init.configure"))
    sandbox.include_file(os.path.join(moz_configure, "bootstrap.configure"))
    # Expand the `bootstrap_path` template for "fix-stacks", and execute the
    # expanded function via `_value_for`, which will trigger autobootstrap.
    sandbox._value_for(sandbox["bootstrap_path"]("fix-stacks"))


def initFixStacks(jsonMode, slowWarning, breakpadSymsDir, hide_errors):
    # Look in MOZ_FETCHES_DIR (for automation), then in MOZBUILD_STATE_PATH
    # (for a local build where the user has that set), then in ~/.mozbuild
    # (for a local build with default settings).
    base = os.environ.get(
        "MOZ_FETCHES_DIR",
        os.environ.get("MOZBUILD_STATE_PATH", os.path.expanduser("~/.mozbuild")),
    )
    fix_stacks_exe = base + "/fix-stacks/fix-stacks"
    if platform.system() == "Windows":
        fix_stacks_exe = fix_stacks_exe + ".exe"

    if not (os.path.isfile(fix_stacks_exe) and os.access(fix_stacks_exe, os.X_OK)):
        try:
            autobootstrap()
        except ImportError:
            # We're out-of-tree (e.g. tests tasks on CI) and can't autobootstrap
            # (we shouldn't anyways).
            pass

    if not (os.path.isfile(fix_stacks_exe) and os.access(fix_stacks_exe, os.X_OK)):
        raise Exception("cannot find `fix-stacks`; please run `./mach bootstrap`")

    args = [fix_stacks_exe]
    if jsonMode:
        args.append("-j")
    if breakpadSymsDir:
        args.append("-b")
        args.append(breakpadSymsDir)

    # Sometimes we need to prevent errors from going to stderr.
    stderr = open(os.devnull) if hide_errors else None

    global fix_stacks
    fix_stacks = Popen(
        args, stdin=PIPE, stdout=PIPE, stderr=stderr, universal_newlines=True
    )

    # Shut down the fix_stacks process on exit. We use `terminate()`
    # because it is more forceful than `wait()`, and the Python docs warn
    # about possible deadlocks with `wait()`.
    def cleanup(fix_stacks):
        for fn in [fix_stacks.stdin.close, fix_stacks.terminate]:
            try:
                fn()
            except OSError:
                pass

    atexit.register(cleanup, fix_stacks)

    if slowWarning:
        print(
            "Initializing stack-fixing for the first stack frame, this may take a while..."
        )


def fixSymbols(
    line, jsonMode=False, slowWarning=False, breakpadSymsDir=None, hide_errors=False
):
    is_bytes = isinstance(line, bytes)
    line_str = line.decode("utf-8") if is_bytes else line
    if line_re.search(line_str) is None:
        return line

    # Sometimes `line` is lacking a trailing newline. If we pass such a `line`
    # to `fix-stacks` it will wait until it receives a newline, causing this
    # script to hang. So we add a newline if one is missing and then remove it
    # from the output.
    is_missing_newline = not line_str.endswith("\n")
    if is_missing_newline:
        line_str = line_str + "\n"

    with fix_stacks_lock:
        if not fix_stacks:
            initFixStacks(jsonMode, slowWarning, breakpadSymsDir, hide_errors)
        fix_stacks.stdin.write(line_str)
        fix_stacks.stdin.flush()
        out = fix_stacks.stdout.readline()
    if is_missing_newline:
        out = out[:-1]

    if is_bytes and not isinstance(out, bytes):
        out = out.encode("utf-8")
    return out


if __name__ == "__main__":
    bpsyms = os.environ.get("BREAKPAD_SYMBOLS_PATH", None)
    for line in sys.stdin:
        sys.stdout.write(fixSymbols(line, breakpadSymsDir=bpsyms))
