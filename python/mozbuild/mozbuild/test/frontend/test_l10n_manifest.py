# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

import mozpack.path as mozpath
from mozunit import main

from mozbuild.frontend.jar_manifest import JarEntry, JarSection, LocaleTemplate
from mozbuild.frontend.l10n_manifest import (
    MANIFEST_VERSION,
    L10nManifest,
    L10nManifestContextData,
    LocalizedFileGroup,
    LocalizedGenScript,
    _locale_relsrcdir,
    build_l10n_manifest_from_substs,
    load_l10n_manifest,
    write_l10n_manifest,
)
from mozbuild.test.common import MockConfig

TOPSRCDIR = mozpath.abspath("/src")
COMMTOPSRCDIR = mozpath.join(TOPSRCDIR, "comm")


class TestL10nManifestRoundTrip(unittest.TestCase):
    def test_round_trip(self):
        manifest = L10nManifest(
            version=MANIFEST_VERSION,
            moz_app_id="{ec8030f7-c20a-464f-9b0e-13a3a9e97384}",
            moz_app_version="121.0",
            moz_app_displayname="Firefox",
            moz_build_app="browser",
            contexts=[
                L10nManifestContextData(
                    relsrcdir="browser/locales",
                    locale_relsrcdir="browser/locales",
                    install_subdir="",
                    defines={"FOO": "bar"},
                    jar_sections=[
                        JarSection(
                            name=LocaleTemplate("browser"),
                            base="",
                            relativesrcdir="browser/locales",
                            chrome_manifests=(
                                LocaleTemplate("locale browser %en-US %"),
                            ),
                            entries=(
                                JarEntry(
                                    source=LocaleTemplate("en-US/foo.ftl"),
                                    output=LocaleTemplate("foo.ftl"),
                                    is_locale=True,
                                    preprocess=False,
                                ),
                            ),
                        ),
                    ],
                    localized_files=[
                        LocalizedFileGroup(subpath="..", sources=["!updater.ini"]),
                    ],
                    localized_pp_files=[],
                    localized_generated_files=[
                        LocalizedGenScript(
                            script="/topsrcdir/browser/locales/generate_ini.py",
                            method="main",
                            inputs=["en-US/updater/updater.ini"],
                            outputs=["updater.ini"],
                            flags=[],
                            force=False,
                        ),
                    ],
                ),
            ],
        )

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "l10n-manifest.json"
            write_l10n_manifest(manifest, path)
            loaded = load_l10n_manifest(path)

        self.assertEqual(loaded, manifest)

    def test_empty_manifest_round_trip(self):
        manifest = L10nManifest(
            version=MANIFEST_VERSION,
            moz_app_id="",
            moz_app_version="",
            moz_app_displayname="",
            moz_build_app="",
            contexts=[],
        )

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "l10n-manifest.json"
            write_l10n_manifest(manifest, path)
            loaded = load_l10n_manifest(path)

        self.assertEqual(loaded, manifest)


class TestBuildL10nManifestFromSubsts(unittest.TestCase):
    def test_full_substs(self):
        substs = {
            "MOZ_APP_ID": "{abcd}",
            "MOZ_APP_VERSION": "121.0",
            "MOZ_APP_DISPLAYNAME": "Firefox",
            "MOZ_BUILD_APP": "browser",
        }
        ctx = L10nManifestContextData(
            relsrcdir="browser/locales",
            locale_relsrcdir="browser/locales",
            install_subdir="",
            defines={},
        )
        manifest = build_l10n_manifest_from_substs(substs, [ctx])

        self.assertEqual(manifest.version, MANIFEST_VERSION)
        self.assertEqual(manifest.moz_app_id, "{abcd}")
        self.assertEqual(manifest.moz_app_version, "121.0")
        self.assertEqual(manifest.moz_app_displayname, "Firefox")
        self.assertEqual(manifest.moz_build_app, "browser")
        self.assertEqual(manifest.contexts, [ctx])

    def test_missing_substs_default_to_empty_string(self):
        manifest = build_l10n_manifest_from_substs({}, [])
        self.assertEqual(manifest.moz_app_id, "")
        self.assertEqual(manifest.moz_app_version, "")
        self.assertEqual(manifest.moz_app_displayname, "")
        self.assertEqual(manifest.moz_build_app, "")
        self.assertEqual(manifest.contexts, [])


class TestLocaleRelsrcdir(unittest.TestCase):
    def _comm_config(self):
        return MockConfig(TOPSRCDIR, extra_substs={"commtopsrcdir": COMMTOPSRCDIR})

    def test_without_commtopsrcdir_relsrcdir_is_unchanged(self):
        config = MockConfig(TOPSRCDIR)
        self.assertEqual(
            _locale_relsrcdir(config, "toolkit/locales"), "toolkit/locales"
        )

    def test_comm_directories_drop_the_comm_prefix(self):
        config = self._comm_config()
        self.assertEqual(_locale_relsrcdir(config, "comm/mail/locales"), "mail/locales")
        self.assertEqual(
            _locale_relsrcdir(config, "comm/mail/branding/thunderbird/locales"),
            "mail/branding/thunderbird/locales",
        )

    def test_gecko_directories_of_a_comm_build_are_unchanged(self):
        config = self._comm_config()
        self.assertEqual(
            _locale_relsrcdir(config, "toolkit/locales"), "toolkit/locales"
        )
        self.assertEqual(
            _locale_relsrcdir(config, "community/locales"), "community/locales"
        )


