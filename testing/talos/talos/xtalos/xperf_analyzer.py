# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import csv
import os
import re
import subprocess
from abc import ABCMeta, abstractmethod
from collections import deque
from uuid import UUID

# This constant must match the event declared in
# toolkit/components/startup/mozprofilerprobe.mof
EVENT_ID_FIREFOX_WINDOW_RESTORED = "{917B96B1-ECAD-4DAB-A760-8D49027748AE}"


class XPerfSession:
    """This class encapsulates data that is retained for the term of the xperf
    analysis. This includes the set of attributes, the set of events that are
    owned by those attributes, and the mapping of field names to row indices.
    """

    def __init__(self):
        self.attrs = set()
        self.evtkey = dict()
        self.evtset = set()

    def is_empty(self):
        return not self.attrs

    def add_field_mapping(self, event_name, field_mapping):
        self.evtkey[event_name] = field_mapping

    def get_field_index(self, event_name, field_name):
        return self.evtkey[event_name][field_name]

    def add_attr(self, attr):
        self.evtset.update(attr.get_events())
        self.attrs.add(attr)

    def remove_attr_events(self, attr):
        self.evtset.difference_update(attr.get_events())

    def remove_event(self, evt):
        self.evtset.remove(evt)

    def remove_attr(self, attr):
        self.attrs.remove(attr)

    def match_events(self, row):
        # Make a shallow copy because events will mutate the event set
        local_evtset = self.evtset.copy()
        for e in local_evtset:
            e.do_match(row)


class XPerfAttribute(metaclass=ABCMeta):
    """Base class for all attributes. Each attribute has one or more events
    that are associated with it. When those events fire, the attribute
    accumulates statistics for those events.

    Once all events for the attribute have fired, the attribute considers
    itself to have completed, at which point its results may be retrieved. Note
    that persistent attributes are an exception to this (see __init__).
    """

    # Keys for the dict returned by get_results:

    # Key whose value should be a dict containing any statistics that were
    # accumulated by this attribute.
    ACCUMULATIONS = "XPerfAttribute.ACCUMULATIONS"

    # The class name of this attribute.
    NAME = "XPerfAttribute.NAME"

    # The primary result of the attribute.
    RESULT = "XPerfAttribute.RESULT"

    # Some attributes may themselves act as containers for other attributes.
    # The results of those contained attributes should be added to a dict that
    # is indexed by this key.
    SUB_ATTRIBUTES = "XPerfAttribute.SUB_ATTRIBUTES"

    # Other constants:
    NON_PERSISTENT = False
    PERSISTENT = True

    def __init__(self, events, persistent=NON_PERSISTENT, **kwargs):
        """Positional arguments:

        events -- a list containing one or more events that will be associated
                  with the attribute.

        Keyword arguments:

        persistent -- either XPerfAttribute.PERSISTENT or
                      XPerfAttribute.NON_PERSISTENT. Non-persistent attributes
                      retire their events as the events occur. The attributes
                      consider themselves to have completed once all of their
                      events have been retired. Persistent attributes never
                      retire their events. This is useful for writing
                      attributes that must accumulate data from an indefinite
                      number of events. Once example scenario would be
                      implementing a counter of file I/O events; we don't want
                      to retire after the first file I/O event is encountered;
                      we want to continue counting the events until the end of
                      the analysis.

        output -- an optional function that accepts a single argument that will
                  be a reference to the attribute itself. This function will be
                  called as soon as the attribute's results are available.
        """
        for e in events:
            e.set_attr(self)
        self.evtlist = events
        self.seen_evtlist = []
        self.persistent = persistent
        try:
            self.output = kwargs["output"]
        except KeyError:
            self.output = lambda a: None

    def get_events(self):
        return self.evtlist

    def is_persistent(self):
        return self.persistent

    def set_session(self, sess):
        if sess:
            sess.add_attr(self)
        else:
            self.sess.remove_attr_events(self)
        self.sess = sess

    def get_field_index(self, key, field):
        return self.sess.get_field_index(key, field)

    def on_event_matched(self, evt):
        """Attributes that override this method should always call super().

        This method is called any time one of the attribute's events matches
        the current event, which is passed in as the evt parameter.
        """
        if evt not in self.evtlist:
            raise Exception(
                f'Event mismatch: "{evt!s}" is not in this ' + "attribute's event list"
            )

        self.accumulate(evt)

        # Persistent attributes never retire their events
        if self.persistent:
            return

        self.remove_event(evt)

        if self.evtlist:
            # Propagate the whiteboard from the current event to the next
            self.evtlist[0].set_whiteboard(evt.get_whiteboard())
        else:
            self.do_process()

    def remove_event(self, evt):
        self.evtlist.remove(evt)
        self.seen_evtlist.append(evt)
        self.sess.remove_event(evt)

    def do_process(self):
        self.sess.remove_attr(self)
        self.process()
        self.output(self)

    def accumulate(self, evt):
        """Optional method that an attribute may implement for the purposes
        of accumulating data about multiple events.
        """
        pass

    @abstractmethod
    def process(self):
        """This method is called once all of the attribute's events have been
        retired.
        """
        pass

    @abstractmethod
    def get_results(self):
        """This method is used to retrieve the attibute's results. It returns
        a dict whose keys are any of the constants declared at the top of this
        class. At the very least, the XPerfAttribute.NAME and
        XPerfAttribute.RESULT keys must be set.
        """
        pass


