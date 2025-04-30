# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.
import concurrent.futures
import json
import logging
import ntpath
import os
import pathlib
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from types import SimpleNamespace

import mozpack.path as mozpath
import six
import yaml
from mach.decorators import Command, CommandArgument, SubCommand
from mach.main import Mach
from mozversioncontrol import get_repository_object

from mozbuild import build_commands
from mozbuild.controller.clobber import Clobberer
from mozbuild.util import cpu_count, memoize


# Function used to run clang-format on a batch of files. It is a helper function
# in order to integrate into the futures ecosystem clang-format.
def run_one_clang_format_batch(args):
    try:
        subprocess.check_output(args)
    except subprocess.CalledProcessError as e:
        return e


def build_repo_relative_path(abs_path, repo_path):
    """Build path relative to repository root"""

    if os.path.islink(abs_path):
        abs_path = mozpath.realpath(abs_path)

    return mozpath.relpath(abs_path, repo_path)


def prompt_bool(prompt, limit=5):
    """Prompts the user with prompt and requires a boolean value."""
    from mach.util import strtobool

    for _ in range(limit):
        try:
            return strtobool(input(prompt + "[Y/N]\n"))
        except ValueError:
            print(
                "ERROR! Please enter a valid option! Please use any of the following:"
                " Y, N, True, False, 1, 0"
            )
    return False


class StaticAnalysisSubCommand(SubCommand):
    def __call__(self, func):
        after = SubCommand.__call__(self, func)
        args = [
            CommandArgument(
                "--verbose", "-v", action="store_true", help="Print verbose output."
            )
        ]
        for arg in args:
            after = arg(after)
        return after


class StaticAnalysisMonitor:
    def __init__(self, srcdir, objdir, checks, total):
        self._total = total
        self._processed = 0
        self._current = None
        self._srcdir = srcdir

        import copy

        self._checks = copy.deepcopy(checks)

        # Transform the configuration to support Regex
        for item in self._checks:
            if item["name"] == "-*":
                continue
            item["name"] = item["name"].replace("*", ".*")

        from mozbuild.compilation.warnings import WarningsCollector, WarningsDatabase

        self._warnings_database = WarningsDatabase()

        def on_warning(warning):
            # Output paths relative to repository root if the paths are under repo tree
            warning["filename"] = build_repo_relative_path(
                warning["filename"], self._srcdir
            )

            self._warnings_database.insert(warning)

        self._warnings_collector = WarningsCollector(on_warning, objdir=objdir)

    @property
    def num_files(self):
        return self._total

    @property
    def num_files_processed(self):
        return self._processed

    @property
    def current_file(self):
        return self._current

    @property
    def warnings_db(self):
        return self._warnings_database

    def on_line(self, line):
        warning = None

        try:
            warning = self._warnings_collector.process_line(line)
        except Exception:
            pass

        if line.find("clang-tidy") != -1:
            filename = line.split(" ")[-1]
            if os.path.isfile(filename):
                self._current = build_repo_relative_path(filename, self._srcdir)
            else:
                self._current = None
            self._processed = self._processed + 1
            return (warning, False)
        if warning is not None:

            def get_check_config(checker_name):
                # get the matcher from self._checks that is the 'name' field
                for item in self._checks:
                    if item["name"] == checker_name:
                        return item

                    # We are using a regex in order to also match 'mozilla-.* like checkers'
                    matcher = re.match(item["name"], checker_name)
                    if matcher is not None and matcher.group(0) == checker_name:
                        return item

            check_config = get_check_config(warning["flag"])
            if check_config is not None:
                warning["reliability"] = check_config.get("reliability", "low")
                warning["reason"] = check_config.get("reason")
                warning["publish"] = check_config.get("publish", True)
            elif warning["flag"] == "clang-diagnostic-error":
                # For a "warning" that is flagged as "clang-diagnostic-error"
                # set it as "publish"
                warning["publish"] = True

        return (warning, True)


# Utilities for running C++ static analysis checks and format.

# List of file extension to consider (should start with dot)
_format_include_extensions = (".cpp", ".c", ".cc", ".h", ".m", ".mm")
# File contaning all paths to exclude from formatting
_format_ignore_file = ".clang-format-ignore"

# (TOOLS) Function return codes
TOOLS_SUCCESS = 0
TOOLS_FAILED_DOWNLOAD = 1
TOOLS_UNSUPORTED_PLATFORM = 2
TOOLS_CHECKER_NO_TEST_FILE = 3
TOOLS_CHECKER_RETURNED_NO_ISSUES = 4
TOOLS_CHECKER_RESULT_FILE_NOT_FOUND = 5
TOOLS_CHECKER_DIFF_FAILED = 6
TOOLS_CHECKER_NOT_FOUND = 7
TOOLS_CHECKER_FAILED_FILE = 8
TOOLS_CHECKER_LIST_EMPTY = 9
TOOLS_GRADLE_FAILED = 10


@Command(
    "clang-tidy",
    category="devenv",
    description="Convenience alias for the static-analysis command",
)
def clang_tidy(command_context):
    # If no arguments are provided, just print a help message.
    """Detailed documentation:
    https://firefox-source-docs.mozilla.org/code-quality/static-analysis/index.html
    """
    mach = Mach(os.getcwd())

    def populate_context(key=None):
        if key == "topdir":
            return command_context.topsrcdir

    mach.populate_context_handler = populate_context
    mach.run(["static-analysis", "--help"])


@Command(
    "static-analysis",
    category="devenv",
    description="Run C++ static analysis checks using clang-tidy",
)
def static_analysis(command_context):
    # If no arguments are provided, just print a help message.
    """Detailed documentation:
    https://firefox-source-docs.mozilla.org/code-quality/static-analysis/index.html
    """
    mach = Mach(os.getcwd())

    def populate_context(key=None):
        if key == "topdir":
            return command_context.topsrcdir

    mach.populate_context_handler = populate_context
    mach.run(["static-analysis", "--help"])


