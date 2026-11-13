# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re


class ASanErrorParser:
    """Parses AddressSanitizer error reports out of a process output stream
    and emits one ``asan_error`` structured-log action per completed report.

    A report opens with an ``==<pid>==ERROR: AddressSanitizer: <message>``
    line, continues with the access description, the stack (one
    ``#N 0x<addr> in <function> <location>`` frame per line), the labeled
    stacks of related events (``freed by thread T0 here:`` ...) and runtime
    hints, and closes with a
    ``SUMMARY: AddressSanitizer: <kind> <location> in <function>`` line,
    after which the runtime may dump shadow memory before it aborts the
    process with ``==<pid>==ABORTING``. Only the runtime's own lines carry
    the ``==<pid>==`` prefix, and that pid is the process the report is about
    even when its output is relayed by a parent process, so reports carry it.

    Symbolizing the stack can take seconds, during which the output of other
    processes sharing the stream interleaves with the report, so lines the
    parser does not recognize leave the report open. In-progress state is
    keyed by the emitting stream.
    """

    # xpcshell demotes the header to "ERROR (will retry):" when it replays a
    # test's buffered output.
    headerRegExp = re.compile(
        r"^==(?P<pid>\d+)==\s*ERROR(?: \(will retry\))?: AddressSanitizer: "
        r"(?P<message>.+?)\s*$"
    )
    deadlySignalRegExp = re.compile(r"^AddressSanitizer:DEADLYSIGNAL\s*$")
    rulerRegExp = re.compile(r"^=+\s*$")
    reportLineRegExp = re.compile(r"^==(?P<pid>\d+)==\s*(?P<body>.*?)\s*$")
    accessRegExp = re.compile(
        r"^(?:READ|WRITE|ACCESS|ASSUME) of size \d+ at 0x[0-9a-fA-F]+ thread T\d+"
    )
    labelRegExp = re.compile(
        r"^(?P<label>(?:freed|previously allocated|allocated) by thread T\d+"
        r"(?: \([^)]*\))? here"
        r"|Thread T\d+(?: \([^)]*\))? created by T\d+(?: \([^)]*\))? here"
        r"|Address 0x[0-9a-fA-F]+ is located in stack of thread T\d+"
        r" at offset \d+ in frame):?\s*$"
    )
    noteRegExp = re.compile(
        r"^(?:0x[0-9a-fA-F]+ is located |Address 0x[0-9a-fA-F]+ is "
        r"|\s+This frame has \d+ object|\s+\[\d+, \d+\) '|HINT: |SCARINESS: "
        r"|\s+\(longjmp and C\+\+ exceptions"
        r"|AddressSanitizer can not provide additional info\.)"
    )
    registerRegExp = re.compile(r"^\s*(?:[a-z0-9]{1,5} = 0x[0-9a-fA-F]+\s*)+$")
    # The kind must start with a letter: LeakSanitizer's "SUMMARY:
    # AddressSanitizer: 123 byte(s) leaked in 4 allocation(s)." is not ours.
    summaryRegExp = re.compile(
        r"^SUMMARY: AddressSanitizer: (?P<kind>[A-Za-z][\w-]*)"
        r"(?: (?P<loc>\(<unknown module>\)|\S+))?(?: \(BuildId: [0-9a-fA-F]+\))?"
        r"(?: in (?P<function>.+?))?\s*$"
    )
    shadowRegExp = re.compile(
        r"^Shadow bytes around the buggy address:$"
        r"|^Shadow byte legend .*:$"
        r"|^(?:=>)?\s*0x[0-9a-fA-F]+:(?:\s*\[?[0-9a-fA-F]{2}\]?)+\s*$"
        r"|^\s+[A-Z][\w /()+-]*:\s+(?:[0-9a-fA-F]{2}\s*)+$"
    )
    # Headers whose first word is not the SUMMARY kind.
    kindPrefixes = (
        ("attempting double-free", "double-free"),
        ("attempting free on address which was not malloc()-ed", "bad-free"),
        ("requested allocation size", "allocation-size-too-big"),
        ("allocator is out of memory", "out-of-memory"),
        ("calloc parameters overflow", "calloc-overflow"),
        ("specified RSS limit exceeded", "rss-limit-exceeded"),
    )
    kindRegExp = re.compile(r"^(?P<kind>[A-Za-z][\w-]*)(?=[ :(]|$)")

    # The frame and location handling below mirrors moztest.ubsan, which
    # parses the same sanitizer runtime output; mozleak has no dependencies
    # so it cannot import it.
    # "    #%n %p %F %L": %F is "in <function>" or empty, %L is a source
    # location, "(<module>+0x<offset>)" or "(<unknown module>)".
    frameRegExp = re.compile(
        r"^\s*#(?P<n>\d+) 0x[0-9a-fA-F]+ ?(?P<body>.*?)"
        r"(?: \(BuildId: [0-9a-fA-F]+\))?\s*$"
    )
    moduleRegExp = re.compile(
        r"^(?:in (?P<func>.*?))?\s*\((?:(?P<module>[^()\s]+)\+"
        r"(?P<offset>0x[0-9a-fA-F]+)|<unknown module>)\)$"
    )
    sourceLocRegExp = re.compile(
        r"^(?P<file>.+?)(?::(?P<line>\d+)(?::(?P<col>\d+))?)?$"
    )
    addressRegExp = re.compile(r"^0x[0-9a-fA-F]+$")
    # Rust frames aren't demangled cleanly and carry a trailing ::h<hex> that
    # changes with compiler versions; see bug 1507350.
    rustRegExp = re.compile(r"::h[a-f0-9]{16}$")

    def __init__(self, logger, scope=None):
        self.logger = logger
        self.scope = scope
        # stream id -> in-progress report dict
        self._reports = {}
        # stream id -> pid of the report whose trailer is still printing
        self._trailers = {}

    def log(self, line, pid=None, scope=None, test=None, time=None, quiet=False):
        """Feed one output line from stream ``pid``.

        Returns True when the line belongs to a report (header, description,
        frame, hint, SUMMARY or the trailer that follows it), False otherwise.
        """
        line = line.rstrip("\r\n")

        header = self.headerRegExp.match(line)
        if header:
            # A new report ends any report still open on this stream.
            self._finish(pid)
            self._trailers.pop(pid, None)
            message = header.group("message")
            report = {
                "pid": int(header.group("pid")),
                "kind": self._kind_from_message(message),
                "message": message,
                "file": None,
                "lineno": None,
                "column": None,
                "function": None,
                "stack": [],
                "stacks": [],
                "scope": scope if scope is not None else self.scope,
                "test": test,
                "time": time,
                "quiet": quiet,
                "discarded": False,
            }
            report["current"] = report["stack"]
            self._reports[pid] = report
            return True

        if self.deadlySignalRegExp.match(line):
            return True

        report = self._reports.get(pid)
        if report is None:
            return self._trailer(pid, line)

        summary = self.summaryRegExp.match(line)
        if summary:
            report["kind"] = summary.group("kind")
            if summary.group("loc"):
                report["file"], report["lineno"], report["column"] = self._location(
                    summary.group("loc")
                )
            if summary.group("function"):
                report["function"] = self.rustRegExp.sub("", summary.group("function"))
            self._finish(pid)
            self._trailers[pid] = report["pid"]
            return True

        frame = self.frameRegExp.match(line)
        if frame:
            if not report["discarded"]:
                report["current"].append(self._frame(frame.group("body").strip()))
            return True

        label = self.labelRegExp.match(line)
        if label:
            stack = []
            report["stacks"].append({"label": label.group("label"), "stack": stack})
            report["current"] = stack
            return True

        report_line = self.reportLineRegExp.match(line)
        if report_line:
            if int(report_line.group("pid")) != report["pid"]:
                return False
            if report_line.group("body") == "ABORTING":
                self._finish(pid)
            return True

        if (
            not line.strip()
            or self.rulerRegExp.match(line)
            or self.accessRegExp.match(line)
            or self.noteRegExp.match(line)
            or self.registerRegExp.match(line)
        ):
            return True

        return False

    def flush(self):
        """Emit any reports left open by truncated output."""
        for pid in list(self._reports):
            self._finish(pid)

    def attribute(self, test):
        """Name ``test`` on the open reports that name none.

        The harness knows ``test`` was running when they started, so a
        report whose SUMMARY never arrives is not blamed on a later test.
        """
        for report in self._reports.values():
            if report["test"] is None:
                report["test"] = test

    def discard(self, key):
        """Drop the open report identified by ``key`` (its ``(pid, message)``)
        once it completes, because a structured action for the same report
        was logged. Its remaining lines are still consumed. Returns whether a
        report matched.
        """
        for report in self._reports.values():
            if not report["discarded"] and self.key_of(report) == key:
                report["discarded"] = True
                return True
        return False

    @staticmethod
    def key_of(data):
        return (data.get("pid"), data["message"])

    @classmethod
    def _kind_from_message(cls, message):
        for prefix, kind in cls.kindPrefixes:
            if message.startswith(prefix):
                return kind
        match = cls.kindRegExp.match(message)
        return match.group("kind") if match else "unknown"

    def _trailer(self, pid, line):
        asan_pid = self._trailers.get(pid)
        if asan_pid is None:
            return False
        report_line = self.reportLineRegExp.match(line)
        if report_line and int(report_line.group("pid")) == asan_pid:
            if report_line.group("body") == "ABORTING":
                del self._trailers[pid]
            return True
        if not line.strip() or self.shadowRegExp.match(line):
            return True
        del self._trailers[pid]
        return False

    def _finish(self, pid):
        report = self._reports.pop(pid, None)
        if report is None or report["discarded"]:
            return
        kwargs = {}
        if report["time"] is not None:
            kwargs["time"] = report["time"]
        self.logger.asan_error(
            kind=report["kind"],
            message=report["message"],
            pid=report["pid"],
            file=report["file"],
            lineno=report["lineno"],
            column=report["column"],
            function=report["function"],
            stack=report["stack"] or None,
            stacks=report["stacks"] or None,
            scope=report["scope"],
            test=report["test"],
            quiet=report["quiet"],
            **kwargs,
        )

    def _frame(self, body):
        module = self.moduleRegExp.match(body)
        if module:
            frame = {}
            if module.group("func"):
                frame["function"] = self.rustRegExp.sub("", module.group("func"))
            if module.group("module"):
                frame["module"] = module.group("module")
                frame["module_offset"] = int(module.group("offset"), 16)
            elif not frame:
                frame["function"] = "<unknown module>"
            return frame

        function = None
        location = body
        if body.startswith("in "):
            function, sep, location = body[3:].rpartition(" ")
            if not sep:
                function, location = location, None
        frame = {}
        if function:
            frame["function"] = self.rustRegExp.sub("", function)
        if location:
            file_, lineno, column = self._location(location)
            if file_:
                frame["file"] = file_
                if lineno is not None:
                    frame["line"] = lineno
                if column is not None:
                    frame["column"] = column
        return frame

    def _location(self, loc):
        if loc.startswith("(") or loc == "<unknown>" or self.addressRegExp.match(loc):
            return None, None, None
        match = self.sourceLocRegExp.match(loc)
        lineno = int(match.group("line")) if match.group("line") else None
        column = int(match.group("col")) if match.group("col") else None
        return match.group("file"), lineno, column
