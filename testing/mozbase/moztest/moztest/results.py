# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import time

import mozinfo


class TestContext:
    """Stores context data about the test"""

    attrs = [
        "hostname",
        "arch",
        "env",
        "os",
        "os_version",
        "tree",
        "revision",
        "product",
        "logfile",
        "testgroup",
        "harness",
        "buildtype",
    ]

    def __init__(
        self,
        hostname="localhost",
        tree="",
        revision="",
        product="",
        logfile=None,
        arch="",
        operating_system="",
        testgroup="",
        harness="moztest",
        buildtype="",
    ):
        self.hostname = hostname
        self.arch = arch or mozinfo.processor
        self.env = os.environ.copy()
        self.os = operating_system or mozinfo.os
        self.os_version = mozinfo.version
        self.tree = tree
        self.revision = revision
        self.product = product
        self.logfile = logfile
        self.testgroup = testgroup
        self.harness = harness
        self.buildtype = buildtype

    def __str__(self):
        return "%s (%s, %s)" % (self.hostname, self.os, self.arch)

    def __repr__(self):
        return "<%s>" % self.__str__()

    def __eq__(self, other):
        if not isinstance(other, TestContext):
            return False
        diffs = [a for a in self.attrs if getattr(self, a) != getattr(other, a)]
        return len(diffs) == 0

    def __hash__(self):
        def get(attr):
            value = getattr(self, attr)
            if isinstance(value, dict):
                value = frozenset(value.items())
            return value

        return hash(frozenset([get(a) for a in self.attrs]))


class TestResult:
    """Stores test result data"""

    FAIL_RESULTS = [
        "UNEXPECTED-PASS",
        "UNEXPECTED-FAIL",
        "ERROR",
    ]
    COMPUTED_RESULTS = FAIL_RESULTS + [
        "PASS",
        "KNOWN-FAIL",
        "SKIPPED",
    ]
    POSSIBLE_RESULTS = [
        "PASS",
        "FAIL",
        "SKIP",
        "ERROR",
    ]

    def __init__(
        self, name, test_class="", time_start=None, context=None, result_expected="PASS"
    ):
        """Create a TestResult instance.
        name = name of the test that is running
        test_class = the class that the test belongs to
        time_start = timestamp (seconds since UNIX epoch) of when the test started
                     running; if not provided, defaults to the current time
                     ! Provide 0 if you only have the duration
        context = TestContext instance; can be None
        result_expected = string representing the expected outcome of the test"""

        msg = "Result '%s' not in possible results: %s" % (
            result_expected,
            ", ".join(self.POSSIBLE_RESULTS),
        )
        assert isinstance(name, str), "name has to be a string"
        assert result_expected in self.POSSIBLE_RESULTS, msg

        self.name = name
        self.test_class = test_class
        self.context = context
        self.time_start = time_start if time_start is not None else time.time()
        self.time_end = None
        self._result_expected = result_expected
        self._result_actual = None
        self.result = None
        self.filename = None
        self.description = None
        self.output = []
        self.reason = None

    @property
    def test_name(self):
        return "%s.py %s.%s" % (
            self.test_class.split(".")[0],
            self.test_class,
            self.name,
        )

    def __str__(self):
        return "%s | %s (%s) | %s" % (
            self.result or "PENDING",
            self.name,
            self.test_class,
            self.reason,
        )

    def __repr__(self):
        return "<%s>" % self.__str__()

    def calculate_result(self, expected, actual):
        if actual == "ERROR":
            return "ERROR"
        if actual == "SKIP":
            return "SKIPPED"

        if expected == "PASS":
            if actual == "PASS":
                return "PASS"
            if actual == "FAIL":
                return "UNEXPECTED-FAIL"

        if expected == "FAIL":
            if actual == "PASS":
                return "UNEXPECTED-PASS"
            if actual == "FAIL":
                return "KNOWN-FAIL"

        # if actual is skip or error, we return at the beginning, so if we get
        # here it is definitely some kind of error
        return "ERROR"

    def infer_results(self, computed_result):
        assert computed_result in self.COMPUTED_RESULTS
        if computed_result == "UNEXPECTED-PASS":
            expected = "FAIL"
            actual = "PASS"
        elif computed_result == "UNEXPECTED-FAIL":
            expected = "PASS"
            actual = "FAIL"
        elif computed_result == "KNOWN-FAIL":
            expected = actual = "FAIL"
        elif computed_result == "SKIPPED":
            expected = actual = "SKIP"
        else:
            return
        self._result_expected = expected
        self._result_actual = actual

    def finish(self, result, time_end=None, output=None, reason=None):
        """Marks the test as finished, storing its end time and status
        ! Provide the duration as time_end if you only have that."""

        if result in self.POSSIBLE_RESULTS:
            self._result_actual = result
            self.result = self.calculate_result(
                self._result_expected, self._result_actual
            )
        elif result in self.COMPUTED_RESULTS:
            self.infer_results(result)
            self.result = result
        else:
            valid = self.POSSIBLE_RESULTS + self.COMPUTED_RESULTS
            msg = "Result '%s' not valid. Need one of: %s" % (result, ", ".join(valid))
            raise ValueError(msg)

        # use lists instead of multiline strings
        if isinstance(output, str):
            output = output.splitlines()

        self.time_end = time_end if time_end is not None else time.time()
        self.output = output or self.output
        self.reason = reason

    @property
    def finished(self):
        """Boolean saying if the test is finished or not"""
        return self.result is not None

    @property
    def duration(self):
        """Returns the time it took for the test to finish. If the test is
        not finished, returns the elapsed time so far"""
        if self.result is not None:
            return self.time_end - self.time_start
        else:
            # returns the elapsed time
            return time.time() - self.time_start


