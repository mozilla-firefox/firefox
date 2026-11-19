#!/usr/bin/env vpython3
# Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

import argparse
import io
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

import generate_changelog

# A commit whose subject, bug and review URL contain markup.
_GIT_LOG = ('abc1234 author@webrtc.org Fix <b>bold</b> handling in pc/\n'
            'Bug: webrtc:1"><script>alert(1)</script>\n'
            'Reviewed-on: https://webrtc-review.googlesource.com/c/src/+/1"x\n'
            '--WEBRTC-COMMIT-DELIMITER--\n')


def _run_main(*args):
    with mock.patch.object(sys, 'argv', ['generate_changelog.py', *args]):
        generate_changelog.main()


class GenerateChangelogTest(unittest.TestCase):

    def setUp(self):
        self._temp_dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self._temp_dir)

    def test_branch_number_accepts_digits(self):
        self.assertEqual(generate_changelog.branch_number('7727'), '7727')

    def test_branch_number_rejects_non_digits(self):
        for value in ('', '-1', '7727 ', '7727;id', '$(id)', '`id`',
                      '\uff17\uff17'):
            with self.subTest(value=value):
                with self.assertRaises(argparse.ArgumentTypeError):
                    generate_changelog.branch_number(value)

    def test_milestone_name(self):
        self.assertEqual(generate_changelog.milestone_name('M148'), 'M148')
        self.assertEqual(generate_changelog.milestone_name('148'), 'M148')
        for value in ('', 'M', 'm148', 'M148 beta', 'M148$(id)'):
            with self.subTest(value=value):
                with self.assertRaises(argparse.ArgumentTypeError):
                    generate_changelog.milestone_name(value)

    def test_format_summary_escapes_text(self):
        summary = ('- Add `RtpSender` API\n'
                   '\n'
                   '* Fix <script>alert("x")</script> & $(id)\n')
        self.assertEqual(
            generate_changelog.format_summary(summary),
            '<ul><li>Add `RtpSender` API</li>'
            '<li>Fix &lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt; '
            '&amp; $(id)</li></ul>')

    def test_format_summary_ignores_blank_lines(self):
        self.assertEqual(generate_changelog.format_summary(' \n\n'), '')

    def test_generate_html_escapes_commit_data(self):
        commits = generate_changelog.parse_log(_GIT_LOG)
        report = generate_changelog.generate_html(commits, '7727', '7778')
        self.assertNotIn('<b>', report)
        self.assertNotIn('<script>', report)
        self.assertIn('Fix &lt;b&gt;bold&lt;/b&gt; handling in pc/', report)
        self.assertIn('/issues/1&quot;&gt;&lt;script&gt;', report)
        self.assertIn('/c/src/+/1&quot;x"', report)
        self.assertIn('AI_SUMMARY_PLACEHOLDER', report)

    @mock.patch.object(sys, 'stdout', new_callable=io.StringIO)
    @mock.patch.object(generate_changelog.subprocess, 'check_output')
    def test_main_reads_summary_from_file(self, check_output, _):
        check_output.return_value = _GIT_LOG.encode('utf-8')
        summary_file = os.path.join(self._temp_dir, 'summary.txt')
        output_file = os.path.join(self._temp_dir, 'changelog.html')
        with open(summary_file, 'w', encoding='utf-8') as summary_fp:
            summary_fp.write('- Faster ICE restarts $(touch pwned)\n')

        _run_main('7727', '7778', output_file, '--summary_file', summary_file,
                  '--milestone', '148')

        check_output.assert_called_once_with([
            'git', 'log', '--format=%h %ae %s%n%b--WEBRTC-COMMIT-DELIMITER--',
            'branch-heads/7727..branch-heads/7778'
        ])
        with open(output_file, encoding='utf-8') as output_fp:
            report = output_fp.read()
        self.assertIn('WebRTC Changelog M148', report)
        self.assertIn('<ul><li>Faster ICE restarts $(touch pwned)</li></ul>',
                      report)

    @mock.patch.object(sys, 'stderr', new_callable=io.StringIO)
    @mock.patch.object(generate_changelog.subprocess, 'check_output')
    def test_main_rejects_invalid_branch_before_running_git(
            self, check_output, _):
        with self.assertRaises(SystemExit):
            _run_main('7727;id', '7778', 'changelog.html')
        check_output.assert_not_called()


if __name__ == '__main__':
    unittest.main()
