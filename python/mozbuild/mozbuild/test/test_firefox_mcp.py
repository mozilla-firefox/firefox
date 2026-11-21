# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import unittest
from unittest.mock import MagicMock, patch

import buildconfig
from mach.registrar import Registrar
from mozunit import main

sys.path.insert(0, os.path.join(buildconfig.topsrcdir, "tools"))


class TestFirefoxMcp(unittest.TestCase):
    def setUp(self):
        self.remove_cats = []
        for cat in ("build", "post-build", "misc", "testing", "devenv"):
            if cat in Registrar.categories:
                continue
            Registrar.register_category(cat, cat, cat)
            self.remove_cats.append(cat)

    def tearDown(self):
        for cat in self.remove_cats:
            del Registrar.categories[cat]
            del Registrar.commands_by_category[cat]

    def _mock_context(self, topobjdir="/obj", binary_path="/obj/dist/bin/firefox"):
        ctx = MagicMock()
        ctx.topobjdir = topobjdir
        ctx.get_binary_path.return_value = binary_path
        return ctx

    @patch("os.path.exists", return_value=True)
    @patch("mach_commands.npx")
    def test_delegates_to_npx_with_build_args(self, mock_npx, mock_exists):
        from mach_commands import firefox_mcp

        mock_context = self._mock_context()
        mock_npx.return_value = 0

        firefox_mcp(mock_context, args=["--pref", "x=1"])

        mock_context.get_binary_path.assert_called_once_with(validate_exists=True)
        mock_npx.assert_called_once_with(
            mock_context,
            [
                "@mozilla/firefox-mcp-moz",
                "--firefox-path",
                "/obj/dist/bin/firefox",
                "--profile-path",
                "/obj/tmp/profile-default",
                "--pref",
                "x=1",
            ],
        )

    @patch("mach_commands.npx")
    def test_starts_without_binary_when_no_build(self, mock_npx):
        from mach_commands import firefox_mcp

        from mozbuild.base import BinaryNotFoundException

        mock_context = self._mock_context()
        mock_context.get_binary_path.side_effect = BinaryNotFoundException(
            "/obj/dist/bin/firefox"
        )
        mock_npx.return_value = 0

        result = firefox_mcp(mock_context, args=[])

        self.assertEqual(result, 0)
        mock_npx.assert_called_once_with(
            mock_context,
            ["@mozilla/firefox-mcp-moz"],
        )

    @patch("mach_commands.npx")
    def test_starts_without_binary_when_no_build_environment(self, mock_npx):
        from mach_commands import firefox_mcp

        from mozbuild.base import BuildEnvironmentNotFoundException

        mock_context = self._mock_context()
        mock_context.get_binary_path.side_effect = BuildEnvironmentNotFoundException(
            "no build environment"
        )
        mock_npx.return_value = 0

        result = firefox_mcp(mock_context, args=[])

        self.assertEqual(result, 0)
        mock_npx.assert_called_once_with(
            mock_context,
            ["@mozilla/firefox-mcp-moz"],
        )

    @patch("os.path.exists", return_value=True)
    @patch("mach_commands.npx")
    def test_works_with_no_extra_args(self, mock_npx, mock_exists):
        from mach_commands import firefox_mcp

        mock_context = self._mock_context()
        mock_npx.return_value = 0

        firefox_mcp(mock_context, args=[])

        mock_npx.assert_called_once_with(
            mock_context,
            [
                "@mozilla/firefox-mcp-moz",
                "--firefox-path",
                "/obj/dist/bin/firefox",
                "--profile-path",
                "/obj/tmp/profile-default",
            ],
        )

    @patch("os.path.exists", return_value=True)
    @patch("mach_commands.npx")
    def test_profile_path_derives_from_topobjdir(self, mock_npx, mock_exists):
        from mach_commands import firefox_mcp

        mock_context = self._mock_context(
            topobjdir="/builds/objdir",
            binary_path="/somewhere/else/dist/bin/firefox",
        )
        mock_npx.return_value = 0

        firefox_mcp(mock_context, args=[])

        call_args = mock_npx.call_args[0][1]
        profile_idx = call_args.index("--profile-path")
        self.assertEqual(
            call_args[profile_idx + 1], "/builds/objdir/tmp/profile-default"
        )

    @patch("os.path.exists", return_value=True)
    @patch("mach_commands.npx")
    def test_skips_binary_detection_when_firefox_path_provided(
        self, mock_npx, mock_exists
    ):
        from mach_commands import firefox_mcp

        mock_context = self._mock_context()
        mock_npx.return_value = 0

        firefox_mcp(mock_context, args=["--firefox-path", "/custom/firefox"])

        mock_context.get_binary_path.assert_not_called()
        mock_npx.assert_called_once_with(
            mock_context,
            ["@mozilla/firefox-mcp-moz", "--firefox-path", "/custom/firefox"],
        )

    @patch("mach_commands.firefox_mcp")
    def test_firefox_devtools_mcp_alias(self, mock_firefox_mcp):
        from mach_commands import firefox_devtools_mcp

        mock_context = self._mock_context()
        mock_firefox_mcp.return_value = 0

        result = firefox_devtools_mcp(mock_context, args=["--headless"])

        self.assertEqual(result, 0)
        mock_firefox_mcp.assert_called_once_with(mock_context, ["--headless"])


if __name__ == "__main__":
    main()
