# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import mozunit
import pytest

from talos import cmdline
from talos.test import Test


def test_executable_path_is_required():
    with pytest.raises(SystemExit):
        cmdline.parse_args(["--activeTests", "ts_paint"])


def test_parse_args_applies_default_option_values():
    args = cmdline.parse_args(["-e", "/path/to/firefox"])

    assert args.browser_path == "/path/to/firefox"
    assert args.title == "qm-pxp01"
    assert args.browser_wait == 5
    assert args.test_timeout == 1200
    assert args.extensions == ["${talos}/talos-powers"]
    assert args.extraPrefs == []
    assert args.fission is True
    assert args.develop is False
    assert args.pdfpaint is False
    assert args.pdfpaint_chunk is None
    assert args.pdfpaint_name is None
    assert args.project == "mozilla-central"
    assert args.screenshot_on_failure is False


def test_parse_args_applies_command_line_overrides():
    args = cmdline.parse_args([
        "-e",
        "firefox",
        "--activeTests",
        "ts_paint:tresize",
        "--browserWait",
        "10",
        "--setpref",
        "a=1",
        "--setpref",
        "b=true",
        "--extension",
        "extra.xpi",
        "--gecko-profile-interval",
        "0.5",
        "--gecko-profile-entries",
        "1000",
        "--pdfPaintChunk",
        "2",
        "--disable-fission",
        "--develop",
    ])

    assert args.activeTests == "ts_paint:tresize"
    assert args.browser_wait == 10
    assert args.extraPrefs == ["a=1", "b=true"]
    assert args.extensions == ["${talos}/talos-powers", "extra.xpi"]
    assert args.gecko_profile_interval == 0.5
    assert args.gecko_profile_entries == 1000
    assert args.pdfpaint_chunk == 2
    assert args.fission is False
    assert args.develop is True


def test_mach_interface_parser_has_no_executable_path():
    parser = cmdline.create_parser(mach_interface=True)
    args = parser.parse_args([])

    assert not hasattr(args, "browser_path")


def test_print_tests(capsys):
    class described(Test):
        """
        First line.
        Second line.
        """

    class undescribed(Test):
        pass

    tests = {"described": described, "undescribed": undescribed}
    with mock.patch("talos.test.test_dict", return_value=tests):
        with pytest.raises(SystemExit) as excinfo:
            cmdline.parse_args(["--print-tests"])

    assert excinfo.value.code == 0
    out = capsys.readouterr().out
    assert out.startswith("Available tests:\n================\n\n")
    assert "described\n---------\nFirst line.\nSecond line.\n\n" in out
    assert "undescribed\n-----------\nNo documentation available yet.\n" in out
    assert out.index("described\n") < out.index("undescribed\n")


def test_print_suites_lists_aligned_suites_and_exits(capsys):
    suites = {
        "short": {"tests": ["a", "b"]},
        "much-longer": {"tests": ["c"]},
    }
    with mock.patch("talos.config.suites_conf", return_value=suites):
        with pytest.raises(SystemExit) as excinfo:
            cmdline.parse_args(["--print-suites"])

    assert excinfo.value.code == 0
    out = capsys.readouterr().out
    assert out.splitlines() == [
        "Available suites:",
        " short       (a:b)",
        " much-longer (c)",
        "",
    ]


def test_print_suites_lists_suites_from_talos_json(capsys):
    with pytest.raises(SystemExit):
        cmdline.parse_args(["--print-suites"])

    out = capsys.readouterr().out
    assert "Available suites:" in out
    assert " other " in out


if __name__ == "__main__":
    mozunit.main()