@StaticAnalysisSubCommand(
    "static-analysis", "check", "Run the checks using the helper tool"
)
@CommandArgument(
    "source",
    nargs="*",
    default=[".*"],
    help="Source files to be analyzed (regex on path). "
    "Can be omitted, in which case the entire code base "
    "is analyzed.  The source argument is ignored if "
    "there is anything fed through stdin, in which case "
    "the analysis is only performed on the files changed "
    "in the patch streamed through stdin.  This is called "
    "the diff mode.",
)
@CommandArgument(
    "--checks",
    "-c",
    default="-*",
    metavar="checks",
    help="Static analysis checks to enable.  By default, this enables only "
    "checks that are published here: https://mzl.la/2DRHeTh, but can be any "
    "clang-tidy checks syntax.",
)
@CommandArgument(
    "--jobs",
    "-j",
    default="0",
    metavar="jobs",
    type=int,
    help="Number of concurrent jobs to run. Default is the number of CPUs.",
)
@CommandArgument(
    "--strip",
    "-p",
    default="1",
    metavar="NUM",
    help="Strip NUM leading components from file names in diff mode.",
)
@CommandArgument(
    "--fix",
    "-f",
    default=False,
    action="store_true",
    help="Try to autofix errors detected by clang-tidy checkers.",
)
@CommandArgument(
    "--header-filter",
    "-h-f",
    default="",
    metavar="header_filter",
    help="Regular expression matching the names of the headers to "
    "output diagnostics from. Diagnostics from the main file "
    "of each translation unit are always displayed",
)
@CommandArgument(
    "--output", "-o", default=None, help="Write clang-tidy output in a file"
)
@CommandArgument(
    "--format",
    default="text",
    choices=("text", "json"),
    help="Output format to write in a file",
)
@CommandArgument(
    "--outgoing",
    default=False,
    action="store_true",
    help="Run static analysis checks on outgoing files from mercurial repository",
)
def check(
    command_context,
    source=None,
    jobs=2,
    strip=1,
    verbose=False,
    checks="-*",
    fix=False,
    header_filter="",
    output=None,
    format="text",
    outgoing=False,
):
    from mozbuild.controller.building import (
        StaticAnalysisFooter,
        StaticAnalysisOutputManager,
    )

    command_context._set_log_level(verbose)
    command_context.activate_virtualenv()
    command_context.log_manager.enable_unstructured()

    rc, clang_paths = get_clang_tools(command_context, verbose=verbose)
    if rc != 0:
        return rc

    if not _is_version_eligible(command_context, clang_paths):
        return 1

    rc, _compile_db, compilation_commands_path = _build_compile_db(
        command_context, verbose=verbose
    )
    rc = rc or _build_export(command_context, jobs=jobs, verbose=verbose)
    if rc != 0:
        return rc

    # Use outgoing files instead of source files
    if outgoing:
        repo = get_repository_object(command_context.topsrcdir)
        files = repo.get_outgoing_files()
        source = get_abspath_files(command_context, files)

    # Split in several chunks to avoid hitting Python's limit of 100 groups in re
    compile_db = json.loads(open(_compile_db).read())
    total = 0
    import re

    chunk_size = 50
    for offset in range(0, len(source), chunk_size):
        source_chunks = [
            re.escape(f) for f in source[offset : offset + chunk_size].copy()
        ]
        name_re = re.compile("(" + ")|(".join(source_chunks) + ")")
        for f in compile_db:
            if name_re.search(f["file"]):
                total = total + 1

    # Filter source to remove excluded files
    source = _generate_path_list(command_context, source, verbose=verbose)

    if not total or not source:
        command_context.log(
            logging.INFO,
            "static-analysis",
            {},
            "There are no files eligible for analysis. Please note that 'header' files "
            "cannot be used for analysis since they do not consist compilation units.",
        )
        return 0

    # Escape the files from source
    source = [re.escape(f) for f in source]

    cwd = command_context.topobjdir

    monitor = StaticAnalysisMonitor(
        command_context.topsrcdir,
        command_context.topobjdir,
        get_clang_tidy_config(command_context).checks_with_data,
        total,
    )

    footer = StaticAnalysisFooter(command_context.log_manager.terminal, monitor)

    with StaticAnalysisOutputManager(
        command_context.log_manager, monitor, footer
    ) as output_manager:
        import math

        batch_size = int(math.ceil(float(len(source)) / cpu_count()))
        for i in range(0, len(source), batch_size):
            args = _get_clang_tidy_command(
                command_context,
                clang_paths,
                compilation_commands_path,
                checks=checks,
                header_filter=header_filter,
                sources=source[i : (i + batch_size)],
                jobs=jobs,
                fix=fix,
            )
            rc = command_context.run_process(
                args=args,
                ensure_exit_code=False,
                line_handler=output_manager.on_line,
                cwd=cwd,
            )

        command_context.log(
            logging.WARNING,
            "warning_summary",
            {"count": len(monitor.warnings_db)},
            "{count} warnings present.",
        )

        # Write output file
        if output is not None:
            output_manager.write(output, format)

    return rc


def get_abspath_files(command_context, files):
    return [mozpath.join(command_context.topsrcdir, f) for f in files]


def get_files_with_commands(command_context, compile_db, source):
    """
    Returns an array of dictionaries having file_path with build command
    """

    compile_db = json.load(open(compile_db))

    commands_list = []

    for f in source:
        # It must be a C/C++ file
        _, ext = os.path.splitext(f)

        if ext.lower() not in _format_include_extensions:
            command_context.log(logging.INFO, "static-analysis", {}, f"Skipping {f}")
            continue
        file_with_abspath = os.path.join(command_context.topsrcdir, f)
        for f in compile_db:
            # Found for a file that we are looking
            if file_with_abspath == f["file"]:
                commands_list.append(f)

    return commands_list


@memoize
def get_clang_tidy_config(command_context):
    from mozbuild.code_analysis.utils import ClangTidyConfig

    return ClangTidyConfig(command_context.topsrcdir)


def _get_required_version(command_context):
    version = get_clang_tidy_config(command_context).version
    if version is None:
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            "ERROR: Unable to find 'package_version' in config.yml",
        )
    return version


def _get_current_version(command_context, clang_paths):
    # Because the fact that we ship together clang-tidy and clang-format
    # we are sure that these two will always share the same version.
    # Thus in order to determine that the version is compatible we only
    # need to check one of them, going with clang-format
    cmd = [clang_paths._clang_format_path, "--version"]
    version_info = None
    try:
        version_info = (
            subprocess.check_output(cmd, stderr=subprocess.STDOUT)
            .decode("utf-8")
            .strip()
        )

        if "MOZ_AUTOMATION" in os.environ:
            # Only show it in the CI
            command_context.log(
                logging.INFO,
                "static-analysis",
                {},
                f"{clang_paths._clang_format_path} Version = {version_info} ",
            )

    except subprocess.CalledProcessError as e:
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            "Error determining the version clang-tidy/format binary, please see the "
            f"attached exception: \n{e.output}",
        )
    return version_info


