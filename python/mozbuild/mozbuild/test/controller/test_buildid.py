# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import unittest

import mozunit

from mozbuild.controller.building import BUILDID_FILE, write_buildid

_SUBSTS = {"MOZ_BUILD_APP": "browser"}


class TestWriteBuildid(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.topobjdir = self._tmp.name
        self.path = os.path.join(self.topobjdir, BUILDID_FILE)

    def tearDown(self):
        self._tmp.cleanup()

    def _read(self):
        with open(self.path, encoding="utf-8") as fh:
            return fh.read()

    def test_build_date(self):
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"})

        self.assertEqual(self._read(), "20260926120000\n")

    def test_current_time(self):
        write_buildid(self.topobjdir, _SUBSTS, {})

        buildid = self._read().strip()
        self.assertEqual(len(buildid), 14)
        self.assertTrue(buildid.isdigit())

    def test_invalid_build_date(self):
        for value in ("2026", "2026092612000X"):
            write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": value})

            buildid = self._read().strip()
            self.assertNotEqual(buildid, value)
            self.assertEqual(len(buildid), 14)
            self.assertTrue(buildid.isdigit())

    def test_replaced_every_build(self):
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"})
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260927120000"})

        self.assertEqual(self._read(), "20260927120000\n")

    def test_replaced_by_faster_build(self):
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"})
        write_buildid(
            self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260927120000"}, ["faster"]
        )

        self.assertEqual(self._read(), "20260927120000\n")

    def test_replaced_by_header_build(self):
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"})
        write_buildid(
            self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260927120000"}, ["buildid.h"]
        )

        self.assertEqual(self._read(), "20260927120000\n")

    def test_partial_build_keeps_id(self):
        write_buildid(self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"})
        write_buildid(
            self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260927120000"}, ["binaries"]
        )

        self.assertEqual(self._read(), "20260926120000\n")

    def test_partial_build_first_id(self):
        write_buildid(
            self.topobjdir, _SUBSTS, {"MOZ_BUILD_DATE": "20260926120000"}, ["binaries"]
        )

        self.assertEqual(self._read(), "20260926120000\n")

    def test_android_keeps_first_id(self):
        substs = {"MOZ_BUILD_APP": "mobile/android"}
        write_buildid(self.topobjdir, substs, {"MOZ_BUILD_DATE": "20260926120000"})
        write_buildid(self.topobjdir, substs, {"MOZ_BUILD_DATE": "20260927120000"})

        self.assertEqual(self._read(), "20260926120000\n")


if __name__ == "__main__":
    mozunit.main()
