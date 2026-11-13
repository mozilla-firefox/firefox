# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import threading
from collections import defaultdict

from mozleak.asan import ASanErrorParser
from mozlog.formatters import TbplFormatter
from mozrunner.utils import get_stack_fixer_function


class ReftestFormatter(TbplFormatter):
    """
    Formatter designed to preserve the legacy "tbpl" format in reftest.

    This is needed for both the reftest-analyzer and mozharness log parsing.
    We can change this format when both reftest-analyzer and mozharness have
    been changed to read structured logs.
    """

    def __call__(self, data):
        if "component" in data and data["component"] == "mozleak":
            # Output from mozleak requires that no prefix be added
            # so that mozharness will pick up these failures.
            if "message" in data:
                return "%s\n" % data["message"]
            return TbplFormatter.__call__(self, data)

        formatted = TbplFormatter.__call__(self, data)

        if formatted is None:
            return
        if data["action"] == "process_output":
            return formatted
        return "REFTEST %s" % formatted

    def log(self, data):
        prefix = "%s |" % data["level"].upper()
        return "%s %s\n" % (prefix, data["message"])

    def _format_status(self, data):
        extra = data.get("extra", {})
        status = data["status"]

        status_msg = "TEST-"
        if "expected" in data:
            status_msg += "UNEXPECTED-%s" % status
        else:
            if status not in ("PASS", "SKIP"):
                status_msg += "KNOWN-"
            status_msg += status
            if extra.get("status_msg") == "Random":
                status_msg += "(EXPECTED RANDOM)"
        return status_msg

    def test_status(self, data):
        extra = data.get("extra", {})
        test = data["test"]

        status_msg = self._format_status(data)
        output_text = "%s | %s | %s" % (
            status_msg,
            test,
            data.get("subtest", "unknown test"),
        )
        if data.get("message"):
            output_text += " | %s" % data["message"]

        if "reftest_screenshots" in extra:
            screenshots = extra["reftest_screenshots"]
            image_1 = screenshots[0]["screenshot"]

            if len(screenshots) == 3:
                image_2 = screenshots[2]["screenshot"]
                output_text += (
                    "\nREFTEST   IMAGE 1 (TEST): data:image/png;base64,%s\n"
                    "REFTEST   IMAGE 2 (REFERENCE): data:image/png;base64,%s"
                ) % (image_1, image_2)
            elif len(screenshots) == 1:
                output_text += "\nREFTEST   IMAGE: data:image/png;base64,%s" % image_1

        return output_text + "\n"

    def test_end(self, data):
        return "TEST-END | %s\n" % data["test"]

    def process_output(self, data):
        return "%s\n" % data["data"]

    def suite_end(self, data):
        lines = []
        summary = data["extra"]["results"]
        summary["success"] = summary["Pass"] + summary["LoadOnly"]
        lines.append(
            "Successful: %(success)s (%(Pass)s pass, %(LoadOnly)s load only)" % summary
        )
        summary["unexpected"] = (
            summary["Exception"]
            + summary["FailedLoad"]
            + summary["UnexpectedFail"]
            + summary["UnexpectedPass"]
            + summary["AssertionUnexpected"]
            + summary["AssertionUnexpectedFixed"]
        )
        lines.append(
            (
                "Unexpected: %(unexpected)s (%(UnexpectedFail)s unexpected fail, "
                "%(UnexpectedPass)s unexpected pass, "
                "%(AssertionUnexpected)s unexpected asserts, "
                "%(FailedLoad)s failed load, "
                "%(Exception)s exception)"
            )
            % summary
        )
        summary["known"] = (
            summary["KnownFail"]
            + summary["AssertionKnown"]
            + summary["Random"]
            + summary["Skip"]
            + summary["Slow"]
        )
        lines.append(
            (
                "Known problems: %(known)s ("
                + "%(KnownFail)s known fail, "
                + "%(AssertionKnown)s known asserts, "
                + "%(Random)s random, "
                + "%(Skip)s skipped, "
                + "%(Slow)s slow)"
            )
            % summary
        )
        lines = ["REFTEST INFO | %s" % s for s in lines]
        lines.append("REFTEST SUITE-END | Shutdown")
        return "INFO | Result summary:\n{}\n".format("\n".join(lines))


class OutputHandler:
    """Process the output of a process during a test run and translate
    raw data logged from reftest.js to an appropriate structured log action,
    where applicable.
    """

    def __init__(self, log, utilityPath, symbolsPath=None):
        self.stack_fixer_function = get_stack_fixer_function(utilityPath, symbolsPath)
        self.log = log
        self.proc_name = None
        self.results = defaultdict(int)
        self.asan_parser = ASanErrorParser(log)
        # Mirrors RefTest.lastTestSeen: the running test, or its manifest
        # between tests, which crashes are blamed on too.
        self.lastTestSeen = None
        self.scope = None

    def __call__(self, line):
        # need to return processed messages to appease remoteautomation.py
        if not line.strip():
            return []
        line = line.decode("utf-8", errors="replace")

        try:
            data = json.loads(line)
        except ValueError:
            self.verbatim(line)
            return [line]

        if isinstance(data, dict) and "action" in data:
            if data["action"] == "results":
                for k, v in data["results"].items():
                    self.results[k] += v
            else:
                self.log.log_raw(data)
        else:
            self.verbatim(json.dumps(data))

        return [data]

    def write(self, data):
        return self.__call__(data)

    def verbatim(self, line):
        if self.stack_fixer_function:
            line = self.stack_fixer_function(line)
        name = self.proc_name or threading.current_thread().name
        self.asan_parser.log(line, pid=name, scope=self.scope, test=self.lastTestSeen)
        self.log.process_output(name, line)

    def finish(self):
        self.asan_parser.flush()
