"""Tests for the hang-signature heuristics."""

import json
import os
import re
import sys

import mozunit

_HERE = os.path.dirname(os.path.abspath(__file__))
_AGGREGATION_DIR = os.path.dirname(_HERE)
if _AGGREGATION_DIR not in sys.path:
    sys.path.insert(0, _AGGREGATION_DIR)

from heuristics import apply_hang_signature_heuristics  # noqa: E402


def _trim(stack):
    """Run the heuristics over (func, lib) frames and return (func, lib).

    apply_hang_signature_heuristics takes and returns
    (func, lib, inline_depth) frames. These tests are about which frames
    survive trimming, not about inlining, so they work in pairs and let this
    shim add and drop the depth. test_inline_depth_survives_trimming below
    covers the depth itself.
    """
    trimmed = apply_hang_signature_heuristics([(f[0], f[1], 0) for f in stack])
    return [(name, lib) for name, lib, _ in trimmed]


def test_empty_stack_returns_empty():
    assert _trim([]) == []


def test_pure_mozilla_stack_unchanged():
    stack = [("main", "xul"), ("DoStuff", "xul"), ("Inner", "xul")]
    assert _trim(stack) == stack


def test_pure_non_mozilla_stack_unchanged():
    stack = [("a", "kernel32"), ("b", "kernel32"), ("c", "kernel32")]
    assert _trim(stack) == stack


def test_non_mozilla_collapsed_to_entry_point():
    stack = [
        ("main", "xul"),
        ("Bar", "xul"),
        ("CallSys", "xul"),
        ("sys1", "kernel32"),
        ("sys2", "kernel32"),
        ("sys3", "kernel32"),
    ]
    expected = [
        ("main", "xul"),
        ("Bar", "xul"),
        ("CallSys", "xul"),
        ("sys1", "kernel32"),
    ]
    assert _trim(stack) == expected


def test_interposed_nt_alone_does_not_trigger_mozilla_boundary():
    stack = [
        ("foo::InterposedNt::CallSys", "xul"),
        ("sys1", "kernel32"),
        ("sys2", "kernel32"),
    ]
    assert _trim(stack) == stack


def test_interposed_nt_does_not_act_as_boundary_but_outer_moz_frame_does():
    stack = [
        ("main", "xul"),
        ("foo::InterposedNt::CallSys", "xul"),
        ("sys1", "kernel32"),
        ("sys2", "kernel32"),
    ]
    expected = [("main", "xul"), ("foo::InterposedNt::CallSys", "xul")]
    assert _trim(stack) == expected


