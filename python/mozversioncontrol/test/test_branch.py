# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

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

    vcs.update(vcs.head_ref)
    assert vcs.branch is None

    vcs.update("test")
    assert vcs.branch == "test"


if __name__ == "__main__":
    mozunit.main()
