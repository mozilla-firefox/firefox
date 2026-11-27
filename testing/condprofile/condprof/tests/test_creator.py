# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import contextlib
import json
import os
import tarfile

import mozunit
import pytest

from condprof.tests import fakearsenic

from condprof.tests.fakearsenic import FakeSession  # isort: skip
from condprof import creator, util  # isort: skip
from condprof.changelog import Changelog  # isort: skip
from condprof.client import ProfileNotFoundError  # isort: skip
from condprof.metadata import METADATA_NAME  # isort: skip
from condprof.util import BaseEnv  # isort: skip


class FakeEnv(BaseEnv):
    def __init__(self, profile, fail_at=None):
        super().__init__(profile, "firefox", "geckodriver", None, None)
        self.fail_at = fail_at
        self.calls = []

    def _call(self, name, *args):
        self.calls.append((name,) + args)
        if name == self.fail_at:
            raise ValueError(name)

    def get_target_platform(self):
        return "linux64"

    @contextlib.contextmanager
    def get_device(self, *args, **kw):
        self._call("get_device", args, kw)
        yield "device"

    @contextlib.contextmanager
    def get_browser(self):
        self._call("get_browser")
        yield

    def get_browser_args(self, headless):
        self._call("get_browser_args", headless)
        return {"moz:firefoxOptions": {"args": []}}

    def prepare(self, logfile):
        self._call("prepare", logfile)
        with open(logfile, "w") as f:
            f.write("adb log for bob")

    def get_geckodriver(self, log_file):
        self._call("get_geckodriver")
        log_file.write("geckodriver log for bob")
        return "geckodriver-service"

    def check_session(self, session):
        self._call("check_session")

    def dump_logs(self):
        self._call("dump_logs")

    def collect_profile(self):
        self._call("collect_profile")

    def stop_browser(self):
        self._call("stop_browser")

    def get_browser_version(self):
        return "150.0"


@pytest.fixture(autouse=True)
def clean_env(monkeypatch):
    monkeypatch.delenv("MOZ_UPLOAD_DIR", raising=False)
    monkeypatch.setenv("CONDPROF_RUNNER", "1")
    monkeypatch.setenv("FXA_USERNAME", "bob")
    monkeypatch.setenv("FXA_PASSWORD", "s3cret")
    monkeypatch.setattr(util, "_CACHED", {})


@pytest.fixture
def profiles(monkeypatch):
    calls = []

    def get_profile(profile, platform, scenario, custom_name):
        calls.append(("get_profile", platform, scenario, custom_name))
        raise ProfileNotFoundError()

    def fresh_profile(profile, customization_data):
        calls.append(("fresh_profile", customization_data["name"]))
        os.makedirs(profile)
        with open(os.path.join(profile, "prefs.js"), "w") as f:
            f.write("")
        os.makedirs(os.path.join(profile, "cache2"))
        return profile

    monkeypatch.setattr(creator, "get_profile", get_profile)
    monkeypatch.setattr(creator, "fresh_profile", fresh_profile)
    return calls


@pytest.fixture
def scenario(monkeypatch):
    calls = []
    result = {"visited_url": 3}

    async def fake_scenario(session, options):
        calls.append(options)
        if isinstance(result.get("raise"), Exception):
            raise result["raise"]
        return dict(result)

    for name in ("fake", "full"):
        monkeypatch.setitem(creator.scenarii, name, fake_scenario)
    fake_scenario.calls = calls
    fake_scenario.result = result
    return fake_scenario


@pytest.fixture
def session():
    session = FakeSession(handles=[0, 1])
    fakearsenic.SESSION_FACTORY[0] = lambda service, browser: session
    yield session
    fakearsenic.SESSION_FACTORY[0] = lambda service, browser: FakeSession()


def _creator(tmp_path, scenario="fake", customization="default", archive=True, **kw):
    archive_dir = str(tmp_path / "archive") if archive else None
    if archive_dir:
        os.makedirs(archive_dir, exist_ok=True)
    env = kw.pop("env", None) or FakeEnv(str(tmp_path / "profile"))
    changelog = Changelog(archive_dir or str(tmp_path))
    return creator.ProfileCreator(
        scenario, customization, archive_dir, changelog, False, env, **kw
    )


