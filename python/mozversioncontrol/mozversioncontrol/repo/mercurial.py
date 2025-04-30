# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import errno
import os
import re
import shutil
import subprocess
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Union

from mozpack.files import FileListFinder

from mozversioncontrol.errors import (
    CannotDeleteFromRootOfRepositoryException,
    MissingVCSExtension,
)
from mozversioncontrol.repo.base import Repository


class HgRepository(Repository):
    """An implementation of `Repository` for Mercurial repositories."""

    def __init__(self, path: Path, hg="hg"):
        import hglib.client

        super(HgRepository, self).__init__(path, tool=hg)
        self._env["HGPLAIN"] = "1"

        # Setting this modifies a global variable and makes all future hglib
        # instances use this binary. Since the tool path was validated, this
        # should be OK. But ideally hglib would offer an API that defines
        # per-instance binaries.
        hglib.HGPATH = str(self._tool)

        # Without connect=False this spawns a persistent process. We want
        # the process lifetime tied to a context manager.
        self._client = hglib.client.hgclient(
            self.path, encoding="UTF-8", configs=None, connect=False
        )

    @property
    def name(self):
        return "hg"

    @property
    def head_ref(self):
        return self._run("log", "-r", ".", "-T", "{node}")

    @property
    def base_ref(self):
        return self._run("log", "-r", "last(ancestors(.) and public())", "-T", "{node}")

    def base_ref_as_hg(self):
        return self.base_ref

    @property
    def branch(self):
        bookmarks_fn = Path(self.path) / ".hg" / "bookmarks.current"
        if bookmarks_fn.exists():
            with open(bookmarks_fn) as f:
                bookmark = f.read()
                return bookmark or None

        return None

    def __enter__(self):
        if self._client.server is None:
            # The cwd if the spawned process should be the repo root to ensure
            # relative paths are normalized to it.
            old_cwd = Path.cwd()
            try:
                os.chdir(self.path)
                self._client.open()
            finally:
                os.chdir(old_cwd)

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._client.close()

    def _run(self, *args, **runargs):
        if not self._client.server:
            return super(HgRepository, self)._run(*args, **runargs)

        # hglib requires bytes on python 3
        args = [a.encode("utf-8") if not isinstance(a, bytes) else a for a in args]
        return self._client.rawcommand(args).decode("utf-8")

    def get_commit_time(self):
        newest_public_revision_time = self._run(
            "log",
            "--rev",
            "heads(ancestors(.) and not draft())",
            "--template",
            "{word(0, date|hgdate)}",
            "--limit",
            "1",
        ).strip()

        if not newest_public_revision_time:
            raise RuntimeError(
                "Unable to find a non-draft commit in this hg "
                "repository. If you created this repository from a "
                'bundle, have you done a "hg pull" from hg.mozilla.org '
                "since?"
            )

        return int(newest_public_revision_time)

    def sparse_checkout_present(self):
        # We assume a sparse checkout is enabled if the .hg/sparse file
        # has data. Strictly speaking, we should look for a requirement in
        # .hg/requires. But since the requirement is still experimental
        # as of Mercurial 4.3, it's probably more trouble than its worth
        # to verify it.
        sparse = Path(self.path) / ".hg" / "sparse"

        try:
            st = sparse.stat()
            return st.st_size > 0
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise

            return False

    def get_user_email(self):
        # Output is in the form "First Last <flast@mozilla.com>"
        username = self._run("config", "ui.username", return_codes=[0, 1])
        if not username:
            # No username is set
            return None
        match = re.search(r"<(.*)>", username)
        if not match:
            # "ui.username" doesn't follow the "Full Name <email@domain>" convention
            return None
        return match.group(1)

    def _format_diff_filter(self, diff_filter, for_status=False):
        df = diff_filter.lower()
        assert all(f in self._valid_diff_filter for f in df)

        # When looking at the changes in the working directory, the hg status
        # command uses 'd' for files that have been deleted with a non-hg
        # command, and 'r' for files that have been `hg rm`ed. Use both.
        return df.replace("d", "dr") if for_status else df

    def _files_template(self, diff_filter):
        template = ""
        df = self._format_diff_filter(diff_filter)
        if "a" in df:
            template += "{file_adds % '{file}\\n'}"
        if "d" in df:
            template += "{file_dels % '{file}\\n'}"
        if "m" in df:
            template += "{file_mods % '{file}\\n'}"
        return template

    def get_changed_files(self, diff_filter="ADM", mode="unstaged", rev=None):
        if rev is None:
            # Use --no-status to print just the filename.
            df = self._format_diff_filter(diff_filter, for_status=True)
            return self._run("status", "--no-status", f"-{df}").splitlines()
        else:
            template = self._files_template(diff_filter)
            return self._run("log", "-r", rev, "-T", template).splitlines()

    def get_outgoing_files(self, diff_filter="ADM", upstream=None):
        template = self._files_template(diff_filter)

        if not upstream:
            return self._run(
                "log", "-r", "draft() and ancestors(.)", "--template", template
            ).split()

        return self._run(
            "outgoing",
            "-r",
            ".",
            "--quiet",
            "--template",
            template,
            upstream,
            return_codes=(1,),
        ).split()

    def add_remove_files(self, *paths: Union[str, Path], force: bool = False):
        if not paths:
            return

        paths = [str(path) for path in paths]

        args = ["addremove"] + paths
        m = re.search(r"\d+\.\d+", self.tool_version)
        simplified_version = float(m.group(0)) if m else 0
        if simplified_version >= 3.9:
            args = ["--config", "extensions.automv="] + args
        self._run(*args)

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        if not paths:
            return

        paths = [str(path) for path in paths]

        self._run("forget", *paths)

    def get_tracked_files_finder(self, path=None):
        # Can return backslashes on Windows. Normalize to forward slashes.
        files = list(
            p.replace("\\", "/") for p in self._run("files", "-0").split("\0") if p
        )
        return FileListFinder(files)

    def get_ignored_files_finder(self):
        # Can return backslashes on Windows. Normalize to forward slashes.
        files = list(
            p.replace("\\", "/").split(" ")[-1]
            for p in self._run("status", "-i").split("\n")
            if p
        )
        return FileListFinder(files)

    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=8):
        args = ["diff", f"-U{context}"]
        if rev:
            args += ["-c", rev]
        else:
            args += ["-r", ".^"]
        for dot_extension in extensions:
            args += ["--include", f"glob:**{dot_extension}"]
        args += ["--exclude", f"listfile:{exclude_file}"]
        return self._pipefrom(*args)

    def working_directory_clean(self, untracked=False, ignored=False):
        args = ["status", "--modified", "--added", "--removed", "--deleted"]
        if untracked:
            args.append("--unknown")
        if ignored:
            args.append("--ignored")

        # If output is empty, there are no entries of requested status, which
        # means we are clean.
        return not len(self._run(*args).strip())

    def clean_directory(self, path: Union[str, Path]):
        if Path(self.path).samefile(path):
            raise CannotDeleteFromRootOfRepositoryException()
        self._run("revert", str(path))
        for single_path in self._run("st", "-un", str(path)).splitlines():
            single_path = Path(single_path)
            if single_path.is_file():
                single_path.unlink()
            else:
                shutil.rmtree(str(single_path))

    def update(self, ref):
        return self._run("update", "--check", ref)

    def raise_for_missing_extension(self, extension: str):
        """Raise `MissingVCSExtension` if `extension` is not installed and enabled."""
        try:
            self._run("showconfig", f"extensions.{extension}")
        except subprocess.CalledProcessError:
            raise MissingVCSExtension(extension)

    def push_to_try(
        self,
        message: str,
        changed_files: Dict[str, str] = {},
        allow_log_capture: bool = False,
    ):
        if changed_files:
            self.stage_changes(changed_files)

        try:
            cmd = (str(self._tool), "push-to-try", "-m", message)
            if allow_log_capture:
                self._push_to_try_with_log_capture(
                    cmd,
                    {
                        "stdout": subprocess.PIPE,
                        "stderr": subprocess.PIPE,
                        "cwd": self.path,
                        "env": self._env,
                        "universal_newlines": True,
                        "bufsize": 1,
                    },
                )
            else:
                subprocess.check_call(
                    cmd,
                    cwd=self.path,
                    env=self._env,
                )
        except subprocess.CalledProcessError:
            self.raise_for_missing_extension("push-to-try")
            raise
        finally:
            self._run("revert", "-a")

    def get_branch_nodes(
        self, head: Optional[str] = None, base_ref: Optional[str] = None
    ) -> List[str]:
        """Return a list of commit SHAs for nodes on the current branch."""
        if not base_ref:
            base_ref = self.base_ref

        head_ref = head or self.head_ref

        return self._run(
            "log",
            "-r",
            f"{base_ref}::{head_ref} and not {base_ref}",
            "-T",
            "{node}\n",
        ).splitlines()

    def get_commit_patches(self, nodes: List[str]) -> List[bytes]:
        """Return the contents of the patch `node` in the VCS' standard format."""
        # Running `hg export` once for each commit in a large stack is
        # slow, so instead we run it once and parse the output for each
        # individual patch.
        args = ["export"]

        for node in nodes:
            args.extend(("-r", node))

        output = self._run(*args, encoding=None)

        patches = []

        current_patch = []
        for i, line in enumerate(output.splitlines(keepends=True)):
            if i != 0 and line.rstrip() == b"# HG changeset patch":
                # When we see the first line of a new patch, add the patch we have been
                # building to the patches list and start building a new patch.
                patches.append(b"".join(current_patch))
                current_patch = [line]
            else:
                # Add a new line to the patch being built.
                current_patch.append(line)

        # Add the last patch to the stack.
        patches.append(b"".join(current_patch))

        return patches

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
        if changed_files:
            self.stage_changes(changed_files)

        # Allow empty commit messages in case we only use try-syntax.
        self._run("--config", "ui.allowemptycommit=1", "commit", "-m", commit_message)

        yield self.head_ref

        try:
            self._run("prune", ".")
        except subprocess.CalledProcessError:
            # The `evolve` extension is required for `uncommit` and `prune`.
            self.raise_for_missing_extension("evolve")
            raise

    def get_last_modified_time_for_file(self, path: Path):
        """Return last modified in VCS time for the specified file."""
        out = self._run(
            "log",
            "--template",
            "{date|isodatesec}",
            "--limit",
            "1",
            "--follow",
            str(path),
        )

        return datetime.strptime(out.strip(), "%Y-%m-%d %H:%M:%S %z")
