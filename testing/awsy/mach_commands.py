# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import logging
import os
import sys

import mozinfo
from mach.decorators import Command, CommandArgument, CommandArgumentGroup
from mozbuild.base import BinaryNotFoundException
from mozbuild.base import MachCommandConditions as conditions


def setup_awsy_argument_parser():
    from marionette_harness.runtests import MarionetteArguments
    from mozlog.structured import commandline

    parser = MarionetteArguments()
    commandline.add_logging_group(parser)

    return parser


from awsy import MAX_TABS


def run_awsy(command_context, tests, binary=None, **kwargs):
    import json

    from marionette_harness.runtests import MarionetteHarness, MarionetteTestRunner
    from mozlog.structured import commandline

    parser = setup_awsy_argument_parser()

    awsy_source_dir = os.path.join(command_context.topsrcdir, "testing", "awsy")
    if not tests:
        if kwargs["base"]:
            filename = "test_base_memory_usage.py"
        else:
            filename = "test_memory_usage.py"
        tests = [os.path.join(awsy_source_dir, "awsy", filename)]

    args = argparse.Namespace(tests=tests)

    args.binary = binary

    if kwargs["quick"]:
        kwargs["entities"] = 3
        kwargs["iterations"] = 1
        kwargs["perTabPause"] = 1
        kwargs["settleWaitTime"] = 1

    runtime_testvars = {}
    for arg in (
        "webRootDir",
        "pageManifest",
        "resultsDir",
        "entities",
        "iterations",
        "perTabPause",
        "settleWaitTime",
        "maxTabs",
        "dmd",
    ):
        if arg in kwargs and kwargs[arg] is not None:
            runtime_testvars[arg] = kwargs[arg]

    if "webRootDir" not in runtime_testvars:
        awsy_tests_dir = os.path.join(command_context.topobjdir, "_tests", "awsy")
        web_root_dir = os.path.join(awsy_tests_dir, "html")
        runtime_testvars["webRootDir"] = web_root_dir
    else:
        web_root_dir = runtime_testvars["webRootDir"]
        awsy_tests_dir = os.path.dirname(web_root_dir)

    if "resultsDir" not in runtime_testvars:
        runtime_testvars["resultsDir"] = os.path.join(awsy_tests_dir, "results")

    runtime_testvars["bin"] = binary
    runtime_testvars["run_local"] = True

    if not os.path.isdir(runtime_testvars["resultsDir"]):
        os.makedirs(runtime_testvars["resultsDir"])

    runtime_testvars_path = os.path.join(awsy_tests_dir, "runtime-testvars.json")
    # If '--testvars' was not specified use the same defaults as CI.
    if not kwargs["testvars"]:
        testvars_file = "base-testvars.json" if kwargs["base"] else "tp6-testvars.json"
        kwargs["testvars"] = [os.path.join(awsy_source_dir, "conf", testvars_file)]
    kwargs["testvars"].append(runtime_testvars_path)

    runtime_testvars_file = open(runtime_testvars_path, "w")
    runtime_testvars_file.write(json.dumps(runtime_testvars, indent=2))
    runtime_testvars_file.close()

    # If '--preferences' was not specified supply our default set.
    if not kwargs["prefs_files"]:
        prefs_file = "base-prefs.json" if kwargs["base"] else "tp6-prefs.json"
        kwargs["prefs_files"] = [os.path.join(awsy_source_dir, "conf", prefs_file)]

    # Setup DMD env vars if necessary.
    if kwargs["dmd"]:
        bin_dir = os.path.dirname(binary)

        if "DMD" not in os.environ:
            os.environ["DMD"] = "1"

        # Work around a startup crash with DMD on windows
        if mozinfo.os == "win":
            kwargs["pref"] = "security.sandbox.content.level:0"
            command_context.log(
                logging.WARNING,
                "awsy",
                {},
                "Forcing 'security.sandbox.content.level' = 0 because DMD is enabled.",
            )
        elif mozinfo.os == "mac":
            # On mac binary is in MacOS and dmd.py is in Resources, ie:
            #   Name.app/Contents/MacOS/libdmd.dylib
            #   Name.app/Contents/Resources/dmd.py
            bin_dir = os.path.join(bin_dir, "../Resources/")

        # Also add the bin dir to the python path so we can use dmd.py
        if bin_dir not in sys.path:
            sys.path.append(bin_dir)

    for k, v in kwargs.items():
        setattr(args, k, v)

    parser.verify_usage(args)

    args.logger = commandline.setup_logging(
        "Are We Slim Yet Tests", args, {"mach": sys.stdout}
    )
    failed = MarionetteHarness(MarionetteTestRunner, args=vars(args)).run()
    if failed > 0:
        return 1
    else:
        return 0


