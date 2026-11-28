# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import email.message
import json
import os
import tempfile
import textwrap
import unittest
from unittest import mock

import yaml
from mozunit import main

from mozbuild.vendor.sbom_python import (
    canonical_name,
    home_page,
    licenses_from_metadata,
    pypi_purl,
    python_records,
    read_metadata,
)

VSDOWNLOAD_MOZ_YAML = {
    "schema": "1",
    "bugzilla": {"product": "Firefox Build System", "component": "General"},
    "origin": {
        "name": "msvc-wine",
        "description": "MSVC download script",
        "url": "https://github.com/mstorsjo/msvc-wine/",
        "license": "ISC",
        "release": "a99ef92 (2026-03-24T08:23:19Z).",
        "revision": "a99ef92",
    },
}

UV_LOCK = textwrap.dedent(
    """
    version = 1

    [[package]]
    name = "mozilla-third-party-python-vendor-dir"
    version = "0.1.0"
    source = { virtual = "." }
    dependencies = [{ name = "fluent-syntax" }]

    [[package]]
    name = "fluent-syntax"
    version = "0.19.0"
    source = { registry = "https://pypi.org/simple" }
    dependencies = [
        { name = "typing-extensions" },
        { name = "hglib" },
    ]
    sdist = { url = "https://files.example/fluent.syntax-0.19.0.tar.gz", hash = "sha256:aaaa" }
    wheels = [
        { url = "https://files.example/fluent.syntax-0.19.0-py2.py3-none-any.whl", hash = "sha256:bbbb" },
    ]

    [[package]]
    name = "hglib"
    version = "2.6.2"
    source = { git = "https://example.com/hglib" }

    [[package]]
    name = "typing-extensions"
    version = "4.15.0"
    source = { registry = "https://pypi.org/simple" }
    sdist = { url = "https://files.example/typing_extensions-4.15.0.tar.gz", hash = "sha256:cccc" }
    wheels = [
        { url = "https://files.example/typing_extensions-4.15.0-cp312-cp312-manylinux.whl", hash = "sha256:dddd" },
    ]

    [[package]]
    name = "pyyaml"
    version = "6.0.3"
    source = { registry = "https://pypi.org/simple" }
    sdist = { url = "https://files.example/pyyaml-6.0.3.tar.gz", hash = "sha256:eeee" }

    [[package]]
    name = "attrs"
    version = "3.0"
    source = { registry = "https://pypi.org/simple" }
    wheels = [
        { url = "https://files.example/attrs-3.0-py2-none-any.whl", hash = "sha256:ffff" },
        { url = "https://files.example/attrs-3.0-py3-none-any.whl", hash = "sha256:0000" },
    ]

    [[package]]
    name = "ansicon"
    version = "1.89.0"
    source = { registry = "https://pypi.org/simple" }
    sdist = { url = "https://files.example/ansicon-1.89.0.tar.gz", hash = "sha256:1111" }

    [[package]]
    name = "six"
    version = "1.17.0"
    source = { registry = "https://pypi.org/simple" }

    [[package]]
    name = "legacy"
    version = "1.0"
    source = { registry = "https://pypi.org/simple" }
    """
)


def metadata(**fields):
    message = email.message.Message()
    for name, values in fields.items():
        for value in values if isinstance(values, list) else [values]:
            message[name.replace("_", "-")] = value
    return message


class TestHelpers(unittest.TestCase):
    def test_canonical_name(self):
        self.assertEqual(canonical_name("Fluent.Syntax"), "fluent-syntax")
        self.assertEqual(canonical_name("boolean_py"), "boolean-py")

    def test_pypi_purl(self):
        self.assertEqual(
            pypi_purl("typing_extensions", "4.15.0"),
            ("pypi", None, "typing-extensions", "4.15.0", {}),
        )

    def test_licenses_from_metadata(self):
        self.assertEqual(
            licenses_from_metadata(
                metadata(License_Expression="MIT", License="Apache 2.0")
            ),
            ["MIT"],
        )
        self.assertEqual(
            licenses_from_metadata(metadata(License="APL 2")), ["Apache-2.0"]
        )
        self.assertEqual(licenses_from_metadata(metadata(License="MPL2")), ["MPL-2.0"])
        self.assertEqual(licenses_from_metadata(metadata(License="BSD")), ["BSD"])
        # A whole license text in the field is not a license name.
        self.assertEqual(
            licenses_from_metadata(
                metadata(
                    License="Copyright 2010 Pallets\n\n Redistribution...",
                    Classifier=[
                        "License :: OSI Approved :: MIT License",
                        "License :: OSI Approved :: BSD License",
                        "Programming Language :: Python",
                    ],
                )
            ),
            ["BSD License", "MIT"],
        )
        self.assertEqual(licenses_from_metadata(metadata(License="UNKNOWN")), [])

    def test_home_page(self):
        self.assertEqual(
            home_page(
                metadata(Home_page="UNKNOWN", Project_URL="Homepage, https://a.example")
            ),
            "https://a.example",
        )
        self.assertEqual(
            licenses_from_metadata(metadata(Classifier="License :: OSI Approved")), []
        )


class PythonFixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.topsrcdir = self.tmp.name
        self.write("third_party/python/uv.lock", UV_LOCK)
        self.write(
            "third_party/python/fluent.syntax/fluent.syntax-0.19.0.dist-info/METADATA",
            "Metadata-Version: 2.1\nName: fluent.syntax\nVersion: 0.19.0\n"
            "Summary: Localization library\nLicense: APL 2\n"
            "Project-URL: Changelog, https://example.com/changes\n"
            "Project-URL: Source Code, https://example.com/fluent\n",
        )
        self.write(
            "third_party/python/typing_extensions/PKG-INFO",
            "Metadata-Version: 2.4\nName: typing_extensions\nVersion: 4.14.0\n"
            "License-Expression: PSF-2.0\n",
        )
        self.write(
            "third_party/python/ansicon/ansicon-1.89.0.dist-info/METADATA",
            "Metadata-Version: 2.1\nName: ansicon\nVersion: 1.89.0\nLicense: MPL-2.0\n",
        )
        self.write(
            "third_party/python/pyyaml/PKG-INFO",
            "Metadata-Version: 2.1\nName: PyYAML\nVersion: 6.0.3\n"
            "License-File: LICENSE\n",
        )

        # Not UTF-8.
        self.write_bytes("third_party/python/six/PKG-INFO", b"Name: six\xff\n")
        self.write(
            "third_party/python/legacy/legacy.egg-info/PKG-INFO",
            "Metadata-Version: 1.1\nName: legacy\nVersion: 1.0.0\n"
            "Home-page: https://example.com/legacy\nLicense: MIT\n",
        )

        # Vendored by hand, outside uv.lock.
        self.write(
            "third_party/python/dlmanager/dlmanager.egg-info/PKG-INFO",
            "Metadata-Version: 2.1\nName: dlmanager\nVersion: 0.1.1\n"
            "License: GPL/LGPL\n",
        )
        self.write("third_party/python/gyp/setup.py", "")
        self.write(
            "third_party/python/vsdownload/moz.yaml",
            yaml.safe_dump(VSDOWNLOAD_MOZ_YAML),
        )
        self.write("third_party/python/_venv/wheels/pip.whl", "")

        records, self.edges = python_records(self.topsrcdir)
        self.refs = [record["bom_ref"] for record in records]
        self.by_ref = dict(zip(self.refs, records))

    def write(self, path, content):
        self.write_bytes(path, content.encode())

    def write_bytes(self, path, content):
        path = os.path.join(self.topsrcdir, path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(content)


class TestPythonRecords(PythonFixture):
    def test_packages(self):
        self.assertEqual(
            self.refs,
            [
                "third_party/python/ansicon",
                "pypi:attrs@3.0",
                "third_party/python/fluent.syntax",
                "third_party/python/legacy",
                "third_party/python/pyyaml",
                "third_party/python/six",
                "third_party/python/typing_extensions",
                "third_party/python/dlmanager",
                "third_party/python/gyp",
                "third_party/python/vsdownload",
            ],
        )

    def test_record(self):
        record = self.by_ref["third_party/python/fluent.syntax"]
        self.assertEqual(record["name"], "fluent-syntax")
        self.assertEqual(record["purl"], ("pypi", None, "fluent-syntax", "0.19.0", {}))
        self.assertEqual(record["description"], "Localization library")
        self.assertEqual(record["website"], "https://example.com/fluent")
        self.assertEqual(record["vcs"], "https://example.com/fluent")
        self.assertEqual(record["licenses"], ["Apache-2.0"])
        self.assertEqual(record["kinds"], ["dev"])
        self.assertEqual(record["occurrences"], ["third_party/python/fluent.syntax"])
        # The pure wheel is what `mach vendor python` unpacked.
        self.assertEqual(record["hashes"], [("SHA-256", "bbbb")])
        self.assertEqual(
            record["properties"],
            {
                "moz:pypi.lockfile": "third_party/python/uv.lock",
                "moz:pypi.artifact": "fluent.syntax-0.19.0-py2.py3-none-any.whl",
                "moz:pypi.unresolved-dependencies": "hglib",
            },
        )

    def test_sdist_when_no_pure_wheel(self):
        record = self.by_ref["third_party/python/typing_extensions"]
        self.assertEqual(record["hashes"], [("SHA-256", "cccc")])
        self.assertEqual(record["licenses"], ["PSF-2.0"])
        self.assertEqual(record["properties"]["moz:pypi.vendored-version"], "4.14.0")

    def test_license_file_is_recorded_when_no_license_is_declared(self):
        record = self.by_ref["third_party/python/pyyaml"]
        self.assertEqual(record["licenses"], [])
        self.assertEqual(record["properties"]["moz:pypi.license-file"], "LICENSE")

    def test_not_vendored_is_reported(self):
        record = self.by_ref["pypi:attrs@3.0"]
        self.assertEqual(record["occurrences"], [])
        self.assertEqual(record["properties"]["moz:pypi.not-vendored"], "true")
        self.assertNotIn("moz:pypi.metadata-unreadable", record["properties"])

    def test_vendoring_excluded_has_no_hash(self):
        record = self.by_ref["third_party/python/ansicon"]
        self.assertEqual(record["hashes"], [])
        self.assertNotIn("moz:pypi.artifact", record["properties"])
        self.assertEqual(record["properties"]["moz:pypi.vendoring-excluded"], "true")

    def test_several_pure_wheels_leave_the_hash_undecided(self):
        record = self.by_ref["pypi:attrs@3.0"]
        self.assertEqual(record["hashes"], [])
        self.assertNotIn("moz:pypi.artifact", record["properties"])
        self.assertEqual(
            record["properties"]["moz:pypi.ambiguous-artifacts"],
            "attrs-3.0-py2-none-any.whl,attrs-3.0-py3-none-any.whl",
        )

    def test_unreadable_metadata_is_reported(self):
        record = self.by_ref["third_party/python/six"]
        self.assertEqual(record["licenses"], [])
        self.assertIn("moz:pypi.metadata-unreadable", record["properties"])

    def test_unreadable_metadata_falls_back_to_the_next_copy(self):
        self.write_bytes("third_party/python/both/PKG-INFO", b"Name: both\xff\n")
        self.write("third_party/python/both/both.egg-info/PKG-INFO", "Name: both\n")
        message, error = read_metadata(
            os.path.join(self.topsrcdir, "third_party/python/both")
        )
        self.assertIsNone(error)
        self.assertEqual(message["Name"], "both")

    def test_egg_info_metadata(self):
        record = self.by_ref["third_party/python/legacy"]
        self.assertEqual(record["licenses"], ["MIT"])
        self.assertEqual(record["website"], "https://example.com/legacy")
        self.assertNotIn("moz:pypi.metadata-unreadable", record["properties"])
        # 1.0.0 is the lockfile's 1.0.
        self.assertNotIn("moz:pypi.vendored-version", record["properties"])

    def test_edges(self):
        self.assertEqual(
            self.edges,
            {
                "third_party/python/fluent.syntax": [
                    "third_party/python/typing_extensions"
                ]
            },
        )

    def test_package_outside_the_lockfile(self):
        record = self.by_ref["third_party/python/dlmanager"]
        self.assertEqual(record["purl"], ("pypi", None, "dlmanager", "0.1.1", {}))
        self.assertEqual(record["licenses"], ["GPL/LGPL"])
        self.assertEqual(record["kinds"], ["dev"])
        self.assertEqual(record["properties"], {"moz:pypi.not-in-lockfile": "true"})

    def test_package_outside_the_lockfile_without_metadata(self):
        record = self.by_ref["third_party/python/gyp"]
        self.assertEqual((record["name"], record["version"]), ("gyp", None))
        self.assertIsNone(record["purl"])
        self.assertIn("moz:pypi.metadata-unreadable", record["properties"])

    def test_package_outside_the_lockfile_with_a_moz_yaml(self):
        record = self.by_ref["third_party/python/vsdownload"]
        self.assertEqual(record["name"], "msvc-wine")
        self.assertEqual(record["licenses"], ["ISC"])
        self.assertEqual(record["kinds"], ["dev"])
        self.assertEqual(
            record["properties"]["moz:moz-yaml.path"],
            "third_party/python/vsdownload/moz.yaml",
        )
        self.assertEqual(record["properties"]["moz:pypi.not-in-lockfile"], "true")

    def test_missing_lockfile(self):
        self.assertEqual(python_records(os.path.join(self.topsrcdir, "none")), ([], {}))


class TestBuildToolingDocument(PythonFixture):
    class Repo:
        head_rev = "abc"

        def get_commit_time(self):
            return 0

        def get_tracked_files_finder(self, path):
            from mozpack.files import FileFinder

            return FileFinder(path)

    @mock.patch("mozbuild.vendor.sbom_cargo.collect_dependency_kinds", return_value={})
    def test_document(self, collect_dependency_kinds):
        from mozbuild.action.generate_sbom import build_tooling_document

        document = json.loads(
            build_tooling_document(
                self.topsrcdir, self.topsrcdir, self.Repo(), version="155.0a1"
            )
        )
        self.assertIn(
            "pkg:pypi/fluent-syntax@0.19.0",
            [component.get("purl") for component in document["components"]],
        )
        self.assertEqual(
            {component.get("scope") for component in document["components"]},
            {"excluded"},
        )


if __name__ == "__main__":
    main()
