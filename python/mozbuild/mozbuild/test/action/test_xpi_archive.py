# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""What an extension archive takes from a staging area."""

import contextlib
import io
import os
import tempfile
import unittest
import zipfile

import mozpack.path as mozpath
import mozunit
from mozpack.manifests import InstallManifest

from mozbuild.action.xpi_archive import main, staged_members


def _write(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(body)


class TestStagedMembers(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = mozpath.normsep(self._tmp.name)

    def _track(self, destinations):
        manifest = InstallManifest()
        for dest in destinations:
            source = mozpath.join(self.tmp, "src", dest)
            _write(source, dest)
            manifest.add_link(source, dest)
        path = mozpath.join(self.tmp, "track")
        manifest.write(path=path)
        return path

    def _archive(self, track, stage, out):
        return main([
            "--track",
            track,
            "--stage-dir",
            stage,
            "--prefix",
            "one",
            "--dep-target",
            out,
            "--dep-file",
            mozpath.join(self.tmp, "deps", "out.pp"),
            out,
        ])

    def test_the_dependency_file_names_what_was_packaged(self):
        track = self._track(["one/a.js", "one/deep/c.js"])
        stage = mozpath.join(self.tmp, "stage")
        for rel in ("a.js", "deep/c.js"):
            _write(mozpath.join(stage, rel), rel)
        out = mozpath.join(self.tmp, "out.xpi")

        self.assertEqual(self._archive(track, stage, out), 0)

        with open(mozpath.join(self.tmp, "deps", "out.pp"), encoding="utf-8") as fh:
            deps = fh.read()
        self.assertIn(mozpath.join(stage, "a.js"), mozpath.normsep(deps))
        self.assertIn(mozpath.join(stage, "deep/c.js"), mozpath.normsep(deps))

    def _deps(self):
        with open(mozpath.join(self.tmp, "deps", "out.pp"), encoding="utf-8") as fh:
            return mozpath.normsep(fh.read())

    def test_dep_file_sources(self):
        track = self._track(["one/a.js", "one/deep/c.js"])
        stage = mozpath.join(self.tmp, "stage")
        for rel in ("a.js", "deep/c.js"):
            _write(mozpath.join(stage, rel), rel)
        out = mozpath.join(self.tmp, "out.xpi")

        self.assertEqual(self._archive(track, stage, out), 0)

        deps = self._deps()
        self.assertIn(mozpath.join(self.tmp, "src", "one/a.js"), deps)
        self.assertIn(mozpath.join(self.tmp, "src", "one/deep/c.js"), deps)

    def test_dep_file_preprocessed_includes(self):
        source = mozpath.join(self.tmp, "src", "p.js")
        included = mozpath.join(self.tmp, "src", "inc.js")
        _write(source, "#include inc.js\n")
        _write(included, "included\n")
        manifest = InstallManifest()
        manifest.add_preprocess(
            source, "one/p.js", mozpath.join(self.tmp, "p.js.pp"), defines={}
        )
        track = mozpath.join(self.tmp, "track")
        manifest.write(path=track)
        stage = mozpath.join(self.tmp, "stage")
        _write(mozpath.join(stage, "p.js"), "included\n")
        out = mozpath.join(self.tmp, "out.xpi")

        self.assertEqual(self._archive(track, stage, out), 0)

        deps = self._deps()
        self.assertIn(source, deps)
        self.assertIn(included, deps)

    def test_only_the_named_staging_area_is_taken(self):
        track = self._track(["one/a.js", "two/b.js", "one/deep/c.js"])

        self.assertEqual(staged_members(track, "one"), ["a.js", "deep/c.js"])

    def test_a_sibling_with_a_shared_prefix_is_not_taken(self):
        track = self._track(["one/a.js", "oneother/b.js"])

        self.assertEqual(staged_members(track, "one"), ["a.js"])

    def test_an_archive_contains_what_the_track_records(self):
        track = self._track(["one/a.js", "one/deep/c.js"])
        stage = mozpath.join(self.tmp, "stage")
        for rel in ("a.js", "deep/c.js"):
            _write(mozpath.join(stage, rel), rel)
        out = mozpath.join(self.tmp, "out.xpi")

        self.assertEqual(self._archive(track, stage, out), 0)
        with zipfile.ZipFile(out) as z:
            self.assertEqual(sorted(z.namelist()), ["a.js", "deep/c.js"])

    def test_a_file_the_track_does_not_record_is_left_out(self):
        track = self._track(["one/a.js"])
        stage = mozpath.join(self.tmp, "stage")
        _write(mozpath.join(stage, "a.js"), "a")
        _write(mozpath.join(stage, "stale.js"), "stale")
        out = mozpath.join(self.tmp, "out.xpi")

        self._archive(track, stage, out)

        with zipfile.ZipFile(out) as z:
            self.assertEqual(z.namelist(), ["a.js"])

    def test_a_recorded_file_that_is_not_staged_is_an_error(self):
        """An incomplete archive is worse than a failed build, because it still
        ships.
        """
        track = self._track(["one/a.js", "one/b.js"])
        stage = mozpath.join(self.tmp, "stage")
        _write(mozpath.join(stage, "a.js"), "a")
        out = mozpath.join(self.tmp, "out.xpi")

        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(self._archive(track, stage, out), 1)
        self.assertIn(
            f"1 file(s) the install recorded are not in {stage}:", err.getvalue()
        )
        self.assertFalse(os.path.exists(out))

    def test_an_archive_creates_the_directory_it_is_written_to(self):
        """`XPI_TESTDIR` names somewhere that need not exist yet."""
        track = self._track(["one/a.js"])
        stage = mozpath.join(self.tmp, "stage")
        _write(mozpath.join(stage, "a.js"), "a")
        out = mozpath.join(self.tmp, "not", "made", "yet", "out.xpi")

        self.assertEqual(self._archive(track, stage, out), 0)
        self.assertTrue(os.path.exists(out))

    def test_an_empty_staging_area_is_an_error(self):
        track = self._track(["other/a.js"])
        out = mozpath.join(self.tmp, "out.xpi")

        with contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(self._archive(track, self.tmp, out), 1)
        self.assertEqual(
            err.getvalue(), f"{out}: the install track records nothing under one\n"
        )


if __name__ == "__main__":
    mozunit.main()
