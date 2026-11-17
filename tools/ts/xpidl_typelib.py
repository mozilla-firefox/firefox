# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import buildconfig
import mozpack.path as mozpath
from xpidl import typescript
from xpidl.xpidl import IDLParser


def xpidl_typelib(topsrcdir, idls):
    """Return the TypeScript data for every XPIDL file in ``idls``."""
    # xpidl resolves an include by file name, so file names must be unique.
    by_name = {}
    for path in idls:
        if (other := by_name.setdefault(mozpath.basename(path), path)) != path:
            raise Exception(f"{path} and {other} should not have the same file name")

    include_dirs = sorted({mozpath.join(topsrcdir, mozpath.dirname(p)) for p in idls})

    parser = IDLParser()
    # The cache maps the full path of each parsed IDL to its result. The key
    # matches the one that Include.resolve builds.
    include_cache = {}
    ts_data = []
    for path in idls:
        fullpath = os.path.join(
            mozpath.join(topsrcdir, mozpath.dirname(path)), mozpath.basename(path)
        )
        if (idl := include_cache.get(fullpath)) is None:
            with open(fullpath, encoding="utf-8") as fh:
                idl = parser.parse(fh.read(), filename=fullpath)
            # The TypeScript output only uses WebIDL type names.
            idl.resolve(
                include_dirs, parser, webidlconfig={}, includeCache=include_cache
            )
            include_cache[fullpath] = idl
        ts_data.append(typescript.ts_source(idl))
    return ts_data


def main(output, idl_list):
    with open(idl_list, encoding="utf-8") as fh:
        idls = fh.read().splitlines()
    typescript.write(xpidl_typelib(buildconfig.topsrcdir, idls), output)
