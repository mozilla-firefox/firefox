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

import json
import os
import time
import uuid
from datetime import timedelta

from client_metrics import HyperLogLog
from heuristics import apply_hang_signature_heuristics
from leaf_grouping import compute_leaf_groups
from profile_processor import ProfileProcessor
from symbolication import UNSYMBOLICATED, symbolicate_modules

_BQ_TABLE = "mozdata.firefox_desktop.hang_report_redacted"
_MAX_SAMPLE_SLICES = 10000

# Project/dataset where query results are materialized before being read back.
# Large result sets cannot be returned inline, so the query writes to a temp
# table here first. We pin this to mozdata.tmp (independent of the billing
# project) to match the old job, which always materialized to mozdata.tmp while
# billing the query to a separately configured project.
_MATERIALIZATION_PROJECT = "mozdata"
_MATERIALIZATION_DATASET = "tmp"
# Lifetime of the temp result table. Set as an OPTION at table-creation time so
# it applies even if this process dies before it can clean anything up.
_MATERIALIZATION_EXPIRY_HOURS = 6

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

    Only the handful of fields the pipeline actually reads are selected, not the
    whole client_info/metrics records (metrics in particular is large). They're
    rebuilt as STRUCTs so the rows keep the same nested shape get_prop expects
    (client_info.os, metrics.object.hangs_reports, ...).
    """
    submission_start = date - timedelta(days=5)
    submission_end = end_date + timedelta(days=5)
    date_str = date.strftime("%Y%m%d")
    end_date_str = end_date.strftime("%Y%m%d")
    client_id_field = (
        ",\n        client_info.client_id AS client_id" if client_metrics else ""
    )
    return f"""
    SELECT
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
    """


def _query_bigquery(sql, billing_project):
    """Run sql against BigQuery and return an iterator over the result rows.

    Selecting production-scale results back inline fails with "Response too
    large to return" (BigQuery's getQueryResults path is capped). So instead of
    selecting rows directly, we wrap the query in a CREATE OR REPLACE TABLE ...
    AS SELECT that writes the rows to a temp table, then read that table with
    list_rows (paginated tabledata.list) so memory stays bounded.

    Because the job is a CREATE TABLE statement, query_job.result() returns an
    empty result (the rows went to the table, not inline), so it is safe to
    block on and never hits the large-result limit.

    The table is created with OPTIONS(expiration_timestamp=...), so BigQuery
    reclaims it on schedule even if this process is killed right afterwards (and
    the mozdata.tmp dataset has a default expiration as a further backstop).

    google-cloud-bigquery is imported lazily and this whole function is the
    single BigQuery touchpoint, so the module imports without the package and
    unit tests can stub it.
    """
    from google.cloud import bigquery

    client = bigquery.Client(project=billing_project)
    destination = (
        f"{_MATERIALIZATION_PROJECT}.{_MATERIALIZATION_DATASET}"
        f".bhr_aggregate_{uuid.uuid4().hex}"
    )
    create_table_sql = (
        f"CREATE OR REPLACE TABLE `{destination}`\n"
        f"OPTIONS (expiration_timestamp = TIMESTAMP_ADD("
        f"CURRENT_TIMESTAMP(), INTERVAL {_MATERIALIZATION_EXPIRY_HOURS} HOUR))\n"
        f"AS\n{sql}"
    )
    query_job = client.query(create_table_sql)
    print(f"BigQuery job {query_job.job_id} writing to {destination}", flush=True)
    query_job.result()  # Block until the table is built; empty result, raises on failure.
    return client.list_rows(destination)


def get_data(
    date,
    sample_size,
    billing_project,
    end_date=None,
    exclude_modules=False,
    client_metrics=False,
):
    """Stream BHR pings from BigQuery, filtered to the active build-date window.

    Args:
        date: datetime.date marking the start of the build-id window
        sample_size: float in [0, 1]; fraction of pings to read
        billing_project: GCP project to bill the BQ query against
        end_date: datetime.date for the end of the build-id window
            (default: same as date — a single-day window)
        exclude_modules: skip the hangs_modules column if True

    Yields:
        Dicts with slash/style keys (one per matching ping) — the same shape
        the rest of the aggregation pipeline expects.
    """
    if end_date is None:
        end_date = date

    sample_slices = compute_sample_slices(sample_size)
    sql = build_query_sql(date, end_date, sample_slices, client_metrics=client_metrics)
    properties = _properties_for(exclude_modules, client_metrics=client_metrics)

    # The build-date window is filtered in SQL (see build_query_sql), so every
    # row that comes back is already in-window. Here we only drop rows that are
    # missing a required field.
    total = 0
    kept = 0
    for row in _query_bigquery(sql, billing_project):
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
):
    """Run the full BHR aggregation for one build-date window.

    Wires together the whole pipeline:
        get_data         -> stream pings from BigQuery
        process_hangs    -> expand each ping into hang tuples
        collect_offsets_by_module + symbolicate_modules -> symbol lookup table
        symbolicate_hang -> symbolicate + trim each hang's stack
        group_hangs      -> aggregate by signature
        ProfileProcessor -> build the columnar output
        write_file       -> hangs_<tag>_<date>.json and hangs_<tag>_current.json

    `date` is the build date to process. usageHoursByDate is set to a dummy
    1.0 because Glean doesn't provide usage hours in the legacy shape (the
    frontend only needs a non-null value here).

    Returns the profile dict that was written.
    """
    config = dict(DEFAULT_CONFIG)
    config["uuid"] = uuid.uuid4().hex
    if config_overrides:
        config.update(config_overrides)

    date_str = date.strftime("%Y%m%d")
    job_start = time.time()

    def _phase(message):
        print(f"[{int(time.time() - job_start):>4}s] {message}", flush=True)

    # Stream pings and expand each valid one into hang tuples. The pings
    # themselves are not retained; only the (much smaller) hang set is.
    _phase(f"Fetching pings from BigQuery for build date {date_str}...")
    hangs = []
    for ping in get_data(
        date,
        sample_size,
        billing_project,
        end_date=end_date,
        exclude_modules=config["exclude_modules"],
        client_metrics=config["client_metrics"],
    ):
        if not ping_is_valid(ping):
            continue
        hangs.extend(process_hangs(ping, config))
    _phase(f"Collected {len(hangs)} hangs.")

    # Symbolicate: one symbol-server fetch per unique module, then resolve
    # every hang's stack and apply the signature heuristics.
    frames_by_module = collect_offsets_by_module(hangs)
    _phase(f"Symbolicating {len(frames_by_module)} unique modules...")
    symbol_map = symbolicate_modules(frames_by_module, config)
    _phase(f"Resolved {len(symbol_map)} (module, offset) frames.")
    symbolicated = [symbolicate_hang(hang, symbol_map) for hang in hangs]

    # Affected-client counts, computed before the client_id is stripped off
    # (group_hangs and ProfileProcessor consume the 8-element row).
    affected_clients = None
    if config["client_metrics"]:
        affected_clients = _compute_affected_clients(symbolicated)
        _phase(
            f"Affected clients (day, distinct HLL): {affected_clients['totalDistinct']}"
        )
        symbolicated = [hang[:8] for hang in symbolicated]

    # Aggregate by signature and build the columnar profile.
    grouped = group_hangs(symbolicated, config)
    _phase(f"Aggregated into {len(grouped)} hang signatures.")
    _phase("Building profile...")
    processor = ProfileProcessor(config)
    processor.ingest(grouped, {date_str: 1.0})
    profile = processor.process_into_profile()

    if affected_clients is not None:
        profile["affectedClients"] = affected_clients

    # Skipped in split-thread mode (a list of per-thread payloads), which the
    # daily job does not use.
    if config["leaf_grouping"] and not config["split_threads_in_out_file"]:
        profile["leafGroups"] = compute_leaf_groups(profile)
        group_count = sum(len(groups) for groups in profile["leafGroups"].values())
        _phase(f"Grouped near-duplicates into {group_count} groups.")

    base = "hangs_" + output_tag
    written = write_file(f"{base}_{date_str}", profile, output_dir)
    write_file(f"{base}_current", profile, output_dir)
    _phase(f"Wrote {written}")
    return profile
