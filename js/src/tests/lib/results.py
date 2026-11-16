import json
import re
import shlex

from .progressbar import NullProgressBar, ProgressBar
from .structuredlog import TestLogger

# subprocess.list2cmdline does not properly escape for sh-like shells


def escape_cmdline(args):
    return " ".join([shlex.quote(a) for a in args])


class TestOutput:
    """Output from a test run."""

    def __init__(self, test, cmd, out, err, rc, dt, timed_out, extra=None):
        self.test = test  # Test
        self.cmd = cmd  # str:   command line of test
        self.out = out  # str:   stdout
        self.err = err  # str:   stderr
        self.rc = rc  # int:   return code
        self.dt = dt  # float: run time
        self.timed_out = timed_out  # bool: did the test time out
        self.extra = extra  # includes the pid on some platforms

    def describe_failure(self):
        if self.timed_out:
            return "Timeout"
        lines = self.err.splitlines()
        if lines:
            return lines[0]
        return "Unknown"


class NullTestOutput:
    """Variant of TestOutput that indicates a test was not run."""

    def __init__(self, test):
        self.test = test
        self.cmd = ""
        self.out = ""
        self.err = ""
        self.rc = 0
        self.dt = 0.0
        self.timed_out = False


class TestResult:
    PASS = "PASS"
    FAIL = "FAIL"
    CRASH = "CRASH"

    """Classified result from a test run."""

    def __init__(self, test, result, results, wpt_results=None):
        self.test = test
        self.result = result
        self.results = results
        self.wpt_results = wpt_results  # Only used for wpt tests.

    @classmethod
    def from_wpt_output(cls, output):
        """Parse the output from a web-platform test that uses testharness.js.
        (The output is written to stdout in js/src/tests/testharnessreport.js.)
        """
        from wptrunner.executors.base import testharness_result_converter

        rc = output.rc
        stdout = output.out.split("\n")
        if rc != 0:
            if rc == 3:
                harness_status = "ERROR"
                harness_message = "Exit code reported exception"
            else:
                harness_status = "CRASH"
                harness_message = "Exit code reported crash"
            tests = []
        else:
            for idx, line in enumerate(stdout):
                if line.startswith("WPT OUTPUT: "):
                    msg = line[len("WPT OUTPUT: ") :]
                    data = [output.test.wpt.url] + json.loads(msg)
                    harness_status_obj, tests = testharness_result_converter(
                        output.test.wpt, data
                    )
                    harness_status = harness_status_obj.status
                    harness_message = "Reported by harness: %s" % (
                        harness_status_obj.message,
                    )
                    del stdout[idx]
                    break
            else:
                harness_status = "ERROR"
                harness_message = "No harness output found"
                tests = []
        stdout.append("Harness status: %s (%s)" % (harness_status, harness_message))

        result = cls.PASS
        results = []
        subtests = []
        expected_harness_status = output.test.wpt.expected()
        if harness_status != expected_harness_status:
            if harness_status == "CRASH":
                result = cls.CRASH
            else:
                result = cls.FAIL
        else:
            for test in tests:
                test_output = 'Subtest "%s": ' % (test.name,)
                expected = output.test.wpt.expected(test.name)
                if test.status == expected:
                    test_result = (cls.PASS, "")
                    test_output += "as expected: %s" % (test.status,)
                else:
                    test_result = (cls.FAIL, test.message)
                    result = cls.FAIL
                    test_output += "expected %s, found %s" % (expected, test.status)
                    if test.message:
                        test_output += ' (with message: "%s")' % (test.message,)
                subtests.append({
                    "test": output.test.wpt.id,
                    "subtest": test.name,
                    "status": test.status,
                    "expected": expected,
                })
                results.append(test_result)
                stdout.append(test_output)

        output.out = "\n".join(stdout) + "\n"

        wpt_results = {
            "name": output.test.wpt.id,
            "status": harness_status,
            "expected": expected_harness_status,
            "subtests": subtests,
        }

        return cls(output.test, result, results, wpt_results)

    @classmethod
    def from_output(cls, output):
        test = output.test
        result = None  # str:      overall result, see class-level variables
        results = []  # (str,str) list: subtest results (pass/fail, message)

        if test.wpt:
            return cls.from_wpt_output(output)

        out, err, rc = output.out, output.err, output.rc

        failures = 0
        passes = 0

        expected_rcs = []
        if test.path.endswith("-n.js"):
            expected_rcs.append(3)

        for line in out.split("\n"):
            if line.startswith(" FAILED!"):
                failures += 1
                msg = line[len(" FAILED! ") :]
                results.append((cls.FAIL, msg))
            elif line.startswith(" PASSED!"):
                passes += 1
                msg = line[len(" PASSED! ") :]
                results.append((cls.PASS, msg))
            else:
                m = re.match(
                    "--- NOTE: IN THIS TESTCASE, WE EXPECT EXIT CODE ((?:-|\\d)+) ---",
                    line,
                )
                if m:
                    expected_rcs.append(int(m.group(1)))

        if test.error is not None:
            expected_rcs.append(3)
            if test.error not in err:
                failures += 1
                results.append((cls.FAIL, f"Expected uncaught error: {test.error}"))

        if rc and rc not in expected_rcs:
            if rc == 3:
                result = cls.FAIL
            else:
                result = cls.CRASH
        elif (rc or passes > 0) and failures == 0:
            result = cls.PASS
        else:
            result = cls.FAIL

        return cls(test, result, results)


