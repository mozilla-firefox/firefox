# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Verify that re-vendoring a library reproduces what is committed.

For each moz.yaml given, re-vendors it at the revision pinned in its moz.yaml
(`./mach vendor <moz.yaml> -r <rev> --patch-mode check --force`) and confirms the
result exactly matches what is committed. This catches changes to a vendored
library (its sources or its patches) that were not re-vendored properly.

Libraries listed in the `expected-fail` file passed with --expected-fail are
inverted: they are known not to re-vendor cleanly yet, so a failure is expected (and
green). If such a library starts reproducing cleanly, the job goes orange with an
UNEXPECTED-PASS, forcing removal of the annotation -- after which it is a hard
green requirement and can't silently regress.

The whole working tree is restored between libraries, because some libraries
(e.g. libvpx) write files outside their own vendor-directory, so a per-directory
restore would leak into the next library's check.

Note on `./mach vendor ... --patch-mode check` exit codes: they are inverted for
our purposes -- 254 (from spurious_check's sys.exit(-2)) means the re-vendored result
matched, and 0 means it differed and left the tree dirty. So the reliable signal
is whether the working directory is clean afterwards; the exit code only
distinguishes "matched" (254) from "errored before re-vendoring finished" (anything
else) when the tree is clean.

Usage:
    ./mach python taskcluster/scripts/misc/verify-vendored-library.py \\
        [--expected-fail vendor-verify-expected-fail.yml] path/to/moz.yaml \\
            [path/to/other/moz.yaml ...]
"""

import argparse
import functools
import logging
import os
import re
import shutil
import subprocess
import sys

import yaml
from mozbuild.vendor.moz_yaml import load_moz_yaml
from mozversioncontrol import get_repository_object

# `./mach vendor ... --patch-mode check` exits with this code (256 + -2) when the
# re-vendored result matched the tree, via spurious_check's sys.exit(-2).
VENDOR_MATCHED_EXIT_CODE = 254

_NOISE = (
    "Press Ctrl-C",
    "Sentry is attempting",
    "Waiting up to",
    "was submitted",
    "Collecting usage",
    "Processing ping",
    "Dumping result",
)
_ERRISH = re.compile(
    r"error|abort:|fatal|not found|could not|no such|reject|traceback|"
    r"failed|exception|unknown|permission",
    re.I,
)


def extract_reason(output, rc):
    """Pull a meaningful error line out of a failed `./mach vendor` run."""
    lines = [x.rstrip() for x in output.splitlines() if x.strip()]
    cand = [x for x in lines if all(n not in x for n in _NOISE)]
    errish = [x for x in cand if _ERRISH.search(x)]
    picked = errish[-1] if errish else (cand[-1] if cand else f"exit {rc}")
    return picked.strip()[:180]


def whole_tree_restore(repo):
    """Reset the working tree to the checked-out revision (tracked + untracked).

    VCS-specific because mozversioncontrol has no whole-tree reset, and `hg purge`
    is unusable in some checkouts (an fsmonitor extension incompatibility).

    Raises if the tree is not restored, because leftovers would be reported as the
    *next* library's drift. Checking the commands is not enough on its own: they can
    succeed and still leave untracked files behind, so the state is verified too.
    """
    run = functools.partial(
        subprocess.run,
        capture_output=True,
        text=True,
        errors="replace",
        check=True,
    )
    try:
        if repo.name == "git":
            run(["git", "reset", "--hard", "HEAD"])
            run(["git", "clean", "-fd"])
        elif repo.name == "hg":
            run(["hg", "revert", "--all", "--no-backup"])
            st = run(["hg", "status", "-un"])
            for line in st.stdout.splitlines():
                f = line.strip()
                if not f or not os.path.exists(f):
                    continue
                if os.path.isdir(f):
                    shutil.rmtree(f, ignore_errors=True)
                else:
                    os.remove(f)
        else:
            raise NotImplementedError(
                f"whole_tree_restore does not support the '{repo.name}' VCS"
            )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(
            f"restoring the working tree failed: `{' '.join(exc.cmd)}` exited "
            f"{exc.returncode}: {(exc.stderr or '').strip()[:300]}"
        ) from exc

    if not repo.working_directory_clean(untracked=True):
        raise RuntimeError(
            "the working tree is still not clean after restoring it, so the next "
            "library's result could not be trusted"
        )


def restore_or_bail(repo):
    """Restore the working tree, or report and exit.

    A failed restore is not recoverable: anything left behind would be reported as
    the next library's drift, so stop with a TEST-UNEXPECTED-ERROR line rather than
    a bare traceback.
    """
    try:
        whole_tree_restore(repo)
    except Exception as exc:
        print(f"TEST-UNEXPECTED-ERROR | working-directory | {exc} (vendoring)")
        logging.error("%s", exc)
        sys.exit(1)


def re_vendor(moz_yaml, repo):
    """Re-vendor one library on an already-clean tree and classify the result.

    Returns a dict: status (reproduces|drift|error), revision, reason,
    changed_files, output. Does not restore the tree.
    """
    manifest = load_moz_yaml(moz_yaml, verify=False)
    revision = manifest["origin"]["revision"]
    command = [
        "./mach",
        "vendor",
        moz_yaml,
        "-r",
        revision,
        "--patch-mode",
        "check",
        "--force",
    ]
    logging.info("Re-vendoring %s at revision %s ...", moz_yaml, revision)
    # check=False is deliberate: `mach vendor --patch-mode check` exits 254 when the
    # re-vendored result MATCHED and 0 when it differed, so a non-zero exit is not an
    # error to raise on -- the code is classified below instead.
    # errors="replace": some libraries emit non-utf-8 bytes while vendoring, and
    # decoding must not crash the check.
    completed = subprocess.run(
        command, capture_output=True, text=True, errors="replace", check=False
    )

    if not repo.working_directory_clean(untracked=True):
        # Best-effort details only: getting the changed-file list must not crash
        # the check (mozversioncontrol decodes VCS output as utf-8, which throws
        # on e.g. binary content in a drifted file).
        try:
            changed = sorted(set(repo.get_changed_files(diff_filter="ADM", mode="all")))
        except Exception:
            changed = []
        return {
            "status": "drift",
            "revision": revision,
            "reason": (
                f"{len(changed)} file(s) differ"
                if changed
                else "re-vendored result differs"
            ),
            "changed_files": changed,
            "output": completed.stdout + completed.stderr,
        }
    if completed.returncode == VENDOR_MATCHED_EXIT_CODE:
        return {
            "status": "reproduces",
            "revision": revision,
            "reason": "",
            "changed_files": [],
            "output": "",
        }
    return {
        "status": "error",
        "revision": revision,
        "reason": extract_reason(
            completed.stdout + completed.stderr, completed.returncode
        ),
        "changed_files": [],
        "output": completed.stdout + completed.stderr,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("moz_yaml", nargs="+", help="moz.yaml file(s) to verify.")
    parser.add_argument(
        "--expected-fail",
        help="yaml file listing libraries known not to re-vendor cleanly yet.",
    )
    args = parser.parse_args()

    logging.basicConfig(format="%(message)s", level=logging.INFO)

    expected_fail = {}
    if args.expected_fail and os.path.exists(args.expected_fail):
        with open(args.expected_fail) as fh:
            expected_fail = (yaml.safe_load(fh) or {}).get("expected-fail", {}) or {}

    repo = get_repository_object(".")
    # A dirty starting tree can't be verified: the check restores the whole tree
    # between libraries, so pre-existing changes would be discarded (and would
    # otherwise surface as spurious drift). Report it like any other failure --
    # a TEST-UNEXPECTED-ERROR line so the reason shows in Treeherder instead of a
    # bare "exit status 1" -- then bail. A CI checkout always starts clean.
    if not repo.working_directory_clean(untracked=True):
        message = (
            "working tree is not clean at startup, so vendoring cannot be verified "
            "(the check restores the whole tree between libraries). Commit or stash "
            "local changes first; a CI checkout should already be clean."
        )
        print(f"TEST-UNEXPECTED-ERROR | working-directory | {message} (vendoring)")
        logging.error(message)
        sys.exit(1)
    failures = []

    restore_or_bail(repo)  # start from a known-clean tree
    for moz_yaml in args.moz_yaml:
        annotated = moz_yaml in expected_fail
        lib_dir = os.path.dirname(moz_yaml)
        # Escape hatch, appended to every failure message. Deliberately scary.
        escape = (
            f"Only if this genuinely cannot be fixed now, and WITH a filed bug, add "
            f'`{moz_yaml}: "bug NNNNNNN"` under `expected-fail:` in {args.expected_fail} -- '
            "this turns off vendoring verification for the library, so real problems "
            "will no longer be caught."
        )

        try:
            result = re_vendor(moz_yaml, repo)
        except Exception as exc:
            # A malformed moz.yaml (bad YAML, missing origin.revision, ...) can
            # raise before we get a result. Report it as this library's own
            # failure (or honor its expected-fail annotation) and move on, rather
            # than aborting the whole group and leaving the tree dirty.
            if annotated:
                logging.info(
                    "OK (expected-fail): %s errored before verification [%s]: %s",
                    moz_yaml,
                    expected_fail[moz_yaml],
                    exc,
                )
            else:
                message = (
                    f"Verifying {moz_yaml} raised an unexpected error before it could "
                    f"be checked: {exc}. Usually the moz.yaml is malformed or missing "
                    f"origin.revision. {escape}"
                )
                failures.append((moz_yaml, "UNEXPECTED-FAIL", message))
                logging.exception("UNEXPECTED-FAIL: %s", message)
            restore_or_bail(repo)
            continue

        reproduces = result["status"] == "reproduces"
        rev = result["revision"]

        if reproduces and not annotated:
            logging.info("OK: re-vendoring %s reproduces the tree.", moz_yaml)
        elif not reproduces and annotated:
            logging.info(
                "OK (expected-fail): re-vendoring %s does not reproduce the tree yet [%s].",
                moz_yaml,
                expected_fail[moz_yaml],
            )
        elif reproduces and annotated:
            message = (
                f"re-vendoring {moz_yaml} now reproduces the tree, but it is still listed under "
                f"`expected-fail` in {args.expected_fail} -- so it has been fixed. Delete the "
                f"`{moz_yaml}: ...` line from the `expected-fail:` section of "
                f"{args.expected_fail}; the library then becomes a hard requirement and any "
                "future drift will be caught."
            )
            failures.append((moz_yaml, "UNEXPECTED-PASS", message))
            logging.error("UNEXPECTED-PASS: %s", message)
        elif result["status"] == "error":
            message = (
                f"Vendoring {moz_yaml} FAILED before it could be verified: "
                f"{result['reason']}. This is a problem with the vendoring itself (the "
                f"moz.yaml, its update-actions/scripts, or the pinned revision {rev}), "
                f"not a source drift. Reproduce with `./mach vendor {moz_yaml} -r {rev} "
                f"--patch-mode check --force` and fix the cause. {escape}"
            )
            failures.append((moz_yaml, "UNEXPECTED-FAIL", message))
            logging.error("UNEXPECTED-FAIL: %s", message)
            if result["output"]:
                logging.error(
                    "--- ./mach vendor output for %s ---\n%s",
                    moz_yaml,
                    result["output"],
                )
        else:  # drift, not annotated
            message = (
                f"re-vendoring {moz_yaml} does not reproduce the committed tree: taking upstream plus "
                f"its patches produced a different tree than what is committed "
                f"({result['reason']}). Usually its vendored sources or patches were "
                f"changed without redoing the vendoring (or the vendoring tooling "
                f"changed). To fix: capture the change as a patch under {lib_dir}/"
                f"patches/ (create the dir if it doesn't exist) and add it to the "
                f"`patches:` list in {moz_yaml}, confirm with `./mach vendor {moz_yaml} "
                f"-r {rev} --patch-mode check --force`, then commit the new patch and "
                f"the {moz_yaml} change. {escape}"
            )
            failures.append((moz_yaml, "UNEXPECTED-FAIL", message))
            if result["changed_files"]:
                logging.error(
                    "Files that differ for %s:\n  %s",
                    moz_yaml,
                    "\n  ".join(result["changed_files"]),
                )
            logging.error("UNEXPECTED-FAIL: %s", message)
            if result["output"]:
                logging.error(
                    "--- ./mach vendor output for %s ---\n%s",
                    moz_yaml,
                    result["output"],
                )

        restore_or_bail(repo)  # clean up for the next library

    # Treeherder builds its failure summary from lines matching known error
    # patterns; emit TEST-UNEXPECTED lines (like mozlint) so the reason shows.
    for moz_yaml, kind, message in failures:
        print(f"TEST-UNEXPECTED-ERROR | {moz_yaml} | {kind}: {message} (vendoring)")

    if failures:
        logging.error("%d/%d libraries failed.", len(failures), len(args.moz_yaml))
        sys.exit(1)
    logging.info("All %d libraries OK.", len(args.moz_yaml))


if __name__ == "__main__":
    main()
