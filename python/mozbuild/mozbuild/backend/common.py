# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import itertools
import json
import os
import pathlib
from collections import defaultdict
from operator import itemgetter

import mozpack.path as mozpath

from mozbuild.backend.base import BuildBackend
from mozbuild.dirutils import mkdir
from mozbuild.frontend.context import ObjDirPath
from mozbuild.frontend.data import (
    BaseProgram,
    BaseRustLibrary,
    BaseRustProgram,
    ComputedFlags,
    ConfigFileSubstitution,
    Exports,
    FinalTargetPreprocessedFiles,
    GeneratedFile,
    HostLibrary,
    HostRustLibrary,
    HostSources,
    IPDLCollection,
    JsShellArchive,
    RustTests,
    SandboxedWasmLibrary,
    SharedLibrary,
    Sources,
    StaticLibrary,
    UnifiedSources,
    WebIDLCollection,
    XPCOMComponentManifests,
    XPIArchive,
    XPIDLModule,
)
from mozbuild.frontend.l10n_manifest import (
    L10nManifestContext,
    build_l10n_manifest_from_substs,
    write_l10n_manifest,
)
from mozbuild.frontend.reader import SandboxValidationError
from mozbuild.licenses import LicenseCollection, LicenseError
from mozbuild.rust_commands import CARGO_SPEC_FILES, CargoCommand, cargo_spec


def configured_defines(environment):
    """The defines a preprocessed install carries beyond its own declaration.

    Recording them on the entry tells the install what to preprocess with, and
    makes a changed define rebuild the files it applies to.
    """
    return {**environment.defines, "AB_CD": environment.substs["MOZ_UI_LOCALE"]}


def _preprocessor_defines(declared, configured):
    """The defines as the preprocessor receives them from a command line.

    A define set to True becomes 1, one set to False is not present at all, and
    a digit valued one is a number.
    """
    defines = {}
    # A configured define wins over one that the declaring moz.build sets.
    for name, value in {**(declared or {}), **configured}.items():
        if value is False:
            continue
        if value is True:
            defines[name] = 1
        elif isinstance(value, str) and value.isdigit():
            defines[name] = int(value)
        else:
            defines[name] = value
    return defines


def add_preprocess_entry(
    manifest,
    source,
    install_target,
    dest,
    basename,
    depdir,
    declared,
    configured,
    **kwargs,
):
    """Describe a preprocessed install to the manifest installing it.

    Both backends have to agree on the marker and on where the dependency file
    goes, or the same declaration would preprocess differently depending on who
    installed it.
    """
    if basename.endswith(".css"):
        kwargs["marker"] = "%"
    depfile = mozpath.join(depdir, install_target, dest, f"{basename}.pp")
    manifest.add_preprocess(
        source,
        mozpath.join(dest, basename),
        depfile,
        defines=_preprocessor_defines(declared, configured),
        **kwargs,
    )


def pattern_link_parts(source):
    """Split a wildcard source into the directory to search and the pattern.

    The search root ends at the first component carrying a wildcard, so what
    the pattern matches is the path below the destination.
    """
    parts = mozpath.split(source)
    for i, part in enumerate(parts):
        if "*" in part:
            return "/".join(parts[:i]), "/".join(parts[i:])
    raise ValueError(f"{source} has no wildcard to split on")


def pattern_link_dest(source, install_dir):
    """Where a wildcard's matches land under an install directory.

    When the destination's basename differs from the pattern's, the destination
    names the directory the matches go in. Otherwise the matched path already
    supplies the basename.
    """
    if source.target_basename != mozpath.basename(source.full_path):
        return mozpath.join(install_dir, source.target_basename)
    return install_dir


def record_chrome_manifest_entry(entries, obj):
    """Collect a registration under the manifest it is written to.

    A manifest other than the install target's own chrome.manifest is reached
    through a `manifest` line in that file, so that line is recorded as well.
    """
    top_level = mozpath.join(obj.install_target, "chrome.manifest")
    if obj.path != top_level:
        entries[top_level].add(
            f"manifest {mozpath.relpath(obj.path, obj.install_target)}"
        )
    entries[obj.path].add(str(obj.entry))


