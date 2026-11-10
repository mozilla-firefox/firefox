# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

from buildconfig import topsrcdir
from mozpack import path as mozpath
from mozunit import MockedOpen, main
from test_toolchain_helpers import CompilerResult

from common import BaseConfigureTest
from mozbuild.configure.options import InvalidOptionError
from mozbuild.configure.util import Version


class TestToolkitMozConfigure(BaseConfigureTest):
    def test_moz_configure_options(self):
        def get_value_for(args=[], environ={}, mozconfig=""):
            sandbox = self.get_sandbox({}, {}, args, environ, mozconfig)

            # Add a fake old-configure option
            sandbox.option_impl(
                "--with-foo", nargs="*", help="Help missing for old configure options"
            )

            # Remove all implied options, otherwise, getting
            # all_configure_options below triggers them, and that triggers
            # configure parts that aren't expected to run during this test.
            del sandbox._implied_options[:]
            result = sandbox._value_for(sandbox["all_configure_options"])
            shell = mozpath.abspath("/bin/sh")
            return result.replace("CONFIG_SHELL=%s " % shell, "")

        self.assertEqual(
            "--enable-application=browser",
            get_value_for(["--enable-application=browser"]),
        )

        self.assertEqual(
            "--enable-application=browser MOZ_VTUNE=1",
            get_value_for(["--enable-application=browser", "MOZ_VTUNE=1"]),
        )

        value = get_value_for(
            environ={"MOZ_VTUNE": "1"},
            mozconfig="ac_add_options --enable-application=browser",
        )

        self.assertEqual("--enable-application=browser MOZ_VTUNE=1", value)

        # --disable-js-shell is the default, so it's filtered out.
        self.assertEqual(
            "--enable-application=browser",
            get_value_for(["--enable-application=browser", "--disable-js-shell"]),
        )

        # Normally, --without-foo would be filtered out because that's the
        # default, but since it is a (fake) old-configure option, it always
        # appears.
        self.assertEqual(
            "--enable-application=browser --without-foo",
            get_value_for(["--enable-application=browser", "--without-foo"]),
        )
        self.assertEqual(
            "--enable-application=browser --with-foo",
            get_value_for(["--enable-application=browser", "--with-foo"]),
        )

        self.assertEqual(
            "--enable-application=browser '--with-foo=foo bar'",
            get_value_for(["--enable-application=browser", "--with-foo=foo bar"]),
        )

    def test_developer_options(self, milestone="42.0a1"):
        def get_value(args=[], environ={}):
            sandbox = self.get_sandbox({}, {}, args, environ)
            return sandbox._value_for(sandbox["developer_options"])

        milestone_path = os.path.join(topsrcdir, "config", "milestone.txt")
        with MockedOpen({milestone_path: milestone}):
            # developer options are enabled by default on "nightly" milestone
            # only
            self.assertEqual(get_value(), "a" in milestone or None)

            self.assertEqual(get_value(["--enable-release"]), None)

            self.assertEqual(get_value(environ={"MOZILLA_OFFICIAL": 1}), None)

            self.assertEqual(
                get_value(["--enable-release"], environ={"MOZILLA_OFFICIAL": 1}), None
            )

            with self.assertRaises(InvalidOptionError):
                get_value(["--disable-release"], environ={"MOZILLA_OFFICIAL": 1})

            self.assertEqual(get_value(environ={"MOZ_AUTOMATION": 1}), None)

    def test_developer_options_release(self):
        self.test_developer_options("42.0")

    def get_milestone(self, args=[]):
        sandbox = self.get_sandbox({}, {}, args, {})
        return sandbox._value_for(sandbox["milestone"])

    def test_early_beta_or_earlier(self, milestone="42.0a1"):
        milestone_path = os.path.join(topsrcdir, "config", "milestone.txt")
        with MockedOpen({milestone_path: milestone}):
            m = self.get_milestone()
            self.assertIn(m.is_early_beta_or_earlier, (True, None))
            self.assertEqual(m.is_early_beta_or_earlier, m.is_nightly)

    def test_early_beta_or_earlier_beta(self):
        self.test_early_beta_or_earlier("42.0b1")

    def test_early_beta_or_earlier_release(self):
        self.test_early_beta_or_earlier("42.0")

    def test_as_milestone_never_enables_early_beta_or_earlier(self):
        milestone_path = os.path.join(topsrcdir, "config", "milestone.txt")
        with MockedOpen({milestone_path: "42.0a1"}):
            for as_milestone in ("beta", "early-beta", "late-beta", "release"):
                m = self.get_milestone([f"--as-milestone={as_milestone}"])
                self.assertIsNone(m.is_early_beta_or_earlier, as_milestone)

    def test_wasm_flags(self):
        def get_values(environ={}):
            sandbox = self.get_sandbox({}, {}, [], environ)
            return (
                sandbox._value_for(sandbox["wasm_cflags"]),
                sandbox._value_for(sandbox["wasm_cxxflags"]),
            )

        defaults = ["-fno-exceptions", "-fno-strict-aliasing"]
        self.assertEqual(get_values(), (defaults, defaults))
        self.assertEqual(
            get_values({"WASM_CFLAGS": "-fcolor-diagnostics -DFOO"}),
            (defaults + ["-fcolor-diagnostics", "-DFOO"], defaults),
        )
        self.assertEqual(
            get_values({"WASM_CXXFLAGS": "-fcolor-diagnostics '-DBAR=a b'"}),
            (defaults, defaults + ["-fcolor-diagnostics", "-DBAR=a b"]),
        )

    def test_wasm_cxx_clang_plugin(self):
        sandbox = self.get_sandbox(
            {}, {}, ["--target=x86_64-pc-windows-msvc", "--enable-clang-plugin"], {}
        )
        wasm_cxx = ["/usr/bin/clang++.EXE", "--target=wasm32"]
        sandbox._dependency_overrides[
            sandbox._depends[sandbox["wasm_cxx_with_flags"]]
        ] = wasm_cxx
        self.assertEqual(
            (sandbox.get_config("WASM_CXX"), sandbox.get_config("WASM_CXX_LINK")),
            (["/usr/bin/clang.exe", "--driver-mode=g++", "--target=wasm32"], wasm_cxx),
        )

    def test_elfhack(self):
        class ReadElf:
            def __init__(self, with_relr):
                self.with_relr = with_relr

            def __call__(self, stdin, args):
                assert len(args) == 2 and args[0] == "-d"
                if self.with_relr:
                    return 0, " 0x0000023 (DT_RELR)     0x1000\n", ""
                return 0, "", ""

        class MockCC:
            def __init__(self, have_pack_relative_relocs, have_arc4random):
                self.have_pack_relative_relocs = have_pack_relative_relocs
                self.have_arc4random = have_arc4random

            def __call__(self, stdin, args):
                if args == ["--version"]:
                    return 0, "mockcc", ""
                if "-Wl,--version" in args:
                    assert len(args) <= 2
                    if len(args) == 1 or args[0] == "-fuse-ld=bfd":
                        return 0, "GNU ld", ""
                    if args[0] == "-fuse-ld=lld":
                        return 0, "LLD", ""
                    if args[0] == "-fuse-ld=gold":
                        return 0, "GNU gold", ""
                elif "-Wl,-z,pack-relative-relocs" in args:
                    if self.have_pack_relative_relocs:
                        return 0, "", ""
                    else:
                        return 1, "", "Unknown flag"

                # Assume the first argument is an input file.
                with open(args[0]) as fh:
                    if "arc4random()" in fh.read():
                        if self.have_arc4random:
                            return 0, "", ""
                        else:
                            return 1, "", "Undefined symbol"
                assert False

        def get_values(mockcc, readelf, args=[]):
            sandbox = self.get_sandbox(
                {
                    "/usr/bin/mockcc": mockcc,
                    "/usr/bin/readelf": readelf,
                },
                {},
                ["--disable-bootstrap", "--disable-release"] + args,
            )
            # Trick the sandbox into not running too much
            dep = sandbox._depends[sandbox["c_compiler"]]
            sandbox._dependency_overrides[dep] = CompilerResult(
                compiler="/usr/bin/mockcc",
                language="C",
                type="clang",
                version=Version("16.0"),
                flags=[],
            )
            dep = sandbox._depends[sandbox["readelf"]]
            sandbox._dependency_overrides[dep] = "/usr/bin/readelf"

            return (
                sandbox._value_for(sandbox["select_linker"]).KIND,
                sandbox._value_for(sandbox["pack_relative_relocs_flags"]),
                sandbox._value_for(sandbox["which_elf_hack"]),
            )

        PACK = ["-Wl,-z,pack-relative-relocs"]
        # The typical case with a bootstrap build: linker supports pack-relative-relocs,
        # but glibc is old and doesn't.
        mockcc = MockCC(True, False)
        readelf = ReadElf(True)
        self.assertEqual(get_values(mockcc, readelf), ("lld", None, "relr"))
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-release"]), ("lld", None, "relr")
        )
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-elf-hack"]), ("lld", None, "relr")
        )
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-elf-hack=relr"]),
            ("lld", None, "relr"),
        )
        # LLD is picked by default and enabling elfhack fails because of that.
        with self.assertRaises(SystemExit):
            get_values(mockcc, readelf, ["--enable-elf-hack=legacy"])
        # If we force to use BFD ld, it works.
        self.assertEqual(
            get_values(
                mockcc, readelf, ["--enable-elf-hack=legacy", "--enable-linker=bfd"]
            ),
            ("bfd", None, "legacy"),
        )

        for mockcc, readelf in (
            # Linker doesn't support pack-relative-relocs. Glibc is old.
            (MockCC(False, False), ReadElf(False)),
            # Linker doesn't support pack-relative-relocs. Glibc is new.
            (MockCC(False, True), ReadElf(False)),
            # Linker doesn't error out for unknown flags. Glibc is old.
            (MockCC(True, False), ReadElf(False)),
            # Linker doesn't error out for unknown flags. Glibc is new.
            (MockCC(True, True), ReadElf(False)),
        ):
            self.assertEqual(get_values(mockcc, readelf), ("lld", None, None))
            self.assertEqual(
                get_values(mockcc, readelf, ["--enable-release"]),
                ("lld", None, None),
            )
            with self.assertRaises(SystemExit):
                get_values(mockcc, readelf, ["--enable-elf-hack"])
            with self.assertRaises(SystemExit):
                get_values(mockcc, readelf, ["--enable-elf-hack=relr"])
            # LLD is picked by default and enabling elfhack fails because of that.
            with self.assertRaises(SystemExit):
                get_values(mockcc, readelf, ["--enable-elf-hack=legacy"])
            # If we force to use BFD ld, it works.
            self.assertEqual(
                get_values(
                    mockcc, readelf, ["--enable-elf-hack", "--enable-linker=bfd"]
                ),
                ("bfd", None, "legacy"),
            )
            self.assertEqual(
                get_values(
                    mockcc, readelf, ["--enable-elf-hack=legacy", "--enable-linker=bfd"]
                ),
                ("bfd", None, "legacy"),
            )

        # Linker supports pack-relative-relocs, and glibc too. We use pack-relative-relocs
        # unless elfhack is explicitly enabled.
        mockcc = MockCC(True, True)
        readelf = ReadElf(True)
        self.assertEqual(get_values(mockcc, readelf), ("lld", PACK, None))
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-release"]), ("lld", PACK, None)
        )
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-elf-hack"]),
            ("lld", None, "relr"),
        )
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-elf-hack=relr"]),
            ("lld", None, "relr"),
        )
        # LLD is picked by default and enabling elfhack fails because of that.
        with self.assertRaises(SystemExit):
            get_values(mockcc, readelf, ["--enable-elf-hack=legacy"])
        # If we force to use BFD ld, it works.
        self.assertEqual(
            get_values(mockcc, readelf, ["--enable-elf-hack", "--enable-linker=bfd"]),
            ("bfd", None, "relr"),
        )
        self.assertEqual(
            get_values(
                mockcc, readelf, ["--enable-elf-hack=legacy", "--enable-linker=bfd"]
            ),
            ("bfd", None, "legacy"),
        )


if __name__ == "__main__":
    main()
