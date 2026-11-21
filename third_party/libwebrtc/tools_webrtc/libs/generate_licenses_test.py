#!/usr/bin/env vpython3

# pylint: disable=protected-access,unused-argument

#  Copyright 2017 The WebRTC project authors. All Rights Reserved.
#
#  Use of this source code is governed by a BSD-style license
#  that can be found in the LICENSE file in the root of the source
#  tree. An additional intellectual property rights grant can be found
#  in the file PATENTS.  All contributing project authors may
#  be found in the AUTHORS file in the root of the source tree.

import unittest
from mock import patch

from generate_licenses import LicenseBuilder


class TestLicenseBuilder(unittest.TestCase):

    @staticmethod
    def _fake_run_gn(buildfile_dir, target):
        return """
    {
      "target1": {
        "deps": [
          "//a/b/third_party/libname1:c",
          "//a/b/third_party/libname2:c(//d/e/f:g)",
          "//a/b/third_party/libname3/c:d(//e/f/g:h)",
          "//a/b/not_third_party/c"
        ]
      }
    }
    """

    def test_parse_library_name(self):
        self.assertEqual(
            LicenseBuilder._parse_library_name('//a/b/third_party/libname1:c'),
            'libname1')
        self.assertEqual(
            LicenseBuilder._parse_library_name(
                '//a/b/third_party/libname2:c(d)'), 'libname2')
        self.assertEqual(
            LicenseBuilder._parse_library_name(
                '//a/b/third_party/libname3/c:d(e)'), 'libname3')
        self.assertEqual(
            LicenseBuilder._parse_library_name('//a/b/not_third_party/c'),
            None)

    def test_parse_library_simple_match(self):
        builder = LicenseBuilder([], [], {}, {})
        self.assertEqual(builder._parse_library('//a/b/third_party/libname:c'),
                         'libname')

    def test_parse_library_regex_no_match_fallbacks_to_default_libname(self):
        lib_dict = {
            'libname:foo.*': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder([], [], lib_dict, {})
        self.assertEqual(
            builder._parse_library('//a/b/third_party/libname:bar_java'),
            'libname')

    def test_parse_library_regex_match(self):
        lib_regex_dict = {
            'libname:foo.*': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder([], [], {}, lib_regex_dict)
        self.assertEqual(
            builder._parse_library('//a/b/third_party/libname:foo_bar_java'),
            'libname:foo.*')

    def test_parse_library_regex_match_with_sub_directory(self):
        lib_regex_dict = {
            'libname/foo:bar.*': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder([], [], {}, lib_regex_dict)
        self.assertEqual(
            builder._parse_library('//a/b/third_party/libname/foo:bar_java'),
            'libname/foo:bar.*')

    def test_parse_library_regex_match_with_star_inside(self):
        lib_regex_dict = {
            'libname/foo.*bar.*': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder([], [], {}, lib_regex_dict)
        self.assertEqual(
            builder._parse_library(
                '//a/b/third_party/libname/fooHAHA:bar_java'),
            'libname/foo.*bar.*')

    @patch('generate_licenses.LicenseBuilder._run_gn', _fake_run_gn)
    def test_get_third_party_libraries_without_regex(self):
        builder = LicenseBuilder([], [], {}, {})
        self.assertEqual(
            builder._get_third_party_libraries('out/arm', 'target1'),
            set(['libname1', 'libname2', 'libname3']))

    @patch('generate_licenses.LicenseBuilder._run_gn', _fake_run_gn)
    def test_get_third_party_libraries_with_regex(self):
        lib_regex_dict = {
            'libname2:c.*': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder([], [], {}, lib_regex_dict)
        self.assertEqual(
            builder._get_third_party_libraries('out/arm', 'target1'),
            set(['libname1', 'libname2:c.*', 'libname3']))

    @patch('generate_licenses.LicenseBuilder._run_gn', _fake_run_gn)
    def test_generate_license_text_fail_if_unknown_library(self):
        lib_dict = {
            'simple_library': ['path/to/LICENSE'],
        }
        builder = LicenseBuilder(['dummy_dir'], ['dummy_target'], lib_dict, {})

        with self.assertRaises(Exception) as context:
            builder.generate_license_text('dummy/dir')

        self.assertEqual(
            context.exception.args[0],
            'Missing licenses for third_party targets: '
            'libname1, libname2, libname3')


if __name__ == '__main__':
    unittest.main()
