# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Tests for leaf-frame grouping of near-duplicate hang signatures."""

import os
import sys

import mozunit

_HERE = os.path.dirname(os.path.abspath(__file__))
_AGGREGATION_DIR = os.path.dirname(_HERE)
if _AGGREGATION_DIR not in sys.path:
    sys.path.insert(0, _AGGREGATION_DIR)

import leaf_grouping  # noqa: E402
from profile_processor import ProfileProcessor  # noqa: E402

_FUNC_INDEX = {}


def _sig(frames, ms, count=1.0):
    """A signature dict shaped like signatures_from_thread's (frames leaf->root).

    frameKeys stands in for funcTable indices: distinct frames get distinct
    ordinals, which is all the grouping pass and its assertions need.
    """
    return {
        "frames": [list(f) for f in frames],
        "frameKeys": [
            _FUNC_INDEX.setdefault(tuple(f), len(_FUNC_INDEX)) for f in frames
        ],
        "ms": ms,
        "count": count,
    }


def test_groups_share_leaf_and_expose_trunk_and_first_unique():
    # Two signatures share leaf L and mid frame A, then diverge at the root.
    sig_a = _sig([["L", "xul"], ["A", "xul"], ["rootA", "xul"]], 100.0)
    sig_b = _sig([["L", "xul"], ["A", "xul"], ["rootB", "xul"]], 40.0)
    # A third signature with a different leaf is a singleton and must drop out.
    sig_c = _sig([["M", "xul"], ["rootC", "xul"]], 500.0)

    groups = leaf_grouping.group_signatures([sig_a, sig_b, sig_c])

    assert len(groups) == 1
    group = groups[0]
    assert group["leafFrame"] == ["L", "xul"]
    assert group["memberCount"] == 2
    # Shared run from the leaf down, stopping before the divergence.
    assert group["commonTrunk"] == [["L", "xul"], ["A", "xul"]]
    assert group["branchFrame"] == ["A", "xul"]
    assert group["displayName"] == "L < A"
    assert group["totalMs"] == 140.0
    assert group["totalCount"] == 2.0
    # Members are sorted by descending ms and carry their divergent frame.
    assert [m["frameKeys"] for m in group["members"]] == [
        sig_a["frameKeys"],
        sig_b["frameKeys"],
    ]
    assert group["members"][0]["firstUniqueFrame"] == ["rootA", "xul"]
    assert group["members"][1]["firstUniqueFrame"] == ["rootB", "xul"]


def test_singletons_are_dropped():
    groups = leaf_grouping.group_signatures([
        _sig([["only", "xul"], ["root", "xul"]], 10.0)
    ])
    assert groups == []


def test_divergence_right_after_leaf_names_group_by_leaf_only():
    # Trunk is just the leaf, so there is no shared caller to add to the name.
    sig_a = _sig([["leaf", "xul"], ["x", "xul"]], 10.0)
    sig_b = _sig([["leaf", "xul"], ["y", "xul"]], 20.0)
    group = leaf_grouping.group_signatures([sig_a, sig_b])[0]
    assert group["commonTrunk"] == [["leaf", "xul"]]
    assert group["displayName"] == "leaf"


def test_member_that_is_the_trunk_has_no_first_unique_frame():
    # One stack is a strict prefix of the other: it ends at the branch point.
    short = _sig([["leaf", "xul"], ["mid", "xul"]], 10.0)
    long = _sig([["leaf", "xul"], ["mid", "xul"], ["deep", "xul"]], 30.0)
    group = leaf_grouping.group_signatures([short, long])[0]
    assert group["commonTrunk"] == [["leaf", "xul"], ["mid", "xul"]]
    by_frames = {tuple(m["frameKeys"]): m for m in group["members"]}
    assert by_frames[tuple(short["frameKeys"])]["firstUniqueFrame"] is None
    assert by_frames[tuple(long["frameKeys"])]["firstUniqueFrame"] == ["deep", "xul"]


def test_groups_sorted_by_total_ms_descending():
    # A big two-member group and a smaller one; the bigger sorts first.
    big_a = _sig([["big", "xul"], ["p", "xul"]], 300.0)
    big_b = _sig([["big", "xul"], ["q", "xul"]], 300.0)
    small_a = _sig([["small", "xul"], ["r", "xul"]], 5.0)
    small_b = _sig([["small", "xul"], ["s", "xul"]], 5.0)
    groups = leaf_grouping.group_signatures([small_a, small_b, big_a, big_b])
    assert [g["leafFrame"][0] for g in groups] == ["big", "small"]


