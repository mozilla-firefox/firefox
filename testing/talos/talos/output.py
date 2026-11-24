# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""output formats for Talos"""

# NOTE: we have a circular dependency with output.py when we import results
import simplejson as json
from mozlog import get_proxy_logger

from talos import filter, utils

LOG = get_proxy_logger()


class Output:
    """abstract base class for Talos output"""

    @classmethod
    def check(cls, urls):
        """check to ensure that the urls are valid"""

    def __init__(self, results, tsresult_class):
        """
        - results : TalosResults instance
        - tsresult_class : Results class
        """
        self.results = results
        self.tsresult_class = tsresult_class

    def __call__(self):
        suites = []
        test_results = {
            "application": {
                "name": self.results.results[0].browser_name.lower(),
                "version": self.results.results[0].browser_version,
            },
            "framework": {
                "name": self.results.results[0].framework,
            },
            "suites": suites,
        }

        for test in self.results.results:
            # serialize test results
            tsresult = None
            if not test.using_xperf:
                subtests = []
                suite = {
                    "name": test.name(),
                    "extraOptions": self.results.extra_options or [],
                    "subtests": subtests,
                    "shouldAlert": test.test_config.get("suite_should_alert", True),
                }

                suites.append(suite)
                vals = []
                replicates = {}

                # TODO: counters!!!! we don't have any, but they suffer the same
                for result in test.results:
                    # XXX this will not work for manifests which list
                    # the same page name twice. It also ignores cycles
                    for page, val in result.raw_values():
                        if page == "NULL":
                            page = test.name()
                            if tsresult is None:
                                tsresult = r = self.tsresult_class()
                                r.results = [
                                    {"index": 0, "page": test.name(), "runs": val}
                                ]
                            else:
                                r = tsresult.results[0]
                                if r["page"] == test.name():
                                    r["runs"].extend(val)
                        replicates.setdefault(page, []).extend(val)

                tresults = [tsresult] if tsresult else test.results

                # Merge results for the same page when using cycle > 1
                merged_results = {}
                for result in tresults:
                    results = []
                    for r in result.results:
                        page = r["page"]
                        if page in merged_results:
                            merged_results[page]["runs"].extend(r["runs"])
                        else:
                            merged_results[page] = r
                            results.append(r)
                    # override the list of page results for each run
                    result.results = results

                for result in tresults:
                    filtered_results = result.values(
                        suite["name"], test.test_config["filters"]
                    )
                    vals.extend([[i["value"], j] for i, j in filtered_results])
                    subtest_index = 0
                    for val, page in filtered_results:
                        if page == "NULL":
                            # no real subtests
                            page = test.name()
                        subtest = {
                            "name": page,
                            "value": val["filtered"],
                            "replicates": replicates[page],
                        }
                        # if results are from a comparison test i.e. perf-reftest, it will also
                        # contain replicates for 'base' and 'reference'; we wish to keep those
                        # to reference; actual results were calculated as the difference of those
                        base_runs = result.results[subtest_index].get("base_runs", None)
                        ref_runs = result.results[subtest_index].get("ref_runs", None)
                        if base_runs and ref_runs:
                            subtest["base_replicates"] = base_runs
                            subtest["ref_replicates"] = ref_runs

                        subtests.append(subtest)
                        subtest_index += 1

                        if test.test_config.get("lower_is_better") is not None:
                            subtest["lowerIsBetter"] = test.test_config[
                                "lower_is_better"
                            ]
                        if test.test_config.get("alert_threshold") is not None:
                            subtest["alertThreshold"] = test.test_config[
                                "alert_threshold"
                            ]
                        if test.test_config.get("subtest_alerts") is not None:
                            subtest["shouldAlert"] = test.test_config["subtest_alerts"]
                        if test.test_config.get("alert_threshold") is not None:
                            subtest["alertThreshold"] = test.test_config[
                                "alert_threshold"
                            ]
                        if test.test_config.get("unit"):
                            subtest["unit"] = test.test_config["unit"]

                # if there is only one subtest, carry alerting setting from the suite
                if len(subtests) == 1:
                    subtests[0]["shouldAlert"] = suite["shouldAlert"]
                # if there is more than one subtest, calculate a summary result
                elif len(subtests) > 1:
                    suite["value"] = self.construct_results(vals, testname=test.name())
                if test.test_config.get("lower_is_better") is not None:
                    suite["lowerIsBetter"] = test.test_config["lower_is_better"]
                if test.test_config.get("alert_threshold") is not None:
                    suite["alertThreshold"] = test.test_config["alert_threshold"]

            # counters results_aux data
            counter_subtests = []
            for cd in test.all_counter_results:
                for name, vals in cd.items():
                    # We want to add the xperf data as talos_counters
                    # exclude counters whose values are tuples (bad for
                    # graphserver)
                    if len(vals) > 0 and isinstance(vals[0], list):
                        continue

                    # mainthread IO is a list of filenames and accesses, we do
                    # not report this as a counter
                    if "mainthreadio" in name:
                        continue

                    subtest = {
                        "name": name,
                        "value": 0.0,
                    }
                    counter_subtests.append(subtest)

                    if test.using_xperf:
                        if len(vals) > 0:
                            subtest["value"] = vals[0]
                    # calculate mean value
                    elif len(vals) > 0:
                        varray = [float(v) for v in vals]
                        subtest["value"] = filter.mean(varray)
            if counter_subtests:
                suites.append({
                    "name": test.name(),
                    "extraOptions": self.results.extra_options or [],
                    "subtests": counter_subtests,
                    "shouldAlert": test.test_config.get("suite_should_alert", True),
                })
        return test_results

    def output(self, results, results_url):
        """output to the a file if results_url starts with file://
        - results : json instance
        - results_url : file:// URL
        """

        # parse the results url
        results_url_split = utils.urlsplit(results_url)
        results_scheme, results_server, results_path, _, _ = results_url_split

        if results_scheme in ("http", "https"):
            self.post(results, results_server, results_path, results_scheme)
        elif results_scheme == "file":
            with open(results_path, "w") as f:
                for result in results:
                    f.write(f"{result}\n")
        else:
            raise NotImplementedError(
                f"{self.__class__.__name__}: {results_url} - only http://, https://, and file:// supported"
            )

        # This is the output that treeherder expects to find when parsing the
        # log file
        if "gecko-profile" in self.results.extra_options:
            LOG.info("gecko-profile enabled")

            for suite in results["suites"]:
                suite["shouldAlert"] = False
                for subtest in suite["subtests"]:
                    subtest["shouldAlert"] = False

        LOG.info(f"PERFHERDER_DATA: {json.dumps(results, ignore_nan=True)}")

        if results_scheme in ("file"):
            json.dump(
                results,
                open(results_path, "w"),
                indent=2,
                sort_keys=True,
                ignore_nan=True,
            )

    def post(self, results, server, path, scheme):
        raise NotImplementedError("Abstract base class")

    @classmethod
    def shortName(cls, name):
        """short name for counters"""
        names = {"% Processor Time": "%cpu", "XRes": "xres"}
        return names.get(name, name)

    @classmethod
    def isMemoryMetric(cls, resultName):
        """returns if the result is a memory metric"""
        memory_metric = ["xres"]  # measured in bytes
        return bool([i for i in memory_metric if i in resultName])

    @classmethod
    def v8_Metric(cls, val_list):
        results = [i for i, j in val_list]
        score = 100 * filter.geometric_mean(results)
        return score

    @classmethod
    def JS_Metric(cls, val_list):
        """v8 benchmark score"""
        results = [i for i, j in val_list]
        return sum(results)

    @classmethod
    def benchmark_score(cls, val_list):
        """
        benchmark_score: ares6/jetstream self reported as 'geomean'
        """
        results = [i for i, j in val_list if j == "geomean"]
        return filter.mean(results)

    @classmethod
    def damp_score(cls, val_list):
        """
        damp_score: damp is only interested in the value of subtests and will
        aggregate data from several suites.
        Use a hardcoded value for the suite to avoid inconsistencies.
        """
        return 100

    def construct_results(self, vals, testname):
        if testname.startswith("v8_7"):
            return self.v8_Metric(vals)
        elif testname.startswith("kraken"):
            return self.JS_Metric(vals)
        elif testname.startswith("ares6"):
            return self.benchmark_score(vals)
        elif testname.startswith("jetstream"):
            return self.benchmark_score(vals)
        elif testname.startswith("speedometer"):
            return self.speedometer_score(vals)
        elif testname.startswith("damp"):
            return self.damp_score(vals)
        elif len(vals) > 1:
            return filter.geometric_mean([i for i, j in vals])
        else:
            return filter.mean([i for i, j in vals])
