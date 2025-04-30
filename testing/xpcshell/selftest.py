#!/usr/bin/env python
#
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/
#
# These tests are run before any xpcshell test is ran when the --self-test flag
# is passed. To run these tests, run:
# ./mach xpcshell-test --self-test
# or use "mach test" with any xpcshell test file or directory:
# ./mach test testing/modules/tests/xpcshell/test_assert.js --self-test

import os
import pprint
import re
import sys
import tempfile
import unittest

import mozfile
import mozinfo
import six
from mozlog import structured
from runxpcshelltests import XPCShellTests

TEST_PASS_STRING = "TEST-PASS"
TEST_FAIL_STRING = "TEST-UNEXPECTED-FAIL"

SIMPLE_PASSING_TEST = "function run_test() { Assert.ok(true); }"
SIMPLE_FAILING_TEST = "function run_test() { Assert.ok(false); }"
SIMPLE_PREFCHECK_TEST = """
function run_test() {
  Assert.ok(Services.prefs.getBoolPref("fake.pref.to.test"));
}
"""

SIMPLE_UNCAUGHT_REJECTION_TEST = """
function run_test() {
  Promise.reject(new Error("Test rejection."));
  Assert.ok(true);
}
"""

ADD_TEST_SIMPLE = """
function run_test() { run_next_test(); }

add_test(function test_simple() {
  Assert.ok(true);
  run_next_test();
});
"""

ADD_TEST_FAILING = """
function run_test() { run_next_test(); }

add_test(function test_failing() {
  Assert.ok(false);
  run_next_test();
});
"""

ADD_TEST_UNCAUGHT_REJECTION = """
function run_test() { run_next_test(); }

add_test(function test_uncaught_rejection() {
  Promise.reject(new Error("Test rejection."));
  run_next_test();
});
"""

CHILD_TEST_PASSING = """
function run_test () { run_next_test(); }

add_test(function test_child_simple () {
  run_test_in_child("test_pass.js");
  run_next_test();
});
"""

CHILD_TEST_FAILING = """
function run_test () { run_next_test(); }

add_test(function test_child_simple () {
  run_test_in_child("test_fail.js");
  run_next_test();
});
"""

CHILD_HARNESS_SIMPLE = """
function run_test () { run_next_test(); }

add_test(function test_child_assert () {
  do_load_child_test_harness();
  do_test_pending("test child assertion");
  sendCommand("Assert.ok(true);", do_test_finished);
  run_next_test();
});
"""

CHILD_TEST_HANG = """
function run_test () { run_next_test(); }

add_test(function test_child_simple () {
  do_test_pending("hang test");
  do_load_child_test_harness();
  sendCommand("_testLogger.info('CHILD-TEST-STARTED'); " +
              + "const _TEST_FILE=['test_pass.js']; _execute_test(); ",
              do_test_finished);
  run_next_test();
});
"""

SIMPLE_LOOPING_TEST = """
function run_test () { run_next_test(); }

add_test(function test_loop () {
  do_test_pending()
});
"""

PASSING_TEST_UNICODE = rb"""
function run_test () { run_next_test(); }

add_test(function test_unicode_print () {
  Assert.equal("\u201c\u201d", "\u201c\u201d");
  run_next_test();
});
"""

ADD_TASK_SINGLE = """
function run_test() { run_next_test(); }

add_task(async function test_task() {
  await Promise.resolve(true);
  await Promise.resolve(false);
});
"""

ADD_TASK_MULTIPLE = """
function run_test() { run_next_test(); }

add_task(async function test_task() {
  await Promise.resolve(true);
});

add_task(async function test_2() {
  await Promise.resolve(true);
});
"""

ADD_TASK_REJECTED = """
function run_test() { run_next_test(); }

add_task(async function test_failing() {
  await Promise.reject(new Error("I fail."));
});
"""

ADD_TASK_REJECTED_UNDEFINED = """
function run_test() { run_next_test(); }

add_task(async function test_failing() {
  await Promise.reject();
});
"""

ADD_TASK_FAILURE_INSIDE = """
function run_test() { run_next_test(); }

add_task(async function test() {
  let result = await Promise.resolve(false);

  Assert.ok(result);
});
"""

ADD_TASK_RUN_NEXT_TEST = """
function run_test() { run_next_test(); }

add_task(function () {
  Assert.ok(true);

  run_next_test();
});
"""

ADD_TASK_STACK_TRACE = """
function run_test() { run_next_test(); }

add_task(async function this_test_will_fail() {
  for (let i = 0; i < 10; ++i) {
    await Promise.resolve();
  }
  Assert.ok(false);
});
"""

