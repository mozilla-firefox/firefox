#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with
# this file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Build a minimal alpha-channel VP8 WebM test video from raw VP8 frame payloads.

This script is NOT run by CI. It is checked in as documentation and as a builder
for the test video; run it by hand to regenerate it or to produce a new alpha
VP8 WebM for a test.

    ./generate_vp8_alpha_webm.py vp8_alpha_decode.webm

Why a byte-level builder instead of ffmpeg: an alpha WebM carries a second VP8
bitstream (the alpha plane) in a per-frame BlockAdditional, which is the shape
Firefox routes to the agnostic media/libvpx VPXDecoder. The default frames are a
hand-crafted VP8 bitstream supplied directly as hex rather than produced by
ffmpeg/libvpx.

What it writes (a WebM the agnostic VPXDecoder accepts):

    EBML
    Segment
      Info    -> TimecodeScale
      Tracks  -> TrackEntry(V_VP8, PixelWidth/Height, AlphaMode=1)
      Cluster -> Timecode
                 per frame: BlockGroup(Block=color VP8,
                                       BlockAdditions/BlockMore/BlockAdditional
                                         = alpha VP8)

Common knobs are exposed on the command line (output path, dimensions,
timecode scale, per-frame duration). The frame payloads themselves live in
FRAMES below; to build a different clip, replace those hex strings (each is a
complete VP8 frame) or extend FRAMES with more entries.

