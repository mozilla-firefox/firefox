# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import manifestdownload
import mozunit
import requests

OLD = "a" * 40
NEW = "b" * 40
ROOT_URL = "https://tc.example"
DEFAULT_URL = (
    f"{ROOT_URL}/api/index/v1/task/"
    "gecko.v2.mozilla-central.latest.source.manifest-upload"
    "/artifacts/public/manifests.tar.gz"
)


def index_url(revision, name):
    return (
        f"{ROOT_URL}/api/index/v1/task/"
        f"gecko.v2.{name}.revision.{revision}.source.manifest-upload"
    )


def git_taskcluster_url(commits, found=None, error=None, miss_status=404):
    requested = []

    def get(logger, url, **kwargs):
        requested.append(url)
        if error:
            raise error
        return mock.Mock(status_code=200 if url == found else miss_status)

    with mock.patch.object(manifestdownload, "get", get):
        with mock.patch.object(manifestdownload, "get_root_url", return_value=ROOT_URL):
            url = manifestdownload.git_taskcluster_url(mock.Mock(), commits)
    return url, requested


def test_git_taskcluster_url_first_indexed_ancestor():
    found = index_url(OLD, "autoland")
    url, requested = git_taskcluster_url([NEW, OLD], found=found)
    assert url == found + "/artifacts/public/manifests.tar.gz"
    assert requested == [
        index_url(NEW, name) for name in manifestdownload.REPOS.values()
    ] + [index_url(OLD, "mozilla-central"), found]


def test_git_taskcluster_url_not_found():
    url, requested = git_taskcluster_url([NEW, OLD])
    assert url == DEFAULT_URL
    assert len(requested) == 2 * len(manifestdownload.REPOS)


def test_git_taskcluster_url_server_error():
    url, requested = git_taskcluster_url([NEW, OLD], miss_status=503)
    assert url == DEFAULT_URL
    assert requested == [index_url(NEW, "mozilla-central")]


def test_git_taskcluster_url_request_error():
    url, requested = git_taskcluster_url(
        [NEW, OLD], error=requests.exceptions.ConnectionError()
    )
    assert url == DEFAULT_URL
    assert requested == [index_url(NEW, "mozilla-central")]


def manifest_url(vcs):
    repo = mock.Mock(get_ancestors=mock.Mock(return_value=[NEW, OLD]))
    repo.name = vcs
    with mock.patch.object(
        manifestdownload.mozversioncontrol, "get_repository_object", return_value=repo
    ):
        with mock.patch.object(manifestdownload, "git_taskcluster_url") as git_url:
            with mock.patch.object(manifestdownload, "taskcluster_url") as hg_url:
                url = manifestdownload.manifest_url(mock.Mock(), "/src")
    return url, git_url, hg_url


def test_manifest_url_git():
    for vcs in ("git", "jj"):
        url, git_url, hg_url = manifest_url(vcs)
        assert url == git_url.return_value
        assert git_url.call_args.args[1] == [NEW, OLD]
        hg_url.assert_not_called()


def test_manifest_url_hg():
    url, git_url, hg_url = manifest_url("hg")
    assert url == hg_url.return_value
    git_url.assert_not_called()


if __name__ == "__main__":
    mozunit.main()
