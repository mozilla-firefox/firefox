# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest

from mozunit import main

from mozbuild.vendor.sbom import (
    clean_version,
    components_for_unmatched,
    discover_manifests,
    is_tooling_path,
    load_license_notices,
    manifest_to_record,
    merge_license_notices,
    purl_slug,
    unattached_notices,
)


def manifest(origin=None, vendoring=None):
    result = {
        "schema": "1",
        "bugzilla": {"product": "Core", "component": "General"},
        "origin": {
            "name": "example",
            "description": "an example",
            "url": "https://example.com/",
            "license": "MIT",
            "release": "v1.0 (2026-01-01T00:00:00Z).",
            **(origin or {}),
        },
    }
    if vendoring is not None:
        result["vendoring"] = vendoring
    return result


class TestCleanVersion(unittest.TestCase):
    def test_clean_version(self):
        # `origin.release` is prose, not a version.
        vectors = [
            ("v1.6.58 (2026-04-15T20:23:26+03:00).", "v1.6.58"),
            ("VER-2-14-2 (2026-03-01T14:55:17+01:00).", "VER-2-14-2"),
            ("version 3.7.3", "3.7.3"),
            ("version 4.2", "4.2"),
            ("puppeteer-v24.35.0", "puppeteer-v24.35.0"),
            ("", None),
            (None, None),
        ]
        for release, expected in vectors:
            self.assertEqual(clean_version(release), expected, release)


class TestPurlSlug(unittest.TestCase):
    def test_purl_slug(self):
        vectors = [
            ("bsdiff/bspatch", "bsdiff-bspatch"),
            ("Chromium sandbox", "chromium-sandbox"),
            ("xz embedded", "xz-embedded"),
            ("Content Analysis SDK", "content-analysis-sdk"),
            ("zstd", "zstd"),
        ]
        for name, expected in vectors:
            self.assertEqual(purl_slug(name), expected, name)


