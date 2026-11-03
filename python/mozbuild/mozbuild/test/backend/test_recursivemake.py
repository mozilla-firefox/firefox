# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import pickle
import unittest

import mozpack.path as mozpath
from mozfile import json
from mozpack.copier import FileRegistry
from mozpack.manifests import InstallManifest
from mozunit import main

from mozbuild.backend import get_backend_class
from mozbuild.backend.recursivemake import RecursiveMakeBackend, RecursiveMakeTraversal
from mozbuild.backend.test_manifest import TestManifestBackend
from mozbuild.frontend.emitter import TreeMetadataEmitter
from mozbuild.frontend.reader import BuildReader
from mozbuild.test.backend.common import BackendTester


class TestRecursiveMakeTraversal(unittest.TestCase):
    def test_traversal(self):
        traversal = RecursiveMakeTraversal()
        traversal.add("", dirs=["A", "B", "C"])
        traversal.add("", dirs=["D"])
        traversal.add("A")
        traversal.add("B", dirs=["E", "F"])
        traversal.add("C", dirs=["G", "H"])
        traversal.add("D", dirs=["I", "K"])
        traversal.add("D", dirs=["J", "L"])
        traversal.add("E")
        traversal.add("F")
        traversal.add("G")
        traversal.add("H")
        traversal.add("I", dirs=["M", "N"])
        traversal.add("J", dirs=["O", "P"])
        traversal.add("K", dirs=["Q", "R"])
        traversal.add("L", dirs=["S"])
        traversal.add("M")
        traversal.add("N", dirs=["T"])
        traversal.add("O")
        traversal.add("P", dirs=["U"])
        traversal.add("Q")
        traversal.add("R", dirs=["V"])
        traversal.add("S", dirs=["W"])
        traversal.add("T")
        traversal.add("U")
        traversal.add("V")
        traversal.add("W", dirs=["X"])
        traversal.add("X")

        parallels = set(("G", "H", "I", "J", "O", "P", "Q", "R", "U"))

        def filter(current, subdirs):
            return (
                current,
                [d for d in subdirs.dirs if d in parallels],
                [d for d in subdirs.dirs if d not in parallels],
            )

        start, deps = traversal.compute_dependencies(filter)
        self.assertEqual(start, ("X",))
        self.maxDiff = None
        self.assertEqual(
            deps,
            {
                "A": ("",),
                "B": ("A",),
                "C": ("F",),
                "D": ("G", "H"),
                "E": ("B",),
                "F": ("E",),
                "G": ("C",),
                "H": ("C",),
                "I": ("D",),
                "J": ("D",),
                "K": ("T", "O", "U"),
                "L": ("Q", "V"),
                "M": ("I",),
                "N": ("M",),
                "O": ("J",),
                "P": ("J",),
                "Q": ("K",),
                "R": ("K",),
                "S": ("L",),
                "T": ("N",),
                "U": ("P",),
                "V": ("R",),
                "W": ("S",),
                "X": ("W",),
            },
        )

        self.assertEqual(
            list(traversal.traverse("", filter)),
            [
                "",
                "A",
                "B",
                "E",
                "F",
                "C",
                "G",
                "H",
                "D",
                "I",
                "M",
                "N",
                "T",
                "J",
                "O",
                "P",
                "U",
                "K",
                "Q",
                "R",
                "V",
                "L",
                "S",
                "W",
                "X",
            ],
        )

        self.assertEqual(list(traversal.traverse("C", filter)), ["C", "G", "H"])

    def test_traversal_2(self):
        traversal = RecursiveMakeTraversal()
        traversal.add("", dirs=["A", "B", "C"])
        traversal.add("A")
        traversal.add("B", dirs=["D", "E", "F"])
        traversal.add("C", dirs=["G", "H", "I"])
        traversal.add("D")
        traversal.add("E")
        traversal.add("F")
        traversal.add("G")
        traversal.add("H")
        traversal.add("I")

        start, deps = traversal.compute_dependencies()
        self.assertEqual(start, ("I",))
        self.assertEqual(
            deps,
            {
                "A": ("",),
                "B": ("A",),
                "C": ("F",),
                "D": ("B",),
                "E": ("D",),
                "F": ("E",),
                "G": ("C",),
                "H": ("G",),
                "I": ("H",),
            },
        )

    def test_traversal_filter(self):
        traversal = RecursiveMakeTraversal()
        traversal.add("", dirs=["A", "B", "C"])
        traversal.add("A")
        traversal.add("B", dirs=["D", "E", "F"])
        traversal.add("C", dirs=["G", "H", "I"])
        traversal.add("D")
        traversal.add("E")
        traversal.add("F")
        traversal.add("G")
        traversal.add("H")
        traversal.add("I")

        def filter(current, subdirs):
            if current == "B":
                current = None
            return current, [], subdirs.dirs

        start, deps = traversal.compute_dependencies(filter)
        self.assertEqual(start, ("I",))
        self.assertEqual(
            deps,
            {
                "A": ("",),
                "C": ("F",),
                "D": ("A",),
                "E": ("D",),
                "F": ("E",),
                "G": ("C",),
                "H": ("G",),
                "I": ("H",),
            },
        )

    def test_traversal_parallel(self):
        traversal = RecursiveMakeTraversal()
        traversal.add("", dirs=["A", "B", "C"])
        traversal.add("A")
        traversal.add("B", dirs=["D", "E", "F"])
        traversal.add("C", dirs=["G", "H", "I"])
        traversal.add("D")
        traversal.add("E")
        traversal.add("F")
        traversal.add("G")
        traversal.add("H")
        traversal.add("I")
        traversal.add("J")

        def filter(current, subdirs):
            return current, subdirs.dirs, []

        start, deps = traversal.compute_dependencies(filter)
        self.assertEqual(start, ("A", "D", "E", "F", "G", "H", "I", "J"))
        self.assertEqual(
            deps,
            {
                "A": ("",),
                "B": ("",),
                "C": ("",),
                "D": ("B",),
                "E": ("B",),
                "F": ("B",),
                "G": ("C",),
                "H": ("C",),
                "I": ("C",),
                "J": ("",),
            },
        )


