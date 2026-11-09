# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import uuid
from collections.abc import Iterator
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional, Union

from mach.util import (
    to_optional_path,
    win_to_msys_path,
)
from mozfile import which
from mozpack.files import FileListFinder
from packaging.version import Version

from mozversioncontrol.errors import (
    CannotDeleteFromRootOfRepositoryException,
    MissingVCSExtension,
)
from mozversioncontrol.repo.base import Repository

# The built-in fsmonitor Windows/macOS for git 2.37+ is better than using the watchman hook.
# Linux users will still need watchman and to enable the hook.
MINIMUM_GIT_VERSION = Version("2.37")


ADD_GIT_CINNABAR_PATH = """
To add git-cinnabar to the PATH, edit your shell initialization script, which
may be called {prefix}/.bash_profile or {prefix}/.profile, and add the following
lines:

    export PATH="{cinnabar_dir}:$PATH"

Then restart your shell.
"""


class GitVersionError(Exception):
    """Raised when the installed git version is too old."""

    pass


class GitRepository(Repository):
    """An implementation of `Repository` for Git repositories."""

    def __init__(self, path: Path, git="git"):
        super().__init__(path, tool=git)

    @property
    def name(self):
        return "git"

    @property
    def head_ref(self):
        return self.branch or "HEAD"

    @property
    def head_rev(self):
        return self._run("rev-parse", "HEAD").strip()

    @property
    def is_shallow(self):
        return self._run("rev-parse", "--is-shallow-repository").strip() == "true"

    def is_cinnabar_repo(self) -> bool:
        """Return `True` if the repo is a git-cinnabar clone."""

        try:
            # First revision of the canonical Firefox repository
            self._run(
                "cat-file",
                "-e",
                "2ca566cd74d5d0863ba7ef0529a4f88b2823eb43^{commit}",
                stderr=subprocess.DEVNULL,
            )
        except subprocess.CalledProcessError:
            output = self._run("for-each-ref")
            return "refs/cinnabar" in output
        return False

    def get_mozilla_upstream_remotes(self) -> Iterator[str]:
        """Return the Mozilla-official upstream remotes for this repo."""
        out = self._run("remote", "-v")
        if not out:
            return

        remotes = out.splitlines()
        if not remotes:
            return

        is_cinnabar_repo = self.is_cinnabar_repo()

        def is_official_remote(url: str) -> bool:
            """Determine if a remote is official.

            Account for `git-cinnabar` remotes with `hg.mozilla.org` in the name,
            as well as SSH and HTTP remotes for Git-native.
            """
            if (
                is_cinnabar_repo
                and "hg.mozilla.org" in url
                and not url.endswith(("hg.mozilla.org/try", "hg.mozilla.org/try/"))
            ):
                return True

            return any(
                remote in url
                for remote in (
                    "github.com/mozilla-firefox/",
                    "github.com:mozilla-firefox/",
                )
            )

        for line in remotes:
            parts = line.split()
            if len(parts) < 3:
                continue

            name, url, action, *_ = parts

            # Only consider fetch sources.
            if action != "(fetch)":
                continue

            if is_official_remote(url):
                yield name

    def get_mozilla_remote_args(self) -> list[str]:
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
        return self.head_rev

    def base_ref_as_hg(self):
        base_ref = self.base_ref
        try:
            return self._run("cinnabar", "git2hg", base_ref).strip()
        except subprocess.CalledProcessError:
            return

    def base_ref_as_commit(self):
        return self.base_ref

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

    def get_user_name(self):
        name = self._run("config", "user.name", return_codes=[0, 1])
        if not name:
            return None
        return name.strip()

    def get_remote_url(self, remote=None, push=False):
        if not remote:
            keys = [f"branch.{self.branch}.remote"]
            if push:
                keys[0:0] = [f"branch.{self.branch}.pushRemote", "remote.pushDefault"]

            for key in keys:
                if remote := self._run("config", key, return_codes=[0, 1]):
                    break
            else:
                return None

            remote = remote.strip()

        cmd = ["remote", "get-url", remote]
        if push:
            cmd.append("--push")
        url = self._run(*cmd, return_codes=[0, 2, 128], stderr=subprocess.DEVNULL)
        return url.strip() if url else None

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

        self._run_batched(*cmd, paths=paths)

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        paths = [str(path) for path in paths]

        self._run_batched("reset", paths=paths)

    def get_tracked_files_finder(self, path=None):
        files = [p for p in self._run("ls-files", "-z").split("\0") if p]
        return FileListFinder(files)

    def _get_tracked_files(self, patterns):
        pathspecs = [f":(glob){pattern}" for pattern in patterns]
        files = self._run("ls-files", "-z", "--", *pathspecs).split("\0")
        deleted = self._run("ls-files", "-z", "--deleted", "--", *pathspecs)
        return sorted(set(files) - set(deleted.split("\0")) - {""})

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
        return ":({}){}".format(",".join(magics), pattern)

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
        if exclude_file is not None:
            with open(exclude_file) as exclude_pattern_file:
                for pattern in exclude_pattern_file.readlines():
                    translated = self._translate_exclude_expr(pattern.rstrip())
                    if translated is not None:
                        args.append(translated)
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

    def push(
        self,
        remote: Optional[str] = None,
        ref: Optional[str] = None,
        dest_branch: Optional[str] = None,
        force: bool = False,
        env: Optional[dict] = None,
    ):
        if ref and not remote:
            raise ValueError("Cannot specify ref without specifying remote")
        if dest_branch and not ref:
            raise ValueError("Cannot specify dest_branch without specifying ref")

        args = []
        if remote and remote.startswith("hg::"):
            # Ensure git-cinnabar adds the `extra.git_commit` metadata to the Mercurial
            # commit.
            args.extend(["-c", "cinnabar.experiments=git_commit"])
        args.append("push")
        if force:
            args.append("--force")
        if remote:
            args.append(remote)
        if ref:
            if dest_branch:
                args.append(f"{ref}:refs/heads/{dest_branch}")
            else:
                args.append(ref)

        runargs = {
            "env": env,
            "stdout": None,  # stream push output
        }
        self._run(*args, **runargs)

    def _resolve_try_branch(self):
        if not self.branch:
            raise ValueError(
                "Cannot push to try from a detached HEAD; checkout a branch first."
            )
        return self.branch

    def _push_to_hg_try(self, message, changed_files, remote, allow_log_capture):
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
                f"hg::{remote}",
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

    def add_note(
        self,
        note: str,
        content: str,
        commit: Optional[str] = None,
    ):
        if not note.startswith("refs/notes/"):
            note = f"refs/notes/{note}"
        self._run("notes", "--ref", note, "add", "-f", "-m", content, commit or "HEAD")

    def set_config(self, name, value):
        self._run("config", name, value)

    def get_commits(
        self,
        head: Optional[str] = None,
        limit: Optional[int] = None,
        follow: Optional[list[str]] = None,
    ) -> list[str]:
        """Return a list of commit SHAs for nodes on the current branch."""
        remote_args = self.get_mozilla_remote_args()

        cmd = [
            "log",
            head or "HEAD",
            "--reverse",
            "--topo-order",
            "--not",
            *remote_args,
            "--pretty=%H",
        ]
        if limit is not None:
            cmd.append(f"-n{limit}")
        if follow is not None:
            cmd += ["--", *follow]
        return self._run(*cmd).splitlines()

    def get_ancestors(self, head: str = "HEAD", limit: int = 20) -> list[str]:
        """Return the SHAs of `head` and its ancestors, newest first."""
        return self._run(
            "rev-list", "--topo-order", f"--max-count={limit}", head
        ).splitlines()

    def get_commit_patches(self, nodes: list[str]) -> list[bytes]:
        """Return the contents of the patch `node` in the VCS' standard format."""
        return [
            self._run(
                "format-patch",
                node,
                "-1",
                "--always",
                "--stdout",
                "--no-base",  # In case the user has format.useAutoBase true
                encoding=None,
            )
            for node in nodes
        ]

    @contextmanager
    def try_commit(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ):
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.
        """
        try_head, cleanup = self.prepare_try_push(commit_message, changed_files)
        yield try_head
        cleanup()

    def prepare_try_push(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ) -> tuple[Optional[str], Callable]:
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.

        This function returns a tuple of the ref of the new head and a function
        that can be called to remove the head from the local repository.
        """
        current_head = self.head_rev

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
        fast_import = "\n".join([
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
        ])

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

        def cleanup():
            # Keep trace of the temporary push in the reflog, as if we did actually commit.
            # This does update HEAD for a small window of time.
            # If we raced with something else that changed the HEAD after we created our
            # commit, update-ref will fail and print an error message. Only the update in
            # the reflog would be lost in this case.
            self._run(
                "update-ref", "-m", "mach try: push", "HEAD", try_head, current_head
            )
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

        return try_head, cleanup

    def get_last_modified_time_for_file(self, path: Path):
        """Return last modified in VCS time for the specified file."""
        out = self._run("log", "-1", "--format=%ad", "--date=iso", path)

        return datetime.strptime(out.strip(), "%Y-%m-%d %H:%M:%S %z")

    def get_config_key_value(self, key: str):
        value = self._run(
            "config", "--get", key, stderr=subprocess.DEVNULL, return_codes=[0, 1]
        ).strip()
        return value or None

    def set_config_key_value(self, key: str, value: str):
        """
        Set a git config value in the given repo and print
        logging output indicating what was done.
        """
        self._run("config", key, value)
        print(f'Set git config: "{key} = {value}"')

    def configure(
        self,
        state_dir: Path,
        update_only: bool = False,
        no_system_changes: bool = False,
    ):
        """Run the Git configuration steps."""
        if not update_only:
            print("Configuring git...")

            match = re.search(
                r"(\d+\.\d+\.\d+)",
                subprocess.check_output(
                    [self._tool, "--version"], universal_newlines=True
                ),
            )
            if not match:
                raise Exception("Could not find git version")
            git_version = Version(match.group(1))

            moz_automation = os.environ.get("MOZ_AUTOMATION")
            # This hard error is to force users to upgrade for performance benefits. If a CI worker on an old
            # distro gets here, but can't upgrade to a newer git version, that's not a blocker, so we skip
            # this check in CI to avoid that scenario.
            if not moz_automation:
                if git_version < MINIMUM_GIT_VERSION:
                    raise GitVersionError(
                        f"Your version of git ({git_version}) is too old. "
                        f"Please upgrade to at least version '{MINIMUM_GIT_VERSION}' to ensure "
                        "full compatibility and performance."
                    )

                if not self.get_user_email():
                    print("\nGit requires an email address to identify your commits.")
                    email = input("Enter your email address: ").strip()
                    if email:
                        self.set_config_key_value("user.email", email)

                if not self.get_user_name():
                    print("\nGit requires a name to identify your commits.")
                    name = input("Enter your name: ").strip()
                    if name:
                        self.set_config_key_value("user.name", name)

            system = platform.system()

            # https://git-scm.com/docs/git-config#Documentation/git-config.txt-coreuntrackedCache
            self.set_config_key_value(key="core.untrackedCache", value="true")
            # https://git-scm.com/docs/git-config#Documentation/git-config.txt-checkoutworkers
            if not self._run("config", "--get", "checkout.workers", return_codes=[1]):
                self.set_config_key_value(key="checkout.workers", value="0")

            # https://git-scm.com/docs/git-config#Documentation/git-config.txt-corefsmonitor
            if system == "Windows":
                # On Windows we enable the built-in fsmonitor which is superior to Watchman.
                self.set_config_key_value(key="core.fscache", value="true")
                # https://github.com/git-for-windows/git/blob/eaeb5b51c389866f207c52f1546389a336914e07/Documentation/config/core.adoc?plain=1#L688-L692
                # We can also enable fscache (only supported on git-for-windows).
                self.set_config_key_value(key="core.fsmonitor", value="true")
            elif system == "Darwin":
                # On macOS (Darwin) we enable the built-in fsmonitor which is superior to Watchman.
                self.set_config_key_value(key="core.fsmonitor", value="true")
            elif system == "Linux" and not no_system_changes:
                # On Linux the built-in fsmonitor isn’t available, so we unset it and attempt to set up
                # Watchman to achieve similar fsmonitor-style speedups.
                subprocess.run(
                    [self._tool, "config", "--unset-all", "core.fsmonitor"],
                    cwd=str(self.path),
                    check=False,
                )
                print("Unset git config: `core.fsmonitor`")

                self._ensure_watchman()

        # Only do cinnabar checks if we're a git cinnabar repo
        if self.is_cinnabar_repo():
            cinnabar_dir = str(self._update_git_cinnabar(state_dir))
            cinnabar = to_optional_path(which("git-cinnabar"))
            if not cinnabar:
                if "MOZILLABUILD" in os.environ:
                    # Slightly modify the path on Windows to be correct
                    # for the copy/paste into the .bash_profile
                    cinnabar_dir = win_to_msys_path(cinnabar_dir)

                    print(
                        ADD_GIT_CINNABAR_PATH.format(
                            prefix="%USERPROFILE%", cinnabar_dir=cinnabar_dir
                        )
                    )
                else:
                    print(
                        ADD_GIT_CINNABAR_PATH.format(
                            prefix="~", cinnabar_dir=cinnabar_dir
                        )
                    )

    def _update_git_cinnabar(self, root_state_dir: Path):
        """Update git tools, hooks and extensions"""
        # Ensure git-cinnabar is up-to-date.
        cinnabar_dir = root_state_dir / "git-cinnabar"
        cinnabar_exe = cinnabar_dir / "git-cinnabar"

        if sys.platform.startswith(("win32", "msys")):
            cinnabar_exe = cinnabar_exe.with_suffix(".exe")

        # Older versions of git-cinnabar can't do self-update. So if we start
        # from such a version, we remove it and start over.
        # The first version that supported self-update is also the first version
        # that wasn't a python script, so we can just look for a hash-bang.
        # Or, on Windows, the .exe didn't exist.
        start_over = cinnabar_dir.exists() and not cinnabar_exe.exists()
        if cinnabar_exe.exists():
            try:
                with cinnabar_exe.open("rb") as fh:
                    start_over = fh.read(2) == b"#!"
            except Exception:
                # If we couldn't read the binary, let's just try to start over.
                start_over = True

        if start_over:
            # git sets pack files read-only, which causes problems removing
            # them on Windows. To work around that, we use an error handler
            # on rmtree that retries to remove the file after chmod'ing it.
            def onerror(func, path, exc):
                if func == os.unlink:
                    os.chmod(path, stat.S_IRWXU)
                    func(path)
                else:
                    raise exc

            shutil.rmtree(str(cinnabar_dir), onerror=onerror)

        # If we already have an executable, ask it to update itself.
        exists = cinnabar_exe.exists()
        if exists:
            try:
                print("\nUpdating git-cinnabar...")
                subprocess.check_call([str(cinnabar_exe), "self-update"])
            except subprocess.CalledProcessError as e:
                print(e)

        # git-cinnabar 0.6.0rc1 self-update had a bug that could leave an empty
        # file. If that happens, install from scratch.
        if not exists or cinnabar_exe.stat().st_size == 0:
            import ssl
            from urllib.request import urlopen

            import certifi

            if not cinnabar_dir.exists():
                cinnabar_dir.mkdir()

            cinnabar_url = "https://github.com/glandium/git-cinnabar/"
            download_py = cinnabar_dir / "download.py"
            with open(download_py, "wb") as fh:
                context = ssl.create_default_context(cafile=certifi.where())
                shutil.copyfileobj(
                    urlopen(f"{cinnabar_url}/raw/master/download.py", context=context),
                    fh,
                )

            try:
                subprocess.check_call(
                    [sys.executable, str(download_py)], cwd=str(cinnabar_dir)
                )
            except subprocess.CalledProcessError as e:
                print(e)
            finally:
                download_py.unlink()

        return cinnabar_dir

    def _ensure_watchman(self):
        watchman = which("watchman")

        if not watchman:
            print(
                "watchman is not installed. Please install `watchman` and "
                "re-run `./mach vcs-setup` to enable faster git commands."
            )
            return

        print("Ensuring watchman is properly configured...")

        hooks = Path(
            subprocess.check_output(
                [
                    self._tool,
                    "rev-parse",
                    "--path-format=absolute",
                    "--git-path",
                    "hooks",
                ],
                cwd=str(self.path),
                universal_newlines=True,
            ).strip()
        )

        watchman_config = hooks / "query-watchman"
        watchman_sample = hooks / "fsmonitor-watchman.sample"

        if not watchman_sample.exists():
            print(
                "watchman is installed but the sample hook (expected here: "
                f"{watchman_sample}) was not found. Please acquire it and copy"
                f" it into `.git/hooks/` and re-run `./mach vcs-setup`."
            )
            return

        if not watchman_config.exists():
            copy_cmd = [
                "cp",
                watchman_sample,
                watchman_config,
            ]
            print(f"Copying {watchman_sample} to {watchman_config}")
            subprocess.check_call(copy_cmd, cwd=str(self.path))
        self.set_config_key_value(key="core.fsmonitor", value=str(watchman_config))

    def get_patches_after_ref(self, base_ref) -> str:
        """
        Retrieve git format-patch style patches of all commits that occurred
        after `base_ref`.
        """
        return self._run(
            "format-patch", f"{base_ref}..HEAD", "--stdout", f"--base={base_ref}"
        )

    def get_patch_for_uncommitted_changes(
        self, message: str = "[PATCH] Uncommitted changes", date: datetime = None
    ) -> str:
        """
        Generate a git format-patch style patch of all uncommitted changes in
        the working directory.
        """
        diff = self._run("diff", "--no-color", "HEAD")
        if not diff.strip():
            return ""

        if not date:
            date = datetime.now()

        name = self.get_user_name()
        email = self.get_user_email()
        formatted_date = date.strftime("%a %b %d %H:%M:%S %Y %z")

        patch = [
            "From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001",
            f"From: {name} <{email}>",
            f"Date: {formatted_date}",
            f"Subject: {message}",
            "\n---\n",
            diff,
        ]

        return "\n".join(patch)
