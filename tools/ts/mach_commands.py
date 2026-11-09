# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import sys

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument, SubCommand

targets = ["dom", "nsresult", "services", "xpcom"]


@Command("ts", category="misc", description="Run TypeScript and related commands.")
def ts(ctx):
    """
    TypeScript related commands to build/update typelibs and type-check js.

    Example:
      # Use tsc to check types in extensions framework code:
      $ ./mach ts check toolkit/components/extensions
    """
    ctx._sub_mach(["help", "ts"])
    return 1


@SubCommand("ts", "build", description="Build typelibs.")
@CommandArgument("lib", choices=targets, nargs="?")
def build(ctx, lib):
    """Command to build the target typelibs."""

    if lib is None:
        for t in targets:
            if rv := build(ctx, t):
                return rv
        return 0

    types_dir = mozpath.join(ctx.distdir, "@types")
    lib_dts = mozpath.join(types_dir, f"lib.gecko.{lib}.d.ts")
    if not os.path.exists(types_dir):
        os.makedirs(types_dir)

    if lib == "nsresult":
        xpc_msg = mozpath.join(ctx.topsrcdir, "js/xpconnect/src/xpc.msg")
        errors_obj = mozpath.join(ctx.topobjdir, "xpcom/base/error_list.json")
        errors_src = mozpath.join(ctx.topsrcdir, "tools/ts/config/error_list.json")

        if os.path.exists(errors_obj):
            print(f"[INFO] {errors_obj} -> {errors_src}")
            shutil.copy(errors_obj, errors_src)

        return node(ctx, "build_nsresult", lib_dts, xpc_msg, errors_src)

    if lib == "services":
        services_json = mozpath.join(ctx.topobjdir, "xpcom/components/services.json")
        if not os.path.isfile(services_json):
            return build_required(lib, services_json)
        return node(ctx, "build_services", lib_dts, services_json)

    if lib == "xpcom":
        # When we hook this up to be part of the build, we'll have
        # an explicit list of files. For now, just get all of them.
        dir = mozpath.join(ctx.topobjdir, "config/makefiles/xpidl")
        if not os.path.isdir(dir):
            return build_required(lib, dir)

        files = [f for f in os.listdir(dir) if f.endswith(".d.json")]
        if not files:
            return build_required(lib, f"*.d.json files in {dir}")

        return node(ctx, "build_xpcom", lib_dts, dir, *files)

    if lib == "dom":
        # Same as above, get all *.webidl files for now.
        dir = mozpath.join(ctx.topsrcdir, "dom")
        objdir_webidl = mozpath.join(ctx.topobjdir, "dom", "bindings")
        files = []
        for subdir in ["webidl", "chrome-webidl"]:
            for file in os.listdir(mozpath.join(dir, subdir)):
                if file.endswith(".webidl"):
                    files.append(subdir + "/" + file)

        return node(ctx, "build_dom", lib_dts, dir, objdir_webidl, *files)

    raise ValueError(f"Unknown typelib: {lib}")


@SubCommand("ts", "check", description="Check types in a project using tsc.")
@CommandArgument("paths", nargs="+", help="Path to a (dir with) tsconfig.json.")
def check(ctx, paths):
    maybe_setup(ctx)

    for p in paths:
        rv = tsc(ctx, "--project", p)
        if rv:
            return rv
    return 0


@SubCommand("ts", "update", description="Update tools/@types libraries.")
def update(ctx):
    typelib_dir = mozpath.join(ctx.topsrcdir, "tools/@types/generated")
    platforms = ["darwin", "linux", "win32"]
    retval = 0

    for lib in targets + platforms + ["glean"]:
        file = f"lib.gecko.{lib}.d.ts"
        path = mozpath.join(ctx.distdir, "@types", file)
        if not os.path.exists(path):
            if not lib in platforms:
                print(f"[ERROR] {path} not found. Did you run `mach ts build`?")
                retval = 1
            continue

        # This command inherently goes in a confusing direction, we're copying:
        # from `<topobjdir>/dist/@types` files generated with `mach ts build`,
        # into `<topsrcdir>/tools/@types` typelib dir back in your source tree.
        print(f"[INFO] Updating {typelib_dir}/{file}")
        shutil.copy(path, typelib_dir)

    print("[WARNING] Your source tree was updated, you should commit the changes.")
    return retval


@SubCommand("ts", "paths", description="Build module path mapping.")
def paths(ctx):
    maybe_setup(ctx)
    lib = mozpath.join(ctx.topsrcdir, "tools/@types/generated/tspaths.json")
    lazy = mozpath.join(ctx.topsrcdir, "tools/@types/generated/lib.gecko.modules.d.ts")
    return node(ctx, "build_paths", ctx.topsrcdir, lib, lazy)


@SubCommand("ts", "subs", description="Emit substitution .d.ts for processed sources.")
def subs(ctx):
    maybe_setup(ctx)
    processed = [
        # AppConstants.sys.mjs has a (better) manually created declaration file.
        "dist/bin/modules/Readerable.sys.mjs",
        "toolkit/components/nimbus/FeatureManifest.sys.mjs",
        "toolkit/components/promiseworker/worker/PromiseWorker.js",
        "toolkit/components/promiseworker/worker/PromiseWorker.mjs",
        "toolkit/components/resistfingerprinting/RFPTargetConstants.sys.mjs",
    ]
    args = ["--declaration", "--emitDeclarationOnly", "--allowJs", "--outDir"]
    subs_dir = mozpath.join(ctx.topsrcdir, "tools/@types/subs")

    for file in processed:
        path = mozpath.join(ctx.topobjdir, file)
        print(f"[INFO] {path} -> {subs_dir}/{mozpath.basename(path)}")
        tsc(ctx, *args, subs_dir, path)


def tsc(ctx, *args):
    return ctx._sub_mach([
        "node",
        os.path.join(ctx.topsrcdir, "node_modules", "typescript", "bin", "tsc"),
        *args,
    ])


def node(ctx, script, *args):
    maybe_setup(ctx)
    path = mozpath.join(mozpath.dirname(__file__), script)
    return ctx._sub_mach(["node", path, *args])


def maybe_setup(ctx):
    """Check if npm modules are installed, and run setup if needed."""
    sys.path.append(mozpath.join(ctx.topsrcdir, "tools", "lint", "eslint"))
    import setup_helper
    from mozbuild.nodeutil import check_node_executables_valid

    if not check_node_executables_valid():
        return 1

    setup_helper.set_project_root(ctx.topsrcdir)
    setup_helper.eslint_maybe_setup(package_name="TypeScript")


def build_required(lib, item):
    print(f"Missing {item}.")
    print(f"Building {lib} typelib requires a full Firefox build.")
    return 1
