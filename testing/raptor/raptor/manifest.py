# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import os
import pathlib
import re
from urllib.parse import (
    parse_qs,
    unquote,
    urlencode,
    urlsplit,
    urlunsplit,
)

from cmdline import GECKO_PROFILER_APPS, TRACE_APPS
from constants.raptor_tests_constants import YOUTUBE_PLAYBACK_MEASURE
from logger.logger import RaptorLogger
from manifestparser import TestManifest
from support_class_utils import import_support_class
from utils import (
    bool_from_str,
    transform_platform,
    transform_subtest,
)

here = os.path.abspath(os.path.dirname(__file__))
raptor_toml = os.path.join(here, "raptor.toml")
tests_dir = os.path.join(here, "tests")
LOG = RaptorLogger(component="raptor-manifest")

LIVE_SITE_TIMEOUT_MULTIPLIER = 1.2

required_settings = [
    "alert_threshold",
    "subtest_alert_threshold",
    "apps",
    "lower_is_better",
    "measure",
    "page_cycles",
    "test_url",
    "scenario_time",
    "type",
    "unit",
]

playback_settings = [
    "playback_pageset_manifest",
]

bool_settings = [
    "lower_is_better",
    "subtest_lower_is_better",
    "accept_zero_vismet",
    "interactive",
    "host_from_parent",
    "expose_browser_profiler",
    "expose_chrome_trace",
    "gather_cpuTime",
    "sparse_checkout",
    "verbose",
    "browser_visualmetrics",
    "login",
    "cold",
    "use_live_sites",
    "newtab_per_cycle",
    "background_test",
    "benchmark_page",
    "benchmark_webserver",
    "screen_capture",
]


def filter_app(tests, values, strict=False):
    for test in tests:
        if values["app"] in [app.strip() for app in test["apps"].split(",")]:
            yield test


def get_browser_test_list(browser_app, run_local):
    LOG.info(raptor_toml)
    test_manifest = TestManifest([raptor_toml], strict=False)
    info = {"app": browser_app, "run_local": run_local}
    return test_manifest.active_tests(
        exists=False, disabled=False, filters=[filter_app], **info
    )


