# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Secondary BHR job: build per-signature daily timeseries from the primary
job's historical daily artifacts.

The primary job (bhr_collection.aggregate) writes one columnar profile per
build date. This job consumes a rolling window of those daily artifacts and
produces a single compact artifact giving, for the top-M hang signatures over
the window, their hang milliseconds and count on each day.

A hang signature is identified across days by a content-based key derived from
its reconstructed stack -- the ordered list of (funcName, libName) frames,
leaf->root. funcTable indices are per-file and cannot be used. The frontend
derives the same key from the per-signature `frames` arrays this job emits, so
the two sides join without any shared hashing.

The job is incremental and self-healing. It keeps a state file holding, per day
in the window, that day's top-N signatures (a cushion above the published
top-M). On each run it prunes days that fell out of the window and fills any
in-window day that has an artifact but no stored entry; only newly seen days are
re-read.

A day in a published series is `null`, not 0, when we do not know what happened:
either that day has no artifact, or the day was truncated to its top-N and this
signature fell below the cut. 0 is reserved for days we can positively say the
signature did not hang, which is only when the day kept every signature it saw.
The distinction matters downstream: a zero drawn on a chart asserts the hang did
not occur, when often it just was not big enough that day to be retained.
"""

import datetime
import glob
import gzip
import json
import os
import re
import uuid

# Sibling module in the aggregation dir. Unions the per-signature HLL sketches
# the primary job emits, so we can report the share of distinct users a
# signature affected over each trailing window.
from client_metrics import HyperLogLog
from stack_keys import canonical_key, reconstruct_stack

# Defaults. window_days matches the issue's "last 365 days"; top_count is the
# published signature cap; per_day_top_n is the per-day cushion kept in state so
# a signature that rises into the global top-M still has history on days where
# it briefly dipped in rank.
DEFAULT_WINDOW_DAYS = 365
DEFAULT_TOP_COUNT = 500
DEFAULT_PER_DAY_TOP_N = 2000

# Trailing sub-windows (in days, ending at the window's last date) over which we
# report the distinct users a signature affected. Each is a suffix of the full
# window, so they nest: d7's dates are the last 7 of d28's, which are the last
# 28 of d365's. Clamped to the days actually available. 28 (four weeks) rather
# than 30 keeps the mid window aligned to whole weeks.
AFFECTED_WINDOWS = (("d7", 7), ("d28", 28), ("d365", 365))

_ARTIFACT_RE = re.compile(r"hangs_(?P<tag>.+)_(?P<date>\d{8})\.json$")


def pick_thread(profile):
    """Select the thread the frontend analyzes: main-process Gecko, else first."""
    threads = profile["threads"]
    for thread in threads:
        if thread["name"] == "Gecko" and thread["processType"] == "default":
            return thread
    return threads[0] if threads else None


def aggregate_day(profile, per_day_top_n):
    """Aggregate one daily profile into per-signature (ms, count), top-N by ms.

    Returns (by_key, total_sketch), where by_key is
    {key: {"frames": [...], "ms": float, "count": float, "sketch": <serialized>|None}}
    mirroring the frontend's pass-1 dedup (fold samples whose stacks are
    identical, ignoring runnable/annotations/platform). `total_sketch` is the
    day's all-signatures HLL sketch (the affected-users denominator), or None
    when the artifact carries no client metrics.

    The per-signature sketch is joined from the primary job's `affectedClients`
    block by the same canonical key used for the ms/count series, so the two
    stay aligned across days.
    """
    thread = pick_thread(profile)
    if thread is None:
        return {}, None

    day = thread["dates"][0]
    sample_ms = day["sampleHangMs"]
    sample_count = day["sampleHangCount"]
    sample_total = thread["sampleTable"]["length"]

    affected = profile.get("affectedClients") or {}
    sketch_by_key = affected.get("sketchBySignature") or {}
    total_sketch = affected.get("totalSketch")

    by_key = {}
    for i in range(sample_total):
        ms = sample_ms[i]
        if ms is None or ms <= 0.0:
            continue
        frames = reconstruct_stack(thread, i)
        key = canonical_key(frames)
        entry = by_key.get(key)
        if entry is None:
            by_key[key] = {
                "frames": frames,
                "ms": ms,
                "count": sample_count[i] or 0.0,
                "sketch": sketch_by_key.get(key),
            }
        else:
            entry["ms"] += ms
            entry["count"] += sample_count[i] or 0.0

    if len(by_key) <= per_day_top_n:
        return by_key, total_sketch
    top = sorted(by_key.items(), key=lambda kv: kv[1]["ms"], reverse=True)
    return dict(top[:per_day_top_n]), total_sketch


def available_artifacts(input_dir, output_tag):
    """Map build-date string -> artifact path for the given tag in input_dir."""
    result = {}
    for path in glob.glob(os.path.join(input_dir, f"hangs_{output_tag}_*.json")):
        match = _ARTIFACT_RE.search(os.path.basename(path))
        if match and match.group("tag") == output_tag:
            result[match.group("date")] = path
    return result


def window_dates(end_date_str, window_days):
    """Return the sorted YYYYMMDD strings for the window ending at end_date."""
    end = datetime.datetime.strptime(end_date_str, "%Y%m%d").date()
    start = end - datetime.timedelta(days=window_days - 1)
    return [
        (start + datetime.timedelta(days=offset)).strftime("%Y%m%d")
        for offset in range((end - start).days + 1)
    ]


def load_state(path):
    """Read the gzipped state file, or start empty if it isn't there yet.

    A missing state file is not an error: build_timeseries refills any window
    date it doesn't find in state from that day's artifact, so a lost or
    corrupt state self-heals on the next run.
    """
    if path and os.path.exists(path):
        with gzip.open(path, "rt", encoding="utf-8") as state_file:
            return json.load(state_file)
    return {"days": {}, "totalSketches": {}}


def merge_window_counts(sketch_by_date, dates):
    """Distinct-user count of the sketch union over each AFFECTED_WINDOWS suffix.

    The windows nest (each is a suffix of the next), so we merge once from the
    most recent day backward and snapshot the running union as we cross each
    boundary. Returns {label: int} covering every window, or None when no day in
    `dates` carries a sketch (nothing to count).
    """
    running = None
    counts = {}
    boundary = {n: label for label, n in AFFECTED_WINDOWS}
    for position, date in enumerate(reversed(dates), start=1):
        sketch = sketch_by_date.get(date)
        if sketch is not None:
            hll = HyperLogLog.deserialize(sketch)
            running = hll if running is None else running.merge(hll)
        if position in boundary:
            counts[boundary[position]] = running.count() if running is not None else 0
    if running is None:
        return None
    # Windows longer than the available history collapse to the full union.
    full = running.count()
    for label, _ in AFFECTED_WINDOWS:
        counts.setdefault(label, full)
    return counts


def _daily_count(sketch):
    """Distinct-user count for a single day's sketch."""
    if sketch is None:
        return None
    return HyperLogLog.deserialize(sketch).count()


