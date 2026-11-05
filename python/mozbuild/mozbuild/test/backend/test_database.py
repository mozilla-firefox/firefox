# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os

import mozpack.path as mozpath
from mozunit import main

from mozbuild.backend.clangd import ClangdBackend
from mozbuild.backend.static_analysis import StaticAnalysisBackend
from mozbuild.compilation.database import CompileDBBackend
from mozbuild.test.backend.common import BackendTester


class TestCompileDBBackends(BackendTester):
    def perform_check(self, compile_commands_path, topsrcdir, topobjdir):
        self.assertTrue(os.path.exists(compile_commands_path))
        compile_db = json.loads(open(compile_commands_path).read())

        # Verify that we have the same number of items
        self.assertEqual(len(compile_db), 5)

        expected_db = [
            {
                "directory": topobjdir,
                "arguments": [
                    "clang",
                    "-o",
                    "/dev/null",
                    "-c",
                    "-ferror-limit=0",
                    f"{topsrcdir}/bar.c",
                ],
                "file": f"{topsrcdir}/bar.c",
            },
            {
                "directory": topobjdir,
                "arguments": [
                    "clang",
                    "-o",
                    "/dev/null",
                    "-c",
                    "-ferror-limit=0",
                    f"{topsrcdir}/foo.c",
                ],
                "file": f"{topsrcdir}/foo.c",
            },
            {
                "directory": topobjdir,
                "arguments": [
                    "clang++",
                    "-o",
                    "/dev/null",
                    "-c",
                    "-ferror-limit=0",
                    f"{topsrcdir}/baz.cpp",
                ],
                "file": f"{topsrcdir}/baz.cpp",
            },
            {
                "directory": topobjdir,
                "arguments": [
                    "clang++",
                    "-o",
                    "/dev/null",
                    "-c",
                    "-ferror-limit=0",
                    f"{topsrcdir}/qux.cpp",
                ],
                "file": f"{topsrcdir}/qux.cpp",
            },
            {
                "directory": topobjdir,
                "arguments": [
                    "clang++",
                    "-o",
                    "/dev/null",
                    "-c",
                    "-xc++",
                    "-ferror-limit=0",
                    f"{topsrcdir}/foo.h",
                ],
                "file": f"{topsrcdir}/foo.h",
            },
        ]

        # Verify item consistency against `expected_db`
        self.assertCountEqual(compile_db, expected_db)

    def test_database(self):
        """Ensure we can generate a `compile_commands.json` and that is correct."""

        env = self._consume("database", CompileDBBackend)
        compile_commands_path = os.path.join(env.topobjdir, "compile_commands.json")

        self.perform_check(compile_commands_path, env.topsrcdir, env.topobjdir)

    def test_database_quoted_defines(self):
        """Ensure string-valued defines are emitted as single, unquoted
        arguments, so that tools don't need to parse shell quoting."""

        env = self._consume("database-defines", CompileDBBackend)
        compile_commands_path = os.path.join(env.topobjdir, "compile_commands.json")

        with open(compile_commands_path) as fh:
            compile_db = json.load(fh)

        self.assertEqual(len(compile_db), 1)
        defines = [a for a in compile_db[0]["arguments"] if a.startswith("-D")]
        self.assertEqual(
            defines,
            ['-DMOZ_APP_NAME="firefox"', '-DMOZ_APP_DISPLAYNAME="Mozilla Firefox"'],
        )

    def test_database_objc(self):
        env = self._consume("database-objc", CompileDBBackend)
        with open(os.path.join(env.topobjdir, "compile_commands.json")) as fh:
            compile_db = json.load(fh)

        flags = [
            f"-I{env.topsrcdir}",
            f"-I{env.topobjdir}",
            f"-I{env.topobjdir}/dist/include",
            "-fno-strict-aliasing",
            "-ffp-contract=off",
        ]
        self.assertCountEqual(
            compile_db,
            [
                {
                    "directory": env.topobjdir,
                    "arguments": [
                        "clang",
                        "-o",
                        "/dev/null",
                        "-c",
                        *flags,
                        "-fobjc-exceptions",
                        "-fobjc-arc",
                        "-ferror-limit=0",
                        f"{env.topsrcdir}/baz.m",
                    ],
                    "file": f"{env.topsrcdir}/baz.m",
                },
                {
                    "directory": env.topobjdir,
                    "arguments": [
                        "clang++",
                        "-o",
                        "/dev/null",
                        "-c",
                        *flags,
                        "-fobjc-call-cxx-cdtors",
                        "-fobjc-weak",
                        "-ferror-limit=0",
                        f"{env.topsrcdir}/qux.mm",
                    ],
                    "file": f"{env.topsrcdir}/qux.mm",
                },
                {
                    "directory": env.topobjdir,
                    "arguments": [
                        "clang++",
                        "-o",
                        "/dev/null",
                        "-c",
                        *flags,
                        "-fobjc-call-cxx-cdtors",
                        "-fobjc-weak",
                        "-xobjective-c++",
                        "-ferror-limit=0",
                        f"{env.topsrcdir}/qux.h",
                    ],
                    "file": f"{env.topsrcdir}/qux.h",
                },
            ],
        )

    def test_database_c_header(self):
        env = self._consume("database-c", CompileDBBackend)
        with open(os.path.join(env.topobjdir, "compile_commands.json")) as fh:
            compile_db = {mozpath.basename(e["file"]): e for e in json.load(fh)}

        self.assertEqual(
            compile_db["foo.h"]["arguments"],
            compile_db["foo.c"]["arguments"][:-2]
            + ["-xc", "-ferror-limit=0", f"{env.topsrcdir}/foo.h"],
        )

    def test_clangd(self):
        """Ensure we can generate a `compile_commands.json` and that is correct.
        in order to be used by ClandBackend"""

        env = self._consume("database", ClangdBackend)
        compile_commands_path = os.path.join(
            env.topobjdir, "clangd", "compile_commands.json"
        )

        self.perform_check(compile_commands_path, env.topsrcdir, env.topobjdir)

    def test_clangd_strips_compiler_wrapper(self):
        """Ensure a compiler wrapper (ccache and/or --with-compiler-wrapper) is
        stripped from the clangd `compile_commands.json`. clangd treats the
        leading token as the compiler driver, so a wrapper prefix makes it
        misparse the rest of the command."""

        env = self._get_environment("database-compiler-wrapper", srcdir_name="database")
        env = self._consume("database", ClangdBackend, env=env)
        compile_commands_path = os.path.join(
            env.topobjdir, "clangd", "compile_commands.json"
        )

        # With the wrapper stripped, the output matches a wrapper-less build.
        self.perform_check(compile_commands_path, env.topsrcdir, env.topobjdir)

    def test_clangd_strips_ccache(self):
        """Ensure a ccache wrapper is also stripped from the clangd
        `compile_commands.json`. ccache is folded into COMPILER_WRAPPER by
        configure, so the same prefix stripping covers it."""

        env = self._get_environment("database-ccache", srcdir_name="database")
        env = self._consume("database", ClangdBackend, env=env)
        compile_commands_path = os.path.join(
            env.topobjdir, "clangd", "compile_commands.json"
        )

        # With the wrapper stripped, the output matches a wrapper-less build.
        self.perform_check(compile_commands_path, env.topsrcdir, env.topobjdir)

    def test_static_analysis(self):
        """Ensure we can generate a `compile_commands.json` and that is correct.
        in order to be used by StaticAnalysisBackend"""

        env = self._consume("database", StaticAnalysisBackend)
        compile_commands_path = os.path.join(
            env.topobjdir, "static-analysis", "compile_commands.json"
        )

        self.perform_check(compile_commands_path, env.topsrcdir, env.topobjdir)


if __name__ == "__main__":
    main()