class TestRecursiveMakeBackend(BackendTester):
    def test_basic(self):
        """Ensure the RecursiveMakeBackend works without error."""
        env = self._consume("stub0", RecursiveMakeBackend)
        self.assertTrue(
            os.path.exists(mozpath.join(env.topobjdir, "backend.RecursiveMakeBackend"))
        )
        self.assertTrue(
            os.path.exists(
                mozpath.join(env.topobjdir, "backend.RecursiveMakeBackend.in")
            )
        )

    def test_output_files(self):
        """Ensure proper files are generated."""
        env = self._consume("stub0", RecursiveMakeBackend)

        expected = ["", "dir1", "dir2"]

        for d in expected:
            out_makefile = mozpath.join(env.topobjdir, d, "Makefile")
            out_backend = mozpath.join(env.topobjdir, d, "backend.mk")

            self.assertTrue(os.path.exists(out_makefile))
            self.assertTrue(os.path.exists(out_backend))

    def test_makefile_conversion(self):
        """Ensure Makefile.in is converted properly."""
        env = self._consume("stub0", RecursiveMakeBackend)

        p = mozpath.join(env.topobjdir, "Makefile")

        lines = [l.strip() for l in open(p).readlines()[1:] if not l.startswith("#")]
        self.assertEqual(
            lines,
            [
                "DEPTH := .",
                "topobjdir := %s" % env.topobjdir,
                "topsrcdir := %s" % env.topsrcdir,
                "srcdir := %s" % env.topsrcdir,
                "srcdir_rel := %s" % mozpath.relpath(env.topsrcdir, env.topobjdir),
                "relativesrcdir := .",
                "include $(DEPTH)/config/autoconf.mk",
                "",
                "FOO := foo",
                "",
                "include $(topsrcdir)/config/recurse.mk",
            ],
        )

    def test_missing_makefile_in(self):
        """Ensure missing Makefile.in results in Makefile creation."""
        env = self._consume("stub0", RecursiveMakeBackend)

        p = mozpath.join(env.topobjdir, "dir2", "Makefile")
        self.assertTrue(os.path.exists(p))

        lines = [l.strip() for l in open(p).readlines()]
        self.assertEqual(len(lines), 10)

        self.assertTrue(lines[0].startswith("# THIS FILE WAS AUTOMATICALLY"))

    def test_backend_mk(self):
        """Ensure backend.mk file is written out properly."""
        env = self._consume("stub0", RecursiveMakeBackend)

        p = mozpath.join(env.topobjdir, "backend.mk")

        lines = [l.strip() for l in open(p).readlines()[2:]]
        self.assertEqual(lines, ["DIRS := dir1 dir2"])

        # Make env.substs writable to add ENABLE_TESTS
        env.substs = dict(env.substs)
        env.substs["ENABLE_TESTS"] = "1"
        self._consume("stub0", RecursiveMakeBackend, env=env)
        p = mozpath.join(env.topobjdir, "backend.mk")

        lines = [l.strip() for l in open(p).readlines()[2:]]
        self.assertEqual(lines, ["DIRS := dir1 dir2 dir3"])

    def test_mtime_no_change(self):
        """Ensure mtime is not updated if file content does not change."""

        env = self._consume("stub0", RecursiveMakeBackend)

        makefile_path = mozpath.join(env.topobjdir, "Makefile")
        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        makefile_mtime = os.path.getmtime(makefile_path)
        backend_mtime = os.path.getmtime(backend_path)

        reader = BuildReader(env)
        emitter = TreeMetadataEmitter(env)
        backend = RecursiveMakeBackend(env)
        backend.consume(emitter.emit(reader.read_topsrcdir()))

        self.assertEqual(os.path.getmtime(makefile_path), makefile_mtime)
        self.assertEqual(os.path.getmtime(backend_path), backend_mtime)

    def test_substitute_config_files(self):
        """Ensure substituted config files are produced."""
        env = self._consume("substitute_config_files", RecursiveMakeBackend)

        p = mozpath.join(env.topobjdir, "foo")
        self.assertTrue(os.path.exists(p))
        lines = [l.strip() for l in open(p).readlines()]
        self.assertEqual(lines, ["TEST = foo"])

    def test_install_substitute_config_files(self):
        """Ensure we recurse into the dirs that install substituted config files."""
        env = self._consume("install_substitute_config_files", RecursiveMakeBackend)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        lines = [l.strip() for l in open(root_deps_path).readlines()]

        # Make sure we actually recurse into the sub directory during export to
        # install the subst file.
        self.assertTrue(any(l == "recurse_export: sub/export" for l in lines))

    def test_variable_passthru(self):
        """Ensure variable passthru is written out correctly."""
        env = self._consume("variable_passthru", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = {
            "RCFILE": ["RCFILE := $(srcdir)/foo.rc"],
            "RCINCLUDE": ["RCINCLUDE := $(srcdir)/bar.rc"],
            "WIN32_EXE_LDFLAGS": ["WIN32_EXE_LDFLAGS += -subsystem:console"],
        }

        for var, val in expected.items():
            # print("test_variable_passthru[%s]" % (var))
            found = [str for str in lines if str.startswith(var)]
            self.assertEqual(found, val)

    def test_sources(self):
        """Ensure SOURCES, HOST_SOURCES and WASM_SOURCES are handled properly."""
        env = self._consume("sources", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = {
            "ASFILES": ["ASFILES += $(srcdir)/bar.s", "ASFILES += $(srcdir)/foo.asm"],
            "CMMSRCS": ["CMMSRCS += $(srcdir)/fuga.mm", "CMMSRCS += $(srcdir)/hoge.mm"],
            "CSRCS": ["CSRCS += $(srcdir)/baz.c", "CSRCS += $(srcdir)/qux.c"],
            "HOST_CPPSRCS": [
                "HOST_CPPSRCS += $(srcdir)/bar.cpp",
                "HOST_CPPSRCS += $(srcdir)/foo.cpp",
            ],
            "HOST_CSRCS": [
                "HOST_CSRCS += $(srcdir)/baz.c",
                "HOST_CSRCS += $(srcdir)/qux.c",
            ],
            "SSRCS": ["SSRCS += $(srcdir)/titi.S", "SSRCS += $(srcdir)/toto.S"],
            "WASM_CSRCS": ["WASM_CSRCS += $(srcdir)/baz.c"],
            "WASM_CPPSRCS": ["WASM_CPPSRCS += $(srcdir)/bar.cpp"],
        }

        for var, val in expected.items():
            found = [str for str in lines if str.startswith(var)]
            self.assertEqual(found, val)

    def test_exports(self):
        """Ensure EXPORTS is handled properly."""
        env = self._consume("exports", RecursiveMakeBackend)

        # EXPORTS files should appear in the dist_include install manifest.
        m = InstallManifest(
            path=mozpath.join(
                env.topobjdir, "_build_manifests", "install", "dist_include"
            )
        )
        self.assertEqual(len(m), 7)
        self.assertIn("foo.h", m)
        self.assertIn("mozilla/mozilla1.h", m)
        self.assertIn("mozilla/dom/dom2.h", m)

    def test_js_shell_archive(self):
        """Ensure JS_SHELL_ARCHIVE_FILES is handled properly."""
        env = self._consume("js-shell-archive", RecursiveMakeBackend)

        manifest_path = mozpath.join(env.topobjdir, "jsshell-archive.list")
        with open(manifest_path) as fh:
            manifest_lines = fh.read().splitlines()
        self.assertEqual(
            manifest_lines, ["js", "libmozglue.so", "llvm-symbolizer", "fuzz-tests"]
        )

        top_backend = open(mozpath.join(env.topobjdir, "backend.mk")).read()
        self.assertNotIn("jsshell-archive", top_backend)

    def test_macos_bundle(self):
        """Ensure MACOS_BUNDLES is handled properly."""
        env = self._consume("macos-bundle", RecursiveMakeBackend)

        spec_path = mozpath.join(env.topobjdir, "dist_bin_Test.app.bundle.json")
        with open(spec_path) as fh:
            spec = json.load(fh)
        self.assertEqual(spec["lproj"], "English.lproj")
        self.assertTrue(spec["bundle"].endswith("dist/bin/Test.app"))
        self.assertTrue(spec["skeleton"].endswith("macos-bundle/skeleton"))
        self.assertTrue(spec["info_plist"].endswith("Info.plist"))
        self.assertTrue(spec["strings"].endswith("InfoPlist.strings"))
        self.assertEqual(len(spec["binaries"]), 1)
        self.assertEqual(spec["binaries"][0][1], "test-bin")
        self.assertEqual(len(spec["extra_files"]), 1)
        self.assertEqual(spec["extra_files"][0][1], "Frameworks/Extra.framework/Extra")
        self.assertEqual(len(spec["copies"]), 1)
        self.assertTrue(spec["copies"][0].endswith("dist/bin/Test-copy.app"))

        with open(mozpath.join(env.topobjdir, "dist_Staged.app.bundle.json")) as fh:
            staged = json.load(fh)
        self.assertTrue(staged["stage"].endswith("dist/bin"))
        self.assertTrue(staged["macos_files"].endswith("macos-files.txt"))
        self.assertTrue(staged["macos_copy_files"].endswith("macos-copy.in"))
        self.assertEqual(
            staged["moves"],
            [["ChannelPrefs.framework", "Frameworks/ChannelPrefs.framework"]],
        )
        self.assertEqual(staged["pkginfo"], "APPLMOZB")

        # A bundle that shares a basename with another in the same directory
        # gets its own spec rather than clobbering it.
        with open(mozpath.join(env.topobjdir, "dist_other_Test.app.bundle.json")) as fh:
            other = json.load(fh)
        self.assertTrue(other["bundle"].endswith("dist/other/Test.app"))

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        backend = open(backend_path).read()
        self.assertIn("py_action,assemble_macos_bundle Test.app", backend)
        self.assertIn("dist_bin_Test.app.bundle.json", backend)
        self.assertIn("py_action,assemble_macos_bundle Staged.app", backend)
        self.assertIn("tools repackage::", backend)
        self.assertIn(".PHONY: repackage", backend)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        root_deps = [l.strip() for l in open(root_deps_path).readlines()]
        self.assertIn("libs: gen/pre-compile", root_deps)

    def test_generated_files(self):
        """Ensure GENERATED_FILES is handled properly."""
        env = self._consume("generated-files", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "PRE_COMPILE_TARGETS += $(MDDEPDIR)/bar.c.stub",
            "bar.c: $(MDDEPDIR)/bar.c.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/bar.c.pp",
            "$(MDDEPDIR)/bar.c.stub: %s/generate-bar.py" % env.topsrcdir,
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate bar.c,%s/generate-bar.py baz bar.c $(MDDEPDIR)/bar.c.pp $(MDDEPDIR)/bar.c.stub)"  # noqa
            % env.topsrcdir,
            "@$(TOUCH) $@",
            "",
            "EXPORT_TARGETS += $(MDDEPDIR)/foo.h.stub",
            "foo.h: $(MDDEPDIR)/foo.h.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/foo.h.pp",
            "$(MDDEPDIR)/foo.h.stub: %s/generate-foo.py $(srcdir)/foo-data"
            % (env.topsrcdir),
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate foo.h,%s/generate-foo.py main foo.h $(MDDEPDIR)/foo.h.pp $(MDDEPDIR)/foo.h.stub $(srcdir)/foo-data)"  # noqa
            % (env.topsrcdir),
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

    def test_generated_files_extra_deps(self):
        """Ensure GENERATED_FILES extra_deps are handled properly."""
        env = self._consume("generated-files-extra-deps", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "consumer", "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "PRE_COMPILE_TARGETS += $(MDDEPDIR)/consumer.c.stub",
            "consumer.c: $(MDDEPDIR)/consumer.c.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/consumer.c.pp",
            "$(MDDEPDIR)/consumer.c.stub: "
            f"{env.topsrcdir}/consumer/generate-consumer.py "
            "$(srcdir)/input-data "
            "$(srcdir)/source-extra "
            "$(DEPTH)/producer/producer.c",
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate consumer.c,"
            f"{env.topsrcdir}/consumer/generate-consumer.py main "
            "consumer.c $(MDDEPDIR)/consumer.c.pp "
            "$(MDDEPDIR)/consumer.c.stub $(srcdir)/input-data)",
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        lines = [l.strip() for l in open(root_deps_path).readlines()]
        self.assertIn("consumer/pre-compile: producer/pre-compile", lines)

    def test_generated_files_host_tool(self):
        env = self._consume("generated-files-host-tool", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "data", "backend.mk")
        with open(backend_path) as fh:
            lines = [l.strip() for l in fh.readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "PRE_COMPILE_TARGETS += $(MDDEPDIR)/big.dat.stub",
            "big.dat: $(MDDEPDIR)/big.dat.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/big.dat.pp",
            "$(MDDEPDIR)/big.dat.stub: "
            f"{env.topsrcdir}/data/convert.py "
            "$(srcdir)/little.dat "
            "$(DEPTH)/dist/host/bin/tool",
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate big.dat,"
            f"{env.topsrcdir}/data/convert.py main "
            "big.dat $(MDDEPDIR)/big.dat.pp "
            "$(MDDEPDIR)/big.dat.stub $(srcdir)/little.dat)",
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines[: len(expected)], expected)

        root_backend_path = mozpath.join(env.topobjdir, "backend.mk")
        with open(root_backend_path) as fh:
            self.assertNotIn("big.dat", fh.read())

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        with open(root_deps_path) as fh:
            lines = [l.strip() for l in fh.readlines()]
        self.assertIn("data/pre-compile: tool/host", lines)
        self.assertIn("data/target-objects: data/pre-compile", lines)

    def test_resolve_targets(self):
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets([
                "gtest",
                "recurse_gtest",
                "binaries",
            ]),
            ["recurse_gtest", "recurse_gtest", "binaries"],
        )
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets(["syms"]), ["recurse_syms"]
        )

        hybrid = get_backend_class("FasterMake+RecursiveMake")
        self.assertEqual(hybrid.resolve_targets(["gtest"]), ["recurse_gtest"])
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets(["analysis-prereqs"]),
            ["pre-export", "export", "pre-compile"],
        )
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets(["rust-prereqs"]),
            ["pre-export", "export"],
        )
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets(["rust-prereqs", "uniffi-target"]),
            ["pre-export", "export", "recurse_uniffi-target"],
        )
        self.assertEqual(
            RecursiveMakeBackend.resolve_targets(["rust-prereqs", "rusttests"]),
            ["pre-export", "export", "recurse_rusttests"],
        )

    def test_resolve_targets_sorted(self):
        targets = RecursiveMakeBackend.TARGETS
        self.assertEqual(list(targets), sorted(targets))

    def test_generated_files_force(self):
        """Ensure GENERATED_FILES with .force is handled properly."""
        env = self._consume("generated-files-force", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "PRE_COMPILE_TARGETS += $(MDDEPDIR)/bar.c.stub",
            "bar.c: $(MDDEPDIR)/bar.c.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/bar.c.pp",
            "$(MDDEPDIR)/bar.c.stub: %s/generate-bar.py FORCE" % env.topsrcdir,
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate bar.c,%s/generate-bar.py baz bar.c $(MDDEPDIR)/bar.c.pp $(MDDEPDIR)/bar.c.stub)"  # noqa
            % env.topsrcdir,
            "@$(TOUCH) $@",
            "",
            "PRE_COMPILE_TARGETS += $(MDDEPDIR)/foo.c.stub",
            "foo.c: $(MDDEPDIR)/foo.c.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/foo.c.pp",
            "$(MDDEPDIR)/foo.c.stub: %s/generate-foo.py $(srcdir)/foo-data"
            % (env.topsrcdir),
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate foo.c,%s/generate-foo.py main foo.c $(MDDEPDIR)/foo.c.pp $(MDDEPDIR)/foo.c.stub $(srcdir)/foo-data)"  # noqa
            % (env.topsrcdir),
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

    def test_localized_generated_files(self):
        """Ensure LOCALIZED_GENERATED_FILES is handled properly."""
        env = self._consume("localized-generated-files", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "MISC_TARGETS += $(MDDEPDIR)/foo.xyz.stub",
            "foo.xyz: $(MDDEPDIR)/foo.xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/foo.xyz.pp",
            f"$(MDDEPDIR)/foo.xyz.stub: {env.topsrcdir}/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input $(if $(IS_LANGUAGE_REPACK),FORCE)",  # noqa
            "$(REPORT_BUILD)",
            f"$(call py_action,file_generate foo.xyz,--locale=$(AB_CD) {env.topsrcdir}/generate-foo.py main foo.xyz $(MDDEPDIR)/foo.xyz.pp $(MDDEPDIR)/foo.xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input)",  # noqa
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

        # The output is generated where it was declared and installed from the
        # top level Makefile, so the install depends on it.
        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "install", "dist_bin")
        )
        reg = FileRegistry()
        m.populate_registry(reg)
        self.assertIn("foo.xyz", reg.paths())

        root_deps = mozpath.join(env.topobjdir, "root-deps.mk")
        rule = [
            l for l in open(root_deps).readlines() if l.startswith("install-dist/bin:")
        ]
        self.assertEqual(len(rule), 1)
        self.assertIn("$(DEPTH)/foo.xyz", rule[0])

    def test_a_localized_generated_output_is_reachable_from_the_top_level_makefile(
        self,
    ):
        """A localized install runs from the top level Makefile, so the rule
        producing what it installs cannot live only in the declaring directory.
        """
        env = self._consume("localized-generated-root", RecursiveMakeBackend)

        root = open(mozpath.join(env.topobjdir, "backend.mk")).read()
        self.assertIn("made.txt", root)

        directory = open(mozpath.join(env.topobjdir, "sub", "backend.mk")).read()
        self.assertNotIn("made.txt", directory)

        root_deps = mozpath.join(env.topobjdir, "root-deps.mk")
        rule = [
            l for l in open(root_deps).readlines() if l.startswith("install-dist/bin:")
        ]
        self.assertEqual(len(rule), 1)
        self.assertIn("$(DEPTH)/sub/made.txt", rule[0])

        self.assertIn("$(call MERGE_RELATIVE_FILE,input.txt,sub)", root)

    def test_localized_generated_files_force(self):
        """Ensure LOCALIZED_GENERATED_FILES with .force is handled properly."""
        env = self._consume("localized-generated-files-force", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "MISC_TARGETS += $(MDDEPDIR)/foo.xyz.stub",
            "foo.xyz: $(MDDEPDIR)/foo.xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/foo.xyz.pp",
            f"$(MDDEPDIR)/foo.xyz.stub: {env.topsrcdir}/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input $(if $(IS_LANGUAGE_REPACK),FORCE)",  # noqa
            "$(REPORT_BUILD)",
            f"$(call py_action,file_generate foo.xyz,--locale=$(AB_CD) {env.topsrcdir}/generate-foo.py main foo.xyz $(MDDEPDIR)/foo.xyz.pp $(MDDEPDIR)/foo.xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input)",  # noqa
            "@$(TOUCH) $@",
            "",
            "MISC_TARGETS += $(MDDEPDIR)/abc.xyz.stub",
            "abc.xyz: $(MDDEPDIR)/abc.xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/abc.xyz.pp",
            f"$(MDDEPDIR)/abc.xyz.stub: {env.topsrcdir}/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input FORCE",  # noqa
            "$(REPORT_BUILD)",
            f"$(call py_action,file_generate abc.xyz,--locale=$(AB_CD) {env.topsrcdir}/generate-foo.py main abc.xyz $(MDDEPDIR)/abc.xyz.pp $(MDDEPDIR)/abc.xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input)",  # noqa
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

    def test_localized_generated_files_AB_CD(self):
        """Ensure LOCALIZED_GENERATED_FILES is handled properly
        when {AB_CD} and {AB_rCD} are used."""
        env = self._consume("localized-generated-files-AB_CD", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "MISC_TARGETS += $(MDDEPDIR)/foo$(AB_CD).xyz.stub",
            "foo$(AB_CD).xyz: $(MDDEPDIR)/foo$(AB_CD).xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/foo$(AB_CD).xyz.pp",
            f"$(MDDEPDIR)/foo$(AB_CD).xyz.stub: {env.topsrcdir}/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input $(if $(IS_LANGUAGE_REPACK),FORCE)",  # noqa
            "$(REPORT_BUILD)",
            f"$(call py_action,file_generate foo$(AB_CD).xyz,--locale=$(AB_CD) {env.topsrcdir}/generate-foo.py main foo$(AB_CD).xyz $(MDDEPDIR)/foo$(AB_CD).xyz.pp $(MDDEPDIR)/foo$(AB_CD).xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,) $(srcdir)/non-localized-input)",  # noqa
            "@$(TOUCH) $@",
            "",
            "bar$(AB_rCD).xyz: $(MDDEPDIR)/bar$(AB_rCD).xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/bar$(AB_rCD).xyz.pp",
            "$(MDDEPDIR)/bar$(AB_rCD).xyz.stub: %s/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,inner/locales) $(srcdir)/non-localized-input $(if $(IS_LANGUAGE_REPACK),FORCE)"  # noqa
            % env.topsrcdir,
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate bar$(AB_rCD).xyz,--locale=$(AB_CD) %s/generate-foo.py main bar$(AB_rCD).xyz $(MDDEPDIR)/bar$(AB_rCD).xyz.pp $(MDDEPDIR)/bar$(AB_rCD).xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,inner/locales) $(srcdir)/non-localized-input)"  # noqa
            % env.topsrcdir,
            "@$(TOUCH) $@",
            "",
            "zot$(AB_rCD).xyz: $(MDDEPDIR)/zot$(AB_rCD).xyz.stub ;",
            "EXTRA_MDDEPEND_FILES += $(MDDEPDIR)/zot$(AB_rCD).xyz.pp",
            "$(MDDEPDIR)/zot$(AB_rCD).xyz.stub: %s/generate-foo.py $(call MERGE_RELATIVE_FILE,localized-input,locales) $(srcdir)/non-localized-input $(if $(IS_LANGUAGE_REPACK),FORCE)"  # noqa
            % env.topsrcdir,
            "$(REPORT_BUILD)",
            "$(call py_action,file_generate zot$(AB_rCD).xyz,--locale=$(AB_CD) %s/generate-foo.py main zot$(AB_rCD).xyz $(MDDEPDIR)/zot$(AB_rCD).xyz.pp $(MDDEPDIR)/zot$(AB_rCD).xyz.stub $(call MERGE_RELATIVE_FILE,localized-input,locales) $(srcdir)/non-localized-input)"  # noqa
            % env.topsrcdir,
            "@$(TOUCH) $@",
            "",
        ]

        self.maxDiff = None
        self.assertEqual(lines, expected)

    def test_exports_generated(self):
        """Ensure EXPORTS that are listed in GENERATED_FILES
        are handled properly."""
        env = self._consume("exports-generated", RecursiveMakeBackend)

        # EXPORTS files should appear in the dist_include install manifest.
        m = InstallManifest(
            path=mozpath.join(
                env.topobjdir, "_build_manifests", "install", "dist_include"
            )
        )
        self.assertEqual(len(m), 8)
        self.assertIn("foo.h", m)
        self.assertIn("mozilla/mozilla1.h", m)
        self.assertIn("mozilla/dom/dom1.h", m)
        self.assertIn("gfx/gfx.h", m)
        self.assertIn("bar.h", m)
        self.assertIn("mozilla/mozilla2.h", m)
        self.assertIn("mozilla/dom/dom2.h", m)
        self.assertIn("mozilla/dom/dom3.h", m)
        # EXPORTS files that are also GENERATED_FILES should be handled as
        # INSTALL_TARGETS.
        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]
        expected = [
            "include $(topsrcdir)/config/AB_rCD.mk",
            "dist_include_FILES += bar.h",
            "dist_include_DEST := $(DEPTH)/dist/include/",
            "dist_include_TARGET := export",
            "INSTALL_TARGETS += dist_include",
            "dist_include_mozilla_FILES += mozilla2.h",
            "dist_include_mozilla_DEST := $(DEPTH)/dist/include/mozilla",
            "dist_include_mozilla_TARGET := export",
            "INSTALL_TARGETS += dist_include_mozilla",
            "dist_include_mozilla_dom_FILES += dom2.h",
            "dist_include_mozilla_dom_FILES += dom3.h",
            "dist_include_mozilla_dom_DEST := $(DEPTH)/dist/include/mozilla/dom",
            "dist_include_mozilla_dom_TARGET := export",
            "INSTALL_TARGETS += dist_include_mozilla_dom",
        ]
        self.maxDiff = None
        self.assertEqual(lines, expected)

    def test_objdir_files_rename(self):
        """OBJDIR_FILES entries install through one manifest for the directory
        and tier, renaming when a (source, target_basename) tuple is given."""
        env = self._consume("objdir-files-rename", RecursiveMakeBackend)

        manifest = "$(topobjdir)/_build_manifests/built/misc"
        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = open(backend_path).read().splitlines()
        self.assertEqual(lines.count(f"misc:: {manifest}.track"), 1)
        self.assertEqual(
            lines.count(
                f"\t$(call py_action,process_install_manifest misc,"
                f"--track {manifest}.track $(topobjdir) {manifest})"
            ),
            1,
        )
        self.assertEqual(lines.count("\t@$(TOUCH) $@"), 1)
        self.assertEqual(
            [l for l in lines if l.startswith(f"{manifest}.track:")],
            [
                f"{manifest}.track: {manifest}",
                f"{manifest}.track: bar",
                f"{manifest}.track: baz",
            ],
        )

        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "built", "misc")
        )
        registry = FileRegistry()
        m.populate_registry(registry)
        self.assertEqual(
            {dest: f.path for dest, f in registry},
            {
                "_tests/foo/renamed": mozpath.join(env.topobjdir, "bar"),
                "_tests/foo/baz": mozpath.join(env.topobjdir, "baz"),
            },
        )

    def test_resources(self):
        """Ensure RESOURCE_FILES is handled properly."""
        env = self._consume("resources", RecursiveMakeBackend)

        # RESOURCE_FILES should appear in the dist_bin install manifest.
        m = InstallManifest(
            path=os.path.join(env.topobjdir, "_build_manifests", "install", "dist_bin")
        )
        self.assertEqual(len(m), 10)
        self.assertIn("res/foo.res", m)
        self.assertIn("res/fonts/font1.ttf", m)
        self.assertIn("res/fonts/desktop/desktop2.ttf", m)

        self.assertIn("res/bar.res.in", m)
        self.assertIn("res/tests/test.manifest", m)
        self.assertIn("res/tests/extra.manifest", m)

    def test_test_manifests_files_written(self):
        """Ensure test manifests get turned into files."""
        env = self._consume("test-manifests-written", RecursiveMakeBackend)

        tests_dir = mozpath.join(env.topobjdir, "_tests")
        m_master = mozpath.join(
            tests_dir, "testing", "mochitest", "tests", "mochitest.toml"
        )
        x_master = mozpath.join(tests_dir, "xpcshell", "xpcshell.toml")
        self.assertTrue(os.path.exists(m_master))
        self.assertTrue(os.path.exists(x_master))

        lines = [l.strip() for l in open(x_master).readlines()]
        self.assertEqual(
            lines,
            [
                "# THIS FILE WAS AUTOMATICALLY GENERATED. DO NOT MODIFY BY HAND.",
                "",
                '["include:dir1/xpcshell.toml"]',
                '["include:xpcshell.toml"]',
            ],
        )

    def test_test_manifest_pattern_matches_recorded(self):
        """Pattern matches in test manifests' support-files should be recorded."""
        env = self._consume("test-manifests-written", RecursiveMakeBackend)
        m = InstallManifest(
            path=mozpath.join(
                env.topobjdir, "_build_manifests", "install", "_test_files"
            )
        )

        # This is not the most robust test in the world, but it gets the job
        # done.
        entries = [e for e in m._dests.keys() if "**" in e]
        self.assertEqual(len(entries), 1)
        self.assertIn("support/**", entries[0])

    def test_test_manifest_deffered_installs_written(self):
        """Shared support files are written to their own data file by the backend."""
        env = self._consume("test-manifest-shared-support", RecursiveMakeBackend)

        # First, read the generated for toml manifest contents.
        test_files_manifest = mozpath.join(
            env.topobjdir, "_build_manifests", "install", "_test_files"
        )
        m = InstallManifest(path=test_files_manifest)

        # Then, synthesize one from the test-installs.pkl file. This should
        # allow us to re-create a subset of the above.
        env = self._consume("test-manifest-shared-support", TestManifestBackend)
        test_installs_path = mozpath.join(env.topobjdir, "test-installs.pkl")

        with open(test_installs_path, "rb") as fh:
            test_installs = pickle.load(fh)

        self.assertEqual(
            set(test_installs.keys()),
            set(["child/test_sub.js", "child/data/**", "child/another-file.sjs"]),
        )
        for key in test_installs.keys():
            self.assertIn(key, test_installs)

        synthesized_manifest = InstallManifest()
        for item, installs in test_installs.items():
            for install_info in installs:
                if len(install_info) == 3:
                    synthesized_manifest.add_pattern_link(*install_info)
                if len(install_info) == 2:
                    synthesized_manifest.add_link(*install_info)

        self.assertEqual(len(synthesized_manifest), 3)
        for item, info in synthesized_manifest._dests.items():
            self.assertIn(item, m)
            self.assertEqual(info, m._dests[item])

    def test_xpidl_generation(self):
        """Ensure xpidl files and directories are written out."""
        env = self._consume("xpidl", RecursiveMakeBackend)

        # Install manifests should contain entries.
        install_dir = mozpath.join(env.topobjdir, "_build_manifests", "install")
        self.assertTrue(os.path.isfile(mozpath.join(install_dir, "xpidl")))

        m = InstallManifest(path=mozpath.join(install_dir, "xpidl"))
        self.assertIn(".deps/my_module.pp", m)

        m = InstallManifest(path=mozpath.join(install_dir, "xpidl"))
        self.assertIn("my_module.xpt", m)

        m = InstallManifest(path=mozpath.join(install_dir, "dist_include"))
        self.assertIn("foo.h", m)

        p = mozpath.join(env.topobjdir, "config/makefiles/xpidl")
        self.assertTrue(os.path.isdir(p))

        self.assertTrue(os.path.isfile(mozpath.join(p, "Makefile")))

        with open(mozpath.join(env.topobjdir, "root.mk")) as fh:
            export_dirs = [l for l in fh.readlines() if l.startswith("export_dirs")]
        self.assertEqual(export_dirs, ["export_dirs := config/makefiles/xpidl\n"])

        with open(mozpath.join(env.topobjdir, "backend.mk")) as fh:
            lines = fh.readlines()
        self.assertIn(
            "NONRECURSIVE_TARGETS_export_xpidl_DIRECTORY = "
            "$(DEPTH)/config/makefiles/xpidl\n",
            lines,
        )
        self.assertIn("NONRECURSIVE_TARGETS_export_xpidl_TARGETS += xpidl\n", lines)

    def test_xpidl_without_compile_environment(self):
        env = self._get_environment("xpidl-no-compile", srcdir_name="xpidl")
        self._consume("xpidl", RecursiveMakeBackend, env=env)

        install_dir = mozpath.join(env.topobjdir, "_build_manifests", "install")
        self.assertFalse(os.path.exists(mozpath.join(install_dir, "xpidl")))
        self.assertFalse(
            os.path.exists(mozpath.join(env.topobjdir, "config/makefiles/xpidl"))
        )

        with open(mozpath.join(env.topobjdir, "root.mk")) as fh:
            export_dirs = [l for l in fh.readlines() if l.startswith("export_dirs")]
        self.assertEqual(export_dirs, ["export_dirs := \n"])

    def test_webidl_build_writes_cppsrcs(self):
        """Ensure _handle_webidl_build writes the unified variable into
        webidlsrcs.mk and CPPSRCS lines into dom/bindings/backend.mk, so that
        dom/bindings/Makefile.in no longer needs to declare them."""
        env = self._get_environment("stub0")
        backend = RecursiveMakeBackend(env)

        bindings_dir = mozpath.join(env.topobjdir, "dom", "bindings")
        os.makedirs(bindings_dir, exist_ok=True)

        class FakeWebIDLCollection:
            srcdir = mozpath.join(env.topsrcdir, "dom", "bindings")
            objdir = bindings_dir
            topsrcdir = env.topsrcdir
            config = env

            def all_non_static_basenames(self):
                return ["GeneratedFooBinding.cpp"]

            def all_test_stems(self):
                return ["TestFoo"]

            def all_preprocessed_sources(self):
                return []

        unified_source_mapping = [
            ("UnifiedBindings0.cpp", ["FooBinding.cpp", "BarBinding.cpp"]),
        ]
        global_define_files = ["RegisterBindings.cpp"]

        backend._handle_webidl_build(
            bindings_dir,
            unified_source_mapping,
            FakeWebIDLCollection(),
            expected_build_output_files=[],
            global_define_files=global_define_files,
        )

        with open(mozpath.join(bindings_dir, "webidlsrcs.mk")) as fh:
            webidlsrcs_contents = fh.read()
        self.assertIn(
            "unified_binding_cpp_files := UnifiedBindings0.cpp",
            webidlsrcs_contents,
        )
        self.assertNotIn("CPPSRCS", webidlsrcs_contents)
        self.assertNotIn("globalgen_sources", webidlsrcs_contents)

        backend_file = backend._backend_files[bindings_dir]
        backend_mk_contents = backend_file.fh.getvalue().decode("utf-8")
        self.assertIn("CPPSRCS += RegisterBindings.cpp", backend_mk_contents)
        self.assertIn("CPPSRCS += $(unified_binding_cpp_files)", backend_mk_contents)

    def test_test_support_files_tracked(self):
        env = self._consume("test-support-binaries-tracked", RecursiveMakeBackend)
        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "install", "_tests")
        )
        self.assertEqual(len(m), 4)
        self.assertIn("xpcshell/tests/mozbuildtest/test-library.dll", m)
        self.assertIn("xpcshell/tests/mozbuildtest/test-one.exe", m)
        self.assertIn("xpcshell/tests/mozbuildtest/test-two.exe", m)
        self.assertIn("xpcshell/tests/mozbuildtest/host-test-library.dll", m)

    def test_old_install_manifest_deleted(self):
        # Simulate an install manifest from a previous backend version. Ensure
        # it is deleted.
        env = self._get_environment("stub0")
        purge_dir = mozpath.join(env.topobjdir, "_build_manifests", "install")
        manifest_path = mozpath.join(purge_dir, "old_manifest")
        os.makedirs(purge_dir)
        m = InstallManifest()
        m.write(path=manifest_path)
        with open(
            mozpath.join(env.topobjdir, "backend.RecursiveMakeBackend"), "w"
        ) as f:
            f.write("%s\n" % manifest_path)

        self.assertTrue(os.path.exists(manifest_path))
        self._consume("stub0", RecursiveMakeBackend, env)
        self.assertFalse(os.path.exists(manifest_path))

    def test_install_manifests_written(self):
        env, objs = self._emit("stub0")
        backend = RecursiveMakeBackend(env)

        m = InstallManifest()
        backend._install_manifests["testing"] = m
        m.add_link(__file__, "self")
        backend.consume(objs)

        man_dir = mozpath.join(env.topobjdir, "_build_manifests", "install")
        self.assertTrue(os.path.isdir(man_dir))

        expected = ["testing"]
        for e in expected:
            full = mozpath.join(man_dir, e)
            self.assertTrue(os.path.exists(full))

            m2 = InstallManifest(path=full)
            self.assertEqual(m, m2)

    def test_ipdl_sources(self):
        """Test that PREPROCESSED_IPDL_SOURCES and IPDL_SOURCES are written to
        ipdlsrcs.mk correctly."""
        env = self._get_environment("ipdl_sources")

        # Use the ipdl directory as the IPDL root for testing.
        ipdl_root = mozpath.join(env.topobjdir, "ipdl")

        # Make substs writable so we can set the value of IPDL_ROOT to reflect
        # the correct objdir.
        env.substs = dict(env.substs)
        env.substs["IPDL_ROOT"] = ipdl_root

        self._consume("ipdl_sources", RecursiveMakeBackend, env)

        manifest_path = mozpath.join(ipdl_root, "ipdlsrcs.mk")
        lines = [l.strip() for l in open(manifest_path).readlines()]

        # Handle Windows paths correctly
        topsrcdir = mozpath.normsep(env.topsrcdir)
        ipdlsrcs_file_path = mozpath.join(ipdl_root, "ipdlsrcs.txt")

        ipdlsrcs = ["bar1.ipdl", "foo1.ipdl"]
        ipdlsrcs.extend(
            "%s/%s" % (topsrcdir, path)
            for path in (
                "bar/bar.ipdl",
                "bar/bar2.ipdlh",
                "foo/foo.ipdl",
                "foo/foo2.ipdlh",
            )
        )

        self.maxDiff = None
        expected = [
            "ALL_IPDLSRCS := %s" % (" ".join(ipdlsrcs)),
            "ALL_IPDLSRCS_FILE := %s" % ipdlsrcs_file_path,
            "IPDLDIRS := %s %s/bar %s/foo" % (ipdl_root, topsrcdir, topsrcdir),
        ]

        found = [str for str in lines if str.startswith(("ALL_IPDLSRCS", "IPDLDIRS"))]
        self.assertEqual(found, expected)

        # Check the ipdlsrcs.txt file was written correctly.
        self.assertTrue(ipdlsrcs_file_path, "ipdlsrcs.txt was written")
        with open(ipdlsrcs_file_path) as f:
            ipdlsrcs_file_contents = f.read().splitlines()

        self.assertEqual(
            ipdlsrcs_file_contents,
            ipdlsrcs,
            "ipdlsrcs.txt contains all IPDL sources",
        )

        # Check that each directory declares the generated relevant .cpp files
        # to be built in CPPSRCS.
        # ENABLE_UNIFIED_BUILD defaults to False without mozilla-central's
        # moz.configure so we don't see unified sources here.
        for dir, expected in (
            (".", []),
            ("ipdl", []),
            (
                "bar",
                [
                    "CPPSRCS += "
                    + " ".join(
                        f"{ipdl_root}/{f}"
                        for f in [
                            "bar.cpp",
                            "bar1.cpp",
                            "bar1Child.cpp",
                            "bar1Parent.cpp",
                            "bar2.cpp",
                            "barChild.cpp",
                            "barParent.cpp",
                        ]
                    )
                ],
            ),
            (
                "foo",
                [
                    "CPPSRCS += "
                    + " ".join(
                        f"{ipdl_root}/{f}"
                        for f in [
                            "foo.cpp",
                            "foo1.cpp",
                            "foo1Child.cpp",
                            "foo1Parent.cpp",
                            "foo2.cpp",
                            "fooChild.cpp",
                            "fooParent.cpp",
                        ]
                    )
                ],
            ),
        ):
            backend_path = mozpath.join(env.topobjdir, dir, "backend.mk")
            lines = [l.strip() for l in open(backend_path).readlines()]

            found = [str for str in lines if str.startswith("CPPSRCS")]
            self.assertEqual(found, expected)

    def test_defines(self):
        """Test that DEFINES are written to backend.mk correctly."""
        env = self._consume("defines", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        var = "DEFINES"
        defines = [val for val in lines if val.startswith(var)]

        expected = ["DEFINES += -DFOO '-DBAZ=\"ab'\\''cd\"' -UQUX -DBAR=7 -DVALUE=xyz"]
        self.assertEqual(defines, expected)

    def test_local_includes(self):
        """Test that LOCAL_INCLUDES are written to backend.mk correctly."""
        env = self._consume("local_includes", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "LOCAL_INCLUDES += -I$(srcdir)/bar/baz",
            "LOCAL_INCLUDES += -I$(srcdir)/foo",
        ]

        found = [str for str in lines if str.startswith("LOCAL_INCLUDES")]
        self.assertEqual(found, expected)

    def test_objdir_local_includes(self):
        env = self._consume("objdir-local-includes", RecursiveMakeBackend)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        lines = [l.strip() for l in open(root_deps_path).readlines()]
        self.assertIn("consumer/target-objects: producer/pre-compile", lines)
        self.assertEqual(
            lines.count("producer/target-objects: producer/pre-compile"), 1
        )
        self.assertEqual(
            [l for l in lines if l.startswith("srcdir-only/target-objects:")], []
        )

    def test_generated_includes(self):
        """Test that GENERATED_INCLUDES are written to backend.mk correctly."""
        env = self._consume("generated_includes", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            "LOCAL_INCLUDES += -I$(CURDIR)/bar/baz",
            "LOCAL_INCLUDES += -I$(CURDIR)/foo",
        ]

        found = [str for str in lines if str.startswith("LOCAL_INCLUDES")]
        self.assertEqual(found, expected)

    def test_generated_file_depends_on_rust_archive(self):
        """Test a generated file depending on a non-default Rust archive."""
        env = self._consume("generated-file-rust-archive-dep", RecursiveMakeBackend)

        root_deps = [
            l.strip()
            for l in open(mozpath.join(env.topobjdir, "root-deps.mk")).readlines()
        ]
        self.assertIn("consumer/pre-compile: rust/test-category", root_deps)

        backend_path = mozpath.join(env.topobjdir, "consumer/backend.mk")
        self.assertIn(
            "$(DEPTH)/x86_64-unknown-linux-gnu/release/librandom_crate.a",
            open(backend_path).read(),
        )

    def test_rust_library(self):
        """Test that a Rust library is written to backend.mk correctly."""
        env = self._consume("rust-library", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "RUST_LIBRARY_FILE := x86_64-unknown-linux-gnu/release/libtest_library.a",
            "CARGO_FILE := $(srcdir)/Cargo.toml",
            "CARGO_TARGET_DIR := %s" % env.topobjdir,
        ]

        self.assertEqual(lines, expected)

    def test_rust_library_with_cargo_profile(self):
        """Test that a Rust library's Cargo profile is written to backend.mk."""
        env = self._consume("rust-library-cargo-profile", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "RUST_LIBRARY_FILE := "
            "x86_64-unknown-linux-gnu/release-custom/libprofile_library.a",
            "CARGO_FILE := $(srcdir)/Cargo.toml",
            f"CARGO_TARGET_DIR := {env.topobjdir}",
        ]

        self.assertEqual(lines, expected)

    def _cargo_spec(self, env, filename):
        with open(mozpath.join(env.topobjdir, filename)) as fh:
            return json.load(fh)

    def test_rust_library_command_spec(self):
        """The Cargo command spec is serialized next to the library."""
        env = self._consume("rust-library", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-library-spec.json")["edge"]

        self.assertEqual(edge["kind"], "library")
        self.assertEqual(edge["names"], ["libtest_library.a"])
        self.assertEqual(
            edge["outputs"], ["x86_64-unknown-linux-gnu/release/libtest_library.a"]
        )
        self.assertEqual(edge["cargo_profile_suffix"], "")
        self.assertEqual(edge["cargo_crate_type"], "")
        self.assertTrue(edge["manifest_path"].endswith("Cargo.toml"))
        self.assertEqual(edge["working_directory"], mozpath.normsep(env.topobjdir))
        self.assertIs(edge["lto"], True)

    def test_rust_library_no_lto_command_spec(self):
        """The LTO opt out reaches the Cargo command spec."""
        env = self._consume("rust-library-no-lto", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-library-spec.json")["edge"]
        self.assertIs(edge["lto"], False)

    def test_rust_command_spec_carries_configuration(self):
        """The spec is the action's only input, so it holds the configuration."""
        from mozbuild.rust_commands import CARGO_CONFIG_KEYS

        env = self._consume("rust-library", RecursiveMakeBackend)

        spec = self._cargo_spec(env, ".cargo-library-spec.json")

        self.assertEqual(spec["topsrcdir"], env.topsrcdir)
        self.assertEqual(spec["topobjdir"], env.topobjdir)
        self.assertEqual(
            spec["config"],
            {k: env.substs[k] for k in CARGO_CONFIG_KEYS if k in env.substs},
        )
        self.assertIn("RUST_TARGET", spec["config"])
        self.assertNotIn("LIB_SUFFIX", spec["config"])

    def test_rust_megazord_library_command_spec(self):
        """A library's Cargo profile and crate type reach its Cargo spec."""
        env = self._consume("rust-megazord-library", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-library-spec.json")["edge"]

        self.assertEqual(edge["kind"], "library")
        self.assertEqual(edge["cargo_profile_suffix"], "megazord")
        self.assertEqual(edge["cargo_crate_type"], "staticlib")

    def test_rust_megazord_tests_command_spec(self):
        """A test colocated with a library uses that library's Cargo profile."""
        env = self._consume("rust-megazord-library", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-tests-spec.json")["edge"]

        self.assertEqual(edge["kind"], "test")
        self.assertEqual(edge["cargo_profile_suffix"], "megazord")
        self.assertEqual(edge["cargo_crate_type"], "")
        self.assertEqual(edge["rustflags"], [])

    def test_rust_tests_command_spec(self):
        """An ordinary test edge carries its package names and no Cargo profile."""
        env = self._consume("rust-library-flags", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-tests-spec.json")["edge"]
        self.assertEqual(edge["kind"], "test")
        self.assertEqual(edge["names"], ["flags-tests"])
        self.assertEqual(edge["cargo_profile_suffix"], "")
        dist_bin = mozpath.join(env.topobjdir, "dist", "bin")
        self.assertEqual(edge["rustflags"], ["-C", f"link-arg=-Wl,-rpath,{dist_bin}"])

    def test_rust_tests_command_spec_clang_cl(self):
        env = self._get_environment(
            "rust-library-flags", substs={"OS_TARGET": "WINNT", "CC_TYPE": "clang-cl"}
        )
        env = self._consume("rust-library-flags", RecursiveMakeBackend, env=env)
        edge = self._cargo_spec(env, ".cargo-tests-spec.json")["edge"]
        self.assertEqual(edge["rustflags"], ["-C", "link-arg=-FORCE:UNRESOLVED"])

    def test_rust_tests_command_spec_mingw(self):
        env = self._get_environment(
            "rust-library-flags", substs={"OS_TARGET": "WINNT", "CC_TYPE": "clang"}
        )
        env = self._consume("rust-library-flags", RecursiveMakeBackend, env=env)
        edge = self._cargo_spec(env, ".cargo-tests-spec.json")["edge"]
        self.assertEqual(edge["rustflags"], [])

    def test_rust_tests_index(self):
        env = self._consume("rust-library-flags", RecursiveMakeBackend)
        self.assertEqual(
            self._cargo_spec(env, "rust-tests.json"),
            {"crates": ["flags-tests"]},
        )

    def test_rust_library_command_spec_computed_flags(self):
        """A directory's computed compile and link flags reach the spec."""
        env = self._consume("rust-library-flags", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-library-spec.json")["edge"]

        self.assertIn("-DMOZ_RUST_SPEC_CFLAG", edge["computed_cflags"])
        self.assertIn("-DMOZ_RUST_SPEC_CFLAG", edge["computed_cxxflags"])
        self.assertIn("-DMOZ_RUST_SPEC_HOST_CFLAG", edge["computed_host_cflags"])
        self.assertIn("-DMOZ_RUST_SPEC_HOST_CFLAG", edge["computed_host_cxxflags"])
        self.assertIn("-Wl,--moz-rust-spec-ldflag", edge["link_flags"])

    def test_host_rust_library(self):
        """Test that a Rust library is written to backend.mk correctly."""
        env = self._consume("host-rust-library", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "HOST_RUST_LIBRARY_FILE := x86_64-unknown-linux-gnu/release/libhostrusttool.a",
            "CARGO_FILE := $(srcdir)/Cargo.toml",
            "CARGO_TARGET_DIR := %s" % env.topobjdir,
        ]

        self.assertEqual(lines, expected)

    def test_host_rust_library_command_spec(self):
        """A host library edge is classified as host in its Cargo spec."""
        env = self._consume("host-rust-library", RecursiveMakeBackend)

        edge = self._cargo_spec(env, ".cargo-host-library-spec.json")["edge"]
        self.assertEqual(edge["kind"], "host-library")
        self.assertEqual(edge["names"], ["libhostrusttool.a"])

    def test_host_rust_library_with_features(self):
        """Test that a host Rust library with features is written to backend.mk correctly."""
        env = self._consume("host-rust-library-features", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "HOST_RUST_LIBRARY_FILE := x86_64-unknown-linux-gnu/release/libhostrusttool.a",
            "CARGO_FILE := $(srcdir)/Cargo.toml",
            "CARGO_TARGET_DIR := %s" % env.topobjdir,
        ]

        self.assertEqual(lines, expected)

    def test_rust_library_with_features(self):
        """Test that a Rust library with features is written to backend.mk correctly."""
        env = self._consume("rust-library-features", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "RUST_LIBRARY_FILE := x86_64-unknown-linux-gnu/release/libfeature_library.a",
            "CARGO_FILE := $(srcdir)/Cargo.toml",
            "CARGO_TARGET_DIR := %s" % env.topobjdir,
        ]

        self.assertEqual(lines, expected)

    def test_rust_program_with_features(self):
        """Test that a Rust program with features is written to backend.mk correctly."""
        env = self._consume("rust-program-features", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            f"CARGO_FILE := {env.topsrcdir}/Cargo.toml",
            f"CARGO_TARGET_DIR := {env.topobjdir}",
            "RUST_PROGRAMS += $(DEPTH)/i686-pc-windows-msvc/release/test-program-features.exe",
            "RUST_CARGO_PROGRAMS += test-program-features",
        ]

        self.assertEqual(lines, expected)

    def test_host_rust_program_with_features(self):
        """Test that a host Rust program with features is written to backend.mk correctly."""
        env = self._consume("host-rust-program-features", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            f"CARGO_FILE := {env.topsrcdir}/Cargo.toml",
            f"CARGO_TARGET_DIR := {env.topobjdir}",
            "HOST_RUST_PROGRAMS += $(DEPTH)/i686-pc-windows-msvc/release/test-host-program-features.exe",
            "HOST_RUST_CARGO_PROGRAMS += test-host-program-features",
        ]

        self.assertEqual(lines, expected)

    def test_rust_programs(self):
        """Test that `{HOST_,}RUST_PROGRAMS` are written to backend.mk correctly."""
        env = self._consume("rust-programs", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "code/backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            # Strip out computed flags, they're a PITA to test.
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            "CARGO_FILE := %s/code/Cargo.toml" % env.topsrcdir,
            "CARGO_TARGET_DIR := %s" % env.topobjdir,
            "RUST_PROGRAMS += $(DEPTH)/i686-pc-windows-msvc/release/target.exe",
            "RUST_CARGO_PROGRAMS += target",
            "HOST_RUST_PROGRAMS += $(DEPTH)/i686-pc-windows-msvc/release/host.exe",
            "HOST_RUST_CARGO_PROGRAMS += host",
        ]

        self.assertEqual(lines, expected)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        lines = [l.strip() for l in open(root_deps_path).readlines()]

        self.assertTrue(
            any(l == "recurse_compile: code/host code/target" for l in lines)
        )

        root_path = mozpath.join(env.topobjdir, "root.mk")
        with open(root_path) as fh:
            syms_line = next(
                (l for l in fh.read().splitlines() if l.startswith("syms_targets :=")),
                "",
            )
        self.assertIn("code/syms", syms_line)

    def test_rust_program_command_specs(self):
        """Target and host program edges group every program of one kind."""
        env = self._consume("rust-programs", RecursiveMakeBackend)

        target = self._cargo_spec(env, "code/.cargo-program-spec.json")["edge"]
        self.assertEqual(target["kind"], "program")
        self.assertEqual(target["names"], ["target"])
        self.assertEqual(target["outputs"], ["i686-pc-windows-msvc/release/target.exe"])
        code = mozpath.normsep(mozpath.join(env.topobjdir, "code"))
        self.assertEqual(target["rustc_flags"], ["-C", f"link-arg={code}/module.res"])

        host = self._cargo_spec(env, "code/.cargo-host-program-spec.json")["edge"]
        self.assertEqual(host["kind"], "host-program")
        self.assertEqual(host["names"], ["host"])
        self.assertEqual(host["outputs"], ["i686-pc-windows-msvc/release/host.exe"])
        self.assertEqual(host["rustc_flags"], [])

    def test_host_rust_program_output_category(self):
        """Test that a host Rust program with output_category is written correctly."""
        env = self._consume("host-rust-program-output-category", RecursiveMakeBackend)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [
            l.strip()
            for l in open(backend_path).readlines()[2:]
            if not l.startswith("COMPUTED_")
        ]

        expected = [
            f"CARGO_FILE := {env.topsrcdir}/Cargo.toml",
            f"CARGO_TARGET_DIR := {env.topobjdir}",
            "HOST_RUST_PROGRAMS += $(DEPTH)/i686-pc-windows-msvc/release/test-host-program-output-category.exe",
            "HOST_RUST_CARGO_PROGRAMS += test-host-program-output-category",
            "test-category:: $(DEPTH)/i686-pc-windows-msvc/release/test-host-program-output-category.exe",
            "MOZBUILD_NON_DEFAULT_TARGETS += $(DEPTH)/i686-pc-windows-msvc/release/test-host-program-output-category.exe",
            "HOST_RUST_PROGRAM_OUTPUT_CATEGORY := test-category",
        ]

        self.assertEqual(lines, expected)

    def test_rust_program_output_category(self):
        """A Rust program with output_category is excluded from syms_targets."""
        env = self._consume("rust-program-output-category", RecursiveMakeBackend)

        root_path = mozpath.join(env.topobjdir, "root.mk")
        with open(root_path) as fh:
            content = fh.read()

        syms_line = next(
            (l for l in content.splitlines() if l.startswith("syms_targets :=")),
            "",
        )

        self.assertIn("without-output-category/syms", syms_line)
        self.assertNotIn("with-output-category/syms", syms_line)

        self.assertIn("mixed/syms", syms_line)
        backend_path = mozpath.join(env.topobjdir, "mixed/backend.mk")
        with open(backend_path) as fh:
            lines = [l.strip() for l in fh.readlines()]
        rust_program = "$(DEPTH)/i686-pc-windows-msvc/release/mixed-rust.exe"
        self.assertIn(f"RUST_PROGRAMS += {rust_program}", lines)
        self.assertIn(f"MOZBUILD_NON_DEFAULT_TARGETS += {rust_program}", lines)

    def test_final_target(self):
        """Test that FINAL_TARGET is written to backend.mk correctly."""
        env = self._consume("final_target", RecursiveMakeBackend)

        expected = dict()
        expected[env.topobjdir] = []
        expected[mozpath.join(env.topobjdir, "both")] = [
            "DIST_SUBDIR = asubdir",
            "FINAL_TARGET = $(DEPTH)/dist/xpi-stage/mycrazyxpi/asubdir",
        ]
        expected[mozpath.join(env.topobjdir, "dist-subdir")] = [
            "DIST_SUBDIR = asubdir",
            "FINAL_TARGET = $(DEPTH)/dist/bin/asubdir",
        ]
        expected[mozpath.join(env.topobjdir, "xpi-name")] = [
            "FINAL_TARGET = $(DEPTH)/dist/xpi-stage/mycrazyxpi",
        ]
        expected[mozpath.join(env.topobjdir, "final-target")] = [
            "FINAL_TARGET = $(DEPTH)/random-final-target"
        ]
        for key, expected_rules in expected.items():
            backend_path = mozpath.join(key, "backend.mk")
            lines = [l.strip() for l in open(backend_path).readlines()[2:]]
            found = [
                str
                for str in lines
                if str.startswith("FINAL_TARGET")
                or str.startswith("XPI_NAME")
                or str.startswith("DIST_SUBDIR")
            ]
            self.assertEqual(found, expected_rules)

    def test_final_target_pp_files(self):
        """Test that FINAL_TARGET_PP_FILES is written to backend.mk correctly."""
        env, reg = self._dist_bin_registry("dist-files")
        entries = dict(reg)

        self.assertIn("install.rdf", entries)
        self.assertIn("main.js", entries)
        self.assertEqual(type(entries["install.rdf"]).__name__, "PreprocessedFile")

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        self.assertEqual([l for l in lines if "DIST_FILES" in l], [])

    def _dist_bin_registry(self, name, manifest="dist_bin"):
        env = self._consume(name, RecursiveMakeBackend)

        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "install", manifest)
        )
        reg = FileRegistry()
        m.populate_registry(reg)
        return env, reg

    def _install_dist_bin_rule(self, name):
        env = self._consume(name, RecursiveMakeBackend)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        rule = [
            l
            for l in open(root_deps_path).readlines()
            if l.startswith("install-dist/bin:")
        ]

        self.assertEqual(len(rule), 1)
        return rule[0]

    def test_pp_defines(self):
        env, reg = self._dist_bin_registry("pp-defines")

        defines = reg["script.js"].defines
        self.assertEqual(defines["AB_CD"], env.substs["MOZ_UI_LOCALE"])
        self.assertEqual(defines["FROM_THE_DECLARATION"], "declared")
        self.assertEqual(defines["A_CONFIGURED_STRING"], "a value")

    def test_pp_defines_digit(self):
        _, reg = self._dist_bin_registry("pp-defines")

        self.assertEqual(reg["script.js"].defines["A_CONFIGURED_NUMBER"], 1)

    def test_pp_defines_boolean(self):
        _, reg = self._dist_bin_registry("pp-defines")

        defines = reg["script.js"].defines
        self.assertEqual(defines["DECLARED_ON"], 1)
        self.assertNotIn("DECLARED_OFF", defines)

    def test_pp_depfile(self):
        env, reg = self._dist_bin_registry("pp-defines")

        depfile = mozpath.normsep(reg["script.js"].depfile)
        self.assertTrue(depfile.endswith("/.deps/dist/bin/script.js.pp"), depfile)
        self.assertTrue(depfile.startswith(mozpath.normsep(env.topobjdir)), depfile)

    def test_pp_css_marker(self):
        _, reg = self._dist_bin_registry("pp-defines")

        self.assertEqual(reg["sheet.css"].marker, "%")
        self.assertEqual(reg["script.js"].marker, "#")

    def test_pp_no_make_rule(self):
        env = self._consume("pp-defines", RecursiveMakeBackend)

        with open(mozpath.join(env.topobjdir, "backend.mk")) as fh:
            body = fh.read()

        self.assertNotIn("PP_TARGETS", body)

    def test_final_target_files_absolute(self):
        """Absolute FINAL_TARGET_FILES are installed, and libraries checked."""
        env = self._get_environment("final-target-files-absolute")

        # The files only need to exist for the moz.build to be read, and *.pdb
        # is ignored tree-wide, so create them in the objdir rather than
        # checking them in.
        so = mozpath.join(env.topobjdir, "libfoo.so")
        pdb = mozpath.join(env.topobjdir, "libfoo.pdb")
        for path in (so, pdb):
            open(path, "a").close()

        self._consume("final-target-files-absolute", RecursiveMakeBackend, env=env)

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        lines = [l.strip() for l in open(backend_path).readlines()[2:]]

        expected = [
            # Debug information is installed, but there is nothing to check.
            f"$(INSTALL) {pdb} $(DEPTH)/dist/bin/",
            f"$(INSTALL) {so} $(DEPTH)/dist/bin/",
            "$(call py_action,check_binary libfoo.so,$(DEPTH)/dist/bin/libfoo.so)",
        ]

        found = [str for str in lines if "libfoo." in str]
        self.assertEqual(found, expected)

    def test_final_target_files_directory_wildcard(self):
        _, reg = self._dist_bin_registry("final-target-files-directory-wildcard")

        self.assertEqual(
            sorted(reg.paths()),
            ["foo/a/config.json", "foo/a/x.js", "foo/b/config.json", "foo/b/y.js"],
        )

    def test_final_target_files_renamed_wildcard(self):
        _, reg = self._dist_bin_registry("final-target-files-renamed-wildcard")

        self.assertEqual(sorted(reg.paths()), ["foo/icons/a.svg", "foo/icons/b.svg"])

    def test_chrome_manifest_resource(self):
        _, reg = self._dist_bin_registry("chrome-manifest-resource")
        body = reg["chrome/browser.manifest"].open().read().decode("utf-8")

        self.assertEqual(
            body.splitlines(),
            [
                "content testcontent browser/testcontent/",
                "resource testres browser/testres/ contentaccessible=yes",
            ],
        )

    def test_chrome_manifest_installed(self):
        env, reg = self._dist_bin_registry("chrome-manifest-resource")

        self.assertIn("chrome/browser.manifest", reg.paths())
        self.assertIn("chrome.manifest", reg.paths())
        top = reg["chrome.manifest"].open().read().decode("utf-8")
        self.assertEqual(top.splitlines(), ["manifest chrome/browser.manifest"])

        with open(mozpath.join(env.topobjdir, "backend.mk")) as fh:
            self.assertNotIn("buildlist", fh.read())

    def test_pp_files_extra_deps_not_generated(self):
        with self.assertRaisesRegex(Exception, "top level backend file"):
            self._consume("pp-files-extra-deps", RecursiveMakeBackend)

    def test_pp_files_extra_deps_post_process(self):
        """PP_FILES_EXTRA_DEPS pointing at a generated file in another
        directory adds a cross-directory build-order dependency."""
        # The install runs from the top level Makefile, so the generated file it
        # reads is a prerequisite of the install rather than of the directory
        # that uses it.
        rule = self._install_dist_bin_rule("pp-files-extra-deps-generated")

        self.assertIn("$(DEPTH)/generated-header.h", rule)

    def test_pp_files_extra_deps_other_directory(self):
        rule = self._install_dist_bin_rule("pp-files-extra-deps-other-directory")

        self.assertIn("$(DEPTH)/producer/generated-header.h", rule)

    def _dist_bin_manifest(self, srcdir):
        env = self._consume(srcdir, RecursiveMakeBackend)

        return env, InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "install", "dist_bin")
        )

    def test_localized_files(self):
        """A localized install is described the same way as any other, so its
        destinations are part of what the install recorded.
        """
        env, m = self._dist_bin_manifest("localized-files")

        reg = FileRegistry()
        m.populate_registry(reg)
        self.assertEqual(sorted(reg.paths()), ["bar.ini", "foo.js"])

        # A pattern is expanded by the install, so it is recorded as one
        # rather than as the files it happens to match now.
        patterns = [e for e in m._dests.values() if e[0] == m.PATTERN_LINK]
        self.assertEqual(len(patterns), 1)
        self.assertEqual(patterns[0][2], "*.abc")

        backend_path = mozpath.join(env.topobjdir, "backend.mk")
        body = open(backend_path).read()
        self.assertNotIn("LOCALIZED_FILES", body)

    def test_a_localized_install_reads_the_source_locale(self):
        """A build installs the source locale, en-US, whatever locale it is
        configured for. Any other locale is staged from the merge tree by the
        locale staging action.
        """
        env = self._get_environment("localized-files", substs={"MOZ_UI_LOCALE": "fr"})
        self._consume("localized-files", RecursiveMakeBackend, env=env)
        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "install", "dist_bin")
        )
        reg = FileRegistry()
        m.populate_registry(reg)

        for dest in ("bar.ini", "foo.js"):
            path = mozpath.normsep(reg[dest].path)
            self.assertTrue(path.endswith(f"/en-US/{dest}"), path)

    def test_localized_pp_files(self):
        """A localized preprocessed install carries what it is preprocessed
        with, and a localization is allowed to hold no directive at all.
        """
        env, m = self._dist_bin_manifest("localized-pp-files")

        reg = FileRegistry()
        m.populate_registry(reg)
        self.assertEqual(sorted(reg.paths()), ["bar.ini", "foo.js"])
        for dest in ("bar.ini", "foo.js"):
            entry = reg[dest]
            self.assertEqual(type(entry).__name__, "PreprocessedFile")
            self.assertTrue(entry.silence_missing_directive_warnings)
            self.assertEqual(entry.defines["AB_CD"], env.substs["MOZ_UI_LOCALE"])

        body = open(mozpath.join(env.topobjdir, "backend.mk")).read()
        self.assertNotIn("LOCALIZED_PP_FILES", body)

    def test_config(self):
        """Test that CONFIGURE_SUBST_FILES are properly handled."""
        env = self._consume("test_config", RecursiveMakeBackend)

        self.assertEqual(
            open(os.path.join(env.topobjdir, "file")).readlines(),
            ["#ifdef foo\n", "bar baz\n", "@bar@\n"],
        )

    def test_prog_lib_c_only(self):
        """Test that C-only binary artifacts are marked as such."""
        env = self._consume("prog-lib-c-only", RecursiveMakeBackend)

        # PROGRAM C-onlyness.
        with open(os.path.join(env.topobjdir, "c-program", "backend.mk")) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            self.assertIn("PROG_IS_C_ONLY_c_test_program := 1", lines)

        with open(os.path.join(env.topobjdir, "cxx-program", "backend.mk")) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            # Test for only the absence of the variable, not the precise
            # form of the variable assignment.
            for line in lines:
                self.assertNotIn("PROG_IS_C_ONLY_cxx_test_program", line)

        # SIMPLE_PROGRAMS C-onlyness.
        with open(os.path.join(env.topobjdir, "c-simple-programs", "backend.mk")) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            self.assertIn("PROG_IS_C_ONLY_c_simple_program := 1", lines)

        with open(
            os.path.join(env.topobjdir, "cxx-simple-programs", "backend.mk")
        ) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            for line in lines:
                self.assertNotIn("PROG_IS_C_ONLY_cxx_simple_program", line)

        # Libraries C-onlyness.
        with open(os.path.join(env.topobjdir, "c-library", "backend.mk")) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            self.assertIn("LIB_IS_C_ONLY := 1", lines)

        with open(os.path.join(env.topobjdir, "cxx-library", "backend.mk")) as fh:
            lines = fh.readlines()
            lines = [line.rstrip() for line in lines]

            for line in lines:
                self.assertNotIn("LIB_IS_C_ONLY", line)

    def test_linkage(self):
        env = self._consume("linkage", RecursiveMakeBackend)
        expected_linkage = {
            "prog": {
                "SHARED_LIBS": ["$(DEPTH)/dist/bin/qux.so", "$(DEPTH)/dist/bin/baz.so"],
                "STATIC_LIBS": ["$(DEPTH)/real/foo.a"],
                "OS_LIBS": ["-lfoo", "-lbaz", "-lbar"],
            },
            "shared": {
                "OS_LIBS": ["-lfoo"],
                "SHARED_LIBS": ["$(DEPTH)/dist/bin/qux.so"],
                "STATIC_LIBS": [],
            },
            "static": {
                "STATIC_LIBS": ["$(DEPTH)/real/foo.a"],
                "OS_LIBS": ["-lbar"],
                "SHARED_LIBS": ["$(DEPTH)/dist/bin/qux.so"],
            },
            "real": {
                "STATIC_LIBS": [],
                "SHARED_LIBS": ["$(DEPTH)/dist/bin/qux.so"],
                "OS_LIBS": ["-lbaz"],
            },
        }
        actual_linkage = {}
        for name in expected_linkage.keys():
            with open(os.path.join(env.topobjdir, name, "backend.mk")) as fh:
                actual_linkage[name] = [line.rstrip() for line in fh.readlines()]
        for name in expected_linkage:
            for var in expected_linkage[name]:
                for val in expected_linkage[name][var]:
                    line = "%s += %s" % (var, val)
                    self.assertIn(line, actual_linkage[name])
                    actual_linkage[name].remove(line)
                for line in actual_linkage[name]:
                    self.assertNotIn("%s +=" % var, line)

    def test_list_files(self):
        env = self._consume("linkage", RecursiveMakeBackend)
        expected_list_files = {
            "prog/MyProgram_exe.list": [
                "../static/bar/bar1.o",
                "../static/bar/bar2.o",
                "../static/bar/bar_helper/bar_helper1.o",
                "../build-static-lib-archive/frob1.o",
            ],
            "shared/baz_so.list": ["baz/baz1.o"],
        }
        actual_list_files = {}
        for name in expected_list_files.keys():
            with open(os.path.join(env.topobjdir, name)) as fh:
                actual_list_files[name] = [line.rstrip() for line in fh.readlines()]
        for name in expected_list_files:
            self.assertEqual(
                actual_list_files[name],
                [os.path.normpath(f) for f in expected_list_files[name]],
            )

        # We don't produce a list file for a shared library composed only of
        # object files in its directory, but instead list them in a variable.
        with open(os.path.join(env.topobjdir, "prog", "qux", "backend.mk")) as fh:
            lines = [line.rstrip() for line in fh.readlines()]

        self.assertIn("qux.so_OBJS := qux1.o", lines)

    def test_build_static_lib_archive(self):
        """BUILD_STATIC_LIB_ARCHIVE builds an archive without changing linkage."""
        env = self._consume("linkage", RecursiveMakeBackend)

        with open(
            os.path.join(env.topobjdir, "build-static-lib-archive", "backend.mk")
        ) as fh:
            lines = [line.rstrip() for line in fh.readlines()]

        self.assertIn("LIBRARY := $(REAL_LIBRARY)", lines)
        self.assertIn("frob.a_OBJS := frob1.o", lines)
        self.assertIn("STATIC_LIB_FILES := $(REAL_LIBRARY)", lines)
        self.assertIn("STATIC_LIB_DEST := $(DIST)/lib", lines)
        self.assertIn("STATIC_LIB_TARGET := target", lines)
        self.assertIn("INSTALL_TARGETS += STATIC_LIB", lines)

        # NO_EXPAND_LIBS implies BUILD_STATIC_LIB_ARCHIVE.
        with open(os.path.join(env.topobjdir, "real", "backend.mk")) as fh:
            lines = [line.rstrip() for line in fh.readlines()]

        self.assertIn("LIBRARY := $(REAL_LIBRARY)", lines)

    def test_rust_library_depends_on_archive(self):
        """A Rust library must depend on the archive of a BUILD_STATIC_LIB_ARCHIVE
        library it uses, so the archive exists before cargo runs."""
        env = self._consume("rust-library-archive-dep", RecursiveMakeBackend)

        root_deps_path = mozpath.join(env.topobjdir, "root-deps.mk")
        with open(root_deps_path) as fh:
            lines = [l.strip() for l in fh.readlines()]

        self.assertIn("rust/uniffi-target: archive/target middle/target-objects", lines)
        self.assertIn("archive/target: archive/target-objects", lines)

        backend_path = mozpath.join(env.topobjdir, "archive", "backend.mk")
        with open(backend_path) as fh:
            backend = [l.strip() for l in fh.readlines()]

        self.assertIn("LIBRARY := $(REAL_LIBRARY)", backend)
        self.assertIn("STATIC_LIB_DEST := $(DIST)/lib", backend)
        # Without the target tier the install defaults to libs, which runs
        # after cargo has already looked for the archive in dist/lib.
        self.assertIn("STATIC_LIB_TARGET := target", backend)

    def test_jar_manifests_non_application(self):
        _, reg = self._dist_bin_registry(
            "jar-manifests-non-application", manifest="_tests"
        )

        self.assertIn(
            "jar-manifests/chrome/test/content/test/thing.js",
            [dest for dest, _ in reg],
        )

    def test_jar_manifests_renamed_wildcard(self):
        _, reg = self._dist_bin_registry("jar-manifest-renamed-wildcard")

        self.assertEqual(
            sorted(reg.paths()),
            [
                "chrome/browser/content/browser/icons/a.svg",
                "chrome/browser/content/browser/icons/b.svg",
            ],
        )

    def test_jar_manifests_generated_pp(self):
        env, reg = self._dist_bin_registry("jar-manifests-generated-pp")
        self.assertIn("chrome/test/content/test/made.js", reg.paths())

        rule = self._install_dist_bin_rule("jar-manifests-generated-pp")
        self.assertIn("$(DEPTH)/made.js", rule)

        m = InstallManifest(
            path=mozpath.join(env.topobjdir, "_build_manifests", "built", "misc")
        )
        built = FileRegistry()
        m.populate_registry(built)
        self.assertEqual(
            {dest: f.path for dest, f in built},
            {
                "dist/bin/chrome/test/content/test/renamed.js": mozpath.join(
                    env.topobjdir, "made.js"
                )
            },
        )

    def test_test_manifests_duplicate_support_files(self):
        """Ensure duplicate support-files in test manifests work."""
        env = self._consume(
            "test-manifests-duplicate-support-files", RecursiveMakeBackend
        )

        p = os.path.join(env.topobjdir, "_build_manifests", "install", "_test_files")
        m = InstallManifest(p)
        self.assertIn("testing/mochitest/tests/support-file.txt", m)

    def test_install_manifests_package_tests(self):
        """Ensure test suites honor package_tests=False."""
        env = self._consume("test-manifests-package-tests", RecursiveMakeBackend)

        man_dir = mozpath.join(env.topobjdir, "_build_manifests", "install")
        self.assertTrue(os.path.isdir(man_dir))

        full = mozpath.join(man_dir, "_test_files")
        self.assertTrue(os.path.exists(full))

        m = InstallManifest(path=full)

        # Only mochitest.js should be in the install manifest.
        self.assertTrue("testing/mochitest/tests/mochitest.js" in m)

        # The path is odd here because we do not normalize at test manifest
        # processing time.  This is a fragile test because there's currently no
        # way to iterate the manifest.
        self.assertFalse("instrumentation/./not_packaged.java" in m)

    def test_program_paths(self):
        """PROGRAMs with various moz.build settings that change the destination should produce
        the expected paths in backend.mk."""
        env = self._consume("program-paths", RecursiveMakeBackend)

        expected = [
            ("dist-bin", "$(DEPTH)/dist/bin/dist-bin.prog"),
            ("dist-subdir", "$(DEPTH)/dist/bin/foo/dist-subdir.prog"),
            ("final-target", "$(DEPTH)/final/target/final-target.prog"),
            ("not-installed", "not-installed.prog"),
        ]
        prefix = "PROGRAM = "
        for subdir, expected_program in expected:
            with open(os.path.join(env.topobjdir, subdir, "backend.mk")) as fh:
                lines = fh.readlines()
                program = [
                    line.rstrip().split(prefix, 1)[1]
                    for line in lines
                    if line.startswith(prefix)
                ][0]
                self.assertEqual(program, expected_program)

    def test_host_cpp_prog_link(self):
        env = self._consume("host-cpp-link", RecursiveMakeBackend)

        with open(os.path.join(env.topobjdir, "cpp", "backend.mk")) as fh:
            self.assertIn("HOST_CPP_PROG_LINK := 1\n", fh.readlines())
        with open(os.path.join(env.topobjdir, "conly", "backend.mk")) as fh:
            self.assertNotIn("HOST_CPP_PROG_LINK := 1\n", fh.readlines())
        with open(os.path.join(env.topobjdir, "conly-cpplib", "backend.mk")) as fh:
            self.assertIn("HOST_CPP_PROG_LINK := 1\n", fh.readlines())
        with open(os.path.join(env.topobjdir, "simple", "backend.mk")) as fh:
            lines = fh.readlines()
        self.assertIn("HOST_SIMPLE_PROGRAMS += host_asimple\n", lines)
        self.assertIn("HOST_CPP_PROG_LINK := 1\n", lines)

    def test_per_source_object_deps(self):
        env = self._consume("per-source-object-deps", RecursiveMakeBackend)

        with open(mozpath.join(env.topobjdir, "backend.mk")) as fh:
            lines = fh.readlines()

        self.assertIn("ASFILES += gen.s\n", lines)
        self.assertIn(
            "gen.$(OBJ_SUFFIX): gen.s include.S $(srcdir)/included.inc\n", lines
        )
        self.assertIn("ASFILES += $(srcdir)/static.s\n", lines)
        self.assertIn(
            "static.$(OBJ_SUFFIX): $(srcdir)/static.s $(srcdir)/included.inc\n", lines
        )
        self.assertEqual([l for l in lines if l.startswith("plain.$(OBJ_SUFFIX)")], [])

    def test_extra_link_deps(self):
        """Test that EXTRA_LINK_DEPS is handled properly."""
        env = self._consume("extra-link-deps", RecursiveMakeBackend)

        subdir_backend = mozpath.join(env.topobjdir, "subdir", "backend.mk")
        with open(subdir_backend) as fh:
            subdir_content = fh.read()
        self.assertIn(
            "extra-link-deps-program: $(srcdir)/dep.txt",
            subdir_content,
        )
        self.assertIn(
            "extra-link-deps-program: generated.plist",
            subdir_content,
        )
        self.assertIn("generated.plist:", subdir_content)

        root_backend = mozpath.join(env.topobjdir, "backend.mk")
        with open(root_backend) as fh:
            root_content = fh.read()
        self.assertNotIn("generated.plist:", root_content)

        sharedlib_backend = mozpath.join(env.topobjdir, "sharedlib", "backend.mk")
        with open(sharedlib_backend) as fh:
            sharedlib_content = fh.read()
        self.assertIn(
            "libextra-link-deps-lib.so: generated.rsp",
            sharedlib_content,
        )
        self.assertIn("COMPUTED_LDFLAGS += @generated.rsp", sharedlib_content)
        # A response file the link reads has to be written before the link, so
        # its rule belongs to the directory rather than the top level.
        self.assertIn("generated.rsp:", sharedlib_content)
        self.assertNotIn("generated.rsp:", root_content)

        # A response file no link reads keeps its rule at the top level, where
        # a consumer in another directory can still see it.
        unrelated_backend = mozpath.join(env.topobjdir, "unrelated", "backend.mk")
        with open(unrelated_backend) as fh:
            unrelated_content = fh.read()
        self.assertIn("unrelated.rsp:", root_content)
        self.assertNotIn("unrelated.rsp:", unrelated_content)

        # nested/deep.rsp and other/deep.rsp share a base name and only the
        # first is linked against, so the two are told apart by path.
        self.assertIn("nested/deep.rsp:", sharedlib_content)
        self.assertNotIn("sharedlib/nested/deep.rsp:", root_content)
        self.assertNotIn("other/deep.rsp:", sharedlib_content)
        self.assertIn("sharedlib/other/deep.rsp:", root_content)

    def test_shared_library_output_category(self):
        """SharedLibrary with output_category should be excluded from
        syms_targets, since the default %/syms: %/target static pattern in
        recurse.mk doesn't apply to non-default tier libs."""
        env = self._consume("shared-library-output-category", RecursiveMakeBackend)

        root_path = mozpath.join(env.topobjdir, "root.mk")
        with open(root_path) as fh:
            content = fh.read()

        syms_line = next(
            (l for l in content.splitlines() if l.startswith("syms_targets :=")),
            "",
        )

        self.assertIn("without-output-category/syms", syms_line)
        self.assertNotIn("with-output-category/syms", syms_line)

    def test_shared_lib_paths(self):
        """SHARED_LIBRARYs with various moz.build settings that change the destination should
        produce the expected paths in backend.mk."""
        env = self._consume("shared-lib-paths", RecursiveMakeBackend)

        expected = [
            ("dist-bin", "$(DEPTH)/dist/bin/libdist-bin.so"),
            ("dist-subdir", "$(DEPTH)/dist/bin/foo/libdist-subdir.so"),
            ("final-target", "$(DEPTH)/final/target/libfinal-target.so"),
            ("not-installed", "libnot-installed.so"),
        ]
        prefix = "SHARED_LIBRARY := "
        for subdir, expected_shared_lib in expected:
            with open(os.path.join(env.topobjdir, subdir, "backend.mk")) as fh:
                lines = fh.readlines()
                shared_lib = [
                    line.rstrip().split(prefix, 1)[1]
                    for line in lines
                    if line.startswith(prefix)
                ][0]
                self.assertEqual(shared_lib, expected_shared_lib)

    def test_shared_lib_import_lib_paths(self):
        """On Windows, import libraries are installed to dist/lib, mirroring
        DIST_SUBDIR so that same-named libraries don't collide."""
        env = self._get_environment(
            "shared-lib-paths-windows", srcdir_name="shared-lib-paths"
        )
        env = self._consume("shared-lib-paths", RecursiveMakeBackend, env=env)

        def backend_mk(subdir):
            with open(os.path.join(env.topobjdir, subdir, "backend.mk")) as fh:
                return fh.read()

        self.assertIn(
            "IMPORT_LIB_DEST = $(DIST)/lib$(DIST_SUBDIR:%=/%)\n",
            backend_mk("dist-subdir"),
        )
        self.assertNotIn("IMPORT_LIB_DEST", backend_mk("not-installed"))

    def test_licenses_json(self):
        """LICENSES declarations are aggregated into licenses.json."""
        env = self._consume("licenses", RecursiveMakeBackend)

        with open(mozpath.join(env.topobjdir, "licenses.json")) as fh:
            licenses = json.load(fh)["licenses"]

        by_id = {license["id"]: license for license in licenses}
        self.assertEqual(sorted(by_id), ["MIT", "mylib"])

        mit = by_id["MIT"]
        self.assertEqual(mit["title"], "MIT License")
        self.assertEqual(mit["spdx"], "MIT")
        self.assertFalse(mit["html"])
        self.assertIn("Permission is hereby granted", mit["text"])
        # The explicit path plus the path-scoped LICENSED_UNDER entry. The
        # declaring directory itself is not listed, because paths narrowed it.
        self.assertEqual(
            mit["paths"], ["lib/vendor/dep.js", "third_party/rust/byteorder"]
        )

        mylib = by_id["mylib"]
        self.assertEqual(mylib["declared_in"], "lib")
        self.assertEqual(mylib["notice"], "Copyright 2026 Somebody.")
        self.assertTrue(mylib["html"])
        self.assertEqual(mylib["paths"], [])
        self.assertEqual(mylib["acknowledgement"], "Portions are copyright Somebody.")
        self.assertIsNone(mit["acknowledgement"])


if __name__ == "__main__":
    main()
