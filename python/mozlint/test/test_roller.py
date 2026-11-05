# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
import signal
import subprocess
import sys
import time
from itertools import chain

import mozpack.path as mozpath
import mozunit
import pytest

from mozlint.errors import LintersNotConfigured, NoValidLinter
from mozlint.result import Issue, ResultSummary
from mozlint.roller import (
    LintRoller,
    _balanced_jobs,
    _group_jobs,
    _read_ignore_patterns,
    _split_dirs,
)

here = os.path.abspath(os.path.dirname(__file__))


def test_roll_no_linters_configured(lint, files):
    with pytest.raises(LintersNotConfigured):
        lint.roll(files)


def test_roll_successful(lint, linters, files):
    lint.read(linters("string", "regex", "external"))

    result = lint.roll(files)
    assert len(result.issues) == 1
    assert result.failed == set([])

    path = list(result.issues.keys())[0]
    assert os.path.basename(path) == "foobar.js"

    errors = result.issues[path]
    assert isinstance(errors, list)
    assert len(errors) == 6

    container = errors[0]
    assert isinstance(container, Issue)
    assert container.rule == "no-foobar"


def test_generate_jobs_root_spelling(lint, filedir):
    lint.root = filedir + "/"
    lint.linters = [{"name": "root", "type": "external", "include": [filedir]}]
    jobs = lint._generate_jobs({filedir}, set(), num_procs=1)
    paths = sorted(p for _, ps in jobs for p in ps)
    assert paths == sorted(os.path.join(filedir, p) for p in os.listdir(filedir))


def test_roll_from_subdir(lint, linters):
    lint.read(linters("string", "regex", "external"))

    oldcwd = os.getcwd()
    try:
        os.chdir(os.path.join(lint.root, "files"))

        # Path relative to cwd works
        result = lint.roll("foobar.js")
        assert len(result.issues) == 1
        assert len(result.failed) == 0
        assert result.returncode == 1

        # Path relative to root doesn't work
        result = lint.roll(os.path.join("files", "foobar.js"))
        assert len(result.issues) == 0
        assert len(result.failed) == 0
        assert result.returncode == 0

        # Paths from vcs are always joined to root instead of cwd
        lint.mock_vcs([os.path.join("files", "foobar.js")])
        result = lint.roll(outgoing=True)
        assert len(result.issues) == 1
        assert len(result.failed) == 0
        assert result.returncode == 1

        result = lint.roll(workdir=True)
        assert len(result.issues) == 1
        assert len(result.failed) == 0
        assert result.returncode == 1

        result = lint.roll(rev='not public() and keyword("dummy revset expression")')
        assert len(result.issues) == 1
        assert len(result.failed) == 0
        assert result.returncode == 1
    finally:
        os.chdir(oldcwd)


def test_roll_catch_exception(lint, linters, files, capfd):
    lint.read(linters("raises"))

    lint.roll(files)  # assert not raises
    out, err = capfd.readouterr()
    assert "LintException" in err


def test_roll_payload_shadowed_on_sys_path(lint, linters, files, tmp_path, monkeypatch):
    shadow = tmp_path / "external.py"
    shadow.write_text(
        "def external(*args, **kwargs):\n"
        "    raise AssertionError('shadow module was imported')\n",
        encoding="utf-8",
    )
    monkeypatch.syspath_prepend(str(tmp_path))
    lint.read(linters("external"))

    result = lint.roll(files)
    assert result.failed == set()
    assert len(result.issues) == 1


def test_roll_with_global_excluded_path(lint, linters, files):
    lint.exclude = ["**/foobar.js"]
    lint.read(linters("string", "regex", "external"))
    result = lint.roll(files)

    assert len(result.issues) == 0
    assert result.failed == set([])


def test_roll_with_local_excluded_path(lint, linters, files):
    lint.read(linters("excludes"))
    result = lint.roll(files)

    assert "**/foobar.js" in lint.linters[0]["local_exclude"]
    assert len(result.issues) == 0
    assert result.failed == set([])


