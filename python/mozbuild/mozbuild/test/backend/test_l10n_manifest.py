# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
from pathlib import Path

import mozpack.path as mozpath
from mozunit import main

from mozbuild.action.l10n_stage import stage_locale
from mozbuild.backend.recursivemake import RecursiveMakeBackend
from mozbuild.test.backend.common import BackendTester


class TestL10nManifestBackendIntegration(BackendTester):
    def _roots_manifest(self, config_name):
        env = self._get_environment(config_name, srcdir_name="l10n-manifest-roots")
        self._consume("l10n-manifest-roots", RecursiveMakeBackend, env=env)

        manifest_path = mozpath.join(env.topobjdir, "l10n-manifest.json")
        self.assertTrue(os.path.exists(manifest_path), manifest_path)
        return env, manifest_path

    def _written_contexts(self, config_name):
        _, manifest_path = self._roots_manifest(config_name)

        with open(manifest_path, encoding="utf-8") as f:
            raw = json.load(f)

        return {ctx["relsrcdir"]: ctx for ctx in raw["contexts"]}

    def test_manifest_roots_drop_chrome_outside_the_roots(self):
        contexts = self._written_contexts("l10n-manifest-roots")

        self.assertEqual(
            sorted(contexts), ["app/locales", "chrome/locales", "shared/locales"]
        )

        shared = contexts["shared/locales"]
        self.assertEqual(shared["jar_sections"], [])
        self.assertEqual(
            [group["sources"] for group in shared["localized_files"]],
            [["en-US/shared.ini"]],
        )

    def test_no_manifest_roots_writes_every_context(self):
        contexts = self._written_contexts("l10n-manifest-roots-unfiltered")

        self.assertEqual(
            sorted(contexts), ["app/locales", "chrome/locales", "shared/locales"]
        )
        self.assertNotEqual(contexts["shared/locales"]["jar_sections"], [])

    def test_manifest_roots_keep_localized_fluent_outside_the_roots(self):
        env, manifest_path = self._roots_manifest("l10n-manifest-roots")
        root = Path(env.topobjdir)
        merged = root / "merge" / "de" / "chrome" / "chrome"
        merged.mkdir(parents=True)
        (merged / "chrome.ftl").write_text("chrome-string = de\n", encoding="utf-8")
        dest = root / "stage"

        stage_locale(
            "de",
            Path(manifest_path),
            root / "merge" / "de",
            dest,
            topsrcdir=Path(env.topsrcdir),
            topobjdir=root,
            mode="chrome",
        )

        self.assertEqual(
            sorted(p.relative_to(dest).as_posix() for p in dest.rglob("*.*")),
            ["localization/de/chrome/chrome.ftl"],
        )
        self.assertEqual(
            (dest / "localization/de/chrome/chrome.ftl").read_text(encoding="utf-8"),
            "chrome-string = de\n",
        )


if __name__ == "__main__":
    main()
