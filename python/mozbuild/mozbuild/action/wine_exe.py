# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys

import buildconfig


def main(argv):
    wine, exe, *args = argv
    # Windows program run via Wine don't like Unix absolute paths (they look
    # like command line arguments). So when needed, create relative paths
    # from absolute paths.
    cwd = os.getcwd()
    args = [
        os.path.relpath(arg, cwd) if os.path.isabs(arg) and os.path.isfile(arg) else arg
        for arg in args
    ]
    # asmarm64 needs a library that can be found in $PATH but for some reason,
    # wine wants its path in $WINEPATH, so fill that to make it happy.
    env = dict(os.environ, WINEPATH=buildconfig.substs["PATH"].replace(":", ";"))
    return subprocess.call([wine, exe, *args], env=env)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
