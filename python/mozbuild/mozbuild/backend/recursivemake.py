# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os
import re
from collections import defaultdict, namedtuple
from functools import reduce
from io import StringIO
from itertools import chain
from operator import itemgetter
from textwrap import dedent

import mozpack.path as mozpath
from mozfile import json
from mozpack.manifests import InstallManifest
from mozshellutil import quote as shell_quote

from mozbuild import frontend
from mozbuild.backend.common import (
    add_preprocess_entry,
    chrome_manifest_contents,
    configured_defines,
    pattern_link_dest,
    pattern_link_parts,
    record_chrome_manifest_entry,
)
from mozbuild.frontend.context import (
    AbsolutePath,
    ObjDirPath,
    Path,
    SourcePath,
)

from ..frontend.data import (
    BaseLibrary,
    BaseProgram,
    BaseRustLibrary,
    ChromeManifestEntry,
    ComputedFlags,
    ConfigFileSubstitution,
    ContextDerived,
    Defines,
    DirectoryTraversal,
    ExternalLibrary,
    FinalTargetFiles,
    FinalTargetPreprocessedFiles,
    GeneratedFile,
    Headers,
    HostDefines,
    HostLibrary,
    HostProgram,
    HostRustProgram,
    HostSharedLibrary,
    HostSimpleProgram,
    HostSources,
    InstallationTarget,
    LegacyRunTests,
    Linkable,
    LocalInclude,
    LocalizedFiles,
    LocalizedPreprocessedFiles,
    MacOSBundle,
    ObjdirFiles,
    ObjdirPreprocessedFiles,
    PerSourceFlag,
    Program,
    RustProgram,
    RustTests,
    SandboxedWasmLibrary,
    SharedLibrary,
    SimpleProgram,
    Sources,
    StaticLibrary,
    TestManifest,
    VariablePassthru,
    WasmSources,
    XPIDLModule,
)
from ..makeutil import Makefile
from ..util import FileAvoidWrite, ensureParentDir, pairwise
from .common import CommonBackend
from .make import MakeBackend

# To protect against accidentally adding logic to Makefiles that belong in moz.build,
# we check if moz.build-like variables are defined in Makefiles. If they are, we throw
# an error to encourage the usage of moz.build instead.
_MOZBUILD_ONLY_VARIABLES = set(frontend.context.VARIABLES.keys()) - {
    # The migration to moz.build from Makefiles still isn't complete, and there's still
    # some straggling Makefile logic that uses variables that only moz.build should
    # use.
    # These remaining variables are excluded from our blacklist. As the variables here
    # are migrated from Makefiles in the future, they should be removed from this
    # "override" list.
    "CFLAGS",
    "CXXFLAGS",
}

DEPRECATED_VARIABLES = [
    "ALLOW_COMPILER_WARNINGS",
    "EXPORT_LIBRARY",
    "EXTRA_LIBS",
    "FAIL_ON_WARNINGS",
    "HOST_LIBS",
    "LIBXUL_LIBRARY",
    "MOCHITEST_A11Y_FILES",
    "MOCHITEST_BROWSER_FILES",
    "MOCHITEST_BROWSER_FILES_PARTS",
    "MOCHITEST_CHROME_FILES",
    "MOCHITEST_FILES",
    "MOCHITEST_FILES_PARTS",
    "MOCHITEST_METRO_FILES",
    "MOCHITEST_ROBOCOP_FILES",
    "MODULE_OPTIMIZE_FLAGS",
    "MOZ_CHROME_FILE_FORMAT",
    "SHORT_LIBNAME",
    "TESTING_JS_MODULES",
    "TESTING_JS_MODULE_DIR",
]

MOZBUILD_VARIABLES_MESSAGE = "It should only be defined in moz.build files."

DEPRECATED_VARIABLES_MESSAGE = (
    "This variable has been deprecated. It does nothing. It must be removed "
    "in order to build."
)


def make_quote(s):
    return s.replace("#", r"\#").replace("$", "$$")


class BackendMakeFile:
    """Represents a generated backend.mk file.

    This is both a wrapper around a file handle as well as a container that
    holds accumulated state.

    It's worth taking a moment to explain the make dependencies. The
    generated backend.mk as well as the Makefile.in (if it exists) are in the
    GLOBAL_DEPS list. This means that if one of them changes, all targets
    in that Makefile are invalidated. backend.mk also depends on all of its
    input files.

    It's worth considering the effect of file mtimes on build behavior.

    Since we perform an "all or none" traversal of moz.build files (the whole
    tree is scanned as opposed to individual files), if we were to blindly
    write backend.mk files, the net effect of updating a single mozbuild file
    in the tree is all backend.mk files have new mtimes. This would in turn
    invalidate all make targets across the whole tree! This would effectively
    undermine incremental builds as any mozbuild change would cause the entire
    tree to rebuild!

    The solution is to not update the mtimes of backend.mk files unless they
    actually change. We use FileAvoidWrite to accomplish this.
    """

    def __init__(self, srcdir, objdir, environment, topsrcdir, topobjdir, dry_run):
        self.topsrcdir = topsrcdir
        self.srcdir = srcdir
        self.objdir = objdir
        self.relobjdir = mozpath.relpath(objdir, topobjdir)
        self.environment = environment
        self.name = mozpath.join(objdir, "backend.mk")

        self.xpt_name = None

        self.fh = FileAvoidWrite(self.name, capture_diff=True, dry_run=dry_run)
        self.fh.write("# THIS FILE WAS AUTOMATICALLY GENERATED. DO NOT EDIT.\n")
        self.fh.write("\n")

    def write(self, buf):
        self.fh.write(buf)

    def write_once(self, buf):
        if "\n" + buf not in self.fh.getvalue().decode():
            self.write(buf)

    # For compatibility with makeutil.Makefile
    def add_statement(self, stmt):
        self.write("%s\n" % stmt)

    def close(self):
        if self.xpt_name:
            # We just recompile all xpidls because it's easier and less error
            # prone.
            self.fh.write("NONRECURSIVE_TARGETS += export\n")
            self.fh.write("NONRECURSIVE_TARGETS_export += xpidl\n")
            self.fh.write(
                "NONRECURSIVE_TARGETS_export_xpidl_DIRECTORY = "
                "$(DEPTH)/config/makefiles/xpidl\n"
            )
            self.fh.write("NONRECURSIVE_TARGETS_export_xpidl_TARGETS += xpidl\n")

        return self.fh.close()

    @property
    def diff(self):
        return self.fh.diff


class RecursiveMakeTraversal:
    """
    Helper class to keep track of how the "traditional" recursive make backend
    recurses subdirectories. This is useful until all adhoc rules are removed
    from Makefiles.

    Each directory may have one or more types of subdirectories:
        - (normal) dirs
        - tests
    """

    SubDirectoryCategories = ["dirs", "tests"]
    SubDirectoriesTuple = namedtuple("SubDirectories", SubDirectoryCategories)

    class SubDirectories(SubDirectoriesTuple):
        def __new__(cls):
            return RecursiveMakeTraversal.SubDirectoriesTuple.__new__(cls, [], [])

    def __init__(self):
        self._traversal = {}
        self._attached = set()

    def add(self, dir, dirs=[], tests=[]):
        """
        Adds a directory to traversal, registering its subdirectories,
        sorted by categories. If the directory was already added to
        traversal, adds the new subdirectories to the already known lists.
        """
        subdirs = self._traversal.setdefault(dir, self.SubDirectories())
        for key, value in (("dirs", dirs), ("tests", tests)):
            assert key in self.SubDirectoryCategories
            # Callers give us generators
            value = list(value)
            getattr(subdirs, key).extend(value)
            self._attached |= set(value)

    @staticmethod
    def default_filter(current, subdirs):
        """
        Default filter for use with compute_dependencies and traverse.
        """
        return current, [], subdirs.dirs + subdirs.tests

    def call_filter(self, current, filter):
        """
        Helper function to call a filter from compute_dependencies and
        traverse.
        """
        return filter(current, self.get_subdirs(current))

    def compute_dependencies(self, filter=None):
        """
        Compute make dependencies corresponding to the registered directory
        traversal.

        filter is a function with the following signature:
            def filter(current, subdirs)

        where current is the directory being traversed, and subdirs the
        SubDirectories instance corresponding to it.
        The filter function returns a tuple (filtered_current, filtered_parallel,
        filtered_dirs) where filtered_current is either current or None if
        the current directory is to be skipped, and filtered_parallel and
        filtered_dirs are lists of parallel directories and sequential
        directories, which can be rearranged from whatever is given in the
        SubDirectories members.

        The default filter corresponds to a default recursive traversal.

        """
        filter = filter or self.default_filter

        deps = {}

        def recurse(start_node, prev_nodes=None):
            current, parallel, sequential = self.call_filter(start_node, filter)
            if current is not None:
                if start_node != "":
                    deps[start_node] = prev_nodes
                prev_nodes = (start_node,)
            if start_node not in self._traversal:
                return prev_nodes
            parallel_nodes = []
            for node in parallel:
                nodes = recurse(node, prev_nodes)
                if nodes and nodes != ("",):
                    parallel_nodes.extend(nodes)
            if parallel_nodes:
                prev_nodes = tuple(parallel_nodes)
            for dir in sequential:
                prev_nodes = recurse(dir, prev_nodes)
            return prev_nodes

        return recurse(""), deps

    def traverse(self, start, filter=None):
        """
        Iterate over the filtered subdirectories, following the traditional
        make traversal order.
        """
        if filter is None:
            filter = self.default_filter

        current, parallel, sequential = self.call_filter(start, filter)
        if current is not None:
            yield start
        if start not in self._traversal:
            return
        for node in parallel:
            yield from self.traverse(node, filter)
        for dir in sequential:
            yield from self.traverse(dir, filter)

    def get_subdirs(self, dir):
        """
        Returns all direct subdirectories under the given directory.
        """
        result = self._traversal.get(dir, self.SubDirectories())
        if dir == "":
            unattached = set(self._traversal) - self._attached - set([""])
            if unattached:
                new_result = self.SubDirectories()
                new_result.dirs.extend(result.dirs)
                new_result.dirs.extend(sorted(unattached))
                new_result.tests.extend(result.tests)
                result = new_result
        return result


