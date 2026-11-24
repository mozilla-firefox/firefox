# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json

import filters
from base_python_support import BasePythonSupport


class MediaCapabilitiesSupport(BasePythonSupport):
    def setup_test(self, next_test, args):
        super().setup_test(next_test, args)
        # summarize_test's test dict doesn't carry subtest_alert_threshold.
        self.subtest_alert_threshold = float(next_test["subtest_alert_threshold"])

    def handle_result(self, bt_result, raw_result, **kwargs):
        """Collect the per-query timings the benchmark posts.

        Each posted entry is {name, values}, where values is the raw list of
        per-query times in order. values[0] is reported as a -cold subtest and
        the rest as -hot, so the first query for each case is tracked separately
        from its steady state instead of being collapsed into one median.

        The benchmark also posts a first-query entry (a single value, main thread
        only) capturing the one-time, process-wide init that the very first query
        in a fresh process pays, so that cost is not charged to whichever codec
        runs first.
        """
        for extra in raw_result["extras"]:
            results = extra.get("browsertime_benchmark", {}).get("media-capabilities")
            if not results:
                continue
            for payload in results:
                for item in json.loads(payload):
                    values = item["values"]
                    if not values:
                        continue
                    bt_result["measurements"].setdefault(
                        item["name"] + "-cold", []
                    ).append(values[0])
                    if len(values) > 1:
                        bt_result["measurements"].setdefault(
                            item["name"] + "-hot", []
                        ).extend(values[1:])

    def summarize_test(self, test, suite, **kwargs):
        suite["type"] = "benchmark"
        if suite["subtests"] == {}:
            suite["subtests"] = []
        for name, replicates in test["measurements"].items():
            if not replicates:
                continue
            subtest = self._build_standard_subtest(test, replicates, name)
            subtest["value"] = round(filters.median(replicates), 3)
            subtest["alertChangeType"] = "absolute"
            subtest["alertThreshold"] = self.subtest_alert_threshold
            suite["subtests"].append(subtest)
        suite["subtests"].sort(key=lambda subtest: subtest["name"])
        suite["value"] = round(
            filters.geometric_mean([s["value"] for s in suite["subtests"]]), 3
        )