def test_noise_leaves_are_skipped_when_choosing_the_grouping_frame():
    # A system leaf (memcpy) over a sync/alloc primitive, then real work: the
    # group is named by the first meaningful Mozilla frame, never the noise.
    sig_a = _sig(
        [
            ["memcpy()", "vcruntime140.amd64"],  # external
            ["je_free(void*)", "mozglue"],  # allocator
            ["DoRealWork()", "xul"],  # <- meaningful leaf
            ["Caller()", "xul"],
        ],
        100.0,
    )
    sig_b = _sig(
        [
            ["RtlEnterCriticalSection", "ntdll"],  # external
            ["mozilla::detail::MutexImpl::lock()", "mozglue"],  # sync
            ["DoRealWork()", "xul"],  # <- same meaningful leaf, so they group
            ["OtherCaller()", "xul"],
        ],
        40.0,
    )
    group = leaf_grouping.group_signatures([sig_a, sig_b])[0]
    assert group["leafFrame"] == ["DoRealWork()", "xul"]
    assert group["displayName"] == "DoRealWork()"
    # Members are still identified by their FULL stack (identity contract).
    assert {tuple(m["frameKeys"]) for m in group["members"]} == {
        tuple(sig_a["frameKeys"]),
        tuple(sig_b["frameKeys"]),
    }
    # The branch point is a meaningful frame, not the shared noise.
    by_frames = {tuple(m["frameKeys"]): m for m in group["members"]}
    assert by_frames[tuple(sig_a["frameKeys"])]["firstUniqueFrame"] == [
        "Caller()",
        "xul",
    ]
    assert by_frames[tuple(sig_b["frameKeys"])]["firstUniqueFrame"] == [
        "OtherCaller()",
        "xul",
    ]


def test_different_noise_prefixes_over_same_work_merge():
    # Two signatures whose only difference is which internal path (allocator vs
    # string copy) was sampled below the same feature frame must merge.
    via_alloc = _sig(
        [["je_realloc(void*, unsigned long long)", "mozglue"], ["Feature()", "xul"]],
        30.0,
    )
    via_string = _sig(
        [
            ["nsTSubstring<char16_t>::Append(char16_t const*)", "xul"],
            ["Feature()", "xul"],
        ],
        20.0,
    )
    groups = leaf_grouping.group_signatures([via_alloc, via_string])
    assert len(groups) == 1
    assert groups[0]["leafFrame"] == ["Feature()", "xul"]
    assert groups[0]["memberCount"] == 2


def test_avg_event_loop_depth_counts_nested_loops():
    deep = _sig(
        [
            ["Work()", "xul"],
            ["NS_ProcessNextEvent(nsIThread*, bool)", "xul"],
            ["SpinInner()", "xul"],
            ["NS_ProcessNextEvent(nsIThread*, bool)", "xul"],
        ],
        10.0,
    )
    shallow = _sig(
        [["Work()", "xul"], ["NS_ProcessNextEvent(nsIThread*, bool)", "xul"]],
        10.0,
    )
    group = leaf_grouping.group_signatures([deep, shallow])[0]
    # Event-loop frames are noise for the leaf, so both still group under Work().
    assert group["leafFrame"] == ["Work()", "xul"]
    # (2 nested loops + 1) / 2 members = 1.5 average depth.
    assert group["avgEventLoopDepth"] == 1.5


def test_all_noise_stack_falls_back_to_raw_leaf():
    # A stack with no meaningful frame still buckets, by its raw leaf, rather
    # than vanishing.
    a = _sig([["(unresolved)", ""], ["free(void*)", "mozglue"]], 10.0)
    b = _sig([["(unresolved)", ""], ["malloc(unsigned long long)", "mozglue"]], 10.0)
    group = leaf_grouping.group_signatures([a, b])[0]
    assert group["leafFrame"] == ["(unresolved)", ""]
    assert group["memberCount"] == 2


def test_variant_collapses_noise_only_differences():
    # Same meaningful stack, different noise below it: one shared variant, so
    # the frontend can fold these two members into a single row.
    via_alloc = _sig(
        [["je_realloc(void*, unsigned long long)", "mozglue"], ["Feature()", "xul"]],
        30.0,
    )
    via_string = _sig(
        [
            ["nsTSubstring<char16_t>::Append(char16_t const*)", "xul"],
            ["Feature()", "xul"],
        ],
        20.0,
    )
    group = leaf_grouping.group_signatures([via_alloc, via_string])[0]
    assert len({m["variant"] for m in group["members"]}) == 1
    # Member identity is still the full stack, so the frameKeys differ.
    assert via_alloc["frameKeys"] != via_string["frameKeys"]


