# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import unittest
from pathlib import Path

from mozunit import main

from mozbuild.action.l10n_stage import MissingJarSource, stage_locale
from mozbuild.frontend.jar_manifest import (
    AB_CD_TOKEN,
    JarEntry,
    JarSection,
    LocaleTemplate,
)
from mozbuild.frontend.l10n_manifest import (
    MANIFEST_VERSION,
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
                jar_sections=[
                    JarSection(
                        name=LocaleTemplate(f"chrome/{AB_CD_TOKEN}"),
                        base="",
                        relativesrcdir="dom/locales",
                        chrome_manifests=(),
                        entries=(
                            JarEntry(
                                source=LocaleTemplate("necko.properties"),
                                output=LocaleTemplate(
                                    f"locale/{AB_CD_TOKEN}/necko/necko.properties"
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
            name=LocaleTemplate(name),
            base="",
            relativesrcdir="",
            chrome_manifests=tuple(
                LocaleTemplate(
                    f"locale {package} {AB_CD_TOKEN} %locale/{AB_CD_TOKEN}/{package}/"
                )
                for package in chrome_packages
            ),
            entries=tuple(
                JarEntry(
                    source=LocaleTemplate(source),
                    output=LocaleTemplate(f"locale/{AB_CD_TOKEN}/{name}/{source}"),
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
                jar_sections=[
                    JarSection(
                        name=LocaleTemplate(f"chrome/{AB_CD_TOKEN}"),
                        base="",
                        relativesrcdir="",
                        chrome_manifests=(),
                        entries=(
                            JarEntry(
                                source=LocaleTemplate("chrome/messenger/messenger.dtd"),
                                output=LocaleTemplate(
                                    f"locale/{AB_CD_TOKEN}/messenger/messenger.dtd"
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

    def test_chrome_mode_replaces_a_linked_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = Path(tmp) / "src" / "zulu.properties"
            installed.parent.mkdir(parents=True)
            installed.write_text("# en-US\n", encoding="utf-8")
            dest = Path(tmp) / "stage" / "zeta" / "locale" / "de" / "zeta"
            dest.mkdir(parents=True)
            os.link(installed, dest / "zulu.properties")

            self._stage(tmp, mode="chrome")

            self.assertEqual(installed.read_text(encoding="utf-8"), "# en-US\n")
            self.assertEqual(
                (dest / "zulu.properties").read_text(encoding="utf-8"),
                "# zulu.properties\n",
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
