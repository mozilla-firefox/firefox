# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import io
import os
import subprocess
import sys
from shutil import which

import mozunit
import pytest

from mozlint import cli

here = os.path.abspath(os.path.dirname(__file__))


@pytest.fixture
def parser():
    return cli.MozlintParser()


@pytest.fixture
def run(parser, files):
    def inner(args=None):
        args = args or []
        if not any("--stdin-filename" in a for a in args):
            args.extend(files)
        lintargs = vars(parser.parse_args(args))
        lintargs["root"] = here
        lintargs["config_paths"] = [os.path.join(here, "linters")]
        return cli.run(**lintargs)

    return inner


def test_cli_with_ascii_encoding(run, monkeypatch, capfd):
    cmd = [sys.executable, "runcli.py", "-l=string", "-f=stylish", "files/foobar.js"]
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(sys.path)
    env["PYTHONIOENCODING"] = "ascii"
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=here,
        env=env,
        universal_newlines=True,
    )
    out = proc.communicate()[0]
    assert "Traceback" not in out


def test_cli_run_with_fix(run, capfd):
    ret = run(["-f", "json", "--fix", "--linter", "external"])
    out, err = capfd.readouterr()
    assert ret == 0
    assert out.endswith("{}\n")


@pytest.mark.skipif(not which("echo"), reason="No `echo` executable found.")
def test_cli_run_with_edit(run, parser, capfd):
    os.environ["EDITOR"] = "echo"

    ret = run(["-f", "compact", "--edit", "--linter", "external"])
    out, err = capfd.readouterr()
    out = out.splitlines()
    assert ret == 1
    assert out[0].endswith("foobar.js")  # from the `echo` editor
    assert "foobar.js: line 1, col 1, Error" in out[1]
    assert "foobar.js: line 2, col 1, Error" in out[2]
    assert "2 problems" in out[-1]
    assert len(out) == 5

    del os.environ["EDITOR"]
    with pytest.raises(SystemExit):
        parser.parse_args(["--edit"])


def test_cli_run_with_setup(run, capfd):
    # implicitly call setup
    ret = run(["-l", "setup", "-l", "setupfailed", "-l", "setupraised"])
    out, err = capfd.readouterr()
    assert "setup passed" in out
    assert "setup failed" in out
    assert "setup raised" in out
    assert ret == 1

    # explicitly call setup
    ret = run(["-l", "setup", "-l", "setupfailed", "-l", "setupraised", "--setup"])
    out, err = capfd.readouterr()
    assert "setup passed" in out
    assert "setup failed" in out
    assert "setup raised" in out
    assert ret == 1


def test_cli_for_exclude_list(run, monkeypatch, capfd):
    ret = run(["-l", "excludes", "--check-exclude-list"])
    out, err = capfd.readouterr()

    assert "**/foobar.js" in out
    assert (
        "The following list of paths are now green and can be removed from the exclude list:"
        in out
    )

    ret = run(["-l", "excludes_empty", "--check-exclude-list"])
    out, err = capfd.readouterr()

    assert "No path in the exclude list is green." in out
    assert ret == 1


def test_cli_run_with_wrong_linters(run, capfd):
    run(["-l", "external", "-l", "foobar"])
    out, err = capfd.readouterr()

    # Check if it identifies foobar as invalid linter
    assert "A failure occurred in the foobar linter." in out

    # Check for exception message
    assert "Invalid linters given, run again using valid linters or no linters" in out


def test_cli_run_with_stdin_filename(run, filedir, capfd, monkeypatch, tmp_path):
    for arg in ("bar.txt", "--workdir", "--outgoing", "--rev=abc"):
        with pytest.raises(SystemExit):
            run(["--stdin-filename=foo.txt", arg])

    capfd.readouterr()
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"foobar\n")))
    run(["-l", "string", f"--stdin-filename={filedir}/foobar.py"])
    out, err = capfd.readouterr()
    assert out == "✖ 0 problems (0 errors, 0 warnings, 0 fixed)\n"

    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"foobar\n")))
    run(["-l", "string", f"--stdin-filename={filedir}/foobar.py", "--dump-stdin-file"])
    out, err = capfd.readouterr()
    assert out == "foobar\n"

    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"foobar\n")))
    run(["-l", "string", f"--stdin-filename={filedir}/foobar.py", "--fix"])
    out, err = capfd.readouterr()
    assert out == "foobar\n"

    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(b"foobar\n")))
    tmpfile = tmp_path / "temp"
    run(
        [
            "-l",
            "string",
            f"--stdin-filename={filedir}/foobar.py",
            "--dump-stdin-file",
            str(tmpfile),
        ]
    )
    out, err = capfd.readouterr()
    assert out == ""
    assert tmpfile.read_text() == "foobar\n"


if __name__ == "__main__":
    mozunit.main()