def _is_version_eligible(command_context, clang_paths, log_error=True):
    version = _get_required_version(command_context)
    if version is None:
        return False

    current_version = _get_current_version(command_context, clang_paths)
    if current_version is None:
        return False
    version = "clang-format version " + version
    if version in current_version:
        return True

    if log_error:
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            f"ERROR: You're using an old or incorrect version ({_get_current_version(command_context, clang_paths)}) of clang-format binary. "
            f"Please update to a more recent one (at least > {_get_required_version(command_context)}) "
            "by running: './mach bootstrap' ",
        )

    return False


def _get_clang_tidy_command(
    command_context,
    clang_paths,
    compilation_commands_path,
    checks,
    header_filter,
    sources,
    jobs,
    fix,
):
    if checks == "-*":
        checks = ",".join(get_clang_tidy_config(command_context).checks)

    common_args = [
        "-clang-tidy-binary",
        clang_paths._clang_tidy_path,
        "-clang-apply-replacements-binary",
        clang_paths._clang_apply_replacements,
        "-checks=%s" % checks,
        "-extra-arg=-DMOZ_CLANG_PLUGIN",
    ]

    # Flag header-filter is passed in order to limit the diagnostic messages only
    # to the specified header files. When no value is specified the default value
    # is considered to be the source in order to limit the diagnostic message to
    # the source files or folders.
    common_args += [
        "-header-filter=%s"
        % (header_filter if len(header_filter) else "|".join(sources))
    ]

    # From our configuration file, config.yaml, we build the configuration list, for
    # the checkers that are used. These configuration options are used to better fit
    # the checkers to our code.
    cfg = get_clang_tidy_config(command_context).checks_config
    if cfg:
        common_args += ["-config=%s" % yaml.dump(cfg)]

    if fix:
        common_args += ["-fix"]

    return (
        [
            command_context.virtualenv_manager.python_path,
            clang_paths._run_clang_tidy_path,
            "-j",
            str(jobs),
            "-p",
            compilation_commands_path,
        ]
        + common_args
        # run-clang-tidy expects regexps, not paths, so we need to escape
        # backslashes.
        + [os.path.normpath(s).replace("\\", "\\\\") for s in sources]
    )


@StaticAnalysisSubCommand(
    "static-analysis",
    "autotest",
    "Run the auto-test suite in order to determine that"
    " the analysis did not regress.",
)
@CommandArgument(
    "--dump-results",
    "-d",
    default=False,
    action="store_true",
    help="Generate the baseline for the regression test. Based on"
    " this baseline we will test future results.",
)
@CommandArgument(
    "--intree-tool",
    "-i",
    default=False,
    action="store_true",
    help="Use a pre-aquired in-tree clang-tidy package from the automation env."
    " This option is only valid on automation environments.",
)
@CommandArgument(
    "checker_names",
    nargs="*",
    default=[],
    help="Checkers that are going to be auto-tested.",
)
def autotest(
    command_context,
    verbose=False,
    dump_results=False,
    intree_tool=False,
    checker_names=[],
):
    # If 'dump_results' is True than we just want to generate the issues files for each
    # checker in particulat and thus 'force_download' becomes 'False' since we want to
    # do this on a local trusted clang-tidy package.
    command_context._set_log_level(verbose)
    command_context.activate_virtualenv()
    dump_results = dump_results

    force_download = not dump_results

    # Configure the tree or download clang-tidy package, depending on the option that we choose
    if intree_tool:
        clang_paths = SimpleNamespace()
        if "MOZ_AUTOMATION" not in os.environ:
            command_context.log(
                logging.INFO,
                "static-analysis",
                {},
                "The `autotest` with `--intree-tool` can only be ran in automation.",
            )
            return 1
        if "MOZ_FETCHES_DIR" not in os.environ:
            command_context.log(
                logging.INFO,
                "static-analysis",
                {},
                "`MOZ_FETCHES_DIR` is missing from the environment variables.",
            )
            return 1

        _, config, _ = _get_config_environment(command_context)
        clang_tools_path = os.environ["MOZ_FETCHES_DIR"]
        clang_paths._clang_tidy_path = mozpath.join(
            clang_tools_path,
            "clang-tidy",
            "bin",
            "clang-tidy" + config.substs.get("HOST_BIN_SUFFIX", ""),
        )
        clang_paths._clang_format_path = mozpath.join(
            clang_tools_path,
            "clang-tidy",
            "bin",
            "clang-format" + config.substs.get("HOST_BIN_SUFFIX", ""),
        )
        clang_paths._clang_apply_replacements = mozpath.join(
            clang_tools_path,
            "clang-tidy",
            "bin",
            "clang-apply-replacements" + config.substs.get("HOST_BIN_SUFFIX", ""),
        )
        clang_paths._run_clang_tidy_path = mozpath.join(
            clang_tools_path, "clang-tidy", "bin", "run-clang-tidy"
        )
        clang_paths._clang_format_diff = mozpath.join(
            clang_tools_path, "clang-tidy", "share", "clang", "clang-format-diff.py"
        )

        # Ensure that clang-tidy is present
        rc = not os.path.exists(clang_paths._clang_tidy_path)
    else:
        rc, clang_paths = get_clang_tools(
            command_context, force=force_download, verbose=verbose
        )

    if rc != 0:
        command_context.log(
            logging.ERROR,
            "ERROR: static-analysis",
            {},
            "ERROR: clang-tidy unable to locate package.",
        )
        return TOOLS_FAILED_DOWNLOAD

    clang_paths._clang_tidy_base_path = mozpath.join(
        command_context.topsrcdir, "tools", "clang-tidy"
    )

    # For each checker run it
    platform, _ = command_context.platform

    if platform not in get_clang_tidy_config(command_context).platforms:
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            f"ERROR: RUNNING: clang-tidy autotest for platform {platform} not supported.",
        )
        return TOOLS_UNSUPORTED_PLATFORM

    max_workers = cpu_count()

    command_context.log(
        logging.INFO,
        "static-analysis",
        {},
        f"RUNNING: clang-tidy autotest for platform {platform} with {max_workers} workers.",
    )

    # List all available checkers
    cmd = [clang_paths._clang_tidy_path, "-list-checks", "-checks=*"]
    clang_output = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode(
        "utf-8"
    )
    available_checks = clang_output.split("\n")[1:]
    clang_tidy_checks = [c.strip() for c in available_checks if c]

    # Build the dummy compile_commands.json
    compilation_commands_path = _create_temp_compilation_db(command_context)
    checkers_test_batch = []
    checkers_results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for item in get_clang_tidy_config(command_context).checks_with_data:
            # Skip if any of the following statements is true:
            # 1. Checker attribute 'publish' is False.
            not_published = not bool(item.get("publish", True))
            # 2. Checker has restricted-platforms and current platform is not of them.
            ignored_platform = (
                "restricted-platforms" in item
                and platform not in item["restricted-platforms"]
            )
            # 3. Checker name is mozilla-* or -*.
            ignored_checker = item["name"] in ["mozilla-*", "-*"]
            # 4. List checker_names is passed and the current checker is not part of the
            #    list or 'publish' is False
            checker_not_in_list = checker_names and (
                item["name"] not in checker_names or not_published
            )
            if (
                not_published
                or ignored_platform
                or ignored_checker
                or checker_not_in_list
            ):
                continue
            checkers_test_batch.append(item["name"])
            futures.append(
                executor.submit(
                    _verify_checker,
                    command_context,
                    clang_paths,
                    compilation_commands_path,
                    dump_results,
                    clang_tidy_checks,
                    item,
                    checkers_results,
                )
            )

        error_code = TOOLS_SUCCESS
        for future in concurrent.futures.as_completed(futures):
            # Wait for every task to finish
            ret_val = future.result()
            if ret_val != TOOLS_SUCCESS:
                # We are interested only in one error and we don't break
                # the execution of for loop since we want to make sure that all
                # tasks finished.
                error_code = ret_val

        if error_code != TOOLS_SUCCESS:
            command_context.log(
                logging.INFO,
                "static-analysis",
                {},
                "FAIL: the following clang-tidy check(s) failed:",
            )
            for failure in checkers_results:
                checker_error = failure["checker-error"]
                checker_name = failure["checker-name"]
                info1 = failure["info1"]
                info2 = failure["info2"]
                info3 = failure["info3"]

                message_to_log = ""
                if checker_error == TOOLS_CHECKER_NOT_FOUND:
                    message_to_log = (
                        "\tChecker "
                        f"{checker_name} not present in this clang-tidy version."
                    )
                elif checker_error == TOOLS_CHECKER_NO_TEST_FILE:
                    message_to_log = (
                        "\tChecker "
                        f"{checker_name} does not have a test file - {checker_name}.cpp"
                    )
                elif checker_error == TOOLS_CHECKER_RETURNED_NO_ISSUES:
                    message_to_log = (
                        f"\tChecker {checker_name} did not find any issues in its test file, "
                        f"clang-tidy output for the run is:\n{info1}"
                    )
                elif checker_error == TOOLS_CHECKER_RESULT_FILE_NOT_FOUND:
                    message_to_log = f"\tChecker {checker_name} does not have a result file - {checker_name}.json"
                elif checker_error == TOOLS_CHECKER_DIFF_FAILED:
                    message_to_log = (
                        f"\tChecker {checker_name}\nExpected: {info1}\n"
                        f"Got: {info2}\n"
                        "clang-tidy output for the run is:\n"
                        f"{info3}"
                    )

                print("\n" + message_to_log)

            # Also delete the tmp folder
            shutil.rmtree(compilation_commands_path)
            return error_code

        # Run the analysis on all checkers at the same time only if we don't dump results.
        if not dump_results:
            ret_val = _run_analysis_batch(
                command_context,
                clang_paths,
                compilation_commands_path,
                checkers_test_batch,
            )
            if ret_val != TOOLS_SUCCESS:
                shutil.rmtree(compilation_commands_path)
                return ret_val

    command_context.log(
        logging.INFO, "static-analysis", {}, "SUCCESS: clang-tidy all tests passed."
    )
    # Also delete the tmp folder
    shutil.rmtree(compilation_commands_path)


