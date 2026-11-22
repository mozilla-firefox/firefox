# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from mach.util import win_to_msys_path
from mozunit import main

from mozboot import rust
from mozboot.base import BaseBootstrapper

SH = shutil.which("sh")
TCSH = shutil.which("tcsh")


def msys_path(path):
    if sys.platform.startswith("win"):
        return win_to_msys_path(path)
    return path


class TestMozbuildBin(unittest.TestCase):
    def setUp(self):
        self.home = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.home)
        self.state_dir = self.home / ".mozbuild"
        self.state_dir.mkdir()
        self.bin_dir = self.state_dir / "bin"

        home_patcher = mock.patch.object(Path, "home", return_value=self.home)
        home_patcher.start()
        self.addCleanup(home_patcher.stop)
        env_patcher = mock.patch.dict(
            os.environ,
            {"PATH": "/usr/bin:/bin", "ZDOTDIR": "", "XDG_CONFIG_HOME": ""},
        )
        env_patcher.start()
        self.addCleanup(env_patcher.stop)

        self.bootstrapper = BaseBootstrapper()
        self.bootstrapper.state_dir = self.state_dir
        self.bootstrapper.prompt_yesno = mock.MagicMock(return_value=True)

    def _set_shell(self, shell):
        os.environ["SHELL"] = f"/bin/{shell}"

    def _source_twice(self, shell, config):
        """Return PATH after sourcing config twice in shell."""
        if shell == "tcsh":
            cmd = [
                TCSH,
                "-fc",
                'setenv HOME "$1"; setenv PATH /usr/bin:/bin; '
                'source "$2"; source "$2"; printf %s "$PATH"',
            ]
        else:
            cmd = [
                SH,
                "-ec",
                'HOME=$1; PATH=/usr/bin:/bin; . "$2"; . "$2"; printf %s "$PATH"',
                "sh",
            ]
        return subprocess.check_output(
            cmd + [str(msys_path(self.home)), str(msys_path(config))],
            text=True,
        )

    def test_already_in_path(self):
        self._set_shell("zsh")
        self.bin_dir.mkdir()
        os.environ["PATH"] = f"/usr/bin{os.pathsep}{self.bin_dir}"

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.bootstrapper.prompt_yesno.assert_not_called()
        self.assertFalse((self.home / ".zshenv").exists())

    def test_zsh(self):
        self._set_shell("zsh")
        zshenv = self.home / ".zshenv"
        zshenv.write_text("export FOO=1")

        self.bootstrapper.ensure_mozbuild_bin_in_path()
        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(zshenv.read_text(), 'export FOO=1\n. "$HOME/.mozbuild/env"\n')

    def test_zsh_config_not_utf8(self):
        self._set_shell("zsh")
        zshenv = self.home / ".zshenv"
        zshenv.write_bytes(b"# caf\xe9 \x81\n")

        self.bootstrapper.ensure_mozbuild_bin_in_path()
        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(
            zshenv.read_bytes(), b'# caf\xe9 \x81\n. "$HOME/.mozbuild/env"\n'
        )

    def test_env_non_ascii_state_dir(self):
        self._set_shell("zsh")
        other = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, other)
        self.bootstrapper.state_dir = other / "mozbüild"
        self.bootstrapper.state_dir.mkdir()

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        env = (self.bootstrapper.state_dir / "env").read_bytes()
        self.assertIn(f'"{win_to_msys_path(other)}/mozbüild/bin"'.encode(), env)
        self.assertIn(
            f'. "{win_to_msys_path(other)}/mozbüild/env"\n'.encode(),
            (self.home / ".zshenv").read_bytes(),
        )

    @unittest.skipUnless(SH, "Needs a POSIX shell")
    def test_env_adds_bin_dir_once(self):
        self._set_shell("zsh")
        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(
            self._source_twice("sh", self.state_dir / "env"),
            f"{msys_path(self.bin_dir)}:/usr/bin:/bin",
        )

    def test_bash_with_bash_profile(self):
        self._set_shell("bash")
        (self.home / ".bash_profile").write_text("")

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        line = '. "$HOME/.mozbuild/env"\n'
        self.assertEqual((self.home / ".bashrc").read_text(), line)
        self.assertEqual((self.home / ".bash_profile").read_text(), line)
        self.assertFalse((self.home / ".profile").exists())

    def _check_special_characters_in_state_dir(self, shell, config):
        self._set_shell(shell)
        other = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, other)
        names = [
            "build$cash",
            "build'quote",
            "build`printf expanded`",
            "build$(printf expanded)",
            "build space",
            "build!bang",
        ]
        # tcsh patterns can't match a literal "[", so env.csh would add it again.
        if shell != "tcsh":
            names.append("build[x]")
        if not sys.platform.startswith("win"):
            names += ['build"quote', r"build\backslash"]
        for parent in (self.home, other):
            for name in names:
                with self.subTest(parent=parent, name=name):
                    self.bootstrapper.state_dir = parent / name
                    self.bootstrapper.state_dir.mkdir()
                    config.unlink(missing_ok=True)

                    self.bootstrapper.ensure_mozbuild_bin_in_path()

                    bin_dir = msys_path(self.bootstrapper.mozbuild_bin_dir())
                    self.assertEqual(
                        self._source_twice(shell, config), f"{bin_dir}:/usr/bin:/bin"
                    )

    @unittest.skipUnless(SH, "Needs a POSIX shell")
    def test_shell_special_characters_in_state_dir(self):
        self._check_special_characters_in_state_dir("bash", self.home / ".bashrc")

    @unittest.skipUnless(TCSH, "Needs tcsh")
    def test_tcsh_special_characters_in_state_dir(self):
        self._check_special_characters_in_state_dir("tcsh", self.home / ".cshrc")

    def test_bash_without_login_config(self):
        self._set_shell("bash")

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        line = '. "$HOME/.mozbuild/env"\n'
        self.assertEqual((self.home / ".bashrc").read_text(), line)
        self.assertEqual((self.home / ".profile").read_text(), line)
        self.assertFalse((self.home / ".bash_profile").exists())

    def test_sh(self):
        self._set_shell("sh")

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(
            (self.home / ".profile").read_text(), '. "$HOME/.mozbuild/env"\n'
        )

    def test_unsupported_shell(self):
        self._set_shell("nu")

        with mock.patch("builtins.print") as print_mock:
            self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.bootstrapper.prompt_yesno.assert_not_called()
        self.assertIn(str(self.bin_dir), print_mock.call_args.args[0])
        self.assertEqual(list(self.home.iterdir()), [self.state_dir])
        self.assertEqual(list(self.state_dir.iterdir()), [])

    def test_fish(self):
        self._set_shell("fish")

        self.bootstrapper.ensure_mozbuild_bin_in_path()
        self.bootstrapper.ensure_mozbuild_bin_in_path()

        config = self.home / ".config" / "fish" / "conf.d" / "mozbuild.fish"
        self.assertEqual(
            config.read_text(),
            'fish_add_path --prepend --path "$HOME/.mozbuild/bin"\n',
        )

    def test_tcsh(self):
        self._set_shell("tcsh")

        self.bootstrapper.ensure_mozbuild_bin_in_path()
        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(
            (self.home / ".cshrc").read_text(),
            "source \"$HOME/\"'.mozbuild/env.csh'\n",
        )
        self.assertFalse((self.home / ".tcshrc").exists())

    def test_tcsh_with_tcshrc(self):
        self._set_shell("tcsh")
        (self.home / ".tcshrc").write_text("")

        self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertEqual(
            (self.home / ".tcshrc").read_text(),
            "source \"$HOME/\"'.mozbuild/env.csh'\n",
        )
        self.assertFalse((self.home / ".cshrc").exists())

    def test_config_not_writable(self):
        self._set_shell("zsh")
        (self.home / ".zshenv").mkdir()

        with mock.patch("builtins.print") as print_mock:
            self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertIn('. "$HOME/.mozbuild/env"', print_mock.call_args.args[0])

    def test_env_not_writable(self):
        self._set_shell("zsh")
        (self.state_dir / "env").mkdir()

        with mock.patch("builtins.print") as print_mock:
            self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.bootstrapper.prompt_yesno.assert_not_called()
        self.assertIn(str(self.bin_dir), print_mock.call_args.args[0])
        self.assertFalse((self.home / ".zshenv").exists())

    def test_declined(self):
        self._set_shell("zsh")
        self.bootstrapper.prompt_yesno.return_value = False

        with mock.patch("builtins.print") as print_mock:
            self.bootstrapper.ensure_mozbuild_bin_in_path()

        self.assertIn(
            f'{self.home / ".zshenv"}:\n\n    . "$HOME/.mozbuild/env"',
            print_mock.call_args.args[0],
        )
        self.assertFalse((self.home / ".zshenv").exists())

    def _check_tools_installed(self, tools_attr, tools_installed, suffix):
        setattr(self.bootstrapper, tools_attr, ("some-cli",))
        other_bin = self.home / "other-bin"
        os.environ["PATH"] = str(other_bin)
        name = "some-cli" + suffix

        def add_tool(bin_dir):
            bin_dir.mkdir()
            (bin_dir / name).write_text("")
            (bin_dir / name).chmod(0o755)

        add_tool(other_bin)
        self.assertFalse(tools_installed())
        add_tool(self.bin_dir)
        self.assertTrue(tools_installed())

    def test_npm_tools_installed(self):
        self._check_tools_installed(
            "NPM_TOOLS",
            self.bootstrapper.npm_tools_installed,
            ".cmd" if sys.platform.startswith("win") else "",
        )

    def test_cargo_tools_installed(self):
        self._check_tools_installed(
            "CARGO_TOOLS", self.bootstrapper.cargo_tools_installed, rust.exe_suffix()
        )

    def test_warn_shadowed_tools(self):
        self.bootstrapper.CARGO_TOOLS = ("some-cli",)
        self.bootstrapper.NPM_TOOLS = ()
        name = "some-cli" + rust.exe_suffix()
        other_bin = self.home / "other-bin"
        for d in (other_bin, self.bin_dir):
            d.mkdir()
            (d / name).write_text("")
            (d / name).chmod(0o755)

        def warnings(path):
            os.environ["PATH"] = os.pathsep.join(str(p) for p in path)
            with mock.patch("builtins.print") as print_mock:
                self.bootstrapper.warn_shadowed_tools()
            return [c.args[0] for c in print_mock.call_args_list]

        self.assertEqual(warnings([self.bin_dir, other_bin]), [])
        self.assertEqual(warnings([other_bin]), [])
        (warning,) = warnings([other_bin, self.bin_dir])
        self.assertIn(str(other_bin / name).lower(), warning.lower())

        # A link to the bootstrapped tool earlier in PATH isn't shadowing it.
        if hasattr(os, "symlink") and sys.platform != "win32":
            (other_bin / name).unlink()
            (other_bin / name).symlink_to(self.bin_dir / name)
            self.assertEqual(warnings([other_bin, self.bin_dir]), [])

    def test_cargo_tools(self):
        artifacts = self.home / "artifacts"
        exe = artifacts / "some-cli" / ("some-cli" + rust.exe_suffix())
        exe.parent.mkdir(parents=True)
        exe.write_text("new")
        self.bin_dir.mkdir()
        (self.bin_dir / exe.name).write_text("old")

        self.bootstrapper.CARGO_TOOLS = ("some-cli",)
        self.bootstrapper.install_toolchain_artifact = lambda tool: str(
            artifacts / tool
        )
        self.bootstrapper.ensure_cargo_tools()

        self.assertEqual((self.bin_dir / exe.name).read_text(), "new")

    @unittest.skipIf(sys.platform.startswith("win"), "Needs a POSIX shell")
    def test_npm_tools(self):
        artifacts = self.home / "artifacts dir"
        node = artifacts / "node" / "bin" / "node"
        node.parent.mkdir(parents=True)
        node.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        node.chmod(0o755)
        tool_dir = artifacts / "some-cli"
        tool_dir.mkdir()
        (tool_dir / "package.json").write_text(
            json.dumps({"name": "@scope/some-cli", "bin": "dist/cli.js"})
        )
        launcher = self.bin_dir / "some-cli"
        self.bin_dir.mkdir()
        launcher.write_text("old")

        self.bootstrapper.NPM_TOOLS = ("some-cli",)
        self.bootstrapper.install_toolchain_artifact = lambda tool: str(
            artifacts / tool
        )
        self.bootstrapper.ensure_npm_tools()

        output = subprocess.check_output([launcher, "a b", "c"], text=True)
        self.assertEqual(output, f"{tool_dir / 'dist' / 'cli.js'}\na b\nc\n")


if __name__ == "__main__":
    main()