class TestManifestToRecord(unittest.TestCase):
    def test_revision_preferred_over_release(self):
        record = manifest_to_record(
            "third_party/zstd/moz.yaml", manifest({"revision": "v1.5.7"})
        )
        self.assertEqual(record["version"], "v1.5.7")

    def test_version_release_preferred_over_commit_hash(self):
        revision = "21d1eb0f306e1141c10931e914dfc038c06121da"
        for release, version in (
            ("5bba75bf8e1e2b3c4d5e6f708192a3b4c5d6e7f8 (2024-01-02)", revision),
            ("20240115", "20240115"),
            ("78.3", "78.3"),
        ):
            record = manifest_to_record(
                "a/moz.yaml", manifest({"release": release, "revision": revision})
            )
            self.assertEqual(record["version"], version)
            self.assertEqual(record["purl"][3], revision)
        self.assertEqual(record["properties"]["moz:origin.revision"], revision)

    def test_release_used_when_no_revision(self):
        record = manifest_to_record(
            "a/moz.yaml", manifest({"release": "version 3.7.3"})
        )
        self.assertEqual(record["version"], "3.7.3")

    def test_bom_ref_is_manifest_directory(self):
        record = manifest_to_record("third_party/zstd/moz.yaml", manifest())
        self.assertEqual(record["bom_ref"], "third_party/zstd")

    def test_license_string_and_list(self):
        record = manifest_to_record("a/moz.yaml", manifest({"license": "MIT"}))
        self.assertEqual(record["licenses"], ["MIT"])
        self.assertNotIn("moz:license.conjunction", record["properties"])

        record = manifest_to_record(
            "a/moz.yaml", manifest({"license": ["IJG", "BSD-3-Clause"]})
        )
        self.assertEqual(record["licenses"], ["IJG", "BSD-3-Clause"])
        # moz.yaml has no AND/OR operator, so the ambiguity must be recorded
        # rather than resolved.
        self.assertEqual(record["properties"]["moz:license.conjunction"], "unspecified")

    def test_github_purl(self):
        record = manifest_to_record(
            "third_party/zstd/moz.yaml",
            manifest(
                {"revision": "v1.5.7"},
                {"url": "https://github.com/facebook/zstd", "source-hosting": "github"},
            ),
        )
        self.assertEqual(record["purl"], ("github", "facebook", "zstd", "v1.5.7", {}))

    def test_github_purl_strips_dot_git_and_lowercases(self):
        record = manifest_to_record(
            "a/moz.yaml",
            manifest(
                {"revision": "abc"},
                {
                    "url": "https://github.com/Microsoft/WebAuthn.git",
                    "source-hosting": "github",
                },
            ),
        )
        self.assertEqual(record["purl"], ("github", "microsoft", "webauthn", "abc", {}))

    def test_self_hosted_gitlab_falls_back_to_generic(self):
        # pkg:gitlab implies gitlab.com; claiming it for a self-hosted instance
        # would point at an unrelated project.
        record = manifest_to_record(
            "a/moz.yaml",
            manifest(
                {"name": "mesa", "revision": "abc"},
                {
                    "url": "https://gitlab.freedesktop.org/mesa/mesa",
                    "source-hosting": "gitlab",
                },
            ),
        )
        purl_type, namespace, name, version, qualifiers = record["purl"]
        self.assertEqual(
            (purl_type, namespace, name, version), ("generic", None, "mesa", "abc")
        )
        self.assertEqual(
            qualifiers["vcs_url"], "git+https://gitlab.freedesktop.org/mesa/mesa@abc"
        )

    def test_gitlab_dot_com_purl(self):
        record = manifest_to_record(
            "a/moz.yaml",
            manifest(
                {"revision": "abc"},
                {
                    "url": "https://gitlab.com/group/sub/proj",
                    "source-hosting": "gitlab",
                },
            ),
        )
        self.assertEqual(record["purl"], ("gitlab", "group/sub", "proj", "abc", {}))

    def test_no_vendoring_block_yields_generic_purl(self):
        record = manifest_to_record(
            "a/moz.yaml", manifest({"name": "fathom", "release": "version 3.7.3"})
        )
        self.assertEqual(record["purl"], ("generic", None, "fathom", "3.7.3", {}))

    def test_purl_hostile_name(self):
        record = manifest_to_record(
            "a/moz.yaml", manifest({"name": "bsdiff/bspatch", "release": "version 4.2"})
        )
        self.assertEqual(record["purl"][2], "bsdiff-bspatch")

    def test_yaml_dir_pseudo_manifest_skipped(self):
        record = manifest_to_record(
            "a/moz.yaml",
            manifest(vendoring={"url": "https://x/", "source-hosting": "yaml-dir"}),
        )
        self.assertIsNone(record)

    def test_properties(self):
        record = manifest_to_record(
            "third_party/zstd/moz.yaml",
            manifest(
                {"revision": "v1.5.7"},
                {
                    "url": "https://github.com/facebook/zstd",
                    "source-hosting": "github",
                    "vendor-directory": "third_party/zstd/lib",
                },
            ),
        )
        self.assertEqual(
            record["properties"]["moz:moz-yaml.path"], "third_party/zstd/moz.yaml"
        )
        self.assertEqual(record["properties"]["moz:bugzilla.product"], "Core")
        self.assertEqual(
            record["properties"]["moz:vendoring.vendor-directory"],
            "third_party/zstd/lib",
        )
        # The upstream tag is often more useful than the revision SHA, so the
        # verbatim release string is preserved.
        self.assertEqual(
            record["properties"]["moz:origin.release"], "v1.0 (2026-01-01T00:00:00Z)."
        )


