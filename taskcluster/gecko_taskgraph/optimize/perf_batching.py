# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Batching strategies that change the cadence performance tests run at on
autoland. Performance tasks are optimized through the perf-cadence-* aliases
(registered in gecko_taskgraph.optimize and assigned by the perf transforms),
which by default behave exactly like the strategies they wrap. The batching
strategies are currently only referenced by the shadow schedulers defined in
taskcluster/kinds/source-test/shadow-scheduler.yml, which override those
aliases to evaluate alternative cadences without affecting what production
schedules.
"""

import json
import logging
import math
import os

from taskcluster.exceptions import TaskclusterRestFailure
from taskgraph.optimize.base import OptimizationStrategy
from taskgraph.util.taskcluster import find_task_id, get_artifact

from gecko_taskgraph.util.bugbug import push_perf_risk
from gecko_taskgraph.util.hg import get_push_data

logger = logging.getLogger(__name__)

# The optimization aliases through which performance tasks are optimized:
# raptor uses `perf-cadence-expanded` or `perf-cadence-backstop` depending on
# tier, mobile browsertime uses `perf-cadence-android`, mozperftest uses
# `perf-cadence-backstop` and talos/awsy use `perf-cadence-default`.
PERF_CADENCE_STRATEGIES = (
    "perf-cadence-android",
    "perf-cadence-backstop",
    "perf-cadence-default",
    "perf-cadence-expanded",
)

# Stateful strategies chain state through the shadow scheduler tasks
# themselves: each run loads the state artifact of the same shadow
# scheduler's last successful run through the Taskcluster index, and records
# an updated one for the next run to find.
STATE_INDEX = "gecko.v2.{project}.latest.source.shadow-scheduler-{state_name}"
STATE_ARTIFACT = "public/shadow-scheduler-state/state.json"
STATE_PATH_ENVVAR = "SHADOW_SCHEDULER_STATE_PATH"
STATE_VERSION = 2

# Number of most recent batches kept in the state. A run replays from the
# newest one before its own push, which is not the last one recorded when the
# state comes from the run of a later push.
MAX_RECORDED_BATCHES = 50

# Beyond this many pushes since the last recorded batch, start over rather
# than replay them all.
MAX_REPLAYED_PUSHES = 500

# Risk scores are capped below 1 so that log(1 - risk) stays finite.
MAX_RISK = 1 - 1e-9


def load_state(project, state_name):
    """Load the state recorded by the last successful run of the
    ``state_name`` shadow scheduler on ``project``, or None when no usable
    state exists (first run, expired, previous runs failed)."""
    index = STATE_INDEX.format(project=project, state_name=state_name)
    try:
        return get_artifact(find_task_id(index), STATE_ARTIFACT)
    except TaskclusterRestFailure:
        logger.warning(f"no previous state found via {index}")
        return None


def save_state(state):
    """Write ``state`` as JSON to the path named by the
    SHADOW_SCHEDULER_STATE_PATH environment variable, where the worker picks
    it up as an artifact. No-op when the variable is unset (local runs, try
    pushes)."""
    path = os.environ.get(STATE_PATH_ENVVAR)
    if not path:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fh:
        json.dump(state, fh, indent=2, sort_keys=True)


def get_pushes_between(params, first_pushlog_id, last_pushlog_id):
    """Return the pushes from ``first_pushlog_id`` to ``last_pushlog_id``
    (inclusive) on the project of ``params``, oldest first. ``dontbuild``
    tells whether the decision task ran no task at all on the push, which
    it decides from the head commit's message as in decision.py."""
    if first_pushlog_id > last_pushlog_id:
        return []
    push_data = get_push_data(
        params["head_repository"],
        params["project"],
        first_pushlog_id,
        last_pushlog_id,
        full=True,
    )
    if push_data is None:
        raise RuntimeError(
            f"Failed to load pushes {first_pushlog_id} to {last_pushlog_id} from the pushlog"
        )
    pushes = []
    for pushlog_id, data in push_data.items():
        head = data["changesets"][-1] if data["changesets"] else None
        pushes.append({
            "pushlog_id": pushlog_id,
            "pushdate": data["date"],
            "head_rev": head["node"] if head else None,
            "dontbuild": bool(head) and "DONTBUILD" in head["desc"],
        })
    return pushes