def affected_value(days, date, key, per_day_top_n, sketch):
    """One day's affected-user count, or None where we do not know it.

    Follows the same rule as the ms and count series, with one extra way to
    land on None: the signature hung that day but the artifact carried no
    client metrics, so there is a hang count and no user count.
    """
    if sketch is not None:
        return _daily_count(sketch)
    day = days.get(date)
    if day is None:
        return None
    if day.get(key) is not None:
        return None
    return 0 if day_is_complete(day, per_day_top_n) else None


def day_is_complete(day, per_day_top_n):
    """Whether a day's map holds every signature that hung, or was truncated.

    aggregate_day keeps only the per_day_top_n signatures with the most hang ms.
    When fewer than that many hung, nothing was dropped and a signature missing
    from the map really did not hang. Once the cap is hit we can no longer tell
    "did not hang" from "hung too little to make the cut", so the two cases have
    to be reported differently.
    """
    return per_day_top_n is None or len(day) < per_day_top_n


def series_value(days, date, key, per_day_top_n, field):
    """One day's value for a signature, or None where we genuinely do not know.

    None means no data rather than zero, and covers two cases: the day has no
    artifact at all, or the day was truncated and this signature fell below the
    cut. Reporting either as 0.0 would claim the hang did not happen, when all
    we know is that we did not keep it.
    """
    day = days.get(date)
    if day is None:
        return None
    entry = day.get(key)
    if entry is not None:
        return entry[field]
    return 0.0 if day_is_complete(day, per_day_top_n) else None


