import csv
import io
import os
import runpy
import sys
from unittest import mock

import mozunit
import pytest

from talos.xtalos import xperf_analyzer
from talos.xtalos.xperf_analyzer import (
    EVENT_ID_FIREFOX_WINDOW_RESTORED,
    BindThread,
    ClassicEvent,
    ContextSwitchToThread,
    EventExpression,
    EventSequence,
    FileIOReadOrWrite,
    Nth,
    ProcessStart,
    ReadyThread,
    SessionStoreWindowRestored,
    ThreadStart,
    XPerfAttribute,
    XPerfCounter,
    XPerfEvent,
    XPerfFile,
    XPerfInterval,
    XPerfSession,
)

HEADER = """\
junk before the header
BeginHeader
P-Start, TimeStamp, Process Name ( PID), ParentPID, Command Line
T-Start, TimeStamp, Process Name ( PID), ThreadID
ReadyThread, TimeStamp, Process Name ( PID), Rdy TID
CSwitch, TimeStamp, New Process Name ( PID), New TID
FileIoRead, TimeStamp, Process Name ( PID), ThreadID, Size, FileName
FileIoWrite, TimeStamp, Process Name ( PID), ThreadID, Size, FileName
UnknownEvent/Classic, TimeStamp, Process Name ( PID), ThreadID, EventGuid
EndHeader
this line right after the header is dropped
"""

RESTORED = EVENT_ID_FIREFOX_WINDOW_RESTORED
OTHER_GUID = "{26D1E091-0AE7-4F49-A554-4214445C505C}"

CACHED_INDICES = [
    (XPerfEvent, "timestamp_index"),
    (ClassicEvent, "guid_index"),
    (ProcessStart, "cmd_line_index"),
    (ProcessStart, "process_index"),
    (ThreadStart, "process_index"),
    (ThreadStart, "tid_index"),
    (ReadyThread, "tid_index"),
    (ContextSwitchToThread, "tid_index"),
    (FileIOReadOrWrite, "tid_index"),
    (FileIOReadOrWrite, "num_bytes_index"),
    (FileIOReadOrWrite, "file_name_index"),
]


@pytest.fixture(autouse=True)
def reset_cached_indices():
    for cls, name in CACHED_INDICES:
        setattr(cls, name, None)
    yield
    for cls, name in CACHED_INDICES:
        setattr(cls, name, None)


def make_file(tmp_path):
    csv_path = tmp_path / "input.csv"
    csv_path.write_text("")
    return XPerfFile(csvfile=str(csv_path))


def feed(xperf, body):
    reader = csv.reader(
        io.StringIO(HEADER + body),
        delimiter=",",
        quotechar='"',
        quoting=csv.QUOTE_NONE,
        skipinitialspace=True,
    )
    xperf.data = xperf.filter_xperf_header(reader)
    xperf.analyze()


class _RaisingReader:
    def __init__(self, items):
        self.items = list(items)

    def __iter__(self):
        return self

    def __next__(self):
        if not self.items:
            raise StopIteration
        item = self.items.pop(0)
        if isinstance(item, Exception):
            raise item
        return item


def test_filter_xperf_header_maps_fields_and_skips_noise(tmp_path):
    xperf = make_file(tmp_path)
    reader = _RaisingReader([
        ["noise"],
        ["BeginHeader"],
        ["P-Start", "TimeStamp", "Process Name ( PID)"],
        csv.Error("bad row"),
        [],
        ["EndHeader"],
        ["dropped"],
        ["P-Start", "1", "a ( 1)"],
        ["P-Start", "2", "b ( 2)"],
    ])

    rows = list(xperf.filter_xperf_header(reader))

    assert rows == [["P-Start", "1", "a ( 1)"], ["P-Start", "2", "b ( 2)"]]
    assert xperf.sess.get_field_index("P-Start", "TimeStamp") == 1
    assert xperf.sess.get_field_index("P-Start", "Process Name ( PID)") == 2


