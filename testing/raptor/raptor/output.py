# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# some parts of this originally taken from /testing/talos/talos/output.py

"""output raptor test results"""

import copy
import json
import os
import platform
import warnings
from abc import ABCMeta, abstractmethod
from collections.abc import Iterable

import filters
from cmdline import ANDROID_APPS
from logger.logger import RaptorLogger
from utils import flatten

LOG = RaptorLogger(component="perftest-output")

VISUAL_METRICS = [
    "SpeedIndex",
    "ContentfulSpeedIndex",
    "PerceptualSpeedIndex",
    "FirstVisualChange",
    "LastVisualChange",
    "VisualReadiness",
    "VisualComplete85",
    "VisualComplete95",
    "VisualComplete99",
]

METRIC_BLOCKLIST = [
    "mean",
    "median",
    "geomean",
]


class PerftestOutput(metaclass=ABCMeta):
    """Abstract base class to handle output of perftest results"""

    def __init__(
        self, results, supporting_data, subtest_alert_on, app, extra_summary_methods=[]
    ):
        """
        - results : list of RaptorTestResult instances
        """
        self.app = app
        self.results = results
        self.summarized_results = {}
        self.supporting_data = supporting_data
        self.summarized_supporting_data = []
        self.summarized_screenshots = []
        self.subtest_alert_on = subtest_alert_on
        self.browser_name = None
        self.browser_version = None
        self.os_name = platform.system()
        self.os_platform_version = None
        self.extra_summary_methods = extra_summary_methods

    @abstractmethod
    def summarize(self, test_names):
        raise NotImplementedError()

    def set_browser_meta(self, browser_name, browser_version):
        # sets the browser metadata for the perfherder data
        self.browser_name = browser_name
        self.browser_version = browser_version

    def _set_platform_version(self):
        if self.os_name == "Windows":
            self.os_platform_version = platform.uname().version
        elif self.os_name == "Darwin":
            self.os_platform_version = platform.mac_ver()[0]
        else:  # Linux
            self.os_platform_version = platform.release()

    def summarize_supporting_data(self):
        """
        Supporting data was gathered outside of the main raptor test; it will be kept
        separate from the main raptor test results. Summarize it appropriately.

        supporting_data = {
            'type': 'data-type',
            'test': 'raptor-test-ran-when-data-was-gathered',
            'unit': 'unit that the values are in',
            'summarize-values': True/False,
            'suite-suffix-type': True/False,
            'values': {
                'name': value_dict,
                'nameN': value_dictN
            }
        }

        More specifically, subtest supporting data will look like this:

        supporting_data = {
            'type': 'power',
            'test': 'raptor-speedometer-geckoview',
            'unit': 'mAh',
            'values': {
                'cpu': {
                    'values': val,
                    'lowerIsBetter': True/False,
                    'alertThreshold': 2.0,
                    'subtest-prefix-type': True/False,
                    'unit': 'mWh'
                },
                'wifi': ...
            }
        }

        We want to treat each value as a 'subtest'; and for the overall aggregated
        test result the summary value is dependent on the unit. An exception is
        raised in case we don't know about the specified unit.
        """
        if self.supporting_data is None:
            return

        self.summarized_supporting_data = []
        support_data_by_type = {}

        for data_set in self.supporting_data:
            data_type = data_set["type"]
            LOG.info(f"summarizing {data_type} data")

            if data_type not in support_data_by_type:
                support_data_by_type[data_type] = {
                    "framework": {"name": "raptor"},
                    "suites": [],
                }

            # suite name will be name of the actual raptor test that ran, plus the type of
            # supporting data i.e. 'raptor-speedometer-geckoview-power'
            vals = []
            subtests = []

            suite_name = data_set["test"]
            if data_set.get("suite-suffix-type", True):
                suite_name = f"{data_set['test']}-{data_set['type']}"

            suite = {
                "name": suite_name,
                "type": data_set["type"],
                "subtests": subtests,
            }
            if data_set.get("summarize-values", True):
                suite.update({
                    "lowerIsBetter": True,
                    "unit": data_set["unit"],
                    "alertThreshold": 2.0,
                })

            for result in self.results:
                if result["name"] == data_set["test"]:
                    suite["extraOptions"] = result["extra_options"]
                    break

            support_data_by_type[data_type]["suites"].append(suite)
            for measurement_name, value_info in data_set["values"].items():
                # Subtests are expected to be specified in a dictionary, this
                # provides backwards compatibility with the old method
                value = value_info
                if not isinstance(value_info, dict):
                    value = {"values": value_info}
                new_subtest = {}
                if value.get("subtest-prefix-type", True):
                    new_subtest["name"] = data_type + "-" + measurement_name
                else:
                    new_subtest["name"] = measurement_name

                new_subtest["value"] = value["values"]
                new_subtest["lowerIsBetter"] = value.get("lowerIsBetter", True)
                new_subtest["alertThreshold"] = value.get("alertThreshold", 2.0)
                new_subtest["unit"] = value.get("unit", data_set["unit"])

                if "shouldAlert" in value:
                    new_subtest["shouldAlert"] = value.get("shouldAlert")

                subtests.append(new_subtest)
                vals.append([new_subtest["value"], new_subtest["name"]])

            if len(subtests) >= 1 and data_set.get("summarize-values", True):
                suite["value"] = self.construct_summary(
                    vals, testname="supporting_data", unit=data_set["unit"]
                )

        # split the supporting data by type, there will be one
        # perfherder output per type
        for data_type in support_data_by_type:
            data = support_data_by_type[data_type]
            if self.browser_name:
                data["application"] = {"name": self.browser_name}
                if self.browser_version:
                    data["application"]["version"] = self.browser_version

            # Add os info only for desktop
            if self.app not in ANDROID_APPS and self.os_name:
                data["os"] = {"name": self.os_name}
                self._set_platform_version()
                if self.os_platform_version:
                    self.data["os"]["platform_version"] = self.os_platform_version
            self.summarized_supporting_data.append(data)

        return

    def output(self, test_names):
        """output to file and perfherder data json"""
        if os.getenv("MOZ_UPLOAD_DIR"):
            # i.e. testing/mozharness/build/raptor.json locally; in production it will
            # be at /tasks/task_*/build/ (where it will be picked up by mozharness later
            # and made into a tc artifact accessible in treeherder as perfherder-data.json)
            results_path = os.path.join(
                os.path.dirname(os.environ["MOZ_UPLOAD_DIR"]), "raptor.json"
            )
            screenshot_path = os.path.join(
                os.path.dirname(os.environ["MOZ_UPLOAD_DIR"]), "screenshots.html"
            )
        else:
            results_path = os.path.join(os.getcwd(), "raptor.json")
            screenshot_path = os.path.join(os.getcwd(), "screenshots.html")

        success = True
        if self.summarized_results == {}:
            success = False
            LOG.error(
                f"no summarized raptor results found for any of {', '.join(test_names)}"
            )
        else:
            for suite in self.summarized_results["suites"]:
                gecko_profiling_enabled = "gecko-profile" in suite.get(
                    "extraOptions", []
                )
                if gecko_profiling_enabled:
                    LOG.info("gecko profiling enabled")
                    suite["shouldAlert"] = False

                # as we do navigation, tname could end in .<alias>
                # test_names doesn't have tname, so either add it to test_names,
                # or strip it
                tname = suite["name"]
                parts = tname.split(".")
                try:
                    tname = ".".join(parts[:-1])
                except Exception as e:
                    LOG.info(f"no alias found on test, ignoring: {e}")
                    pass

                # Since test names might have been modified, check if
                # part of the test name exists in the test_names list entries
                found = False
                for test in test_names:
                    if tname in test:
                        found = True
                        break
                if not found:
                    success = False
                    LOG.error(f"no summarized raptor results found for {tname}")

            with open(results_path, "w") as f:
                for result in self.summarized_results:
                    f.write(f"{result}\n")

        if len(self.summarized_screenshots) > 0:
            with open(screenshot_path, "w") as f:
                for result in self.summarized_screenshots:
                    f.write(f"{result}\n")
            LOG.info(f"screen captures can be found locally at: {screenshot_path}")

        # now that we've checked for screen captures too, if there were no actual
        # test results we can bail out here
        if self.summarized_results == {}:
            return success, 0

        test_type = self.summarized_results["suites"][0].get("type", "")
        output_perf_data = True
        not_posting = "- not posting regular test results for perfherder"
        if test_type == "scenario":
            # if a resource-usage flag was supplied the perfherder data
            # will still be output from output_supporting_data
            LOG.info(f"scenario test type was run {not_posting}")
            output_perf_data = False

        if self.browser_name:
            self.summarized_results["application"] = {"name": self.browser_name}
            if self.browser_version:
                self.summarized_results["application"]["version"] = self.browser_version

        # Add os info only for desktop
        if self.app not in ANDROID_APPS and self.os_name:
            self.summarized_results["os"] = {"name": self.os_name}
            self._set_platform_version()
            if self.os_platform_version:
                self.summarized_results["os"]["platform_version"] = (
                    self.os_platform_version
                )

        total_perfdata = 0
        if output_perf_data:
            # if we have supporting data i.e. power, we ONLY want those measurements
            # dumped out. TODO: Bug 1515406 - Add option to output both supplementary
            # data (i.e. power) and the regular Raptor test result
            # Both are already available as separate PERFHERDER_DATA json blobs
            if len(self.summarized_supporting_data) == 0:
                LOG.info(f"PERFHERDER_DATA: {json.dumps(self.summarized_results)}")
                total_perfdata = 1
            else:
                LOG.info(
                    "supporting data measurements exist - only posting those to perfherder"
                )

        json.dump(
            self.summarized_results, open(results_path, "w"), indent=2, sort_keys=True
        )
        LOG.info(f"results can also be found locally at: {results_path}")

        return success, total_perfdata

    def output_supporting_data(self, test_names):
        """
        Supporting data was gathered outside of the main raptor test; it has already
        been summarized, now output it appropriately.

        We want to output supporting data in a completely separate perfherder json blob and
        in a corresponding file artifact. This way, supporting data can be ingested as its own
        test suite in perfherder and alerted upon if desired; kept outside of the test results
        from the actual Raptor test which was run when the supporting data was gathered.
        """
        if len(self.summarized_supporting_data) == 0:
            LOG.error(
                f"no summarized supporting data found for {', '.join(test_names)}"
            )
            return False, 0

        total_perfdata = 0
        for next_data_set in self.summarized_supporting_data:
            data_type = next_data_set["suites"][0]["type"]

            if os.environ["MOZ_UPLOAD_DIR"]:
                # i.e. testing/mozharness/build/raptor.json locally; in production it will
                # be at /tasks/task_*/build/ (where it will be picked up by mozharness later
                # and made into a tc artifact accessible in treeherder as perfherder-data.json)
                results_path = os.path.join(
                    os.path.dirname(os.environ["MOZ_UPLOAD_DIR"]),
                    f"raptor-{data_type}.json",
                )
            else:
                results_path = os.path.join(os.getcwd(), f"raptor-{data_type}.json")

            # dump data to raptor-data.json artifact
            json.dump(next_data_set, open(results_path, "w"), indent=2, sort_keys=True)

            # the output that treeherder expects to find
            LOG.info(f"PERFHERDER_DATA: {json.dumps(next_data_set)}")
            LOG.info(
                f"{data_type} results can also be found locally at: {results_path}"
            )
            total_perfdata += 1

        return True, total_perfdata

    def construct_summary(self, vals, testname, unit=None):
        def _filter(vals, value=None):
            if value is None:
                return [i for i, j in vals]
            return [i for i, j in vals if j == value]

        if testname.startswith("raptor-v8_7"):
            return 100 * filters.geometric_mean(_filter(vals))

        if testname in ("speedometer3", "speedometer-experimental"):
            score = None
            for val, name in vals:
                if name == "score":
                    score = val
            if score is None:
                raise Exception(f"Unable to find score for {testname}")
            return score

        if "speedometer" in testname:
            correctionFactor = 3
            results = _filter(vals)
            # speedometer has 16 tests, each of these are made of up 9 subtests
            # and a sum of the 9 values.  We receive 160 values, and want to use
            # the 16 test values, not the sub test values.
            if len(results) != 160:
                raise Exception(
                    f"Speedometer has 160 subtests, found: {len(results)} instead"
                )

            results = results[9::10]
            # pylint --py3k W1619
            score = 60 * 1000 / filters.geometric_mean(results) / correctionFactor
            return score

        if "stylebench" in testname:
            # see https://bug-172968-attachments.webkit.org/attachment.cgi?id=319888
            correctionFactor = 3
            results = _filter(vals)

            # stylebench has 6 tests. Five of them are made of up 5 subtests
            #
            #   * Adding classes.
            #   * Removing classes.
            #   * Mutating attributes.
            #   * Adding leaf elements.
            #   * Removing leaf elements.
            #
            # which are made of two subtests each (sync/async) and repeated 5 times
            # each, thus, the list here looks like:
            #
            #   [Test name/Adding classes - 0/ Sync; <x>]
            #   [Test name/Adding classes - 0/ Async; <y>]
            #   [Test name/Adding classes - 0; <x> + <y>]
            #   [Test name/Removing classes - 0/ Sync; <x>]
            #   [Test name/Removing classes - 0/ Async; <y>]
            #   [Test name/Removing classes - 0; <x> + <y>]
            #   ...
            #   [Test name/Adding classes - 1 / Sync; <x>]
            #   [Test name/Adding classes - 1 / Async; <y>]
            #   [Test name/Adding classes - 1 ; <x> + <y>]
            #   ...
            #   [Test name/Removing leaf elements - 4; <x> + <y>]
            #   [Test name; <sum>] <- This is what we want.
            #
            # So, 5 (subtests) *
            #     5 (repetitions) *
            #     3 (entries per repetition (sync/async/sum)) =
            #     75 entries for test before the sum.
            #
            # We receive 76 entries per test, which ads up to 380. We want to use
            # the 5 test entries, not the rest.
            #
            # Then there's the sixth "Dynamic media queries" test, which gives
            # results for viewports in increments of 50px like:
            #
            #   Dynamic media queries/Resizing to 300px - 0/Sync
            #   Dynamic media queries/Resizing to 300px - 0/Async
            #   Dynamic media queries/Resizing to 300px - 0
            #   Dynamic media queries/Resizing to 350px - 0/Sync
            #   Dynamic media queries/Resizing to 350px - 0/Async
            #   Dynamic media queries/Resizing to 350px - 0
            #   ...
            #   Dynamic media queries/Resizing to 800px - 0/Sync
            #   Dynamic media queries/Resizing to 800px - 0/Async
            #   Dynamic media queries/Resizing to 800px - 0
            #   Dynamic media queries/Resizing to 350px - 1/Sync
            #   Dynamic media queries/Resizing to 350px - 1/Async
            #   Dynamic media queries/Resizing to 350px - 1
            #   Dynamic media queries/Resizing to 400px - 1/Sync
            #   Dynamic media queries/Resizing to 400px - 1/Async
            #   Dynamic media queries/Resizing to 400px - 1
            #   ...
            #   Dynamic media queries/Resizing to 800px - 4/Sync
            #   Dynamic media queries/Resizing to 800px - 4/Async
            #   Dynamic media queries/Resizing to 800px - 4
            #   Dynamic media queries <- What we want
            #
            # So len([300, 350, 400, 450, 500, 550, 600, 650, 700, 750, 800]) is 11.
            #
            # So, 11 (subtests) *
            #     5 (repetitions) *
            #     3 (entries per repetition (sync/async/sum)) =
            #     165 entries for test before the sum.
            EXPECTED_ENTRIES = 380 + 166
            if len(results) != EXPECTED_ENTRIES:
                raise Exception(
                    f"StyleBench requires {EXPECTED_ENTRIES} entries, found: {len(results)} instead"
                )
            results = results[:380][75::76] + [results[-1]]
            # pylint --py3k W1619
            return 60 * 1000 / filters.geometric_mean(results) / correctionFactor

        if testname.startswith("raptor-kraken") or "sunspider" in testname:
            return sum(_filter(vals))

        if "unity-webgl" in testname or "webaudio" in testname:
            # webaudio_score and unity_webgl_score: self reported as 'Geometric Mean'
            return filters.mean(_filter(vals, "Geometric Mean"))

        if "assorted-dom" in testname:
            # pylint: disable=W1633
            return round(filters.geometric_mean(_filter(vals)), 2)

        if "wasm-misc" in testname:
            # wasm_misc_score: self reported as '__total__'
            return filters.mean(_filter(vals, "__total__"))

        if "wasm-godot" in testname:
            # wasm_godot_score: first-interactive mean
            return filters.mean(_filter(vals, "first-interactive"))

        if "youtube-playback" in testname:
            # pylint: disable=W1633
            return round(filters.mean(_filter(vals)), 2)

        if "twitch-animation" in testname:
            return round(filters.geometric_mean(_filter(vals, "run")), 2)

        if testname.startswith("supporting_data"):
            if not unit:
                return sum(_filter(vals))

            if unit == "%":
                return filters.mean(_filter(vals))

            if unit in ("W", "MHz"):
                # For power in Watts and clock frequencies,
                # summarize with the sum of the averages
                allavgs = []
                for val, subtest in vals:
                    if "avg" in subtest:
                        allavgs.append(val)
                if allavgs:
                    return sum(allavgs)

                raise Exception(
                    "No average measurements found for supporting data with W, or MHz unit ."
                )

            if unit in ["KB", "mAh", "mWh"]:
                return sum(_filter(vals))

            raise NotImplementedError(f"Unit {unit} not suported")

        if len(vals) > 1:
            # pylint: disable=W1633
            return round(filters.geometric_mean(_filter(vals)), 2)

        # pylint: disable=W1633
        return round(filters.mean(_filter(vals)), 2)

    def parseUnknown(self, test):
        # Attempt to flatten whatever we've been given
        # Dictionary keys will be joined by dashes, arrays represent
        # represent "iterations"
        _subtests = {}

        if not isinstance(test["measurements"], dict):
            raise Exception(
                "Expected a dictionary with a single entry as the name of the test. "
                "The value of this key should be the data."
            )
        if test.get("custom_data", False):
            # If custom_data is true it means that the data was already flattened
            # and the test name is included in the keys (the test might have
            # also removed it if it's in the subtest_name_filters option). Handle this
            # exception by wrapping it
            test["measurements"] = {test["name"]: [test["measurements"]]}

        for iteration in test["measurements"][list(test["measurements"].keys())[0]]:
            flattened_metrics = None
            if not test.get("custom_data", False):
                flattened_metrics = flatten(iteration, ())

            for metric, value in (flattened_metrics or iteration).items():
                if metric in METRIC_BLOCKLIST:
                    # TODO: Add an option in the test manifest for this
                    continue
                if metric not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[metric] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": metric,
                        "replicates": [],
                    }
                updated_metric = value
                if not isinstance(value, Iterable):
                    updated_metric = [value]
                # pylint: disable=W1633
                _subtests[metric]["replicates"].extend([
                    round(x, 3) for x in updated_metric
                ])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        summaries = {
            "median": filters.median,
            "mean": filters.mean,
            "geomean": filters.geometric_mean,
        }
        for name in names:
            summary_method = test.get("submetric_summary_method", "median")
            _subtests[name]["value"] = round(
                summaries[summary_method](_subtests[name]["replicates"]), 3
            )
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseSpeedometerOutput(self, test):
        # each benchmark 'index' becomes a subtest; each pagecycle / iteration
        # of the test has multiple values per index/subtest

        # this is the format we receive the results in from the benchmark
        # i.e. this is ONE pagecycle of speedometer:

        # {u'name': u'raptor-speedometer', u'type': u'benchmark', u'measurements':
        # {u'speedometer': [[{u'AngularJS-TodoMVC/DeletingAllItems': [147.3000000000011,
        # 149.95999999999913, 143.29999999999927, 150.34000000000378, 257.6999999999971],
        # u'Inferno-TodoMVC/CompletingAllItems/Sync': [88.03999999999996,#
        # 85.60000000000036, 94.18000000000029, 95.19999999999709, 86.47999999999593],
        # u'AngularJS-TodoMVC': [518.2400000000016, 525.8199999999997, 610.5199999999968,
        # 532.8200000000215, 640.1800000000003], ...(repeated for each index/subtest)}]]},
        # u'browser': u'Firefox 62.0a1 20180528123052', u'lower_is_better': False, u'page':
        # u'http://localhost:55019/Speedometer/index.html?raptor', u'unit': u'score',
        # u'alert_threshold': 2}

        _subtests = {}
        data = test["measurements"]["speedometer"]
        for page_cycle in data:
            for sub, replicates in page_cycle[0].items():
                # for each pagecycle, build a list of subtests and append all related replicates
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "shouldAlert": True,
                        "replicates": [],
                    }
                # pylint: disable=W1633
                _subtests[sub]["replicates"].extend([round(x, 3) for x in replicates])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseAresSixOutput(self, test):
        """
        https://browserbench.org/ARES-6/
        Every pagecycle will perform the tests from the index page
        We have 4 main tests per index page:
        - Air, Basic, Babylon, ML
        - and from these 4 above, ares6 generates the Overall results
        Each test has 3 subtests (firstIteration, steadyState, averageWorstCase):
        - _steadyState
        - _firstIteration
        - _averageWorstCase
        Each index page will run 5 cycles, this is set in glue.js

        {
            'expected_browser_cycles': 1,
            'subtest_unit': 'ms',
            'name': 'raptor-ares6-firefox',
            'lower_is_better': False,
            'browser_cycle': '1',
            'subtest_lower_is_better': True,
            'cold': False,
            'browser': 'Firefox 69.0a1 20190531035909',
            'type': 'benchmark',
            'page': 'http://127.0.0.1:35369/ARES-6/index.html?raptor',
            'unit': 'ms',
            'alert_threshold': 2
            'measurements': {
                'ares6': [[{
                    'Babylon_firstIteration': [
                        123.68,
                        168.21999999999997,
                        127.34000000000003,
                        113.56,
                        128.78,
                        169.44000000000003
                    ],
                    'Air_steadyState': [
                        21.184723618090434,
                        22.906331658291457,
                        19.939396984924624,
                        20.572462311557775,
                        20.790452261306534,
                        18.378693467336696
                    ],
                    etc.
                }]]
            }
        }

        Details on how /ARES6/index.html is showing the mean on subsequent test results:

        I selected just a small part from the metrics just to be easier to explain
        what is going on.

        After the raptor GeckoView test finishes, we have these results in the logs:

        Extracted from "INFO - raptor-control-server Info: received webext_results:"
        'Air_firstIteration': [660.8000000000002, 626.4599999999999, 655.6199999999999,
        635.9000000000001, 636.4000000000001]

        Extracted from "INFO - raptor-output Info: PERFHERDER_DATA:"
        {"name": "Air_firstIteration", "lowerIsBetter": true, "alertThreshold": 2.0,
        "replicates": [660.8, 626.46, 655.62, 635.9, 636.4], "value": 636.4, "unit": "ms"}

        On GeckoView's /ARES6/index.html this is what we see for Air - First Iteration:

        - on 1st test cycle : 660.80 (rounded from 660.8000000000002)

        - on 2nd test cycle : 643.63 , this is coming from
          (660.8000000000002 + 626.4599999999999) / 2 ,
          then rounded up to a precision of 2 decimals

        - on 3rd test cycle : 647.63 this is coming from
          (660.8000000000002 + 626.4599999999999 + 655.6199999999999) / 3 ,
          then rounded up to a precision of 2 decimals

        - and so on
        """

        _subtests = {}
        data = test["measurements"]["ares6"]

        for page_cycle in data:
            for sub, replicates in page_cycle[0].items():
                # for each pagecycle, build a list of subtests and append all related replicates
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                # pylint: disable=W1633
                _subtests[sub]["replicates"].extend([
                    float(round(x, 3)) for x in replicates
                ])

        vals = []
        for name, subtest_data in _subtests.items():
            subtest_data["value"] = filters.mean(subtest_data["replicates"])
            vals.append([subtest_data["value"], name])

        # pylint W1656
        return list(_subtests.values()), sorted(vals, reverse=True)

    def parseMotionmarkOutput(self, test):
        # for motionmark we want the frameLength:average value for each test

        # this is the format we receive the results in from the benchmark
        # i.e. this is ONE pagecycle of motionmark htmlsuite test:composited Transforms:

        # {u'name': u'raptor-motionmark-firefox',
        #  u'type': u'benchmark',
        #  u'measurements': {
        #    u'motionmark':
        #      [[{u'HTMLsuite':
        #        {u'Composited Transforms':
        #          {u'scoreLowerBound': 272.9947975553528,
        #           u'frameLength': {u'average': 25.2, u'stdev': 27.0,
        #                            u'percent': 68.2, u'concern': 39.5},
        #           u'controller': {u'average': 300, u'stdev': 0, u'percent': 0, u'concern': 3},
        #           u'scoreUpperBound': 327.0052024446473,
        #           u'complexity': {u'segment1': [[300, 16.6], [300, 16.6]], u'complexity': 300,
        #                           u'segment2': [[300, None], [300, None]], u'stdev': 6.8},
        #           u'score': 300.00000000000006,
        #           u'complexityAverage': {u'segment1': [[30, 30], [30, 30]], u'complexity': 30,
        #                                  u'segment2': [[300, 300], [300, 300]], u'stdev': None}
        #  }}}]]}}

        _subtests = {}
        data = test["measurements"]["motionmark"]
        for page_cycle in data:
            page_cycle_results = page_cycle[0]

            # TODO: this assumes a single suite is run
            suite = list(page_cycle_results)[0]
            for sub in page_cycle_results[suite].keys():
                try:
                    # pylint: disable=W1633
                    replicate = round(
                        float(
                            page_cycle_results[suite][sub]["complexity"]["bootstrap"][
                                "median"
                            ]
                            if "ramp" in test["name"]
                            else page_cycle_results[suite][sub]["frameLength"][
                                "average"
                            ]
                        ),
                        3,
                    )
                except TypeError as e:
                    LOG.warning(f"[{suite}][{sub}] : {e.__class__.__name__} - {e}")

                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                _subtests[sub]["replicates"].extend([replicate])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseYoutubePlaybackPerformanceOutput(self, test):
        """Parse the metrics for the Youtube playback performance test.

        For each video measured values for dropped and decoded frames will be
        available from the benchmark site.

        {u'PlaybackPerf.VP9.2160p60@2X': {u'droppedFrames': 1, u'decodedFrames': 796}

        With each page cycle / iteration of the test multiple values can be present.

        Raptor will calculate the percentage of dropped frames to decoded frames.
        All those three values will then be emitted as separate sub tests.
        """
        _subtests = {}
        test_name = [
            measurement
            for measurement in test["measurements"].keys()
            if "youtube-playback" in measurement
        ]
        if len(test_name) > 0:
            data = test["measurements"].get(test_name[0])
        else:
            raise Exception("No measurements found for youtube test!")

        def create_subtest_entry(
            name,
            value,
            unit=test["subtest_unit"],
            lower_is_better=test["subtest_lower_is_better"],
        ):
            # build a list of subtests and append all related replicates
            if name not in _subtests:
                # subtest not added yet, first pagecycle, so add new one
                _subtests[name] = {
                    "name": name,
                    "unit": unit,
                    "lowerIsBetter": lower_is_better,
                    "replicates": [],
                }

            _subtests[name]["replicates"].append(value)
            if self.subtest_alert_on is not None:
                if name in self.subtest_alert_on:
                    LOG.info(
                        f"turning on subtest alerting for measurement type: {name}"
                    )
                    _subtests[name]["shouldAlert"] = True

        failed_tests = []
        for pagecycle in data:
            for _sub, _value in pagecycle[0].items():
                if _value["decodedFrames"] == 0:
                    failed_tests.append(
                        f"{_sub} test Failed. "
                        f"decodedFrames {_value['decodedFrames']} "
                        f"droppedFrames {_value['droppedFrames']}."
                    )

                try:
                    percent_dropped = (
                        float(_value["droppedFrames"]) / _value["decodedFrames"] * 100.0
                    )
                except ZeroDivisionError:
                    # if no frames have been decoded the playback failed completely
                    percent_dropped = 100.0

                # Remove the not needed "PlaybackPerf." prefix from each test
                _sub = _sub.split("PlaybackPerf", 1)[-1]
                if _sub.startswith("."):
                    _sub = _sub[1:]

                # build a list of subtests and append all related replicates
                create_subtest_entry(
                    f"{_sub}_decoded_frames",
                    _value["decodedFrames"],
                    lower_is_better=False,
                )
                create_subtest_entry(f"{_sub}_dropped_frames", _value["droppedFrames"])
                create_subtest_entry(f"{_sub}_%_dropped_frames", percent_dropped)

        # Check if any youtube test failed and generate exception
        if len(failed_tests) > 0:
            [LOG.warning(f"Youtube sub-test FAILED: {test}") for test in failed_tests]
            # TODO: Change this to raise Exception after we figure out the failing tests
            LOG.warning(
                "Youtube playback sub-tests failed!!! "
                "Not submitting results to perfherder!"
            )
        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            # pylint: disable=W1633
            _subtests[name]["value"] = round(
                float(filters.median(_subtests[name]["replicates"])), 2
            )
            subtests.append(_subtests[name])
            # only include dropped_frames values, without the %_dropped_frames values
            if name.endswith("X_dropped_frames"):
                vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseUnityWebGLOutput(self, test):
        """
        Example output (this is one page cycle):

        {'name': 'raptor-unity-webgl-firefox',
         'type': 'benchmark',
         'measurements': {
            'unity-webgl': [
                [
                    '[{"benchmark":"Mandelbrot GPU","result":1035361},...}]'
                ]
            ]
         },
         'lower_is_better': False,
         'unit': 'score'
        }
        """
        _subtests = {}
        data = test["measurements"]["unity-webgl"]
        for page_cycle in data:
            data = json.loads(page_cycle[0])
            for item in data:
                # for each pagecycle, build a list of subtests and append all related replicates
                sub = item["benchmark"]
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                _subtests[sub]["replicates"].append(item["result"])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseWebaudioOutput(self, test):
        # each benchmark 'index' becomes a subtest; each pagecycle / iteration
        # of the test has multiple values per index/subtest

        # this is the format we receive the results in from the benchmark
        # i.e. this is ONE pagecycle of speedometer:

        # {u'name': u'raptor-webaudio-firefox', u'type': u'benchmark', u'measurements':
        # {u'webaudio': [[u'[{"name":"Empty testcase","duration":26,"buffer":{}},{"name"
        # :"Simple gain test without resampling","duration":66,"buffer":{}},{"name":"Simple
        # gain test without resampling (Stereo)","duration":71,"buffer":{}},{"name":"Simple
        # gain test without resampling (Stereo and positional)","duration":67,"buffer":{}},
        # {"name":"Simple gain test","duration":41,"buffer":{}},{"name":"Simple gain test
        # (Stereo)","duration":59,"buffer":{}},{"name":"Simple gain test (Stereo and positional)",
        # "duration":68,"buffer":{}},{"name":"Upmix without resampling (Mono -> Stereo)",
        # "duration":53,"buffer":{}},{"name":"Downmix without resampling (Mono -> Stereo)",
        # "duration":44,"buffer":{}},{"name":"Simple mixing (same buffer)",
        # "duration":288,"buffer":{}}

        _subtests = {}
        data = test["measurements"]["webaudio"]
        for page_cycle in data:
            data = json.loads(page_cycle[0])
            for item in data:
                # for each pagecycle, build a list of subtests and append all related replicates
                sub = item["name"]
                replicates = [item["duration"]]
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                # pylint: disable=W1633
                _subtests[sub]["replicates"].extend([
                    float(round(x, 3)) for x in replicates
                ])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        print(subtests)
        return subtests, vals

    def parseWASMGodotOutput(self, test):
        """
        {u'wasm-godot': [
            {
              "name": "wasm-instantiate",
              "time": 349
            },{
              "name": "engine-instantiate",
              "time": 1263
            ...
            }]}
        """
        _subtests = {}
        data = test["measurements"]["wasm-godot"]
        print(data)
        for page_cycle in data:
            for item in page_cycle[0]:
                # for each pagecycle, build a list of subtests and append all related replicates
                sub = item["name"]
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                _subtests[sub]["replicates"].append(item["time"])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseSunspiderOutput(self, test):
        _subtests = {}
        data = test["measurements"]["sunspider"]
        for page_cycle in data:
            for sub, replicates in page_cycle[0].items():
                # for each pagecycle, build a list of subtests and append all related replicates
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                # pylint: disable=W1633
                _subtests[sub]["replicates"].extend([
                    float(round(x, 3)) for x in replicates
                ])

        subtests = []
        vals = []

        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.mean(_subtests[name]["replicates"])
            subtests.append(_subtests[name])

            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseAssortedDomOutput(self, test):
        # each benchmark 'index' becomes a subtest; each pagecycle / iteration
        # of the test has multiple values

        # this is the format we receive the results in from the benchmark
        # i.e. this is ONE pagecycle of assorted-dom ('test' is a valid subtest name btw):

        # {u'worker-getname-performance-getter': 5.9, u'window-getname-performance-getter': 6.1,
        # u'window-getprop-performance-getter': 6.1, u'worker-getprop-performance-getter': 6.1,
        # u'test': 5.8, u'total': 30}

        # the 'total' is provided for us from the benchmark; the overall score will be the mean of
        # the totals from all pagecycles; but keep all the subtest values for the logs/json

        _subtests = {}
        data = test["measurements"]["assorted-dom"]
        for pagecycle in data:
            for _sub, _value in pagecycle[0].items():
                # build a list of subtests and append all related replicates
                if _sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[_sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": _sub,
                        "replicates": [],
                    }
                _subtests[_sub]["replicates"].extend([_value])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            # pylint: disable=W1633
            _subtests[name]["value"] = float(
                round(filters.median(_subtests[name]["replicates"]), 2)
            )
            subtests.append(_subtests[name])
            # only use the 'total's to compute the overall result
            if name == "total":
                vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseJetstreamTwoOutput(self, test):
        # https://browserbench.org/JetStream/

        _subtests = {}
        data = test["measurements"]["jetstream2"]
        for page_cycle in data:
            for sub, replicates in page_cycle[0].items():
                # for each pagecycle, build a list of subtests and append all related replicates
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                # pylint: disable=W1633
                _subtests[sub]["replicates"].extend([
                    float(round(x, 3)) for x in replicates
                ])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.mean(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseWASMMiscOutput(self, test):
        """
        {u'wasm-misc': [
          [[{u'name': u'validate', u'time': 163.44000000000005},
            ...
            {u'name': u'__total__', u'time': 63308.434904788155}]],
          ...
          [[{u'name': u'validate', u'time': 129.42000000000002},
            {u'name': u'__total__', u'time': 63181.24089257814}]]
         ]}
        """
        _subtests = {}
        data = test["measurements"]["wasm-misc"]
        for page_cycle in data:
            for item in page_cycle[0]:
                # for each pagecycle, build a list of subtests and append all related replicates
                sub = item["name"]
                if sub not in _subtests:
                    # subtest not added yet, first pagecycle, so add new one
                    _subtests[sub] = {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    }
                _subtests[sub]["replicates"].append(item["time"])

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.median(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseMatrixReactBenchOutput(self, test):
        # https://github.com/jandem/matrix-react-bench

        _subtests = {}
        data = test["measurements"]["matrix-react-bench"]
        for page_cycle in data:
            # Each cycle is formatted like `[[iterations, val], [iterations, val2], ...]`
            for iteration, val in page_cycle:
                sub = f"{iteration}-iterations"
                _subtests.setdefault(
                    sub,
                    {
                        "unit": test["subtest_unit"],
                        "alertThreshold": float(test["alert_threshold"]),
                        "lowerIsBetter": test["subtest_lower_is_better"],
                        "name": sub,
                        "replicates": [],
                    },
                )

                # The values produced are far too large for perfherder
                _subtests[sub]["replicates"].append(val)

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.mean(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals

    def parseTwitchAnimationOutput(self, test):
        _subtests = {}

        for metric_name, data in test["measurements"].items():
            if "perfstat-" not in metric_name and metric_name != "twitch-animation":
                # Only keep perfstats or the run metric
                continue
            if metric_name == "twitch-animation":
                metric = "run"
            else:
                metric = metric_name

            # data is just an array with a single number
            for polymorphic_page_cycle in data:
                # Each benchmark cycle is formatted like `[val]`, perfstats
                # are not
                if not isinstance(polymorphic_page_cycle, list):
                    page_cycle = [polymorphic_page_cycle]
                else:
                    page_cycle = polymorphic_page_cycle
                for val in page_cycle:
                    _subtests.setdefault(
                        metric,
                        {
                            "unit": test["subtest_unit"],
                            "alertThreshold": float(test["alert_threshold"]),
                            "lowerIsBetter": test["subtest_lower_is_better"],
                            "name": metric,
                            "replicates": [],
                        },
                    )

                    # The values produced are far too large for perfherder
                    _subtests[metric]["replicates"].append(val)

        vals = []
        subtests = []
        names = list(_subtests)
        names.sort(reverse=True)
        for name in names:
            _subtests[name]["value"] = filters.mean(_subtests[name]["replicates"])
            subtests.append(_subtests[name])
            vals.append([_subtests[name]["value"], name])

        return subtests, vals


class RaptorOutput(PerftestOutput):
    """class for raptor output"""

    def summarize(self, test_names):
        suites = []
        test_results = {"framework": {"name": "raptor"}, "suites": suites}

        # check if we actually have any results
        if len(self.results) == 0:
            LOG.error(f"no raptor test results found for {', '.join(test_names)}")
            return

        for test in self.results:
            vals = []
            subtests = []
            suite = {
                "name": test["name"],
                "type": test["type"],
                "tags": test.get("tags", []),
                "extraOptions": test["extra_options"],
                "subtests": subtests,
                "lowerIsBetter": test["lower_is_better"],
                "unit": test["unit"],
                "alertThreshold": float(test["alert_threshold"]),
            }

            # Check if optional properties have been set by the test
            if hasattr(test, "alert_change_type"):
                suite["alertChangeType"] = test["alert_change_type"]

            # if cold load add that info to the suite result dict; this will be used later
            # when combining the results from multiple browser cycles into one overall result
            if test["cold"] is True:
                suite["cold"] = True
                suite["browser_cycle"] = int(test["browser_cycle"])
                suite["expected_browser_cycles"] = int(test["expected_browser_cycles"])
                suite["tags"].append("cold")
            else:
                suite["tags"].append("warm")

            suites.append(suite)

            # process results for pageloader type of tests
            if test["type"] in ("pageload", "scenario"):
                # each test can report multiple measurements per pageload
                # each measurement becomes a subtest inside the 'suite'

                # this is the format we receive the results in from the pageload test
                # i.e. one test (subtest) in raptor-firefox-tp6:

                # {u'name': u'raptor-firefox-tp6-amazon', u'type': u'pageload', u'measurements':
                # {u'fnbpaint': [788, 315, 334, 286, 318, 276, 296, 296, 292, 285, 268, 277, 274,
                # 328, 295, 290, 286, 270, 279, 280, 346, 303, 308, 398, 281]}, u'browser':
                # u'Firefox 62.0a1 20180528123052', u'lower_is_better': True, u'page':
                # u'https://www.amazon.com/s/url=search-alias%3Daps&field-keywords=laptop',
                # u'unit': u'ms', u'alert_threshold': 2}

                for measurement_name, replicates in test["measurements"].items():
                    new_subtest = {}
                    new_subtest["name"] = measurement_name
                    new_subtest["replicates"] = replicates
                    new_subtest["lowerIsBetter"] = test["subtest_lower_is_better"]
                    new_subtest["alertThreshold"] = float(test["alert_threshold"])
                    new_subtest["value"] = 0
                    new_subtest["unit"] = test["subtest_unit"]

                    if test["cold"] is False:
                        # for warm page-load, ignore first value due to 1st pageload noise
                        LOG.info(
                            f"ignoring the first {measurement_name} value due to "
                            "initial pageload noise"
                        )
                        filtered_values = filters.ignore_first(
                            new_subtest["replicates"], 1
                        )
                    else:
                        # for cold-load we want all the values
                        filtered_values = new_subtest["replicates"]

                    # for pageload tests that measure TTFI: TTFI is not guaranteed to be available
                    # everytime; the raptor measure.js webext will substitute a '-1' value in the
                    # cases where TTFI is not available, which is acceptable; however we don't want
                    # to include those '-1' TTFI values in our final results calculations
                    if measurement_name == "ttfi":
                        filtered_values = filters.ignore_negative(filtered_values)
                        # we've already removed the first pageload value; if there aren't any more
                        # valid TTFI values available for this pageload just remove it from results
                        if len(filtered_values) < 1:
                            continue

                    # if 'alert_on' is set for this particular measurement, then we want to set the
                    # flag in the perfherder output to turn on alerting for this subtest
                    if self.subtest_alert_on is not None:
                        if measurement_name in self.subtest_alert_on:
                            LOG.info(
                                "turning on subtest alerting for measurement type: "
                                f"{measurement_name}"
                            )
                            new_subtest["shouldAlert"] = True
                        else:
                            # Explicitly set `shouldAlert` to False so that the measurement
                            # is not alerted on. Otherwise Perfherder defaults to alerting
                            LOG.info(
                                "turning off subtest alerting for measurement type: "
                                f"{measurement_name}"
                            )
                            new_subtest["shouldAlert"] = False

                    new_subtest["value"] = filters.median(filtered_values)

                    vals.append([new_subtest["value"], new_subtest["name"]])
                    subtests.append(new_subtest)

            elif test["type"] == "benchmark":
                if any([
                    "youtube-playback" in measurement
                    for measurement in test["measurements"].keys()
                ]):
                    subtests, vals = self.parseYoutubePlaybackPerformanceOutput(test)
                elif "assorted-dom" in test["measurements"]:
                    subtests, vals = self.parseAssortedDomOutput(test)
                elif "ares6" in test["measurements"]:
                    subtests, vals = self.parseAresSixOutput(test)
                elif "jetstream2" in test["measurements"]:
                    subtests, vals = self.parseJetstreamTwoOutput(test)
                elif "motionmark" in test["measurements"]:
                    subtests, vals = self.parseMotionmarkOutput(test)
                elif "speedometer" in test["measurements"]:
                    # this includes stylebench
                    subtests, vals = self.parseSpeedometerOutput(test)
                elif "sunspider" in test["measurements"]:
                    subtests, vals = self.parseSunspiderOutput(test)
                elif "unity-webgl" in test["measurements"]:
                    subtests, vals = self.parseUnityWebGLOutput(test)
                elif "wasm-godot" in test["measurements"]:
                    subtests, vals = self.parseWASMGodotOutput(test)
                elif "wasm-misc" in test["measurements"]:
                    subtests, vals = self.parseWASMMiscOutput(test)
                elif "webaudio" in test["measurements"]:
                    subtests, vals = self.parseWebaudioOutput(test)
                else:
                    subtests, vals in self.parseUnknown(test)

                suite["subtests"] = subtests

            else:
                LOG.error(
                    "output.summarize received unsupported test results type for "
                    f"{test['name']}"
                )
                return

            suite["tags"].append(test["type"])

            # for benchmarks there is generally more than one subtest in each cycle
            # and a benchmark-specific formula is needed to calculate the final score
            # we no longer summarise the page load as we alert on individual subtests
            # and the geometric mean was found to be of little value
            if len(subtests) > 1 and test["type"] != "pageload":
                suite["value"] = self.construct_summary(vals, testname=test["name"])

            subtests.sort(key=lambda subtest: subtest["name"])
            suite["tags"].sort()

        suites.sort(key=lambda suite: suite["name"])

        self.summarized_results = test_results

    def combine_browser_cycles(self):
        """
        At this point the results have been summarized; however there may have been multiple
        browser cycles (i.e. cold load). In which case the results have one entry for each
        test for each browser cycle. For each test we need to combine the results for all
        browser cycles into one results entry.

        For example, this is what the summarized results suites list looks like from a test that
        was run with multiple (two) browser cycles:

        [{'expected_browser_cycles': 2, 'extraOptions': [],
            'name': u'raptor-tp6m-amazon-geckoview-cold', 'lowerIsBetter': True,
            'alertThreshold': 2.0, 'value': 1776.94, 'browser_cycle': 1,
            'subtests': [{'name': u'dcf', 'lowerIsBetter': True, 'alertThreshold': 2.0,
                'value': 818, 'replicates': [818], 'unit': u'ms'}, {'name': u'fcp',
                'lowerIsBetter': True, 'alertThreshold': 2.0, 'value': 1131, 'shouldAlert': True,
                'replicates': [1131], 'unit': u'ms'}, {'name': u'fnbpaint', 'lowerIsBetter': True,
                'alertThreshold': 2.0, 'value': 1056, 'replicates': [1056], 'unit': u'ms'},
                {'name': u'ttfi', 'lowerIsBetter': True, 'alertThreshold': 2.0, 'value': 18074,
                'replicates': [18074], 'unit': u'ms'}, {'name': u'loadtime', 'lowerIsBetter': True,
                'alertThreshold': 2.0, 'value': 1002, 'shouldAlert': True, 'replicates': [1002],
                'unit': u'ms'}],
            'cold': True, 'type': u'pageload', 'unit': u'ms'},
        {'expected_browser_cycles': 2, 'extraOptions': [],
            'name': u'raptor-tp6m-amazon-geckoview-cold', 'lowerIsBetter': True,
            'alertThreshold': 2.0, 'value': 840.25, 'browser_cycle': 2,
            'subtests': [{'name': u'dcf', 'lowerIsBetter': True, 'alertThreshold': 2.0,
                'value': 462, 'replicates': [462], 'unit': u'ms'}, {'name': u'fcp',
                'lowerIsBetter': True, 'alertThreshold': 2.0, 'value': 718, 'shouldAlert': True,
                'replicates': [718], 'unit': u'ms'}, {'name': u'fnbpaint', 'lowerIsBetter': True,
                'alertThreshold': 2.0, 'value': 676, 'replicates': [676], 'unit': u'ms'},
                {'name': u'ttfi', 'lowerIsBetter': True, 'alertThreshold': 2.0, 'value': 3084,
                'replicates': [3084], 'unit': u'ms'}, {'name': u'loadtime', 'lowerIsBetter': True,
                'alertThreshold': 2.0, 'value': 605, 'shouldAlert': True, 'replicates': [605],
                'unit': u'ms'}],
            'cold': True, 'type': u'pageload', 'unit': u'ms'}]

        Need to combine those into a single entry.
        """
        # check if we actually have any results
        if len(self.results) == 0:
            LOG.info(
                "error: no raptor test results found, so no need to combine browser cycles"
            )
            return

        # first build a list of entries that need to be combined; and as we do that, mark the
        # original suite entry as up for deletion, so once combined we know which ones to del
        # note that summarized results are for all tests that were ran in the session, which
        # could include cold and / or warm page-load and / or benchnarks combined
        suites_to_be_combined = []
        combined_suites = []

        for _index, suite in enumerate(self.summarized_results.get("suites", [])):
            if suite.get("cold") is None:
                continue

            if suite["expected_browser_cycles"] > 1:
                _name = suite["name"]
                _details = suite.copy()
                suites_to_be_combined.append({"name": _name, "details": _details})
                suite["to_be_deleted"] = True

        # now create a new suite entry that will have all the results from
        # all of the browser cycles, but in one result entry for each test
        combined_suites = {}

        for next_suite in suites_to_be_combined:
            suite_name = next_suite["details"]["name"]
            browser_cycle = next_suite["details"]["browser_cycle"]
            LOG.info(
                f"combining results from browser cycle {browser_cycle} for {suite_name}"
            )
            if suite_name not in combined_suites:
                # first browser cycle so just take entire entry to start with
                combined_suites[suite_name] = next_suite["details"]
                LOG.info("created new combined result with intial cycle replicates")
                # remove the 'cold', 'browser_cycle', and 'expected_browser_cycles' info
                # as we don't want that showing up in perfherder data output
                del combined_suites[suite_name]["cold"]
                del combined_suites[suite_name]["browser_cycle"]
                del combined_suites[suite_name]["expected_browser_cycles"]
            else:
                # subsequent browser cycles, already have an entry; just add subtest replicates
                for next_subtest in next_suite["details"]["subtests"]:
                    # find the existing entry for that subtest in our new combined test entry
                    found_subtest = False
                    for combined_subtest in combined_suites[suite_name]["subtests"]:
                        if combined_subtest["name"] == next_subtest["name"]:
                            # add subtest (measurement type) replicates to the combined entry
                            LOG.info(f"adding replicates for {next_subtest['name']}")
                            combined_subtest["replicates"].extend(
                                next_subtest["replicates"]
                            )
                            found_subtest = True
                    # the subtest / measurement type wasn't found in our existing combined
                    # result entry; if it is for the same suite name add it - this could happen
                    # as ttfi may not be available in every browser cycle
                    if not found_subtest:
                        LOG.info(f"adding replicates for {next_subtest['name']}")
                        combined_suites[next_suite["details"]["name"]][
                            "subtests"
                        ].append(next_subtest)

        # now we have a single entry for each test; with all replicates from all browser cycles
        for i, name in enumerate(combined_suites):
            vals = []
            for next_sub in combined_suites[name]["subtests"]:
                # calculate sub-test results (i.e. each measurement type)
                next_sub["value"] = filters.median(next_sub["replicates"])
                # add to vals; vals is used to calculate overall suite result i.e. the
                # geomean of all of the subtests / measurement types
                vals.append([next_sub["value"], next_sub["name"]])

            # calculate overall suite result ('value') which is geomean of all measures
            if len(combined_suites[name]["subtests"]) > 1:
                combined_suites[name]["value"] = self.construct_summary(
                    vals, testname=name
                )

            # now add the combined suite entry to our overall summarized results!
            self.summarized_results["suites"].append(combined_suites[name])

        # now it is safe to delete the original entries that were made by each cycle
        self.summarized_results["suites"] = [
            item
            for item in self.summarized_results["suites"]
            if item.get("to_be_deleted") is not True
        ]

    def summarize_screenshots(self, screenshots):
        if len(screenshots) == 0:
            return

        self.summarized_screenshots.append(
            """<!DOCTYPE html>
        <head>
        <style>
            table, th, td {
              border: 1px solid black;
              border-collapse: collapse;
            }
        </style>
        </head>
        <html> <body>
        <h1>Captured screenshots!</h1>
        <table style="width:100%">
          <tr>
            <th>Test Name</th>
            <th>Pagecycle</th>
            <th>Screenshot</th>
          </tr>"""
        )

        for screenshot in screenshots:
            self.summarized_screenshots.append(
                f"""<tr>
            <th>{screenshot["test_name"]}</th>
            <th> {screenshot["page_cycle"]}</th>
            <th>
                <img src="{screenshot["screenshot"]}" alt="{screenshot["test_name"]} {screenshot["page_cycle"]}" width="320" height="240">
            </th>
            </tr>"""
            )

        self.summarized_screenshots.append("""</table></body> </html>""")


class BrowsertimeOutput(PerftestOutput):
    """class for browsertime output"""

    def summarize(self, test_names):
        """
        Summarize the parsed browsertime test output, and format accordingly so the output can
        be ingested by Perfherder.

        At this point each entry in self.results for browsertime-pageload tests is in this format:

        {'statistics':{'fcp': {u'p99': 932, u'mdev': 10.0941, u'min': 712, u'p90': 810, u'max':
        932, u'median': 758, u'p10': 728, u'stddev': 50, u'mean': 769}, 'dcf': {u'p99': 864,
        u'mdev': 11.6768, u'min': 614, u'p90': 738, u'max': 864, u'median': 670, u'p10': 632,
        u'stddev': 58, u'mean': 684}, 'fnbpaint': {u'p99': 830, u'mdev': 9.6851, u'min': 616,
        u'p90': 719, u'max': 830, u'median': 668, u'p10': 642, u'stddev': 48, u'mean': 680},
        'loadtime': {u'p99': 5818, u'mdev': 111.7028, u'min': 3220, u'p90': 4450, u'max': 5818,
        u'median': 3476, u'p10': 3241, u'stddev': 559, u'mean': 3642}}, 'name':
        'raptor-tp6-guardian-firefox', 'url': 'https://www.theguardian.co.uk', 'lower_is_better':
        True, 'measurements': {'fcp': [932, 744, 744, 810, 712, 775, 759, 744, 777, 739, 809, 906,
        734, 742, 760, 758, 728, 792, 757, 759, 742, 759, 775, 726, 730], 'dcf': [864, 679, 637,
        662, 652, 651, 710, 679, 646, 689, 686, 845, 670, 694, 632, 703, 670, 738, 633, 703, 614,
        703, 650, 622, 670], 'fnbpaint': [830, 648, 666, 704, 616, 683, 678, 650, 685, 651, 719,
        820, 634, 664, 681, 664, 642, 703, 668, 670, 669, 668, 681, 652, 642], 'loadtime': [4450,
        3592, 3770, 3345, 3453, 3220, 3434, 3621, 3511, 3416, 3430, 5818, 4729, 3406, 3506, 3588,
        3245, 3381, 3707, 3241, 3595, 3483, 3236, 3390, 3476]}, 'subtest_unit': 'ms', 'bt_ver':
        '4.9.2-android', 'alert_threshold': 2, 'cold': True, 'type': 'browsertime-pageload',
        'unit': 'ms', 'browser': "{u'userAgent': u'Mozilla/5.0 (Macintosh; Intel Mac OS X 10.13;
        rv:70.0) Gecko/20100101 Firefox/70.0', u'windowSize': u'1366x694'}"}

        Now we must process this further and prepare the result for output suitable for perfherder
        ingestion.

        Note: For the overall subtest values/results (i.e. for each measurement type) we will use
        the Browsertime-provided statistics, instead of calcuating our own geomeans from the
        replicates.
        """

        def _filter_data(data, method, subtest_name):
            import numpy as np
            from scipy.cluster.vq import kmeans2, whiten

            """
            Take the kmeans of the data, and attempt to filter this way.
            We'll use hard-coded values to get rid of data that is 2x
            smaller/larger than the majority of the data. If the data approaches
            a 35%/65% split, then it won't be filtered as we can't figure
            out which one is the right mean to take.

            The way that this will work for multi-modal data (more than 2 modes)
            is that the majority of the modes will end up in either one bin or
            the other. Taking the group with the most points lets us
            consistently remove very large outliers out of the data and target the
            modes with the largest prominence.

            TODO: The seed exists because using a randomized one often gives us
            multiple results on the same dataset. This should keep things more
            consistent from one task to the next. We should also look into playing
            with iterations, but that comes at the cost of processing time (this
            might not be a valid concern).
            """
            data = np.asarray(data)

            # Disable kmeans2 empty cluster warnings
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                kmeans, result = kmeans2(
                    whiten(np.asarray([float(d) for d in data])), 2, seed=1000
                )

            if len(kmeans) < 2:
                # Default to a gaussian filter if we didn't get 2 means
                summary_method = np.mean
                if method == "geomean":
                    filters.geometric_mean

                # Apply a gaussian filter
                data = data[
                    np.where(data > summary_method(data) - (np.std(data) * 2))[0]
                ]
                data = list(
                    data[np.where(data < summary_method(data) + (np.std(data) * 2))[0]]
                )
            else:
                first_group = data[np.where(result == 0)]
                secnd_group = data[np.where(result == 1)]

                total_len = len(data)
                first_len = len(first_group)
                secnd_len = len(secnd_group)

                ratio = np.ceil((min(first_len, secnd_len) / total_len) * 100)
                if ratio <= 35:
                    # If one of the groups are less than 35% of the total
                    # size, then filter it out if the difference in the
                    # k-means are large enough (200% difference).
                    max_mean = max(kmeans)
                    min_mean = min(kmeans)
                    if abs(max_mean / min_mean) > 2:
                        major_group = first_group
                        major_mean = np.mean(first_group) if first_len > 0 else 0
                        minor_mean = np.mean(secnd_group) if secnd_len > 0 else 0
                        if first_len < secnd_len:
                            major_group = secnd_group
                            tmp = major_mean
                            major_mean = minor_mean
                            minor_mean = tmp

                        LOG.info(
                            f"{subtest_name}: Filtering out {total_len - len(major_group)} "
                            f"data points found in minor_group of data with "
                            f"mean {minor_mean} vs. {major_mean} in major group"
                        )
                        data = major_group

            return data

        def _process_geomean(subtest):
            data = subtest["replicates"]
            subtest["value"] = round(filters.geometric_mean(data), 1)

        def _process_alt_method(subtest, alternative_method):
            data = subtest["replicates"]
            if alternative_method == "median":
                subtest["value"] = filters.median(data)

        # converting suites and subtests into lists, and sorting them
        def _process(subtest, method="geomean"):
            if test["type"] == "power":
                subtest["value"] = filters.mean(subtest["replicates"])
            elif method == "geomean":
                _process_geomean(subtest)
            else:
                _process_alt_method(subtest, method)
            return subtest

        def _process_suite(suite):
            suite["subtests"] = [
                _process(subtest)
                for subtest in suite["subtests"].values()
                if subtest["replicates"]
            ]

            # Duplicate for different summary values if needed
            if self.extra_summary_methods:
                new_subtests = []
                for subtest in suite["subtests"]:
                    try:
                        for alternative_method in self.extra_summary_methods:
                            new_subtest = copy.deepcopy(subtest)
                            new_subtest["name"] = (
                                f"{new_subtest['name']} ({alternative_method})"
                            )
                            _process(new_subtest, alternative_method)
                            new_subtests.append(new_subtest)
                    except Exception as e:
                        # Ignore failures here
                        LOG.info(f"Failed to summarize with alternative methods: {e}")
                        pass
                suite["subtests"].extend(new_subtests)

            suite["subtests"].sort(key=lambda subtest: subtest["name"])

            # for benchmarks there is generally more than one subtest in each cycle
            # and a benchmark-specific formula is needed to calculate the final score
            # we no longer summarise the page load as we alert on individual subtests
            # and the geometric mean was found to be of little value
            if len(suite["subtests"]) > 1 and suite["type"] != "pageload":
                vals = [
                    [subtest["value"], subtest["name"]] for subtest in suite["subtests"]
                ]
                testname = suite["name"]
                if suite["type"] == "power":
                    testname = "supporting_data"
                suite["value"] = self.construct_summary(vals, testname=testname)
            return suite

        LOG.info("preparing browsertime results for output")

        # check if we actually have any results
        if len(self.results) == 0:
            LOG.error(f"no browsertime test results found for {', '.join(test_names)}")
            return

        test_results = {"framework": {"name": "browsertime"}}

        # using a mapping so we can have a unique set of results given a name
        suites = {}

        for test in self.results:
            test_name = test["name"]
            extra_options = test["extra_options"]

            # If a test with the same name has different extra options, handle it
            # by appending the difference in options to the key used in `suites`. We
            # need to do a loop here in case we get a conflicting test name again.
            prev_name = test_name
            while (
                test_name in suites
                and suites[test_name]["extraOptions"] != extra_options
            ):
                missing = set(extra_options) - set(suites[test_name]["extraOptions"])
                if len(missing) == 0:
                    missing = set(suites[test_name]["extraOptions"]) - set(
                        extra_options
                    )
                test_name = test_name + "-".join(list(missing))

                if prev_name == test_name:
                    # Kill the loop if we get the same name again
                    break
                else:
                    prev_name = test_name

            suite = suites.setdefault(
                test_name,
                {
                    "name": test["name"],
                    "type": test["type"],
                    "extraOptions": extra_options,
                    # There may be unique options in tags now, but we don't want to remove the
                    # previous behaviour which includes the extra options in the tags.
                    "tags": test.get("tags", []) + extra_options,
                    "lowerIsBetter": test["lower_is_better"],
                    "unit": test["unit"],
                    "alertThreshold": float(test["alert_threshold"]),
                    # like suites, subtests are identified by names
                    "subtests": {},
                },
            )

            # Add the alert window settings if needed
            for alert_option, schema_name in (
                ("min_back_window", "minBackWindow"),
                ("max_back_window", "maxBackWindow"),
                ("fore_window", "foreWindow"),
            ):
                if test.get(alert_option, None) is not None:
                    suite[schema_name] = int(test[alert_option])

            # Setting shouldAlert to False whenever self.app is any of the chrom* applications
            if self.app in (
                "chrome",
                "chrome-m",
                "custom-car",
                "cstm-car-m",
            ):
                suite["shouldAlert"] = False
            # Check if the test has set optional properties
            if (
                test.get("alert_change_type", None) is not None
                and "alertChangeType" not in suite
            ):
                suite["alertChangeType"] = test["alert_change_type"]

            def _process_measurements(measurement_name, replicates):
                subtest = {}
                subtest["name"] = measurement_name
                subtest["lowerIsBetter"] = test["subtest_lower_is_better"]
                subtest["alertThreshold"] = float(test["alert_threshold"])
                if measurement_name == "cpuTime":
                    subtest["unit"] = "ms"
                elif measurement_name == "powerUsage":
                    subtest["unit"] = "uWh"
                else:
                    subtest["unit"] = test["subtest_unit"]

                # Add the alert window settings if needed here too in case
                # there is no summary value in the test
                for schema_name in (
                    "minBackWindow",
                    "maxBackWindow",
                    "foreWindow",
                ):
                    if suite.get(schema_name, None) is not None:
                        subtest[schema_name] = suite[schema_name]

                # if 'alert_on' is set for this particular measurement, then we want to set
                # the flag in the perfherder output to turn on alerting for this subtest
                if self.subtest_alert_on is not None:
                    if measurement_name in self.subtest_alert_on:
                        LOG.info(
                            "turning on subtest alerting for measurement type: "
                            f"{measurement_name}"
                        )
                        subtest["shouldAlert"] = True
                        if self.app in (
                            "chrome",
                            "chrome-m",
                            "custom-car",
                            "cstm-car-m",
                        ):
                            subtest["shouldAlert"] = False
                    else:
                        # Explicitly set `shouldAlert` to False so that the measurement
                        # is not alerted on. Otherwise Perfherder defaults to alerting.
                        LOG.info(
                            "turning off subtest alerting for measurement type: "
                            f"{measurement_name}"
                        )
                        subtest["shouldAlert"] = False
                subtest["replicates"] = replicates
                return subtest

            if test.get("support_class"):
                test.get("support_class").summarize_test(test, suite)

            elif test["type"] in ["pageload", "scenario", "power"]:
                LOG.warning(
                    "This test is using a soon-to-be deprecated method for summarizing "
                    "output. A subclass of browsertime_pageload.py should be built for "
                    "this instead. Output handling is already built there, and the results "
                    "parsing will need to be added for this specific test."
                )
                for measurement_name, replicates in test["measurements"].items():
                    new_subtest = _process_measurements(measurement_name, replicates)
                    if measurement_name not in suite["subtests"]:
                        suite["subtests"][measurement_name] = new_subtest
                    else:
                        suite["subtests"][measurement_name]["replicates"].extend(
                            new_subtest["replicates"]
                        )

            elif "benchmark" in test["type"]:
                subtests = None

                if "speedometer" in test["measurements"]:
                    # this includes stylebench
                    subtests, vals = self.parseSpeedometerOutput(test)
                elif "ares6" in test["name"]:
                    subtests, vals = self.parseAresSixOutput(test)
                elif "motionmark" in test["measurements"]:
                    subtests, vals = self.parseMotionmarkOutput(test)
                elif "youtube-playback" in test["name"]:
                    subtests, vals = self.parseYoutubePlaybackPerformanceOutput(test)
                elif "unity-webgl" in test["name"]:
                    subtests, vals = self.parseUnityWebGLOutput(test)
                elif "webaudio" in test["measurements"]:
                    subtests, vals = self.parseWebaudioOutput(test)
                elif "wasm-godot" in test["measurements"]:
                    subtests, vals = self.parseWASMGodotOutput(test)
                elif "wasm-misc" in test["measurements"]:
                    subtests, vals = self.parseWASMMiscOutput(test)
                elif "sunspider" in test["measurements"]:
                    subtests, vals = self.parseSunspiderOutput(test)
                elif "assorted-dom" in test["measurements"]:
                    subtests, vals = self.parseAssortedDomOutput(test)
                elif "jetstream2" in test["measurements"]:
                    subtests, vals = self.parseJetstreamTwoOutput(test)
                elif "matrix-react-bench" in test["name"]:
                    subtests, vals = self.parseMatrixReactBenchOutput(test)
                elif "twitch-animation" in test["name"]:
                    subtests, vals = self.parseTwitchAnimationOutput(test)
                else:
                    # Attempt to parse the unknown benchmark by flattening the
                    # given data and merging all the arrays of non-iterable
                    # data that fall under the same key.
                    # XXX Note that this is not fully implemented for the summary
                    # of the metric or test as we don't have a use case for that yet.
                    subtests, vals = self.parseUnknown(test)

                if subtests is None:
                    raise Exception("No benchmark metrics found in browsertime results")

                suite["subtests"] = subtests

                if "cpuTime" in test["measurements"] and test.get(
                    "gather_cpuTime", None
                ):
                    replicates = test["measurements"]["cpuTime"]
                    cpu_subtest = _process_measurements("cpuTime", replicates)
                    _process(cpu_subtest)
                    suite["subtests"].append(cpu_subtest)

                if "powerUsage" in test["measurements"]:
                    replicates = test["measurements"]["powerUsage"]
                    power_subtest = _process_measurements("powerUsage", replicates)
                    power_subtest["value"] = round(filters.mean(replicates), 2)
                    suite["subtests"].append(power_subtest)

                # summarize results for both benchmark type tests
                if len(subtests) > 1:
                    suite["value"] = self.construct_summary(vals, testname=test["name"])
                subtests.sort(key=lambda subtest: subtest["name"])

        # convert suites to list
        suites = [
            (
                s
                if ("benchmark" in s["type"] or test.get("support_class"))
                else _process_suite(s)
            )
            for s in suites.values()
        ]

        if test.get("support_class"):
            # Bug 1874690 - Add a verification step to prevent the suites
            # from conflicting during Perfherder ingestion.
            test.get("support_class").summarize_suites(suites)

        suites.sort(key=lambda suite: suite["name"])

        test_results["suites"] = suites
        self.summarized_results = test_results
