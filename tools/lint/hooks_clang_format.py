#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
from subprocess import CalledProcessError, check_output

here = os.path.dirname(os.path.realpath(__file__))
topsrcdir = os.path.join(here, os.pardir, os.pardir)

EXTRA_PATHS = (
    "python/mach",
    "python/mozversioncontrol",
    "testing/mozbase/mozfile",
    "third_party/python/jsmin",
)
sys.path[:0] = [os.path.join(topsrcdir, p) for p in EXTRA_PATHS]

from mozversioncontrol import InvalidRepoPath, get_repository_object


def run_clang_format(hooktype, changedFiles):
    try:
        vcs = get_repository_object(topsrcdir)
    except InvalidRepoPath:
        return

    if not changedFiles:
        # No files have been touched
        return

    # We have also a copy of this list in:
    # python/mozbuild/mozbuild/mach_commands.py
    # version-control-tools/hgext/clang-format/__init__.py
    # release-services/src/staticanalysis/bot/static_analysis_bot/config.py
    # Too heavy to import the full class just for this variable
    extensions = (".cpp", ".c", ".cc", ".h", ".m", ".mm")
    path_list = []
    for filename in sorted(changedFiles):
        # Ignore files unsupported in clang-format
        if filename.endswith(extensions):
            path_list.append(filename)

    if not path_list:
        # No files have been touched
        return

    arguments = ["clang-format", "-p"] + path_list
    # On windows we need this to call the command in a shell, see Bug 1511594
    if os.name == "nt":
        clang_format_cmd = [sys.executable, "mach"] + arguments
    else:
        clang_format_cmd = [os.path.join(topsrcdir, "mach")] + arguments
    if "commit" in hooktype:
        # don't prevent commits, just display the clang-format results
        subprocess.call(clang_format_cmd)

        vcs.add_remove_files(*path_list)

        return False
    print(f"warning: '{hooktype}' is not a valid clang-format hooktype")
    return False


def hg(ui, repo, node, **kwargs):
    print(
        "warning: this hook has been deprecated. Please use the hg extension instead.\n"
        "please add 'clang-format = ~/.mozbuild/version-control-tools/hgext/clang-format'"
        " to hgrc\n"
        "Or run 'mach bootstrap'"
    )
    return False


def git():
    hooktype = os.path.basename(__file__)
    if hooktype == "hooks_clang_format.py":
        hooktype = "pre-push"

    try:
        changedFiles = check_output(
            ["git", "diff", "--staged", "--diff-filter=d", "--name-only", "HEAD"],
            text=True,
        ).split()
        # TODO we should detect if we are in a "add -p" mode and show a warning
        return run_clang_format(hooktype, changedFiles)

    except CalledProcessError:
        print("Command to retrieve local files failed")
        return 1


if __name__ == "__main__":
    sys.exit(git())
