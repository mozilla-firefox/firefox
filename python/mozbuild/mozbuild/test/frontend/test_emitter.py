# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest

import mozpack.path as mozpath
from mozunit import main

from mozbuild.frontend.context import ObjDirPath, SourcePath
from mozbuild.frontend.data import (
    ComputedFlags,
    ConfigFileSubstitution,
    DeclaredLicensedPaths,
    DeclaredLicenseNotice,
    Defines,
    DirectoryTraversal,
    Exports,
    FinalTargetFiles,
    FinalTargetPreprocessedFiles,
    GeneratedFile,
    Headers,
    HostProgram,
    HostRustLibrary,
    HostRustProgram,
    HostSources,
    IPDLCollection,
    LocalInclude,
    LocalizedFiles,
    LocalizedPreprocessedFiles,
    MozSrcFiles,
    PerSourceFlag,
    Program,
    RustLibrary,
    RustProgram,
    SharedLibrary,
    SimpleProgram,
    Sources,
    StaticLibrary,
    TestHarnessFiles,
    TestManifest,
    UnifiedSources,
    VariablePassthru,
    WasmSources,
)
from mozbuild.frontend.emitter import TreeMetadataEmitter
from mozbuild.frontend.l10n_manifest import L10nManifestContext
from mozbuild.frontend.reader import (
    BuildReader,
    BuildReaderError,
    SandboxValidationError,
)
from mozbuild.test.common import MockConfig

data_path = mozpath.abspath(mozpath.dirname(__file__))
data_path = mozpath.join(data_path, "data")