class XPerfInterval(XPerfAttribute):
    """This attribute computes the duration of time between a start event and
    and end event. It also accepts sub-attributes which are only active for the
    duration of the interval.
    """

    def __init__(self, startevt, endevt, attrs=None, **kwargs):
        super().__init__([startevt, endevt], **kwargs)
        if not attrs:
            self.attrs_during_interval = []
        elif isinstance(attrs, list):
            self.attrs_during_interval = attrs
        else:
            self.attrs_during_interval = [attrs]

    def on_event_matched(self, evt):
        if evt == self.evtlist[0]:
            # When we see our start event, we need to activate our
            # sub-attributes by setting their session to the same as ours.
            for a in self.attrs_during_interval:
                a.set_session(self.sess)
        elif evt == self.evtlist[-1]:
            # When we see our end event, we need to deactivate our
            # sub-attributes by setting their session to None.
            for a in self.attrs_during_interval:
                a.set_session(None)
        super().on_event_matched(evt)

    def process(self):
        # Propagate the process call to our sub-attributes
        for a in self.attrs_during_interval:
            a.process()

    def __str__(self):
        if len(self.seen_evtlist) == 0:
            return ""
        end = self.seen_evtlist[-1]
        start = self.seen_evtlist[0]
        duration = end.get_timestamp() - start.get_timestamp()
        msg = (
            f"Interval from [{start!s}] to [{end!s}] took [{duration:.3f}]"
            " milliseconds."
        )
        if self.attrs_during_interval:
            msg += " Within this interval:"
            for attr in self.attrs_during_interval:
                msg += f" {attr!s}"
        msg += f"\nStart: [{start.get_timestamp()}]"
        msg += f" End: [{end.get_timestamp()}]"
        return msg

    def get_results(self):
        """The result of an XPerf interval is the interval's duration, in
        milliseconds. The results of the sub-attributes are also provided.
        """
        if len(self.seen_evtlist) == 0:
            return {}

        end = self.seen_evtlist[-1]
        start = self.seen_evtlist[0]
        duration = end.get_timestamp() - start.get_timestamp()

        sub_attrs = []
        for attr in self.attrs_during_interval:
            sub_attrs.append(attr.get_results())

        results = {
            XPerfAttribute.NAME: self.__class__.__name__,
            XPerfAttribute.RESULT: duration,
        }
        if sub_attrs:
            results[XPerfAttribute.SUB_ATTRIBUTES] = sub_attrs

        return results