def test_roll_with_no_files_to_lint(lint, linters, capfd):
    lint.read(linters("string", "regex", "external"))
    lint.mock_vcs([])
    result = lint.roll([], workdir=True)
    assert isinstance(result, ResultSummary)
    assert len(result.issues) == 0
    assert len(result.failed) == 0

    out, err = capfd.readouterr()
    assert "WARNING: no files linted" in err


def test_roll_with_invalid_extension(lint, linters, filedir):
    lint.read(linters("external"))
    result = lint.roll(os.path.join(filedir, "foobar.py"))
    assert len(result.issues) == 0
    assert result.failed == set([])


def test_roll_with_failure_code(lint, linters, files):
    lint.read(linters("badreturncode"))

    result = lint.roll(files, num_procs=1)
    assert len(result.issues) == 0
    assert result.failed == set(["BadReturnCodeLinter"])


def test_roll_warnings(lint, linters, files):
    lint.read(linters("warning"))
    result = lint.roll(files)
    assert len(result.issues) == 0
    assert result.total_issues == 0
    assert len(result.suppressed_warnings) == 1
    assert result.total_suppressed_warnings == 2

    lint.lintargs["show_warnings"] = True
    result = lint.roll(files)
    assert len(result.issues) == 1
    assert result.total_issues == 2
    assert len(result.suppressed_warnings) == 0
    assert result.total_suppressed_warnings == 0


def test_roll_code_review(monkeypatch, linters, files):
    monkeypatch.setenv("CODE_REVIEW", "1")
    lint = LintRoller(root=here, show_warnings=False)
    lint.read(linters("warning"))
    result = lint.roll(files)
    assert len(result.issues) == 1
    assert result.total_issues == 2
    assert len(result.suppressed_warnings) == 0
    assert result.total_suppressed_warnings == 0
    assert result.returncode == 1


def test_roll_code_review_warnings_disabled(monkeypatch, linters, files):
    monkeypatch.setenv("CODE_REVIEW", "1")
    lint = LintRoller(root=here, show_warnings=False)
    lint.read(linters("warning_no_code_review"))
    result = lint.roll(files)
    assert len(result.issues) == 0
    assert result.total_issues == 0
    assert lint.result.fail_on_warnings is True
    assert len(result.suppressed_warnings) == 1
    assert result.total_suppressed_warnings == 2
    assert result.returncode == 0


def test_roll_code_review_warnings_soft(linters, files):
    lint = LintRoller(root=here, show_warnings="soft")
    lint.read(linters("warning_no_code_review"))
    result = lint.roll(files)
    assert len(result.issues) == 1
    assert result.total_issues == 2
    assert lint.result.fail_on_warnings is False
    assert len(result.suppressed_warnings) == 0
    assert result.total_suppressed_warnings == 0
    assert result.returncode == 0


def _linter_filtered_files(files, linters):
    extensions = set()
    extensions.update(*[linter.get("extensions", []) for linter in linters])
    return [
        f
        for f in files
        if os.path.isdir(f) or any(f.endswith(ext) for ext in extensions)
    ]