class TestL10nManifestRoots(unittest.TestCase):
    def _context(self, relsrcdir):
        return L10nManifestContextData(
            relsrcdir=relsrcdir,
            locale_relsrcdir=relsrcdir,
            install_subdir="",
            defines={},
        )

    def _relsrcdirs(self, roots, relsrcdirs):
        substs = {} if roots is None else {"MOZ_L10N_CHROME_ROOTS": roots}
        contexts = [self._context(relsrcdir) for relsrcdir in relsrcdirs]
        manifest = build_l10n_manifest_from_substs(substs, contexts)
        return [data.relsrcdir for data in manifest.contexts]

    def test_subst_absent_retains_everything(self):
        self.assertEqual(
            self._relsrcdirs(None, ["netwerk/locales", "mobile/android/locales"]),
            ["netwerk/locales", "mobile/android/locales"],
        )

    def test_empty_roots_retain_everything(self):
        self.assertEqual(
            self._relsrcdirs([], ["netwerk/locales", "mobile/android/locales"]),
            ["netwerk/locales", "mobile/android/locales"],
        )

    def test_exact_root_and_descendant_match(self):
        self.assertEqual(
            self._relsrcdirs(
                ["mobile/android/locales"],
                [
                    "mobile/android/locales",
                    "mobile/android/locales/nested",
                    "mobile/android/themes/geckoview",
                    "netwerk/locales",
                ],
            ),
            ["mobile/android/locales", "mobile/android/locales/nested"],
        )

    def test_similar_prefix_does_not_match(self):
        self.assertEqual(
            self._relsrcdirs(
                ["mobile/android/locales"],
                ["mobile/android/locales2", "mobile2/android/locales"],
            ),
            [],
        )

    def _scoped(self, roots, context):
        manifest = build_l10n_manifest_from_substs(
            {"MOZ_L10N_CHROME_ROOTS": roots}, [context]
        )
        return manifest.contexts

    def _jar_section(self):
        return JarSection(
            name=LocaleTemplate("chrome"),
            base="",
            relativesrcdir="",
            chrome_manifests=(),
            entries=(
                JarEntry(
                    source=LocaleTemplate("en-US/necko.properties"),
                    output=LocaleTemplate("locale/necko.properties"),
                    is_locale=True,
                    preprocess=False,
                ),
            ),
        )

    def _gen_script(self):
        return LocalizedGenScript(
            script="generate_default_locale.py",
            method="main",
            inputs=[],
            outputs=["default.locale"],
            flags=[],
            force=False,
        )

    def test_out_of_root_context_keeps_its_generated_files(self):
        context = self._context("toolkit/locales")
        context.jar_sections = [self._jar_section()]
        context.localized_generated_files = [self._gen_script()]
        context.localized_files = [
            LocalizedFileGroup(subpath="", sources=["!default.locale"])
        ]

        contexts = self._scoped(["mobile/android/locales"], context)

        self.assertEqual(len(contexts), 1)
        self.assertEqual(contexts[0].jar_sections, [])
        self.assertEqual(contexts[0].localized_generated_files, [self._gen_script()])
        self.assertEqual(
            contexts[0].localized_files,
            [LocalizedFileGroup(subpath="", sources=["!default.locale"])],
        )

    def test_out_of_root_context_with_only_chrome_is_dropped(self):
        context = self._context("netwerk/locales")
        context.jar_sections = [self._jar_section()]

        self.assertEqual(self._scoped(["mobile/android/locales"], context), [])

    def _localization_section(self, *entries, chrome_manifests=()):
        return JarSection(
            name=LocaleTemplate("@AB_CD@"),
            base="localization",
            relativesrcdir=None,
            chrome_manifests=chrome_manifests,
            entries=entries,
        )

    def test_out_of_root_context_keeps_its_localized_fluent_files(self):
        localized = JarEntry(
            source=LocaleTemplate("toolkit/**/*.ftl"),
            output=LocaleTemplate("toolkit"),
            is_locale=True,
            preprocess=False,
        )
        fallback = JarEntry(
            source=LocaleTemplate("../locales-preview/aboutRestricted.ftl"),
            output=LocaleTemplate("locales-preview/aboutRestricted.ftl"),
            is_locale=False,
            preprocess=False,
        )
        context = self._context("toolkit/locales")
        context.jar_sections = [
            self._jar_section(),
            self._localization_section(
                localized,
                fallback,
                chrome_manifests=("locale toolkit MOZ_L10N_AB_CD_PLACEHOLDER %",),
            ),
        ]

        contexts = self._scoped(["mobile/android/locales"], context)

        self.assertEqual(len(contexts), 1)
        self.assertEqual(
            contexts[0].jar_sections, [self._localization_section(localized)]
        )

    def test_out_of_root_context_with_only_unlocalized_fluent_is_dropped(self):
        context = self._context("remote/locales")
        context.jar_sections = [
            self._localization_section(
                JarEntry(
                    source=LocaleTemplate("../locales-preview/remote-agent.ftl"),
                    output=LocaleTemplate("locales-preview/remote-agent.ftl"),
                    is_locale=False,
                    preprocess=False,
                )
            )
        ]

        self.assertEqual(self._scoped(["mobile/android/locales"], context), [])

    def test_in_root_context_keeps_its_chrome(self):
        context = self._context("mobile/android/locales")
        context.jar_sections = [self._jar_section()]

        contexts = self._scoped(["mobile/android/locales"], context)

        self.assertEqual(len(contexts), 1)
        self.assertEqual(contexts[0].jar_sections, [self._jar_section()])

    def test_configured_branding_root_outside_app_dir(self):
        roots = ["mobile/android/locales", "third_party/acme/branding/locales"]
        self.assertEqual(
            self._relsrcdirs(
                roots,
                [
                    "third_party/acme/branding/locales",
                    "third_party/acme/content",
                    "mobile/android/locales",
                ],
            ),
            ["third_party/acme/branding/locales", "mobile/android/locales"],
        )


if __name__ == "__main__":
    main()
