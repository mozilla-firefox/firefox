#!/usr/bin/env vpython3

# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

# pylint: disable=protected-access
"""Tests for manual_roll.py."""

import io
import tarfile
import unittest

import manual_roll
from manual_roll import roll_deps

LLVM_PATH = 'src/third_party/llvm-build/Release+Asserts'

DEPS_TEMPLATE = """
vars = {
  'chromium_revision': '%(chromium_revision)s',
  'siso_version': 'git_revision:%(version)s',
}

deps = {
  'src/third_party/foo':
    'https://chromium.googlesource.com/foo.git@%(version)s',
  'src/third_party/tool/cipd': {
      'packages': [
          {
              'package': '%(tool_package)s',
              'version': 'v%(version)s',
          },
      ],
      'dep_type': 'cipd',
  },
  'src/third_party/llvm-build/Release+Asserts': {
    'dep_type': 'gcs',
    'bucket': 'chromium-browser-clang',
    'objects': [%(objects)s
    ],
  },%(extra_deps)s
  # === ANDROID_DEPS Generated Code Start ===
  # === ANDROID_DEPS Generated Code End ===
}
"""

OBJECT_TEMPLATE = """
      {
        'object_name': 'Linux_x64/%s-llvmorg-24-init-1-gabc-%s.tar.xz',
        'sha256sum': 'abc',
        'size_bytes': 1,
        'generation': 1,
      },"""

ANDROID_DEP = """
  'src/third_party/android_deps/old/cipd': {
      'packages': [
          {
              'package': 'chromium/third_party/android_deps/old',
              'version': 'v1',
          },
      ],
      'dep_type': 'cipd',
  },"""

GONE_DEP = """
  'src/third_party/gone/cipd': {
      'packages': [
          {
              'package': 'chromium/third_party/gone',
              'version': 'v1',
          },
      ],
      'dep_type': 'cipd',
  },"""


def _deps(version,
          tool_package='chromium/third_party/tool',
          objects=('clang', 'clang-tidy'),
          extra_deps=''):
    return DEPS_TEMPLATE % {
        'chromium_revision': version * 40,
        'version': version,
        'tool_package': tool_package,
        'objects': ''.join(OBJECT_TEMPLATE % (name, version)
                           for name in objects),
        'extra_deps': extra_deps,
    }


def _blocker_paths(webrtc_content, cr_content):
    blockers = manual_roll.find_blockers(
        roll_deps.ParseDepsDict(webrtc_content),
        roll_deps.ParseDepsDict(cr_content))
    return [path for path, _ in blockers], [reason for _, reason in blockers]


class FindBlockersTest(unittest.TestCase):

    def test_new_versions_do_not_block(self):
        self.assertEqual(_blocker_paths(_deps('1'), _deps('2')), ([], []))

    def test_renamed_cipd_package_blocks(self):
        paths, _ = _blocker_paths(
            _deps('1'),
            _deps('2', tool_package='chromium/third_party/tool/${{platform}}'))
        self.assertEqual(paths, ['src/third_party/tool/cipd'])

    def test_added_gcs_object_blocks(self):
        paths, reasons = _blocker_paths(
            _deps('1'),
            _deps('2', objects=('clang-runtime', 'clang', 'clang-tidy')))
        self.assertEqual(paths, [LLVM_PATH])
        self.assertIn('number of objects', reasons[0])

    def test_reordered_gcs_objects_block(self):
        paths, reasons = _blocker_paths(
            _deps('1'), _deps('2', objects=('clang-tidy', 'clang')))
        self.assertEqual(paths, [LLVM_PATH])
        self.assertIn('not in the same order', reasons[0])

    def test_dep_removed_from_chromium_blocks(self):
        paths, _ = _blocker_paths(_deps('1', extra_deps=GONE_DEP), _deps('2'))
        self.assertEqual(paths, ['src/third_party/gone/cipd'])

    def test_changed_url_blocks(self):
        paths, _ = _blocker_paths(_deps('1'),
                                  _deps('2').replace('foo.git', 'bar.git'))
        self.assertEqual(paths, ['src/third_party/foo'])

    def test_android_dep_dropped_by_chromium_does_not_block(self):
        self.assertEqual(
            _blocker_paths(_deps('1', extra_deps=ANDROID_DEP), _deps('2')),
            ([], []))


def _stale_android_deps(webrtc_content):
    return manual_roll.find_stale_android_deps(
        roll_deps.ParseDepsDict(webrtc_content),
        roll_deps.ParseDepsDict(_deps('2')), webrtc_content)


class FindStaleAndroidDepsTest(unittest.TestCase):

    def test_dropped_dep_outside_android_deps_section_is_stale(self):
        self.assertEqual(
            _stale_android_deps(_deps('1', extra_deps=ANDROID_DEP)),
            ['src/third_party/android_deps/old/cipd'])

    def test_dropped_dep_inside_android_deps_section_is_not_stale(self):
        webrtc_content = _deps('1').replace('Code Start ===',
                                            'Code Start ===' + ANDROID_DEP)
        self.assertEqual(_stale_android_deps(webrtc_content), [])


