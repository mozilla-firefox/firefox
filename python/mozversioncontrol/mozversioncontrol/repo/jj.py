# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import string
import subprocess
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Union

import mozpack.path as mozpath
from mozpack.files import FileListFinder

from mozversioncontrol.errors import (
    CannotDeleteFromRootOfRepositoryException,
    MissingVCSExtension,
    MissingVCSInfo,
)
from mozversioncontrol.repo.base import Repository
from mozversioncontrol.repo.git import GitRepository


class JujutsuRepository(Repository):
    """An implementation of `Repository` for JJ repositories using the git backend."""

    def __init__(self, path: Path, jj="jj", git="git"):
        super(JujutsuRepository, self).__init__(path, tool=jj)
        self._git = GitRepository(path, git=git)

        # Find git root. Newer jj has `jj git root`, but this should support
        # older versions for now.
        out = self._run("root")
        if not out:
            raise MissingVCSInfo("cannot find jj workspace root")

        try:
            jj_ws_root = Path(out.rstrip())
            jj_repo = jj_ws_root / ".jj" / "repo"
            if not jj_repo.is_dir():
                jj_repo = Path(jj_repo.read_text())
        except Exception:
            raise MissingVCSInfo("cannot find jj repo")

        try:
            git_target = jj_repo / "store" / "git_target"
            git_dir = git_target.parent / Path(git_target.read_text())
        except Exception:
            raise MissingVCSInfo("cannot find git dir")

        if not git_dir.is_dir():
            raise MissingVCSInfo("cannot find git dir")

        self._git._env["GIT_DIR"] = str(git_dir.resolve())

    def _run_read_only(self, *args, **kwargs):
        """_run_read_only() should be used instead of _run() for read-only jj commands.

        It will avoid locking the working copy and can prevent potential concurrency issues.
        """
        return super()._run("--ignore-working-copy", *args, **kwargs)

    def _snapshot(self):
        """_snapshot() can be used to update the repository after changing files in the working
        directory. Normally jj commands will do this automatically, but we often run jj commands
        using `_run_read_only` which passes `--ignore-working-copy` to jj.
        See bug 1962245 and bug 1962389.

        An alternative option would be to add an extra argument to methods such as
        `get_branch_nodes`.
        """
        self._run("log", "-n0")

    def resolve_to_change(self, revset: str) -> Optional[str]:
        change_id = self._run_read_only(
            "log", "--no-graph", "-n1", "-r", revset, "-T", "change_id.short()"
        ).rstrip()
        return change_id if change_id != "" else None

    @property
    def name(self):
        return "jj"

    @property
    def head_ref(self):
        # This is not really a defined concept in jj. Map it to @, or rather the
        # persistent change id for the current @. Warning: this cannot be passed
        # directly to a git command, it must be converted to a commit id first
        # (eg via convert_change_to_commit). This isn't done here because
        # callers should be aware when they're dropping down to git semantics.
        return self.resolve_to_change("@")

    @property
    def base_ref(self):
        ref = self.resolve_to_change("latest(roots(::@ & mutable())-)")
        return ref if ref else self.head_ref

    def resolve_to_commit(self, revset):
        commit = self._run_read_only(
            "log", "--no-graph", "-r", f"latest({revset})", "-T", "commit_id"
        ).rstrip()
        return commit

    def base_ref_as_hg(self):
        base_ref = self.resolve_to_commit(self.base_ref)
        try:
            return self._git._run("cinnabar", "git2hg", base_ref).strip()
        except subprocess.CalledProcessError:
            return

    @property
    def branch(self):
        # jj does not have an "active branch" concept. The lone caller will fall
        # back to self.head_ref.
        return None

    @property
    def has_git_cinnabar(self):
        return self._git.has_git_cinnabar

    def get_commit_time(self):
        return int(
            self._run_read_only(
                "log", "-n1", "--no-graph", "-T", 'committer.timestamp().format("%s")'
            ).strip()
        )

    def sparse_checkout_present(self):
        return self._run_read_only("sparse", "list").rstrip() != "."

    def get_user_email(self):
        email = self._run_read_only("config", "get", "user.email", return_codes=[0, 1])
        if not email:
            return None
        return email.strip()

    def get_changed_files(self, diff_filter="ADM", mode="(ignored)", rev="@"):
        assert all(f.lower() in self._valid_diff_filter for f in diff_filter)

        out = self._run_read_only(
            "log",
            "-r",
            rev,
            "--no-graph",
            "-T",
            'diff.files().map(|f| surround("", "\n", separate("\t", f.status(), f.source().path(), f.target().path()))).join("")',
        )
        changed = []
        for line in out.splitlines():
            op, source, target = line.split("\t")
            if op == "modified":
                if "M" in diff_filter:
                    changed.append(source)
            elif op == "added":
                if "A" in diff_filter:
                    changed.append(source)
            elif op == "removed":
                if "D" in diff_filter:
                    changed.append(source)
            elif op == "copied":
                if "A" in diff_filter:
                    changed.append(target)
            elif op == "renamed":
                if "A" in diff_filter:
                    changed.append(target)
                if "D" in diff_filter:
                    changed.append(source)
            else:
                raise Exception(f"unexpected jj file status '{op}'")

        return changed

    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=8):
        if rev is None:
            rev = "latest((@ | @-) ~ empty())"
        rev = self.resolve_to_commit(rev)
        return self._git.diff_stream(
            rev=rev, extensions=extensions, exclude_file=exclude_file, context=context
        )

    def get_outgoing_files(self, diff_filter="ADM", upstream=None):
        assert all(f.lower() in self._valid_diff_filter for f in diff_filter)

        if upstream is None:
            upstream = self.base_ref

        lines = self._run_read_only(
            "diff",
            "--from",
            upstream,
            "--to",
            "@",
            "--summary",
        ).splitlines()

        outgoing = []
        for line in lines:
            op, file = line.split(" ", 1)
            if op.upper() in diff_filter:
                outgoing.append(mozpath.normsep(file))
        return outgoing

    def add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        paths = [str(path) for path in paths]

        self._run("file", "track", *paths)

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        paths = [str(path) for path in paths]

        self._run_read_only("file", "untrack", *paths)

    def get_tracked_files_finder(self, path=None):
        files = [mozpath.normsep(p) for p in self._run("file", "list").splitlines()]
        return FileListFinder(files)

    def get_ignored_files_finder(self):
        raise Exception("unimplemented")

    def working_directory_clean(self, untracked=False, ignored=False):
        # Working directory is in the top commit.
        return True

    def update(self, ref):
        self._run("new", ref)

    def edit(self, ref):
        self._run("edit", ref)

    def clean_directory(self, path: Union[str, Path]):
        if Path(self.path).samefile(path):
            raise CannotDeleteFromRootOfRepositoryException()

        self._run("restore", "-r", "@-", str(path))

    def commit(self, message, author=None, date=None, paths=None):
        run_kwargs = {}
        cmd = ["commit", "-m", message]
        if author:
            cmd += ["--author", author]
        if date:
            dt = datetime.strptime(date, "%Y-%m-%d %H:%M:%S %z")
            run_kwargs["env"] = {"JJ_TIMESTAMP": dt.isoformat()}
        if paths:
            cmd.extend(paths)
        self._run(*cmd, **run_kwargs)

    def push_to_try(
        self,
        message: str,
        changed_files: Dict[str, str] = {},
        allow_log_capture: bool = False,
    ):
        if not self.has_git_cinnabar:
            raise MissingVCSExtension("cinnabar")

        with self.try_commit(message, changed_files) as head:
            if "mach_tryserver" in self._git._run("remote"):
                self._run(
                    "git", "remote", "remove", "mach_tryserver", return_codes=[0, 1]
                )
            # `jj git remote add` would barf on the cinnabar syntax here.
            self._git._run(
                "remote", "add", "mach_tryserver", "hg::ssh://hg.mozilla.org/try"
            )
            self._run("git", "import")
            cmd = (
                str(self._tool),
                "git",
                "push",
                "--remote",
                "mach_tryserver",
                "--change",
                head,
                "--allow-new",
                "--allow-empty-description",
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
        self._run("git", "remote", "remove", "mach_tryserver", return_codes=[0, 1])

    def set_config(self, name, value):
        self._run_read_only("config", name, value)

    def get_branch_nodes(self, head: Optional[str] = "@") -> List[str]:
        """Return a list of commit SHAs for nodes on the current branch, in order that they should be applied."""
        # Note: lando gets grumpy if you try to push empty commits.
        return list(
            reversed(
                self._run_read_only(
                    "log",
                    "--no-graph",
                    "-r",
                    f"(::{head} & mutable()) ~ empty()",
                    "-T",
                    'commit_id ++ "\n"',
                ).splitlines()
            )
        )

    def looks_like_change_id(self, id):
        return len(id) > 0 and all(letter >= "k" and letter <= "z" for letter in id)

    def looks_like_commit_id(self, id):
        return len(id) > 0 and all(letter in string.hexdigits for letter in id)

    def get_commit_patches(self, nodes: List[str]) -> List[bytes]:
        """Return the contents of the patch `node` in the git standard format."""
        # Warning: tests, at least, may call this with change ids rather than
        # commit ids.
        nodes = [
            id if self.looks_like_commit_id(id) else self.resolve_to_commit(id)
            for id in nodes
        ]
        return [
            self._git._run(
                "format-patch", node, "-1", "--always", "--stdout", encoding=None
            )
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
        opid = self._run_read_only(
            "operation", "log", "-n1", "--no-graph", "-T", "id.short(16)"
        ).rstrip()
        try:
            self._run("new", "-m", commit_message, "latest((@ | @-) ~ empty())")
            for path, content in (changed_files or {}).items():
                p = self.path / Path(path)
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
            # Update the jj commit with the changes we just made.
            self._snapshot()
            yield self.resolve_to_change("@")
        finally:
            self._run("operation", "restore", opid)

    def get_last_modified_time_for_file(self, path: Path) -> datetime:
        """Return last modified in VCS time for the specified file."""
        date = self._run_read_only(
            "log",
            "--no-graph",
            "-n1",
            "-T",
            "committer.timestamp()",
            str(path),
        ).rstrip()
        return datetime.strptime(date, "%Y-%m-%d %H:%M:%S.%f %z")
