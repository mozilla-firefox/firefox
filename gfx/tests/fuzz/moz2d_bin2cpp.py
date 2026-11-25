#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from argparse import ArgumentParser
from pathlib import Path
from struct import calcsize, unpack

WR_IMAGEFORMATS = {
    1: "R8",
    2: "R16",
    3: "BGRA8",
    4: "RGBAF32",
    5: "RG8",
    6: "RG16",
    7: "RGBAI32",
    8: "RGBA8",
}


def bytes_to_hex_literal(data):
    # data is bytes
    # yields hex formatted strings like "0xFF"
    # this can be simplified in 3.8+ using bytes.hex(sep=",").split(",")
    hexified = iter(data.hex().upper())

    # see grouper in itertools recipes
    for result in zip(hexified, hexified):
        yield f"0x{''.join(result)}"


def unpack_fp(fmt, fp):
    # fmt is a struct format string
    # fp is an open file handle
    # returns tuple as per struct.unpack(fmt, ...)
    size = calcsize(fmt)
    return unpack(fmt, fp.read(size))


def main():
    aparse = ArgumentParser()
    aparse.add_argument("dump", type=Path, help="A Moz2D libFuzzer testcase to read")
    args = aparse.parse_args()

    with args.dump.open("rb") as dump:
        (
            aFormat,  # B
            aRenderRect_min_x,  # 8i
            aRenderRect_min_y,
            aRenderRect_max_x,
            aRenderRect_max_y,
            aVisibleRect_min_x,
            aVisibleRect_min_y,
            aVisibleRect_max_x,
            aVisibleRect_max_y,
            aTileSize,  # H
        ) = unpack_fp("=B8iH", dump)

        if aTileSize:
            (aTileOffset_x, aTileOffset_y) = unpack_fp("=2i", dump)

        (aDirtyRect_notnull,) = unpack_fp("=B", dump)

        if aDirtyRect_notnull:
            (
                aDirtyRect_min_x,
                aDirtyRect_min_y,
                aDirtyRect_max_x,
                aDirtyRect_max_y,
            ) = unpack_fp("=4i", dump)

        (output_len,) = unpack_fp("=I", dump)

        blob_len = 0

        print("const uint8_t blob_buffer[] = {")
        while True:
            line = tuple(bytes_to_hex_literal(dump.read(16)))
            if not line:
                break
            blob_len += len(line)
            print(f"  {', '.join(line)},")
        print("};")

    print(f"uint8_t output_buffer[{output_len}];")

    print("auto aRenderRect = mozilla::wr::LayoutIntRect {")
    print(f"  .min: {{ .x: {aRenderRect_min_x}, .y: {aRenderRect_min_y} }},")
    print(f"  .max: {{ .x: {aRenderRect_max_x}, .y: {aRenderRect_max_y} }},")
    print("};")
    print("auto aVisibleRect = mozilla::wr::DeviceIntRect {")
    print(f"  .min: {{ .x: {aVisibleRect_min_x}, .y: {aVisibleRect_min_y} }},")
    print(f"  .max: {{ .x: {aVisibleRect_max_x}, .y: {aVisibleRect_max_y} }},")
    print("};")

    if aTileSize:
        print(f"uint16_t tileSize = {aTileSize};")
        print("auto aTileOffset = mozilla::wr::TileOffset {")
        print(f"  .x: {aTileOffset_x}, .y: {aTileOffset_y},")
        print("};")
        aTileSize = "tileSize"
        aTileOffset = "&aTileOffset"
    else:
        aTileSize = "0"
        aTileOffset = "nullptr"

    if aDirtyRect_notnull:
        print("auto aDirtyRect = mozilla::wr::LayoutIntRect {")
        print(f"  .min: {{ .x: {aDirtyRect_min_x}, .y: {aDirtyRect_min_y} }},")
        print(f"  .max: {{ .x: {aDirtyRect_max_x}, .y: {aDirtyRect_max_y} }},")
        print("};")
        aDirtyRect = "&aDirtyRect"
    else:
        aDirtyRect = "nullptr"

    print()
    print("wr_moz2d_render_cb(")
    print(f"  mozilla::wr::ByteSlice {{ .buffer: blob_buffer, .len: {blob_len} }},")
    print("  mozilla::wr::IdNamespace { .mHandle: 0 },")
    if aFormat in WR_IMAGEFORMATS:
        print(f"  mozilla:wr::ImageFormat::{WR_IMAGEFORMATS[aFormat]},")
    else:
        print(f"  {aFormat}, // mozilla:wr::ImageFormat::?")
    print("  &aRenderRect, &aVisibleRect,")
    if aTileSize or aDirtyRect_notnull:
        print(f"  {aTileSize}, {aTileOffset},")
        print(f"  {aDirtyRect},")
    else:
        print(f"  {aTileSize}, {aTileOffset}, {aDirtyRect},")
    print(
        f"  mozilla::wr::MutByteSlice {{ .buffer: output_buffer,"
        f" .len: {output_len} }});"
    )


if __name__ == "__main__":
    main()
