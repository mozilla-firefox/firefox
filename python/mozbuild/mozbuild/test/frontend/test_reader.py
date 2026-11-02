# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest

import mozpack.path as mozpath
from mozunit import main

from mozbuild import schedules
from mozbuild.frontend.context import BugzillaComponent
from mozbuild.frontend.reader import BuildReader, BuildReaderError
from mozbuild.test.common import MockConfig

data_path = mozpath.abspath(mozpath.dirname(__file__))
data_path = mozpath.join(data_path, "data")


class TestBuildReader(unittest.TestCase):
    def setUp(self):
        self._old_env = dict(os.environ)
        os.environ.pop("MOZ_OBJDIR", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._old_env)

    def config(self, name, **kwargs):
        path = mozpath.join(data_path, name)

        return MockConfig(path, **kwargs)

    def reader(self, name, enable_tests=False, error_is_fatal=True, **kwargs):
        extra = {}
        if enable_tests:
            extra["ENABLE_TESTS"] = "1"
        config = self.config(name, extra_substs=extra, error_is_fatal=error_is_fatal)

        return BuildReader(config, **kwargs)

    def file_path(self, name, *args):
        return mozpath.join(data_path, name, *args)

    def test_dirs_traversal_simple(self):
        reader = self.reader("traversal-simple")

        contexts = list(reader.read_topsrcdir())

        self.assertEqual(len(contexts), 4)

    def test_dirs_traversal_no_descend(self):
        reader = self.reader("traversal-simple")

        path = mozpath.join(reader.config.topsrcdir, "moz.build")
        self.assertTrue(os.path.exists(path))

        contexts = list(reader.read_mozbuild(path, reader.config, descend=False))

        self.assertEqual(len(contexts), 1)

    def test_dirs_traversal_all_variables(self):
        reader = self.reader("traversal-all-vars")

        contexts = list(reader.read_topsrcdir())
        self.assertEqual(len(contexts), 2)

        reader = self.reader("traversal-all-vars", enable_tests=True)

        contexts = list(reader.read_topsrcdir())
        self.assertEqual(len(contexts), 3)

    def test_relative_dirs(self):
        # Ensure relative directories are traversed.
        reader = self.reader("traversal-relative-dirs")

        contexts = list(reader.read_topsrcdir())
        self.assertEqual(len(contexts), 3)

    def test_repeated_dirs_error(self):
        reader = self.reader("traversal-repeated-dirs")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertEqual(
            e.actual_file, self.file_path("traversal-repeated-dirs", "bar", "moz.build")
        )
        self.assertIn(
            "File already read. A directory should not be added to DIRS twice: foo/moz.build is referred from moz.build as 'foo', and bar/moz.build as '../foo'",
            str(e),
        )

    def test_outside_topsrcdir(self):
        # References to directories outside the topsrcdir should fail.
        reader = self.reader("traversal-outside-topsrcdir")

        with self.assertRaises(Exception):
            list(reader.read_topsrcdir())

    def test_error_basic(self):
        reader = self.reader("reader-error-basic")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertEqual(
            e.actual_file, self.file_path("reader-error-basic", "moz.build")
        )

        self.assertIn("The error occurred while processing the", str(e))

    def test_error_included_from(self):
        reader = self.reader("reader-error-included-from")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertEqual(
            e.actual_file, self.file_path("reader-error-included-from", "child.build")
        )
        self.assertEqual(
            e.main_file, self.file_path("reader-error-included-from", "moz.build")
        )

        self.assertIn("This file was included as part of processing", str(e))

    def test_error_syntax_error(self):
        reader = self.reader("reader-error-syntax")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("Python syntax error on line 4", str(e))
        self.assertIn("    foo =", str(e))
        self.assertIn("         ^", str(e))

    def test_error_read_unknown_global(self):
        reader = self.reader("reader-error-read-unknown-global")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("The error was triggered on line 4", str(e))
        self.assertIn("The underlying problem is an attempt to read", str(e))
        self.assertIn("    FOO", str(e))

    def test_error_write_unknown_global(self):
        reader = self.reader("reader-error-write-unknown-global")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("The error was triggered on line 6", str(e))
        self.assertIn("The underlying problem is an attempt to write", str(e))
        self.assertIn("    FOO", str(e))

    def test_error_write_bad_value(self):
        reader = self.reader("reader-error-write-bad-value")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("The error was triggered on line 4", str(e))
        self.assertIn("is an attempt to write an illegal value to a special", str(e))

        self.assertIn("variable whose value was rejected is:\n\n    DIRS", str(e))

        self.assertIn(
            "written to it was of the following type:\n\n    %s" % "str", str(e)
        )

        self.assertIn("expects the following type(s):\n\n    list", str(e))

    def test_error_illegal_path(self):
        reader = self.reader("reader-error-outside-topsrcdir")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("The underlying problem is an illegal file access", str(e))

    def test_error_missing_include_path(self):
        reader = self.reader("reader-error-missing-include")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("we referenced a path that does not exist", str(e))

    def test_error_script_error(self):
        reader = self.reader("reader-error-script-error")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("The error appears to be the fault of the script", str(e))
        self.assertIn('    ["TypeError: unsupported operand', str(e))

    def test_error_bad_dir(self):
        reader = self.reader("reader-error-bad-dir")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("we referenced a path that does not exist", str(e))

    def test_error_repeated_dir(self):
        reader = self.reader("reader-error-repeated-dir")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("Directory (foo) registered multiple times", str(e))

    def test_error_error_func(self):
        reader = self.reader("reader-error-error-func")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("A moz.build file called the error() function.", str(e))
        self.assertIn("    Some error.", str(e))

    def test_error_error_func_ok(self):
        reader = self.reader("reader-error-error-func", error_is_fatal=False)

        list(reader.read_topsrcdir())

    def test_error_empty_list(self):
        reader = self.reader("reader-error-empty-list")

        with self.assertRaises(BuildReaderError) as bre:
            list(reader.read_topsrcdir())

        e = bre.exception
        self.assertIn("Variable DIRS assigned an empty value.", str(e))

    def test_inheriting_variables(self):
        reader = self.reader("inheriting-variables")

        contexts = list(reader.read_topsrcdir())

        self.assertEqual(len(contexts), 4)
        self.assertEqual(
            [context.relsrcdir for context in contexts], ["", "foo", "foo/baz", "bar"]
        )
        self.assertEqual(
            [context["XPIDL_MODULE"] for context in contexts],
            ["foobar", "foobar", "baz", "foobar"],
        )

    def test_find_relevant_mozbuilds(self):
        reader = self.reader("reader-relevant-mozbuild")

        # Absolute paths outside topsrcdir are rejected.
        with self.assertRaises(Exception):
            reader._find_relevant_mozbuilds(["/foo"])

        # File in root directory.
        paths = reader._find_relevant_mozbuilds(["file"])
        self.assertEqual(paths, {"file": ["moz.build"]})

        # File in child directory.
        paths = reader._find_relevant_mozbuilds(["d1/file1"])
        self.assertEqual(paths, {"d1/file1": ["moz.build", "d1/moz.build"]})

        # Multiple files in same directory.
        paths = reader._find_relevant_mozbuilds(["d1/file1", "d1/file2"])
        self.assertEqual(
            paths,
            {
                "d1/file1": ["moz.build", "d1/moz.build"],
                "d1/file2": ["moz.build", "d1/moz.build"],
            },
        )

        # Missing moz.build from missing intermediate directory.
        paths = reader._find_relevant_mozbuilds([
            "d1/no-intermediate-moz-build/child/file"
        ])
        self.assertEqual(
            paths,
            {
                "d1/no-intermediate-moz-build/child/file": [
                    "moz.build",
                    "d1/moz.build",
                    "d1/no-intermediate-moz-build/child/moz.build",
                ]
            },
        )

        # Lots of empty directories.
        paths = reader._find_relevant_mozbuilds([
            "d1/parent-is-far/dir1/dir2/dir3/file"
        ])
        self.assertEqual(
            paths,
            {
                "d1/parent-is-far/dir1/dir2/dir3/file": [
                    "moz.build",
                    "d1/moz.build",
                    "d1/parent-is-far/moz.build",
                ]
            },
        )

        # Lots of levels.
        paths = reader._find_relevant_mozbuilds([
            "d1/every-level/a/file",
            "d1/every-level/b/file",
        ])
        self.assertEqual(
            paths,
            {
                "d1/every-level/a/file": [
                    "moz.build",
                    "d1/moz.build",
                    "d1/every-level/moz.build",
                    "d1/every-level/a/moz.build",
                ],
                "d1/every-level/b/file": [
                    "moz.build",
                    "d1/moz.build",
                    "d1/every-level/moz.build",
                    "d1/every-level/b/moz.build",
                ],
            },
        )

        # Different root directories.
        paths = reader._find_relevant_mozbuilds(["d1/file", "d2/file", "file"])
        self.assertEqual(
            paths,
            {
                "file": ["moz.build"],
                "d1/file": ["moz.build", "d1/moz.build"],
                "d2/file": ["moz.build", "d2/moz.build"],
            },
        )

    def test_read_relevant_mozbuilds(self):
        reader = self.reader("reader-relevant-mozbuild")

        paths, contexts = reader.read_relevant_mozbuilds([
            "d1/every-level/a/file",
            "d1/every-level/b/file",
            "d2/file",
        ])
        self.assertEqual(len(paths), 3)
        self.assertEqual(len(contexts), 6)

        self.assertEqual(
            [ctx.relsrcdir for ctx in paths["d1/every-level/a/file"]],
            ["", "d1", "d1/every-level", "d1/every-level/a"],
        )
        self.assertEqual(
            [ctx.relsrcdir for ctx in paths["d1/every-level/b/file"]],
            ["", "d1", "d1/every-level", "d1/every-level/b"],
        )
        self.assertEqual([ctx.relsrcdir for ctx in paths["d2/file"]], ["", "d2"])

    def test_all_mozbuild_paths(self):
        reader = self.reader("reader-relevant-mozbuild")

        paths = list(reader.all_mozbuild_paths())
        # Ensure no duplicate paths.
        self.assertEqual(sorted(paths), sorted(set(paths)))
        self.assertEqual(len(paths), 10)

    def test_files_bad_bug_component(self):
        reader = self.reader("files-info")

        with self.assertRaises(BuildReaderError):
            reader.files_info(["bug_component/bad-assignment/moz.build"])

    def test_files_bug_component_static(self):
        reader = self.reader("files-info")

        v = reader.files_info([
            "bug_component/static/foo",
            "bug_component/static/bar",
            "bug_component/static/foo/baz",
        ])
        self.assertEqual(len(v), 3)
        self.assertEqual(
            v["bug_component/static/foo"]["BUG_COMPONENT"],
            BugzillaComponent("FooProduct", "FooComponent"),
        )
        self.assertEqual(
            v["bug_component/static/bar"]["BUG_COMPONENT"],
            BugzillaComponent("BarProduct", "BarComponent"),
        )
        self.assertEqual(
            v["bug_component/static/foo/baz"]["BUG_COMPONENT"],
            BugzillaComponent("default_product", "default_component"),
        )

    def test_files_bug_component_simple(self):
        reader = self.reader("files-info")

        v = reader.files_info(["bug_component/simple/moz.build"])
        self.assertEqual(len(v), 1)
        flags = v["bug_component/simple/moz.build"]
        self.assertEqual(flags["BUG_COMPONENT"].product, "Firefox Build System")
        self.assertEqual(flags["BUG_COMPONENT"].component, "General")

    def test_files_bug_component_different_matchers(self):
        reader = self.reader("files-info")

        v = reader.files_info([
            "bug_component/different-matchers/foo.sys.mjs",
            "bug_component/different-matchers/bar.cpp",
            "bug_component/different-matchers/baz.misc",
        ])
        self.assertEqual(len(v), 3)

        js_flags = v["bug_component/different-matchers/foo.sys.mjs"]
        cpp_flags = v["bug_component/different-matchers/bar.cpp"]
        misc_flags = v["bug_component/different-matchers/baz.misc"]

        self.assertEqual(js_flags["BUG_COMPONENT"], BugzillaComponent("Firefox", "JS"))
        self.assertEqual(
            cpp_flags["BUG_COMPONENT"], BugzillaComponent("Firefox", "C++")
        )
        self.assertEqual(
            misc_flags["BUG_COMPONENT"],
            BugzillaComponent("default_product", "default_component"),
        )

    def test_files_bug_component_final(self):
        reader = self.reader("files-info")

        v = reader.files_info([
            "bug_component/final/foo",
            "bug_component/final/Makefile.in",
            "bug_component/final/subcomponent/Makefile.in",
            "bug_component/final/subcomponent/bar",
        ])

        self.assertEqual(
            v["bug_component/final/foo"]["BUG_COMPONENT"],
            BugzillaComponent("default_product", "default_component"),
        )
        self.assertEqual(
            v["bug_component/final/Makefile.in"]["BUG_COMPONENT"],
            BugzillaComponent("Firefox Build System", "General"),
        )
        self.assertEqual(
            v["bug_component/final/subcomponent/Makefile.in"]["BUG_COMPONENT"],
            BugzillaComponent("Firefox Build System", "General"),
        )
        self.assertEqual(
            v["bug_component/final/subcomponent/bar"]["BUG_COMPONENT"],
            BugzillaComponent("Another", "Component"),
        )

    def test_invalid_flavor(self):
        reader = self.reader("invalid-files-flavor")

        with self.assertRaises(BuildReaderError):
            reader.files_info(["foo.js"])

    def test_schedules(self):
        reader = self.reader("schedules")
        info = reader.files_info([
            "win.and.osx",
            "somefile",
            "foo.win",
            "foo.osx",
            "subd/aa.py",
            "subd/yaml.py",
            "subd/win.js",
        ])
        # default: all exclusive, no inclusive
        self.assertEqual(info["somefile"]["SCHEDULES"].inclusive, [])
        self.assertEqual(
            info["somefile"]["SCHEDULES"].exclusive, schedules.EXCLUSIVE_COMPONENTS
        )
        # windows-only
        self.assertEqual(info["foo.win"]["SCHEDULES"].inclusive, [])
        self.assertEqual(info["foo.win"]["SCHEDULES"].exclusive, ["windows"])
        # osx-only
        self.assertEqual(info["foo.osx"]["SCHEDULES"].inclusive, [])
        self.assertEqual(info["foo.osx"]["SCHEDULES"].exclusive, ["macosx"])
        # top-level moz.build specifies subd/**.py with an inclusive option
        self.assertEqual(info["subd/aa.py"]["SCHEDULES"].inclusive, ["py-lint"])
        self.assertEqual(
            info["subd/aa.py"]["SCHEDULES"].exclusive, schedules.EXCLUSIVE_COMPONENTS
        )
        # Files('yaml.py') in subd/moz.build combines with Files('subdir/**.py')
        self.assertEqual(
            info["subd/yaml.py"]["SCHEDULES"].inclusive, ["py-lint", "yaml-lint"]
        )
        self.assertEqual(
            info["subd/yaml.py"]["SCHEDULES"].exclusive, schedules.EXCLUSIVE_COMPONENTS
        )
        # .. but exlusive does not override inclusive
        self.assertEqual(info["subd/win.js"]["SCHEDULES"].inclusive, ["js-lint"])
        self.assertEqual(info["subd/win.js"]["SCHEDULES"].exclusive, ["windows"])

        self.assertEqual(
            set(info["subd/yaml.py"]["SCHEDULES"].components),
            set(schedules.EXCLUSIVE_COMPONENTS + ["py-lint", "yaml-lint"]),
        )

        # win.and.osx is defined explicitly, and matches *.osx, and the two have
        # conflicting SCHEDULES.exclusive settings, so the later one is used
        self.assertEqual(
            set(info["win.and.osx"]["SCHEDULES"].exclusive), set(["macosx", "windows"])
        )

    def test_hook(self):
        extra = {
            "MOZ_BUILD_HOOK": mozpath.abspath(self.file_path("mozbuild.hook")),
        }
        config = self.config(
            "traversal-simple", extra_substs=extra, error_is_fatal=True
        )

        reader = BuildReader(config)

        contexts = {ctx.relsrcdir: ctx for ctx in reader.read_topsrcdir()}

        self.assertEqual(len(contexts), 4)
        self.assertEqual(contexts[""]["DEFINES"], {"ALL": True})
        self.assertEqual(
            contexts["foo"]["DEFINES"], {"ALL": True, "FOO": True, "FOO_ONLY": True}
        )
        self.assertEqual(contexts["foo/biz"]["DEFINES"], {"ALL": True, "FOO": True})
        self.assertEqual(contexts["bar"]["DEFINES"], {"ALL": True, "BAR_ONLY": True})

    def test_source_headers(self):
        reader = self.reader("reader-source-headers")
        contexts = list(reader.read_topsrcdir())
        self.assertEqual(len(contexts), 1)

        source_headers = contexts[0]["SOURCE_HEADERS"]
        self.assertEqual(source_headers, ["header.h", "exported.h", "installed.h"])

        reader = self.reader("reader-source-headers-duplicate")
        with self.assertRaises(BuildReaderError) as e:
            contexts = list(reader.read_topsrcdir())
        self.assertIn(
            "SOURCE_HEADERS lists header.h which is already listed in EXPORTS",
            str(e.exception),
        )


if __name__ == "__main__":
    main()
