# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import tempfile
import unittest
from unittest import mock

import yaml
from mozpack.files import FileFinder
from mozunit import main

from mozbuild.action.generate_sbom import (
    SbomError,
    build_document,
    build_tooling_document,
    generate,
)
from mozbuild.vendor.sbom_cyclonedx import ROOT_BOM_REF
from mozbuild.vendor.sbom_gradle import RUNTIME_DEPENDENCIES

PACKAGE_LOCK = {
    "lockfileVersion": 3,
    "packages": {
        "": {"name": "aboutwelcome", "dependencies": {"react": "16.13.1"}},
        "node_modules/react": {
            "version": "16.13.1",
            "license": "MIT",
            "dependencies": {"scheduler": "^0.19.1"},
        },
        "node_modules/scheduler": {"version": "0.19.1", "license": "MIT"},
        "node_modules/webpack": {"version": "5.109.0", "dev": True},
    },
}

MOZ_YAML = {
    "schema": "1",
    "bugzilla": {"product": "Core", "component": "General"},
    "origin": {
        "name": "pkijs",
        "description": "PKI.js",
        "url": "https://pkijs.org/",
        "license": "BSD-3-Clause",
        "release": "abc123 (2026-01-01T00:00:00Z).",
        "revision": "abc123",
        "npm-name": "pkijs",
    },
}


CARGO_LOCK = """
version = 4

[[package]]
name = "gkrust"
version = "0.1.0"
dependencies = ["serde", "hyper"]

[[package]]
name = "serde"
version = "1.0.200"
source = "registry+https://github.com/rust-lang/crates.io-index"

[[package]]
name = "hyper"
version = "1.0.0"
source = "registry+https://github.com/rust-lang/crates.io-index"
dependencies = ["serde"]
"""

KINDS = {("serde", "1.0.200"): ["normal", "tooling"], ("hyper", "1.0.0"): ["tooling"]}


class Repo:
    head_rev = "0" * 40

    def __init__(self, topsrcdir):
        self.topsrcdir = topsrcdir

    def get_tracked_files_finder(self, path):
        return FileFinder(path)

    def get_commit_time(self):
        return 0


