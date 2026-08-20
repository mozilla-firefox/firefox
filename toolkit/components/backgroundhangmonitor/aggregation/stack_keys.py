# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Canonical hang-signature keys, shared by the BHR aggregation jobs.

A hang signature is identified by a content-based key derived from its
reconstructed stack: the ordered list of (funcName, libName) frames, leaf->root.
funcTable indices are per-file and cannot be used.

The key has to stay byte-for-byte identical everywhere it is derived, including
the dashboard outside this repo, because that is what joins a displayed hang to
its rolled-up numbers. It lives here so there is a single implementation to keep
in step rather than one per job.
"""

# Frame and stack separators for the canonical key. Control characters that
# cannot appear in a symbol or library name, so the join is unambiguous.
FIELD_SEP = "\x1f"
FRAME_SEP = "\x1e"


def canonical_key(frames):
    """Stable cross-day signature key for a leaf->root list of [name, lib]."""
    return FRAME_SEP.join(f"{name}{FIELD_SEP}{lib}" for name, lib in frames)


def reconstruct_stack(thread, sample_index):
    """Return a sample's stack as [funcName, libName] pairs, leaf->root.

    Uses the library's stripped ``name`` (not ``debugName``) so the derived key
    matches the frontend.
    """
    frames, _ = reconstruct_stack_indexed(thread, sample_index)
    return frames


def reconstruct_stack_indexed(thread, sample_index):
    """Return a sample's stack as (frames, func_indices), both leaf->root.

    Same walk as reconstruct_stack, additionally handing back each frame's
    funcTable index. Callers that emit a stack into the artifact use the
    indices, since the profile already interns those strings and repeating them
    per frame is what made leafGroups dominate the file.
    """
    string_array = thread["stringArray"]
    libs = thread["libs"]
    func_name = thread["funcTable"]["name"]
    func_lib = thread["funcTable"]["lib"]
    prefix = thread["stackTable"]["prefix"]
    func = thread["stackTable"]["func"]

    frames = []
    func_indices = []
    stack = thread["sampleTable"]["stack"][sample_index]
    while stack:
        func_index = func[stack]
        name = string_array[func_name[func_index]]
        lib_index = func_lib[func_index]
        lib = "" if lib_index is None else libs[lib_index]["name"]
        frames.append([name, lib])
        func_indices.append(func_index)
        stack = prefix[stack]
    return frames, func_indices
