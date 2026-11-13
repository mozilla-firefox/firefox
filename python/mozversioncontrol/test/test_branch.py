# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        hg bookmark test
        """,
        """
        echo "bar" > foo
        hg commit -m "second commit"
        """,
    ],
    "git": [
        """
        git checkout -b test
        """,
        """
        echo "bar" > foo
        git commit -a -m "second commit"
        """,
    ],
    "jj": [],
}


def test_branch(repo):
    vcs = get_repository_object(repo.dir)
    if vcs.name == "jj":
        mozunit.pytest.skip("jj does not have an active branch")

    if vcs.name == "git":
        assert vcs.branch == "master"
    else:
        assert vcs.branch is None

    repo.execute_next_step()
    assert vcs.branch == "test"

    repo.execute_next_step()
    assert vcs.branch == "test"

    vcs.update(vcs.head_rev)
    assert vcs.branch is None

    vcs.update("test")
    assert vcs.branch == "test"


def test_jj_branch_diverged_bookmark(repo):
    """jj renders a bookmark that diverges from its remote-tracking target with a
    trailing "*". `branch` must return the bare name so it stays usable as a
    revset (bug: `mach try perf` failed with "Revision `foo*` doesn't exist")."""
    vcs = get_repository_object(repo.dir)
    if vcs.name != "jj":
        mozunit.pytest.skip("jj-specific")

    # `master` tracks `master@upstream`. Moving the local bookmark ahead of the
    # remote target makes jj decorate it as diverged.
    vcs._run("describe", "--message", "local work")
    vcs._run("bookmark", "set", "master", "--revision", "@")

    assert vcs.branch == "master"
    # The returned name must round-trip as a revset; this is what used to fail.
    vcs.update(vcs.branch)


def test_head_rev(repo):
    vcs = get_repository_object(repo.dir)
    assert re.fullmatch(r"[0-9a-f]{40}", vcs.head_rev)


if __name__ == "__main__":
    mozunit.main()
