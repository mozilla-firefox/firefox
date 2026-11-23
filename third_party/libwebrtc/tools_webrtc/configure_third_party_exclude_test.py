#!/usr/bin/env vpython3
# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

import os
import shutil
import tempfile
import unittest

import configure_third_party_exclude


class ConfigureThirdPartyExcludeTest(unittest.TestCase):

    def setUp(self):
        self._temp_dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self._temp_dir)

    def test_parse_third_party_deps(self):
        deps_file = os.path.join(self._temp_dir, 'DEPS')
        with open(deps_file, 'w', encoding='utf-8') as deps_fp:
            deps_fp.write("""
vars = {'chromium_git': 'https://chromium.googlesource.com'}
deps = {
  'src/build': Var('chromium_git') + '/chromium/src/build',
  'src/third_party': Var('chromium_git') + '/chromium/src/third_party',
  'src/third_party/depot_tools': Var('chromium_git') + '/depot_tools',
  'src/third_party/boringssl/src': {'url': 'https://example.com/boringssl'},
}
""")
        paths = configure_third_party_exclude.parse_third_party_deps(deps_file)
        self.assertEqual(paths, ['/boringssl/src', '/depot_tools'])

    def test_configure_exclude_creates_and_updates_idempotently(self):
        deps_file = os.path.join(self._temp_dir, 'DEPS')
        with open(deps_file, 'w', encoding='utf-8') as deps_fp:
            deps_fp.write("""
deps = {
  'src/third_party/depot_tools': 'https://example.com/depot_tools',
  'src/third_party/ffmpeg': 'https://example.com/ffmpeg',
}
""")
        tp_dir = os.path.join(self._temp_dir, 'third_party')
        exclude_dir = os.path.join(tp_dir, '.git', 'info')
        os.makedirs(exclude_dir)
        exclude_file = os.path.join(exclude_dir, 'exclude')
        with open(exclude_file, 'w', encoding='utf-8') as exclude_fp:
            exclude_fp.write('# Existing comment\n')

        changed = configure_third_party_exclude.configure_exclude(
            deps_file, tp_dir)
        self.assertTrue(changed)

        with open(exclude_file, 'r', encoding='utf-8') as exclude_fp:
            content = exclude_fp.read()
        self.assertEqual(
            content, '# Existing comment\n\n'
            '# BEGIN WEBRTC DEPS EXCLUDES\n'
            '/depot_tools\n'
            '/ffmpeg\n'
            '# END WEBRTC DEPS EXCLUDES\n')

        # Second call with identical DEPS should be a no-op.
        changed_again = configure_third_party_exclude.configure_exclude(
            deps_file, tp_dir)
        self.assertFalse(changed_again)

    def test_no_op_when_git_dir_missing(self):
        deps_file = os.path.join(self._temp_dir, 'DEPS')
        with open(deps_file, 'w', encoding='utf-8') as deps_fp:
            deps_fp.write('deps = {}\n')
        tp_dir = os.path.join(self._temp_dir, 'third_party')
        os.makedirs(tp_dir)
        self.assertFalse(
            configure_third_party_exclude.configure_exclude(deps_file, tp_dir))


if __name__ == '__main__':
    unittest.main()