class RecursiveMakeBackend(MakeBackend):
    """Backend that integrates with the existing recursive make build system.

    This backend facilitates the transition from Makefile.in to moz.build
    files.

    This backend performs Makefile.in -> Makefile conversion. It also writes
    out .mk files containing content derived from moz.build files. Both are
    consumed by the recursive make builder.

    This backend may eventually evolve to write out non-recursive make files.
    However, as long as there are Makefile.in files in the tree, we are tied to
    recursive make and thus will need this backend.
    """

    INSTALL_MANIFEST_BASES = ("dist/bin", "dist/xpi-stage", "_tests", "dist/include")
    BUILT_INSTALL_MANIFESTS = "_build_manifests/built"
    TARGETS = {
        "analysis-prereqs": ["pre-export", "export", "pre-compile"],
        "gtest": ["recurse_gtest"],
        "rust-prereqs": ["pre-export", "export"],
        "rusttests": ["recurse_rusttests"],
        "syms": ["recurse_syms"],
        "uniffi-target": ["recurse_uniffi-target"],
    }

    @classmethod
    def resolve_targets(cls, what):
        return [t for name in what for t in cls.TARGETS.get(name, [name])]

    def _init(self):
        MakeBackend._init(self)

        self._backend_files = {}
        self._idl_dirs = set()

        self._makefile_in_count = 0
        self._makefile_out_count = 0

        self._test_manifests = {}

        self.backend_input_files.add(
            mozpath.join(self.environment.topobjdir, "config", "autoconf.mk")
        )

        # Chrome registrations, keyed by the manifest they are written to.
        self._manifest_entries = defaultdict(set)
        self._install_manifests = defaultdict(InstallManifest)
        self._built_install_manifests = defaultdict(InstallManifest)
        self._staged_objdir_installs = defaultdict(list)
        # Generated files that preprocessed installs read, keyed by the
        # manifest that installs them.
        self._install_manifest_deps = defaultdict(set)
        # Objdir files an install reads, keyed like _install_manifest_deps, and
        # the generated outputs that have a rule in the top level backend file.
        # An install runs from the top level Makefile, so it can only depend on
        # the latter.
        self._install_manifest_objdir_deps = defaultdict(set)
        self._root_generated_outputs = set()
        self._configured_defines = configured_defines(self.environment)
        # The build system relies on some install manifests always existing
        # even if they are empty, because the directories are still filled
        # by the build system itself, and the install manifests are only
        # used for a "magic" rm -rf.
        self._install_manifests["dist_public"]
        self._install_manifests["dist_private"]

        self._traversal = RecursiveMakeTraversal()
        self._compile_graph = defaultdict(set)
        self._rust_targets = set()
        self._gkrust_target = None
        self._pre_compile = set()
        self._objdir_local_includes = defaultdict(set)

        # For a given file produced by the build, gives the top-level target
        # that will produce it.
        self._target_per_file = {}
        # A set of dependencies that need correlation after everything has
        # been processed.
        self._post_process_dependencies = []

        self._no_skip = {
            "pre-export": set(),
            "export": set(),
            "libs": set(),
            "misc": set(),
            "tools": set(),
            "check": set(),
            "syms": set(),
        }

    def summary(self):
        summary = super().summary()
        summary.extend(
            "; {makefile_in:d} -> {makefile_out:d} Makefile",
            makefile_in=self._makefile_in_count,
            makefile_out=self._makefile_out_count,
        )
        return summary

    def _get_backend_file_for(self, obj):
        # For generated files that we put in the export or misc tiers, we use
        # the top-level backend file, so that what reads one can depend on it
        # from the top level Makefile.
        if isinstance(obj, GeneratedFile) and not obj.required_during_compile:
            objdir = self.environment.topobjdir
        else:
            objdir = obj.objdir

        if objdir not in self._backend_files:
            self._backend_files[objdir] = BackendMakeFile(
                obj.srcdir,
                objdir,
                obj.config,
                obj.topsrcdir,
                self.environment.topobjdir,
                self.dry_run,
            )
        return self._backend_files[objdir]

    def consume_object(self, obj):
        """Write out build files necessary to build with recursive make."""

        if not isinstance(obj, ContextDerived):
            return False

        backend_file = self._get_backend_file_for(obj)

        consumed = CommonBackend.consume_object(self, obj)

        # CommonBackend handles XPIDLModule, but we want to do
        # some extra things for them.
        if isinstance(obj, XPIDLModule):
            backend_file.xpt_name = "%s.xpt" % obj.name
            self._idl_dirs.add(obj.relobjdir)

        # If CommonBackend acknowledged the object, we're done with it.
        if consumed:
            return True

        if not isinstance(obj, Defines):
            self.consume_object(obj.defines)

        if isinstance(obj, Linkable):
            self._process_test_support_file(obj)
            self._process_relrhack(obj)

        if isinstance(obj, DirectoryTraversal):
            self._process_directory_traversal(obj, backend_file)
        elif isinstance(obj, ConfigFileSubstitution):
            # Other ConfigFileSubstitution should have been acked by
            # CommonBackend.
            assert os.path.basename(obj.output_path) == "Makefile"
            self._create_makefile(obj)
        elif isinstance(obj, Sources):
            suffix_map = {
                ".s": "ASFILES",
                ".c": "CSRCS",
                ".m": "CMSRCS",
                ".mm": "CMMSRCS",
                ".cpp": "CPPSRCS",
                ".S": "SSRCS",
            }
            variables = [suffix_map[obj.canonical_suffix]]
            for files, base, cls, prefix in (
                (obj.static_files, backend_file.srcdir, SourcePath, ""),
                (obj.generated_files, backend_file.objdir, ObjDirPath, "!"),
            ):
                for f in sorted(files):
                    p = self._pretty_path(
                        cls(obj._context, prefix + mozpath.relpath(f, base)),
                        backend_file,
                    )
                    for var in variables:
                        backend_file.write("%s += %s\n" % (var, p))
            self._compile_graph[mozpath.join(backend_file.relobjdir, "target-objects")]
        elif isinstance(obj, HostSources):
            suffix_map = {
                ".c": "HOST_CSRCS",
                ".cpp": "HOST_CPPSRCS",
            }
            variables = [suffix_map[obj.canonical_suffix]]
            for files, base, cls, prefix in (
                (obj.static_files, backend_file.srcdir, SourcePath, ""),
                (obj.generated_files, backend_file.objdir, ObjDirPath, "!"),
            ):
                for f in sorted(files):
                    p = self._pretty_path(
                        cls(obj._context, prefix + mozpath.relpath(f, base)),
                        backend_file,
                    )
                    for var in variables:
                        backend_file.write("%s += %s\n" % (var, p))
            self._compile_graph[mozpath.join(backend_file.relobjdir, "host-objects")]
        elif isinstance(obj, WasmSources):
            suffix_map = {".c": "WASM_CSRCS", ".cpp": "WASM_CPPSRCS"}
            variables = [suffix_map[obj.canonical_suffix]]
            for files, base, cls, prefix in (
                (obj.static_files, backend_file.srcdir, SourcePath, ""),
                (obj.generated_files, backend_file.objdir, ObjDirPath, "!"),
            ):
                for f in sorted(files):
                    p = self._pretty_path(
                        cls(obj._context, prefix + mozpath.relpath(f, base)),
                        backend_file,
                    )
                    for var in variables:
                        backend_file.write("%s += %s\n" % (var, p))
            self._compile_graph[mozpath.join(backend_file.relobjdir, "target-objects")]
        elif isinstance(obj, VariablePassthru):
            # Sorted so output is consistent and we don't bump mtimes.
            for k, v in sorted(obj.variables.items()):
                if isinstance(v, list):
                    for item in v:
                        backend_file.write(
                            "%s += %s\n" % (k, make_quote(shell_quote(item)))
                        )
                elif isinstance(v, bool):
                    if v:
                        backend_file.write("%s := 1\n" % k)
                elif isinstance(v, Path):
                    path = self._pretty_path(Path(obj._context, v), backend_file)
                    backend_file.write("%s := %s\n" % (k, path))
                else:
                    backend_file.write("%s := %s\n" % (k, v))

        elif isinstance(obj, HostDefines):
            self._process_defines(obj, backend_file, which="HOST_DEFINES")
        elif isinstance(obj, Defines):
            self._process_defines(obj, backend_file)

        elif isinstance(obj, GeneratedFile):
            if obj.required_before_export:
                tier = "pre-export"
            elif obj.required_before_compile:
                tier = "export"
            elif obj.required_during_compile:
                tier = "pre-compile"
            else:
                tier = "misc"
            if tier == "pre-compile":
                self._pre_compile.add(obj.relobjdir)
            else:
                self._no_skip[tier].add(obj.relobjdir)
            backend_file.write_once("include $(topsrcdir)/config/AB_rCD.mk\n")
            reldir = mozpath.relpath(obj.objdir, backend_file.objdir)
            if not reldir:
                for out in obj.outputs:
                    target = mozpath.join(obj.objdir, out)
                    assert target not in self._target_per_file
                    self._target_per_file[target] = (obj.relobjdir, tier)
                for dep in chain(obj.inputs, obj.extra_deps):
                    if isinstance(dep, ObjDirPath):
                        self._post_process_dependencies.append((
                            obj.relobjdir,
                            tier,
                            dep,
                        ))
            if not obj.required_during_compile:
                for out in obj.outputs:
                    self._root_generated_outputs.add(mozpath.join(obj.objdir, out))
            # For generated files that we handle in the top-level backend file,
            # we want to have a `directory/tier` target depending on the file.
            # For the others, we want a `tier` target.
            if tier != "pre-compile" and reldir:
                tier = "%s/%s" % (reldir, tier)
            for stmt in self._format_statements_for_generated_file(
                obj, tier, extra_dependencies="backend.mk" if obj.flags else ""
            ):
                backend_file.write(stmt + "\n")

        elif isinstance(obj, MacOSBundle):
            self._process_macos_bundle(obj, backend_file)

        elif isinstance(obj, RustProgram):
            self._process_rust_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            # Hook the program into the compile graph.
            build_target = self._build_target_for_obj(obj)
            self._compile_graph[build_target]
            self._rust_targets.add(build_target)
            if not obj.output_category:
                self._no_skip["syms"].add(backend_file.relobjdir)

        elif isinstance(obj, HostRustProgram):
            self._process_host_rust_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            # Hook the program into the compile graph.
            build_target = self._build_target_for_obj(obj)
            self._compile_graph[build_target]
            self._rust_targets.add(build_target)

        elif isinstance(obj, RustTests):
            self._process_rust_tests(obj, backend_file)
            self._add_rust_build_order_deps(obj)

        elif isinstance(obj, LegacyRunTests):
            self._process_legacy_run_tests(obj, backend_file)

        elif isinstance(obj, Program):
            self._process_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            self._no_skip["syms"].add(backend_file.relobjdir)

        elif isinstance(obj, Headers):
            # nothing to do
            ...

        elif isinstance(obj, HostProgram):
            self._process_host_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            self._target_per_file[obj.output_path.full_path] = (
                backend_file.relobjdir,
                obj.KIND,
            )

        elif isinstance(obj, SimpleProgram):
            self._process_simple_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            self._no_skip["syms"].add(backend_file.relobjdir)

        elif isinstance(obj, HostSimpleProgram):
            self._process_host_simple_program(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)

        elif isinstance(obj, LocalInclude):
            self._process_local_include(obj.path, backend_file)
            if isinstance(obj.path, ObjDirPath):
                self._objdir_local_includes[backend_file.relobjdir].add(
                    mozpath.relpath(obj.path.full_path, self.environment.topobjdir)
                )

        elif isinstance(obj, PerSourceFlag):
            self._process_per_source_flag(obj, backend_file)

        elif isinstance(obj, ComputedFlags):
            self._process_computed_flags(obj, backend_file)

        elif isinstance(obj, InstallationTarget):
            self._process_installation_target(obj, backend_file)

        elif isinstance(obj, BaseRustLibrary):
            self.backend_input_files.add(obj.cargo_file)
            self._process_rust_library(obj, backend_file)

            # Hook the library into the compile graph.
            build_target = self._build_target_for_obj(obj)
            self._compile_graph[build_target]
            self._rust_targets.add(build_target)
            self._add_rust_build_order_deps(obj)
            assert obj.import_path.full_path not in self._target_per_file
            self._target_per_file[obj.import_path.full_path] = (
                backend_file.relobjdir,
                mozpath.basename(build_target),
            )

            if obj.is_gkrust:
                self._gkrust_target = build_target

        elif isinstance(obj, SharedLibrary):
            self._process_shared_library(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)
            if not obj.output_category:
                self._no_skip["syms"].add(backend_file.relobjdir)

        elif isinstance(obj, StaticLibrary):
            self._process_static_library(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)

        elif isinstance(obj, SandboxedWasmLibrary):
            self._process_sandboxed_wasm_library(obj, backend_file)

        elif isinstance(obj, HostLibrary):
            self._process_linked_libraries(obj, backend_file)

        elif isinstance(obj, HostSharedLibrary):
            self._process_host_shared_library(obj, backend_file)
            self._process_linked_libraries(obj, backend_file)

        elif isinstance(obj, ObjdirFiles):
            self._process_objdir_files(obj, obj.files, backend_file)

        elif isinstance(obj, ObjdirPreprocessedFiles):
            self._process_objdir_pp_files(obj, obj.files, backend_file)

        elif isinstance(obj, LocalizedFiles):
            self._process_localized_files(obj, obj.files)

        elif isinstance(obj, LocalizedPreprocessedFiles):
            self._process_localized_pp_files(obj, obj.files)

        elif isinstance(obj, FinalTargetFiles):
            self._process_final_target_files(obj, obj.files, backend_file)

        elif isinstance(obj, FinalTargetPreprocessedFiles):
            self._process_installed_pp_files(obj, obj.files)

        elif isinstance(obj, ChromeManifestEntry):
            record_chrome_manifest_entry(self._manifest_entries, obj)

        elif isinstance(obj, TestManifest):
            self._process_test_manifest(obj, backend_file)

        else:
            return False

        return True

    def _fill_root_mk(self):
        """
        Create two files, root.mk and root-deps.mk, the first containing
        convenience variables, and the other dependency definitions for a
        hopefully proper directory traversal.
        """
        for tier, no_skip in self._no_skip.items():
            self.log(
                logging.DEBUG,
                "fill_root_mk",
                {"number": len(no_skip), "tier": tier},
                "Using {number} directories during {tier}",
            )

        def should_skip(tier, dir):
            if tier in self._no_skip:
                return dir not in self._no_skip[tier]
            return False

        # Traverse directories in parallel, and skip static dirs
        def parallel_filter(current, subdirs):
            all_subdirs = subdirs.dirs + subdirs.tests
            if should_skip(tier, current) or current.startswith("subtiers/"):
                current = None
            return current, all_subdirs, []

        # build everything in parallel, including static dirs
        # Because of bug 925236 and possible other unknown race conditions,
        # don't parallelize the libs tier.
        def libs_filter(current, subdirs):
            if should_skip("libs", current) or current.startswith("subtiers/"):
                current = None
            return current, [], subdirs.dirs + subdirs.tests

        # Because of bug 925236 and possible other unknown race conditions,
        # don't parallelize the tools tier. There aren't many directories for
        # this tier anyways.
        def tools_filter(current, subdirs):
            if should_skip("tools", current) or current.startswith("subtiers/"):
                current = None
            return current, [], subdirs.dirs + subdirs.tests

        filters = [
            ("export", parallel_filter),
            ("libs", libs_filter),
            ("misc", parallel_filter),
            ("tools", tools_filter),
            ("check", parallel_filter),
        ]

        root_deps_mk = Makefile()

        # Fill the dependencies for traversal of each tier.
        for tier, filter in sorted(filters, key=itemgetter(0)):
            main, all_deps = self._traversal.compute_dependencies(filter)
            for dir, deps in sorted(all_deps.items()):
                if deps is not None or (dir in self._idl_dirs and tier == "export"):
                    rule = root_deps_mk.create_rule(["%s/%s" % (dir, tier)])
                    if deps:
                        rule.add_dependencies(
                            "%s/%s" % (d, tier) for d in sorted(deps) if d
                        )
            rule = root_deps_mk.create_rule(["recurse_%s" % tier])
            if main:
                rule.add_dependencies("%s/%s" % (d, tier) for d in sorted(main))

        rule = root_deps_mk.create_rule(["recurse_pre-compile"])
        rule.add_dependencies("%s/pre-compile" % d for d in sorted(self._pre_compile))

        targets_with_pre_compile = sorted(
            t for t in self._compile_graph if mozpath.dirname(t) in self._pre_compile
        )
        for t in targets_with_pre_compile:
            relobjdir = mozpath.dirname(t)
            rule = root_deps_mk.create_rule([t])
            rule.add_dependencies(["%s/pre-compile" % relobjdir])

        for relobjdir, includes in sorted(self._objdir_local_includes.items()):
            target = mozpath.join(relobjdir, "target-objects")
            deps = sorted(
                f"{d}/pre-compile"
                for d in includes
                if d != relobjdir and d in self._pre_compile
            )
            if deps and target in self._compile_graph:
                root_deps_mk.create_rule([target]).add_dependencies(deps)

        all_compile_deps = (
            reduce(lambda x, y: x | y, self._compile_graph.values())
            if self._compile_graph
            else set()
        )
        # Include the following as dependencies of the top recursion target for
        # compilation:
        # - nodes that are not dependended upon by anything. Typically, this
        #   would include programs, that need to be recursed, but that nothing
        #   depends on.
        # - nodes that are rust targets.
        compile_roots = [
            t
            for t, deps in self._compile_graph.items()
            if t in self._rust_targets or t not in all_compile_deps
        ]

        def add_category_rules(category, roots, graph):
            rule = root_deps_mk.create_rule(["recurse_%s" % category])
            # Directories containing rust compilations don't generally depend
            # on other directories in the tree, so putting them first here will
            # start them earlier in the build.
            rust_roots = sorted(r for r in roots if r in self._rust_targets)
            if category == "compile" and rust_roots:
                rust_rule = root_deps_mk.create_rule(["recurse_rust"])
                rust_rule.add_dependencies(rust_roots)
                # Ensure our cargo invocations are serialized, and gecko comes
                # first. Cargo will lock on the build output directory anyway,
                # so trying to run things in parallel is not useful. Dependencies
                # for gecko are especially expensive to build and parallelize
                # poorly, so prioritizing these will save some idle time in full
                # builds.
                for prior_target, target in pairwise(
                    sorted(
                        [t for t in reversed(rust_roots)],
                        key=lambda t: t != self._gkrust_target,
                    )
                ):
                    r = root_deps_mk.create_rule([target])
                    r.add_dependencies([prior_target])

            rule.add_dependencies(chain(rust_roots, sorted(roots)))
            for target, deps in sorted(graph.items()):
                if deps:
                    rule = root_deps_mk.create_rule([target])
                    rule.add_dependencies(sorted(deps))

        non_default_roots = defaultdict(list)
        non_default_graphs = defaultdict(lambda: defaultdict(set))

        for root in compile_roots:
            # If this is a non-default target, separate the root from the
            # rest of the compile graph.
            target_name = mozpath.basename(root)

            if target_name not in ("target", "target-objects", "host", "host-objects"):
                non_default_roots[target_name].append(root)
                non_default_graphs[target_name][root] = self._compile_graph[root]
                del self._compile_graph[root]

        for root in chain(*non_default_roots.values()):
            compile_roots.remove(root)
            dirname = mozpath.dirname(root)
            # If a directory only contains non-default compile targets, we don't
            # attempt to dump symbols there.
            if (
                dirname in self._no_skip["syms"]
                and "%s/target" % dirname not in self._compile_graph
            ):
                self._no_skip["syms"].remove(dirname)

        add_category_rules("compile", compile_roots, self._compile_graph)
        for category, graph in sorted(non_default_graphs.items()):
            add_category_rules(category, non_default_roots[category], graph)

        for relobjdir, tier, input in self._post_process_dependencies:
            dep = self._target_per_file.get(input.full_path)
            if dep:
                rule = root_deps_mk.create_rule([mozpath.join(relobjdir, tier)])
                rule.add_dependencies([mozpath.join(*dep)])

        root_mk = Makefile()

        # Fill root.mk with the convenience variables.
        for tier, filter in filters:
            all_dirs = self._traversal.traverse("", filter)
            root_mk.add_statement("%s_dirs := %s" % (tier, " ".join(all_dirs)))

        # Need a list of compile targets because we can't use pattern rules:
        # https://savannah.gnu.org/bugs/index.php?42833
        root_mk.add_statement(
            "pre_compile_targets := %s"
            % " ".join(sorted("%s/pre-compile" % p for p in self._pre_compile))
        )
        root_mk.add_statement(
            "compile_targets := %s"
            % " ".join(sorted(set(self._compile_graph.keys()) | all_compile_deps))
        )
        root_mk.add_statement(
            "syms_targets := %s"
            % " ".join(sorted(set("%s/syms" % d for d in self._no_skip["syms"])))
        )
        root_mk.add_statement(
            "rust_targets := %s" % " ".join(sorted(self._rust_targets))
        )

        root_mk.add_statement(
            "non_default_tiers := %s" % " ".join(sorted(non_default_roots.keys()))
        )

        for category, graphs in sorted(non_default_graphs.items()):
            category_dirs = [mozpath.dirname(target) for target in graphs.keys()]
            root_mk.add_statement("%s_dirs := %s" % (category, " ".join(category_dirs)))

        # Root install-manifest rules.
        is_hybrid = "FasterMake+RecursiveMake" in self.environment.substs.get(
            "BUILD_BACKENDS", []
        )
        install_manifests = [
            "dist/branding",
            "dist/include",
            "dist/public",
            "dist/private",
            "dist/xpi-stage",
            "_tests",
        ]
        # Skip the dist/bin install manifest when using the hybrid
        # FasterMake/RecursiveMake backend. This is a hack until bug 1241744 moves
        # xpidl handling to FasterMake in that case, mechanically making the dist/bin
        # install manifest non-existent (non-existent manifests being skipped)
        if not is_hybrid:
            install_manifests.append("dist/bin")

        install_targets = [f"install-{m}" for m in install_manifests]
        phony_targets = [
            "install-manifests",
            "install-manifests-binaries",
            *install_targets,
        ]
        root_deps_mk.add_statement(".PHONY: " + " ".join(phony_targets))

        agg = root_deps_mk.create_rule(["install-manifests"])
        agg.add_dependencies(install_targets)
        if is_hybrid:
            agg.add_dependencies(["faster"])

        binaries = root_deps_mk.create_rule(["install-manifests-binaries"])
        binaries.add_dependencies(["install-dist/include"])
        if is_hybrid:
            binaries.add_dependencies(["faster"])

        root_deps_mk.add_statement(".PHONY: install-tests install-test-files")
        install_tests = root_deps_mk.create_rule(["install-tests"])
        install_tests.add_dependencies(["install-test-files"])
        test_files = root_deps_mk.create_rule(["install-test-files"])
        test_files.add_commands([
            "$(call py_action,process_install_manifest test/files,"
            "--track install__test_files.track _tests "
            "_build_manifests/install/_test_files)"
        ])

        if is_hybrid:
            root_deps_mk.add_statement(".PHONY: faster")
            faster = root_deps_mk.create_rule(["faster"])
            faster.add_commands(["$(MAKE) -C faster FASTER_RECURSIVE_MAKE=1"])

        for manifest in install_manifests:
            target = f"install-{manifest}"
            underscored = manifest.replace("/", "_")
            rule = root_deps_mk.create_rule([target])
            rule.add_dependencies(["$(install_manifest_depends)"])
            for path in sorted(self._install_manifest_objdir_deps[manifest]):
                self._check_root_generated_output(path, f"The install for {manifest}")
            rule.add_dependencies(sorted(self._install_manifest_deps[manifest]))
            commands = []
            if is_hybrid:
                # If we're using the hybrid FasterMake/RecursiveMake backend, we want
                # to ensure the FasterMake end doesn't have install manifests for the
                # same directory, because that would blow up
                commands.append(
                    f"$(if $(wildcard _build_manifests/install/{underscored}),"
                    f"$(if $(wildcard faster/install_{underscored}*),"
                    f"$(error FasterMake and RecursiveMake ends of the "
                    f"hybrid build system want to handle {manifest})))"
                )
            commands.append(
                f"$(foreach manifest,"
                f"$(wildcard _build_manifests/install/{underscored}),"
                f"$(call py_action,process_install_manifest {manifest},"
                f"--track install_{underscored}.track "
                f"{manifest} $(manifest)))"
            )
            rule.add_commands(commands)

            # The track says what the install put where. Giving it a rule lets
            # another rule depend on it before the install has run.
            root_deps_mk.add_statement(
                f"$(DEPTH)/install_{underscored}.track: {target} ;"
            )

        self._add_staged_objdir_rules(root_deps_mk)
        self._add_xpi_archive_rules(root_deps_mk)

        # Wrapper rules for the faster backend's underscored targets.
        for manifest in install_manifests:
            if not manifest.startswith("dist/"):
                continue
            underscored = manifest.replace("/", "_")
            rule = root_deps_mk.create_rule([f"install-{underscored}"])
            rule.add_dependencies([f"install-{manifest}"])

        root_mk.add_statement("include root-deps.mk")

        with self._write_file(
            mozpath.join(self.environment.topobjdir, "root.mk")
        ) as root:
            root_mk.dump(root, removal_guard=False)

        with self._write_file(
            mozpath.join(self.environment.topobjdir, "root-deps.mk")
        ) as root_deps:
            root_deps_mk.dump(root_deps, removal_guard=False)

    def _check_root_generated_output(self, path, reader):
        if path not in self._root_generated_outputs:
            raise Exception(
                f"{reader} reads {path}, which has no rule in the top level "
                "backend file. It runs from the top level Makefile, so it can "
                "only read a GENERATED_FILES output that nothing links against."
            )

    def _add_staged_objdir_rules(self, root_deps_mk):
        """Declare root-level installs of built files into XPI staging areas."""
        tracks = []
        for stage, installs in sorted(self._staged_objdir_installs.items()):
            manifest = self._built_install_manifests[stage]
            rule = root_deps_mk.create_rule([self._built_install_track(stage)])
            rule.add_dependencies([self._built_install_manifest_path(stage)])
            for source, dest in sorted(installs):
                produced = mozpath.join(self.environment.topobjdir, source)
                self._check_root_generated_output(produced, f"The copy into {dest}")
                manifest.add_link(produced, dest)
                rule.add_dependencies([mozpath.join("$(DEPTH)", source)])
            rule.add_commands([self._built_install_command(stage), "@$(TOUCH) $@"])
            tracks.append(self._built_install_track(stage))

        if tracks:
            root_deps_mk.create_rule(["recurse_misc"]).add_dependencies(tracks)

    def _add_xpi_archive_rules(self, root_deps_mk):
        """Declare each archive as an output of what was installed for it.

        The archive is a target of the install track, so it is rebuilt when the
        install that fills it changes and not otherwise. It is declared beside
        the install it reads, because that is where the rule producing the
        track it depends on is.
        """
        archives = []
        test_archives = []
        for obj in sorted(self._xpi_archives, key=lambda o: o.archive):
            base = mozpath.basedir(obj.install_target, self.INSTALL_MANIFEST_BASES)
            track = f"install_{base.replace('/', '_')}.track"
            archive = mozpath.join("$(DEPTH)", obj.archive)
            depfile = mozpath.join("$(MDDEPDIR)", "xpi", obj.archive + ".pp")
            (test_archives if obj.destdir else archives).append(archive)

            rule = root_deps_mk.create_rule([archive])
            rule.add_dependencies([mozpath.join("$(DEPTH)", track)])
            if obj.install_target in self._staged_objdir_installs:
                rule.add_dependencies([self._built_install_track(obj.install_target)])
            rule.add_commands([
                f"$(call py_action,xpi_archive {obj.name},"
                f"--track $(DEPTH)/{track} "
                f"--stage-dir $(DEPTH)/{obj.install_target} "
                f"--prefix {mozpath.relpath(obj.install_target, base)} "
                f"--dep-target {archive} "
                f"--dep-file {depfile} "
                f"{archive})"
            ])
            # rules.mk has already read its dependency files by the time this
            # fragment is included, so the archive includes its own.
            root_deps_mk.add_statement(f"-include {depfile}")

        if archives:
            root_deps_mk.create_rule(["recurse_tools"]).add_dependencies(archives)
        if test_archives:
            root_deps_mk.create_rule(["recurse_misc"]).add_dependencies(test_archives)

    def _add_unified_build_rules(
        self,
        makefile,
        unified_source_mapping,
        unified_files_makefile_variable="unified_files",
        include_curdir_build_rules=True,
    ):
        # In case it's a generator.
        unified_source_mapping = sorted(unified_source_mapping)

        explanation = (
            "\n"
            "# We build files in 'unified' mode by including several files\n"
            "# together into a single source file.  This cuts down on\n"
            "# compilation times and debug information size."
        )
        makefile.add_statement(explanation)

        all_sources = " ".join(source for source, _ in unified_source_mapping)
        makefile.add_statement(
            "%s := %s" % (unified_files_makefile_variable, all_sources)
        )

        if include_curdir_build_rules:
            makefile.add_statement(
                "\n"
                '# Make sometimes gets confused between "foo" and "$(CURDIR)/foo".\n'
                "# Help it out by explicitly specifiying dependencies."
            )
            makefile.add_statement(
                "all_absolute_unified_files := \\\n"
                "  $(addprefix $(CURDIR)/,$(%s))" % unified_files_makefile_variable
            )
            rule = makefile.create_rule(["$(all_absolute_unified_files)"])
            rule.add_dependencies(["$(CURDIR)/%: %"])

    def _check_blacklisted_variables(self, makefile_in, makefile_content):
        if "EXTERNALLY_MANAGED_MAKE_FILE" in makefile_content:
            # Bypass the variable restrictions for externally managed makefiles.
            return

        for l in makefile_content.splitlines():
            l = l.strip()
            # Don't check comments
            if l.startswith("#"):
                continue
            for x in chain(_MOZBUILD_ONLY_VARIABLES, DEPRECATED_VARIABLES):
                if x not in l:
                    continue

                # Finding the variable name in the Makefile is not enough: it
                # may just appear as part of something else, like DIRS appears
                # in GENERATED_DIRS.
                if re.search(r"\b%s\s*[:?+]?=" % x, l):
                    if x in _MOZBUILD_ONLY_VARIABLES:
                        message = MOZBUILD_VARIABLES_MESSAGE
                    else:
                        message = DEPRECATED_VARIABLES_MESSAGE
                    raise Exception(
                        "Variable %s is defined in %s. %s" % (x, makefile_in, message)
                    )

    def consume_finished(self):
        CommonBackend.consume_finished(self)

        for objdir, backend_file in sorted(self._backend_files.items()):
            srcdir = backend_file.srcdir
            with self._write_file(fh=backend_file) as bf:
                makefile_in = mozpath.join(srcdir, "Makefile.in")
                makefile = mozpath.join(objdir, "Makefile")

                # If Makefile.in exists, use it as a template. Otherwise,
                # create a stub.
                stub = not os.path.exists(makefile_in)
                if not stub:
                    self.log(
                        logging.DEBUG,
                        "substitute_makefile",
                        {"path": makefile},
                        "Substituting makefile: {path}",
                    )
                    self._makefile_in_count += 1

                    # In the export and libs tiers, we don't skip directories
                    # containing a Makefile.in.
                    # topobjdir is handled separatedly, don't do anything for
                    # it.
                    if bf.relobjdir:
                        for tier in ("export", "libs"):
                            self._no_skip[tier].add(bf.relobjdir)
                else:
                    self.log(
                        logging.DEBUG,
                        "stub_makefile",
                        {"path": makefile},
                        "Creating stub Makefile: {path}",
                    )

                obj = self.Substitution()
                obj.output_path = makefile
                obj.input_path = makefile_in
                obj.topsrcdir = backend_file.topsrcdir
                obj.topobjdir = bf.environment.topobjdir
                obj.config = bf.environment
                self._create_makefile(obj, stub=stub)
                with open(obj.output_path, encoding="utf-8") as fh:
                    topobjdir = self.environment.topobjdir
                    content = fh.read()
                    # Directories with a Makefile containing a tools target
                    # can't be skipped and must run during the 'tools' tier.
                    if objdir != topobjdir and re.search(
                        r"(?:^|\s)tools.*::", content, re.M
                    ):
                        self._no_skip["tools"].add(mozpath.relpath(objdir, topobjdir))

                    # Directories with a Makefile containing a check target
                    # can't be skipped and must run during the 'check' tier.
                    if re.search(r"(?:^|\s)check.*::", content, re.M):
                        self._no_skip["check"].add(mozpath.relpath(objdir, topobjdir))

                    # Detect any Makefile.ins that contain variables on the
                    # moz.build-only list
                    self._check_blacklisted_variables(makefile_in, content)

        self._fill_root_mk()

        # Make the master test manifest files.
        for flavor, t in self._test_manifests.items():
            install_prefix, manifests = t
            manifest_stem = mozpath.join(install_prefix, "%s.toml" % flavor)
            self._write_master_test_manifest(
                mozpath.join(self.environment.topobjdir, "_tests", manifest_stem),
                manifests,
            )

            # Catch duplicate inserts.
            try:
                self._install_manifests["_tests"].add_optional_exists(manifest_stem)
            except ValueError:
                pass

        self._add_chrome_manifest_content()

        self._write_manifests("install", self._install_manifests)
        self._write_manifests("built", self._built_install_manifests)

        ensureParentDir(mozpath.join(self.environment.topobjdir, "dist", "foo"))

    def _pretty_path_parts(self, path, backend_file):
        assert isinstance(path, Path)
        if isinstance(path, SourcePath):
            if path.full_path.startswith(backend_file.srcdir):
                return "$(srcdir)", path.full_path[len(backend_file.srcdir) :]
            if path.full_path.startswith(backend_file.topsrcdir):
                return "$(topsrcdir)", path.full_path[len(backend_file.topsrcdir) :]
        elif isinstance(path, ObjDirPath):
            if path.full_path.startswith(backend_file.objdir):
                return "", path.full_path[len(backend_file.objdir) + 1 :]
            if path.full_path.startswith(self.environment.topobjdir):
                return "$(DEPTH)", path.full_path[len(self.environment.topobjdir) :]

        return "", path.full_path

    def _pretty_path(self, path, backend_file):
        return "".join(self._pretty_path_parts(path, backend_file))

    def _process_unified_sources(self, obj):
        backend_file = self._get_backend_file_for(obj)

        suffix_map = {
            ".c": "UNIFIED_CSRCS",
            ".m": "UNIFIED_CMSRCS",
            ".mm": "UNIFIED_CMMSRCS",
            ".cpp": "UNIFIED_CPPSRCS",
        }

        var = suffix_map[obj.canonical_suffix]
        non_unified_var = var[len("UNIFIED_") :]

        if obj.have_unified_mapping:
            self._add_unified_build_rules(
                backend_file,
                obj.unified_source_mapping,
                unified_files_makefile_variable=var,
                include_curdir_build_rules=False,
            )
            backend_file.write("%s += $(%s)\n" % (non_unified_var, var))
        else:
            # Sorted so output is consistent and we don't bump mtimes.
            source_files = list(sorted(obj.files))

            backend_file.write("%s += %s\n" % (non_unified_var, " ".join(source_files)))

        self._compile_graph[mozpath.join(backend_file.relobjdir, "target-objects")]

    def _process_directory_traversal(self, obj, backend_file):
        """Process a data.DirectoryTraversal instance."""
        fh = backend_file.fh

        def relativize(base, dirs):
            return (mozpath.relpath(d.translated, base) for d in dirs)

        if obj.dirs:
            fh.write(
                "DIRS := %s\n" % " ".join(relativize(backend_file.objdir, obj.dirs))
            )
            self._traversal.add(
                backend_file.relobjdir,
                dirs=relativize(self.environment.topobjdir, obj.dirs),
            )

        # The directory needs to be registered whether subdirectories have been
        # registered or not.
        self._traversal.add(backend_file.relobjdir)

    def _process_defines(self, obj, backend_file, which="DEFINES"):
        """Output the DEFINES rules to the given backend file."""
        defines = list(obj.get_defines())
        if defines:
            defines = " ".join(shell_quote(d) for d in defines)
            backend_file.write_once("%s += %s\n" % (which, defines))

    def _process_installation_target(self, obj, backend_file):
        if obj.subdir:
            backend_file.write("DIST_SUBDIR = %s\n" % (obj.subdir))
        backend_file.write(f"FINAL_TARGET = $(DEPTH)/{obj.target}\n")

        if not obj.enabled:
            backend_file.write("NO_DIST_INSTALL := 1\n")

    def _handle_idl_manager(self, manager):
        build_files = self._install_manifests["xpidl"]

        for p in ("Makefile", "backend.mk", ".deps/.mkdir.done"):
            build_files.add_optional_exists(p)

        for stem in manager.idl_stems():
            self._install_manifests["dist_include"].add_optional_exists("%s.h" % stem)

        for module in manager.modules:
            build_files.add_optional_exists(mozpath.join(".deps", "%s.pp" % module))

        modules = manager.modules
        xpt_modules = sorted(modules.keys())

        mk = Makefile()
        all_directories = set()

        for module_name in xpt_modules:
            module = manager.modules[module_name]
            all_directories |= module.directories
            deps = sorted(module.idl_files)

            # It may seem strange to have the .idl files listed as
            # prerequisites both here and in the auto-generated .pp files.
            # It is necessary to list them here to handle the case where a
            # new .idl is added to an xpt. If we add a new .idl and nothing
            # else has changed, the new .idl won't be referenced anywhere
            # except in the command invocation. Therefore, the .xpt won't
            # be rebuilt because the dependencies say it is up to date. By
            # listing the .idls here, we ensure the make file has a
            # reference to the new .idl. Since the new .idl presumably has
            # an mtime newer than the .xpt, it will trigger xpt generation.

            mk.add_statement("%s_deps := %s" % (module_name, " ".join(deps)))

            build_files.add_optional_exists("%s.xpt" % module_name)

        mk.add_statement("all_idl_dirs := %s" % " ".join(sorted(all_directories)))

        rules = StringIO()
        mk.dump(rules, removal_guard=False)

        xpidl_dir = "config/makefiles/xpidl"
        self._traversal.add("", dirs=[xpidl_dir])
        self._traversal.add(xpidl_dir)
        self._no_skip["export"].add(xpidl_dir)

        # Create dependency for output header so we force regeneration if the
        # header was deleted. This ideally should not be necessary. However,
        # some processes (such as PGO at the time this was implemented) wipe
        # out dist/include without regard to our install manifests.

        obj = self.Substitution()
        obj.output_path = mozpath.join(
            self.environment.topobjdir, xpidl_dir, "Makefile"
        )
        obj.input_path = mozpath.join(
            self.environment.topsrcdir, xpidl_dir, "Makefile.in"
        )
        obj.topsrcdir = self.environment.topsrcdir
        obj.topobjdir = self.environment.topobjdir
        obj.config = self.environment
        self._create_makefile(
            obj,
            extra=dict(
                xpidl_rules=rules.getvalue(), xpidl_modules=" ".join(xpt_modules)
            ),
        )

    def _process_program(self, obj, backend_file):
        backend_file.write(
            "PROGRAM = %s\n" % self._pretty_path(obj.output_path, backend_file)
        )
        if not obj.cxx_link and not self.environment.bin_suffix:
            backend_file.write("PROG_IS_C_ONLY_%s := 1\n" % obj.program)

    def _process_host_program(self, program, backend_file):
        backend_file.write(
            "HOST_PROGRAM = %s\n" % self._pretty_path(program.output_path, backend_file)
        )
        if program.cxx_link:
            backend_file.write_once("HOST_CPP_PROG_LINK := 1\n")

    def _process_rust_program_base(
        self, obj, backend_file, target_variable, target_cargo_variable
    ):
        backend_file.write_once("CARGO_FILE := %s\n" % obj.cargo_file)
        target_dir = mozpath.normpath(backend_file.environment.topobjdir)
        backend_file.write_once("CARGO_TARGET_DIR := %s\n" % target_dir)
        backend_file.write("%s += $(DEPTH)/%s\n" % (target_variable, obj.location))
        backend_file.write("%s += %s\n" % (target_cargo_variable, obj.name))
        if obj.output_category:
            program_target = f"$(DEPTH)/{obj.location}"
            self._process_non_default_target(obj, program_target, backend_file)
            backend_file.write_once(
                f"{obj.OUTPUT_CATEGORY_VAR} := {obj.output_category}\n"
            )

    def _process_rust_program(self, obj, backend_file):
        self._process_rust_program_base(
            obj, backend_file, "RUST_PROGRAMS", "RUST_CARGO_PROGRAMS"
        )

    def _process_host_rust_program(self, obj, backend_file):
        self._process_rust_program_base(
            obj, backend_file, "HOST_RUST_PROGRAMS", "HOST_RUST_CARGO_PROGRAMS"
        )

    def _process_rust_tests(self, obj, backend_file):
        if obj.config.substs.get("MOZ_RUST_TESTS"):
            # If --enable-rust-tests has been set, run these as a part of
            # make check.
            self._no_skip["check"].add(backend_file.relobjdir)
            backend_file.write("check:: force-cargo-test-run\n")
        build_target = self._build_target_for_obj(obj)
        self._compile_graph[build_target]
        self._process_non_default_target(obj, "force-cargo-test-run", backend_file)
        backend_file.write_once("CARGO_FILE := $(srcdir)/Cargo.toml\n")
        backend_file.write_once("RUST_TESTS := %s\n" % " ".join(obj.names))

    def _process_legacy_run_tests(self, obj, backend_file):
        self._no_skip["check"].add(backend_file.relobjdir)
        rules = Makefile()
        for i, test in enumerate(obj.tests):
            backend_file.write(f"check:: force-run-test{i}\n")
            program, *args = test["args"]
            program = self._pretty_path(Path(obj._context, program), backend_file)
            args = [
                (
                    arg
                    if arg.startswith("-")
                    else self._pretty_path(Path(obj._context, arg), backend_file)
                )
                for arg in args
            ]
            args = [shell_quote(arg) for arg in args]
            rule = rules.create_rule([f"force-run-test{i}"])
            depends = [program] + [
                self._pretty_path(Path(obj._context, dep), backend_file)
                for dep in test["depends"]
            ]
            rule.add_dependencies(depends)

            commands = []
            if description := test["description"]:
                commands.append(f"@echo Running {description}")
            if env := test["env"]:
                envargs = " ".join(
                    make_quote(shell_quote(f"{k}={v}")) for k, v in env.items()
                )
                command = f"env {envargs} "
            else:
                command = ""
            command += f"{program} "
            command += " ".join(args)
            commands.append(command)
            rule.add_commands(commands)
        rules.dump(backend_file.fh)

    def _process_simple_program(self, obj, backend_file):
        if obj.is_unit_test:
            backend_file.write("CPP_UNIT_TESTS += %s\n" % obj.program)
            assert obj.cxx_link
        else:
            backend_file.write("SIMPLE_PROGRAMS += %s\n" % obj.program)
            if not obj.cxx_link and not self.environment.bin_suffix:
                backend_file.write("PROG_IS_C_ONLY_%s := 1\n" % obj.program)

    def _process_host_simple_program(self, obj, backend_file):
        backend_file.write(f"HOST_SIMPLE_PROGRAMS += {obj.program}\n")
        if obj.cxx_link:
            backend_file.write_once("HOST_CPP_PROG_LINK := 1\n")

    def _process_relrhack(self, obj):
        if isinstance(
            obj, (SimpleProgram, Program, SharedLibrary)
        ) and obj.config.substs.get("RELRHACK"):
            # When building with RELR-based ELF hack, we need to build the relevant parts
            # before any target.
            node = self._compile_graph[self._build_target_for_obj(obj)]
            node.add("build/unix/elfhack/host")
            node.add("build/unix/elfhack/inject/target-objects")

    def _process_test_support_file(self, obj):
        # Ensure test support programs and libraries are tracked by an
        # install manifest for the benefit of the test packager.
        if not obj.install_target.startswith("_tests"):
            return

        dest_basename = None
        if isinstance(obj, BaseLibrary):
            dest_basename = obj.lib_name
        elif isinstance(obj, BaseProgram):
            dest_basename = obj.program
        if dest_basename is None:
            return

        self._install_manifests["_tests"].add_optional_exists(
            mozpath.join(obj.install_target[len("_tests") + 1 :], dest_basename)
        )

    def _process_test_manifest(self, obj, backend_file):
        # Much of the logic in this function could be moved to CommonBackend.
        for source in obj.source_relpaths:
            self.backend_input_files.add(mozpath.join(obj.topsrcdir, source))

        # Don't allow files to be defined multiple times unless it is allowed.
        # We currently allow duplicates for non-test files or test files if
        # the manifest is listed as a duplicate.
        for source, (dest, is_test) in obj.installs.items():
            try:
                self._install_manifests["_test_files"].add_link(source, dest)
            except ValueError:
                if not obj.dupe_manifest and is_test:
                    raise

        for base, pattern, dest in obj.pattern_installs:
            try:
                self._install_manifests["_test_files"].add_pattern_link(
                    base, pattern, dest
                )
            except ValueError:
                if not obj.dupe_manifest:
                    raise

        for dest in obj.external_installs:
            try:
                self._install_manifests["_test_files"].add_optional_exists(dest)
            except ValueError:
                if not obj.dupe_manifest:
                    raise

        m = self._test_manifests.setdefault(obj.flavor, (obj.install_prefix, set()))
        m[1].add(obj.manifest_obj_relpath)

        try:
            from reftest import ReftestManifest

            if isinstance(obj.manifest, ReftestManifest):
                # Mark included files as part of the build backend so changes
                # result in re-config.
                self.backend_input_files |= obj.manifest.manifests.keys()
        except ImportError:
            # Ignore errors caused by the reftest module not being present.
            # This can happen when building SpiderMonkey standalone, for example.
            pass

    def _process_local_include(self, local_include, backend_file):
        d, path = self._pretty_path_parts(local_include, backend_file)
        if isinstance(local_include, ObjDirPath) and not d:
            # path doesn't start with a slash in this case
            d = "$(CURDIR)/"
        elif d == "$(DEPTH)":
            d = "$(topobjdir)"
        quoted_path = shell_quote(path) if path else path
        if quoted_path != path:
            path = quoted_path[0] + d + quoted_path[1:]
        else:
            path = d + path
        backend_file.write("LOCAL_INCLUDES += -I%s\n" % path)

    def _process_per_source_flag(self, per_source_flag, backend_file):
        for flag in per_source_flag.flags:
            backend_file.write(
                "%s_FLAGS += %s\n" % (mozpath.basename(per_source_flag.file_name), flag)
            )
        stem = mozpath.splitext(mozpath.basename(per_source_flag.file_name))[0]
        if per_source_flag.object_deps:
            prerequisites = " ".join(
                self._pretty_path(p, backend_file)
                for p in (per_source_flag.path, *per_source_flag.object_deps)
            )
            backend_file.write(f"{stem}.$(OBJ_SUFFIX): {prerequisites}\n")

    def _process_computed_flags(self, computed_flags, backend_file):
        for var, flags in computed_flags.get_flags():
            backend_file.write(
                "COMPUTED_%s += %s\n"
                % (var, " ".join(make_quote(shell_quote(f)) for f in flags))
            )

    def _process_non_default_target(self, libdef, target_name, backend_file):
        backend_file.write(f"{libdef.output_category}:: {target_name}\n")
        backend_file.write(f"MOZBUILD_NON_DEFAULT_TARGETS += {target_name}\n")

    def _process_shared_library(self, libdef, backend_file):
        backend_file.write_once("LIBRARY_NAME := %s\n" % libdef.basename)
        backend_file.write("FORCE_SHARED_LIB := 1\n")
        backend_file.write(
            "IMPORT_LIBRARY := %s\n"
            % self._pretty_path(libdef.import_path, backend_file)
        )
        shared_lib = self._pretty_path(libdef.output_path, backend_file)
        backend_file.write("SHARED_LIBRARY := %s\n" % shared_lib)
        if libdef.soname:
            backend_file.write("DSO_SONAME := %s\n" % libdef.soname)
        if libdef.symbols_file:
            if libdef.symbols_link_arg:
                backend_file.write("EXTRA_DSO_LDOPTS += %s\n" % libdef.symbols_link_arg)
        if not libdef.cxx_link:
            backend_file.write("LIB_IS_C_ONLY := 1\n")
        if libdef.output_category:
            self._process_non_default_target(libdef, shared_lib, backend_file)
        if self.environment.substs.get("OS_ARCH") == "WINNT" and libdef.installed:
            backend_file.write("IMPORT_LIB_FILES := $(IMPORT_LIBRARY)\n")
            backend_file.write("IMPORT_LIB_DEST = $(DIST)/lib$(DIST_SUBDIR:%=/%)\n")
            backend_file.write("IMPORT_LIB_TARGET := target\n")
            backend_file.write("INSTALL_TARGETS += IMPORT_LIB\n")

    def _process_static_library(self, libdef, backend_file):
        backend_file.write_once("LIBRARY_NAME := %s\n" % libdef.basename)
        backend_file.write("FORCE_STATIC_LIB := 1\n")
        backend_file.write("REAL_LIBRARY := %s\n" % libdef.lib_name)
        if libdef.build_static_lib_archive:
            backend_file.write("LIBRARY := $(REAL_LIBRARY)\n")
        if libdef.build_static_lib_archive and libdef._context.get("DIST_INSTALL"):
            backend_file.write("STATIC_LIB_FILES := $(REAL_LIBRARY)\n")
            backend_file.write("STATIC_LIB_DEST := $(DIST)/lib\n")
            backend_file.write("STATIC_LIB_TARGET := target\n")
            backend_file.write("INSTALL_TARGETS += STATIC_LIB\n")

    def _process_sandboxed_wasm_library(self, libdef, backend_file):
        backend_file.write("WASM_ARCHIVE := %s\n" % libdef.basename)

    def _process_rust_library(self, libdef, backend_file):
        rust_lib = self._pretty_path(libdef.import_path, backend_file)
        backend_file.write_once("%s := %s\n" % (libdef.LIB_FILE_VAR, rust_lib))
        backend_file.write_once("CARGO_FILE := $(srcdir)/Cargo.toml\n")
        # Need to normalize the path so Cargo sees the same paths from all
        # possible invocations of Cargo with this CARGO_TARGET_DIR.  Otherwise,
        # Cargo's dependency calculations don't work as we expect and we wind
        # up recompiling lots of things.
        target_dir = mozpath.normpath(backend_file.environment.topobjdir)
        backend_file.write("CARGO_TARGET_DIR := %s\n" % target_dir)
        if libdef.output_category:
            self._process_non_default_target(libdef, rust_lib, backend_file)

    def _process_host_shared_library(self, libdef, backend_file):
        backend_file.write("HOST_SHARED_LIBRARY = %s\n" % libdef.lib_name)

    def _build_target_for_obj(self, obj):
        if hasattr(obj, "output_category") and obj.output_category:
            target_name = obj.output_category
        elif isinstance(obj, BaseRustLibrary):
            target_name = f"{obj.KIND}-objects"
        else:
            target_name = obj.KIND
        if target_name == "wasm":
            target_name = "target"
        return f"{obj.relobjdir}/{target_name}"

    def _add_build_order_deps(self, build_target, obj):
        # Make the build target depend on all the target/host-objects that
        # recursively are linked into it.
        for lib in obj.linked_libraries:
            if (
                isinstance(lib, (StaticLibrary, HostLibrary))
                and not lib.build_static_lib_archive
            ):
                self._add_build_order_deps(build_target, lib)
            elif not isinstance(lib, ExternalLibrary):
                self._compile_graph[build_target].add(self._build_target_for_obj(lib))
        objects_target = mozpath.join(obj.relobjdir, f"{obj.KIND}-objects")
        if objects_target != build_target and objects_target in self._compile_graph:
            self._compile_graph[build_target].add(objects_target)

    def _add_rust_build_order_deps(self, obj):
        # Cargo handles the actual linking for Rust libraries and tests, so we
        # don't go through _process_linked_libraries. We still need their
        # USE_LIBS built first, so add them as build-order dependencies.
        build_target = self._build_target_for_obj(obj)
        self._compile_graph[build_target]
        self._add_build_order_deps(build_target, obj)

    def _process_linked_libraries(self, obj, backend_file):
        objs, shared_libs, os_libs, static_libs = self._expand_libs(obj)

        obj_target = obj.name
        if isinstance(obj, (Program, SharedLibrary)):
            obj_target = self._pretty_path(obj.output_path, backend_file)

        objs_ref = " \\\n    ".join(os.path.relpath(o, obj.objdir) for o in objs)
        if isinstance(obj, (SimpleProgram, Program, SharedLibrary)):
            # Don't bother with a list file if we're only linking objects built
            # in this directory.
            if objs == obj.objs:
                backend_file.write_once("%s_OBJS := %s\n" % (obj.name, objs_ref))
            else:
                list_file_path = "%s.list" % obj.name.replace(".", "_")
                list_file_ref = self._make_list_file(
                    obj.KIND, obj.objdir, objs, list_file_path
                )
                backend_file.write_once("%s_OBJS := %s\n" % (obj.name, list_file_ref))
                backend_file.write_once("%s: %s\n" % (obj_target, list_file_path))
                backend_file.write("%s: %s\n" % (obj_target, objs_ref))

        elif (
            not isinstance(
                obj,
                (
                    HostLibrary,
                    HostRustProgram,
                    RustProgram,
                    StaticLibrary,
                    SandboxedWasmLibrary,
                ),
            )
            or isinstance(obj, (StaticLibrary, SandboxedWasmLibrary))
            and obj.build_static_lib_archive
        ):
            response_file_path = "%s.list" % obj.name.replace(".", "_")
            response_file_ref = self._make_ar_response_file(
                obj.objdir, objs, response_file_path
            )
            if response_file_ref:
                backend_file.write_once(
                    "%s_OBJS := %s\n" % (obj.name, response_file_ref)
                )
                backend_file.write_once("%s: %s\n" % (obj_target, response_file_path))
            else:
                backend_file.write_once("%s_OBJS := %s\n" % (obj.name, objs_ref))
            backend_file.write("%s: %s\n" % (obj_target, objs_ref))
        if getattr(obj, "symbols_file", None):
            backend_file.write_once("%s: %s\n" % (obj_target, obj.symbols_file))

        for dep in getattr(obj, "extra_link_deps", ()):
            backend_file.write_once(
                f"{obj_target}: {self._pretty_path(dep, backend_file)}\n"
            )

        for lib in shared_libs:
            assert obj.KIND not in {"host", "wasm"}
            backend_file.write_once(
                "SHARED_LIBS += %s\n" % self._pretty_path(lib.import_path, backend_file)
            )

        # We have to link any Rust libraries after all intermediate static
        # libraries have been listed to ensure that the Rust libraries are
        # searched after the C/C++ objects that might reference Rust symbols.
        var = "HOST_LIBS" if obj.KIND == "host" else "STATIC_LIBS"
        for lib in chain(
            (l for l in static_libs if not isinstance(l, BaseRustLibrary)),
            (l for l in static_libs if isinstance(l, BaseRustLibrary)),
        ):
            backend_file.write_once(
                "%s += %s\n" % (var, self._pretty_path(lib.import_path, backend_file))
            )

        for lib in os_libs:
            if obj.KIND == "target":
                backend_file.write_once("OS_LIBS += %s\n" % lib)
            elif obj.KIND == "host":
                backend_file.write_once("HOST_EXTRA_LIBS += %s\n" % lib)

        if (
            not isinstance(obj, (StaticLibrary, HostLibrary))
            or obj.build_static_lib_archive
        ):
            # This will create the node even if there aren't any linked libraries.
            build_target = self._build_target_for_obj(obj)
            self._compile_graph[build_target]
            self._add_build_order_deps(build_target, obj)

        # Process library-based defines
        self._process_defines(obj.lib_defines, backend_file)

    def _add_install_target(self, backend_file, install_target, tier, dest, files):
        self._no_skip[tier].add(backend_file.relobjdir)
        for f in files:
            backend_file.write("%s_FILES += %s\n" % (install_target, f))
        backend_file.write("%s_DEST := %s\n" % (install_target, dest))
        backend_file.write("%s_TARGET := %s\n" % (install_target, tier))
        backend_file.write("INSTALL_TARGETS += %s\n" % install_target)

    def _built_install_manifest_path(self, name):
        return f"$(topobjdir)/{self.BUILT_INSTALL_MANIFESTS}/{name}"

    def _built_install_track(self, name):
        return self._built_install_manifest_path(name) + ".track"

    def _built_install_command(self, name):
        return (
            f"$(call py_action,process_install_manifest {name},"
            f"--track {self._built_install_track(name)} $(topobjdir) "
            f"{self._built_install_manifest_path(name)})"
        )

    def _add_objdir_install_target(self, backend_file, tier, f, dest):
        """Install a built file through the manifest for its directory and tier.

        ``dest`` is relative to the object directory. The manifest is processed
        once per directory and tier, after every file it installs is built.
        """
        self._no_skip[tier].add(backend_file.relobjdir)
        name = mozpath.join(backend_file.relobjdir, tier)
        self._built_install_manifests[name].add_link(f.full_path, dest)
        track = self._built_install_track(name)
        backend_file.write_once(
            dedent(f"""\
                {tier}:: {track}
                {track}: {self._built_install_manifest_path(name)}
                \t{self._built_install_command(name)}
                \t@$(TOUCH) $@
                """)
        )
        backend_file.write(f"{track}: {self._pretty_path(f, backend_file)}\n")

    def _process_final_target_files(self, obj, files, backend_file):
        target = obj.install_target
        path = mozpath.basedir(target, self.INSTALL_MANIFEST_BASES)
        if not path:
            raise Exception("Cannot install to " + target)

        # Exports are not interesting to artifact builds.
        if path == "dist/include" and self.environment.is_artifact_build:
            return

        manifest = path.replace("/", "_")
        install_manifest = self._install_manifests[manifest]
        reltarget = mozpath.relpath(target, path)

        for subpath, subfiles in files.walk():
            target_var = (mozpath.join(target, subpath) if subpath else target).replace(
                "/", "_"
            )
            # We don't necessarily want to combine these, because non-wildcard
            # absolute files tend to be libraries, and we don't want to mix
            # those in with objdir headers that will be installed during export.
            # (See bug 1642882 for details.)
            objdir_files = []
            renamed_objdir_files = []
            absolute_files = []

            for f in subfiles:
                dest_dir = mozpath.join(reltarget, subpath)
                dest_file = mozpath.join(dest_dir, f.target_basename)
                if not isinstance(f, ObjDirPath):
                    if "*" in f:
                        if f.startswith("/") or isinstance(f, AbsolutePath):
                            # A rooted pattern names the directory to search, so
                            # what it matches is the path below the destination.
                            prefix, pattern = pattern_link_parts(f.full_path)
                            install_manifest.add_pattern_link(
                                prefix, pattern, pattern_link_dest(f, dest_dir)
                            )
                        else:
                            # A srcdir-relative pattern keeps the directories it
                            # matched, so `foo/**` installs under `<dest>/foo`.
                            install_manifest.add_pattern_link(f.srcdir, f, dest_dir)
                    elif isinstance(f, AbsolutePath):
                        if not f.full_path.lower().endswith((
                            ".dll",
                            ".pdb",
                            ".so",
                            ".dylib",
                        )):
                            raise Exception(
                                "Absolute paths installed to FINAL_TARGET_FILES must"
                                " only be shared libraries or associated debug"
                                " information."
                            )
                        install_manifest.add_optional_exists(dest_file)
                        absolute_files.append(f.full_path)
                    else:
                        install_manifest.add_link(f.full_path, dest_file)
                else:
                    install_manifest.add_optional_exists(dest_file)
                    if f.target_basename != mozpath.basename(f.full_path):
                        renamed_objdir_files.append(f)
                    else:
                        objdir_files.append(f)
            install_location = "$(DEPTH)/%s" % mozpath.join(target, subpath)
            if objdir_files:
                if obj.install_target == "dist/include":
                    # Generated headers install in bulk during export because they
                    # are regular files for which the fixed install mode is fine.
                    self._add_install_target(
                        backend_file,
                        target_var,
                        "export",
                        install_location,
                        [self._pretty_path(f, backend_file) for f in objdir_files],
                    )
                else:
                    # Other built files (e.g. binaries) are installed as they
                    # are, so an executable keeps its +x bit.
                    for f in objdir_files:
                        # We cannot generate multilocale.txt during misc at the moment.
                        tier = (
                            "libs" if f.target_basename == "multilocale.txt" else "misc"
                        )
                        self._add_built_file_install(
                            obj, backend_file, tier, f, target, subpath
                        )
            if renamed_objdir_files:
                tier = "export" if obj.install_target == "dist/include" else "misc"
                for f in renamed_objdir_files:
                    self._add_built_file_install(
                        obj, backend_file, tier, f, target, subpath
                    )
            if absolute_files:
                # Unfortunately, we can't use _add_install_target because on
                # Windows, the absolute file paths that we want to install
                # from often have spaces.  So we write our own rule.
                self._no_skip["misc"].add(backend_file.relobjdir)
                rules = []
                for f in absolute_files:
                    basename = mozpath.basename(f)
                    rules.append(
                        "\t$(INSTALL) %s %s"
                        % (make_quote(shell_quote(f)), install_location)
                    )
                    # Libraries installed this way are prebuilt, coming from
                    # outside the build, so unlike the ones we link ourselves,
                    # nothing has checked that they work on the systems we
                    # support. Do it here, as this is where they enter the build.
                    if f.lower().endswith((".dll", ".so", ".dylib")):
                        rules.append(
                            "\t$(call py_action,check_binary %s,%s)"
                            % (basename, mozpath.join(install_location, basename))
                        )
                backend_file.write("misc::\n%s\n" % "\n".join(rules))

    def _add_built_file_install(self, obj, backend_file, tier, f, target, subpath):
        """Install a built file from the directory that builds it.

        A copy into an extension staging area is recorded for the top level
        Makefile instead, where the archive packaging that area can depend on
        it.
        """
        dest = mozpath.join(target, subpath, f.target_basename)
        if mozpath.basedir(obj.install_target, ("dist/xpi-stage",)):
            source = mozpath.relpath(f.full_path, self.environment.topobjdir)
            self._staged_objdir_installs[obj.install_target].append((source, dest))
            return
        self._add_objdir_install_target(backend_file, tier, f, dest)

    def _process_installed_pp_files(self, obj, files):
        """Install preprocessed files through the manifest installing the rest.

        Their destinations then appear in the expanded install track, so whatever
        reads that track sees the whole install rather than only the part that
        needed no preprocessing.
        """
        target = obj.install_target
        base = mozpath.basedir(target, self.INSTALL_MANIFEST_BASES)
        if not base:
            raise Exception("Cannot install to " + target)
        install_manifest = self._install_manifests[base.replace("/", "_")]
        reltarget = mozpath.relpath(target, base)

        for dep in obj.extra_deps:
            self._add_install_manifest_dep(base, dep)

        for path, walk_files in files.walk():
            dest_dir = mozpath.join(reltarget, path)
            for f in walk_files:
                if isinstance(f, ObjDirPath):
                    self._add_install_manifest_dep(base, f)
                self._add_preprocess_entry(
                    install_manifest,
                    obj,
                    f.full_path,
                    dest_dir,
                    FinalTargetPreprocessedFiles.get_obj_basename(f),
                )

    def _add_preprocess_entry(
        self, install_manifest, obj, source, dest_dir, basename, **kwargs
    ):
        add_preprocess_entry(
            install_manifest,
            source,
            obj.install_target,
            dest_dir,
            basename,
            mozpath.join(self.environment.topobjdir, ".deps"),
            obj.defines.defines if obj.defines else None,
            self._configured_defines,
            **kwargs,
        )

    def _add_install_manifest_dep(self, base, path):
        """Make ``path`` a prerequisite of the install for ``base``.

        The install runs from the top level Makefile, so what it reads has to
        be a prerequisite of the install rather than of the directory declaring
        it.
        """
        if isinstance(path, ObjDirPath):
            dep = mozpath.join(
                "$(DEPTH)",
                mozpath.relpath(path.full_path, self.environment.topobjdir),
            )
            self._install_manifest_objdir_deps[base].add(path.full_path)
        elif path.full_path.startswith(self.environment.topsrcdir):
            dep = mozpath.join(
                "$(topsrcdir)",
                mozpath.relpath(path.full_path, self.environment.topsrcdir),
            )
        else:
            dep = path.full_path
        self._install_manifest_deps[base].add(dep)

    def _process_objdir_pp_files(self, obj, files, backend_file):
        # OBJDIR_PP_FILES cannot become an install manifest, because there is no
        # install manifest for the root of the object directory.
        for dep in obj.extra_deps:
            if isinstance(dep, ObjDirPath):
                self._post_process_dependencies.append((
                    backend_file.relobjdir,
                    "misc",
                    dep,
                ))
        for i, (walk_path, walk_files) in enumerate(files.walk()):
            self._no_skip["misc"].add(backend_file.relobjdir)
            var = f"OBJDIR_PP_FILES_{i}"
            for f in walk_files:
                backend_file.write(
                    "%s += %s\n" % (var, self._pretty_path(f, backend_file))
                )
            backend_file.write(
                "%s_PATH := $(DEPTH)/%s\n"
                % (var, mozpath.join(obj.install_target, walk_path))
            )
            backend_file.write("%s_TARGET := misc\n" % var)
            if obj.extra_deps:
                deps = " ".join(
                    self._pretty_path(d, backend_file) for d in obj.extra_deps
                )
                backend_file.write(f"{var}_EXTRA_DEPS := {deps}\n")
            backend_file.write("PP_TARGETS += %s\n" % var)

    def _localized_install_manifest(self, obj):
        """The manifest a localized install belongs to, its base, and where
        under it the install lands.

        A locale other than the source one is staged from the merge tree by the
        locale staging action, so what is installed here reads from the source
        tree and is described the same way as any other install.
        """
        target = obj.install_target
        base = mozpath.basedir(target, ("dist/bin",))
        if not base:
            raise Exception("Cannot install localized files to " + target)
        return (
            self._install_manifests[base.replace("/", "_")],
            base,
            mozpath.relpath(target, base),
        )

    def _process_localized_files(self, obj, files):
        install_manifest, base, reltarget = self._localized_install_manifest(obj)

        for walk_path, walk_files in files.walk():
            dest_dir = mozpath.join(reltarget, walk_path)
            for f in walk_files:
                if isinstance(f, ObjDirPath):
                    # An output of LOCALIZED_GENERATED_FILES, generated where it
                    # was declared and installed from the top level Makefile.
                    self._add_install_manifest_dep(base, f)
                    install_manifest.add_link(
                        f.full_path,
                        mozpath.normpath(mozpath.join(dest_dir, f.target_basename)),
                    )
                elif "*" in f:
                    prefix, pattern = pattern_link_parts(f.full_path)
                    install_manifest.add_pattern_link(
                        prefix, pattern, pattern_link_dest(f, dest_dir)
                    )
                else:
                    install_manifest.add_link(
                        f.full_path,
                        mozpath.normpath(mozpath.join(dest_dir, f.target_basename)),
                    )

    def _process_localized_pp_files(self, obj, files):
        install_manifest, base, reltarget = self._localized_install_manifest(obj)
        for dep in obj.extra_deps:
            self._add_install_manifest_dep(base, dep)

        for path, file_list in files.walk():
            dest_dir = mozpath.join(reltarget, path)
            for f in file_list:
                # A localized file may contain no preprocessor directive at all,
                # so the missing directive warning is silenced.
                self._add_preprocess_entry(
                    install_manifest,
                    obj,
                    f.full_path,
                    dest_dir,
                    FinalTargetPreprocessedFiles.get_obj_basename(f),
                    silence_missing_directive_warnings=True,
                )

    def _process_objdir_files(self, obj, files, backend_file):
        for path, file_list in files.walk():
            for f in file_list:
                dest = mozpath.join(path, f.target_basename)
                self._add_objdir_install_target(backend_file, "misc", f, dest)

    def _add_chrome_manifest_content(self):
        """Install every chrome manifest from the registrations it holds."""
        for target, content in chrome_manifest_contents(self._manifest_entries):
            base = mozpath.basedir(target, self.INSTALL_MANIFEST_BASES)
            if not base:
                raise Exception("Cannot install to " + target)
            self._install_manifests[base.replace("/", "_")].add_content(
                content, mozpath.relpath(target, base)
            )

    def _process_macos_bundle(self, obj, backend_file):
        context = obj._context
        spec = obj.bundle

        resolved = {
            "bundle": Path(context, spec["bundle"]).full_path,
            "lproj": spec["lproj"],
            "binaries": [],
            "extra_files": [],
            "moves": [list(m) for m in spec["moves"]],
            "pkginfo": spec["pkginfo"],
            "copies": [Path(context, c).full_path for c in spec["copies"]],
        }
        inputs = []
        if spec["skeleton"]:
            resolved["skeleton"] = Path(context, spec["skeleton"]).full_path
        for key in ("info_plist", "strings"):
            if value := spec[key]:
                path = Path(context, value)
                resolved[key] = path.full_path
                inputs.append(path)
        for key in ("binaries", "extra_files"):
            for source, dest in spec[key]:
                path = Path(context, source)
                resolved[key].append([path.full_path, dest])
                inputs.append(path)
        if spec["stage"]:
            resolved["stage"] = Path(context, spec["stage"]).full_path
            macos_files = Path(context, spec["macos_files"])
            macos_copy_files = Path(context, spec["macos_copy_files"])
            resolved["macos_files"] = macos_files.full_path
            resolved["macos_copy_files"] = macos_copy_files.full_path
            inputs.append(macos_files)
            inputs.append(macos_copy_files)

        name = mozpath.basename(resolved["bundle"])
        safe_name = name.replace(" ", "")
        # Bundles in one directory can share a basename (the same `.app` built
        # into different locations), so key the spec on the objdir-relative path.
        spec_id = (
            mozpath
            .relpath(resolved["bundle"], self.environment.topobjdir)
            .replace("/", "_")
            .replace(" ", "")
        )
        spec_name = f"{spec_id}.bundle.json"
        with self._write_file(mozpath.join(backend_file.objdir, spec_name)) as fh:
            json.dump(resolved, fh, sort_keys=True, indent=2)

        if spec["stage"]:
            tier = "tools"
            targets = ["tools", "repackage:"]
        else:
            tier = "libs"
            targets = ["libs:"]

        for dep in inputs:
            if isinstance(dep, ObjDirPath):
                self._post_process_dependencies.append((
                    backend_file.relobjdir,
                    tier,
                    dep,
                ))

        fragment = Makefile()
        rule = fragment.create_rule(targets=targets)
        rule.add_dependencies(
            [self._pretty_path(p, backend_file) for p in inputs] + [spec_name]
        )
        rule.add_commands([
            f"$(call py_action,assemble_macos_bundle {safe_name},{spec_name})"
        ])
        fragment.dump(backend_file.fh, removal_guard=False)
        if spec["stage"]:
            backend_file.write(".PHONY: repackage\n")

        self._no_skip[tier].add(backend_file.relobjdir)

    def _write_manifests(self, dest, manifests):
        man_dir = mozpath.join(self.environment.topobjdir, "_build_manifests", dest)

        for k, manifest in manifests.items():
            with self._write_file(mozpath.join(man_dir, k)) as fh:
                manifest.write(fileobj=fh)

    def _write_master_test_manifest(self, path, manifests):
        with self._write_file(path) as master:
            master.write(
                "# THIS FILE WAS AUTOMATICALLY GENERATED. DO NOT MODIFY BY HAND.\n\n"
            )

            for manifest in sorted(manifests):
                master.write('["include:%s"]\n' % manifest)

    class Substitution:
        """BaseConfigSubstitution-like class for use with _create_makefile."""

        __slots__ = ("input_path", "output_path", "topsrcdir", "topobjdir", "config")

    def _create_makefile(self, obj, stub=False, extra=None):
        """Creates the given makefile. Makefiles are treated the same as
        config files, but some additional header and footer is added to the
        output.

        When the stub argument is True, no source file is used, and a stub
        makefile with the default header and footer only is created.
        """
        with self._get_preprocessor(obj) as pp:
            if extra:
                pp.context.update(extra)
            pp.handleLine(
                "# THIS FILE WAS AUTOMATICALLY GENERATED. DO NOT MODIFY BY HAND.\n"
            )
            pp.handleLine("DEPTH := @DEPTH@\n")
            pp.handleLine("topobjdir := @topobjdir@\n")
            pp.handleLine("topsrcdir := @top_srcdir@\n")
            pp.handleLine("srcdir := @srcdir@\n")
            pp.handleLine("srcdir_rel := @srcdir_rel@\n")
            pp.handleLine("relativesrcdir := @relativesrcdir@\n")
            pp.handleLine("include $(DEPTH)/config/autoconf.mk\n")
            if not stub:
                pp.do_include(obj.input_path)
            # Empty line to avoid failures when last line in Makefile.in ends
            # with a backslash.
            pp.handleLine("\n")
            pp.handleLine("include $(topsrcdir)/config/recurse.mk\n")
        if not stub:
            # Adding the Makefile.in here has the desired side-effect
            # that if the Makefile.in disappears, this will force
            # moz.build traversal. This means that when we remove empty
            # Makefile.in files, the old file will get replaced with
            # the autogenerated one automatically.
            self.backend_input_files.add(obj.input_path)

        self._makefile_out_count += 1

    def _handle_ipdl_sources(
        self,
        ipdl_dir,
        sorted_ipdl_sources,
        sorted_nonstatic_ipdl_sources,
        sorted_static_ipdl_sources,
    ):
        # Write out a master list of all IPDL source files.
        mk = Makefile()

        sorted_nonstatic_ipdl_basenames = list()
        for source in sorted_nonstatic_ipdl_sources:
            basename = os.path.basename(source)
            sorted_nonstatic_ipdl_basenames.append(basename)
            rule = mk.create_rule([basename])
            rule.add_dependencies([
                source,
                "backend.mk",
                "Makefile",
                "$(DEPTH)/config/autoconf.mk",
                "$(topsrcdir)/config/config.mk",
            ])
            rule.add_commands([
                "$(RM) $@",
                "$(call py_action,preprocessor $@,$(DEFINES) $(ACDEFINES) $< -o $@)",
            ])

        mk.add_statement(
            "ALL_IPDLSRCS := %s %s"
            % (
                " ".join(sorted_nonstatic_ipdl_basenames),
                " ".join(sorted_static_ipdl_sources),
            )
        )

        ipdl_srcs_path = mozpath.join(ipdl_dir, "ipdlsrcs.txt")
        mk.add_statement(f"ALL_IPDLSRCS_FILE := {ipdl_srcs_path}")

        # Preprocessed ipdl files are generated in ipdl_dir.
        mk.add_statement(
            "IPDLDIRS := %s %s"
            % (
                ipdl_dir,
                " ".join(
                    sorted(set(mozpath.dirname(p) for p in sorted_static_ipdl_sources))
                ),
            )
        )

        # Bug 1885948: Passing all of these filenames directly to ipdl.py as
        # command-line arguments can go over the maximum argument length on
        # Windows (32768 bytes) if the checkout path is sufficiently long.
        with self._write_file(ipdl_srcs_path) as srcs:
            for filename in sorted_nonstatic_ipdl_basenames:
                srcs.write(f"{filename}\n")

            for filename in sorted_static_ipdl_sources:
                srcs.write(f"{filename}\n")

        with self._write_file(mozpath.join(ipdl_dir, "ipdlsrcs.mk")) as ipdls:
            mk.dump(ipdls, removal_guard=False)

    def _handle_webidl_build(
        self,
        bindings_dir,
        unified_source_mapping,
        webidls,
        expected_build_output_files,
        global_define_files,
    ):
        include_dir = mozpath.join(self.environment.topobjdir, "dist", "include")
        for f in expected_build_output_files:
            if f.startswith(include_dir):
                self._install_manifests["dist_include"].add_optional_exists(
                    mozpath.relpath(f, include_dir)
                )

        # We pass WebIDL info to make via a completely generated make file.
        mk = Makefile()
        mk.add_statement(
            "nonstatic_webidl_files := %s"
            % " ".join(sorted(webidls.all_non_static_basenames()))
        )
        mk.add_statement(
            "test_sources := %s"
            % " ".join(sorted("%sBinding.cpp" % s for s in webidls.all_test_stems()))
        )

        # Add rules to preprocess bindings.
        # This should ideally be using PP_TARGETS. However, since the input
        # filenames match the output filenames, the existing PP_TARGETS rules
        # result in circular dependencies and other make weirdness. One
        # solution is to rename the input or output files repsectively. See
        # bug 928195 comment 129.
        for source in sorted(webidls.all_preprocessed_sources()):
            basename = os.path.basename(source)
            rule = mk.create_rule([basename])
            rule.add_dependencies([
                source,
                "backend.mk",
                "Makefile",
                "$(DEPTH)/config/autoconf.mk",
                "$(topsrcdir)/config/config.mk",
            ])
            rule.add_commands([
                # Remove the file before writing so bindings that go from
                # static to preprocessed don't end up writing to a symlink,
                # which would modify content in the source directory.
                "$(RM) $@",
                "$(call py_action,preprocessor $@,$(DEFINES) $(ACDEFINES) $< -o $@)",
            ])

        self._add_unified_build_rules(
            mk,
            unified_source_mapping,
            unified_files_makefile_variable="unified_binding_cpp_files",
        )

        webidls_mk = mozpath.join(bindings_dir, "webidlsrcs.mk")
        with self._write_file(webidls_mk) as fh:
            mk.dump(fh, removal_guard=False)

        backend_file = self._get_backend_file_for(webidls)
        for f in sorted(global_define_files):
            backend_file.write(f"CPPSRCS += {f}\n")
        backend_file.write("CPPSRCS += $(unified_binding_cpp_files)\n")

        # Add the test directory to the compile graph.
        if self.environment.substs.get("ENABLE_TESTS"):
            self._compile_graph[
                mozpath.join(
                    mozpath.relpath(bindings_dir, self.environment.topobjdir),
                    "test",
                    "target-objects",
                )
            ]

    def _format_generated_file_input_name(self, path, obj):
        if obj.localized:
            # Localized generated files can have locale-specific inputs, which
            # are indicated by paths starting with `en-US/` or containing
            # `locales/en-US/`.
            if "locales/en-US" in path:
                # We need an "absolute source path" relative to
                # topsrcdir, like "/source/path".
                if not path.startswith("/"):
                    path = "/" + mozpath.relpath(path.full_path, obj.topsrcdir)
                e, f = path.split("locales/en-US/", 1)
                assert f
                return "$(call MERGE_RELATIVE_FILE,{},{}locales)".format(
                    f, e if not e.startswith("/") else e[len("/") :]
                )
            elif path.startswith("en-US/"):
                e, f = path.split("en-US/", 1)
                assert not e
                return f"$(call MERGE_RELATIVE_FILE,{f},{obj.relsrcdir})"
            return self._pretty_path(path, self._get_backend_file_for(obj))
        else:
            return self._pretty_path(path, self._get_backend_file_for(obj))

    def _format_generated_file_output_name(self, path, obj):
        if not isinstance(path, Path):
            path = ObjDirPath(obj._context, "!" + path)
        return self._pretty_path(path, self._get_backend_file_for(obj))