class TestEmitterBasic(unittest.TestCase):
    def setUp(self):
        self._old_env = dict(os.environ)
        os.environ.pop("MOZ_OBJDIR", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._old_env)

    def reader(self, name, enable_tests=False, extra_substs=None):
        substs = dict(
            ENABLE_TESTS="1" if enable_tests else "",
            BIN_SUFFIX=".prog",
            HOST_BIN_SUFFIX=".hostprog",
            OS_TARGET="WINNT",
            COMPILE_ENVIRONMENT="1",
            STL_FLAGS=["-I/path/to/topobjdir/dist/stl_wrappers"],
            VISIBILITY_FLAGS=["-include", "$(topsrcdir)/config/gcc_hidden.h"],
            OBJ_SUFFIX="obj",
            WASM_OBJ_SUFFIX="wasm",
            WASM_CFLAGS=["-foo"],
        )
        if extra_substs:
            substs.update(extra_substs)
        config = MockConfig(mozpath.join(data_path, name), extra_substs=substs)

        return BuildReader(config)

    def read_topsrcdir(self, reader, filter_common=True):
        emitter = TreeMetadataEmitter(reader.config)
        objs = list(emitter.emit(reader.read_topsrcdir()))
        self.assertGreater(len(objs), 0)

        filtered = []
        for obj in objs:
            if filter_common and isinstance(
                obj, (DirectoryTraversal, L10nManifestContext)
            ):
                continue

            filtered.append(obj)

        return filtered

    def test_dirs_traversal_simple(self):
        reader = self.reader("traversal-simple")
        objs = self.read_topsrcdir(reader, filter_common=False)
        self.assertEqual(len(objs), 4)

        for o in objs:
            self.assertIsInstance(o, DirectoryTraversal)
            self.assertTrue(os.path.isabs(o.context_main_path))
            self.assertEqual(len(o.context_all_paths), 1)

        reldirs = [o.relsrcdir for o in objs]
        self.assertEqual(reldirs, ["", "foo", "foo/biz", "bar"])

        dirs = [[d.full_path for d in o.dirs] for o in objs]
        self.assertEqual(
            dirs,
            [
                [
                    mozpath.join(reader.config.topsrcdir, "foo"),
                    mozpath.join(reader.config.topsrcdir, "bar"),
                ],
                [mozpath.join(reader.config.topsrcdir, "foo", "biz")],
                [],
                [],
            ],
        )

    def test_traversal_all_vars(self):
        reader = self.reader("traversal-all-vars")
        objs = self.read_topsrcdir(reader, filter_common=False)
        self.assertEqual(len(objs), 2)

        for o in objs:
            self.assertIsInstance(o, DirectoryTraversal)

        reldirs = set([o.relsrcdir for o in objs])
        self.assertEqual(reldirs, set(["", "regular"]))

        for o in objs:
            reldir = o.relsrcdir

            if reldir == "":
                self.assertEqual(
                    [d.full_path for d in o.dirs],
                    [mozpath.join(reader.config.topsrcdir, "regular")],
                )

    def test_traversal_all_vars_enable_tests(self):
        reader = self.reader("traversal-all-vars", enable_tests=True)
        objs = self.read_topsrcdir(reader, filter_common=False)
        self.assertEqual(len(objs), 3)

        for o in objs:
            self.assertIsInstance(o, DirectoryTraversal)

        reldirs = set([o.relsrcdir for o in objs])
        self.assertEqual(reldirs, set(["", "regular", "test"]))

        for o in objs:
            reldir = o.relsrcdir

            if reldir == "":
                self.assertEqual(
                    [d.full_path for d in o.dirs],
                    [
                        mozpath.join(reader.config.topsrcdir, "regular"),
                        mozpath.join(reader.config.topsrcdir, "test"),
                    ],
                )

    def test_config_file_substitution(self):
        reader = self.reader("config-file-substitution")
        objs = self.read_topsrcdir(reader)
        self.assertEqual(len(objs), 2)

        self.assertIsInstance(objs[0], ConfigFileSubstitution)
        self.assertIsInstance(objs[1], ConfigFileSubstitution)

        topobjdir = mozpath.abspath(reader.config.topobjdir)
        self.assertEqual(objs[0].relpath, "foo")
        self.assertEqual(
            mozpath.normpath(objs[0].output_path),
            mozpath.normpath(mozpath.join(topobjdir, "foo")),
        )
        self.assertEqual(
            mozpath.normpath(objs[1].output_path),
            mozpath.normpath(mozpath.join(topobjdir, "bar")),
        )

    def test_variable_passthru(self):
        reader = self.reader("variable-passthru")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], VariablePassthru)

        wanted = {
            "NO_DIST_INSTALL": True,
            "RCFILE": "foo.rc",
            "RCINCLUDE": "bar.rc",
            "WIN32_EXE_LDFLAGS": ["-subsystem:console"],
        }

        variables = objs[0].variables
        maxDiff = self.maxDiff
        self.maxDiff = None
        self.assertEqual(wanted, variables)
        self.maxDiff = maxDiff

    def test_compile_flags(self):
        reader = self.reader(
            "compile-flags", extra_substs={"WARNINGS_AS_ERRORS": "-Werror"}
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["STL"], reader.config.substs["STL_FLAGS"])
        self.assertEqual(
            flags.flags["VISIBILITY"], reader.config.substs["VISIBILITY_FLAGS"]
        )
        self.assertEqual(flags.flags["WARNINGS_AS_ERRORS"], ["-Werror"])
        self.assertEqual(flags.flags["MOZBUILD_CFLAGS"], ["-Os", "-funroll-loops"])
        self.assertEqual(flags.flags["MOZBUILD_CXXFLAGS"], ["-funroll-loops", "-Os"])

    def test_win32_exe_default_ldflags(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={"WIN32_EXE_DEFAULT_LDFLAGS": ["-STACK:8388608"]},
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        link_flags = dict(ldflags.get_flags())
        self.assertEqual(link_flags["PROGRAM_LDFLAGS"], ["-STACK:8388608"])
        self.assertNotIn("-STACK:8388608", link_flags.get("LDFLAGS", []))
        for _, flags in host_ldflags.get_flags():
            self.assertNotIn("-STACK:8388608", flags)

    def test_macos_program_rpath(self):
        reader = self.reader(
            "compile-flags",
            extra_substs={"OS_ARCH": "Darwin"},
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertEqual(ldflags.flags["MACOS_RPATH"], ["-Wl,-rpath,@executable_path"])

    def test_macos_program_rpath_other_os(self):
        reader = self.reader("compile-flags")
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertEqual(ldflags.flags["MACOS_RPATH"], [])

    def test_per_source_object_deps(self):
        reader = self.reader("per-source-object-deps")
        objs = self.read_topsrcdir(reader)
        flags = [o for o in objs if isinstance(o, PerSourceFlag)]
        self.assertEqual(len(flags), 1)
        self.assertEqual(mozpath.basename(flags[0].file_name), "gen.s")
        generated, source = flags[0].object_deps
        self.assertIsInstance(generated, ObjDirPath)
        self.assertEqual(generated, "!include.S")
        self.assertIsInstance(source, SourcePath)
        self.assertEqual(source, "included.inc")

    def test_per_source_object_deps_missing(self):
        reader = self.reader("per-source-object-deps-missing")
        with self.assertRaisesRegex(
            SandboxValidationError, "Dependency of gen.s does not exist"
        ):
            self.read_topsrcdir(reader)

    def test_asflags(self):
        reader = self.reader("asflags", extra_substs={"ASFLAGS": ["-safeseh"]})
        as_sources, sources, ldflags, lib, flags, asflags = self.read_topsrcdir(reader)
        self.assertIsInstance(asflags, ComputedFlags)
        self.assertEqual(asflags.flags["OS"], reader.config.substs["ASFLAGS"])
        self.assertEqual(asflags.flags["MOZBUILD"], ["-no-integrated-as"])

    def test_debug_flags(self):
        reader = self.reader(
            "compile-flags",
            extra_substs={"MOZ_DEBUG_FLAGS": ["-g"], "MOZ_DEBUG_SYMBOLS": "1"},
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["DEBUG"], ["-g"])

    def test_disable_debug_flags(self):
        reader = self.reader(
            "compile-flags",
            extra_substs={"MOZ_DEBUG_FLAGS": ["-g"], "MOZ_DEBUG_SYMBOLS": ""},
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["DEBUG"], [])

    def test_link_flags(self):
        reader = self.reader(
            "link-flags",
            extra_substs={
                "OS_LDFLAGS": ["-Wl,rpath-link=/usr/lib"],
                "MOZ_OPTIMIZE": "",
                "MOZ_OPTIMIZE_LDFLAGS": ["-Wl,-dead_strip"],
                "MOZ_DEBUG_LDFLAGS": ["-framework ExceptionHandling"],
            },
        )
        sources, ldflags, lib, compile_flags = self.read_topsrcdir(reader)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertEqual(ldflags.flags["OS"], reader.config.substs["OS_LDFLAGS"])
        self.assertEqual(
            ldflags.flags["MOZBUILD"], ["-Wl,-U_foo", "-framework Foo", "-x"]
        )
        self.assertEqual(ldflags.flags["OPTIMIZE"], [])

    def test_debug_ldflags(self):
        reader = self.reader(
            "link-flags",
            extra_substs={
                "MOZ_DEBUG_SYMBOLS": "1",
                "MOZ_DEBUG_LDFLAGS": ["-framework ExceptionHandling"],
            },
        )
        sources, ldflags, lib, compile_flags = self.read_topsrcdir(reader)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertEqual(ldflags.flags["OS"], reader.config.substs["MOZ_DEBUG_LDFLAGS"])

    def test_windows_opt_link_flags(self):
        reader = self.reader(
            "link-flags",
            extra_substs={
                "OS_ARCH": "WINNT",
                "CC_TYPE": "clang-cl",
                "MOZ_OPTIMIZE": "1",
                "MOZ_DEBUG_LDFLAGS": ["-DEBUG"],
                "MOZ_DEBUG_SYMBOLS": "1",
                "MOZ_OPTIMIZE_FLAGS": [],
                "MOZ_OPTIMIZE_LDFLAGS": [],
            },
        )
        sources, ldflags, lib, compile_flags = self.read_topsrcdir(reader)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIn("-DEBUG", ldflags.flags["OS"])
        self.assertIn("-OPT:REF,ICF", ldflags.flags["OS"])

    def test_windows_dmd_link_flags(self):
        reader = self.reader(
            "link-flags",
            extra_substs={
                "OS_ARCH": "WINNT",
                "CC_TYPE": "clang-cl",
                "MOZ_DMD": "1",
                "MOZ_DEBUG_LDFLAGS": ["-DEBUG"],
                "MOZ_DEBUG_SYMBOLS": "1",
                "MOZ_OPTIMIZE": "1",
                "MOZ_OPTIMIZE_FLAGS": [],
            },
        )
        sources, ldflags, lib, compile_flags = self.read_topsrcdir(reader)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertEqual(ldflags.flags["OS"], ["-DEBUG", "-OPT:REF,ICF"])

    def test_host_compile_flags(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={
                "HOST_CXXFLAGS": ["-Wall", "-Werror"],
                "HOST_CFLAGS": ["-Werror", "-Wall"],
                "HOST_LDFLAGS": ["-Wl,--nostdlib"],
            },
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        self.assertIsInstance(host_flags, ComputedFlags)
        self.assertEqual(
            host_flags.flags["HOST_CXXFLAGS"], reader.config.substs["HOST_CXXFLAGS"]
        )
        self.assertEqual(
            host_flags.flags["HOST_CFLAGS"], reader.config.substs["HOST_CFLAGS"]
        )
        self.assertEqual(
            host_ldflags.flags["HOST_LDFLAGS"], reader.config.substs["HOST_LDFLAGS"]
        )
        self.assertEqual(
            set(host_flags.flags["HOST_DEFINES"]),
            set(["-DFOO", '-DBAZ="abcd"', "-UQUX", "-DBAR=7", "-DVALUE=xyz"]),
        )
        self.assertEqual(
            host_flags.flags["MOZBUILD_HOST_CFLAGS"], ["-funroll-loops", "-host-arg"]
        )
        self.assertEqual(host_flags.flags["MOZBUILD_HOST_CXXFLAGS"], [])
        self.assertEqual(host_ldflags.flags["MOZBUILD"], ["-Wl,--gc-sections"])

    def test_host_no_optimize_flags(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={"MOZ_OPTIMIZE": "1", "MOZ_OPTIMIZE_FLAGS": ["-O2"]},
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        self.assertIsInstance(host_flags, ComputedFlags)
        self.assertEqual(host_flags.flags["HOST_OPTIMIZE"], [])

    def test_host_optimize_flags(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={"HOST_OPTIMIZE_FLAGS": ["-O2"]},
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        self.assertIsInstance(host_flags, ComputedFlags)
        self.assertEqual(host_flags.flags["HOST_OPTIMIZE"], ["-O2"])

    def test_cross_optimize_flags(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={
                "MOZ_OPTIMIZE": "1",
                "MOZ_OPTIMIZE_FLAGS": ["-O2"],
                "HOST_OPTIMIZE_FLAGS": ["-O3"],
                "CROSS_COMPILE": "1",
            },
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        self.assertIsInstance(host_flags, ComputedFlags)
        self.assertEqual(host_flags.flags["HOST_OPTIMIZE"], ["-O3"])

    def test_host_rtl_flag(self):
        reader = self.reader(
            "host-compile-flags",
            extra_substs={
                "OS_ARCH": "WINNT",
                "MOZ_DEBUG": "1",
                "CC_TYPE": "clang-cl",
            },
        )
        sources, ldflags, host_flags, host_ldflags, lib, target_flags = (
            self.read_topsrcdir(reader)
        )
        self.assertIsInstance(host_flags, ComputedFlags)
        self.assertEqual(host_flags.flags["RTL"], ["-MDd"])

    def test_compile_flags_validation(self):
        reader = self.reader("compile-flags-field-validation")

        with self.assertRaisesRegex(BuildReaderError, "Invalid value."):
            self.read_topsrcdir(reader)

        reader = self.reader("compile-flags-type-validation")
        with self.assertRaisesRegex(
            BuildReaderError, "A list of strings must be provided"
        ):
            self.read_topsrcdir(reader)

    def test_compile_flags_templates(self):
        reader = self.reader(
            "compile-flags-templates",
            extra_substs={
                "NSPR_CFLAGS": ["-I/nspr/path"],
                "NSS_CFLAGS": ["-I/nss/path"],
                "MOZ_JPEG_CFLAGS": ["-I/jpeg/path"],
                "MOZ_PNG_CFLAGS": ["-I/png/path"],
                "MOZ_ZLIB_CFLAGS": ["-I/zlib/path"],
                "MOZ_PIXMAN_CFLAGS": ["-I/pixman/path"],
            },
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["STL"], [])
        self.assertEqual(flags.flags["VISIBILITY"], [])
        self.assertEqual(
            flags.flags["OS_INCLUDES"],
            [
                "-I/nspr/path",
                "-I/nss/path",
                "-I/jpeg/path",
                "-I/png/path",
                "-I/zlib/path",
                "-I/pixman/path",
            ],
        )

    def test_disable_stl_wrapping(self):
        reader = self.reader("disable-stl-wrapping")
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["STL"], [])

    def test_visibility_flags(self):
        reader = self.reader("visibility-flags")
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(flags.flags["VISIBILITY"], [])

    def test_defines_in_flags(self):
        reader = self.reader("compile-defines")
        defines, sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(
            flags.flags["LIBRARY_DEFINES"], ["-DMOZ_LIBRARY_DEFINE=MOZ_TEST"]
        )
        self.assertEqual(flags.flags["DEFINES"], ["-DMOZ_TEST_DEFINE"])

    def test_resolved_flags_error(self):
        reader = self.reader("resolved-flags-error")
        with self.assertRaisesRegex(
            BuildReaderError, "`DEFINES` may not be set in COMPILE_FLAGS from moz.build"
        ):
            self.read_topsrcdir(reader)

    def test_includes_in_flags(self):
        reader = self.reader("compile-includes")
        defines, sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(
            flags.flags["BASE_INCLUDES"],
            ["-I%s" % reader.config.topsrcdir, "-I%s" % reader.config.topobjdir],
        )
        self.assertEqual(
            flags.flags["EXTRA_INCLUDES"],
            ["-I%s/dist/include" % reader.config.topobjdir],
        )
        self.assertEqual(
            flags.flags["LOCAL_INCLUDES"], ["-I%s/subdir" % reader.config.topsrcdir]
        )

    def test_allow_compiler_warnings(self):
        reader = self.reader(
            "allow-compiler-warnings", extra_substs={"WARNINGS_AS_ERRORS": "-Werror"}
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertEqual(flags.flags["WARNINGS_AS_ERRORS"], [])

    def test_disable_compiler_warnings(self):
        reader = self.reader(
            "disable-compiler-warnings", extra_substs={"WARNINGS_CFLAGS": "-Wall"}
        )
        sources, ldflags, lib, flags = self.read_topsrcdir(reader)
        self.assertEqual(flags.flags["WARNINGS_CFLAGS"], [])

    def test_use_nasm(self):
        # When nasm is not available, this should raise.
        reader = self.reader("use-nasm")
        with self.assertRaisesRegex(SandboxValidationError, "nasm is not available"):
            self.read_topsrcdir(reader)

        # When nasm is available, this should work.
        reader = self.reader(
            "use-nasm", extra_substs=dict(NASM="nasm", NASM_ASFLAGS="-foo")
        )

        sources, passthru, ldflags, lib, flags, asflags = self.read_topsrcdir(reader)

        self.assertIsInstance(passthru, VariablePassthru)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertIsInstance(asflags, ComputedFlags)

        self.assertEqual(asflags.flags["OS"], reader.config.substs["NASM_ASFLAGS"])

        maxDiff = self.maxDiff
        self.maxDiff = None
        self.assertEqual(
            passthru.variables,
            {"AS": "nasm", "AS_DASH_C_FLAG": "", "ASOUTOPTION": "-o "},
        )
        self.maxDiff = maxDiff

    def test_generated_files(self):
        reader = self.reader("generated-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 3)
        for o in objs:
            self.assertIsInstance(o, GeneratedFile)
            self.assertFalse(o.localized)
            self.assertFalse(o.force)

        expected = ["bar.c", "foo.c", ("xpidllex.py", "xpidlyacc.py")]
        for o, f in zip(objs, expected):
            expected_filename = f if isinstance(f, tuple) else (f,)
            self.assertEqual(o.outputs, expected_filename)
            self.assertEqual(o.script, None)
            self.assertEqual(o.method, None)
            self.assertEqual(o.inputs, [])

    def test_generated_files_force(self):
        reader = self.reader("generated-files-force")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 3)
        for o in objs:
            self.assertIsInstance(o, GeneratedFile)
            self.assertEqual(o.force, "bar.c" in o.outputs)

    def test_localized_generated_files(self):
        reader = self.reader("localized-generated-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 2)
        for o in objs:
            self.assertIsInstance(o, GeneratedFile)
            self.assertTrue(o.localized)

        expected = ["abc.ini", ("bar", "baz")]
        for o, f in zip(objs, expected):
            expected_filename = f if isinstance(f, tuple) else (f,)
            self.assertEqual(o.outputs, expected_filename)
            self.assertEqual(o.script, None)
            self.assertEqual(o.method, None)
            self.assertEqual(o.inputs, [])

    def test_localized_generated_files_force(self):
        reader = self.reader("localized-generated-files-force")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 2)
        for o in objs:
            self.assertIsInstance(o, GeneratedFile)
            self.assertTrue(o.localized)
            self.assertEqual(o.force, "abc.ini" in o.outputs)

    def test_localized_files_from_generated(self):
        """Test that using LOCALIZED_GENERATED_FILES and then putting the output in
        LOCALIZED_FILES as an objdir path works.
        """
        reader = self.reader("localized-files-from-generated")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 2)
        self.assertIsInstance(objs[0], GeneratedFile)
        self.assertIsInstance(objs[1], LocalizedFiles)

    def test_localized_files_not_localized_generated(self):
        """Test that using GENERATED_FILES and then putting the output in
        LOCALIZED_FILES as an objdir path produces an error.
        """
        reader = self.reader("localized-files-not-localized-generated")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Objdir file listed in LOCALIZED_FILES not in LOCALIZED_GENERATED_FILES:",
        ):
            self.read_topsrcdir(reader)

    def test_localized_generated_files_final_target_files(self):
        """Test that using LOCALIZED_GENERATED_FILES and then putting the output in
        FINAL_TARGET_FILES as an objdir path produces an error.
        """
        reader = self.reader("localized-generated-files-final-target-files")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Outputs of LOCALIZED_GENERATED_FILES cannot be used in FINAL_TARGET_FILES:",
        ):
            self.read_topsrcdir(reader)

    def test_build_static_lib_archive(self):
        """Test that BUILD_STATIC_LIB_ARCHIVE builds an archive without
        changing consumer linkage."""
        reader = self.reader("build-static-lib-archive")
        objs = self.read_topsrcdir(reader)

        libraries = [o for o in objs if isinstance(o, StaticLibrary)]
        self.assertEqual(len(libraries), 1)
        self.assertTrue(libraries[0].build_static_lib_archive)
        self.assertFalse(libraries[0].no_expand_lib)

    def test_build_static_lib_archive_non_static(self):
        """Test that BUILD_STATIC_LIB_ARCHIVE requires a static library."""
        reader = self.reader("build-static-lib-archive-non-static")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "BUILD_STATIC_LIB_ARCHIVE can only be set for static libraries.",
        ):
            self.read_topsrcdir(reader)

    def test_generated_files_method_names(self):
        reader = self.reader("generated-files-method-names")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 2)
        for o in objs:
            self.assertIsInstance(o, GeneratedFile)

        expected = ["bar.c", "foo.c"]
        expected_method_names = ["make_bar", "main"]
        for o, expected_filename, expected_method in zip(
            objs, expected, expected_method_names
        ):
            self.assertEqual(o.outputs, (expected_filename,))
            self.assertEqual(o.method, expected_method)
            self.assertEqual(o.inputs, [])

    def test_generated_files_absolute_script(self):
        reader = self.reader("generated-files-absolute-script")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)

        o = objs[0]
        self.assertIsInstance(o, GeneratedFile)
        self.assertEqual(o.outputs, ("bar.c",))
        self.assertRegex(o.script, "script.py$")
        self.assertEqual(o.method, "make_bar")
        self.assertEqual(o.inputs, [])

    def test_generated_files_no_script(self):
        reader = self.reader("generated-files-no-script")
        with self.assertRaisesRegex(
            SandboxValidationError, "Script for generating bar.c does not exist"
        ):
            self.read_topsrcdir(reader)

    def test_generated_files_no_inputs(self):
        reader = self.reader("generated-files-no-inputs")
        with self.assertRaisesRegex(
            SandboxValidationError, "Input for generating foo.c does not exist"
        ):
            self.read_topsrcdir(reader)

    def test_generated_files_no_python_script(self):
        reader = self.reader("generated-files-no-python-script")
        with self.assertRaisesRegex(
            SandboxValidationError, "Script for generating bar.c does not end in .py"
        ):
            self.read_topsrcdir(reader)

    def test_generated_files_extra_deps(self):
        """Test that GENERATED_FILES extra_deps is properly parsed."""
        reader = self.reader("generated-files-extra-deps")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        o = objs[0]
        self.assertIsInstance(o, GeneratedFile)
        self.assertEqual(o.outputs, ("foo.c",))
        self.assertEqual(o.inputs, [])
        self.assertEqual(len(o.extra_deps), 1)
        self.assertIsInstance(o.extra_deps[0], ObjDirPath)
        self.assertEqual(o.extra_deps[0], "!/generated-header.h")

    def test_generated_files_extra_deps_missing(self):
        """Test that a missing extra_deps source is an error."""
        reader = self.reader("generated-files-extra-deps-missing")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "extra_dep for generating foo.c does not exist",
        ):
            self.read_topsrcdir(reader)

    def test_exports(self):
        reader = self.reader("exports")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], Exports)

        expected = [
            ("", ["foo.h", "bar.h", "baz.h"]),
            ("mozilla", ["mozilla1.h", "mozilla2.h"]),
            ("mozilla/dom", ["dom1.h", "dom2.h", "dom3.h"]),
            ("mozilla/gfx", ["gfx.h"]),
            ("nspr/private", ["pprio.h", "pprthred.h"]),
            ("vpx", ["mem.h", "mem2.h"]),
        ]
        for (expect_path, expect_headers), (actual_path, actual_headers) in zip(
            expected, [(path, list(seq)) for path, seq in objs[0].files.walk()]
        ):
            self.assertEqual(expect_path, actual_path)
            self.assertEqual(expect_headers, actual_headers)

    def test_exports_missing(self):
        """
        Missing files in EXPORTS is an error.
        """
        reader = self.reader("exports-missing")
        with self.assertRaisesRegex(
            SandboxValidationError, "File listed in EXPORTS does not exist:"
        ):
            self.read_topsrcdir(reader)

    def test_exports_missing_generated(self):
        """
        An objdir file in EXPORTS that is not in GENERATED_FILES is an error.
        """
        reader = self.reader("exports-missing-generated")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Objdir file listed in EXPORTS not in GENERATED_FILES:",
        ):
            self.read_topsrcdir(reader)

    def test_exports_generated(self):
        reader = self.reader("exports-generated")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 2)
        self.assertIsInstance(objs[0], GeneratedFile)
        self.assertIsInstance(objs[1], Exports)
        exports = [(path, list(seq)) for path, seq in objs[1].files.walk()]
        self.assertEqual(
            exports, [("", ["foo.h"]), ("mozilla", ["mozilla1.h", "!mozilla2.h"])]
        )
        path, files = exports[1]
        self.assertIsInstance(files[1], ObjDirPath)

    def test_test_harness_files(self):
        reader = self.reader("test-harness-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], TestHarnessFiles)

        expected = {
            "mochitest": ["runtests.py", "utils.py"],
            "testing/mochitest": ["mochitest.py", "mochitest.toml"],
        }

        for path, strings in objs[0].files.walk():
            self.assertTrue(path in expected)
            basenames = sorted(mozpath.basename(s) for s in strings)
            self.assertEqual(sorted(expected[path]), basenames)

    def test_test_harness_files_root(self):
        reader = self.reader("test-harness-files-root")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Cannot install files to the root of TEST_HARNESS_FILES",
        ):
            self.read_topsrcdir(reader)

    def test_program(self):
        reader = self.reader("program")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 6)
        self.assertIsInstance(objs[0], Sources)
        self.assertIsInstance(objs[1], ComputedFlags)
        self.assertIsInstance(objs[2], ComputedFlags)
        self.assertIsInstance(objs[3], Program)
        self.assertIsInstance(objs[4], SimpleProgram)
        self.assertIsInstance(objs[5], SimpleProgram)

        self.assertEqual(objs[3].program, "test_program.prog")
        self.assertEqual(objs[4].program, "test_program1.prog")
        self.assertEqual(objs[5].program, "test_program2.prog")

        self.assertEqual(objs[3].name, "test_program.prog")
        self.assertEqual(objs[4].name, "test_program1.prog")
        self.assertEqual(objs[5].name, "test_program2.prog")

        self.assertEqual(
            objs[4].objs,
            [
                mozpath.join(
                    reader.config.topobjdir,
                    "test_program1.%s" % reader.config.substs["OBJ_SUFFIX"],
                )
            ],
        )
        self.assertEqual(
            objs[5].objs,
            [
                mozpath.join(
                    reader.config.topobjdir,
                    "test_program2.%s" % reader.config.substs["OBJ_SUFFIX"],
                )
            ],
        )

    def test_program_paths(self):
        """Various moz.build settings that change the destination of PROGRAM should be
        accurately reflected in Program.output_path."""
        reader = self.reader("program-paths")
        objs = self.read_topsrcdir(reader)
        prog_paths = [o.output_path for o in objs if isinstance(o, Program)]
        self.assertEqual(
            prog_paths,
            [
                "!/dist/bin/dist-bin.prog",
                "!/dist/bin/foo/dist-subdir.prog",
                "!/final/target/final-target.prog",
                "!not-installed.prog",
            ],
        )

    def test_shared_lib_paths(self):
        """Various moz.build settings that change the destination of SHARED_LIBRARY
        should be accurately reflected in Program.output_path."""
        reader = self.reader("shared-lib-paths")
        objs = self.read_topsrcdir(reader)
        prog_paths = [o.output_path for o in objs if isinstance(o, SharedLibrary)]
        self.assertEqual(
            prog_paths,
            [
                "!/dist/bin/libdist-bin.so",
                "!/dist/bin/foo/libdist-subdir.so",
                "!/final/target/libfinal-target.so",
                "!libnot-installed.so",
            ],
        )

    def test_host_program_paths(self):
        """The destination of a HOST_PROGRAM (almost always dist/host/bin)
        should be accurately reflected in Program.output_path."""
        reader = self.reader("host-program-paths")
        objs = self.read_topsrcdir(reader)
        prog_paths = [o.output_path for o in objs if isinstance(o, HostProgram)]
        self.assertEqual(
            prog_paths,
            [
                "!/dist/host/bin/final-target.hostprog",
                "!/dist/host/bin/dist-host-bin.hostprog",
                "!not-installed.hostprog",
            ],
        )

    def test_test_manifest_missing_manifest(self):
        """A missing manifest file should result in an error."""
        reader = self.reader("test-manifest-missing-manifest")

        with self.assertRaisesRegex(BuildReaderError, "Missing files"):
            self.read_topsrcdir(reader)

    def test_empty_test_manifest_rejected(self):
        """A test manifest without any entries is rejected."""
        reader = self.reader("test-manifest-empty")

        with self.assertRaisesRegex(SandboxValidationError, "Empty test manifest"):
            self.read_topsrcdir(reader)

    def test_test_manifest_just_support_files(self):
        """A test manifest with no tests but support-files is not supported."""
        reader = self.reader("test-manifest-just-support")

        with self.assertRaisesRegex(SandboxValidationError, "Empty test manifest"):
            self.read_topsrcdir(reader)

    def test_test_manifest_dupe_support_files(self):
        """A test manifest with dupe support-files in a single test is not
        supported.
        """
        reader = self.reader("test-manifest-dupes")

        with self.assertRaisesRegex(
            SandboxValidationError,
            "bar.js appears multiple times in a test manifest under a support-files field, please omit the duplicate entry.",
        ):
            self.read_topsrcdir(reader)

    def test_test_manifest_absolute_support_files(self):
        """Support files starting with '/' are placed relative to the install root"""
        reader = self.reader("test-manifest-absolute-support")

        objs = self.read_topsrcdir(reader)
        self.assertEqual(len(objs), 1)
        o = objs[0]
        self.assertEqual(len(o.installs), 3)
        expected = [
            mozpath.normpath(mozpath.join(o.install_prefix, "../.well-known/foo.txt")),
            mozpath.join(o.install_prefix, "absolute-support.toml"),
            mozpath.join(o.install_prefix, "test_file.js"),
        ]
        paths = sorted([v[0] for v in o.installs.values()])
        self.assertEqual(paths, expected)

    @unittest.skip("Bug 1304316 - Items in the second set but not the first")
    def test_test_manifest_shared_support_files(self):
        """Support files starting with '!' are given separate treatment, so their
        installation can be resolved when running tests.
        """
        reader = self.reader("test-manifest-shared-support")
        supported, child = self.read_topsrcdir(reader)

        expected_deferred_installs = {
            "!/child/test_sub.js",
            "!/child/another-file.sjs",
            "!/child/data/**",
        }

        self.assertEqual(len(supported.installs), 3)
        self.assertEqual(set(supported.deferred_installs), expected_deferred_installs)
        self.assertEqual(len(child.installs), 3)
        self.assertEqual(len(child.pattern_installs), 1)

    def test_test_manifest_deffered_install_missing(self):
        """A non-existent shared support file reference produces an error."""
        reader = self.reader("test-manifest-shared-missing")

        with self.assertRaisesRegex(
            SandboxValidationError, "entry in support-files not present in the srcdir"
        ):
            self.read_topsrcdir(reader)

    def test_test_manifest_install_includes(self):
        """Ensure that any [include:foo.toml] are copied to the objdir."""
        reader = self.reader("test-manifest-install-includes")

        objs = self.read_topsrcdir(reader)
        self.assertEqual(len(objs), 1)
        o = objs[0]
        self.assertEqual(len(o.installs), 3)
        self.assertEqual(o.manifest_relpath, "mochitest.toml")
        self.assertEqual(o.manifest_obj_relpath, "mochitest.toml")
        expected = [
            mozpath.normpath(mozpath.join(o.install_prefix, "common.toml")),
            mozpath.normpath(mozpath.join(o.install_prefix, "mochitest.toml")),
            mozpath.normpath(mozpath.join(o.install_prefix, "test_foo.html")),
        ]
        paths = sorted([v[0] for v in o.installs.values()])
        self.assertEqual(paths, expected)

    def test_test_manifest_includes(self):
        """Ensure that manifest objects from the emitter list a correct manifest."""
        reader = self.reader("test-manifest-emitted-includes")
        [obj] = self.read_topsrcdir(reader)

        # Expected manifest leafs for our tests.
        expected_manifests = {
            "reftest1.html": "reftest.list",
            "reftest1-ref.html": "reftest.list",
            "reftest2.html": "included-reftest.list",
            "reftest2-ref.html": "included-reftest.list",
        }

        for t in obj.tests:
            self.assertTrue(t["manifest"].endswith(expected_manifests[t["name"]]))

    def test_test_manifest_keys_extracted(self):
        """Ensure all metadata from test manifests is extracted."""
        reader = self.reader("test-manifest-keys-extracted")

        objs = [o for o in self.read_topsrcdir(reader) if isinstance(o, TestManifest)]

        self.assertEqual(len(objs), 8)

        metadata = {
            "a11y.toml": {
                "flavor": "a11y",
                "installs": {"a11y.toml": False, "test_a11y.js": True},
                "pattern-installs": 1,
            },
            "browser.toml": {
                "flavor": "browser-chrome",
                "installs": {
                    "browser.toml": False,
                    "test_browser.js": True,
                    "support1": False,
                    "support2": False,
                },
            },
            "mochitest.toml": {
                "flavor": "mochitest",
                "installs": {"mochitest.toml": False, "test_mochitest.js": True},
                "external": {"external1", "external2"},
            },
            "chrome.toml": {
                "flavor": "chrome",
                "installs": {"chrome.toml": False, "test_chrome.js": True},
            },
            "xpcshell.toml": {
                "flavor": "xpcshell",
                "dupe": True,
                "installs": {
                    "xpcshell.toml": False,
                    "test_xpcshell.js": True,
                    "head1": False,
                    "head2": False,
                },
            },
            "reftest.list": {"flavor": "reftest", "installs": {}},
            "crashtest.list": {"flavor": "crashtest", "installs": {}},
            "python.toml": {"flavor": "python", "installs": {"python.toml": False}},
        }

        for o in objs:
            m = metadata[mozpath.basename(o.manifest_relpath)]

            self.assertTrue(o.path.startswith(o.directory))
            self.assertEqual(o.flavor, m["flavor"])
            self.assertEqual(o.dupe_manifest, m.get("dupe", False))

            external_normalized = set(mozpath.basename(p) for p in o.external_installs)
            self.assertEqual(external_normalized, m.get("external", set()))

            self.assertEqual(len(o.installs), len(m["installs"]))
            for path in o.installs.keys():
                self.assertTrue(path.startswith(o.directory))
                relpath = path[len(o.directory) + 1 :]

                self.assertIn(relpath, m["installs"])
                self.assertEqual(o.installs[path][1], m["installs"][relpath])

            if "pattern-installs" in m:
                self.assertEqual(len(o.pattern_installs), m["pattern-installs"])

    def test_test_manifest_unmatched_generated(self):
        reader = self.reader("test-manifest-unmatched-generated")

        with self.assertRaisesRegex(
            SandboxValidationError, "entry in generated-files not present elsewhere"
        ):
            (self.read_topsrcdir(reader),)

    def test_test_manifest_parent_support_files_dir(self):
        """support-files referencing a file in a parent directory works."""
        reader = self.reader("test-manifest-parent-support-files-dir")

        objs = [o for o in self.read_topsrcdir(reader) if isinstance(o, TestManifest)]

        self.assertEqual(len(objs), 1)

        o = objs[0]

        expected = mozpath.join(o.srcdir, "support-file.txt")
        self.assertIn(expected, o.installs)
        self.assertEqual(
            o.installs[expected],
            ("testing/mochitest/tests/child/support-file.txt", False),
        )

    def test_test_manifest_missing_test_error(self):
        """Missing test files should result in error."""
        reader = self.reader("test-manifest-missing-test-file")

        with self.assertRaisesRegex(
            SandboxValidationError, "lists test that does not exist: test_missing.html"
        ):
            self.read_topsrcdir(reader)

    def test_test_manifest_missing_test_error_unfiltered(self):
        """Missing test files should result in error, even when the test list is not filtered."""
        reader = self.reader("test-manifest-missing-test-file-unfiltered")

        with self.assertRaisesRegex(
            SandboxValidationError, "lists test that does not exist: missing.js"
        ):
            self.read_topsrcdir(reader)

    def test_ipdl_sources(self):
        reader = self.reader(
            "ipdl_sources",
            extra_substs={"IPDL_ROOT": mozpath.abspath("/path/to/topobjdir")},
        )
        objs = self.read_topsrcdir(reader)
        ipdl_collection = objs[0]
        self.assertIsInstance(ipdl_collection, IPDLCollection)

        ipdls = set(
            mozpath.relpath(p, ipdl_collection.topsrcdir)
            for p in ipdl_collection.all_regular_sources()
        )
        expected = set([
            "bar/bar.ipdl",
            "bar/bar2.ipdlh",
            "foo/foo.ipdl",
            "foo/foo2.ipdlh",
        ])

        self.assertEqual(ipdls, expected)

        pp_ipdls = set(
            mozpath.relpath(p, ipdl_collection.topsrcdir)
            for p in ipdl_collection.all_preprocessed_sources()
        )
        expected = set(["bar/bar1.ipdl", "foo/foo1.ipdl"])
        self.assertEqual(pp_ipdls, expected)

    def test_local_includes(self):
        """Test that LOCAL_INCLUDES is emitted correctly."""
        reader = self.reader("local_includes")
        objs = self.read_topsrcdir(reader)

        local_includes = [o.path for o in objs if isinstance(o, LocalInclude)]
        expected = ["/bar/baz", "foo"]

        self.assertEqual(local_includes, expected)

        local_includes = [o.path.full_path for o in objs if isinstance(o, LocalInclude)]
        expected = [
            mozpath.join(reader.config.topsrcdir, "bar/baz"),
            mozpath.join(reader.config.topsrcdir, "foo"),
        ]

        self.assertEqual(local_includes, expected)

    def test_local_includes_invalid(self):
        """Test that invalid LOCAL_INCLUDES are properly detected."""
        reader = self.reader("local_includes-invalid/srcdir")

        with self.assertRaisesRegex(
            SandboxValidationError,
            "Path specified in LOCAL_INCLUDES.*resolves to the topsrcdir or topobjdir",
        ):
            self.read_topsrcdir(reader)

        reader = self.reader("local_includes-invalid/objdir")

        with self.assertRaisesRegex(
            SandboxValidationError,
            "Path specified in LOCAL_INCLUDES.*resolves to the topsrcdir or topobjdir",
        ):
            self.read_topsrcdir(reader)

    def test_local_includes_file(self):
        """Test that a filename can't be used in LOCAL_INCLUDES."""
        reader = self.reader("local_includes-filename")

        with self.assertRaisesRegex(
            SandboxValidationError, "Path specified in LOCAL_INCLUDES is a filename"
        ):
            self.read_topsrcdir(reader)

    def test_generated_includes(self):
        """Test that GENERATED_INCLUDES is emitted correctly."""
        reader = self.reader("generated_includes")
        objs = self.read_topsrcdir(reader)

        generated_includes = [o.path for o in objs if isinstance(o, LocalInclude)]
        expected = ["!/bar/baz", "!foo"]

        self.assertEqual(generated_includes, expected)

        generated_includes = [
            o.path.full_path for o in objs if isinstance(o, LocalInclude)
        ]
        expected = [
            mozpath.join(reader.config.topobjdir, "bar/baz"),
            mozpath.join(reader.config.topobjdir, "foo"),
        ]

        self.assertEqual(generated_includes, expected)

    def test_defines(self):
        reader = self.reader("defines")
        objs = self.read_topsrcdir(reader)

        defines = {}
        for o in objs:
            if isinstance(o, Defines):
                defines = o.defines

        expected = {
            "BAR": 7,
            "BAZ": '"abcd"',
            "FOO": True,
            "VALUE": "xyz",
            "QUX": False,
        }

        self.assertEqual(defines, expected)

    def test_jar_manifests(self):
        """An application tree jar manifest becomes ordinary installs."""
        reader = self.reader("jar-manifests")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        obj = objs[0]
        self.assertIsInstance(obj, FinalTargetFiles)
        self.assertEqual(obj.install_target, "dist/bin")
        self.assertEqual(
            [dest for dest, _ in obj.files.walk() if dest],
            ["chrome/test/content/test"],
        )

    def test_jar_manifests_pp_files_extra_deps(self):
        """A preprocessed jar entry reads what PP_FILES_EXTRA_DEPS names."""
        reader = self.reader("jar-manifests-pp-extra-deps")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        obj = objs[0]
        self.assertIsInstance(obj, FinalTargetPreprocessedFiles)
        self.assertEqual(
            [mozpath.basename(d.full_path) for d in obj.extra_deps], ["generated.h"]
        )

    def test_jar_manifests_non_application(self):
        """Any install target is expanded, not only the application tree."""
        reader = self.reader("jar-manifests-non-application")
        objs = self.read_topsrcdir(reader)

        files = [o for o in objs if isinstance(o, FinalTargetFiles)]
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0].install_target, "_tests/jar-manifests")

    def test_jar_manifests_pattern_in_subdir(self):
        """A pattern resolves beside the jar.mn, like a plain source."""
        reader = self.reader("jar-manifests-subdirectory")
        objs = self.read_topsrcdir(reader)

        files = [o for o in objs if isinstance(o, FinalTargetFiles)]
        self.assertEqual(len(files), 1)
        sources = [f for _, entries in files[0].files.walk() for f in entries]
        self.assertEqual(len(sources), 1)
        self.assertEqual(
            mozpath.normsep(sources[0].full_path),
            mozpath.join(mozpath.normsep(reader.config.topsrcdir), "sub/assets/**"),
        )

    def test_jar_manifests_empty(self):
        """A jar.mn with no section is still a build input."""
        reader = self.reader("jar-manifests-empty")
        objs = self.read_topsrcdir(reader, filter_common=False)

        recorded = set()
        for obj in objs:
            recorded |= {mozpath.normsep(p) for p in obj.context_all_paths}
        self.assertIn(
            mozpath.join(mozpath.normsep(reader.config.topsrcdir), "jar.mn"), recorded
        )

    def test_jar_manifests_multiple_files(self):
        with self.assertRaisesRegex(SandboxValidationError, "limited to one value"):
            reader = self.reader("jar-manifests-multiple-files")
            self.read_topsrcdir(reader)

    def test_xpidl_module_no_sources(self):
        """XPIDL_MODULE without XPIDL_SOURCES should be rejected."""
        with self.assertRaisesRegex(
            SandboxValidationError, "XPIDL_MODULE cannot be defined"
        ):
            reader = self.reader("xpidl-module-no-sources")
            self.read_topsrcdir(reader)

    def test_xpidl_module_missing_sources(self):
        """Missing XPIDL_SOURCES should be rejected."""
        with self.assertRaisesRegex(
            SandboxValidationError, "File .* from XPIDL_SOURCES does not exist"
        ):
            reader = self.reader("missing-xpidl")
            self.read_topsrcdir(reader)

    def test_missing_local_includes(self):
        """LOCAL_INCLUDES containing non-existent directories should be rejected."""
        with self.assertRaisesRegex(
            SandboxValidationError, "Path specified in LOCAL_INCLUDES does not exist"
        ):
            reader = self.reader("missing-local-includes")
            self.read_topsrcdir(reader)

    def test_library_defines(self):
        """Test that LIBRARY_DEFINES is propagated properly."""
        reader = self.reader("library-defines")
        objs = self.read_topsrcdir(reader)

        libraries = [o for o in objs if isinstance(o, StaticLibrary)]
        library_flags = [
            o
            for o in objs
            if isinstance(o, ComputedFlags) and "LIBRARY_DEFINES" in o.flags
        ]
        expected = {
            "liba": "-DIN_LIBA",
            "libb": "-DIN_LIBB -DIN_LIBA",
            "libc": "-DIN_LIBA -DIN_LIBB",
            "libd": "",
        }
        defines = {}
        for lib in libraries:
            defines[lib.basename] = " ".join(lib.lib_defines.get_defines())
        self.assertEqual(expected, defines)
        defines_in_flags = {}
        for flags in library_flags:
            defines_in_flags[flags.relobjdir] = " ".join(
                flags.flags["LIBRARY_DEFINES"] or []
            )
        self.assertEqual(expected, defines_in_flags)

    def test_sources(self):
        """Test that SOURCES works properly."""
        reader = self.reader("sources")
        objs = self.read_topsrcdir(reader)

        as_flags = objs.pop()
        self.assertIsInstance(as_flags, ComputedFlags)
        computed_flags = objs.pop()
        self.assertIsInstance(computed_flags, ComputedFlags)
        # The third to last object is a Linkable.
        linkable = objs.pop()
        self.assertTrue(linkable.cxx_link)
        ld_flags = objs.pop()
        self.assertIsInstance(ld_flags, ComputedFlags)
        self.assertEqual(len(objs), 6)
        for o in objs:
            self.assertIsInstance(o, Sources)

        suffix_map = {obj.canonical_suffix: obj for obj in objs}
        self.assertEqual(len(suffix_map), 6)

        expected = {
            ".cpp": ["a.cpp", "b.cc", "c.cxx"],
            ".c": ["d.c"],
            ".m": ["e.m"],
            ".mm": ["f.mm"],
            ".S": ["g.S"],
            ".s": ["h.s", "i.asm"],
        }
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.files, [mozpath.join(reader.config.topsrcdir, f) for f in files]
            )

            for f in files:
                self.assertIn(
                    mozpath.join(
                        reader.config.topobjdir,
                        "%s.%s"
                        % (mozpath.splitext(f)[0], reader.config.substs["OBJ_SUFFIX"]),
                    ),
                    linkable.objs,
                )

    def test_sources_just_c(self):
        """Test that a linkable with no C++ sources doesn't have cxx_link set."""
        reader = self.reader("sources-just-c")
        objs = self.read_topsrcdir(reader)

        as_flags = objs.pop()
        self.assertIsInstance(as_flags, ComputedFlags)
        flags = objs.pop()
        self.assertIsInstance(flags, ComputedFlags)
        # The third to last object is a Linkable.
        linkable = objs.pop()
        self.assertFalse(linkable.cxx_link)

    def test_linkables_cxx_link(self):
        """Test that linkables transitively set cxx_link properly."""
        reader = self.reader("test-linkables-cxx-link")
        got_results = 0
        for obj in self.read_topsrcdir(reader):
            if isinstance(obj, SharedLibrary):
                if obj.basename == "cxx_shared":
                    self.assertEqual(
                        obj.name,
                        "%scxx_shared%s"
                        % (reader.config.dll_prefix, reader.config.dll_suffix),
                    )
                    self.assertTrue(obj.cxx_link)
                    got_results += 1
                elif obj.basename == "just_c_shared":
                    self.assertEqual(
                        obj.name,
                        "%sjust_c_shared%s"
                        % (reader.config.dll_prefix, reader.config.dll_suffix),
                    )
                    self.assertFalse(obj.cxx_link)
                    got_results += 1
        self.assertEqual(got_results, 2)

    def test_extra_link_deps(self):
        """Test that EXTRA_LINK_DEPS is properly parsed."""
        reader = self.reader("extra-link-deps")
        objs = self.read_topsrcdir(reader)
        programs = [o for o in objs if isinstance(o, Program)]
        self.assertEqual(len(programs), 1)
        deps = programs[0].extra_link_deps
        self.assertEqual([str(d) for d in deps], ["!generated.txt", "bar.txt"])
        self.assertIsInstance(deps[0], ObjDirPath)
        self.assertIsInstance(deps[1], SourcePath)

    def test_extra_link_deps_shared_library(self):
        """Test that EXTRA_LINK_DEPS is recorded on a shared library."""
        reader = self.reader("extra-link-deps-shared")
        objs = self.read_topsrcdir(reader)
        libs = [o for o in objs if isinstance(o, SharedLibrary)]
        self.assertEqual(len(libs), 1)
        deps = libs[0].extra_link_deps
        self.assertEqual([str(d) for d in deps], ["!generated.rsp"])
        self.assertIsInstance(deps[0], ObjDirPath)

    def test_extra_link_deps_no_linkable(self):
        """Test that EXTRA_LINK_DEPS without a linkable is an error."""
        reader = self.reader("extra-link-deps-no-linkable")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "EXTRA_LINK_DEPS is set but no program or shared library",
        ):
            self.read_topsrcdir(reader)

    def test_extra_link_deps_missing(self):
        """Test that EXTRA_LINK_DEPS srcdir entries must exist."""
        reader = self.reader("extra-link-deps-missing")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Path specified in EXTRA_LINK_DEPS does not exist",
        ):
            self.read_topsrcdir(reader)

    def test_generated_sources(self):
        """Test that GENERATED_SOURCES works properly."""
        reader = self.reader("generated-sources")
        objs = self.read_topsrcdir(reader)

        as_flags = objs.pop()
        self.assertIsInstance(as_flags, ComputedFlags)
        flags = objs.pop()
        self.assertIsInstance(flags, ComputedFlags)
        # The third to last object is a Linkable.
        linkable = objs.pop()
        self.assertTrue(linkable.cxx_link)
        flags = objs.pop()
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(len(objs), 6)

        generated_sources = [
            o for o in objs if isinstance(o, Sources) and o.generated_files
        ]
        self.assertEqual(len(generated_sources), 6)

        suffix_map = {obj.canonical_suffix: obj for obj in generated_sources}
        self.assertEqual(len(suffix_map), 6)

        expected = {
            ".cpp": ["a.cpp", "b.cc", "c.cxx"],
            ".c": ["d.c"],
            ".m": ["e.m"],
            ".mm": ["f.mm"],
            ".S": ["g.S"],
            ".s": ["h.s", "i.asm"],
        }
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.generated_files,
                [mozpath.join(reader.config.topobjdir, f) for f in files],
            )

            for f in files:
                self.assertIn(
                    mozpath.join(
                        reader.config.topobjdir,
                        "%s.%s"
                        % (mozpath.splitext(f)[0], reader.config.substs["OBJ_SUFFIX"]),
                    ),
                    linkable.objs,
                )

    def test_host_sources(self):
        """Test that HOST_SOURCES works properly."""
        reader = self.reader("host-sources")
        objs = self.read_topsrcdir(reader)

        # This objdir will generate target flags.
        flags = objs.pop()
        self.assertIsInstance(flags, ComputedFlags)
        # The second to last object is a Linkable
        linkable = objs.pop()
        self.assertTrue(linkable.cxx_link)
        # This objdir will also generate host flags.
        host_flags = objs.pop()
        self.assertIsInstance(host_flags, ComputedFlags)
        # and host ldflags,
        host_ldflags = objs.pop()
        self.assertIsInstance(host_ldflags, ComputedFlags)
        # ...and ldflags.
        ldflags = objs.pop()
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertEqual(len(objs), 2)
        for o in objs:
            self.assertIsInstance(o, HostSources)

        suffix_map = {obj.canonical_suffix: obj for obj in objs}
        self.assertEqual(len(suffix_map), 2)

        expected = {
            ".cpp": ["a.cpp", "b.cc", "c.cxx"],
            ".c": ["d.c"],
        }
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.files, [mozpath.join(reader.config.topsrcdir, f) for f in files]
            )

            for f in files:
                self.assertIn(
                    mozpath.join(
                        reader.config.topobjdir,
                        "host_%s.%s"
                        % (mozpath.splitext(f)[0], reader.config.substs["OBJ_SUFFIX"]),
                    ),
                    linkable.objs,
                )

    def test_wasm_sources(self):
        """Test that WASM_SOURCES works properly."""
        reader = self.reader(
            "wasm-sources", extra_substs={"WASM_CC": "clang", "WASM_CXX": "clang++"}
        )
        objs = list(self.read_topsrcdir(reader))

        # The second to last object is a linkable.
        linkable = objs[-2]
        # Other than that, we only care about the WasmSources objects.
        objs = objs[:2]
        for o in objs:
            self.assertIsInstance(o, WasmSources)

        suffix_map = {obj.canonical_suffix: obj for obj in objs}
        self.assertEqual(len(suffix_map), 2)

        expected = {".cpp": ["a.cpp", "b.cc", "c.cxx"], ".c": ["d.c"]}
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.files, [mozpath.join(reader.config.topsrcdir, f) for f in files]
            )
            for f in files:
                self.assertIn(
                    mozpath.join(
                        reader.config.topobjdir,
                        "%s.%s"
                        % (
                            mozpath.splitext(f)[0],
                            reader.config.substs["WASM_OBJ_SUFFIX"],
                        ),
                    ),
                    linkable.objs,
                )

    def test_unified_sources(self):
        """Test that UNIFIED_SOURCES works properly."""
        reader = self.reader("unified-sources")
        objs = self.read_topsrcdir(reader)

        # The last object is a ComputedFlags, the second to last a Linkable,
        # followed by ldflags, ignore them.
        linkable = objs[-2]
        objs = objs[:-3]
        self.assertEqual(len(objs), 3)
        for o in objs:
            self.assertIsInstance(o, UnifiedSources)

        suffix_map = {obj.canonical_suffix: obj for obj in objs}
        self.assertEqual(len(suffix_map), 3)

        expected = {
            ".cpp": ["bar.cxx", "foo.cpp", "quux.cc"],
            ".mm": ["objc1.mm", "objc2.mm"],
            ".c": ["c1.c", "c2.c"],
        }
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.files, [mozpath.join(reader.config.topsrcdir, f) for f in files]
            )

            # Unified sources are not required
            if sources.have_unified_mapping:
                for f in dict(sources.unified_source_mapping).keys():
                    self.assertIn(
                        mozpath.join(
                            reader.config.topobjdir,
                            "%s.%s"
                            % (
                                mozpath.splitext(f)[0],
                                reader.config.substs["OBJ_SUFFIX"],
                            ),
                        ),
                        linkable.objs,
                    )

    def test_unified_sources_non_unified(self):
        """Test that UNIFIED_SOURCES with FILES_PER_UNIFIED_FILE=1 works properly."""
        reader = self.reader("unified-sources-non-unified")
        objs = self.read_topsrcdir(reader)

        # The last object is a Linkable, the second to last ComputedFlags,
        # followed by ldflags, ignore them.
        objs = objs[:-3]
        self.assertEqual(len(objs), 3)
        for o in objs:
            self.assertIsInstance(o, UnifiedSources)

        suffix_map = {obj.canonical_suffix: obj for obj in objs}
        self.assertEqual(len(suffix_map), 3)

        expected = {
            ".cpp": ["bar.cxx", "foo.cpp", "quux.cc"],
            ".mm": ["objc1.mm", "objc2.mm"],
            ".c": ["c1.c", "c2.c"],
        }
        for suffix, files in expected.items():
            sources = suffix_map[suffix]
            self.assertEqual(
                sources.files, [mozpath.join(reader.config.topsrcdir, f) for f in files]
            )
            self.assertFalse(sources.have_unified_mapping)

    def test_object_conflicts(self):
        """Test that object name conflicts are detected."""
        reader = self.reader("object-conflicts/1")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Test.cpp from SOURCES would have the same object name as"
            " Test.c from SOURCES\\.",
        ):
            self.read_topsrcdir(reader)

        reader = self.reader("object-conflicts/2")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Test.cpp from SOURCES would have the same object name as"
            " subdir/Test.cpp from SOURCES\\.",
        ):
            self.read_topsrcdir(reader)

        reader = self.reader("object-conflicts/3")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Test.cpp from UNIFIED_SOURCES would have the same object name as"
            " Test.c from SOURCES in non-unified builds\\.",
        ):
            self.read_topsrcdir(reader)

        reader = self.reader("object-conflicts/4")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Test.cpp from UNIFIED_SOURCES would have the same object name as"
            " Test.c from UNIFIED_SOURCES in non-unified builds\\.",
        ):
            self.read_topsrcdir(reader)

    def test_xpi_pkgname_without_xpi_name(self):
        """An archive packages a staging area, and only XPI_NAME makes one."""
        reader = self.reader("xpi-pkgname-without-xpi-name")
        with self.assertRaisesRegex(
            SandboxValidationError, "XPI_PKGNAME set but XPI_NAME not set"
        ):
            self.read_topsrcdir(reader)

    def test_final_target_pp_files(self):
        """Test that FINAL_TARGET_PP_FILES works properly."""
        reader = self.reader("dist-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], FinalTargetPreprocessedFiles)

        # Ideally we'd test hierarchies, but that would just be testing
        # the HierarchicalStringList class, which we test separately.
        for path, files in objs[0].files.walk():
            self.assertEqual(path, "")
            self.assertEqual(len(files), 2)

            expected = {"install.rdf", "main.js"}
            for f in files:
                self.assertTrue(str(f) in expected)

    def test_missing_final_target_pp_files(self):
        """Test that FINAL_TARGET_PP_FILES with missing files throws errors."""
        with self.assertRaisesRegex(
            SandboxValidationError,
            "File listed in FINAL_TARGET_PP_FILES does not exist",
        ):
            reader = self.reader("dist-files-missing")
            self.read_topsrcdir(reader)

    def test_final_target_pp_files_non_srcdir(self):
        """Test that non-srcdir paths in FINAL_TARGET_PP_FILES throws errors."""
        reader = self.reader("final-target-pp-files-non-srcdir")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Only source directory paths allowed in FINAL_TARGET_PP_FILES:",
        ):
            self.read_topsrcdir(reader)

    def test_pp_files_extra_deps_without_pp_files(self):
        """PP_FILES_EXTRA_DEPS without any preprocessed files is an error."""
        reader = self.reader("pp-files-extra-deps-no-pp-files")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "PP_FILES_EXTRA_DEPS is set but no preprocessed files",
        ):
            self.read_topsrcdir(reader)

    def test_localized_files(self):
        """Test that LOCALIZED_FILES works properly."""
        reader = self.reader("localized-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], LocalizedFiles)

        for path, files in objs[0].files.walk():
            self.assertEqual(path, "foo")
            self.assertEqual(len(files), 3)

            expected = {"en-US/bar.ini", "en-US/code/*.js", "en-US/foo.js"}
            for f in files:
                self.assertTrue(str(f) in expected)

    def test_localized_files_no_en_us(self):
        """Test that LOCALIZED_FILES errors if a path does not start with
        `en-US/` or contain `locales/en-US/`."""
        reader = self.reader("localized-files-no-en-us")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "LOCALIZED_FILES paths must start with `en-US/` or contain `locales/en-US/`: foo.js",
        ):
            self.read_topsrcdir(reader)

    def test_localized_pp_files(self):
        """Test that LOCALIZED_PP_FILES works properly."""
        reader = self.reader("localized-pp-files")
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 1)
        self.assertIsInstance(objs[0], LocalizedPreprocessedFiles)

        for path, files in objs[0].files.walk():
            self.assertEqual(path, "foo")
            self.assertEqual(len(files), 2)

            expected = {"en-US/bar.ini", "en-US/foo.js"}
            for f in files:
                self.assertTrue(str(f) in expected)

    def test_mozsrc_files(self):
        """Test that MOZ_SRC_FILES automatically match objdir folders with the
        provided SourcePath."""
        reader = self.reader("moz-src-files")
        objs = self.read_topsrcdir(reader)
        self.assertIsInstance(objs[0], MozSrcFiles)

        map = {path: [str(f) for f in files] for path, files in objs[0].files.walk()}
        self.assertDictEqual(
            map,
            {
                "": ["file.txt"],
                "dir": ["dir/file1.txt", "dir/file2.txt"],
                "dir/subdir": ["dir/subdir/otherfile.txt"],
            },
        )

    def test_rust_library_no_cargo_toml(self):
        """Test that defining a RustLibrary without a Cargo.toml fails."""
        reader = self.reader("rust-library-no-cargo-toml")
        with self.assertRaisesRegex(SandboxValidationError, "No Cargo.toml file found"):
            self.read_topsrcdir(reader)

    def test_rust_library_name_mismatch(self):
        """Test that defining a RustLibrary that doesn't match Cargo.toml fails."""
        reader = self.reader("rust-library-name-mismatch")
        with self.assertRaisesRegex(
            SandboxValidationError, "library.*does not match Cargo.toml-defined package"
        ):
            self.read_topsrcdir(reader)

    def test_rust_library_no_lib_section(self):
        """Test that a RustLibrary Cargo.toml with no [lib] section fails."""
        reader = self.reader("rust-library-no-lib-section")
        with self.assertRaisesRegex(
            SandboxValidationError, "Cargo.toml for.* has no \\[lib\\] section"
        ):
            self.read_topsrcdir(reader)

    def test_rust_library_invalid_crate_type(self):
        """Test that a RustLibrary Cargo.toml has a permitted crate-type."""
        reader = self.reader("rust-library-invalid-crate-type")
        with self.assertRaisesRegex(
            SandboxValidationError, "crate-type.* must include 'staticlib'"
        ):
            self.read_topsrcdir(reader)

    def test_rust_library_invalid_cargo_crate_type(self):
        """Test that a RustLibrary is restricted to a static library."""
        reader = self.reader("rust-library-invalid-cargo-crate-type")
        with self.assertRaisesRegex(
            SandboxValidationError, "cargo_crate_type.* must be 'staticlib'"
        ):
            self.read_topsrcdir(reader)

    def test_rust_library_dash_folding(self):
        """Test that on-disk names of RustLibrary objects convert dashes to underscores."""
        reader = self.reader(
            "rust-library-dash-folding",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, lib, cflags = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(lib, RustLibrary)
        self.assertRegex(lib.lib_name, "random_crate")
        self.assertRegex(lib.import_name, "random_crate")
        self.assertRegex(lib.basename, "random-crate")

    def test_multiple_rust_libraries(self):
        """Test that linking multiple Rust libraries throws an error"""
        reader = self.reader(
            "multiple-rust-libraries",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc"),
        )
        with self.assertRaisesRegex(
            SandboxValidationError, "Cannot link the following Rust libraries"
        ):
            self.read_topsrcdir(reader)

    def test_rust_library_features(self):
        """Test that RustLibrary features are correctly emitted."""
        reader = self.reader(
            "rust-library-features",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, lib, cflags = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(lib, RustLibrary)
        self.assertEqual(lib.features, ["musthave", "cantlivewithout"])
        self.assertFalse(lib.no_lto)

    def test_rust_library_no_lto(self):
        """Test that a RustLibrary LTO opt out is correctly emitted."""
        reader = self.reader(
            "rust-library-no-lto",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, lib, cflags = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(lib, RustLibrary)
        self.assertTrue(lib.no_lto)

    def test_rust_library_duplicate_features(self):
        """Test that duplicate RustLibrary features are rejected."""
        reader = self.reader("rust-library-duplicate-features")
        with self.assertRaisesRegex(
            SandboxValidationError, "features for .* should not contain duplicates"
        ):
            self.read_topsrcdir(reader)

    def test_rust_program_features(self):
        """Test that RustProgram features are correctly emitted."""
        reader = self.reader(
            "rust-program-features",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc", BIN_SUFFIX=".exe"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, cflags, prog = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(prog, RustProgram)
        self.assertEqual(prog.features, ["musthave", "cantlivewithout"])

    def test_rust_program_duplicate_features(self):
        """Test that duplicate RustProgram features are rejected."""
        reader = self.reader("rust-program-duplicate-features")
        with self.assertRaisesRegex(
            SandboxValidationError, "features for .* should not contain duplicates"
        ):
            self.read_topsrcdir(reader)

    def test_rust_program_no_cargo_toml(self):
        """Test that specifying RUST_PROGRAMS without a Cargo.toml fails."""
        reader = self.reader("rust-program-no-cargo-toml")
        with self.assertRaisesRegex(SandboxValidationError, "No Cargo.toml file found"):
            self.read_topsrcdir(reader)

    def test_host_rust_program_no_cargo_toml(self):
        """Test that specifying HOST_RUST_PROGRAMS without a Cargo.toml fails."""
        reader = self.reader("host-rust-program-no-cargo-toml")
        with self.assertRaisesRegex(SandboxValidationError, "No Cargo.toml file found"):
            self.read_topsrcdir(reader)

    def test_rust_program_nonexistent_name(self):
        """Test that specifying RUST_PROGRAMS that don't exist in Cargo.toml
        correctly throws an error."""
        reader = self.reader("rust-program-nonexistent-name")
        with self.assertRaisesRegex(
            SandboxValidationError, "Cannot find Cargo.toml definition for"
        ):
            self.read_topsrcdir(reader)

    def test_host_rust_program_nonexistent_name(self):
        """Test that specifying HOST_RUST_PROGRAMS that don't exist in
        Cargo.toml correctly throws an error."""
        reader = self.reader("host-rust-program-nonexistent-name")
        with self.assertRaisesRegex(
            SandboxValidationError, "Cannot find Cargo.toml definition for"
        ):
            self.read_topsrcdir(reader)

    def test_rust_programs(self):
        """Test RUST_PROGRAMS emission."""
        reader = self.reader(
            "rust-programs",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc", BIN_SUFFIX=".exe"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, cflags, prog = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(prog, RustProgram)
        self.assertEqual(prog.name, "some")

    def test_host_rust_programs(self):
        """Test HOST_RUST_PROGRAMS emission."""
        reader = self.reader(
            "host-rust-programs",
            extra_substs=dict(
                RUST_HOST_TARGET="i686-pc-windows-msvc", HOST_BIN_SUFFIX=".exe"
            ),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        print(objs)
        ldflags, cflags, host_cflags, host_ldflags, prog = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(prog, HostRustProgram)
        self.assertEqual(prog.name, "some")

    def test_host_rust_libraries(self):
        """Test HOST_RUST_LIBRARIES emission."""
        reader = self.reader(
            "host-rust-libraries",
            extra_substs=dict(
                RUST_HOST_TARGET="i686-pc-windows-msvc", HOST_BIN_SUFFIX=".exe"
            ),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, lib, cflags = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(lib, HostRustLibrary)
        self.assertRegex(lib.lib_name, "host_lib")
        self.assertRegex(lib.import_name, "host_lib")

    def test_crate_dependency_path_resolution(self):
        """Test recursive dependencies resolve with the correct paths."""
        reader = self.reader(
            "crate-dependency-path-resolution",
            extra_substs=dict(RUST_TARGET="i686-pc-windows-msvc"),
        )
        objs = self.read_topsrcdir(reader)

        self.assertEqual(len(objs), 5)
        ldflags, host_cflags, host_ldflags, lib, cflags = objs
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(cflags, ComputedFlags)
        self.assertIsInstance(host_ldflags, ComputedFlags)
        self.assertIsInstance(host_cflags, ComputedFlags)
        self.assertIsInstance(lib, RustLibrary)

    def test_missing_workspace_hack(self):
        """Test detection of a missing workspace hack."""
        reader = self.reader("rust-no-workspace-hack")
        with self.assertRaisesRegex(
            SandboxValidationError, "doesn't contain the workspace hack"
        ):
            self.read_topsrcdir(reader)

    def test_old_workspace_hack(self):
        """Test detection of an old workspace hack."""
        reader = self.reader("rust-old-workspace-hack")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "needs an update to its mozilla-central-workspace-hack dependency",
        ):
            self.read_topsrcdir(reader)

    def test_install_shared_lib(self):
        """Test that we can install a shared library with TEST_HARNESS_FILES"""
        reader = self.reader("test-install-shared-lib")
        objs = self.read_topsrcdir(reader)
        self.assertIsInstance(objs[0], TestHarnessFiles)
        self.assertIsInstance(objs[1], VariablePassthru)
        self.assertIsInstance(objs[2], ComputedFlags)
        self.assertIsInstance(objs[3], SharedLibrary)
        self.assertIsInstance(objs[4], ComputedFlags)
        for path, files in objs[0].files.walk():
            for f in files:
                self.assertEqual(str(f), "!libfoo.so")
                self.assertEqual(path, "foo/bar")

    def test_symbols_file(self):
        """Test that SYMBOLS_FILE works"""
        reader = self.reader("test-symbols-file")
        genfile, ldflags, shlib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(genfile, GeneratedFile)
        self.assertIsInstance(flags, ComputedFlags)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(shlib, SharedLibrary)
        # This looks weird but MockConfig sets DLL_{PREFIX,SUFFIX} and
        # the reader method in this class sets OS_TARGET=WINNT.
        self.assertEqual(shlib.symbols_file, "libfoo.so.def")

    def test_symbols_file_objdir(self):
        """Test that a SYMBOLS_FILE in the objdir works"""
        reader = self.reader("test-symbols-file-objdir")
        genfile, ldflags, shlib, flags = self.read_topsrcdir(reader)
        self.assertIsInstance(genfile, GeneratedFile)
        self.assertEqual(
            genfile.script, mozpath.join(reader.config.topsrcdir, "foo.py")
        )
        self.assertIsInstance(flags, ComputedFlags)
        self.assertIsInstance(ldflags, ComputedFlags)
        self.assertIsInstance(shlib, SharedLibrary)
        self.assertEqual(shlib.symbols_file, "foo.symbols")

    def test_symbols_file_objdir_missing_generated(self):
        """Test that a SYMBOLS_FILE in the objdir that's missing
        from GENERATED_FILES is an error.
        """
        reader = self.reader("test-symbols-file-objdir-missing-generated")
        with self.assertRaisesRegex(
            SandboxValidationError,
            "Objdir file specified in SYMBOLS_FILE not in GENERATED_FILES:",
        ):
            self.read_topsrcdir(reader)

    def test_wasm_compile_flags(self):
        reader = self.reader(
            "wasm-compile-flags",
            extra_substs={"WASM_CC": "clang", "WASM_CXX": "clang++"},
        )
        flags = list(self.read_topsrcdir(reader))[2]
        self.assertIsInstance(flags, ComputedFlags)
        self.assertEqual(
            flags.flags["WASM_CFLAGS"], reader.config.substs["WASM_CFLAGS"]
        )
        self.assertEqual(
            flags.flags["MOZBUILD_WASM_CFLAGS"], ["-funroll-loops", "-wasm-arg"]
        )
        self.assertEqual(
            set(flags.flags["WASM_DEFINES"]),
            set(["-DFOO", '-DBAZ="abcd"', "-UQUX", "-DBAR=7", "-DVALUE=xyz"]),
        )

    def test_source_headers(self):
        reader = self.reader("reader-source-headers")
        objs = self.read_topsrcdir(reader)
        headers = [
            header for obj in objs if isinstance(obj, Headers) for header in obj.files
        ]
        self.assertEqual(
            headers,
            [
                mozpath.join(reader.config.topsrcdir, "exported.h"),
                mozpath.join(reader.config.topsrcdir, "header.h"),
                mozpath.join(reader.config.topsrcdir, "installed.h"),
            ],
        )

    def test_licenses(self):
        reader = self.reader("licenses")
        objs = self.read_topsrcdir(reader)

        notices = {o.id: o for o in objs if isinstance(o, DeclaredLicenseNotice)}
        coverage = [o for o in objs if isinstance(o, DeclaredLicensedPaths)]

        self.assertEqual(sorted(notices), ["MIT", "mylib"])

        mit = notices["MIT"]
        self.assertEqual(mit.title, "MIT License")
        self.assertEqual(mit.spdx, "MIT")
        self.assertEqual(mozpath.basename(mit.text_path), "mit.txt")
        self.assertIsNone(mit.notice)

        mylib = notices["mylib"]
        self.assertEqual(mylib.notice, "Copyright 2026 Somebody.")
        self.assertEqual(mylib.paths, ["extra/path"])
        self.assertEqual(mylib.acknowledgement, "Portions are copyright Somebody.")
        self.assertIsNone(mit.acknowledgement)

        self.assertEqual([o.id for o in coverage], ["MIT"])
        self.assertEqual(coverage[0].relsrcdir, "lib")
        # paths are resolved relative to the declaring moz.build
        self.assertEqual(coverage[0].paths, ["lib/vendor/dep.js", "lib/vendor/other*"])

    def test_licenses_missing_title(self):
        reader = self.reader("licenses-missing-title")
        with self.assertRaisesRegex(SandboxValidationError, "requires a title"):
            self.read_topsrcdir(reader)

    def test_licenses_missing_text_file(self):
        reader = self.reader("licenses-missing-text-file")
        with self.assertRaisesRegex(SandboxValidationError, "does not exist"):
            self.read_topsrcdir(reader)

    def test_licenses_no_text_file_at_all(self):
        # No `text`, and no moz.yaml above it naming one either.
        reader = self.reader("licenses-no-text")
        with self.assertRaisesRegex(SandboxValidationError, "requires a text file"):
            self.read_topsrcdir(reader)

    def test_licenses_declared_twice_in_one_file(self):
        reader = self.reader("licenses-declared-twice")
        with self.assertRaisesRegex(SandboxValidationError, "declared twice here"):
            self.read_topsrcdir(reader)


if __name__ == "__main__":
    main()
