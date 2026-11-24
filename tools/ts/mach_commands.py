# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os
import shutil
import sys
import tempfile

import mozpack.path as mozpath
from mach.decorators import Command, CommandArgument, SubCommand

sys.path.append(mozpath.dirname(__file__))
from typelibs import TYPES_DIR, stage_typelibs


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


@SubCommand("ts", "check", description="Check types in a project using tsc.")
@CommandArgument("paths", nargs="+", help="Path to a (dir with) tsconfig.json.")
def check(ctx, paths):
    maybe_setup(ctx)

    for p in paths:
        rv = tsc(ctx, "--noEmit", "--project", p)
        if rv:
            return rv
    return 0


@SubCommand(
    "ts",
    "summary",
    description="Count the tsc errors in a project by file and cause, or compare "
    "them with a saved run.",
)
@CommandArgument("project", help="Path to a (dir with a) tsconfig.json.")
@CommandArgument(
    "--save", metavar="FILE", help="Write tsc's output to FILE, for a later --diff."
)
@CommandArgument(
    "--diff",
    metavar="BEFORE",
    help="List the errors added or removed since the output saved in BEFORE, "
    "instead of the summary, and exit with 1 if there are any. Not combinable "
    "with --members or --full-lists.",
)
@CommandArgument(
    "--members", metavar="TYPE", help="Also list the properties missing from TYPE."
)
@CommandArgument("--full-lists", action="store_true", help="Do not shorten the lists.")
def summary(ctx, project, save=None, diff=None, members=None, full_lists=False):
    args = [project]
    if save:
        args += ["--save", save]
    if diff:
        args += ["--diff", diff]
    if members:
        args += ["--members", members]
    if full_lists:
        args.append("--full-lists")
    return node(ctx, "summary.mjs", *args)


@SubCommand("ts", "update", description="Update tools/@types libraries.")
def update(ctx):
    if rv := maybe_setup(ctx):
        return rv

    types_dir = mozpath.join(ctx.topsrcdir, TYPES_DIR)
    with tempfile.TemporaryDirectory() as tmp:
        stage_dir = mozpath.join(tmp, "stage")
        if rv := stage_typelibs(
            ctx.topsrcdir,
            ctx.virtualenv_manager.python_path,
            mozpath.join(tmp, "inputs"),
            stage_dir,
            lambda message: ctx.log(
                logging.INFO, "ts", {"message": message}, "{message}"
            ),
        ):
            return rv

        shutil.copytree(stage_dir, types_dir, dirs_exist_ok=True)
        ctx.log(logging.INFO, "ts", {"types_dir": types_dir}, "Updated {types_dir}")
        ctx.log(
            logging.WARNING,
            "ts",
            {},
            "Your source tree was updated, you should commit the changes.",
        )
        return 0


def tsc(ctx, *args):
    return ctx._sub_mach([
        "node",
        os.path.join(ctx.topsrcdir, "node_modules", "typescript", "bin", "tsc"),
        *args,
    ])


def node(ctx, script, *args):
    if rv := maybe_setup(ctx):
        return rv
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