def _run_analysis(
    command_context,
    clang_paths,
    compilation_commands_path,
    checks,
    header_filter,
    sources,
    jobs=1,
    fix=False,
    print_out=False,
):
    cmd = _get_clang_tidy_command(
        command_context,
        clang_paths,
        compilation_commands_path,
        checks=checks,
        header_filter=header_filter,
        sources=sources,
        jobs=jobs,
        fix=fix,
    )

    try:
        clang_output = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode(
            "utf-8"
        )
    except subprocess.CalledProcessError as e:
        print(e.output)
        return None
    return _parse_issues(command_context, clang_output), clang_output


def _run_analysis_batch(command_context, clang_paths, compilation_commands_path, items):
    command_context.log(
        logging.INFO,
        "static-analysis",
        {},
        "RUNNING: clang-tidy checker batch analysis.",
    )
    if not len(items):
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            "ERROR: clang-tidy checker list is empty!",
        )
        return TOOLS_CHECKER_LIST_EMPTY

    issues, clang_output = _run_analysis(
        command_context,
        clang_paths,
        compilation_commands_path,
        checks="-*," + ",".join(items),
        header_filter="",
        sources=[
            mozpath.join(clang_paths._clang_tidy_base_path, "test", checker) + ".cpp"
            for checker in items
        ],
        print_out=True,
    )

    if issues is None:
        return TOOLS_CHECKER_FAILED_FILE

    failed_checks = []
    failed_checks_baseline = []
    for checker in items:
        test_file_path_json = (
            mozpath.join(clang_paths._clang_tidy_base_path, "test", checker) + ".json"
        )
        # Read the pre-determined issues
        baseline_issues = _get_autotest_stored_issues(test_file_path_json)

        # We also stored the 'reliability' index so strip that from the baseline_issues
        baseline_issues[:] = [
            item for item in baseline_issues if "reliability" not in item
        ]

        found = all([element_base in issues for element_base in baseline_issues])

        if not found:
            failed_checks.append(checker)
            failed_checks_baseline.append(baseline_issues)

    if len(failed_checks) > 0:
        command_context.log(
            logging.ERROR,
            "static-analysis",
            {},
            "ERROR: The following check(s) failed for bulk analysis: "
            + " ".join(failed_checks),
        )

        for failed_check, baseline_issue in zip(failed_checks, failed_checks_baseline):
            print(
                f"\tChecker {failed_check} expect following results: \n\t\t{baseline_issue}"
            )

        print(
            f"This is the output generated by clang-tidy for the bulk build:\n{clang_output}"
        )
        return TOOLS_CHECKER_DIFF_FAILED

    return TOOLS_SUCCESS


def _create_temp_compilation_db(command_context):
    directory = tempfile.mkdtemp(prefix="cc")
    with open(mozpath.join(directory, "compile_commands.json"), "w") as file_handler:
        compile_commands = []
        director = mozpath.join(
            command_context.topsrcdir, "tools", "clang-tidy", "test"
        )
        for item in get_clang_tidy_config(command_context).checks:
            if item in ["-*", "mozilla-*"]:
                continue
            file = item + ".cpp"
            element = {}
            element["directory"] = director
            element["command"] = "cpp -std=c++17 " + file
            element["file"] = mozpath.join(director, file)
            compile_commands.append(element)

        json.dump(compile_commands, file_handler)
        file_handler.flush()

        return directory


