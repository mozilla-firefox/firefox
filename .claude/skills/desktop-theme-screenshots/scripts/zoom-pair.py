#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Crop the same rect from a before and an after capture, enlarge both without
smoothing, and put them side by side, before on the left.

Run under `./mach python --virtualenv wpt`, the mach virtualenv with Pillow:

    ./mach python --virtualenv wpt zoom-pair.py before.png after.png zoom.png \
        --rect X,Y,W,H [--dpr 2] [--offset X,Y] [--scale 8] [--gap 8]

--rect, --dpr and --offset mean what they mean for pixel-diff.py: CSS px from
the PNG's own top-left corner, scaled to device pixels, shifted by the page's
mozInnerScreenX/Y for a screen grab. The rect is clipped to the smaller of the
two images, with a note on stderr. --scale multiplies each device pixel
(default 8) and --gap puts that many output pixels of magenta between the
halves, a color no theme surface uses, so the gap cannot read as a line. It
prints "wrote OUT: WxH px" and exits 2 on an error.
"""

import argparse
import math
import sys

from PIL import Image


def fail(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(2)


def rect(s):
    try:
        v = [float(x) for x in s.split(",")]
    except ValueError:
        v = []
    if len(v) != 4 or v[2] <= 0 or v[3] <= 0:
        raise argparse.ArgumentTypeError("expected X,Y,W,H with a positive W and H")
    return v


def load(path):
    try:
        im = Image.open(path)
        im.load()
    except OSError as e:
        fail(e)
    if im.mode.startswith(("I", "F")):
        fail(f"{path}: mode {im.mode} is not 8 bits per channel")
    return im.convert("RGBA")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("out")
    ap.add_argument("--rect", metavar="X,Y,W,H", type=rect, required=True)
    ap.add_argument("--dpr", type=float, default=1.0)
    ap.add_argument("--offset", metavar="X,Y", default="0,0")
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--gap", type=int, default=8)
    args = ap.parse_args()
    if args.scale < 1 or args.gap < 0:
        fail("--scale must be at least 1 and --gap at least 0")
    if args.dpr <= 0:
        fail("--dpr must be positive")
    try:
        ox, oy = (float(v) for v in args.offset.split(","))
    except ValueError:
        fail("--offset: expected X,Y")

    x, y, w, h = args.rect
    x, y = x + ox, y + oy
    box = (
        math.floor(x * args.dpr + 1e-9),
        math.floor(y * args.dpr + 1e-9),
        math.ceil((x + w) * args.dpr - 1e-9),
        math.ceil((y + h) * args.dpr - 1e-9),
    )
    images = [load(path) for path in (args.before, args.after)]
    iw = min(im.width for im in images)
    ih = min(im.height for im in images)
    clipped = (max(box[0], 0), max(box[1], 0), min(box[2], iw), min(box[3], ih))
    if clipped[0] >= clipped[2] or clipped[1] >= clipped[3]:
        fail(f"rect is device px {box}, outside the {iw}x{ih} overlap of the images")
    if clipped != box:
        print(f"note: rect clipped from device px {box} to {clipped}", file=sys.stderr)
    halves = []
    for im in images:
        crop = im.crop(clipped)
        halves.append(
            crop.resize(
                (crop.width * args.scale, crop.height * args.scale), Image.NEAREST
            )
        )
    hw, hh = halves[0].size
    out = Image.new("RGBA", (2 * hw + args.gap, hh), (255, 0, 255, 255))
    out.paste(halves[0], (0, 0))
    out.paste(halves[1], (hw + args.gap, 0))
    try:
        out.save(args.out)
    except (OSError, ValueError) as e:
        fail(e)
    print(f"wrote {args.out}: {out.width}x{out.height} px")


if __name__ == "__main__":
    main()
