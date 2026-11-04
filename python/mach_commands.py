# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import subprocess
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed, thread

import mozinfo
from mach.decorators import Command, CommandArgument
from mach.util import record_resource_marker, resource_marker
from manifestparser import TestManifest
from manifestparser import filters as mpf
from mozbuild.util import cpu_count
from mozfile import which
from tqdm import tqdm


@Command("python", category="devenv", description="Run Python.")
@CommandArgument(
    "--exec-file", default=None, help="Execute this Python file using `exec`"
)
@CommandArgument(
    "--ipython",
    action="store_true",
    default=False,
    help="Use ipython instead of the default Python REPL.",
)
@CommandArgument(
    "--virtualenv",
    default=None,
    help="Prepare and use the virtualenv with the provided name. If not specified, "
    "then the Mach context is used instead.",
)
@CommandArgument("args", nargs=argparse.REMAINDER)
def python(
    command_context,
    exec_file,
    ipython,
    virtualenv,
    args,
):
    # Avoid logging the command
    command_context.log_manager.terminal_handler.setLevel(logging.CRITICAL)

    # Note: subprocess requires native strings in os.environ on Windows.
    append_env = {"PYTHONDONTWRITEBYTECODE": "1"}

    if virtualenv:
        command_context._virtualenv_name = virtualenv

    if exec_file:
        command_context.activate_virtualenv()
        exec(open(exec_file).read())
        return 0

    if ipython:
        if virtualenv:
            command_context.virtualenv_manager.ensure()
            python_path = which(
                "ipython", path=command_context.virtualenv_manager.bin_path
            )
            if not python_path:
                raise Exception(
                    "--ipython was specified, but the provided "
                    '--virtualenv doesn\'t have "ipython" installed.'
                )
        else:
            command_context._virtualenv_name = "ipython"
            command_context.virtualenv_manager.ensure()
            python_path = which(
                "ipython", path=command_context.virtualenv_manager.bin_path
            )
    else:
        command_context.virtualenv_manager.ensure()
        python_path = command_context.virtualenv_manager.python_path

    return command_context.run_process(
        [python_path] + args,
        pass_thru=True,  # Allow user to run Python interactively.
        ensure_exit_code=False,  # Don't throw on non-zero exit code.
        python_unbuffered=False,  # Leave input buffered.
        append_env=append_env,
    )


@Command(
    "python-test",
    category="testing",
    virtualenv_name="python-test",
    description="Run Python unit tests with pytest.",
)
@CommandArgument(
    "-v", "--verbose", default=False, action="store_true", help="Verbose output."
)
@CommandArgument(
    "-j",
    "--jobs",
    default=None,
    type=int,
    help="Number of concurrent jobs to run. Default is the number of CPUs "
    "in the system.",
)
@CommandArgument(
    "-x",
    "--exitfirst",
    default=False,
    action="store_true",
    help="Runs all tests sequentially and breaks at the first failure.",
)
@CommandArgument(
    "--subsuite",
    default=None,
    help=(
        "Python subsuite to run. If not specified, all subsuites are run. "
        "Use the string `default` to only run tests without a subsuite."
    ),
)
@CommandArgument(
    "tests",
    nargs="*",
    metavar="TEST",
    help=(
        "Tests to run. Each test can be a single file or a directory. "
        "Default test resolution relies on PYTHON_UNITTEST_MANIFESTS."
    ),
)
@CommandArgument(
    "extra",
    nargs=argparse.REMAINDER,
    metavar="PYTEST ARGS",
    help=(
        "Arguments that aren't recognized by mach. These will be "
        "passed as it is to pytest"
    ),
)
def python_test(command_context, *args, **kwargs):
    try:
        tempdir = str(tempfile.mkdtemp(suffix="-python-test"))
        os.environ["PYTHON_TEST_TMP"] = tempdir
        return run_python_tests(command_context, *args, **kwargs)
    finally:
        import mozfile

        mozfile.remove(tempdir)