@StaticAnalysisSubCommand(
    "static-analysis", "install", "Install the static analysis helper tool"
)
@CommandArgument(
    "source",
    nargs="?",
    type=str,
    help="Where to fetch a local archive containing the static-analysis and "
    "format helper tool."
    "It will be installed in ~/.mozbuild/clang-tools."
    "Can be omitted, in which case the latest clang-tools "
    "helper for the platform would be automatically detected and installed.",
)
@CommandArgument(
    "--skip-cache",
    action="store_true",
    help="Skip all local caches to force re-fetching the helper tool.",
    default=False,
)
@CommandArgument(
    "--force",
    action="store_true",
    help="Force re-install even though the tool exists in mozbuild.",
    default=False,
)
def install(
    command_context,
    source=None,
    skip_cache=False,
    force=False,
    verbose=False,
):
    command_context._set_log_level(verbose)
    rc, _ = get_clang_tools(
        command_context,
        force=force,
        skip_cache=skip_cache,
        source=source,
        verbose=verbose,
    )
    return rc


@StaticAnalysisSubCommand(
    "static-analysis",
    "clear-cache",
    "Delete local helpers and reset static analysis helper tool cache",
)
def clear_cache(command_context, verbose=False):
    command_context._set_log_level(verbose)
    rc, _ = get_clang_tools(
        command_context,
        force=True,
        download_if_needed=True,
        skip_cache=True,
        verbose=verbose,
    )

    if rc != 0:
        return rc

    from mozbuild.artifact_commands import artifact_clear_cache

    return artifact_clear_cache(command_context)


@StaticAnalysisSubCommand(
    "static-analysis",
    "print-checks",
    "Print a list of the static analysis checks performed by default",
)
def print_checks(command_context, verbose=False):
    command_context._set_log_level(verbose)
    rc, clang_paths = get_clang_tools(command_context, verbose=verbose)

    if rc != 0:
        return rc

    args = [
        clang_paths._clang_tidy_path,
        "-list-checks",
        "-checks=%s" % get_clang_tidy_config(command_context).checks,
    ]

    return command_context.run_process(args=args, pass_thru=True)


@Command(
    "clang-format",
    category="misc",
    description="Run clang-format on current changes",
)
@CommandArgument(
    "--show",
    "-s",
    action="store_const",
    const="stdout",
    dest="output_path",
    help="Show diff output on stdout instead of applying changes",
)
@CommandArgument(
    "--assume-filename",
    "-a",
    nargs=1,
    default=None,
    help="This option is usually used in the context of hg-formatsource."
    "When reading from stdin, clang-format assumes this "
    "filename to look for a style config file (with "
    "-style=file) and to determine the language. When "
    "specifying this option only one file should be used "
    "as an input and the output will be forwarded to stdin. "
    "This option also impairs the download of the clang-tools "
    "and assumes the package is already located in it's default "
    "location",
)
@CommandArgument(
    "--path", "-p", nargs="+", default=None, help="Specify the path(s) to reformat"
)
@CommandArgument(
    "--commit",
    "-c",
    default=None,
    help="Specify a commit to reformat from. "
    "For git you can also pass a range of commits (foo..bar) "
    "to format all of them at the same time.",
)
@CommandArgument(
    "--output",
    "-o",
    default=None,
    dest="output_path",
    help="Specify a file handle to write clang-format raw output instead of "
    "applying changes. This can be stdout or a file path.",
)
@CommandArgument(
    "--format",
    "-f",
    choices=("diff", "json"),
    default="diff",
    dest="output_format",
    help="Specify the output format used: diff is the raw patch provided by "
    "clang-format, json is a list of atomic changes to process.",
)
@CommandArgument(
    "--outgoing",
    default=False,
    action="store_true",
    help="Run clang-format on outgoing files from mercurial repository.",
)
def clang_format(
    command_context,
    assume_filename,
    path,
    commit,
    output_path=None,
    output_format="diff",
    verbose=False,
    outgoing=False,
):
    # Run clang-format or clang-format-diff on the local changes
    # or files/directories
    if path is None and outgoing:
        repo = get_repository_object(command_context.topsrcdir)
        path = repo.get_outgoing_files()

    if path:
        # Create the full path list
        def path_maker(f_name):
            return os.path.join(command_context.topsrcdir, f_name)

        path = map(path_maker, path)

    os.chdir(command_context.topsrcdir)

    # Load output file handle, either stdout or a file handle in write mode
    output = None
    if output_path is not None:
        output = sys.stdout if output_path == "stdout" else open(output_path, "w")

    # With assume_filename we want to have stdout clean since the result of the
    # format will be redirected to stdout. Only in case of errror we
    # write something to stdout.
    # We don't actually want to get the clang-tools here since we want in some
    # scenarios to do this in parallel so we relay on the fact that the tools
    # have already been downloaded via './mach bootstrap' or directly via
    # './mach static-analysis install'
    if assume_filename:
        rc, clang_paths = _set_clang_tools_paths(command_context)
        if rc != 0:
            print("clang-format: Unable to set path to clang-format tools.")
            return rc

        if not _do_clang_tools_exist(clang_paths):
            print("clang-format: Unable to set locate clang-format tools.")
            return 1

        if not _is_version_eligible(command_context, clang_paths):
            return 1
    else:
        rc, clang_paths = get_clang_tools(command_context, verbose=verbose)
        if rc != 0:
            return rc

    if path is None:
        return _run_clang_format_diff(
            command_context,
            clang_paths._clang_format_diff,
            clang_paths._clang_format_path,
            commit,
            output,
        )

    if assume_filename:
        return _run_clang_format_in_console(
            command_context, clang_paths._clang_format_path, path, assume_filename
        )

    return _run_clang_format_path(
        command_context, clang_paths._clang_format_path, path, output, output_format
    )