def test_interval_process_start_to_window_restored(tmp_path):
    xperf = make_file(tmp_path)
    outputs = []
    interval = XPerfInterval(
        ProcessStart("Firefox.exe"),
        SessionStoreWindowRestored(),
        output=outputs.append,
    )
    xperf.add_attr(interval)

    feed(
        xperf,
        "P-Start, 500, other.exe ( 9), 1, other.exe\n"
        "UnknownEvent/Classic, 600, firefox.exe ( 10), 3, " + OTHER_GUID + "\n"
        'P-Start, 1000, firefox.exe ( 10), 1, "C:\\fx\\firefox.exe" -foreground\n'
        "UnknownEvent/Classic, 2000, firefox.exe ( 10), 3, " + OTHER_GUID + "\n"
        "UnknownEvent/Classic, 4500, firefox.exe ( 10), 3, " + RESTORED + "\n"
        "UnknownEvent/Classic, 9000, firefox.exe ( 10), 3, " + RESTORED + "\n",
    )

    assert outputs == [interval]
    assert xperf.sess.is_empty()
    results = interval.get_results()
    assert results == {
        XPerfAttribute.NAME: "XPerfInterval",
        XPerfAttribute.RESULT: pytest.approx(3.5),
    }
    text = str(interval)
    assert "Start of a [firefox.exe] process" in text
    assert "Firefox Session Store Window Restored" in text
    assert "took [3.500] milliseconds." in text
    assert "Start: [1.0] End: [4.5]" in text
    assert "Within this interval" not in text
    assert interval.seen_evtlist[0].get_whiteboard() == {
        XPerfEvent.EVENT_DATA_PID: 10,
        XPerfEvent.EVENT_DATA_CMD_LINE: {10: ["C:\\fx\\firefox.exe", "-foreground"]},
    }


def test_interval_without_events_has_no_results():
    interval = XPerfInterval(ProcessStart("firefox.exe"), ReadyThread())
    assert interval.get_results() == {}
    assert str(interval) == ""
    assert interval.attrs_during_interval == []


def test_interval_wraps_single_sub_attribute():
    inner = XPerfInterval(ThreadStart(), ReadyThread())
    outer = XPerfInterval(ProcessStart("firefox.exe"), ReadyThread(), inner)
    assert outer.attrs_during_interval == [inner]


def test_nested_interval_and_thread_sequence(tmp_path):
    xperf = make_file(tmp_path)
    inner = XPerfInterval(
        ProcessStart("firefox.exe"),
        EventSequence(ThreadStart(), ContextSwitchToThread()),
    )
    outer = XPerfInterval(
        ProcessStart("firefox.exe"),
        EventSequence(ThreadStart(), ReadyThread()),
        [inner],
    )
    xperf.add_attr(outer)

    feed(
        xperf,
        "P-Start, 1000, firefox.exe ( 20), 1, firefox.exe\n"
        "P-Start, 1500, firefox.exe ( 20), 1, firefox.exe\n"
        "T-Start, 2000, other.exe ( 99), 5\n"
        "T-Start, 3000, firefox.exe ( 20), 7\n"
        "CSwitch, 3500, firefox.exe ( 20), 8\n"
        "ReadyThread, 3600, firefox.exe ( 20), 8\n"
        "CSwitch, 4000, firefox.exe ( 20), 7\n"
        "ReadyThread, 6000, firefox.exe ( 20), 7\n",
    )

    inner_results = inner.get_results()
    assert inner_results[XPerfAttribute.RESULT] == pytest.approx(2.5)
    results = outer.get_results()
    assert results[XPerfAttribute.RESULT] == pytest.approx(5.0)
    assert results[XPerfAttribute.SUB_ATTRIBUTES] == [inner_results]
    text = str(outer)
    assert "Within this interval:" in text
    assert "When [Thread start in process [20]], then [Thread [7] is ready]" in text
    assert "then [Context switch to thread [7]]" in text


def test_counter_on_main_thread_file_io(tmp_path):
    xperf = make_file(tmp_path)
    outputs = []
    sequence = EventSequence(
        Nth(2, ProcessStart("firefox.exe")),
        ThreadStart(),
        BindThread(FileIOReadOrWrite(FileIOReadOrWrite.READ)),
    )
    counter = XPerfCounter(
        sequence,
        output=outputs.append,
        filters={
            XPerfEvent.EVENT_FILE_NAME: lambda f: not f.endswith(".dll"),
            "absent_key": lambda v: False,
        },
    )
    xperf.add_attr(counter)

    feed(
        xperf,
        "P-Start, 1000, firefox.exe ( 30), 1, firefox.exe\n"
        "P-Start, 1100, firefox.exe ( 31), 30, firefox.exe -contentproc\n"
        "T-Start, 1200, firefox.exe ( 31), 40\n"
        'FileIoRead, 1300, firefox.exe ( 31), 40, 0x10, "C:\\a.txt"\n'
        'FileIoRead, 1400, firefox.exe ( 31), 41, 0x100, "C:\\b.txt"\n'
        'FileIoRead, 1500, firefox.exe ( 31), 40, 0x1000, "C:\\c.dll"\n'
        "P-Start, 1550, firefox.exe ( 32), 30, firefox.exe\n"
        'FileIoRead, 1600, firefox.exe ( 31), 40, 0x20, "C:\\d.txt"\n',
    )

    assert counter.get_results() == {
        XPerfAttribute.NAME: "XPerfCounter",
        XPerfAttribute.RESULT: 2,
        XPerfAttribute.ACCUMULATIONS: {XPerfEvent.EVENT_NUM_BYTES: 0x30},
    }
    pid_cmd_lines = sequence.get_whiteboard()[XPerfEvent.EVENT_DATA_CMD_LINE]
    assert pid_cmd_lines[30] == ["firefox.exe"]
    assert pid_cmd_lines[31] == ["firefox.exe", "-contentproc"]

    counter.process()
    assert not xperf.sess.evtset
    text = str(counter)
    assert text.startswith("[2] events of type [When [2nd [Start of a [firefox.exe]")
    assert "then [[File I/O Bytes Read] bound to thread [40]]" in text
    assert "with accumulated [[num_bytes] == 48]" in text
    assert outputs == []


