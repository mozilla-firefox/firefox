import mozunit
import pytest
from mozleak import ASanErrorParser


class FakeLogger:
    def __init__(self):
        self.errors = []

    def asan_error(self, **data):
        self.errors.append(data)


DOCUMENT_H = "/builds/worker/workspace/obj-build/dist/include/mozilla/dom/Document.h"
SEGV_MESSAGE = (
    "SEGV on unknown address 0x00000000030b "
    "(pc 0x7b03e9dd40af bp 0x7fff627fb7f0 sp 0x7fff627fb7e0 T0)"
)

# A content process report relayed through the parent's stdout, with the
# output of other processes and the harness interleaved while the runtime
# symbolizes the stack.
SEGV_REPORT = f"""\
AddressSanitizer:DEADLYSIGNAL
=================================================================
==28130==ERROR: AddressSanitizer: {SEGV_MESSAGE}
==28130==The signal is caused by a READ memory access.
==28130==Hint: address points to the zero page.
-----------------------------------------------------
Suppressions used:
  count      bytes template
      3        320 libfontconfig.so
-----------------------------------------------------
TEST-OK | /css/selectors/old-tests/css3-modsel-21b.xml | took 1252ms
    #0 0x7b03e9dd40af in GetInnerWindow {DOCUMENT_H}:1942:12
    #1 0x7b03e9dd40af in mozilla::dom::SpeculationRules::RemoveLink(mozilla::dom::Element*) /builds/worker/checkouts/gecko/dom/base/SpeculationRules.cpp:385:7
    #2 0x7b03f2c61a75 in std::thread::Builder::spawn::hdeadbeefdeadbeef /builds/worker/checkouts/gecko/x.rs:1:1
    #3 0x561bbfeecf08 in _start (/builds/worker/workspace/build/application/firefox/firefox+0xc1f08) (BuildId: a89a059a4297241439326ea1c0f4913c1fb0a9d6)
==28130==Register values:
rax = 0x0000000000000309  rbx = 0x00007bc4038bca80  rcx = 0x0000000000000310  rdx = 0x0000000000000000
 r8 = 0x00007bc4038bcada   r9 = 0x00000f788071795b  r10 = 0x00007bc4038bcad0  r11 = 0x00000f788071795a
AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: SEGV {DOCUMENT_H}:1942:12 in GetInnerWindow
==28130==ABORTING
"""
SEGV_INTERLEAVED = range(5, 11)
SEGV_KEY = (28130, SEGV_MESSAGE)

UAF_MESSAGE = (
    "heap-use-after-free on address 0x60300000eff0 at pc 0x55d2f4b1c1a4 "
    "bp 0x7ffd1c2b3c40 sp 0x7ffd1c2b3c38"
)

UAF_REPORT = f"""\
=================================================================
==12345==ERROR: AddressSanitizer: {UAF_MESSAGE}
READ of size 4 at 0x60300000eff0 thread T0
    #0 0x55d2f4b1c1a3 in main /src/uaf.cpp:5:10
    #1 0x7f8c2a4291c9 in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
0x60300000eff0 is located 0 bytes inside of 32-byte region [0x60300000eff0,0x60300000f010)
freed by thread T0 here:
    #0 0x55d2f4b1a8d2 in operator delete[](void*) /src/asan_new_delete.cpp:155:3
    #1 0x55d2f4b1c16d in main /src/uaf.cpp:4:3
previously allocated by thread T0 here:
    #0 0x55d2f4b19d7d in operator new[](unsigned long) /src/asan_new_delete.cpp:98:3
    #1 0x55d2f4b1c15a in main /src/uaf.cpp:3:19
SUMMARY: AddressSanitizer: heap-use-after-free /src/uaf.cpp:5:10 in main
Shadow bytes around the buggy address:
  0x60300000ed00: fa fa fd fd fd fd fa fa fd fd fd fd fa fa fd fd
=>0x60300000ef80: fa fa fa fa fa fa fa fa fa fa fa fa fa fa[fd]fd
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
==12345==ABORTING
"""