def fake_run_worker(config, paths, **lintargs):
    result = ResultSummary(lintargs["root"])
    result.issues["count"].append(1)
    return result


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="monkeypatch issues with multiprocessing on Windows",
)
@pytest.mark.parametrize("num_procs", [1, 4, 8, 16])
def test_number_of_jobs(monkeypatch, lint, linters, files, num_procs):
    monkeypatch.setattr(sys.modules[lint.__module__], "_run_worker", fake_run_worker)

    linters = linters("string", "regex", "external")
    lint.read(linters)
    num_jobs = len(lint.roll(files, num_procs=num_procs).issues["count"])

    filtered_files = _linter_filtered_files(files, lint.linters)
    if len(filtered_files) >= num_procs:
        assert num_jobs == num_procs * len(linters)
    else:
        assert num_jobs == len(filtered_files) * len(linters)


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="monkeypatch issues with multiprocessing on Windows",
)
@pytest.mark.parametrize("max_paths,expected_jobs", [(1, 9), (4, 6), (16, 6)])
def test_max_paths_per_job(monkeypatch, lint, linters, files, max_paths, expected_jobs):
    monkeypatch.setattr(sys.modules[lint.__module__], "_run_worker", fake_run_worker)

    files = files[:4]
    assert len(files) == 4

    linters = linters("string", "regex", "external")[:3]
    assert len(linters) == 3

    lint.MAX_PATHS_PER_JOB = max_paths
    lint.read(linters)
    num_jobs = len(lint.roll(files, num_procs=2).issues["count"])
    assert num_jobs == expected_jobs


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="monkeypatch issues with multiprocessing on Windows",
)
@pytest.mark.parametrize("num_procs", [1, 4, 8, 16])
def test_number_of_jobs_global(monkeypatch, lint, linters, files, num_procs):
    monkeypatch.setattr(sys.modules[lint.__module__], "_run_worker", fake_run_worker)

    linters = linters("global")
    lint.read(linters)
    num_jobs = len(lint.roll(files, num_procs=num_procs).issues["count"])

    assert num_jobs == 1


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="monkeypatch issues with multiprocessing on Windows",
)
@pytest.mark.parametrize("max_paths", [1, 4, 16])
def test_max_paths_per_job_global(monkeypatch, lint, linters, files, max_paths):
    monkeypatch.setattr(sys.modules[lint.__module__], "_run_worker", fake_run_worker)

    files = files[:4]
    assert len(files) == 4

    linters = linters("global")[:1]
    assert len(linters) == 1

    lint.MAX_PATHS_PER_JOB = max_paths
    lint.read(linters)
    num_jobs = len(lint.roll(files, num_procs=2).issues["count"])
    assert num_jobs == 1


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="signal.CTRL_C_EVENT isn't causing a KeyboardInterrupt on Windows",
)
def test_keyboard_interrupt():
    # We use two linters so we'll have two jobs. One (string.yml) will complete
    # quickly. The other (slow.yml) will run slowly.  This way the first worker
    # will be be stuck blocking on the ProcessPoolExecutor._call_queue when the
    # signal arrives and the other still be doing work.
    cmd = [sys.executable, "runcli.py", "-l=string", "-l=slow", "files/foobar.js"]
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(sys.path)
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=here,
        env=env,
        universal_newlines=True,
    )
    time.sleep(1)
    proc.send_signal(signal.SIGINT)

    out = proc.communicate()[0]
    print(out)
    assert "WARNING: \nnot all files were linted" in out
    assert "2 problems" in out
    assert "Traceback" not in out


@pytest.mark.parametrize(
    "pattern,expected",
    [
        ("foobar.py", True),
        ("files/foobar.py", False),
        ("**/foobar.py", True),
        ("*.js", False),
    ],
)
def test_support_files_relative(lint, filedir, pattern, expected):
    lint.root = filedir
    linter = {"support-files": [pattern]}
    vcs_paths = {os.path.join(filedir, "foobar.py")}
    assert lint.should_lint_entire_tree(vcs_paths, linter) == expected


