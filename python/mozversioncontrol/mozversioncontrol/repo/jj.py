# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import functools
import json
import re
import string
import subprocess
import sys
import threading
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Optional, Union

import mozpack.path as mozpath
from mozfile import which
from mozpack.files import FileListFinder
from packaging.version import Version

MINIMUM_SUPPORTED_JJ_VERSION = Version("0.38")

from mozversioncontrol.errors import (
    CannotDeleteFromRootOfRepositoryException,
    MissingVCSExtension,
    MissingVCSInfo,
    StaleWorkspaceError,
)
from mozversioncontrol.repo.base import Repository
from mozversioncontrol.repo.git import GitRepository

_git_root_lock = threading.Lock()


@functools.cache
def _git_root(jj: str, path: str) -> str:
    return subprocess.run(
        (jj, "--quiet", "git", "root"),
        cwd=path,
        encoding="utf-8",
        capture_output=True,
        check=True,
    ).stdout


class JjVersionError(Exception):
    """Raised when the installed jj version is too old."""

    pass


class JujutsuRepository(Repository):
    """An implementation of `Repository` for JJ repositories using the git backend."""

    # Revset for a "HEAD-like" change. Use @ (the working copy commit) unless it
    # would be discarded when switching away (because it's empty and has no
    # description.)
    HEAD_REVSET = 'coalesce(@ ~ (empty() & description(exact:"")) ~ bookmarks(), @-)'

    def __init__(self, path: Path, jj="jj", git="git"):
        super().__init__(path, tool=jj)
        self._git = GitRepository(path, git=git)

        try:
            with _git_root_lock:
                git_dir = _git_root(str(self._tool), self.path)
        except subprocess.CalledProcessError as e:
            stderr = e.stderr or ""
            if "working copy is stale" in stderr.lower():
                raise StaleWorkspaceError(stderr.rstrip()) from e
            print(stderr, end="", file=sys.stderr)
            raise

        if not git_dir:
            raise MissingVCSInfo("cannot find `jj git root`")

        git_dir = Path(git_dir.rstrip())
        if not git_dir.is_dir():
            raise MissingVCSInfo("cannot find git dir")

        self._git._env["GIT_DIR"] = str(git_dir.resolve())

    def _run(self, *args, **kwargs):
        # Suppress extra output like "Snapshotting ..."
        return super()._run("--quiet", *args, **kwargs)

    def _run_read_only(self, *args, **kwargs):
        """_run_read_only() should be used instead of _run() for read-only jj commands.

        It will avoid locking the working copy and can prevent potential concurrency issues.
        """
        return self._run("--ignore-working-copy", *args, **kwargs)

    def _snapshot(self, reason):
        """_snapshot() can be used to update the repository after changing files in the working
        directory. Normally jj commands will do this automatically, but we often run jj commands
        using `_run_read_only` which passes `--ignore-working-copy` to jj.
        See bug 1962245 and bug 1962389.

        An alternative option would be to add an extra argument to methods such as
        `get_commits`.
        """
        # Do-nothing command with an explanatory message visible in `jj op log`.
        self._run("log", "--limit=0", "--template", f'"snapshot: {reason}"')

    def _resolve_to_change(self, revset: str) -> Optional[str]:
        change_id = self._run_read_only(
            "log",
            "--no-graph",
            "--limit=1",
            "--revisions",
            revset,
            "--template",
            "change_id.short()",
        ).rstrip()
        return change_id if change_id != "" else None

    @property
    def name(self):
        return "jj"

    @property
    def head_ref(self):
        # This is not really a defined concept in jj. Map it to the persistent
        # change id for @. Unless that's empty, in which case use @- instead.
        #
        # Note that this returns a JJ change id, not a git commit id. That's
        # because I want it to fail if something tries to use it as a git commit
        # directly rather than going through the generic vcs interface.
        # (VCS-specific code can use _resolve_to_commit if it is necessary to
        # drop down to git semantics.)
        return self._resolve_to_change(self.HEAD_REVSET)

    @property
    def head_rev(self):
        return self._resolve_to_commit(self.HEAD_REVSET)

    @property
    def is_shallow(self):
        return self._git.is_shallow

    def is_cinnabar_repo(self) -> bool:
        return self._git.is_cinnabar_repo()

    @property
    def base_ref(self):
        return self._resolve_to_change("roots((::@ & mutable())-)")

    def _resolve_to_commit(self, revset):
        commit = self._run_read_only(
            "log",
            "--no-graph",
            "--revisions",
            f"latest({revset})",
            "--template",
            "commit_id",
        ).rstrip()
        return commit

    def base_ref_as_hg(self):
        return self._git.base_ref_as_hg()

    def base_ref_as_commit(self):
        return self._resolve_to_commit(self.base_ref)

    @property
    def branch(self):
        output = self._run_read_only(
            "log",
            "--no-graph",
            "--limit=1",
            "--revisions",
            self.HEAD_REVSET,
            "--template",
            # Use name() rather than the default RefName formatting, which
            # decorates diverged ("*") and conflicted ("??") bookmarks.
            'local_bookmarks.map(|b| b.name()).join("\n")',
        )
        bookmark = output.split("\n")[0].strip()
        return bookmark or None

    @property
    def has_git_cinnabar(self):
        return self._git.has_git_cinnabar

    def get_commit_time(self):
        return int(
            self._run_read_only(
                "log",
                "--limit=1",
                "--no-graph",
                "--template",
                'committer.timestamp().format("%s")',
            ).strip()
        )

    def sparse_checkout_present(self):
        return self._run_read_only("sparse", "list").rstrip() != "."

    def get_user_email(self):
        email = self._run_read_only("config", "get", "user.email", return_codes=[0, 1])
        if not email:
            return None
        return email.strip()

    def get_remote_url(self, remote=None, push=False):
        if not remote:
            if push:
                if remote := self._run(
                    "config", "get", "git.push", return_codes=[0, 1]
                ):
                    remote = remote.strip().strip('"')
            else:
                fetch_config = self._run(
                    "config", "get", "git.fetch", return_codes=[0, 1]
                )
                if fetch_config:
                    # Windows may add extra quotes around JSON values
                    fetch_config = fetch_config.strip().strip('"')
                    remote = json.loads(fetch_config)[0]

            if not remote:
                return None

        return self._git.get_remote_url(remote, push)

    def get_changed_files(self, diff_filter="ADM", mode="(ignored)", rev="@"):
        assert all(f.lower() in self._valid_diff_filter for f in diff_filter)

        out = self._run(
            "log",
            "--revisions",
            rev,
            "--no-graph",
            "--template",
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

    # Convert a line in a regexp-based ignore file (in this case,
    # .clang-format-ignore) to a jj fileset expression. Support the small number
    # of regexp expressions used in practice.
    #
    # Note: it is possible that jj filesets will acquire regex patterns (cf
    # https://github.com/jj-vcs/jj/issues/6893 for strings), which would allow
    # drastically simplifying this code.
    def _translate_exclude_expr(self, pattern):
        if not pattern or pattern.startswith("#"):
            return None  # empty or comment
        # .*/.ignore -> **/.ignore
        pattern = pattern.replace(".*/", "**/")
        # .*/.*.pb.h --> **/*.pb.h
        pattern = re.sub(r"[.][*][^/]", "*", pattern)
        # gfx/angle/.* -> gfx/angle/**/*
        pattern = pattern.replace("(^|[^/]).*", "**/*")
        # js/src/dtoa.c.* -> js/src/dtoa.c*/**/*
        pattern = pattern.replace(".*", "*/**/*")
        selector = "glob"
        if pattern.startswith("^"):
            selector = "root-glob"
            pattern = pattern[1:]
        elif "*" not in pattern:
            selector = "root-file"
        return f'{selector}:"{pattern}"'

    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=8):
        if rev is None:
            rev = self.HEAD_REVSET
        args = ["diff", "--revisions", rev, "--git"]

        # File patterns to include
        patterns = [f'glob:"**/*{dot_extension}"' for dot_extension in extensions]
        if not patterns:
            patterns = ["all()"]

        # File patterns to exclude
        excludes = []
        if exclude_file is not None:
            with open(exclude_file) as fh:
                excludes.extend(line.strip() for line in fh)
        exclude_patterns = []

        # Construct "(F1 | F2 | F3) ~ F4 ~ F5" (Patterns F1-3 are included, F4-5
        # are excluded.)
        fileset = " ~ ".join(["(" + " | ".join(patterns) + ")"] + exclude_patterns)
        args.append(fileset)

        return self._pipefrom(*args)

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

    def add_remove_files(self, *paths: Union[str, Path], force: bool = False):
        if not paths:
            return

        relative_paths = [self._repo_root_relative_path(p) for p in paths]
        self._run_batched("file", "track", paths=relative_paths)

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        relative_paths = [self._repo_root_relative_path(p) for p in paths]
        self._run_batched("file", "untrack", paths=relative_paths)

    def get_tracked_files_finder(self, path=None):
        files = [mozpath.normsep(p) for p in self._run("file", "list").splitlines()]
        return FileListFinder(files)

    def _get_tracked_files(self, patterns):
        fileset = " | ".join(f'root-glob:"{pattern}"' for pattern in patterns)
        files = self._run("file", "list", fileset).splitlines()
        return sorted(mozpath.normsep(p) for p in files)

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

        self._run("restore", "--", str(path))

    def commit(self, message, author=None, date=None, paths=None):
        run_kwargs = {}
        cmd = ["commit", "--message", message]
        if date:
            dt = datetime.strptime(date, "%Y-%m-%d %H:%M:%S %z")
            run_kwargs["env"] = {"JJ_TIMESTAMP": dt.isoformat()}
        if paths:
            cmd.extend(paths)
        self._run(*cmd, **run_kwargs)
        if author:
            self._run("metaedit", "--author", author, "@-")

    def add_note(
        self,
        note: str,
        content: str,
        commit: Optional[str] = None,
    ):
        commit = commit or self.head_rev
        self._git.add_note(note, content, commit)

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

        if ref and dest_branch:
            ref = self._resolve_to_commit(ref)
        self._git.push(remote, ref=ref, dest_branch=dest_branch, force=force, env=env)

    def _resolve_try_branch(self):
        dest_branch = self.branch
        if not dest_branch:
            # Replicate `jj git push -c` and create a new bookmark
            template = (
                self._run_read_only(
                    "config", "get", "templates.git_push_bookmark", return_codes=[0, 1]
                ).strip()
                or '"push-" ++ change_id.short()'
            )
            dest_branch = self._run_read_only(
                "log",
                "--no-graph",
                "--limit=1",
                "--revisions",
                self.HEAD_REVSET,
                "--template",
                template,
            ).strip()
            self._run("bookmark", "create", dest_branch, "--revision", self.HEAD_REVSET)

        return dest_branch

    def _push_to_hg_try(self, message, changed_files, remote, allow_log_capture):
        if not self.has_git_cinnabar:
            raise MissingVCSExtension("cinnabar")

        with self.try_commit(message, changed_files) as head:
            if "mach_tryserver" in self._git._run("remote"):
                self._run(
                    "git", "remote", "remove", "mach_tryserver", return_codes=[0, 1]
                )
            # `jj git remote add` would barf on the cinnabar syntax here.
            self._git._run("remote", "add", "mach_tryserver", f"hg::{remote}")
            self._run("git", "import")
            cmd = (
                str(self._tool),
                "--quiet",
                "git",
                "push",
                "--remote",
                "mach_tryserver",
                "--change",
                head,
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

    def get_commits(
        self,
        head: Optional[str] = "@",
        limit: Optional[int] = None,
        follow: Optional[list[str]] = None,
    ) -> list[str]:
        """Return a list of commit SHAs for nodes on the current branch, in order that they should be applied."""
        # Note: lando gets grumpy if you try to push empty commits.
        cmd = [
            "log",
            "--no-graph",
            "--revisions",
            f"(::{head} & mutable()) ~ empty()",
            "--template",
            'commit_id ++ "\n"',
        ]
        if limit is not None:
            cmd.append(f"--limit={limit}")
        if follow is not None:
            cmd.extend(follow)

        return list(reversed(self._run_read_only(*cmd).splitlines()))

    def get_ancestors(self, limit: int = 20) -> list[str]:
        """Return the SHAs of the head commit and its ancestors, newest first."""
        return self._git.get_ancestors(self.head_rev, limit)

    def _looks_like_change_id(self, id):
        return len(id) > 0 and all(letter >= "k" and letter <= "z" for letter in id)

    def _looks_like_commit_id(self, id):
        return len(id) > 0 and all(letter in string.hexdigits for letter in id)

    def get_commit_patches(self, nodes: list[str]) -> list[bytes]:
        """Return the contents of the patch `node` in the git standard format."""
        # Warning: tests, at least, may call this with change ids rather than
        # commit ids.
        nodes = [
            id if self._looks_like_commit_id(id) else self._resolve_to_commit(id)
            for id in nodes
        ]
        return [
            self._git._run(
                "format-patch",
                node,
                "-1",
                "--always",
                "--stdout",
                "--no-base",  # in case the user's gitconfig has format.useAutoBase=true
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
        opid = self._run(
            "operation", "log", "--limit=1", "--no-graph", "--template", "id.short(16)"
        ).rstrip()
        try:
            change, _ = self.prepare_try_push(commit_message, changed_files)
            yield change
        finally:
            self._run("operation", "restore", opid)

    def prepare_try_push(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ) -> tuple[Optional[str], Callable]:
        """Create a temporary try commit as a context manager.

        Create a new commit using `commit_message` as the commit message. The commit
        may be empty, for example when only including try syntax.

        `changed_files` may contain a dict of file paths and their contents,
        see `stage_changes`.

        This function returns a tuple of the changeid of the new head and a
        function that can be called to restore the repository to its original
        state prior to this function having been run.
        """
        print("Pushing changes:")
        print(
            self._run(
                "log",
                "--no-graph",
                "--revisions",
                "heads(trunk() | (remote_bookmarks() & ancestors(@)))..@ ~ description(exact:'')",
                "--template",
                "'  ' ++ description.first_line() ++ '\n'",
            ),
            end="",
        )
        self._snapshot("prepare_try_push")
        # Redundant with the snapshot from the next command, but the semantics
        # of this operation depend on a snapshot happening (and it will eat
        # working-copy changes if not!), so be extra explicit here in case it
        # becomes possible to default snapshotting off.
        opid = self._run(
            "operation", "log", "--limit=1", "--no-graph", "--template", "id.short(16)"
        ).rstrip()
        try:
            self._run("new", "--message", commit_message, self.HEAD_REVSET)
            for path, content in (changed_files or {}).items():
                p = self.path / Path(path)
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content)
                # Manually track the file in case it is not automatically,
                # e.g. because `snapshot.auto-track` has been configured.
                self.add_remove_files(p)
            # Update the jj commit with the changes we just made.
            self._snapshot("prepare_try_push")

            # Tug any bookmarks from parent commit for pushing.
            self._run(
                "bookmark", "move", "--from", "heads(@- & bookmarks())", "--to", "@"
            )

            def cleanup():
                self._run("operation", "restore", opid)

            return self._resolve_to_change("@"), cleanup
        except:
            # cleanup is handled by the caller in the happy path to allow
            # it to log metrics. we also need to handle cleanup in the
            # exception case, where the caller won't have the cleanup handler
            # available. we lose metrics for this case, but they're not
            # crucial for this path.
            self._run("operation", "restore", opid)
            raise

    def get_last_modified_time_for_file(self, path: Path) -> datetime:
        """Return last modified in VCS time for the specified file."""
        escaped_path = str(path).replace("\\", "\\\\")
        date = self._run(
            "log",
            "--no-graph",
            "--limit=1",
            "--template",
            "committer.timestamp()",
            f'"{escaped_path}"',
        ).rstrip()
        return datetime.strptime(date, "%Y-%m-%d %H:%M:%S.%f %z")

    def config_key_list_value_missing(self, key: str):
        output = self._run_read_only(
            "config", "list", "--repo", key, stderr=subprocess.DEVNULL
        ).strip()

        # Empty output means the key is missing from repo config. We can't rely on
        # warnings because there could be one or more other warnings (eg: deprecated
        # config flags) and parsing that would be messy.
        if not output:
            return True

        if output.startswith(key):
            return False

        raise ValueError(f"Unexpected output: {output}")

    def set_config_key_value(self, key: str, value: Any):
        value_str = json.dumps(value)
        print(f'Set jj config: "{key} = {value_str}"')
        self._run("config", "set", "--repo", key, value_str)

    def _set_default_if_missing(self, config_key: str, default_value):
        """
        If `config_key` is missing in jj, set it to `default_value`.
        """
        if self.config_key_list_value_missing(config_key):
            self.set_config_key_value(config_key, default_value)
        else:
            print(f'jj config: "{config_key}" already set; skipping')

    def _get_config_value(self, key: str) -> Optional[str]:
        """Get a config value, returning None if not set."""
        value = self._run_read_only(
            "config", "get", key, return_codes=[0, 1], stderr=subprocess.DEVNULL
        )
        return value.strip() if value else None

    def _migrate_config_value(
        self, key: str, deprecated_value: str, default_value: str
    ):
        """
        Migrate a config key from a deprecated value to a new valid default value.
        Only updates if the current value matches deprecated_value.
        """
        if self._get_config_value(key) == deprecated_value:
            print(f'Migrating jj config: "{key}"')
            self.set_config_key_value(key, default_value)

    def _copy_from_git_if_missing(self, config_key: str) -> bool:
        """
        If `config_key` exists in Git and is missing in jj, copy it into jj
        Returns True if a value was copied (i.e. jj was updated).
        """
        git_value = self._git.get_config_key_value(config_key)
        if git_value and self.config_key_list_value_missing(config_key):
            self.set_config_key_value(config_key, git_value)
            return True
        return False

    def configure(
        self,
        state_dir: Path,
        update_only: bool = False,
        no_system_changes: bool = False,
    ):
        """Run the Jujutsu configuration steps."""
        print(
            "\nOur jj support currently relies on Git; checks will run for both jj and Git.\n"
        )

        self._git.configure(state_dir, update_only, no_system_changes=no_system_changes)

        topsrcdir = Path(self.path)
        if not update_only:
            print("\nConfiguring jj...")

            version_str = self._run_read_only("--version")
            if match := re.search(r"(\d+\.\d+\.\d+)", version_str):
                jj_version = Version(match.group(1))
            else:
                raise Exception("Could not find jj version")

            if jj_version < MINIMUM_SUPPORTED_JJ_VERSION:
                raise JjVersionError(
                    f"Your version of jj ({jj_version}) is too old. "
                    f"Please upgrade to at least version '{MINIMUM_SUPPORTED_JJ_VERSION}' to ensure "
                    "full compatibility and performance."
                )

            print(f"Detected jj version `{jj_version}`, which is sufficiently modern.")

            # Only set these values if they haven't been set yet so that we
            # don't overwrite existing user preferences.

            updated_author = False

            # Copy over the user.name and user.email if they've been set there by not for jj
            for key in ("user.name", "user.email"):
                if self._copy_from_git_if_missing(key):
                    updated_author = True

            if updated_author:
                self._run("metaedit", "--update-author")

            immutable_heads_key = 'revset-aliases."immutable_heads()"'
            immutable_heads_default_value = "builtin_immutable_heads() | remote_bookmarks(glob:'*', remote=exact:'origin')"
            immutable_heads_deprecated_value = (
                "builtin_immutable_heads() | remote_bookmarks(glob:'*', 'origin')"
            )
            self._migrate_config_value(
                immutable_heads_key,
                immutable_heads_deprecated_value,
                immutable_heads_default_value,
            )
            self._set_default_if_missing(
                immutable_heads_key, immutable_heads_default_value
            )

            self._set_default_if_missing("snapshot.auto-update-stale", True)

            # This enables `jj fix` which does `./mach lint --fix` on every commit in parallel
            fix_cmd = [f"{topsrcdir.as_posix()}/tools/lint/pipelint", "$path"]
            if sys.platform.startswith("win"):
                fix_cmd.insert(0, "python3")
            self._set_default_if_missing("fix.tools.mozlint.command", fix_cmd)
            self._set_default_if_missing("fix.tools.mozlint.patterns", ["glob:**/*"])

            # This enables watchman if it's installed.
            if which("watchman"):
                # Remove deprecated config keys (renamed in 0.32.0) to prevent warnings
                for key in [
                    "core.fsmonitor",
                    "core.watchman.register-snapshot-trigger",
                ]:
                    self._run(
                        "config",
                        "unset",
                        "--repo",
                        key,
                        return_codes=[0, 1],
                        stderr=subprocess.DEVNULL,
                    )

                # Set 0.32.0+ config keys
                self._set_default_if_missing("fsmonitor.backend", "watchman")
                self._set_default_if_missing(
                    "fsmonitor.watchman.register-snapshot-trigger", False
                )

                print("Checking if watchman is enabled...")
                output = self._run_read_only("debug", "watchman", "status")

                pattern = re.compile(
                    r"^Background snapshotting is (disabled|currently inactive)"
                )

                for line in output.splitlines():
                    # Filter out any 'Background snapshotting is' disabled/inactive messages.
                    # Snapshotting is disabled on purpose, and these lines could mislead users
                    # into thinking watchman isn't functioning. We only need to verify watchman status
                    # without potentially misleading users into enabling snapshotting and causing issues.
                    if not pattern.match(line):
                        print(line)
            else:
                print(
                    "Watchman could not be found on the PATH. It is recommended to "
                    "install watchman to improve performance for jj operations"
                )
