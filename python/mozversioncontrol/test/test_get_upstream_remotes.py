# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [],
    "git": [
        "git remote add blah https://example.com/blah",
        """
        git remote add unified hg::https://hg.mozilla.org/mozilla-unified
        git remote add central hg::https://hg.mozilla.org/central
        git remote add try hg::https://hg.mozilla.org/try
        """,
    ],
    "jj": [],
}


def test_get_upstream_remotes(repo):
    # Test is only relevant for Git.
    if not repo.vcs == "git":
        return

    repo.execute_next_step()

    vcs = get_repository_object(repo.dir)
    remotes = vcs.get_mozilla_upstream_remotes()

    assert list(remotes) == [], "No official remotes should be found."

    repo.execute_next_step()

    remotes = sorted(list(vcs.get_mozilla_upstream_remotes()))

    assert remotes == [
        "central",
        "unified",
    ], "Multiple non-try remotes should be found."


if __name__ == "__main__":
    mozunit.main()