class XPerfCounter(XPerfAttribute):
    """This persistent attribute computes the number of occurrences of the
    event specified to __init__. It can also accumulate additional data from
    the events.
    """

    def __init__(self, evt, **kwargs):
        """Positional parameters:

        evt -- The event to be counted.

        Keyword arguments:

        filters -- An optional argument that provides a dictionary that
                   provides filters to be used to screen out unwanted events.
                   Their key points to one of the XPerfEvent constants, and the
                   value is a function that evaluates the corresponding value
                   from the event's whiteboard.
        """
        super().__init__([evt], XPerfAttribute.PERSISTENT, **kwargs)
        self.values = dict()
        self.count = 0
        try:
            self.filters = kwargs["filters"]
        except KeyError:
            self.filters = dict()

    def accumulate(self, evt):
        data = evt.get_whiteboard()

        for key, comp in self.filters.items():
            try:
                testdata = data[key]
            except KeyError:
                pass
            else:
                if not comp(testdata):
                    return

        self.count += 1

        fields = data[XPerfEvent.EVENT_ACCUMULATABLE_FIELDS]

        for f in fields:
            value = data[f]
            try:
                self.values[f] += value
            except KeyError:
                self.values[f] = value

    def process(self):
        self.remove_event(self.evtlist[0])

    def __str__(self):
        msg = f"[{self.count!s}] events of type [{self.seen_evtlist[0]!s}]"
        if self.values:
            msg += " with accumulated"
            for k, v in self.values.items():
                msg += f" [[{k!s}] == {v!s}]"
        return msg

    def get_results(self):
        results = {
            XPerfAttribute.NAME: self.__class__.__name__,
            XPerfAttribute.RESULT: self.count,
        }

        if self.values:
            results[XPerfAttribute.ACCUMULATIONS] = self.values

        return results


class XPerfEvent:
    """Base class for all events. An important feature of this class is the
    whiteboard variable. This variable allows for passing values between
    successive events that are *owned by the same attribute*.

    This allows, for example, a thread ID from a scheduler event to be consumed
    by a subsequent event that only wants to fire for particular thread IDs.
    """

    # These keys are used to reference accumulated data that is passed across
    # events by |self.whiteboard|:

    # The pid recorded by a process or thread related event
    EVENT_DATA_PID = "pid"
    # The command line recorded by a ProcessStart event
    EVENT_DATA_CMD_LINE = "cmd_line"
    # The tid recorded by a thread related event
    EVENT_DATA_TID = "tid"
    # Number of bytes recorded by an event that contains such quantities
    EVENT_NUM_BYTES = "num_bytes"
    # File name recorded by an I/O event
    EVENT_FILE_NAME = "file_name"
    # Set of field names that may be accumulated by an XPerfCounter. The
    # counter uses this to query the whiteboard for other EVENT_* keys that
    # contain values that should be accumulated.
    EVENT_ACCUMULATABLE_FIELDS = "accumulatable_fields"

    timestamp_index = None

    def __init__(self, key):
        self.key = key
        self.whiteboard = dict()

    def set_attr(self, attr):
        self.attr = attr

    def get_attr(self):
        return self.attr

    def set_whiteboard(self, data):
        self.whiteboard = data

    def get_whiteboard(self):
        return self.whiteboard

    def get_field_index(self, field):
        return self.attr.get_field_index(self.key, field)

    def do_match(self, row):
        if not self.match(row):
            return False

        # All events use the same index for timestamps, so timestamp_index can
        # be a class variable.
        if not XPerfEvent.timestamp_index:
            XPerfEvent.timestamp_index = self.get_field_index("TimeStamp")

        # Convert microseconds to milliseconds
        self.timestamp = float(row[XPerfEvent.timestamp_index]) / 1000.0
        self.attr.on_event_matched(self)
        return True

    def match(self, row):
        return self.key == row[0]

    def get_timestamp(self):
        return self.timestamp


class EventExpression(metaclass=ABCMeta):
    """EventExpression is an optional layer that sits between attributes and
    events, and allow the user to compose multiple events into a more complex
    event. To achieve this, EventExpression implementations must implement both
    the XPerfEvent interface (so that their underlying attributes may
    communicate with them), as well as the XPerfAttribute interface, so that
    they present themselves as attributes to the events that run above them.
    """

    def __init__(self, events):
        # Event expressions implement the attribute interface, so for each
        # event, we set ourselves as the underlying attribute
        if isinstance(events, list):
            for e in events:
                e.set_attr(self)
        else:
            events.set_attr(self)

    def set_attr(self, attr):
        self.attr = attr

    def get_attr(self):
        return self.attr

    def get_field_index(self, key, field):
        return self.attr.get_field_index(key, field)

    @abstractmethod
    def set_whiteboard(self, data):
        pass

    @abstractmethod
    def get_whiteboard(self):
        pass

    @abstractmethod
    def on_event_matched(self, evt):
        pass

    @abstractmethod
    def do_match(self, row):
        pass

    @abstractmethod
    def get_timestamp(self):
        pass


