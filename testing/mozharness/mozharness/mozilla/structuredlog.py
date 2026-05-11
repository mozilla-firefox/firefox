# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.
import json
import time
from collections import defaultdict, namedtuple

from mozsystemmonitor.resourcemonitor import SystemResourceMonitor

from mozharness.base import log
from mozharness.base.log import ERROR, INFO, WARNING, OutputParser
from mozharness.mozilla.automation import (
    TBPL_FAILURE,
    TBPL_RETRY,
    TBPL_SUCCESS,
    TBPL_WARNING,
    TBPL_WORST_LEVEL_TUPLE,
)
from mozharness.mozilla.testing.errors import TinderBoxPrintRe
from mozharness.mozilla.testing.unittest import tbox_print_summary


class StructuredOutputParser(OutputParser):
    # The script class using this must inherit the MozbaseMixin to ensure
    # that mozlog is available.
    def __init__(self, **kwargs):
        """Object that tracks the overall status of the test run"""
        # The 'strict' argument dictates whether the presence of output
        # from the harness process other than line-delimited json indicates
        # failure. If it does not, the errors_list parameter may be used
        # to detect additional failure output from the harness process.
        if "strict" in kwargs:
            self.strict = kwargs.pop("strict")
        else:
            self.strict = True

        self.suite_category = kwargs.pop("suite_category", None)

        tbpl_compact = kwargs.pop("log_compact", False)
        super().__init__(**kwargs)
        self.allow_crashes = kwargs.pop("allow_crashes", False)

        mozlog = self._get_mozlog_module()
        self.formatter = mozlog.formatters.TbplFormatter(compact=tbpl_compact)
        self.handler = mozlog.handlers.StatusHandler()
        self.log_actions = mozlog.structuredlog.log_actions()

        self.worst_log_level = INFO
        self.tbpl_status = TBPL_SUCCESS
        self.harness_retry_re = TinderBoxPrintRe["harness_error"]["retry_regex"]
        self.prev_was_unstructured = False

    def _get_mozlog_module(self):
        try:
            import mozlog
        except ImportError:
            self.fatal(
                "A script class using structured logging must inherit "
                "from the MozbaseMixin to ensure that mozlog is available."
            )
        return mozlog

    def _handle_unstructured_output(self, line, log_output=True):
        self.log_output = log_output
        return super().parse_single_line(line)

    def parse_single_line(self, line):
        """Parses a line of log output from the child process and passes
        it to mozlog to update the overall status of the run.
        Re-emits the logged line in human-readable format.
        """
        level = INFO
        tbpl_level = TBPL_SUCCESS

        data = None
        try:
            candidate_data = json.loads(line)
            if (
                isinstance(candidate_data, dict)
                and "action" in candidate_data
                and candidate_data["action"] in self.log_actions
            ):
                data = candidate_data
        except ValueError:
            pass

        if data is None:
            if self.strict:
                if not self.prev_was_unstructured:
                    self.info(
                        "Test harness output was not a valid structured log message"
                    )
                    self.info(line)
                else:
                    self.info(line)
                self.prev_was_unstructured = True
            else:
                self._handle_unstructured_output(line)
            return

        self.prev_was_unstructured = False

        if "time" not in data:
            data["time"] = int(time.time() * 1000)

        self.handler(data)

        action = data["action"]
        if action == "test_start":
            SystemResourceMonitor.begin_test(data)
        elif action == "test_end":
            SystemResourceMonitor.end_test(data)
        elif action == "test_status":
            SystemResourceMonitor.test_status(data)
        elif action == "log":
            SystemResourceMonitor.test_status(data)
        elif action == "process_output":
            SystemResourceMonitor.test_status(data)
        elif action == "crash":
            SystemResourceMonitor.crash(data)
        elif action == "lsan_leak":
            SystemResourceMonitor.lsan_leak(data)
        elif action == "lsan_summary":
            SystemResourceMonitor.lsan_summary(data)
        elif action == "mozleak_object":
            SystemResourceMonitor.mozleak_object(data)
        elif action == "mozleak_total":
            SystemResourceMonitor.mozleak_total(data)
        elif action == "suite_start":
            SystemResourceMonitor.begin_marker("suite", data["source"])
        elif action == "suite_end":
            SystemResourceMonitor.end_marker("suite", data["source"])
        elif action == "group_start":
            SystemResourceMonitor.begin_marker("test", data["name"])
        elif action == "group_end":
            SystemResourceMonitor.end_marker("test", data["name"])

        if action in ("log", "process_output"):
            if action == "log":
                message = data["message"]
                level = getattr(log, data["level"].upper())
            else:
                message = data["data"]

            # Run log and process_output actions through the error lists, but make sure
            # the super parser doesn't print them to stdout (they should go through the
            # log formatter).
            error_level = self._handle_unstructured_output(message, log_output=False)
            if error_level is not None:
                level = self.worst_level(error_level, level)

            if self.harness_retry_re.search(message):
                self.update_levels(TBPL_RETRY, log.CRITICAL)
                tbpl_level = TBPL_RETRY
                level = log.CRITICAL

        log_data = self.formatter(data)
        if log_data is not None:
            self.log(log_data, level=level)
            self.update_levels(tbpl_level, level)

    def _subtract_tuples(self, old, new):
        items = set(list(old.keys()) + list(new.keys()))
        merged = defaultdict(int)
        for item in items:
            merged[item] = new.get(item, 0) - old.get(item, 0)
            if merged[item] <= 0:
                del merged[item]
        return merged

    def evaluate_parser(self, return_code, success_codes=None, previous_summary=None):
        success_codes = success_codes or [0]
        summary = self.handler.summarize()

        """
          We can run evaluate_parser multiple times, it will duplicate failures
          and status which can mean that future tests will fail if a previous test fails.
          When we have a previous summary, we want to do 2 things:
            1) Remove previous data from the new summary to only look at new data
            2) Build a joined summary to include the previous + new data
        """
        RunSummary = namedtuple(
            "RunSummary",
            (
                "unexpected_statuses",
                "expected_statuses",
                "known_intermittent_statuses",
                "log_level_counts",
                "action_counts",
            ),
        )
        if previous_summary == {}:
            previous_summary = RunSummary(
                defaultdict(int),
                defaultdict(int),
                defaultdict(int),
                defaultdict(int),
                defaultdict(int),
            )
        if previous_summary:
            # Always preserve retry status: if any failure triggers retry, the script
            # must exit with TBPL_RETRY to trigger task retry.
            if self.tbpl_status != TBPL_RETRY:
                self.tbpl_status = TBPL_SUCCESS
            joined_summary = summary

            # Remove previously known status messages
            if "ERROR" in summary.log_level_counts:
                summary.log_level_counts["ERROR"] -= self.handler.no_tests_run_count

            summary = RunSummary(
                self._subtract_tuples(
                    previous_summary.unexpected_statuses, summary.unexpected_statuses
                ),
                self._subtract_tuples(
                    previous_summary.expected_statuses, summary.expected_statuses
                ),
                self._subtract_tuples(
                    previous_summary.known_intermittent_statuses,
                    summary.known_intermittent_statuses,
                ),
                self._subtract_tuples(
                    previous_summary.log_level_counts, summary.log_level_counts
                ),
                summary.action_counts,
            )

            # If we have previous data to ignore,
            # cache it so we don't parse the log multiple times
            self.summary = summary
        else:
            joined_summary = summary

        fail_pair = TBPL_WARNING, WARNING
        error_pair = TBPL_FAILURE, ERROR

        # These are warning/orange statuses.
        failure_conditions = [
            (sum(summary.unexpected_statuses.values()), 0, "statuses", False),
            (
                summary.action_counts.get("crash", 0),
                summary.expected_statuses.get("CRASH", 0),
                "crashes",
                self.allow_crashes,
            ),
            (
                summary.action_counts.get("valgrind_error", 0),
                0,
                "valgrind errors",
                False,
            ),
        ]
        for value, limit, type_name, allow in failure_conditions:
            if value > limit:
                msg = "%d unexpected %s" % (value, type_name)
                if limit != 0:
                    msg += " expected at most %d" % (limit)
                if not allow:
                    self.update_levels(*fail_pair)
                    msg = "Got " + msg
                    # Force level to be WARNING as message is not necessary in Treeherder
                    self.warning(msg)
                else:
                    msg = "Ignored " + msg
                    self.warning(msg)

        # These are error/red statuses. A message is output here every time something
        # wouldn't otherwise be highlighted in the UI.
        required_actions = {
            "suite_end": "No suite end message was emitted by this harness.",
            "test_end": "No checks run.",
        }
        for action, diagnostic_message in required_actions.items():
            if action not in summary.action_counts:
                self.log(diagnostic_message, ERROR)
                self.update_levels(*error_pair)

        failure_log_levels = ["ERROR", "CRITICAL"]
        for level in failure_log_levels:
            if level in summary.log_level_counts:
                self.update_levels(*error_pair)

        # If a superclass was used to detect errors with a regex based output parser,
        # this will be reflected in the status here.
        if self.num_errors:
            self.update_levels(*error_pair)

        # Harnesses typically return non-zero on test failure, so don't promote
        # to error if we already have a failing status.
        if return_code not in success_codes and self.tbpl_status == TBPL_SUCCESS:
            self.update_levels(*error_pair)

        return self.tbpl_status, self.worst_log_level, joined_summary

    def update_levels(self, tbpl_level, log_level):
        self.worst_log_level = self.worst_level(log_level, self.worst_log_level)
        self.tbpl_status = self.worst_level(
            tbpl_level, self.tbpl_status, levels=TBPL_WORST_LEVEL_TUPLE
        )

    def print_summary(self, suite_name):
        # Summary text provided for compatibility. Counts are currently
        # in the format <pass count>/<fail count>/<todo count>,
        # <expected count>/<unexpected count>/<expected fail count> will yield the
        # expected info from a structured log (fail count from the prior implementation
        # includes unexpected passes from "todo" assertions).
        try:
            summary = self.summary
        except AttributeError:
            summary = self.handler.summarize()

        unexpected_count = sum(summary.unexpected_statuses.values())
        expected_count = sum(summary.expected_statuses.values())
        expected_failures = summary.expected_statuses.get("FAIL", 0)

        if unexpected_count:
            fail_text = '<em class="testfail">%s</em>' % unexpected_count
        else:
            fail_text = "0"

        text_summary = "%s/%s/%s" % (expected_count, fail_text, expected_failures)
        self.info("TinderboxPrint: %s<br/>%s\n" % (suite_name, text_summary))

    def append_tinderboxprint_line(self, suite_name):
        try:
            summary = self.summary
        except AttributeError:
            summary = self.handler.summarize()

        unexpected_count = sum(summary.unexpected_statuses.values())
        expected_count = sum(summary.expected_statuses.values())
        expected_failures = summary.expected_statuses.get("FAIL", 0)
        crashed = 0
        if "crash" in summary.action_counts:
            crashed = summary.action_counts["crash"]
        text_summary = tbox_print_summary(
            expected_count, unexpected_count, expected_failures, crashed > 0, False
        )
        self.info("TinderboxPrint: %s<br/>%s\n" % (suite_name, text_summary))
