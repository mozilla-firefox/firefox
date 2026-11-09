# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tarfile
from datetime import datetime, timedelta

import mozversioncontrol
import requests
from mozbuild.artifact_builds import GECKO_ESR_TREES, GECKO_NIGHTLY_TREES

try:
    from cStringIO import StringIO as BytesIO
except ImportError:
    from io import BytesIO

HEADERS = {"User-Agent": "wpt manifest download"}


def get(logger, url, **kwargs):
    logger.debug(url)
    if "headers" not in kwargs:
        kwargs["headers"] = HEADERS
    return requests.get(url, **kwargs)


def abs_path(path):
    return os.path.abspath(os.path.expanduser(path))


def get_commits(logger, repo_root):
    try:
        repo = mozversioncontrol.get_repository_object(repo_root)
    except mozversioncontrol.InvalidRepoPath:
        logger.warning("No VCS found for path %s" % repo_root)
        return []

    return repo.get_commits(
        limit=50,
        follow=[
            "testing/web-platform/tests",
            "testing/web-platform/mozilla/tests",
        ],
    )


def should_download(logger, manifest_paths, rebuild_time=timedelta(days=5)):
    # TODO: Improve logic for when to download. Maybe if x revisions behind?
    for manifest_path in manifest_paths:
        if not os.path.exists(manifest_path):
            return True
        mtime = datetime.fromtimestamp(os.path.getmtime(manifest_path))
        if mtime < datetime.now() - rebuild_time:
            return True
        if os.path.getsize(manifest_path) == 0:
            return True

    logger.info("Skipping manifest download because existing file is recent")
    return False


def taskcluster_url(logger, commits):
    artifact_path = "/artifacts/public/manifests.tar.gz"

    repos = {
        tree: tree.split("/")[-1] for tree in GECKO_NIGHTLY_TREES + GECKO_ESR_TREES
    }
    cset_url = (
        "https://hg.mozilla.org/{repo}/json-pushes?"
        "changeset={changeset}&version=2&tipsonly=1"
    )

    tc_url = (
        "https://firefox-ci-tc.services.mozilla.com/api/index/v1/"
        "task/gecko.v2.{name}."
        "revision.{changeset}.source.manifest-upload"
    )

    default = (
        "https://firefox-ci-tc.services.mozilla.com/api/index/v1/"
        "task/gecko.v2.mozilla-central.latest.source.manifest-upload" + artifact_path
    )

    for revision in commits:
        req = None

        if revision == 40 * "0":
            continue

        for repo_path, index_name in repos.items():
            try:
                req_headers = HEADERS.copy()
                req_headers.update({"Accept": "application/json"})
                req = get(
                    logger,
                    cset_url.format(changeset=revision, repo=repo_path),
                    headers=req_headers,
                )
                req.raise_for_status()
            except requests.exceptions.RequestException:
                if req is not None and req.status_code == 404:
                    # The API returns a 404 if it can't find a changeset for the revision.
                    logger.debug("%s not found in %s" % (revision, repo_path))
                    continue
                else:
                    return default

            result = req.json()

            pushes = result["pushes"]
            if not pushes:
                logger.debug("Error reading response; 'pushes' key not found")
                continue
            [cset] = next(iter(pushes.values()))["changesets"]

            tc_index_url = tc_url.format(changeset=cset, name=index_name)
            try:
                req = get(logger, tc_index_url)
            except requests.exceptions.RequestException:
                return default

            if req.status_code == 200:
                return tc_index_url + artifact_path

    logger.info(
        "Can't find a commit-specific manifest so just using the most recent one"
    )

    return default


def download_manifest(logger, test_paths, commits_func, url_func, force=False):
    manifest_paths = [
        (item["manifest_path"] if isinstance(item, dict) else item.manifest_path)
        for item in test_paths.values()
    ]

    if not force and not should_download(logger, manifest_paths):
        return True

    commits = commits_func()

    url = url_func(logger, commits)
    if not url:
        logger.warning("No generated manifest found")
        return False

    logger.info("Downloading manifest from %s" % url)
    try:
        req = get(logger, url)
    except Exception:
        logger.warning("Downloading pregenerated manifest failed")
        return False

    if req.status_code != 200:
        logger.warning(
            "Downloading pregenerated manifest failed; got "
            "HTTP status %d" % req.status_code
        )
        return False

    tar = tarfile.open(mode="r:gz", fileobj=BytesIO(req.content))
    for paths in test_paths.values():
        manifest_rel_path = (
            paths["manifest_rel_path"]
            if isinstance(paths, dict)
            else paths.manifest_rel_path
        )
        manifest_path = (
            paths["manifest_path"] if isinstance(paths, dict) else paths.manifest_path
        )

        try:
            member = tar.getmember(manifest_rel_path.replace(os.path.sep, "/"))
        except KeyError:
            logger.warning("Failed to find downloaded manifest %s" % manifest_rel_path)
        else:
            try:
                logger.debug("Unpacking %s to %s" % (member.name, manifest_path))
                src = tar.extractfile(member)
                with open(manifest_path, "wb") as dest:
                    dest.write(src.read())
                src.close()
            except OSError:
                import traceback

                logger.warning(
                    "Failed to decompress %s:\n%s"
                    % (manifest_rel_path, traceback.format_exc())
                )
                return False

        os.utime(manifest_path, None)

    return True


def download_from_taskcluster(logger, repo_root, test_paths, force=False):
    return download_manifest(
        logger,
        test_paths,
        lambda: get_commits(logger, repo_root),
        taskcluster_url,
        force,
    )