class Nth(EventExpression):
    """This is a simple EventExpression that does not fire until the Nth
    occurrence of the event that it encapsulates.
    """

    def __init__(self, N, event):
        super().__init__(event)
        self.event = event
        self.N = N
        self.match_count = 0

    def on_event_matched(self, evt):
        if evt != self.event:
            raise Exception(
                "Nth expression for event "
                + f'"{self.event}" fired for event "{evt}" instead'
            )
        self.match_count += 1
        if self.match_count == self.N:
            self.attr.on_event_matched(self)

    def set_whiteboard(self, data):
        self.event.set_whiteboard(data)

    def get_whiteboard(self):
        return self.event.get_whiteboard()

    def do_match(self, row):
        self.event.do_match(row)

    def get_timestamp(self):
        return self.event.get_timestamp()

    def get_suffix(self):
        lastDigit = str(self.N)[-1]
        if lastDigit == "1":
            return "st"
        elif lastDigit == "2":
            return "nd"
        elif lastDigit == "3":
            return "rd"
        else:
            return "th"

    def __str__(self):
        suffix = self.get_suffix()
        return f"{self.N!s}{suffix} [{self.event!s}]"


class EventSequence(EventExpression):
    """This EventExpression represents a sequence of events that must fire in
    the correct order. Once the final event in the sequence is received, then
    the EventSequence fires itself.

    One interesting point of note is what happens when one of the events passed
    into the EventSequence is persistent. If a peristent event is supplied as
    the final entry in the sequence, and since the persistent event never
    retires itself, the sequence will keep firing every time the persistent
    event fires. This allows the user to provide an event sequence that is
    essentially interpreted as, "once all of these other events have triggered,
    fire this last one repeatedly for the remainder of the analysis."
    """

    def __init__(self, *events):
        super().__init__(list(events))
        if len(events) < 2:
            raise Exception(
                f"EventSequence requires at least two events, {len(events)} provided"
            )
        self.events = deque(events)
        self.seen_events = []

    def on_event_matched(self, evt):
        unseen_events = len(self.events) > 0
        if (
            unseen_events
            and evt != self.events[0]
            or not unseen_events
            and evt != self.seen_events[-1]
        ):
            raise Exception(
                f'Unexpected event "{evt}" is not a member of this event sequence'
            )

        # Move the event from events queue to seen_events
        if unseen_events:
            self.events.popleft()
            self.seen_events.append(evt)

        if self.events:
            # Transfer attr data to the next event that will run
            self.events[0].set_whiteboard(evt.get_whiteboard())
        else:
            # Or else we have run all of our events; notify the attribute
            self.attr.on_event_matched(self)

    def set_whiteboard(self, data):
        self.events[0].set_whiteboard(data)

    def get_whiteboard(self):
        return self.seen_events[-1].get_whiteboard()

    def do_match(self, row):
        if self.attr.is_persistent() and len(self.events) == 0:
            # Persistent attributes may repeatedly match the final event
            self.seen_events[-1].do_match(row)
        else:
            self.events[0].do_match(row)

    def get_timestamp(self):
        return self.seen_events[-1].get_timestamp()

    def __str__(self):
        result = ""
        for e in self.seen_events[:-1]:
            result += f"When [{e!s}], "
        result += f"then [{self.seen_events[-1]!s}]"
        return result


class BindThread(EventExpression):
    """This event expression binds the event that it encapsulates to a
    specific thread ID. This is used to force an event to only fire when it
    matches the thread ID supplied by the whiteboard.
    """

    def __init__(self, event):
        super().__init__(event)
        self.event = event
        self.tid = None

    def on_event_matched(self, evt):
        if evt != self.event:
            raise Exception(
                "BindThread expression for event "
                + f'"{self.event}" fired for event "{evt}" instead'
            )
        self.attr.on_event_matched(self)

    def set_whiteboard(self, data):
        self.tid = data[XPerfEvent.EVENT_DATA_TID]
        self.event.set_whiteboard(data)

    def get_whiteboard(self):
        return self.event.get_whiteboard()

    def do_match(self, row):
        try:
            tid_index = self.get_field_index(row[0], "ThreadID")
        except KeyError:
            # Not every event has a thread ID. We don't care about those.
            return

        if int(row[tid_index]) == self.tid:
            self.event.do_match(row)

    def get_timestamp(self):
        return self.event.get_timestamp()

    def __str__(self):
        return f"[{self.event!s}] bound to thread [{self.tid!s}]"


