# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Hang-signature heuristics for BHR stack aggregation.

Ported from the frontend's getHangFrames in
https://github.com/mozilla/hang-stats/blob/master/bhr.js so that the daily
aggregation job can trim stacks upstream instead of the frontend doing it on
every page render.

History:
  - Originally introduced in python_mozetl issue #410 / PR (heuristics
    migration) as a Python port of the JS algorithm, with byte-for-byte parity
    verified against 2,080 real recorded samples.
  - Moved here as part of the bhr_collection migration from python_mozetl into
    mozilla-central. The algorithm is unchanged.

The three heuristics:

  1. Nested event loop trim — stop walking the stack at the innermost
     ``nsThread::ProcessNextEvent`` frame. Frames outside that point are
     ancestor event loops that don't help identify the hang.
  2. Non-Mozilla code collapse — once we cross into Mozilla code walking from
     leaf to root, drop all but the immediate entry-point non-Mozilla frame.
     We care HOW Firefox code reached system libraries, not the system internals.
  3. SpiderMonkey internals strip — between two JS frames, drop frames that
     are recognizable as JS engine internals. The JS interpreter machinery
     isn't useful signal for hang triage.

  Plus an XPConnect-glue special case that strips XPC_WN_* / XPCWrappedNative
  / XPTC__InvokebyIndex chains when they sit between a JS frame and native code.

The function operates on a symbolicated stack — a list of
``(func_name, lib_name)`` tuples in outer-first (root -> leaf) order.
"""

import re

# Mozilla libraries we recognize as "Firefox code". The frontend's isMozLib
# operates on a normalized lib name (with any trailing ".pdb" stripped); we
# replicate that normalization in _is_moz_lib so Windows debug-name variants
# match the Linux/macOS spellings.
_MOZILLA_LIBS = frozenset(["xul", "XUL", "libxul.so", "mozglue", "libmozglue.so"])

_EVENT_LOOP_FUNC_PREFIX = "nsThread::ProcessNextEvent(bool"

# SpiderMonkey-internal frame name prefixes. Any frame whose name starts with
# one of these is treated as "engine machinery" between JS frames.
_JS_INTERNAL_PREFIXES = (
    "js::",
    "JS::",
    "static bool InternalCall",
    "static bool Interpret",
    "static bool js::",
    "bool js::",
    "static bool SetExistingProperty",
    "(unresolved)",
)

# Pattern identifying JS function names: contains ".js" or ".xul" (file
# basename style) or starts with "self-hosted:".
_JS_FUNC_NAME_RE = re.compile(r"\.js|\.xul|^self-hosted:")


def _is_moz_lib(lib_name):
    if not lib_name:
        return False
    if lib_name.endswith(".pdb"):
        lib_name = lib_name[:-4]
    return lib_name in _MOZILLA_LIBS


def _is_js_func_name(func_name):
    return bool(_JS_FUNC_NAME_RE.search(func_name))


def apply_hang_signature_heuristics(stack):
    """Trim a symbolicated stack using the frontend's hang-signature heuristics.

    The input is a list of ``(func_name, lib_name, inline_depth)`` tuples in
    outer-first (root -> leaf) order. Frames the frontend would mark as hidden
    are removed entirely so the upstream output has the same shape the frontend
    would render. inline_depth rides along untouched: trimming decides which
    frames survive, not whether a surviving frame was inlined.

    Mirrors getHangFrames in hang-stats/bhr.js. Walks the stack leaf-first
    internally (the order the frontend uses) and reverses on return so callers
    can keep working in outer-first order.
    """
    frames = []
    should_remove_prefix = True

    for func_name, lib_name, inline_depth in reversed(stack):
        if func_name.startswith(_EVENT_LOOP_FUNC_PREFIX):
            break

        if (
            should_remove_prefix
            and _is_moz_lib(lib_name)
            and "::InterposedNt" not in func_name
        ):
            should_remove_prefix = False
            if len(frames) > 1:
                for i in range(len(frames) - 1):
                    frames[i][2] = True

        if not lib_name and _is_js_func_name(func_name) and frames:
            i = len(frames) - 1
            while i and frames[i][0].startswith(_JS_INTERNAL_PREFIXES):
                i -= 1
            anchor_func, anchor_lib = frames[i][0], frames[i][1]
            anchor_is_js = (
                not anchor_lib and _is_js_func_name(anchor_func)
            ) or anchor_func.startswith("static bool XPC_WN_")
            if anchor_is_js:
                for ii in range(i + 1, len(frames)):
                    frames[ii][2] = True

            if (
                len(frames) > 3
                and frames[-3][0] in ("XPTC__InvokebyIndex", "NS_InvokeByIndex")
                and frames[-2][0].startswith("XPCWrappedNative::CallMethod(")
                and frames[-1][0].startswith("static bool XPC_WN_")
            ):
                for ii in range(len(frames) - 3, len(frames)):
                    frames[ii][2] = True

        frames.append([func_name, lib_name, False, inline_depth])

    visible = [(f[0], f[1], f[3]) for f in frames if not f[2]]
    visible.reverse()
    return visible
