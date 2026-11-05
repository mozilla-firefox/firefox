# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

# This modules provides functionality for dealing with code completion.

import os

from mach.decorators import Command, CommandArgument
from mozfile import json
from mozshellutil import quote as shell_quote

# Instropection commands.


@Command(
    "compileflags",
    category="devenv",
    description="Display the compilation flags for a given source file",
)
@CommandArgument(
    "what", default=None, help="Source file to display compilation flags for"
)
def compileflags(command_context, what):
    from mozbuild.compilation import util

    db = util.find_compile_db(command_context.topobjdir)
    if db is None:
        print(
            "No compilation database found. Please run |mach build| with no "
            "arguments, or |mach build-backend -b CompileDB|."
        )
        return 1

    arg = command_context._wrap_path_argument(what)
    path = os.path.abspath(arg.objdir_path())
    if not os.path.exists(path):
        path = os.path.abspath(arg.srcdir_path())
    with open(db, encoding="utf-8") as fh:
        flags = util.compile_flags_for(json.load(fh), path)

    if flags is None:
        print(f"No file compiled at or above {what} in the compilation database.")
        return 1

    print(shell_quote(*flags))
