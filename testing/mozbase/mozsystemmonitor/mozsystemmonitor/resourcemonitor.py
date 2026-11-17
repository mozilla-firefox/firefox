# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import posixpath
import re
import time
from collections import deque

from mozsystemmonitor.basemonitor import (
    FORWARD_ENV,  # noqa: F401
    BaseResourceMonitor,
    SystemResourceUsage,  # noqa: F401
    have_psutil,  # noqa: F401
)

# Common prefix in log lines from a Gecko process: "[Child|Parent <pid>: <thread>]"
# (DocShell-style logging) and "[Child|Parent <pid>, <thread>]" (NS_WARNING/ASSERTION).
_PROC_PREFIX_COLON = r"\[(?P<proc>Child|Parent) (?P<pid>\d+): (?P<thread>[^\]]+)\]"
_PROC_PREFIX_COMMA = r"\[(?P<proc>Child|Parent) (?P<pid>\d+), (?P<thread>[^\]]+)\]"

# ++DOCSHELL/--DOCSHELL leak log lines (nsDocShell.cpp), e.g.:
#   [Child 4208: Main Thread]: I/DocShellAndDOMWindowLeak ++DOCSHELL 2f804b00 == 2 [pid = 4208] [id = 37]
#   [Child 4208: Main Thread]: I/DocShellAndDOMWindowLeak --DOCSHELL 2f804b00 == 0 [pid = 4208] [id = 37] [url = about:aichatcontent]
_DOCSHELL_RE = re.compile(
    _PROC_PREFIX_COLON + r": [A-Z]/DocShellAndDOMWindowLeak (?P<op>\+\+|--)DOCSHELL "
    r"(?P<ptr>[0-9a-fA-F]+) == \d+ \[pid = \d+\] \[id = (?P<id>\d+)\]"
    r"(?: \[url = (?P<url>[^\]]*)\])?\s*$"
)

# ++DOMWINDOW/--DOMWINDOW leak log lines (nsGlobalWindow{Inner,Outer}.cpp), e.g.:
#   [Child 3444: Main Thread]: I/DocShellAndDOMWindowLeak ++DOMWINDOW == 2 (b3bc400) [pid = 3444] [serial = 2] [outer = 4f60940]
#   [Child 3444: Main Thread]: I/DocShellAndDOMWindowLeak --DOMWINDOW == 1 (b3bc400) [pid = 3444] [serial = 2] [outer = 4f60940] [url = about:blank]
_DOMWINDOW_RE = re.compile(
    _PROC_PREFIX_COLON + r": [A-Z]/DocShellAndDOMWindowLeak (?P<op>\+\+|--)DOMWINDOW "
    r"== \d+ \((?P<ptr>[0-9a-fA-F]+)\) \[pid = \d+\] "
    r"\[serial = (?P<serial>\d+)\] \[outer = (?P<outer>[0-9a-fA-F]+)\]"
    r"(?: \[url = (?P<url>[^\]]*)\])?\s*$"
)

# "JavaScript error:" / "JavaScript warning:" lines, e.g.:
#   JavaScript error: chrome://browser/content/places/browserPlacesViews.js, line 118: Error: No DOM node set for aPlacesNode.
_JS_ERROR_RE = re.compile(
    r"^JavaScript (?P<level>error|warning): (?P<file>[^,]*), line (?P<line>\d+): "
    r"(?P<message>.*)$"
)

# NS_WARNING / ###!!! ASSERTION lines (nsDebugImpl::FormatMsg).
# The format string is built from three optional pieces:
#   "<sev>: " [+ "<str>: "] [+ "'<expr>', "] + "file <file>:<line>"
# so "message" can be "str", "'expr'", or "str: 'expr'" depending on which
# arguments NS_DebugBreak got. We capture the whole thing as "message" and
# leave it to the front-end to render verbatim.
_WARNING_RE = re.compile(
    _PROC_PREFIX_COMMA
    + r" WARNING: (?P<message>.*?)(?:, |: )file (?P<file>[^:]+):(?P<line>\d+)\s*$"
)

_ASSERTION_RE = re.compile(
    r"\x07?"
    + _PROC_PREFIX_COMMA
    + r" ###!!! ASSERTION: (?P<message>.*?), file (?P<file>[^:]+):(?P<line>\d+)\s*$"
)

# console.<method>: ... lines, e.g.:
#   console.error: (new Error("Unable to retrieve the translation models.", "resource://...", 2674))
#   console.warn: "No view for invalid view, switching to default"
#   console.log: Downloads: Closing the downloads panel.
# The single-line shape comes from Console.cpp and from createDumper() in
# Console.sys.mjs. Console.sys.mjs's createMultiLineDumper() (used for debug,
# error, dir, dirxml; console.exception is also routed through it but emits
# with level "error", so it appears as "console.error:" in the output)
# instead emits a header with no real body followed by indented
# "Message:"/"Stack:" lines and JS-style frames. Header shapes seen in CI:
#   console.error:                       (no prefix, no body)
#   console.error: services.settings:    (prefix "services.settings", no body)
# The message group is optional and may end with ":" when a prefix is present.
_CONSOLE_RE = re.compile(r"^console\.(?P<method>[a-zA-Z]+):(?: (?P<message>.*))?$")

# Methods routed through Console.sys.mjs's createMultiLineDumper(): when one
# of these emits a header with no real body, the following process_output
# lines are the multi-line body ("  Message:", "  Stack:", and frames).
_MULTILINE_CONSOLE_METHODS = frozenset({"debug", "error", "dir", "dirxml"})

# Body lines for the multi-line console.* dumper.
_CONSOLE_MESSAGE_RE = re.compile(r"^  Message: (?P<message>.*)$")
_CONSOLE_STACK_HEADER_RE = re.compile(r"^  Stack:\s*$")
# JS stack frame: "<func>@<file>:<line>:<col>". The function name may be empty
# (top-level/async frames) or contain '/' and '<' (anonymous nested closures).
# Leading whitespace varies: log() prefixes the first frame with 4 spaces, but
# subsequent frames embedded in the same Error.stack string come unindented.
_CONSOLE_JS_FRAME_RE = re.compile(
    r"^\s*(?P<func>[^@]*)@(?P<file>.+):(?P<line>\d+):(?P<col>\d+)\s*$"
)

# Stack frames that follow console.trace, formatted by Console.cpp as
# "<filename> <line> <funcname>" per frame (one process_output line each).
_CONSOLE_FRAME_RE = re.compile(r"^(?P<file>\S+/\S+) (?P<line>\d+) (?P<func>\S.*)$")

# CI builds embed paths like
# "/builds/worker/workspace/obj-build/.../checkouts/gecko/<repo-relative>".
# Strip the build prefix so frames carry the repo-relative path expected by
# the profiler source view.
_CHECKOUTS_GECKO = "checkouts/gecko/"

