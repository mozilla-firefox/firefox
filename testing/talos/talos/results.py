#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
objects and methods for parsing and serializing Talos results
see https://wiki.mozilla.org/Buildbot/Talos/DataFormat
"""

import csv
import json
import os
import re

from talos import filter, output, utils


class TalosResults:
    """Container class for Talos results"""

    def __init__(self):
        self.results = []
        self.extra_options = []

    def add(self, test_results):
        self.results.append(test_results)

    def add_extra_option(self, extra_option):
        self.extra_options.append(extra_option)

    def has_results(self):
        return len(self.results) > 0

    def output(self, output_formats):
        """
        output all results to appropriate URLs
        - output_formats: a dict mapping formats to a list of URLs
        """
        try:
            for key, urls in output_formats.items():
                _output = output.Output(self, Results)
                results = _output()
                for url in urls:
                    _output.output(results, url)
        except utils.TalosError as e:
            # print to results.out
            try:
                _output = output.GraphserverOutput(self)
                results = _output()
                _output.output(
                    "file://" + os.path.join(os.getcwd(), "results.out"), results
                )
            except Exception:
                pass
            fail_msg = str(e).replace("\n", "\nRETURN:")
            print(f"\nFAIL: {fail_msg}")
            raise e


class TestResults:
    """container object for all test results across cycles"""

    def __init__(self, test_config, global_counters=None, framework=None):
        self.results = []
        self.test_config = test_config
        self.format = None
        self.global_counters = global_counters or {}
        self.all_counter_results = []
        self.framework = framework
        self.using_xperf = False

    def name(self):
        return self.test_config["name"]

    def mainthread(self):
        return self.test_config["mainthread"]

    def add(self, results, counter_results=None):
        """
        accumulate one cycle of results
        - results : TalosResults instance or path to browser log
        - counter_results : counters accumulated for this cycle
        """

        # convert to a results class via parsing the browser log
        format_pagename = True
        if not self.test_config["format_pagename"]:
            format_pagename = False

        browserLog = BrowserLogResults(
            results,
            format_pagename=format_pagename,
            counter_results=counter_results,
            global_counters=self.global_counters,
        )
        results = browserLog.results()

        self.using_xperf = browserLog.using_xperf
        # ensure the results format matches previous results
        if self.results:
            if not results.format == self.results[0].format:
                raise utils.TalosError("Conflicting formats for results")
        else:
            self.format = results.format

        self.results.append(results)

        if counter_results:
            self.all_counter_results.append(counter_results)


class Results:
    def filter(self, testname, filters):
        """
        filter the results set;
        applies each of the filters in order to the results data
        filters should be callables that take a list
        the last filter should return a scalar (float or int)
        returns a list of [[data, page], ...]
        """
        retval = []
        for result in self.results:
            page = result["page"]
            data = result["runs"]
            remaining_filters = []

            # ignore* functions return a filtered set of data
            for f in filters:
                if f.func.__name__.startswith("ignore"):
                    data = f.apply(data)
                else:
                    remaining_filters.append(f)

            # apply the summarization filters
            for f in remaining_filters:
                if f.func.__name__ == "v8_subtest":
                    # for v8_subtest we need to page for reference data
                    data = filter.v8_subtest(data, page)
                else:
                    data = f.apply(data)

            summary = {
                "filtered": data,  # backward compatibility with perfherder
                "value": data,
            }

            retval.append([summary, page])

        return retval

    def raw_values(self):
        return [(result["page"], result["runs"]) for result in self.results]

    def values(self, testname, filters):
        """return filtered (value, page) for each value"""
        return [
            [val, page]
            for val, page in self.filter(testname, filters)
            if val["filtered"] > -1
        ]


class TsResults(Results):
    """
    results for Ts tests
    """

    format = "tsformat"

    def __init__(self, string, counter_results=None, format_pagename=True):
        self.counter_results = counter_results

        string = string.strip()
        lines = string.splitlines()

        # gather the data
        self.results = []
        index = 0

        # Case where one test iteration may report multiple event values i.e. ts_paint
        if string.startswith("{"):
            jsonResult = json.loads(string)
            result = {"runs": {}}
            result["index"] = index
            result["page"] = "NULL"

            for event_label in jsonResult:
                result["runs"][str(event_label)] = [jsonResult[event_label]]
            self.results.append(result)

        # Case where we support a pagename in the results
        if not self.results:
            for line in lines:
                result = {}
                r = line.strip().split(",")
                r = [i for i in r if i]
                if len(r) <= 1:
                    continue
                result["index"] = index
                result["page"] = r[0]
                # note: if we have len(r) >1, then we have pagename,raw_results
                result["runs"] = [float(i) for i in r[1:]]
                self.results.append(result)
                index += 1

        # Original case where we just have numbers and no pagename
        if not self.results:
            result = {}
            result["index"] = index
            result["page"] = "NULL"
            result["runs"] = [float(val) for val in string.split("|")]
            self.results.append(result)


class PageloaderResults(Results):
    """
    results from a browser_dump snippet
    https://wiki.mozilla.org/Buildbot/Talos/DataFormat#browser_output.txt
    """

    format = "tpformat"

    def __init__(self, string, counter_results=None, format_pagename=True):
        """
        - string : string of relevent part of browser dump
        - counter_results : counter results dictionary
        """

        self.counter_results = counter_results

        string = string.strip()
        lines = string.splitlines()

        # currently we ignore the metadata on top of the output (e.g.):
        # _x_x_mozilla_page_load
        # _x_x_mozilla_page_load_details
        # |i|pagename|runs|
        lines = [line for line in lines if ";" in line]

        # gather the data
        self.results = []
        prev_line = ""
        for line in lines:
            result = {}

            # Bug 1562883 - Determine what is causing a single line to get
            # written on multiple lines.
            if prev_line:
                line = prev_line + line
                prev_line = ""

            r = line.strip("|").split(";")
            r = [i for i in r if i]

            if len(r) <= 2:
                prev_line = line
                continue

            result["index"] = int(r[0])
            result["page"] = r[1]
            result["runs"] = [float(i) for i in r[2:]]

            # fix up page
            if format_pagename:
                result["page"] = self.format_pagename(result["page"])

            self.results.append(result)

    def format_pagename(self, page):
        """
        fix up the page for reporting
        """
        page = page.rstrip("/")
        if "/" in page:
            if "base_page" in page or "ref_page" in page:
                # for base vs ref type test, the page name is different format, i.e.
                # base_page_1_http://localhost:53309/tests/perf-reftest/bloom-basic.html
                page = page.split("/")[-1]
            else:
                page = page.split("/")[0]
        return page


class BrowserLogResults:
    """parse the results from the browser log output"""

    # tokens for the report types
    report_tokens = [
        ("tsformat", ("__start_report", "__end_report")),
        ("tpformat", ("__start_tp_report", "__end_tp_report")),
    ]

    # tokens for timestamps, in order (attribute, (start_delimeter,
    # end_delimter))
    time_tokens = [
        ("startTime", ("__startTimestamp", "__endTimestamp")),
        (
            "beforeLaunchTime",
            ("__startBeforeLaunchTimestamp", "__endBeforeLaunchTimestamp"),
        ),
        (
            "endTime",
            ("__startAfterTerminationTimestamp", "__endAfterTerminationTimestamp"),
        ),
    ]

    # regular expression for failure case if we can't parse the tokens
    RESULTS_REGEX_FAIL = re.compile("__FAIL(.*?)__FAIL", re.DOTALL | re.MULTILINE)

    # classes for results types
    classes = {"tsformat": TsResults, "tpformat": PageloaderResults}

    # If we are using xperf, we do not upload the regular results, only
    # xperf counters
    using_xperf = False

    def __init__(
        self,
        results_raw,
        format_pagename=True,
        counter_results=None,
        global_counters=None,
    ):
        """
        - shutdown : whether to record shutdown results or not
        """

        self.counter_results = counter_results
        self.global_counters = global_counters
        self.format_pagename = format_pagename
        self.results_raw = results_raw

        # parse the results
        try:
            match = self.RESULTS_REGEX_FAIL.search(self.results_raw)
            if match:
                self.error(match.group(1))
                raise utils.TalosError(match.group(1))

            self.parse()
        except utils.TalosError:
            # TODO: consider investigating this further or adding additional
            # information
            raise  # reraise failing exception

        # accumulate counter results
        self.counters(self.counter_results, self.global_counters)

    def error(self, message):
        """raise a TalosError for bad parsing of the browser log"""
        raise utils.TalosError(message)

    def parse(self):
        position = -1

        # parse the report
        for format, tokens in self.report_tokens:
            report, position = self.get_single_token(*tokens)
            if report is None:
                continue
            self.browser_results = report
            self.format = format
            previous_tokens = tokens
            break
        else:
            self.error(f"Could not find report in browser output: {self.report_tokens}")

        # parse the timestamps
        for attr, tokens in self.time_tokens:
            # parse the token contents
            value, _last_token = self.get_single_token(*tokens)

            # check for errors
            if not value:
                self.error(
                    f"Could not find {attr} in browser output: (tokens: {tokens})"
                )
            try:
                value = int(float(value))
            except ValueError:
                self.error(f"Could not cast {attr} to an integer: {value}")
            if _last_token < position:
                self.error(
                    f"{tokens} [character position: {_last_token}] found before {previous_tokens}"
                    f" [character position: {position}]"
                )

            # process
            setattr(self, attr, value)
            position = _last_token
            previous_tokens = tokens

    def get_single_token(self, start_token, end_token):
        """browser logs should only have a single instance of token pairs"""
        try:
            parts, last_token = utils.tokenize(self.results_raw, start_token, end_token)
        except AssertionError as e:
            self.error(str(e))
        if not parts:
            return None, -1  # no match
        if len(parts) != 1:
            self.error(f"Multiple matches for {start_token},{end_token}")
        return parts[0], last_token

    def results(self):
        """return results instance appropriate to the format detected"""

        if self.format not in self.classes:
            raise utils.TalosError(
                f"Unable to find a results class for format: {repr(self.format)}"
            )

        return self.classes[self.format](
            self.browser_results, format_pagename=self.format_pagename
        )

    # methods for counters

    def counters(self, counter_results=None, global_counters=None):
        """accumulate all counters"""

        if global_counters is not None:
            self.xperf(global_counters)

    def xperf(self, counter_results):
        """record xperf counters in counter_results dictionary"""

        session_store_counter = "time_to_session_store_window_restored_ms"

        counters = [
            "main_startup_fileio",
            "main_startup_netio",
            "main_normal_fileio",
            "main_normal_netio",
            "nonmain_startup_fileio",
            "nonmain_normal_fileio",
            "nonmain_normal_netio",
            session_store_counter,
        ]

        mainthread_counter_keys = ["readcount", "readbytes", "writecount", "writebytes"]
        mainthread_counters = [
            "_".join(["mainthread", counter_key])
            for counter_key in mainthread_counter_keys
        ]

        self.mainthread_io(counter_results)

        if (
            not set(counters)
            .union(set(mainthread_counters))
            .intersection(counter_results.keys())
        ):
            # no xperf counters to accumulate
            return

        filename = "etl_output_thread_stats.csv"
        if not os.path.exists(filename):
            raise utils.TalosError(
                f"Error: we are looking for xperf results file {filename},"
                " and didn't find it"
            )

        contents = open(filename).read()
        lines = contents.splitlines()
        reader = csv.reader(lines)
        header = None
        for row in reader:
            # Read CSV
            row = [i.strip() for i in row]
            if not header:
                # We are assuming the first row is the header and all other
                # data is counters
                header = row
                continue
            values = dict(zip(header, row))

            # Format for talos
            thread = values["thread"]
            counter = values["counter"].rsplit("_io_bytes", 1)[0]
            counter_name = f"{thread}_{values['stage']}_{counter}io"
            value = float(values["value"])

            # Accrue counter
            if counter_name in counter_results:
                counter_results.setdefault(counter_name, []).append(value)
                self.using_xperf = True

        if set(mainthread_counters).intersection(counter_results.keys()):
            filename = "etl_output.csv"
            if not os.path.exists(filename):
                raise utils.TalosError(
                    "Error: we are looking for xperf results file"
                    f" {filename}, and didn't find it"
                )

            contents = open(filename).read()
            lines = contents.splitlines()
            reader = csv.reader(lines)
            header = None
            for row in reader:
                row = [i.strip() for i in row]
                if not header:
                    # We are assuming the first row is the header and all
                    # other data is counters
                    header = row
                    continue
                values = dict(zip(header, row))
                for i, mainthread_counter in enumerate(mainthread_counters):
                    if int(values[mainthread_counter_keys[i]]) > 0:
                        counter_results.setdefault(mainthread_counter, []).append([
                            int(values[mainthread_counter_keys[i]]),
                            values["filename"],
                        ])

        if session_store_counter in counter_results.keys():
            filename = "etl_output_session_restore_stats.csv"
            # This file is a csv but it only contains one field, so we'll just
            # obtain the value by converting the second line in the file.
            with open(filename) as contents:
                lines = contents.read().splitlines()
                if len(lines) > 1:
                    value = float(lines[1].strip())
                    counter_results.setdefault(session_store_counter, []).append(value)

    def mainthread_io(self, counter_results):
        """record mainthread IO counters in counter_results dictionary"""

        # we want to measure mtio on xperf runs.
        # this will be shoved into the xperf results as we ignore those
        SCRIPT_DIR = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
        filename = os.path.join(SCRIPT_DIR, "mainthread_io.json")
        try:
            contents = open(filename).read()
            counter_results.setdefault("mainthreadio", []).append(contents)
            self.using_xperf = True
        except Exception:
            # silent failure is fine here as we will only see this on tp5n runs
            pass
