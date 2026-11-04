# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

import mozpack.path as mozpath
import mozunit
import pytest

from mozlint.result import Issue, ResultSummary


@pytest.fixture
def path(filedir):
    def _path(name):
        return mozpath.join(filedir, name)

    return _path


@pytest.fixture(
    params=[
        "external.yml",
        "global.yml",
        "regex.yml",
        "string.yml",
        "structured.yml",
    ]
)
def linter(lintdir, request):
    return os.path.join(lintdir, request.param)


def test_linter_types(lint, linter, files, path):
    lint.read(linter)
    result = lint.roll(files)
    assert isinstance(result, ResultSummary)
    assert isinstance(result.issues, dict)
    assert path("foobar.js") in result.issues
    assert path("no_foobar.js") not in result.issues

    issue = result.issues[path("foobar.js")][0]
    assert isinstance(issue, Issue)

    name = os.path.basename(linter).split(".")[0]
    assert issue.linter.lower().startswith(name)


def test_linter_missing_files(lint, linter, filedir):
    # Missing files should be caught by `mozlint.cli`, so the only way they
    # could theoretically happen is if they show up from versioncontrol. So
    # let's just make sure they get ignored.
    lint.read(linter)
    files = [
        os.path.join(filedir, "missing.js"),
        os.path.join(filedir, "missing.py"),
    ]
    result = lint.roll(files)
    assert result.returncode == 0

    lint.mock_vcs(files)
    result = lint.roll(outgoing=True)
    assert result.returncode == 0


def test_no_filter(lint, lintdir, files):
    lint.read(os.path.join(lintdir, "explicit_path.yml"))
    result = lint.roll(files)
    assert len(result.issues) == 0

    lint.lintargs["use_filters"] = False
    result = lint.roll(files)
    assert len(result.issues) == 3


def test_global_skipped(lint, lintdir, files):
    lint.read(os.path.join(lintdir, "global_skipped.yml"))
    result = lint.roll(files)
    assert len(result.issues) == 0


def test_global_skipped_directory(lint, lintdir, filedir):
    lint.read(os.path.join(lintdir, "global_skipped.yml"))
    result = lint.roll([filedir])
    assert result.failed == set()
    assert len(result.issues) == 0


def test_global_directory_without_extensions(lint, lintdir, filedir, path):
    # A linter that declares no extensions has nothing to filter a directory
    # by, which is not the same as having nothing to lint. The payload lints
    # what it is handed, so these issues also show the directory was expanded.
    lint.read(os.path.join(lintdir, "global_no_extensions.yml"))
    result = lint.roll([filedir])
    assert result.failed == set()
    assert path("foobar.js") in result.issues
    assert path("foobar.py") in result.issues
    assert path("no_foobar.js") not in result.issues


def test_line_type_directory_extensions(lint, lintdir, filedir, path, tmp_path):
    (tmp_path / "foobar.d.ts").write_text("foobar\n")
    (tmp_path / "foobar.ts").write_text("foobar\n")
    lint.read(os.path.join(lintdir, "string.yml"))
    lint.linters[0]["extensions"] = ["js", "py", "d.ts"]
    lint.linters[0]["include"] = [".", str(tmp_path)]
    result = lint.roll([filedir, str(tmp_path)])
    assert path("foobar.js") in result.issues
    assert path("foobar.py") in result.issues
    assert mozpath.join(tmp_path, "foobar.d.ts") in result.issues
    assert path("irrelevant/file.txt") not in result.issues
    assert all(p.endswith((".js", ".py", ".d.ts")) for p in result.issues)


if __name__ == "__main__":
    mozunit.main()