# Matches the sourceURL mozharness writes into the profile metadata, e.g.
# "https://hg.mozilla.org/try/rev/56b3cc68b5e7557a3e13fca984f0f8aebc60dd22".
# The non-greedy <repo> group lets us match multi-segment repo paths like
# "integration/autoland" and "releases/mozilla-beta".
_HG_SOURCE_URL_RE = re.compile(
    r"^https?://(?P<host>hg\.mozilla\.org)/(?P<repo>.+?)/rev/(?P<rev>[0-9a-f]+)$"
)


# Marker schemas for the markers built from logs and test output.
_MARKER_SCHEMAS = [
    {
        "name": "Test",
        "tooltipLabel": "{marker.data.name}",
        "tableLabel": "{marker.data.status} — {marker.data.test}",
        "chartLabel": "{marker.data.name}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "test",
                "label": "Test Name",
                "format": "string",
            },
            {
                "key": "name",
                "label": "Short Name",
                "format": "string",
                "hidden": True,
            },
            {
                "key": "subsuite",
                "label": "Subsuite",
                "format": "string",
            },
            {
                "key": "status",
                "label": "Status",
                "format": "string",
            },
            {
                "key": "expected",
                "label": "Expected",
                "format": "string",
            },
            {
                "key": "unexpected_subtests",
                "label": "Unexpected subtests",
                "format": "integer",
            },
            {
                "key": "message",
                "label": "Message",
                "format": "string",
            },
            {
                "key": "timeoutfactor",
                "label": "Timeout Factor",
                "format": "integer",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "TestStatus",
        "tableLabel": "{marker.data.message} — {marker.data.test} {marker.data.subtest}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "message",
                "label": "Message",
                "format": "string",
            },
            {
                "key": "test",
                "label": "Test Name",
                "format": "string",
            },
            {
                "key": "subtest",
                "label": "Subtest",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "Crash",
        "tableLabel": "{marker.data.signature} — {marker.data.test}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "signature",
                "label": "Signature",
                "format": "string",
            },
            {
                "key": "reason",
                "label": "Reason",
                "format": "string",
            },
            {
                "key": "test",
                "label": "Test Name",
                "format": "string",
            },
            {
                "key": "minidump",
                "label": "Minidump",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "LSanLeak",
        "tooltipLabel": "{marker.data.kind} leak of {marker.data.bytes} in {marker.data.objects} object(s)",
        "tableLabel": "{marker.data.kind} leak of {marker.data.bytes} in {marker.data.objects} object(s) — {marker.data.scope}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "kind",
                "label": "Kind",
                "format": "string",
            },
            {
                "key": "bytes",
                "label": "Bytes",
                "format": "bytes",
            },
            {
                "key": "objects",
                "label": "Objects",
                "format": "integer",
            },
            {
                "key": "scope",
                "label": "Scope",
                "format": "string",
            },
            {
                "key": "allowed_match",
                "label": "Allowed Match",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "LSanSummary",
        "tooltipLabel": "{marker.data.bytes} in {marker.data.allocations} allocation(s)",
        "tableLabel": "{marker.data.bytes} in {marker.data.allocations} allocation(s)",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "bytes",
                "label": "Bytes",
                "format": "bytes",
            },
            {
                "key": "allocations",
                "label": "Allocations",
                "format": "integer",
            },
            {
                "key": "allowed",
                "label": "Allowed",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "TSanError",
        "tooltipLabel": "{marker.data.kind} (report {marker.data.report_index}) — {marker.data.label}",
        "tableLabel": "{marker.data.kind} (report {marker.data.report_index}) — {marker.data.label}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "kind",
                "label": "Kind",
                "format": "string",
            },
            {
                "key": "report_index",
                "label": "Report",
                "format": "integer",
            },
            {
                "key": "pid",
                "label": "PID",
                "format": "integer",
            },
            {
                "key": "scope",
                "label": "Scope",
                "format": "string",
            },
            {
                "key": "description",
                "label": "Description",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
            # Declared last so it renders directly above the stack.
            {
                "key": "label",
                "label": "Stack",
                "format": "string",
            },
        ],
    },
    {
        "name": "AssertionFailure",
        "tooltipLabel": "{marker.data.kind}: {marker.data.message}",
        "tableLabel": "{marker.data.kind}: {marker.data.message} — {marker.data.file}:{marker.data.line}",
        "chartLabel": "{marker.data.message}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {"key": "kind", "label": "Kind", "format": "string"},
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "file", "format": "string", "hidden": True},
            {"key": "line", "format": "integer", "hidden": True},
            {"key": "fatal", "label": "Fatal", "format": "string"},
            {"key": "process", "label": "Process", "format": "string"},
            {"key": "pid", "label": "Process ID", "format": "integer"},
            {"key": "thread", "label": "Thread", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
            {"key": "color", "hidden": True},
        ],
    },
    {
        "name": "UBSanError",
        "tooltipLabel": "{marker.data.message}",
        "tableLabel": "{marker.data.message} — {marker.data.file}:{marker.data.line}",
        "chartLabel": "{marker.data.message}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {"key": "kind", "label": "Kind", "format": "string"},
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "file", "format": "string", "hidden": True},
            {"key": "line", "format": "integer", "hidden": True},
            {"key": "column", "format": "integer", "hidden": True},
            {"key": "scope", "label": "Scope", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
            {"key": "color", "hidden": True},
        ],
    },
    {
        "name": "ASanError",
        "tooltipLabel": "{marker.data.message}",
        "tableLabel": "{marker.data.kind} — {marker.data.location}",
        "chartLabel": "{marker.data.kind}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {"key": "kind", "label": "Kind", "format": "string"},
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "file", "format": "string", "hidden": True},
            {"key": "line", "format": "integer", "hidden": True},
            {"key": "column", "format": "integer", "hidden": True},
            {"key": "location", "format": "string", "hidden": True},
            {"key": "function", "label": "Function", "format": "string"},
            {"key": "pid", "label": "Process ID", "format": "integer"},
            {"key": "scope", "label": "Scope", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
            {"key": "color", "hidden": True},
        ],
    },
    {
        "name": "MozLeakObject",
        "tooltipLabel": "{marker.data.bytes_leaked} in {marker.data.count} {marker.data.name}",
        "tableLabel": "{marker.data.process} leaked {marker.data.bytes_leaked} in {marker.data.count} {marker.data.name}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "name",
                "label": "Object",
                "format": "string",
            },
            {
                "key": "count",
                "label": "Instances Leaked",
                "format": "integer",
            },
            {
                "key": "bytes_leaked",
                "label": "Bytes Leaked",
                "format": "bytes",
            },
            {
                "key": "bytes_per_inst",
                "label": "Bytes Per Instance",
                "format": "bytes",
            },
            {
                "key": "total_instances",
                "label": "Total Instances",
                "format": "integer",
            },
            {
                "key": "allowed",
                "label": "Allowed",
                "format": "string",
            },
            {
                "key": "process",
                "label": "Process",
                "format": "string",
            },
            {
                "key": "scope",
                "label": "Scope",
                "format": "string",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "MozLeakTotal",
        "tableLabel": "{marker.data.process} — {marker.data.bytes} leaked",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {
                "key": "process",
                "label": "Process",
                "format": "string",
            },
            {
                "key": "bytes",
                "label": "Bytes",
                "format": "bytes",
            },
            {
                "key": "scope",
                "label": "Scope",
                "format": "string",
            },
            {
                "key": "induced_crash",
                "label": "Induced Crash",
                "format": "string",
            },
            {
                "key": "ignore_missing",
                "label": "Ignore Missing",
                "format": "string",
            },
            {
                "key": "objects",
                "label": "Leaked Objects",
                "format": "list",
            },
            {
                "key": "color",
                "hidden": True,
            },
        ],
    },
    {
        "name": "DocShell",
        "tooltipLabel": "{marker.data.url}",
        "tableLabel": "DOCSHELL {marker.data.pointer} [{marker.data.process} {marker.data.pid}: {marker.data.thread}] id = {marker.data.id} {marker.data.url}",
        "chartLabel": "{marker.data.url}",
        "display": ["marker-chart", "marker-table"],
        "data": [
            {"key": "url", "label": "URL", "format": "url"},
            {"key": "id", "label": "ID", "format": "integer"},
            {"key": "pointer", "label": "Address", "format": "string"},
            {"key": "process", "label": "Process", "format": "string"},
            {"key": "pid", "label": "Process ID", "format": "integer"},
            {"key": "thread", "label": "Thread", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
        ],
    },
    {
        "name": "DOMWindow",
        "tooltipLabel": "{marker.data.url}",
        "tableLabel": "DOMWINDOW {marker.data.pointer} [{marker.data.process} {marker.data.pid}: {marker.data.thread}] serial = {marker.data.serial} outer = {marker.data.outer} {marker.data.url}",
        "chartLabel": "{marker.data.url}",
        "display": ["marker-chart", "marker-table"],
        "data": [
            {"key": "url", "label": "URL", "format": "url"},
            {"key": "serial", "label": "Serial", "format": "integer"},
            {"key": "pointer", "label": "Address", "format": "string"},
            {"key": "outer", "label": "Outer", "format": "string"},
            {"key": "process", "label": "Process", "format": "string"},
            {"key": "pid", "label": "Process ID", "format": "integer"},
            {"key": "thread", "label": "Thread", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
        ],
    },
    {
        "name": "jsError",
        "tooltipLabel": "{marker.data.message}",
        "tableLabel": "{marker.data.message} — {marker.data.file}:{marker.data.line}",
        "chartLabel": "{marker.data.message}",
        "display": ["marker-chart", "marker-table"],
        "data": [
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "file", "format": "string", "hidden": True},
            {"key": "line", "format": "integer", "hidden": True},
            {"key": "test", "label": "Test", "format": "string"},
        ],
    },
    {
        "name": "cppDebug",
        "tooltipLabel": "{marker.data.message}",
        "tableLabel": "{marker.data.message} — {marker.data.file}:{marker.data.line}",
        "chartLabel": "{marker.data.message}",
        "display": ["marker-chart", "marker-table"],
        "colorField": "color",
        "data": [
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "file", "format": "string", "hidden": True},
            {"key": "line", "format": "integer", "hidden": True},
            {"key": "process", "label": "Process", "format": "string"},
            {"key": "pid", "label": "Process ID", "format": "integer"},
            {"key": "thread", "label": "Thread", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
            {"key": "color", "hidden": True},
        ],
    },
    {
        "name": "console",
        "tooltipLabel": "{marker.data.message}",
        "tableLabel": "{marker.data.message}",
        "chartLabel": "{marker.data.message}",
        "display": ["marker-chart", "marker-table"],
        "data": [
            {"key": "message", "label": "Message", "format": "string"},
            {"key": "test", "label": "Test", "format": "string"},
        ],
    },
]


