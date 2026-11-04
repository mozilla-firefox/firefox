# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import copy
import json
import os

from mach.decorators import Command, CommandArgument
from mozbuild.base import BuildEnvironmentNotFoundException
from mozbuild.base import MachCommandConditions as conditions
from mozbuild.util import construct_log_filename
from mozsystemmonitor.resourcemonitor import SystemResourceMonitor

here = os.path.abspath(os.path.dirname(__file__))
EXCLUSION_FILES = [
    os.path.join("tools", "rewriting", "Generated.txt"),
    os.path.join("tools", "rewriting", "ThirdPartyPaths.txt"),
]

EXCLUSION_FILES_OPTIONAL = []
thunderbird_excludes = os.path.join("comm", "tools", "lint", "GlobalExclude.txt")
if os.path.exists(thunderbird_excludes):
    EXCLUSION_FILES_OPTIONAL.append(thunderbird_excludes)

GLOBAL_EXCLUDES = [
    "**/node_modules",
    "tools/lint/test/files",
    ".hg",
    ".git",
    ".jj",
    ".claude/worktrees",
    ".mypy_cache",
    ".pytest_cache",
    ".ruff_cache",
    ".rumdl_cache",
    ".skip_fails_cache",
    "artifacts",
]

VALID_FORMATTERS = {"ruff-format", "clang-format", "eslint", "rustfmt", "stylelint"}
VALID_ANDROID_FORMATTERS = {"android-format"}

# Code-review bot must index issues from the whole codebase when pushing
# to autoland or try repositories. In such cases, output warnings in the
# task's JSON artifact but do not fail if only warnings are found.
REPORT_WARNINGS = os.environ.get("GECKO_HEAD_REPOSITORY", "").rstrip("/") in (
    "https://hg.mozilla.org/mozilla-central",
    "https://hg.mozilla.org/integration/autoland",
    "https://hg.mozilla.org/try",
)


def setup_argument_parser():
    from mozlint import cli

    return cli.MozlintParser()


def get_global_excludes(**lintargs):
    # exclude misc paths
    excludes = GLOBAL_EXCLUDES[:]
    topsrcdir = lintargs["root"]

    # exclude top level paths that look like objdirs
    excludes.extend([
        name
        for name in os.listdir(topsrcdir)
        if name.startswith("obj") and os.path.isdir(name)
    ])

    return excludes


def get_third_party_excludes(**lintargs):
    excludes = []
    topsrcdir = lintargs["root"]

    for path in EXCLUSION_FILES + EXCLUSION_FILES_OPTIONAL:
        with open(os.path.join(topsrcdir, path)) as fh:
            excludes.extend([f.strip() for f in fh.readlines()])

    return excludes


@Command(
    "lint",
    category="devenv",
    description="Run linters.",
    parser=setup_argument_parser,
    virtualenv_name="lint",
)
def lint(command_context, *runargs, **lintargs):
    """Run linters."""
    command_context.activate_virtualenv()
    from mozlint import cli, parser

    try:
        buildargs = {}
        buildargs["substs"] = copy.deepcopy(dict(command_context.substs))
        buildargs["defines"] = copy.deepcopy(dict(command_context.defines))
        buildargs["topobjdir"] = command_context.topobjdir
        lintargs.update(buildargs)
    except BuildEnvironmentNotFoundException:
        pass

    lintargs.setdefault("root", command_context.topsrcdir)
    lintargs["exclude"] = get_global_excludes(**lintargs)
    lintargs["third_party_exclude"] = get_third_party_excludes(**lintargs)
    lintargs["config_paths"].insert(0, here)
    lintargs["virtualenv_bin_path"] = command_context.virtualenv_manager.bin_path
    lintargs["virtualenv_manager"] = command_context.virtualenv_manager
    if REPORT_WARNINGS and lintargs.get("show_warnings") is None:
        lintargs["show_warnings"] = "soft"
    for path in EXCLUSION_FILES:
        parser.GLOBAL_SUPPORT_FILES.append(
            os.path.join(command_context.topsrcdir, path)
        )
    setupargs = {
        "mach_command_context": command_context,
    }

    monitor = SystemResourceMonitor(poll_interval=0.1)
    monitor.start()

    try:
        ret = cli.run(*runargs, setupargs=setupargs, **lintargs)
    finally:
        monitor.stop()

        if not monitor.forwarding:
            if os.environ.get("MOZ_AUTOMATION") == "1":
                profile_path = "/builds/worker/profile_resource-usage.json"
            else:
                log_subdir = os.path.join("logs", "lint")
                command_context._ensure_state_subdir_exists(log_subdir)
                profile_path = command_context._get_state_filename(
                    construct_log_filename("profile"), subdir=log_subdir
                )

            with open(profile_path, "w", encoding="utf-8", newline="\n") as f:
                to_write = json.dumps(monitor.as_profile(), separators=(",", ":"))
                f.write(to_write)

    return ret


