# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import sys
from functools import partial

from mach.decorators import Command, CommandArgument, SubCommand
from mozbuild.base import MachCommandConditions as conditions

_TRY_PLATFORMS = {
    "linux-xpcshell": "perftest-linux-try-xpcshell",
    "mac-aarch64-xpcshell": "perftest-macosx-aarch64-try-xpcshell",
    "linux-browsertime": "perftest-linux-try-browsertime",
    "mac-aarch64-browsertime": "perftest-macosx-aarch64-try-browsertime",
    "win-browsertimee": "perftest-windows-try-browsertime",
}


HERE = os.path.dirname(__file__)


def get_perftest_parser():
    from mozperftest import PerftestArgumentParser

    return PerftestArgumentParser


def get_perftest_tools_parser(tool):
    def tools_parser_func():
        from mozperftest import PerftestToolsArgumentParser

        PerftestToolsArgumentParser.tool = tool
        return PerftestToolsArgumentParser

    return tools_parser_func


def get_parser():
    return run_perftest._mach_command._parser


@Command(
    "perftest",
    category="testing",
    conditions=[partial(conditions.is_buildapp_in, apps=["firefox", "android"])],
    description="Run any flavor of perftest",
    parser=get_perftest_parser,
)
def run_perftest(command_context, **kwargs):
    # original parser that brought us there
    original_parser = get_parser()

    from mozperftest.script import ParseError, ScriptInfo, ScriptType

    # Refer people to the --help command if they are lost
    if not kwargs["tests"] or kwargs["tests"] == ["help"]:
        print("No test selected!\n")
        print("See `./mach perftest --help` for more info\n")
        return

    if len(kwargs["tests"]) > 1:
        print("\nSorry no support yet for multiple local perftest")
        return

    # if the script is xpcshell, we can force the flavor here
    # XXX on multi-selection,  what happens if we have several flavors?
    try:
        script_info = ScriptInfo(kwargs["tests"][0])
    except ParseError as e:
        if e.exception is IsADirectoryError:
            script_info = None
        else:
            raise
    else:
        if script_info.script_type == ScriptType.xpcshell:
            kwargs["flavor"] = script_info.script_type.name
        elif script_info.script_type == ScriptType.alert:
            kwargs["flavor"] = script_info.script_type.name
        elif script_info.script_type == ScriptType.eval_mochitest:
            kwargs["flavor"] = "eval-mochitest"
        elif "flavor" not in kwargs:
            # we set the value only if not provided (so "mobile-browser"
            # can be picked)
            kwargs["flavor"] = "desktop-browser"

    from mozperftest.runner import run_tests

    run_tests(command_context, kwargs, original_parser.get_user_args(kwargs))

    print("\nFirefox. Fast For Good.\n")


@Command(
    "perftest-test",
    category="testing",
    description="Run perftest tests",
    virtualenv_name="perftest-test",
)
@CommandArgument(
    "tests", default=None, nargs="*", help="Tests to run. By default will run all"
)
@CommandArgument(
    "-v", "--verbose", action="store_true", default=False, help="Verbose mode"
)
@CommandArgument(
    "-r",
    "--raptor",
    action="store_true",
    default=False,
    help="Run raptor tests",
)
def run_tests(command_context, **kwargs):
    from pathlib import Path

    from mozperftest.utils import temporary_env

    COVERAGE_RCFILE = str(Path(HERE, ".mpt-coveragerc"))
    if kwargs.get("raptor", False):
        print("Running raptor unit tests through mozperftest")
        COVERAGE_RCFILE = str(Path(HERE, ".raptor-coveragerc"))

    with temporary_env(COVERAGE_RCFILE=COVERAGE_RCFILE, RUNNING_TESTS="YES"):
        _run_tests(command_context, **kwargs)


def _run_tests(command_context, **kwargs):
    from pathlib import Path

    from mozperftest.utils import ON_TRY, checkout_python_script

    venv = command_context.virtualenv_manager
    verbose = kwargs.get("verbose", False)

    # running pytest with coverage
    # coverage is done in three steps:
    # 1/ coverage erase => erase any previous coverage data
    # 2/ coverage run pytest ... => run the tests and collect info
    # 3/ coverage report => generate the report
    tests_dir = Path(HERE, "tests").resolve()

    tests = kwargs.get("tests", [])
    if tests == []:
        tests = str(tests_dir)
        run_coverage_check = True
    else:
        run_coverage_check = False

        def _get_test(test):
            if Path(test).exists():
                return str(test)
            return str(tests_dir / test)

        tests = " ".join([_get_test(test) for test in tests])

    # on macOS + try we skip the coverage
    # because macOS workers prevent us from installing
    # packages from PyPI
    if sys.platform == "darwin" and ON_TRY:
        run_coverage_check = False

    options = "-xs"
    if kwargs.get("verbose"):
        options += "v"

    # If we run mozperftest with the --raptor argument,
    # then only run the raptor unit tests
    if kwargs.get("raptor"):
        run_coverage_check = True
        tests = str(Path(command_context.topsrcdir, "testing", "raptor", "test"))

    if run_coverage_check:
        assert checkout_python_script(
            venv, "coverage", ["erase"], label="remove old coverage data"
        )

    args = ["run", "-m", "pytest", options, "--durations", "10", tests]

    assert checkout_python_script(
        venv, "coverage", args, label="running tests", verbose=verbose
    )
    if run_coverage_check and not checkout_python_script(
        venv, "coverage", ["report"], display=True
    ):
        raise ValueError("Coverage is too low!")


