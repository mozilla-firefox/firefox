# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# This modules provides functionality for dealing with code completion.

import os
from collections import defaultdict

import mozpack.path as mozpath

from mozbuild.backend.common import CommonBackend
from mozbuild.frontend.data import (
    ComputedFlags,
    DirectoryTraversal,
    Headers,
    PerSourceFlag,
    Sources,
)
from mozbuild.util import expand_variables


class CompileDBBackend(CommonBackend):
    def _init(self):
        CommonBackend._init(self)

        # The database we're going to dump out to.
        self._db = {}

        # The cache for per-directory flags
        self._flags = {}

        self._envs = {}
        self._local_flags = defaultdict(dict)
        self._per_source_flags = defaultdict(list)
        self._headers = []
        self._source_suffixes = defaultdict(set)

    def _build_cmd(self, cmd, filename, unified):
        cmd = list(cmd)
        if unified is None:
            cmd.append(filename)
        else:
            cmd.append(unified)

        return cmd

    def consume_object(self, obj):
        # Those are difficult directories, that will be handled later.
        if obj.relsrcdir in (
            "build/unix/elfhack",
            "build/unix/elfhack/inject",
            "build/clang-plugin",
            "build/clang-plugin/tests",
        ):
            return True

        consumed = CommonBackend.consume_object(self, obj)

        if consumed:
            return True

        if isinstance(obj, DirectoryTraversal):
            self._envs[obj.objdir] = obj.config

        elif isinstance(obj, Sources):
            # For other sources, include each source file.
            for f in obj.files:
                self._build_db_line(
                    obj.objdir, obj.relsrcdir, obj.config, f, obj.canonical_suffix
                )

        elif isinstance(obj, Headers):
            self._headers.append(obj)

        elif isinstance(obj, PerSourceFlag):
            self._per_source_flags[obj.file_name].extend(obj.flags)

        elif isinstance(obj, ComputedFlags):
            for var, flags in obj.get_flags():
                self._local_flags[obj.objdir]["COMPUTED_%s" % var] = flags

        return True

    def consume_finished(self):
        CommonBackend.consume_finished(self)

        # The order is the priority used to pick a header's language.
        header_suffixes = {
            objdir: next(
                (s for s in (".cpp", ".mm", ".m", ".c") if s in suffixes), ".h"
            )
            for objdir, suffixes in self._source_suffixes.items()
        }
        for obj in self._headers:
            suffix = header_suffixes.get(obj.objdir, ".h")
            # FIXME: Currently skip generated headers in obj.generated_files.
            for f in obj.static_files:
                db_line = self._build_db_line(
                    obj.objdir, obj.relsrcdir, obj.config, f, suffix
                )
                db_line.append(f"-x{self.HEADER_LANGUAGES[suffix]}")

        db = []

        for (directory, filename, unified), cmd in self._db.items():
            env = self._envs[directory]
            cmd = self._build_cmd(cmd, filename, unified)
            variables = {
                "DIST": mozpath.join(env.topobjdir, "dist"),
                "DEPTH": env.topobjdir,
                "MOZILLA_DIR": env.topsrcdir,
                "topsrcdir": env.topsrcdir,
                "topobjdir": env.topobjdir,
            }
            variables.update(self._local_flags[directory])
            c = []
            for a in cmd:
                accum = ""
                for word in expand_variables(a, variables).split():
                    # We can't just split() the output of expand_variables since
                    # there can be spaces enclosed by quotes, e.g. '"foo bar"'.
                    # Handle that case by checking whether there are an even
                    # number of double-quotes in the word and appending it to
                    # the accumulator if not. Meanwhile, shlex.split() and
                    # mozshellutil.split() aren't able to properly handle
                    # this and break in various ways, so we can't use something
                    # off-the-shelf.
                    has_quote = bool(word.count('"') % 2)
                    if accum and has_quote:
                        c.append(accum + " " + word)
                        accum = ""
                    elif accum and not has_quote:
                        accum += " " + word
                    elif not accum and has_quote:
                        accum = word
                    else:
                        c.append(word)
            # Tell clangd to keep parsing to the end of a file, regardless of
            # how many errors are encountered. (Unified builds mean that we
            # encounter a lot of errors parsing some files.)
            c.insert(-1, "-ferror-limit=0")

            # C++ header entries add -xc++ to tell clang to treat the .h file
            # as C++. On Windows, CXXFLAGS also contains -TP, clang-cl's own
            # "treat as C++" flag. With both present, clang warns that -TP is
            # unused, so drop it.
            if "-xc++" in c:
                c = [arg for arg in c if arg != "-TP"]

            per_source_flags = self._per_source_flags.get(filename)
            if per_source_flags is not None:
                c.extend(per_source_flags)
            db.append({
                "directory": directory,
                "arguments": c,
                "file": mozpath.join(directory, filename),
            })

        import json

        outputfile = self._outputfile_path()
        with self._write_file(outputfile) as jsonout:
            json.dump(db, jsonout, indent=0)

    def _outputfile_path(self):
        # Output the database (a JSON file) to objdir/compile_commands.json
        return os.path.join(self.environment.topobjdir, "compile_commands.json")

    def _process_unified_sources_without_mapping(self, obj):
        for f in list(sorted(obj.files)):
            self._build_db_line(
                obj.objdir, obj.relsrcdir, obj.config, f, obj.canonical_suffix
            )

    def _process_unified_sources(self, obj):
        if not obj.have_unified_mapping:
            return self._process_unified_sources_without_mapping(obj)

        # For unified sources, only include the unified source file.
        # Note that unified sources are never used for host sources.
        for f in obj.unified_source_mapping:
            self._build_db_line(
                obj.objdir, obj.relsrcdir, obj.config, f[0], obj.canonical_suffix
            )
            for entry in f[1]:
                self._build_db_line(
                    obj.objdir,
                    obj.relsrcdir,
                    obj.config,
                    entry,
                    obj.canonical_suffix,
                    unified=f[0],
                )

    def _handle_idl_manager(self, idl_manager):
        pass

    def _handle_ipdl_sources(
        self,
        ipdl_dir,
        sorted_ipdl_sources,
        sorted_nonstatic_ipdl_sources,
        sorted_static_ipdl_sources,
    ):
        pass

    def _handle_webidl_build(
        self,
        bindings_dir,
        unified_source_mapping,
        webidls,
        expected_build_output_files,
        global_define_files,
    ):
        for f in unified_source_mapping:
            self._build_db_line(bindings_dir, None, self.environment, f[0], ".cpp")
        for f in global_define_files:
            self._build_db_line(bindings_dir, None, self.environment, f, ".cpp")

    COMPILERS = {
        ".c": "CC",
        ".h": "CXX",
        ".cpp": "CXX",
        ".m": "CC",
        ".mm": "CXX",
    }

    CFLAGS = {
        ".c": "CFLAGS",
        ".h": "CXXFLAGS",
        ".cpp": "CXXFLAGS",
        ".m": "CFLAGS",
        ".mm": "CXXFLAGS",
    }

    HEADER_LANGUAGES = {
        ".c": "c",
        ".h": "c++",
        ".cpp": "c++",
        ".m": "objective-c",
        ".mm": "objective-c++",
    }

    def _get_compiler_args(self, cenv, canonical_suffix):
        if canonical_suffix not in self.COMPILERS:
            return None
        return cenv.substs[self.COMPILERS[canonical_suffix]]

    def _build_db_line(
        self, objdir, reldir, cenv, filename, canonical_suffix, unified=None
    ):
        compiler_args = self._get_compiler_args(cenv, canonical_suffix)
        if compiler_args is None:
            return
        self._source_suffixes[objdir].add(canonical_suffix)
        db = self._db.setdefault(
            (objdir, filename, unified),
            compiler_args + ["-o", "/dev/null", "-c"],
        )
        reldir = reldir or mozpath.relpath(objdir, cenv.topobjdir)

        db.append("$(COMPUTED_%s)" % self.CFLAGS[canonical_suffix])
        if canonical_suffix == ".m":
            db.append("$(COMPUTED_CMFLAGS)")
        elif canonical_suffix == ".mm":
            db.append("$(COMPUTED_CMMFLAGS)")
        return db