@Command(
    "eslint",
    category="devenv",
    description="Run eslint or help configure eslint for optimal development.",
)
@CommandArgument(
    "paths",
    default=None,
    nargs="*",
    help="Paths to file or directories to lint, like "
    "'browser/' Defaults to the "
    "current directory if not given.",
)
@CommandArgument(
    "-s",
    "--setup",
    default=False,
    action="store_true",
    help="Configure eslint for optimal development.",
)
@CommandArgument("-b", "--binary", default=None, help="Path to eslint binary.")
@CommandArgument(
    "--fix",
    default=False,
    action="store_true",
    help="Request that eslint automatically fix errors, where possible.",
)
@CommandArgument(
    "--rule",
    default=[],
    dest="rules",
    action="append",
    help="Specify an additional rule for ESLint to run, e.g. 'no-new-object: error'",
)
@CommandArgument(
    "extra_args",
    nargs=argparse.REMAINDER,
    help="Extra args that will be forwarded to eslint.",
)
def eslint(command_context, paths, extra_args=[], **kwargs):
    command_context._mach_context.commands.dispatch(
        "lint",
        command_context._mach_context,
        linters=["eslint"],
        paths=paths,
        argv=extra_args,
        **kwargs,
    )


@Command(
    "prettier",
    category="devenv",
    description="Run prettier to reformat the relevant files.",
)
@CommandArgument(
    "paths",
    default=None,
    nargs="*",
    help="Paths to file or directories to lint, like "
    "'browser/' Defaults to the "
    "current directory if not given.",
)
@CommandArgument(
    "extra_args",
    nargs=argparse.REMAINDER,
    help="Extra args that will be forwarded to prettier.",
)
def prettier(command_context, paths, extra_args=[], **kwargs):
    command_context._mach_context.commands.dispatch(
        "format",
        command_context._mach_context,
        linters=["eslint", "stylelint"],
        paths=paths,
        argv=extra_args,
        **kwargs,
    )


@Command(
    "format",
    category="devenv",
    description=(
        "Format files, alternative to 'lint --fix'. "
        "Runs the following formatters: "
        + ", ".join(sorted(VALID_FORMATTERS))
        + " (plus "
        + ", ".join(sorted(VALID_ANDROID_FORMATTERS))
        + " on Android, unless --skip-android is passed)."
    ),
    parser=setup_argument_parser,
)
@CommandArgument(
    "--skip-android",
    default=False,
    action="store_true",
    help="Skips checking if android formatters are valid in this context.",
)
def format_files(command_context, paths, extra_args=[], **kwargs):
    linters = kwargs["linters"]
    skip_android = kwargs["skip_android"]

    formatters = VALID_FORMATTERS
    if not skip_android and conditions.is_android(command_context):
        formatters |= VALID_ANDROID_FORMATTERS

    if not linters:
        linters = formatters
    else:
        invalid_linters = set(linters) - formatters
        if invalid_linters:
            print(
                "error: One or more linters passed are not valid formatters. "
                "Note that only the following linters are valid formatters:"
            )
            print("\n".join(sorted(formatters)))
            print("Further note that for eslint, only prettier will run.")
            return 1

    kwargs["linters"] = list(linters)

    kwargs["fix"] = True
    kwargs["formatonly"] = True
    command_context._mach_context.commands.dispatch(
        "lint", command_context._mach_context, paths=paths, argv=extra_args, **kwargs
    )