def test_counter_without_events(tmp_path):
    xperf = make_file(tmp_path)
    counter = XPerfCounter(FileIOReadOrWrite(FileIOReadOrWrite.WRITE))
    xperf.add_attr(counter)

    feed(xperf, 'FileIoRead, 1000, firefox.exe ( 1), 2, 0x8, "C:\\r.txt"\n')

    counter.process()
    assert counter.get_results() == {
        XPerfAttribute.NAME: "XPerfCounter",
        XPerfAttribute.RESULT: 0,
    }
    assert str(counter) == "[0] events of type [File I/O Bytes Write]"


def test_attribute_rejects_foreign_event():
    interval = XPerfInterval(ProcessStart("firefox.exe"), ReadyThread())
    with pytest.raises(Exception, match="Event mismatch"):
        XPerfAttribute.on_event_matched(interval, FileIOReadOrWrite(0))


def test_attribute_without_output_and_base_methods():
    interval = XPerfInterval(ProcessStart("firefox.exe"), ReadyThread())
    assert interval.output(interval) is None
    assert interval.is_persistent() is XPerfAttribute.NON_PERSISTENT
    assert XPerfAttribute.accumulate(interval, None) is None
    assert XPerfAttribute.process(interval) is None
    assert XPerfAttribute.get_results(interval) is None


class _StubAttr:
    def __init__(self, mapping):
        self.mapping = mapping
        self.matched = []

    def get_field_index(self, key, field):
        return self.mapping[key][field]

    def on_event_matched(self, evt):
        self.matched.append(evt)

    def is_persistent(self):
        return False


def test_event_matches_own_name_and_converts_timestamp():
    evt = XPerfEvent("Foo")
    attr = _StubAttr({"Foo": {"TimeStamp": 1}})
    evt.set_attr(attr)
    assert evt.get_attr() is attr
    assert evt.do_match(["Bar", "5"]) is False
    assert evt.do_match(["Foo", "2500"]) is True
    assert evt.get_timestamp() == 2.5
    assert attr.matched == [evt]


@pytest.mark.parametrize(
    "n,expected",
    [(1, "1st"), (2, "2nd"), (3, "3rd"), (4, "4th"), (22, "22nd"), (40, "40th")],
)
def test_nth_suffix(n, expected):
    nth = Nth(n, ProcessStart("firefox.exe"))
    assert str(nth) == f"{expected} [Start of a [firefox.exe] process]"


def test_nth_delegates_and_rejects_foreign_event():
    inner = ThreadStart()
    nth = Nth(2, inner)
    attr = _StubAttr({})
    nth.set_attr(attr)
    assert nth.get_attr() is attr
    nth.set_whiteboard({XPerfEvent.EVENT_DATA_PID: 1})
    assert nth.get_whiteboard() == {XPerfEvent.EVENT_DATA_PID: 1}
    inner.timestamp = 7.0
    assert nth.get_timestamp() == 7.0
    nth.on_event_matched(inner)
    assert attr.matched == []
    nth.on_event_matched(inner)
    assert attr.matched == [nth]
    with pytest.raises(Exception, match="Nth expression"):
        nth.on_event_matched(FileIOReadOrWrite(0))


def test_event_expression_base_methods_return_none():
    nth = Nth(1, ThreadStart())
    for name in [
        "get_whiteboard",
        "get_timestamp",
    ]:
        assert getattr(EventExpression, name)(nth) is None
    for name in ["set_whiteboard", "on_event_matched", "do_match"]:
        assert getattr(EventExpression, name)(nth, None) is None