class HelpersTest(unittest.TestCase):

    def test_object_stem(self):
        self.assertEqual(
            manual_roll.object_stem(
                'Mac/clang-tidy-llvmorg-24-init-7747-g62397f8b-67.tar.xz'),
            'Mac/clang-tidy-llvmorg')
        self.assertEqual(
            manual_roll.object_stem(
                'Win/rust-toolchain-1edd55dc-2-llvmorg-24.tar.xz'),
            'Win/rust-toolchain')

    def test_raw_entry(self):
        content = _deps('1')
        entry = manual_roll.raw_entry(content, 'src/third_party/tool/cipd')
        self.assertTrue(entry.startswith("'src/third_party/tool/cipd': {"))
        self.assertTrue(entry.endswith("'dep_type': 'cipd',\n  },"))
        self.assertIsNone(manual_roll.raw_entry(content, 'src/missing'))

    def test_setdep_on_edits_deps_file_without_checkout(self):
        calls = []

        def record(command, **_):
            calls.append(command)

        run_command = roll_deps._RunCommand
        is_existing_dir = roll_deps._IsExistingDir
        roll_deps._RunCommand = record
        try:
            with manual_roll._setdep_on('/path/DEPS'):
                roll_deps._RunCommand(['gclient', 'setdep', '-r', 'a@b'])
                roll_deps._RunCommand(['git', 'ls-remote', 'url', 'HEAD'])
                self.assertTrue(roll_deps._IsExistingDir('/does/not/exist'))
            self.assertIs(roll_deps._RunCommand, record)
        finally:
            roll_deps._RunCommand = run_command
        self.assertIs(roll_deps._IsExistingDir, is_existing_dir)
        self.assertEqual(calls, [
            ['gclient', 'setdep', '-r', 'a@b', '--deps-file', '/path/DEPS'],
            ['git', 'ls-remote', 'url', 'HEAD'],
        ])


def _archive(files):
    """Returns a .tar.gz with the given {name: content} files."""
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w:gz') as tar:
        for name, content in files.items():
            content = content.encode('utf-8')
            info = tarfile.TarInfo(name)
            info.size = len(content)
            tar.addfile(info, io.BytesIO(content))
    return data.getvalue()


def _cipd_dep():
    return {'packages': [], 'dep_type': 'cipd'}


class FindMissingBuildDepsTest(unittest.TestCase):

    def test_third_party_refs(self):
        archive = _archive({
            'android/internal_rules.gni':
            '_jar = "//third_party/signer/cipd/signer.jar"\n'
            '_dep = "//third_party/jacoco:jacocoagent_java"\n'
            '_sdk = "${android_sdk_root}/platforms"\n',
            'android/BUILD.gn':
            'deps = [ "//third_party/signer/cipd/" ]\n',
            'README.md':
            '"//third_party/ignored/cipd"\n',
        })
        self.assertEqual(
            manual_roll.third_party_refs(archive, 'build/config'), {
                'third_party/signer/cipd/signer.jar':
                {'build/config/android/internal_rules.gni'},
                'third_party/signer/cipd': {'build/config/android/BUILD.gn'},
                'third_party/jacoco':
                {'build/config/android/internal_rules.gni'},
            })

    def test_reports_prebuilts_only_the_new_build_uses(self):
        cr_deps = {
            'deps': {
                'src/third_party/signer/cipd': _cipd_dep(),
                'src/third_party/protoc/cipd': _cipd_dep(),
                'src/third_party/r8/cipd': _cipd_dep(),
                'src/third_party/git_dep': 'https://example.com/a.git@1',
            }
        }
        webrtc_deps = {'deps': {'src/third_party/r8/cipd': _cipd_dep()}}
        old_refs = {
            'third_party/protoc/cipd/protoc': {'old.gni'},
            'third_party/r8/cipd/lib/r8.jar': {'old.gni'},
        }
        new_refs = {
            'third_party/signer/cipd/signer.jar': {'new.gni'},
            'third_party/protoc/cipd/protoc': {'new.gni'},
            'third_party/r8/cipd/lib/r8.jar': {'new.gni'},
            'third_party/git_dep/x': {'new.gni'},
        }
        self.assertEqual(
            manual_roll.find_missing_build_deps(old_refs, new_refs,
                                                webrtc_deps, cr_deps),
            {'src/third_party/signer/cipd': {'new.gni'}})

    def test_does_not_match_sibling_paths(self):
        cr_deps = {'deps': {'src/third_party/r8/cipd': _cipd_dep()}}
        self.assertEqual(
            manual_roll.find_missing_build_deps(
                {}, {'third_party/r8/cipd_other/x': {'new.gni'}}, {'deps': {}},
                cr_deps), {})


if __name__ == '__main__':
    unittest.main()
