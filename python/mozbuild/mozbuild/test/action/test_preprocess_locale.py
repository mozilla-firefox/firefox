# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import importlib.util
import io
import os
import re
import shutil
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from textwrap import dedent

import mozunit

TOPSRCDIR = Path(__file__).resolve().parents[5]
SCRIPT = TOPSRCDIR / "toolkit/mozapps/installer/windows/nsis/preprocess-locale.py"

spec = importlib.util.spec_from_file_location("preprocess_locale", SCRIPT)
preprocess_locale = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preprocess_locale)

LINE_RE = re.compile(r'^(?:LangString \^?|!define )(\S+)\s+(?:0\s+)?"(.*)"$')


class TestPreprocessLocale(unittest.TestCase):
    """
    Unit tests for preprocess-locale.py, converting installer Fluent files to
    NSIS locale files.
    """

    def setUp(self):
        self.tmpdir = tempfile.mkdtemp()
        self.en_us = os.path.join(self.tmpdir, "en-US")
        self.locale = os.path.join(self.tmpdir, "locale")
        os.makedirs(self.en_us)
        os.makedirs(self.locale)

    def tearDown(self):
        shutil.rmtree(self.tmpdir)

    def write(self, directory, filename, content):
        with open(os.path.join(directory, filename), "w", encoding="utf-8") as fp:
            fp.write(dedent(content))

    def convert(self, en_us, locale=None, ab_cd="it"):
        """
        Converts custom.ftl and returns a dict of NSIS values by name.
        """
        self.write(self.en_us, "custom.ftl", en_us)
        if locale is not None:
            self.write(self.locale, "custom.ftl", locale)
        output = preprocess_locale.get_locale_strings(
            "custom.ftl",
            [self.locale, self.en_us],
            ab_cd,
            "LangString ",
            " 0 ",
            preprocess_locale.default_name,
        )
        return dict(LINE_RE.match(line).groups() for line in output.splitlines())

    def test_escaping(self):
        strings = self.convert(
            """
            test-escaping =
                100$ "quoted"
                second\tline
            test-no-accesskey = Tom & Jerry
            """
        )
        self.assertEqual(
            strings["TEST_ESCAPING"], '100$$ $\\"quoted$\\"$\\r$\\nsecond$\\tline'
        )
        # Ampersands are only escaped in labels with an accesskey.
        self.assertEqual(strings["TEST_NO_ACCESSKEY"], "Tom & Jerry")

    def test_variables_and_literals(self):
        strings = self.convert(
            """
            test-brand = { $BrandShortName } and { $BrandFullNameDA }
            test-click = Continue. { $ClickInstructions }
            test-version = Requires { $MinSupportedVer } and { $MinSupportedCPU }
            test-file =
                File:

                { $FilePath }
            test-space = Space required:{ " " }
            test-line-start =
                First line
                { "[" }OK]
            """
        )
        self.assertEqual(strings["TEST_BRAND"], "$BrandShortName and $BrandFullNameDA")
        self.assertEqual(strings["TEST_CLICK"], "Continue. $_CLICK")
        self.assertEqual(
            strings["TEST_VERSION"],
            "Requires ${MinSupportedVer} and ${MinSupportedCPU}",
        )
        self.assertEqual(strings["TEST_FILE"], "File:$\\r$\\n$\\r$\\n$0")
        self.assertEqual(strings["TEST_SPACE"], "Space required: ")
        self.assertEqual(strings["TEST_LINE_START"], "First line$\\r$\\n[OK]")

    def test_accesskey_inline(self):
        en_us = """
            test-browse = Browse…
                .accesskey = r
            test-desktop = On my desktop
                .accesskey = d
            test-next = Next >
                .accesskey = N
            test-terms = Terms & Conditions
                .accesskey = T
            """
        locale = """
            test-browse = Sfoglia…
                .accesskey = f
            test-desktop = Auf dem Desktop
                .accesskey = D
            test-next = Avanti >
                .accesskey = A
            test-terms = Termini & condizioni
                .accesskey = c
            """
        strings = self.convert(en_us, locale)
        self.assertEqual(strings["TEST_BROWSE"], "S&foglia…")
        # A case-sensitive match is preferred to an earlier one.
        self.assertEqual(strings["TEST_DESKTOP"], "Auf dem &Desktop")
        # Otherwise, the first case-insensitive match is used.
        self.assertEqual(strings["TEST_NEXT"], "&Avanti >")
        self.assertEqual(strings["TEST_TERMS"], "Termini && &condizioni")

    def test_accesskey_parentheses(self):
        en_us = """
            test-browse = Browse…
                .accesskey = r
            test-next = Next >
                .accesskey = N
            test-options = { $BrandShortName } Options
                .accesskey = O
            """
        locale = """
            test-browse = 参照…
                .accesskey = R
            test-next = 次へ >
                .accesskey = N
            test-options = { $BrandShortName } のオプション
                .accesskey = B
            """
        strings = self.convert(en_us, locale, ab_cd="ja")
        self.assertEqual(strings["TEST_BROWSE"], "参照(&R)…")
        self.assertEqual(strings["TEST_NEXT"], "次へ(&N) >")
        # The accesskey is never searched in variable names.
        self.assertEqual(strings["TEST_OPTIONS"], "$BrandShortName のオプション(&B)")

        strings = self.convert(en_us, locale.replace("参照", "Öffnen"), "de")
        self.assertEqual(strings["TEST_BROWSE"], "Öffnen (&R)…")

    def test_accesskey_fallback(self):
        en_us = """
            test-missing = Next >
                .accesskey = N
            test-quoted = Next >
                .accesskey = N
            test-empty = Next >
                .accesskey = N
            """
        locale = """
            test-missing = Avanti >
            test-quoted = Weiter >
                .accesskey = { "W" }
            test-empty = Avanti >
                .accesskey = { "" }
            """
        strings = self.convert(en_us, locale)
        self.assertEqual(strings["TEST_MISSING"], "Ava&nti >")
        self.assertEqual(strings["TEST_QUOTED"], "&Weiter >")
        self.assertEqual(strings["TEST_EMPTY"], "Ava&nti >")

    def test_message_fallback(self):
        en_us = """
            test-translated = Install
            test-missing = Uninstall
            test-select = { $BrandShortName } Setup
            test-term = Cancel
            test-variable = Install { $BrandShortName }
            test-no-value = Close
            test-accesskey = Next >
                .accesskey = N
            """
        locale = """
            test-translated = Installa
            test-select =
                { $count ->
                   *[other] Installazione
                }
            test-term = { -brand-short-name }
            test-variable = Installa { $BrandShortNam }
            test-no-value =
                .label = Chiudi
            test-accesskey = Avanti >
                .accesskey = { $key }
            test-junk = { broken
            """
        stderr = io.StringIO()
        with redirect_stderr(stderr):
            strings = self.convert(en_us, locale)
        self.assertEqual(strings["TEST_TRANSLATED"], "Installa")
        self.assertEqual(strings["TEST_MISSING"], "Uninstall")
        self.assertEqual(strings["TEST_SELECT"], "$BrandShortName Setup")
        self.assertEqual(strings["TEST_TERM"], "Cancel")
        self.assertEqual(strings["TEST_VARIABLE"], "Install $BrandShortName")
        self.assertEqual(strings["TEST_NO_VALUE"], "Close")
        self.assertEqual(strings["TEST_ACCESSKEY"], "&Next >")
        warnings = stderr.getvalue()
        self.assertIn("test-select uses unsupported syntax", warnings)
        self.assertIn("test-term uses unsupported syntax", warnings)
        self.assertIn("test-variable uses unsupported syntax", warnings)
        self.assertIn("test-no-value has no value", warnings)
        self.assertIn("test-accesskey.accesskey uses unsupported syntax", warnings)
        # Messages not in en-US are ignored.
        self.assertNotIn("TEST_JUNK", strings)

    def test_invalid_reference(self):
        with self.assertRaisesRegex(
            preprocess_locale.InvalidReference, "Missing en-US file"
        ):
            preprocess_locale.get_locale_strings(
                "custom.ftl",
                [self.locale, self.en_us],
                "it",
                "LangString ",
                " 0 ",
                preprocess_locale.default_name,
            )
        with self.assertRaisesRegex(
            preprocess_locale.InvalidReference, "Invalid syntax"
        ):
            self.convert(
                """
                test-valid = Valid
                test-broken = { $BrandShortName
                """
            )

    def test_preprocess_locale_files(self):
        self.write(
            self.en_us,
            "override.ftl",
            """
            nsis-back-btn = < Back
                .accesskey = B
            nsis-components-sub-text2-no-inst-types = Select components:
            """,
        )
        self.write(self.locale, "override.ftl", "nsis-back-btn = < Indietro\n")
        self.write(self.en_us, "mui.ftl", "mui-text-finish-title = Complete\n")
        self.write(self.en_us, "custom.ftl", "upgrade-button = Upgrade\n")
        config_dir = os.path.join(self.tmpdir, "config")
        os.makedirs(config_dir)
        preprocess_locale.preprocess_locale_files(
            config_dir, [self.locale, self.en_us], "it"
        )

        def read(filename):
            with open(os.path.join(config_dir, filename), "rb") as fp:
                data = fp.read()
            self.assertTrue(data.startswith(b"\xff\xfe"))
            return data.decode("utf-16")

        override = read("overrideLocale.nsh")
        self.assertIn('LangString ^BackBtn 0  "< Indietro (&B)"', override)
        self.assertIn(
            'LangString ^ComponentsSubText2_NoInstTypes 0  "Select components:"',
            override,
        )
        mui = read("baseLocale.nsh")
        self.assertIn('!define MUI_TEXT_FINISH_TITLE  "Complete"', mui)
        self.assertTrue(mui.rstrip().endswith("!insertmacro MOZ_MUI_LANGUAGEFILE_END"))
        self.assertIn(
            'LangString UPGRADE_BUTTON 0  "Upgrade"', read("customLocale.nsh")
        )

    def test_unknown_override(self):
        with self.assertRaisesRegex(ValueError, "Unknown NSIS override string"):
            preprocess_locale.override_name("nsis-not-a-string")


if __name__ == "__main__":
    mozunit.main()
