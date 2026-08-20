# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Tests for the bhr_timeseries secondary job."""

import datetime
import gzip
import json
import os
import sys

import mozunit

_HERE = os.path.dirname(os.path.abspath(__file__))
_AGGREGATION_DIR = os.path.dirname(_HERE)
if _AGGREGATION_DIR not in sys.path:
    sys.path.insert(0, _AGGREGATION_DIR)

import bhr_timeseries  # noqa: E402
from client_metrics import HyperLogLog  # noqa: E402
from profile_processor import ProfileProcessor  # noqa: E402


def _make_processor():
    return ProfileProcessor({
        "use_minimal_sample_table": False,
        "post_sample_size": 1.0,
        "stack_acceptance_threshold": 0.0,
        "print_debug_info": False,
        "uuid": "test-uuid",
        "split_threads_in_out_file": False,
    })


def _row(stack, build_date, hang_ms, hang_count=1.0):
    # Frames are (func, lib, inline_depth); these tests are about cross-day
    # aggregation, not inlining, so they pass plain (func, lib) pairs and get
    # depth 0.
    stack = [f if len(f) == 3 else (f[0], f[1], 0) for f in stack]
    return (
        stack,
        "",
        "Gecko",
        build_date,
        [("UserInteracting", "true")],
        "Linux",
        hang_ms,
        hang_count,
    )


def _profile_for_day(rows, date_str):
    processor = _make_processor()
    processor.ingest(rows, {date_str: 1.0})
    return processor.process_into_profile()


STACK_A = [("frameA", "xul"), ("leafA", "xul")]
STACK_B = [("frameB", "xul"), ("leafB", "xul")]

# Reconstructed stacks are leaf->root, so the canonical key reverses the
# child->leaf order the rows are built with.
KEY_A = bhr_timeseries.canonical_key([["leafA", "xul"], ["frameA", "xul"]])
KEY_B = bhr_timeseries.canonical_key([["leafB", "xul"], ["frameB", "xul"]])


def test_canonical_key_is_order_sensitive_and_stable():
    key1 = bhr_timeseries.canonical_key([["a", "xul"], ["b", "xul"]])
    key2 = bhr_timeseries.canonical_key([["b", "xul"], ["a", "xul"]])
    assert key1 != key2
    assert key1 == bhr_timeseries.canonical_key([["a", "xul"], ["b", "xul"]])


def test_reconstruct_stack_round_trips_through_profile():
    profile = _profile_for_day([_row(STACK_A, "20260401", 100.0)], "20260401")
    thread = bhr_timeseries.pick_thread(profile)
    frames = bhr_timeseries.reconstruct_stack(thread, 0)
    assert frames == [["leafA", "xul"], ["frameA", "xul"]]


def test_aggregate_day_dedups_identical_stacks():
    profile = _profile_for_day(
        [
            _row(STACK_A, "20260401", 100.0, 1.0),
            _row(STACK_A, "20260401", 50.0, 1.0),
            _row(STACK_B, "20260401", 30.0, 1.0),
        ],
        "20260401",
    )
    day, total_sketch = bhr_timeseries.aggregate_day(profile, per_day_top_n=10)
    assert day[KEY_A]["ms"] == 150.0
    assert day[KEY_A]["count"] == 2.0
    assert day[KEY_B]["ms"] == 30.0
    # No client metrics on this profile, so no sketches are attached.
    assert day[KEY_A]["sketch"] is None
    assert total_sketch is None


def test_aggregate_day_keeps_only_top_n_by_ms():
    profile = _profile_for_day(
        [_row(STACK_A, "20260401", 100.0), _row(STACK_B, "20260401", 30.0)],
        "20260401",
    )
    day, _ = bhr_timeseries.aggregate_day(profile, per_day_top_n=1)
    assert list(day) == [KEY_A]


def test_window_dates_is_inclusive_and_sorted():
    dates = bhr_timeseries.window_dates("20260403", window_days=3)
    assert dates == ["20260401", "20260402", "20260403"]


def _write_profile(directory, date_str, rows, tag="main"):
    profile = _profile_for_day(rows, date_str)
    path = os.path.join(directory, f"hangs_{tag}_{date_str}.json")
    with open(path, "w", encoding="utf-8") as out:
        json.dump(profile, out)
    return path


def test_build_timeseries_end_to_end(tmpdir):
    work = str(tmpdir)
    _write_profile(work, "20260401", [_row(STACK_A, "20260401", 100.0)])
    _write_profile(
        work,
        "20260402",
        [_row(STACK_A, "20260402", 40.0), _row(STACK_B, "20260402", 200.0)],
    )

    published = bhr_timeseries.build_timeseries(
        input_dir=work, output_dir=work, window_days=2, top_count=10
    )

    assert published["dates"] == ["20260401", "20260402"]
    by_leaf = {tuple(s["frames"][0]): s for s in published["signatures"]}
    # STACK_B only occurs on day 2; its day-1 entry is filled with zero.
    sig_b = by_leaf[("leafB", "xul")]
    assert sig_b["ms"] == [0.0, 200.0]
    sig_a = by_leaf[("leafA", "xul")]
    assert sig_a["ms"] == [100.0, 40.0]
    assert sig_a["totalMs"] == 140.0

    assert os.path.exists(os.path.join(work, "hangs_timeseries_main.json"))
    assert os.path.exists(os.path.join(work, "hangs_timeseries_main_state.json.gz"))
    # Profiles without client metrics: affected-users fields are omitted, not
    # emitted as misleading zeros.
    assert "totalUsers" not in published
    assert "affectedUsers" not in sig_a


