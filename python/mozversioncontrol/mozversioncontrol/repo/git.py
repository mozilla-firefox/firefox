# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import subprocess
import uuid
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Union

from mozpack.files import FileListFinder

from mozversioncontrol.errors import (
    CannotDeleteFromRootOfRepositoryException,
    MissingVCSExtension,
)
from mozversioncontrol.repo.base import Repository


class GitRepository(Repository):
    """An implementation of `Repository` for Git repositories."""

    def __init__(self, path: Path, git="git"):
        super(GitRepository, self).__init__(path, tool=git)

    @property
    def name(self):
        return "git"

    @property
    def head_ref(self):
        return self._run("rev-parse", "HEAD").strip()

    def get_mozilla_upstream_remotes(self) -> Iterator[str]:
        """Return the Mozilla-official upstream remotes for this repo."""
        out = self._run("remote", "-v")
        if not out:
            return

        remotes = out.splitlines()
        if not remotes:
            return

        for line in remotes:
            name, url, action = line.split()

            # Only consider fetch sources.
            if action != "(fetch)":
                continue

            # Return any `hg.mozilla.org` remotes, ignoring `try`.
            if "hg.mozilla.org" in url and not url.endswith("hg.mozilla.org/try"):
                yield name

    def get_mozilla_remote_args(self) -> List[str]:
        """Return a list of `--remotes` arguments to limit commits to official remotes."""
        official_remotes = [
            f"--remotes={remote}" for remote in self.get_mozilla_upstream_remotes()
        ]

        return official_remotes if official_remotes else ["--remotes"]

    @property
    def base_ref(self):
        remote_args = self.get_mozilla_remote_args()

        refs = self._run(
            "rev-list", "HEAD", "--topo-order", "--boundary", "--not", *remote_args
        ).splitlines()
        if refs:
            return refs[-1][1:]  # boundary starts with a prefix `-`
        return self.head_ref

    def base_ref_as_hg(self):
        base_ref = self.base_ref
        try:
            return self._run("cinnabar", "git2hg", base_ref).strip()
        except subprocess.CalledProcessError:
            return

    @property
    def branch(self):
        # This mimics `git branch --show-current` for older versions of git.
        branch = self._run("symbolic-ref", "-q", "HEAD", return_codes=[0, 1]).strip()
        if not branch.startswith("refs/heads/"):
            return None
        return branch[len("refs/heads/") :]

    @property
    def has_git_cinnabar(self):
        try:
            self._run("cinnabar", "--version")
        except subprocess.CalledProcessError:
            return False
        return True

    def get_commit_time(self):
        return int(self._run("log", "-1", "--format=%ct").strip())

    def sparse_checkout_present(self):
        # Not yet implemented.
        return False

    def get_user_email(self):
        email = self._run("config", "user.email", return_codes=[0, 1])
        if not email:
            return None
        return email.strip()

    def get_changed_files(self, diff_filter="ADM", mode="unstaged", rev=None):
        assert all(f.lower() in self._valid_diff_filter for f in diff_filter)

        if rev is None:
            cmd = ["diff"]
            if mode == "staged":
                cmd.append("--cached")
            elif mode == "all":
                cmd.append("HEAD")
        else:
            cmd = ["diff-tree", "-r", "--no-commit-id", rev]

        cmd.append("--name-only")
        cmd.append("--diff-filter=" + diff_filter.upper())

        return self._run(*cmd).splitlines()

    def get_outgoing_files(self, diff_filter="ADM", upstream=None):
        assert all(f.lower() in self._valid_diff_filter for f in diff_filter)

        not_condition = upstream if upstream else "--remotes"

        files = self._run(
            "log",
            "--name-only",
            f"--diff-filter={diff_filter.upper()}",
            "--oneline",
            "--topo-order",
            "--pretty=format:",
            "HEAD",
            "--not",
            not_condition,
        ).splitlines()
        return [f for f in files if f]

    def add_remove_files(self, *paths: Union[str, Path], force: bool = False):
        if not paths:
            return

        paths = [str(path) for path in paths]

        cmd = ["add"]

        if force:
            cmd.append("-f")

        cmd.extend(paths)

        self._run(*cmd)

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        paths = [str(path) for path in paths]

        self._run("reset", *paths)

    def get_tracked_files_finder(self, path=None):
        files = [p for p in self._run("ls-files", "-z").split("\0") if p]
        return FileListFinder(files)

    def get_ignored_files_finder(self):
        files = [
            p
            for p in self._run(
                "ls-files", "-i", "-o", "-z", "--exclude-standard"
            ).split("\0")
            if p
        ]
        return FileListFinder(files)

    def _translate_exclude_expr(self, pattern):
        if not pattern or pattern.startswith("#"):
            return None  # empty or comment
        pattern = pattern.replace(".*", "**")
        magics = ["exclude"]
        if pattern.startswith("^"):
            magics += ["top"]
            pattern = pattern[1:]
        return ":({0}){1}".format(",".join(magics), pattern)

    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=8):
        commit_range = "HEAD"  # All uncommitted changes.
        if rev:
            commit_range = rev if ".." in rev else f"{rev}~..{rev}"
        args = ["diff", "--no-color", f"-U{context}", commit_range, "--"]
        for dot_extension in extensions:
            args += [f"*{dot_extension}"]
        # git-diff doesn't support an 'exclude-from-files' param, but
        # allow to add individual exclude pattern since v1.9, see
        # https://git-scm.com/docs/gitglossary#gitglossary-aiddefpathspecapathspec
        with open(exclude_file) as exclude_pattern_file:
            for pattern in exclude_pattern_file.readlines():
                pattern = self._translate_exclude_expr(pattern.rstrip())
                if pattern is not None:
                    args.append(pattern)
        return self._pipefrom(*args)

    def working_directory_clean(self, untracked=False, ignored=False):
        args = ["status", "--porcelain"]

        # Even in --porcelain mode, behavior is affected by the
        # ``status.showUntrackedFiles`` option, which means we need to be
        # explicit about how to treat untracked files.
        if untracked:
            args.append("--untracked-files=all")
        else:
            args.append("--untracked-files=no")

        if ignored:
            args.append("--ignored")

        return not len(self._run(*args).strip())

    def clean_directory(self, path: Union[str, Path]):
        if Path(self.path).samefile(path):
            raise CannotDeleteFromRootOfRepositoryException()

        self._run("checkout", "--", str(path))
        self._run("clean", "-df", str(path))

    def update(self, ref):
        self._run("checkout", ref)

    def push_to_try(
        self,
        message: str,
        changed_files: Dict[str, str] = {},
        allow_log_capture: bool = False,
    ):
        if not self.has_git_cinnabar:
            raise MissingVCSExtension("cinnabar")

        with self.try_commit(message, changed_files) as head:
            cmd = (
                str(self._tool),
                "-c",
                # Never store git-cinnabar metadata for pushes to try.
                # Normally git-cinnabar asks the server what the phase of what it pushed
                # is, and figures on its own, but that request takes a long time on try.
                "cinnabar.data=never",
                "push",
                "hg::ssh://hg.mozilla.org/try",
                f"+{head}:refs/heads/branches/default/tip",
            )
            if allow_log_capture:
                self._push_to_try_with_log_capture(
                    cmd,
                    {
                        "stdout": subprocess.PIPE,
                        "stderr": subprocess.STDOUT,
                        "cwd": self.path,
                        "universal_newlines": True,
                        "bufsize": 1,
                    },
                )
            else:
                subprocess.check_call(cmd, cwd=self.path)

    def set_config(self, name, value):
        self._run("config", name, value)

    def get_branch_nodes(self, head: Optional[str] = None) -> List[str]:
        """Return a list of commit SHAs for nodes on the current branch."""
        remote_args = self.get_mozilla_remote_args()

        return self._run(
            "log",
            head or "HEAD",
            "--reverse",
            "--topo-order",
            "--not",
            *remote_args,
            "--pretty=%H",
        ).splitlines()

    def get_commit_patches(self, nodes: List[str]) -> List[bytes]:
        """Return the contents of the patch `node` in the VCS' standard format."""
        return [
            self._run("format-patch", node, "-1", "--always", "--stdout", encoding=None)
            for node in nodes
        ]

    @contextmanager
    def try_commit(
        self, commit_message: str, changed_files: Optional[Dict[str, str]] = None
    ):
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.
        """
        current_head = self.head_ref

        def data(content):
            return f"data {len(content)}\n{content}"

        author = self._run("var", "GIT_AUTHOR_IDENT").strip()
        committer = self._run("var", "GIT_COMMITTER_IDENT").strip()
        # A random enough temporary branch name that shouldn't conflict with
        # anything else, even in the machtry namespace.
        branch = str(uuid.uuid4())
        # The following fast-import script creates a new commit on a temporary
        # branch that it deletes at the end, based off the current HEAD, and
        # adding or modifying the files from `changed_files`.
        # fast-import will output the sha1 for that temporary commit on stdout
        # (via `get-mark`).
        fast_import = "\n".join(
            [
                f"commit refs/machtry/{branch}",
                "mark :1",
                f"author {author}",
                f"committer {committer}",
                data(commit_message),
                f"from {current_head}",
                "\n".join(
                    f"M 100644 inline {path}\n{data(content)}"
                    for path, content in (changed_files or {}).items()
                ),
                f"reset refs/machtry/{branch}",
                "from 0000000000000000000000000000000000000000",
                "get-mark :1",
                "",
            ]
        )

        cmd = (str(self._tool), "fast-import", "--quiet")
        stdout = subprocess.check_output(
            cmd,
            cwd=self.path,
            env=self._env,
            # text=True changes line endings on Windows, and git fast-import
            # doesn't like \r\n.
            input=fast_import.encode("utf-8"),
        )

        try_head = stdout.decode("ascii").strip()
        yield try_head

        # Keep trace of the temporary push in the reflog, as if we did actually commit.
        # This does update HEAD for a small window of time.
        # If we raced with something else that changed the HEAD after we created our
        # commit, update-ref will fail and print an error message. Only the update in
        # the reflog would be lost in this case.
        self._run("update-ref", "-m", "mach try: push", "HEAD", try_head, current_head)
        # Likewise, if we raced with something else that updated the HEAD between our
        # two update-ref, update-ref will fail and print an error message.
        self._run(
            "update-ref",
            "-m",
            "mach try: restore",
            "HEAD",
            current_head,
            try_head,
        )

    def get_last_modified_time_for_file(self, path: Path):
        """Return last modified in VCS time for the specified file."""
        out = self._run("log", "-1", "--format=%ad", "--date=iso", path)

        return datetime.strptime(out.strip(), "%Y-%m-%d %H:%M:%S %z")
