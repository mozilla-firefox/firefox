#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Count the pixels that differ between two captures and print their bounding box.

Run under `./mach python --virtualenv wpt`, the mach virtualenv with Pillow
(plain `./mach python` does not):

    ./mach python --virtualenv wpt pixel-diff.py after.png before.png [--rect X,Y,W,H]

Like cmp, it exits 0 when no pixels differ, 1 when some do and 2 on an error.

A pixel differs when any one channel, alpha included, differs by more than
--threshold (default 20, out of 255; 0 counts every difference), so a hue
shift counts as much as a brightness one. --rect confines the count and the
bounding box to a rect in CSS px measured from the PNG's own top-left corner,
not the window's: for a capture.js frame, subtract the SHOT line's origin=
from a window rect. --dpr scales the rect to device pixels and --offset
shifts it by X,Y CSS px, the page's mozInnerScreenX/Y when the captures are
screen grabs rather than page screenshots. The rect covers every device pixel
it touches, and is clipped to the image. The printed bbox is (left, top,
right, bottom) in device pixels of the whole image, the rect's position and
--offset included. Two captures of different sizes
differ in every pixel only one of them has. A screen grab includes the
browser chrome, so a hit at the window's bottom edge may be the link-target
status panel.

--lines N also prints the device-pixel rows and columns holding at least N
differing pixels, merged into runs, and the most common color pairs among the
differing pixels as png -> other, so a 1px border change reads as "row 84"
with its before and after colors.
"""

import argparse
import math
import sys
import traceback

from PIL import Image, ImageChops

EXIT_ERROR = 2


def floats(n, positive=()):
    def parse(s):
        try:
            v = [float(x) for x in s.split(",")]
        except ValueError:
            v = []
        if len(v) != n:
            raise argparse.ArgumentTypeError(f"expected {n} comma-separated numbers")
        if any(v[i] <= 0 for i in positive):
            raise argparse.ArgumentTypeError("width and height must be positive")
        return v

    return parse


def fail(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(EXIT_ERROR)


def load(path):
    try:
        im = Image.open(path)
        im.load()
    except OSError as e:
        fail(e)
    if im.mode.startswith(("I", "F")):
        fail(f"{path}: mode {im.mode} is not 8 bits per channel")
    return im.convert("RGBA")


def runs(counts, minimum, offset):
    out = []
    for i, c in enumerate(counts, offset):
        if c < minimum:
            continue
        if out and out[-1][1] == i - 1:
            out[-1][1] = i
            out[-1][2] += c
        else:
            out.append([i, i, c])
    return (
        ", ".join(f"{a} ({n})" if a == b else f"{a}-{b} ({n})" for a, b, n in out)
        or f"none with >= {minimum}"
    )


def report_lines(d, origin, im, other, minimum):
    px = d.load()
    a = im.load()
    b = other.load()
    w = min(im.width, other.width) - origin[0]
    h = min(im.height, other.height) - origin[1]
    rows = [0] * d.height
    cols = [0] * d.width
    pairs = {}
    for y in range(d.height):
        for x in range(d.width):
            if px[x, y] != 255:
                continue
            rows[y] += 1
            cols[x] += 1
            if x < w and y < h:
                key = (a[x + origin[0], y + origin[1]], b[x + origin[0], y + origin[1]])
                pairs[key] = pairs.get(key, 0) + 1
    print(f"rows: {runs(rows, minimum, origin[1])}")
    print(f"columns: {runs(cols, minimum, origin[0])}")
    for (p, q), n in sorted(pairs.items(), key=lambda kv: -kv[1])[:3]:
        print(f"colors: rgba{p} -> rgba{q} x{n}")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("png")
    ap.add_argument("other")
    ap.add_argument(
        "--rect",
        metavar="X,Y,W,H",
        type=floats(4, positive=(2, 3)),
        help="confine the count and bounding box to this rect, in CSS px from the PNG's top-left",
    )
    ap.add_argument(
        "--dpr",
        type=float,
        default=1.0,
        help="device pixels per CSS pixel in the captures",
    )
    ap.add_argument(
        "--offset",
        metavar="X,Y",
        type=floats(2),
        default=[0.0, 0.0],
        help="X,Y CSS px added to the rect: the page's mozInnerScreenX/Y when the PNGs are screen grabs",
    )
    ap.add_argument(
        "--threshold",
        type=int,
        default=20,
        help="count a pixel when a channel differs by more than this (default 20; 0 counts any difference)",
    )
    ap.add_argument(
        "--lines",
        metavar="N",
        type=int,
        help="also print the rows and columns with at least N differing pixels, and the top color pairs",
    )
    args = ap.parse_args()
    if args.dpr <= 0:
        fail("--dpr must be positive")
    if args.lines is not None and args.lines < 1:
        fail("--lines must be at least 1")
    ox, oy = args.offset

    im = load(args.png)
    other = load(args.other)
    w = min(im.width, other.width)
    h = min(im.height, other.height)
    bands = ImageChops.difference(im.crop((0, 0, w, h)), other.crop((0, 0, w, h)))
    r, g, b, a = bands.split()
    channel_max = ImageChops.lighter(ImageChops.lighter(r, g), ImageChops.lighter(b, a))
    overlap = channel_max.point(lambda v: 255 if v > args.threshold else 0)
    d = Image.new("L", (max(im.width, other.width), max(im.height, other.height)))
    for size in (im.size, other.size):
        d.paste(255, (0, 0, *size))
    d.paste(overlap, (0, 0))

    notes = []
    if im.size != other.size:
        notes.append(f"sizes differ: {im.size} vs {other.size}")
    if args.rect:
        rx, ry, rw, rh = args.rect
        # Cover every device pixel the rect touches; the epsilon keeps float
        # error such as 10 * 1.1 = 11.000000000000002 from adding a pixel.
        x0, y0, x1, y1 = (ox + rx, oy + ry, ox + rx + rw, oy + ry + rh)
        box = (
            math.floor(x0 * args.dpr + 1e-9),
            math.floor(y0 * args.dpr + 1e-9),
            math.ceil(x1 * args.dpr - 1e-9),
            math.ceil(y1 * args.dpr - 1e-9),
        )
        clipped = (
            max(box[0], 0),
            max(box[1], 0),
            min(box[2], d.width),
            min(box[3], d.height),
        )
        if clipped[0] >= clipped[2] or clipped[1] >= clipped[3]:
            fail(f"rect is device px {box}, outside the {d.width}x{d.height} image")
        if clipped != box:
            notes.append(f"rect clipped from device px {box} to {clipped}")
        box = clipped
        d = d.crop(box)
    bbox = d.getbbox()
    if bbox and args.rect:
        bbox = (
            bbox[0] + box[0],
            bbox[1] + box[1],
            bbox[2] + box[0],
            bbox[3] + box[1],
        )
    n = d.histogram()[255]
    where = " within rect {:g},{:g},{:g},{:g}".format(*args.rect) if args.rect else ""
    note = f" ({'; '.join(notes)})" if notes else ""
    if bbox:
        print(f"{n} pixels differ{where}{note}; bbox {bbox}")
    else:
        hist = (channel_max.crop(box) if args.rect else channel_max).histogram()
        below = sum(hist[1:])
        if below:
            top = max(i for i, c in enumerate(hist) if c)
            note += (
                f"; {below} differ at --threshold 0, by at most {top}"
                f" per channel, under --threshold {args.threshold:g}"
            )
        print(f"no pixels differ{where}{note}")
    if bbox and args.lines is not None:
        report_lines(d, box[:2] if args.rect else (0, 0), im, other, args.lines)
    return 1 if bbox else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except BaseException:
        traceback.print_exc()
        sys.exit(EXIT_ERROR)
