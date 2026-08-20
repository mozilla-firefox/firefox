# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Group near-duplicate hang signatures by their first *meaningful* frame.

Many hang signatures are really the same underlying hang: they share the same
Firefox code getting stuck and only differ in noise below it (which system
call, allocator path, or JS-interpreter trampoline happened to be sampled; how
deep the nested event loop was). Because a signature is the whole stack, every
such variation becomes its own row, so the true size of a hang is split across
many near-identical entries and the list reads as noise.

This module collapses those signatures server-side. The key idea, following the
hang-triage heuristics, is that the frame that *identifies* a hang is not the
raw leaf (which is usually a lock, a sleep, an allocation, or an event-loop
wait) but the first frame that names actual Firefox work. So before bucketing
we normalize each stack down to its meaningful frames and group by the leaf of
*that*. Concretely, walking from the raw leaf downward we skip:

  - external code - system / third-party libraries (ntdll, kernelbase, user32,
    vcruntime, ...). We care how Firefox code reached them, not their internals.
  - synchronization + allocation primitives - condition-variable / mutex
    wrappers and the jemalloc/arena/operator-new machinery. A hang "in a lock"
    or "in free()" is identified by *what* is locking or freeing, not the
    primitive, so ``SleepConditionVariableSRW`` / ``je_free`` are never a name.
  - SpiderMonkey + XPConnect glue - the interpreter/JIT trampolines and
    native<->JS call plumbing between script frames. What the engine is doing
    internally is rarely the hang; the JS entry point (e.g. ``js::Stringify``)
    is kept.
  - event-loop + app-run machinery - ``NS_ProcessNextEvent``, the message
    pump, ``TaskController``, ``XRE_main`` and friends. Ancestor/nested event
    loops don't identify the hang; instead their count is summarized per group
    as ``avgEventLoopDepth`` so the nesting isn't lost entirely.

We then bucket by the meaningful leaf, find the common trunk each bucket shares
walking from that leaf downward, and label the group by where its stacks first
branch apart. Only multi-member groups are emitted. The job emits the groups
pre-grouped and pre-named so the frontend does no fuzzy matching of its own;
each group identifies its members so the dashboard can join a displayed
signature to its group.

A member is identified by ``frameKeys``: its stack as funcTable indices,
leaf -> root. The frontend resolves those against the same funcTable and
recomputes the canonical key (see stack_keys), so the two sides still agree
byte-for-byte without any shared hashing. Emitting indices rather than the key
itself keeps the block small: the columnar profile already interns every one of
those strings, and spelling them out again per member made leafGroups roughly
90% of the artifact.