class TestDuration:
    def __init__(self, test, duration):
        self.test = test
        self.duration = duration


class ResultsSink:
    def __init__(self, testsuite, options, testcount):
        self.options = options
        self.fp = options.output_fp
        if self.options.format == "automation":
            self.slog = TestLogger(testsuite)
            self.slog.suite_start()

        self.wptreport = None
        if self.options.wptreport:
            try:
                from .wptreport import WptreportHandler

                self.wptreport = WptreportHandler(self.options.wptreport)
                self.wptreport.suite_start()
            except ImportError:
                pass

        self.groups = {}
        self.output_dict = {}
        self.counts = {"PASS": 0, "FAIL": 0, "TIMEOUT": 0, "SKIP": 0}
        self.slow_tests = []
        self.n = 0

        if options.hide_progress:
            self.pb = NullProgressBar()
        else:
            fmt = [
                {"value": "PASS", "color": "green"},
                {"value": "FAIL", "color": "red"},
                {"value": "TIMEOUT", "color": "blue"},
                {"value": "SKIP", "color": "brightgray"},
            ]
            self.pb = ProgressBar(testcount, fmt)

    def push(self, output):
        if self.options.show_slow and output.dt >= self.options.slow_test_threshold:
            self.slow_tests.append(TestDuration(output.test, output.dt))
        if output.timed_out:
            self.counts["TIMEOUT"] += 1
        if isinstance(output, NullTestOutput):
            if self.options.format == "automation":
                self.print_automation_result(
                    "TEST-KNOWN-FAIL", output.test, time=output.dt, skip=True
                )
            self.counts["SKIP"] += 1
            self.n += 1
        else:
            result = TestResult.from_output(output)

            if self.wptreport is not None and result.wpt_results:
                self.wptreport.test(result.wpt_results, output.dt)

            tup = (result.result, result.test.expect, result.test.random)
            dev_label = self.LABELS[tup][1]

            if self.options.check_output:
                if output.test.path in self.output_dict.keys():
                    if self.output_dict[output.test.path] != output:
                        self.counts["FAIL"] += 1
                        self.print_automation_result(
                            "TEST-UNEXPECTED-FAIL",
                            result.test,
                            time=output.dt,
                            message="Same test with different flag producing different output",
                        )
                else:
                    self.output_dict[output.test.path] = output

            if output.timed_out:
                dev_label = "TIMEOUTS"
            self.groups.setdefault(dev_label, []).append(result)

            if dev_label == "REGRESSIONS":
                show_output = (
                    self.options.show_output or not self.options.no_show_failed
                )
            elif dev_label == "TIMEOUTS":
                show_output = self.options.show_output
            else:
                show_output = self.options.show_output and not self.options.failed_only

            if dev_label in ("REGRESSIONS", "TIMEOUTS"):
                show_cmd = self.options.show_cmd
            else:
                show_cmd = self.options.show_cmd and not self.options.failed_only

            if show_output or show_cmd:
                self.pb.beginline()

                if show_output:
                    print(
                        f"## {output.test.path}: rc = {output.rc:d}, run time = {output.dt}",
                        file=self.fp,
                    )

                if show_cmd:
                    print(escape_cmdline(output.cmd), file=self.fp)

                if show_output:

                    def write_with_fallback(fp, data):
                        try:
                            fp.write(data)
                        except UnicodeEncodeError as e:
                            # In case the data contains something not directly
                            # encodable, use \uXXXX.
                            fp.write(f"WARNING: Falling back from exception: {e}\n")
                            fp.write("WARNING: The following output is escaped, ")
                            fp.write("and may be different than original one.\n")
                            fp.write(
                                data.encode("ascii", "namereplace").decode("ascii")
                            )

                    write_with_fallback(self.fp, output.out)
                    write_with_fallback(self.fp, output.err)

            self.n += 1

            if result.result == TestResult.PASS and not result.test.random:
                self.counts["PASS"] += 1
            elif result.test.expect and not result.test.random:
                self.counts["FAIL"] += 1
            else:
                self.counts["SKIP"] += 1

            if self.options.format == "automation":
                if result.result != TestResult.PASS and len(result.results) > 1:
                    for sub_ok, msg in result.results:
                        tup = (sub_ok, result.test.expect, result.test.random)
                        label = self.LABELS[tup][0]
                        if label == "TEST-UNEXPECTED-PASS":
                            label = "TEST-PASS (EXPECTED RANDOM)"
                        self.print_automation_result(
                            label, result.test, time=output.dt, message=msg
                        )
                tup = (result.result, result.test.expect, result.test.random)
                self.print_automation_result(
                    self.LABELS[tup][0],
                    result.test,
                    time=output.dt,
                    extra=getattr(output, "extra", None),
                )
            elif dev_label:

                def singular(label):
                    return "FIXED" if label == "FIXES" else label[:-1]

                self.pb.message(f"{singular(dev_label)} - {output.test.path}")

        self.pb.update(self.n, self.counts)

    def finish(self, completed):
        self.pb.finish(completed)
        if self.options.format == "automation":
            self.slog.suite_end()
        else:
            self.list(completed)

        if self.n != 0 and self.options.show_slow:
            self.show_slow_tests()

        if self.wptreport is not None:
            self.wptreport.suite_end()

    # Conceptually, this maps (test result x test expectation) to text labels.
    #      key   is (result, expect, random)
    #      value is (automation label, dev test category)
    LABELS = {
        (TestResult.CRASH, False, False): ("TEST-UNEXPECTED-FAIL", "REGRESSIONS"),
        (TestResult.CRASH, False, True): ("TEST-UNEXPECTED-FAIL", "REGRESSIONS"),
        (TestResult.CRASH, True, False): ("TEST-UNEXPECTED-FAIL", "REGRESSIONS"),
        (TestResult.CRASH, True, True): ("TEST-UNEXPECTED-FAIL", "REGRESSIONS"),
        (TestResult.FAIL, False, False): ("TEST-KNOWN-FAIL", ""),
        (TestResult.FAIL, False, True): ("TEST-KNOWN-FAIL (EXPECTED RANDOM)", ""),
        (TestResult.FAIL, True, False): ("TEST-UNEXPECTED-FAIL", "REGRESSIONS"),
        (TestResult.FAIL, True, True): ("TEST-KNOWN-FAIL (EXPECTED RANDOM)", ""),
        (TestResult.PASS, False, False): ("TEST-UNEXPECTED-PASS", "FIXES"),
        (TestResult.PASS, False, True): ("TEST-PASS (EXPECTED RANDOM)", ""),
        (TestResult.PASS, True, False): ("TEST-PASS", ""),
        (TestResult.PASS, True, True): ("TEST-PASS (EXPECTED RANDOM)", ""),
    }

    def list(self, completed):
        for label, results in sorted(self.groups.items()):
            if label == "":
                continue

            print(label)
            for result in results:
                print(
                    "    {}".format(
                        " ".join(
                            result.test.jitflags
                            + result.test.options
                            + [result.test.path]
                        )
                    )
                )

        if self.options.failure_file:
            failure_file = open(self.options.failure_file, "w")
            if not self.all_passed():
                if "REGRESSIONS" in self.groups:
                    for result in self.groups["REGRESSIONS"]:
                        print(result.test.path, file=failure_file)
                if "TIMEOUTS" in self.groups:
                    for result in self.groups["TIMEOUTS"]:
                        print(result.test.path, file=failure_file)
            failure_file.close()

        suffix = "" if completed else " (partial run -- interrupted by user)"
        if self.all_passed():
            print("PASS" + suffix)
        else:
            print("FAIL" + suffix)

    def show_slow_tests(self):
        threshold = self.options.slow_test_threshold
        fraction_fast = 1 - len(self.slow_tests) / self.n
        self.log_info(f"{fraction_fast * 100:5.2f}% of tests ran in under {threshold}s")

        self.log_info(f"Slowest tests that took longer than {threshold}s:")
        slow_tests = sorted(self.slow_tests, key=lambda x: x.duration, reverse=True)
        any = False
        for i in range(min(len(slow_tests), 20)):
            test = slow_tests[i]
            self.log_info(f"  {test.duration:6.2f} {test.test}")
            any = True
        if not any:
            self.log_info("None")

    def log_info(self, message):
        if self.options.format == "automation":
            self.slog.log_info(message)
        else:
            print(message)

    def all_passed(self):
        return "REGRESSIONS" not in self.groups and "TIMEOUTS" not in self.groups

    def print_automation_result(
        self, label, test, message=None, skip=False, time=None, extra=None
    ):
        result = label
        result += " | " + test.path
        args = []
        if self.options.shell_args:
            args.extend(self.options.shell_args)
        args += test.jitflags
        result += ' | (args: "{}")'.format(" ".join(args))
        if message:
            result += " | " + message
        if skip:
            result += " | (SKIP)"
        if time > self.options.timeout:
            result += " | (TIMEOUT)"
        result += f" [{time:.1f} s]"
        print(result)

        details = {"extra": extra.copy() if extra else {}}
        if self.options.shell_args:
            details["extra"]["shell_args"] = shlex.join(self.options.shell_args)
        details["extra"]["jitflags"] = test.jitflags
        if message:
            details["message"] = message
        status = "FAIL" if "TEST-UNEXPECTED" in label else "PASS"

        self.slog.test(test.path, status, time or 0, **details)