class ClassicEvent(XPerfEvent):
    """Classic ETW events are differentiated via a GUID. This class
    implements the boilerplate for matching those events.
    """

    guid_index = None

    def __init__(self, guidstr):
        super().__init__("UnknownEvent/Classic")
        self.guid = UUID(guidstr)

    def match(self, row):
        if not super().match(row):
            return False

        if not ClassicEvent.guid_index:
            ClassicEvent.guid_index = self.get_field_index("EventGuid")

        guid = UUID(row[ClassicEvent.guid_index])
        return guid.int == self.guid.int

    def __str__(self):
        return f"User event (classic): [{{{self.guid!s}}}]"


class SessionStoreWindowRestored(ClassicEvent):
    """The Firefox session store window restored event"""

    def __init__(self):
        super().__init__(EVENT_ID_FIREFOX_WINDOW_RESTORED)

    def __str__(self):
        return "Firefox Session Store Window Restored"


class ProcessStart(XPerfEvent):
    cmd_line_index = None
    process_index = None
    extractor = re.compile(r"^(.+) \(\s*(\d+)\)$")

    def __init__(self, leafname):
        super().__init__("P-Start")
        self.leafname = leafname.lower()

    @staticmethod
    def tokenize_cmd_line(cmd_line_str):
        result = []
        quoted = False
        current = ""

        for c in cmd_line_str:
            if quoted:
                if c == '"':
                    quoted = False
            elif c == '"':
                quoted = True
            elif c == " ":
                result.append(current)
                current = ""
                continue

            current += c

        # Capture the final token
        if current:
            result.append(current)

        return [t.strip('"') for t in result]

    def match(self, row):
        if not super().match(row):
            return False

        if not ProcessStart.process_index:
            ProcessStart.process_index = self.get_field_index("Process Name ( PID)")

        m = ProcessStart.extractor.match(row[ProcessStart.process_index])
        executable = m.group(1).lower()

        if executable != self.leafname:
            return False

        pid = int(m.group(2))

        if not ProcessStart.cmd_line_index:
            ProcessStart.cmd_line_index = self.get_field_index("Command Line")

        cmd_line = row[ProcessStart.cmd_line_index]
        cmd_line_tokens = ProcessStart.tokenize_cmd_line(cmd_line)

        self.whiteboard[XPerfEvent.EVENT_DATA_PID] = pid

        try:
            cmd_line_dict = self.whiteboard[XPerfEvent.EVENT_DATA_CMD_LINE]
        except KeyError:
            self.whiteboard[XPerfEvent.EVENT_DATA_CMD_LINE] = {pid: cmd_line_tokens}
        else:
            cmd_line_dict[pid] = cmd_line_tokens

        return True

    def __str__(self):
        return f"Start of a [{self.leafname!s}] process"


class ThreadStart(XPerfEvent):
    """ThreadStart only fires for threads whose process matches the
    XPerfEvent.EVENT_DATA_PID entry in the whiteboard.
    """

    process_index = None
    tid_index = None
    pid_extractor = re.compile(r"^.+ \(\s*(\d+)\)$")

    def __init__(self):
        super().__init__("T-Start")

    def match(self, row):
        if not super().match(row):
            return False

        if not ThreadStart.process_index:
            ThreadStart.process_index = self.get_field_index("Process Name ( PID)")

        m = ThreadStart.pid_extractor.match(row[ThreadStart.process_index])
        if self.whiteboard[XPerfEvent.EVENT_DATA_PID] != int(m.group(1)):
            return False

        if not ThreadStart.tid_index:
            ThreadStart.tid_index = self.get_field_index("ThreadID")

        self.whiteboard[XPerfEvent.EVENT_DATA_TID] = int(row[ThreadStart.tid_index])
        return True

    def __str__(self):
        s = f"Thread start in process [{self.whiteboard[XPerfEvent.EVENT_DATA_PID]}]"
        return s