def _verify_checker(
    command_context,
    clang_paths,
    compilation_commands_path,
    dump_results,
    clang_tidy_checks,
    item,
    checkers_results,
):
    check = item["name"]
    test_file_path = mozpath.join(clang_paths._clang_tidy_base_path, "test", check)
    test_file_path_cpp = test_file_path + ".cpp"
    test_file_path_json = test_file_path + ".json"

    command_context.log(
        logging.INFO,
        "static-analysis",
        {},
        f"RUNNING: clang-tidy checker {check}.",
    )

    # Structured information in case a checker fails
    checker_error = {
        "checker-name": check,
        "checker-error": "",
        "info1": "",
        "info2": "",
        "info3": "",
    }

    # Verify if this checker actually exists
    if check not in clang_tidy_checks:
        checker_error["checker-error"] = TOOLS_CHECKER_NOT_FOUND
        checkers_results.append(checker_error)
        return TOOLS_CHECKER_NOT_FOUND

    # Verify if the test file exists for this checker
    if not os.path.exists(test_file_path_cpp):
        checker_error["checker-error"] = TOOLS_CHECKER_NO_TEST_FILE
        checkers_results.append(checker_error)
        return TOOLS_CHECKER_NO_TEST_FILE

    issues, clang_output = _run_analysis(
        command_context,
        clang_paths,
        compilation_commands_path,
        checks="-*," + check,
        header_filter="",
        sources=[test_file_path_cpp],
    )
    if issues is None:
        return TOOLS_CHECKER_FAILED_FILE

    # Verify to see if we got any issues, if not raise exception
    if not issues:
        checker_error["checker-error"] = TOOLS_CHECKER_RETURNED_NO_ISSUES
        checker_error["info1"] = clang_output
        checkers_results.append(checker_error)
        return TOOLS_CHECKER_RETURNED_NO_ISSUES

    # Also store the 'reliability' index for this checker
    issues.append({"reliability": item["reliability"]})

    if dump_results:
        _build_autotest_result(test_file_path_json, json.dumps(issues))
    else:
        if not os.path.exists(test_file_path_json):
            # Result file for test not found maybe regenerate it?
            checker_error["checker-error"] = TOOLS_CHECKER_RESULT_FILE_NOT_FOUND
            checkers_results.append(checker_error)
            return TOOLS_CHECKER_RESULT_FILE_NOT_FOUND

        # Read the pre-determined issues
        baseline_issues = _get_autotest_stored_issues(test_file_path_json)

        # Compare the two lists
        if issues != baseline_issues:
            checker_error["checker-error"] = TOOLS_CHECKER_DIFF_FAILED
            checker_error["info1"] = baseline_issues
            checker_error["info2"] = issues
            checker_error["info3"] = clang_output
            checkers_results.append(checker_error)
            return TOOLS_CHECKER_DIFF_FAILED

    return TOOLS_SUCCESS


def _build_autotest_result(file, issues):
    with open(file, "w") as f:
        f.write(issues)


def _get_autotest_stored_issues(file):
    with open(file) as f:
        return json.load(f)


def _parse_issues(command_context, clang_output):
    """
    Parse clang-tidy output into structured issues
    """

    # Limit clang output parsing to 'Enabled checks:'
    end = re.search(r"^Enabled checks:\n", clang_output, re.MULTILINE)
    if end is not None:
        clang_output = clang_output[: end.start() - 1]

    platform, _ = command_context.platform
    re_strip_colors = re.compile(r"\x1b\[[\d;]+m", re.MULTILINE)
    filtered = re_strip_colors.sub("", clang_output)
    # Starting with clang 8, for the diagnostic messages we have multiple `LF CR`
    # in order to be compatiable with msvc compiler format, and for this
    # we are not interested to match the end of line.
    regex_string = r"(.+):(\d+):(\d+): (warning|error): ([^\[\]\n]+)(?: \[([\.\w-]+)\])"

    # For non 'win' based platforms we also need the 'end of the line' regex
    if platform not in ("win64", "win32"):
        regex_string += "?$"

    regex_header = re.compile(regex_string, re.MULTILINE)

    # Sort headers by positions
    headers = sorted(regex_header.finditer(filtered), key=lambda h: h.start())
    issues = []
    for _, header in enumerate(headers):
        header_group = header.groups()
        element = [header_group[3], header_group[4], header_group[5]]
        issues.append(element)
    return issues


def _get_config_environment(command_context):
    ran_configure = False
    config = None

    try:
        config = command_context.config_environment
    except Exception:
        command_context.log(
            logging.WARNING,
            "static-analysis",
            {},
            "Looks like configure has not run yet, running it now...",
        )

        clobber = Clobberer(command_context.topsrcdir, command_context.topobjdir)

        if clobber.clobber_needed():
            choice = prompt_bool(
                "Configuration has changed and Clobber is needed. "
                "Do you want to proceed?"
            )
            if not choice:
                command_context.log(
                    logging.ERROR,
                    "static-analysis",
                    {},
                    "ERROR: Without Clobber we cannot continue execution!",
                )
                return (1, None, None)
            os.environ["AUTOCLOBBER"] = "1"

        rc = build_commands.configure(command_context)
        if rc != 0:
            return (rc, config, ran_configure)
        ran_configure = True
        try:
            config = command_context.config_environment
        except Exception:
            pass

    return (0, config, ran_configure)


def _build_compile_db(command_context, verbose=False):
    compilation_commands_path = mozpath.join(
        command_context.topobjdir, "static-analysis"
    )
    compile_db = mozpath.join(compilation_commands_path, "compile_commands.json")

    if os.path.exists(compile_db):
        return 0, compile_db, compilation_commands_path

    rc, config, ran_configure = _get_config_environment(command_context)
    if rc != 0:
        return rc, compile_db, compilation_commands_path

    if ran_configure:
        # Configure may have created the compilation database if the
        # mozconfig enables building the CompileDB backend by default,
        # So we recurse to see if the file exists once again.
        return _build_compile_db(command_context, verbose=verbose)

    if config:
        print(
            "Looks like a clang compilation database has not been "
            "created yet, creating it now..."
        )
        rc = build_commands.build_backend(
            command_context, ["StaticAnalysis"], verbose=verbose
        )
        if rc != 0:
            return rc, compile_db, compilation_commands_path
        assert os.path.exists(compile_db)
        return 0, compile_db, compilation_commands_path


def _build_export(command_context, jobs, verbose=False):
    def on_line(line):
        command_context.log(logging.INFO, "build_output", {"line": line}, "{line}")

    # First install what we can through install manifests.
    rc = command_context._run_make(
        directory=command_context.topobjdir,
        target="pre-export",
        line_handler=None,
        silent=not verbose,
    )
    if rc != 0:
        return rc

    # Then build the rest of the build dependencies by running the full
    # export target, because we can't do anything better.
    for target in ("export", "pre-compile"):
        rc = command_context._run_make(
            directory=command_context.topobjdir,
            target=target,
            line_handler=None,
            silent=not verbose,
            num_jobs=jobs,
        )
        if rc != 0:
            return rc

    return 0


