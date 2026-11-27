# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import re
import shutil
import tarfile

import mozunit
import pytest
import responses
from mozprofile.prefs import Preferences

from condprof import client

PROFILE_FOR_TESTS = os.path.join(os.path.dirname(__file__), "profile")
ARCHIVE = re.compile(client.ARTIFACTS_SERVICE + "/.*tgz")
LATEST = re.compile(client.ROOT_URL + r"/v1/task/gecko\.v2\.[^/]*\.latest\..*tgz")
BY_DATE = re.compile(client.ROOT_URL + r"/v1/task/gecko\.v2\.[^/]*\.pushdate\..*tgz")


@pytest.fixture(autouse=True)
def setup(tmp_path, monkeypatch):
    monkeypatch.setattr(client, "RETRY_PAUSE", 0)
    monkeypatch.setattr(client.time, "sleep", lambda s: None)
    monkeypatch.setattr(client, "CONDPROF_CACHE", str(tmp_path / "cache"))
    monkeypatch.setattr(client, "TASK_CLUSTER", True)


@pytest.fixture(autouse=True)
def tmp_dirs(tmp_path, monkeypatch):
    created = []

    def mkdtemp():
        path = tmp_path / f"tmp{len(created)}"
        path.mkdir()
        created.append(path)
        return str(path)

    monkeypatch.setattr(client.tempfile, "mkdtemp", mkdtemp)
    return created


@pytest.fixture
def tarball(tmp_path):
    path = tmp_path / "profile.tgz"
    with tarfile.open(path, "w:gz") as tar:
        tar.add(PROFILE_FOR_TESTS, arcname=".")
    return path.read_bytes()


def _serve(pattern, body, status=200):
    headers = {"content-length": str(len(body)), "Content-Type": "application/gzip"}
    responses.add(responses.HEAD, pattern, headers=headers, status=status)
    responses.add(responses.GET, pattern, body=body, headers=headers, status=status)


def _service(url, status=200):
    responses.add(responses.HEAD, url, status=status)


def test_retries_success_after_failure():
    errors = []
    attempts = []

    def flaky():
        attempts.append(1)
        if len(attempts) < 2:
            raise ValueError("boom")
        return "ok"

    assert client._retries(flaky, errors.append) == "ok"
    assert [str(e) for e in errors] == ["boom"]


@pytest.mark.parametrize("retries", [1, client.RETRIES])
def test_retries_gives_up(retries):
    calls = []

    def fail():
        calls.append(1)
        raise ValueError()

    with pytest.raises(client.RetriesError):
        client._retries(fail, retries=retries)
    assert len(calls) == retries


@responses.activate
def test_check_service_unreachable():
    with pytest.raises(client.ServiceUnreachableError):
        client._check_service(client.TC_SERVICE)


@responses.activate
def test_check_service_reachable():
    _service(client.TC_SERVICE)
    assert client._check_service(client.TC_SERVICE) is None


def test_check_profile_forces_prefs_and_strips_marionette_from_user_js(tmp_path):
    profile = tmp_path / "profile"
    shutil.copytree(PROFILE_FOR_TESTS, profile)
    with open(profile / "prefs.js", "a") as f:
        Preferences.write(f, {"browser.startup.page": 3})
    original_user = dict(Preferences.read_prefs(str(profile / "user.js")))
    assert "marionette.port" in original_user

    client._check_profile(str(profile))

    prefs = dict(Preferences.read_prefs(str(profile / "prefs.js")))
    assert prefs == {
        "browser.startup.page": 3,
        "extensions.startupScanScopes": 1,
        "profile.conditioned": True,
    }
    user = dict(Preferences.read_prefs(str(profile / "user.js")))
    assert user == {
        name: value
        for name, value in original_user.items()
        if not name.startswith("marionette.")
    }
    assert user


@responses.activate
def test_get_profile_downloads_from_task_artifacts_without_cache(tmp_path, tarball):
    _service(client.ARTIFACTS_SERVICE)
    _serve(ARCHIVE, tarball)
    target = tmp_path / "target"
    result = client.get_profile(
        str(target),
        "linux64",
        "settled",
        task_id="TASKID",
        download_cache=False,
        version="150",
    )
    assert result == str(target)
    assert (target / "prefs.js").exists()
    urls = [call.request.url for call in responses.calls]
    assert (
        client.ARTIFACTS_SERVICE
        + "/TASKID/0/public/condprof/profile-v150-linux64-settled-default.tgz"
        in urls
    )
    assert not (tmp_path / "cache").exists()


@responses.activate
def test_get_profile_bad_archive_falls_back_to_older(tmp_path, tarball):
    _service(client.TC_SERVICE)
    _serve(LATEST, b"not a tarball")
    _serve(BY_DATE, tarball)
    target = tmp_path / "target"
    assert client.get_profile(str(target), "linux64", "settled", retries=1) == str(
        target
    )
    assert (target / "prefs.js").exists()
    cache = tmp_path / "cache"
    assert os.listdir(cache) == ["profile-linux64-settled-default.tgz"]


@responses.activate
def test_get_profile_not_found(tmp_path):
    _service(client.TC_SERVICE)
    responses.add(
        responses.HEAD,
        re.compile(client.ROOT_URL + "/.*"),
        headers={"Content-Type": "text/html"},
    )
    with pytest.raises(client.ProfileNotFoundError, match="pushdate"):
        client.get_profile(str(tmp_path / "target"), "linux64", "settled", retries=1)


@responses.activate
def test_get_profile_onerror_cannot_remove(tmp_path, monkeypatch):
    source = tmp_path / "source"
    source.mkdir()
    (source / "prefs.js").write_text("")
    path = tmp_path / "no-user-js.tgz"
    with tarfile.open(path, "w:gz") as tar:
        tar.add(str(source), arcname=".")
    _service(client.TC_SERVICE)
    _serve(re.compile(client.ROOT_URL + "/.*"), path.read_bytes())
    removed = []

    def remove(path):
        removed.append(path)
        raise OSError("locked")

    monkeypatch.setattr(client.os, "remove", remove)
    with pytest.raises(client.ProfileNotFoundError):
        client.get_profile(str(tmp_path / "target"), "linux64", "settled", retries=1)
    cached = str(tmp_path / "cache" / "profile-linux64-settled-default.tgz")
    assert removed == [cached, cached]


@responses.activate
def test_read_changelog_returns_remote_history(tmp_path):
    body = json.dumps({"changes": [{"when": 1, "action": "update"}]}).encode()
    pattern = re.compile(client.ROOT_URL + "/.*/changelog.json")
    responses.add(responses.HEAD, pattern, headers={"Content-Type": "application/json"})
    responses.add(
        responses.GET,
        pattern,
        body=body,
        headers={"content-length": str(len(body))},
    )
    changelog = client.read_changelog("linux64")
    assert changelog.history() == [{"when": 1, "action": "update"}]
    assert "condprof-linux64-settled" in responses.calls[0].request.url


@responses.activate
def test_read_changelog_not_found(monkeypatch):
    responses.add(
        responses.HEAD,
        re.compile(client.ROOT_URL + "/.*"),
        headers={"Content-Type": "text/html"},
    )
    with pytest.raises(client.ProfileNotFoundError, match="changelog.json"):
        client.read_changelog("linux64")


if __name__ == "__main__":
    mozunit.main()
