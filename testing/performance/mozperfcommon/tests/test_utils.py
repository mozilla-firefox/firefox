#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from datetime import datetime, timedelta, timezone

import mozunit
import pytest
from mozperfcommon.utils import (
    UnknownFrameworkError,
    format_date,
    get_ci_revision,
    get_framework_id,
    signature_key,
)


def test_get_framework_id():
    assert get_framework_id("talos") == 1
    assert get_framework_id("browsertime") == 13
    with pytest.raises(
        UnknownFrameworkError, match="Unknown Perfherder framework: unknown"
    ):
        get_framework_id("unknown")


def test_get_ci_revision(monkeypatch):
    monkeypatch.delenv("GECKO_HEAD_REV", raising=False)
    assert get_ci_revision() is None
    monkeypatch.setenv("GECKO_HEAD_REV", "abcdef")
    assert get_ci_revision() == "abcdef"


def test_format_date_converts_to_utc():
    date = datetime(2026, 10, 1, 5, 26, 56, 123, tzinfo=timezone(timedelta(hours=2)))
    assert format_date(date) == "2026-10-01T03:26:56"


def test_format_date_treats_naive_dates_as_utc():
    assert format_date(datetime(2026, 10, 1, 5, 26, 56)) == "2026-10-01T05:26:56"


def test_signature_key():
    assert signature_key("suite", None, ["fission", "e10s"], None) == (
        "suite",
        None,
        ("e10s", "fission"),
        "",
    )
    assert signature_key("suite", "", [], "") == signature_key(
        "suite", None, None, None
    )
    assert signature_key("suite", "test", None, "firefox") == (
        "suite",
        "test",
        (),
        "firefox",
    )


if __name__ == "__main__":
    mozunit.main()