def chrome_manifest_contents(entries):
    """Each chrome manifest with the content its registrations make."""
    for target, lines in sorted(entries.items()):
        yield target, "".join(f"{line}\n" for line in sorted(lines))


class XPIDLManager:
    """Helps manage XPCOM IDLs in the context of the build system."""

    class Module:
        def __init__(self):
            self.idl_files = set()
            self.directories = set()
            self._stems = set()

        def add_idls(self, idls):
            self.idl_files.update(idl.full_path for idl in idls)
            self.directories.update(mozpath.dirname(idl.full_path) for idl in idls)
            self._stems.update(
                mozpath.splitext(mozpath.basename(idl))[0] for idl in idls
            )

        def stems(self):
            return iter(self._stems)

    def __init__(self, config):
        self.config = config
        self.topsrcdir = config.topsrcdir
        self.topobjdir = config.topobjdir

        self._idls = set()
        self.modules = defaultdict(self.Module)

    def link_module(self, module):
        """Links an XPIDL module with with this instance."""
        for idl in module.idl_files:
            basename = mozpath.basename(idl.full_path)

            if basename in self._idls:
                raise Exception("IDL already registered: %s" % basename)
            self._idls.add(basename)

        self.modules[module.name].add_idls(module.idl_files)

    def idl_stems(self):
        """Return an iterator of stems of the managed IDL files.

        The stem of an IDL file is the basename of the file with no .idl extension.
        """
        return itertools.chain(*[m.stems() for m in self.modules.values()])


class BinariesCollection:
    """Tracks state of binaries produced by the build."""

    def __init__(self):
        self.shared_libraries = []
        self.programs = []


