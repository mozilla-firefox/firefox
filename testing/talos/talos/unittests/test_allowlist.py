# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import re

import mozunit
import pytest

from talos import allowlist


@pytest.fixture
def xre(tmp_path):
    path = tmp_path / "xre"
    path.mkdir()
    (path / "dependentlibs.list").write_text("xul.dll\nnss3.dll\n")
    return path


def make(xre, path_subs=None, name_subs=None, **kwargs):
    return allowlist.Allowlist(
        "mytest",
        {"{xre}": str(xre)},
        path_subs or {},
        name_subs or {},
        **kwargs,
    )


def test_new_allowlist_starts_with_empty_maps(xre):
    wl = make(xre)
    assert wl.listmap == {}
    assert wl.dependent_libs == {}
    assert wl.expected_event_sources == []


def test_load_reads_allowlist_and_builds_dependent_libs_map(xre, tmp_path):
    allowlist_file = tmp_path / "allow.json"
    allowlist_file.write_text(json.dumps({"C:\\Foo.DLL": {"ignore": True}}))
    wl = make(xre)
    assert wl.load(str(allowlist_file)) is True
    assert wl.listmap == {"c:\\foo.dll": {"ignore": True}}
    assert wl.dependent_libs == {
        "{xre}" + os.path.sep + "xul.dll": {"ignore": True},
        "{xre}" + os.path.sep + "nss3.dll": {"ignore": True},
    }


def test_load_fails_when_dependentlibs_list_is_missing(tmp_path, capsys):
    wl = make(tmp_path)
    assert wl.load(str(tmp_path / "allow.json")) is False
    assert "dependentlibs.list" in capsys.readouterr().out


def test_load_fails_when_allowlist_file_is_missing(xre, tmp_path, capsys):
    wl = make(xre)
    assert wl.load(str(tmp_path / "missing.json")) is False
    assert "missing.json" in capsys.readouterr().out


def test_sanitize_filename_path_substitution(xre):
    wl = make(xre, path_subs={"\\firefox": "{xre}"})
    assert wl.sanitize_filename("C:\\Program Files\\Firefox\\XUL.dll") == (
        "{xre}\\xul.dll"
    )


def test_sanitize_filename_profile_records_parent_dirs(xre):
    wl = make(xre, path_subs={"\\profile": "{profile}"})
    name = wl.sanitize_filename("C:\\Users\\me\\Temp\\tmpabcdefgh\\profile\\prefs.js")
    assert name == "{profile}\\prefs.js"
    assert wl.PRE_PROFILE == "c:\\users\\me\\temp\\tmpabcdefgh"
    for parent in (
        "c:\\users",
        "c:\\users\\me",
        "c:\\users\\me\\temp",
        "c:\\users\\me\\temp\\tmpabcdefgh",
    ):
        assert parent in wl.listmap

    wl.listmap.clear()
    wl.sanitize_filename("C:\\Other\\profile\\x")
    assert wl.listmap == {}


def test_sanitize_filename_short_profile_dir(xre):
    wl = make(xre, path_subs={"\\profile": "{profile}"})
    wl.sanitize_filename("C:\\temp\\tmpabc~1\\profile\\prefs.js")
    assert wl.PRE_PROFILE == ""
    assert wl.listmap == {"c:\\temp\\tmpabc~1": {}}


def test_sanitize_filename_name_substitutions(xre):
    wl = make(
        xre,
        name_subs={
            "prefetch": "{prefetch}",
            re.compile(r"{\w{8}-\w{4}-\w{4}-\w{4}-\w{12}}"): "{uuid}",
        },
    )
    assert wl.sanitize_filename("C:\\Windows\\Prefetch\\FIREFOX.EXE-1.pf") == (
        "c:\\windows\\{prefetch}"
    )
    assert wl.sanitize_filename(
        "c:\\x\\{12345678-1234-1234-1234-123456789012}\\y "
    ) == ("c:\\x\\{uuid}\\y")


def test_check_reports_only_unexpected_unlisted_files(xre, tmp_path):
    allowlist_file = tmp_path / "allow.json"
    allowlist_file.write_text(
        json.dumps({
            "ignored": {"ignore": True},
            "listed": {},
            "off": {"ignore": False},
        })
    )
    wl = make(xre, event_sources=["Expected"])
    assert wl.load(str(allowlist_file))
    dependent = "{xre}" + os.path.sep + "xul.dll"
    data = {
        ("normal", "src", "ignored", "read"): 1,
        ("normal", "src", "listed", "read"): 2,
        ("normal", "src", "off", "read"): 3,
        ("normal", "src", dependent, "read"): 4,
        ("normal", "Expected", "unknown", "read"): 5,
        ("normal", "src", "unknown", "read"): 6,
        ("normal", "src", "unknown", "write"): 7,
    }
    errors = wl.check(data, 2, 1)
    assert errors == {"unknown": [6, 7]}
    assert wl.check(data, 2) == {"unknown": [5, 6, 7]}


def test_check_duration_ignores_durations_within_limits(xre):
    wl = make(xre, init_with=None)
    wl.listmap["slow"] = {"ignoreduration": 500.0}
    data = {
        ("normal", "src", "fast", "read"): {"Duration": 1.0},
        ("normal", "src", "slow", "read"): {"Duration": 400.0},
    }
    assert wl.checkDuration(data, 2, "Duration") == {}


def test_filter_keeps_unlisted_entries(xre, tmp_path):
    allowlist_file = tmp_path / "allow.json"
    allowlist_file.write_text(json.dumps({"listed": {}}))
    wl = make(xre)
    assert wl.load(str(allowlist_file))
    data = {("normal", "src", "listed", "read"): 1, ("normal", "src", "x", "r"): 2}
    wl.filter(data, 2)
    assert len(data) == 2


def test_error_strings_and_print_errors_format_unexpected_files(xre, capsys):
    strs = allowlist.Allowlist.get_error_strings({"f": [1, {"a": 2}]})
    assert strs == [
        "File 'f' was accessed and we were not expecting it: 1",
        "File 'f' was accessed and we were not expecting it: {'a': 2}",
    ]
    make(xre).print_errors(strs)
    lines = capsys.readouterr().out.splitlines()
    assert lines[0] == "TEST-UNEXPECTED-FAIL | mytest | " + strs[0]
    assert len(lines) == 2


if __name__ == "__main__":
    mozunit.main()
