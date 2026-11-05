# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# This module provides a backend static-analysis, like clang-tidy and coverity.
# The main difference between this and the default database backend is that this one
# tracks folders that can be built in the non-unified environment and generates
# the coresponding build commands for the files.

import os

import mozpack.path as mozpath

from mozbuild.compilation.database import CompileDBBackend


class StaticAnalysisBackend(CompileDBBackend):
    def _init(self):
        CompileDBBackend._init(self)
        self.non_unified_build = []

        # List of directories can be built outside of the unified build system.
        non_unified_compat = mozpath.join(
            self.environment.topsrcdir, "build", "non-unified-compat"
        )
        self.backend_input_files.add(non_unified_compat)
        with open(non_unified_compat) as fh:
            content = fh.readlines()
            self.non_unified_build = [
                mozpath.join(self.environment.topsrcdir, line.strip())
                for line in content
            ]

    def _build_cmd(self, cmd, filename, unified):
        cmd = list(cmd)
        # Maybe the file is in non-unified environment or it resides under a directory
        # that can also be built in non-unified environment
        if unified is None or any(
            filename.startswith(path) for path in self.non_unified_build
        ):
            cmd.append(filename)
        else:
            cmd.append(unified)

        return cmd

    def _outputfile_path(self):
        database_path = os.path.join(self.environment.topobjdir, "static-analysis")

        if not os.path.exists(database_path):
            os.mkdir(database_path)

        # Output the database (a JSON file) to objdir/static-analysis/compile_commands.json
        return mozpath.join(database_path, "compile_commands.json")
