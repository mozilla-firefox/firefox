# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import abc
import os
import re
import subprocess
from contextlib import contextmanager
from pathlib import Path
from typing import Dict, List, Optional, Union

from mach.util import to_optional_path
from mozfile import which

from mozversioncontrol.errors import MissingVCSInfo, MissingVCSTool


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


class Repository:
    """A class wrapping utility methods around version control repositories.

    This class is abstract and never instantiated. Obtain an instance by
    calling a ``get_repository_*()`` helper function.

    Clients are recommended to use the object as a context manager. But not
    all methods require this.
    """

    __metaclass__ = abc.ABCMeta

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

    def _process_run_args(self, *args, **runargs):
        return_codes = runargs.get("return_codes", [])
        env = self._env
        if "env" in runargs:
            env = env.copy()
            env.update(runargs["env"])

        cmd = (str(self._tool),) + args
        return (cmd, return_codes, env)

    def _run(self, *args, encoding="utf-8", **runargs):
        # Check if we have a tool, either hg or git. If this is a source release
        # we return "src", indicating we don't have a tool to use. This caused
        # jstests to fail before fixing, because it uses a packaged mozjs
        # release source.
        if not self._tool:
            return "src"

        (cmd, return_codes, env) = self._process_run_args(*args, **runargs)
        try:
            return subprocess.check_output(
                cmd,
                cwd=self.path,
                encoding=encoding,
                env=env,
            )
        except subprocess.CalledProcessError as e:
            if e.returncode in return_codes:
                return ""
            raise

    def _pipefrom(self, *args, encoding="utf-8"):
        (cmd, _return_codes, env) = self._process_run_args(*args)
        return subprocess.Popen(
            cmd,
            cwd=self.path,
            encoding=encoding,
            env=env,
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
        """True if the repository is using git cinnabar."""
        return False

    @abc.abstractproperty
    def name(self):
        """Name of the tool."""

    @abc.abstractproperty
    def head_ref(self):
        """Hash of HEAD revision."""

    @abc.abstractproperty
    def base_ref(self):
        """Hash of revision the current topic branch is based on."""

    @abc.abstractmethod
    def base_ref_as_hg(self):
        """Mercurial hash of revision the current topic branch is based on.

        Return None if the hg hash of the base ref could not be calculated.
        """

    @abc.abstractproperty
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
    def push_to_try(
        self,
        message: str,
        changed_files: Dict[str, str] = {},
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
        """

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
    def get_branch_nodes(self, head: Optional[str] = None) -> List[str]:
        """Return a list of commit SHAs for nodes on the current branch."""

    @abc.abstractmethod
    def get_commit_patches(self, nodes: str) -> List[bytes]:
        """Return the contents of the patch `node` in the VCS's standard format."""

    @contextmanager
    @abc.abstractmethod
    def try_commit(
        self, commit_message: str, changed_files: Optional[Dict[str, str]] = None
    ):
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.
        """

    def stage_changes(self, changed_files: Dict[str, str]):
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