class TestBuildDocument(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.topsrcdir = self.tmp.name
        self.write(
            "browser/components/aboutwelcome/package-lock.json",
            json.dumps(PACKAGE_LOCK),
        )
        self.write("third_party/js/PKI.js/moz.yaml", yaml.safe_dump(MOZ_YAML))
        self.write(
            "third_party/js/PKI.js/package.json",
            json.dumps({"name": "pkijs", "version": "3.2.4"}),
        )
        self.write("Cargo.lock", CARGO_LOCK)
        patcher = mock.patch(
            "mozbuild.vendor.sbom_cargo.collect_dependency_kinds",
            return_value=KINDS,
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def write(self, path, content):
        path = os.path.join(self.topsrcdir, path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(content)

    def build(self, **kwargs):
        document = build_document(
            self.topsrcdir,
            self.topsrcdir,
            Repo(self.topsrcdir),
            version="155.0a1",
            **kwargs,
        )
        return json.loads(document)

    def test_npm_records_and_purl_upgrade(self):
        document = self.build()
        purls = {c["bom-ref"]: c.get("purl") for c in document["components"]}
        self.assertEqual(purls["npm:react@16.13.1"], "pkg:npm/react@16.13.1")
        self.assertEqual(purls["npm:scheduler@0.19.1"], "pkg:npm/scheduler@0.19.1")
        self.assertNotIn("npm:webpack@5.109.0", purls)
        self.assertEqual(purls["third_party/js/PKI.js"], "pkg:npm/pkijs@3.2.4")

        edges = {d["ref"]: d.get("dependsOn", []) for d in document["dependencies"]}
        self.assertEqual(edges["npm:react@16.13.1"], ["npm:scheduler@0.19.1"])

    def write_runtime_dependencies(self):
        component = {"group": "androidx.core", "name": "core", "version": "1.19.0"}
        self.write(RUNTIME_DEPENDENCIES, json.dumps({"components": [component]}))

    def purls(self, document):
        return {c.get("purl") for c in document["components"]}

    def test_android_maven_dependencies(self):
        self.write_runtime_dependencies()
        document = self.build(substs={"MOZ_BUILD_APP": "mobile/android"})
        self.assertIn("pkg:maven/androidx.core/core@1.19.0", self.purls(document))

    def test_android_without_maven_dependencies(self):
        substs = {"MOZ_BUILD_APP": "mobile/android"}
        with self.assertRaises(SbomError):
            self.build(substs=substs, strict=True)
        # Without strict, the document is still written, without them.
        purls = self.purls(self.build(substs=substs))
        self.assertFalse([p for p in purls if p and p.startswith("pkg:maven/")])

    def test_other_builds_skip_maven_dependencies(self):
        self.write_runtime_dependencies()
        document = self.build(substs={"MOZ_BUILD_APP": "browser"}, strict=True)
        self.assertNotIn("pkg:maven/androidx.core/core@1.19.0", self.purls(document))

    def test_vendored_python_leaves_the_product_document(self):
        manifest = dict(MOZ_YAML, origin=dict(MOZ_YAML["origin"], name="msvc-wine"))
        del manifest["origin"]["npm-name"]
        self.write("third_party/python/vsdownload/moz.yaml", yaml.safe_dump(manifest))
        refs = {c["bom-ref"] for c in self.build()["components"]}
        self.assertIn("third_party/js/PKI.js", refs)
        self.assertNotIn("third_party/python/vsdownload", refs)

    def test_build_tooling_document(self):
        document = json.loads(
            build_tooling_document(
                self.topsrcdir,
                self.topsrcdir,
                Repo(self.topsrcdir),
                version="155.0a1",
            )
        )
        self.assertEqual(
            sorted((c["purl"], c["scope"]) for c in document["components"]),
            [
                ("pkg:cargo/hyper@1.0.0", "excluded"),
                ("pkg:npm/webpack@5.109.0", "excluded"),
            ],
        )

    def test_strict_build_tooling_document_needs_cargo_metadata(self):
        with mock.patch(
            "mozbuild.vendor.sbom_cargo.collect_dependency_kinds",
            return_value={},
        ):
            with self.assertRaises(SbomError):
                build_tooling_document(
                    self.topsrcdir,
                    self.topsrcdir,
                    Repo(self.topsrcdir),
                    version="155.0a1",
                    strict=True,
                )

    def test_tooling_crates_leave_the_product_document(self):
        document = json.loads(
            build_document(
                self.topsrcdir,
                self.topsrcdir,
                Repo(self.topsrcdir),
                version="155.0a1",
            )
        )
        purls = {c.get("purl") for c in document["components"]}
        self.assertIn("pkg:cargo/serde@1.0.200", purls)
        self.assertNotIn("pkg:cargo/hyper@1.0.0", purls)
        # Its edge to serde left with hyper.
        edges = {d["ref"]: d.get("dependsOn", []) for d in document["dependencies"]}
        self.assertIn("cargo:serde@1.0.200", edges[ROOT_BOM_REF])

    def write_notice(self, crate):
        self.write(f"third_party/rust/{crate}/Cargo.toml", "")
        notice = {"id": "mit", "title": "MIT", "spdx": "MIT", "url": None}
        notice["paths"] = [f"third_party/rust/{crate}"]
        self.write("licenses.json", json.dumps({"licenses": [notice]}))

    def test_noticed_tooling_crates_do_not_come_back(self):
        self.write_notice("hyper")
        refs = {c["bom-ref"] for c in self.build()["components"]}
        self.assertFalse([r for r in refs if "hyper" in r or r.startswith("license:")])

    def write_manifest(self, directory, name):
        manifest = dict(MOZ_YAML, origin=dict(MOZ_YAML["origin"], name=name))
        del manifest["origin"]["npm-name"]
        self.write(f"{directory}/moz.yaml", yaml.safe_dump(manifest))

    def refs(self, document):
        return {c["bom-ref"] for c in document["components"]}

    def tooling_document(self, **kwargs):
        return json.loads(
            build_tooling_document(
                self.topsrcdir,
                self.topsrcdir,
                Repo(self.topsrcdir),
                version="155.0a1",
                **kwargs,
            )
        )

    def test_test_suites_leave_the_product_document(self):
        self.write_manifest("third_party/googletest", "googletest")
        self.write_manifest("dom/webgpu/tests/cts", "webgpu-cts")
        product = self.refs(self.build())
        self.assertIn("third_party/js/PKI.js", product)
        self.assertNotIn("third_party/googletest", product)
        self.assertNotIn("dom/webgpu/tests/cts", product)
        tooling = self.refs(self.tooling_document())
        self.assertIn("third_party/googletest", tooling)
        self.assertIn("dom/webgpu/tests/cts", tooling)
        self.assertNotIn("third_party/js/PKI.js", tooling)

    def test_crates_for_other_platforms_leave_both_documents(self):
        product, tooling = self.target_documents({("hyper", "1.0.0"): ["tooling"]})
        self.assertNotIn("cargo:serde@1.0.200", product)
        self.assertIn("third_party/js/PKI.js", product)
        self.assertNotIn("cargo:serde@1.0.200", tooling)

    def test_crates_this_platform_only_builds_for_tooling(self):
        product, tooling = self.target_documents({("serde", "1.0.200"): ["tooling"]})
        self.assertNotIn("cargo:serde@1.0.200", product)
        self.assertIn("cargo:serde@1.0.200", tooling)

    def test_crates_stay_when_the_target_run_fails(self):
        self.assertIn("cargo:serde@1.0.200", self.target_documents({})[0])

    def test_crates_cargo_metadata_does_not_reach_stay(self):
        with mock.patch.dict(KINDS, {("serde", "1.0.200"): []}):
            product, _ = self.target_documents({("hyper", "1.0.0"): ["tooling"]})
        self.assertIn("cargo:serde@1.0.200", product)

    def test_noticed_crates_for_other_platforms_do_not_come_back(self):
        self.write_notice("serde")
        product, _ = self.target_documents({("hyper", "1.0.0"): ["tooling"]})
        self.assertFalse([
            r for r in product if "serde" in r or r.startswith("license:")
        ])

    def target_documents(self, target_kinds):
        def kinds(*args, target=None, **kwargs):
            return target_kinds if target else KINDS

        substs = {"RUST_TARGET": "aarch64-linux-android"}
        with mock.patch(
            "mozbuild.vendor.sbom_cargo.collect_dependency_kinds", side_effect=kinds
        ):
            return (
                self.refs(self.build(substs=substs)),
                self.refs(self.tooling_document(substs=substs)),
            )

    def test_rejected_arguments(self):
        output = os.path.join(self.topsrcdir, "sbom.json")
        for arguments in (
            {
                "gradle_runtime_dependencies": "runtime-dependencies.json",
                "build_tooling": True,
            },
            {"build_tooling": True, "strict": True},
        ):
            self.assertEqual(
                generate(
                    self.topsrcdir,
                    self.topsrcdir,
                    Repo(self.topsrcdir),
                    output=output,
                    **arguments,
                ),
                1,
            )
        self.assertFalse(os.path.exists(output))


if __name__ == "__main__":
    main()
