# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from subprocess import CalledProcessError

import mozunit
import pytest

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo "bar" >> bar
        echo "baz" > foo
        hg commit -m "second commit"
        """,
        """
        echo "foobar" > foo
        """,
    ],
    "git": [
        """
        echo "bar" >> bar
        echo "baz" > foo
        git add *
        git commit -m "second commit"
        """,
        """
        echo "foobar" > foo
        """,
    ],
    "jj": [
        """
        echo "bar" >> bar
        echo "baz" > foo
        jj commit -m "second commit"
        """,
        """
        echo "foobar" > foo
        """,
    ],
}


def test_update(repo):
    vcs = get_repository_object(repo.dir)
    rev0 = vcs.head_ref

    # Create a commit with modified `foo` and `bar`.
    repo.execute_next_step()
    rev1 = vcs.head_ref
    assert rev0 != rev1

    if repo.vcs == "hg":
        vcs.update(".~1")
    elif repo.vcs == "git":
        vcs.update("HEAD~1")
    elif repo.vcs == "jj":
        vcs.edit("@-")
    assert vcs.head_ref == rev0

    if repo.vcs != "jj":
        vcs.update(rev1)
    else:
        vcs.update(rev0)
        rev1 = vcs.head_ref
    assert vcs.head_ref == rev1

    # Modify `foo` and update. Should fail with dirty working directory.
    repo.execute_next_step()
    if repo.vcs != "jj":
        with pytest.raises(CalledProcessError):
            vcs.update(rev0)
        assert vcs.head_ref == rev1
    else:
        # jj doesn't have a "dirty working directory".
        pass


if __name__ == "__main__":
    mozunit.main()
