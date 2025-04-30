# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import mozunit
import pytest

from mozversioncontrol import get_repository_object
from mozversioncontrol.errors import MissingVCSExtension


def test_try_commit(repo):
    commit_message = "try commit message"
    vcs = get_repository_object(repo.dir)
    initial_head_ref = vcs.head_ref

    # Create a non-empty commit.
    try:
        with vcs.try_commit(commit_message, {"try_task_config.json": "{}"}) as head:
            assert vcs.get_changed_files(rev=head) == ["try_task_config.json"]
    except MissingVCSExtension:
        pytest.xfail("Requires the Mercurial evolve extension.")

    assert (
        vcs.head_ref == initial_head_ref
    ), "We should have reverted to previous head after try_commit"

    # Create an empty commit.
    with vcs.try_commit(commit_message) as head:
        assert vcs.get_changed_files(rev=head) == []

    assert (
        vcs.head_ref == initial_head_ref
    ), "We should have reverted to previous head after try_commit"


if __name__ == "__main__":
    mozunit.main()