def test_support_files(lint, linters, filedir, monkeypatch, files):
    jobs = []

    # Replace the original _generate_jobs with a new one that simply
    # adds jobs to a list (and then doesn't return anything).
    orig_generate_jobs = lint._generate_jobs

    def fake_generate_jobs(*args, **kwargs):
        jobs.extend([job[1] for job in orig_generate_jobs(*args, **kwargs)])
        return []

    monkeypatch.setattr(lint, "_generate_jobs", fake_generate_jobs)

    linter_path = linters("support_files")[0]
    lint.read(linter_path)
    lint.root = filedir

    # Modified support files only lint entire root if --outgoing or --workdir
    # are used.
    path = os.path.join(filedir, "foobar.js")
    vcs_path = os.path.join(filedir, "foobar.py")

    lint.mock_vcs([vcs_path])
    lint.roll(path)
    actual_files = sorted(chain(*jobs))
    assert actual_files == [path]

    expected_files = sorted(_linter_filtered_files(files, lint.linters))

    jobs = []
    lint.roll(path, workdir=True)
    actual_files = sorted(chain(*jobs))
    assert actual_files == expected_files

    jobs = []
    lint.roll(path, outgoing=True)
    actual_files = sorted(chain(*jobs))
    assert actual_files == expected_files

    jobs = []
    lint.roll(path, rev='draft() and keyword("dummy revset expression")')
    actual_files = sorted(chain(*jobs))
    assert actual_files == expected_files

    # Lint config file is implicitly added as a support file
    lint.mock_vcs([linter_path])
    jobs = []
    lint.roll(path, outgoing=True, workdir=True)
    actual_files = sorted(chain(*jobs))
    assert actual_files == expected_files

    # Avoid linting the entire root when `--fix` is passed.
    lint.mock_vcs([vcs_path])
    lint.lintargs["fix"] = True

    jobs = []
    lint.roll(path, outgoing=True)
    actual_files = sorted(chain(*jobs))
    assert actual_files == [path], (
        "`--fix` with `--outgoing` on a `support-files` change should "
        "avoid linting the entire root."
    )

    jobs = []
    lint.roll(path, workdir=True)
    actual_files = sorted(chain(*jobs))
    assert actual_files == [path], (
        "`--fix` with `--workdir` on a `support-files` change should "
        "avoid linting the entire root."
    )

    jobs = []
    lint.roll(path, rev='draft() and keyword("dummy revset expression")')
    actual_files = sorted(chain(*jobs))
    assert actual_files == [path], (
        "`--fix` with `--rev` on a `support-files` change should "
        "avoid linting the entire root."
    )


@pytest.mark.parametrize(
    "find_dotfiles,expected",
    [
        (False, ["a.js", "sub/b.js", "sub/deep"]),
        (True, [".hidden", "a.js", "sub/.d.js", "sub/b.js", "sub/deep"]),
    ],
)
def test_split_dirs(tmp_path, find_dotfiles, expected):
    for path in (".hidden/x.js", "a.js", "sub/.d.js", "sub/b.js", "sub/deep/c.js"):
        (tmp_path / "top" / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / "top" / path).write_text("")
    top = tmp_path / "top"
    split = _split_dirs([str(top)], find_dotfiles, depth=2)
    assert [os.path.relpath(p, top) for p in split] == [
        os.path.normpath(p) for p in expected
    ]


def test_split_dirs_depth(tmp_path, lint):
    for path in ("a.js", "sub/b.js", "sub/deep/c.js"):
        (tmp_path / "top" / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / "top" / path).write_text("")
    top = str(tmp_path / "top")
    lint.root = str(tmp_path)
    lint.linters = [
        {"name": "depth", "type": "external", "include": [top], "split-dirs": 1}
    ]
    jobs = lint._generate_jobs({top}, set(), num_procs=8)
    paths = sorted(os.path.relpath(p, top) for _, ps in jobs for p in ps)
    assert paths == ["a.js", "sub"]


