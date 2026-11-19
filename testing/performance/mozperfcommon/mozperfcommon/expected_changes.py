# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import os
from fnmatch import fnmatchcase
from pathlib import Path

import yaml

EXPECTED_CHANGES_RELPATH = Path(
    "testing", "performance", "common", "expected_changes.yml"
)
REQUIRED_KEYS = {"bug", "suite", "expires"}
OPTIONAL_KEYS = {"test", "platform", "extra_options", "application"}
PATTERN_KEYS = {"suite", "test", "platform", "application"}


class InvalidExpectedChangeError(Exception):
    pass


def get_expected_changes_path():
    """Return the path to the expected changes YAML.

    Uses either the in-tree location or a custom location set by the env
    variable `PERF_EXPECTED_CHANGES_PATH`.
    """
    if os.environ.get("PERF_EXPECTED_CHANGES_PATH"):
        return Path(os.environ["PERF_EXPECTED_CHANGES_PATH"])
    topsrcdir = Path(__file__).resolve().parents[4]
    return topsrcdir / EXPECTED_CHANGES_RELPATH


def load_expected_changes(path=None):
    """Validates, and returns the contents of the expected changes YAML."""
    path = Path(path) if path else get_expected_changes_path()
    if not path.exists():
        raise FileNotFoundError(
            f"Expected changes file not found at {path}, set "
            "PERF_EXPECTED_CHANGES_PATH to its location"
        )
    with path.open() as f:
        try:
            expected_changes = yaml.safe_load(f) or []
        except yaml.YAMLError as e:
            raise InvalidExpectedChangeError(f"Invalid YAML in {path}: {e}") from e

    if not isinstance(expected_changes, list):
        raise InvalidExpectedChangeError(f"{path} must contain a list")

    for expected_change in expected_changes:
        if not isinstance(expected_change, dict):
            raise InvalidExpectedChangeError(
                f"Invalid expected change: {expected_change}"
            )
        keys = set(expected_change)
        missing_keys = REQUIRED_KEYS - keys
        unknown_keys = keys - REQUIRED_KEYS - OPTIONAL_KEYS
        if missing_keys or unknown_keys:
            raise InvalidExpectedChangeError(
                f"Expected change {expected_change} has missing keys "
                f"{sorted(missing_keys)} and unknown keys {sorted(unknown_keys)}. "
                f"It must have the keys {sorted(REQUIRED_KEYS)}, and optionally "
                f"{sorted(OPTIONAL_KEYS)}"
            )
        bug = expected_change["bug"]
        if not isinstance(bug, int) or isinstance(bug, bool):
            raise InvalidExpectedChangeError(
                f"Expected change {expected_change} must have a bug number as bug"
            )
        if not all(
            isinstance(expected_change[key], str) for key in keys & PATTERN_KEYS
        ):
            raise InvalidExpectedChangeError(
                f"Expected change {expected_change} must have strings as "
                f"{sorted(PATTERN_KEYS)}"
            )
        expires = expected_change["expires"]
        if not isinstance(expires, datetime.date) or isinstance(
            expires, datetime.datetime
        ):
            raise InvalidExpectedChangeError(
                f"Expected change {expected_change} must expire on a YYYY-MM-DD date"
            )
        extra_options = expected_change.get("extra_options", [])
        if not isinstance(extra_options, list) or not all(
            isinstance(option, str) for option in extra_options
        ):
            raise InvalidExpectedChangeError(
                f"Expected change {expected_change} must have a list of strings "
                "as extra_options"
            )

    return expected_changes


def find_expected_change(expected_changes, key, platform, date):
    """Returns an expected change if it exists given the parameters.

    Checks if it's expired on `date` (a date or datetime), and if not, matches on
    the suite, test (if it exists), extra options and application of the
    signature key (see :func:`~mozperfcommon.utils.signature_key`), and then the
    platform.
    """
    if isinstance(date, datetime.datetime):
        date = date.date()
    suite, test, extra_options, application = key
    for expected_change in expected_changes:
        if (
            date <= expected_change["expires"]
            and fnmatchcase(suite, expected_change["suite"])
            and (
                "test" not in expected_change
                or (test is not None and fnmatchcase(test, expected_change["test"]))
            )
            and (
                "extra_options" not in expected_change
                or set(expected_change["extra_options"]) == set(extra_options)
            )
            and fnmatchcase(application, expected_change.get("application", "*"))
            and fnmatchcase(platform, expected_change.get("platform", "*"))
        ):
            return expected_change
    return None