def test_event_sequence_requires_two_events_and_enforces_order():
    with pytest.raises(Exception, match="at least two events, 1 provided"):
        EventSequence(ThreadStart())

    first, second = FileIOReadOrWrite(0), FileIOReadOrWrite(1)
    seq = EventSequence(first, second)
    seq.set_attr(_StubAttr({}))
    seq.set_whiteboard({XPerfEvent.EVENT_DATA_PID: 1})
    assert first.get_whiteboard() == {XPerfEvent.EVENT_DATA_PID: 1}
    with pytest.raises(Exception, match="not a member of this event sequence"):
        seq.on_event_matched(second)
    seq.on_event_matched(first)
    second.timestamp = 3.0
    seq.on_event_matched(second)
    assert seq.get_timestamp() == 3.0
    with pytest.raises(Exception, match="not a member of this event sequence"):
        seq.on_event_matched(first)


def test_bind_thread_only_matches_events_on_bound_thread():
    inner = FileIOReadOrWrite(FileIOReadOrWrite.READ)
    bind = BindThread(inner)
    attr = _StubAttr({"FileIoRead": {"ThreadID": 1, "TimeStamp": 2}})
    bind.set_attr(attr)
    bind.set_whiteboard({XPerfEvent.EVENT_DATA_TID: 5})
    assert str(bind) == "[File I/O Bytes Read] bound to thread [5]"

    with mock.patch.object(inner, "do_match") as do_match:
        bind.do_match(["P-Start", "1"])
        bind.do_match(["FileIoRead", "6", "1"])
        do_match.assert_not_called()
        bind.do_match(["FileIoRead", "5", "1"])
        do_match.assert_called_once_with(["FileIoRead", "5", "1"])

    inner.timestamp = 4.0
    assert bind.get_timestamp() == 4.0
    with pytest.raises(Exception, match="BindThread expression"):
        bind.on_event_matched(FileIOReadOrWrite(1))


def test_classic_event_matches_only_its_guid():
    evt = ClassicEvent(OTHER_GUID)
    attr = _StubAttr({"UnknownEvent/Classic": {"EventGuid": 1, "TimeStamp": 2}})
    evt.set_attr(attr)
    assert str(evt) == "User event (classic): [{26d1e091-0ae7-4f49-a554-4214445c505c}]"
    assert not evt.match(["P-Start", OTHER_GUID])
    assert not evt.match(["UnknownEvent/Classic", RESTORED])
    assert evt.match(["UnknownEvent/Classic", OTHER_GUID.lower()])


def test_ready_thread_and_cswitch_without_tid():
    for evt, key, field in [
        (ReadyThread(), "ReadyThread", "Rdy TID"),
        (ContextSwitchToThread(), "CSwitch", "New TID"),
    ]:
        evt.set_attr(_StubAttr({key: {field: 1}}))
        assert evt.match([key, "3"]) is False
        assert evt.match(["Other", "3"]) is False
        evt.set_whiteboard({XPerfEvent.EVENT_DATA_TID: 3})
        assert evt.match([key, "3"]) is True
        assert evt.match([key, "4"]) is False


def test_thread_start_and_process_start_non_matching_rows():
    ts = ThreadStart()
    ts.set_attr(_StubAttr({}))
    assert ts.match(["P-Start"]) is False
    ps = ProcessStart("firefox.exe")
    assert ps.match(["T-Start"]) is False


@pytest.mark.parametrize(
    "cmd_line,expected",
    [
        ("firefox.exe", ["firefox.exe"]),
        ("a b c", ["a", "b", "c"]),
        (
            '"C:\\Program Files\\fx.exe" -url "x y"',
            ["C:\\Program Files\\fx.exe", "-url", "x y"],
        ),
        ("a ", ["a"]),
        ("", []),
    ],
)
def test_tokenize_cmd_line(cmd_line, expected):
    assert ProcessStart.tokenize_cmd_line(cmd_line) == expected


def test_file_io_invalid_verb():
    with pytest.raises(Exception, match="Invalid verb"):
        FileIOReadOrWrite(2)


def test_session_tracks_events_of_attached_attributes():
    sess = XPerfSession()
    assert sess.is_empty()
    interval = XPerfInterval(ProcessStart("firefox.exe"), ReadyThread())
    interval.set_session(sess)
    assert not sess.is_empty()
    assert sess.evtset == set(interval.get_events())
    interval.set_session(None)
    assert sess.evtset == set()
    assert interval.sess is None


def test_xperf_file_requires_input():
    with pytest.raises(Exception, match="Missing parameters"):
        XPerfFile()