Normalization only chooses how signatures are bucketed and named; it never
changes a signature's identity.
"""

from stack_keys import canonical_key, reconstruct_stack_indexed

# A bucket needs at least this many distinct signatures before grouping it is
# worthwhile; a lone signature already stands for itself.
DEFAULT_MIN_GROUP_SIZE = 2

# Mozilla libraries we recognize as "Firefox code". Mirrors the frontend's
# isMozLib / heuristics._MOZILLA_LIBS. A frame with no library (a pseudo/label
# or JS frame) is also treated as own code, not external.
_MOZILLA_LIBS = frozenset(["xul", "XUL", "libxul.so", "mozglue", "libmozglue.so"])

# --- Noise classes skipped when choosing the meaningful grouping frame. ---
# Each is a tuple of str.startswith() prefixes unless noted. Kept deliberately
# surgical (specific symbols, not broad namespaces like bare "js::") so real
# work frames are never mistaken for glue.

# Event-loop entry points. Also counted to report a group's event-loop depth.
_EVENT_LOOP_PREFIXES = (
    "NS_ProcessNextEvent",
    "nsThread::ProcessNextEvent",
    "mozilla::SpinEventLoopUntil",
    "nsThreadManager::SpinEventLoopUntil",
    "nsAppShell::ProcessNextNativeEvent",
)

# Event-loop entries plus the surrounding message-pump / app-run scaffolding,
# all of which sits between the real work and the thread root.
_RUNLOOP_PREFIXES = _EVENT_LOOP_PREFIXES + (
    "MessageLoop::Run",
    "MessageLoop::DoWork",
    "mozilla::ipc::MessagePump",
    "base::MessagePump",
    "nsBaseAppShell::Run",
    "nsAppShell::Run",
    "nsAppStartup::Run",
    "XREMain::",
    "XRE_main",
    "mozilla::BootstrapImpl::",
    "TaskController::",
    "mozilla::TaskController::",
    "nsThread::ThreadFunc",
)

# SpiderMonkey interpreter / JIT / XPConnect call glue between script frames.
# Public engine entry points (js::Stringify, js::NativeGetProperty, ...) do NOT
# match.
_JS_GLUE_PREFIXES = (
    "js::Interpret",
    "js::RunScript",
    "js::Call(",
    "js::Call<",
    "js::CallSelfHostedFunction",
    "js::InternalCall",
    "js::jit::",
    "js::fun_call",
    "js::fun_apply",
    "Interpret(",
    "EnterJit",
    "XPC_WN_",
    "XPCWrappedNative::CallMethod",
    "XPTC__InvokebyIndex",
    "NS_InvokeByIndex",
    "nsXPCWrappedJS::CallMethod",
    "XPCConvert::",
    "JS_CallFunctionValue",
)

# Generic IPC send plumbing. The specific Send<Message> frame that names the
# call sits just below ChannelSend, so only the generic layer is skipped.
_IPC_GLUE_PREFIXES = (
    "mozilla::ipc::IProtocol::ChannelSend",
    "mozilla::ipc::MessageChannel::Send",
)

# Synchronization primitives: a hang in a lock/condvar is named by what holds
# it, not by the primitive.
_SYNC_PREFIXES = (
    "mozilla::detail::ConditionVariableImpl",
    "mozilla::detail::MutexImpl",
    "mozilla::Mutex",
    "mozilla::OffTheBooksMutex",
    "mozilla::BaseAutoLock",
    "mozilla::detail::BaseAutoLock",
    "mozilla::Monitor",
    "mozilla::ReentrantMonitor",
    "mozilla::CondVar",
    "PR_Wait",
    "PR_Lock",
    "PR_Unlock",
    "PRCondVar",
)

# Allocator machinery: a hang "in free()/realloc()" is named by the caller.
# Symbols carry an argument list (je_free(void*)), so these match by prefix.
_ALLOC_PREFIXES = (
    "je_free",
    "je_malloc",
    "je_realloc",
    "je_calloc",
    "malloc(",
    "free(",
    "realloc(",
    "calloc(",
    "arena_t::",
    "BaseAllocator::",
    "moz_xmalloc",
    "moz_xrealloc",
    "moz_xcalloc",
    "operator new",
    "operator delete",
    "mozilla::Vector",  # Vector<...>::growStorageBy and friends
)

# Container / string boilerplate: a hang copying, growing, or freeing a string
# or array is identified by the caller, not the container method.
_CONTAINER_PREFIXES = (
    "nsTSubstring",
    "nsTString",
    "nsAString_internal",
    "nsACString_internal",
    "nsTArray_Impl",
    "nsTArray<",
    "AutoTArray",
    "nsTHashtable",
    "nsBaseHashtable",
)

# Every noise prefix, flattened once so the per-frame check is a single
# startswith() rather than rebuilding the tuple on each call.
_NOISE_PREFIXES = (
    _ALLOC_PREFIXES
    + _CONTAINER_PREFIXES
    + _SYNC_PREFIXES
    + _JS_GLUE_PREFIXES
    + _IPC_GLUE_PREFIXES
    + _RUNLOOP_PREFIXES
)


def _norm_lib(lib):
    if lib and lib.endswith(".pdb"):
        return lib[:-4]
    return lib


def _is_external(lib):
    """A system / third-party library (own code has a Mozilla lib or no lib)."""
    return bool(lib) and _norm_lib(lib) not in _MOZILLA_LIBS


def _is_event_loop_frame(name):
    return name.startswith(_EVENT_LOOP_PREFIXES)


def _is_grouping_noise(name, lib):
    """Whether a frame is skipped when choosing the meaningful grouping frame."""
    if _is_external(lib):
        return True
    if not name or name in ("(unresolved)", "(root)"):
        return True
    if "self-hosted:" in name:
        return True
    return name.startswith(_NOISE_PREFIXES)


def _grouping_frames(frames):
    """The meaningful frames of a stack (leaf->root), noise removed.

    Falls back to the raw leaf when a stack is entirely noise, so such a hang
    still buckets (as itself) rather than vanishing.
    """
    kept = [f for f in frames if not _is_grouping_noise(f[0], f[1])]
    return kept if kept else [frames[0]]


def _event_loop_depth(frames):
    """How many event-loop entry frames a stack contains (nested-loop depth)."""
    return sum(1 for name, _ in frames if _is_event_loop_frame(name))


def signatures_from_thread(thread):
    """Fold a thread's samples into per-signature {frames, frameKeys, ms, count}.

    Samples with identical stacks (but differing runnable/annotations/platform,
    which the frontend ignores for signature identity) are summed together, and
    hang ms/count are totalled across every date the thread carries.
    """
    total = thread["sampleTable"]["length"]

    ms = [0.0] * total
    count = [0.0] * total
    for date in thread["dates"]:
        hang_ms = date["sampleHangMs"]
        hang_count = date["sampleHangCount"]
        for i in range(len(hang_count)):
            if i < len(hang_ms) and hang_ms[i]:
                ms[i] += hang_ms[i]
            if hang_count[i]:
                count[i] += hang_count[i]

    by_key = {}
    for i in range(total):
        if ms[i] <= 0.0:
            continue
        frames, frame_keys = reconstruct_stack_indexed(thread, i)
        if not frames:
            continue
        key = canonical_key(frames)
        entry = by_key.get(key)
        if entry is None:
            by_key[key] = {
                "frames": frames,
                "frameKeys": frame_keys,
                "ms": ms[i],
                "count": count[i],
            }
        else:
            entry["ms"] += ms[i]
            entry["count"] += count[i]
    return list(by_key.values())


def _common_trunk(frame_lists):
    """Longest run of frames, from the leaf down, shared by every member.

    Operates on the members' meaningful (noise-removed) frame lists. Always at
    least length 1: every member of a bucket shares the meaningful leaf.
    """
    min_len = min(len(f) for f in frame_lists)
    trunk = []
    for depth in range(min_len):
        frame = frame_lists[0][depth]
        if all(fl[depth] == frame for fl in frame_lists):
            trunk.append(frame)
        else:
            break
    return trunk


def _display_name(leaf, trunk, member_count):
    """Readable name for a group: the leaf, plus its deepest shared caller.

    Labels the group by where the family shares context (the branching point),
    which answers "what is this pile of near-duplicates" better than any single
    member's divergent tail. Members still carry their own first-unique frame
    for callers that want the "what makes this one different" view instead.
    """
    leaf_func = leaf[0]
    if member_count <= 1 or len(trunk) <= 1:
        return leaf_func
    branch_func = trunk[-1][0]
    if branch_func == leaf_func:
        return leaf_func
    return f"{leaf_func} < {branch_func}"


def group_signatures(signatures, min_group_size=DEFAULT_MIN_GROUP_SIZE):
    """Group signatures sharing a meaningful leaf frame; return multi-member groups.

    `signatures` is a list of {key, frames (leaf->root [name, lib]), ms, count}.
    Returns a list of groups sorted by descending total ms, each:

        {
          "displayName": str,
          "leafFrame": [name, lib],            # meaningful leaf (the group id)
          "commonTrunk": [[name, lib], ...],   # leaf -> branch, shared (meaningful)
          "branchFrame": [name, lib],          # deepest shared meaningful frame
          "memberCount": int,
          "totalMs": float,
          "totalCount": float,
          "avgEventLoopDepth": float,          # mean nested-event-loop depth
          "members": [
            {"frameKeys": [funcIndex, ...],      # leaf->root, identifies the member
             "ms", "count",
             "variant": int,                     # group-local variant ordinal
             "firstUniqueFrame": [name, lib] | None},
            ...                                  # sorted by descending ms
          ],
        }

    `firstUniqueFrame` is the earliest *meaningful* frame that separates a
    member from its siblings - the real branch point, not a noise frame.

    Groups smaller than `min_group_size` are dropped: a signature with no
    near-duplicate is its own row and needs no grouping.
    """
    # Precompute each signature's meaningful frames and event-loop depth once.
    for sig in signatures:
        if not sig["frames"]:
            continue
        sig["_gframes"] = _grouping_frames(sig["frames"])
        sig["_eld"] = _event_loop_depth(sig["frames"])

    buckets = {}
    for sig in signatures:
        if not sig["frames"]:
            continue
        leaf = tuple(sig["_gframes"][0])
        buckets.setdefault(leaf, []).append(sig)

    groups = []
    for leaf, members in buckets.items():
        if len(members) < min_group_size:
            continue
        trunk = _common_trunk([m["_gframes"] for m in members])
        trunk_depth = len(trunk)

        group_members = []
        variant_ids = {}
        for member in members:
            gframes = member["_gframes"]
            first_unique = gframes[trunk_depth] if len(gframes) > trunk_depth else None
            # Members sharing a variant are the same hang differing only in
            # skipped noise frames; the frontend collapses them into one row.
            # The id only has to distinguish variants within this group, so it
            # is a small ordinal rather than the meaningful stack's key.
            variant = variant_ids.setdefault(canonical_key(gframes), len(variant_ids))
            group_members.append({
                "frameKeys": member["frameKeys"],
                "ms": member["ms"],
                "count": member["count"],
                "firstUniqueFrame": first_unique,
                "variant": variant,
            })
        group_members.sort(key=lambda m: m["ms"], reverse=True)

        groups.append({
            "displayName": _display_name(list(leaf), trunk, len(members)),
            "leafFrame": list(leaf),
            "commonTrunk": [list(f) for f in trunk],
            "branchFrame": list(trunk[-1]) if trunk else list(leaf),
            "memberCount": len(members),
            "totalMs": sum(m["ms"] for m in members),
            "totalCount": sum(m["count"] for m in members),
            "avgEventLoopDepth": (sum(m["_eld"] for m in members) / len(members)),
            "members": group_members,
        })

    groups.sort(key=lambda g: g["totalMs"], reverse=True)
    return groups


def compute_leaf_groups(profile, min_group_size=DEFAULT_MIN_GROUP_SIZE):
    """Leaf-frame groups per thread for a columnar profile.

    Returns {threadName: [group, ...]}, ready to attach to the profile so the
    frontend can look up a signature's group by its canonical key.
    """
    result = {}
    for thread in profile.get("threads", []):
        signatures = signatures_from_thread(thread)
        result[thread["name"]] = group_signatures(
            signatures, min_group_size=min_group_size
        )
    return result
