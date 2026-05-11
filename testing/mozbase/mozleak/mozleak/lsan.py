# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re


class LSANLeaks:
    """
    Parses the log when running an LSAN build, looking for interesting stack frames
    in allocation stacks
    """

    def __init__(
        self,
        logger,
        scope=None,
        allowed=None,
        maxNumRecordedFrames=None,
        allowAll=False,
    ):
        self.logger = logger
        self.inReport = False
        self.fatalError = False
        self.symbolizerError = False
        self.foundLeaks = []
        self.recordMoreFrames = None
        self.currStack = None
        self.currStructuredStack = None
        self.currBytes = None
        self.currObjects = None
        self.currKind = None
        self.maxNumRecordedFrames = maxNumRecordedFrames if maxNumRecordedFrames else 4
        self.summaryData = None
        self.scope = scope
        self.allowedMatch = None
        self.allowAll = allowAll
        self.sawError = False

        # Don't various allocation-related stack frames, as they do not help much to
        # distinguish different leaks.
        unescapedSkipList = [
            "malloc",
            "js_malloc",
            "malloc_",
            "__interceptor_malloc",
            "moz_xmalloc",
            "calloc",
            "js_calloc",
            "calloc_",
            "__interceptor_calloc",
            "moz_xcalloc",
            "realloc",
            "js_realloc",
            "realloc_",
            "__interceptor_realloc",
            "moz_xrealloc",
            "new",
            "js::MallocProvider",
        ]
        self.skipListRegExp = re.compile(
            "^" + "|".join([re.escape(f) for f in unescapedSkipList]) + "$"
        )

        self.startRegExp = re.compile(
            r"==\d+==ERROR: LeakSanitizer: detected memory leaks"
        )
        self.fatalErrorRegExp = re.compile(
            r"==\d+==LeakSanitizer has encountered a fatal error."
        )
        self.symbolizerOomRegExp = re.compile(
            "LLVMSymbolizer: error reading file: Cannot allocate memory"
        )
        self.stackFrameRegExp = re.compile(
            r"    #\d+ (?P<offset>0x[0-9a-f]+) in (?P<func>[^(</]+)"
            r"(?:[^ ]* (?P<file>[^:]+)(?::(?P<line>\d+)(?::(?P<col>\d+))?)?)?$"
        )
        self.sysLibStackFrameRegExp = re.compile(
            r"    #\d+ (?P<offset>0x[0-9a-f]+) \((?P<module>[^+]+)\+(?P<modoffset>0x[0-9a-f]+)\)"
        )
        self.leakHeaderRegexp = re.compile(
            r"^(Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object\(s\) allocated from"
        )
        self.summaryRegexp = re.compile(
            r"SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked in (\d+) allocation\(s\)."
        )
        self.rustRegexp = re.compile("::h[a-f0-9]{16}$")
        self.setAllowed(allowed)

    def setAllowed(self, allowedLines):
        if not allowedLines or self.allowAll:
            self.allowedRegexp = None
        else:
            self.allowedRegexp = re.compile(
                "^" + "|".join([re.escape(f) for f in allowedLines])
            )

    def log(self, line):
        if re.match(self.startRegExp, line):
            self.inReport = True
            # Downgrade this from an ERROR
            self.sawError = True
            return "LeakSanitizer: detected memory leaks"

        if re.match(self.fatalErrorRegExp, line):
            self.fatalError = True
            return line

        if re.match(self.symbolizerOomRegExp, line):
            self.symbolizerError = True
            return line

        if not self.inReport:
            return line

        leakHeader = self.leakHeaderRegexp.match(line)
        if leakHeader:
            self._finishStack()
            self.recordMoreFrames = True
            self.currStack = []
            self.currStructuredStack = []
            self.currKind = leakHeader.group(1)
            self.currBytes = int(leakHeader.group(2))
            self.currObjects = int(leakHeader.group(3))
            return line

        summaryData = self.summaryRegexp.match(line)
        if summaryData:
            assert self.summaryData is None
            self._finishStack()
            self.inReport = False
            self.summaryData = (int(item) for item in summaryData.groups())
            # We don't return the line here because we want to control whether the
            # leak is seen as an expected failure later
            return

        if not self.recordMoreFrames:
            return line

        stackFrame = self.stackFrameRegExp.match(line)
        if stackFrame:
            # Split the frame to remove any return types.
            frame = stackFrame.group("func").split()[-1]
            if not re.match(self.skipListRegExp, frame):
                structured = {"function": frame, "offset": stackFrame.group("offset")}
                if file_ := stackFrame.group("file"):
                    structured["file"] = file_
                if line_ := stackFrame.group("line"):
                    structured["line"] = int(line_)
                if col := stackFrame.group("col"):
                    structured["column"] = int(col)
                self._recordFrame(frame, structured)
            return line

        sysLibStackFrame = self.sysLibStackFrameRegExp.match(line)
        if sysLibStackFrame:
            # System library stack frames will never match the skip list,
            # so don't bother checking if they do.
            module = sysLibStackFrame.group("module")
            structured = {
                "module": module,
                "offset": sysLibStackFrame.group("offset"),
                "module_offset": sysLibStackFrame.group("modoffset"),
            }
            self._recordFrame(module, structured)

        # If we don't match either of these, just ignore the frame.
        # We'll end up with "unknown stack" if everything is ignored.
        return line

    def process(self):
        failures = 0

        if self.allowAll:
            self.logger.info("LeakSanitizer | Leak checks disabled")
            return

        if self.summaryData:
            allowed = all(leak[2] for leak in self.foundLeaks)
            self.logger.lsan_summary(*self.summaryData, allowed=allowed)
            self.summaryData = None

        if self.fatalError:
            self.logger.error(
                "LeakSanitizer | LeakSanitizer has encountered a fatal error."
            )
            failures += 1

        if self.symbolizerError:
            self.logger.error(
                "LeakSanitizer | LLVMSymbolizer was unable to allocate memory.\n"
                "This will cause leaks that "
                "should be ignored to instead be reported as an error"
            )
            failures += 1

        if self.foundLeaks:
            self.logger.info(
                "LeakSanitizer | To show the "
                "addresses of leaked objects add report_objects=1 to LSAN_OPTIONS\n"
                "This can be done in testing/mozbase/mozrunner/mozrunner/utils.py"
            )
            self.logger.info("Allowed depth was %d" % self.maxNumRecordedFrames)

            for (
                frames,
                stack,
                allowed,
                kind,
                nbytes,
                nobjects,
            ) in self.foundLeaks:
                self.logger.lsan_leak(
                    frames,
                    kind,
                    nbytes,
                    nobjects,
                    stack=list(stack),
                    scope=self.scope,
                    allowed_match=allowed,
                )
                if not allowed:
                    failures += 1

        if self.sawError and not (
            self.summaryData
            or self.foundLeaks
            or self.fatalError
            or self.symbolizerError
        ):
            self.logger.error(
                "LeakSanitizer | Memory leaks detected but no leak report generated"
            )

        self.sawError = False

        return failures

    def _finishStack(self):
        if self.recordMoreFrames and len(self.currStack) == 0:
            self.currStack = ["unknown stack"]
            self.currStructuredStack = [{"function": "unknown stack"}]
        if self.currStack:
            self.foundLeaks.append((
                tuple(self.currStack),
                tuple(self.currStructuredStack),
                self.allowedMatch,
                self.currKind,
                self.currBytes,
                self.currObjects,
            ))
            self.currStack = None
            self.currStructuredStack = None
            self.allowedMatch = None
            self.currKind = None
            self.currBytes = None
            self.currObjects = None
        self.recordMoreFrames = False
        self.numRecordedFrames = 0

    def _recordFrame(self, frame, structured):
        # Only currStack is capped: it drives allow-list matching, where a
        # deeper stack could spuriously match an allow-list entry on an
        # allocator wrapper. currStructuredStack is uncapped so profile
        # markers can show the full leak stack.
        frame = self._cleanFrame(frame)
        structured["function"] = self._cleanFrame(structured.get("function", frame))
        self.currStructuredStack.append(structured)
        if self.numRecordedFrames < self.maxNumRecordedFrames:
            if self.allowedMatch is None and self.allowedRegexp is not None:
                self.allowedMatch = frame if self.allowedRegexp.match(frame) else None
            self.currStack.append(frame)
            self.numRecordedFrames += 1

    def _cleanFrame(self, frame):
        # Rust frames aren't properly demangled and in particular can contain
        # some trailing junk of the form ::h[a-f0-9]{16} that changes with
        # compiler versions; see bug 1507350.
        return self.rustRegexp.sub("", frame)
