# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from collections import defaultdict

import mozpack.path as mozpath

C_SUFFIXES = (".c",)
CXX_SUFFIXES = (".cpp", ".cc", ".cxx")
OBJC_SUFFIXES = (".m",)
OBJCXX_SUFFIXES = (".mm",)
HEADER_SUFFIXES = (".h", ".hpp")


def find_compile_db(topobjdir):
    for path in (
        mozpath.join(topobjdir, "clangd", "compile_commands.json"),
        mozpath.join(topobjdir, "compile_commands.json"),
    ):
        if os.path.exists(path):
            return path
    return None


def compile_flags_for(entries, path):
    """Return the compile flags for ``path`` from the compilation database
    ``entries``, without the compiler, output and source arguments.

    A file with no entry of its own, such as a header, takes the flags of a file
    compiled in the nearest directory at or above it, without the flags that
    file sets for itself alone. A ``.c`` file prefers C, then Objective-C, and a
    ``.m`` file the reverse. A ``.mm`` file prefers Objective-C++, then C++. Any
    other file prefers C++, then Objective-C++, and a header then falls back to
    C and Objective-C.
    """

    def key(p):
        return os.path.normcase(mozpath.normpath(p))

    by_file = {}
    by_dir = defaultdict(list)
    for entry in entries:
        k = key(entry["file"])
        by_file[k] = entry
        by_dir[os.path.dirname(k)].append(k)

    path = key(path)
    entry = by_file.get(path)
    own_entry = entry is not None
    if path.endswith(C_SUFFIXES):
        languages = (C_SUFFIXES, OBJC_SUFFIXES)
    elif path.endswith(OBJC_SUFFIXES):
        languages = (OBJC_SUFFIXES, C_SUFFIXES)
    elif path.endswith(OBJCXX_SUFFIXES):
        languages = (OBJCXX_SUFFIXES, CXX_SUFFIXES)
    elif path.endswith(HEADER_SUFFIXES):
        languages = (CXX_SUFFIXES, OBJCXX_SUFFIXES, C_SUFFIXES, OBJC_SUFFIXES)
    else:
        languages = (CXX_SUFFIXES, OBJCXX_SUFFIXES)
    directory = os.path.dirname(path)
    while entry is None:
        for suffixes in languages:
            # The entry with the fewest arguments wins, then the first by name,
            # so the choice does not depend on per source flags or entry order.
            candidate = min(
                (f for f in by_dir.get(directory, ()) if f.endswith(suffixes)),
                key=lambda f: (len(by_file[f]["arguments"]), f),
                default=None,
            )
            if candidate is not None:
                entry = by_file[candidate]
                break
        else:
            parent = os.path.dirname(directory)
            if parent == directory:
                return None
            directory = parent

    args = entry["arguments"]
    output = args.index("-o")
    compiler = next(i for i, arg in enumerate(args) if arg.startswith("-"))
    start = args.index("-c") + 1
    end = args.index("-ferror-limit=0")
    flags = args[compiler:output] + args[start:end]
    if own_entry:
        flags.extend(args[end + 2 :])
    return sanitize_cflags(flags)


def sanitize_cflags(flags):
    # We filter out -Xclang arguments as clang based tools typically choke on
    # passing these flags down to the clang driver.  -Xclang tells the clang
    # driver driver to pass whatever comes after it down to clang cc1, which is
    # why we skip -Xclang and the argument immediately after it.  Here is an
    # example: the following two invocations pass |-foo -bar -baz| to cc1:
    # clang -cc1 -foo -bar -baz
    # clang -Xclang -foo -Xclang -bar -Xclang -baz
    sanitized = []
    saw_xclang = False
    for flag in flags:
        if flag == "-Xclang":
            saw_xclang = True
        elif saw_xclang:
            saw_xclang = False
        else:
            sanitized.append(flag)
    return sanitized