def build_published(state, dates, top_count):
    """Derive the slim top-M published artifact from the per-day state."""
    days = state["days"]
    per_day_top_n = state.get("perDayTopN")
    total_sketches = state.get("totalSketches", {})

    totals = {}
    frames_by_key = {}
    for date in dates:
        for key, entry in days.get(date, {}).items():
            total = totals.get(key)
            if total is None:
                totals[key] = [entry["ms"], entry["count"]]
                frames_by_key[key] = entry["frames"]
            else:
                total[0] += entry["ms"]
                total[1] += entry["count"]

    top_keys = sorted(totals, key=lambda k: totals[k][0], reverse=True)[:top_count]

    # Distinct users over each trailing window, the shared denominator for the
    # per-signature percentages. None when no artifact in the window carried
    # client metrics, in which case the affected-users fields are all omitted.
    total_users = merge_window_counts(total_sketches, dates)

    signatures = []
    for key in top_keys:
        ms_series = []
        count_series = []
        sig_sketches = {}
        for date in dates:
            ms_series.append(series_value(days, date, key, per_day_top_n, "ms"))
            count_series.append(series_value(days, date, key, per_day_top_n, "count"))
            entry = days.get(date, {}).get(key)
            if entry and entry.get("sketch") is not None:
                sig_sketches[date] = entry["sketch"]

        signature = {
            "frames": frames_by_key[key],
            "totalMs": totals[key][0],
            "totalCount": totals[key][1],
            "ms": ms_series,
            "count": count_series,
        }
        if total_users is not None:
            affected = merge_window_counts(sig_sketches, dates) or {
                label: 0 for label, _ in AFFECTED_WINDOWS
            }
            signature["affectedUsers"] = affected
            signature["affectedPct"] = {
                label: (affected[label] / total_users[label])
                if total_users[label] > 0
                else 0.0
                for label, _ in AFFECTED_WINDOWS
            }
            # Per-day distinct users for this signature, parallel to `dates`, so
            # the frontend can show that single day's affected count on hover.
            signature["affected"] = [
                affected_value(days, date, key, per_day_top_n, sig_sketches.get(date))
                for date in dates
            ]
        signatures.append(signature)

    published = {
        "uuid": uuid.uuid4().hex,
        "windowStartDate": dates[0],
        "windowEndDate": dates[-1],
        "topCount": top_count,
        "dates": dates,
        "signatures": signatures,
    }
    if total_users is not None:
        published["totalUsers"] = total_users
        published["affectedWindows"] = [n for _, n in AFFECTED_WINDOWS]
        # Per-day distinct users across all signatures, the daily denominator.
        published["totalAffected"] = [
            _daily_count(total_sketches.get(date)) for date in dates
        ]
    return published


def write_file(path, data):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as json_file:
        json.dump(data, json_file, ensure_ascii=False)
    return path


def write_state(path, state):
    """Write the state file gzipped.

    State is dominated by stack strings repeated across days, so it compresses
    about 11x: a 30-day window measured 244 MB plain against 22 MB gzipped.
    Only state is compressed; the published artifact is fetched directly by the
    dashboard and stays plain JSON.
    """
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with gzip.open(path, "wt", encoding="utf-8") as state_file:
        json.dump(state, state_file, ensure_ascii=False)
    return path


def build_timeseries(
    input_dir,
    output_dir,
    output_tag="main",
    end_date=None,
    window_days=DEFAULT_WINDOW_DAYS,
    top_count=DEFAULT_TOP_COUNT,
    per_day_top_n=DEFAULT_PER_DAY_TOP_N,
):
    """Incrementally update the timeseries state and emit the published artifact.

    Reads daily `hangs_<tag>_<date>.json` artifacts from input_dir, maintains
    `hangs_timeseries_<tag>_state.json.gz` (per-day top-N), and writes the slim
    `hangs_timeseries_<tag>.json` the frontend consumes. Returns the published
    dict.
    """
    artifacts = available_artifacts(input_dir, output_tag)
    if not artifacts:
        raise RuntimeError(
            f"No hangs_{output_tag}_*.json artifacts found in {input_dir}"
        )

    end_date_str = end_date.strftime("%Y%m%d") if end_date else max(artifacts)
    dates = window_dates(end_date_str, window_days)
    in_window = set(dates)

    state_path = os.path.join(
        output_dir, f"hangs_timeseries_{output_tag}_state.json.gz"
    )
    state = load_state(state_path)
    days = state["days"]
    total_sketches = state.setdefault("totalSketches", {})

    for date in list(days):
        if date not in in_window:
            del days[date]
            total_sketches.pop(date, None)

    for date in dates:
        if date in days or date not in artifacts:
            continue
        print(f"Filling {date} from {artifacts[date]}", flush=True)
        with open(artifacts[date], encoding="utf-8") as profile_file:
            profile = json.load(profile_file)
        days[date], day_total_sketch = aggregate_day(profile, per_day_top_n)
        if day_total_sketch is not None:
            total_sketches[date] = day_total_sketch

    state["windowDays"] = window_days
    state["perDayTopN"] = per_day_top_n
    write_state(state_path, state)

    published = build_published(state, dates, top_count)
    written = write_file(
        os.path.join(output_dir, f"hangs_timeseries_{output_tag}.json"), published
    )
    print(
        f"Wrote {len(published['signatures'])} signatures across "
        f"{len(dates)} days to {written}",
        flush=True,
    )
    return published