LSAN_REPORT = """\
=================================================================
==1689==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 488 byte(s) in 3 object(s) allocated from:
    #0 0x55b338290a4e in malloc /builds/worker/fetches/llvm-project/compiler-rt/lib/asan/asan_malloc_linux.cpp:69:3
    #1 0x7b4178779e4f in moz_xmalloc /builds/worker/checkouts/gecko/memory/mozalloc/mozalloc.cpp:52:15

SUMMARY: AddressSanitizer: 488 byte(s) leaked in 3 allocation(s).
"""


def feed(parser, text, pid=None, **kwargs):
    return [parser.log(line, pid=pid, **kwargs) for line in text.splitlines()]


def test_segv_report():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    consumed = feed(parser, SEGV_REPORT, pid=19529, scope="/css/selectors", test="t1")

    # Only the ruler before the header and the interleaved lines are unknown;
    # they do not close the report.
    assert [i for i, c in enumerate(consumed) if not c] == [1, *SEGV_INTERLEAVED]
    assert len(logger.errors) == 1
    report = logger.errors[0]
    assert report["kind"] == "SEGV"
    assert report["message"] == SEGV_MESSAGE
    assert report["pid"] == 28130
    assert report["file"] == DOCUMENT_H
    assert report["lineno"] == 1942
    assert report["column"] == 12
    assert report["function"] == "GetInnerWindow"
    assert report["stacks"] is None
    assert report["scope"] == "/css/selectors"
    assert report["test"] == "t1"
    assert report["quiet"] is False
    assert "time" not in report

    stack = report["stack"]
    assert [f["function"] for f in stack] == [
        "GetInnerWindow",
        "mozilla::dom::SpeculationRules::RemoveLink(mozilla::dom::Element*)",
        "std::thread::Builder::spawn",
        "_start",
    ]
    assert stack[0] == {
        "function": "GetInnerWindow",
        "file": DOCUMENT_H,
        "line": 1942,
        "column": 12,
    }
    assert stack[3] == {
        "function": "_start",
        "module": "/builds/worker/workspace/build/application/firefox/firefox",
        "module_offset": 0xC1F08,
    }


def test_use_after_free_stacks():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    consumed = feed(parser, UAF_REPORT)

    assert consumed == [False] + [True] * (len(consumed) - 1)
    assert len(logger.errors) == 1
    report = logger.errors[0]
    assert report["kind"] == "heap-use-after-free"
    assert report["message"] == UAF_MESSAGE
    assert report["pid"] == 12345
    assert (report["file"], report["lineno"], report["column"]) == (
        "/src/uaf.cpp",
        5,
        10,
    )
    assert report["function"] == "main"
    assert [f["function"] for f in report["stack"]] == [
        "main",
        "__libc_start_call_main",
    ]
    assert [s["label"] for s in report["stacks"]] == [
        "freed by thread T0 here",
        "previously allocated by thread T0 here",
    ]
    assert [f["line"] for f in report["stacks"][0]["stack"]] == [155, 4]
    assert [f["line"] for f in report["stacks"][1]["stack"]] == [98, 3]


@pytest.mark.parametrize(
    ("message", "kind"),
    [
        (SEGV_MESSAGE, "SEGV"),
        (UAF_MESSAGE, "heap-use-after-free"),
        ("attempting double-free on 0x602000000010 in thread T0:", "double-free"),
        (
            "attempting free on address which was not malloc()-ed: "
            "0x7ffd1c2b3c40 in thread T0",
            "bad-free",
        ),
        (
            "alloc-dealloc-mismatch (malloc vs operator delete) on 0x602000000010",
            "alloc-dealloc-mismatch",
        ),
        (
            "requested allocation size 0xfffffffffffffff0 (0x0 after adjustments "
            "for alignment, red zones etc.) exceeds maximum supported size of "
            "0x10000000000 (thread T0)",
            "allocation-size-too-big",
        ),
        (
            "stack-overflow on address 0x7ffd1c2b3c40 (pc 0x55d2f4b1c1a4 "
            "bp 0x7ffd1c2b3c40 sp 0x7ffd1c2b3c38 T0)",
            "stack-overflow",
        ),
        ("negative-size-param: (size=-1)", "negative-size-param"),
        (
            "memcpy-param-overlap: memory ranges [0x1,0x2) and [0x1, 0x2) overlap",
            "memcpy-param-overlap",
        ),
    ],
)
def test_kind_from_header_when_summary_missing(message, kind):
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    parser.log(f"==42==ERROR: AddressSanitizer: {message}")
    parser.log("    #0 0x1 in main /src/a.cpp:1:1")
    assert logger.errors == []

    parser.flush()
    assert len(logger.errors) == 1
    assert logger.errors[0]["kind"] == kind
    assert logger.errors[0]["message"] == message
    assert logger.errors[0]["file"] is None
    assert logger.errors[0]["function"] is None
    assert len(logger.errors[0]["stack"]) == 1

    parser.flush()
    assert len(logger.errors) == 1


