# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import unittest
from unittest import mock

import mozunit
from mozversioncontrol import InvalidRepoPath

from mozbuild.controller import building
from mozbuild.controller.building import SOURCE_REPO_FILE, write_source_repo


class TestWriteSourceRepo(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.topobjdir = self._tmp.name
        self.path = os.path.join(self.topobjdir, SOURCE_REPO_FILE)

    def tearDown(self):
        self._tmp.cleanup()

    def _write(self, substs=None, head_rev=None, side_effect=None, topsrcdir="/src"):
        vcs = mock.Mock(head_rev=head_rev)
        with mock.patch.object(
            building,
            "get_repository_from_build_config",
            return_value=vcs,
            side_effect=side_effect,
        ) as get_repo:
            write_source_repo(self.topobjdir, topsrcdir, substs or {})
        return get_repo

    def _read(self):
        with open(self.path, encoding="utf-8") as fh:
            return fh.read()

    def test_configured(self):
        get_repo = self._write({
            "MOZ_SOURCE_REPO": "https://hg.mozilla.org/mozilla-central",
            "MOZ_SOURCE_CHANGESET": "abc123",
        })

        self.assertEqual(
            self._read(), "abc123\nhttps://hg.mozilla.org/mozilla-central\n"
        )
        self.assertEqual(get_repo.call_count, 0)

    def test_unconfigured(self):
        get_repo = self._write(head_rev="def456")

        self.assertEqual(self._read(), "\n\n")
        self.assertEqual(get_repo.call_count, 0)

    def test_configured_repo_changeset_from_vcs(self):
        substs = {"MOZ_SOURCE_REPO": "ssh://example.com/repo/"}
        get_repo = self._write(substs, head_rev="def456")

        self.assertEqual(self._read(), "def456\nssh://example.com/repo/\n")
        config = get_repo.call_args.args[0]
        self.assertEqual((config.topsrcdir, config.substs), ("/src", substs))

    def test_configured_repo_without_changeset(self):
        with self.assertRaisesRegex(Exception, "could not resolve changeset"):
            self._write(
                {"MOZ_SOURCE_REPO": "https://example.com/repo"},
                side_effect=InvalidRepoPath("no repo"),
            )

    def _write_sourcestamp(self, body):
        with tempfile.TemporaryDirectory() as topsrcdir:
            with open(
                os.path.join(topsrcdir, "sourcestamp.txt"), "w", encoding="utf-8"
            ) as fh:
                fh.write(body)
            self._write(topsrcdir=topsrcdir)

    def test_sourcestamp(self):
        self._write_sourcestamp(
            "20260926120000\nhttps://hg.mozilla.org/mozilla-central/rev/abc123\n"
        )

        self.assertEqual(
            self._read(), "abc123\nhttps://hg.mozilla.org/mozilla-central\n"
        )

    def test_sourcestamp_malformed(self):
        self._write_sourcestamp("20260926120000\n")

        self.assertEqual(self._read(), "\n\n")

    def test_reconfigured(self):
        repo = {"MOZ_SOURCE_REPO": "https://example.com/repo"}
        self._write({**repo, "MOZ_SOURCE_CHANGESET": "first"})
        self._write({**repo, "MOZ_SOURCE_CHANGESET": "second"})

        self.assertEqual(self._read(), "second\nhttps://example.com/repo\n")


if __name__ == "__main__":
    mozunit.main()