def test_nested_event_loops_trimmed_to_innermost():
    stack = [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("HandlerA", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("InnerHandler", "xul"),
        ("leaf", "xul"),
    ]
    expected = [("InnerHandler", "xul"), ("leaf", "xul")]
    assert _trim(stack) == expected


def test_spidermonkey_internals_between_js_frames_dropped():
    stack = [
        ("doJsCall.js:10", ""),
        ("js::InternalCallOrConstruct", ""),
        ("static bool Interpret(JSContext*)", ""),
        ("inner.js:42", ""),
    ]
    expected = [("doJsCall.js:10", ""), ("inner.js:42", "")]
    assert _trim(stack) == expected


def test_xpconnect_internals_dropped():
    stack = [
        ("script.js:99", ""),
        ("static bool XPC_WN_CallMethod(JSContext*)", ""),
        ("XPCWrappedNative::CallMethod(XPCCallContext&)", "xul"),
        ("XPTC__InvokebyIndex", "xul"),
        ("native_leaf", "xul"),
    ]
    expected = [("script.js:99", ""), ("native_leaf", "xul")]
    assert _trim(stack) == expected


def test_pdb_suffix_matched_as_mozilla():
    stack = [
        ("main", "xul.pdb"),
        ("Bar", "xul.pdb"),
        ("sys1", "kernel32.pdb"),
        ("sys2", "kernel32.pdb"),
    ]
    expected = [
        ("main", "xul.pdb"),
        ("Bar", "xul.pdb"),
        ("sys1", "kernel32.pdb"),
    ]
    assert _trim(stack) == expected


def test_event_loop_at_leaf_returns_empty():
    stack = [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
    ]
    assert _trim(stack) == []


# Reference: a faithful Python port of getHangFrames from hang-stats/bhr.js,
# used as an oracle for the parity tests below.

_REFERENCE_MOZ_LIBS = {"xul", "XUL", "libxul.so", "mozglue", "libmozglue.so"}
_REFERENCE_JS_INTERNAL_PREFIXES = (
    "js::",
    "JS::",
    "static bool InternalCall",
    "static bool Interpret",
    "static bool js::",
    "bool js::",
    "static bool SetExistingProperty",
    "(unresolved)",
)
_REFERENCE_JS_FUNC_RE = re.compile(r"\.js|\.xul|^self-hosted:")


def _reference_is_moz_lib(lib_name):
    # Mirrors isMozLib in bhr.js; operates on the lib's .name, which has had
    # any trailing .pdb stripped.
    if lib_name is None:
        return False
    if lib_name.endswith(".pdb"):
        lib_name = lib_name[:-4]
    return lib_name in _REFERENCE_MOZ_LIBS


def _reference_is_js_func_name(func_name):
    return _REFERENCE_JS_FUNC_RE.search(func_name) is not None


def reference_get_hang_frames(stack):
    # Walks leaf-first to match getHangFrames. Input is outer-first, so
    # reverse on entry and reverse again on exit. gShowTasks is treated as
    # False since the upstream job never sets it.
    frames = []
    should_remove_prefix = True

    for func_name, lib_name in reversed(stack):
        if func_name.startswith("nsThread::ProcessNextEvent(bool"):
            break

        if (
            should_remove_prefix
            and _reference_is_moz_lib(lib_name)
            and "::InterposedNt" not in func_name
        ):
            should_remove_prefix = False
            if len(frames) > 1:
                for i in range(len(frames) - 1):
                    frames[i]["hidden"] = "Foreign code"

        if not lib_name and _reference_is_js_func_name(func_name) and frames:
            i = len(frames) - 1
            while i and frames[i]["funcName"].startswith(
                _REFERENCE_JS_INTERNAL_PREFIXES
            ):
                i -= 1
            anchor = frames[i]
            anchor_is_js = (
                not anchor["libName"] and _reference_is_js_func_name(anchor["funcName"])
            ) or anchor["funcName"].startswith("static bool XPC_WN_")
            if anchor_is_js:
                for ii in range(i + 1, len(frames)):
                    frames[ii]["hidden"] = "JS Engine Internal"

            if (
                len(frames) > 3
                and frames[-3]["funcName"]
                in ("XPTC__InvokebyIndex", "NS_InvokeByIndex")
                and frames[-2]["funcName"].startswith("XPCWrappedNative::CallMethod(")
                and frames[-1]["funcName"].startswith("static bool XPC_WN_")
            ):
                for ii in range(len(frames) - 3, len(frames)):
                    frames[ii]["hidden"] = "JS Engine Internal"

        frames.append({"funcName": func_name, "libName": lib_name, "hidden": ""})

    visible = [(f["funcName"], f["libName"]) for f in frames if not f["hidden"]]
    visible.reverse()
    return visible


PARITY_STACKS = [
    [],
    [("main", "xul")],
    [("main", "xul"), ("DoStuff", "xul"), ("leaf", "xul")],
    [("a", "kernel32"), ("b", "kernel32"), ("c", "kernel32")],
    [
        ("main", "xul"),
        ("Bar", "xul"),
        ("CallSys", "xul"),
        ("sys1", "kernel32"),
        ("sys2", "kernel32"),
        ("sys3", "kernel32"),
    ],
    [
        ("main", "xul"),
        ("foo::InterposedNt::CallSys", "xul"),
        ("sys1", "kernel32"),
        ("sys2", "kernel32"),
    ],
    [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("Handler", "xul"),
        ("leaf", "xul"),
    ],
    [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("HandlerA", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("InnerHandler", "xul"),
        ("leaf", "xul"),
    ],
    [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
    ],
    [
        ("doJsCall.js:10", ""),
        ("js::InternalCallOrConstruct", ""),
        ("static bool Interpret(JSContext*)", ""),
        ("inner.js:42", ""),
    ],
    [
        ("XPTC__InvokebyIndex", "xul"),
        ("XPCWrappedNative::CallMethod(XPCCallContext&)", "xul"),
        ("static bool XPC_WN_CallMethod(JSContext*)", ""),
        ("script.js:99", ""),
    ],
    [
        ("main", "xul.pdb"),
        ("Bar", "xul.pdb"),
        ("sys1", "kernel32.pdb"),
        ("sys2", "kernel32.pdb"),
    ],
    [
        ("script.js:1", ""),
        ("Bar", "xul"),
        ("leaf", "xul"),
    ],
    [
        ("main", "xul"),
        ("nsThread::ProcessNextEvent(bool, bool*)", "xul"),
        ("Handler", "xul"),
        ("XPTC__InvokebyIndex", "xul"),
        ("XPCWrappedNative::CallMethod(XPCCallContext&)", "xul"),
        ("static bool XPC_WN_CallMethod(JSContext*)", ""),
        ("outer.js:1", ""),
        ("js::Interpret", ""),
        ("inner.js:5", ""),
        ("sysA", "kernel32"),
        ("sysB", "kernel32"),
    ],
    [
        ("main", "mozglue"),
        ("Helper", "mozglue"),
        ("syscall", "kernel32"),
    ],
    [
        ("main", "libxul.so"),
        ("Bar", "libxul.so"),
        ("sys", "libc.so"),
    ],
]


def test_parity_with_reference_frontend_logic_on_synthetic_stacks():
    for stack in PARITY_STACKS:
        assert _trim(stack) == reference_get_hang_frames(stack), (
            f"parity mismatch on stack: {stack}"
        )


def _reconstruct_real_stack(thread, sample_idx):
    stack_table = thread["stackTable"]
    func_table = thread["funcTable"]
    libs = thread["libs"]
    string_array = thread["stringArray"]

    result = []
    stack_idx = thread["sampleTable"]["stack"][sample_idx]
    while stack_idx:
        func_idx = stack_table["func"][stack_idx]
        prefix = stack_table["prefix"][stack_idx]
        func_name = string_array[func_table["name"][func_idx]]
        lib_idx = func_table["lib"][func_idx]
        if lib_idx is None:
            lib_name = ""
        else:
            lib_name = libs[lib_idx]["name"]
        result.append((func_name, lib_name))
        stack_idx = prefix
    result.reverse()
    return result


def test_parity_with_reference_on_real_hang_aggregates():
    # Load any locally-available pre-migration backend output, reconstruct
    # each sampled stack, and confirm both implementations produce the same
    # visible frames. Skipped when the fixture isn't present (CI, fresh
    # checkouts).
    sample_paths = [
        os.path.expanduser("~/hang-stats/hangs_main_current.json"),
        os.path.expanduser("~/hang-stats/hangs_main_20260502.json"),
    ]
    sample_paths = [p for p in sample_paths if os.path.exists(p)]
    if not sample_paths:
        import pytest

        pytest.skip("no recorded hang_aggregates samples available locally")

    checked = 0
    for path in sample_paths:
        with open(path) as f:
            data = json.load(f)
        for thread in data["threads"]:
            n_samples = len(thread["sampleTable"]["stack"])
            cap = min(n_samples, 2000)
            for i in range(cap):
                stack = _reconstruct_real_stack(thread, i)
                ours = _trim(stack)
                theirs = reference_get_hang_frames(stack)
                assert ours == theirs, (
                    f"parity mismatch in {os.path.basename(path)} "
                    f"thread={thread['name']} sample={i}\n"
                    f"  input: {stack}\n  ours:  {ours}\n  ref:   {theirs}"
                )
                checked += 1
    assert checked > 0, "no real samples were actually checked"


def test_inline_depth_survives_trimming():
    # Depth rides through untouched: trimming decides which frames survive,
    # not whether a surviving frame was inlined. The kernel32 frame is dropped
    # as a system prefix; the Mozilla frames keep their own depths.
    stack = [
        ("main", "xul", 0),
        ("Outer", "xul", 0),
        ("Inlined", "xul", 1),
        ("DeeperInlined", "xul", 2),
    ]
    assert apply_hang_signature_heuristics(stack) == stack


def test_inline_depth_is_not_part_of_trimming_decisions():
    # Two stacks identical apart from depth trim to the same frames, so the
    # signature is unchanged by inlining (bug 2052961's dedup property).
    plain = [("main", "xul", 0), ("Work", "xul", 0)]
    inlined = [("main", "xul", 0), ("Work", "xul", 1)]
    a = [(n, l) for n, l, _ in apply_hang_signature_heuristics(plain)]
    b = [(n, l) for n, l, _ in apply_hang_signature_heuristics(inlined)]
    assert a == b


if __name__ == "__main__":
    mozunit.main()
