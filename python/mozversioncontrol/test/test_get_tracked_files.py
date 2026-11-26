# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re
from pathlib import Path

import mozunit
import pytest

from mozversioncontrol import get_repository_object


def create_files(working_dir):
    (Path(working_dir) / ".gitignore").touch()
    for path in ("a/x.idl", "a/b/y.idl", "a/b/z.txt"):
        fullpath = Path(working_dir) / path
        fullpath.parent.mkdir(parents=True, exist_ok=True)
        fullpath.write_text("x\n", encoding="utf-8")


STEPS = {
    "hg": [create_files, "hg add a"],
    "git": [create_files, "git add a"],
    "jj": [create_files],
    "src": [create_files],
}


def test_get_tracked_files(repo):
    for _ in STEPS[repo.vcs]:
        repo.execute_next_step()

    vcs = get_repository_object(repo.dir)
    assert vcs.get_tracked_files(["**/*.idl"]) == ["a/b/y.idl", "a/x.idl"]
    assert vcs.get_tracked_files(["a/*.idl", "**/z.txt"]) == ["a/b/z.txt", "a/x.idl"]
    assert vcs.get_tracked_files(["**/*.missing"]) == []
    assert vcs.get_tracked_files([]) == []
    for pattern in ("a/?.idl", "a\\*.idl", 'a/"*.idl'):
        with pytest.raises(
            ValueError, match=f"Unsupported character.*{re.escape(pattern)}"
        ):
            vcs.get_tracked_files([pattern])

    (Path(repo.dir) / "a/x.idl").unlink()
    assert vcs.get_tracked_files(["**/*.idl"]) == ["a/b/y.idl"]

    with vcs:
        assert vcs.get_tracked_files(["**/*.idl"]) == ["a/b/y.idl"]
        assert vcs.get_tracked_files(["**/*.missing"]) == []


def test_get_tracked_files_finder(repo):
    for _ in STEPS[repo.vcs]:
        repo.execute_next_step()

    vcs = get_repository_object(repo.dir)
    finder = vcs.get_tracked_files_finder()
    assert sorted(p for p, _ in finder.find("**/*.idl")) == ["a/b/y.idl", "a/x.idl"]


if __name__ == "__main__":
    mozunit.main()