class TestMergeLicenseNotices(unittest.TestCase):
    def notice(self, id, paths, spdx=None, declared_in="toolkit/content/licenses"):
        return {
            "id": id,
            "paths": paths,
            "spdx": spdx,
            "declared_in": declared_in,
            "title": id,
        }

    def record(self, bom_ref, licenses=None):
        return {"bom_ref": bom_ref, "licenses": licenses or [], "properties": {}}

    def test_exact_and_parent_directory_match(self):
        records = [self.record("gfx/harfbuzz")]
        merge_license_notices(
            records, [self.notice("harfbuzz", ["gfx/harfbuzz/"], "MIT")]
        )
        self.assertEqual(records[0]["properties"]["moz:license.notice-ids"], "harfbuzz")

    def test_glob_match(self):
        records = [self.record("js/src/jit/mips64")]
        merge_license_notices(records, [self.notice("v8", ["js/src/jit/mips*"])])
        self.assertEqual(records[0]["properties"]["moz:license.notice-ids"], "v8")

    def test_fills_in_missing_license(self):
        records = [self.record("third_party/rust/byteorder")]
        merge_license_notices(
            records, [self.notice("mit", ["third_party/rust/byteorder"], "MIT")]
        )
        self.assertEqual(records[0]["licenses"], ["MIT"])

    def test_does_not_override_declared_license(self):
        records = [self.record("gfx/harfbuzz", licenses=["MIT OR Apache-2.0"])]
        merge_license_notices(
            records, [self.notice("harfbuzz", ["gfx/harfbuzz"], "MIT")]
        )
        self.assertEqual(records[0]["licenses"], ["MIT OR Apache-2.0"])

    def test_declaring_directory_is_not_covered(self):
        # toolkit/content/licenses holds the shared notice texts; declaring one
        # there says nothing about the license of the code in that directory.
        records = [self.record("toolkit/content/licenses")]
        merge_license_notices(records, [self.notice("mit", ["a/one.js"], "MIT")])
        self.assertEqual(records[0]["properties"], {})
        self.assertEqual(records[0]["licenses"], [])

    def test_unrelated_directory_is_untouched(self):
        records = [self.record("media/libvpx")]
        merge_license_notices(records, [self.notice("harfbuzz", ["gfx/harfbuzz"])])
        self.assertEqual(records[0]["properties"], {})

    def test_paths_inside_the_component_become_occurrences(self):
        # The notice names one file of a vendored library; the component
        # directory alone would lose which file it was.
        records = [self.record("nsprpub")]
        merge_license_notices(
            records, [self.notice("dtoa", ["nsprpub/pr/src/misc/dtoa.c"])]
        )
        self.assertEqual(records[0]["occurrences"], ["nsprpub/pr/src/misc/dtoa.c"])

    def test_a_path_covering_the_component_is_not_an_occurrence(self):
        records = [self.record("gfx/harfbuzz")]
        merge_license_notices(records, [self.notice("harfbuzz", ["gfx/harfbuzz/"])])
        self.assertNotIn("occurrences", records[0])

    def test_multiple_notices_are_sorted(self):
        records = [self.record("nsprpub/pr/src/misc")]
        merge_license_notices(
            records,
            [
                self.notice("praton", ["nsprpub/pr/src/misc/praton.c"]),
                self.notice("dtoa", ["nsprpub/pr/src/misc/dtoa.c"]),
            ],
        )
        self.assertEqual(
            records[0]["properties"]["moz:license.notice-ids"], "dtoa,praton"
        )

    def test_npm_package_matches_where_it_is_installed(self):
        record = dict(
            self.record("npm:react@19.2.0"),
            occurrences=["third_party/node/node_modules/react"],
        )
        merge_license_notices(
            [record], [self.notice("mit", ["third_party/node/node_modules/react"])]
        )
        self.assertEqual(record["properties"]["moz:license.notice-ids"], "mit")
        self.assertEqual(record["occurrences"], ["third_party/node/node_modules/react"])

    def test_missing_licenses_json_is_not_an_error(self):
        self.assertEqual(load_license_notices("/nonexistent/licenses.json"), [])