Run with --verify to assert the default output still matches the checked-in
test video's SHA-256.
"""

import argparse
import hashlib
from pathlib import Path

# Element IDs, verbatim EBML/Matroska/WebM ids (bytes).
EBML = bytes.fromhex("1a45dfa3")
EBML_VERSION = bytes.fromhex("4286")
EBML_READ_VERSION = bytes.fromhex("42f7")
EBML_MAX_ID_LENGTH = bytes.fromhex("42f2")
EBML_MAX_SIZE_LENGTH = bytes.fromhex("42f3")
DOC_TYPE = bytes.fromhex("4282")
DOC_TYPE_VERSION = bytes.fromhex("4287")
DOC_TYPE_READ_VERSION = bytes.fromhex("4285")
SEGMENT = bytes.fromhex("18538067")
INFO = bytes.fromhex("1549a966")
TIMECODE_SCALE = bytes.fromhex("2ad7b1")
TRACKS = bytes.fromhex("1654ae6b")
TRACK_ENTRY = bytes.fromhex("ae")
TRACK_NUMBER = bytes.fromhex("d7")
TRACK_UID = bytes.fromhex("73c5")
TRACK_TYPE = bytes.fromhex("83")
CODEC_ID = bytes.fromhex("86")
VIDEO = bytes.fromhex("e0")
PIXEL_WIDTH = bytes.fromhex("b0")
PIXEL_HEIGHT = bytes.fromhex("ba")
ALPHA_MODE = bytes.fromhex("53c0")
CLUSTER = bytes.fromhex("1f43b675")
TIMECODE = bytes.fromhex("e7")
BLOCK_GROUP = bytes.fromhex("a0")
BLOCK = bytes.fromhex("a1")
BLOCK_ADDITIONS = bytes.fromhex("75a1")
BLOCK_MORE = bytes.fromhex("a6")
BLOCK_ADD_ID = bytes.fromhex("ee")
BLOCK_ADDITIONAL = bytes.fromhex("a5")

TRACK_TYPE_VIDEO = 1
VIDEO_TRACK_NUMBER = 1

# The two default frames: a neutral-gray keyframe and a following inter frame.
# Each hex string is a complete, hand-crafted VP8 frame supplied directly; the
# color plane and the alpha plane carry the same payload here.
FRAMES = [
    {
        "color": "1602009d012a100010001007088585888584880202000619680000",
        "alpha": "1602009d012a100010001007088585888584880202000619680000",
    },
    {
        "color": "f70100401010001800186ff40c02c03e2d8000",
        "alpha": "f70100401010001800186ff40c02c03e2d8000",
    },
]

# SHA-256 of the checked-in vp8_alpha_decode.webm, for --verify.
TEST_VIDEO_SHA256 = "6d52cd596d119d5687181bc7d5692fa2f2bd51f0749a4a0d4b6526d8fa0392ef"


def vint(value):
    """Encode an unsigned integer as a minimal-length EBML variable-size int."""
    for length in range(1, 9):
        # The all-ones value of a given width is reserved for "unknown size".
        if value < (1 << (7 * length)) - 1:
            marker = 0x80 >> (length - 1)
            body = value.to_bytes(length, "big")
            return bytes([body[0] | marker]) + body[1:]
    raise ValueError(f"value too large for a vint: {value}")


def elem(element_id, payload):
    """An EBML element: id, size (as a vint), then payload."""
    return element_id + vint(len(payload)) + payload


def uint(value):
    """Big-endian minimal-width unsigned integer element body."""
    return value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big")


def block(track_number, timecode, frame):
    """A Matroska Block body: track vint, signed int16 timecode, flags, data."""
    header = (
        vint(track_number)
        + int(timecode).to_bytes(2, "big", signed=True)
        + bytes([0x00])
    )
    return header + frame


def build(width, height, timecode_scale, frame_duration_ms, frames):
    header = elem(
        EBML,
        elem(EBML_VERSION, uint(1))
        + elem(EBML_READ_VERSION, uint(1))
        + elem(EBML_MAX_ID_LENGTH, uint(4))
        + elem(EBML_MAX_SIZE_LENGTH, uint(8))
        + elem(DOC_TYPE, b"webm")
        + elem(DOC_TYPE_VERSION, uint(4))
        + elem(DOC_TYPE_READ_VERSION, uint(2)),
    )

    info = elem(INFO, elem(TIMECODE_SCALE, uint(timecode_scale)))

    video = elem(
        VIDEO,
        elem(PIXEL_WIDTH, uint(width))
        + elem(PIXEL_HEIGHT, uint(height))
        + elem(ALPHA_MODE, uint(1)),
    )
    track_entry = elem(
        TRACK_ENTRY,
        elem(TRACK_NUMBER, uint(VIDEO_TRACK_NUMBER))
        + elem(TRACK_UID, uint(VIDEO_TRACK_NUMBER))
        + elem(TRACK_TYPE, uint(TRACK_TYPE_VIDEO))
        + elem(CODEC_ID, b"V_VP8")
        + video,
    )
    tracks = elem(TRACKS, track_entry)

    groups = b""
    for index, frame in enumerate(frames):
        timecode = index * frame_duration_ms
        color = bytes.fromhex(frame["color"])
        alpha = bytes.fromhex(frame["alpha"])
        additions = elem(
            BLOCK_ADDITIONS,
            elem(
                BLOCK_MORE,
                elem(BLOCK_ADD_ID, uint(1)) + elem(BLOCK_ADDITIONAL, alpha),
            ),
        )
        groups += elem(
            BLOCK_GROUP,
            elem(BLOCK, block(VIDEO_TRACK_NUMBER, timecode, color)) + additions,
        )
    cluster = elem(CLUSTER, elem(TIMECODE, uint(0)) + groups)

    return header + elem(SEGMENT, info + tracks + cluster)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output",
        nargs="?",
        default="vp8_alpha_decode.webm",
        type=Path,
        help="output WebM path (default: vp8_alpha_decode.webm)",
    )
    parser.add_argument("--width", type=int, default=16)
    parser.add_argument("--height", type=int, default=16)
    parser.add_argument("--timecode-scale", type=int, default=1000000)
    parser.add_argument("--frame-duration-ms", type=int, default=33)
    parser.add_argument(
        "--verify",
        action="store_true",
        help="assert the output matches the checked-in test video SHA-256",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    data = build(
        args.width,
        args.height,
        args.timecode_scale,
        args.frame_duration_ms,
        FRAMES,
    )
    digest = hashlib.sha256(data).hexdigest()
    if args.verify and digest != TEST_VIDEO_SHA256:
        raise SystemExit(
            f"SHA-256 mismatch: got {digest}, expected {TEST_VIDEO_SHA256}"
        )
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes, sha256 {digest})")


if __name__ == "__main__":
    main()
