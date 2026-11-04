#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import traceback

import mozinfo

# need this so raptor imports work both from /raptor and via mach
here = os.path.abspath(os.path.dirname(__file__))

try:
    from mozbuild.base import MozbuildObject

    build = MozbuildObject.from_environment(cwd=here)
except ImportError:
    build = None

from cmdline import DESKTOP_APPS, parse_args
from logger.logger import RaptorLogger
from manifest import get_raptor_test_list
from mozlog import commandline
from mozprofile.cli import parse_key_value, parse_preferences
from signal_handler import SignalHandler
from utils import view_gecko_profile_from_raptor

LOG = RaptorLogger(component="raptor-main")


def main(args=sys.argv[1:]):
    args = parse_args()

    args.extra_prefs = parse_preferences(args.extra_prefs or [])

    if args.enable_marionette_trace:
        args.extra_prefs.update({
            "remote.log.level": "Trace",
        })

    args.environment = dict(parse_key_value(args.environment or [], context="--setenv"))

    commandline.setup_logging("raptor", args, {"tbpl": sys.stdout})
    LOG.info(f"Python version: {sys.version}")
    LOG.info("raptor-start")

    if args.debug_mode:
        LOG.info("debug-mode enabled")

    LOG.info(f"received command line arguments: {args}")

    # if a test name specified on command line, and it exists, just run that one
    # otherwise run all available raptor tests that are found for this browser
    raptor_test_list = get_raptor_test_list(args, mozinfo.os)
    raptor_test_names = [raptor_test["name"] for raptor_test in raptor_test_list]

    # ensure we have at least one valid test to run
    if len(raptor_test_list) == 0:
        LOG.critical(
            f"test '{args.test}' could not be found for {args.app}. "
            "For a list of tests run: ./mach raptor --print-tests"
        )
        sys.exit(1)

    LOG.info("raptor tests scheduled to run:")
    for next_test in raptor_test_list:
        LOG.info(next_test["name"])

    def raptor_class(*inner_args, **inner_kwargs):
        outer_kwargs = vars(args)
        # peel off arguments that are specific to browsertime
        for key in outer_kwargs.keys():
            if key.startswith("browsertime_"):
                inner_kwargs[key] = outer_kwargs.get(key)

        if args.app in DESKTOP_APPS:
            from browsertime.desktop import BrowsertimeDesktop as klass
        else:
            from browsertime.android import BrowsertimeAndroid as klass

        return klass(*inner_args, **inner_kwargs)

    try:
        raptor = raptor_class(
            args.app,
            args.binary,
            run_local=args.run_local,
            no_install=args.no_install,
            installerpath=args.installerpath,
            obj_path=args.obj_path,
            gecko_profile=args.gecko_profile,
            gecko_profile_interval=args.gecko_profile_interval,
            gecko_profile_entries=args.gecko_profile_entries,
            gecko_profile_extra_threads=args.gecko_profile_extra_threads,
            gecko_profile_threads=args.gecko_profile_threads,
            gecko_profile_features=args.gecko_profile_features,
            extra_profiler_run=args.extra_profiler_run,
            etw_profile=args.etw_profile,
            samply_profile=args.samply_profile,
            perf_profile=args.perf_profile,
            symbols_path=args.symbols_path,
            host=args.host,
            live_sites=args.live_sites,
            cold=args.cold,
            is_release_build=args.is_release_build,
            debug_mode=args.debug_mode,
            post_startup_delay=args.post_startup_delay,
            activity=args.activity,
            intent=args.intent,
            interrupt_handler=SignalHandler(),
            extra_prefs=args.extra_prefs or {},
            environment=args.environment or {},
            device_name=args.device_name,
            disable_perf_tuning=args.disable_perf_tuning,
            conditioned_profile=args.conditioned_profile,
            install_extensions=args.install_extensions,
            test_bytecode_cache=args.test_bytecode_cache,
            chimera=args.chimera,
            project=args.project,
            verbose=args.verbose,
            fission=args.fission,
            extra_summary_methods=args.extra_summary_methods,
            benchmark_repository=args.benchmark_repository,
            benchmark_revision=args.benchmark_revision,
            benchmark_branch=args.benchmark_branch,
            page_timeout=args.page_timeout,
            clean=args.clean,
            screenshot_on_failure=args.screenshot_on_failure,
            power_test=args.power_test,
            simpleperf=args.simpleperf,
            test_names=raptor_test_names,
        )
    except Exception:
        traceback.print_exc()
        LOG.critical(
            "TEST-UNEXPECTED-FAIL: could not initialize the raptor test runner"
        )
        os.sys.exit(1)

    raptor.results_handler.use_existing_results(args.browsertime_existing_results)
    success = raptor.run_tests(raptor_test_list)

    if not success:
        # if we have results but one test page timed out (i.e. one tp6 test page didn't load
        # but others did) we still dumped PERFHERDER_DATA for the successfull pages but we
        # want the overall test job to marked as a failure
        pages_that_timed_out = raptor.get_page_timeout_list()
        if pages_that_timed_out:
            for _page in pages_that_timed_out:
                message = [
                    ("TEST-UNEXPECTED-FAIL", f"test '{_page['test_name']}'"),
                    ("timed out loading test page", "waiting for pending metrics"),
                ]
                if _page.get("pending_metrics") is not None:
                    LOG.warning(
                        "page cycle {} has pending metrics: {}".format(
                            _page["page_cycle"], _page["pending_metrics"]
                        )
                    )

                LOG.critical(" ".join(f"{subject}: {msg}" for subject, msg in message))
        else:
            # we want the job to fail when we didn't get any test results
            # (due to test timeout/crash/etc.)
            LOG.critical(
                "TEST-UNEXPECTED-FAIL: no raptor test results were found for "
                f"{', '.join(raptor_test_names)}"
            )
        os.sys.exit(1)

    # if we're running browsertime in the CI, we want to zip the result dir
    if args.browsertime and not args.run_local:
        result_dir = raptor.results_handler.result_dir()
        if os.path.exists(result_dir):
            is_profiling_job = True if args.gecko_profile else False
            raptor.results_handler.archive_raptor_artifacts(is_profiling_job)

    # when running raptor locally with gecko profiling on, use the view-gecko-profile
    # tool to automatically load the latest gecko profile in profiler.firefox.com
    profiling = any([
        args.gecko_profile,
        args.simpleperf,
        args.samply_profile,
        args.perf_profile,
    ])
    if profiling and args.run_local:
        if os.environ.get("DISABLE_PROFILE_LAUNCH", "0") == "1":
            LOG.info(
                "Not launching profiler.firefox.com because DISABLE_PROFILE_LAUNCH=1"
            )
        else:
            view_gecko_profile_from_raptor()


if __name__ == "__main__":
    main()