class TestComponentsForUnmatched(unittest.TestCase):
    def notice(self, id, paths, spdx=None, url=None):
        return {
            "id": id,
            "paths": paths,
            "spdx": spdx,
            "url": url,
            "declared_in": "toolkit/content/licenses",
            "title": f"{id} License",
        }

    def record(self, bom_ref):
        return {"bom_ref": bom_ref, "licenses": [], "properties": {}}

    def test_npm_package_covers_its_installed_directory(self):
        record = dict(
            self.record("npm:react@19.2.0"),
            occurrences=["third_party/node/node_modules/react"],
        )
        extra = components_for_unmatched(
            [record],
            [
                self.notice(
                    "mit",
                    ["third_party/node/node_modules/react", "third_party/rlbox"],
                    "MIT",
                )
            ],
        )
        self.assertEqual(extra[0]["occurrences"], ["third_party/rlbox"])

    def test_code_described_elsewhere_is_covered(self):
        extra = components_for_unmatched(
            [],
            [self.notice("mit", ["third_party/rust/proc-macro2"], "MIT")],
            elsewhere=[self.record("third_party/rust/proc-macro2")],
        )
        self.assertEqual(extra, [])

    def test_notice_without_a_manifest_becomes_a_component(self):
        extra = components_for_unmatched(
            [], [self.notice("mit", ["third_party/rust/byteorder"], "MIT")]
        )
        self.assertEqual(len(extra), 1)
        self.assertEqual(extra[0]["bom_ref"], "license:mit")
        self.assertEqual(extra[0]["name"], "mit License")
        self.assertEqual(extra[0]["licenses"], ["MIT"])
        self.assertEqual(extra[0]["occurrences"], ["third_party/rust/byteorder"])
        self.assertEqual(extra[0]["properties"]["moz:license.notice-ids"], "mit")
        self.assertIsNone(extra[0]["bugzilla"])

    def test_notice_without_spdx_is_licensed_by_its_title(self):
        extra = components_for_unmatched([], [self.notice("jpnic", ["netwerk/dns"])])
        self.assertEqual(extra[0]["licenses"], ["jpnic License"])

    def test_no_purl_is_invented(self):
        extra = components_for_unmatched([], [self.notice("mit", ["a/one.js"])])
        self.assertIsNone(extra[0]["purl"])

    def test_every_path_of_a_notice_is_one_component(self):
        # Thirty files under one notice are one piece of third-party code, not
        # thirty siblings of the real libraries.
        extra = components_for_unmatched(
            [], [self.notice("mit", ["a/one.js", "b/two.js", "c/three.js"])]
        )
        self.assertEqual(len(extra), 1)
        self.assertEqual(
            extra[0]["occurrences"], ["a/one.js", "b/two.js", "c/three.js"]
        )

    def test_type_is_file_when_every_occurrence_is_one(self):
        files = components_for_unmatched([], [self.notice("mit", ["a/one.js"])])
        self.assertEqual(files[0]["type"], "file")
        mixed = components_for_unmatched(
            [], [self.notice("mit", ["a/one.js", "b/lib"])]
        )
        self.assertEqual(mixed[0]["type"], "library")

    def test_is_file_predicate_is_used_when_given(self):
        extra = components_for_unmatched(
            [], [self.notice("mit", ["a/no-suffix"])], is_file=lambda path: True
        )
        self.assertEqual(extra[0]["type"], "file")

    def test_path_covered_by_a_manifest_is_skipped(self):
        records = [self.record("gfx/harfbuzz")]
        extra = components_for_unmatched(
            records, [self.notice("harfbuzz", ["gfx/harfbuzz/"])]
        )
        self.assertEqual(extra, [])

    def test_path_under_a_manifest_directory_is_skipped(self):
        records = [self.record("media/libvpx")]
        extra = components_for_unmatched(
            records, [self.notice("vp8", ["media/libvpx/vp8/encoder"])]
        )
        self.assertEqual(extra, [])

    def test_path_containing_a_manifest_directory_is_kept(self):
        # ipc/chromium is attributed to the Chromium license as a whole; the
        # one component under it describes a single vendored library, so the
        # broader path still needs a component of its own.
        records = [self.record("ipc/chromium/src/third_party/libevent")]
        extra = components_for_unmatched(
            records, [self.notice("chromium", ["ipc/chromium"])]
        )
        self.assertEqual(extra[0]["occurrences"], ["ipc/chromium"])

    def test_covered_path_is_dropped_from_the_occurrences(self):
        records = [self.record("gfx/harfbuzz")]
        extra = components_for_unmatched(
            records, [self.notice("mit", ["gfx/harfbuzz", "a/one.js"])]
        )
        self.assertEqual(extra[0]["occurrences"], ["a/one.js"])

    def test_one_path_under_two_notices_is_one_component_each(self):
        extra = components_for_unmatched(
            [],
            [
                self.notice("praton", ["nsprpub/pr/src/misc/praton.c"], "MIT"),
                self.notice(
                    "ucal", ["nsprpub/pr/src/misc/praton.c"], "BSD-4-Clause-UC"
                ),
            ],
        )
        self.assertEqual(
            [e["bom_ref"] for e in extra], ["license:praton", "license:ucal"]
        )
        self.assertEqual(extra[0]["licenses"], ["MIT"])
        self.assertEqual(extra[1]["licenses"], ["BSD-4-Clause-UC"])

    def test_pathless_notice_yields_nothing(self):
        self.assertEqual(components_for_unmatched([], [self.notice("mpl", [])]), [])

    def test_components_are_sorted_by_notice_id(self):
        extra = components_for_unmatched(
            [], [self.notice("mit", ["b/two"]), self.notice("apache", ["a/one"])]
        )
        self.assertEqual(
            [e["bom_ref"] for e in extra], ["license:apache", "license:mit"]
        )


