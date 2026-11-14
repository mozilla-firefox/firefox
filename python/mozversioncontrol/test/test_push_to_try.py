# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import textwrap
import uuid

import mozunit
import pytest

from mozversioncontrol import MissingVCSExtension, get_repository_object


def test_push_to_try(repo, monkeypatch):
    if repo.vcs == "src":
        pytest.skip("src repo cannot push")

    commit_message = "commit message"
    vcs = get_repository_object(repo.dir)

    captured_commands = []
    captured_inputs = []

    def fake_run(*args, **kwargs):
        cmd = args[0]
        captured_commands.append(cmd)
        if cmd[1] == "var" and cmd[2] in ("GIT_AUTHOR_IDENT", "GIT_COMMITTER_IDENT"):
            return "FooBar <foobar@example.com> 0 +0000"
        if cmd[1:] == ("rev-parse", "HEAD"):
            return "0987654321098765432109876543210987654321"
        if cmd[1:] == ("fast-import", "--quiet"):
            if input := kwargs.get("input"):
                captured_inputs.append(input)
            return "1234567890123456789012345678901234567890"
        if os.path.basename(cmd[0]).startswith("hg") and cmd[1] == "--version":
            return "version 6.7"
        return ""

    def fake_subprocess_run(*args, **kwargs):
        return subprocess.CompletedProcess(args[0], 0, stdout=fake_run(*args, **kwargs))

    def normalize_fake_run(*args, **kwargs):
        if (
            kwargs.get("text")
            or kwargs.get("universal_newlines")
            or kwargs.get("encoding")
        ):
            return fake_run(*args, **kwargs)
        if input := kwargs.get("input"):
            kwargs["input"] = input.decode("utf-8")
        return fake_run(*args, **kwargs).encode("utf-8")

    def fake_uuid():
        return "974284fd-f395-4a15-a9d7-814a71241242"

    monkeypatch.setattr(subprocess, "run", fake_subprocess_run)
    monkeypatch.setattr(subprocess, "check_output", normalize_fake_run)
    monkeypatch.setattr(subprocess, "check_call", normalize_fake_run)
    monkeypatch.setattr(uuid, "uuid4", fake_uuid)

    vcs.push_to_try(
        commit_message,
        {
            "extra-file": "content",
            "other/extra-file": "content2",
        },
    )
    tool = vcs._tool

    if repo.vcs == "hg":
        expected = [
            (str(tool), "--version"),
            (
                str(tool),
                "--config",
                "extensions.automv=",
                "addremove",
                os.path.join(vcs.path, "extra-file"),
                os.path.join(vcs.path, "other", "extra-file"),
            ),
            (str(tool), "push-to-try", "--message", commit_message),
            (str(tool), "revert", "--all"),
        ]
        expected_inputs = []
    elif repo.vcs == "git":
        expected = [
            (str(tool), "cinnabar", "--version"),
            (str(tool), "rev-parse", "HEAD"),
            (str(tool), "var", "GIT_AUTHOR_IDENT"),
            (str(tool), "var", "GIT_COMMITTER_IDENT"),
            (str(tool), "fast-import", "--quiet"),
            (
                str(tool),
                "-c",
                "cinnabar.data=never",
                "push",
                "hg::ssh://hg.mozilla.org/try",
                "+1234567890123456789012345678901234567890:refs/heads/branches/default/tip",
            ),
            (
                str(tool),
                "update-ref",
                "-m",
                "mach try: push",
                "HEAD",
                "1234567890123456789012345678901234567890",
                "0987654321098765432109876543210987654321",
            ),
            (
                str(tool),
                "update-ref",
                "-m",
                "mach try: restore",
                "HEAD",
                "0987654321098765432109876543210987654321",
                "1234567890123456789012345678901234567890",
            ),
        ]
        expected_inputs = [
            textwrap.dedent(
                f"""\
                commit refs/machtry/974284fd-f395-4a15-a9d7-814a71241242
                mark :1
                author FooBar <foobar@example.com> 0 +0000
                committer FooBar <foobar@example.com> 0 +0000
                data {len(commit_message)}
                {commit_message}
                from 0987654321098765432109876543210987654321
                M 100644 inline extra-file
                data 7
                content
                M 100644 inline other/extra-file
                data 8
                content2
                reset refs/machtry/974284fd-f395-4a15-a9d7-814a71241242
                from 0000000000000000000000000000000000000000
                get-mark :1
            """
            ),
        ]
    else:
        assert repo.vcs == "jj"
        expected = [
            (str(vcs._git._tool), "cinnabar", "--version"),
            (
                str(tool),
                "--quiet",
                "operation",
                "log",
                "--limit=1",
                "--no-graph",
                "--template",
                "id.short(16)",
            ),
            (
                str(tool),
                "--quiet",
                "log",
                "--no-graph",
                "--revisions",
                "heads(trunk() | (remote_bookmarks() & ancestors(@)))..@ ~ description(exact:'')",
                "--template",
                "'  ' ++ description.first_line() ++ '\n'",
            ),
            (
                str(tool),
                "--quiet",
                "log",
                "--limit=0",
                "--template",
                '"snapshot: prepare_try_push"',
            ),
            (
                str(tool),
                "--quiet",
                "operation",
                "log",
                "--limit=1",
                "--no-graph",
                "--template",
                "id.short(16)",
            ),
            (
                str(tool),
                "--quiet",
                "new",
                "--message",
                "commit message",
                'coalesce(@ ~ (empty() & description(exact:"") ~ bookmarks()), @-)',
            ),
            (str(tool), "--quiet", "file", "track", "extra-file"),
            (str(tool), "--quiet", "file", "track", "other/extra-file"),
            (
                str(tool),
                "--quiet",
                "log",
                "--limit=0",
                "--template",
                '"snapshot: prepare_try_push"',
            ),
            (
                str(tool),
                "--quiet",
                "bookmark",
                "move",
                "--from",
                "heads(@- & bookmarks())",
                "--to",
                "@",
            ),
            (
                str(tool),
                "--quiet",
                "--ignore-working-copy",
                "log",
                "--no-graph",
                "--limit=1",
                "--revisions",
                "@",
                "--template",
                "change_id.short()",
            ),
            (str(vcs._git._tool), "remote"),
            (
                str(vcs._git._tool),
                "remote",
                "add",
                "mach_tryserver",
                "hg::ssh://hg.mozilla.org/try",
            ),
            (str(tool), "--quiet", "git", "import"),
            (
                str(tool),
                "--quiet",
                "git",
                "push",
                "--remote",
                "mach_tryserver",
                "--change",
                None,
                "--allow-empty-description",
            ),
            (str(tool), "--quiet", "operation", "restore", ""),
            (str(tool), "--quiet", "git", "remote", "remove", "mach_tryserver"),
        ]
        expected_inputs = []

    for i, value in enumerate(captured_commands):
        assert value == expected[i]

    assert len(captured_commands) == len(expected)

    for i, value in enumerate(captured_inputs):
        assert value == expected_inputs[i]

    assert len(captured_inputs) == len(expected_inputs)


