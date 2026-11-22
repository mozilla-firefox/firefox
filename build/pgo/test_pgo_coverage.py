# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit
import pytest
from pgo_coverage import (
    OUTSIDE_TREE,
    RUST_HASH,
    directory_coverage,
    function_key,
    parse_symbols,
    parse_undname,
    perfherder_suite,
    source_dir,
    unescape_rust,
)

SYMBOLS = """\
MODULE Linux x86_64 0123456789ABCDEF0123456789ABCDEF0 libxul.so
INFO CODE_ID 0123456789ABCDEF
FILE 0 hg:hg.mozilla.org/mozilla-central:dom/media/MediaDecoder.cpp:0123abcd
FILE 1 hg:hg.mozilla.org/mozilla-central:xpcom/string/nsTString.h:0123abcd
FILE 2 /usr/include/c++/v1/vector
FILE 3 git:github.com/rust-lang/rust:library/core/src/fmt/mod.rs:0123abcd
FILE 4 s3:gecko-generated-sources:0123abcd/dom/bindings/WindowBinding.cpp:
INLINE_ORIGIN 0 nsTString<char>::Length() const
INLINE_ORIGIN 1 std::vector<int>::size() const
FUNC 1000 30 0 mozilla::MediaDecoder::Play()
INLINE 0 10 0 0 1008 8
INLINE 1 20 1 1 100c 4
1000 8 5 0
1008 4 7 1
100c 4 8 2
1010 20 9 0
FUNC m 2000 10 0 mozilla::dom::Window_Binding::get_foo(JSContext*)
2000 10 3 4
FUNC 3000 10 0 core::fmt::write
3000 10 1 3
PUBLIC 4000 0 _start
STACK CFI INIT 1000 30 .cfa: $rsp 8 +
"""


@pytest.mark.parametrize(
    "path,expected",
    [
        (
            "hg:hg.mozilla.org/try:dom/media/MediaDecoder.cpp:0123abcd",
            "dom/media",
        ),
        (
            "git:github.com/rust-lang/rust:library/core/src/lib.rs:0123abcd",
            OUTSIDE_TREE,
        ),
        (
            "s3:gecko-generated-sources:0123abcd/ipc/ipdl/PContent.cpp:",
            "(generated)/ipc/ipdl",
        ),
        ("/usr/include/c++/v1/vector", OUTSIDE_TREE),
    ],
)
def test_source_dir(path, expected):
    assert source_dir(path) == expected


# Pairs of a name as dump_syms writes it and as llvm-cxxfilt --no-params
# demangles it.
@pytest.mark.parametrize(
    "dump_syms,cxxfilt",
    [
        ("mozilla::Foo::Bar(int, char const*)", "mozilla::Foo::Bar"),
        ("mozilla::Foo::Bar() const", "mozilla::Foo::Bar"),
        ("mozilla::Maybe<int>::ref() const &", "mozilla::Maybe<int>::ref"),
        ("mozilla::Foo::Bar() const volatile &&", "mozilla::Foo::Bar"),
        ("void mozilla::Foo<int>(int)", "void mozilla::Foo<int>"),
        ("Foo::operator()(int)", "Foo::operator()"),
        ("Foo::operator()() const", "Foo::operator()"),
        ("Foo::operator()<int>(int)", "Foo::operator()<int>"),
        ("Foo::operator->()", "Foo::operator->"),
        ("Foo::operator>>(int)", "Foo::operator>>"),
        ("Foo::operator<<(int)", "Foo::operator<<"),
        # llvm-undname
        ("mozilla::Foo::Bar(int, char const*)", "mozilla::Foo::Bar(int, char const *)"),
        ("mozilla::Foo::Bar() const", "mozilla::Foo::Bar(void) const"),
    ],
)
def test_function_key(dump_syms, cxxfilt):
    assert function_key(dump_syms) == function_key(cxxfilt)


def test_parse_undname():
    output = (
        "?Bar@Foo@@QEAAXH@Z\nFoo::Bar(int)\n\n"
        "?bad\n\n"
        "?Baz@Foo@mozilla@@QEBAHXZ\nmozilla::Foo::Baz(void) const\n\n"
    )
    assert parse_undname(output) == {
        "?Bar@Foo@@QEAAXH@Z": "Foo::Bar(int)",
        "?Baz@Foo@mozilla@@QEBAHXZ": "mozilla::Foo::Baz(void) const",
    }


def test_unescape_rust():
    name = (
        "_$LT$core..fmt..Arguments$u20$as$u20$core..fmt..Display$GT$"
        "::fmt::h0123456789abcdef"
    )
    assert (
        unescape_rust(RUST_HASH.sub("", name))
        == "<core::fmt::Arguments as core::fmt::Display>::fmt"
    )
    assert (
        unescape_rust("core::ptr::drop_in_place$LT$$LP$u8$C$$u20$u32$RP$$GT$")
        == "core::ptr::drop_in_place<(u8, u32)>"
    )
    assert unescape_rust("_$u7b$$u7b$closure$u7d$$u7d$") == "{{closure}}"
    assert unescape_rust("mozilla::Foo::Bar") == "mozilla::Foo::Bar"


def test_parse_symbols(tmp_path):
    sym_file = tmp_path / "libxul.so.sym"
    sym_file.write_text(SYMBOLS)
    function_dirs = {}
    parse_symbols(str(sym_file), function_dirs)
    assert function_dirs == {
        "mozilla::MediaDecoder::Play": "dom/media",
        "nsTString<char>::Length": "xpcom/string",
        "std::vector<int>::size": OUTSIDE_TREE,
        "mozilla::dom::Window_Binding::get_foo": "(generated)/dom/bindings",
        "core::fmt::write": OUTSIDE_TREE,
    }


def test_parse_symbols_keeps_first_directory(tmp_path):
    sym_file = tmp_path / "libxul.so.sym"
    sym_file.write_text(SYMBOLS)
    function_dirs = {"mozilla::MediaDecoder::Play": "elsewhere"}
    parse_symbols(str(sym_file), function_dirs)
    assert function_dirs["mozilla::MediaDecoder::Play"] == "elsewhere"


def test_directory_coverage():
    functions = [
        ("dom/media/webrtc", True),
        ("dom/media/webrtc", False),
        ("dom/media", False),
        ("xpcom/string", True),
    ]
    total, executed = directory_coverage(functions, 2)
    assert total == {"dom/media": 3, "xpcom/string": 1}
    assert executed == {"dom/media": 1, "xpcom/string": 1}

    total, executed = directory_coverage(functions, 3)
    assert total == {"dom/media/webrtc": 2, "dom/media": 1, "xpcom/string": 1}


def test_perfherder_suite():
    total = {"dom/media": 1000, "xpcom/string": 10}
    executed = {"dom/media": 250, "xpcom/string": 10}
    suite = perfherder_suite(total, executed)
    assert suite["value"] == pytest.approx(100 * 260 / 1010)
    assert [(s["name"], s["value"]) for s in suite["subtests"]] == [("dom/media", 25)]


if __name__ == "__main__":
    mozunit.main()
