---
name: perftest
description: >
  Run Firefox performance tests locally or in CI. Use when the user asks how to run a
  perf test, needs the mach invocation for Raptor, Talos, MozPerftest, AWSY, or
  browsertime, wants to push perf tests to try, wants to run the tests belonging to an
  alert summary (`mach try perf --alert <id>`), or mentions `mach try perf`,
  `mach perftest`, `mach raptor`, or `mach talos-test`. Covers picking the right
  harness, finding a test's name, and the local-vs-CI tradeoff.
  Not for triaging or root-causing a Perfherder regression bug — confirming whether a
  regression is real, identifying the culprit patch, interpreting a PerfCompare result,
  or deciding backout vs fix-forward (use perf-regression-triage). Not for analyzing an
  existing Firefox profile (use profiler-analysis) or SpiderMonkey/JS engine
  benchmarking (use js-perf-investigation).
allowed-tools:
  - Bash(./mach perftest --help:*)
  - Bash(./mach raptor --help:*)
  - Bash(./mach raptor --print-tests:*)
  - Bash(./mach talos-test --help:*)
  - Bash(./mach talos-test --print-tests:*)
  - Bash(./mach talos-test --print-suites:*)
  - Bash(./mach awsy-test --help:*)
  - Bash(./mach try perf --help:*)
  - Bash(./mach try perf --no-push:*)
  - Read
  - Grep
  - Glob
---

# Running Firefox performance tests

Any of these tests can be run locally. Whether a local run will *answer the
question* is a separate matter — check that first.

## Local or CI? Check the platform before running anything

**If the work is about a regression, compare the alert's platform to the machine
you are on.** A performance alert is platform-specific: the platform is in the
bug title and in the `Platform` column of the alert summary table. If they don't
match, a local run cannot reproduce the regression, and a clean local result
means nothing.

