# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

from mozunit import main

from mozbuild.action.l10n_stage import MissingJarSource, stage_locale
from mozbuild.frontend.jar_manifest import JarEntry, JarSection
from mozbuild.frontend.l10n_manifest import (
    MANIFEST_VERSION,
    MOZ_L10N_AB_CD_PLACEHOLDER,
    L10nManifest,
    L10nManifestContextData,
    write_l10n_manifest,
)


def _necko_manifest():
    return L10nManifest(
        version=MANIFEST_VERSION,
        moz_app_id="{abcd}",
        moz_app_version="121.0",
        moz_app_displayname="Firefox",
        moz_build_app="mobile/android",
        contexts=[
            L10nManifestContextData(
                relsrcdir="netwerk/locales",
                locale_relsrcdir="netwerk/locales",
                install_subdir="",
                defines={},
                locale_pp_defines={},
                jar_sections=[
                    JarSection(
                        name=f"chrome/{MOZ_L10N_AB_CD_PLACEHOLDER}",
                        base="",
                        relativesrcdir="dom/locales",
                        chrome_manifests=(),
                        entries=(
                            JarEntry(
                                source="necko.properties",
                                output=(
                                    f"locale/{MOZ_L10N_AB_CD_PLACEHOLDER}"
                                    "/necko/necko.properties"
                                ),
                                is_locale=True,
                                preprocess=False,
                            ),
                        ),
                    )
                ],
            )
        ],
    )


def _two_section_manifest():
    def section(name, chrome_packages, sources):
        return JarSection(
            name=name,
            base="",
            relativesrcdir="",
            chrome_manifests=tuple(
                f"locale {package} {MOZ_L10N_AB_CD_PLACEHOLDER} "
                f"%locale/{MOZ_L10N_AB_CD_PLACEHOLDER}/{package}/"
                for package in chrome_packages
            ),
            entries=tuple(
                JarEntry(
                    source=source,
                    output=f"locale/{MOZ_L10N_AB_CD_PLACEHOLDER}/{name}/{source}",
                    is_locale=True,
                    preprocess=False,
                )
                for source in sources
            ),
        )

    return L10nManifest(
        version=MANIFEST_VERSION,
        moz_app_id="{abcd}",
        moz_app_version="121.0",
        moz_app_displayname="Firefox",
        moz_build_app="browser",
        contexts=[
            L10nManifestContextData(
                relsrcdir="app/locales",
                locale_relsrcdir="app/locales",
                install_subdir="",
                defines={},
                locale_pp_defines={},
                jar_sections=[
                    section("zeta", ["zulu", "alpha"], ["zulu.properties"]),
                    section("alpha", ["one"], ["one.properties"]),
                ],
            )
        ],
    )


def _comm_manifest():
    return L10nManifest(
        version=MANIFEST_VERSION,
        moz_app_id="{abcd}",
        moz_app_version="121.0",
        moz_app_displayname="Thunderbird",
        moz_build_app="comm/mail",
        contexts=[
            L10nManifestContextData(
                relsrcdir="comm/mail/locales",
                locale_relsrcdir="mail/locales",
                install_subdir="",
                defines={},
                locale_pp_defines={},
                jar_sections=[
                    JarSection(
                        name=f"chrome/{MOZ_L10N_AB_CD_PLACEHOLDER}",
                        base="",
                        relativesrcdir="",
                        chrome_manifests=(),
                        entries=(
                            JarEntry(
                                source="chrome/messenger/messenger.dtd",
                                output=(
                                    f"locale/{MOZ_L10N_AB_CD_PLACEHOLDER}"
                                    "/messenger/messenger.dtd"
                                ),
                                is_locale=True,
                                preprocess=False,
                            ),
                        ),
                    )
                ],
            )
        ],
    )