def validate_test_toml(test_details):
    # validate all required test details were found in the test TOML
    valid_settings = True

    for setting in required_settings:
        # measure setting not required for benchmark type tests
        if setting == "measure" and test_details["type"] == "benchmark":
            continue
        if setting == "scenario_time" and test_details["type"] != "scenario":
            continue
        # subtest_alert_threshold is optional
        if setting == "subtest_alert_threshold":
            continue
        if test_details.get(setting) is None:
            # if page-cycles is not specified, it's ok as long as browser-cycles is there
            if (
                setting == "page-cycles"
                and test_details.get("browser_cycles") is not None
            ):
                continue
            valid_settings = False
            LOG.error(
                f"setting '{setting}' is required but not found in "
                f"{test_details['manifest']}"
            )

    test_details.setdefault("page_timeout", 30000)

    # if playback is specified, we need more playback settings
    if test_details.get("playback") is not None:
        for setting in playback_settings:
            if test_details.get(setting) is None:
                valid_settings = False
                LOG.error(
                    f"setting '{setting}' is required but not found in "
                    f"{test_details['manifest']}"
                )

    # if 'alert-on' is specified, we need to make sure that the value given is valid
    # i.e. any 'alert_on' values must be values that exist in the 'measure' toml setting
    if test_details.get("alert_on") is not None:
        # support with or without spaces, i.e. 'measure = fcp, loadtime' or '= fcp,loadtime'
        # convert to a list; and remove any spaces
        # when switching to .toml, values are \n separated, convert to a ',' and this will
        # support scenarios where we have older .ini format as well as embedded commas
        # this can also have regexes inside
        test_details["alert_on"] = [
            _item.strip()
            for _item in test_details["alert_on"].replace("\n", ",").split(",")
        ]

        # this variable will store all the concrete values for alert_on elements
        # that have a match in "measure" list
        valid_alerts = []

        # if test is raptor-youtube-playback and measure is empty, use all the tests
        if test_details.get(
            "measure"
        ) is None and "youtube-playback" in test_details.get("name", ""):
            test_details["measure"] = YOUTUBE_PLAYBACK_MEASURE

        # convert "measure" to string, so we can use it inside a regex
        measure_as_string = " ".join(test_details["measure"])

        # now make sure each alert_on value provided is valid
        for alert_on_value in test_details["alert_on"]:
            # replace the '*' with a valid regex pattern
            alert_on_value_pattern = alert_on_value.replace("*", "[a-zA-Z0-9.@_%]*")
            # store all elements that have been found in "measure_as_string"
            matches = re.findall(alert_on_value_pattern, measure_as_string)

            if len(matches) == 0:
                LOG.error(
                    f"The 'alert_on' value of '{alert_on_value}' is not valid "
                    "because it doesn't exist in the 'measure' test setting!"
                )
                valid_settings = False
            else:
                # add the matched elements to valid_alerts
                valid_alerts.extend(matches)

        # replace old alert_on values with valid elements (no more regexes inside)
        # and also remove duplicates if any, by converting valid_alerts to a 'set' first
        test_details["alert_on"] = sorted(set(valid_alerts))

    # if repository is defined, then a revision also needs to be defined
    # the path is optional and we'll default to the root of the repo
    if test_details.get("repository", None) is not None:
        if test_details.get("repository_revision", None) is None:
            LOG.error(
                "`repository_revision` is required when a `repository` is defined."
            )
            valid_settings = False
        elif test_details.get("type") not in ("benchmark") and not test_details.get(
            "benchmark_webserver", False
        ):
            LOG.error("`repository` is only available for benchmark test types.")
            valid_settings = False

    return valid_settings


def add_test_url_params(url, extra_params):
    # add extra parameters to the test_url query string
    # the values that already exist are re-written

    # urlsplit returns a result as a tuple like (scheme, netloc, path, query, fragment)
    parsed_url = urlsplit(url)

    parsed_query_params = parse_qs(parsed_url.query)
    parsed_extra_params = parse_qs(extra_params)

    for name, value in parsed_extra_params.items():
        # overwrite the old value
        parsed_query_params[name] = value

    final_query_string = unquote(urlencode(parsed_query_params, doseq=True))

    # reconstruct test_url with the changed query string
    return urlunsplit((
        parsed_url.scheme,
        parsed_url.netloc,
        parsed_url.path,
        final_query_string,
        parsed_url.fragment,
    ))