def test_variant_differs_when_meaningful_frames_differ():
    sig_a = _sig([["L", "xul"], ["A", "xul"], ["rootA", "xul"]], 100.0)
    sig_b = _sig([["L", "xul"], ["A", "xul"], ["rootB", "xul"]], 40.0)
    group = leaf_grouping.group_signatures([sig_a, sig_b])[0]
    variant_by_frames = {tuple(m["frameKeys"]): m["variant"] for m in group["members"]}
    # Nothing here is noise, so neither member collapses into the other.
    assert (
        variant_by_frames[tuple(sig_a["frameKeys"])]
        != variant_by_frames[tuple(sig_b["frameKeys"])]
    )


def test_ipc_send_glue_is_noise_but_the_named_send_is_not():
    # The generic channel layer is skipped; the specific Send<Message> frame
    # just below it is what names the call, so it must survive.
    sig_a = _sig(
        [
            ["mozilla::ipc::IProtocol::ChannelSend(IPC::Message*)", "xul"],
            ["mozilla::dom::PContentChild::SendSyncMessage()", "xul"],
            ["Caller()", "xul"],
        ],
        50.0,
    )
    sig_b = _sig(
        [
            ["mozilla::ipc::MessageChannel::Send(IPC::Message*)", "xul"],
            ["mozilla::dom::PContentChild::SendSyncMessage()", "xul"],
            ["OtherCaller()", "xul"],
        ],
        10.0,
    )
    group = leaf_grouping.group_signatures([sig_a, sig_b])[0]
    assert group["leafFrame"] == [
        "mozilla::dom::PContentChild::SendSyncMessage()",
        "xul",
    ]
    assert group["memberCount"] == 2


def _make_processor():
    return ProfileProcessor({
        "use_minimal_sample_table": False,
        "post_sample_size": 1.0,
        "stack_acceptance_threshold": 0.0,
        "print_debug_info": False,
        "uuid": "test-uuid",
        "split_threads_in_out_file": False,
    })


def _row(stack, ms, count=1.0):
    # ProfileProcessor consumes stacks root->leaf; the leaf is the last frame.
    # Frames are (func, lib, inline_depth); grouping ignores depth, so callers
    # pass plain (func, lib) pairs and get depth 0.
    stack = [f if len(f) == 3 else (f[0], f[1], 0) for f in stack]
    return (
        stack,
        "",
        "Gecko",
        "20260401",
        [("UserInteracting", "true")],
        "Linux",
        ms,
        count,
    )


def test_compute_leaf_groups_end_to_end_through_a_profile():
    processor = _make_processor()
    # root->leaf: two near-duplicates share leaf "L" and caller "mid".
    processor.ingest(
        [
            _row([("rootA", "xul"), ("mid", "xul"), ("L", "xul")], 100.0),
            _row([("rootB", "xul"), ("mid", "xul"), ("L", "xul")], 40.0),
            _row([("rootC", "xul"), ("M", "xul")], 500.0),  # singleton leaf
        ],
        {"20260401": 1.0},
    )
    profile = processor.process_into_profile()

    by_thread = leaf_grouping.compute_leaf_groups(profile)
    groups = by_thread["Gecko"]
    assert len(groups) == 1
    group = groups[0]
    assert group["leafFrame"] == ["L", "xul"]
    assert group["memberCount"] == 2
    assert group["commonTrunk"] == [["L", "xul"], ["mid", "xul"]]
    assert group["displayName"] == "L < mid"
    assert group["totalMs"] == 140.0

    # A member is identified by funcTable indices, and resolving those against
    # the profile has to reproduce the canonical key the frontend computes.
    # This is the join the dashboard depends on, so assert the round trip
    # rather than just the emitted value.
    thread = profile["threads"][0]
    string_array = thread["stringArray"]
    func_name = thread["funcTable"]["name"]
    func_lib = thread["funcTable"]["lib"]
    libs = thread["libs"]

    def resolve(frame_keys):
        frames = []
        for func_index in frame_keys:
            lib_index = func_lib[func_index]
            frames.append([
                string_array[func_name[func_index]],
                "" if lib_index is None else libs[lib_index]["name"],
            ])
        return leaf_grouping.canonical_key(frames)

    expected_key = leaf_grouping.canonical_key([
        ["L", "xul"],
        ["mid", "xul"],
        ["rootA", "xul"],
    ])
    assert any(resolve(m["frameKeys"]) == expected_key for m in group["members"])


if __name__ == "__main__":
    mozunit.main()
