# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

from mozunit import main

from mozbuild.vendor.sbom_cargo import (
    REGISTRY_SOURCE,
    collect_dependency_kinds,
    crate_records,
    dependency_kinds,
    is_tooling,
)

LOCK = textwrap.dedent(
    """
    version = 4

    [[package]]
    name = "gkrust"
    version = "0.1.0"
    dependencies = [
     "serde",
     "log 0.4.20",
    ]

    [[package]]
    name = "serde"
    version = "1.0.200"
    source = "%(registry)s"
    checksum = "1111111111111111111111111111111111111111111111111111111111111111"
    dependencies = [
     "log 0.4.20",
    ]

    [[package]]
    name = "log"
    version = "0.4.20"
    source = "%(registry)s"
    checksum = "2222222222222222222222222222222222222222222222222222222222222222"

    [[package]]
    name = "log"
    version = "0.3.9"
    source = "%(registry)s"
    checksum = "3333333333333333333333333333333333333333333333333333333333333333"

    [[package]]
    name = "patched"
    version = "0.2.0"
    source = "git+https://github.com/mozilla/patched?rev=abc#abc"
    """
    % {"registry": REGISTRY_SOURCE}
)

CARGO_TOML = textwrap.dedent(
    """
    [package]
    name = "serde"
    version = "1.0.200"
    description = "A serialization framework"
    license = "MIT OR Apache-2.0"
    homepage = "https://serde.rs"
    repository = "https://github.com/serde-rs/serde"
    """
)