def _parse_hg_source_url(source_url):
    """Return (prefix, rev) for an hg.mozilla.org sourceURL, or (None, None).

    The returned prefix combines with a repo-relative path and the rev to form
    the "hg:<host>/<repo>:<path>:<rev>" shape the profiler source view fetches.
    """
    if not source_url:
        return (None, None)
    m = _HG_SOURCE_URL_RE.match(source_url)
    if not m:
        return (None, None)
    return (f"hg:{m['host']}/{m['repo']}:", m["rev"])


class _AssertionFallbackLogger:
    """Logger-like sink for the line-based AssertionFailureParser: the
    actions it would emit become markers instead."""

    def __init__(self, monitor):
        self._monitor = monitor

    def assertion_failure(self, **data):
        self._monitor._record_line_assertion(data)


def _ubsan_key(data):
    """Identity of a UBSan report, shared by the ubsan_error action and the
    line-based fallback so the two can be matched."""
    return (data.get("file"), data.get("lineno"), data.get("column"), data["message"])


class _UBSanFallbackLogger:
    """Logger-like sink for the line-based UBSanErrorParser: the actions it
    would emit become markers instead."""

    def __init__(self, monitor):
        self._monitor = monitor

    def ubsan_error(self, **data):
        self._monitor._record_line_ubsan(data)


def _asan_key(data):
    """Identity of an ASan report, shared by the asan_error action and the
    line-based fallback so the two can be matched."""
    return (data.get("pid"), data["message"])


class _ASanFallbackLogger:
    """Logger-like sink for the line-based ASanErrorParser: the actions it
    would emit become markers instead."""

    def __init__(self, monitor):
        self._monitor = monitor

    def asan_error(self, **data):
        self._monitor._record_line_asan(data)


def _test_id_str(test):
    if isinstance(test, (list, tuple)):
        return " ".join(test)
    return test


