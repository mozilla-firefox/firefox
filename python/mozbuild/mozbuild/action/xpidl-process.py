#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This script is used to generate an output header and xpt file for
# input IDL file(s). It's purpose is to directly support the build
# system. The API will change to meet the needs of the build system.

import argparse
import os
import sys

from buildconfig import topsrcdir
from mozpack import path as mozpath
from xpidl import jsonxpt
from xpidl.header import print_header
from xpidl.rust import print_rust_bindings
from xpidl.rust_macros import print_rust_macros_bindings
from xpidl.xpidl import IDLParser

from mozbuild.makeutil import Makefile
from mozbuild.pythonutil import iter_modules_in_path
from mozbuild.util import FileAvoidWrite


def process(
    input_dirs,
    inc_paths,
    bindings_conf,
    header_dir,
    xpcrs_dir,
    xpt_dir,
    deps_dir,
    module,
    idl_files,
):
    p = IDLParser()

    xpts = []

    mk = Makefile()
    rule = mk.create_rule()

    glbl = {}
    exec(open(bindings_conf, encoding="utf-8").read(), glbl)
    webidlconfig = glbl["DOMInterfaces"]

    # Write out dependencies for Python modules we import. If this list isn't
    # up to date, we will not re-process XPIDL files if the processor changes.
    rule.add_dependencies(s for s in iter_modules_in_path(topsrcdir))

    for path in idl_files:
        basename = os.path.basename(path)
        stem, _ = os.path.splitext(basename)
        idl_data = open(path, encoding="utf-8").read()

        idl = p.parse(idl_data, filename=path)
        idl.resolve(inc_paths, p, webidlconfig)

        header_path = os.path.join(header_dir, f"{stem}.h")
        rs_rt_path = os.path.join(xpcrs_dir, "rt", f"{stem}.rs")
        rs_bt_path = os.path.join(xpcrs_dir, "bt", f"{stem}.rs")

        xpts.append(jsonxpt.build_typelib(idl))

        rule.add_dependencies(idl.deps)

        # The print_* functions don't actually do anything with the
        # passed-in path other than writing it into the file to let people
        # know where the original source was.  This script receives
        # absolute paths, which are not so great to embed in header files
        # (they mess with deterministic generation of files on different
        # machines, Searchfox logic, shared compilation caches, etc.), so
        # we pass in fake paths that are the same across compilations, but
        # should still enable people to figure out where to go.
        relpath = mozpath.relpath(path, topsrcdir)

        with FileAvoidWrite(header_path) as fh:
            print_header(idl, fh, path, relpath)

        with FileAvoidWrite(rs_rt_path) as fh:
            print_rust_bindings(idl, fh, relpath)

        with FileAvoidWrite(rs_bt_path) as fh:
            print_rust_macros_bindings(idl, fh, relpath)

    # NOTE: We don't use FileAvoidWrite here as we may re-run this code due to a
    # number of different changes in the code, which may not cause the .xpt
    # files to be changed in any way. This means that make will re-run us every
    # time a build is run whether or not anything changed. To fix this we
    # unconditionally write out the file.
    xpt_path = os.path.join(xpt_dir, f"{module}.xpt")
    with open(xpt_path, "w", encoding="utf-8", newline="\n") as fh:
        jsonxpt.write(jsonxpt.link(xpts), fh)

    rule.add_targets([xpt_path])
    if deps_dir:
        deps_path = os.path.join(deps_dir, f"{module}.pp")
        with FileAvoidWrite(deps_path) as fh:
            mk.dump(fh)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--depsdir", help="Directory in which to write dependency files."
    )
    parser.add_argument(
        "--bindings-conf", help="Path to the WebIDL binding configuration file."
    )
    parser.add_argument(
        "--input-dir",
        dest="input_dirs",
        action="append",
        default=[],
        help="Directory(ies) in which to find source .idl files.",
    )
    parser.add_argument("headerdir", help="Directory in which to write header files.")
    parser.add_argument(
        "xpcrsdir", help="Directory in which to write rust xpcom binding files."
    )
    parser.add_argument("xptdir", help="Directory in which to write xpt file.")
    parser.add_argument(
        "module", help="Final module name to use for linked output xpt file."
    )
    parser.add_argument("idls", nargs="+", help="Source .idl file(s).")
    parser.add_argument(
        "-I",
        dest="incpath",
        action="append",
        default=[],
        help="Extra directories where to look for included .idl files.",
    )

    args = parser.parse_args(argv)
    incpath = [os.path.join(topsrcdir, p) for p in args.incpath]
    process(
        args.input_dirs,
        incpath,
        args.bindings_conf,
        args.headerdir,
        args.xpcrsdir,
        args.xptdir,
        args.depsdir,
        args.module,
        args.idls,
    )


if __name__ == "__main__":
    main(sys.argv[1:])