def test_build_timeseries_is_incremental_and_prunes(tmpdir):
    work = str(tmpdir)
    _write_profile(work, "20260401", [_row(STACK_A, "20260401", 100.0)])
    _write_profile(work, "20260402", [_row(STACK_B, "20260402", 200.0)])
    bhr_timeseries.build_timeseries(
        input_dir=work, output_dir=work, window_days=2, top_count=10
    )

    # A third day arrives; the window slides forward and day 1 drops out.
    _write_profile(work, "20260403", [_row(STACK_A, "20260403", 70.0)])
    published = bhr_timeseries.build_timeseries(
        input_dir=work, output_dir=work, window_days=2, top_count=10
    )

    assert published["dates"] == ["20260402", "20260403"]
    state_path = os.path.join(work, "hangs_timeseries_main_state.json.gz")
    with gzip.open(state_path, "rt", encoding="utf-8") as state_file:
        state = json.load(state_file)
    assert "20260401" not in state["days"]
    assert set(state["days"]) == {"20260402", "20260403"}


def test_top_count_caps_published_signatures(tmpdir):
    work = str(tmpdir)
    _write_profile(
        work,
        "20260401",
        [_row(STACK_A, "20260401", 100.0), _row(STACK_B, "20260401", 30.0)],
    )
    published = bhr_timeseries.build_timeseries(
        input_dir=work, output_dir=work, window_days=1, top_count=1
    )
    assert len(published["signatures"]) == 1
    assert published["signatures"][0]["frames"][0] == ["leafA", "xul"]


STACK_C = [("frameC", "xul"), ("leafC", "xul")]


def test_day_without_an_artifact_is_null_not_zero(tmpdir):
    # A gap in the window (the job did not run, or the artifact is gone) is not
    # evidence that the hang stopped happening.
    work = str(tmpdir)
    _write_profile(work, "20260401", [_row(STACK_A, "20260401", 100.0)])
    _write_profile(work, "20260403", [_row(STACK_A, "20260403", 70.0)])

    published = bhr_timeseries.build_timeseries(
        input_dir=work,
        output_dir=work,
        end_date=datetime.date(2026, 4, 3),
        window_days=3,
        top_count=10,
    )

    assert published["dates"] == ["20260401", "20260402", "20260403"]
    sig = published["signatures"][0]
    assert sig["ms"] == [100.0, None, 70.0]
    assert sig["count"] == [1.0, None, 1.0]
    # The totals only ever sum the days we actually have.
    assert sig["totalMs"] == 170.0


def test_truncated_day_reports_null_for_signatures_below_the_cut(tmpdir):
    # Day 1 sees three signatures but keeps two, so anything it dropped is
    # unknown rather than absent. Day 2 sees one and keeps it, so a signature
    # missing from day 2 really did not hang that day.
    work = str(tmpdir)
    _write_profile(
        work,
        "20260401",
        [
            _row(STACK_A, "20260401", 500.0),
            _row(STACK_B, "20260401", 400.0),
            _row(STACK_C, "20260401", 10.0),
        ],
    )
    _write_profile(work, "20260402", [_row(STACK_C, "20260402", 300.0)])

    published = bhr_timeseries.build_timeseries(
        input_dir=work,
        output_dir=work,
        window_days=2,
        top_count=10,
        per_day_top_n=2,
    )

    by_leaf = {tuple(s["frames"][0]): s for s in published["signatures"]}
    # STACK_C was cut from day 1, so that day is unknown, not a zero.
    assert by_leaf[("leafC", "xul")]["ms"] == [None, 300.0]
    # Day 2 kept everything it saw, so STACK_A's absence there is a real zero.
    assert by_leaf[("leafA", "xul")]["ms"] == [500.0, 0.0]


def test_day_is_complete_only_below_the_cap():
    # At the cap we cannot tell "kept everything" from "dropped something", so
    # the day is treated as truncated.
    assert bhr_timeseries.day_is_complete({"a": 1}, 2) is True
    assert bhr_timeseries.day_is_complete({"a": 1, "b": 2}, 2) is False
    # No cap recorded (older state) means nothing was ever dropped.
    assert bhr_timeseries.day_is_complete({"a": 1}, None) is True


def _sketch_for(client_ids):
    """Serialized HLL sketch over a set of client ids, as the primary job emits."""
    hll = HyperLogLog()
    for client_id in client_ids:
        hll.add(client_id)
    return hll.serialize()


