#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Draw before/after pairs of captures into one PNG sheet, each pair under its
caption as before, after and, where it is hard to see, the difference.

    ./mach python --virtualenv desktop-theme -- sheet.py OUT.png [--title TEXT] \\
        [--diff auto|all|none] [--rect N=X,Y,W,H]... [--dpr N] [--threshold N] \\
        CAPTION BEFORE AFTER [CAPTION BEFORE AFTER]...

Run from the checkout root. The sheet numbers the captions 1, 2 and so on in
the order given, so a CAPTION leaves the number out. A pixel differs when a channel differs by more than
--threshold, as for ./mach theme pixel-diff, but the default here is 0 where
pixel-diff's is 20, so the two report different counts for one pair. The
difference is the after capture faded, with the differing pixels in magenta. By default
(--diff auto) a pair shows the difference only when no channel differs by more
than 64 or the differing pixels cover at most 1% of the capture, and the script
prints each pair it leaves the difference off; --diff all and --diff none draw
it for every pair or for none. --rect crops both captures of pair N, counted
from 1, to a rect in CSS px from the PNG's top-left corner, scaled by --dpr
(default 1) to cover every device pixel it touches and clipped to the
capture; for a screen grab of a popup, its getOuterScreenRect() is already
in the grab's coordinates. Below each pair, the captures are cropped to the
differing pixels and enlarged without smoothing; when those cover half the
capture's width or more, a capture small enough is enlarged whole. A pair is drawn stacked
when side by side would be wider than 1600 px. A sheet over Phabricator's 8 MiB
upload limit is split into OUT-1.png, OUT-2.png and so on. It prints the sheets
it wrote and exits 2 on an error.
"""

import argparse
import math
import sys
import tempfile
from pathlib import Path

# Phabricator's limit for a file uploaded in one request.
SHEET_LIMIT = 8 * 1024 * 1024
SHEET_MARGIN = 16
SHEET_GAP = 12
# The widest row of images drawn side by side; a wider one is stacked.
ROW_CAP = 1600
FONT_SIZE = 16
ZOOM_PAD = 12
# A difference is drawn by default only when it is this faint in every channel
# or covers no more than this share of the capture.
SUBTLE_DELTA = 64
SUBTLE_SHARE = 0.01
ZOOM_SIZE = 240


def sheet_font(size):
    from PIL import ImageFont

    for name in (
        "DejaVuSans.ttf",
        "LiberationSans-Regular.ttf",
        "Arial.ttf",
        "arial.ttf",
        "Helvetica.ttc",
    ):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def fonts():
    return {"caption": sheet_font(FONT_SIZE + 2), "label": sheet_font(FONT_SIZE)}


def text_size(font, text):
    left, _, right, _ = font.getbbox(text)
    ascent, descent = font.getmetrics()
    return right - left, ascent + descent


def zoom_box(bbox, size):
    """The crop of a capture of `size` around the differing pixels in `bbox`,
    plus ZOOM_PAD, and the scale that enlarges it to about ZOOM_SIZE device
    px. When the crop covers half the width or more, the whole capture takes
    its place, so that a small capture is still enlarged. None when it would
    be enlarged less than twice."""
    x0, y0 = max(0, bbox[0] - ZOOM_PAD), max(0, bbox[1] - ZOOM_PAD)
    x1, y1 = min(size[0], bbox[2] + ZOOM_PAD), min(size[1], bbox[3] + ZOOM_PAD)
    if (x1 - x0) * 2 > size[0]:
        x0, y0, x1, y1 = 0, 0, *size
    scale = min(8, ZOOM_SIZE // max(x1 - x0, y1 - y0))
    if scale < 2:
        return None
    return (x0, y0, x1, y1), scale


def enlarge(im, box, scale):
    from PIL import Image

    crop = im.crop(box)
    return crop.resize((crop.width * scale, crop.height * scale), Image.NEAREST)


def panel_row(images, labels, font):
    """Draw labelled images side by side, or stacked when side by side would be
    wider than ROW_CAP; an image of None draws its label's placeholder text in
    a grey box."""
    from PIL import Image, ImageDraw

    sizes = [im.size for im in images if im]
    line = text_size(font, "Ag")[1]
    columns = []
    for im, (label, missing) in zip(images, labels):
        if im:
            size = im.size
        else:
            w, h = sizes[0] if sizes else (0, 0)
            size = (max(w, text_size(font, missing)[0] + 12), max(h, line + 8))
        width = max(size[0], text_size(font, label)[0])
        columns.append((im, size, label, missing, width + 2))
    cell_heights = [line + 4 + c[1][1] + 2 for c in columns]
    width = sum(c[4] for c in columns) + SHEET_GAP * (len(columns) - 1)
    stacked = width > ROW_CAP
    if stacked:
        width = max(c[4] for c in columns)
        height = sum(cell_heights) + SHEET_GAP * (len(columns) - 1)
    else:
        height = max(cell_heights)
    out = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(out)
    x = top = 0
    for (im, (w, h), label, missing, column), cell_height in zip(columns, cell_heights):
        draw.text((x, top), label, fill="black", font=font)
        y = top + line + 4
        draw.rectangle((x, y, x + w + 1, y + h + 1), outline="#888888")
        if im:
            out.paste(im, (x + 1, y + 1))
        else:
            draw.rectangle((x + 1, y + 1, x + w, y + h), fill="#eeeeee")
            draw.text((x + 6, y + 4), missing, fill="#555555", font=font)
        if stacked:
            top += cell_height + SHEET_GAP
        else:
            x += column + SHEET_GAP
    return out


def pair_block(
    caption, summary, images, fonts, zoomed=None, missing_diff="no pixel differs"
):
    """One sheet row: the caption and summary, then before, after and, when
    `images` holds a third, the difference, then `zoomed`, a (images, scale)
    pair of their crops, when given."""
    from PIL import Image, ImageDraw

    labels = [
        ("Before", "not captured"),
        ("After", "not captured"),
        ("Difference", missing_diff),
    ][: len(images)]
    parts = [panel_row(images, labels, fonts["label"])]
    if zoomed:
        crops, scale = zoomed
        parts.append(
            panel_row(
                crops,
                [
                    (f"{half}, zoomed {scale}x", "")
                    for half in ("Before", "After", "Difference")[: len(crops)]
                ],
                fonts["label"],
            )
        )
    caption_size = text_size(fonts["caption"], caption)
    summary_size = text_size(fonts["label"], summary)
    width = max(caption_size[0], summary_size[0], *(p.width for p in parts))
    height = (
        caption_size[1]
        + summary_size[1]
        + 8
        + sum(p.height + SHEET_GAP for p in parts)
        - SHEET_GAP
    )
    out = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(out)
    draw.text((0, 0), caption, fill="black", font=fonts["caption"])
    draw.text((0, caption_size[1]), summary, fill="#333333", font=fonts["label"])
    y = caption_size[1] + summary_size[1] + 8
    for p in parts:
        out.paste(p, (0, y))
        y += p.height + SHEET_GAP
    return out


def compose_sheet(heading, blocks, fonts):
    from PIL import Image, ImageDraw

    heading_size = text_size(fonts["caption"], heading)
    width = max(heading_size[0], *(b.width for b in blocks)) + 2 * SHEET_MARGIN
    height = (
        2 * SHEET_MARGIN
        + heading_size[1]
        + sum(b.height + 2 * SHEET_GAP + 1 for b in blocks)
    )
    out = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(out)
    draw.text(
        (SHEET_MARGIN, SHEET_MARGIN), heading, fill="black", font=fonts["caption"]
    )
    y = SHEET_MARGIN + heading_size[1] + SHEET_GAP
    for b in blocks:
        draw.line((SHEET_MARGIN, y, width - SHEET_MARGIN, y), fill="#888888")
        y += SHEET_GAP + 1
        out.paste(b, (SHEET_MARGIN, y))
        y += b.height + SHEET_GAP
    return out


def png_bytes(im):
    import io

    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def write_sheets(path, heading, blocks, fonts):
    """Write the blocks as `path`, a .png, or as its stem plus -1.png, -2.png
    and so on when one image would exceed SHEET_LIMIT; return the paths."""
    parts = [blocks]
    encoded = None
    while True:
        encoded = []
        for i, part in enumerate(parts):
            suffix = f" ({i + 1}/{len(parts)})" if len(parts) > 1 else ""
            data = png_bytes(compose_sheet(heading + suffix, part, fonts))
            if len(data) > SHEET_LIMIT and len(part) > 1:
                half = len(part) // 2
                parts[i : i + 1] = [part[:half], part[half:]]
                break
            encoded.append(data)
        else:
            break
    paths = []
    for i, data in enumerate(encoded):
        out = path.with_name(f"{path.stem}-{i + 1}.png") if len(encoded) > 1 else path
        if len(data) > SHEET_LIMIT:
            print(
                f"{out.name}: one row alone is {len(data) // 1024} KiB, over the "
                f"{SHEET_LIMIT // 1024 // 1024} MiB upload limit",
                file=sys.stderr,
            )
        out.write_bytes(data)
        paths.append(out)
    return paths


def subtle(count, max_delta, size):
    """Whether a difference is hard to see, by its largest channel difference
    or by the share of the capture it covers."""
    return max_delta <= SUBTLE_DELTA or count <= SUBTLE_SHARE * size[0] * size[1]


def pair(
    caption, before_path, after_path, rect, diff_mode, threshold, pixel_diff, tmp, fonts
):
    """One sheet row for a pair, and why it leaves the difference out, or
    None."""
    from PIL import Image

    after, before = pixel_diff.load(after_path), pixel_diff.load(before_path)
    if rect:
        width = min(after.width, before.width)
        height = min(after.height, before.height)
        box = (
            max(rect[0], 0),
            max(rect[1], 0),
            min(rect[2], width),
            min(rect[3], height),
        )
        if box[0] >= box[2] or box[1] >= box[3]:
            pixel_diff.fail(
                f"{caption}: rect is device px {rect}, outside the "
                f"{width}x{height} capture"
            )
        after, before = (im.crop(box) for im in (after, before))
    mask, channel_max = pixel_diff.diff_mask(after, before, threshold)
    count = mask.histogram()[255]
    max_delta = channel_max.getextrema()[1]
    notes = []
    if after.size != before.size:
        notes.append("size {}x{} → {}x{} device px".format(*before.size, *after.size))
    images = [before.convert("RGB"), after.convert("RGB")]
    left_out = None
    if count:
        bbox = mask.getbbox()
        notes.append(
            f"{count} pixels differ, from {bbox[0]},{bbox[1]} to {bbox[2]},{bbox[3]}"
        )
        share = count / (mask.width * mask.height)
        if diff_mode == "all" or (
            diff_mode == "auto" and subtle(count, max_delta, mask.size)
        ):
            diff_path = Path(tmp) / "diff.png"
            pixel_diff.write_diff_image(str(diff_path), after, mask, None)
            with Image.open(diff_path) as im:
                images.append(im.convert("RGB"))
        elif diff_mode == "auto":
            left_out = (
                f"the largest channel difference is {max_delta} and the "
                f"differing pixels cover {share:.1%} of the capture"
            )
    else:
        notes.append("no pixel differs")
        if diff_mode != "none":
            images.append(None)
    zoomed = None
    picked = count and zoom_box(mask.getbbox(), mask.size)
    if picked:
        box, scale = picked
        zoomed = ([enlarge(im, box, scale) for im in images], scale)
    return pair_block(caption, "; ".join(notes), images, fonts, zoomed), left_out


def numbered_rect(text):
    number, _, rect = text.partition("=")
    try:
        n = int(number)
        x, y, w, h = (float(v) for v in rect.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError("expected N=X,Y,W,H")
    if n < 1 or w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError(
            "expected N=X,Y,W,H with N >= 1 and a positive W and H"
        )
    return n, (x, y, w, h)


def get_parser():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("out", help="the sheet to write, a .png")
    ap.add_argument(
        "pairs",
        nargs="+",
        metavar="CAPTION BEFORE AFTER",
        help="a caption and the two captures of each pair; the sheet numbers "
        "the captions itself",
    )
    ap.add_argument("--title", default="Before and after", help="the sheet's heading")
    ap.add_argument(
        "--rect",
        action="append",
        default=[],
        type=numbered_rect,
        metavar="N=X,Y,W,H",
        help="crop both captures of pair N to this rect in CSS px (repeatable)",
    )
    ap.add_argument(
        "--dpr",
        type=float,
        default=1.0,
        help="device pixels per CSS px in the captures (default 1)",
    )
    ap.add_argument(
        "--diff",
        choices=("auto", "all", "none"),
        default="auto",
        help="draw the difference of every pair that differs, of none, or "
        "(auto, the default) of those where the difference is hard to see",
    )
    ap.add_argument(
        "--threshold",
        type=int,
        default=0,
        help="count a pixel when a channel differs by more than this (default 0)",
    )
    return ap


def main():
    ap = get_parser()
    args = ap.parse_args()
    if len(args.pairs) % 3:
        ap.error("give a caption, a before and an after capture for each pair")
    out = Path(args.out)
    if out.suffix != ".png":
        ap.error(f"{args.out} is not a .png")
    if args.dpr <= 0:
        ap.error("--dpr must be positive")
    sys.path.insert(
        0, str(Path(__file__).resolve().parents[4] / "toolkit/themes/tools")
    )
    import pixel_diff

    sheet_fonts = fonts()
    triples = [args.pairs[i : i + 3] for i in range(0, len(args.pairs), 3)]
    rects = {}
    for n, (x, y, w, h) in args.rect:
        if n > len(triples):
            ap.error(f"--rect {n}=...: there are only {len(triples)} pairs")
        # Cover every device pixel the rect touches, as pixel-diff does.
        rects[n] = (
            math.floor(x * args.dpr + 1e-9),
            math.floor(y * args.dpr + 1e-9),
            math.ceil((x + w) * args.dpr - 1e-9),
            math.ceil((y + h) * args.dpr - 1e-9),
        )
    blocks = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (caption, before, after) in enumerate(triples, 1):
            block, left_out = pair(
                f"{i}. {caption}",
                before,
                after,
                rects.get(i),
                args.diff,
                args.threshold,
                pixel_diff,
                tmp,
                sheet_fonts,
            )
            blocks.append(block)
            if left_out:
                print(f"pair {i}: no difference drawn, since {left_out}")
    for path in write_sheets(out, args.title, blocks, sheet_fonts):
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
