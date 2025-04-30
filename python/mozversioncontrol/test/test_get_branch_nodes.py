# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo bar >> bar
        hg commit -m "commit 1"
        echo baz > baz
        hg add baz
        hg commit -m "commit 2"
        """
    ],
    "git": [
        """
        echo bar >> bar
        git add bar
        git commit -m "commit 1"
        echo baz > baz
        git add baz
        git commit -m "commit 2"
        """
    ],
    "jj": [
        """
        jj new -m "commit 1"
        echo bar >> bar
        jj commit -m "commit 2"
        echo baz > baz
        jj log -n0 # snapshot, since mach's jj integration avoids doing this automatically
        """
    ],
}


def test_get_branch_nodes(repo):
    vcs = get_repository_object(repo.dir)

    # Create some commits
    repo.execute_next_step()

    # Get list of branch nodes.
    nodes = vcs.get_branch_nodes()

    assert len(nodes) == 2
    assert all(len(node) == 40 for node in nodes), "Each node should be a 40-char SHA."


if __name__ == "__main__":
    mozunit.main()