class TestCrateRecords(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.topsrcdir = self.tmp.name
        self.addCleanup(self.tmp.cleanup)
        with open(os.path.join(self.topsrcdir, "Cargo.lock"), "w") as lock:
            lock.write(LOCK)
        vendored = os.path.join(self.topsrcdir, "third_party", "rust", "serde")
        os.makedirs(vendored)
        with open(os.path.join(vendored, "Cargo.toml"), "w") as manifest:
            manifest.write(CARGO_TOML)

    def records(self):
        records, edges = crate_records(self.topsrcdir)
        return {record["bom_ref"]: record for record in records}, edges

    def test_workspace_members_are_not_components(self):
        records, _ = self.records()
        self.assertNotIn("cargo:gkrust@0.1.0", records)
        self.assertEqual(len(records), 4)

    def test_bom_ref_is_the_vendored_directory_when_there_is_one(self):
        records, _ = self.records()
        self.assertIn("third_party/rust/serde", records)
        # No vendored directory in this fixture, so nothing pretends there is
        # a path.
        self.assertIn("cargo:log@0.4.20", records)

    def test_purl_is_a_cargo_purl(self):
        records, _ = self.records()
        self.assertEqual(
            records["third_party/rust/serde"]["purl"],
            ("cargo", None, "serde", "1.0.200", {}),
        )

    def test_git_dependency_keeps_its_source_in_the_purl(self):
        records, _ = self.records()
        purl = records["cargo:patched@0.2.0"]["purl"]
        self.assertEqual(purl[0], "cargo")
        self.assertEqual(
            purl[4]["vcs_url"], "git+https://github.com/mozilla/patched?rev=abc#abc"
        )

    def test_checksum_becomes_a_sha256_hash(self):
        records, _ = self.records()
        self.assertEqual(
            records["third_party/rust/serde"]["hashes"],
            [("SHA-256", "1" * 64)],
        )
        # A git dependency has no published artifact to checksum.
        self.assertEqual(records["cargo:patched@0.2.0"]["hashes"], [])

    def test_metadata_comes_from_the_vendored_manifest(self):
        records, _ = self.records()
        record = records["third_party/rust/serde"]
        self.assertEqual(record["description"], "A serialization framework")
        self.assertEqual(record["licenses"], ["MIT OR Apache-2.0"])
        self.assertEqual(record["website"], "https://serde.rs")
        self.assertEqual(record["vcs"], "https://github.com/serde-rs/serde")

    def test_legacy_slash_license_becomes_an_spdx_expression(self):
        manifest = os.path.join(
            self.topsrcdir, "third_party", "rust", "serde", "Cargo.toml"
        )
        with open(manifest, "w") as legacy:
            legacy.write(CARGO_TOML.replace("MIT OR Apache-2.0", "MIT / Apache-2.0"))

        records, _ = self.records()
        self.assertEqual(
            records["third_party/rust/serde"]["licenses"], ["MIT OR Apache-2.0"]
        )

    def test_dependencies_become_edges(self):
        _, edges = self.records()
        self.assertEqual(edges["third_party/rust/serde"], ["cargo:log@0.4.20"])

    def test_versioned_dependency_picks_the_right_package(self):
        # Two versions of log coexist, so Cargo writes "log 0.4.20" and the
        # bare name would be ambiguous.
        _, edges = self.records()
        self.assertNotIn("cargo:log@0.3.9", edges.get("third_party/rust/serde", []))

    def test_crates_with_no_dependencies_have_no_edge(self):
        _, edges = self.records()
        self.assertNotIn("cargo:log@0.4.20", edges)

    def test_kinds_land_on_the_records(self):
        kinds = {("serde", "1.0.200"): ["normal"]}
        records, _ = crate_records(self.topsrcdir, kinds=kinds)
        by_ref = {record["bom_ref"]: record for record in records}
        self.assertEqual(by_ref["third_party/rust/serde"]["kinds"], ["normal"])
        # Nothing to say about a crate cargo metadata did not report.
        self.assertEqual(by_ref["cargo:log@0.4.20"]["kinds"], [])

    def test_is_tooling(self):
        self.assertTrue(is_tooling({"kinds": ["dev"]}))
        self.assertTrue(is_tooling({"kinds": ["dev", "tooling"]}))
        self.assertFalse(is_tooling({"kinds": ["dev", "normal"]}))
        self.assertTrue(is_tooling({"kinds": ["build", "tooling"]}))
        self.assertFalse(is_tooling({"kinds": ["build", "normal"]}))
        # Without cargo metadata nothing is known to be tooling.
        self.assertFalse(is_tooling({"kinds": []}))

    def test_unreadable_vendored_manifest_is_reported(self):
        manifest = os.path.join(
            self.topsrcdir, "third_party", "rust", "serde", "Cargo.toml"
        )
        with open(manifest, "w") as broken:
            broken.write("[package\nname =")

        records, _ = self.records()
        record = records["third_party/rust/serde"]
        # Without the property the crate reads as one declaring no license.
        self.assertEqual(record["licenses"], [])
        self.assertIn("moz:cargo.manifest-unreadable", record["properties"])

    def test_unresolvable_dependency_is_reported(self):
        # An edge Cargo.lock names but does not list as a package. A bare name
        # matching two versions is the other way to get here.
        with open(os.path.join(self.topsrcdir, "Cargo.lock"), "w") as lock:
            lock.write(
                LOCK.replace(
                    'dependencies = [\n "log 0.4.20",\n]',
                    'dependencies = [\n "log 0.4.20",\n "ghost 1.0.0",\n]',
                )
            )

        records, edges = self.records()
        self.assertEqual(
            records["third_party/rust/serde"]["properties"][
                "moz:cargo.unresolved-dependencies"
            ],
            "ghost 1.0.0",
        )
        # The edges that do resolve are unaffected.
        self.assertEqual(edges["third_party/rust/serde"], ["cargo:log@0.4.20"])

    def test_missing_cargo_is_not_an_error(self):
        # An unconfigured tree has no vendored-source config for cargo to use.
        self.assertEqual(
            collect_dependency_kinds(self.topsrcdir, "/nonexistent/objdir"), {}
        )

    def test_cargo_metadata_utf8(self):
        topobjdir = os.path.join(self.topsrcdir, "obj")
        os.makedirs(os.path.join(topobjdir, ".cargo"))
        metadata = {
            "workspace_members": ["gkrust@0.1.0"],
            "packages": [
                package("gkrust", "0.1.0", source=None),
                {**package("serde", "1.0.200"), "description": "с"},
            ],
            "resolve": {
                "nodes": [
                    node("gkrust", "0.1.0", [("serde", "1.0.200", None)]),
                    node("serde", "1.0.200", []),
                ]
            },
        }
        output = json.dumps(metadata, ensure_ascii=False).encode("utf-8")
        with open(
            os.path.join(self.topsrcdir, "metadata"), "w", encoding="utf-8"
        ) as fh:
            fh.write(f"import sys\nsys.stdout.buffer.write({output!r})\n")
        self.assertEqual(
            collect_dependency_kinds(self.topsrcdir, topobjdir, sys.executable),
            {("serde", "1.0.200"): ["normal"]},
        )

    def test_rustc_is_passed_to_cargo(self):
        # Resolving the graph makes cargo run `rustc -vV`, which it looks for
        # on PATH; the Linux build tasks do not have it there, so the RUSTC
        # subst has to reach the subprocess or `cargo metadata` exits 101.
        cargo_home = os.path.join(self.topsrcdir, ".cargo")
        os.makedirs(cargo_home, exist_ok=True)
        empty = {
            "workspace_root": self.topsrcdir,
            "workspace_members": [],
            "packages": [],
            "resolve": {"nodes": []},
        }
        completed = subprocess.CompletedProcess(
            [], 0, stdout=json.dumps(empty), stderr=""
        )
        with mock.patch("subprocess.run", return_value=completed) as run:
            collect_dependency_kinds(
                self.topsrcdir,
                self.topsrcdir,
                "/path/to/cargo",
                rustc="/path/to/rustc",
            )
        self.assertEqual(run.call_args.kwargs["env"]["RUSTC"], "/path/to/rustc")

    def test_missing_cargo_lock_is_not_an_error(self):
        os.remove(os.path.join(self.topsrcdir, "Cargo.lock"))
        self.assertEqual(crate_records(self.topsrcdir), ([], {}))


def package(name, version, source=REGISTRY_SOURCE):
    return {
        "id": f"{name}@{version}",
        "name": name,
        "version": version,
        "source": source,
    }


def node(name, version, deps):
    """A resolve node. ``deps`` is [(name, version, kind)]."""
    return {
        "id": f"{name}@{version}",
        "deps": [
            {"pkg": f"{dep}@{dep_version}", "dep_kinds": [{"kind": kind}]}
            for dep, dep_version, kind in deps
        ],
    }


class TestDependencyKinds(unittest.TestCase):
    """The kinds Cargo.lock cannot express, from `cargo metadata`."""

    def metadata(self):
        return {
            "workspace_members": ["gkrust@0.1.0"],
            "packages": [
                package("gkrust", "0.1.0", source=None),
                package("serde", "1.0.200"),
                package("log", "0.4.20"),
                package("cc", "1.0.0"),
                package("mockall", "0.13.0"),
                package("fragile", "2.0.0"),
            ],
            "resolve": {
                "nodes": [
                    node(
                        "gkrust",
                        "0.1.0",
                        [
                            ("serde", "1.0.200", None),
                            ("cc", "1.0.0", "build"),
                            ("mockall", "0.13.0", "dev"),
                        ],
                    ),
                    node("serde", "1.0.200", [("log", "0.4.20", None)]),
                    node("log", "0.4.20", []),
                    node("cc", "1.0.0", [("log", "0.4.20", None)]),
                    node("mockall", "0.13.0", [("fragile", "2.0.0", None)]),
                    node("fragile", "2.0.0", []),
                ]
            },
        }

    def test_kinds_are_propagated_down_the_graph(self):
        kinds = dependency_kinds(self.metadata())
        self.assertEqual(kinds[("serde", "1.0.200")], ["normal"])
        self.assertEqual(kinds[("cc", "1.0.0")], ["build"])
        # Reached normally through serde and as a build dependency through cc.
        self.assertEqual(kinds[("log", "0.4.20")], ["build", "normal"])

    def test_dev_only_crates_are_labelled_dev(self):
        kinds = dependency_kinds(self.metadata())
        self.assertEqual(kinds[("mockall", "0.13.0")], ["dev"])
        # A dev-dependency's own dependencies are dev too, not normal: nothing
        # under mockall is built into the product.
        self.assertEqual(kinds[("fragile", "2.0.0")], ["dev"])

    def test_workspace_members_are_not_reported(self):
        self.assertNotIn(("gkrust", "0.1.0"), dependency_kinds(self.metadata()))

    def test_proc_macros_and_their_dependencies_are_build(self):
        metadata = self.metadata()
        metadata["packages"] += [
            dict(
                package("serde_derive", "1.0.200"),
                targets=[{"kind": ["proc-macro"]}],
            ),
            package("syn", "2.0.0"),
        ]
        nodes = metadata["resolve"]["nodes"]
        nodes[0]["deps"].append({
            "pkg": "serde_derive@1.0.200",
            "dep_kinds": [{"kind": None}],
        })
        nodes += [
            node("serde_derive", "1.0.200", [("syn", "2.0.0", None)]),
            node("syn", "2.0.0", []),
        ]
        kinds = dependency_kinds(metadata)
        self.assertEqual(kinds[("serde_derive", "1.0.200")], ["build"])
        self.assertEqual(kinds[("syn", "2.0.0")], ["build"])
        self.assertEqual(kinds[("serde", "1.0.200")], ["normal"])

    def test_dependencies_of_a_proc_macro_member_are_build(self):
        metadata = self.metadata()
        metadata["workspace_members"].append("style_derive@0.0.1")
        metadata["packages"] += [
            dict(
                package("style_derive", "0.0.1", source=None),
                targets=[{"kind": ["proc-macro"]}],
            ),
            package("syn", "2.0.0"),
        ]
        nodes = metadata["resolve"]["nodes"]
        nodes[0]["deps"].append({
            "pkg": "style_derive@0.0.1",
            "dep_kinds": [{"kind": None}],
        })
        nodes += [
            node("style_derive", "0.0.1", [("syn", "2.0.0", None)]),
            node("syn", "2.0.0", []),
        ]
        self.assertEqual(dependency_kinds(metadata)[("syn", "2.0.0")], ["build"])

    def test_dependencies_of_a_member_only_a_build_script_uses_do_not_ship(self):
        metadata = self.metadata()
        metadata["workspace_members"].append("build_helper@0.0.1")
        metadata["packages"] += [
            package("build_helper", "0.0.1", source=None),
            package("cc", "1.0.0"),
        ]
        nodes = metadata["resolve"]["nodes"]
        nodes[0]["deps"].append({
            "pkg": "build_helper@0.0.1",
            "dep_kinds": [{"kind": "build"}],
        })
        nodes += [
            node("build_helper", "0.0.1", [("cc", "1.0.0", None)]),
            node("cc", "1.0.0", []),
        ]
        self.assertNotIn("normal", dependency_kinds(metadata)[("cc", "1.0.0")])

    def test_a_build_dependency_of_a_dependency_is_build(self):
        metadata = self.metadata()
        metadata["packages"] += [
            package("bindgen", "0.70.0"),
            package("clang-sys", "1.0.0"),
        ]
        metadata["resolve"]["nodes"][1]["deps"].append({
            "pkg": "bindgen@0.70.0",
            "dep_kinds": [{"kind": "build"}],
        })
        metadata["resolve"]["nodes"] += [
            node("bindgen", "0.70.0", [("clang-sys", "1.0.0", None)]),
            node("clang-sys", "1.0.0", []),
        ]
        self.assertEqual(dependency_kinds(metadata)[("clang-sys", "1.0.0")], ["build"])


class TestToolingMembers(unittest.TestCase):
    """A crate only a test server or a code generator builds does not ship."""

    def member(self, name, directory):
        return dict(
            package(name, "0.1.0", source=None),
            manifest_path=f"/src/{directory}/Cargo.toml",
        )

    def metadata(self):
        return {
            "workspace_root": "/src",
            "workspace_members": ["gkrust@0.1.0", "server@0.1.0", "shared@0.1.0"],
            "packages": [
                self.member("gkrust", "toolkit/library/rust"),
                self.member("server", "netwerk/test/server"),
                self.member("shared", "netwerk/shared"),
                package("serde", "1.0.200"),
                package("hyper", "1.0.0"),
                package("tokio", "1.0.0"),
            ],
            "resolve": {
                "nodes": [
                    node("gkrust", "0.1.0", [("shared", "0.1.0", None)]),
                    node(
                        "server",
                        "0.1.0",
                        [("shared", "0.1.0", None), ("hyper", "1.0.0", None)],
                    ),
                    node("shared", "0.1.0", [("serde", "1.0.200", None)]),
                    node("serde", "1.0.200", []),
                    node("hyper", "1.0.0", [("tokio", "1.0.0", None)]),
                    node("tokio", "1.0.0", []),
                ]
            },
        }

    def test_tooling_members(self):
        kinds = dependency_kinds(
            self.metadata(), tooling_members=("netwerk/test/server",)
        )
        self.assertEqual(kinds[("hyper", "1.0.0")], ["tooling"])
        self.assertEqual(kinds[("tokio", "1.0.0")], ["tooling"])
        # A member the product also builds stays shipped, whoever else uses it.
        self.assertEqual(kinds[("serde", "1.0.200")], ["normal", "tooling"])

    def test_a_member_only_tooling_builds_is_tooling(self):
        metadata = self.metadata()
        metadata["workspace_members"].append("helper@0.1.0")
        metadata["packages"] += [
            self.member("helper", "testing/helper"),
            package("warp", "0.3.0"),
        ]
        nodes = metadata["resolve"]["nodes"]
        nodes[1]["deps"].append({"pkg": "helper@0.1.0", "dep_kinds": [{"kind": None}]})
        nodes += [
            node("helper", "0.1.0", [("warp", "0.3.0", None)]),
            node("warp", "0.3.0", []),
        ]
        kinds = dependency_kinds(metadata, tooling_members=("netwerk/test/server",))
        self.assertEqual(kinds[("warp", "0.3.0")], ["tooling"])

    def test_a_node_no_package_describes_is_left_out(self):
        # cargo metadata resolves a node the package list does not cover: it
        # has neither a directory nor a name to report, and dropping it must
        # not take the rest of the graph with it.
        metadata = self.metadata()
        metadata["resolve"]["nodes"].append(
            node("ghost", "0.1.0", [("serde", "1.0.200", None)])
        )
        metadata["resolve"]["nodes"][0]["deps"].append({
            "pkg": "ghost@0.1.0",
            "dep_kinds": [{"kind": None}],
        })
        kinds = dependency_kinds(metadata, tooling_members=("netwerk/test/server",))
        self.assertNotIn(("ghost", "0.1.0"), kinds)
        self.assertEqual(kinds[("serde", "1.0.200")], ["normal", "tooling"])

    def test_an_unlisted_member_counts_as_shipped(self):
        kinds = dependency_kinds(self.metadata(), tooling_members=())
        self.assertEqual(kinds[("hyper", "1.0.0")], ["normal"])

    def test_stale_tooling_member_is_reported(self):
        messages = []
        dependency_kinds(
            self.metadata(),
            tooling_members=("netwerk/test/server", "netwerk/test/moved"),
            report=messages.append,
        )
        self.assertEqual(
            messages,
            ["TOOLING_MEMBERS names netwerk/test/moved, which is no workspace member"],
        )


if __name__ == "__main__":
    main()
