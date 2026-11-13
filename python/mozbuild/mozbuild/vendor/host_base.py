# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import tempfile
import urllib

import mozfile
import requests
import urllib3


class HttpError(Exception):
    pass


class TagNotFound(Exception):
    """No matching tag was found in the upstream repository."""


class BaseHost:
    MAX_RETRIES_DEFAULT = urllib3.util.Retry(
        total=5, backoff_factor=1, status_forcelist=[429, 500, 502, 503, 504]
    )

    def __init__(self, manifest, max_retries=MAX_RETRIES_DEFAULT):
        self.manifest = manifest
        self.repo_url = urllib.parse.urlparse(self.manifest["vendoring"]["url"])
        adapter = requests.adapters.HTTPAdapter(max_retries=max_retries)
        self.session = requests.Session()
        self.session.mount("http://", adapter)
        self.session.mount("https://", adapter)

    def upstream_tag(self, revision):
        """Temporarily fetch the repo's tags to get the latest tag and timestamp"""
        fetch_config = []
        if self.manifest["vendoring"].get("tolerate-git-fsck-errors", False):
            fetch_config += [
                "-c",
                "fetch.fsckObjects=false",
                "-c",
                "transfer.fsckObjects=false",
            ]

        with tempfile.TemporaryDirectory() as repo_clone:
            # Run git with cwd= rather than moving the process with os.chdir():
            # this directory is deleted when the block exits, so a caller that
            # handles the exceptions raised below would be left sitting in a
            # directory that no longer exists.
            subprocess.check_call(["git", "init", "--quiet"], cwd=repo_clone)
            # Sorting tags by creatordate and reading a tag's date only needs the
            # tag objects and the commits they point at, so fetch just those: no
            # history, trees or file contents. HEAD is fetched too because git
            # fails a fetch whose only refspec matches nothing, and a repo without
            # tags should raise TagNotFound below.
            subprocess.check_call(
                ["git"]
                + fetch_config
                + [
                    "fetch",
                    "--quiet",
                    "--depth=1",
                    "--filter=tree:0",
                    self.manifest["vendoring"]["url"],
                    "HEAD",
                    "refs/tags/*:refs/tags/*",
                ],
                cwd=repo_clone,
            )
            revision_arg = []
            if revision and revision != "HEAD":
                revision_arg = [revision]

            try:
                tag = subprocess.run(
                    [
                        "git",
                        "--no-pager",
                        "tag",
                        "-l",
                        "--no-column",
                        "--sort=creatordate",
                    ]
                    + revision_arg,
                    cwd=repo_clone,
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.splitlines()[-1]
            except IndexError:  # 0 lines of output, the tag does not exist
                if revision:
                    raise TagNotFound(f"Requested tag {revision} not found in source.")
                else:
                    raise TagNotFound("No tags found in source.")

            tag_timestamp = subprocess.run(
                [
                    "git",
                    "log",
                    "-1",
                    "--date=iso8601-strict",
                    "--format=%cd",
                    tag,
                ],
                cwd=repo_clone,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.splitlines()[-1]
            return tag, tag_timestamp

    def upstream_snapshot(self, revision):
        raise Exception("Unimplemented for this subclass...")

    def upstream_path_to_file(self, revision, filepath):
        raise Exception("Unimplemented for this subclass...")

    def upstream_release_artifact(self, revision, release_artifact):
        raise Exception("Unimplemented for this subclass...")

    def _transform_single_file_to_destination(self, from_file, destination):
        shutil.copy2(from_file.name, destination)

    def download_single_file(self, url, destination):
        response = self.session.get(url, stream=True)
        if response.status_code == requests.codes.not_found:
            if os.path.isfile(destination):
                # File no longer exists remove local copy.
                os.remove(destination)
            return False

        if response.status_code != 200:
            raise HttpError(response.status_code, url)
        with mozfile.NamedTemporaryFile() as tmpfile:
            for data in response.iter_content(4096):
                tmpfile.write(data)

            tmpfile.seek(0)
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            self._transform_single_file_to_destination(tmpfile, destination)
        return True