def test_summary_overrides_the_header_kind():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    parser.log(
        "==42==ERROR: AddressSanitizer: attempting double-free on 0x1 in thread T0:"
    )
    parser.log("SUMMARY: AddressSanitizer: double-free (libxul.so+0x1234) in free")

    assert logger.errors[0]["kind"] == "double-free"
    assert logger.errors[0]["file"] is None
    assert logger.errors[0]["function"] == "free"


def test_aborting_without_summary_finishes_the_report():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    parser.log(f"==42==ERROR: AddressSanitizer: {SEGV_MESSAGE}")
    assert parser.log("==42==ABORTING")

    assert len(logger.errors) == 1
    assert logger.errors[0]["stack"] is None
    assert not parser.log("==42==ABORTING")


def test_will_retry_header():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    assert parser.log(f"==42==ERROR (will retry): AddressSanitizer: {SEGV_MESSAGE}")
    parser.flush()

    assert logger.errors[0]["message"] == SEGV_MESSAGE


def test_quiet_scope_test_and_time_are_propagated():
    logger = FakeLogger()
    parser = ASanErrorParser(logger, scope="xpcshell")
    feed(parser, UAF_REPORT, pid=4242, test="test_a.js", time=1234, quiet=True)

    assert len(logger.errors) == 1
    assert logger.errors[0]["scope"] == "xpcshell"
    assert logger.errors[0]["test"] == "test_a.js"
    assert logger.errors[0]["time"] == 1234
    assert logger.errors[0]["quiet"] is True


def test_interleaved_streams():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    a = SEGV_REPORT.splitlines()
    b = UAF_REPORT.splitlines()
    for line in a[:5]:
        parser.log(line, pid="A")
    for line in b:
        parser.log(line, pid="B")
    for line in a[5:]:
        parser.log(line, pid="A")

    assert [e["kind"] for e in logger.errors] == ["heap-use-after-free", "SEGV"]
    assert len(logger.errors[0]["stack"]) == 2
    assert len(logger.errors[1]["stack"]) == 4


def test_foreign_report_lines_are_not_consumed():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    parser.log(f"==42==ERROR: AddressSanitizer: {SEGV_MESSAGE}")
    assert not parser.log("==43==The signal is caused by a READ memory access.")
    assert parser.log("==42==The signal is caused by a READ memory access.")
    parser.flush()

    assert len(logger.errors) == 1


def test_new_header_closes_the_previous_report():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    for line in SEGV_REPORT.splitlines()[:5]:
        parser.log(line)
    feed(parser, UAF_REPORT)

    assert [e["kind"] for e in logger.errors] == ["SEGV", "heap-use-after-free"]
    assert logger.errors[0]["stack"] is None


def test_attribute_names_the_open_reports():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    for line in SEGV_REPORT.splitlines()[:5]:
        parser.log(line)
    parser.attribute("/a.html")
    feed(parser, UAF_REPORT)

    assert [e["test"] for e in logger.errors] == ["/a.html", None]


def test_attribute_keeps_the_test_a_report_names():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    for line in UAF_REPORT.splitlines()[:3]:
        parser.log(line, test="/x.html")
    parser.attribute("/y.html")
    parser.flush()

    assert logger.errors[0]["test"] == "/x.html"