@Command(
    "awsy-test",
    category="testing",
    description="Run Are We Slim Yet (AWSY) memory usage testing using marionette.",
    parser=setup_awsy_argument_parser,
)
@CommandArgumentGroup("AWSY")
@CommandArgument(
    "--web-root",
    group="AWSY",
    action="store",
    type=str,
    dest="webRootDir",
    help="Path to web server root directory. If not specified, "
    "defaults to topobjdir/_tests/awsy/html.",
)
@CommandArgument(
    "--page-manifest",
    group="AWSY",
    action="store",
    type=str,
    dest="pageManifest",
    help="Path to a YAML page manifest listing the urls to test. If not "
    "specified, defaults to conf/tp6-pages.yml.",
)
@CommandArgument(
    "--results",
    group="AWSY",
    action="store",
    type=str,
    dest="resultsDir",
    help="Path to results directory. If not specified, defaults "
    "to the parent directory of the web root.",
)
@CommandArgument(
    "--quick",
    group="AWSY",
    action="store_true",
    dest="quick",
    default=False,
    help="Set --entities=3, --iterations=1, --per-tab-pause=1, "
    "--settle-wait-time=1 for a quick test. Overrides any explicit "
    "argument settings.",
)
@CommandArgument(
    "--entities",
    group="AWSY",
    action="store",
    type=int,
    dest="entities",
    help="Number of urls to load. Defaults to the total number of urls.",
)
@CommandArgument(
    "--max-tabs",
    group="AWSY",
    action="store",
    type=int,
    dest="maxTabs",
    help=f"Maximum number of tabs to open. Defaults to {MAX_TABS}.",
)
@CommandArgument(
    "--iterations",
    group="AWSY",
    action="store",
    type=int,
    dest="iterations",
    help="Number of times to run through the test suite. Defaults to the "
    "value in conf/tp6-testvars.json (or conf/base-testvars.json with --base).",
)
@CommandArgument(
    "--per-tab-pause",
    group="AWSY",
    action="store",
    type=int,
    dest="perTabPause",
    help="Seconds to wait in between opening tabs. Defaults to the value in "
    "conf/tp6-testvars.json (or conf/base-testvars.json with --base).",
)
@CommandArgument(
    "--settle-wait-time",
    group="AWSY",
    action="store",
    type=int,
    dest="settleWaitTime",
    help="Seconds to wait for things to settled down. Defaults to the value "
    "in conf/tp6-testvars.json (or conf/base-testvars.json with --base).",
)
@CommandArgument(
    "--dmd",
    group="AWSY",
    action="store_true",
    dest="dmd",
    default=False,
    help="Enable DMD during testing. Requires a DMD-enabled build.",
)
@CommandArgument(
    "--base",
    group="AWSY",
    action="store_true",
    dest="base",
    default=False,
    help="Run base memory usage tests.",
)
def run_awsy_test(command_context, tests, **kwargs):
    """mach awsy-test runs the in-tree version of the Are We Slim Yet
    (AWSY) tests.

    awsy-test is implemented as a marionette test and marionette
    test arguments also apply although they are not necessary
    since reasonable defaults will be chosen.

    The AWSY specific arguments can be found in the Command
    Arguments for AWSY section below.

    The results of the test will be placed in the results
    directory specified by the --results argument.

    On Windows, you may experience problems due to path length
    errors when attempting to write checkpoints to the results
    directory. In that case, you should specify both the
    --web-root and --results arguments pointing to a location
    with a short path. For example:

    --web-root=c:\\\\tmp\\\\html --results=c:\\\\tmp\\\\results

    Note that the double backslashes are required.
    """
    kwargs["logger_name"] = "Awsy Tests"
    if "test_objects" in kwargs:
        tests = []
        for obj in kwargs["test_objects"]:
            tests.append(obj["file_relpath"])
        del kwargs["test_objects"]

    if not kwargs.get("binary") and conditions.is_firefox(command_context):
        try:
            kwargs["binary"] = command_context.get_binary_path("app")
        except BinaryNotFoundException as e:
            command_context.log(
                logging.ERROR, "awsy", {"error": str(e)}, "ERROR: {error}"
            )
            command_context.log(logging.INFO, "awsy", {"help": e.help()}, "{help}")
            return 1
    return run_awsy(command_context, tests, **kwargs)
