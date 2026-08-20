# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import pathlib
import sys
from collections.abc import Iterable

import filters

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from browsertime_pageload import PageloadSupport
from logger.logger import RaptorLogger

LOG = RaptorLogger(component="perftest-support-class")

METRIC_BLOCKLIST = [
    "mean",
    "median",
    "geomean",
]


class MissingBenchmarkResultsError(Exception):
    """
    This error is raised when the benchmark results from a test
    run do not contain the `browsertime_benchmark` entry in the dict
    of extra data.
    """

    pass


class BenchmarkSupport(PageloadSupport):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.failed_tests = []
        self.youtube_playback_failure = False

    def setup_test(self, next_test, args):
        super().setup_test(next_test, args)
        if next_test.get("custom_data", False) == "true":
            raise ValueError(
                "Cannot use BenchmarkSupport class for custom data, a "
                "new support class should be built for that use case."
            )

    def modify_command(self, cmd, test):
        # Enable cpuTime, and wallclock-tracking metrics
        cmd.extend([
            "--browsertime.cpuTime_test",
            "true",
            "--browsertime.wallclock_tracking_test",
            "true",
        ])

    def handle_result(self, bt_result, raw_result, **kwargs):
        """Parse a result for the required results.

        See base_python_support.py for what's expected from this method.
        """
        # Each entry here is a separate cold pageload iteration (or browser cycle)
        for custom_types in raw_result["extras"]:
            browsertime_benchmark_results = custom_types.get("browsertime_benchmark")
            if not browsertime_benchmark_results:
                raise MissingBenchmarkResultsError(
                    "Could not find `browsertime_benchmark` entry "
                    "in the browsertime `extra` results"
                )
            for metric, values in browsertime_benchmark_results.items():
                bt_result["measurements"].setdefault(metric, []).append(values)

        if self.perfstats:
            for cycle in raw_result["geckoPerfStats"]:
                for metric in cycle:
                    bt_result["measurements"].setdefault(
                        "perfstat-" + metric, []
                    ).append(cycle[metric])

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

        for pagecycle in data:
            for _sub, _value in pagecycle[0].items():
                if _value["decodedFrames"] == 0:
                    self.failed_tests.append(
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
        if len(self.failed_tests) > 0:
            self.youtube_playback_failure = True
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

    def parseWebCodecsOutput(self, test):
        """
        Example output (this is one page cycle):

        {
            'name': 'webcodecs',
            'type': 'benchmark',
            'measurements': {
            'webcodecs': [
                ['{
                    "vp8 realtime encode": {
                        "frame-to-frame mean (key)": {"value":5.222857,"unit":"ms"},
                        "frame-to-frame cv (key)":{"value":27.052957,"unit":"%"},
                        "frame-dropping rate (key)":{"value":0,"unit":"%"},
                        "frame-to-frame mean (non key)":{"value":1.460678,"unit":"ms"},
                        "frame-to-frame cv (non key)":{"value":65.4360136,"unit":"%"},
                        "frame-dropping rate (non key)":{"value":0,"unit":"%"}
                    }
                }'],
                ...
            ]
            },
            'lower_is_better': False,
            'unit': 'score'
        }
        """

        data = test["measurements"]["webcodecs"]
        results = {}
        for page_cycle in data:
            d = json.loads(page_cycle[0])
            for test_name, test_data in d.items():
                results.setdefault(test_name, []).append(test_data)

        _subtests = {}
        for test_name in results:
            for result in results[test_name]:
                for subtest_name, subtest_result in result.items():
                    subtest_result_name = f"{test_name} - {subtest_name}"
                    _subtests.setdefault(
                        subtest_result_name,
                        {
                            "unit": subtest_result["unit"],
                            "alertThreshold": float(test["alert_threshold"]),
                            "lowerIsBetter": test["subtest_lower_is_better"],
                            "name": subtest_result_name,
                            "replicates": [],
                            "shouldAlert": True,
                        },
                    )["replicates"].append(subtest_result["value"])

        for subtest in _subtests.values():
            subtest["value"] = filters.median(subtest["replicates"])

        subtests = sorted(_subtests.values(), key=lambda x: x["name"], reverse=True)
        for subtest in subtests:
            if isinstance(subtest["value"], float):
                subtest["value"] = round(subtest["value"], 3)
        vals = [[subtest["value"], subtest["name"]] for subtest in subtests]
        return subtests, vals

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

        for iteration in test["measurements"][list(test["measurements"].keys())[0]]:
            flattened_metrics = None

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

        if testname.startswith("ve-"):
            if "rt" in testname:
                # We collect the mean and cv of frame-to-frame performance and the
                # frame-dropping rate for both keyframe and non-keyframe. However,
                # the most important factor is the frame-to-frame mean, so we only
                # include it in the summarized score. Note that all the values
                # collected are monitored by "shouldAlert".
                means = [i for i, j in vals if "mean" in j]
                if len(means) > 0:
                    return round(filters.geometric_mean(means), 2)
                return -1

            if "q" in testname:
                if len(vals) > 0:
                    return round(filters.mean(_filter(vals)), 2)
                return -1

            raise NotImplementedError(f"Summary for {testname} is not implemented")

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

    def _process_measurements(self, suite, test, measurement_name, replicates):
        subtest = {}
        subtest["name"] = measurement_name
        subtest["lowerIsBetter"] = test["subtest_lower_is_better"]
        subtest["alertThreshold"] = float(test["alert_threshold"])

        unit = test["subtest_unit"]
        if measurement_name == "cpuTime":
            unit = "ms"
        elif measurement_name == "powerUsage":
            unit = "uWh"
        subtest["unit"] = unit

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

        if self.power_test and measurement_name == "powerUsage":
            subtest["shouldAlert"] = True

        subtest["replicates"] = replicates
        return subtest

    def summarize_test(self, test, suite, **kwargs):
        subtests = None
        if "youtube-playback" in test["name"]:
            subtests, vals = self.parseYoutubePlaybackPerformanceOutput(test)
        elif test["name"].startswith("ve-"):
            subtests, vals = self.parseWebCodecsOutput(test)
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

        self.add_additional_metrics(test, suite)

        # summarize results for both benchmark type tests
        if len(subtests) > 1:
            suite["value"] = self.construct_summary(vals, testname=test["name"])
        subtests.sort(key=lambda subtest: subtest["name"])

    def summarize_suites(self, suites):
        pass

    def report_test_success(self):
        if len(self.failed_tests) > 0:
            LOG.warning("Some tests failed.")
            if self.youtube_playback_failure:
                for test in self.failed_tests:
                    LOG.warning(f"Youtube sub-test FAILED: {test}")
                LOG.warning(
                    "Youtube playback sub-tests failed!!! "
                    "Not submitting results to perfherder!"
                )
            return False
        return True
