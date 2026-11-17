# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import unittest

import buildconfig
import mozunit

sys.path.append(os.path.join(os.path.dirname(__file__), "..", ".."))
import services_typelib
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

    def test_js_files(self):
        self.assertEqual(
            typelib_inputs.js_files([
                "a/one.js",
                "a/two.mjs",
                "a/three.html",
                "a/four.xhtml",
                "a/five.jsx",
                "testing/web-platform/b/six.js",
            ]),
            ["a/four.xhtml", "a/one.js", "a/three.html", "a/two.mjs"],
        )

    def test_services_typelib(self):
        services = services_typelib.services_typelib(
            buildconfig.topsrcdir, [f"{DATA}/services/a/components.conf"]
        )

        self.assertEqual(
            services,
            {"nsITsAll": "tsAll", "nsITsDefined": "tsDefined", "nsITsMac": "tsMac"},
        )

    def test_services_typelib_conflict(self):
        with self.assertRaisesRegex(
            Exception, "nsITsAll is exposed as both Services.tsAll and Services.tsOther"
        ):
            services_typelib.services_typelib(
                buildconfig.topsrcdir,
                [
                    f"{DATA}/services/a/components.conf",
                    f"{DATA}/services/b/components.conf",
                ],
            )

    def test_conf_files(self):
        self.assertEqual(
            typelib_inputs.conf_files([
                "a/components.conf",
                "a/other.conf",
                "third_party/b/components.conf",
            ]),
            ["a/components.conf"],
        )

    def test_services_tables_sorted(self):
        self.assertEqual(
            list(typelib_inputs.CONF_EXCLUDED_DIRS),
            sorted(typelib_inputs.CONF_EXCLUDED_DIRS),
        )
        self.assertEqual(
            list(services_typelib.PLATFORM_SUBSTS),
            sorted(services_typelib.PLATFORM_SUBSTS),
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

    def test_patterns_sorted(self):
        for table in (typelib_inputs.JS_PATTERNS, typelib_inputs.WEBIDL_PATTERNS):
            self.assertEqual(list(table), sorted(table))

    def test_idl_excluded_dirs_sorted(self):
        self.assertEqual(
            list(typelib_inputs.IDL_EXCLUDED_DIRS),
            sorted(typelib_inputs.IDL_EXCLUDED_DIRS),
        )


if __name__ == "__main__":
    mozunit.main()
