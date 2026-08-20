# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Tests for the BigQuery reader (bhr_collection.get_data and helpers).

The BQ client is fully mocked — no network access, no google-cloud-bigquery
install required.
"""

import datetime
import json
import os
import sys

import mozunit

_HERE = os.path.dirname(os.path.abspath(__file__))
_AGGREGATION_DIR = os.path.dirname(_HERE)
if _AGGREGATION_DIR not in sys.path:
    sys.path.insert(0, _AGGREGATION_DIR)

import bhr_collection  # noqa: E402

# --- Pure helpers (no client) ----------------------------------------------


def test_get_prop_walks_slash_delimited_path():
    obj = {"client_info": {"os": "Linux", "os_version": "5.10"}}
    assert bhr_collection.get_prop(obj, "client_info/os") == "Linux"
    assert bhr_collection.get_prop(obj, "client_info/os_version") == "5.10"


def test_get_prop_returns_none_for_none_input():
    assert bhr_collection.get_prop(None, "anything") is None


def test_get_prop_returns_none_when_intermediate_is_none():
    obj = {"client_info": None}
    assert bhr_collection.get_prop(obj, "client_info/os") is None


def test_get_ping_properties_flattens_into_slash_keys():
    ping = {
        "client_info": {"os": "Linux", "app_build": "20260501123456"},
        "metrics": {"object": {"hangs_reports": "[]"}},
    }
    out = bhr_collection.get_ping_properties(
        ping,
        ["client_info/os", "client_info/app_build", "metrics/object/hangs_reports"],
    )
    assert out == {
        "client_info/os": "Linux",
        "client_info/app_build": "20260501123456",
        "metrics/object/hangs_reports": "[]",
    }


def test_properties_are_not_none_true_when_all_present():
    ping = {"a": 1, "b": "x"}
    assert bhr_collection.properties_are_not_none(ping, ["a", "b"]) is True


def test_properties_are_not_none_false_when_any_missing():
    ping = {"a": 1, "b": None}
    assert bhr_collection.properties_are_not_none(ping, ["a", "b"]) is False


def test_ping_is_valid_requires_string_typed_metadata():
    valid = {
        "client_info/os_version": "5.10",
        "client_info/os": "Linux",
        "client_info/app_build": "20260501",
        "metrics/object/hangs_reports": "[]",
    }
    assert bhr_collection.ping_is_valid(valid) is True

    not_string_os = dict(valid, **{"client_info/os": 42})
    assert bhr_collection.ping_is_valid(not_string_os) is False

    missing_reports = dict(valid, **{"metrics/object/hangs_reports": None})
    assert bhr_collection.ping_is_valid(missing_reports) is False


# --- Sample-size to slice math ---------------------------------------------


def test_sample_slice_count_for_common_sample_sizes():
    assert bhr_collection.compute_sample_slices(1.0) == 10000
    assert bhr_collection.compute_sample_slices(0.5) == 5000
    assert bhr_collection.compute_sample_slices(0.001) == 10
    assert bhr_collection.compute_sample_slices(0.0002) == 2


def test_sample_slice_count_clamped_to_at_least_one():
    # A misconfigured 0 should still read at least one slice rather than
    # silently producing no rows.
    assert bhr_collection.compute_sample_slices(0) == 1
    assert bhr_collection.compute_sample_slices(0.00001) == 1


def test_sample_slice_count_clamped_above_max():
    # If someone passes 5.0, we still cap at 10000 — the SQL is bounded by
    # MOD(..., 10000), so values above that wouldn't do anything anyway.
    assert bhr_collection.compute_sample_slices(5.0) == 10000


# --- SQL construction ------------------------------------------------------


def test_query_sql_includes_correct_submission_window():
    # date - 5 days = 2026-04-26, end_date + 5 days = 2026-05-12.
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 7), 100
    )
    assert "BETWEEN '2026-04-26' AND '2026-05-12'" in sql


def test_query_sql_includes_correct_sample_slices():
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 1), 42
    )
    assert "FARM_FINGERPRINT(document_id), 10000)) < 42" in sql


def test_query_sql_targets_the_redacted_hang_report_view():
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 1), 1
    )
    assert "`mozdata.firefox_desktop.hang_report_redacted`" in sql


def test_query_sql_filters_build_date_in_sql():
    # The build-date window is filtered server-side (on app_build's leading
    # YYYYMMDD), not in Python.
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 7), 100
    )
    assert (
        "SUBSTR(client_info.app_build, 1, 8) BETWEEN '20260501' AND '20260507'" in sql
    )


def test_query_sql_does_not_select_unused_ping_info():
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 1), 1
    )
    assert "ping_info" not in sql


def test_query_sql_selects_only_needed_nested_fields():
    # Only the handful of fields the pipeline reads are selected (not the whole
    # client_info/metrics records), rebuilt as STRUCTs that keep the nested
    # client_info.<f> / metrics.object.<f> shape get_prop walks.
    sql = bhr_collection.build_query_sql(
        datetime.date(2026, 5, 1), datetime.date(2026, 5, 1), 1
    )
    for field in (
        "client_info.os AS os",
        "client_info.os_version AS os_version",
        "client_info.architecture AS architecture",
        "client_info.app_build AS app_build",
        "metrics.object.hangs_modules AS hangs_modules",
        "metrics.object.hangs_reports AS hangs_reports",
    ):
        assert field in sql


# --- get_data end-to-end with mocked BQ client -----------------------------


def _make_row(os_name, os_version, arch, app_build, hangs_reports, hangs_modules=""):
    """Build a fake BQ row as a nested dict — same shape google-cloud-bigquery
    returns for our SELECT (top-level columns are client_info and metrics, each
    a nested record/dict)."""
    return {
        "client_info": {
            "os": os_name,
            "os_version": os_version,
            "architecture": arch,
            "app_build": app_build,
        },
        "metrics": {
            "object": {
                "hangs_reports": hangs_reports,
                "hangs_modules": hangs_modules,
            }
        },
    }


def _install_fake_client(monkeypatch, rows):
    # _query_bigquery is the single BigQuery touchpoint; stub it to return our
    # fake rows so the tests stay offline and don't need google-cloud-bigquery.
    monkeypatch.setattr(
        bhr_collection, "_query_bigquery", lambda sql, billing_project: iter(rows)
    )


def test_get_data_yields_ping_for_valid_row_in_build_window(monkeypatch):
    rows = [
        _make_row("Linux", "5.10", "x86_64", "20260501123456", "[]"),
    ]
    _install_fake_client(monkeypatch, rows)

    pings = list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            sample_size=0.001,
            billing_project="test-project",
        )
    )

    assert len(pings) == 1
    assert pings[0]["client_info/os"] == "Linux"
    assert pings[0]["client_info/app_build"] == "20260501123456"


def test_get_data_drops_rows_with_missing_required_fields(monkeypatch):
    rows = [
        _make_row("Linux", "5.10", "x86_64", "20260501123456", "[]"),
        # OS missing — should be dropped.
        _make_row(None, "5.10", "x86_64", "20260501123456", "[]"),
    ]
    _install_fake_client(monkeypatch, rows)

    pings = list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            sample_size=0.001,
            billing_project="test-project",
        )
    )
    assert len(pings) == 1


def _capture_sql(monkeypatch):
    """Stub _query_bigquery, recording the SQL it's handed (returns no rows).

    Build-date filtering lives only in the SQL, so get_data's date handling is
    verified by checking the query it builds rather than by filtering rows.
    """
    captured = {}

    def fake_query(sql, billing_project):
        captured["sql"] = sql
        return iter([])

    monkeypatch.setattr(bhr_collection, "_query_bigquery", fake_query)
    return captured


def test_get_data_default_end_date_is_same_as_start(monkeypatch):
    # When end_date isn't passed, the build window collapses to one day, which
    # is what the SQL build-date filter should use.
    captured = _capture_sql(monkeypatch)

    list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            sample_size=0.001,
            billing_project="test-project",
        )
    )
    assert (
        "SUBSTR(client_info.app_build, 1, 8) BETWEEN '20260501' AND '20260501'"
        in captured["sql"]
    )


def test_get_data_explicit_end_date_widens_window(monkeypatch):
    captured = _capture_sql(monkeypatch)

    list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            end_date=datetime.date(2026, 5, 2),
            sample_size=0.001,
            billing_project="test-project",
        )
    )
    assert (
        "SUBSTR(client_info.app_build, 1, 8) BETWEEN '20260501' AND '20260502'"
        in captured["sql"]
    )


def test_get_data_exclude_modules_omits_hangs_modules_property(monkeypatch):
    rows = [
        # hangs_modules is None — this would normally drop the ping via
        # properties_are_not_none. With exclude_modules=True, hangs_modules
        # isn't required so the ping passes.
        {
            "client_info": {
                "os": "Linux",
                "os_version": "5.10",
                "architecture": "x86_64",
                "app_build": "20260501123456",
            },
            "metrics": {"object": {"hangs_reports": "[]", "hangs_modules": None}},
        },
    ]
    _install_fake_client(monkeypatch, rows)

    pings = list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            sample_size=0.001,
            billing_project="test-project",
            exclude_modules=True,
        )
    )
    assert len(pings) == 1
    assert "metrics/object/hangs_modules" not in pings[0]


def test_get_data_passes_billing_project_to_query(monkeypatch):
    captured = {}

    def fake_query(sql, billing_project):
        captured["billing_project"] = billing_project
        return iter([])

    monkeypatch.setattr(bhr_collection, "_query_bigquery", fake_query)

    list(
        bhr_collection.get_data(
            date=datetime.date(2026, 5, 1),
            sample_size=0.001,
            billing_project="my-bill-here",
        )
    )
    assert captured["billing_project"] == "my-bill-here"


def test_get_data_is_lazy_generator(monkeypatch):
    # Calling get_data must NOT consume the iterator — yielding stays lazy.
    rows_seen = []

    def make_iterator():
        for row in [
            _make_row("Linux", "5.10", "x86_64", "20260501123456", "[]"),
            _make_row("Linux", "5.10", "x86_64", "20260501123456", "[]"),
        ]:
            rows_seen.append(row)
            yield row

    monkeypatch.setattr(
        bhr_collection, "_query_bigquery", lambda sql, billing_project: make_iterator()
    )

    gen = bhr_collection.get_data(
        date=datetime.date(2026, 5, 1),
        sample_size=0.001,
        billing_project="test-project",
    )
    # Generator created but not yet consumed.
    assert rows_seen == []

    # Pull one ping, that should pull exactly one row from upstream.
    next(gen)
    assert len(rows_seen) == 1


# --- collect_offsets_by_module ----------------------------------------------


def _make_hang(stack):
    # process_hangs builds 8-tuples; only the stack matters for collection.
    return (stack, 100.0, "Gecko", "", "main", [], "20260101", "Linux")


def _old_get_frames_by_module_reference(hangs):
    """Pure-Python mirror of the old Spark RDD grouping pipeline.

    The original code did:
        flatMap(stack frames) -> map(module, (offset,)) -> distinct()
        -> reduceByKey(tuple concatenation)

    The new helper returns sets instead of offset tuples, but the observable
    grouping/deduping semantics should be the same.
    """
    distinct_frames = set()
    for hang in hangs:
        for module, offset in hang[0]:
            distinct_frames.add((module, offset))

    by_module = {}
    for module, offset in distinct_frames:
        by_module.setdefault(module, set()).add(offset)
    return by_module


def test_collect_offsets_by_module_empty_input():
    assert bhr_collection.collect_offsets_by_module([]) == {}


def test_collect_offsets_by_module_matches_old_rdd_semantics():
    hangs = [
        _make_hang([
            (("xul.pdb", "ABC"), "100"),
            (("xul.pdb", "ABC"), "100"),
            (("xul.pdb", "ABC"), "200"),
            (("kernel32.pdb", "XYZ"), "500"),
        ]),
        _make_hang([
            (("kernel32.pdb", "XYZ"), "500"),
            (("pseudo", None), "PseudoFrame"),
            (None, "deadbeef"),
        ]),
    ]

    assert bhr_collection.collect_offsets_by_module(
        hangs
    ) == _old_get_frames_by_module_reference(hangs)


# --- process_frame ----------------------------------------------------------

_MODULES = [["xul.pdb", "ABC"], ["kernel32.pdb", "XYZ"]]


def test_process_frame_glean_dict_with_valid_module():
    frame = {"frame": "1000", "module": 0}
    assert bhr_collection.process_frame(frame, _MODULES) == (("xul.pdb", "ABC"), "1000")


def test_process_frame_glean_dict_without_module_is_pseudo():
    frame = {"frame": "labelText"}
    assert bhr_collection.process_frame(frame, _MODULES) == (
        ("pseudo", None),
        "labelText",
    )


def test_process_frame_glean_dict_out_of_range_module_drops_module():
    frame = {"frame": "1000", "module": 99}
    assert bhr_collection.process_frame(frame, _MODULES) == (None, "1000")


def test_process_frame_legacy_list_format():
    assert bhr_collection.process_frame([1, "2000"], _MODULES) == (
        ("kernel32.pdb", "XYZ"),
        "2000",
    )


def test_process_frame_bare_value_fallback_is_pseudo():
    assert bhr_collection.process_frame("rawlabel", _MODULES) == (
        ("pseudo", None),
        "rawlabel",
    )


# --- filter_hang ------------------------------------------------------------

_FILTER_CONFIG = {"thread_filter": "Gecko"}


def test_filter_hang_keeps_matching_thread_with_valid_stack():
    hang = {"thread": "Gecko", "stack": [{"frame": "0", "module": 0}]}
    assert bhr_collection.filter_hang(hang, _FILTER_CONFIG) is True


def test_filter_hang_drops_wrong_thread():
    hang = {"thread": "Compositor", "stack": [{"frame": "0"}]}
    assert bhr_collection.filter_hang(hang, _FILTER_CONFIG) is False


def test_filter_hang_drops_missing_stack():
    # Some Glean records arrive without a stack field.
    hang = {"thread": "Gecko"}
    assert bhr_collection.filter_hang(hang, _FILTER_CONFIG) is False


def test_filter_hang_drops_empty_stack():
    hang = {"thread": "Gecko", "stack": []}
    assert bhr_collection.filter_hang(hang, _FILTER_CONFIG) is False


def test_filter_hang_drops_overlong_stack():
    hang = {"thread": "Gecko", "stack": [{"frame": "0"}] * 300}
    assert bhr_collection.filter_hang(hang, _FILTER_CONFIG) is False


# --- process_hang -----------------------------------------------------------


def test_process_hang_plain_dict_unchanged():
    hang = {"thread": "Gecko", "duration": 100}
    assert bhr_collection.process_hang(hang) is hang


def test_process_hang_row_like_object_is_converted():
    class FakeRow:
        def asDict(self, recursive=False):
            return {"thread": "Gecko", "recursive": recursive}

    assert bhr_collection.process_hang(FakeRow()) == {
        "thread": "Gecko",
        "recursive": True,
    }


# --- process_hangs ----------------------------------------------------------


def _make_ping(hangs_reports, hangs_modules, os_name="Linux", app_build="20260501x"):
    return {
        "client_info/app_build": app_build,
        "client_info/os": os_name,
        "metrics/object/hangs_modules": hangs_modules,
        "metrics/object/hangs_reports": hangs_reports,
    }


def test_process_hangs_expands_one_hang_into_a_tuple():
    ping = _make_ping(
        hangs_reports=[
            {
                "thread": "Gecko",
                "process": "main",
                "duration": 250,
                "stack": [
                    {"frame": "1000", "module": 0},
                    {"frame": "2000", "module": 1},
                ],
                "annotations": [],
            }
        ],
        hangs_modules=[["xul.pdb", "ABC"], ["kernel32.pdb", "XYZ"]],
    )
    out = bhr_collection.process_hangs(ping, {"thread_filter": "Gecko"})

    assert len(out) == 1
    stack, duration, thread, runnable, process, annotations, build_date, platform = out[
        0
    ]
    assert stack == [(("xul.pdb", "ABC"), "1000"), (("kernel32.pdb", "XYZ"), "2000")]
    assert duration == 250
    assert thread == "Gecko"
    assert build_date == "20260501"
    assert platform == "Linux"


def test_process_hangs_parses_json_string_payloads():
    # Glean object metrics sometimes arrive as JSON strings.
    ping = _make_ping(
        hangs_reports=json.dumps([
            {
                "thread": "Gecko",
                "process": "main",
                "duration": 100,
                "stack": [{"frame": "1000", "module": 0}],
                "annotations": [],
            }
        ]),
        hangs_modules=json.dumps([["xul.pdb", "ABC"]]),
    )
    out = bhr_collection.process_hangs(ping, {"thread_filter": "Gecko"})
    assert len(out) == 1
    assert out[0][0] == [(("xul.pdb", "ABC"), "1000")]


def test_process_hangs_drops_hangs_on_other_threads():
    ping = _make_ping(
        hangs_reports=[
            {
                "thread": "Compositor",
                "process": "main",
                "duration": 100,
                "stack": [{"frame": "1000", "module": 0}],
                "annotations": [],
            }
        ],
        hangs_modules=[["xul.pdb", "ABC"]],
    )
    assert bhr_collection.process_hangs(ping, {"thread_filter": "Gecko"}) == []


# --- symbolicate_stacks -----------------------------------------------------


def test_symbolicate_stacks_resolves_known_frames():
    stack = [(("xul.pdb", "ABC"), "1000"), (("kernel32.pdb", "XYZ"), "2000")]
    symbol_map = {
        (("xul.pdb", "ABC"), "1000"): [("nsThread::ProcessNextEvent(bool)", "xul.pdb")],
        (("kernel32.pdb", "XYZ"), "2000"): [("WaitForSingleObjectEx", "kernel32.pdb")],
    }
    # Nothing inlined here, so every frame is at depth 0.
    assert bhr_collection.symbolicate_stacks(stack, symbol_map) == [
        ("nsThread::ProcessNextEvent(bool)", "xul.pdb", 0),
        ("WaitForSingleObjectEx", "kernel32.pdb", 0),
    ]


def test_symbolicate_stacks_splices_inline_frames():
    # A single (module, offset) whose symbol map entry carries an inlined chain
    # expands in place (outer-first), so equivalent hangs dedup (bug 2052961).
    stack = [(("xul.pdb", "ABC"), "1000"), (("xul.pdb", "ABC"), "2000")]
    symbol_map = {
        (("xul.pdb", "ABC"), "1000"): [
            ("OuterFunc()", "xul"),
            ("InlinedInner()", "xul"),
        ],
        (("xul.pdb", "ABC"), "2000"): [("LeafFunc()", "xul")],
    }
    # The chain's position becomes inline_depth: the enclosing FUNC is 0 and
    # each inlined callee counts up from 1 (bug 2059443).
    assert bhr_collection.symbolicate_stacks(stack, symbol_map) == [
        ("OuterFunc()", "xul", 0),
        ("InlinedInner()", "xul", 1),
        ("LeafFunc()", "xul", 0),
    ]


def test_symbolicate_stacks_unknown_frame_falls_back_to_debug_name():
    stack = [(("xul.pdb", "ABC"), "9999")]
    assert bhr_collection.symbolicate_stacks(stack, {}) == [
        ("<unsymbolicated>", "xul.pdb", 0)
    ]


def test_symbolicate_stacks_none_module_falls_back_to_unknown():
    stack = [(None, "1000")]
    assert bhr_collection.symbolicate_stacks(stack, {}) == [
        ("<unsymbolicated>", "unknown", 0)
    ]


# --- symbolicate_hang (symbolicate + heuristics) ----------------------------


def test_symbolicate_hang_applies_symbols_then_heuristics():
    # Two xul frames plus a nested event-loop frame the heuristic trims away.
    raw_hang = (
        [
            (("xul.pdb", "ABC"), "1"),
            (("xul.pdb", "ABC"), "2"),
            (("xul.pdb", "ABC"), "3"),
        ],
        100.0,
        "Gecko",
        "",
        "main",
        [],
        "20260101",
        "Linux",
    )
    symbol_map = {
        (("xul.pdb", "ABC"), "1"): [("nsThread::ProcessNextEvent(bool, bool*)", "xul")],
        (("xul.pdb", "ABC"), "2"): [("HandlerFunc", "xul")],
        (("xul.pdb", "ABC"), "3"): [("LeafFunc", "xul")],
    }
    out = bhr_collection.symbolicate_hang(raw_hang, symbol_map)
    # The heuristic stops at ProcessNextEvent, leaving the two inner frames.
    assert out[0] == [("HandlerFunc", "xul", 0), ("LeafFunc", "xul", 0)]
    # Metadata fields are preserved.
    assert out[1:] == raw_hang[1:]


# --- map_to_hang_data / merge_hang_data -------------------------------------

_BOUNDS_CONFIG = {"hang_lower_bound": 128, "hang_upper_bound": 65536}


def _symbolicated_hang(stack, duration, annotations=()):
    # Frames are (func, lib, inline_depth); tests that don't care about
    # inlining pass plain (func, lib) pairs and get depth 0. Raw (module,
    # offset) stacks for symbolicate_hang are left alone.
    stack = [
        f
        if not (isinstance(f, tuple) and len(f) == 2 and isinstance(f[0], str))
        else (f[0], f[1], 0)
        for f in stack
    ]
    return (
        stack,
        duration,
        "Gecko",
        "",
        "main",
        list(annotations),
        "20260101",
        "Linux",
    )


def test_map_to_hang_data_builds_key_and_value():
    hang = _symbolicated_hang([("FooFunc", "xul")], 250)
    out = bhr_collection.map_to_hang_data(hang, _BOUNDS_CONFIG)
    assert len(out) == 1
    key, value = out[0]
    # Depth is deliberately absent from the key, so builds that inline
    # differently still merge; it rides in the value instead.
    assert key[0] == (("FooFunc", "xul"),)  # stack frozen to tuple of pairs
    assert key[2] == "Gecko"  # thread
    assert value == (250.0, 1.0, (0,))


def test_map_to_hang_data_drops_below_lower_bound():
    hang = _symbolicated_hang([("FooFunc", "xul")], 100)  # < 128
    assert bhr_collection.map_to_hang_data(hang, _BOUNDS_CONFIG) == []


def test_map_to_hang_data_drops_at_or_above_upper_bound():
    hang = _symbolicated_hang([("FooFunc", "xul")], 65536)  # >= 65536
    assert bhr_collection.map_to_hang_data(hang, _BOUNDS_CONFIG) == []


def test_merge_hang_data_sums_duration_and_count():
    assert bhr_collection.merge_hang_data(
        (100.0, 1.0, (0, 1)), (250.0, 2.0, (1, 0))
    ) == (350.0, 3.0, (1, 1))


# --- group_hangs (aggregation) ----------------------------------------------


def test_group_hangs_merges_identical_stacks():
    hangs = [
        _symbolicated_hang([("FooFunc", "xul")], 200),
        _symbolicated_hang([("FooFunc", "xul")], 300),
    ]
    grouped = bhr_collection.group_hangs(hangs, _BOUNDS_CONFIG)
    assert len(grouped) == 1
    # 8-element row: key fields + (duration_sum, count_sum).
    row = grouped[0]
    assert row[-2:] == (500.0, 2.0)


def test_group_hangs_keeps_distinct_stacks_separate():
    hangs = [
        _symbolicated_hang([("FooFunc", "xul")], 200),
        _symbolicated_hang([("BarFunc", "xul")], 300),
    ]
    grouped = bhr_collection.group_hangs(hangs, _BOUNDS_CONFIG)
    assert len(grouped) == 2


def test_group_hangs_output_shape_matches_profile_processor_input():
    # ProfileProcessor.ingest unpacks rows as:
    #   (stack, runnable, thread, build_date, annotations, platform,
    #    hang_ms, hang_count)
    hangs = [_symbolicated_hang([("FooFunc", "xul")], 200)]
    (row,) = bhr_collection.group_hangs(hangs, _BOUNDS_CONFIG)
    assert len(row) == 8
    stack, runnable, thread, build_date, annotations, platform, hang_ms, count = row
    assert thread == "Gecko"
    assert build_date == "20260101"
    assert platform == "Linux"
    assert hang_ms == 200.0
    assert count == 1.0


# --- end-to-end: symbolicate -> group --------------------------------------


def test_symbolicate_then_group_full_chain():
    # Two raw hangs whose stacks symbolicate to the same frames should merge.
    symbol_map = {
        (("xul.pdb", "ABC"), "1"): [("FooFunc", "xul")],
        (("xul.pdb", "ABC"), "2"): [("BarFunc", "xul")],
    }
    raw_hangs = [
        _symbolicated_hang([(("xul.pdb", "ABC"), "1"), (("xul.pdb", "ABC"), "2")], 200),
        _symbolicated_hang([(("xul.pdb", "ABC"), "1"), (("xul.pdb", "ABC"), "2")], 300),
    ]
    symbolicated = [bhr_collection.symbolicate_hang(h, symbol_map) for h in raw_hangs]
    grouped = bhr_collection.group_hangs(symbolicated, _BOUNDS_CONFIG)

    assert len(grouped) == 1
    row = grouped[0]
    # group_hangs folds the merged depths back onto the frames.
    assert row[0] == (("FooFunc", "xul", 0), ("BarFunc", "xul", 0))
    assert row[-2:] == (500.0, 2.0)


# --- aggregate() end-to-end (offline) ---------------------------------------


def test_aggregate_end_to_end_offline(monkeypatch, tmp_path):
    # One fake ping carrying one Gecko hang. Everything is mocked so the test
    # runs offline: the BQ client yields our row, and symbol fetching always
    # fails (so frames come back unsymbolicated, which is fine for a wiring
    # smoke test).
    import symbolication

    hang = {
        "thread": "Gecko",
        "process": "main",
        "duration": 500,  # within [128, 65536)
        "stack": [
            {"frame": "1000", "module": 0},
            {"frame": "2000", "module": 1},
        ],
        # Real BHR hangs always carry at least one annotation; an empty list
        # would leave the annotationsTable empty and make process_thread raise
        # (a python_mozetl quirk we preserve).
        "annotations": [["UserInteracting", "true"]],
    }
    row = _make_row(
        "Linux",
        "5.10",
        "x86_64",
        "20260502123456",
        json.dumps([hang]),
        json.dumps([["xul.pdb", "ABC"], ["kernel32.pdb", "XYZ"]]),
    )
    _install_fake_client(monkeypatch, [row])
    monkeypatch.setattr(symbolication, "fetch_url", lambda _url: (False, ""))

    profile = bhr_collection.aggregate(
        date=datetime.date(2026, 5, 2),
        sample_size=0.001,
        billing_project="test-project",
        output_dir=str(tmp_path),
    )

    # Profile shape the frontend expects. leafGroups is attached by the
    # near-duplicate grouping pass (empty here: a single hang can't group).
    assert set(profile.keys()) == {"threads", "usageHoursByDate", "uuid", "leafGroups"}
    assert profile["leafGroups"] == {"Gecko": []}
    assert profile["usageHoursByDate"] == {"20260502": 1.0}
    assert len(profile["threads"]) == 1
    assert profile["threads"][0]["name"] == "Gecko"
    assert profile["threads"][0]["sampleTable"]["length"] == 1

    # Both output files were written.
    assert (tmp_path / "hangs_main_20260502.json").exists()
    assert (tmp_path / "hangs_main_current.json").exists()

    # The written file round-trips to the same profile.
    with open(tmp_path / "hangs_main_current.json") as f:
        assert json.load(f)["usageHoursByDate"] == {"20260502": 1.0}


if __name__ == "__main__":
    mozunit.main()
