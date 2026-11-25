#!/usr/bin/python3
# Copyright 2023 The ANGLE Project Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import subprocess
import sys


def command(args):
    import buildconfig

    metal_dir = buildconfig.substs.get('MOZ_METAL_TOOLCHAIN_DIR')
    if not metal_dir:
        return ['xcrun', '--sdk', 'macosx'] + args

    cmd = [os.path.join(metal_dir, 'usr', 'bin', args[0])]
    args = args[1:]

    # Support cross-compilation by optionally executing via `darling-run`
    # from https://github.com/mozilla/darling/tree/standalone
    darling_dir = buildconfig.substs.get('MOZ_DARLING_DIR')
    if darling_dir:
        cmd = [os.path.join(darling_dir, 'bin', 'darling-run')] + cmd
        # Darling resolves absolute paths within its own root, so map host paths
        # through /Volumes/SystemRoot.
        args = ['/Volumes/SystemRoot' + a if os.path.isabs(a) else a for a in args]
    return cmd + args


def main(args):
    return subprocess.run(command(args), stdout=subprocess.PIPE, text=True).returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
