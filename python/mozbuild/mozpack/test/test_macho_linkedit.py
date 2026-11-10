# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import struct
from io import BytesIO

import mozunit

from mozpack.executables import macho_linkedit_errors


def macho(symoff, stroff, strsize=8, nsyms=1, indirect=None):
    cmds = [
        struct.pack(
            "<2L16s4Q2l2L", 0x19, 72, b"__LINKEDIT", 0, 0x100, 0x1000, 0x100, 1, 1, 0, 0
        ),
        struct.pack("<6L", 0x2, 24, symoff, nsyms, stroff, strsize),
    ]
    if indirect:
        dysymtab = [0] * 18
        dysymtab[12:14] = indirect
        cmds.append(struct.pack("<20L", 0xB, 80, *dysymtab))
    data = b"".join(cmds)
    header = struct.pack(
        "<8L", 0xFEEDFACF, 0x0100000C, 0, 6, len(cmds), len(data), 0, 0
    )
    return header + data


def errors(data):
    return macho_linkedit_errors(BytesIO(data))


def test_aligned():
    assert errors(macho(0x1000, 0x1010)) == []


def test_misaligned_string_pool():
    assert errors(macho(0x1000, 0x1014)) == [
        "string pool at 0x1014 is not 8-byte aligned"
    ]


def test_overlap_and_overflow():
    assert errors(macho(0x1000, 0x1018, 0x100, nsyms=2)) == [
        "string pool overlaps symbol table",
        "string pool extends past the end of __LINKEDIT",
    ]


def test_starts_before_linkedit():
    assert errors(macho(0xF00, 0x1010)) == ["symbol table starts before __LINKEDIT"]


def test_string_pool_after_indirect_symbol_table():
    assert errors(macho(0x1000, 0x1020, indirect=(0x1010, 3))) == []
    assert errors(macho(0x1000, 0x101C, indirect=(0x1010, 3))) == [
        "string pool at 0x101c is not 8-byte aligned"
    ]


if __name__ == "__main__":
    mozunit.main()
