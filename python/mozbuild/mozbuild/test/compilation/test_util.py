# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import tempfile
import unittest
from unittest import mock

from mozunit import main

from mozbuild.base import PathArgument
from mozbuild.compilation.util import compile_flags_for, find_compile_db


def entry(directory, file, flags, source=None, extra=""):
    return {
        "directory": directory,
        "arguments": [
            "ccache",
            "clang++",
            "-std=c++20",
            "-o",
            "/dev/null",
            "-c",
            *flags.split(),
            "-ferror-limit=0",
            source or file,
            *extra.split(),
        ],
        "file": file,
    }


ENTRIES = [
    entry("/obj/a", "/src/a/one.cpp", "-DONE -Xclang -load -Xclang plugin.so"),
    entry("/obj/a", "/src/a/two.c", "-DTWO"),
    entry("/obj/a", "/src/a/three.cpp", "-DTHREE", extra="-DPER_SOURCE"),
    entry("/obj/b", "/src/b/four.cpp", "-DFOUR", source="Unified_cpp_b0.cpp"),
    entry("/obj/c", "/src/c/five.cpp", "-DFIVE", extra="-DFIVE_ONLY"),
    entry("/obj/a", "/src/a/a.mm", "-DOBJCXX"),
    entry("/obj/d", "/src/d/six.mm", "-DSIX"),
    entry("/obj/d", "/src/d/seven.m", "-DSEVEN"),
    entry("/obj/a", "/src/a/listed.h", "-DLISTED -xc++"),
    entry("/obj/e", "/src/e/eight.c", "-DEIGHT"),
    entry("/obj/e", "/src/e/nine.m", "-DNINE"),
]


class TestCompileFlagsFor(unittest.TestCase):
    def test_compiled_file(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/one.cpp"), ["-std=c++20", "-DONE"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/two.c"), ["-std=c++20", "-DTWO"]
        )

    def test_header_entry(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/listed.h"),
            ["-std=c++20", "-DLISTED", "-xc++"],
        )

    def test_unified_source(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/b/four.cpp"), ["-std=c++20", "-DFOUR"]
        )

    def test_header_per_source_flags(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/c/five.h"), ["-std=c++20", "-DFIVE"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/c/five.cpp"),
            ["-std=c++20", "-DFIVE", "-DFIVE_ONLY"],
        )

    def test_fallback_language(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/one.h"), ["-std=c++20", "-DTHREE"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/other.c"), ["-std=c++20", "-DTWO"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/other.m"), ["-std=c++20", "-DTWO"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/e/eight.h"), ["-std=c++20", "-DEIGHT"]
        )

    def test_fallback_objective_c(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/d/six.h"), ["-std=c++20", "-DSIX"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/d/other.c"), ["-std=c++20", "-DSEVEN"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/e/other.m"), ["-std=c++20", "-DNINE"]
        )
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/a/other.mm"), ["-std=c++20", "-DOBJCXX"]
        )

    def test_header_parent_directory(self):
        self.assertEqual(
            compile_flags_for(ENTRIES, "/src/b/sub/five.h"), ["-std=c++20", "-DFOUR"]
        )

    def test_no_compiled_file(self):
        self.assertIsNone(compile_flags_for(ENTRIES, "/elsewhere/six.h"))


class TestFindCompileDB(unittest.TestCase):
    def test_find_compile_db(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(find_compile_db(tmp))

            compile_db = os.path.join(tmp, "compile_commands.json")
            with open(compile_db, "w", encoding="utf-8") as fh:
                fh.write("[]")
            self.assertEqual(
                os.path.normpath(find_compile_db(tmp)), os.path.normpath(compile_db)
            )

            os.mkdir(os.path.join(tmp, "clangd"))
            clangd_db = os.path.join(tmp, "clangd", "compile_commands.json")
            with open(clangd_db, "w", encoding="utf-8") as fh:
                fh.write("[]")
            self.assertEqual(
                os.path.normpath(find_compile_db(tmp)), os.path.normpath(clangd_db)
            )


class TestCompileFlagsCommand(unittest.TestCase):
    def _run(self, what, entries=ENTRIES, cwd=None, generated=None):
        from mach.registrar import Registrar

        if "devenv" not in Registrar.categories:
            Registrar.register_category("devenv", "devenv", "devenv")
        from mozbuild.compilation import codecomplete

        with tempfile.TemporaryDirectory() as tmp:
            if generated:
                path = os.path.join(tmp, generated)
                os.makedirs(os.path.dirname(path))
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write("")
                entries = entries + [entry(tmp, path, "-DGENERATED")]
            if entries is not None:
                db = os.path.join(tmp, "compile_commands.json")
                with open(db, "w", encoding="utf-8") as fh:
                    json.dump(
                        [{**e, "file": os.path.abspath(e["file"])} for e in entries],
                        fh,
                    )
            command_context = mock.Mock(topobjdir=tmp)
            command_context._wrap_path_argument = lambda arg: PathArgument(
                arg, "/src", tmp, cwd=tmp if cwd is None else cwd
            )
            with mock.patch("builtins.print") as printed:
                rc = codecomplete.compileflags(command_context, what)
        return rc, printed

    def test_compileflags(self):
        rc, printed = self._run("/src/a/one.cpp")

        self.assertIsNone(rc)
        printed.assert_called_once_with("-std=c++20 -DONE")

    def test_compileflags_objdir_relative(self):
        rc, printed = self._run("a/one.cpp")

        self.assertIsNone(rc)
        printed.assert_called_once_with("-std=c++20 -DONE")

    def test_compileflags_srcdir_relative(self):
        rc, printed = self._run("a/one.cpp", cwd="/elsewhere")

        self.assertIsNone(rc)
        printed.assert_called_once_with("-std=c++20 -DONE")

    def test_compileflags_objdir_file(self):
        rc, printed = self._run("gen/generated.cpp", generated="gen/generated.cpp")

        self.assertIsNone(rc)
        printed.assert_called_once_with("-std=c++20 -DGENERATED")

    def test_compileflags_no_compiled_file(self):
        rc, printed = self._run("/elsewhere/six.h")

        self.assertEqual(rc, 1)
        printed.assert_called_once_with(
            "No file compiled at or above /elsewhere/six.h in the compilation database."
        )

    def test_compileflags_no_database(self):
        rc, printed = self._run("/src/a/one.cpp", entries=None)

        self.assertEqual(rc, 1)
        printed.assert_called_once_with(
            "No compilation database found. Please run |mach build| with no "
            "arguments, or |mach build-backend -b CompileDB|."
        )


if __name__ == "__main__":
    main()