def _set_clang_tools_paths(command_context):
    rc, config, _ = _get_config_environment(command_context)

    clang_paths = SimpleNamespace()

    if rc != 0:
        return rc, clang_paths

    clang_paths._clang_tools_path = mozpath.join(
        command_context._mach_context.state_dir, "clang-tools"
    )
    clang_paths._clang_tidy_path = mozpath.join(
        clang_paths._clang_tools_path,
        "clang-tidy",
        "bin",
        "clang-tidy" + config.substs.get("HOST_BIN_SUFFIX", ""),
    )
    clang_paths._clang_format_path = mozpath.join(
        clang_paths._clang_tools_path,
        "clang-tidy",
        "bin",
        "clang-format" + config.substs.get("HOST_BIN_SUFFIX", ""),
    )
    clang_paths._clang_apply_replacements = mozpath.join(
        clang_paths._clang_tools_path,
        "clang-tidy",
        "bin",
        "clang-apply-replacements" + config.substs.get("HOST_BIN_SUFFIX", ""),
    )
    clang_paths._run_clang_tidy_path = mozpath.join(
        clang_paths._clang_tools_path,
        "clang-tidy",
        "bin",
        "run-clang-tidy",
    )
    clang_paths._clang_format_diff = mozpath.join(
        clang_paths._clang_tools_path,
        "clang-tidy",
        "share",
        "clang",
        "clang-format-diff.py",
    )
    return 0, clang_paths


def _do_clang_tools_exist(clang_paths):
    return (
        os.path.exists(clang_paths._clang_tidy_path)
        and os.path.exists(clang_paths._clang_format_path)
        and os.path.exists(clang_paths._clang_apply_replacements)
        and os.path.exists(clang_paths._run_clang_tidy_path)
    )


def get_clang_tools(
    command_context,
    force=False,
    skip_cache=False,
    source=None,
    download_if_needed=True,
    verbose=False,
):
    rc, clang_paths = _set_clang_tools_paths(command_context)

    if rc != 0:
        return rc, clang_paths

    if (
        _do_clang_tools_exist(clang_paths)
        and _is_version_eligible(command_context, clang_paths, log_error=False)
        and not force
    ):
        return 0, clang_paths

    if os.path.isdir(clang_paths._clang_tools_path) and download_if_needed:
        # The directory exists, perhaps it's corrupted?  Delete it
        # and start from scratch.
        shutil.rmtree(clang_paths._clang_tools_path)
        return get_clang_tools(
            command_context,
            force=force,
            skip_cache=skip_cache,
            source=source,
            verbose=verbose,
            download_if_needed=download_if_needed,
        )

    # Create base directory where we store clang binary
    os.mkdir(clang_paths._clang_tools_path)

    if source:
        return _get_clang_tools_from_source(command_context, clang_paths, source)

    if not download_if_needed:
        return 0, clang_paths

    from mozbuild.bootstrap import bootstrap_toolchain

    clang_tidy = bootstrap_toolchain("clang-tools/clang-tidy")
    if not clang_tidy:
        raise Exception("clang-tidy not found")

    return 0 if _is_version_eligible(command_context, clang_paths) else 1, clang_paths


def _get_clang_tools_from_source(command_context, clang_paths, filename):
    from mozbuild.action.tooltool import unpack_file

    clang_tidy_path = mozpath.join(
        command_context._mach_context.state_dir, "clang-tools"
    )

    currentWorkingDir = os.getcwd()
    os.chdir(clang_tidy_path)

    unpack_file(filename)

    # Change back the cwd
    os.chdir(currentWorkingDir)

    clang_path = mozpath.join(clang_tidy_path, "clang")

    if not os.path.isdir(clang_path):
        raise Exception("Extracted the archive but didn't find the expected output")

    assert os.path.exists(clang_paths._clang_tidy_path)
    assert os.path.exists(clang_paths._clang_format_path)
    assert os.path.exists(clang_paths._clang_apply_replacements)
    assert os.path.exists(clang_paths._run_clang_tidy_path)
    return 0, clang_paths


def _run_clang_format_diff(
    command_context, clang_format_diff, clang_format, commit, output_file
):
    # Run clang-format on the diff
    # Note that this will potentially miss a lot things
    from subprocess import CalledProcessError, check_output

    diff_stream = command_context.repository.diff_stream(
        rev=commit,
        extensions=_format_include_extensions,
        exclude_file=_format_ignore_file,
        context=0,
    )
    args = [sys.executable, clang_format_diff, "-p1", "-binary=%s" % clang_format]

    if not output_file:
        args.append("-i")
    try:
        output = check_output(args, stdin=diff_stream)
        if output_file:
            # We want to print the diffs
            print(output, file=output_file)

        return 0
    except CalledProcessError as e:
        # Something wrong happend
        print("clang-format: An error occured while running clang-format-diff.")
        return e.returncode


def _is_ignored_path(command_context, ignored_dir_re, f):
    # path needs to be relative to the src root
    root_dir = command_context.topsrcdir + os.sep
    if f.startswith(root_dir):
        f = f[len(root_dir) :]
    # the ignored_dir_re regex uses / on all platforms
    return re.match(ignored_dir_re, f.replace(os.sep, "/"))


def _generate_path_list(command_context, paths, verbose=True):
    path_to_third_party = os.path.join(command_context.topsrcdir, _format_ignore_file)
    ignored_dir = []
    with open(path_to_third_party) as fh:
        for line in fh:
            # Remove comments and empty lines
            if line.startswith("#") or len(line.strip()) == 0:
                continue
            # The regexp is to make sure we are managing relative paths
            ignored_dir.append(r"^[\./]*" + line.rstrip())

    # Generates the list of regexp
    ignored_dir_re = "(%s)" % "|".join(ignored_dir)
    extensions = _format_include_extensions

    path_list = []
    for f in paths:
        if _is_ignored_path(command_context, ignored_dir_re, f):
            # Early exit if we have provided an ignored directory
            if verbose:
                print(f"static-analysis: Ignored third party code '{f}'")
            continue

        if os.path.isdir(f):
            # Processing a directory, generate the file list
            for folder, subs, files in os.walk(f):
                subs.sort()
                for filename in sorted(files):
                    f_in_dir = posixpath.join(pathlib.Path(folder).as_posix(), filename)
                    if f_in_dir.endswith(extensions) and not _is_ignored_path(
                        command_context, ignored_dir_re, f_in_dir
                    ):
                        # Supported extension and accepted path
                        path_list.append(f_in_dir)
        else:
            # Make sure that the file exists and it has a supported extension
            if os.path.isfile(f) and f.endswith(extensions):
                path_list.append(f)

    return path_list


def _run_clang_format_in_console(command_context, clang_format, paths, assume_filename):
    path_list = _generate_path_list(command_context, assume_filename, False)

    if path_list == []:
        return 0

    # We use -assume-filename in order to better determine the path for
    # the .clang-format when it is ran outside of the repo, for example
    # by the extension hg-formatsource
    args = [clang_format, f"-assume-filename={assume_filename[0]}"]

    process = subprocess.Popen(args, stdin=subprocess.PIPE)
    with open(paths[0]) as fin:
        process.stdin.write(fin.read())
        process.stdin.close()
        process.wait()
        return process.returncode


