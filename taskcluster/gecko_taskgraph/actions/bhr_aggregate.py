# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import logging
from datetime import datetime, timedelta

from .registry import register_callback_action
from .util import create_tasks, fetch_graph_and_labels

logger = logging.getLogger(__name__)

TASK_LABEL = "bhr-aggregate-cron"
# Where a custom-date run publishes. The cron's own routes are all "this is the
# aggregation for this push", which a backfill is not.
BUILD_DATE_ROUTE = "index.gecko.v2.mozilla-central.bhr-aggregate.build.{date}"
# A window's run time grows with the pings it reads: streaming one
# day's rows back from BigQuery at the cron's 0.5 sample alone takes 20 to 40
# minutes. Days times the effective sample size bounds those pings, and the day
# count bounds per-day work. bhr_collection.aggregate enforces the same limits.
MAX_WINDOW_DAYS = 3
MAX_SAMPLED_DAYS = 1.5
WINDOW_MAX_RUN_TIME = 4 * 60 * 60


@register_callback_action(
    title="BHR aggregation (custom date)",
    name="bhr-aggregate",
    symbol="bhr-custom",
    description=(
        "Run the Background Hang Reporter aggregation for a specific build date "
        "and/or sample size, rather than the daily cron's four-days-ago at 0.5. "
        "Used to backfill a historical day or re-run a day that failed."
    ),
    order=1000,
    context=[],
    # Its own permission rather than "generic": the task this creates carries
    # secrets:get on the production BigQuery credential, and the action task has
    # to hold that scope to grant it. Keeping it off action:generic means the
    # grant covers this action alone. Needs a matching role and hook in
    # fxci-config, as the cron itself did (bug 2056977).
    permission="bhr-aggregate",
    schema={
        "type": "object",
        "properties": {
            "date": {
                "type": "string",
                "pattern": "^[0-9]{8}$",
                "title": "Build date",
                "description": (
                    "Build date to aggregate, as YYYYMMDD. Leave empty to keep "
                    "the cron's behaviour of resolving the date at run time from "
                    "BHR_AGGREGATE_DATE_OFFSET_DAYS."
                ),
            },
            "end_date": {
                "type": "string",
                "pattern": "^[0-9]{8}$",
                "title": "Last build date of the window",
                "description": (
                    "Aggregate every build date from the date above through "
                    f"this one, inclusive: at most {MAX_WINDOW_DAYS} days, and "
                    f"at most {MAX_SAMPLED_DAYS:g} once multiplied by the "
                    "sample size, so 3 days at the cron's 0.5. One "
                    "BigQuery scan covers the whole window rather than one per "
                    "day; each day is still published as its own artifact, "
                    "indexed under its own build date. Like any pinned run it "
                    "leaves the roll-up alone: name its days in refill_dates "
                    "on a later run to bring them in."
                ),
            },
            "refill_dates": {
                "type": "array",
                "items": {"type": "string", "pattern": "^[0-9]{8}$"},
                "title": "Roll-up dates to recompute",
                "description": (
                    "Build dates whose timeseries entry should be rebuilt from "
                    "their published artifact. The roll-up keeps whatever a day's "
                    "first run produced, so days that were backfilled or re-run "
                    "only reach it when named here. Leave empty unless that is "
                    "what you are doing; this run then also publishes the daily "
                    "artifact and roll-up as usual, so do not pin a date with it."
                ),
            },
            "sample_size": {
                "type": "number",
                "exclusiveMinimum": 0,
                "maximum": 1,
                "title": "Sample size",
                "description": (
                    "Fraction of pings to read, in (0, 1]. Leave empty to keep the "
                    "cron's production sampling. A small value is much cheaper and "
                    "is usually enough to check that a change behaves."
                ),
            },
        },
        "additionalProperties": False,
    },
    # The job reads production BigQuery with a stored credential granted only to
    # the level-3 mozilla-central cron, so it must not be reachable elsewhere.
    available=lambda parameters: parameters["project"] == "mozilla-central",
)
def bhr_aggregate_action(parameters, graph_config, input, task_group_id, task_id):
    decision_task_id, full_task_graph, label_to_taskid, _ = fetch_graph_and_labels(
        parameters, graph_config
    )

    if TASK_LABEL not in full_task_graph.tasks:
        raise Exception(f"{TASK_LABEL} was not found in the task-graph")

    date = input.get("date")
    end_date = input.get("end_date")
    sample_size = input.get("sample_size")
    refill_dates = input.get("refill_dates") or []

    if end_date and not date:
        raise Exception("end_date needs a date to start the window from")
    if end_date and refill_dates:
        raise Exception("end_date and refill_dates cannot be combined")
    window = [date] if date else []
    if end_date:
        first = datetime.strptime(date, "%Y%m%d")
        days = (datetime.strptime(end_date, "%Y%m%d") - first).days + 1
        if days < 1:
            raise Exception(f"end_date {end_date} is before date {date}")
        if days > MAX_WINDOW_DAYS:
            raise Exception(
                f"{date}..{end_date} spans {days} days; one run covers at most "
                f"{MAX_WINDOW_DAYS}"
            )
        if sample_size is not None:
            effective = sample_size
        else:
            cron_env = full_task_graph.tasks[TASK_LABEL].task["payload"]["env"]
            effective = float(cron_env["BHR_AGGREGATE_SAMPLE_SIZE"])
        if days * effective > MAX_SAMPLED_DAYS:
            raise Exception(
                f"{date}..{end_date} at sample size {effective:g} comes to "
                f"{days * effective:g} days * sample size; one run covers at "
                f"most {MAX_SAMPLED_DAYS:g}"
            )
        window = [(first + timedelta(days=i)).strftime("%Y%m%d") for i in range(days)]
    if date and refill_dates:
        raise Exception(
            "date and refill_dates cannot be combined: a run pinned to a past "
            "build date must not publish the roll-up, which is shared state "
            "ending at the most recent day."
        )

    def modifier(task):
        if task.label != TASK_LABEL:
            return task

        env = task.task["payload"]["env"]
        if date:
            # Pins the run to one build date. The script prefers this over
            # resolving an offset against the current time, so the task stays
            # reproducible however long after the fact it is triggered.
            env["BHR_AGGREGATE_DATE"] = date
        if end_date:
            env["BHR_AGGREGATE_END_DATE"] = end_date
        if len(window) > 1:
            task.task["payload"]["maxRunTime"] = WINDOW_MAX_RUN_TIME
        if sample_size is not None:
            env["BHR_AGGREGATE_SAMPLE_SIZE"] = str(sample_size)

        # Distinguish any run triggered here from the daily one on Treeherder.
        task.task["extra"]["treeherder"]["symbol"] += "-custom"

        if refill_dates:
            # Recomputing the roll-up is the one thing this action does that is
            # meant to replace the day's run, so it keeps the cron's routes and
            # its timeseries step: it aggregates the current day as usual and
            # rebuilds the named days alongside it.
            env["BHR_TIMESERIES_REFILL_DATES"] = ",".join(refill_dates)
            return task

        # Otherwise this is a one-off, and it must not inherit the push's index
        # routes. Those include the "latest" route, which is both the build the
        # dashboard shows by default and where the next cron run reads its
        # timeseries state, and the pushdate route the dashboard resolves a
        # build date through -- a backfill landing on either would displace the
        # real run for the day it was triggered on.
        routes = [
            route
            for route in task.task.get("routes", [])
            if not route.startswith("index.")
        ]
        routes.extend(BUILD_DATE_ROUTE.format(date=day) for day in window)
        task.task["routes"] = routes

        # Nor may it touch the shared roll-up. build_timeseries ends the window
        # at the build date and drops every state day outside it, so rolling up
        # a past day would prune all the later days out of the state the cron
        # publishes -- and nothing can refill them, since their artifacts are
        # not local to that run.
        env["BHR_SKIP_TIMESERIES"] = "1"
        return task

    logger.info(
        "Triggering %s with date=%s end_date=%s sample_size=%s refill_dates=%s",
        TASK_LABEL,
        date or "(cron default)",
        end_date or "(same as date)",
        sample_size if sample_size is not None else "(cron default)",
        ",".join(refill_dates) or "(none)",
    )
    create_tasks(
        graph_config,
        [TASK_LABEL],
        full_task_graph,
        label_to_taskid,
        parameters,
        decision_task_id,
        modifier=modifier,
    )