ADD_TASK_SKIP = """
add_task(async function skipMeNot1() {
  Assert.ok(true, "Well well well.");
});

add_task(async function skipMe1() {
  Assert.ok(false, "Not skipped after all.");
}).skip();

add_task(async function skipMeNot2() {
  Assert.ok(true, "Well well well.");
});

add_task(async function skipMeNot3() {
  Assert.ok(true, "Well well well.");
});

add_task(async function skipMe2() {
  Assert.ok(false, "Not skipped after all.");
}).skip();
"""

ADD_TASK_SKIPALL = """
add_task(async function skipMe1() {
  Assert.ok(false, "Not skipped after all.");
});

add_task(async function skipMe2() {
  Assert.ok(false, "Not skipped after all.");
}).skip();

add_task(async function skipMe3() {
  Assert.ok(false, "Not skipped after all.");
}).only();

add_task(async function skipMeNot() {
  Assert.ok(true, "Well well well.");
}).only();

add_task(async function skipMe4() {
  Assert.ok(false, "Not skipped after all.");
});
"""

ADD_TEST_THROW_STRING = """
function run_test() {do_throw("Passing a string to do_throw")};
"""

ADD_TEST_THROW_OBJECT = """
let error = {
  message: "Error object",
  fileName: "failure.js",
  stack: "ERROR STACK",
  toString: function() {return this.message;}
};
function run_test() {do_throw(error)};
"""

ADD_TEST_REPORT_OBJECT = """
let error = {
  message: "Error object",
  fileName: "failure.js",
  stack: "ERROR STACK",
  toString: function() {return this.message;}
};
function run_test() {do_report_unexpected_exception(error)};
"""

ADD_TEST_VERBOSE = """
function run_test() {info("a message from info")};
"""

# A test for genuine JS-generated Error objects
ADD_TEST_REPORT_REF_ERROR = """
function run_test() {
  let obj = {blah: 0};
  try {
    obj.noSuchFunction();
  }
  catch (error) {
    do_report_unexpected_exception(error);
  }
};
"""

# A test for failure to load a test due to a syntax error
LOAD_ERROR_SYNTAX_ERROR = """
function run_test(
"""

# A test for failure to load a test due to an error other than a syntax error
LOAD_ERROR_OTHER_ERROR = """
"use strict";
no_such_var = "foo"; // assignment to undeclared variable
"""

# A test that crashes outright.
TEST_CRASHING = """
function run_test () {
  const { ctypes } = ChromeUtils.importESModule(
    "resource://gre/modules/ctypes.sys.mjs"
  );
  let zero = new ctypes.intptr_t(8);
  let badptr = ctypes.cast(zero, ctypes.PointerType(ctypes.int32_t));
  badptr.contents;
}
"""

# A test for asynchronous cleanup functions
ASYNC_CLEANUP = """
function run_test() {
  // The list of checkpoints in the order we encounter them.
  let checkpoints = [];

  // Cleanup tasks, in reverse order
  registerCleanupFunction(function cleanup_checkout() {
    Assert.equal(checkpoints.join(""), "123456");
    info("At this stage, the test has succeeded");
    do_throw("Throwing an error to force displaying the log");
  });

  registerCleanupFunction(function sync_cleanup_2() {
    checkpoints.push(6);
  });

  registerCleanupFunction(async function async_cleanup_4() {
    await undefined;
    checkpoints.push(5);
  });

  registerCleanupFunction(async function async_cleanup_3() {
    await undefined;
    checkpoints.push(4);
  });

  registerCleanupFunction(function async_cleanup_2() {
    let deferred = Promise.withResolvers();
    executeSoon(deferred.resolve);
    return deferred.promise.then(function() {
      checkpoints.push(3);
    });
  });

  registerCleanupFunction(function sync_cleanup() {
    checkpoints.push(2);
  });

  registerCleanupFunction(function async_cleanup() {
    let deferred = Promise.withResolvers();
    executeSoon(deferred.resolve);
    return deferred.promise.then(function() {
      checkpoints.push(1);
    });
  });

}
"""

# A test to check that add_test() tests run without run_test()
NO_RUN_TEST_ADD_TEST = """
add_test(function no_run_test_add_test() {
  Assert.ok(true);
  run_next_test();
});
"""

# A test to check that add_task() tests run without run_test()
NO_RUN_TEST_ADD_TASK = """
add_task(function no_run_test_add_task() {
  Assert.ok(true);
});
"""

# A test to check that both add_task() and add_test() work without run_test()
NO_RUN_TEST_ADD_TEST_ADD_TASK = """
add_test(function no_run_test_add_test() {
  Assert.ok(true);
  run_next_test();
});

add_task(function no_run_test_add_task() {
  Assert.ok(true);
});
"""

# A test to check that an empty test file without run_test(),
# add_test() or add_task() works.
NO_RUN_TEST_EMPTY_TEST = """
// This is an empty test file.
"""

NO_RUN_TEST_ADD_TEST_FAIL = """
add_test(function no_run_test_add_test_fail() {
  Assert.ok(false);
  run_next_test();
});
"""