class ReadyThread(XPerfEvent):
    """ReadyThread only fires for the last thread whose ID was recorded in the
    whiteboard via the XPerfEvent.EVENT_DATA_TID key.
    """

    tid_index = None

    def __init__(self):
        super().__init__("ReadyThread")

    def set_whiteboard(self, data):
        super().set_whiteboard(data)

    def match(self, row):
        if not super().match(row):
            return False

        if not ReadyThread.tid_index:
            ReadyThread.tid_index = self.get_field_index("Rdy TID")

        try:
            return self.whiteboard[XPerfEvent.EVENT_DATA_TID] == int(
                row[ReadyThread.tid_index]
            )
        except KeyError:
            return False

    def __str__(self):
        return f"Thread [{self.whiteboard[XPerfEvent.EVENT_DATA_TID]!s}] is ready"


class ContextSwitchToThread(XPerfEvent):
    """ContextSwitchToThread only fires for the last thread whose ID was
    recorded in the whiteboard via the XPerfEvent.EVENT_DATA_TID key.
    """

    tid_index = None

    def __init__(self):
        super().__init__("CSwitch")

    def match(self, row):
        if not super().match(row):
            return False

        if not ContextSwitchToThread.tid_index:
            ContextSwitchToThread.tid_index = self.get_field_index("New TID")

        try:
            return self.whiteboard[XPerfEvent.EVENT_DATA_TID] == int(
                row[ContextSwitchToThread.tid_index]
            )
        except KeyError:
            return False

    def __str__(self):
        return (
            "Context switch to thread "
            + f"[{self.whiteboard[XPerfEvent.EVENT_DATA_TID]!s}]"
        )


class FileIOReadOrWrite(XPerfEvent):
    READ = 0
    WRITE = 1

    tid_index = None
    num_bytes_index = None
    file_name_index = None

    def __init__(self, verb):
        if verb == FileIOReadOrWrite.WRITE:
            evt_name = "FileIoWrite"
            self.strverb = "Write"
        elif verb == FileIOReadOrWrite.READ:
            evt_name = "FileIoRead"
            self.strverb = "Read"
        else:
            raise Exception("Invalid verb argument to FileIOReadOrWrite")

        super().__init__(evt_name)

        self.verb = verb

    def match(self, row):
        if not super().match(row):
            return False

        if not FileIOReadOrWrite.tid_index:
            FileIOReadOrWrite.tid_index = self.get_field_index("ThreadID")

        if not FileIOReadOrWrite.num_bytes_index:
            FileIOReadOrWrite.num_bytes_index = self.get_field_index("Size")

        if not FileIOReadOrWrite.file_name_index:
            FileIOReadOrWrite.file_name_index = self.get_field_index("FileName")

        self.whiteboard[XPerfEvent.EVENT_DATA_TID] = int(
            row[FileIOReadOrWrite.tid_index]
        )
        self.whiteboard[XPerfEvent.EVENT_NUM_BYTES] = int(
            row[FileIOReadOrWrite.num_bytes_index], 0
        )
        self.whiteboard[XPerfEvent.EVENT_FILE_NAME] = row[
            FileIOReadOrWrite.file_name_index
        ].strip('"')
        self.whiteboard[XPerfEvent.EVENT_ACCUMULATABLE_FIELDS] = {
            XPerfEvent.EVENT_NUM_BYTES
        }

        return True

    def __str__(self):
        return f"File I/O Bytes {self.strverb}"