class SystemResourceMonitor(BaseResourceMonitor):
    """A BaseResourceMonitor that also turns structured log actions and test
    output into markers."""

    marker_schemas = {schema["name"]: schema for schema in _MARKER_SCHEMAS}

    def __init__(self, poll_interval=1.0, metadata=None, forward_to=None):
        super().__init__(poll_interval, metadata, forward_to)

        self._active_tests = {}

        # Counter used to correlate the several markers emitted for one TSan
        # report (one marker per labeled stack).
        self._tsan_report_count = 0
        # ++DOCSHELL/++DOMWINDOW lines awaiting their matching -- line.
        # Keyed by (kind, pid, ptr, id|serial); value is (kind, start_time, marker_data).
        self._leaked_instances = {}
        # console.trace: line waiting for follow-up stack frames; flushed when
        # a non-frame process_output line arrives or the monitor stops.
        self._pending_console_trace = None
        # Line-based fallback for harnesses that do not emit assertion_failure
        # actions: assembles assertion headers and their stack frames out of
        # process_output lines. An assertion_failure action for the same
        # assertion supersedes the pending line-based report.
        self._assertion_parser = None
        try:
            from moztest.assertions import AssertionFailureParser
        except ImportError:
            pass
        else:
            self._assertion_parser = AssertionFailureParser(
                _AssertionFallbackLogger(self)
            )
        # (message, file, lineno) of assertion_failure actions that found no
        # pending line-based report, so that one completing later is dropped.
        self._recent_assertion_actions = deque(maxlen=32)
        # Line-based fallback for harnesses that do not emit ubsan_error
        # actions: assembles UBSan reports out of process_output lines. A
        # ubsan_error action for the same report supersedes the pending
        # line-based one.
        self._ubsan_parser = None
        try:
            from moztest.ubsan import UBSanErrorParser
        except ImportError:
            pass
        else:
            self._ubsan_parser = UBSanErrorParser(_UBSanFallbackLogger(self))
        # Keys of ubsan_error actions that found no pending line-based
        # report, so that one completing later is dropped.
        self._recent_ubsan_actions = deque(maxlen=32)
        # Line-based fallback for harnesses that do not emit asan_error
        # actions: assembles AddressSanitizer reports out of process_output
        # lines. An asan_error action for the same report supersedes the
        # pending line-based one.
        self._asan_parser = None
        try:
            from mozleak.asan import ASanErrorParser
        except ImportError:
            pass
        else:
            self._asan_parser = ASanErrorParser(_ASanFallbackLogger(self))
        # Keys of asan_error actions that found no pending line-based
        # report, so that one completing later is dropped.
        self._recent_asan_actions = deque(maxlen=32)
        # Multi-line console.<method>: body (Message:/Stack:/frames) waiting
        # to be assembled into a single marker. Tuple of
        # (name, timestamp, marker_data, phase, prefix_body) where phase
        # progresses through "await_message" -> "await_stack" -> "frames",
        # or starts at "speculative" when the header carried a body that
        # ended in ":" and could be either a prefix-only multi-line header
        # ("console.error: services.settings:") or a real single-line message.
        # prefix_body is the original body text for the speculative case so
        # we can either stitch the prefix back in front of the Message: text
        # or fall back to a single-line marker. Flushed when a line breaks
        # the expected sequence or the monitor stops.
        self._pending_multiline_console = None
        # "hg:<host>/<repo>:" prefix and revision parsed once from the profile
        # metadata's sourceURL, used to wrap repo-relative frame paths into
        # URLs the profiler source view can fetch.
        self._frame_file_prefix, self._frame_file_rev = _parse_hg_source_url(
            self.metadata.get("sourceURL")
        )

    def _flush_pending(self):
        self._flush_leak_logs()
        self._flush_pending_console_trace()
        self._flush_pending_multiline_console()
        if self._assertion_parser is not None:
            self._assertion_parser.flush()
        if self._ubsan_parser is not None:
            self._ubsan_parser.flush()
        if self._asan_parser is not None:
            self._asan_parser.flush()

    def _clean_frame_file(self, path):
        """Return (display_path, source_view_path).

        display_path is the build-prefix-stripped path for tooltip/marker display.
        source_view_path is an "hg:<host>/<repo>:<path>:<rev>" URL the profiler
        source view can fetch when the path was repo-relative (i.e. contained the
        CI "checkouts/gecko/" marker) and this monitor knows the hg prefix from
        the profile metadata; otherwise it equals display_path.
        Sysroot, fetches and rust-stdlib paths fall through unchanged in both
        fields so the source view doesn't 404 trying to fetch them from hg.
        """
        if not path:
            return (path, path)
        idx = path.rfind(_CHECKOUTS_GECKO)
        if idx == -1:
            return (path, path)
        # The path has already been narrowed to a CI build path (POSIX) by
        # the rfind above; use posixpath.normpath rather than os.path.normpath
        # so we don't produce backslash separators on Windows.
        cleaned = posixpath.normpath(path[idx + len(_CHECKOUTS_GECKO) :])
        if not self._frame_file_prefix:
            return (cleaned, cleaned)
        return (
            cleaned,
            f"{self._frame_file_prefix}{cleaned}:{self._frame_file_rev}",
        )

    def _parse_process_output(self, line, timestamp, test_name, process=None):
        """Parse a single process_output line and emit a typed marker if it matches a known pattern.

        `process` identifies the output stream the line came from, so that
        multi-line reports from interleaved streams are kept apart.

        Returns True if the line produced a specialized marker, False otherwise.
        """
        line = line.rstrip("\r\n")

        # If we're collecting frames for a previous console.trace:, attach this
        # line if it looks like a frame, otherwise flush and fall through.
        if self._pending_console_trace is not None:
            if m := _CONSOLE_FRAME_RE.match(line):
                self._pending_console_trace[2]["stack"].append({
                    "file": m["file"],
                    "line": int(m["line"]),
                    "function": m["func"],
                    "is_js": True,
                })
                return True
            self._flush_pending_console_trace()

        # If we're assembling a multi-line console.<method>: body, route the
        # line through the state machine; on a mismatch we flush and fall
        # through so the line still gets a chance at the regular patterns.
        if self._pending_multiline_console is not None:
            if self._handle_multiline_console_line(line):
                return True

        if m := _DOCSHELL_RE.match(line):
            return self._handle_leak_log(
                m,
                timestamp,
                test_name,
                kind="DocShell",
                key=("docshell", m["pid"], m["ptr"], m["id"]),
            )

        if m := _DOMWINDOW_RE.match(line):
            return self._handle_leak_log(
                m,
                timestamp,
                test_name,
                kind="DOMWindow",
                key=("domwindow", m["pid"], m["ptr"], m["serial"]),
            )

        if m := _JS_ERROR_RE.match(line):
            name = "JavaScript error" if m["level"] == "error" else "JavaScript warning"
            marker_data = {
                "type": "jsError",
                "message": m["message"],
                "file": m["file"],
                "line": int(m["line"]),
                "stack": [{"file": m["file"], "line": int(m["line"]), "is_js": True}],
            }
            if test_name:
                marker_data["test"] = test_name
            self._add_event(name, timestamp, marker_data)
            return True

        if m := _WARNING_RE.match(line):
            display, frame_file = self._clean_frame_file(m["file"])
            marker_data = {
                "type": "cppDebug",
                "message": m["message"],
                "file": display,
                "line": int(m["line"]),
                "process": m["proc"],
                "pid": int(m["pid"]),
                "thread": m["thread"],
                "stack": [{"file": frame_file, "line": int(m["line"])}],
            }
            if test_name:
                marker_data["test"] = test_name
            self._add_event("C++ warning", timestamp, marker_data)
            return True

        if self._asan_parser is not None and self._asan_parser.log(
            line, pid=process, test=test_name, time=timestamp
        ):
            return True

        if self._ubsan_parser is not None and self._ubsan_parser.log(
            line, pid=process, test=test_name, time=timestamp
        ):
            return True

        if self._assertion_parser is not None:
            if self._assertion_parser.log(
                line, pid=process, test=test_name, time=timestamp
            ):
                return True
        elif m := _ASSERTION_RE.match(line):
            display, frame_file = self._clean_frame_file(m["file"])
            marker_data = {
                "type": "cppDebug",
                "message": m["message"],
                "file": display,
                "line": int(m["line"]),
                "process": m["proc"],
                "pid": int(m["pid"]),
                "thread": m["thread"],
                "stack": [{"file": frame_file, "line": int(m["line"])}],
                "color": "red",
            }
            if test_name:
                marker_data["test"] = test_name
            self._add_event("C++ assertion", timestamp, marker_data)
            return True

        if m := _CONSOLE_RE.match(line):
            method = m["method"]
            message = m["message"] or ""
            name = "console." + method
            # createMultiLineDumper() (Console.sys.mjs) emits a header with no
            # real body. Without a console prefix the body is empty; with a
            # prefix it's "<prefix>:" (followed by a space the harness may
            # rstrip away). Defer the marker in either case so we can attach
            # the follow-up Message:/Stack:/frame lines, and keep enough state
            # to fall back to a single-line marker when the body turns out to
            # be a real message that just happens to end with ":".
            if method in _MULTILINE_CONSOLE_METHODS:
                if not message:
                    marker_data = {"type": "console", "message": ""}
                    if test_name:
                        marker_data["test"] = test_name
                    self._pending_multiline_console = (
                        name,
                        timestamp,
                        marker_data,
                        "await_message",
                        None,
                    )
                    return True
                if message.endswith(":"):
                    marker_data = {"type": "console", "message": message}
                    if test_name:
                        marker_data["test"] = test_name
                    self._pending_multiline_console = (
                        name,
                        timestamp,
                        marker_data,
                        "speculative",
                        message,
                    )
                    return True
            marker_data = {"type": "console", "message": message}
            if test_name:
                marker_data["test"] = test_name
            if method == "trace":
                marker_data["stack"] = []
                self._pending_console_trace = (name, timestamp, marker_data)
            else:
                self._add_event(name, timestamp, marker_data)
            return True

        return False

    def _handle_multiline_console_line(self, line):
        """Attach a body line to the pending multi-line console.* marker.

        Returns True if the line was consumed; False if it broke the expected
        sequence (in which case the pending marker is flushed and the caller
        should continue parsing the line through the regular patterns).
        """
        name, timestamp, marker_data, phase, prefix_body = (
            self._pending_multiline_console
        )
        if phase in ("await_message", "speculative"):
            if m := _CONSOLE_MESSAGE_RE.match(line):
                # If the header carried a "<prefix>:" body, stitch it back in
                # front of the Message: text so the marker reads the same as
                # the single-line "console.<method>: <prefix>: <text>" form.
                marker_data["message"] = (
                    f"{prefix_body} {m['message']}" if prefix_body else m["message"]
                )
                self._pending_multiline_console = (
                    name,
                    timestamp,
                    marker_data,
                    "await_stack",
                    prefix_body,
                )
                return True
        elif phase == "await_stack":
            if _CONSOLE_STACK_HEADER_RE.match(line):
                marker_data["stack"] = []
                self._pending_multiline_console = (
                    name,
                    timestamp,
                    marker_data,
                    "frames",
                    prefix_body,
                )
                return True
        elif phase == "frames":
            if m := _CONSOLE_JS_FRAME_RE.match(line):
                marker_data["stack"].append({
                    "file": m["file"],
                    "line": int(m["line"]),
                    "column": int(m["col"]),
                    "function": m["func"],
                    "is_js": True,
                })
                return True
        self._flush_pending_multiline_console()
        return False

    def _flush_pending_console_trace(self):
        """Emit a deferred console.trace marker once its stack is collected."""
        if self._pending_console_trace is None:
            return
        name, timestamp, marker_data = self._pending_console_trace
        self._pending_console_trace = None
        self._add_event(name, timestamp, marker_data)

    def _flush_pending_multiline_console(self):
        """Emit a deferred multi-line console.<method> marker.

        For "speculative" pending markers the body turned out to be a regular
        single-line message ending in ":" rather than a multi-line header, so
        marker_data["message"] still holds the original line as captured.
        """
        if self._pending_multiline_console is None:
            return
        name, timestamp, marker_data, _, _ = self._pending_multiline_console
        self._pending_multiline_console = None
        self._add_event(name, timestamp, marker_data)

    def _handle_leak_log(self, m, timestamp, test_name, kind, key):
        """Emit a duration marker pairing ++DOCSHELL/++DOMWINDOW with its matching --."""
        groups = m.groupdict()
        marker_data = {
            "type": kind,
            "process": m["proc"],
            "pid": int(m["pid"]),
            "thread": m["thread"],
            "pointer": m["ptr"],
        }
        if groups.get("id") is not None:
            marker_data["id"] = int(m["id"])
        if groups.get("serial") is not None:
            marker_data["serial"] = int(m["serial"])
        if groups.get("outer") is not None:
            marker_data["outer"] = m["outer"]
        if test_name:
            marker_data["test"] = test_name

        if m["op"] == "++":
            self._leaked_instances[key] = (kind, timestamp, marker_data)
            return True

        active = self._leaked_instances.pop(key, None)
        start = active[1] if active else timestamp
        if m["url"]:
            marker_data["url"] = m["url"]

        self._add_marker(kind, start, timestamp, marker_data, self.OTHER_CATEGORY)
        return True

    def _flush_leak_logs(self):
        """Emit instant markers for leak-log instances created but never destroyed."""
        for kind, timestamp, marker_data in self._leaked_instances.values():
            self._add_marker(kind, timestamp, None, marker_data, self.OTHER_CATEGORY)
        self._leaked_instances.clear()

    @staticmethod
    def _test_key(data):
        return (data.get("subsuite") or "", _test_id_str(data["test"]))

    @staticmethod
    def begin_test(data):
        """Begin tracking a test with enhanced metadata support.

        Args:
            data: Dictionary containing test data (e.g., {"test": "test_name", "time": timestamp})
        """
        if SystemResourceMonitor.instance and "test" in data:
            SystemResourceMonitor.instance._active_tests[
                SystemResourceMonitor._test_key(data)
            ] = (SystemResourceMonitor.instance.get_monotonic_time_from_data(data), [])

    @staticmethod
    def end_test(data):
        """End tracking a test and record it with status and color.

        Args:
            data: Dictionary containing test data including:
                  - "test": test name
                  - "status": test status ("PASS", "OK", "FAIL", "TIMEOUT", "CRASH", etc.)
                  - "expected": the expected status if it differs from "status"
                  - "known_intermittent": statuses the test is known to produce
                  - "message": A string describing the status.
        """
        if not SystemResourceMonitor.instance or "test" not in data:
            return

        # Imported lazily: this module is also copied standalone into docker
        # images, where moztest isn't available and no test record arrives.
        from moztest.outcome import Outcome

        key = SystemResourceMonitor._test_key(data)
        active = SystemResourceMonitor.instance._active_tests.pop(key, None)
        if active is None:
            return
        start, subtest_outcomes = active
        test_name = key[1]
        end = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        # Create marker data with test information
        marker_data = {
            "type": "Test",
            "test": test_name,
            "name": test_name.split("/")[-1],
        }

        # Include timeout factor if present in extra data
        extra = data.get("extra", {})
        if extra and "timeoutfactor" in extra:
            marker_data["timeoutfactor"] = extra["timeoutfactor"]

        if key[0]:
            marker_data["subsuite"] = key[0]

        status = data.get("status", "")
        if status:
            marker_data["status"] = status
            expected = data.get("expected")  # None if result was as expected
            if expected is not None:
                marker_data["expected"] = expected
            unexpected_subtests = sum(
                1
                for subtest_outcome in subtest_outcomes
                if subtest_outcome in (Outcome.UNEXPECTED, Outcome.WILL_RETRY)
            )
            if unexpected_subtests:
                marker_data["unexpected_subtests"] = unexpected_subtests

            # A test with subtests ends OK when the harness ran it fine: its
            # subtests decide whether it passed.
            outcome = max([Outcome.from_log(data), *subtest_outcomes])
            if status in ("SKIP", "TIMEOUT"):
                marker_data["color"] = "yellow"
            elif status in ("CRASH", "ERROR"):
                marker_data["color"] = "red"
            elif outcome in (Outcome.UNEXPECTED, Outcome.WILL_RETRY):
                marker_data["color"] = "orange"
            elif outcome == Outcome.KNOWN_INTERMITTENT:
                marker_data["color"] = "yellow"
            else:
                marker_data["color"] = "green"
            message = data.get("message", "")
            if message and marker_data["color"] != "green":
                marker_data["message"] = message

        SystemResourceMonitor.instance.record_marker("test", start, end, marker_data)

    @staticmethod
    def test_status(data):
        """Record a test_status/log/process_output event.

        Args:
            data: Dictionary containing test_status/log/process_output data including:
                  - "action": the action type
                  - "test": test name (optional)
                  - "subtest": subtest name (optional, only for test_status/log)
                  - "status" or "level": status for test_status/log
                  - "time": timestamp in milliseconds
                  - "message" or "data": optional message
        """
        if not SystemResourceMonitor.instance:
            return

        from moztest.outcome import Outcome

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        marker_data = {"type": "TestStatus"}
        test_name = _test_id_str(data.get("test"))
        outcome = None

        if data.get("action") == "process_output":
            line = data.get("data")
            if line and SystemResourceMonitor.instance._parse_process_output(
                line, timestamp, test_name, process=data.get("process")
            ):
                # Line was parsed into a specialized marker; nothing else to do.
                return
            # Fallback: keep the raw line as a generic "output" marker.
            marker_name = "output"
            message = line
        else:
            # test_status and log actions
            status = (data.get("status") or data.get("level")).upper()
            marker_name = status
            if data.get("action") == "test_status":
                outcome = Outcome.from_log(data)
            if status == "ERROR":
                marker_data["color"] = "red"
            elif outcome == Outcome.KNOWN_INTERMITTENT:
                marker_name = f"KNOWN-INTERMITTENT-{status}"
                marker_data["color"] = "yellow"
            elif outcome == Outcome.EXPECTED:
                if status == "PASS":
                    marker_data["color"] = "green"
                elif status != "SKIP":
                    marker_name = f"KNOWN-{status}"
                    marker_data["color"] = "yellow"
            elif outcome is not None:
                if status == "PASS":
                    # A todo() that passed; this fails the test.
                    marker_name = "UNEXPECTED-PASS"
                marker_data["color"] = "orange"

            if subtest := data.get("subtest"):
                marker_data["subtest"] = subtest

            message = data.get("message")

        if test_name:
            marker_data["test"] = test_name

        if message:
            marker_data["message"] = message

        if stack := data.get("stack"):
            marker_data["stack"] = stack

        SystemResourceMonitor.record_event(marker_name, timestamp, marker_data)

        if not test_name or outcome in (None, Outcome.EXPECTED):
            return
        key = SystemResourceMonitor._test_key(data)
        active = SystemResourceMonitor.instance._active_tests.get(key)
        if active is not None:
            active[1].append(outcome)
        elif outcome == Outcome.UNEXPECTED:
            # The test was reported passing before this status (a shutdown
            # leak, a replayed log): fail its marker.
            SystemResourceMonitor.instance._fail_test_marker(key, timestamp)

    def _fail_test_marker(self, key, timestamp):
        for marker in reversed(self.markers):
            marker_type, marker_start, marker_end, marker_data, _ = marker
            if (
                marker_type == "test"
                and (marker_data.get("subsuite") or "", marker_data.get("test")) == key
                and marker_start <= timestamp <= marker_end
                and marker_data.get("color") in ("green", "yellow")
                and marker_data.get("status") not in ("SKIP", "TIMEOUT")
            ):
                marker_data["color"] = "orange"
                marker_data["unexpected_subtests"] = (
                    marker_data.get("unexpected_subtests", 0) + 1
                )
                break

    @staticmethod
    def crash(data):
        """Record a crash event.

        Args:
            data: Dictionary containing crash data including:
                  - "signature": crash signature
                  - "reason": crash reason (optional)
                  - "test": test name (optional)
                  - "minidump_path": path to minidump file (optional)
                  - "stack": structured stack (array of frame dicts) (optional)
                  - "time": timestamp in milliseconds
        """
        if not SystemResourceMonitor.instance:
            return

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        marker_data = {
            "type": "Crash",
            "color": "red",
        }

        if signature := data.get("signature"):
            marker_data["signature"] = signature
        if reason := data.get("reason"):
            marker_data["reason"] = reason
        if test := data.get("test"):
            marker_data["test"] = test

        if minidump_path := data.get("minidump_path"):
            # Extract the minidump name (without extension) from the path
            # e.g., "/tmp/xpc-other-k49po531/7a7f1343-4dc3-224c-638b-5806ab642301.dmp"
            # -> "7a7f1343-4dc3-224c-638b-5806ab642301"
            minidump_name = os.path.splitext(os.path.basename(minidump_path))[0]
            marker_data["minidump"] = minidump_name

        # Add stack if available (structured format: array of frame dicts)
        if stack := data.get("crashing_thread_stack"):
            marker_data["stack"] = stack

        SystemResourceMonitor.record_event("CRASH", timestamp, marker_data)

    @staticmethod
    def lsan_leak(data):
        """Record an LSan leak event.

        Args:
            data: Dictionary containing lsan_leak data including:
                  - "kind": "Direct" or "Indirect"
                  - "bytes": bytes leaked at this allocation site
                  - "objects": number of objects leaked at this allocation site
                  - "stack": structured stack frames (list of frame dicts), if any
                  - "scope": optional scope (e.g. test name)
                  - "allowed_match": frame matching an allow-list entry, if any
        """
        if not SystemResourceMonitor.instance:
            return

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        marker_data = {
            "type": "LSanLeak",
            "kind": data["kind"],
            "bytes": data["bytes"],
            "objects": data["objects"],
        }

        if stack := data.get("stack"):
            rewritten = []
            for frame in stack:
                if "file" in frame:
                    rewritten.append({
                        **frame,
                        "file": SystemResourceMonitor.instance._clean_frame_file(
                            frame["file"]
                        )[1],
                    })
                else:
                    rewritten.append(frame)
            marker_data["stack"] = rewritten
        if scope := data.get("scope"):
            marker_data["scope"] = scope
        if allowed_match := data.get("allowed_match"):
            marker_data["allowed_match"] = allowed_match
            marker_data["color"] = "yellow"
        else:
            marker_data["color"] = "orange"

        SystemResourceMonitor.record_event("LSan Leak", timestamp, marker_data)

    @staticmethod
    def lsan_summary(data):
        """Record an LSan summary event.

        Args:
            data: Dictionary containing lsan_summary data including:
                  - "bytes": total bytes leaked
                  - "allocations": total allocations leaked
                  - "allowed": whether the leak is allow-listed
        """
        if not SystemResourceMonitor.instance:
            return

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        allowed = data.get("allowed", False)
        marker_data = {
            "type": "LSanSummary",
            "bytes": data["bytes"],
            "allocations": data["allocations"],
            "color": "yellow" if allowed else "orange",
        }
        if allowed:
            marker_data["allowed"] = True

        SystemResourceMonitor.record_event("LSan Summary", timestamp, marker_data)

    @staticmethod
    def tsan_error(data):
        """Record a ThreadSanitizer report.

        A TSan report can carry several labeled stacks (e.g. the two
        acquisition sites of a lock-order inversion, or the racing accesses of
        a data race). A profiler marker holds a single stack, so one marker is
        emitted per labeled stack; all markers from the same report share a
        "report_index" so they can be correlated on the timeline.

        Args:
            data: Dictionary containing tsan_error data including:
                  - "kind": report kind (e.g. "data race",
                            "lock-order-inversion (potential deadlock)")
                  - "signature": SUMMARY location (e.g.
                                 "Mutex_posix.cpp:91:3 in mutexLock")
                  - "pid": pid the report is about (optional)
                  - "description": extra context such as the lock-order cycle
                                   graph (optional)
                  - "stacks": list of {"label", "stack"} dicts, one per labeled
                              stack in the report
                  - "scope": identifier for the browser session, e.g. a
                             directory name (optional)
        """
        if not SystemResourceMonitor.instance:
            return

        monitor = SystemResourceMonitor.instance
        timestamp = monitor.get_monotonic_time_from_data(data)

        report_index = monitor._tsan_report_count
        monitor._tsan_report_count += 1

        def make_marker_data():
            marker_data = {
                "type": "TSanError",
                "kind": data["kind"],
                "report_index": report_index,
                "color": "orange",
            }
            if pid := data.get("pid"):
                marker_data["pid"] = pid
            if description := data.get("description"):
                marker_data["description"] = description
            if scope := data.get("scope"):
                marker_data["scope"] = scope
            return marker_data

        stacks = data.get("stacks") or []
        if not stacks:
            SystemResourceMonitor.record_event(
                "TSan Error", timestamp, make_marker_data()
            )
            return

        for substack in stacks:
            marker_data = make_marker_data()
            marker_data["label"] = substack.get("label", "")
            rewritten = []
            for frame in substack.get("stack", []):
                if "file" in frame:
                    rewritten.append({
                        **frame,
                        "file": monitor._clean_frame_file(frame["file"])[1],
                    })
                else:
                    rewritten.append(frame)
            marker_data["stack"] = rewritten
            SystemResourceMonitor.record_event("TSan Error", timestamp, marker_data)

    @staticmethod
    def assertion_failure(data):
        """Record a native assertion failure.

        Args:
            data: Dictionary containing assertion_failure data including:
                  - "kind": "MOZ_ASSERT", "MOZ_CRASH", "NS_ASSERTION" or "NS_ABORT"
                  - "message": asserted condition or crash reason
                  - "file", "lineno": source location (optional)
                  - "pid", "process_type", "thread": asserting process (optional)
                  - "fatal": whether the process aborts (optional)
                  - "stack": list of profiler-format frame dicts (optional)
                  - "test": test name (optional)
                  - "time": timestamp in milliseconds

        The same assertion may be pending in the line-based fallback fed by
        process_output lines; the structured action wins.
        """
        if not SystemResourceMonitor.instance:
            return

        monitor = SystemResourceMonitor.instance
        timestamp = monitor.get_monotonic_time_from_data(data)

        key = (data["message"], data.get("file"), data.get("lineno"))
        if not (monitor._assertion_parser and monitor._assertion_parser.discard(key)):
            monitor._recent_assertion_actions.append(key)
        monitor._emit_assertion_marker(timestamp, data)

    def _record_line_assertion(self, data):
        key = (data["message"], data.get("file"), data.get("lineno"))
        if key in self._recent_assertion_actions:
            self._recent_assertion_actions.remove(key)
            return
        self._emit_assertion_marker(data.pop("time", time.monotonic()), data)

    def _rewrite_stack_files(self, stack):
        """Point the frames' files at the source view."""
        return [
            {**frame, "file": self._clean_frame_file(frame["file"])[1]}
            if "file" in frame
            else frame
            for frame in stack
        ]

    def _emit_assertion_marker(self, timestamp, data):
        fatal = bool(data.get("fatal"))
        marker_data = {
            "type": "AssertionFailure",
            "kind": data["kind"],
            "message": data["message"],
            "color": "red" if fatal else "orange",
        }
        if fatal:
            marker_data["fatal"] = True
        frame_file = None
        if file_ := data.get("file"):
            marker_data["file"], frame_file = self._clean_frame_file(file_)
        lineno = data.get("lineno")
        if lineno is not None:
            marker_data["line"] = lineno
        for source, target in (
            ("pid", "pid"),
            ("process_type", "process"),
            ("thread", "thread"),
            ("test", "test"),
        ):
            if value := data.get(source):
                marker_data[target] = value
        if stack := data.get("stack"):
            marker_data["stack"] = self._rewrite_stack_files(stack)
        elif frame_file and lineno is not None:
            marker_data["stack"] = [{"file": frame_file, "line": lineno}]
        self._add_event(data["kind"], timestamp, marker_data)

    @staticmethod
    def ubsan_error(data):
        """Record an UndefinedBehaviorSanitizer report.

        Args:
            data: Dictionary containing ubsan_error data including:
                  - "kind": check kind (e.g. "undefined-behavior")
                  - "message": the "runtime error:" message
                  - "file", "lineno", "column": source location (optional)
                  - "stack": list of profiler-format frame dicts (optional)
                  - "scope": identifier for the browser session, e.g. a
                             directory name (optional)
                  - "test": test name (optional)
                  - "time": timestamp in milliseconds

        The same report may be pending in the line-based fallback fed by
        process_output lines; the structured action wins.
        """
        if not SystemResourceMonitor.instance:
            return

        monitor = SystemResourceMonitor.instance
        timestamp = monitor.get_monotonic_time_from_data(data)

        key = _ubsan_key(data)
        if not (monitor._ubsan_parser and monitor._ubsan_parser.discard(key)):
            monitor._recent_ubsan_actions.append(key)
        monitor._emit_ubsan_marker(timestamp, data)

    def _record_line_ubsan(self, data):
        key = _ubsan_key(data)
        if key in self._recent_ubsan_actions:
            self._recent_ubsan_actions.remove(key)
            return
        self._emit_ubsan_marker(data.pop("time", time.monotonic()), data)

    def _emit_ubsan_marker(self, timestamp, data):
        marker_data = {
            "type": "UBSanError",
            "kind": data["kind"],
            "message": data["message"],
            "color": "orange",
        }
        frame_file = None
        if file_ := data.get("file"):
            marker_data["file"], frame_file = self._clean_frame_file(file_)
        lineno = data.get("lineno")
        if lineno is not None:
            marker_data["line"] = lineno
        for key in ("column", "scope", "test"):
            if data.get(key) is not None:
                marker_data[key] = data[key]
        if stack := data.get("stack"):
            marker_data["stack"] = self._rewrite_stack_files(stack)
        elif frame_file and lineno is not None:
            marker_data["stack"] = [{"file": frame_file, "line": lineno}]
        self._add_event("UBSan Error", timestamp, marker_data)

    @staticmethod
    def asan_error(data):
        """Record an AddressSanitizer report.

        Args:
            data: Dictionary containing asan_error data including:
                  - "kind": error kind (e.g. "SEGV", "heap-use-after-free")
                  - "message": the header text after "AddressSanitizer: "
                  - "pid": pid of the process the report is about (optional)
                  - "file", "lineno", "column", "function": SUMMARY location
                    (optional)
                  - "stack": list of profiler-format frame dicts (optional)
                  - "stacks": list of {"label", "stack"} dicts, one per
                              labeled stack of the report such as "freed by
                              thread T0 here" (optional)
                  - "scope": identifier for the browser session, e.g. a
                             directory name (optional)
                  - "test": test name (optional)
                  - "quiet": whether the test run will be retried (optional)
                  - "time": timestamp in milliseconds

        The same report may be pending in the line-based fallback fed by
        process_output lines; the structured action wins.
        """
        if not SystemResourceMonitor.instance:
            return

        monitor = SystemResourceMonitor.instance
        timestamp = monitor.get_monotonic_time_from_data(data)

        key = _asan_key(data)
        if not (monitor._asan_parser and monitor._asan_parser.discard(key)):
            monitor._recent_asan_actions.append(key)
        monitor._emit_asan_marker(timestamp, data)

    def _record_line_asan(self, data):
        key = _asan_key(data)
        if key in self._recent_asan_actions:
            self._recent_asan_actions.remove(key)
            return
        self._emit_asan_marker(data.pop("time", time.monotonic()), data)

    def _emit_asan_marker(self, timestamp, data):
        marker_data = {
            "type": "ASanError",
            "kind": data["kind"],
            "message": data["message"],
            "color": "orange" if data.get("quiet") else "red",
        }
        frame_file = None
        if file_ := data.get("file"):
            marker_data["file"], frame_file = self._clean_frame_file(file_)
        lineno = data.get("lineno")
        if lineno is not None:
            marker_data["line"] = lineno
        for key in ("column", "function", "pid", "scope", "test"):
            if data.get(key) is not None:
                marker_data[key] = data[key]
        stack = data.get("stack") or []
        # For the marker table: JIT code, system libraries and truncated
        # reports have no SUMMARY location.
        if "file" in marker_data:
            location = marker_data["file"]
            if lineno is not None:
                location += f":{lineno}"
        else:
            location = (
                data.get("function")
                or next((f["function"] for f in stack if f.get("function")), None)
                or data["message"]
            )
        marker_data["location"] = location
        if stack:
            marker_data["stack"] = self._rewrite_stack_files(stack)
        elif frame_file and lineno is not None:
            marker_data["stack"] = [{"file": frame_file, "line": lineno}]
        if stacks := data.get("stacks"):
            marker_data["stacks"] = [
                {
                    "label": s.get("label", ""),
                    "stack": self._rewrite_stack_files(s.get("stack", [])),
                }
                for s in stacks
            ]
        self._add_event("ASan Error", timestamp, marker_data)

    @staticmethod
    def mozleak_object(data):
        """Record a per-process per-class leaked object event.

        Args:
            data: Dictionary containing mozleak_object data including:
                  - "process": process name
                  - "name": leaked object/class name
                  - "count": leaked instance count
                  - "bytes_per_inst": per-instance size in bytes
                  - "bytes_leaked": total bytes leaked for this class
                  - "total_instances": total instances allocated
                  - "scope": optional scope
                  - "allowed": whether the leak is allow-listed
        """
        if not SystemResourceMonitor.instance:
            return

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        allowed = data.get("allowed", False)
        marker_data = {
            "type": "MozLeakObject",
            "process": data["process"],
            "name": data["name"],
            "count": data["count"],
            "bytes_per_inst": data["bytes_per_inst"],
            "bytes_leaked": data["bytes_leaked"],
            "total_instances": data["total_instances"],
            "color": "yellow" if allowed else "orange",
        }
        if scope := data.get("scope"):
            marker_data["scope"] = scope
        if allowed:
            marker_data["allowed"] = True

        SystemResourceMonitor.record_event("Leaked Object", timestamp, marker_data)

    @staticmethod
    def mozleak_total(data):
        """Record a per-process leak total event.

        Clean totals (zero bytes leaked) are not recorded as markers, to keep
        the timeline focused on actual leaks.

        Args:
            data: Dictionary containing mozleak_total data including:
                  - "process": process name
                  - "bytes": total bytes leaked, or None if no TOTAL line was seen
                  - "objects": list of leaked object class names
                  - "scope": optional scope
                  - "induced_crash": whether the process deliberately crashed
                  - "ignore_missing": whether a missing total should be ignored
        """
        if not SystemResourceMonitor.instance:
            return

        bytes_leaked = data.get("bytes")
        if bytes_leaked == 0:
            return

        timestamp = SystemResourceMonitor.instance.get_monotonic_time_from_data(data)

        marker_data = {
            "type": "MozLeakTotal",
            "process": data["process"],
            "bytes": bytes_leaked,
            "objects": data.get("objects", []),
        }
        if scope := data.get("scope"):
            marker_data["scope"] = scope

        induced_crash = data.get("induced_crash", False)
        ignore_missing = data.get("ignore_missing", False)
        if induced_crash:
            marker_data["induced_crash"] = True
        if ignore_missing:
            marker_data["ignore_missing"] = True

        if bytes_leaked is None:
            if induced_crash or ignore_missing:
                marker_data["color"] = "grey"
            else:
                marker_data["color"] = "red"
        else:
            marker_data["color"] = "orange"

        SystemResourceMonitor.record_event("Leaked Total", timestamp, marker_data)
