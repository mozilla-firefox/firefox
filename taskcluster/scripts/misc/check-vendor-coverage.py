# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check every vendored moz.yaml is verified by a vendor-verify task.

Turns the tree orange if a vendored moz.yaml (has a `vendoring:` section,
non-rust) is not in any task's `members` in vendor-verify.yml -- so a newly added
vendored library can't silently escape the re-vendoring checks. Also flags stale
entries, in `members` or in the expected-fail annotations, whose moz.yaml no longer
exists or is no longer vendored. Run as a source-test task gated on moz.yaml
+ config changes.
"""

import os
import sys

import yaml

TASKS = "taskcluster/kinds/source-test/vendor-verify.yml"
EXPECTED_FAIL = "taskcluster/scripts/misc/vendor-verify-expected-fail.yml"


def vendored_moz_yamls():
    found = set()
    for dirpath, dirnames, filenames in os.walk("."):
        dirnames[:] = [
            d
            for d in dirnames
            if d not in (".hg", ".git", "node_modules", "__pycache__")
            and not d.startswith("obj-")
        ]
        if "moz.yaml" not in filenames:
            continue
        path = os.path.join(dirpath, "moz.yaml").replace("./", "", 1)
        try:
            with open(path) as fh:
                data = yaml.safe_load(fh) or {}
        except Exception:
            continue
        vendoring = data.get("vendoring") or {}
        # rust flavor is covered by the separate `rust` vendoring task.
        if vendoring and vendoring.get("flavor") != "rust":
            found.add(path)
    return found


def main():
    with open(TASKS) as fh:
        # Every top-level key in a `tasks-from` file is a task, except the defaults.
        tasks = yaml.safe_load(fh) or {}
    tasks.pop("task-defaults", None)
    with open(EXPECTED_FAIL) as fh:
        annotations = (yaml.safe_load(fh) or {}).get("expected-fail") or {}

    verified = {m for task in tasks.values() for m in (task.get("members") or [])}
    annotated = set(annotations)
    vendored = vendored_moz_yamls()

    unassigned = sorted(vendored - verified)
    stale_member = sorted(verified - vendored)
    stale_expected_fail = sorted(annotated - vendored)

    for path in unassigned:
        print(
            f"TEST-UNEXPECTED-ERROR | {path} | this vendored library (its moz.yaml has "
            f"a `vendoring:` section) is not in any task's `members` in {TASKS}, so its "
            f"vendoring would not be verified. Add `{path}` to one of them -- any task "
            "works; it only picks which job verifies it, so put it with related "
            f"libraries. See the header of {TASKS}. (vendoring-coverage)"
        )
    for path in stale_member:
        print(
            f"TEST-UNEXPECTED-ERROR | {TASKS} | a task's `members` lists `{path}`, which "
            "is no longer a vendored moz.yaml (it was removed, or its "
            f"`vendoring:` section was). Delete that entry from {TASKS}. "
            "(vendoring-coverage)"
        )
    for path in stale_expected_fail:
        print(
            f"TEST-UNEXPECTED-ERROR | {EXPECTED_FAIL} | `expected-fail` references "
            f"`{path}`, which is not a vendored moz.yaml -- likely a typo or a "
            f"removed library. Fix or delete that entry in {EXPECTED_FAIL}. "
            "(vendoring-coverage)"
        )

    if unassigned or stale_member or stale_expected_fail:
        print(
            f"vendor-verify coverage FAILED: {len(unassigned)} unassigned, "
            f"{len(stale_member)} stale `members` entries, "
            f"{len(stale_expected_fail)} stale expected-fail entries."
        )
        sys.exit(1)
    print(
        f"vendor-verify coverage OK: all {len(vendored)} vendored "
        "moz.yaml are verified by a task, and every expected-fail entry is valid."
    )


if __name__ == "__main__":
    main()