class XPerfFile:
    """This class is the main entry point into xperf analysis. The user should
    create one or more attributes, add them via add_attr(), and then call
    analyze() to run.
    """

    def __init__(self, xperf_path=None, debug=False, **kwargs):
        """Keyword arguments:

        debug -- When True, enables additional diagnostics
        etlfile -- Path to a merged .etl file to use for the analysis.
        etluser -- Path a a user-mode .etl file to use for the analysis. It
                   will be merged with the supplied kernel-mode .etl file
                   before running the analysis.
        etlkernel -- Path to a kernel-mode .etl file to use for the analysis.
                     It will be merged with the supplied user-mode .etl file
                     before running the analysis.
        csvfile -- Path to a CSV file that was previously exported using xperf.
                   This file will be used for the analysis.
        csvout -- When used with either the etlfile option or the (etluser and
                  etlkernel) option, specifies the path to use for the exported
                  CSV file.
        keepcsv -- When true, any CSV file generated during the analysis will
                   be left on the file system. Otherwise, the CSV file will be
                   removed once the analysis is complete.
        xperf_path -- Absolute path to xperf.exe. When absent, XPerfFile will
                      attempt to resolve xperf via the system PATH.
        """

        self.csv_fd = None
        self.csvfile = None
        self.csvout = None
        self.debug = debug
        self.etlfile = None
        self.keepcsv = False
        self.xperf_path = xperf_path

        if "etlfile" in kwargs:
            self.etlfile = os.path.abspath(kwargs["etlfile"])
        elif "etluser" in kwargs and "etlkernel" in kwargs:
            self.etlfile = self.etl_merge_user_kernel(**kwargs)
        elif "csvfile" not in kwargs:
            raise Exception("Missing parameters: etl or csv files required")

        if self.etlfile:
            try:
                self.csvout = os.path.abspath(kwargs["csvout"])
            except KeyError:
                pass
            self.csvfile = self.etl2csv()
        else:
            self.csvfile = os.path.abspath(kwargs["csvfile"])

        try:
            self.keepcsv = kwargs["keepcsv"]
        except KeyError:
            # If we've been supplied a csvfile, assume by default that we don't
            # want that file deleted by us.
            self.keepcsv = "csvfile" in kwargs

        self.sess = XPerfSession()

    def add_attr(self, attr):
        attr.set_session(self.sess)

    def get_xperf_path(self):
        if self.xperf_path:
            return self.xperf_path

        leaf_name = "xperf.exe"
        access_flags = os.R_OK | os.X_OK
        path_entries = os.environ["PATH"].split(os.pathsep)
        for entry in path_entries:
            full = os.path.join(entry, leaf_name)
            if os.access(full, access_flags):
                self.xperf_path = os.path.abspath(full)
                return self.xperf_path

        raise Exception("Cannot find xperf")

    def etl_merge_user_kernel(self, **kwargs):
        user = os.path.abspath(kwargs["etluser"])
        kernel = os.path.abspath(kwargs["etlkernel"])
        (base, leaf) = os.path.split(user)
        merged = os.path.join(base, "merged.etl")

        xperf_cmd = [self.get_xperf_path(), "-merge", user, kernel, merged]
        if self.debug:
            print(f"Executing '{subprocess.list2cmdline(xperf_cmd)}'")
        subprocess.call(xperf_cmd)
        return merged

    def etl2csv(self):
        if self.csvout:
            abs_csv_name = self.csvout
        else:
            (base, leaf) = os.path.split(self.etlfile)
            (leaf, ext) = os.path.splitext(leaf)
            abs_csv_name = os.path.join(base, f"{leaf}.csv")

        xperf_cmd = [self.get_xperf_path(), "-i", self.etlfile, "-o", abs_csv_name]
        if self.debug:
            print(f"Executing '{subprocess.list2cmdline(xperf_cmd)}'")
        subprocess.call(xperf_cmd)
        return abs_csv_name

    def __enter__(self):
        if not self.load():
            raise Exception("Load failed")
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.csv_fd:
            self.csv_fd.close()
        if not self.csvout and not self.keepcsv:
            os.remove(self.csvfile)

    def load(self):
        if not self.csvfile:
            return False

        self.csv_fd = open(self.csvfile, "rb")
        self.data = self.filter_xperf_header(
            csv.reader(
                self.csv_fd,
                delimiter=",",
                quotechar='"',
                quoting=csv.QUOTE_NONE,
                skipinitialspace=True,
            )
        )

        return True

    def filter_xperf_header(self, csvdata):
        XPERF_CSV_NO_HEADER = -1
        XPERF_CSV_IN_HEADER = 0
        XPERF_CSV_END_HEADER_SEEN = 1
        XPERF_CSV_PAST_HEADER = 2

        state = XPERF_CSV_NO_HEADER

        while True:
            try:
                row = next(csvdata)
            except StopIteration:
                break
            except csv.Error:
                continue

            if not row:
                continue

            if state < XPERF_CSV_IN_HEADER:
                if row[0] == "BeginHeader":
                    state = XPERF_CSV_IN_HEADER
                continue

            if state == XPERF_CSV_IN_HEADER:
                if row[0] == "EndHeader":
                    state = XPERF_CSV_END_HEADER_SEEN
                    continue

                # Map field names to indices
                self.sess.add_field_mapping(
                    row[0], {v: k + 1 for k, v in enumerate(row[1:])}
                )
                continue

            if state >= XPERF_CSV_END_HEADER_SEEN:
                state += 1

            if state > XPERF_CSV_PAST_HEADER:
                yield row

    def analyze(self):
        for row in self.data:
            self.sess.match_events(row)
            if self.sess.is_empty():
                # No more attrs to look for, we might as well quit
                return


