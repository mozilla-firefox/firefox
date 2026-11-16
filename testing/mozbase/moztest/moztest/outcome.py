# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import enum
from collections.abc import Mapping
from typing import Any


class Outcome(enum.IntEnum):
    """Classification of a test_status or test_end record, from the least to
    the most severe, so the worst outcome of a run is max() of its records'.

    A record carries ``expected`` only when its ``status`` differs from the
    expected one, ``known_intermittent`` lists the other statuses the test is
    known to produce, and ``will_retry`` marks a run the harness will retry.
    A test fails when its test_end or any of its test_status records is
    UNEXPECTED.
    """

    EXPECTED = 0
    KNOWN_INTERMITTENT = 1
    WILL_RETRY = 2
    UNEXPECTED = 3

    @classmethod
    def from_log(cls, data: Mapping[str, Any]) -> Outcome:
        if data.get("will_retry"):
            return cls.WILL_RETRY
        status = data.get("status")
        if "expected" not in data or data["expected"] == status:
            return cls.EXPECTED
        if status in (data.get("known_intermittent") or []):
            return cls.KNOWN_INTERMITTENT
        return cls.UNEXPECTED
