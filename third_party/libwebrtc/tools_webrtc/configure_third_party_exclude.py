#!/usr/bin/env vpython3
# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
"""Populates third_party/.git/info/exclude with nested third_party DEPS paths.

In WebRTC, src/third_party is checked out as a separate Git repository while
dozens of other dependencies (such as src/third_party/depot_tools,
src/third_party/boringssl/src, src/third_party/ffmpeg, etc.) are checked out
inside src/third_party/.

When bot_update runs `gclient sync --reset --delete_unversioned_trees`,
gclient's `_Scrub` runs `git status --porcelain` in src/third_party and deletes
any untracked directories not ignored by git. Because Chromium removed
submodule paths from chromium/src/third_party/.gitignore in
https://crrev.com/c/4877868, those nested DEPS directories appear untracked to
src/third_party's git repo and get deleted on every build (including
src/third_party/depot_tools/win_toolchain, forcing a 2.1 GiB / 9 GB Windows SDK
re-download and extraction on every Windows build).

Writing the nested src/third_party/* DEPS paths to
src/third_party/.git/info/exclude keeps git in src/third_party from treating
nested DEPS checkouts as untracked residuals across builds.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, os.pardir))

BEGIN_MARKER = '# BEGIN WEBRTC DEPS EXCLUDES'
END_MARKER = '# END WEBRTC DEPS EXCLUDES'
THIRD_PARTY_PREFIX = 'src/third_party/'


def parse_third_party_deps(deps_file):
    scope = {'Var': lambda x: x, 'Str': str}
    with open(deps_file, 'r', encoding='utf-8') as deps_fp:
        exec(deps_fp.read(), scope)  # pylint: disable=exec-used
    deps = scope.get('deps', {})
    return sorted(key[len('src/third_party'):] for key in deps
                  if key.startswith(THIRD_PARTY_PREFIX))


def _resolve_git_dir(repo_dir):
    dot_git = os.path.join(repo_dir, '.git')
    if os.path.isdir(dot_git):
        return dot_git
    if os.path.isfile(dot_git):
        with open(dot_git, 'r', encoding='utf-8') as git_fp:
            line = git_fp.readline().strip()
        if line.startswith('gitdir:'):
            git_dir = line[len('gitdir:'):].strip()
            if not os.path.isabs(git_dir):
                git_dir = os.path.normpath(os.path.join(repo_dir, git_dir))
            if os.path.isdir(git_dir):
                return git_dir
    return None


def _update_exclude_content(existing_content, exclude_paths):
    lines = existing_content.splitlines()
    out_lines = []
    in_block = False
    for line in lines:
        if line.strip() == BEGIN_MARKER:
            in_block = True
            continue
        if line.strip() == END_MARKER:
            in_block = False
            continue
        if not in_block:
            out_lines.append(line)

    while out_lines and not out_lines[-1].strip():
        out_lines.pop()

    if out_lines:
        out_lines.append('')
    out_lines.append(BEGIN_MARKER)
    out_lines.extend(exclude_paths)
    out_lines.append(END_MARKER)
    return '\n'.join(out_lines) + '\n'


def configure_exclude(deps_file, third_party_dir):
    git_dir = _resolve_git_dir(third_party_dir)
    if not git_dir:
        return False

    exclude_paths = parse_third_party_deps(deps_file)
    exclude_file = os.path.join(git_dir, 'info', 'exclude')
    os.makedirs(os.path.dirname(exclude_file), exist_ok=True)

    existing_content = ''
    if os.path.exists(exclude_file):
        with open(exclude_file, 'r', encoding='utf-8') as exclude_fp:
            existing_content = exclude_fp.read()

    new_content = _update_exclude_content(existing_content, exclude_paths)
    if new_content != existing_content:
        with open(exclude_file, 'w', encoding='utf-8') as exclude_fp:
            exclude_fp.write(new_content)
        return True
    return False


def main():
    deps_file = os.path.join(SRC_DIR, 'DEPS')
    third_party_dir = os.path.join(SRC_DIR, 'third_party')
    configure_exclude(deps_file, third_party_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
