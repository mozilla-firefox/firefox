# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import sys
import unittest
from pathlib import Path
from shutil import rmtree
from tempfile import mkdtemp
from unittest import mock

import mozunit
from mozfile import json

from mozbuild.action import nsis_stage

_DEFINES = {"ALLDEFINES": {"MOZ_APP_NAME": "firefox"}}


class FakeStamp:
    """Stands in for the FileAvoidWrite that file_generate hands `generate`."""

    def __init__(self):
        self.data = b""

    def write(self, buf):
        self.data += buf.encode() if isinstance(buf, str) else buf


class TestNsisStage(unittest.TestCase):
    def setUp(self):
        self.tmpdir = Path(mkdtemp())

    def tearDown(self):
        rmtree(self.tmpdir)

    def _write(self, path, content=""):
        full = self.tmpdir / path
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_text(content, encoding="utf-8")
        return str(full)

    def test_stage_copies_wipes_and_preprocesses(self):
        installer = self._write("src/nsis/installer.nsi", "nsi")
        plugin = self._write("plugins/UAC.dll", "dll")
        defines_in = self._write("src/nsis/defines.nsi.in", "in")
        ppl = self._write("toolkit/preprocess-locale.py")
        config_dir = self.tmpdir / "instgen"
        config_dir.mkdir()
        (config_dir / "stale").write_text("old", encoding="utf-8")

        calls = []

        def fake_check_call(argv):
            calls.append(argv)
            return 0

        with mock.patch.object(
            nsis_stage.subprocess, "check_call", side_effect=fake_check_call
        ), mock.patch.object(nsis_stage, "Preprocessor") as pp_cls, mock.patch.object(
            nsis_stage.buildconfig, "defines", _DEFINES
        ):
            rc = nsis_stage.main([
                "--config-dir",
                str(config_dir),
                "--install",
                installer,
                "--install",
                plugin,
                "--defines-in",
                defines_in,
                "--defines-out",
                str(config_dir / "defines.nsi"),
                "--topsrcdir",
                str(self.tmpdir),
                "--preprocess-locale-script",
                ppl,
                "--locale-arg",
                "browser/locales/en-US/installer",
                "--ab-cd",
                "en-US",
                "--preprocess-locale",
                "--single-file",
                "nsisstrings.ftl",
                "nsisstrings.nlf",
                "--convert-utf8",
                "extensionsLocale.nsh",
                str(config_dir / "extensionsLocale.nsh"),
                "-DFOO=1",
            ])

        self.assertEqual(rc, 0)
        self.assertFalse((config_dir / "stale").exists())
        self.assertEqual(
            (config_dir / "installer.nsi").read_text(encoding="utf-8"), "nsi"
        )
        self.assertEqual((config_dir / "UAC.dll").read_text(encoding="utf-8"), "dll")

        pp = pp_cls.return_value
        pp.context.update.assert_called_once_with(_DEFINES["ALLDEFINES"])
        # Unrecognized -D arguments are forwarded to the preprocessor.
        pp.handleCommandLine.assert_called_once()
        self.assertEqual(
            pp.handleCommandLine.call_args[0][0],
            [
                "-Fsubstitution",
                "-DFOO=1",
                defines_in,
                "-o",
                str(config_dir / "defines.nsi"),
            ],
        )

        self.assertEqual(
            calls,
            [
                [
                    sys.executable,
                    ppl,
                    "--preprocess-locale",
                    str(self.tmpdir),
                    "browser/locales/en-US/installer",
                    "en-US",
                    str(config_dir),
                ],
                [
                    sys.executable,
                    ppl,
                    "--preprocess-single-file",
                    str(self.tmpdir),
                    "browser/locales/en-US/installer",
                    "--ab-cd=en-US",
                    str(config_dir),
                    "nsisstrings.ftl",
                    "nsisstrings.nlf",
                ],
                [
                    sys.executable,
                    ppl,
                    "--convert-utf8-utf16le",
                    "extensionsLocale.nsh",
                    str(config_dir / "extensionsLocale.nsh"),
                ],
            ],
        )

    def test_preprocess_failure_propagates(self):
        config_dir = self.tmpdir / "instgen"

        with mock.patch.object(
            nsis_stage.subprocess,
            "check_call",
            side_effect=subprocess.CalledProcessError(4, "preprocess-locale.py"),
        ):
            rc = nsis_stage.main([
                "--config-dir",
                str(config_dir),
                "--topsrcdir",
                str(self.tmpdir),
                "--preprocess-locale-script",
                self._write("toolkit/preprocess-locale.py"),
                "--preprocess-locale",
            ])

        self.assertEqual(rc, 4)

    def test_locale_step_without_script_errors(self):
        config_dir = self.tmpdir / "instgen"
        steps = (
            ["--preprocess-locale"],
            ["--single-file", "nsisstrings.ftl", "nsisstrings.nlf"],
            ["--convert-utf8", "extensionsLocale.nsh", "out.nsh"],
        )

        for step in steps:
            with self.subTest(step=step[0]), self.assertRaises(SystemExit):
                nsis_stage.main(
                    ["--config-dir", str(config_dir), "--topsrcdir", str(self.tmpdir)]
                    + step
                )

    def test_copy_preserves_mtime(self):
        # $(INSTALL) is $(NSINSTALL) -t, which is shutil.copy2. makensis stores
        # the mtime in the installer, so staging must not assign a fresh one.
        installer = self._write("src/nsis/installer.nsi", "nsi")
        os.utime(installer, (1000000, 1000000))
        config_dir = self.tmpdir / "instgen"

        rc = nsis_stage.main([
            "--config-dir",
            str(config_dir),
            "--install",
            installer,
        ])

        self.assertEqual(rc, 0)
        self.assertEqual(
            (config_dir / "installer.nsi").stat().st_mtime,
            Path(installer).stat().st_mtime,
        )

    def test_defines_string_precedes_verbatim(self):
        defines_in = self._write("src/nsis/defines.nsi.in", "in")
        config_dir = self.tmpdir / "instgen"

        with mock.patch.object(nsis_stage, "Preprocessor") as pp_cls, mock.patch.object(
            nsis_stage.buildconfig, "defines", _DEFINES
        ):
            rc = nsis_stage.main([
                "--config-dir",
                str(config_dir),
                "--defines-in",
                defines_in,
                "--defines-out",
                str(config_dir / "defines.nsi"),
                "-DTOPOBJDIR=/o",
                "--defines-string=-DA=1 -DB='a b'",
                "--defines-string=-DC=3",
            ])

        self.assertEqual(rc, 0)
        self.assertEqual(
            pp_cls.return_value.handleCommandLine.call_args[0][0],
            [
                "-Fsubstitution",
                "-DA=1",
                "-DB=a b",
                "-DC=3",
                "-DTOPOBJDIR=/o",
                defines_in,
                "-o",
                str(config_dir / "defines.nsi"),
            ],
        )

    def test_global_defines_reach_the_preprocessor(self):
        # Runs the real Preprocessor, so this covers quoting, dollar signs,
        # value types and precedence rather than the call that seeds them.
        defines_in = self._write(
            "src/nsis/defines.nsi.in",
            "name=@MOZ_APP_DISPLAYNAME@\n"
            "path=@WITH_DOLLAR@\n"
            "bits=@HAVE_64BIT_BUILD@\n"
            "over=@OVERRIDDEN@\n"
            "#ifdef MOZ_MAINTENANCE_SERVICE\n"
            "service=yes\n"
            "#endif\n",
        )
        config_dir = self.tmpdir / "instgen"
        defines = {
            "ALLDEFINES": {
                "MOZ_APP_DISPLAYNAME": "Firefox Nightly",
                "WITH_DOLLAR": "a$b",
                "HAVE_64BIT_BUILD": 1,
                "MOZ_MAINTENANCE_SERVICE": True,
                "OVERRIDDEN": "from-globals",
            }
        }

        with mock.patch.object(nsis_stage.buildconfig, "defines", defines):
            rc = nsis_stage.main([
                "--config-dir",
                str(config_dir),
                "--defines-in",
                defines_in,
                "--defines-out",
                str(config_dir / "defines.nsi"),
                "--defines-string=-DOVERRIDDEN=from-flags",
            ])

        self.assertEqual(rc, 0)
        lines = (config_dir / "defines.nsi").read_text(encoding="utf-8").splitlines()
        self.assertIn("name=Firefox Nightly", lines)
        self.assertIn("path=a$b", lines)
        self.assertIn("bits=1", lines)
        self.assertIn("over=from-flags", lines)
        self.assertIn("service=yes", lines)

    def test_spec_out_requires_repack_l10n_dir(self):
        with self.assertRaises(SystemExit):
            nsis_stage.main([
                "--config-dir",
                str(self.tmpdir / "instgen"),
                "--spec-out",
                str(self.tmpdir / "nsis-stage.json"),
                "--repack-locale-arg",
                "browser/locales/en-US/installer",
            ])

    def test_spec_out_requires_repack_locale_arg(self):
        with mock.patch.object(nsis_stage.argparse.ArgumentParser, "error") as error:
            error.side_effect = SystemExit
            with self.assertRaises(SystemExit):
                nsis_stage.main([
                    "--config-dir",
                    str(self.tmpdir / "instgen"),
                    "--spec-out",
                    str(self.tmpdir / "nsis-stage.json"),
                    "--repack-l10n-dir",
                    "browser/installer",
                ])

        error.assert_called_once_with("--repack-locale-arg is required with --spec-out")

    def test_spec_records_staging_inputs(self):
        installer = self._write("src/nsis/installer.nsi", "nsi")
        defines_in = self._write("src/nsis/defines.nsi.in", "in")
        ppl = self._write("toolkit/preprocess-locale.py")
        config_dir = self.tmpdir / "instgen"
        spec_out = self.tmpdir / "nsis-stage.json"

        with mock.patch.object(nsis_stage.subprocess, "check_call"), mock.patch.object(
            nsis_stage, "Preprocessor"
        ), mock.patch.object(nsis_stage.buildconfig, "defines", _DEFINES):
            rc = nsis_stage.main([
                "--config-dir",
                str(config_dir),
                "--install",
                installer,
                "--defines-in",
                defines_in,
                "--defines-out",
                str(config_dir / "defines.nsi"),
                "--topsrcdir",
                str(self.tmpdir),
                "--preprocess-locale-script",
                ppl,
                "--locale-arg=--l10n-dir=l10n/de/browser/installer",
                "--locale-arg=--l10n-dir=browser/locales/en-US/installer",
                "--ab-cd",
                "de",
                "--preprocess-locale",
                "--single-file",
                "nsisstrings.ftl",
                "nsisstrings.nlf",
                "--convert-utf8",
                "extensionsLocale.nsh",
                str(config_dir / "extensionsLocale.nsh"),
                "--spec-out",
                str(spec_out),
                "--repack-l10n-dir",
                "browser/installer",
                "--repack-locale-arg",
                "browser/locales/en-US/installer",
                "--defines-string=-DA=1",
                "-DAB_CD=de",
            ])

        self.assertEqual(rc, 0)
        self.assertEqual(
            json.loads(spec_out.read_text(encoding="utf-8")),
            {
                "installs": [installer],
                "defines_in": defines_in,
                "defines_out": "defines.nsi",
                "preprocessor_args": ["-DA=1", "-DAB_CD=de"],
                "topsrcdir": str(self.tmpdir),
                "preprocess_locale_script": ppl,
                "repack_locale_args": ["browser/locales/en-US/installer"],
                "preprocess_locale": True,
                "single_files": [["nsisstrings.ftl", "nsisstrings.nlf"]],
                "convert_utf8": [["extensionsLocale.nsh", "extensionsLocale.nsh"]],
                "repack_l10n_dir": "browser/installer",
            },
        )

    def _write_spec(self, **overrides):
        spec = {
            "installs": ["/src/nsis/installer.nsi"],
            "defines_in": "/src/nsis/defines.nsi.in",
            "defines_out": "defines.nsi",
            "preprocessor_args": ["-DA=1", "-DAB_CD=en-US", "-DTOPOBJDIR=/obj"],
            "topsrcdir": "/src",
            "preprocess_locale_script": "/src/toolkit/preprocess-locale.py",
            "repack_locale_args": ["/src/browser/locales/en-US/installer"],
            "preprocess_locale": True,
            "single_files": [["nsisstrings.ftl", "nsisstrings.nlf"]],
            "convert_utf8": [
                ["/src/nsis/extensionsLocale.nsh", "extensionsLocale.nsh"]
            ],
            "repack_l10n_dir": "browser/installer",
        }
        spec.update(overrides)
        path = self.tmpdir / "nsis-stage.json"
        path.write_text(json.dumps(spec), encoding="utf-8")
        return path

    def test_stage_repack_relocates_the_recorded_inputs(self):
        spec_path = self._write_spec()
        config_dir = Path("/obj/installer/windows/l10ngen")

        with mock.patch.object(nsis_stage, "nsis_stage", return_value=0) as stage:
            rc = nsis_stage.stage_repack(
                spec_path=spec_path,
                config_dir=config_dir,
                ab_cd="de",
                real_locale_mergedir=Path("/obj/merged"),
            )

        self.assertEqual(rc, 0)
        stage.assert_called_once_with(
            config_dir=config_dir,
            installs=["/src/nsis/installer.nsi"],
            defines_in="/src/nsis/defines.nsi.in",
            defines_out=str(config_dir / "defines.nsi"),
            preprocessor_args=["-DA=1", "-DAB_CD=de", "-DTOPOBJDIR=/obj"],
            topsrcdir=Path("/src"),
            preprocess_locale_script="/src/toolkit/preprocess-locale.py",
            locale_args=[
                f"--l10n-dir={Path('/obj/merged') / 'browser/installer'}",
                "--l10n-dir=/src/browser/locales/en-US/installer",
            ],
            ab_cd="de",
            preprocess_locale=True,
            single_files=[["nsisstrings.ftl", "nsisstrings.nlf"]],
            convert_utf8=[
                [
                    "/src/nsis/extensionsLocale.nsh",
                    str(config_dir / "extensionsLocale.nsh"),
                ]
            ],
        )

    def test_stage_repack_adds_ab_cd_when_absent(self):
        spec_path = self._write_spec(preprocessor_args=["-DA=1"])

        with mock.patch.object(nsis_stage, "nsis_stage", return_value=0) as stage:
            nsis_stage.stage_repack(
                spec_path=spec_path,
                config_dir=self.tmpdir / "l10ngen",
                ab_cd="de",
                real_locale_mergedir=self.tmpdir / "merged",
            )

        self.assertEqual(
            stage.call_args.kwargs["preprocessor_args"], ["-DA=1", "-DAB_CD=de"]
        )

    def test_stage_repack_requires_repack_l10n_dir(self):
        spec_path = self._write_spec(repack_l10n_dir="")

        with self.assertRaises(ValueError):
            nsis_stage.stage_repack(
                spec_path=spec_path,
                config_dir=self.tmpdir / "l10ngen",
                ab_cd="de",
                real_locale_mergedir=self.tmpdir / "merged",
            )

    def test_digest_frames_names_and_contents(self):
        # Without length framing these two trees hash the same byte stream.
        first = self.tmpdir / "first"
        second = self.tmpdir / "second"
        for d, a, b in ((first, "", "bX"), (second, "b", "X")):
            d.mkdir()
            (d / "a").write_text(a, encoding="utf-8")
            (d / "b").write_text(b, encoding="utf-8")

        self.assertNotEqual(
            nsis_stage.stage_digest(first),
            nsis_stage.stage_digest(second),
        )

    def test_generate_returns_locale_files(self):
        de = self._write("l10n/de/browser/installer/custom.properties")
        en_us = self._write("src/browser/locales/en-US/installer/mui.properties")
        config_dir = self.tmpdir / "instgen"

        deps = nsis_stage.generate(
            FakeStamp(),
            "--config-dir",
            str(config_dir),
            f"--locale-arg=--l10n-dir={Path(de).parent}",
            f"--locale-arg=--l10n-dir={Path(en_us).parent}",
            f"--locale-arg=--l10n-dir={self.tmpdir / 'l10n/de/missing'}",
        )

        self.assertEqual(
            deps, {de, en_us, str(Path(de).parent), str(Path(en_us).parent)}
        )

    def _generate(self, installer, config_dir, generated_mtime):
        def fake_pp(argv):
            out = Path(argv[-1])
            out.write_text("defines", encoding="utf-8")
            os.utime(out, (generated_mtime, generated_mtime))

        stamp = FakeStamp()
        with mock.patch.object(nsis_stage, "Preprocessor") as pp_cls, mock.patch.object(
            nsis_stage.buildconfig, "defines", _DEFINES
        ):
            pp_cls.return_value.handleCommandLine.side_effect = fake_pp
            rc = nsis_stage.generate(
                stamp,
                "--config-dir",
                str(config_dir),
                "--install",
                installer,
                "--defines-in",
                self._write("src/nsis/defines.nsi.in", "in"),
                "--defines-out",
                str(config_dir / "defines.nsi"),
            )
        self.assertEqual(rc, set())
        return stamp.data

    def test_generate_stamp_converges(self):
        # A generated file gets a fresh mtime on every pass. That must not
        # reach the stamp, or every restage would rebuild all three installers.
        installer = self._write("src/nsis/installer.nsi", "nsi")
        os.utime(installer, (1000000, 1000000))
        config_dir = self.tmpdir / "instgen"

        first = self._generate(installer, config_dir, 2000000)
        second = self._generate(installer, config_dir, 3000000)

        self.assertTrue(first)
        self.assertEqual(first, second)

    def test_generate_stamp_ignores_copied_mtime(self):
        installer = self._write("src/nsis/installer.nsi", "nsi")
        config_dir = self.tmpdir / "instgen"

        os.utime(installer, (1000000, 1000000))
        first = self._generate(installer, config_dir, 2000000)
        os.utime(installer, (1500000, 1500000))
        second = self._generate(installer, config_dir, 2000000)

        self.assertEqual(first, second)

    def test_generate_stamp_tracks_copied_contents(self):
        installer = self._write("src/nsis/installer.nsi", "nsi")
        config_dir = self.tmpdir / "instgen"

        os.utime(installer, (1000000, 1000000))
        first = self._generate(installer, config_dir, 2000000)
        Path(installer).write_text("new", encoding="utf-8")
        os.utime(installer, (1000000, 1000000))
        second = self._generate(installer, config_dir, 2000000)

        self.assertNotEqual(first, second)


if __name__ == "__main__":
    mozunit.main()
