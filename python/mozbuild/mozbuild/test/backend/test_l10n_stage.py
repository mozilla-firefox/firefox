# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""What `stage_locale` produces for a jar manifest, recorded so that a change
to the locale model has to keep producing the same output.
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
        merge.mkdir(parents=True, exist_ok=True)
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

    def test_chrome_mode_stages_the_jar_sections(self):
        dest = self._stage("de", mode="chrome")

        self.assertEqual(self._chrome(dest, "de", "plain.txt"), "plain en-US\n")


if __name__ == "__main__":
    main()
