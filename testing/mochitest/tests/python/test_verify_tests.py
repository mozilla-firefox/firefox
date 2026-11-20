# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import io
import json
from argparse import Namespace

import mozunit
import pytest
from conftest import setup_args
from mozlog.formatters import JSONFormatter
from mozlog.handlers.base import StreamHandler
from mozlog.structuredlog import StructuredLogger

FOO = "browser/base/content/test/browser_foo.js"
GROUP = f"{FOO} --verify"


@pytest.fixture
def verify(setup_test_harness, parser):
    setup_test_harness(*setup_args)
    runtests = pytest.importorskip("runtests")

    buf = io.StringIO()
    logger = StructuredLogger("mochitest_verify_tests")
    logger.add_handler(StreamHandler(buf, JSONFormatter()))
    md = runtests.MochitestDesktop("browser", {"log": logger})

    def run_tests(options):
        md.logPreamble([])
        for test in options.test_paths:
            md.log.test_start(test)
            md.log.test_end(test, "PASS")
        return 0

    md.runTests = run_tests
    md.getActiveTests = lambda options: [{"path": p} for p in options.test_paths]

    def inner(test_paths):
        options = vars(parser.parse_args([]))
        options.update({"verify": True, "test_paths": test_paths})
        md.verifyTests(Namespace(**options))
        md.message_logger.finish()
        return [json.loads(line) for line in buf.getvalue().splitlines()]

    return inner


def test_group_per_verified_test(verify):
    markers = [(r["action"], r.get("name")) for r in verify([FOO])]
    tests = [i for i, (action, _) in enumerate(markers) if action == "test_start"]
    assert tests
    assert markers.index(("group_start", GROUP)) < tests[0]
    assert tests[-1] < markers.index(("group_end", GROUP))
    steps = [
        marker
        for step in ("repeat", "standalone")
        for marker in (("group_start", step), ("group_end", step))
    ]
    assert [m for m in markers if m[0] in ("group_start", "group_end")] == [
        ("group_start", GROUP),
        *steps,
        ("group_start", "chaos"),
        *steps,
        ("group_end", "chaos"),
        ("group_end", GROUP),
    ]


if __name__ == "__main__":
    mozunit.main()
