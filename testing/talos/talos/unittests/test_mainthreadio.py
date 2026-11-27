# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit
import pytest

from talos import mainthreadio

LOG = (
    "1,read,0.5,src,startup.dll\n"
    "0,NEXT-STAGE\n"
    "1,read,0.5,src,(not available)\n"
    "1,read,0.5,src,::\\{leaked}\n"
    "1,read,0.5,src,file.txt\n"
    "1,read,0.25,src,file.txt\n"
    "1,write,1.0,PoisonIOInterposer,other.txt\n"
    "garbage line\n"
    "0,NEXT-STAGE\n"
    "1,read,2.0,src,file.txt\n"
)


@pytest.fixture
def logfile(tmp_path):
    path = tmp_path / "mainthread_io.log"
    path.write_text(LOG)
    return path


def test_parse_aggregates_log_entries_by_phase_and_file(logfile):
    data = {}
    assert mainthreadio.parse(str(logfile), data) is True
    assert set(data) == {
        ("normal", "src", "file.txt", "read"),
        ("normal", "PoisonIOInterposer", "other.txt", "write"),
        ("shutdown", "src", "file.txt", "read"),
    }
    entry = data[("normal", "src", "file.txt", "read")]
    assert entry["Count"] == 2
    assert entry["Duration"] == 0.75
    assert data[("shutdown", "src", "file.txt", "read")] == {
        "Count": 1,
        "RunCount": 1,
        "Duration": 2.0,
    }


def test_parse_missing_file(tmp_path, capsys):
    assert mainthreadio.parse(str(tmp_path / "missing.log"), {}) is False
    assert "missing.log" in capsys.readouterr().out


def test_write_output_writes_json_rows(tmp_path):
    out = tmp_path / "out.json"
    data = {
        ("normal", "src", "f", "read"): {"Count": 2, "RunCount": 1, "Duration": 1.5}
    }
    assert mainthreadio.write_output(str(out), data) is True
    text = out.read_text()
    assert text.startswith("[\n")
    assert text.endswith("]\n")
    assert '["normal", "src", "f", "read", 2, 1, 1.500000]' in text


def test_write_output_error(tmp_path, capsys):
    assert mainthreadio.write_output(str(tmp_path), {}) is False
    assert str(tmp_path) in capsys.readouterr().out


@pytest.fixture
def xre(tmp_path):
    path = tmp_path / "xre"
    path.mkdir()
    (path / "dependentlibs.list").write_text("xul.dll\n")
    return path


def test_main_without_args_prints_usage(capsys):
    assert mainthreadio.main(["mtio"]) == 1
    assert "Usage" in capsys.readouterr().out


def test_main_missing_xre(tmp_path, capsys):
    argv = ["mtio", "log", "out", str(tmp_path / "nope")]
    assert mainthreadio.main(argv) == 1
    assert "does not exist" in capsys.readouterr().out


def test_main_parse_failure(tmp_path, xre, capsys):
    argv = ["mtio", str(tmp_path / "nope.log"), "out", str(xre)]
    assert mainthreadio.main(argv) == 1
    assert "Log parsing failed" in capsys.readouterr().out


def test_main_allowlist_failure(tmp_path, logfile, capsys):
    xre_dir = tmp_path / "empty_xre"
    xre_dir.mkdir()
    argv = ["mtio", str(logfile), str(tmp_path / "out"), str(xre_dir)]
    assert mainthreadio.main(argv) == 1
    assert "Failed to load allowlist" in capsys.readouterr().out


def test_main_write_failure(tmp_path, logfile, xre):
    assert mainthreadio.main(["mtio", str(logfile), str(tmp_path), str(xre)]) == 1


def test_main_reports_unexpected_files(tmp_path, logfile, xre, capsys):
    out = tmp_path / "out.json"
    assert mainthreadio.main(["mtio", str(logfile), str(out), str(xre)]) == 0
    assert out.exists()
    errors = [
        line
        for line in capsys.readouterr().out.splitlines()
        if line.startswith("TEST-UNEXPECTED-FAIL")
    ]
    assert len(errors) == 2
    assert all("'file.txt' was accessed" in line for line in errors)
    assert not any("other.txt" in line for line in errors)


if __name__ == "__main__":
    mozunit.main()