class SkipUnlessBatchDue(OptimizationStrategy):
    """Base class of the stateful batching strategies, which remove tasks
    except on the pushes where a batch is due.

    Whether a batch is due depends on every push since the last batch, but
    the shadow scheduler runs that carry the state overlap, fail, or are
    missing for some pushes, so a running total handed from run to run would
    lose pushes. Instead, each run replays the pushes since the most recent
    batch recorded by any previous run, re-deriving the batches of the runs
    it cannot see. Batching decisions only depend on the pushlog (and on
    bugbug's cached risk scores), so every run reaches the same decisions
    whichever state it loaded.

    The decision task strips every task from a push whose commit message
    says DONTBUILD, so no shadow scheduler runs on it and no batch can run
    on it either: the replay observes such a push, so its commits count
    towards the next decision, but never makes it a batch.

    Tasks are kept when no previous batch can be found (first run, state
    expired, too many pushes to replay); the recorded state marks this as a
    cold start so evaluation can discount it. The decision is computed and
    recorded once per push and reused for every task.

    Args:
        state_name (str): Name of the shadow scheduler task whose runs carry
            this strategy's state, as used in its index job-name.
    """

    def __init__(self, state_name):
        self.state_name = state_name
        self._decisions = {}

    @property
    def config(self):
        """The strategy's parameters, recorded in the state."""
        raise NotImplementedError

    def batch_due(self, params, last_batch, pending):
        """Return whether a batch is due on the last of the ``pending``
        pushes, which all came after ``last_batch``, along with the score
        that decision is based on."""
        raise NotImplementedError

    def push_details(self, params, push):
        """Return extra information about ``push`` to record in the state."""
        return {}

    def should_remove_task(self, task, params, _):
        """Remove the task unless this push is a batch push. The batch
        decision is per-push, so it is computed once and cached for all the
        tasks of the same push."""
        pushlog_id = int(params["pushlog_id"])
        if pushlog_id not in self._decisions:
            self._decisions[pushlog_id] = self._batch_and_record(params)
        return not self._decisions[pushlog_id]

    def _batch_and_record(self, params):
        """Decide whether this push batches by replaying the pushes since
        the last recorded batch, and record the updated state for the
        strategy's next run to load."""
        push = {
            "pushlog_id": int(params["pushlog_id"]),
            "pushdate": int(params["pushdate"]),
            "head_rev": params["head_rev"],
        }
        state = load_state(params["project"], self.state_name) or {}
        # A run for a later push may have finished first; its batches after
        # this push are left out, later runs re-derive them.
        batches = [
            batch
            for batch in state.get("batches", [])
            if batch["pushlog_id"] < push["pushlog_id"]
        ]
        replayed_from = batches[-1]["pushlog_id"] if batches else None

        cold_start = (
            replayed_from is None
            or push["pushlog_id"] - replayed_from > MAX_REPLAYED_PUSHES
        )
        if cold_start:
            batch, score = True, None
            batches.append(push)
        else:
            batch, score = False, None
            pending = []
            earlier_pushes = get_pushes_between(
                params, replayed_from + 1, push["pushlog_id"] - 1
            )
            for pending_push in earlier_pushes + [push]:
                pending.append(pending_push)
                if pending_push.get("dontbuild"):
                    continue
                batch, score = self.batch_due(params, batches[-1], pending)
                if batch:
                    batches.append(pending_push)
                    pending = []

        save_state({
            "version": STATE_VERSION,
            "strategy": self.state_name,
            "config": self.config,
            "batched": batch,
            "cold_start": cold_start,
            "score": score,
            "replayed_from": replayed_from,
            **push,
            **self.push_details(params, push),
            "batches": batches[-MAX_RECORDED_BATCHES:],
        })
        return batch


class SkipUnlessPushesSinceLastBatch(SkipUnlessBatchDue):
    """Removes tasks unless at least ``push_interval`` pushes have been made
    since the last batch, counted by pushlog id like the backstop's push
    interval. Unlike a fixed grid of pushlog ids, a batch that falls on a
    push nothing runs on is deferred to the next push rather than lost.

    Args:
        push_interval (int): Minimum number of pushes between batches.
        state_name (str): See SkipUnlessBatchDue.
    """

    def __init__(self, push_interval, state_name):
        super().__init__(state_name)
        self.push_interval = push_interval

    @property
    def description(self):
        """Human-readable label shown in optimization log messages."""
        return f"skip-unless-{self.push_interval}-pushes-since-last-batch"

    @property
    def config(self):
        return {"push_interval": self.push_interval}

    def batch_due(self, params, last_batch, pending):
        pushes = pending[-1]["pushlog_id"] - last_batch["pushlog_id"]
        return pushes >= self.push_interval, pushes