def test_xperf_file_csv_input_is_kept(tmp_path):
    csv_path = tmp_path / "kept.csv"
    csv_path.write_text(HEADER)
    xperf = XPerfFile(csvfile=str(csv_path))
    assert xperf.csvfile == os.path.abspath(str(csv_path))
    assert xperf.keepcsv is True
    with xperf as entered:
        assert entered is xperf
        assert xperf.csv_fd is not None
    assert xperf.csv_fd.closed
    assert csv_path.exists()


def test_xperf_file_load_without_csv(tmp_path):
    xperf = make_file(tmp_path)
    xperf.csvfile = None
    assert xperf.load() is False
    with pytest.raises(Exception, match="Load failed"):
        xperf.__enter__()


def test_xperf_file_etl_converts_and_removes_csv(tmp_path, capsys):
    etl = tmp_path / "trace.etl"
    expected_csv = os.path.join(str(tmp_path), "trace.csv")
    with mock.patch.object(xperf_analyzer.subprocess, "call") as call:
        xperf = XPerfFile(xperf_path="xperf.exe", debug=True, etlfile=str(etl))

    call.assert_called_once_with([
        "xperf.exe",
        "-i",
        os.path.abspath(str(etl)),
        "-o",
        expected_csv,
    ])
    assert xperf.csvfile == expected_csv
    assert xperf.keepcsv is False
    assert "Executing 'xperf.exe -i" in capsys.readouterr().out

    with open(expected_csv, "w") as f:
        f.write(HEADER)
    with xperf:
        pass
    assert not os.path.exists(expected_csv)


def test_xperf_file_etl_with_csvout_and_keepcsv(tmp_path):
    etl = tmp_path / "trace.etl"
    out = tmp_path / "out.csv"
    with mock.patch.object(xperf_analyzer.subprocess, "call") as call:
        xperf = XPerfFile(
            xperf_path="xperf.exe", etlfile=str(etl), csvout=str(out), keepcsv=False
        )
    assert call.call_args[0][0][-1] == os.path.abspath(str(out))
    assert xperf.csvfile == os.path.abspath(str(out))
    out.write_text(HEADER)
    with xperf:
        pass
    assert out.exists()


def test_xperf_file_merges_user_and_kernel(tmp_path, capsys):
    user = os.path.join(str(tmp_path), "u.etl")
    kernel = os.path.join(str(tmp_path), "k.etl")
    merged = os.path.join(str(tmp_path), "merged.etl")
    with mock.patch.object(xperf_analyzer.subprocess, "call") as call:
        xperf = XPerfFile(
            xperf_path="xperf.exe",
            debug=True,
            etluser=user,
            etlkernel=kernel,
            keepcsv=True,
        )
    assert call.call_args_list == [
        mock.call(["xperf.exe", "-merge", user, kernel, merged]),
        mock.call([
            "xperf.exe",
            "-i",
            merged,
            "-o",
            os.path.join(str(tmp_path), "merged.csv"),
        ]),
    ]
    assert xperf.etlfile == merged
    assert xperf.keepcsv is True
    assert "Executing 'xperf.exe -merge" in capsys.readouterr().out


def test_get_xperf_path_from_path(tmp_path):
    dir_a = os.path.join(str(tmp_path), "a")
    dir_b = os.path.join(str(tmp_path), "b")
    expected = os.path.join(dir_b, "xperf.exe")
    xperf = make_file(tmp_path)
    with mock.patch.dict(os.environ, {"PATH": os.pathsep.join([dir_a, dir_b])}):
        with mock.patch.object(
            xperf_analyzer.os, "access", side_effect=lambda p, mode: p == expected
        ):
            assert xperf.get_xperf_path() == os.path.abspath(expected)
            assert xperf.get_xperf_path() == os.path.abspath(expected)


def test_get_xperf_path_missing(tmp_path):
    xperf = make_file(tmp_path)
    with mock.patch.dict(os.environ, {"PATH": str(tmp_path)}):
        with mock.patch.object(xperf_analyzer.os, "access", return_value=False):
            with pytest.raises(Exception, match="Cannot find xperf"):
                xperf.get_xperf_path()


def test_main_sample_runs_on_header_only_csv(tmp_path):
    csv_path = tmp_path / "header_only.csv"
    csv_path.write_text(HEADER)
    argv = ["xperf_analyzer.py", "csv", str(csv_path)]
    with mock.patch.object(sys, "argv", argv):
        runpy.run_path(xperf_analyzer.__file__, run_name="__main__")
    assert csv_path.exists()


if __name__ == "__main__":
    mozunit.main()