def _get_clang_format_cfg(command_context, current_dir):
    clang_format_cfg_path = mozpath.join(current_dir, ".clang-format")

    if os.path.exists(clang_format_cfg_path):
        # Return found path for .clang-format
        return clang_format_cfg_path

    if current_dir != command_context.topsrcdir:
        # Go to parent directory
        return _get_clang_format_cfg(command_context, os.path.split(current_dir)[0])
    # We have reached command_context.topsrcdir so return None
    return None


def _copy_clang_format_for_show_diff(
    command_context, current_dir, cached_clang_format_cfg, tmpdir
):
    # Lookup for .clang-format first in cache
    clang_format_cfg = cached_clang_format_cfg.get(current_dir, None)

    if clang_format_cfg is None:
        # Go through top directories
        clang_format_cfg = _get_clang_format_cfg(command_context, current_dir)

        # This is unlikely to happen since we must have .clang-format from
        # command_context.topsrcdir but in any case we should handle a potential error
        if clang_format_cfg is None:
            print("Cannot find corresponding .clang-format.")
            return 1

        # Cache clang_format_cfg for potential later usage
        cached_clang_format_cfg[current_dir] = clang_format_cfg

    # Copy .clang-format to the tmp dir where the formatted file is copied
    shutil.copy(clang_format_cfg, tmpdir)
    return 0


def _run_clang_format_path(
    command_context, clang_format, paths, output_file, output_format
):
    # Run clang-format on files or directories directly
    from subprocess import CalledProcessError, check_output

    if output_format == "json":
        # Get replacements in xml, then process to json
        args = [clang_format, "-output-replacements-xml"]
    else:
        args = [clang_format, "-i"]

    if output_file:
        # We just want to show the diff, we create the directory to copy it
        tmpdir = os.path.join(command_context.topobjdir, "tmp")
        if not os.path.exists(tmpdir):
            os.makedirs(tmpdir)

    path_list = _generate_path_list(command_context, paths)

    if path_list == []:
        return

    print("Processing %d file(s)..." % len(path_list))

    if output_file:
        patches = {}
        cached_clang_format_cfg = {}
        for i in range(0, len(path_list)):
            l = path_list[i : (i + 1)]

            # Copy the files into a temp directory
            # and run clang-format on the temp directory
            # and show the diff
            original_path = l[0]
            local_path = ntpath.basename(original_path)
            current_dir = ntpath.dirname(original_path)
            target_file = os.path.join(tmpdir, local_path)
            faketmpdir = os.path.dirname(target_file)
            if not os.path.isdir(faketmpdir):
                os.makedirs(faketmpdir)
            shutil.copy(l[0], faketmpdir)
            l[0] = target_file

            ret = _copy_clang_format_for_show_diff(
                command_context, current_dir, cached_clang_format_cfg, faketmpdir
            )
            if ret != 0:
                return ret

            # Run clang-format on the list
            try:
                output = check_output(args + l)
                if output and output_format == "json":
                    # Output a relative path in json patch list
                    relative_path = os.path.relpath(
                        original_path, command_context.topsrcdir
                    )
                    patches[relative_path] = _parse_xml_output(original_path, output)
            except CalledProcessError as e:
                # Something wrong happend
                print("clang-format: An error occured while running clang-format.")
                return e.returncode

            # show the diff
            if output_format == "diff":
                diff_command = ["diff", "-u", original_path, target_file]
                try:
                    output = check_output(diff_command)
                except CalledProcessError as e:
                    # diff -u returns 0 when no change
                    # here, we expect changes. if we are here, this means that
                    # there is a diff to show
                    if e.output:
                        # Replace the temp path by the path relative to the repository to
                        # display a valid patch
                        relative_path = os.path.relpath(
                            original_path, command_context.topsrcdir
                        )
                        # We must modify the paths in order to be compatible with the
                        # `diff` format.
                        original_path_diff = os.path.join("a", relative_path)
                        target_path_diff = os.path.join("b", relative_path)
                        e.output = e.output.decode("utf-8")
                        patch = e.output.replace(
                            f"+++ {target_file}",
                            f"+++ {target_path_diff}",
                        ).replace(
                            f"-- {original_path}",
                            f"-- {original_path_diff}",
                        )
                        patches[original_path] = patch

        if output_format == "json":
            output = json.dumps(patches, indent=4)
        else:
            # Display all the patches at once
            output = "\n".join(patches.values())

        # Output to specified file or stdout
        print(output, file=output_file)

        shutil.rmtree(tmpdir)
        return 0

    # Run clang-format in parallel trying to saturate all of the available cores.
    import math

    max_workers = cpu_count()

    # To maximize CPU usage when there are few items to handle,
    # underestimate the number of items per batch, then dispatch
    # outstanding items across workers. Per definition, each worker will
    # handle at most one outstanding item.
    batch_size = int(math.floor(float(len(path_list)) / max_workers))
    outstanding_items = len(path_list) - batch_size * max_workers

    batches = []

    i = 0
    while i < len(path_list):
        num_items = batch_size + (1 if outstanding_items > 0 else 0)
        batches.append(args + path_list[i : (i + num_items)])

        outstanding_items -= 1
        i += num_items

    error_code = None

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for batch in batches:
            futures.append(executor.submit(run_one_clang_format_batch, batch))

        for future in concurrent.futures.as_completed(futures):
            # Wait for every task to finish
            ret_val = future.result()
            if ret_val is not None:
                error_code = ret_val

        if error_code is not None:
            return error_code
    return 0


def _parse_xml_output(path, clang_output):
    """
    Parse the clang-format XML output to convert it in a JSON compatible
    list of patches, and calculates line level informations from the
    character level provided changes.
    """
    content = six.ensure_str(open(path).read())

    def _nb_of_lines(start, end):
        return len(content[start:end].splitlines())

    def _build(replacement):
        offset = int(replacement.attrib["offset"])
        length = int(replacement.attrib["length"])
        last_line = content.rfind("\n", 0, offset)
        return {
            "replacement": replacement.text,
            "char_offset": offset,
            "char_length": length,
            "line": _nb_of_lines(0, offset),
            "line_offset": last_line != -1 and (offset - last_line) or 0,
            "lines_modified": _nb_of_lines(offset, offset + length),
        }

    return [
        _build(replacement)
        for replacement in ET.fromstring(clang_output).findall("replacement")
    ]