NO_RUN_TEST_ADD_TASK_FAIL = """
add_task(function no_run_test_add_task_fail() {
  Assert.ok(false);
});
"""

NO_RUN_TEST_ADD_TASK_MULTIPLE = """
add_task(async function test_task() {
  await Promise.resolve(true);
});

add_task(async function test_2() {
  await Promise.resolve(true);
});
"""

LOAD_MOZINFO = """
function run_test() {
  Assert.notEqual(typeof mozinfo, undefined);
  Assert.notEqual(typeof mozinfo.os, undefined);
}
"""

CHILD_MOZINFO = """
function run_test () { run_next_test(); }

add_test(function test_child_mozinfo () {
  run_test_in_child("test_mozinfo.js");
  run_next_test();
});
"""

HEADLESS_TRUE = """
add_task(function headless_true() {
  Assert.equal(Services.env.get("MOZ_HEADLESS"), "1", "Check MOZ_HEADLESS");
  Assert.equal(Services.env.get("DISPLAY"), "77", "Check DISPLAY");
});
"""

HEADLESS_FALSE = """
add_task(function headless_false() {
  Assert.notEqual(Services.env.get("MOZ_HEADLESS"), "1", "Check MOZ_HEADLESS");
  Assert.notEqual(Services.env.get("DISPLAY"), "77", "Check DISPLAY");
});
"""