def write_test_settings_json(args, test_details, oskey):
    # write test settings json file with test details that the control
    # server will provide for the web ext
    test_url = transform_platform(test_details["test_url"], oskey)
    # this is needed for raptor browsertime to pick up the replaced
    # {platform} argument for motionmark tests
    test_details["test_url"] = test_url

    test_settings = {
        "raptor-options": {
            "type": test_details["type"],
            "cold": test_details["cold"],
            "test_url": test_url,
            "expected_browser_cycles": test_details["expected_browser_cycles"],
            "page_cycles": int(test_details["page_cycles"]),
            "host": args.host,
        }
    }

    if test_details["type"] == "pageload":
        test_settings["raptor-options"]["measure"] = {}

        # test_details['measure'] was already converted to a list in get_raptor_test_list below
        # the 'hero=' line is still a raw string from the test TOML
        for m in test_details["measure"]:
            test_settings["raptor-options"]["measure"][m] = True
            if m == "hero":
                test_settings["raptor-options"]["measure"][m] = [
                    h.strip() for h in test_details["hero"].split(",")
                ]

        if test_details.get("alert_on", None) is not None:
            # alert_on was already converted to list above
            test_settings["raptor-options"]["alert_on"] = test_details["alert_on"]

    if test_details.get("page_timeout", None) is not None:
        test_settings["raptor-options"]["page_timeout"] = int(
            test_details["page_timeout"]
        )

    test_settings["raptor-options"]["unit"] = test_details.get("unit", "ms")

    test_settings["raptor-options"]["lower_is_better"] = test_details.get(
        "lower_is_better", True
    )

    # support optional subtest unit/lower_is_better fields
    val = test_details.get("subtest_unit", test_settings["raptor-options"]["unit"])
    test_settings["raptor-options"]["subtest_unit"] = val
    subtest_lower_is_better = test_details.get("subtest_lower_is_better")

    if subtest_lower_is_better is None:
        # default to main test values if not set
        test_settings["raptor-options"]["subtest_lower_is_better"] = test_settings[
            "raptor-options"
        ]["lower_is_better"]
    else:
        test_settings["raptor-options"]["subtest_lower_is_better"] = (
            subtest_lower_is_better
        )

    if test_details.get("alert_change_type", None) is not None:
        test_settings["raptor-options"]["alert_change_type"] = test_details[
            "alert_change_type"
        ]

    if test_details.get("alert_threshold", None) is not None:
        test_settings["raptor-options"]["alert_threshold"] = float(
            test_details["alert_threshold"]
        )

    if test_details.get("screen_capture", None) is not None:
        test_settings["raptor-options"]["screen_capture"] = test_details.get(
            "screen_capture"
        )

    # if Gecko profiling is enabled, write profiling settings for webext
    if test_details.get("gecko_profile", False):
        threads = ["GeckoMain", "Compositor", "Renderer"]

        if test_details.get("gecko_profile_threads"):
            # pylint --py3k: W1639
            test_threads = list(
                filter(None, test_details["gecko_profile_threads"].split(","))
            )
            threads.extend(test_threads)

        test_settings["raptor-options"].update({
            "gecko_profile": True,
            "gecko_profile_entries": int(
                test_details.get("gecko_profile_entries", 1000000)
            ),
            "gecko_profile_interval": float(
                test_details.get("gecko_profile_interval", 1)
            ),
            "gecko_profile_threads": ",".join(set(threads)),
        })

        features = test_details.get("gecko_profile_features")
        if features:
            test_settings["raptor-options"]["gecko_profile_features"] = features

    if test_details.get("extra_profiler_run", False):
        test_settings["raptor-options"]["extra_profiler_run"] = True

    if test_details.get("newtab_per_cycle", None) is not None:
        test_settings["raptor-options"]["newtab_per_cycle"] = test_details[
            "newtab_per_cycle"
        ]

    if test_details["type"] == "scenario":
        test_settings["raptor-options"]["scenario_time"] = test_details["scenario_time"]
        if "background_test" in test_details:
            test_settings["raptor-options"]["background_test"] = test_details[
                "background_test"
            ]
        else:
            test_settings["raptor-options"]["background_test"] = False

    jsons_dir = os.path.join(tests_dir, "json")

    if not os.path.exists(jsons_dir):
        os.mkdir(os.path.join(tests_dir, "json"))

    settings_file = os.path.join(jsons_dir, test_details["name"] + ".json")
    try:
        with open(settings_file, "w") as out_file:
            json.dump(test_settings, out_file, indent=4, ensure_ascii=False)
            out_file.close()
    except OSError:
        LOG.info("abort: exception writing test settings json!")


