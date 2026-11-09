# Performance Test Batching

On autoland, nearly all performance tests only run on backstop pushes (see [Current Cadence](#current-cadence)).
The batching strategies described here evaluate other cadences: each one picks *batch pushes*, on which the whole performance suite runs, and skips performance tests on every other push.
They are only used by shadow schedulers for now, so they do not change what autoland schedules.
The work is tracked in bug 2055854.

## Current Cadence

Two steps decide whether a performance task runs on an autoland push:

1. On pushes that are not backstop pushes, the `autoland_tasks` target tasks method drops every test against a shippable `opt` build before optimization runs (`filter_shippable_tests_on_non_backstop` in {searchfox}`taskcluster/gecko_taskgraph/target_tasks.py`).
   That covers talos, awsy and most raptor tests, including every Android hardware test.
2. The remaining tasks go through their optimization strategy, a `perf-cadence-*` alias described in the next section.

As a result:

| Push | Performance tasks that run |
|---|---|
| Backstop push | All push-based performance tasks |
| Every 10th push (pushlog ID a multiple of 10) | Tier 1 raptor tests against builds that are not shippable, currently Speedometer 3 on the `nightlyasrelease` builds |
| Any other push | None |

A push is a backstop push when 20 pushes or 4 hours have passed since the last backstop push (`is_backstop` in {searchfox}`taskcluster/gecko_taskgraph/util/backstop.py`), which the decision task records in the `backstop` parameter (see [Parameters](/taskcluster/parameters.md)).
Both the push count and the time restart from the last backstop push, so a backstop triggered by time shifts the following ones, and quiet periods such as weekends get one every 4 hours.
Pushes with `DONTBUILD` in their commit message are never backstop pushes.

The decision task also computes a separate Android performance backstop, every 30 pushes or 6 hours, recorded in the `android_perftest_backstop` parameter.
It adds no runs on autoland: the tasks using it run against shippable builds, which are dropped on pushes that are not regular backstop pushes, and the Android hardware tests that run on autoland, on `android-hw-a55`, are switched to the regular backstop by the raptor transforms.

Performance tests that only run on mozilla-central or from cron are out of the scope of this page.

## How Performance Tasks Are Optimized

Performance tasks are not optimized through the strategies shared with builds and unit tests, but through dedicated aliases, so that their cadence can be replaced on its own.
Each alias behaves exactly like the strategy it wraps unless it is overridden:

| Alias | Wraps | Used by |
|---|---|---|
| `perf-cadence-backstop` | `skip-unless-backstop` | raptor at tier 2 and above outside `android-hw` platforms, raptor and mobile browsertime on `android-hw-a55`, mozperftest |
| `perf-cadence-expanded` | `skip-unless-expanded`, which runs on backstop pushes and every 10th push | raptor at tier 1 outside `android-hw` platforms |
| `perf-cadence-android` | `skip-unless-android-perftest-backstop` | mobile browsertime outside `android-hw-a55` |
| `perf-cadence-default` | `test` | talos, awsy |

The aliases are registered in {searchfox}`taskcluster/gecko_taskgraph/optimize/__init__.py`.
`perf_batch_overrides` in {searchfox}`taskcluster/gecko_taskgraph/optimize/perf_batching.py` builds a set of strategy overrides that replaces all four aliases with one batching strategy and keeps the autoland strategies for everything else.

## Shadow Schedulers

A shadow scheduler is a tier 3 task, run on autoland pushes after the decision task, that runs the push's optimization again with a different set of strategies and uploads the result instead of scheduling anything.
The tasks are defined in {searchfox}`taskcluster/kinds/source-test/shadow-scheduler.yml`, and the strategy set each one uses is named by its `TASKGRAPH_OPTIMIZE_STRATEGIES` variable.
They show up on Treeherder as `SS(<name>)` once tier 3 jobs are shown.

On non-backstop pushes, the autoland target tasks method drops tests against shippable builds before optimization runs, which covers most performance tasks.
The performance shadow schedulers therefore first rewrite the decision task's parameters with {searchfox}`taskcluster/scripts/misc/shadow-scheduler-parameters.py`, setting the target tasks method to `autoland_perf_shadow_scheduler_tasks`, which targets every task optimized through a `perf-cadence-*` alias on every push.

Each performance shadow scheduler uploads:

`public/shadow-scheduler/optimized-tasks.json`
: The tasks the strategy would have scheduled on this push.

`public/shadow-scheduler/parameters.yml`
: The parameters the optimization ran with.

`public/shadow-scheduler-state/state.json`
: The batching state, for the strategies that keep one (see [Batching State](#batching-state)).

To reproduce a run locally for a decision task:

```shell
./mach python --virtualenv taskgraph taskcluster/scripts/misc/shadow-scheduler-parameters.py \
    --decision-task-id <decision task id> \
    --target-tasks-method autoland_perf_shadow_scheduler_tasks \
    parameters.yml
TASKGRAPH_OPTIMIZE_STRATEGIES=gecko_taskgraph.optimize:experimental.perf_rasb \
    ./mach taskgraph optimized -p parameters.yml
```

A local run loads the state of the latest autoland run, and records nothing unless `SHADOW_SCHEDULER_STATE_PATH` is set.

## Strategies

Each shadow scheduler is named after the entry of the `experimental` class in {searchfox}`taskcluster/gecko_taskgraph/optimize/__init__.py` that it runs.
The parameter values live there only; task names do not include them, so tuning a strategy only changes that file.
The current values come from bugbug's perf scheduling simulator (`docs/perf-scheduling-simulation.md` in the bugbug repository), which replays each strategy over autoland's pushes, confirmed regressions and risk scores; each strategy is set to match the backstop's cost while catching every regression it caught.
Risk thresholds are probabilities, since bugbug serves calibrated scores (see below).

| Shadow scheduler | Strategy | A batch is due when |
|---|---|---|
| `perf_fixed_batch` | `SkipUnlessPushesSinceLastBatch` | N pushes have been made since the last batch, counted by pushlog ID like the backstop's push interval |
| `perf_time_batch` | `SkipUnlessTimeSinceLastBatch` | H hours have passed since the last batch |
| `perf_rasb` | `SkipUnlessRiskThreshold` | the probability that a commit since the last batch regressed performance reaches a threshold |
| `perf_rasb_la` | `SkipUnlessRiskThreshold` | the sum of the risk scores of the commits since the last batch reaches a threshold |
| `perf_rapb` | `SkipUnlessRiskThreshold` | as `perf_rasb`, with risk scores that rise the longer commits wait |
| `perf_rapb_la` | `SkipUnlessRiskThreshold` | as `perf_rasb_la`, with risk scores that rise the longer commits wait |

### Risk-Aware Strategies

The risk-aware strategies score the commits pushed since the last batch with bugbug's performance regression predictor, through `push_perf_risk` in {searchfox}`taskcluster/gecko_taskgraph/util/bugbug.py`.
For each commit `i`:

- `r_i` is the probability that the commit regresses performance: bugbug calibrates the predictor's raw score against the share of past commits with that score that turned out to be culprits, so the scores it serves are probabilities.
- `w_i` is the number of hours from the commit's push to the push being decided.

Commits that bugbug does not score, such as merges and web-platform-tests imports, are left out; backouts are scored like any other commit.
Decisions are only made on pushes, and the commits are cleared once a batch is due.

Risk-aware stream batching (RASB, `perf_rasb`) computes the probability that at least one commit regressed, treating commits as independent:

```
R = 1 - (1 - r_1) * (1 - r_2) * ... * (1 - r_n)
```

It is computed in log space, where the product becomes a sum, and compared with the threshold `t` there:

```
-ln(1 - R) = sum of -ln(1 - r_i)        batch when it reaches -ln(1 - t)
```

Its linear aggregation variant (RASB-la, `perf_rasb_la`) adds the risk scores up instead, which is the expected number of regressing commits since the scores are calibrated:

```
S = r_1 + r_2 + ... + r_n               batch when S reaches t
```

Risk-aged priority batching (RAPB, `perf_rapb`, and `perf_rapb_la` for its linear aggregation variant) replaces each risk score with an aged score before aggregating them the same way, using an aging rate `a` per hour:

```
aged r_i = 1 - (1 - r_i) * exp(-a * w_i)
```

The share of a commit that is not risky shrinks the longer the commit waits, halving every `ln(2) / a` hours, so that low-risk commits are eventually tested too.
In log space, aging adds `a` for every hour each commit has waited:

```
-ln(1 - R) = sum of -ln(1 - r_i)  +  a * sum of w_i
```

Two consequences of these definitions:

- A product aggregation threshold must be between 0 and 1.
- Each aged score stays below 1, so a linear aggregation threshold `t` can only be reached once more than `t` commits are waiting.

## Batching State

Whether a batch is due on a push depends on every push since the last batch, but shadow scheduler runs are separate tasks.
`SkipUnlessPushesSinceLastBatch`, `SkipUnlessTimeSinceLastBatch` and `SkipUnlessRiskThreshold` share the state handling of `SkipUnlessBatchDue`, which chains it through the shadow scheduler tasks themselves.
Each run loads the `state.json` of the latest run of the same shadow scheduler through the `gecko.v2.autoland.latest.source.shadow-scheduler-<name>` index, and records an updated one.

The latest run is the one that finished last, not the run for the previous push: runs overlap, and some pushes get no run at all.
A total carried over from run to run would therefore lose pushes.
Instead, each run:

1. Takes the most recent batch recorded before its push.
2. Loads the pushes after it from the pushlog, and their risk scores from bugbug, which caches them.
3. Replays the strategy push by push, recording a batch wherever one is due and starting over from it.

Decisions only depend on the pushlog and the risk scores, so every run reaches the same batches whichever state it loaded, including the batches of runs it could not see.
A push whose commit message says `DONTBUILD` gets no tasks at all, so no shadow scheduler runs on it and no batch can run on it.
The replay reads the head commit's message from the pushlog, counts such a push's commits towards the next decision, but never makes it a batch: a batch due there waits for the next push.

Two conditions keep it that way:

- A run fails rather than counting a push it could not get a risk score for. The next run replays that push.
- A parameter change applies from the most recent batch onward; runs for pushes around the change may disagree.

A run with no recorded batch to start from, or with more than `MAX_REPLAYED_PUSHES` pushes since it, batches and is flagged as a cold start.
Evaluation should discount cold starts, and read the batch sequence from the `batches` recorded in the state rather than from the runs that batched.

The state records:

`batches`
: The most recent batches, each with its `pushlog_id`, `pushdate` and `head_rev`, up to `MAX_RECORDED_BATCHES`.

`batched`
: Whether this push is a batch push.

`cold_start`
: Whether no previous batch could be replayed from.

`score`
: The score the decision was based on: the hours since the last batch, or the risk score.

`replayed_from`
: The pushlog ID of the batch the replay started from.

`config`
: The strategy's parameters.

`risk_prediction`
: For the risk-aware strategies, bugbug's prediction for this push, with a score for each commit.

## Adding a Strategy

1. Add an entry to the `experimental` class in {searchfox}`taskcluster/gecko_taskgraph/optimize/__init__.py`, built with `perf_batch_overrides`.
2. Add a task to {searchfox}`taskcluster/kinds/source-test/shadow-scheduler.yml`, reusing the artifacts and command of the existing performance shadow schedulers.
   Treeherder symbols are limited to 25 characters.
3. For a stateful strategy, pass the task's name as `state_name`, so that the task's `shadow-scheduler-<name>` index finds its state.