@Command(
    "perfdocs",
    category="testing",
    description="Generate performance testing documentation",
    virtualenv_name="lint",
)
@CommandArgument(
    "--generate",
    action="store_true",
    default=False,
    help="Regenerate the documentation",
)
@CommandArgument(
    "--regen-variants",
    action="store_true",
    default=False,
    help="Regenerate the variants in the documentation.",
)
@CommandArgument(
    "--output-file",
    default=None,
    help="Path to write lint errors as JSON (same format as mach lint --format json)",
)
@CommandArgument(
    "paths",
    nargs="*",
    default=None,
    help="Paths to include (default: all perfdocs locations)",
)
def perfdocs(
    command_context,
    generate=False,
    regen_variants=False,
    output_file=None,
    paths=None,
    **kwargs,
):
    import json
    import logging
    import pathlib
    from collections import defaultdict

    from mozperftest.perfdocs.perfdocs import run_perfdocs

    topsrcdir = pathlib.Path(command_context.topsrcdir)

    class ReviewbotLogger:
        def __init__(self, log):
            self._log = log
            self.issues = defaultdict(list)

        def info(self, msg):
            self._log(logging.INFO, "perfdocs", {}, msg)

        def lint_error(
            self, message, lineno=0, column=None, path=None, linter=None, rule=None
        ):
            location = f"{path}:{lineno}" if path else "unknown"
            self._log(
                logging.ERROR,
                "perfdocs",
                {},
                f"TEST-UNEXPECTED-ERROR: {message} ({location})",
            )

            # Path from PerfDocLogger is relative, sometimes with a leading slash
            if path:
                p = pathlib.Path(path)
                relpath = p.relative_to("/") if p.is_absolute() else p
                abs_path = topsrcdir / relpath
            else:
                relpath = ""
                abs_path = ""
            self.issues.setdefault(str(relpath), []).append({
                "linter": linter,
                "path": str(abs_path),
                "lineno": lineno or 0,
                "column": column,
                "message": message,
                "hint": None,
                "source": None,
                "level": "error",
                "rule": rule,
                "lineoffset": None,
                "diff": None,
                "relpath": str(relpath),
            })

        def critical(self, msg):
            self._log(logging.ERROR, "perfdocs", {}, msg)

    logger = ReviewbotLogger(command_context.log)

    if not paths:
        try:
            from mozversioncontrol import get_repository_object

            vcs = get_repository_object(str(topsrcdir))
            changed = set(vcs.get_changed_files("AM", mode="all"))
            changed.update(vcs.get_outgoing_files("AM"))
            # The "AM" filter is applied per commit, so a file modified in
            # one commit and deleted in a later one is still captured. Keep
            # only the paths that still exist, otherwise run_perfdocs errors
            # trying to lint a deleted file.
            paths = [str(topsrcdir / p) for p in changed if (topsrcdir / p).exists()]
        except Exception as e:
            logger.info(f"Failed to get modified files: {e}")

        if not paths:
            # TODO: Bug 2036346 - Rework how paths are handled for perfdocs
            paths = [
                str(topsrcdir / "python" / "mozperftest"),
                str(topsrcdir / "testing" / "awsy"),
                str(topsrcdir / "testing" / "raptor"),
                str(topsrcdir / "testing" / "talos"),
                str(topsrcdir / "testing" / "performance" / "mach-try-perf"),
                str(topsrcdir / "dom" / "indexedDB" / "test"),
            ]

    failed = run_perfdocs(
        config=None,
        logger=logger,
        paths=paths,
        generate=generate,
        regen_variants=regen_variants,
    )

    if output_file:
        with open(output_file, "w", encoding="utf-8") as fh:
            json.dump(dict(logger.issues) or {}, fh)

    if failed:
        logger.critical("[TEST-UNEXPECTED-ERROR] Perfdocs need to be regenerated.")
        return 1
    return 0


@Command(
    "perftest-tools",
    category="testing",
    description="Run perftest tools",
)
def run_tools(command_context, **kwargs):
    """
    Runs various perftest tools such as the side-by-side generator.
    """
    print("Runs various perftest tools such as the side-by-side generator.")


@SubCommand(
    "perftest-tools",
    "side-by-side",
    description="This tool can be used to generate a side-by-side visualization of two videos. "
    "When using this tool, make sure that the `--test-name` is an exact match, i.e. if you are "
    "comparing  the task `test-linux64-shippable-qr/opt-browsertime-tp6-firefox-linkedin-e10s` "
    "between two revisions, then use `browsertime-tp6-firefox-linkedin-e10s` as the suite name "
    "and `test-linux64-shippable-qr/opt` as the platform.",
    parser=get_perftest_tools_parser("side-by-side"),
)
def run_side_by_side(command_context, **kwargs):
    from mozperftest.runner import run_tools

    kwargs["tool"] = "side-by-side"
    run_tools(command_context, kwargs)


@SubCommand(
    "perftest-tools",
    "change-detector",
    description="This tool can be used to determine if there are differences between two "
    "revisions. It can do either direct comparisons, or searching for regressions in between "
    "two revisions (with a maximum or autocomputed depth).",
    parser=get_perftest_tools_parser("change-detector"),
)
def run_change_detector(command_context, **kwargs):
    from mozperftest.runner import run_tools

    kwargs["tool"] = "change-detector"
    run_tools(command_context, kwargs)