class TestCommLocaleRelsrcdir(unittest.TestCase):
    """comm-central localizes from commtopsrcdir, so its merge tree paths
    drop the comm/ prefix that relsrcdir carries.
    """

    def test_merge_source_drops_the_comm_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest_path = root / "l10n-manifest.json"
            write_l10n_manifest(_comm_manifest(), manifest_path)
            src = root / "merge-dir" / "de" / "mail" / "chrome" / "messenger"
            src.mkdir(parents=True)
            (src / "messenger.dtd").write_text("<!-- de -->\n", encoding="utf-8")
            dest = root / "stage"
            stage_locale(
                locale="de",
                manifest_path=manifest_path,
                merge_tree=root / "merge-dir" / "de",
                dest_xpi_stage=dest,
                topsrcdir=root / "src",
                topobjdir=root / "obj",
            )
            staged = dest / "chrome" / "de" / "locale" / "de" / "messenger"
            self.assertEqual(
                (staged / "messenger.dtd").read_text(encoding="utf-8"),
                "<!-- de -->\n",
            )

    def test_en_us_source_keeps_the_comm_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest_path = root / "l10n-manifest.json"
            write_l10n_manifest(_comm_manifest(), manifest_path)
            src = (
                root
                / "src"
                / "comm"
                / "mail"
                / "locales"
                / "en-US"
                / "chrome"
                / "messenger"
            )
            src.mkdir(parents=True)
            (src / "messenger.dtd").write_text("<!-- en-US -->\n", encoding="utf-8")
            dest = root / "stage"
            stage_locale(
                locale="en-US",
                manifest_path=manifest_path,
                merge_tree=root / "merge-dir" / "en-US",
                dest_xpi_stage=dest,
                topsrcdir=root / "src",
                topobjdir=root / "obj",
            )
            staged = dest / "chrome" / "en-US" / "locale" / "en-US" / "messenger"
            self.assertEqual(
                (staged / "messenger.dtd").read_text(encoding="utf-8"),
                "<!-- en-US -->\n",
            )


class TestChromeManifestOrdering(unittest.TestCase):
    def _stage(self, tmp, mode="langpack"):
        root = Path(tmp)
        manifest_path = root / "l10n-manifest.json"
        write_l10n_manifest(_two_section_manifest(), manifest_path)
        merge_tree = root / "merge-dir" / "de" / "app"
        merge_tree.mkdir(parents=True, exist_ok=True)
        for name in ("zulu.properties", "one.properties"):
            (merge_tree / name).write_text(f"# {name}\n", encoding="utf-8")
        dest = root / "stage"
        stage_locale(
            locale="de",
            manifest_path=manifest_path,
            merge_tree=root / "merge-dir" / "de",
            dest_xpi_stage=dest,
            topsrcdir=root / "src",
            topobjdir=root / "obj",
            mode=mode,
        )
        return dest

    def test_every_manifest_is_sorted(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = self._stage(tmp)

            self.assertEqual(
                (dest / "chrome.manifest").read_text(encoding="utf-8").splitlines(),
                ["manifest alpha.manifest", "manifest zeta.manifest"],
            )
            self.assertEqual(
                (dest / "zeta.manifest").read_text(encoding="utf-8").splitlines(),
                [
                    "locale alpha de zeta/locale/de/alpha/",
                    "locale zulu de zeta/locale/de/zulu/",
                ],
            )

    def test_chrome_mode_merges_with_entries_already_on_disk(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = self._stage(tmp, mode="chrome")
            (dest / "zeta.manifest").write_text(
                "locale zulu fr zeta/locale/fr/zulu/\n", encoding="utf-8"
            )
            self._stage(tmp, mode="chrome")

            self.assertEqual(
                (dest / "zeta.manifest").read_text(encoding="utf-8").splitlines(),
                [
                    "locale alpha de zeta/locale/de/alpha/",
                    "locale zulu de zeta/locale/de/zulu/",
                    "locale zulu fr zeta/locale/fr/zulu/",
                ],
            )


class TestMissingJarSource(unittest.TestCase):
    def _stage(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest_path = root / "l10n-manifest.json"
            write_l10n_manifest(_necko_manifest(), manifest_path)
            merge_tree = root / "merge-dir" / "de"
            merge_tree.mkdir(parents=True)
            stage_locale(
                locale="de",
                manifest_path=manifest_path,
                merge_tree=merge_tree,
                dest_xpi_stage=root / "xpi-stage" / "locale-de",
                topsrcdir=root / "src",
                topobjdir=root / "obj",
            )

    def test_error_names_locale_context_and_resolved_path(self):
        with self.assertRaises(MissingJarSource) as raised:
            self._stage()

        error = raised.exception
        self.assertEqual(error.locale, "de")
        self.assertEqual(error.context_relsrcdir, "netwerk/locales")
        self.assertEqual(error.relsrcdir, "dom/locales")
        self.assertEqual(error.source, "necko.properties")
        self.assertTrue(error.is_locale)
        # A locale entry resolves under the section's relativesrcdir in the
        # merge tree, not under the context it was declared in.
        self.assertTrue(
            error.resolved.endswith("merge-dir/de/dom/necko.properties"),
            error.resolved,
        )


if __name__ == "__main__":
    main()
