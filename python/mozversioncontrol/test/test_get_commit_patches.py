# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo bar >> bar
        hg commit -m "FIRST PATCH"
        """,
        """
        printf "baz\\r\\nqux" > baz
        hg add baz
        hg commit -m "SECOND PATCH"
        """,
    ],
    "git": [
        """
        echo bar >> bar
        git add bar
        git commit -m "FIRST PATCH"
        """,
        """
        printf "baz\\r\\nqux" > baz
        git -c core.autocrlf=false add baz
        git commit -m "SECOND PATCH"
        """,
    ],
    "jj": [
        """
        jj new -m "FIRST PATCH"
        echo bar >> bar
        """,
        """
        jj new -m "SECOND PATCH"
        printf "baz\\r\\nqux" > baz
        jj log -n0 # snapshot, since bug 1962245 suppresses automatic ones
       """,
    ],
}


def test_get_commit_patches(repo):
    vcs = get_repository_object(repo.dir)
    nodes = []

    # Create some commits and note the SHAs.
    repo.execute_next_step()
    nodes.append(vcs.head_ref)

    repo.execute_next_step()
    nodes.append(vcs.head_ref)

    patches = vcs.get_commit_patches(nodes)

    assert len(patches) == 2
    # Assert the patches are returned in the correct order.
    assert b"FIRST PATCH" in patches[0]
    assert b"SECOND PATCH" in patches[1]
    # Assert the CRLF are correctly preserved.
    assert b"baz\r\n" in patches[1]


if __name__ == "__main__":
    mozunit.main()