if __name__ == "__main__":

    def main():
        import argparse

        parser = argparse.ArgumentParser()
        subparsers = parser.add_subparsers()

        etl_parser = subparsers.add_parser(
            "etl", help="Input consists of one .etl file"
        )
        etl_parser.add_argument(
            "etlfile",
            type=str,
            help="Path to a single .etl containing merged kernel "
            + "and user mode data",
        )
        etl_parser.add_argument(
            "--csvout",
            required=False,
            help="Specify a path to save the interim csv file to disk",
        )
        etl_parser.add_argument(
            "--keepcsv",
            required=False,
            help="Do not delete the interim csv file that was written to disk",
            action="store_true",
        )

        etls_parser = subparsers.add_parser(
            "etls", help="Input consists of two .etl files"
        )
        etls_parser.add_argument(
            "--user",
            type=str,
            help="Path to a user-mode .etl file",
            dest="etluser",
            required=True,
        )
        etls_parser.add_argument(
            "--kernel",
            type=str,
            help="Path to a kernel-mode .etl file",
            dest="etlkernel",
            required=True,
        )
        etls_parser.add_argument(
            "--csvout",
            required=False,
            help="Specify a path to save the interim csv file to disk",
        )
        etls_parser.add_argument(
            "--keepcsv",
            required=False,
            help="Do not delete the interim csv file that was written to disk",
            action="store_true",
        )

        csv_parser = subparsers.add_parser(
            "csv", help="Input consists of one .csv file"
        )
        csv_parser.add_argument(
            "csvfile", type=str, help="Path to a .csv file generated by xperf"
        )
        # We always imply --keepcsv when running in csv mode
        csv_parser.add_argument(
            "--keepcsv",
            required=False,
            help=argparse.SUPPRESS,
            action="store_true",
            default=True,
        )

        args = parser.parse_args()

        # This is merely sample code for running analyses.

        with XPerfFile(**vars(args)) as etl:

            def null_output(attr):
                pass

            def structured_output(attr):
                print(f"Results: [{attr.get_results()!r}]")

            def test_filter_exclude_dll(file):
                (base, ext) = os.path.splitext(file)
                return ext.lower() != ".dll"

            myfilters = {XPerfEvent.EVENT_FILE_NAME: test_filter_exclude_dll}

            fxstart1 = ProcessStart("firefox.exe")
            sess_restore = SessionStoreWindowRestored()
            interval1 = XPerfInterval(
                fxstart1, sess_restore, output=lambda a: print(str(a))
            )
            etl.add_attr(interval1)

            fxstart2 = ProcessStart("firefox.exe")
            ready = EventSequence(
                Nth(2, ProcessStart("firefox.exe")), ThreadStart(), ReadyThread()
            )
            interval2 = XPerfInterval(fxstart2, ready, output=structured_output)
            etl.add_attr(interval2)

            browser_main_thread_file_io_read = EventSequence(
                Nth(2, ProcessStart("firefox.exe")),
                ThreadStart(),
                BindThread(FileIOReadOrWrite(FileIOReadOrWrite.READ)),
            )
            read_counter = XPerfCounter(
                browser_main_thread_file_io_read,
                output=structured_output,
                filters=myfilters,
            )

            browser_main_thread_file_io_write = EventSequence(
                Nth(2, ProcessStart("firefox.exe")),
                ThreadStart(),
                BindThread(FileIOReadOrWrite(FileIOReadOrWrite.WRITE)),
            )
            write_counter = XPerfCounter(
                browser_main_thread_file_io_write, output=structured_output
            )

            # This is equivalent to the old-style xperf test (with launcher)
            parent_process_started = Nth(2, ProcessStart("firefox.exe"))
            interval3 = XPerfInterval(
                parent_process_started,
                SessionStoreWindowRestored(),
                read_counter,
                output=structured_output,
            )
            etl.add_attr(interval3)

            parent_process_started2 = Nth(2, ProcessStart("firefox.exe"))
            interval4 = XPerfInterval(
                parent_process_started2,
                SessionStoreWindowRestored(),
                write_counter,
                output=structured_output,
            )
            etl.add_attr(interval4)

            etl.analyze()

    main()
