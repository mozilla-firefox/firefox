---
name: perftest-review
description: Use when reviewing or preparing a Firefox performance-testing patch (raptor, talos, mozperftest, browsertime, perfdocs, condprofile, mozproxy recordings, perf task kinds, or the `mach try perf` selector).
---

# Perftest patch review and pre-submit check

This skill covers the perftest side of a patch: tasks, schedules, harness code, perfherder output and perfdocs. The component's reviewers own the feature semantics.

## 1. Pre-submit: run the covering python tests

Some perftest backouts came from python unit tests outside the perftest directories. A try push of perf tasks doesn't run these tests, so run them locally first.

1. List the changed files with `git diff --name-only <base>` (or `jj diff --name-only`).
2. Find each changed file in the table below. Collect all the commands from the matching rows.
3. Run each command. Write the output to `artifacts/<name>.log`, then read the log.
4. Make sure mach reports more than 0 tests for each command.
5. If a command fails, fix the failure and run the command again until it passes.
6. If a changed file has no matching row, run `./mach lint --outgoing` and say so in the report.

This step is done when each command passes, or when a failure also happens on the base revision.

| Changed path | Run |
|---|---|
| `tools/tryselect/**` (incl. `selectors/perf.py`, `selectors/perfselector/`) | `./mach python-test tools/tryselect/test` (at least `test_perf.py` and `test_mozharness_integration.py`) |
| `taskcluster/kinds/{browsertime,talos,perftest,fetch,toolchain}/**`, `taskcluster/test_configs/**`, `.cron.yml` | `./mach python-test taskcluster/gecko_taskgraph/test` and `./mach python-test taskcluster/test`, then `./mach taskgraph full` |
| `taskcluster/kinds/test/**`, `taskcluster/config.yml`, `taskcluster/gecko_taskgraph/transforms/**` | the row above, plus `./mach python-test tools/tryselect/test` |
| any other `taskcluster/**/*.py` (for example `gecko_taskgraph/morph.py`, `target_tasks.py`) | the same commands as the first taskcluster row |
| `python/mozperftest/**`, `testing/perfdocs/**`, any perfdocs `config.yml` or `index.md` | `./mach perftest-test`, then `./mach lint -l perfdocs .` |
| `testing/performance/**` | `./mach lint -l perfdocs .` |
| `testing/raptor/**` | `./mach python-test --subsuite raptor` and `./mach perftest-test` |
| `testing/talos/**` | `./mach python-test --subsuite talos` and `./mach perftest-test` |
| `testing/condprofile/**` | `./mach python-test --subsuite condprof` |
| `testing/mozharness/mozharness/mozilla/testing/{raptor,talos}.py` | `./mach python-test testing/mozharness/test` |
| `testing/mozbase/manifestparser/**` | `./mach python-test testing/mozbase/manifestparser/tests` |
| `testing/mozbase/{mozproxy,mozdevice}/**` | `./mach python-test testing/mozbase/mozproxy/tests`, `./mach python-test testing/mozbase/mozdevice/tests` |
| any `*.py` or `*.yml` touched | `./mach lint --outgoing` |

Use these commands as written. Before you suggest a mach command that isn't in this table, make sure it exists with `./mach help`.

Notes:
- The perfdocs unit tests are in `python/mozperftest/mozperftest/tests/`. They moved from `tools/lint/test/` in bug 1868773.
- The `--subsuite` commands are the same tests that the `source-test-python-{raptor,talos,condprof}` CI tasks run.

## 2. Review the patch

Read [references/checklist.md](references/checklist.md). Compare each item with the diff, and give each item one mark:
- satisfied, with the file or link that shows it
- violated, with the file and the fix
- not applicable

Before r+ or submit, make sure the patch has this evidence:
- A try push that runs the changed tasks on each touched platform. If the patch touches android, the push needs `--no-artifact`, because android tasks don't run in artifact mode.
- A PerfCompare link against a try base without the patch, if the numbers can change.
- A check of the results for 0 values, missing subtests and large shifts.

This step is done when each checklist item has a mark. Each violated item also needs a fix or a follow-up bug.

## 3. Report

Give the pre-submit commands with their results, the checklist violations by severity, and the missing evidence to ask for.
