# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import unittest

import mozpack.path as mozpath
import mozunit

from mozbuild.frontend.jar_manifest import parse_jar_manifest

MANIFEST = """\
#filter substitution
browser.jar:
% content browser %content/browser/
  content/browser/main.xhtml     (main.xhtml)
* content/browser/built.js       (built.js)
  content/browser/wild/          (wild/*)
  locale/browser/thing.dtd       (%thing.dtd)
"""


def _write(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(body)


class TestParseJarManifest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmpdir = mozpath.normsep(self._tmp.name)

    def _parse(self, body=MANIFEST, defines=None, includes=None):
        for name, included in (includes or {}).items():
            _write(mozpath.join(self.tmpdir, name), included)
        path = mozpath.join(self.tmpdir, "jar.mn")
        _write(path, body)
        return parse_jar_manifest(path, defines or {}, "en-US")

    def test_one_section_is_named_and_based(self):
        sections, _ = self._parse()

        self.assertEqual(len(sections), 1)
        self.assertEqual(sections[0].name, "chrome/browser")
        self.assertEqual(sections[0].base, "")

    def test_entries_carry_source_and_destination(self):
        sections, _ = self._parse()
        entries = {e.output: e for e in sections[0].entries}

        self.assertEqual(entries["content/browser/main.xhtml"].source, "main.xhtml")
        self.assertEqual(entries["content/browser/wild/"].source, "wild/*")

    def test_a_star_marks_an_entry_for_preprocessing(self):
        sections, _ = self._parse()
        entries = {e.output: e for e in sections[0].entries}

        self.assertTrue(entries["content/browser/built.js"].preprocess)
        self.assertFalse(entries["content/browser/main.xhtml"].preprocess)

    def test_a_percent_marks_an_entry_as_localized(self):
        sections, _ = self._parse()
        entries = {e.output: e for e in sections[0].entries}

        self.assertTrue(entries["locale/browser/thing.dtd"].is_locale)
        self.assertFalse(entries["content/browser/main.xhtml"].is_locale)

    def test_chrome_manifest_lines_are_kept(self):
        sections, _ = self._parse()

        self.assertEqual(
            sections[0].chrome_manifests, ("content browser %content/browser/",)
        )

    def test_the_manifest_is_reported_as_a_read_input(self):
        _, inputs = self._parse()

        self.assertIn(
            mozpath.join(self.tmpdir, "jar.mn"),
            [mozpath.normsep(path) for path in inputs],
        )

    def test_an_included_file_is_reported_as_a_read_input(self):
        body = MANIFEST.replace(
            "browser.jar:", "#include included.inc\nbrowser.jar:", 1
        )
        _, inputs = self._parse(
            body, includes={"included.inc": "#define FROM_INCLUDE 1\n"}
        )

        self.assertIn(
            mozpath.join(self.tmpdir, "included.inc"),
            [mozpath.normsep(path) for path in inputs],
        )

    def test_a_define_from_an_included_file_reaches_the_preprocessor(self):
        body = MANIFEST.replace(
            "browser.jar:", "#include included.inc\nbrowser.jar:", 1
        )
        body += "  content/browser/@FROM_INCLUDE@.js  (named.js)\n"
        sections, _ = self._parse(
            body, includes={"included.inc": "#define FROM_INCLUDE included\n"}
        )
        outputs = [e.output for e in sections[0].entries]

        self.assertIn("content/browser/included.js", outputs)

    def test_defines_reach_the_preprocessor(self):
        body = MANIFEST + "  content/browser/@THING@.js  (named.js)\n"
        sections, _ = self._parse(body, defines={"THING": "resolved"})
        outputs = [e.output for e in sections[0].entries]

        self.assertIn("content/browser/resolved.js", outputs)


if __name__ == "__main__":
    mozunit.main()
