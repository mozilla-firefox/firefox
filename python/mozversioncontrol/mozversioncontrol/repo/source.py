# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import contextlib
import os
from pathlib import Path
from typing import Callable, Optional, Union

from mozpack.files import FileListFinder

from mozversioncontrol.errors import MissingVCSTool
from mozversioncontrol.repo.base import Repository


class SrcRepository(Repository):
    """An implementation of `Repository` for Git repositories."""

    def __init__(self, path: Path, src="src"):
        super().__init__(path, tool=None)

    @property
    def name(self):
        return "src"

    @property
    def head_ref(self):
        pass

    @property
    def head_rev(self):
        pass

    @property
    def is_shallow(self):
        return False

    def is_cinnabar_repo(self) -> bool:
        return False

    @property
    def base_ref(self):
        pass

    def base_ref_as_hg(self):
        pass

    def base_ref_as_commit(self):
        pass

    @property
    def branch(self):
        pass

    @property
    def has_git_cinnabar(self):
        pass

    def get_commit_time(self):
        pass

    def sparse_checkout_present(self):
        pass

    def get_user_email(self):
        pass

    def get_upstream(self):
        pass

    def get_remote_url(self, remote=None, push=False):
        return None

    def get_changed_files(self, diff_filter="ADM", mode="unstaged", rev=None):
        return []

    def diff_stream(self, rev=None, extensions=(), exclude_file=None, context=None):
        pass

    def get_outgoing_files(self, diff_filter="ADM", upstream=None):
        return []

    def add_remove_files(self, *paths: Union[str, Path], force: bool = False):
        pass

    def forget_add_remove_files(self, *paths: Union[str, Path]):
        pass

    def get_ignored_files_finder(self):
        return FileListFinder([])

    def git_ignore(self, path):
        """This function reads the mozilla-central/.gitignore file and creates a
        list of the patterns to ignore
        """
        ignore = []
        f = open(path + "/.gitignore")
        while True:
            line = f.readline()
            if not line:
                break
            if line.startswith("#"):
                pass
            elif line.strip() and line not in ["\r", "\r\n"]:
                ignore.append(line.strip().lstrip("/"))
        f.close()
        return ignore

    def get_files(self, path):
        """This function gets all files in your source folder e.g mozilla-central
        and creates a list of that
        """
        res = []
        # move away the .git or .hg folder from path to more easily test in a hg/git repo
        for root, dirs, files in os.walk(self.path):
            base = os.path.relpath(root, self.path)
            for name in files:
                res.append(os.path.join(base, name))
        return res

    def get_tracked_files_finder(self, path=None):
        """Get files, similar to 'hg files -0' or 'git ls-files -z', thats why
        we read the .gitignore file for patterns to ignore.
        Speed could probably be improved.
        """
        import fnmatch

        if path is None:
            path = self.path
        files = list(
            p.replace("\\", "/").replace("./", "") for p in self.get_files(path) if p
        )
        files.sort()
        ig = self.git_ignore(path)
        mat = []
        for i in ig:
            x = fnmatch.filter(files, i)
            if x:
                mat = mat + x
        match = list(set(files) - set(mat))
        match.sort()
        if len(match) == 0:
            return None
        else:
            return FileListFinder(match)

    def working_directory_clean(self, untracked=False, ignored=False):
        pass

    def clean_directory(self, path: Union[str, Path]):
        pass

    def update(self, ref):
        pass

    def push(
        self,
        remote: Optional[str] = None,
        ref: Optional[str] = None,
        dest_branch: Optional[str] = None,
        force: bool = False,
        env: Optional[dict] = None,
    ):
        pass

    def _resolve_try_branch(self):
        pass

    def _push_to_git_try(self, *args, **kwargs):
        pass

    def _push_to_hg_try(self, *args, **kwargs):
        pass

    def set_config(self, name, value):
        pass

    def get_commits(self, head=None, limit=None, follow=None):
        return []

    def get_commit_patches(self, nodes: str):
        return []

    def try_commit(self, commit_message: str, changed_files=None):
        return contextlib.nullcontext()

    def prepare_try_push(
        self, commit_message: str, changed_files: Optional[dict[str, str]] = None
    ) -> tuple[Optional[str], Callable]:
        return "", lambda: None

    def get_last_modified_time_for_file(self, path: Path):
        """Return last modified in VCS time for the specified file."""
        raise MissingVCSTool

    def configure(
        self,
        state_dir: Path,
        update_only: bool = False,
        no_system_changes: bool = False,
    ):
        pass