def test_lsan_report_is_ignored():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    assert not any(feed(parser, LSAN_REPORT))
    parser.flush()
    assert logger.errors == []

    # The leak summary neither closes an open report nor belongs to it.
    parser.log(f"==42==ERROR: AddressSanitizer: {SEGV_MESSAGE}")
    assert not parser.log(
        "SUMMARY: AddressSanitizer: 488 byte(s) leaked in 3 allocation(s)."
    )
    assert logger.errors == []
    assert parser.log("    #0 0x1 in main /src/a.cpp:1:1")
    parser.flush()
    assert len(logger.errors[0]["stack"]) == 1


def test_trailer_stops_at_unrelated_line():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    lines = UAF_REPORT.splitlines()
    summary = lines.index(next(l for l in lines if l.startswith("SUMMARY:")))
    for line in lines[: summary + 1]:
        parser.log(line)
    assert parser.log("Shadow bytes around the buggy address:")
    assert parser.log(
        "  0x60300000ed00: fa fa fd fd fd fd fa fa fd fd fd fd fa fa fd fd"
    )
    assert not parser.log("TEST-START | /dom/historical.html")
    assert not parser.log("  Addressable:           00")
    assert not parser.log("==12345==ABORTING")

    assert len(logger.errors) == 1


def test_discard():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    lines = SEGV_REPORT.splitlines()
    for line in lines[:12]:
        parser.log(line)

    assert not parser.discard((28130, "nope"))
    assert parser.discard(SEGV_KEY)
    # The rest of the report is still consumed, but nothing is emitted.
    assert all(parser.log(line) for line in lines[12:])
    assert logger.errors == []
    assert not parser.discard(SEGV_KEY)

    feed(parser, SEGV_REPORT)
    assert len(logger.errors) == 1


def test_discarded_report_is_not_flushed():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    for line in SEGV_REPORT.splitlines()[:5]:
        parser.log(line)
    assert parser.discard(SEGV_KEY)
    parser.flush()

    assert logger.errors == []


def test_key_of_matches_the_emitted_action():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    feed(parser, SEGV_REPORT)

    assert ASanErrorParser.key_of(logger.errors[0]) == SEGV_KEY


@pytest.mark.parametrize(
    ("summary", "location", "function"),
    [
        pytest.param(
            "SUMMARY: AddressSanitizer: SEGV (<unknown module>)",
            (None, None, None),
            None,
            id="unknown_module",
        ),
        pytest.param(
            "SUMMARY: AddressSanitizer: SEGV (/opt/firefox/firefox+0xc1f08) "
            "(BuildId: a89a059a4297241439326ea1c0f4913c1fb0a9d6) in _start",
            (None, None, None),
            "_start",
            id="module_with_build_id",
        ),
        pytest.param(
            "SUMMARY: AddressSanitizer: SEGV /src/a.cpp:12 in f::h0123456789abcdef",
            ("/src/a.cpp", 12, None),
            "f",
            id="rust_hash_stripped",
        ),
    ],
)
def test_summary_locations(summary, location, function):
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    parser.log(f"==42==ERROR: AddressSanitizer: {SEGV_MESSAGE}")
    assert parser.log(summary)
    assert parser.log("==42==ABORTING")

    assert len(logger.errors) == 1
    report = logger.errors[0]
    assert (report["file"], report["lineno"], report["column"]) == location
    assert report["function"] == function


def test_unrelated_output_is_ignored():
    logger = FakeLogger()
    parser = ASanErrorParser(logger)
    assert not parser.log("    #0 0x1 in main /src/a.c:1:1")
    assert not parser.log(
        "SUMMARY: AddressSanitizer: heap-use-after-free /src/a.c:1:1 in main"
    )
    assert not parser.log(
        "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /src/a.c:1:1"
    )
    assert not parser.log(
        "==42==WARNING: AddressSanitizer: failed to intercept 'crypt'"
    )
    assert not parser.log(
        "================================================================="
    )
    assert not parser.log("")
    parser.flush()

    assert logger.errors == []


if __name__ == "__main__":
    mozunit.main()