class SkipUnlessTimeSinceLastBatch(SkipUnlessBatchDue):
    """Removes tasks unless at least ``hours`` have passed since the last
    batch.

    Args:
        hours (float): Minimum time between batches.
        state_name (str): See SkipUnlessBatchDue.
    """

    def __init__(self, hours, state_name):
        super().__init__(state_name)
        self.hours = hours

    @property
    def description(self):
        """Human-readable label shown in optimization log messages."""
        return f"skip-unless-{self.hours}h-since-last-batch"

    @property
    def config(self):
        return {"hours": self.hours}

    def batch_due(self, params, last_batch, pending):
        elapsed = pending[-1]["pushdate"] - last_batch["pushdate"]
        return elapsed >= self.hours * 3600, elapsed / 3600


class SkipUnlessRiskThreshold(SkipUnlessBatchDue):
    """Removes tasks unless the risk of the commits pushed since the last
    batch reaches ``threshold``.

    The risk r of a commit is the probability that it regresses performance,
    as predicted by bugbug's perf regression predictor and calibrated against
    the culprit rate of past commits; commits bugbug does not score (merges,
    web-platform-tests imports, ...) are left out. With aging, the score of
    a commit that has waited w hours rises towards 1 as
    1 - (1 - r) * exp(-aging_rate * w), so that low-risk commits eventually
    get tested too. Commit scores are aggregated either as the probability
    that at least one commit regressed, 1 - prod(1 - score), computed in log
    space, or as their sum.

    Args:
        threshold (float): Aggregated score at which a batch is due.
        aggregation (str): "product" or "linear".
        state_name (str): See SkipUnlessBatchDue.
        aging_rate (float): Aging rate per hour waited, 0 to disable aging.
    """

    def __init__(self, threshold, aggregation, state_name, aging_rate=0):
        if aggregation not in ("product", "linear"):
            raise ValueError(f"unknown aggregation: {aggregation}")
        if aggregation == "product" and not 0 < threshold < 1:
            raise ValueError("a product aggregation threshold must be in (0, 1)")
        super().__init__(state_name)
        self.threshold = threshold
        self.aggregation = aggregation
        self.aging_rate = aging_rate

    @property
    def description(self):
        """Human-readable label shown in optimization log messages."""
        aging = f"-aging-{self.aging_rate}" if self.aging_rate else ""
        return f"skip-unless-{self.aggregation}-risk-{self.threshold}{aging}"

    @property
    def config(self):
        return {
            "threshold": self.threshold,
            "aggregation": self.aggregation,
            "aging_rate": self.aging_rate,
        }

    def batch_due(self, params, last_batch, pending):
        now = pending[-1]["pushdate"]
        # -log(1 - aged score) of each commit, to which aging adds linearly.
        hazards = [
            -math.log1p(-min(risk, MAX_RISK))
            + self.aging_rate * (now - push["pushdate"]) / 3600
            for push in pending
            for risk in self._commit_risks(params, push)
        ]
        if self.aggregation == "product":
            total = sum(hazards)
            return total >= -math.log1p(-self.threshold), -math.expm1(-total)
        score = sum(-math.expm1(-hazard) for hazard in hazards)
        return score >= self.threshold, score

    def push_details(self, params, push):
        return {"risk_prediction": self._prediction(params, push)}

    def _prediction(self, params, push):
        if push["head_rev"] is None:
            return {}
        return push_perf_risk(params["project"], push["head_rev"])

    def _commit_risks(self, params, push):
        return [
            commit["risk_score"]
            for commit in self._prediction(params, push).get("commits", [])
            if commit.get("ignored_reason") is None
        ]


def perf_batch_overrides(base_overrides, batch_strategy):
    """Build a strategy override dict that applies ``batch_strategy`` to all
    performance tasks while keeping the behavior of ``base_overrides`` for
    everything else.
    """
    overrides = dict(base_overrides)
    for name in PERF_CADENCE_STRATEGIES:
        overrides[name] = batch_strategy
    return overrides
