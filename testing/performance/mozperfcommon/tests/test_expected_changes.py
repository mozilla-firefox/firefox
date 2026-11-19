#!/usr/bin/env python
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from datetime import date, datetime

import mozunit
import pytest
from mozperfcommon.expected_changes import (
    InvalidExpectedChangeError,
    find_expected_change,
    get_expected_changes_path,
    load_expected_changes,
)
from mozperfcommon.utils import signature_key

EXPECTED_CHANGE = {
    "bug": 1234567,
    "suite": "speedometer*",
    "test": "TodoMVC-*",
    "platform": "windows11-*",
    "expires": date(2026, 11, 1),
}


def test_in_tree_expected_changes_are_valid(monkeypatch):
    monkeypatch.delenv("PERF_EXPECTED_CHANGES_PATH", raising=False)
    path = get_expected_changes_path()
    assert path.parts[-4:] == (
        "testing",
        "performance",
        "common",
        "expected_changes.yml",
    )
    assert path.exists()
    assert isinstance(load_expected_changes(), list)


def test_expected_changes_path_from_environment(tmp_path, monkeypatch):
    path = tmp_path / "expected_changes.yml"
    path.write_text("- {bug: 1, suite: speedometer3, expires: 2026-11-01}\n")
    monkeypatch.setenv("PERF_EXPECTED_CHANGES_PATH", str(path))

    assert get_expected_changes_path() == path
    assert [change["bug"] for change in load_expected_changes()] == [1]


def test_load_missing_expected_changes(tmp_path, monkeypatch):
    path = tmp_path / "missing.yml"
    monkeypatch.setenv("PERF_EXPECTED_CHANGES_PATH", str(path))
    with pytest.raises(FileNotFoundError, match="PERF_EXPECTED_CHANGES_PATH"):
        load_expected_changes()


def test_load_expected_changes(tmp_path):
    path = tmp_path / "expected_changes.yml"
    path.write_text(
        "- bug: 1234567\n"
        "  suite: speedometer*\n"
        "  test: TodoMVC-*\n"
        "  platform: windows11-*\n"
        "  expires: 2026-11-01\n"
    )
    assert load_expected_changes(path) == [EXPECTED_CHANGE]


def test_load_expected_changes_from_string_path(tmp_path):
    path = tmp_path / "expected_changes.yml"
    path.write_text("- {bug: 1, suite: speedometer3, expires: 2026-11-01}\n")
    assert [change["bug"] for change in load_expected_changes(str(path))] == [1]


def test_load_empty_expected_changes(tmp_path):
    path = tmp_path / "expected_changes.yml"
    path.write_text("# Only comments.\n")
    assert load_expected_changes(path) == []


@pytest.mark.parametrize(
    "content",
    [
        "bug: 1234567\n",
        "- not a mapping\n",
        "- {bug: 1234567, suite: speedometer3}\n",
        "- {bug: 1234567, suite: speedometer3, expires: 2026-11-01, typo: 1}\n",
        "- {bug: 1234567, suite: speedometer3, expires: next week}\n",
        "- {bug: 1, suite: speedometer3, expires: 2026-11-01, extra_options: fission}\n",
        "- {bug: 1, suite: speedometer3, expires: 2026-11-01, extra_options: [1]}\n",
        "- {bug: 1, suite: speedometer3, expires: 2026-11-01 12:00:00}\n",
        "- {bug: 1, suite: 3, expires: 2026-11-01}\n",
        "- {bug: 1, suite: speedometer3, test: [a], expires: 2026-11-01}\n",
        "- {bug: 1, suite: speedometer3, platform: 1, expires: 2026-11-01}\n",
        "- {bug: 1, suite: speedometer3, application: 1, expires: 2026-11-01}\n",
        "- {bug: Bug 1, suite: speedometer3, expires: 2026-11-01}\n",
        "- {bug: true, suite: speedometer3, expires: 2026-11-01}\n",
        "- {bug: 1, suite: speedometer3, expires: 2026-11-01\n",
    ],
    ids=[
        "not-a-list",
        "not-a-mapping",
        "missing-key",
        "unknown-key",
        "bad-date",
        "extra-options-not-a-list",
        "extra-options-not-strings",
        "datetime",
        "suite-not-a-string",
        "test-not-a-string",
        "platform-not-a-string",
        "application-not-a-string",
        "bug-not-a-number",
        "bug-boolean",
        "invalid-yaml",
    ],
)
def test_load_invalid_expected_changes(tmp_path, content):
    path = tmp_path / "expected_changes.yml"
    path.write_text(content)
    with pytest.raises(InvalidExpectedChangeError):
        load_expected_changes(path)


