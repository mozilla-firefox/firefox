# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import abc
import os
import re
import subprocess
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, Optional, Union

from mach.util import batch_paths, to_optional_path
from mozfile import which

from mozversioncontrol.errors import MissingVCSInfo, MissingVCSTool

HG_TRY_URL = "ssh://hg.mozilla.org/try"


def get_tool_path(tool: Optional[Union[str, Path]] = None):
    """Obtain the path of `tool`."""
    tool = Path(tool)
    if tool.is_absolute() and tool.exists():
        return str(tool)

    path = to_optional_path(which(str(tool)))
    if not path:
        raise MissingVCSTool(
            f"Unable to obtain {tool} path. Try running "
            "|mach bootstrap| to ensure your environment is up to "
            "date."
        )
    return str(path)


class Repository(abc.ABC):
    """A class wrapping utility methods around version control repositories.

    This class is abstract and never instantiated. Obtain an instance by
    calling a ``get_repository_*()`` helper function.

    Clients are recommended to use the object as a context manager. But not
    all methods require this.
    """

    def __init__(self, path: Path, tool: Optional[str] = None):
        self.path = str(path.resolve())
        self._tool = Path(get_tool_path(tool)) if tool else None
        self._version = None
        self._valid_diff_filter = ("m", "a", "d")
        self._env = os.environ.copy()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        pass

    def _repo_root_relative_path(self, path: Union[str, Path]):
        repo_root = Path(self.path).resolve()
        absolute_path = Path(path).resolve()
        try:
            relative_path = absolute_path.relative_to(repo_root)
        except ValueError:
            raise ValueError(
                f"Path {absolute_path} is outside of repository root {repo_root}."
            )
        return relative_path.as_posix()

    def _run(self, *args, encoding="utf-8", return_codes=None, **runargs):
        # Check if we have a tool, either hg or git. If this is a source release
        # we return "src", indicating we don't have a tool to use. This caused
        # jstests to fail before fixing, because it uses a packaged mozjs
        # release source.
        if not self._tool:
            return "src"

        cmd = (str(self._tool),) + args
        return_codes = return_codes or []

        env = self._env
        if extra_env := runargs.pop("env", None):
            env = env.copy()
            env.update(extra_env)

        runargs.setdefault("stdout", subprocess.PIPE)
        runargs.setdefault("check", True)
        try:
            result = subprocess.run(  # noqa: PLW1510
                cmd, cwd=self.path, encoding=encoding, env=env, **runargs
            )
            return result.stdout
        except subprocess.CalledProcessError as e:
            if e.returncode in return_codes:
                return ""
            raise

    def _run_batched(self, *args, paths: list[Union[str, Path]], **runargs):
        for batch in batch_paths(paths):
            self._run(*args, *batch, **runargs)

    def _pipefrom(self, *args, encoding="utf-8"):
        cmd = (str(self._tool),) + args
        return subprocess.Popen(
            cmd,
            cwd=self.path,
            encoding=encoding,
            env=self._env,
            stdout=subprocess.PIPE,
        ).stdout

    @property
    def tool_version(self):
        """Return the version of the VCS tool in use as a string."""
        if self._version:
            return self._version
        info = self._run("--version").strip()
        match = re.search(r"version ([^+)]+)", info)
        if not match:
            raise Exception("Unable to identify tool version.")

        self.version = match.group(1)
        return self.version

    @property
    def has_git_cinnabar(self):
        """True if git cinnabar is installed."""
        return False

    @property
    @abc.abstractmethod
    def name(self):
        """Name of the tool."""

    @property
    @abc.abstractmethod
    def head_ref(self):
        """Head reference."""

    @property
    @abc.abstractmethod
    def head_rev(self):
        """Head revision."""

    @property
    @abc.abstractmethod
    def is_shallow(self) -> bool:
        """Whether this repo is a shallow clone."""

    @property
    @abc.abstractmethod
    def is_cinnabar_repo(self) -> bool:
        """True if the repo is a git cinnabar repo"""

    @property
    @abc.abstractmethod
    def base_ref(self):
        """Hash of revision the current topic branch is based on."""

    @abc.abstractmethod
    def base_ref_as_hg(self):
        """Mercurial hash of revision the current topic branch is based on.

        Return None if the hg hash of the base ref could not be calculated.
        """

    @abc.abstractmethod
    def base_ref_as_commit(self):
        """Git hash of revision the current topic branch is based on."""

    @property
    @abc.abstractmethod
    def branch(self):
        """Current branch or bookmark the checkout has active."""

    @abc.abstractmethod
    def get_commit_time(self):
        """Return the Unix time of the HEAD revision."""

    @abc.abstractmethod
    def sparse_checkout_present(self):
        """Whether the working directory is using a sparse checkout.

        A sparse checkout is defined as a working directory that only
        materializes a subset of files in a given revision.

        Returns a bool.
        """

    @abc.abstractmethod
    def get_user_email(self):
        """Return the user's email address.

        If no email is configured, then None is returned.
        """

    @abc.abstractmethod
    def get_remote_url(self, remote=None, push=False):
        """Return the URL for the specified remote."""

    @abc.abstractmethod
    def get_changed_files(self, diff_filter, mode="unstaged", rev=None):
        """Return a list of files that are changed in this repository's
        working copy.

        ``diff_filter`` controls which kinds of modifications are returned.
        It is a string which may only contain the following characters:

            A - Include files that were added
            D - Include files that were deleted
            M - Include files that were modified

        By default, all three will be included.

        ``mode`` can be one of 'unstaged', 'staged' or 'all'. Only has an
        effect on git. Defaults to 'unstaged'.

        ``rev`` is a specifier for which changesets to consider for
        changes. The exact meaning depends on the vcs system being used.
        """

    @abc.abstractmethod
    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=None):
        """Return a BufferedReader of a diff."""

    @abc.abstractmethod
    def get_outgoing_files(self, diff_filter, upstream):
        """Return a list of changed files compared to upstream.

        ``diff_filter`` works the same as `get_changed_files`.
        ``upstream`` is a remote ref to compare against. If unspecified,
        this will be determined automatically. If there is no remote ref,
        a MissingUpstreamRepo exception will be raised.
        """

    @abc.abstractmethod
    def add_remove_files(self, *paths: Union[str, Path], force: bool = False):
        """Add and remove files under `paths` in this repository's working copy."""

    @abc.abstractmethod
    def forget_add_remove_files(self, *paths: Union[str, Path]):
        """Undo the effects of a previous add_remove_files call for `paths`."""

    @abc.abstractmethod
    def get_tracked_files_finder(self, path=None):
        """Obtain a mozpack.files.BaseFinder of managed files in the working
        directory.

        The Finder will have its list of all files in the repo cached for its
        entire lifetime, so operations on the Finder will not track with, for
        example, commits to the repo during the Finder's lifetime.
        """

    def get_tracked_files(self, patterns: list[str]) -> list[str]:
        """Return the sorted paths, relative to the repository root, of the
        managed files in the working directory that match any of the
        ``patterns``. Each pattern selects files by name with the ``*`` and
        ``**`` wildcards, such as ``**/*.idl`` or ``dom/webidl/*.webidl``. A
        pattern that only matches directories, such as ``dom/*``, is not
        supported."""
        if unsupported := [p for p in patterns if any(c in p for c in '?[]\\"')]:
            raise ValueError(
                f"Unsupported character in tracked file patterns: {', '.join(unsupported)}"
            )
        if not patterns:
            return []
        return self._get_tracked_files(patterns)

    def _get_tracked_files(self, patterns: list[str]) -> list[str]:
        finder = self.get_tracked_files_finder(self.path)
        return sorted({
            path for pattern in patterns for path, _ in finder.find(pattern)
        })

    @abc.abstractmethod
    def get_ignored_files_finder(self):
        """Obtain a mozpack.files.BaseFinder of ignored files in the working
        directory.

        The Finder will have its list of all files in the repo cached for its
        entire lifetime, so operations on the Finder will not track with, for
        example, changes to the repo during the Finder's lifetime.
        """

    @abc.abstractmethod
    def working_directory_clean(self, untracked=False, ignored=False):
        """Determine if the working directory is free of modifications.

        Returns True if the working directory does not have any file
        modifications. False otherwise.

        By default, untracked and ignored files are not considered. If
        ``untracked`` or ``ignored`` are set, they influence the clean check
        to factor these file classes into consideration.
        """

    @abc.abstractmethod
    def clean_directory(self, path: Union[str, Path]):
        """Undo all changes (including removing new untracked files) in the
        given `path`.
        """

    @abc.abstractmethod
    def push(
        self,
        remote: Optional[str] = None,
        ref: Optional[str] = None,
        dest_branch: Optional[str] = None,
        force: bool = False,
        env: Optional[dict] = None,
    ):
        """Push to a remote repository.

        `remote` specifies the remote to push to. If None, the default remote is used.
        `ref` specifies the branch or ref to push. If None, the current branch/ref is used.
        `dest_branch` specifies the destination branch name. If None, pushes ref to ref.
        `force` whether to use a force push (default False).
        `env` additional environment to set while pushing.
        """

    def add_note(
        self,
        note: str,
        content: str,
        commit: Optional[str] = None,
    ):
        """Attach a git note with `content` to `commit`.

        `note` is the notes namespace under refs/notes/.
        `content` is the data to store in the note.
        `commit` is the commit to associage the note with (defaults to HEAD).
        """
        raise NotImplementedError

    @abc.abstractmethod
    def _resolve_try_branch(self) -> str:
        pass

    def _push_to_git_try(self, message, changed_files, remote):
        dest_branch = self._resolve_try_branch()
        email = self.get_user_email()
        if not email:
            raise ValueError(
                "user.email is not configured; run 'git config user.email <email>'"
            )

        user_prefix = f"{email.split('@', 1)[0]}/"
        if not dest_branch.startswith(user_prefix):
            dest_branch = f"{user_prefix}{dest_branch}"

        global_prefix = "user/"
        if not dest_branch.startswith(global_prefix):
            dest_branch = f"{global_prefix}{dest_branch}"

        with self.try_commit(message, changed_files) as head:
            rev = self._try_head_to_commit(head)
            self.push(
                remote,
                ref=rev,
                dest_branch=dest_branch,
                force=True,
            )
        return rev

    def _try_head_to_commit(self, head: str) -> str:
        """Return the commit SHA for the `head` yielded by `try_commit`."""
        return head

    @abc.abstractmethod
    def _push_to_hg_try(self, message, changed_files, remote, allow_log_capture):
        pass

    def push_to_try(
        self,
        message: str,
        changed_files: dict[str, str] = {},
        remote: str = HG_TRY_URL,
        allow_log_capture: bool = False,
    ):
        """Create a temporary commit, push it to try and clean it up
        afterwards.

        With mercurial, MissingVCSExtension will be raised if the `push-to-try`
        extension is not installed. On git, MissingVCSExtension will be raised
        if git cinnabar is not present.

        `changed_files` is a dict of file paths and their contents, see
        `stage_changes`.

        If `allow_log_capture` is set to `True`, then the push-to-try will be run using
        Popen instead of check_call so that the logs can be captured elsewhere.

        Returns the pushed commit SHA when pushing to a git remote, otherwise None.
        """
        if HG_TRY_URL in remote:
            self._push_to_hg_try(message, changed_files, remote, allow_log_capture)
            return None
        return self._push_to_git_try(message, changed_files, remote)

    @abc.abstractmethod
    def update(self, ref):
        """Update the working directory to the specified reference."""

    def commit(self, message, author=None, date=None, paths=None):
        """Create a commit using the provided commit message. The author, date,
        and files/paths to be included may also be optionally provided. The
        message, author and date arguments must be strings, and are passed as-is
        to the commit command. Multiline commit messages are supported. The
        paths argument must be None or an array of strings that represents the
        set of files and folders to include in the commit.
        """
        args = ["commit", "-m", message]
        if author is not None:
            if self.name == "hg":
                args = args + ["--user", author]
            elif self.name == "git":
                args = args + ["--author", author]
            else:
                raise MissingVCSInfo("Unknown repo type")
        if date is not None:
            args = args + ["--date", date]
        if paths is not None:
            args = args + paths
        self._run(*args)

    def _push_to_try_with_log_capture(self, cmd, subprocess_opts):
        """Push to try but with the ability for the user to capture logs.

        We need to use Popen for this because neither the run method nor
        check_call will allow us to reasonably catch the logs. With check_call,
        hg hangs, and with the run method, the logs are output too slowly
        so you're left wondering if it's working (prime candidate for
        corrupting local repos).
        """
        process = subprocess.Popen(cmd, **subprocess_opts)

        # Print out the lines as they appear so they can be
        # parsed for information
        for line in process.stdout or []:
            print(line)
        process.stdout.close()
        process.wait()

        if process.returncode != 0:
            for line in process.stderr or []:
                print(line)
            raise subprocess.CalledProcessError(
                returncode=process.returncode,
                cmd=cmd,
                output="Failed to push-to-try",
                stderr=process.stderr,
            )

    @abc.abstractmethod
    def get_commits(
        self,
        head: Optional[str] = None,
        limit: Optional[int] = None,
        follow: Optional[list[str]] = None,
    ) -> list[str]:
        """Return a list of commit SHAs for nodes on the current branch."""

    @abc.abstractmethod
    def get_commit_patches(self, nodes: str) -> list[bytes]:
        """Return the contents of the patch `node` in the VCS's standard format."""

    @contextmanager
    @abc.abstractmethod
    def try_commit(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ):
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.
        """

    @abc.abstractmethod
    def prepare_try_push(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ) -> tuple[Optional[str], Callable]:
        pass

    def stage_changes(self, changed_files: dict[str, str]):
        """Stage a set of file changes

        `changed_files` is a dict that contains the paths of files to change or
        create as keys and their respective contents as values.
        """
        paths = []
        for path, content in changed_files.items():
            full_path = Path(self.path) / path
            full_path.parent.mkdir(parents=True, exist_ok=True)
            with full_path.open("w") as fh:
                fh.write(content)
            paths.append(full_path)

        if paths:
            self.add_remove_files(*paths)

    @abc.abstractmethod
    def get_last_modified_time_for_file(self, path: Path):
        """Return last modified in VCS time for the specified file."""
        pass

    @abc.abstractmethod
    def configure(
        self,
        state_dir: Path,
        update_only: bool = False,
        no_system_changes: bool = False,
    ):
        """Perform initial VCS setup, applying sensible defaults for configuration."""
        pass