class TestUnattachedNotices(unittest.TestCase):
    def test_notice_on_no_component_is_reported(self):
        notices = [
            {"id": "mpl", "paths": [], "spdx": "MPL-2.0"},
            {"id": "mit", "paths": ["a"], "spdx": "MIT"},
        ]
        records = [{"properties": {"moz:license.notice-ids": "mit"}}]
        self.assertEqual(
            [n["id"] for n in unattached_notices(records, notices)], ["mpl"]
        )

    def test_nothing_unattached_when_all_are_carried(self):
        notices = [{"id": "mit", "paths": ["a"], "spdx": "MIT"}]
        records = [{"properties": {"moz:license.notice-ids": "mit"}}]
        self.assertEqual(unattached_notices(records, notices), [])


class TestSubcomponentNotices(unittest.TestCase):
    def notice(self, id, paths, spdx=None, subcomponent=False):
        return {
            "id": id,
            "paths": paths,
            "spdx": spdx,
            "subcomponent": subcomponent,
            "declared_in": "extensions/spellcheck/hunspell",
            "title": id,
        }

    def record(self, bom_ref, licenses=None):
        return {"bom_ref": bom_ref, "licenses": licenses or [], "properties": {}}

    def test_subcomponent_license_joins_the_declared_one(self):
        # hunspell's moz.yaml declares the library's license; the MySpell files
        # inside it are BSD-2-Clause, and that is not a duplicate of it.
        records = [self.record("extensions/spellcheck/hunspell", ["MPL-1.1"])]
        merge_license_notices(
            records,
            [
                self.notice(
                    "myspell",
                    ["extensions/spellcheck/hunspell"],
                    "BSD-2-Clause",
                    subcomponent=True,
                )
            ],
        )
        self.assertEqual(records[0]["licenses"], ["BSD-2-Clause", "MPL-1.1"])

    def test_a_plain_notice_still_defers_to_moz_yaml(self):
        records = [self.record("gfx/harfbuzz", ["MIT"])]
        merge_license_notices(
            records, [self.notice("harfbuzz", ["gfx/harfbuzz"], "Apache-2.0")]
        )
        self.assertEqual(records[0]["licenses"], ["MIT"])

    def test_a_subcomponent_license_is_not_repeated(self):
        records = [self.record("extensions/spellcheck/hunspell", ["MPL-1.1"])]
        merge_license_notices(
            records,
            [
                self.notice(
                    "myspell",
                    ["extensions/spellcheck/hunspell"],
                    "MPL-1.1",
                    subcomponent=True,
                )
            ],
        )
        self.assertEqual(records[0]["licenses"], ["MPL-1.1"])


class TestDiscoverManifests(unittest.TestCase):
    class FakeRepo:
        def __init__(self, paths):
            self.paths = paths

        def get_tracked_files_finder(self, topsrcdir):
            paths = self.paths

            class Finder:
                def find(self, pattern):
                    return [(path, None) for path in paths]

            return Finder()

    def test_lint_fixtures_are_skipped(self):
        # They are deliberately invalid, so --strict would fail the build on
        # them.
        repo = self.FakeRepo([
            "gfx/harfbuzz/moz.yaml",
            "tools/lint/test/files/license-declarations/bad/vendored/moz.yaml",
        ])
        self.assertEqual(discover_manifests(repo, "/src"), ["gfx/harfbuzz/moz.yaml"])


class TestIsToolingPath(unittest.TestCase):
    def test_test_suites_and_benchmarks(self):
        for path in (
            "dom/canvas/test/webgl-conf",
            "dom/webgpu/tests/cts",
            "remote/test/puppeteer",
            "testing/mochitest/pywebsocket3",
            "third_party/googletest",
            "third_party/webkit/PerformanceTests/Speedometer3",
            "tools/fuzzing/libfuzzer",
            "tools/lint/eslint/eslint-plugin-sdl-vendored",
        ):
            self.assertTrue(is_tooling_path(path), path)

    def test_shipped_code(self):
        for path in (
            "media/libvpx",
            "third_party/googletest-like",
            "toolkit/components/pdfjs",
            "third_party/js/testlib",
        ):
            self.assertFalse(is_tooling_path(path), path)


if __name__ == "__main__":
    main()