@pytest.mark.parametrize(
    ("suite", "test", "platform", "day", "matches"),
    [
        ("speedometer3", "TodoMVC-React", "windows11-64-24h2-shippable", 1, True),
        ("speedometer3", "TodoMVC-React", "windows11-64-24h2-shippable", 2, False),
        ("speedometer3", "Editor-CodeMirror", "windows11-64-24h2-shippable", 1, False),
        ("speedometer3", None, "windows11-64-24h2-shippable", 1, False),
        ("speedometer3", "TodoMVC-React", "macosx1500-aarch64-shippable", 1, False),
        ("jetstream", "TodoMVC-React", "windows11-64-24h2-shippable", 1, False),
    ],
    ids=[
        "match",
        "expired",
        "other-test",
        "suite-value",
        "other-platform",
        "other-suite",
    ],
)
def test_find_expected_change(suite, test, platform, day, matches):
    found = find_expected_change(
        [EXPECTED_CHANGE],
        signature_key(suite, test, ["fission"], "firefox"),
        platform,
        date(2026, 11, day),
    )
    assert found == (EXPECTED_CHANGE if matches else None)


def test_load_invalid_expected_changes_names_keys(tmp_path):
    path = tmp_path / "expected_changes.yml"
    path.write_text("- {bug: 1, suite: speedometer3, typo: 1}\n")
    with pytest.raises(
        InvalidExpectedChangeError,
        match=r"missing keys \['expires'\] and unknown keys \['typo'\]",
    ):
        load_expected_changes(path)


def test_find_expected_change_with_datetime():
    key = signature_key("speedometer3", "TodoMVC-React", ["fission"], "firefox")
    platform = "windows11-64-24h2-shippable"
    assert (
        find_expected_change(
            [EXPECTED_CHANGE], key, platform, datetime(2026, 11, 1, 23, 59)
        )
        == EXPECTED_CHANGE
    )
    assert (
        find_expected_change([EXPECTED_CHANGE], key, platform, datetime(2026, 11, 2))
        is None
    )


@pytest.mark.parametrize("test", [None, "TodoMVC-React"])
def test_find_expected_change_without_test_or_platform(test):
    expected_change = {"bug": 1, "suite": "speedometer3", "expires": date(2026, 11, 1)}
    found = find_expected_change(
        [expected_change],
        signature_key("speedometer3", test, ["fission"], "firefox"),
        "any-platform",
        date(2026, 10, 1),
    )
    assert found == expected_change


@pytest.mark.parametrize(
    ("extra_options", "application", "matches"),
    [
        (["webrender", "fission"], "firefox", True),
        (["fission"], "firefox", False),
        (["fission", "webrender", "e10s"], "firefox", False),
        (["fission", "webrender"], "chrome", False),
        (["webrender", "fission", "fission"], "firefox", True),
    ],
    ids=[
        "match",
        "missing-option",
        "extra-option",
        "other-application",
        "duplicate-option",
    ],
)
def test_find_expected_change_variant(extra_options, application, matches):
    expected_change = {
        "bug": 1,
        "suite": "speedometer3",
        "extra_options": ["fission", "webrender"],
        "application": "fire*",
        "expires": date(2026, 11, 1),
    }
    found = find_expected_change(
        [expected_change],
        signature_key("speedometer3", None, extra_options, application),
        "any-platform",
        date(2026, 10, 1),
    )
    assert found == (expected_change if matches else None)


if __name__ == "__main__":
    mozunit.main()
