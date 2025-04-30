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
            (str(tool), "push-to-try", "-m", commit_message),
            (str(tool), "revert", "-a"),
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
                "--ignore-working-copy",
                "operation",
                "log",
                "-n1",
                "--no-graph",
                "-T",
                "id.short(16)",
            ),
            (str(tool), "new", "-m", "commit message", "latest((@ | @-) ~ empty())"),
            (str(tool), "log", "-n0"),
            (
                str(tool),
                "--ignore-working-copy",
                "log",
                "--no-graph",
                "-n1",
                "-r",
                "@",
                "-T",
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
            (str(tool), "git", "import"),
            (
                str(tool),
                "git",
                "push",
                "--remote",
                "mach_tryserver",
                "--change",
                None,
                "--allow-new",
                "--allow-empty-description",
            ),
            (str(tool), "operation", "restore", ""),
            (str(tool), "git", "remote", "remove", "mach_tryserver"),
        ]
        expected_inputs = []

    for i, value in enumerate(captured_commands):
        assert value == expected[i]

    assert len(captured_commands) == len(expected)

    for i, value in enumerate(captured_inputs):
        assert value == expected_inputs[i]

    assert len(captured_inputs) == len(expected_inputs)


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
