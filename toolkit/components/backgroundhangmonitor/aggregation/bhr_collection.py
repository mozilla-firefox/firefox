# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""BigQuery reader for the BHR aggregation pipeline.

Streams hang-report pings from BigQuery for a given build-date window,
filtered down to the columns the downstream pipeline actually consumes.

Ported from the original python_mozetl/mozetl/bhr_collection/bhr_collection.py. The
SQL and FARM_FINGERPRINT-based deterministic sampling are unchanged. The
PySpark BigQuery connector has been replaced with the google-cloud-bigquery
Python client; rows are yielded one at a time rather than materialised into
a Spark DataFrame so memory stays bounded at production sample sizes
(~500K rows × ~5-10 KB each would otherwise need 3-5 GB on a single worker).

The google-cloud-bigquery import is deferred to the call site so this
module can be imported in environments where the package isn't installed
(e.g. unit tests that mock the client). Production runs need the package
present in the runtime — that's handled by the TaskCluster Docker image
in a later phase of the migration.
"""

import hashlib
import json
import os
import resource
import sys
import time
import uuid
from datetime import datetime, timedelta, timezone

from client_metrics import HyperLogLog
from heuristics import apply_hang_signature_heuristics
from leaf_grouping import compute_leaf_groups
from profile_processor import ProfileProcessor
from symbolication import UNSYMBOLICATED, symbolicate_modules

_BQ_TABLE = "mozdata.firefox_desktop.hang_report_redacted"
_MAX_SAMPLE_SLICES = 10000
# How far either side of its build date a build's pings are read.
_SUBMISSION_PADDING_DAYS = 5
# Limits on one run's window, which the bhr-aggregate action also enforces and
# sizes the task's run time for. Days times the effective sample size bounds the
# pings read, at 3 days for the cron's 0.5; the day count bounds per-day work.
_MAX_WINDOW_DAYS = 3
_MAX_SAMPLED_DAYS = 1.5

# Project/dataset where query results are materialized before being read back.
# Large result sets cannot be returned inline, so the query writes to a temp
# table here first. We pin this to mozdata.tmp (independent of the billing
# project) to match the old job, which always materialized to mozdata.tmp while
# billing the query to a separately configured project.
_MATERIALIZATION_PROJECT = "mozdata"
_MATERIALIZATION_DATASET = "tmp"
# Lifetime of the temp result table, set as an OPTION when it is created:
# BigQuery deletes the table once it expires, and nothing else deletes it.
_MATERIALIZATION_EXPIRY_HOURS = 6
# A table a later run may reuse is kept long enough for a failed run to be
# retried days later, and each reuse pushes its expiry back by as much.
_REUSABLE_MATERIALIZATION_EXPIRY_HOURS = 72

# ru_maxrss is in bytes on macOS and in KiB on Linux.
_MAXRSS_PER_GIB = 2**30 if sys.platform == "darwin" else 2**20

# How often to print a heartbeat while streaming rows, so a long production run
# visibly makes progress instead of sitting silent.
_PROGRESS_EVERY_ROWS = 10000

# Pipeline defaults. The production CLI in python_mozetl overrode the stale
# 16000 upper bound with 65536, so we use that here for parity. uuid is added
# fresh per run in aggregate().
DEFAULT_CONFIG = {
    "thread_filter": "Gecko",
    "hang_lower_bound": 128,
    "hang_upper_bound": 65536,
    "stack_acceptance_threshold": 0.0,
    "symbol_server_url": "https://symbols.mozilla.org/",
    "print_debug_info": False,
    "split_threads_in_out_file": False,
    "use_minimal_sample_table": False,
    "post_sample_size": 1.0,
    "exclude_modules": False,
    # Compute per-signature distinct affected-client counts via HyperLogLog.
    # Reads client_id (locally only); only counts and mergeable sketches are
    # emitted, never ids. See client_metrics.py.
    "client_metrics": False,
    # Attach near-duplicate groups to the profile so the frontend can show them
    # without fuzzy matching of its own. Additive: the signature list itself is
    # unchanged. See leaf_grouping.py.
    "leaf_grouping": True,
}

# Frontend signature key. Each frame is "funcName<FIELD_SEP>libName" where
# libName is the module name with a trailing ".pdb" stripped (matching
# profile_processor.get_default_lib), frames joined leaf -> root. The
# symbolicated stack is root -> leaf, so we reverse it.
#
# This has to stay byte-for-byte identical to canonicalKey in the dashboard,
# which lives outside this repo and is what joins these counts to a displayed
# hang:
# https://github.com/skylarkning/bhr-dashboard/blob/main/src/processing/signatureKey.ts
_SIG_FIELD_SEP = "\x1f"
_SIG_FRAME_SEP = "\x1e"


def _signature_key(symbolicated_stack):
    frames = []
    for name, lib in reversed(symbolicated_stack):
        lib_name = lib[:-4] if lib.endswith(".pdb") else lib
        frames.append(f"{name}{_SIG_FIELD_SEP}{lib_name}")
    return _SIG_FRAME_SEP.join(frames)


def _compute_affected_clients(symbolicated):
    """Distinct affected clients per signature (HyperLogLog), plus the day total.

    `symbolicated` hangs carry client_id as their last element (appended by
    process_hangs under --client-metrics). Keyed by the frontend signature key
    so the dashboard can join the counts to each hang row.

    Emits both the day's distinct-client estimate and the mergeable HLL sketch
    per signature and for the day total. The secondary roll-up job
    unions these register-wise across a trailing window to report the share of
    users a signature affected over 7 / 28 / 365 days. Only counts and sketches
    (never client ids) are emitted, so cross-day counting stays privacy-safe.
    """
    by_key = {}
    day_total = HyperLogLog()
    for hang in symbolicated:
        client_id = hang[-1]
        if client_id is None:
            continue
        day_total.add(client_id)
        key = _signature_key(hang[0])
        hll = by_key.get(key)
        if hll is None:
            hll = by_key[key] = HyperLogLog()
        hll.add(client_id)
    return {
        "totalDistinct": day_total.count(),
        "totalSketch": day_total.serialize(),
        "bySignature": {key: hll.count() for key, hll in by_key.items()},
        "sketchBySignature": {key: hll.serialize() for key, hll in by_key.items()},
    }


# Top-level ping fields fetched from BigQuery. Returned in `slash/key` form
# because downstream code (process_hangs, process_module, etc.) expects this
# same shape. exclude_modules drops the hangs_modules column for the cases
# where module info isn't needed downstream.
_BASE_PROPERTIES = (
    "client_info/os",
    "client_info/os_version",
    "client_info/architecture",
    "client_info/app_build",
)


def _properties_for(exclude_modules, client_metrics=False):
    properties = list(_BASE_PROPERTIES)
    if exclude_modules:
        properties.append("metrics/object/hangs_reports")
    else:
        properties += [
            "metrics/object/hangs_modules",
            "metrics/object/hangs_reports",
        ]
    if client_metrics:
        properties.append("client_info/client_id")
    return properties


def get_prop(val, prop):
    """Walk a slash-delimited property path on a dict-like or Row-like value.

    Returns None if any intermediate node is None, matching the behaviour of
    the python_mozetl version. Works on plain dicts (used in tests) and on
    google.cloud.bigquery.Row objects (used in production).
    """
    if val is None:
        return None

    for key in prop.split("/"):
        val = val[key]
        if val is None:
            return None
    return val


def get_ping_properties(ping, properties):
    return {prop: get_prop(ping, prop) for prop in properties}


def properties_are_not_none(ping, properties):
    return all(ping[prop] is not None for prop in properties)


def ping_is_valid(ping):
    if not isinstance(ping["client_info/os_version"], str):
        return False
    if not isinstance(ping["client_info/os"], str):
        return False
    if not isinstance(ping["client_info/app_build"], str):
        return False
    if ping["metrics/object/hangs_reports"] is None:
        return False
    return True


def compute_sample_slices(sample_size):
    """Translate a fractional sample-size into a discrete slice count.

    The BigQuery query uses ``ABS(MOD(FARM_FINGERPRINT(document_id), 10000)) < N``
    to deterministically sample pings. N here is the slice count: sample-size
    1.0 → 10000 slices (everything), 0.5 → 5000, 0.0002 → 2, etc. Clamped to
    at least 1 so a misconfigured 0 doesn't accidentally read zero rows.
    """
    raw = _MAX_SAMPLE_SLICES * sample_size
    return int(max(min(raw, _MAX_SAMPLE_SLICES), 1))


def build_query_sql(date, end_date, sample_slices, client_metrics=False):
    """Build the BigQuery SQL string. Pure function — no client needed.

    Exposed (rather than inlined into get_data) so tests can verify the
    SQL shape without mocking the BQ client.

    The submission window is the build-date window padded by 5 days on each
    side (a build's pings keep arriving for a few days). The build-date filter
    itself is done here in SQL rather than in Python: the old python_mozetl job
    fetched the whole submission window and filtered build dates client-side,
    which means streaming ~10 days of builds just to keep one day's worth.
    Filtering in SQL cuts the result set (and the runtime) by roughly that
    factor.

    Each ping is also held to its own build's padded range, so that every day
    of a multi-day window gets exactly the pings a single-day run would give it
    rather than the late ones up to the end of the window. The window-wide
    range stays too: only a constant range prunes partitions, and so the scan.

    Only the handful of fields the pipeline actually reads are selected, not the
    whole client_info/metrics records (metrics in particular is large). They're
    rebuilt as STRUCTs so the rows keep the same nested shape get_prop expects
    (client_info.os, metrics.object.hangs_reports, ...). The build date is also
    selected on its own, since a table can only be clustered on a top-level
    column.
    """
    submission_start = date - timedelta(days=_SUBMISSION_PADDING_DAYS)
    submission_end = end_date + timedelta(days=_SUBMISSION_PADDING_DAYS)
    date_str = date.strftime("%Y%m%d")
    end_date_str = end_date.strftime("%Y%m%d")
    # SAFE so a malformed app_build drops its ping instead of failing the query.
    ping_build_date = "SAFE.PARSE_DATE('%Y%m%d', SUBSTR(client_info.app_build, 1, 8))"
    client_id_field = (
        ",\n        client_info.client_id AS client_id" if client_metrics else ""
    )
    return f"""
    SELECT
      SUBSTR(client_info.app_build, 1, 8) AS build_date,
      STRUCT(
        client_info.os AS os,
        client_info.os_version AS os_version,
        client_info.architecture AS architecture,
        client_info.app_build AS app_build{client_id_field}
      ) AS client_info,
      STRUCT(
        STRUCT(
          metrics.object.hangs_modules AS hangs_modules,
          metrics.object.hangs_reports AS hangs_reports
        ) AS object
      ) AS metrics
    FROM
      `{_BQ_TABLE}`
    WHERE
      -- Use document_id to sample
      ABS(MOD(FARM_FINGERPRINT(document_id), {_MAX_SAMPLE_SLICES})) < {sample_slices}
      AND submission_timestamp BETWEEN '{submission_start}' AND '{submission_end}'
      -- Keep only pings whose build date is in the requested window. app_build
      -- starts with YYYYMMDD; compare those first 8 characters.
      AND SUBSTR(client_info.app_build, 1, 8) BETWEEN '{date_str}' AND '{end_date_str}'
      -- And submitted within its own build date's padded range.
      AND submission_timestamp BETWEEN
        TIMESTAMP(DATE_SUB({ping_build_date}, INTERVAL {_SUBMISSION_PADDING_DAYS} DAY))
        AND TIMESTAMP(DATE_ADD({ping_build_date}, INTERVAL {_SUBMISSION_PADDING_DAYS} DAY))
    """


def _query_bigquery(sql, billing_project, reuse):
    """Run sql against BigQuery and return an iterator over the result rows."""
    client, destination = _materialize(sql, billing_project, reuse)
    return client.list_rows(destination)


def _materialize(sql, billing_project, reuse, clustered=False):
    """Run sql into a temp table, clustered by build_date if `clustered`, and
    return the client and the table's id.

    Selecting production-scale results back inline fails with "Response too
    large to return" (BigQuery's getQueryResults path is capped). So instead of
    selecting rows directly, we wrap the query in a CREATE TABLE ... AS SELECT
    that writes the rows to a temp table, which is then read with list_rows
    (paginated tabledata.list) so memory stays bounded.

    Because the job is a CREATE TABLE statement, query_job.result() returns an
    empty result (the rows went to the table, not inline), so it is safe to
    block on and never hits the large-result limit.

    The table is created with OPTIONS(expiration_timestamp=...), so BigQuery
    reclaims it on schedule even if this process is killed right afterwards (and
    the mozdata.tmp dataset has a default expiration as a further backstop).

    With `reuse`, the table is named after a hash of the query and kept for
    _REUSABLE_MATERIALIZATION_EXPIRY_HOURS, and one that already exists is read
    rather than scanned for again, its expiry first pushed back as far so that
    it outlives the read: CREATE TABLE AS only creates the table once its query
    has succeeded, so an existing one holds a complete result, as of when it
    was created. A run that dies after its scan can then be retried without
    paying for it twice. Without `reuse`, the table gets a name of its own.

    So a retry still pays for a new scan in two cases. One is a run without
    `reuse`, as the daily cron's always is. The other is runs of the same query
    that overlap, whether started together or retried while the first run's
    query is still running in BigQuery: each scans, and the later CREATE fails
    with Conflict and reads the earlier table instead of replacing it.

    google-cloud-bigquery is imported lazily, so the module imports without the
    package installed.
    """
    from google.api_core.exceptions import Conflict, NotFound, PreconditionFailed
    from google.cloud import bigquery

    client = bigquery.Client(project=billing_project)
    if reuse:
        name = hashlib.sha256(sql.encode()).hexdigest()[:32]
        expiry_hours = _REUSABLE_MATERIALIZATION_EXPIRY_HOURS
    else:
        name = uuid.uuid4().hex
        expiry_hours = _MATERIALIZATION_EXPIRY_HOURS
    destination = (
        f"{_MATERIALIZATION_PROJECT}.{_MATERIALIZATION_DATASET}.bhr_aggregate_{name}"
    )
    if reuse:
        try:
            table = client.get_table(destination)
        except NotFound:
            pass
        else:
            table.expires = datetime.now(timezone.utc) + timedelta(hours=expiry_hours)
            try:
                client.update_table(table, ["expires"])
            except PreconditionFailed:
                # Its ETag moved: a concurrent run reusing it pushed it back first.
                pass
            print(f"Reusing {destination} from an earlier run", flush=True)
            return client, destination

    cluster = "CLUSTER BY build_date\n" if clustered else ""
    create_table_sql = (
        f"CREATE TABLE `{destination}`\n"
        f"{cluster}"
        f"OPTIONS (expiration_timestamp = TIMESTAMP_ADD("
        f"CURRENT_TIMESTAMP(), INTERVAL {expiry_hours} HOUR))\n"
        f"AS\n{sql}"
    )
    query_job = client.query(create_table_sql)
    print(f"BigQuery job {query_job.job_id} writing to {destination}", flush=True)
    try:
        query_job.result()  # Block until the table is built; empty result, raises on failure.
    except Conflict:
        if not reuse:
            raise
        # A concurrent run of the same query created it: same rows, expiry just set.
        print(f"{destination} was created by a concurrent run; reading it", flush=True)
    return client, destination


def get_data(
    date,
    sample_size,
    billing_project,
    end_date=None,
    exclude_modules=False,
    client_metrics=False,
    reuse=False,
):
    """Stream BHR pings from BigQuery, filtered to the active build-date window.

    Args:
        date: datetime.date marking the start of the build-id window
        sample_size: float in [0, 1]; fraction of pings to read
        billing_project: GCP project to bill the BQ query against
        end_date: datetime.date for the end of the build-id window
            (default: same as date — a single-day window)
        exclude_modules: skip the hangs_modules column if True
        reuse: read an earlier run's table for the same query if one exists
            (see _REUSABLE_MATERIALIZATION_EXPIRY_HOURS) instead of scanning
            again. A recent range's table may be missing pings that arrived
            since.

    Yields:
        Dicts with slash/style keys (one per matching ping) — the same shape
        the rest of the aggregation pipeline expects.
    """
    for _, pings in get_data_by_day(
        date,
        sample_size,
        billing_project,
        end_date=end_date,
        exclude_modules=exclude_modules,
        client_metrics=client_metrics,
        reuse=reuse,
    ):
        yield from pings


def get_data_by_day(
    date,
    sample_size,
    billing_project,
    end_date=None,
    exclude_modules=False,
    client_metrics=False,
    reuse=False,
):
    """Stream BHR pings from BigQuery one build date at a time.

    Takes get_data's arguments and yields (build_date, pings) for every day of
    the window in order, pings being that day's as get_data yields them. A
    window of several days is scanned once into a table clustered by build
    date, and each day is read from it only when its turn comes, so a caller
    can be done with one day before the next is read. `reuse` applies to the
    scan alone: a day's table only copies that day's blocks, so it is cheap to
    make again.
    """
    if end_date is None:
        end_date = date

    sample_slices = compute_sample_slices(sample_size)
    sql = build_query_sql(date, end_date, sample_slices, client_metrics=client_metrics)
    properties = _properties_for(exclude_modules, client_metrics=client_metrics)
    build_dates = [
        (date + timedelta(days=i)).strftime("%Y%m%d")
        for i in range((end_date - date).days + 1)
    ]
    if len(build_dates) == 1:
        rows = _query_bigquery(sql, billing_project, reuse)
        yield build_dates[0], _pings(rows, properties)
        return

    _, window_table_id = _materialize(sql, billing_project, reuse, clustered=True)
    for build_date in build_dates:
        day_sql = f"SELECT * FROM `{window_table_id}` WHERE build_date = '{build_date}'"
        rows = _query_bigquery(day_sql, billing_project, reuse=False)
        yield build_date, _pings(rows, properties)


def _pings(rows, properties):
    # The build-date window is filtered in SQL (see build_query_sql), so every
    # row that comes back is already in-window. Here we only drop rows that are
    # missing a required field.
    total = 0
    kept = 0
    for row in rows:
        total += 1
        if total % _PROGRESS_EVERY_ROWS == 0:
            print(f"  ...read {total:,} rows ({kept:,} kept)", flush=True)
        ping = get_ping_properties(row, properties)
        if not properties_are_not_none(ping, properties):
            continue
        kept += 1
        yield ping

    print(f"{total} results total", flush=True)
    print(f"{kept} results after field filter", flush=True)


def collect_offsets_by_module(hangs):
    """Group unique (module, offset) frames by module across a set of hangs.

    Each hang is expected to be a tuple whose first element is a list of
    (module, offset) frames as produced by process_frame. The returned
    dict has one entry per distinct module, mapping it to the set of
    distinct offsets seen for that module across all hangs.

    This is the input shape symbolication.symbolicate_modules() consumes:
    one symbol-server fetch per module, then bisect-resolve every offset
    against the parsed symbol map.

    Replaces the PySpark RDD pattern from the python_mozetl version:
        hangs.flatMap(...).map(...).distinct().reduceByKey(...)
    """
    by_module = {}
    for hang in hangs:
        for module, offset in hang[0]:
            by_module.setdefault(module, set()).add(offset)
    return by_module


def process_frame(frame, modules):
    """Turn one raw stack frame into a (module, offset) pair.

    Glean delivers frames as {"frame": offset, "module": index} dicts. A
    missing module index means a pseudo (label) frame; an out-of-range
    index means the module table didn't have an entry, so we keep the
    offset but drop the module. The list form and the bare-value fallback
    handle legacy/odd shapes the python_mozetl version also accepted.
    """
    # Glean format: {"frame": "...", "module": 0}
    if isinstance(frame, dict):
        module_index = frame.get("module")
        offset = frame.get("frame")

        if module_index is None:
            return (("pseudo", None), offset)

        if module_index < 0 or module_index >= len(modules):
            return (None, offset)

        debug_name, breakpad_id = modules[module_index]
        return ((debug_name, breakpad_id), offset)

    # Legacy telemetry format: [module_index, offset]
    if isinstance(frame, list):
        module_index, offset = frame
        if module_index is None or module_index < 0 or module_index >= len(modules):
            return (None, offset)
        debug_name, breakpad_id = modules[module_index]
        return ((debug_name, breakpad_id), offset)

    # Pseudo frame fallback.
    return (("pseudo", None), frame)


def filter_hang(hang, config):
    """Keep only hangs on the thread we care about with a sane stack length.

    Uses .get() rather than indexing because some Glean hang records arrive
    without a stack field (see the python_mozetl follow-up that fixed the
    crash on larger samples).
    """
    stack = hang.get("stack")
    return (
        hang.get("thread") == config["thread_filter"]
        and isinstance(stack, list)
        and len(stack) > 0
        and len(stack) < 300
    )


def process_hang(hang):
    """Normalise a hang record to a plain dict.

    BigQuery rows come back as Row objects with an asDict() method; plain
    dicts (used in tests) are returned unchanged.
    """
    if hasattr(hang, "asDict"):
        return hang.asDict(recursive=True)
    return hang


def process_hangs(ping, config):
    """Expand one ping into a list of per-hang tuples.

    Each returned tuple is:
        (stack, duration, thread, runnable_name, process, annotations,
         build_date, platform)
    where stack is a list of (module, offset) pairs from process_frame.
    """
    build_date = ping["client_info/app_build"][:8]  # "YYYYMMDD"
    platform = "{}".format(ping["client_info/os"])

    modules = ping["metrics/object/hangs_modules"]
    if isinstance(modules, str):
        modules = json.loads(modules)

    raw_hangs = ping["metrics/object/hangs_reports"]
    if isinstance(raw_hangs, str):
        raw_hangs = json.loads(raw_hangs)

    hangs = [process_hang(h) for h in raw_hangs]

    result = []
    for h in hangs:
        if not filter_hang(h, config):
            continue
        stack = [
            process_frame(frame, modules)
            for frame in h["stack"]
            if not isinstance(frame, list) or len(frame) == 2
        ]
        annotations = h.get("annotations") or []
        row = (
            stack,
            h["duration"],
            h["thread"],
            "",
            h["process"],
            annotations,
            build_date,
            platform,
        )
        # Under --client-metrics, carry the ping's client_id as the last element
        # so the affected-clients pass can attribute the hang. Stripped again
        # before group_hangs, which unpacks the 8-element aggregation row.
        if config.get("client_metrics"):
            row = row + (ping["client_info/client_id"],)
        result.append(row)

    return result


def symbolicate_stacks(stack, symbol_map):
    """Replace (module, offset) frames with (symbol, lib_name, inline_depth).

    symbol_map is the dict returned by symbolication.symbolicate_modules,
    keyed by (module, offset). Each value is a list of (symbol, lib_name)
    frames: usually one, but more than one when the address resolves inside
    inlined code, in which case the whole inlined chain is spliced into the
    stack in place (outer-first) so equivalent hangs dedup (bug 2052961). A
    frame with no module, no map entry, or a None leading symbol falls back to
    the UNSYMBOLICATED sentinel.

    Each frame carries its position in that chain as inline_depth: 0 for a
    real frame (the enclosing FUNC, or an address that inlined nothing) and
    1, 2, ... for each inlined callee. Once spliced, an inlined frame is
    otherwise indistinguishable from a real caller, so without this the
    frontend cannot tell them apart or fold them back (bug 2059443).
    """
    symbolicated = []
    for module, offset in stack:
        if module is not None:
            debug_name = module[0]
            processed = symbol_map.get((tuple(module), offset), None)
            if processed and processed[0][0] is not None:
                for depth, (name, lib) in enumerate(processed):
                    symbolicated.append((name, lib, depth))
            else:
                symbolicated.append((UNSYMBOLICATED, debug_name, 0))
        else:
            symbolicated.append((UNSYMBOLICATED, "unknown", 0))
    return symbolicated


def symbolicate_hang(hang, symbol_map):
    """Symbolicate a raw hang's stack and apply the signature heuristics.

    Takes a raw hang tuple (stack of (module, offset) frames, then the
    metadata fields) and returns the same tuple with its stack replaced by
    a symbolicated, heuristic-trimmed stack. Mirrors process_hang_key from
    python_mozetl.
    """
    stack = hang[0]
    symbolicated = symbolicate_stacks(stack, symbol_map)
    symbolicated = apply_hang_signature_heuristics(symbolicated)
    return (symbolicated,) + tuple(hang[1:])


def tupleize_annotation_list(annotations):
    """Sort annotations by key and freeze them into a hashable tuple.

    Annotations become part of the aggregation key, so they need a stable,
    hashable representation.
    """
    return tuple((k, v) for k, v in sorted(annotations, key=lambda x: x[0]))


def map_to_hang_data(hang, config):
    """Turn one symbolicated hang into a (key, (duration, count)) pair.

    The key bundles everything that makes two hangs "the same" for
    aggregation: the stack, runnable name, thread, build date, annotations,
    and platform. Hangs outside the configured duration bounds are dropped
    (returns an empty list so the caller can flat-map over it).
    """
    (
        stack,
        duration,
        thread,
        runnable_name,
        process,
        annotations,
        build_date,
        platform,
    ) = hang
    if duration < config["hang_lower_bound"]:
        return []
    if duration >= config["hang_upper_bound"]:
        return []

    # Inline depth is deliberately NOT part of the key. A build that inlined a
    # call and one that did not produce the same reconstructed stack, and the
    # point of bug 2052961 is that those merge. Depth rides in the value and is
    # max-merged, so a frame counts as inlined if any contributing build
    # inlined it.
    key = (
        tuple((a, b) for a, b, _ in stack),
        runnable_name,
        thread,
        build_date,
        tupleize_annotation_list(annotations),
        platform,
    )
    depths = tuple(depth for _, _, depth in stack)
    return [(key, (float(duration), 1.0, depths))]


def merge_hang_data(a, b):
    """Sum the (duration, count) of two hangs that share a key, max their depths.

    The stacks are identical (they are the key), so the depth tuples are the
    same length and merge element-wise.
    """
    return (a[0] + b[0], a[1] + b[1], tuple(map(max, a[2], b[2])))


def group_hangs(hangs, config):
    """Aggregate symbolicated hangs by key, summing duration and count.

    Replaces the python_mozetl Spark chain
    flatMap(map_to_hang_data).reduceByKey(merge_hang_data).collect().
    Returns a list of (key fields..., duration_sum, count_sum) tuples, the
    8-element shape ProfileProcessor.ingest consumes. The merged inline depths
    are folded back onto the stack frames rather than carried as a ninth field,
    so the row shape is unchanged.
    """
    grouped = {}
    for hang in hangs:
        for key, value in map_to_hang_data(hang, config):
            if key in grouped:
                grouped[key] = merge_hang_data(grouped[key], value)
            else:
                grouped[key] = value
    rows = []
    for key, (duration, count, depths) in grouped.items():
        stack, *rest = key
        with_depth = tuple(
            (name, lib, depth) for (name, lib), depth in zip(stack, depths)
        )
        rows.append((with_depth, *rest, duration, count))
    return rows


def write_file(name, data, output_dir):
    """Write `data` as JSON to output_dir/<name>.json, creating the dir."""
    os.makedirs(output_dir, exist_ok=True)
    path = os.path.join(output_dir, name + ".json")
    with open(path, "w", encoding="utf8") as json_file:
        json.dump(data, json_file, ensure_ascii=False)
    return path


def aggregate(
    date,
    sample_size,
    billing_project,
    output_dir="output",
    output_tag="main",
    end_date=None,
    config_overrides=None,
    reuse=False,
):
    """Run the full BHR aggregation for one build-date window.

    Wires together the whole pipeline:
        get_data         -> stream pings from BigQuery
        process_hangs    -> expand each ping into hang tuples
        collect_offsets_by_module + symbolicate_modules -> symbol lookup table
        symbolicate_hang -> symbolicate + trim each hang's stack
        group_hangs      -> aggregate by signature, per build date
        ProfileProcessor -> build the columnar output, per build date
        write_file       -> hangs_<tag>_<date>.json per day, plus
                            hangs_<tag>_current.json for the newest

    `date` through `end_date` is the build-date window to process, inclusive,
    defaulting to the single day `date`. A window costs one BigQuery scan
    instead of one per day, which is what makes a long backfill affordable, but
    it still writes each day as its own artifact: both the timeseries roll-up
    and the dashboard address one file per build date. A day in the window with
    no hangs gets an empty one. Each day is read, symbolicated and written
    before the next is read, so memory holds one day's hangs and a crash keeps
    the days already written. Streaming a day back still takes a while, so a
    window spans at most _MAX_WINDOW_DAYS days, and at most _MAX_SAMPLED_DAYS
    once multiplied by the sample size the query reads.

    usageHoursByDate is set to a dummy 1.0 because Glean doesn't provide usage
    hours in the legacy shape (the frontend only needs a non-null value here).

    Returns the profile dict for the newest day written.
    """
    config = dict(DEFAULT_CONFIG)
    if config_overrides:
        config.update(config_overrides)

    end_date = end_date or date
    assert end_date >= date, f"end_date {end_date} is before date {date}"
    date_str = date.strftime("%Y%m%d")
    end_date_str = end_date.strftime("%Y%m%d")
    days = (end_date - date).days + 1
    if days > _MAX_WINDOW_DAYS:
        raise ValueError(
            f"Build dates {date_str}..{end_date_str} span more than "
            f"{_MAX_WINDOW_DAYS} days"
        )
    sampled_days = days * compute_sample_slices(sample_size) / _MAX_SAMPLE_SLICES
    if sampled_days > _MAX_SAMPLED_DAYS:
        raise ValueError(
            f"Build dates {date_str}..{end_date_str} at sample size "
            f"{sampled_days / days:g} come to {sampled_days:g} days * sample "
            f"size, more than {_MAX_SAMPLED_DAYS:g}"
        )
    job_start = time.time()

    def _phase(message):
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / _MAXRSS_PER_GIB
        print(
            f"[{int(time.time() - job_start):>4}s, peak {peak:5.1f} GiB] {message}",
            flush=True,
        )

    window = date_str if end_date_str == date_str else f"{date_str}..{end_date_str}"
    _phase(f"Fetching pings from BigQuery for build date {window}...")
    base = "hangs_" + output_tag
    for build_date_str, pings in get_data_by_day(
        date,
        sample_size,
        billing_project,
        end_date=end_date,
        exclude_modules=config["exclude_modules"],
        client_metrics=config["client_metrics"],
        reuse=reuse,
    ):
        # Expand each valid ping into hang tuples. The pings themselves are not
        # retained; only the (much smaller) hang set is, for this day alone.
        hangs = []
        for ping in pings:
            if not ping_is_valid(ping):
                continue
            hangs.extend(process_hangs(ping, config))
        _phase(f"{build_date_str}: collected {len(hangs)} hangs.")

        # Symbolicate: one symbol-server fetch per unique module, then resolve
        # every hang's stack and apply the signature heuristics.
        frames_by_module = collect_offsets_by_module(hangs)
        _phase(
            f"{build_date_str}: symbolicating {len(frames_by_module)} unique modules..."
        )
        symbol_map = symbolicate_modules(frames_by_module, config)
        _phase(f"{build_date_str}: resolved {len(symbol_map)} (module, offset) frames.")
        day_hangs = [symbolicate_hang(hang, symbol_map) for hang in hangs]

        # Affected-client counts, computed before the client_id is stripped off
        # (group_hangs and ProfileProcessor consume the 8-element row).
        affected_clients = None
        if config["client_metrics"]:
            affected_clients = _compute_affected_clients(day_hangs)
            _phase(
                f"{build_date_str}: affected clients (distinct HLL): "
                f"{affected_clients['totalDistinct']}"
            )
            day_hangs = [hang[:8] for hang in day_hangs]

        grouped = group_hangs(day_hangs, config)
        _phase(f"{build_date_str}: aggregated into {len(grouped)} hang signatures.")
        # Each day is its own document, so it gets its own uuid.
        day_config = dict(config)
        day_config["uuid"] = uuid.uuid4().hex
        processor = ProfileProcessor(day_config)
        processor.ingest(grouped, {build_date_str: 1.0})
        profile = processor.process_into_profile()

        if affected_clients is not None:
            profile["affectedClients"] = affected_clients

        # Skipped in split-thread mode (a list of per-thread payloads), which the
        # daily job does not use.
        if config["leaf_grouping"] and not config["split_threads_in_out_file"]:
            profile["leafGroups"] = compute_leaf_groups(profile)
            group_count = sum(len(groups) for groups in profile["leafGroups"].values())
            _phase(
                f"{build_date_str}: grouped near-duplicates into {group_count} groups."
            )

        written = write_file(f"{base}_{build_date_str}", profile, output_dir)
        _phase(f"Wrote {written}")

    # `current` is the newest day produced, which for the daily cron is the
    # only one.
    write_file(f"{base}_current", profile, output_dir)
    return profile
