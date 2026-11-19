# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from datetime import timezone

TREEHERDER_DATE_FORMAT = "%Y-%m-%dT%H:%M:%S"

# Perfherder framework ids
FRAMEWORK_IDS = {
    "talos": 1,
    "build_metrics": 2,
    "awsy": 4,
    "platform_microbench": 6,
    "js-bench": 11,
    "devtools": 12,
    "browsertime": 13,
    "mozperftest": 15,
    "fxrecord": 16,
    "telemetry": 17,
    "mozharness": 18,
}


class UnknownFrameworkError(Exception):
    """Raised when an unknown framework name is found."""

    pass


def get_ci_revision():
    """Return the revision being tested in CI, or None outside of CI."""
    return os.environ.get("GECKO_HEAD_REV")


def get_framework_id(name):
    if name not in FRAMEWORK_IDS:
        raise UnknownFrameworkError(f"Unknown Perfherder framework: {name}")
    return FRAMEWORK_IDS[name]


def format_date(date):
    """Format a datetime as the UTC date string expected by Treeherder. Naive
    datetimes are treated as UTC."""
    if date.tzinfo is None:
        date = date.replace(tzinfo=timezone.utc)
    return date.astimezone(timezone.utc).strftime(TREEHERDER_DATE_FORMAT)


def signature_key(suite, test, extra_options, application):
    """Return the key identifying a Perfherder signature on a platform. The
    test is None for suite signatures."""
    return (
        suite,
        test or None,
        tuple(sorted(extra_options or [])),
        application or "",
    )
