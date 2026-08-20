# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Tests for ProfileProcessor and its supporting columnar primitives."""

import os
import sys

import mozunit

_HERE = os.path.dirname(os.path.abspath(__file__))
_AGGREGATION_DIR = os.path.dirname(_HERE)
if _AGGREGATION_DIR not in sys.path:
    sys.path.insert(0, _AGGREGATION_DIR)

from profile_processor import (  # noqa: E402
    GrowToFitList,
    ProfileProcessor,
    UniqueKeyedTable,
    merge_number_dicts,
)


def _make_processor():
    return ProfileProcessor({
        "use_minimal_sample_table": False,
        "post_sample_size": 1.0,
        "stack_acceptance_threshold": 0.0,
        "print_debug_info": False,
        "uuid": "test-uuid",
        "split_threads_in_out_file": False,
    })


def _hang(
    stack,
    hang_ms=100.0,
    hang_count=1.0,
    thread="Gecko",
    build_date="20260101",
    annotations=(("UserInteracting", "true"),),
):
    # ingest expects (stack, runnable_name, thread, build_date, annotations,
    # platform, hang_ms, hang_count). At least one annotation is required —
    # the annotationsTable raises if it's still empty when process_thread
    # tries to serialize it. Real BHR data always has annotations.
    #
    # Frames are (func, lib, inline_depth); tests that don't care about
    # inlining pass plain (func, lib) pairs and get depth 0.
    stack = [f if len(f) == 3 else (f[0], f[1], 0) for f in stack]
    return (
        stack,
        "",
        thread,
        build_date,
        list(annotations),
        "Linux",
        hang_ms,
        hang_count,
    )


# --- ProfileProcessor end-to-end behavior ----------------------------------


def test_empty_ingest_produces_no_threads():
    p = _make_processor()
    p.ingest([], {})
    profile = p.process_into_profile()
    assert profile["threads"] == []
    assert profile["usageHoursByDate"] == {}
    assert profile["uuid"] == "test-uuid"


def test_single_hang_creates_one_thread_with_one_sample():
    p = _make_processor()
    p.ingest([_hang([("main", "xul"), ("Bar", "xul")])], {"20260101": 1.0})
    profile = p.process_into_profile()

    assert len(profile["threads"]) == 1
    thread = profile["threads"][0]
    assert thread["name"] == "Gecko"
    assert thread["processType"] == "default"
    assert thread["sampleTable"]["length"] == 1
    assert len(thread["dates"]) == 1
    date = thread["dates"][0]
    assert date["date"] == "20260101"
    assert date["sampleHangCount"] == [1.0]
    assert date["sampleHangMs"] == [100.0]


def test_duplicate_stacks_merge_into_one_sample():
    p = _make_processor()
    rows = [
        _hang([("main", "xul"), ("Bar", "xul")], hang_ms=100.0),
        _hang([("main", "xul"), ("Bar", "xul")], hang_ms=200.0, hang_count=2.0),
    ]
    p.ingest(rows, {"20260101": 1.0})
    profile = p.process_into_profile()

    thread = profile["threads"][0]
    assert thread["sampleTable"]["length"] == 1
    date = thread["dates"][0]
    assert date["sampleHangCount"] == [3.0]
    assert date["sampleHangMs"] == [300.0]


def test_different_stacks_produce_distinct_samples():
    p = _make_processor()
    rows = [
        _hang([("main", "xul"), ("Foo", "xul")]),
        _hang([("main", "xul"), ("Bar", "xul")]),
    ]
    p.ingest(rows, {"20260101": 1.0})
    profile = p.process_into_profile()
    thread = profile["threads"][0]
    assert thread["sampleTable"]["length"] == 2


def test_gecko_child_thread_gets_tab_process_type():
    p = _make_processor()
    p.ingest([_hang([("f", "xul")], thread="Gecko_Child")], {"20260101": 1.0})
    profile = p.process_into_profile()
    assert profile["threads"][0]["processType"] == "tab"