| Situation | Do this |
|---|---|
| Alert platform ≠ your OS (e.g. Linux alert, you're on macOS) | Push to CI. A local run is not evidence. |
| Alert is on Android | Push to CI unless you have the device/emulator set up. |
| Alert platform = your OS | Local run is worth trying, but treat it as directional only. |
| No regression involved — writing a test, debugging a harness | Run locally. |

Say this out loud to the user when it applies. Someone on a Mac chasing a Linux
alert will otherwise spend an afternoon on local runs that cannot show the
regression, and read the flat result as "already fixed".

Even when the platform matches, CI hardware differs from a dev machine, so a
local number can disagree with CI in both directions. Use CI to decide whether a
patch regressed or fixed something; use local runs to iterate quickly on a fix
and to debug the test itself.

`./mach try perf --alert <ID>` is the shortest path when the platform doesn't
match — it runs what alerted, on the platform it alerted on. To target platforms
by hand, `--platforms` accepts `linux`, `macosx`, `windows`, `android`,
`android-a55`, and `desktop`.

## Pick the harness

| Harness | Command | What it covers |
|---|---|---|
| Raptor (incl. browsertime) | `./mach raptor` | Page load, benchmarks (speedometer, etc.), most desktop + mobile |
| Talos | `./mach talos-test` | Older desktop-only suites (tp5, damp, sessionrestore) |
| MozPerftest | `./mach perftest` | Custom scripts, mobile startup, xpcshell, alert replay |
| AWSY | `./mach awsy-test` | Memory usage |

A test belongs to exactly one harness. If you don't know which, find the test in
`testing/perfdocs/generated/test-list.md` — it is generated from the in-tree
manifests and is the authoritative index.

## Run locally

A local build is required (`./mach build`), or pass an explicit binary.

```
./mach raptor -t speedometer-desktop           # Raptor benchmark suite
./mach raptor -t google-search                 # Raptor page-load test
./mach talos-test -a damp                      # Talos, by active test
./mach talos-test --suite svgr                 # Talos, by suite
./mach perftest perftest_script.js             # MozPerftest, by path
./mach perftest                                # MozPerftest, interactive picker
./mach awsy-test                               # AWSY
```

`./mach raptor -t` accepts either a suite name as printed by `--print-tests`
(`speedometer-desktop`) or an individual test defined inside that suite's TOML
(`speedometer3`, from `testing/raptor/raptor/tests/benchmarks/speedometer-desktop.toml`).
`--print-tests` only lists the suite level, so if a name from a bug or an alert
isn't in that output, grep the TOMLs under `testing/raptor/raptor/tests/` for it
before concluding it doesn't exist.

Useful across harnesses:

- `--app {firefox,chrome,geckoview,fenix,...}` — target a different browser
- `-b/--binary PATH` — test a binary other than your objdir build
- `--gecko-profile` — capture a profile during the run (then use `profiler-analysis`)

MozPerftest writes results to a top-level `artifacts/` folder by default
(`--output` to change it).

### Cut run time: always lower the post-startup delay

Raptor waits `POST_DELAY_DEFAULT = 30000` ms after each browser start before the
test begins (`testing/raptor/raptor/perftest.py`). That is 30s **per browser
cycle**, and it dominates wall-clock on short tests. Drop it to 1 ms by default:

```
./mach raptor -t google-search --post-startup-delay 1
./mach perftest test.js --browsertime-extra-options 'browsertime.post_startup_delay=1'
./mach try perf --extra-args post-startup-delay=1
```

The spelling differs per entry point — Raptor takes a real flag in ms, MozPerftest
passes it through to browsertime as a `key=value` pair (comma-separate several),
and `mach try perf` uses `--extra-args`. Talos and AWSY have no equivalent.

Use it for iterating on a fix, debugging a test, or confirming a test runs at all.
Leave it at the default when the number itself has to be trustworthy — a shorter
settle time means the browser is still warming up, which adds noise and shifts
results away from what CI measures.

For a fully custom page-load run, Raptor exposes a generic `browsertime` test:

```
./mach raptor -t browsertime \
  --browsertime-arg test_script=pageload \
  --browsertime-arg browsertime.url=https://example.com \
  --browsertime-arg iterations=3
```

`test_script` accepts `pageload`, `interactive`, or a path. This generic test is
local-only. Use `./mach raptor`, not `./mach browsertime`, when you care about
profiles — `./mach browsertime` does not symbolicate.

## Find a test's name

```
./mach raptor --print-tests
./mach talos-test --print-tests
./mach talos-test --print-suites
```

Or read the generated docs, which include per-test descriptions:
`testing/perfdocs/generated/{raptor,talos,mozperftest,awsy,test-list}.md`.

## Run in CI

`./mach try perf` is the perf-specific try selector. It shows *categories* of
tasks rather than raw task names, so you don't need to know platform strings.

```
./mach try perf                    # interactive category selector
./mach try perf -q "speedometer"   # non-interactive, query the categories
./mach try perf --no-push          # print the selected tasks, push nothing
```

Without a non-empty `-q`, an `fzf` picker opens and waits, even with `-t` or
`--no-push`. Only `--alert` skips it. Each `-q` is a fuzzy filter that selects
every category it matches, so check the result with `--no-push` first:

```
./mach try perf --no-push --platforms linux --apps firefox -q speedometer3 -q jetstream3
```

It creates **two** pushes: one with your patches, and one on the base revision
they sit on. It prints a PerfCompare link that compares them once both finish.

Flags worth knowing:

- `--show-all` / `--full` — fall back to the fuzzy selector over every task.
  Some tests (e.g. the mobile startup ones) exist only here, not in a category.
- `--single-run` — skip the base push and the comparison
- `--variants fission live-sites profiling ...` — expand the category list
- `--platforms` / `--apps` — narrow what the selector offers
- `--chrome`, `--safari`, `--custom-car` — include other browsers (off by default)
- `-t/--tests amazon -q amazon` — replace the built-in categories with one per
  named test, useful for tests with no category of their own; still needs `-q`
- `--rebuild N` — run each selected task N times
- `--extra-args post-startup-delay=1` — cut 30s per browser cycle

## Reproduce a performance alert

Given an alert summary ID from a regression bug (Perfherder's alert table):

```
./mach try perf --alert 12345      # CI: run everything that alerted, vs. base
./mach perftest 12345              # local: run the alerting tests
./mach perftest 12345 --alert-exact          # use CI's exact command/options
./mach perftest 12345 --alert-tests webaudio # only these tests from the alert
```

`--alert-exact` pulls the options from the task that triggered the alert, which
is what you want when a local run disagrees with CI.

## Gotchas

- **The base push is cached.** `--rebuild N` only applies to the first try run
  made against a given base revision. Clear it with `--clear-cache`.
- **`--no-push` still computes everything** — it is the cheap way to check what a
  category expands to before spending CI time.
- Pushing to try is outward-facing; confirm with the user before running a
  `./mach try perf` that actually pushes.
- Perf runs are slow. Redirect output to a file under `artifacts/` and read that,
  rather than piping through `tail`/`grep` and re-running.

## Reference

- `testing/performance/perftest-in-a-nutshell/perfdocs/index.md` — end-to-end
  guide from alert to fix
- `testing/performance/mach-try-perf/perfdocs/` — try perf and CompareView
- `python/mozperftest/perfdocs/` — MozPerftest running/writing/developing