def get_raptor_test_list(args, oskey):
    """
    A test toml (i.e. raptor-firefox-tp6.toml) will have one or more subtests inside,
    each with it's own name ([the-toml-file-test-section]).

    We want the ability to eiter:
        - run * all * of the subtests listed inside the test toml; - or -
        - just run a single one of those subtests that are inside the toml

    A test name is received on the command line. This will either match the name
    of a single subtest (within an toml) - or - if there's no matching single
    subtest with that name, then the test name provided might be the name of a
    test toml itself (i.e. raptor-firefox-tp6) that contains multiple subtests.

    First look for a single matching subtest name in the list of all availble tests,
    and if it's found we will just run that single subtest.

    Then look at the list of all available tests - each available test has a manifest
    name associated to it - and pull out all subtests whose manifest name matches
    the test name provided on the command line i.e. run all subtests in a specified toml.

    If no tests are found at all then the test name is invalid.
    """
    tests_to_run = []
    # get list of all available tests for the browser we are testing against
    available_tests = get_browser_test_list(args.app, args.run_local)

    # look for single subtest that matches test name provided on cmd line
    for next_test in available_tests:
        if next_test["name"] == args.test:
            tests_to_run.append(next_test)
            break

    # Check to make sure that there isn't another test with the same name
    # and raise an exception if that happens
    all_tests_available = [
        next_test for next_test in available_tests if next_test["name"] == args.test
    ]
    if len(all_tests_available) > 1:
        raise Exception(
            f"Too many tests found with the same test name `{args.test}` for this app. "
            f"Found in these manifests: "
            f"{[test['manifest'] for test in all_tests_available]}"
        )

    # no matches, so now look for all subtests that come from a test toml
    # manifest that matches the test name provided on the commmand line
    if len(tests_to_run) == 0:
        _toml = args.test + ".toml"
        for next_test in available_tests:
            head, tail = os.path.split(next_test["manifest"])
            if tail == _toml:
                # subtest comes from matching test toml file name, so add it
                tests_to_run.append(next_test)

    if args.collect_perfstats and args.app.lower() not in (
        "chrome",
        "custom-car",
    ):
        for next_test in tests_to_run:
            next_test["perfstats"] = "true"

    # enable live sites if requested with --live-sites
    if args.live_sites:
        for next_test in tests_to_run:
            # set use_live_sites to `true` and disable mitmproxy playback
            # immediately so we don't follow playback paths below
            next_test["use_live_sites"] = "true"
            next_test["playback"] = None

    # go through each test and set the page-cycles and page-timeout, and some config flags
    # the page-cycles value in the TOML can be overriden when debug-mode enabled, when
    # gecko-profiling enabled, or when --page-cycles cmd line arg was used (that overrides all)
    for next_test in tests_to_run:
        LOG.info(f"configuring settings for test {next_test['name']}")
        max_page_cycles = int(next_test.get("page_cycles", 1))
        max_browser_cycles = int(next_test.get("browser_cycles", 1))

        for setting in bool_settings:
            if next_test.get(setting, None) is not None:
                next_test[setting] = bool_from_str(next_test.get(setting))

        # If using playback, the playback recording info may need to be transformed.
        # This transformation needs to happen before the test name is changed
        # below (for cold tests for instance)

        if next_test.get("playback") is not None:
            next_test["playback_pageset_manifest"] = transform_subtest(
                next_test["playback_pageset_manifest"], next_test["name"]
            )

        # Check if either --gecko-profiler or --extra-profiler-run is enabled.
        if args.gecko_profile or (
            args.extra_profiler_run and (args.app in GECKO_PROFILER_APPS + TRACE_APPS)
        ):
            if args.gecko_profile:
                # This is a --gecko-profiler run.
                next_test["gecko_profile"] = True
                LOG.info("gecko-profiling enabled")
                max_page_cycles = 3
                max_browser_cycles = 3
            else:
                # This is an --extra-profiler-run run.
                next_test["extra_profiler_run"] = True
                LOG.info("extra-profiler-run enabled")
                next_test["extra_profiler_run_browser_cycles"] = 1
                if args.chimera:
                    next_test["extra_profiler_run_page_cycles"] = 2
                else:
                    next_test["extra_profiler_run_page_cycles"] = 1

            # Both --gecko-profiler and --extra-profiler-run shares the same arguments for Firefox.
            if args.app in GECKO_PROFILER_APPS:
                if (
                    "gecko_profile_entries" in args
                    and args.gecko_profile_entries is not None
                ):
                    next_test["gecko_profile_entries"] = str(args.gecko_profile_entries)
                    LOG.info(
                        f"gecko-profiling entries set to {args.gecko_profile_entries}"
                    )

                if (
                    "gecko_profile_interval" in args
                    and args.gecko_profile_interval is not None
                ):
                    next_test["gecko_profile_interval"] = str(
                        args.gecko_profile_interval
                    )
                    LOG.info(
                        f"gecko-profiling interval set to {args.gecko_profile_interval}"
                    )

                if (
                    "gecko_profile_threads" in args
                    and args.gecko_profile_threads is not None
                ):
                    # pylint --py3k: W1639
                    threads = list(
                        filter(
                            None, next_test.get("gecko_profile_threads", "").split(",")
                        )
                    )
                    threads.extend(args.gecko_profile_threads.split(","))
                    if (
                        "gecko_profile_extra_threads" in args
                        and args.gecko_profile_extra_threads is not None
                    ):
                        threads.extend(getattr(args, "gecko_profile_extra_threads", []))
                    next_test["gecko_profile_threads"] = ",".join(threads)
                    LOG.info(f"gecko-profiling threads {args.gecko_profile_threads}")
                if (
                    "gecko_profile_features" in args
                    and args.gecko_profile_features is not None
                ):
                    next_test["gecko_profile_features"] = args.gecko_profile_features
                    LOG.info(f"gecko-profiling features {args.gecko_profile_features}")
        else:
            # if the gecko profiler or extra profiler run is not enabled, ignore all of its
            # settings.
            args.extra_profiler_run = False
            next_test.pop("gecko_profile_entries", None)
            next_test.pop("gecko_profile_interval", None)
            next_test.pop("gecko_profile_threads", None)
            next_test.pop("gecko_profile_features", None)

        if args.debug_mode is True:
            next_test["debug_mode"] = True
            LOG.info("debug-mode enabled")
            max_page_cycles = 2

        # if --page-cycles was provided on the command line, use that instead of TOML
        # if just provided in the TOML use that but cap at 3 if gecko-profiling is enabled
        if args.page_cycles is not None:
            next_test["page_cycles"] = args.page_cycles
            LOG.info(
                f"setting page-cycles to {args.page_cycles} as specified on cmd line"
            )
        elif int(next_test.get("page_cycles", 1)) > max_page_cycles:
            next_test["page_cycles"] = max_page_cycles
            LOG.info(
                f"setting page-cycles to {next_test['page_cycles']} because "
                "gecko-profling is enabled"
            )

        # if --browser-cycles was provided on the command line, use that instead of TOML
        # if just provided in the TOML use that but cap at 3 if gecko-profiling is enabled
        if args.browser_cycles is not None:
            next_test["browser_cycles"] = args.browser_cycles
            LOG.info(
                f"setting browser-cycles to {args.browser_cycles} as specified "
                "on cmd line"
            )
        elif int(next_test.get("browser_cycles", 1)) > max_browser_cycles:
            next_test["browser_cycles"] = max_browser_cycles
            LOG.info(
                f"setting browser-cycles to {next_test['browser_cycles']} because "
                "gecko-profilng is enabled"
            )

        # if --page-timeout was provided on the command line, use that instead of TOML
        if args.page_timeout is not None:
            LOG.info(
                f"setting page-timeout to {args.page_timeout} as specified on cmd line"
            )
            next_test["page_timeout"] = args.page_timeout

        _running_cold = False

        # check command line to see if we set cold page load from command line
        if args.cold or next_test.get("cold"):
            # for raptor-webext jobs cold page-load is determined by the 'cold' key
            # in test manifest TOML
            _running_cold = True
        else:
            # if it's a warm load test ignore browser_cycles if set
            next_test["browser_cycles"] = 1

        if _running_cold:
            # when running in cold mode, set browser-cycles to the page-cycles value; as we want
            # the browser to restart between page-cycles; and set page-cycles to 1 as we only
            # want 1 single page-load for every browser-cycle
            next_test["cold"] = True
            next_test["expected_browser_cycles"] = int(next_test["browser_cycles"])
            if args.chimera:
                next_test["page_cycles"] = 2
            else:
                next_test["page_cycles"] = 1
        else:
            # when running in warm mode, just set test-cycles to 1 and leave page-cycles as/is
            next_test["cold"] = False
            next_test["expected_browser_cycles"] = 1

        # either warm or cold-mode, initialize the starting current 'browser-cycle'
        next_test["browser_cycle"] = 1

        # if --test-url-params was provided on the command line, add the params to the test_url
        # provided in the TOML
        if args.test_url_params is not None:
            initial_test_url = next_test["test_url"]
            next_test["test_url"] = add_test_url_params(
                initial_test_url, args.test_url_params
            )
            LOG.info(
                f"adding extra test_url params ({args.test_url_params}) as specified "
                f"on cmd line to the current test_url ({initial_test_url}), "
                f"resulting: {next_test['test_url']}"
            )

        if next_test.get("use_live_sites"):
            # when using live sites we want to turn off playback
            LOG.info("using live sites so turning playback off!")
            next_test["playback"] = None
            # Only for raptor-youtube-playback tests until they are removed
            # in favor of the browsertime variant
            if "raptor-youtube-playback" in next_test["name"]:
                next_test["name"] = next_test["name"] + "-live"
            # allow a slightly higher page timeout due to remote page loads
            next_test["page_timeout"] = (
                int(next_test["page_timeout"]) * LIVE_SITE_TIMEOUT_MULTIPLIER
            )
            LOG.info(
                f"using live sites so using page timeout of {next_test['page_timeout']}ms"
            )

        if not args.browsertime and "browsertime" in next_test.get("manifest", ""):
            raise Exception(
                f"{next_test.get('name', 'Unknown')} test can only be run with "
                "--browsertime"
            )

        # browsertime doesn't use the 'measure' test toml setting; however just for the sake
        # of supporting both webext and browsertime, just provide a dummy 'measure' setting
        # here to prevent having to check in multiple places; it has no effect on what
        # browsertime actually measures; remove this when eventually we remove webext support
        if (
            args.browsertime
            and next_test.get("measure") is None
            and next_test.get("type") == "pageload"
        ):
            next_test["measure"] = (
                "fnbpaint, fcp, loadtime, "
                "ContentfulSpeedIndex, PerceptualSpeedIndex, "
                "SpeedIndex, FirstVisualChange, LastVisualChange, "
                "largestContentfulPaint"
            )

        # convert 'measure =' test TOML line to list
        if next_test.get("measure") is not None:
            _measures = []
            for measure in [m.strip() for m in next_test["measure"].split(",")]:
                # build the 'measures =' list
                _measures.append(measure)
            next_test["measure"] = _measures

            # if using live sites, don't measure hero element as it only exists in recordings
            if "hero" in next_test["measure"] and next_test.get("use_live_sites"):
                # remove 'hero' from the 'measures =' list
                next_test["measure"].remove("hero")
                # remove the 'hero =' line since no longer measuring hero
                del next_test["hero"]

        if next_test.get("support_class", None) is not None:
            support_class = import_support_class(
                pathlib.Path(
                    here,
                    "..",
                    "browsertime",
                    "support-scripts",
                    next_test["support_class"],
                ).resolve()
            )
            next_test["support_class"] = support_class()
            next_test["support_class"].setup_test(next_test, args)

    # write out .json test setting files for the control server to read and send to web ext
    if len(tests_to_run) != 0:
        for test in tests_to_run:
            if validate_test_toml(test):
                write_test_settings_json(args, test, oskey)
            else:
                # test doesn't have valid settings, remove it from available list
                LOG.info(f"test {test['name']} is not valid due to missing settings")
                tests_to_run.remove(test)

    return tests_to_run
