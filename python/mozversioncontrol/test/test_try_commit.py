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
            if vcs.name != "src":
                assert vcs.get_changed_files(rev=head) == ["try_task_config.json"]
    except MissingVCSExtension:
        pytest.xfail("Requires the Mercurial evolve extension.")

    assert vcs.head_ref == initial_head_ref, (
        "We should have reverted to previous head after try_commit"
    )

    # Create an empty commit.
    with vcs.try_commit(commit_message) as head:
        assert vcs.get_changed_files(rev=head) == []

    assert vcs.head_ref == initial_head_ref, (
        "We should have reverted to previous head after try_commit"
    )


@pytest.mark.parametrize("repo", ["jj"], indirect=True)
@pytest.mark.parametrize("bookmarked", [False, True])
@pytest.mark.parametrize("empty", [False, True])
@pytest.mark.parametrize("described", [False, True])
def test_jj_try_commit_preserves_working_commit(repo, bookmarked, empty, described):
    vcs = get_repository_object(repo.dir)
    (repo.dir / "bar").write_text("first patch")
    vcs._run("commit", "--message", "first patch")

    if not empty:
        (repo.dir / "working-patch").write_text("working patch")
    vcs._run("describe", "--message", "working patch" if described else "")
    if bookmarked:
        vcs._run("bookmark", "create", "working-patch", "--revision", "@")

    original_head = vcs._resolve_to_commit("@")
    expected_head = (
        original_head
        if not empty or described or bookmarked
        else vcs._resolve_to_commit("@-")
    )
    assert vcs.head_rev == expected_head

    with vcs.try_commit("try config", {"try_task_config.json": "{}"}) as head:
        assert vcs._resolve_to_commit(f"{head}-") == expected_head
        nodes = vcs.get_commits(head)
        assert len(nodes) == (2 if empty else 3)
        if not empty:
            assert original_head in nodes
            assert (repo.dir / "working-patch").read_text() == "working patch"

    assert vcs._resolve_to_commit("@") == original_head
    if bookmarked:
        assert vcs._resolve_to_commit("working-patch") == original_head


if __name__ == "__main__":
    mozunit.main()