def test_tmp_dir_uses_moz_upload_dir_when_archive_disabled(tmp_path, monkeypatch):
    profile_creator = _creator(tmp_path, archive=False)
    assert os.path.isdir(profile_creator.tmp_dir)
    os.rmdir(profile_creator.tmp_dir)

    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(tmp_path / "upload"))
    profile_creator = _creator(tmp_path, archive=False)
    assert profile_creator.tmp_dir == str(tmp_path / "upload" / "condprof")
    assert os.path.isdir(profile_creator.tmp_dir)
    assert profile_creator._log_filename("adb") == str(
        tmp_path / "upload" / "condprof" / "adb-fake-default.log"
    )


def test_run_builds_profile_archive_logs_metadata_and_changelog(
    tmp_path, profiles, scenario, session
):
    profile_creator = _creator(tmp_path, remote_test_root="/r/")
    asyncio.run(profile_creator.run(headless=False))

    env = profile_creator.env
    archive = tmp_path / "archive"
    assert profiles == [
        ("get_profile", "linux64", "fake", "default"),
        ("fresh_profile", "default"),
    ]
    assert scenario.calls == [{"platform": "linux64"}]
    assert [call[0] for call in env.calls] == [
        "get_device",
        "get_browser",
        "prepare",
        "get_browser_args",
        "get_geckodriver",
        "check_session",
        "stop_browser",
        "collect_profile",
        "dump_logs",
    ]
    assert env.calls[0][1:] == (
        (2828,),
        {"verbose": True, "remote_test_root": "/r/"},
    )
    assert env.calls[3] == ("get_browser_args", False)
    assert session.switched == [0]

    assert (archive / "adb-fake-default.log").read_text() == "adb log for <USERNAME>"
    assert (
        archive / "geckodriver-fake-default.log"
    ).read_text() == "geckodriver log for <USERNAME>"

    with tarfile.open(archive / "profile-linux64-fake-default.tgz") as tar:
        names = tar.getnames()
    assert "prefs.js" in names
    assert METADATA_NAME in names
    assert not any(name.startswith("cache2") for name in names)

    metadata = json.loads((tmp_path / "profile" / METADATA_NAME).read_text())
    assert metadata["visited_url"] == 3
    assert metadata["name"] == "fake"
    assert metadata["customization"] == "default"
    assert metadata["version"] == "150.0"
    assert metadata["platform"] == "linux64"

    (entry,) = profile_creator.changelog.history()
    assert entry["action"] == "update"
    assert entry["name"] == "fake"
    assert entry["platform"] == "linux64"


def test_run_full_default_creates_versioned_archive(
    tmp_path, profiles, scenario, session
):
    scenario.result["logs"] = {
        "sync": [{"name": "sync.log", "content": "bob uses s3cret"}]
    }
    profile_creator = _creator(tmp_path, scenario="full", skip_logs=True)
    asyncio.run(profile_creator.run())

    archive = tmp_path / "archive"
    assert (archive / "profile-linux64-full-default.tgz").exists()
    assert (archive / "profile-v150.0-linux64-full-default.tgz").exists()
    assert (archive / "sync-sync.log").read_text() == "<USERNAME> uses <PASSWORD>"
    assert "dump_logs" not in [call[0] for call in profile_creator.env.calls]
    (entry,) = profile_creator.changelog.history()
    assert "logs" not in entry
    assert scenario.calls == [{"max_urls": 150, "platform": "linux64"}]


def test_run_without_archive_writes_logs_to_tmp_dir_only(
    tmp_path, profiles, scenario, session, monkeypatch
):
    monkeypatch.setenv("MOZ_UPLOAD_DIR", str(tmp_path / "upload"))
    profile_creator = _creator(tmp_path, archive=False)
    asyncio.run(profile_creator.run())
    assert profile_creator.changelog.history() == []
    assert os.path.exists(os.path.join(profile_creator.tmp_dir, "adb-fake-default.log"))
    assert (tmp_path / "profile" / METADATA_NAME).exists()


