# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import unittest
from pathlib import Path
from unittest.mock import MagicMock

from mach.logging import BUILD_ERROR, SUPPRESSED_WARNING, THIRD_PARTY_WARNING
from mozunit import main

from mozbuild.compilation.warnings import WarningsCollector
from mozbuild.controller.building import BuildOutputManager

STDOUT = "stdout"
STDERR = "stderr"

TOPSRCDIR = Path(__file__).resolve().parents[5].as_posix()

SUPPRESSED_WARNING_FLAGS = [
    "-Wno-error=nonportable-include-path",
]

# fmt: off
# (stream, expected_log_level, line_template)
# Note: {topsrcdir} is replaced with actual TOPSRCDIR at runtime
SAMPLE_LOG_BUILD_OUTPUT_TEMPLATES = [
    (STDOUT, logging.INFO,        "RecursiveMake backend executed in 5.99s"),
    (STDOUT, logging.INFO,        "  5829 total backend files; 5829 created; 0 updated;"),
    (STDOUT, logging.INFO,        "./mozilla-config.h.stub"),
    (STDOUT, logging.INFO,        "./buildid.h.stub"),
    (STDERR, THIRD_PARTY_WARNING, r'D:/.mozbuild/vs/Windows Kits/10/Include/10.0.26100.0/um/objidl.idl(67,10): warning: non-portable path to file "objidlbase.Idl"; specified path differs in case from file name on disk [-Wnonportable-include-path]'),
    (STDERR, THIRD_PARTY_WARNING, '   67 | #include "objidlbase.idl"'),
    (STDERR, THIRD_PARTY_WARNING, "      |          ^~~~~~~~~~~~~~~~"),
    (STDERR, THIRD_PARTY_WARNING, '      |          "objidlbase.Idl"'),
    (STDERR, BUILD_ERROR,         "1 error generated."),
    (STDOUT, logging.INFO,        "./registered_field_trials.h.stub"),
    (STDERR, SUPPRESSED_WARNING,  r'{topsrcdir}/first/party/path/objidl.idl(67,10): warning: non-portable path to file "objidlbase.Idl"; specified path differs in case from file name on disk [-Wnonportable-include-path]'),
    (STDERR, SUPPRESSED_WARNING,  '   67 | #include "objidlbase.idl"'),
    (STDERR, SUPPRESSED_WARNING,  "      |          ^~~~~~~~~~~~~~~~"),
    (STDERR, SUPPRESSED_WARNING,  '      |          "objidlbase.Idl"'),
    (STDERR, BUILD_ERROR,         "1 error generated."),
    (STDOUT, logging.INFO,        "./registered_field_trials.h.stub"),
    (STDERR, THIRD_PARTY_WARNING, "{topsrcdir}/nsprpub/pr/src/misc/prdtoa.c(1261,27): warning: operator '>>' has lower precedence than '-'; '-' will be evaluated first [-Wshift-op-parentheses]"),
    (STDERR, THIRD_PARTY_WARNING, " 1261 |     d1 = z << k | y >> 32 - k;"),
    (STDERR, THIRD_PARTY_WARNING, "      |                     ~~ ~~~^~~"),
    # Same warning as above, but with relative path {
    (STDERR, THIRD_PARTY_WARNING, "../../nsprpub/pr/src/misc/prdtoa.c(1261,27): warning: operator '>>' has lower precedence than '-'; '-' will be evaluated first [-Wshift-op-parentheses]"),
    (STDERR, THIRD_PARTY_WARNING, " 1261 |     d1 = z << k | y >> 32 - k;"),
    (STDERR, THIRD_PARTY_WARNING, "      |                     ~~ ~~~^~~"),
    # }
    (STDERR, BUILD_ERROR,         "1 error generated."),
    (STDOUT, logging.INFO,        "browser/components/about"),
    (STDOUT, logging.INFO,        "   Compiling syn v2.0.106"),
    (STDERR, logging.WARNING,     "22 warnings generated."),
    (STDOUT, logging.WARNING,     "warning: deprecated API"),
    (STDOUT, logging.INFO,        "   Compiling syn v2.0.106"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/browser/app/nsBrowserApp.cpp(6,1): error: unknown type name 'syntax'"),
    (STDERR, BUILD_ERROR,         "    6 | syntax error here for testing build output"),
    (STDERR, BUILD_ERROR,         "      | ^"),
    (STDOUT, logging.INFO,        "toolkit/library/build/xul.dll.def.stub"),
    (STDERR, THIRD_PARTY_WARNING, r"lld-link: warning: nss\lib\ssl\ssl_ssl\win32err.obj: locally defined symbol imported: PR_SetError (defined in ..\config\external\nspr\pr\Unified_c_external_nspr_pr2.obj) [LNK4217]"),
    (STDERR, BUILD_ERROR,         "Traceback (most recent call last):"),
    (STDERR, BUILD_ERROR,         '  File "{topsrcdir}/python/mozbuild/mozbuild/preprocessor.py", line 875, in do_include'),
    (STDERR, BUILD_ERROR,         '    args = open(args, encoding="utf-8")'),
    (STDERR, BUILD_ERROR,         "FileNotFoundError: [Errno 2] No such file or directory: 'missing-file.xhtml'"),
    (STDERR, BUILD_ERROR,         "During handling of the above exception, another exception occurred:"),
    (STDERR, BUILD_ERROR,         "Traceback (most recent call last):"),
    (STDERR, BUILD_ERROR,         "mozbuild.preprocessor.Preprocessor.Error: ('source.xhtml', 1, 'FILE_NOT_FOUND', 'missing-file.xhtml')"),
    (STDERR, BUILD_ERROR,         "error[E0308]: `match` arms have incompatible types"),
    (STDERR, BUILD_ERROR,         "   --> browser/extensions/felt/rust/src/components.rs:167:23"),
    (STDERR, BUILD_ERROR,         "     |"),
    (STDERR, BUILD_ERROR,         " 167 |               Err(_) => NS_ERROR_FAILURE,"),
    (STDERR, BUILD_ERROR,         "     |                         ^^^^^^^^^^^^^^^^ expected `()`, found `nsresult`"),
    (STDERR, BUILD_ERROR,         "error: could not compile `felt` (lib) due to 2 previous errors; 1 warning emitted"),
    (STDOUT, logging.INFO,        "   Compiling syn v2.0.106"),
    (STDOUT, logging.WARNING,     "warning: windows@0.62.2: ignoring 'hints.mostly-unused', pass `-Zprofile-hint-mostly-unused` to enable it"),
    (STDOUT, logging.WARNING,     "3 warnings:"),
    (STDOUT, logging.WARNING,     '  6010: install function "ExitProcess" not referenced - zeroing code (0-1) out'),
    (STDOUT, logging.WARNING,     '  6010: install function "createProfileCleanup" not referenced - zeroing code (798-855) out'),
    (STDOUT, logging.WARNING,     '  6012: label "OnError" not used'),
    (STDERR, logging.WARNING,     "592 compiler warnings present."),
    (STDERR, logging.WARNING,     "(suppressed 588 warnings in third-party code)"),
    (STDERR, logging.WARNING,     "(suppressed 2 warnings in ipc/chromium/src/base)"),
    (STDOUT, logging.WARNING,     "warning: error finalizing incremental compilation session directory "),
    (STDERR, logging.WARNING,     '{topsrcdir}/browser/app/nsBrowserApp.cpp(6,2): warning: "test warning for build output" [-W#warnings]'),
    (STDERR, logging.WARNING,     '    6 | #warning "test warning for build output"'),
    (STDERR, logging.WARNING,     "      |  ^"),
    (STDERR, logging.WARNING,     "1 warning generated"),
    (STDOUT, logging.INFO,        "toolkit/components/glean/EventGIFFTMap.cpp.stub"),
    (STDERR, BUILD_ERROR,         "2 error generated."),
    (STDERR, BUILD_ERROR,         "mozmake[4]: *** [{topsrcdir}/config/rules.mk:670: nsBrowserApp.obj] Error 1"),
    (STDERR, BUILD_ERROR,         "mozmake[3]: *** [{topsrcdir}/config/recurse.mk:72: browser/app/target-objects] Error 2"),
    (STDERR, BUILD_ERROR,         "mozmake[3]: *** Waiting for unfinished jobs...."),
    (STDOUT, logging.INFO,        "Finished `release` profile [optimized] target(s) in 6.42s"),
    (STDERR, BUILD_ERROR,         "mozmake[2]: *** [{topsrcdir}/config/recurse.mk:34: compile] Error 2"),
    (STDERR, BUILD_ERROR,         "mozmake[1]: *** [{topsrcdir}/config/rules.mk:359: default] Error 2"),
    (STDERR, BUILD_ERROR,         "mozmake: *** [client.mk:60: build] Error 2"),
    (STDOUT, logging.INFO,        "Finished `release` profile [optimized] target(s) in 6.42s"),
]
# fmt: on

# fmt: off
# (stream, expected_log_level, line_template)
# Note: {topsrcdir} is replaced with actual TOPSRCDIR at runtime
SAMPLE_GCC_BUILD_OUTPUT_TEMPLATES = [
    (STDOUT, logging.INFO,        "RecursiveMake backend executed in 5.99s"),
    (STDOUT, logging.INFO,        "./buildid.h.stub"),
    (STDOUT, logging.INFO,        "config/external/nspr/pr"),
    (STDERR, THIRD_PARTY_WARNING, "In file included from {topsrcdir}/nsprpub/pr/src/misc/prdtoa.c:307:"),
    (STDERR, THIRD_PARTY_WARNING, "{topsrcdir}/nsprpub/pr/src/misc/prdtoa.c:566:52: warning: comparison of integer expressions of different signedness: 'long int' and 'long unsigned int' [-Wsign-compare]"),
    (STDERR, THIRD_PARTY_WARNING, "  566 |     if (k <= Kmax && pmem_next - private_mem + len <= PRIVATE_mem) {{"),
    (STDERR, THIRD_PARTY_WARNING, "      |                                                    ^~"),
    # Same warning as above, but with relative paths {
    (STDERR, THIRD_PARTY_WARNING, "In file included from ./../../nsprpub/pr/src/misc/prdtoa.c:307:"),
    (STDERR, THIRD_PARTY_WARNING, "./../../nsprpub/pr/src/misc/prdtoa.c:566:52: warning: comparison of integer expressions of different signedness: 'long int' and 'long unsigned int' [-Wsign-compare]"),
    (STDERR, THIRD_PARTY_WARNING, "  566 |     if (k <= Kmax && pmem_next - private_mem + len <= PRIVATE_mem) {{"),
    (STDERR, THIRD_PARTY_WARNING, "      |                                                    ^~"),
    # }
    (STDOUT, logging.INFO,        "security/nss/lib/nss"),
    (STDERR, logging.WARNING,     "In file included from {topsrcdir}/objdir/dist/include/nss/seccomon.h:27,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from /usr/include/ctype.h:66,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from /usr/include/c++/v1/ctype.h:42,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from {topsrcdir}/security/nss/lib/nss/nssinit.c:8:"),
    (STDERR, THIRD_PARTY_WARNING, "/usr/include/sys/cdefs.h:900:9: note: this is the location of the previous definition"),
    (STDERR, THIRD_PARTY_WARNING, "  900 | #define __STDC_WANT_LIB_EXT1__ 1"),
    (STDERR, THIRD_PARTY_WARNING, "      |         ^~~~~~~~~~~~~~~~~~~~~~"),
    (STDOUT, logging.INFO,        "mozglue/misc"),
    (STDERR, logging.WARNING,     "{topsrcdir}/layout/mathml/nsMathMLChar.cpp: In lambda function:"),
    (STDERR, logging.WARNING,     "{topsrcdir}/layout/mathml/nsMathMLChar.cpp:677:19: warning: possibly dangling reference to a temporary [-Wdangling-reference]"),
    (STDERR, logging.WARNING,     "  677 |       const auto& firstFontInList = familyList.list.AsSpan()[0];"),
    (STDERR, logging.WARNING,     "      |                   ^~~~~~~~~~~~~~~"),
    (STDOUT, logging.INFO,        "mozglue/misc2"),
    (STDERR, logging.WARNING,     "{topsrcdir}/layout/tables/nsCellMap.h: At global scope:"),
    (STDOUT, logging.INFO,        "mozglue/misc3"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/layout/tables/nsCellMap.h:482: error: extra qualification 'nsCellMapColumnIterator::' on member 'invalid' [-fpermissive]"),
    (STDERR, BUILD_ERROR,         "  482 |   int nsCellMapColumnIterator::invalid(void);"),
    (STDERR, BUILD_ERROR,         "      |       ^~~~~~~~~~~~~~~~~~~~~~~"),
    (STDOUT, logging.INFO,        "browser/app/firefox"),
    (STDERR, logging.WARNING,     "In file included from {topsrcdir}/layout/tables/nsTableFrame.h:10,"),
    (STDERR, logging.WARNING,     "                 from {topsrcdir}/layout/tables/nsTableRowGroupFrame.h:13,"),
    (STDERR, logging.WARNING,     "                 from {topsrcdir}/layout/tables/nsTableRowFrame.h:10,"),
    (STDERR, logging.WARNING,     "                 from {topsrcdir}/layout/tables/nsTableCellFrame.h:15,"),
    (STDERR, logging.WARNING,     "                 from {topsrcdir}/layout/tables/BasicTableLayoutStrategy.cpp:20,"),
    (STDERR, logging.WARNING,     "                 from Unified_cpp_layout_tables0.cpp:2:"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/layout/tables/nsCellMap.h:482:7: error: extra qualification 'nsCellMapColumnIterator::' on member 'invalid' [-fpermissive]"),
    (STDERR, BUILD_ERROR,         "  482 |   int nsCellMapColumnIterator::invalid(void);"),
    (STDERR, BUILD_ERROR,         "      |       ^~~~~~~~~~~~~~~~~~~~~~~"),
    (STDOUT, logging.INFO,        "toolkit/library"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/nsprpub/pr/src/misc/prdtoa.c:400:26: error: implicit declaration of function '__builtin_flt_rounds' [-Wimplicit-function-declaration]"),
    (STDERR, BUILD_ERROR,         "  400 | #      define Flt_Rounds FLT_ROUNDS"),
    (STDERR, BUILD_ERROR,         "      |                          ^~~~~~~~~~"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/nsprpub/pr/src/misc/prdtoa.c:1855:10: note: in expansion of macro 'Flt_Rounds'"),
    (STDERR, BUILD_ERROR,         " 1855 |       && Flt_Rounds == 1"),
    (STDERR, BUILD_ERROR,         "      |          ^~~~~~~~~~"),
    (STDERR, BUILD_ERROR,         "make[4]: *** [Unified_c_external_nspr_pr1.o] Error 1"),
    (STDERR, BUILD_ERROR,         "make[3]: *** [config/external/nspr/pr/target-objects] Error 2"),
    (STDERR, BUILD_ERROR,         "make[3]: *** Waiting for unfinished jobs...."),
    (STDOUT, logging.INFO,        "   Compiling syn v2.0.106"),
    (STDERR, logging.WARNING,     "{topsrcdir}/browser/app/nsBrowserApp.cpp:6:2: warning: #warning \"test warning\" [-Wcpp]"),
    (STDERR, logging.WARNING,     '    6 | #warning "test warning"'),
    (STDERR, logging.WARNING,     "      |  ^~~~~~~"),
    (STDERR, logging.WARNING,     "1 warning generated."),
    (STDOUT, logging.INFO,        "toolkit/components/glean/EventGIFFTMap.cpp.stub"),
    (STDERR, BUILD_ERROR,         "{topsrcdir}/browser/app/nsBrowserApp.cpp:10:1: error: expected ';' after class definition"),
    (STDERR, BUILD_ERROR,         "   10 | }}"),
    (STDERR, BUILD_ERROR,         "      | ^"),
    (STDERR, BUILD_ERROR,         "      | ;"),
    (STDERR, BUILD_ERROR,         "1 error generated."),
    (STDERR, BUILD_ERROR,         "make[4]: *** [nsBrowserApp.obj] Error 1"),
    (STDOUT, logging.INFO,        "Finished `release` profile [optimized] target(s) in 6.42s"),
    (STDERR, SUPPRESSED_WARNING,  r'{topsrcdir}/first/party/path/file.cpp:67:10: warning: non-portable path to file "header.h" [-Wnonportable-include-path]'),
    (STDERR, SUPPRESSED_WARNING,  '   67 | #include "Header.h"'),
    (STDERR, SUPPRESSED_WARNING,  "      |          ^~~~~~~~~~"),
    (STDERR, logging.WARNING,     "1 warning generated."),
    (STDOUT, logging.INFO,        "./registered_field_trials.h.stub"),
    (STDERR, THIRD_PARTY_WARNING, "cc1: warning: command-line option '-fvisibility-inlines-hidden' is valid for C++/ObjC++ but not for C"),
    (STDOUT, logging.INFO,        "browser/components/about"),
    (STDERR, THIRD_PARTY_WARNING, "In file included from /usr/include/c++/v1/utility:257,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from {topsrcdir}/objdir/dist/include/mozilla/MaybeStorageBase.h:14,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from {topsrcdir}/memory/build/Mutex.h:21:"),
    (STDOUT, logging.INFO,        "toolkit/components/glean/EventGIFFTMap.cpp.stub"),
    (STDERR, BUILD_ERROR,         "/usr/include/c++/v1/__utility/pair.h:536:1: error: use of built-in trait '__decay(_Tp)' in function signature"),
    (STDERR, BUILD_ERROR,         "  536 | make_pair(_T1&& __t1, _T2&& __t2) {{"),
    (STDERR, BUILD_ERROR,         "      | ^~~~~~~~~"),
    (STDERR, BUILD_ERROR,         "make[4]: *** [Unified_cpp_memory_build0.o] Error 1"),
    (STDOUT, logging.INFO,        "   Compiling uniffi_bindgen v0.29.3"),
    (STDERR, logging.WARNING,     "592 compiler warnings present."),
    (STDERR, logging.WARNING,     "(suppressed 588 warnings in third-party code)"),
    (STDERR, THIRD_PARTY_WARNING, "In file included from \x1b[01m\x1b[K/usr/include/features.h:3\x1b[m\x1b[K,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from \x1b[01m\x1b[K/usr/include/c++/14.2/bits/os_defines.h:39\x1b[m\x1b[K,"),
    (STDERR, THIRD_PARTY_WARNING, "                 from \x1b[01m\x1b[KUnified_cpp_layout_tables0.cpp:2\x1b[m\x1b[K:"),
    (STDERR, THIRD_PARTY_WARNING, "\x1b[01m\x1b[K/usr/include/features.h:435:4:\x1b[m\x1b[K \x1b[01;35m\x1b[Kwarning: \x1b[m\x1b[K#warning message [\x1b[01;35m\x1b[K-Wcpp\x1b[m\x1b[K]"),
    (STDERR, THIRD_PARTY_WARNING, "  435 | #  \x1b[01;35m\x1b[Kwarning\x1b[m\x1b[K message here"),
    (STDERR, THIRD_PARTY_WARNING, "      |    \x1b[01;35m\x1b[K^~~~~~~\x1b[m\x1b[K"),
    (STDOUT, logging.INFO,        "media/libvpx"),
    (STDERR, BUILD_ERROR,         "\x1b[01m\x1b[K{topsrcdir}/layout/tables/nsCellMap.h:482:\x1b[m\x1b[K \x1b[01;31m\x1b[Kerror: \x1b[m\x1b[Kextra qualification"),
    (STDERR, BUILD_ERROR,         "  482 |   int nsCellMapColumnIterator::invalid(void);"),
]
# fmt: on


def get_sample_log_build_output():
    return [
        (stream, level, line.format(topsrcdir=TOPSRCDIR))
        for stream, level, line in SAMPLE_LOG_BUILD_OUTPUT_TEMPLATES
    ]


def get_sample_gcc_build_output():
    return [
        (stream, level, line.format(topsrcdir=TOPSRCDIR))
        for stream, level, line in SAMPLE_GCC_BUILD_OUTPUT_TEMPLATES
    ]


class MockBuildMonitor:
    def __init__(self):
        self.topsrcdir = TOPSRCDIR
        self.substs = {
            "WARNINGS_CFLAGS": SUPPRESSED_WARNING_FLAGS,
            "WARNINGS_CXXFLAGS": SUPPRESSED_WARNING_FLAGS,
        }
        self._warnings_collector = WarningsCollector(cb=lambda w: None)

    def on_line(self, line):
        warning = self._warnings_collector.process_line(line)
        return warning, False, line


class BuildOutputTestHarness:
    def __init__(self):
        self.captured_log_level = None
        self._setup_manager()

    def _setup_manager(self):
        mock_log_manager = MagicMock()
        mock_log_manager.terminal = None
        self.mock_monitor = MockBuildMonitor()

        self.manager = BuildOutputManager(
            mock_log_manager, self.mock_monitor, footer=None
        )

        def capture_log(level, *args, **kwargs):
            self.captured_log_level = level

        self.manager.log = capture_log
        self.manager.log_record = MagicMock()

    def process_line(self, line, stream):
        self.captured_log_level = None
        if stream == STDOUT:
            self.manager.on_stdout_line(line)
        else:
            self.manager.on_stderr_line(line)
        return self.captured_log_level


class TestBuildOutputLogLevel(unittest.TestCase):
    def test_build_output_log_levels(self):
        test_harness = BuildOutputTestHarness()

        for stream, expected, line in get_sample_log_build_output():
            with self.subTest(line=line, stream=stream):
                log_level = test_harness.process_line(line, stream)
                self.assertEqual(
                    log_level,
                    expected,
                    f"Line '{line}' ({stream}) expected {expected}, got {log_level}",
                )

    def test_gcc_build_output_log_levels(self):
        test_harness = BuildOutputTestHarness()

        for stream, expected, line in get_sample_gcc_build_output():
            with self.subTest(line=line, stream=stream):
                log_level = test_harness.process_line(line, stream)
                self.assertEqual(
                    log_level,
                    expected,
                    f"Line '{line}' ({stream}) expected {expected}, got {log_level}",
                )


if __name__ == "__main__":
    main()
