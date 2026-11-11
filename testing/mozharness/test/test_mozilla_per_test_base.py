import json
import os
import shutil
import tempfile
import unittest

import mozunit
from mozharness.mozilla.testing.per_test_base import SingleTestMixin


class Harness(SingleTestMixin):
    """Minimal host for the mixin: get_indexed_logs() reads these two flags."""

    def __init__(self, verify_enabled=False, per_test_coverage=False):
        super().__init__()
        self.verify_enabled = verify_enabled
        self.per_test_coverage = per_test_coverage


class TestTestsUnder(unittest.TestCase):
    TESTS = {
        os.path.join("a", "b", "test_one.js"): None,
        os.path.join("a", "b", "test_two.js"): None,
        os.path.join("a", "bb", "test_three.js"): None,
        os.path.join("c", "test_four.js"): None,
    }

    def under(self, *paths):
        return SingleTestMixin._tests_under(self.TESTS, paths)

    def test_directory_expands_to_the_tests_below_it(self):
        self.assertEqual(
            self.under("a/b"),
            [
                os.path.join("a", "b", "test_one.js"),
                os.path.join("a", "b", "test_two.js"),
            ],
        )

    def test_a_test_file_matches_itself(self):
        self.assertEqual(
            self.under("c/test_four.js"), [os.path.join("c", "test_four.js")]
        )


class TestChunkSuites(unittest.TestCase):
    def chunk(self, suites, total_chunks, this_chunk):
        harness = Harness()
        harness.suites = suites
        return harness._chunk_suites(total_chunks, this_chunk)

    def test_chunks_together_cover_each_test_once(self):
        tests = ["test_%02d.js" % i for i in range(23)]
        seen = []
        for n in range(1, 4):
            seen.extend(self.chunk({"xpcshell": list(tests)}, 3, n).get("xpcshell", []))

        self.assertEqual(sorted(seen), sorted(tests))
        self.assertEqual(len(seen), len(set(seen)))

    def test_chunk_does_not_depend_on_insertion_order(self):
        """find_modified_tests builds the suites by iterating a set, whose
        order varies between processes, so two tasks of the same push would
        otherwise slice different lists."""
        tests = ["test_%02d.js" % i for i in range(23)]

        self.assertEqual(
            self.chunk({"xpcshell": list(tests)}, 3, 2),
            self.chunk({"xpcshell": list(reversed(tests))}, 3, 2),
        )

    def test_every_chunk_gets_the_same_mix_of_suites(self):
        suites = {
            "mochitest-plain": ["m%d.js" % i for i in range(4)],
            "xpcshell": ["x%d.js" % i for i in range(6)],
        }

        self.assertEqual(
            self.chunk(dict(suites), 2, 1),
            {
                "mochitest-plain": ["m0.js", "m2.js"],
                "xpcshell": ["x0.js", "x2.js", "x4.js"],
            },
        )
        self.assertEqual(
            self.chunk(dict(suites), 2, 2),
            {
                "mochitest-plain": ["m1.js", "m3.js"],
                "xpcshell": ["x1.js", "x3.js", "x5.js"],
            },
        )

    def test_chunks_beyond_the_tests_are_empty(self):
        self.assertEqual(self.chunk({"xpcshell": ["a.js"]}, 3, 3), {})

    def test_a_single_chunk_runs_everything(self):
        suites = {"xpcshell": ["b.js", "a.js"]}

        self.assertEqual(self.chunk(suites, 1, 1), {"xpcshell": ["a.js", "b.js"]})


class TestIndexedLogs(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(suffix=".mozharness")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_verify_mode_gives_each_run_distinct_logs(self):
        harness = Harness(verify_enabled=True)
        first_raw, first_errors = harness.get_indexed_logs(self.dir, "mochitest-plain")
        second_raw, second_errors = harness.get_indexed_logs(
            self.dir, "mochitest-plain"
        )

        self.assertEqual(os.path.basename(first_raw), "mochitest-plain-test1_raw.log")
        self.assertEqual(
            os.path.basename(first_errors), "mochitest-plain-test1_errorsummary.log"
        )
        self.assertEqual(os.path.basename(second_raw), "mochitest-plain-test2_raw.log")
        self.assertEqual(
            os.path.basename(second_errors), "mochitest-plain-test2_errorsummary.log"
        )

    def test_logs_are_unindexed_outside_verify_mode(self):
        harness = Harness()
        raw, errors = harness.get_indexed_logs(self.dir, "xpcshell")

        self.assertEqual(os.path.basename(raw), "xpcshell_raw.log")
        self.assertEqual(os.path.basename(errors), "xpcshell_errorsummary.log")


class TestTestSummaryFile(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(suffix=".mozharness")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    @property
    def summary(self):
        return os.path.join(self.dir, "summary.jsonl")

    def test_suite_without_opt_in_gets_no_summary(self):
        harness = Harness()

        path = harness.query_test_summary_file(self.dir, {"run_filename": "run.py"})

        self.assertIsNone(path)
        self.assertFalse(os.path.exists(self.summary))

    def test_suite_opting_in_gets_the_task_summary(self):
        harness = Harness()

        path = harness.query_test_summary_file(self.dir, {"test_summary": True})

        self.assertEqual(path, self.summary)

    def test_stale_summary_is_replaced_once_per_task(self):
        harness = Harness(verify_enabled=True)
        with open(self.summary, "w", encoding="utf-8") as fh:
            fh.write('{"action": "test_start", "test": "stale"}\n')

        harness.query_test_summary_file(self.dir, {"test_summary": True})
        self.assertEqual(self.read_summary(), [])

        with open(self.summary, "a", encoding="utf-8") as fh:
            fh.write('{"action": "test_start", "test": "first run"}\n')
        harness.query_test_summary_file(self.dir, {"test_summary": True})
        self.assertEqual([r["test"] for r in self.read_summary()], ["first run"])

    def read_summary(self):
        with open(self.summary, encoding="utf-8") as fh:
            return [json.loads(line) for line in fh]


if __name__ == "__main__":
    mozunit.main()
