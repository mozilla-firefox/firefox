---
name: chromium-roll
description: Use when the Chromium to WebRTC DEPS autoroller (the "Auto-roll - WebRTC DEPS" cron builder) fails because tools_webrtc/autoroller/roll_deps.py can't apply a change of Chromium's DEPS, e.g. "The number of objects in ... is different between Chromium's DEPS and WebRTC's DEPS", "You need to do a manual roll" or "WebRTC DEPS entries are missing from Chromium", or when a roll CL fails on the CQ because a third_party input is "missing and no known rule to make it". Finds every blocking DEPS entry and missing dep, chooses between a DEPS-only fix and a manual Chromium roll, prepares the roll and uploads it with the usual roll commit message.
---

# Manual Chromium Roll

The
[Auto-roll - WebRTC DEPS](https://ci.chromium.org/ui/p/webrtc/builders/cron/Auto-roll%20-%20WebRTC%20DEPS)
builder rolls Chromium into WebRTC with `tools_webrtc/autoroller/roll_deps.py`.
It fails when Chromium's DEPS changes in a way it can't apply:

- `AssertionError: The number of objects in <path> is different between Chromium's DEPS and WebRTC's DEPS`:
  GCS objects were added or removed.
- `AssertionError: Old: [...] New: [...]. You need to do a manual roll`: CIPD
  packages were renamed, added or removed.
- `RollError: WebRTC DEPS entries are missing from Chromium`: Chromium removed a
  dependency that WebRTC still has.

`roll_deps.py` stops at the first failure, so there are often more blockers than
the one in the builder log.

## How roll_deps.py Rolls

- Entries that are also in Chromium's DEPS get Chromium's revision, CIPD
  versions or GCS objects. `*_version` vars get Chromium's values.
- The Chromium mirrors (`src/build`, `src/buildtools`, `src/ios`, `src/testing`,
  `src/third_party`, `src/tools`) and other WebRTC-only git deps roll to the
  HEAD of their repository.
- CIPD packages must have the same names as in Chromium. GCS objects are updated
  by index, so their number and order must match Chromium's.
- Only the `ANDROID_DEPS` section is copied from Chromium. Other entries are
  never added or deleted.

## Workflow

Run the commands from the root of a synced WebRTC checkout, with depot_tools in
`PATH`. Use `vpython3`: the system Python may fail to verify the TLS certificate
of googlesource.com.

### 1. Start From an Up-to-Date main

```bash
git checkout main && git pull
git new-branch manual_chromium_roll
```

### 2. Find Every Blocker

```bash
vpython3 agents/skills/chromium-roll/scripts/manual_roll.py diagnose
```

`diagnose` pins Chromium HEAD (or `--revision <hash>`), saves Chromium's DEPS
and the commit positions in `out/manual_roll/` (see `--state-dir`), and prints
every blocking entry with the reason and Chromium's version of the entry. It
also reports:

- GCS objects that are not in Chromium's order. `roll_deps.py` doesn't check
  this, and `gclient setdep` would silently swap their conditions.
- Missing deps: CIPD and GCS deps of Chromium that the new `//build` uses in its
  `config/` and `android/` GN files but the pinned one doesn't, and that
  WebRTC's DEPS lacks. `roll_deps.py` never adds them, so builds fail on the CQ,
  e.g. with `signflinger.jar ... missing and no known rule to make it`.
- Android deps that Chromium dropped, which `roll_deps.py` never deletes.

### 3. Choose Between a DEPS-Only Fix and a Manual Roll

For each blocker, check whether the pinned `//build` still uses the old entry,
and whether the new `//build` needs the new one:

```bash
# Pinned //build (local checkout).
grep -rn "third_party/r8/d8" build/
# //build at the pinned Chromium revision.
vpython3 agents/skills/chromium-roll/scripts/manual_roll.py cat \
    build/config/android/internal_rules.gni | grep -n "r8/"
```

- **DEPS-only fix**: only if no blocker changes what the pinned `//build` uses,
  e.g. a missing dep, or a new GCS object that only newer toolchains need. Once
  it lands, the autoroller rolls on its next run. Entries must keep matching the
  pinned revisions, so a new GCS object must be for the current toolchain (see
  [Metadata of a GCS Object](#metadata-of-a-gcs-object)).
- **Manual roll**: in all other cases, and when in doubt. The DEPS fixes must
  land in the same CL as the `src/build` roll. Examples: a removed dep that the
  pinned `//build` still uses (`src/third_party/r8/d8/cipd`), or a new SDK
  platform package (`platforms/android-37.0` to `platforms/android-37.2`).

### 4. Make DEPS Match Chromium

Use the Chromium entries printed by `diagnose`:

- **CIPD**: use Chromium's package names, versions and condition. A per-platform
  package (`.../${{platform}}`) may only exist for the hosts that Chromium's
  condition allows.
- **GCS**: add or remove objects so that the list matches Chromium's, in the
  same order. Copy new objects with their condition, and keep the conditions of
  existing objects, which may differ from Chromium's on purpose.
- **Removed deps** and **dropped Android deps**: delete them (manual roll only).
- **Missing deps**: copy Chromium's entry. This is safe before the roll too,
  since the pinned `//build` doesn't use them.

Then rerun `diagnose` with the `--revision` it printed, until it reports no
blockers and no missing deps. For a DEPS-only fix, skip to step 6.

### 5. Roll

```bash
vpython3 agents/skills/chromium-roll/scripts/manual_roll.py apply
```

`apply` runs `roll_deps.py`'s own logic (`CalculateChangedDeps` and
`UpdateDepsFile`) against the pinned Chromium DEPS, checks that DEPS then
matches Chromium, and writes the usual roll commit message to
`out/manual_roll/commit_message.txt`. Unlike `roll_deps.py` run without
`--dry-run`, it doesn't create a branch, commit or upload.

### 6. Commit and Upload

For a manual roll, use the roll commit message as is:

```bash
git commit -a -F out/manual_roll/commit_message.txt
git cl upload --force --commit-description=+ -d
```

- To turn an existing CL into the roll, append its `Change-Id:` footer to the
  message after a blank line, and use `git commit -a --amend -F ...` instead.
- `--force` skips the prompts, `--commit-description=+` makes the commit message
  the CL description, and `-d` starts a CQ dry run.
- A roll can span weeks of Chromium changes. Fix any compile or test failure in
  the same CL.

For a DEPS-only fix, write a regular CL description (see the `cl-description`
skill).

## Metadata of a GCS Object

A DEPS-only fix that adds a GCS object needs its sha256, size and generation.
`GetDepsObjectInfo` downloads the object to compute them:

```bash
PATH="$PWD/third_party/depot_tools:$PATH" vpython3 -c '
import sys
sys.path.insert(0, "tools/clang/scripts")
import sync_deps
print(sync_deps.GetDepsObjectInfo("Linux_x64/<object>.tar.xz"))'
```

It prints `object_name,sha256sum,size_bytes,generation`.

## Gotchas

- `git ls-remote` (e.g. through an `sso://` URL rewrite) can return a Chromium
  commit that gitiles only serves minutes later, with a 404 meanwhile.
  `diagnose` pins through gitiles instead.
- `urllib` waits forever by default; the script uses timeouts and retries.
- `roll_deps.UpdateDepsFile` fails for deps that aren't checked out locally,
  e.g. Android deps on a Mac. `gclient setdep` only edits DEPS, so `apply` skips
  that check.
- The missing deps check only reads the GN files of `//build/config` and
  `//build/android`. If the CQ fails with
  `"../../third_party/<path>", needed by ..., missing and no known rule to make it`,
  find the dep in Chromium's DEPS (saved as `out/manual_roll/DEPS.chromium`) and
  copy its entry.