def run_python_tests(
    command_context,
    tests=None,
    test_objects=None,
    subsuite=None,
    verbose=False,
    jobs=None,
    exitfirst=False,
    extra=None,
    **kwargs,
):
    if test_objects is None:
        from moztest.resolve import TestResolver

        with resource_marker("python-test", "resolve tests"):
            resolver = command_context._spawn(TestResolver)
            # If we were given test paths, try to find tests matching them.
            test_objects = list(resolver.resolve_tests(paths=tests, flavor="python"))
    else:
        # We've received test_objects from |mach test|. We need to ignore
        # the subsuite because python-tests don't use this key like other
        # harnesses do and |mach test| doesn't realize this.
        subsuite = None

    mp = TestManifest()
    mp.tests.extend(test_objects)

    filters = []
    if subsuite == "default":
        filters.append(mpf.subsuite(None))
    elif subsuite:
        filters.append(mpf.subsuite(subsuite))

    tests = mp.active_tests(filters=filters, disabled=False, python=3, **mozinfo.info)

    if not tests:
        submsg = f"for subsuite '{subsuite}' " if subsuite else ""
        message = (
            "TEST-UNEXPECTED-FAIL | No tests collected "
            + f"{submsg}(Not in PYTHON_UNITTEST_MANIFESTS?)"
        )
        command_context.log(logging.WARN, "python-test", {}, message)
        return 1

    parallel = []
    sequential = []
    os.environ.setdefault("PYTEST_ADDOPTS", "")

    if extra:
        os.environ["PYTEST_ADDOPTS"] += " " + " ".join(extra)

    installed_requirements = set()
    for test in tests:
        if (
            test.get("requirements")
            and test["requirements"] not in installed_requirements
        ):
            command_context.virtualenv_manager.install_pip_requirements(
                test["requirements"], quiet=True
            )
            installed_requirements.add(test["requirements"])

    if exitfirst:
        sequential = tests
        os.environ["PYTEST_ADDOPTS"] += " -x"
    else:
        for test in tests:
            if test.get("sequential"):
                sequential.append(test)
            else:
                parallel.append(test)

    jobs = jobs or cpu_count()

    return_code = 0
    failure_output = []

    def on_test_finished(result):
        output, ret, test_path = result

        if ret:
            # Log the output of failed tests at the end so it's easy to find.
            failure_output.extend(output)

            if not return_code:
                command_context.log(
                    logging.ERROR,
                    "python-test",
                    {"test_path": test_path, "ret": ret},
                    "Setting retcode to {ret} from {test_path}",
                )
        else:
            for line in output:
                command_context.log(
                    logging.INFO, "python-test", {"line": line.rstrip()}, "{line}"
                )

        return return_code or ret

    with tqdm(
        total=(len(parallel) + len(sequential)),
        unit="Test",
        desc="Tests Completed",
        initial=0,
    ) as progress_bar:
        try:
            with ThreadPoolExecutor(max_workers=jobs) as executor:
                futures = []

                for test in parallel:
                    command_context.log(
                        logging.DEBUG,
                        "python-test",
                        {"line": f"Launching thread for test {test['file_relpath']}"},
                        "{line}",
                    )
                    futures.append(
                        executor.submit(
                            _run_python_test, command_context, test, jobs, verbose
                        )
                    )

                try:
                    for future in as_completed(futures):
                        progress_bar.clear()
                        return_code = on_test_finished(future.result())
                        progress_bar.update(1)
                except KeyboardInterrupt:
                    # Hack to force stop currently running threads.
                    # https://gist.github.com/clchiou/f2608cbe54403edb0b13
                    executor._threads.clear()
                    thread._threads_queues.clear()
                    raise

            for test in sequential:
                test_result = _run_python_test(command_context, test, jobs, verbose)

                progress_bar.clear()
                return_code = on_test_finished(test_result)
                if return_code and exitfirst:
                    break

                progress_bar.update(1)
        finally:
            progress_bar.clear()
            # Now log all failures (even if there was a KeyboardInterrupt or other exception).
            for line in failure_output:
                command_context.log(
                    logging.INFO, "python-test", {"line": line.rstrip()}, "{line}"
                )

    command_context.log(
        logging.INFO,
        "python-test",
        {"return_code": return_code},
        "Return code from mach python-test: {return_code}",
    )

    return return_code


def _run_python_test(command_context, test, jobs, verbose):
    output = []

    def _log(line):
        # Buffer messages if more than one worker to avoid interleaving
        if jobs > 1:
            output.append(line)
        else:
            command_context.log(
                logging.INFO, "python-test", {"line": line.rstrip()}, "{line}"
            )

    _log(test["path"])
    python = command_context.virtualenv_manager.python_path
    cmd = [python, test["path"]]
    env = os.environ.copy()
    env["PYTHONDONTWRITEBYTECODE"] = "1"

    start = time.monotonic()
    result = subprocess.run(
        cmd,
        check=False,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="UTF-8",
    )
    end = time.monotonic()

    return_code = result.returncode

    file_displayed_test = False

    for line in result.stdout.split(os.linesep):
        if not file_displayed_test:
            test_ran = "Ran" in line or "collected" in line or line.startswith("TEST-")
            if test_ran:
                file_displayed_test = True

        # Hack to make sure treeherder highlights pytest failures
        if "FAILED" in line.rsplit(" ", 1)[-1]:
            line = line.replace("FAILED", "TEST-UNEXPECTED-FAIL")

        _log(line)

    if not file_displayed_test:
        return_code = 1
        _log(
            "TEST-UNEXPECTED-FAIL | No test output (missing mozunit.main() "
            "call?): {}".format(test["path"])
        )

    marker_data = {
        "type": "Test",
        "test": test["file_relpath"],
        "name": os.path.basename(test["path"]),
    }
    if return_code:
        marker_data.update(status="FAIL", expected="PASS", color="orange")
    else:
        marker_data.update(status="PASS", color="green")
    record_resource_marker("test file", start, end, marker_data)

    if verbose:
        if return_code != 0:
            _log("Test failed: {}".format(test["path"]))
        else:
            _log("Test passed: {}".format(test["path"]))

    return output, return_code, test["path"]
