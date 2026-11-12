# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json

from .base import BaseFormatter


class TestSummaryFormatter(BaseFormatter):
    """Passthrough formatter that emits only:
      - `group_start`, `group_end`, `suite_start`, `suite_end`, `test_start`
        and `test_end` actions
      - `test_status` actions where the status differs from the expected
        status (i.e. the subtest result was unexpected)
      - `crash`, `tsan_error` and `ubsan_error` actions
      - `log` actions of level ERROR or CRITICAL (failures not tied to a
        test, e.g. LeakSanitizer reports or harness errors)
      - `mozleak_total` actions that exceed their threshold or lack a total
        line (the leakcheck failures the TBPL formatter prints as
        TEST-UNEXPECTED-FAIL)
      - `lsan_summary` actions not marked `allowed` and `lsan_leak` actions
        without an `allowed_match` (the LeakSanitizer lines the TBPL
        formatter prints at ERROR or as TEST-UNEXPECTED-FAIL)

    All other actions are dropped.

    In addition, the fields `thread`, `pid`, `source`, `extra`, `tests`,
    `js_source`, `crashing_thread_stack` and any field whose name starts with
    `stackwalk_` are stripped from every emitted record, except that
    `tsan_error` keeps `pid`, which names the racing process rather than the
    logging one
    """

    _ALLOWED_ACTIONS = frozenset({
        "group_start",
        "group_end",
        "suite_start",
        "suite_end",
        "test_start",
        "test_end",
        "test_status",
        "crash",
        "tsan_error",
        "ubsan_error",
        "log",
        "mozleak_total",
        "lsan_summary",
        "lsan_leak",
    })
    _ALWAYS_STRIP = frozenset({
        "crashing_thread_stack",
        "extra",
        "js_source",
        "pid",
        "source",
        "tests",
        "thread",
    })

    def __call__(self, data):
        action = data.get("action")
        if action not in self._ALLOWED_ACTIONS:
            return
        if action == "test_status" and (
            "expected" not in data or data["expected"] == data.get("status")
        ):
            return
        if action == "log" and data.get("level") not in ("ERROR", "CRITICAL"):
            return
        if action == "mozleak_total" and not self._mozleak_total_failed(data):
            return
        if action == "lsan_summary" and data.get("allowed"):
            return
        if action == "lsan_leak" and data.get("allowed_match"):
            return

        strip = self._ALWAYS_STRIP
        if action == "tsan_error":
            strip = strip - {"pid"}
        data = {
            k: v
            for k, v in data.items()
            if k not in strip and not k.startswith("stackwalk_")
        }

        return json.dumps(data) + "\n"

    @staticmethod
    def _mozleak_total_failed(data):
        """Mirror TbplFormatter.mozleak_total: a missing total is a failure
        unless the process crashed on purpose or the caller ignores it, a
        present one is a failure past its threshold."""
        leaked = data.get("bytes")
        if leaked is None:
            return not (data.get("induced_crash") or data.get("ignore_missing"))
        return leaked != 0 and leaked > data.get("threshold", 0)
