#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Check (and optionally fix) cf_status_nss on Bugzilla bugs for an NSS release.

Compares the set of bugs in the hg release against bugs already marked in
Bugzilla, then reports:
  - bugs in the release but not yet marked (need to be set)
  - bugs already marked but not in the release (may have been backed out)

Usage: python3 bugzilla_cf_status_nss.py <version> [options]

Example:
  python3 bugzilla_cf_status_nss.py 3.122
  python3 bugzilla_cf_status_nss.py 3.122 --fix
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from release_utils import get_bug_list_for_version, extract_bug_ids

BASE_URL = "https://bugzilla.mozilla.org/rest"
FIELD = "cf_status_nss"
UNSET = "---"


def bz_request(method, path, api_key, payload=None, params=None):
    url = f"{BASE_URL}{path}"
    if params:
        url += "?" + urllib.parse.urlencode(params, doseq=True)
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if api_key:
        req.add_header("X-BUGZILLA-API-KEY", api_key)
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        try:
            msg = json.loads(body).get("message", body[:300])
        except Exception:
            msg = body[:300]
        raise RuntimeError(f"HTTP {e.code}: {msg}") from None


def search_marked_bugs(version, api_key):
    """Return the set of bug IDs where cf_status_nss == version."""
    data = bz_request(
        "GET",
        "/bug",
        api_key,
        params={
            FIELD: version,
            "include_fields": "id",
        },
    )
    return {bug["id"] for bug in data.get("bugs", [])}


def fetch_field_values(bug_ids, api_key, chunk_size=20):
    """Return {bug_id: cf_status_nss_value} for the given bug IDs."""
    results = {}
    for i in range(0, len(bug_ids), chunk_size):
        chunk = bug_ids[i : i + chunk_size]
        data = bz_request(
            "GET",
            "/bug",
            api_key,
            params={
                "id": [str(b) for b in chunk],
                "include_fields": f"id,{FIELD}",
            },
        )
        for bug in data.get("bugs", []):
            results[bug["id"]] = bug.get(FIELD, UNSET)
    return results


def bulk_update(bug_ids, value, label, api_key):
    errors = []
    for bid in bug_ids:
        try:
            bz_request("PUT", f"/bug/{bid}", api_key, {FIELD: value})
            print(f"  {label} Bug {bid}")
        except Exception as e:
            print(f"  ERROR {label} Bug {bid}: {e}", file=sys.stderr)
            errors.append(bid)
    return errors


def apply_updates(to_set, to_clear, version, api_key):
    errors = []
    if to_set:
        errors += bulk_update(to_set, version, "set  ", api_key)
    if to_clear:
        errors += bulk_update(to_clear, UNSET, "clear", api_key)
    return errors


def main():
    parser = argparse.ArgumentParser(
        description="Check (and optionally fix) cf_status_nss for an NSS release.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("version", help="NSS version string, e.g. 3.122")
    parser.add_argument(
        "--fix", action="store_true", help="Set missing bugs and clear spurious ones."
    )
    parser.add_argument("--api-key", metavar="KEY", help="Bugzilla API key.")
    args = parser.parse_args()

    api_key = args.api_key or os.environ.get("BUGZILLA_API_KEY")

    version = args.version.strip()
    parts = version.split(".")
    bz_version = ".".join(parts[:2]) if len(parts) > 2 else version

    if not api_key:
        print("No Bugzilla API key found.")
        print("Generate one at: https://bugzilla.mozilla.org/userprefs.cgi?tab=apikey")
        api_key = input("API key: ").strip()
        if not api_key:
            raise SystemExit("error: no API key provided.")

    # Point releases share cf_status_nss with the base release.
    versions = [bz_version]
    if len(parts) > 2:
        versions += [f"{bz_version}.{p}" for p in range(1, int(parts[2]) + 1)]

    hg_ids = set()
    for v in versions:
        print(f"Extracting bug list for NSS {v} from hg...")
        hg_ids |= set(extract_bug_ids(get_bug_list_for_version(v)))
    print(f"  {len(hg_ids)} bug(s) in {bz_version}.x.")

    print(f"Querying Bugzilla for bugs with {FIELD} = {bz_version!r}...")
    bz_ids = search_marked_bugs(bz_version, api_key)
    print(f"  {len(bz_ids)} bug(s) already marked.")
    print()

    correct = hg_ids & bz_ids
    to_clear = sorted(bz_ids - hg_ids)

    unmarked = sorted(hg_ids - bz_ids)
    if unmarked:
        print(f"Fetching current {FIELD} values for {len(unmarked)} unmarked bug(s)...")
        current_values = fetch_field_values(unmarked, api_key)
        to_set, blocked = [], {}
        for b in unmarked:
            val = current_values.get(b, UNSET)
            if val == UNSET:
                to_set.append(b)
            else:
                blocked[b] = val
    else:
        to_set, blocked = [], {}
    print()

    print(f"  {len(correct)} already correct")
    print(f"  {len(to_set)} need to be set")
    if blocked:
        print(f"  {len(blocked)} skipped (already set to a different value)")
    print(f"  {len(to_clear)} should be cleared (not in release)")

    if to_set:
        print()
        print("Bugs to set:")
        for bid in to_set:
            print(f"  Bug {bid}")

    if blocked:
        print()
        print("Bugs skipped (already set to another value — review manually):")
        for bid, val in sorted(blocked.items()):
            print(f"  Bug {bid}  {FIELD} = {val!r}")

    if to_clear:
        print()
        print("Bugs to clear (not in release):")
        for bid in to_clear:
            print(f"  Bug {bid}")

    if not args.fix:
        if to_set or to_clear:
            print()
            print("Run with --fix to apply changes.")
        return

    if not to_set and not to_clear:
        print()
        print("Nothing to do.")
        return

    print()
    errors = apply_updates(to_set, to_clear, bz_version, api_key)
    print()
    if errors:
        print(f"{len(errors)} error(s) — see above.", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"Done. {len(to_set)} set, {len(to_clear)} cleared.")


if __name__ == "__main__":
    main()