def test_split_dirs_filtered(tmp_path, lint):
    for path in ("a.js", "b.py", "skip/c.js", "sub/d.js"):
        (tmp_path / "top" / path).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / "top" / path).write_text("")
    top = str(tmp_path / "top")
    lint.root = str(tmp_path)
    lint.linters = [
        {
            "name": "filtered",
            "type": "external",
            "include": [top],
            "exclude": ["top/skip"],
            "extensions": ["js"],
            "split-dirs": 1,
        }
    ]
    jobs = lint._generate_jobs({top}, set(), num_procs=8)
    paths = sorted(os.path.relpath(p, top) for _, ps in jobs for p in ps)
    assert paths == ["a.js", "sub"]


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="monkeypatch issues with multiprocessing on Windows",
)
def test_split_dirs_jobs(monkeypatch, lint, linters, filedir):
    monkeypatch.setattr(sys.modules[lint.__module__], "_run_worker", fake_run_worker)

    lint.read(linters("string"))
    lint.linters[0]["split-dirs"] = 2
    num_jobs = len(lint.roll(filedir, num_procs=16).issues["count"])
    assert num_jobs == 2


def is_js(path):
    return path.endswith(".js")


def auto_jobs(lint, tmp_path, files, **linter):
    top = tmp_path / "top"
    for path in files:
        (top / path).parent.mkdir(parents=True, exist_ok=True)
        (top / path).write_text("")
    lint.root = str(tmp_path)
    lint.linters = [
        {
            "name": "auto",
            "type": "external",
            "include": [str(top)],
            "split-dirs": "auto",
            **linter,
        }
    ]
    jobs = lint._generate_jobs({str(top)}, set(), num_procs=2)
    return [sorted(mozpath.relpath(p, str(top)) for p in ps) for _, ps in jobs]


def test_balanced_jobs(tmp_path):
    top = tmp_path / "top"
    files = [f"big/{d}/{i}.js" for d in ("x", "y") for i in range(4)]
    files += [f"vendor/{i}.js" for i in range(20)]
    files += ["small/a.js", "small/b.py", "skip/c.js", ".dot/d.js"]
    for path in files:
        (top / path).parent.mkdir(parents=True, exist_ok=True)
        (top / path).write_text("")
    paths = [str(top / name) for name in ("big", "small", "skip", "vendor", ".dot")]
    jobs = _balanced_jobs(
        paths,
        2,
        str(tmp_path),
        is_js,
        mozpath.matcher(["top/skip"]),
        mozpath.matcher(["top/vendor"]),
        True,
        200000,
    )
    rel = [sorted(mozpath.relpath(p, str(top)) for p in job) for job in jobs]
    assert rel == [["big/x", "small"], [".dot", "big/y", "vendor"]]


def test_balanced_jobs_max_chars(tmp_path):
    top = tmp_path / "top"
    for name in ("a", "b", "c"):
        (top / name).mkdir(parents=True)
        (top / name / "f.js").write_text("")
    paths = [str(top / name) for name in ("a", "b", "c")]
    size = len(paths[0]) + 1
    none = mozpath.matcher([])
    jobs = _balanced_jobs(paths, 1, str(tmp_path), is_js, none, none, False, size)
    assert sorted(len(job) for job in jobs) == [1, 1, 1]


def test_balanced_jobs_max_chars_files(tmp_path):
    top = tmp_path / "top"
    top.mkdir()
    for i in range(10):
        (top / f"{i}.js").write_text("")
    size = 3 * (len(str(top / "0.js")) + 1)
    none = mozpath.matcher([])
    jobs = _balanced_jobs([str(top)], 2, str(tmp_path), is_js, none, none, False, size)
    rel = sorted(sorted(mozpath.relpath(p, str(top)) for p in job) for job in jobs)
    assert rel == [
        ["0.js", "1.js", "2.js"],
        ["3.js", "4.js", "5.js"],
        ["6.js", "7.js", "8.js"],
        ["9.js"],
    ]


def test_balanced_jobs_no_filter(tmp_path, lint):
    lint.lintargs["use_filters"] = False
    jobs = auto_jobs(
        lint, tmp_path, ["skip/a.js"], exclude=["top/skip"], extensions=["js"]
    )
    assert jobs == [["skip/a.js"]]


