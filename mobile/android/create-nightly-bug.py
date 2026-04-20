#!/usr/bin/python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Create (or find) the Bugzilla bug for a new Nightly development cycle.

If a bug with the expected summary already exists, its ID is reused so
that retries don't produce duplicates.

Usage:
    BUGZILLA_API_KEY=<key> python3 create-nightly-bug.py <VERSION>

Set --staging to use bugzilla-dev.allizom.org for testing.
The bug ID is printed to stdout; status messages go to stderr.
"""

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

BUGZILLA_PROD_URL = "https://bugzilla.mozilla.org/rest/bug"
BUGZILLA_STAGING_URL = "https://bugzilla-dev.allizom.org/rest/bug"
PRODUCT = "Firefox for Android"
COMPONENT = "General"
SUMMARY_TEMPLATE = "Start the nightly {} development cycle"
RELEASE_CHECKLIST_URL = (
    "https://firefox-source-docs.mozilla.org/mobile/android/fenix/"
    "release-checklist.html"
    "#dev-team-starting-the-next-nightly-development-cycle"
)

TEST XYZ


def _bugzilla_request(url, data=None, api_key=None):
    if api_key:
        sep = "&" if "?" in url else "?"
        url = f"{url}{sep}api_key={api_key}"
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"} if data else {},
    )
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print(f"ERROR: Bugzilla API returned {e.code}: {body}", file=sys.stderr)
        sys.exit(1)


def find_existing_bug(base_url, version, api_key):
    params = urllib.parse.urlencode({
        "product": PRODUCT,
        "component": COMPONENT,
        "summary": SUMMARY_TEMPLATE.format(version),
    })
    result = _bugzilla_request(f"{base_url}?{params}", api_key=api_key)
    expected = SUMMARY_TEMPLATE.format(version)
    for bug in result.get("bugs", []):
        if bug["summary"] == expected:
            return bug["id"]
    return None


def create_bug(base_url, version, api_key):
    summary = SUMMARY_TEMPLATE.format(version)
    description = (
        "Follow **[[Dev team] Starting the next Nightly development cycle]"
        f"({RELEASE_CHECKLIST_URL})**"
    )
    payload = json.dumps({
        "product": PRODUCT,
        "component": COMPONENT,
        "summary": summary,
        "description": description,
        "type": "task",
        "version": "unspecified",
        "op_sys": "Android",
        "platform": "All",
    }).encode("utf-8")
    result = _bugzilla_request(base_url, data=payload, api_key=api_key)
    return result["id"]


def main():
    args = sys.argv[1:]
    staging = "--staging" in args
    if staging:
        args.remove("--staging")

    if len(args) != 1 or not args[0].isdigit():
        print("Usage: create-nightly-bug.py [--staging] VERSION", file=sys.stderr)
        print("Example: create-nightly-bug.py 150", file=sys.stderr)
        print("         create-nightly-bug.py --staging 150", file=sys.stderr)
        print("", file=sys.stderr)
        print("Requires BUGZILLA_API_KEY environment variable.", file=sys.stderr)
        sys.exit(1)

    version = args[0]
    base_url = BUGZILLA_STAGING_URL if staging else BUGZILLA_PROD_URL
    api_key = os.environ.get("BUGZILLA_API_KEY")
    if not api_key:
        print(
            "ERROR: BUGZILLA_API_KEY environment variable is not set.",
            file=sys.stderr,
        )
        sys.exit(1)

    if staging:
        print("Using staging instance (bugzilla-dev.allizom.org).", file=sys.stderr)

    existing = find_existing_bug(base_url, version, api_key)
    if existing:
        print(
            f"Bug {existing} already exists for nightly {version}.",
            file=sys.stderr,
        )
        print(existing)
        return

    bug_id = create_bug(base_url, version, api_key)
    print(f"Created bug {bug_id} for nightly {version}.", file=sys.stderr)
    print(bug_id)


if __name__ == "__main__":
    main()
