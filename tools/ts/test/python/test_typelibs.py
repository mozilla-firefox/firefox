# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import unittest

import buildconfig
import mozunit

sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
import typelib_inputs
import xpidl_typelib

DATA = "tools/ts/test/python/data"


class TestTypelibs(unittest.TestCase):
    def test_select_source_files(self):
        found = typelib_inputs.select_source_files(
            [
                "a/one.idl",
                "a/b/two.idl",
                "a/b/other.txt",
                "excluded/three.idl",
                "excluded-not/four.idl",
            ],
            ["**/*.idl"],
            ["excluded"],
        )

        self.assertEqual(found, ["a/b/two.idl", "a/one.idl", "excluded-not/four.idl"])

    def test_idl_files(self):
        self.assertEqual(
            typelib_inputs.idl_files([
                "a/nsIOne.idl",
                "a/nsIOne.h",
                "third_party/b/nsITwo.idl",
            ]),
            ["a/nsIOne.idl"],
        )

    def test_xpidl_typelib(self):
        ts_data = xpidl_typelib.xpidl_typelib(
            buildconfig.topsrcdir,
            [
                f"{DATA}/xpidl/a/nsITsSupports.idl",
                f"{DATA}/xpidl/b/nsITsChild.idl",
            ],
        )

        self.assertEqual(
            ts_data,
            [
                {
                    "path": f"{DATA}/xpidl/a/nsITsSupports.idl",
                    "interfaces": [
                        {
                            "id": "nsISupports",
                            "base": None,
                            "callable": False,
                            "enums": [],
                            "consts": [],
                            "members": [],
                        }
                    ],
                    "typedefs": [],
                },
                {
                    "path": f"{DATA}/xpidl/b/nsITsChild.idl",
                    "interfaces": [
                        {
                            "id": "nsITsChild",
                            "base": "nsISupports",
                            "callable": False,
                            "enums": [],
                            "consts": [],
                            "members": [
                                {"name": "count", "type": "i32", "readonly": True}
                            ],
                        }
                    ],
                    "typedefs": [],
                },
            ],
        )

    def test_xpidl_typelib_same_file_name(self):
        with self.assertRaisesRegex(Exception, "have the same file name"):
            xpidl_typelib.xpidl_typelib(
                buildconfig.topsrcdir, ["a/nsIFoo.idl", "b/nsIFoo.idl"]
            )

    def test_idl_excluded_dirs_sorted(self):
        self.assertEqual(
            list(typelib_inputs.IDL_EXCLUDED_DIRS),
            sorted(typelib_inputs.IDL_EXCLUDED_DIRS),
        )


if __name__ == "__main__":
    mozunit.main()