def test_balanced_jobs_exclude_extensions(tmp_path, lint):
    files = [f"{ext}/{i}.{ext}" for ext in ("js", "py") for i in range(4)]
    jobs = auto_jobs(lint, tmp_path, files, exclude_extensions=["py"])
    assert jobs == [["js/0.js", "js/1.js", "py"], ["js/2.js", "js/3.js"]]


def test_balanced_jobs_ignore_files(tmp_path, lint):
    (tmp_path / "ignores.txt").write_text("top/vendor/\n\n")
    files = [f"{d}/{i}.js" for d in ("app", "vendor") for i in range(4)]
    jobs = auto_jobs(
        lint,
        tmp_path,
        files,
        extensions=["js"],
        **{"split-ignore-files": ["ignores.txt"]},
    )
    assert jobs == [["app/0.js", "app/1.js", "vendor"], ["app/2.js", "app/3.js"]]


def test_read_ignore_patterns(tmp_path):
    ignores = tmp_path / "ignores.mjs"
    ignores.write_text(
        "export default [\n"
        '  "!.*.js",\n'
        '  "*.ts",\n'
        '  "**/reftests/",\n'
        '  "js/src/jit-test/", // comment\n'
        "];\n"
    )
    assert _read_ignore_patterns(str(ignores)) == [
        "**/*.ts",
        "**/reftests",
        "js/src/jit-test",
    ]


def test_read_ignore_patterns_txt(tmp_path):
    ignores = tmp_path / "ignores.txt"
    ignores.write_text("third_party/rust/\n\n  js/src/jit-test\n")
    assert _read_ignore_patterns(str(ignores)) == [
        "third_party/rust",
        "js/src/jit-test",
    ]


def test_group_jobs(filedir):
    a = {"name": "a"}
    b = {"name": "b"}
    foobar = os.path.join(filedir, "foobar.js")
    no_foobar = os.path.join(filedir, "no_foobar.js")
    irrelevant = os.path.join(filedir, "irrelevant")
    jobs = [
        (a, [foobar]),
        (a, [no_foobar]),
        (b, [filedir]),
        (b, [foobar]),
        (b, [irrelevant]),
        (a, [f"{filedir}_other"]),
    ]
    assert _group_jobs(jobs) == [
        [jobs[0], jobs[1], jobs[4], jobs[5]],
        [jobs[2]],
        [jobs[3]],
    ]


def test_setup(lint, linters, filedir, capfd):
    with pytest.raises(NoValidLinter):
        lint.setup()

    lint.read(linters("setup", "setupfailed", "setupraised", "setupskipped"))
    ret = lint.setup()
    assert ret == 1

    out, err = capfd.readouterr()
    assert "oh no setup failed" in err
    assert "ERROR: problem with lint setup, skipping" in err
    assert lint.result.failed_setup == set(["SetupFailedLinter", "SetupRaisedLinter"])


def test_setup_all_skipped(lint, linters, filedir, capfd):
    lint.read(linters("setupskipped"))
    ret = lint.setup()
    assert ret == 1

    out, err = capfd.readouterr()
    assert "ERROR: all linters were skipped due to setup, nothing to do!" in err
    assert lint.result.failed_setup == set()


def test_setup_roll(lint, linters, filedir, capfd):
    lint.read(linters("setup"))
    lint.roll(os.path.join(filedir, "foobar.py"))
    out, err = capfd.readouterr()
    assert "setup passed" not in out

    lint.roll(os.path.join(filedir, "foobar.js"))
    out, err = capfd.readouterr()
    assert "setup passed" in out


def test_setup_roll_failed(lint, linters, filedir):
    lint.read(linters("setupfailed"))
    result = lint.roll(os.path.join(filedir, "foobar.js"))
    assert result.failed_setup == set(["SetupFailedLinter"])
    assert result.returncode == 1


if __name__ == "__main__":
    mozunit.main()
