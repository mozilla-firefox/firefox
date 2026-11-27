# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from unittest import mock

import mozunit
import pytest

from talos import utils


def test_timer_elapsed_formats_seconds():
    with mock.patch("talos.utils.time.time", side_effect=[100.0, 100.0 + 3725]):
        timer = utils.Timer()
        assert timer.elapsed() == "01:02:05"


def test_interpolate_replaces_talos_with_talos_dir():
    assert utils.interpolate("${talos}/x") == f"{utils.here}/x"
    assert os.path.isdir(utils.here)


def test_interpolate_extra_kwargs_and_unknown_kept():
    assert utils.interpolate("${a}-${b}", a="1") == "1-${b}"
    assert utils.interpolate("${talos}", talos="override") == "override"


def test_findall_returns_all_start_offsets():
    assert utils.findall("a.b.a.b", "a.") == [0, 4]
    assert utils.findall("abc", "z") == []


def test_tokenize_returns_parts_and_last_position():
    text = "x<s>one</s>y<s>two</s>"
    parts, last = utils.tokenize(text, "<s>", "</s>")
    assert parts == ["one", "two"]
    assert last == text.rindex("</s>")


def test_tokenize_without_tokens_returns_empty_list():
    assert utils.tokenize("nothing", "<s>", "</s>") == ([], -1)


@pytest.mark.parametrize(
    "text,start,end,message",
    [
        ("", "abc", "b", "End token"),
        ("", "b", "abc", "Start token"),
        ("<s>only", "<s>", "</s>", "Could not find end token"),
        ("only</s>", "<s>", "</s>", "Could not find start token"),
        ("<s>a</s><s>b", "<s>", "</s>", "Unmatched number of tokens"),
        ("</s>a<s>", "<s>", "</s>", "occurs before start token"),
    ],
)
def test_tokenize_raises_assertion_for_invalid_tokens(text, start, end, message):
    with pytest.raises(AssertionError, match=message):
        utils.tokenize(text, start, end)


def test_urlsplit_file_scheme_default():
    assert utils.urlsplit("C:\\path\\file.txt") == [
        "file",
        "",
        "C:\\path\\file.txt",
        "",
        "",
    ]
    assert utils.urlsplit("file:///tmp/x") == ["file", "", "/tmp/x", "", ""]


def test_urlsplit_http():
    assert utils.urlsplit("http://host:80/p?q=1#f") == [
        "http",
        "host:80",
        "/p",
        "q=1",
        "f",
    ]
    assert utils.urlsplit("host/p", default_scheme="https")[0] == "https"


@pytest.mark.parametrize(
    "value,expected", [("true", True), ("false", False), ("5", 5), ("abc", "abc")]
)
def test_parse_pref_converts_values_to_python_types(value, expected):
    assert utils.parse_pref(value) == expected


@pytest.fixture
def linux():
    with mock.patch("talos.utils.platform.system", return_value="Linux"):
        yield


def test_command_line_splits_string_extra_args(linux):
    assert utils.GenerateBrowserCommandLine(
        " firefox ", "-arg", "prof", "http://x a"
    ) == ["firefox", "-arg", "-profile", "prof", "http://x", "a"]


def test_command_line_list_extra_args_no_url(linux):
    assert utils.GenerateBrowserCommandLine("ff", ["-a", "-b"], "prof", None) == [
        "ff",
        "-a",
        "-b",
        "-profile",
        "prof",
    ]


def test_command_line_blank_extra_args(linux):
    assert utils.GenerateBrowserCommandLine("ff", "  ", "prof", None) == [
        "ff",
        "-profile",
        "prof",
    ]


def test_command_line_darwin_adds_foreground():
    with mock.patch("talos.utils.platform.system", return_value="Darwin"):
        cmd = utils.GenerateBrowserCommandLine("ff", [], "prof", None)
    assert cmd == ["ff", "-foreground", "-profile", "prof"]


def test_command_line_profiling_info_appended_to_url(linux):
    info = {"a": "1"}
    assert utils.GenerateBrowserCommandLine("ff", [], "p", "http://x", info)[-1] == (
        "http://x?a=1"
    )
    assert utils.GenerateBrowserCommandLine("ff", [], "p", "http://x?b=2", info)[
        -1
    ] == ("http://x?b=2&a=1")


def test_command_line_profiling_info_without_url(linux):
    assert utils.GenerateBrowserCommandLine("ff", [], "p", None, {"a": "1"}) == [
        "ff",
        "-profile",
        "p",
    ]


@pytest.mark.parametrize(
    "config,expected",
    [
        ({}, False),
        ({"debug": True}, True),
        ({"debugger": "gdb"}, True),
        ({"debug": False, "debugger": None}, False),
    ],
)
def test_run_in_debug_mode_depends_on_debug_config(config, expected):
    assert utils.run_in_debug_mode(config) is expected


if __name__ == "__main__":
    mozunit.main()