class XPCShellTestsTests(unittest.TestCase):
    """
    Yes, these are unit tests for a unit test harness.
    """

    def __init__(self, name):
        super(XPCShellTestsTests, self).__init__(name)
        from buildconfig import substs
        from mozbuild.base import MozbuildObject

        os.environ.pop("MOZ_OBJDIR", None)
        self.build_obj = MozbuildObject.from_environment()

        objdir = self.build_obj.topobjdir
        self.testing_modules = os.path.join(objdir, "_tests", "modules")

        if mozinfo.isMac:
            self.xpcshellBin = os.path.join(
                objdir,
                "dist",
                substs["MOZ_MACBUNDLE_NAME"],
                "Contents",
                "MacOS",
                "xpcshell",
            )
        else:
            self.xpcshellBin = os.path.join(objdir, "dist", "bin", "xpcshell")

        if sys.platform == "win32":
            self.xpcshellBin += ".exe"
        self.utility_path = os.path.join(objdir, "dist", "bin")
        self.symbols_path = None
        candidate_path = os.path.join(self.build_obj.distdir, "crashreporter-symbols")
        if os.path.isdir(candidate_path):
            self.symbols_path = candidate_path

    def setUp(self):
        self.log = six.StringIO()
        self.tempdir = tempfile.mkdtemp()
        logger = structured.commandline.setup_logging(
            "selftest%s" % id(self), {}, {"tbpl": self.log}
        )
        self.x = XPCShellTests(logger)
        self.x.harness_timeout = 30 if not mozinfo.info["ccov"] else 60

    def tearDown(self):
        mozfile.remove(self.tempdir)
        self.x.shutdownNode()

    def writeFile(self, name, contents, mode="w"):
        """
        Write |contents| to a file named |name| in the temp directory,
        and return the full path to the file.
        """
        fullpath = os.path.join(self.tempdir, name)
        with open(fullpath, mode) as f:
            f.write(contents)
        return fullpath

    def writeManifest(self, tests, prefs=[]):
        """
        Write an xpcshell.ini in the temp directory and set
        self.manifest to its pathname. |tests| is a list containing
        either strings (for test names), or tuples with a test name
        as the first element and manifest conditions as the following
        elements. |prefs| is an optional list of prefs in the form of
        "prefname=prefvalue" strings.
        """
        testlines = []
        for t in tests:
            testlines.append('["%s"]' % (t if isinstance(t, str) else t[0]))
            if isinstance(t, tuple):
                testlines.extend(t[1:])
        prefslines = []
        for p in prefs:
            # Append prefs lines as indented inside "prefs=" manifest option.
            prefslines.append('  "%s",' % p)

        val = """
[DEFAULT]
head = ""
tail = ""
prefs = [
"""
        val += "\n".join(prefslines)
        val += "]\n"
        val += "\n".join(testlines)
        self.manifest = self.writeFile("xpcshell.toml", val)

    def assertTestResult(self, expected, shuffle=False, verbose=False, headless=False):
        """
        Assert that self.x.runTests with manifest=self.manifest
        returns |expected|.
        """
        kwargs = {}
        kwargs["app_binary"] = self.app_binary
        kwargs["xpcshell"] = self.xpcshellBin
        kwargs["symbolsPath"] = self.symbols_path
        kwargs["manifest"] = self.manifest
        kwargs["mozInfo"] = mozinfo.info
        kwargs["shuffle"] = shuffle
        kwargs["verbose"] = verbose
        kwargs["headless"] = headless
        kwargs["sequential"] = True
        kwargs["testingModulesDir"] = self.testing_modules
        kwargs["utility_path"] = self.utility_path
        kwargs["repeat"] = 0
        self.assertEqual(
            expected,
            self.x.runTests(kwargs),
            msg="""Tests should have %s, log:
========
%s
========
"""
            % ("passed" if expected else "failed", self.log.getvalue()),
        )

    def _assertLog(self, s, expected):
        l = self.log.getvalue()
        self.assertEqual(
            expected,
            s in l,
            msg="""Value %s %s in log:
========
%s
========"""
            % (s, "expected" if expected else "not expected", l),
        )

    def assertInLog(self, s):
        """
        Assert that the string |s| is contained in self.log.
        """
        self._assertLog(s, True)

    def assertNotInLog(self, s):
        """
        Assert that the string |s| is not contained in self.log.
        """
        self._assertLog(s, False)

    def testPass(self):
        """
        Check that a simple test without any manifest conditions passes.
        """
        self.writeFile("test_basic.js", SIMPLE_PASSING_TEST)
        self.writeManifest(["test_basic.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testFail(self):
        """
        Check that a simple failing test without any manifest conditions fails.
        """
        self.writeFile("test_basic.js", SIMPLE_FAILING_TEST)
        self.writeManifest(["test_basic.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testPrefsInManifestVerbose(self):
        """
        Check prefs configuration option is supported in xpcshell manifests.
        """
        self.writeFile("test_prefs.js", SIMPLE_PREFCHECK_TEST)
        self.writeManifest(tests=["test_prefs.js"], prefs=["fake.pref.to.test=true"])

        self.assertTestResult(True, verbose=True)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertInLog("Per-test extra prefs will be set:")
        self.assertInLog("fake.pref.to.test=true")

    def testPrefsInManifestNonVerbose(self):
        """
        Check prefs configuration are not logged in non verbose mode.
        """
        self.writeFile("test_prefs.js", SIMPLE_PREFCHECK_TEST)
        self.writeManifest(tests=["test_prefs.js"], prefs=["fake.pref.to.test=true"])

        self.assertTestResult(True, verbose=False)
        self.assertNotInLog("Per-test extra prefs will be set:")
        self.assertNotInLog("fake.pref.to.test=true")

    @unittest.skipIf(
        mozinfo.isWin or not mozinfo.info.get("debug"),
        "We don't have a stack fixer on hand for windows.",
    )
    def testAssertStack(self):
        """
        When an assertion is hit, we should produce a useful stack.
        """
        self.writeFile(
            "test_assert.js",
            """
          add_test(function test_asserts_immediately() {
            Components.classes["@mozilla.org/xpcom/debug;1"]
                      .getService(Components.interfaces.nsIDebug2)
                      .assertion("foo", "assertion failed", "test.js", 1)
            run_next_test();
          });
        """,
        )

        self.writeManifest(["test_assert.js"])
        self.assertTestResult(False)

        self.assertInLog("###!!! ASSERTION")
        log_lines = self.log.getvalue().splitlines()
        line_pat = r"#\d\d:"
        unknown_pat = r"#\d\d\: \?\?\?\[.* \+0x[a-f0-9]+\]"
        self.assertFalse(
            any(re.search(unknown_pat, line) for line in log_lines),
            "An stack frame without symbols was found in\n%s"
            % pprint.pformat(log_lines),
        )
        self.assertTrue(
            any(re.search(line_pat, line) for line in log_lines),
            "No line resembling a stack frame was found in\n%s"
            % pprint.pformat(log_lines),
        )

    def testChildPass(self):
        """
        Check that a simple test running in a child process passes.
        """
        self.writeFile("test_pass.js", SIMPLE_PASSING_TEST)
        self.writeFile("test_child_pass.js", CHILD_TEST_PASSING)
        self.writeManifest(["test_child_pass.js"])

        self.assertTestResult(True, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertInLog("CHILD-TEST-STARTED")
        self.assertInLog("CHILD-TEST-COMPLETED")
        self.assertNotInLog(TEST_FAIL_STRING)

    def testChildFail(self):
        """
        Check that a simple failing test running in a child process fails.
        """
        self.writeFile("test_fail.js", SIMPLE_FAILING_TEST)
        self.writeFile("test_child_fail.js", CHILD_TEST_FAILING)
        self.writeManifest(["test_child_fail.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("CHILD-TEST-STARTED")
        self.assertInLog("CHILD-TEST-COMPLETED")
        self.assertNotInLog(TEST_PASS_STRING)

    def testChildHang(self):
        """
        Check that incomplete output from a child process results in a
        test failure.
        """
        self.writeFile("test_pass.js", SIMPLE_PASSING_TEST)
        self.writeFile("test_child_hang.js", CHILD_TEST_HANG)
        self.writeManifest(["test_child_hang.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("CHILD-TEST-STARTED")
        self.assertNotInLog("CHILD-TEST-COMPLETED")
        self.assertNotInLog(TEST_PASS_STRING)

    def testChild(self):
        """
        Checks that calling do_load_child_test_harness without run_test_in_child
        results in a usable test state. This test has a spurious failure when
        run using |mach python-test|. See bug 1103226.
        """
        self.writeFile("test_child_assertions.js", CHILD_HARNESS_SIMPLE)
        self.writeManifest(["test_child_assertions.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testSkipForAddTest(self):
        """
        Check that add_test is skipped if |skip_if| condition is true
        """
        self.writeFile(
            "test_skip.js",
            """
add_test({
  skip_if: () => true,
}, function test_should_be_skipped() {
  Assert.ok(false);
  run_next_test();
});
""",
        )
        self.writeManifest(["test_skip.js"])
        self.assertTestResult(True, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertInLog("TEST-SKIP")
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNotSkipForAddTask(self):
        """
        Check that add_task is not skipped if |skip_if| condition is false
        """
        self.writeFile(
            "test_not_skip.js",
            """
add_task({
  skip_if: () => false,
}, function test_should_not_be_skipped() {
  Assert.ok(true);
});
""",
        )
        self.writeManifest(["test_not_skip.js"])
        self.assertTestResult(True, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog("TEST-SKIP")
        self.assertNotInLog(TEST_FAIL_STRING)

    def testSkipForAddTask(self):
        """
        Check that add_task is skipped if |skip_if| condition is true
        """
        self.writeFile(
            "test_skip.js",
            """
add_task({
  skip_if: () => true,
}, function test_should_be_skipped() {
  Assert.ok(false);
});
""",
        )
        self.writeManifest(["test_skip.js"])
        self.assertTestResult(True, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertInLog("TEST-SKIP")
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNotSkipForAddTest(self):
        """
        Check that add_test is not skipped if |skip_if| condition is false
        """
        self.writeFile(
            "test_not_skip.js",
            """
add_test({
  skip_if: () => false,
}, function test_should_not_be_skipped() {
  Assert.ok(true);
  run_next_test();
});
""",
        )
        self.writeManifest(["test_not_skip.js"])
        self.assertTestResult(True, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog("TEST-SKIP")
        self.assertNotInLog(TEST_FAIL_STRING)

    def testSyntaxError(self):
        """
        Check that running a test file containing a syntax error produces
        a test failure and expected output.
        """
        self.writeFile("test_syntax_error.js", '"')
        self.writeManifest(["test_syntax_error.js"])

        self.assertTestResult(False, verbose=True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testUnicodeInAssertMethods(self):
        """
        Check that passing unicode characters through an assertion method works.
        """
        self.writeFile("test_unicode_assert.js", PASSING_TEST_UNICODE, mode="wb")
        self.writeManifest(["test_unicode_assert.js"])

        self.assertTestResult(True, verbose=True)

    @unittest.skipIf(
        "MOZ_AUTOMATION" in os.environ,
        "Timeout code path occasionally times out (bug 1098121)",
    )
    def testHangingTimeout(self):
        """
        Check that a test that never finishes results in the correct error log.
        """
        self.writeFile("test_loop.js", SIMPLE_LOOPING_TEST)
        self.writeManifest(["test_loop.js"])

        old_timeout = self.x.harness_timeout
        self.x.harness_timeout = 1

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog("TEST-UNEXPECTED-TIMEOUT")

        self.x.harness_timeout = old_timeout

    def testPassFail(self):
        """
        Check that running more than one test works.
        """
        self.writeFile("test_pass.js", SIMPLE_PASSING_TEST)
        self.writeFile("test_fail.js", SIMPLE_FAILING_TEST)
        self.writeManifest(["test_pass.js", "test_fail.js"])

        self.assertTestResult(False)
        self.assertEqual(2, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertInLog(TEST_FAIL_STRING)

    def testSkip(self):
        """
        Check that a simple failing test skipped in the manifest does
        not cause failure.
        """
        self.writeFile("test_basic.js", SIMPLE_FAILING_TEST)
        self.writeManifest([("test_basic.js", "skip-if = true")])
        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertNotInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testKnownFail(self):
        """
        Check that a simple failing test marked as known-fail in the manifest
        does not cause failure.
        """
        self.writeFile("test_basic.js", SIMPLE_FAILING_TEST)
        self.writeManifest([("test_basic.js", "fail-if = true")])
        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(1, self.x.todoCount)
        self.assertInLog("TEST-FAIL")
        # This should be suppressed because the harness doesn't include
        # the full log from the xpcshell run when things pass.
        self.assertNotInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testUnexpectedPass(self):
        """
        Check that a simple failing test marked as known-fail in the manifest
        that passes causes an unexpected pass.
        """
        self.writeFile("test_basic.js", SIMPLE_PASSING_TEST)
        self.writeManifest([("test_basic.js", "fail-if = true")])
        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        # From the outer (Python) harness
        self.assertInLog("TEST-UNEXPECTED-PASS")
        self.assertNotInLog("TEST-KNOWN-FAIL")

    def testReturnNonzero(self):
        """
        Check that a test where xpcshell returns nonzero fails.
        """
        self.writeFile("test_error.js", "throw 'foo'")
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testUncaughtRejection(self):
        """
        Ensure a simple test with an uncaught rejection is reported.
        """
        self.writeFile(
            "test_simple_uncaught_rejection.js", SIMPLE_UNCAUGHT_REJECTION_TEST
        )
        self.writeManifest(["test_simple_uncaught_rejection.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("test_simple_uncaught_rejection.js:3:18")
        self.assertInLog("Test rejection.")
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTestSimple(self):
        """
        Ensure simple add_test() works.
        """
        self.writeFile("test_add_test_simple.js", ADD_TEST_SIMPLE)
        self.writeManifest(["test_add_test_simple.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)

    def testCrashLogging(self):
        """
        Test that a crashing test process logs a failure.
        """
        self.writeFile("test_crashes.js", TEST_CRASHING)
        self.writeManifest(["test_crashes.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        if mozinfo.info.get("crashreporter"):
            self.assertInLog("\nPROCESS-CRASH")

    def testLogCorrectFileName(self):
        """
        Make sure a meaningful filename and line number is logged
        by a passing test.
        """
        self.writeFile("test_add_test_simple.js", ADD_TEST_SIMPLE)
        self.writeManifest(["test_add_test_simple.js"])

        self.assertTestResult(True, verbose=True)
        self.assertInLog("true == true")
        self.assertNotInLog("[Assert.ok :")
        self.assertInLog("[test_simple : 5]")

    def testAddTestFailing(self):
        """
        Ensure add_test() with a failing test is reported.
        """
        self.writeFile("test_add_test_failing.js", ADD_TEST_FAILING)
        self.writeManifest(["test_add_test_failing.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTestUncaughtRejection(self):
        """
        Ensure add_test() with an uncaught rejection is reported.
        """
        self.writeFile(
            "test_add_test_uncaught_rejection.js", ADD_TEST_UNCAUGHT_REJECTION
        )
        self.writeManifest(["test_add_test_uncaught_rejection.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTaskTestSingle(self):
        """
        Ensure add_test_task() with a single passing test works.
        """
        self.writeFile("test_add_task_simple.js", ADD_TASK_SINGLE)
        self.writeManifest(["test_add_task_simple.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)

    def testAddTaskTestMultiple(self):
        """
        Ensure multiple calls to add_test_task() work as expected.
        """
        self.writeFile("test_add_task_multiple.js", ADD_TASK_MULTIPLE)
        self.writeManifest(["test_add_task_multiple.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)

    def testAddTaskTestRejected(self):
        """
        Ensure rejected task reports as failure.
        """
        self.writeFile("test_add_task_rejected.js", ADD_TASK_REJECTED)
        self.writeManifest(["test_add_task_rejected.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTaskTestRejectedUndefined(self):
        """
        Ensure rejected task with undefined reason reports as failure and does not hang.
        """
        self.writeFile(
            "test_add_task_rejected_undefined.js", ADD_TASK_REJECTED_UNDEFINED
        )
        self.writeManifest(["test_add_task_rejected_undefined.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertNotInLog("TEST-UNEXPECTED-TIMEOUT")

    def testAddTaskTestFailureInside(self):
        """
        Ensure tests inside task are reported as failures.
        """
        self.writeFile("test_add_task_failure_inside.js", ADD_TASK_FAILURE_INSIDE)
        self.writeManifest(["test_add_task_failure_inside.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTaskRunNextTest(self):
        """
        Calling run_next_test() from inside add_task() results in failure.
        """
        self.writeFile("test_add_task_run_next_test.js", ADD_TASK_RUN_NEXT_TEST)
        self.writeManifest(["test_add_task_run_next_test.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)

    def testAddTaskStackTrace(self):
        """
        Ensuring that calling Assert.ok(false) from inside add_task()
        results in a human-readable stack trace.
        """
        self.writeFile("test_add_task_stack_trace.js", ADD_TASK_STACK_TRACE)
        self.writeManifest(["test_add_task_stack_trace.js"])

        self.assertTestResult(False)
        self.assertInLog("this_test_will_fail")
        self.assertInLog("run_next_test")
        self.assertInLog("run_test")

    def testAddTaskSkip(self):
        self.writeFile("test_tasks_skip.js", ADD_TASK_SKIP)
        self.writeManifest(["test_tasks_skip.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)

    def testAddTaskSkipAll(self):
        self.writeFile("test_tasks_skipall.js", ADD_TASK_SKIPALL)
        self.writeManifest(["test_tasks_skipall.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)

    def testMissingHeadFile(self):
        """
        Ensure that missing head file results in fatal failure.
        """
        self.writeFile("test_basic.js", SIMPLE_PASSING_TEST)
        self.writeManifest([("test_basic.js", 'head = "missing.js"')])

        raised = False

        try:
            # The actual return value is never checked because we raise.
            self.assertTestResult(True)
        except Exception as ex:
            raised = True
            self.assertEqual(str(ex)[0:9], "head file")

        self.assertTrue(raised)

    def testRandomExecution(self):
        """
        Check that random execution doesn't break.
        """
        manifest = []
        for i in range(0, 10):
            filename = "test_pass_%d.js" % i
            self.writeFile(filename, SIMPLE_PASSING_TEST)
            manifest.append(filename)

        self.writeManifest(manifest)
        self.assertTestResult(True, shuffle=True)
        self.assertEqual(10, self.x.testCount)
        self.assertEqual(10, self.x.passCount)

    def testDoThrowString(self):
        """
        Check that do_throw produces reasonable messages when the
        input is a string instead of an object
        """
        self.writeFile("test_error.js", ADD_TEST_THROW_STRING)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("Passing a string to do_throw")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoThrowForeignObject(self):
        """
        Check that do_throw produces reasonable messages when the
        input is a generic object with 'filename', 'message' and 'stack' attributes
        but 'object instanceof Error' returns false
        """
        self.writeFile("test_error.js", ADD_TEST_THROW_OBJECT)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("failure.js")
        self.assertInLog("Error object")
        self.assertInLog("ERROR STACK")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoReportForeignObject(self):
        """
        Check that do_report_unexpected_exception produces reasonable messages when the
        input is a generic object with 'filename', 'message' and 'stack' attributes
        but 'object instanceof Error' returns false
        """
        self.writeFile("test_error.js", ADD_TEST_REPORT_OBJECT)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("failure.js")
        self.assertInLog("Error object")
        self.assertInLog("ERROR STACK")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoReportRefError(self):
        """
        Check that do_report_unexpected_exception produces reasonable messages when the
        input is a JS-generated Error
        """
        self.writeFile("test_error.js", ADD_TEST_REPORT_REF_ERROR)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("test_error.js")
        self.assertInLog("obj.noSuchFunction is not a function")
        self.assertInLog("run_test@")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoReportSyntaxError(self):
        """
        Check that attempting to load a test file containing a syntax error
        generates details of the error in the log
        """
        self.writeFile("test_error.js", LOAD_ERROR_SYNTAX_ERROR)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("test_error.js:3")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoReportNonSyntaxError(self):
        """
        Check that attempting to load a test file containing an error other
        than a syntax error generates details of the error in the log
        """
        self.writeFile("test_error.js", LOAD_ERROR_OTHER_ERROR)
        self.writeManifest(["test_error.js"])

        self.assertTestResult(False)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertInLog("ReferenceError: assignment to undeclared variable")
        self.assertInLog("test_error.js:3")
        self.assertNotInLog(TEST_PASS_STRING)

    def testDoPrintWhenVerboseNotExplicit(self):
        """
        Check that info() and similar calls that generate output do
        not have the output when not run verbosely.
        """
        self.writeFile("test_verbose.js", ADD_TEST_VERBOSE)
        self.writeManifest(["test_verbose.js"])

        self.assertTestResult(True)
        self.assertNotInLog("a message from info")

    def testDoPrintWhenVerboseExplicit(self):
        """
        Check that info() and similar calls that generate output have the
        output shown when run verbosely.
        """
        self.writeFile("test_verbose.js", ADD_TEST_VERBOSE)
        self.writeManifest(["test_verbose.js"])
        self.assertTestResult(True, verbose=True)
        self.assertInLog("a message from info")

    def testDoPrintWhenVerboseInManifest(self):
        """
        Check that info() and similar calls that generate output have the
        output shown when 'verbose = true' is in the manifest, even when
        not run verbosely.
        """
        self.writeFile("test_verbose.js", ADD_TEST_VERBOSE)
        self.writeManifest([("test_verbose.js", "verbose = true")])

        self.assertTestResult(True)
        self.assertInLog("a message from info")

    def testAsyncCleanup(self):
        """
        Check that registerCleanupFunction handles nicely async cleanup tasks
        """
        self.writeFile("test_asyncCleanup.js", ASYNC_CLEANUP)
        self.writeManifest(["test_asyncCleanup.js"])
        self.assertTestResult(False)
        self.assertInLog('"123456" == "123456"')
        self.assertInLog("At this stage, the test has succeeded")
        self.assertInLog("Throwing an error to force displaying the log")

    def testNoRunTestAddTest(self):
        """
        Check that add_test() works fine without run_test() in the test file.
        """
        self.writeFile("test_noRunTestAddTest.js", NO_RUN_TEST_ADD_TEST)
        self.writeManifest(["test_noRunTestAddTest.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNoRunTestAddTask(self):
        """
        Check that add_task() works fine without run_test() in the test file.
        """
        self.writeFile("test_noRunTestAddTask.js", NO_RUN_TEST_ADD_TASK)
        self.writeManifest(["test_noRunTestAddTask.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNoRunTestAddTestAddTask(self):
        """
        Check that both add_test() and add_task() work without run_test()
        in the test file.
        """
        self.writeFile("test_noRunTestAddTestAddTask.js", NO_RUN_TEST_ADD_TEST_ADD_TASK)
        self.writeManifest(["test_noRunTestAddTestAddTask.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNoRunTestEmptyTest(self):
        """
        Check that the test passes on an empty file that contains neither
        run_test() nor add_test(), add_task().
        """
        self.writeFile("test_noRunTestEmptyTest.js", NO_RUN_TEST_EMPTY_TEST)
        self.writeManifest(["test_noRunTestEmptyTest.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNoRunTestAddTestFail(self):
        """
        Check that test fails on using add_test() without run_test().
        """
        self.writeFile("test_noRunTestAddTestFail.js", NO_RUN_TEST_ADD_TEST_FAIL)
        self.writeManifest(["test_noRunTestAddTestFail.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testNoRunTestAddTaskFail(self):
        """
        Check that test fails on using add_task() without run_test().
        """
        self.writeFile("test_noRunTestAddTaskFail.js", NO_RUN_TEST_ADD_TASK_FAIL)
        self.writeManifest(["test_noRunTestAddTaskFail.js"])

        self.assertTestResult(False)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(0, self.x.passCount)
        self.assertEqual(1, self.x.failCount)
        self.assertInLog(TEST_FAIL_STRING)
        self.assertNotInLog(TEST_PASS_STRING)

    def testNoRunTestAddTaskMultiple(self):
        """
        Check that multple add_task() tests work without run_test().
        """
        self.writeFile(
            "test_noRunTestAddTaskMultiple.js", NO_RUN_TEST_ADD_TASK_MULTIPLE
        )
        self.writeManifest(["test_noRunTestAddTaskMultiple.js"])

        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testMozinfo(self):
        """
        Check that mozinfo.json is loaded
        """
        self.writeFile("test_mozinfo.js", LOAD_MOZINFO)
        self.writeManifest(["test_mozinfo.js"])
        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testChildMozinfo(self):
        """
        Check that mozinfo.json is loaded in child process
        """
        self.writeFile("test_mozinfo.js", LOAD_MOZINFO)
        self.writeFile("test_child_mozinfo.js", CHILD_MOZINFO)
        self.writeManifest(["test_child_mozinfo.js"])
        self.assertTestResult(True)
        self.assertEqual(1, self.x.testCount)
        self.assertEqual(1, self.x.passCount)
        self.assertEqual(0, self.x.failCount)
        self.assertEqual(0, self.x.todoCount)
        self.assertInLog(TEST_PASS_STRING)
        self.assertNotInLog(TEST_FAIL_STRING)

    def testNotHeadlessByDefault(self):
        """
        Check that the default is not headless.
        """
        self.writeFile("test_notHeadlessByDefault.js", HEADLESS_FALSE)
        self.writeManifest(["test_notHeadlessByDefault.js"])
        self.assertTestResult(True)

    def testHeadlessWhenHeadlessExplicit(self):
        """
        Check that explicitly requesting headless works when the manifest doesn't override.
        """
        self.writeFile("test_headlessWhenExplicit.js", HEADLESS_TRUE)
        self.writeManifest(["test_headlessWhenExplicit.js"])
        self.assertTestResult(True, headless=True)

    def testHeadlessWhenHeadlessTrueInManifest(self):
        """
        Check that enabling headless in the manifest alone works.
        """
        self.writeFile("test_headlessWhenTrueInManifest.js", HEADLESS_TRUE)
        self.writeManifest([("test_headlessWhenTrueInManifest.js", "headless = true")])
        self.assertTestResult(True)

    def testNotHeadlessWhenHeadlessFalseInManifest(self):
        """
        Check that the manifest entry overrides the explicit default.
        """
        self.writeFile("test_notHeadlessWhenFalseInManifest.js", HEADLESS_FALSE)
        self.writeManifest(
            [("test_notHeadlessWhenFalseInManifest.js", "headless = false")]
        )
        self.assertTestResult(True, headless=True)


if __name__ == "__main__":
    import mozunit

    mozinfo.find_and_update_from_json()
    mozunit.main()