def test_push_to_git_try(repo, mocker):
    if repo.vcs not in ("git", "jj"):
        pytest.skip("pushing to git try only applies to git/jj")

    vcs = get_repository_object(repo.dir)
    remote = "upstream"

    mock_try_commit = mocker.patch.object(vcs, "try_commit")
    mock_try_commit.return_value.__enter__.return_value = "fakehead"
    mock_push = mocker.patch.object(vcs, "push")

    vcs.push_to_try("msg", remote=remote)

    mock_push.assert_called_once_with(
        remote, ref="fakehead", dest_branch="user/test/master", force=True
    )


def test_push_to_git_try_creates_bookmark(repo, mocker):
    if repo.vcs != "jj":
        pytest.skip("bookmark creation only applies to jj")

    vcs = get_repository_object(repo.dir)
    remote = "upstream"

    mocker.patch.object(
        type(vcs), "branch", new_callable=mocker.PropertyMock, return_value=None
    )

    def fake_run_read_only(*args, **kwargs):
        if args[:3] == ("config", "get", "user.email"):
            return "test@example.org"
        if args[0] == "config":
            return ""
        if args[0] == "log":
            return "push-abc123\n"
        return ""

    mocker.patch.object(vcs, "_run_read_only", side_effect=fake_run_read_only)
    mock_run = mocker.patch.object(vcs, "_run")
    mock_try_commit = mocker.patch.object(vcs, "try_commit")
    mock_try_commit.return_value.__enter__.return_value = "fakehead"
    mock_push = mocker.patch.object(vcs, "push")

    vcs.push_to_try("msg", remote=remote)

    mock_run.assert_called_once_with(
        "bookmark", "create", "push-abc123", "--revision", vcs.HEAD_REVSET
    )
    mock_push.assert_called_once_with(
        remote, ref="fakehead", dest_branch="user/test/push-abc123", force=True
    )


def test_push_to_git_try_bookmark_persists(repo, mocker):
    if repo.vcs != "jj":
        pytest.skip("bookmark persistence only applies to jj")

    subprocess.check_call(
        ["jj", "new", "--message", "test commit"],
        cwd=repo.dir,
        env={**os.environ, "JJ_CONFIG": ""},
    )

    vcs = get_repository_object(repo.dir)
    assert vcs.branch is None

    mocker.patch.object(vcs, "push")

    vcs._push_to_git_try("msg", {}, "upstream")

    output = vcs._run_read_only(
        "log",
        "--no-graph",
        "--limit=1",
        "--revisions",
        "@",
        "--template",
        'local_bookmarks.join("\n")',
    )
    bookmark = output.split("\n")[0].strip()
    assert bookmark.startswith("push-")


def test_push_to_try_missing_extensions(repo, monkeypatch):
    if repo.vcs not in ("git", "jj"):
        return

    vcs = get_repository_object(repo.dir)

    orig = vcs._run

    def cinnabar_raises(*args, **kwargs):
        # Simulate not having git cinnabar
        if args[0] == "cinnabar":
            raise subprocess.CalledProcessError(1, args)
        return orig(*args, **kwargs)

    monkeypatch.setattr(vcs, "_run", cinnabar_raises)
    if hasattr(vcs, "_git"):
        monkeypatch.setattr(vcs._git, "_run", cinnabar_raises)

    with pytest.raises(MissingVCSExtension):
        vcs.push_to_try("commit message")


if __name__ == "__main__":
    mozunit.main()
