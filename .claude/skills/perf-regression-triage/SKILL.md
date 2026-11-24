---
name: perf-regression-triage
description: >
  Handle a Perfherder performance regression bug end to end: read the alert bug, confirm
  whether the regression is real, find the cause, and iterate to a fix. Use when the user
  has a Perfherder/browsertime regression bug filed in Testing::Performance, needs to
  establish whether an alert summary is a real regression, is interpreting a PerfCompare
  baseRev/newRev comparison, or asks what
  to do about a patch of theirs that regressed a benchmark.
  Not for simply running perf tests or pushing them to try — including running an alert
  summary's tests with `mach try perf --alert` (use perftest) — nor for choosing or
  writing perf tests (use perftest), analyzing a profile you already have in hand
  (use profiler-analysis), SpiderMonkey microbenchmarks (use js-perf-investigation), or
  non-performance regressions such as failing tests, crashes, or build bustage.
allowed-tools:
  - Bash(./mach try perf --help:*)
  - Bash(./mach try perf --no-push:*)
  - Bash(treeherder-cli:*)
  - Bash(profiler-cli:*)
  - Bash(curl -s 'https://treeherder.mozilla.org/api/:*)
  - Bash(git log:*)
  - Bash(git show:*)
  - Bash(git status:*)
  - Bash(jj log:*)
  - Bash(hg log:*)
  - mcp__moz__get_bugzilla_bug
  - Read
  - Grep
  - Glob
---

# Triaging a Perfherder regression bug

## Tooling

Three tools cover the three kinds of evidence in a regression bug. Use them rather than
fetching URLs by hand — `WebFetch` on a Treeherder or profiler URL returns a UI shell with
no data in it.

- **The regression bug** — `mcp__moz__get_bugzilla_bug`, or the `@moz:bugzilla://bug/{id}`
  resource. There is no Bugzilla CLI; this is the supported path.
- **Push and job data** — `treeherder-cli <rev>`. Use `--perf` for performance and resource
  data, `--repo autoland` to inspect the culprit push, and `--watch --notify` to wait on a
  running try push instead of polling it.
- **Before/After profiles** — `profiler-cli`, driven through the **profiler-analysis**
  skill. If it is not installed, run `./mach bootstrap`, then restart the agent from a new
  terminal, since the current shell's PATH won't include the directory bootstrap installs
  it to.

`profiler-analysis` owns the profiler-cli protocol (run `profiler-cli guide` first, stop
the daemon when done). Hand profiles to that skill instead of reimplementing it here.

Neither CLI is guaranteed to be present, and a missing one is not a reason to stop or to
retreat to `WebFetch`. The same alert, push, and profile data is served as plain JSON by
the Treeherder REST API, and a whole triage can be run through it. Read
`references/rest-api.md` when you need that path — it also records two filters that look
like they work and silently do not.

The regression policy gives the patch author **3 business days** to acknowledge and start
investigating before the patch may be backed out. Establish early whether that clock is
running, and tell the user if it is close to expiring.

## Step 1: read the bug

Fetch it with `@moz:bugzilla://bug/{id}`. Every Perfherder alert bug carries the same
machine-readable payload. Pull out all of it before doing anything else:

- **Alert summary ID** — the `perfherder/alerts?id=NNNNN` link. Feeds `--alert` in step 2.
- **Culprit push revision** — the `pushloghtml?changeset=...` link in comment 0.
- **Base / new revisions** — `baseRev=` and `newRev=` in the PerfCompare link.
- **Regressed tests** — first column of the alert table.
- **Platform and options** — e.g. `linux2404-64-shippable`, `fission webrender`.
- **Before/After profiles** — the profiler.firefox.com links in the last column.

The culprit push usually contains several patches. Inspect it directly rather than reading
the pushlog HTML:

```
treeherder-cli <culprit-rev> --repo autoland --perf
```

Identify which patch is the user's and which files it touched (`git show <rev>` / the
linked Phabricator revision). If the push has multiple candidate patches and it is not
obvious which is responsible, say so — the confirmation push below tests the user's patch
specifically, which is what settles it.

Every field above is also in the alert summary's JSON, which is worth fetching when the
bug's table is ambiguous or `treeherder-cli` is missing: see `references/rest-api.md`.

Summarize for the user: how big the regression is, which tests, which platform, and what
the patch changed. Then move to confirmation.

## Step 2: confirm it

Do this before any investigation. A meaningful fraction of alerts do not reproduce.

The whole confirmation is **one command**, not two manual pushes. `--alert` runs exactly
the tests in the alert summary and compares your working revision against the base revision
your patch sits on, pushing both sides for you:

```
./mach try perf --alert <ALERT_ID> --rebuild 10 --no-artifact
```

Read `references/confirm.md` before running it — it covers getting the local repo onto the
right base revision, how many retriggers are needed, when to use `--artifact` instead of
`--no-artifact`, and how to read the result. The example above is for a compiled patch.

**Never push to try without explicit approval from the user.** Show the exact command, say
roughly what it will cost in CI, and wait.

Then branch on the outcome:

- **Reproduces** — continue to step 3.
- **Does not reproduce** — do not start optimizing. Go to `references/close-out.md`; this
  is likely noise or an unrelated patch in the same push.
- **Ambiguous / overlapping distributions** — more retriggers, or narrow to the single
  most-regressed test. See `references/confirm.md`.

## Step 3: find the cause

Start with the Before/After profiles already linked in the bug — they are free and
specific to the regressed test. Hand them to the **profiler-analysis** skill rather than
fetching them yourself; `WebFetch` on a profiler URL only retrieves the UI shell.

Compare against what the patch actually changed. Most Perfherder regressions on
speedometer-class benchmarks come from work added to a hot path, a lost fast path, extra
allocation, or added main-thread sync work.

## Step 4: iterate to a fix

Read `references/iterate.md`. The loop is change → narrow try push → check, and the entire
point of that file is keeping each round cheap: narrow the test set, drop to one platform,
and only re-run the full alert set for the final confirmation.

## Step 5: close out

Read `references/close-out.md` for the possible resolutions, the bug fields to set, who to
talk to when the right answer is "this regression is acceptable", and how to pass feedback
on this skill back to the people who maintain it.

## Cost discipline

This skill exists partly to keep regression work cheap, in CI and in tokens.

- One push per question. `--alert` gives base and new together; do not push twice.
- Confirm broadly once, then iterate narrowly. Full alert-set reruns are for the final
  check only.
- Do not read the reference files up front. Read the one for the step you are on.
- Read profiles through profiler-analysis, not by downloading them into context.