class TestResultCollection(list):
    """Container class that stores test results"""

    resultClass = TestResult

    def __init__(self, suite_name, time_taken=0, resultClass=None):
        list.__init__(self)
        self.suite_name = suite_name
        self.time_taken = time_taken
        if resultClass is not None:
            self.resultClass = resultClass

    def __str__(self):
        return "%s (%.2fs)\n%s" % (self.suite_name, self.time_taken, list.__str__(self))

    def subset(self, predicate):
        tests = self.filter(predicate)
        duration = 0
        sub = TestResultCollection(self.suite_name)
        for t in tests:
            sub.append(t)
            duration += t.duration
        sub.time_taken = duration
        return sub

    @property
    def contexts(self):
        """List of unique contexts for the test results contained"""
        cs = [tr.context for tr in self]
        return list(set(cs))

    def filter(self, predicate):
        """Returns a generator of TestResults that satisfy a given predicate"""
        return (tr for tr in self if predicate(tr))

    def tests_with_result(self, result):
        """Returns a generator of TestResults with the given result"""
        msg = "Result '%s' not in possible results: %s" % (
            result,
            ", ".join(self.resultClass.COMPUTED_RESULTS),
        )
        assert result in self.resultClass.COMPUTED_RESULTS, msg
        return self.filter(lambda t: t.result == result)

    @property
    def tests(self):
        """Generator of all tests in the collection"""
        return (t for t in self)

    def add_result(
        self,
        test,
        result_expected="PASS",
        result_actual="PASS",
        output="",
        context=None,
    ):
        def get_class(test):
            return test.__class__.__module__ + "." + test.__class__.__name__

        t = self.resultClass(
            name=str(test).split()[0],
            test_class=get_class(test),
            time_start=0,
            result_expected=result_expected,
            context=context,
        )
        t.finish(result_actual, time_end=0, reason=relevant_line(output), output=output)
        self.append(t)

    @property
    def num_failures(self):
        fails = 0
        for t in self:
            if t.result in self.resultClass.FAIL_RESULTS:
                fails += 1
        return fails

    def add_unittest_result(self, result, context=None):
        """Adds the python unittest result provided to the collection"""
        if hasattr(result, "time_taken"):
            self.time_taken += result.time_taken

        for test, output in result.errors:
            self.add_result(test, result_actual="ERROR", output=output)

        for test, output in result.failures:
            self.add_result(test, result_actual="FAIL", output=output)

        if hasattr(result, "unexpectedSuccesses"):
            for test in result.unexpectedSuccesses:
                self.add_result(test, result_expected="FAIL", result_actual="PASS")

        if hasattr(result, "skipped"):
            for test, output in result.skipped:
                self.add_result(
                    test, result_expected="SKIP", result_actual="SKIP", output=output
                )

        if hasattr(result, "expectedFailures"):
            for test, output in result.expectedFailures:
                self.add_result(
                    test, result_expected="FAIL", result_actual="FAIL", output=output
                )

        # unittest does not store these by default
        if hasattr(result, "tests_passed"):
            for test in result.tests_passed:
                self.add_result(test)

    @classmethod
    def from_unittest_results(cls, context, *results):
        """Creates a TestResultCollection containing the given python
        unittest results"""

        if not results:
            return cls("from unittest")

        # all the TestResult instances share the same context
        context = context or TestContext()

        collection = cls("from %s" % results[0].__class__.__name__)

        for result in results:
            collection.add_unittest_result(result, context)

        return collection


# used to get exceptions/errors from tracebacks
def relevant_line(s):
    KEYWORDS = ("Error:", "Exception:", "error:", "exception:")
    lines = s.splitlines()
    for line in lines:
        for keyword in KEYWORDS:
            if keyword in line:
                return line
    return "N/A"