def test_run_raises_with_result_message_and_skips_changelog(
    tmp_path, profiles, scenario, session
):
    scenario.result.update({"result": 1, "result_message": "sync failed"})
    profile_creator = _creator(tmp_path)
    with pytest.raises(Exception, match="sync failed"):
        asyncio.run(profile_creator.run())
    assert profile_creator.changelog.history() == []


def _custom(tmp_path, **data):
    data.setdefault("name", "custom")
    data.setdefault("addons", {})
    data.setdefault("prefs", {})
    path = tmp_path / "custom.json"
    path.write_text(json.dumps(data))
    return str(path)


@pytest.mark.parametrize(
    "data",
    [{"ignore_scenario": ["fake"]}, {"platforms": ["win64"]}],
)
def test_run_is_skipped_when_customization_excludes_it(
    tmp_path, profiles, scenario, data
):
    profile_creator = _creator(tmp_path, customization=_custom(tmp_path, **data))
    assert asyncio.run(profile_creator.run()) is None
    assert profile_creator.env.calls == []
    assert scenario.calls == []


def test_run_applies_customization_scenario_options_on_allowed_platform(
    tmp_path, profiles, scenario, session
):
    custom = _custom(tmp_path, platforms=["linux64"], scenario={"fake": {"sleep": 1}})
    profile_creator = _creator(tmp_path, customization=custom)
    asyncio.run(profile_creator.run())
    assert scenario.calls == [{"sleep": 1, "platform": "linux64"}]
    assert (tmp_path / "archive" / "profile-linux64-fake-custom.tgz").exists()


def test_build_profile_force_new_creates_fresh_profile(
    tmp_path, profiles, scenario, session
):
    profile_creator = _creator(tmp_path)
    profile_creator.force_new = True
    metadata = asyncio.run(profile_creator.build_profile("device", True))
    assert profiles == [("fresh_profile", "default")]
    assert metadata["visited_url"] == 3


def test_build_profile_reuses_existing_profile(
    tmp_path, monkeypatch, scenario, session
):
    def get_profile(profile, platform, scenario, custom_name):
        os.makedirs(profile)
        with open(os.path.join(profile, METADATA_NAME), "w") as f:
            f.write(json.dumps({"created": "2026-01-01 00:00:00.000000"}))

    monkeypatch.setattr(creator, "get_profile", get_profile)
    profile_creator = _creator(tmp_path)
    metadata = asyncio.run(profile_creator.build_profile("device", True))
    assert metadata["created"] == "2026-01-01 00:00:00.000000"
    assert metadata["updated"] > metadata["created"]


@pytest.mark.parametrize(
    "fail_at", ["get_browser_args", "get_geckodriver", "session", "scenario"]
)
def test_build_profile_stops_browser_and_dumps_logs_on_failure(
    tmp_path, profiles, scenario, session, fail_at
):
    env = FakeEnv(str(tmp_path / "profile"), fail_at=fail_at)
    if fail_at == "session":

        def broken(service, browser):
            raise ValueError("session")

        fakearsenic.SESSION_FACTORY[0] = broken
    if fail_at == "scenario":
        scenario.result["raise"] = ValueError("scenario")
    profile_creator = _creator(tmp_path, env=env)
    with pytest.raises(ValueError, match=fail_at):
        asyncio.run(profile_creator.run())
    names = [call[0] for call in env.calls]
    assert names[-2:] == ["stop_browser", "dump_logs"]
    assert "collect_profile" not in names


def test_build_profile_failure_without_adb_log_still_stops_browser(
    tmp_path, profiles, scenario, monkeypatch
):
    def broken_firefox(**kw):
        raise ValueError("firefox")

    monkeypatch.setattr(creator, "Firefox", broken_firefox)

    class NoLogEnv(FakeEnv):
        def prepare(self, logfile):
            self._call("prepare", logfile)

    env = NoLogEnv(str(tmp_path / "profile"))
    profile_creator = _creator(tmp_path, env=env)
    with pytest.raises(ValueError, match="firefox"):
        asyncio.run(profile_creator.build_profile("device", True))
    assert not (tmp_path / "archive" / "adb-fake-default.log").exists()
    assert [call[0] for call in env.calls][-1] == "stop_browser"


if __name__ == "__main__":
    mozunit.main()