def _reference_count(*client_id_sets):
    """Exact HLL count over the union of several client-id sets, for comparison.

    Built by the same estimator the job uses, so the assertion checks that the
    job merged the right days rather than depending on HLL being exact.
    """
    hll = HyperLogLog()
    for client_ids in client_id_sets:
        for client_id in client_ids:
            hll.add(client_id)
    return hll.count()


def _write_profile_with_clients(directory, date_str, rows, clients_by_key, tag="main"):
    """Write a daily profile and graft on an affectedClients block.

    `clients_by_key` maps a canonical signature key to the set of client ids
    that hit it that day; the day total is their union.
    """
    profile = _profile_for_day(rows, date_str)
    day_total = set()
    for client_ids in clients_by_key.values():
        day_total |= client_ids
    profile["affectedClients"] = {
        "totalSketch": _sketch_for(day_total),
        "sketchBySignature": {
            key: _sketch_for(client_ids) for key, client_ids in clients_by_key.items()
        },
    }
    path = os.path.join(directory, f"hangs_{tag}_{date_str}.json")
    with open(path, "w", encoding="utf-8") as out:
        json.dump(profile, out)
    return path


def test_affected_users_merge_across_the_window(tmpdir):
    work = str(tmpdir)
    # STACK_A hit by {c1,c2,c3} on day 1 and {c3,c4} on day 2 (c3 recurs, so the
    # distinct union is 4). STACK_B hit by {c5} on day 2 only.
    _write_profile_with_clients(
        work,
        "20260401",
        [_row(STACK_A, "20260401", 100.0)],
        {KEY_A: {"c1", "c2", "c3"}},
    )
    _write_profile_with_clients(
        work,
        "20260402",
        [_row(STACK_A, "20260402", 40.0), _row(STACK_B, "20260402", 200.0)],
        {KEY_A: {"c3", "c4"}, KEY_B: {"c5"}},
    )

    published = bhr_timeseries.build_timeseries(
        input_dir=work, output_dir=work, window_days=2, top_count=10
    )

    # Distinct users across the whole (2-day) window: {c1..c5}.
    assert published["totalUsers"]["d365"] == _reference_count({
        "c1",
        "c2",
        "c3",
        "c4",
        "c5",
    })
    assert published["affectedWindows"] == [7, 28, 365]

    by_leaf = {tuple(s["frames"][0]): s for s in published["signatures"]}
    sig_a = by_leaf[("leafA", "xul")]
    sig_b = by_leaf[("leafB", "xul")]

    # STACK_A's union deduplicates the recurring client c3 down to 4 users.
    assert sig_a["affectedUsers"]["d365"] == _reference_count(
        {"c1", "c2", "c3"}, {"c3", "c4"}
    )
    assert sig_b["affectedUsers"]["d365"] == _reference_count({"c5"})

    # Every window is a suffix of a 2-day span, so all collapse to the full
    # union; the percentage is affected / total over that same window.
    total = published["totalUsers"]["d365"]
    for label in ("d7", "d28", "d365"):
        assert sig_a["affectedUsers"][label] == sig_a["affectedUsers"]["d365"]
        assert sig_a["affectedPct"][label] == sig_a["affectedUsers"][label] / total

    # Per-day affected series runs parallel to `dates`: STACK_A on day 1 hit
    # {c1,c2,c3} (3) and day 2 {c3,c4} (2); STACK_B only day 2 (1).
    assert published["dates"] == ["20260401", "20260402"]
    assert sig_a["affected"] == [
        _reference_count({"c1", "c2", "c3"}),
        _reference_count({"c3", "c4"}),
    ]
    assert sig_b["affected"] == [0, _reference_count({"c5"})]
    # Daily distinct users across all signatures: day 1 {c1,c2,c3}, day 2 {c3,c4,c5}.
    assert published["totalAffected"] == [
        _reference_count({"c1", "c2", "c3"}),
        _reference_count({"c3", "c4", "c5"}),
    ]


def test_affected_windows_are_trailing_suffixes(tmpdir):
    # A client that only appears on the oldest day must fall out of a window
    # that no longer covers that day. Exercise merge_window_counts directly so
    # we can use a short window without generating hundreds of profiles.
    sketch_by_date = {
        "20260401": _sketch_for({"old-only"}),
        "20260402": _sketch_for({"c1", "c2"}),
        "20260403": _sketch_for({"c2", "c3"}),
    }
    dates = ["20260401", "20260402", "20260403"]

    # Patch the window definition to a trailing 2-day window for this check.
    original = bhr_timeseries.AFFECTED_WINDOWS
    bhr_timeseries.AFFECTED_WINDOWS = (("d2", 2), ("dall", 365))
    try:
        counts = bhr_timeseries.merge_window_counts(sketch_by_date, dates)
    finally:
        bhr_timeseries.AFFECTED_WINDOWS = original

    # Last 2 days: {c1,c2,c3}. "old-only" is excluded.
    assert counts["d2"] == _reference_count({"c1", "c2"}, {"c2", "c3"})
    # Full span includes the oldest day's client.
    assert counts["dall"] == _reference_count({"old-only"}, {"c1", "c2"}, {"c2", "c3"})
    assert counts["d2"] < counts["dall"]


if __name__ == "__main__":
    mozunit.main()