def test_zero_duration_hangs_are_filtered_out():
    p = _make_processor()
    p.ingest([_hang([("f", "xul")], hang_ms=0.0)], {"20260101": 1.0})
    profile = p.process_into_profile()
    # Filter drops the only row, so no thread ever gets touched.
    assert profile["threads"] == []


def test_usage_hours_are_merged_into_profile():
    p = _make_processor()
    p.ingest([], {"20260101": 12.5, "20260102": 7.0})
    profile = p.process_into_profile()
    assert profile["usageHoursByDate"] == {"20260101": 12.5, "20260102": 7.0}


# --- Columnar primitives ----------------------------------------------------


def test_unique_keyed_table_deduplicates_by_key():
    t = UniqueKeyedTable(lambda key: f"item-{key}")
    assert t.key_to_index("a") == 0
    assert t.key_to_index("b") == 1
    assert t.key_to_index("a") == 0  # same key, same index
    assert t.get_items() == ["item-a", "item-b"]


def test_grow_to_fit_list_extends_on_demand():
    g = GrowToFitList()
    g[3] = "x"
    assert len(g) == 4
    assert g[0] is None
    assert g[3] == "x"
    # Reading beyond the end returns None instead of IndexError.
    assert g[99] is None


def test_merge_number_dicts_adds_overlapping_keys():
    a = {"x": 1.0, "y": 2.0}
    b = {"y": 3.0, "z": 4.0}
    assert merge_number_dicts(a, b) == {"x": 1.0, "y": 5.0, "z": 4.0}


def _stack_columns(thread):
    """Map each stack node to (funcName, inlineDepth, inlineRatio)."""
    st = thread["stackTable"]
    names = thread["stringArray"]
    func_name = thread["funcTable"]["name"]
    return {
        names[func_name[st["func"][i]]]: (st["inlineDepth"][i], st["inlineRatio"][i])
        for i in range(1, st["length"])
    }


def test_inline_depth_and_ratio_are_emitted_per_stack_node():
    p = _make_processor()
    p.ingest(
        [_hang([("main", "xul", 0), ("Outer", "xul", 0), ("Inlined", "xul", 1)])],
        {"20260101": 1.0},
    )
    cols = _stack_columns(p.process_into_profile()["threads"][0])
    assert cols["main"] == (0, 0)
    assert cols["Outer"] == (0, 0)
    # Always inlined here, so depth 1 and the whole node's weight is inlined.
    assert cols["Inlined"] == (1, 1.0)


def test_inline_ratio_is_fractional_when_builds_disagree():
    # The same reconstructed stack from two builds: one inlined the leaf, one
    # did not. They merge into one node (that is bug 2052961's dedup), and the
    # ratio records that it was inlined some of the time.
    p = _make_processor()
    p.ingest(
        [
            _hang([("main", "xul", 0), ("Leaf", "xul", 1)], hang_count=3.0),
            _hang(
                [("main", "xul", 0), ("Leaf", "xul", 0)],
                hang_count=1.0,
                build_date="20260102",
            ),
        ],
        {"20260101": 1.0, "20260102": 1.0},
    )
    depth, ratio = _stack_columns(p.process_into_profile()["threads"][0])["Leaf"]
    assert depth == 1
    assert ratio == 0.75  # 3 of 4 hangs inlined


def test_inline_columns_run_parallel_to_the_stack_table():
    p = _make_processor()
    p.ingest(
        [_hang([("main", "xul", 0), ("Inlined", "xul", 2)])],
        {"20260101": 1.0},
    )
    st = p.process_into_profile()["threads"][0]["stackTable"]
    assert len(st["inlineDepth"]) == st["length"]
    assert len(st["inlineRatio"]) == st["length"]
    # Root node carries no frame.
    assert st["inlineDepth"][0] == 0
    assert st["inlineRatio"][0] == 0


if __name__ == "__main__":
    mozunit.main()
