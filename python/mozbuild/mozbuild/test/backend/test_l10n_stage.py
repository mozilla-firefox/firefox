# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""What `stage_locale` produces for a jar manifest that selects sources by
locale, recorded so that a change to the locale model has to keep producing
the same output.
"""

import tempfile
from pathlib import Path

from mozunit import main

from mozbuild.action.l10n_stage import stage_locale
from mozbuild.backend.recursivemake import RecursiveMakeBackend
from mozbuild.test.backend.common import BackendTester


class TestL10nStage(BackendTester):
    def setUp(self):
        super().setUp()
        self.env = self._consume("l10n-stage", RecursiveMakeBackend)
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def _stage(self, locale, mode="langpack"):
        root = Path(self._tmp.name) / f"{locale}-{mode}"
        merge = root / "merge"
        dest = root / "dest"
        # A locale other than en-US reads sources marked % from the merge tree.
        merge.mkdir(parents=True, exist_ok=True)
        (merge / "extra.ftl").write_text(f"extra-string = {locale}\n", encoding="utf-8")
        stage_locale(
            locale,
            Path(self.env.topobjdir) / "l10n-manifest.json",
            merge,
            dest,
            topsrcdir=Path(self.env.topsrcdir),
            topobjdir=Path(self.env.topobjdir),
            mode=mode,
        )
        return dest

    def _read(self, dest, relpath):
        path = dest / relpath
        return path.read_text(encoding="utf-8") if path.exists() else None

    def _chrome(self, dest, locale, name):
        return self._read(dest, f"chrome/browser/locale/{locale}/testing/{name}")

    def test_ab_cd_substitutes_into_destinations(self):
        dest = self._stage("de")

        self.assertEqual(self._chrome(dest, "de", "plain.txt"), "plain en-US\n")

    def test_ab_cd_substitutes_into_chrome_registrations(self):
        dest = self._stage("de")
        manifest = self._read(dest, "chrome/browser.manifest")

        self.assertIn("locale testing de", manifest)

    def test_an_exact_marketplace_mapping_selects_its_source(self):
        dest = self._stage("de")

        self.assertEqual(self._chrome(dest, "de", "android.png"), "android de\n")

    def test_a_wildcard_marketplace_mapping_selects_its_source(self):
        dest = self._stage("es-MX")

        self.assertEqual(self._chrome(dest, "es-MX", "android.png"), "android es-ES\n")

    def test_an_unmapped_locale_falls_back(self):
        dest = self._stage("ja")

        self.assertEqual(self._chrome(dest, "ja", "android.png"), "android en-US\n")
        self.assertEqual(self._chrome(dest, "ja", "ios.svg"), "ios en-US\n")

    def test_android_and_ios_resolve_independently(self):
        de = self._stage("de")
        fr = self._stage("fr")

        self.assertEqual(self._chrome(de, "de", "android.png"), "android de\n")
        self.assertEqual(self._chrome(de, "de", "ios.svg"), "ios en-US\n")
        self.assertEqual(self._chrome(fr, "fr", "android.png"), "android en-US\n")
        self.assertEqual(self._chrome(fr, "fr", "ios.svg"), "ios fr\n")

    def test_chrome_mode_stages_the_jar_sections(self):
        dest = self._stage("de", mode="chrome")

        self.assertEqual(self._chrome(dest, "de", "plain.txt"), "plain en-US\n")

    def test_the_translated_only_section_is_staged_for_other_locales(self):
        dest = self._stage("de")

        self.assertEqual(
            self._read(dest, "localization/de/testing/extra"), "extra-string = de\n"
        )

    def test_the_translated_only_section_for_en_us(self):
        dest = self._stage("en-US")

        # Recorded as it behaves today, not as the jar manifest reads.
        self.assertEqual(
            self._read(dest, "localization/en-US/testing/extra"),
            "extra-string = en-US\n",
        )


if __name__ == "__main__":
    main()