class CommonBackend(BuildBackend):
    """Holds logic common to all build backends."""

    def _init(self):
        self._idl_manager = XPIDLManager(self.environment)
        self._binaries = BinariesCollection()
        self._configs = set()
        self._generated_sources = set()
        self._l10n_manifest_data = []
        self._licenses = LicenseCollection()
        self._xpi_archives = []
        self._computed_flags = defaultdict(list)
        # Collect Rust edges until all per directory flags are available.
        self._rust_libraries_for_spec = []
        self._rust_programs_for_spec = defaultdict(list)
        self._rust_tests_for_spec = []

    def _computed_flag_list(self, relobjdir, var):
        """Return all values for a computed flag in one directory."""
        out = []
        for cf in self._computed_flags.get(relobjdir, ()):
            out.extend(dict(cf.get_flags()).get(var, []))
        return out

    def _rust_computed_flags(self, relobjdir):
        return {
            "computed_cflags": self._computed_flag_list(relobjdir, "CFLAGS"),
            "computed_cxxflags": self._computed_flag_list(relobjdir, "CXXFLAGS"),
            "computed_host_cflags": self._computed_flag_list(relobjdir, "HOST_CFLAGS"),
            "computed_host_cxxflags": self._computed_flag_list(
                relobjdir, "HOST_CXXFLAGS"
            ),
            "link_flags": self._computed_flag_list(relobjdir, "LDFLAGS"),
        }

    def _rust_library_command(self, lib, profile_suffix):
        is_host = isinstance(lib, HostRustLibrary)
        return CargoCommand(
            kind="host-library" if is_host else "library",
            manifest_path=mozpath.normsep(lib.cargo_file),
            names=(lib.lib_name,),
            outputs=(
                mozpath.relpath(lib.import_path.full_path, self.environment.topobjdir),
            ),
            features=lib.features or (),
            cargo_profile_suffix=profile_suffix,
            cargo_crate_type=lib.cargo_crate_type,
            lto=not lib.no_lto,
            working_directory=mozpath.normsep(lib.objdir),
            **self._rust_computed_flags(lib.relobjdir),
        )

    def _rust_program_command(self, programs, kind, profile_suffix):
        """Build one Cargo command for all Rust programs of one kind in a directory."""
        first = programs[0]
        objdir = mozpath.normsep(first.objdir)
        rustc_flags = ()
        # Windows programs link a version resource generated in the object
        # directory.
        if (
            kind == "program"
            and self.environment.substs.get("MOZ_WIDGET_TOOLKIT") == "windows"
        ):
            rustc_flags = ("-C", f"link-arg={objdir}/module.res")
        return CargoCommand(
            kind=kind,
            manifest_path=mozpath.normsep(first.cargo_file),
            names=tuple(program.name for program in programs),
            outputs=tuple(mozpath.normsep(program.location) for program in programs),
            features=first.features or (),
            cargo_profile_suffix=profile_suffix,
            working_directory=objdir,
            rustc_flags=rustc_flags,
            **self._rust_computed_flags(first.relobjdir),
        )

    def _rust_tests_command(self, tests, profile_suffix=""):
        """Build a Cargo test command using the directory's Cargo profile."""
        rustflags = ()
        substs = self.environment.substs
        # Test executables load shared libraries from dist/bin. Other platforms
        # use an rpath, while Windows stages the libraries next to the test
        # binaries.
        if substs.get("OS_TARGET") != "WINNT":
            dist_bin = mozpath.join(self.environment.topobjdir, "dist", "bin")
            rustflags = ("-C", f"link-arg=-Wl,-rpath,{dist_bin}")
        # Some crates reference Gecko or breakpad functions that their tests
        # never call. `-FORCE:UNRESOLVED` lets every clang-cl test binary link
        # with unresolved symbols, and a test that calls one crashes.
        elif substs.get("CC_TYPE") == "clang-cl":
            rustflags = ("-C", "link-arg=-FORCE:UNRESOLVED")
        return CargoCommand(
            kind="test",
            manifest_path=mozpath.normsep(mozpath.join(tests.srcdir, "Cargo.toml")),
            names=tests.names,
            features=tests.features or (),
            cargo_profile_suffix=profile_suffix,
            working_directory=mozpath.normsep(tests.objdir),
            rustflags=rustflags,
            **self._rust_computed_flags(tests.relobjdir),
        )

    def _rust_commands(self):
        """Yield each spec path and Cargo command after collecting directory flags."""
        # Every Rust edge in a directory containing a target library with a custom
        # Cargo profile uses that profile.
        profile_suffixes = {
            lib.relobjdir: lib.cargo_profile_suffix
            for lib in self._rust_libraries_for_spec
            if lib.KIND == "target" and lib.cargo_profile_suffix
        }
        for lib in self._rust_libraries_for_spec:
            command = self._rust_library_command(
                lib, profile_suffixes.get(lib.relobjdir, "")
            )
            path = mozpath.join(lib.objdir, CARGO_SPEC_FILES[command.kind])
            yield (path, command)

        for (relobjdir, kind_attr), programs in self._rust_programs_for_spec.items():
            kind = "program" if kind_attr == "target" else "host-program"
            path = mozpath.join(programs[0].objdir, CARGO_SPEC_FILES[kind])
            command = self._rust_program_command(
                programs, kind, profile_suffixes.get(relobjdir, "")
            )
            yield (path, command)

        for tests in self._rust_tests_for_spec:
            path = mozpath.join(tests.objdir, CARGO_SPEC_FILES["test"])
            command = self._rust_tests_command(
                tests, profile_suffixes.get(tests.relobjdir, "")
            )
            yield (path, command)

    def _write_rust_command(self, path, command):
        spec = cargo_spec(
            command,
            self.environment.substs,
            self.environment.topsrcdir,
            self.environment.topobjdir,
        )
        with self._write_file(path) as fh:
            json.dump(spec, fh, indent=2, sort_keys=True)

    def consume_object(self, obj):
        self._configs.add(obj.config)

        # A duplicate id is only detectable once a second moz.build has been
        # read, so unlike the rest of the license validation this cannot happen
        # in the emitter. Report it the same way the emitter would.
        try:
            consumed = self._licenses.add(obj)
        except LicenseError as error:
            raise SandboxValidationError(str(error), obj._context)
        if consumed:
            self.backend_input_files.update(self._licenses.text_paths)
            return True

        if isinstance(obj, XPIDLModule):
            self._idl_manager.link_module(obj)

        elif isinstance(obj, ConfigFileSubstitution):
            # Do not handle ConfigFileSubstitution for Makefiles. Leave that
            # to other
            if mozpath.basename(obj.output_path) == "Makefile":
                return False
            with self._get_preprocessor(obj) as pp:
                pp.do_include(obj.input_path)
            self.backend_input_files.add(obj.input_path)

        elif isinstance(obj, WebIDLCollection):
            self._handle_webidl_collection(obj)

        elif isinstance(obj, IPDLCollection):
            self._handle_ipdl_sources(
                obj.objdir,
                list(sorted(obj.all_sources())),
                list(sorted(obj.all_preprocessed_sources())),
                list(sorted(obj.all_regular_sources())),
            )

        elif isinstance(obj, XPCOMComponentManifests):
            self._handle_xpcom_collection(obj)

        elif isinstance(obj, UnifiedSources):
            if obj.generated_files:
                self._handle_generated_sources(obj.generated_files)

            # Unified sources aren't relevant to artifact builds.
            if self.environment.is_artifact_build:
                return True

            if obj.have_unified_mapping:
                self._write_unified_files(obj.unified_source_mapping, obj.objdir)
            if hasattr(self, "_process_unified_sources"):
                self._process_unified_sources(obj)

        elif isinstance(obj, ComputedFlags):
            self._computed_flags[obj.relobjdir].append(obj)
            return False

        elif isinstance(obj, XPIArchive):
            self._xpi_archives.append(obj)

        elif isinstance(obj, BaseRustLibrary):
            self._rust_libraries_for_spec.append(obj)
            return False

        elif isinstance(obj, BaseRustProgram):
            self._rust_programs_for_spec[(obj.relobjdir, obj.KIND)].append(obj)
            return False

        elif isinstance(obj, RustTests):
            self._rust_tests_for_spec.append(obj)
            return False

        elif isinstance(obj, BaseProgram):
            self._binaries.programs.append(obj)
            return False

        elif isinstance(obj, SharedLibrary):
            self._binaries.shared_libraries.append(obj)
            return False

        elif isinstance(obj, SandboxedWasmLibrary):
            self._handle_generated_sources([
                mozpath.join(obj.relobjdir, f"{obj.basename}.h")
            ])
            return False
        elif isinstance(obj, (Sources, HostSources)):
            if obj.generated_files:
                self._handle_generated_sources(obj.generated_files)
            return False
        elif isinstance(obj, GeneratedFile):
            for f in obj.outputs:
                if f == "cbindgen-metadata.json":
                    # FIXME (bug 1865785)
                    #
                    # The content of cbindgen-metadata.json is not sorted and
                    # the order is not consistent across multiple runs.
                    #
                    # Exclude this file in order to avoid breaking the
                    # taskcluster/kinds/diffoscope/reproducible.yml jobs.
                    continue
                fullpath = ObjDirPath(obj._context, "!" + f).full_path
                self._handle_generated_sources([fullpath])
            return False

        elif isinstance(obj, Exports):
            objdir_files = [
                f.full_path
                for path, files in obj.files.walk()
                for f in files
                if isinstance(f, ObjDirPath)
            ]
            if objdir_files:
                self._handle_generated_sources(objdir_files)
            return False

        elif isinstance(obj, FinalTargetPreprocessedFiles):
            for path, files in obj.files.walk():
                for f in files:
                    basename = FinalTargetPreprocessedFiles.get_obj_basename(f)
                    relpath = mozpath.join(obj.install_target, path, basename)
                    self._handle_generated_sources([
                        ObjDirPath(obj._context, "!/" + relpath).full_path
                    ])
            return False

        elif isinstance(obj, JsShellArchive):
            self._process_js_shell_archive(obj)

        elif isinstance(obj, L10nManifestContext):
            self._l10n_manifest_data.append(obj.data)

        else:
            return False

        return True

    def _process_js_shell_archive(self, obj):
        manifest_path = mozpath.join(self.environment.topobjdir, "jsshell-archive.list")
        with self._write_file(manifest_path) as fh:
            for f in obj.files:
                fh.write(f.target_basename + "\n")

    def _write_licenses_json(self):
        """Write the tree's LICENSES declarations for the generator and the SBOM.

        Only licenses reachable in this configuration appear, because an
        unconfigured directory is never traversed.
        """
        path = mozpath.join(self.environment.topobjdir, "licenses.json")
        with self._write_file(path) as fh:
            json.dump(
                {"licenses": self._licenses.records()}, fh, sort_keys=True, indent=2
            )

    def consume_finished(self):
        if len(self._idl_manager.modules):
            self._write_rust_xpidl_summary(self._idl_manager)
            self._handle_idl_manager(self._idl_manager)
            self._handle_xpidl_sources()

        for config in self._configs:
            self.backend_input_files.add(config.source)

        # Write out a machine-readable file describing binaries.
        topobjdir = self.environment.topobjdir
        with self._write_file(mozpath.join(topobjdir, "binaries.json")) as fh:
            d = {
                "shared_libraries": sorted(
                    (s.to_dict() for s in self._binaries.shared_libraries),
                    key=itemgetter("basename"),
                ),
                "programs": sorted(
                    (p.to_dict() for p in self._binaries.programs),
                    key=itemgetter("program"),
                ),
            }
            json.dump(d, fh, sort_keys=True, indent=4)

        self._write_licenses_json()

        # Write out a file listing generated sources.
        with self._write_file(mozpath.join(topobjdir, "generated-sources.json")) as fh:
            d = {"sources": sorted(self._generated_sources)}
            json.dump(d, fh, sort_keys=True, indent=4)

        if self._l10n_manifest_data:
            manifest = build_l10n_manifest_from_substs(
                self.environment.substs, self._l10n_manifest_data
            )
            write_l10n_manifest(manifest, pathlib.Path(topobjdir, "l10n-manifest.json"))

        test_crates = set()
        for path, command in self._rust_commands():
            self._write_rust_command(path, command)
            if command.kind == "test":
                test_crates.update(command.names)

        with self._write_file(mozpath.join(topobjdir, "rust-tests.json")) as fh:
            json.dump({"crates": sorted(test_crates)}, fh, sort_keys=True, indent=2)

    def _expand_libs(self, input_bin):
        os_libs = []
        shared_libs = []
        static_libs = []
        objs = []

        seen_objs = set()
        seen_libs = set()

        def add_objs(lib):
            for o in lib.objs:
                if o in seen_objs:
                    continue

                seen_objs.add(o)
                objs.append(o)

        def expand(lib, recurse_objs, system_libs):
            if isinstance(lib, (HostLibrary, StaticLibrary, SandboxedWasmLibrary)):
                if lib.no_expand_lib:
                    static_libs.append(lib)
                    recurse_objs = False
                elif recurse_objs:
                    add_objs(lib)

                for l in lib.linked_libraries:
                    expand(l, recurse_objs, system_libs)

                if system_libs:
                    for l in lib.linked_system_libs:
                        if l not in seen_libs:
                            seen_libs.add(l)
                            os_libs.append(l)

            elif isinstance(lib, SharedLibrary):
                if lib not in seen_libs:
                    seen_libs.add(lib)
                    shared_libs.append(lib)

        add_objs(input_bin)

        system_libs = not isinstance(
            input_bin, (HostLibrary, StaticLibrary, SandboxedWasmLibrary)
        )
        for lib in input_bin.linked_libraries:
            if isinstance(lib, (HostLibrary, StaticLibrary, SandboxedWasmLibrary)):
                expand(lib, True, system_libs)
            elif isinstance(lib, SharedLibrary):
                if lib not in seen_libs:
                    seen_libs.add(lib)
                    shared_libs.append(lib)

        for lib in input_bin.linked_system_libs:
            if lib not in seen_libs:
                seen_libs.add(lib)
                os_libs.append(lib)

        return (objs, shared_libs, os_libs, static_libs)

    def _make_ar_response_file(self, objdir, objs, name):
        if not objs:
            return None

        if not self.environment.substs.get("AR_SUPPORTS_RESPONSE_FILE"):
            return None

        response_file_path = mozpath.join(objdir, name)
        ref = "@" + response_file_path
        content = "\n".join(objs)

        mkdir(objdir)
        with self._write_file(response_file_path) as fh:
            fh.write(content)

        return ref

    def _make_list_file(self, kind, objdir, objs, name):
        if not objs:
            return None
        if kind == "target":
            list_style = self.environment.substs.get("EXPAND_LIBS_LIST_STYLE")
        else:
            # The host compiler is not necessarily the same kind as the target
            # compiler, so we can't be sure EXPAND_LIBS_LIST_STYLE is the right
            # style to use ; however, all compilers support the `list` type, so
            # use that. That doesn't cause any practical problem because where
            # it really matters to use something else than `list` is when
            # linking tons of objects (because of command line argument limits),
            # which only really happens for libxul.
            list_style = "list"
        list_file_path = mozpath.join(objdir, name)
        objs = [os.path.relpath(o, objdir) for o in objs]
        content = "\n".join(objs)
        if list_style == "filelist":
            ref = "-Wl,-filelist," + list_file_path
        elif list_style == "linkerlist":
            ref = "-Wl,@" + list_file_path
        elif list_style == "list":
            ref = "@" + list_file_path
        else:
            return None

        mkdir(objdir)
        with self._write_file(list_file_path) as fh:
            fh.write(content)

        return ref

    def _handle_generated_sources(self, files):
        self._generated_sources.update(
            mozpath.relpath(f, self.environment.topobjdir) for f in files
        )

    def _handle_xpidl_sources(self):
        bindings_rt_dir = mozpath.join(
            self.environment.topobjdir, "dist", "xpcrs", "rt"
        )
        bindings_bt_dir = mozpath.join(
            self.environment.topobjdir, "dist", "xpcrs", "bt"
        )
        include_dir = mozpath.join(self.environment.topobjdir, "dist", "include")

        self._handle_generated_sources(
            itertools.chain.from_iterable(
                (
                    mozpath.join(include_dir, "%s.h" % stem),
                    mozpath.join(bindings_rt_dir, "%s.rs" % stem),
                    mozpath.join(bindings_bt_dir, "%s.rs" % stem),
                )
                for stem in self._idl_manager.idl_stems()
            )
        )

        self._handle_generated_sources([
            mozpath.join(bindings_rt_dir, "all.rs"),
            mozpath.join(bindings_bt_dir, "all.rs"),
        ])

    def _handle_webidl_collection(self, webidls):
        bindings_dir = mozpath.join(self.environment.topobjdir, "dom", "bindings")

        all_inputs = set(webidls.all_static_sources())
        for s in webidls.all_non_static_basenames():
            all_inputs.add(mozpath.join(bindings_dir, s))

        generated_events_stems = webidls.generated_events_stems()
        exported_stems = webidls.all_regular_stems()

        # The WebIDL manager reads configuration from a JSON file. So, we
        # need to write this file early.
        o = dict(
            webidls=sorted(all_inputs),
            generated_events_stems=sorted(generated_events_stems),
            exported_stems=sorted(exported_stems),
            example_interfaces=sorted(webidls.example_interfaces),
        )

        file_lists = mozpath.join(bindings_dir, "file-lists.json")
        with self._write_file(file_lists) as fh:
            json.dump(o, fh, sort_keys=True, indent=2)

        import mozwebidlcodegen

        manager = mozwebidlcodegen.create_build_system_manager(
            self.environment.topsrcdir,
            self.environment.topobjdir,
            mozpath.join(self.environment.topobjdir, "dist"),
        )
        self._handle_generated_sources(manager.expected_build_output_files())
        self._write_unified_files(
            webidls.unified_source_mapping, bindings_dir, poison_windows_h=True
        )
        self._handle_webidl_build(
            bindings_dir,
            webidls.unified_source_mapping,
            webidls,
            manager.expected_build_output_files(),
            manager.GLOBAL_DEFINE_FILES,
        )

    def _handle_xpcom_collection(self, manifests):
        components_dir = mozpath.join(manifests.topobjdir, "xpcom", "components")

        # The code generators read their configuration from this file, so it
        # needs to be written early.
        o = dict(manifests=sorted(manifests.all_sources()))

        conf_file = mozpath.join(components_dir, "manifest-lists.json")
        with self._write_file(conf_file) as fh:
            json.dump(o, fh, sort_keys=True, indent=2)

    def _write_unified_file(
        self, unified_file, source_filenames, output_directory, poison_windows_h=False
    ):
        with self._write_file(mozpath.join(output_directory, unified_file)) as f:
            f.write("#define MOZ_UNIFIED_BUILD\n")
            includeTemplate = '#include "%(cppfile)s"'
            if poison_windows_h:
                includeTemplate += (
                    "\n"
                    "#if defined(_WINDOWS_) && !defined(MOZ_WRAPPED_WINDOWS_H)\n"
                    '#pragma message("wrapper failure reason: " MOZ_WINDOWS_WRAPPER_DISABLED_REASON)\n'  # noqa
                    '#error "%(cppfile)s included unwrapped windows.h"\n'
                    "#endif"
                )
            includeTemplate += (
                "\n"
                "#ifdef PL_ARENA_CONST_ALIGN_MASK\n"
                '#error "%(cppfile)s uses PL_ARENA_CONST_ALIGN_MASK, '
                'so it cannot be built in unified mode."\n'
                "#undef PL_ARENA_CONST_ALIGN_MASK\n"
                "#endif\n"
                "#ifdef INITGUID\n"
                '#error "%(cppfile)s defines INITGUID, '
                'so it cannot be built in unified mode."\n'
                "#undef INITGUID\n"
                "#endif"
            )
            for s in source_filenames:
                # Prefer a relative path to make the output not depend on the sourcedir.
                # This makes caching across worktrees possible.
                if os.path.isabs(s):
                    rel_path = mozpath.relpath(s, output_directory)
                else:
                    rel_path = s
                f.write(includeTemplate % {"cppfile": rel_path})
                f.write("\n")

    def _write_unified_files(
        self, unified_source_mapping, output_directory, poison_windows_h=False
    ):
        for unified_file, source_filenames in unified_source_mapping:
            self._write_unified_file(
                unified_file, source_filenames, output_directory, poison_windows_h
            )

    def localized_path(self, relativesrcdir, filename):
        """Return the localized path for a file.

        Given ``relativesrcdir``, a path relative to the topsrcdir, return a path to ``filename``
        from the current locale as specified by ``MOZ_UI_LOCALE``, using ``L10NBASEDIR`` as the
        parent directory for non-en-US locales.
        """
        ab_cd = self.environment.substs["MOZ_UI_LOCALE"]
        l10nbase = mozpath.join(self.environment.substs["L10NBASEDIR"], ab_cd)
        # Filenames from LOCALIZED_FILES will start with en-US/.
        if filename.startswith("en-US/"):
            e, filename = filename.split("en-US/")
            assert not e
        if ab_cd == "en-US":
            return mozpath.join(
                self.environment.topsrcdir, relativesrcdir, "en-US", filename
            )
        if mozpath.basename(relativesrcdir) == "locales":
            l10nrelsrcdir = mozpath.dirname(relativesrcdir)
        else:
            l10nrelsrcdir = relativesrcdir
        return mozpath.join(l10nbase, l10nrelsrcdir, filename)

    def _write_rust_xpidl_summary(self, manager):
        """Write out a rust file which includes the generated xpcom rust modules"""
        topobjdir = self.environment.topobjdir

        include_tmpl = 'include!(mozbuild::objdir_path!("dist/xpcrs/%s/%s.rs"))'

        # Ensure deterministic output files.
        stems = sorted(manager.idl_stems())

        with self._write_file(
            mozpath.join(topobjdir, "dist", "xpcrs", "rt", "all.rs")
        ) as fh:
            fh.write("// THIS FILE IS GENERATED - DO NOT EDIT\n\n")
            for stem in stems:
                fh.write(include_tmpl % ("rt", stem))
                fh.write(";\n")

        with self._write_file(
            mozpath.join(topobjdir, "dist", "xpcrs", "bt", "all.rs")
        ) as fh:
            fh.write("// THIS FILE IS GENERATED - DO NOT EDIT\n\n")
            fh.write("&[\n")
            for stem in stems:
                fh.write(include_tmpl % ("bt", stem))
                fh.write(",\n")
            fh.write("]\n")
