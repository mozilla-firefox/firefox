# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import re

import mozunit
import pytest

from condprof import progress


class TTY(io.StringIO):
    def isatty(self):
        return True


@pytest.fixture
def stream(monkeypatch):
    out = TTY()
    monkeypatch.setattr(progress, "STREAM", out)
    return out


def test_bar_hidden_when_not_a_tty(monkeypatch):
    out = io.StringIO()
    monkeypatch.setattr(progress, "STREAM", out)
    assert list(progress.bar([1, 2, 3])) == [1, 2, 3]
    assert out.getvalue() == ""


def test_bar_hidden_without_isatty(monkeypatch):
    monkeypatch.setattr(progress, "STREAM", object())
    assert progress.Bar().hide is True


def test_bar_shows_progress(stream):
    assert list(progress.bar(["a", "b"], label="L", width=4)) == ["a", "b"]
    lines = stream.getvalue().split("\r")
    assert lines[0] == "L[    ] 0/2 - 00:00:00"
    assert lines[1] == "L[##  ] 1/2 - 00:00:00"
    assert lines[2] == "L[####] 2/2 - 00:00:00"
    assert stream.getvalue().endswith("\n")


def test_bar_every_limits_redraws(stream):
    list(progress.bar(range(4), width=4, every=3))
    shown = re.findall(r"\d+/\d+", stream.getvalue())
    assert shown == ["0/4", "3/4", "4/4", "4/4"]


def test_bar_computes_eta_from_iteration_time(stream, monkeypatch):
    now = [100.0]
    monkeypatch.setattr(progress.time, "time", lambda: now[0])
    bar = progress.Bar(expected_size=10, width=10)
    now[0] = 102.0
    bar.show(1)
    assert bar.ittimes == [1.0]
    assert bar.eta == 9.0
    assert bar.etadisp == "00:00:09"


def test_bar_show_needs_size():
    bar = progress.Bar(hide=True)
    with pytest.raises(Exception, match="expected_size"):
        bar.show(1)
    bar.show(1, count=2)
    assert bar.expected_size == 2
    assert bar.last_progress == 1


def test_bar_context_manager_ends_line_on_exception(stream):
    with pytest.raises(ValueError):
        with progress.Bar(expected_size=1) as bar:
            raise ValueError()
    assert bar.elapsed >= 0
    assert stream.getvalue().endswith("\n")


def test_dots_prints_label_and_dots_every_n_items(stream):
    assert list(progress.dots(range(5), label="go", every=2)) == list(range(5))
    assert stream.getvalue() == "go...\n"


def test_dots_hidden_prints_only_newline(stream):
    assert list(progress.dots(range(3), label="go", hide=True)) == [0, 1, 2]
    assert stream.getvalue() == "\n"


def test_mill_spins_through_frames_and_counts(stream):
    assert list(progress.mill(range(3), label="m")) == [0, 1, 2]
    assert stream.getvalue() == "m | 0/3\rm / 1/3\rm - 2/3\rm   3/3\r\n"


def test_mill_hidden_prints_nothing(stream):
    assert list(progress.mill([], hide=True, expected_size=0)) == []
    assert stream.getvalue() == ""


if __name__ == "__main__":
    mozunit.main()
