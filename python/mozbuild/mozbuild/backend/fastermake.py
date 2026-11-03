# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from collections import defaultdict
from operator import itemgetter

import mozpack.path as mozpath
from mozpack.manifests import InstallManifest

from mozbuild.backend.base import PartialBackend
from mozbuild.backend.common import (
    add_preprocess_entry,
    chrome_manifest_contents,
    configured_defines,
    pattern_link_dest,
    pattern_link_parts,
    record_chrome_manifest_entry,
)
from mozbuild.backend.make import MakeBackend
from mozbuild.frontend.context import ObjDirPath, Path
from mozbuild.frontend.data import (
    ChromeManifestEntry,
    FinalTargetFiles,
    FinalTargetPreprocessedFiles,
    GeneratedFile,
    LocalizedFiles,
    LocalizedPreprocessedFiles,
    XPIDLModule,
)
from mozbuild.makeutil import Makefile


class FasterMakeBackend(MakeBackend, PartialBackend):
    def _init(self):
        super()._init()

        self._manifest_entries = defaultdict(set)

        self._install_manifests = defaultdict(InstallManifest)
        self._configured_defines = configured_defines(self.environment)

        self._dependencies = defaultdict(list)
        self._l10n_dependencies = defaultdict(list)

        self._has_xpidl = False

        self._generated_files_map = {}
        self._generated_files = []

    def _add_preprocess(self, obj, src, dest, f, defines=None, **kwargs):
        add_preprocess_entry(
            self._install_manifests[obj.install_target],
            mozpath.join(obj.srcdir, src),
            obj.install_target,
            dest,
            FinalTargetPreprocessedFiles.get_obj_basename(f),
            mozpath.join(self.environment.topobjdir, "faster", ".deps"),
            defines,
            self._configured_defines,
            **kwargs,
        )

    def consume_object(self, obj):
        if isinstance(
            obj, (FinalTargetFiles, FinalTargetPreprocessedFiles)
        ) and obj.install_target.startswith("dist/bin"):
            ab_cd = self.environment.substs["MOZ_UI_LOCALE"]
            localized = isinstance(obj, (LocalizedFiles, LocalizedPreprocessedFiles))
            defines = obj.defines or {}
            if defines:
                defines = defines.defines
            for path, files in obj.files.walk():
                for f in files:
                    # For localized files we need to find the file from the locale directory.
                    if localized and not isinstance(f, ObjDirPath) and ab_cd != "en-US":
                        src = self.localized_path(obj.relsrcdir, f)

                        dep_target = "install-%s" % obj.install_target

                        if "*" not in src:
                            merge = mozpath.abspath(
                                mozpath.join(
                                    self.environment.topobjdir,
                                    "l10n_merge",
                                    obj.relsrcdir,
                                    f,
                                )
                            )
                            self._l10n_dependencies[dep_target].append((
                                merge,
                                f.full_path,
                                src,
                            ))
                            src = merge
                    else:
                        src = f.full_path

                    if isinstance(obj, FinalTargetPreprocessedFiles):
                        self._add_preprocess(obj, src, path, f, defines=defines)
                    elif "*" in f:
                        prefix, pattern = pattern_link_parts(src)
                        self._install_manifests[obj.install_target].add_pattern_link(
                            prefix, pattern, pattern_link_dest(f, path)
                        )
                    else:
                        self._install_manifests[obj.install_target].add_link(
                            src, mozpath.join(path, f.target_basename)
                        )
                    if isinstance(f, ObjDirPath):
                        dep_target = "install-%s" % obj.install_target
                        dep = mozpath.relpath(f.full_path, self.environment.topobjdir)
                        if dep in self._generated_files_map:
                            # Only the first output file is specified as a
                            # dependency. If there are multiple output files
                            # from a single GENERATED_FILES invocation that are
                            # installed, we only want to run the command once.
                            dep = self._generated_files_map[dep]
                        self._dependencies[dep_target].append(dep)

        elif isinstance(obj, ChromeManifestEntry) and obj.install_target.startswith(
            "dist/bin"
        ):
            record_chrome_manifest_entry(self._manifest_entries, obj)

        elif isinstance(obj, GeneratedFile):
            if obj.outputs:
                first_output = mozpath.relpath(
                    mozpath.join(obj.objdir, obj.outputs[0]), self.environment.topobjdir
                )
                for o in obj.outputs[1:]:
                    fullpath = mozpath.join(obj.objdir, o)
                    self._generated_files_map[
                        mozpath.relpath(fullpath, self.environment.topobjdir)
                    ] = first_output
            self._generated_files.append(obj)
            return False

        elif isinstance(obj, XPIDLModule):
            self._has_xpidl = True
            # We're not actually handling XPIDL files.
            return False

        else:
            return False

        return True

    def consume_finished(self):
        mk = Makefile()
        # Add the default rule at the very beginning.
        mk.create_rule(["default"])
        mk.add_statement("TOPSRCDIR = %s" % self.environment.topsrcdir)
        mk.add_statement("TOPOBJDIR = %s" % self.environment.topobjdir)
        mk.add_statement("MDDEPDIR = .deps")
        mk.add_statement("TOUCH ?= touch")
        mk.add_statement("include $(TOPSRCDIR)/config/makefiles/functions.mk")
        mk.add_statement("include $(TOPSRCDIR)/config/AB_rCD.mk")
        mk.add_statement(f"AB_CD = {self.environment.substs['MOZ_UI_LOCALE']}")
        if not self._has_xpidl:
            mk.add_statement("NO_XPIDL = 1")

        # Add a few necessary variables inherited from configure
        for var in (
            "PYTHON3",
            "MOZ_BUILD_APP",
            "MOZ_WIDGET_TOOLKIT",
        ):
            value = self.environment.substs.get(var)
            if value is not None:
                mk.add_statement("%s = %s" % (var, value))

        install_manifests_bases = self._install_manifests.keys()

        # Add information for chrome manifest generation
        manifest_targets = []

        for target, content in chrome_manifest_contents(self._manifest_entries):
            manifest_targets.append(target)
            install_target = mozpath.basedir(target, install_manifests_bases)
            self._install_manifests[install_target].add_content(
                content, mozpath.relpath(target, install_target)
            )

        # Add information for install manifests.
        mk.add_statement(
            "INSTALL_MANIFESTS = %s" % " ".join(sorted(self._install_manifests.keys()))
        )

        # Add dependencies we inferred:
        for target, deps in sorted(self._dependencies.items()):
            mk.create_rule([target]).add_dependencies(
                "$(TOPOBJDIR)/%s" % d for d in sorted(deps)
            )

        # This is not great, but it's better to have some dependencies on this Python file.
        python_deps = [
            "$(TOPSRCDIR)/third_party/python/moz_l10n/moz/l10n/bin/build_file.py",
        ]
        # Add l10n dependencies we inferred:
        for target, deps in sorted(self._l10n_dependencies.items()):
            mk.create_rule([target]).add_dependencies(
                "%s" % d[0] for d in sorted(deps, key=itemgetter(0))
            )
            for merge, ref_file, l10n_file in deps:
                rule = mk.create_rule([merge]).add_dependencies(
                    [ref_file, l10n_file] + python_deps
                )
                rule.add_commands([
                    "$(PYTHON3) -m moz.l10n.bin.build_file "
                    f"--source {ref_file} --l10n {l10n_file} --target {merge}"
                ])
                # Add a dummy rule for the l10n file since it might not exist.
                mk.create_rule([l10n_file])

        mk.add_statement("include $(TOPSRCDIR)/config/faster/rules.mk")

        for base, install_manifest in self._install_manifests.items():
            with self._write_file(
                mozpath.join(
                    self.environment.topobjdir,
                    "faster",
                    "install_%s" % base.replace("/", "_"),
                )
            ) as fh:
                install_manifest.write(fileobj=fh)

        # Write a single unified manifest for consumption by |mach watch|.
        # Since this doesn't start 'install_', it's not processed by the build.
        unified_manifest = InstallManifest()
        for base, install_manifest in self._install_manifests.items():
            # Expect 'dist/bin/**', which includes 'dist/bin' with no trailing slash.
            assert base.startswith("dist/bin")
            base = base[len("dist/bin") :]
            if base and base[0] == "/":
                base = base[1:]
            unified_manifest.add_entries_from(install_manifest, base=base)

        with self._write_file(
            mozpath.join(
                self.environment.topobjdir, "faster", "unified_install_dist_bin"
            )
        ) as fh:
            unified_manifest.write(fileobj=fh)

        for obj in self._generated_files:
            for stmt in self._format_statements_for_generated_file(obj, "default"):
                mk.add_statement(stmt)

        with self._write_file(
            mozpath.join(self.environment.topobjdir, "faster", "Makefile")
        ) as fh:
            mk.dump(fh, removal_guard=False)

    def _pretty_path(self, path, obj):
        if path.startswith(self.environment.topobjdir):
            return mozpath.join(
                "$(TOPOBJDIR)", mozpath.relpath(path, self.environment.topobjdir)
            )
        elif path.startswith(self.environment.topsrcdir):
            return mozpath.join(
                "$(TOPSRCDIR)", mozpath.relpath(path, self.environment.topsrcdir)
            )
        else:
            return path

    def _format_generated_file_input_name(self, path, obj):
        return self._pretty_path(path.full_path, obj)

    def _format_generated_file_output_name(self, path, obj):
        if not isinstance(path, Path):
            path = ObjDirPath(obj._context, "!" + path)
        return self._pretty_path(path.full_path, obj)
