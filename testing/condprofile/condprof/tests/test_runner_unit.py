# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import asyncio
import json
import os

import mozunit
import pytest

from condprof.tests import fakearsenic  # noqa

from condprof import runner, util  # isort: skip
from condprof.android import AndroidEnv  # isort: skip
from condprof.changelog import Changelog  # isort: skip
from condprof.client import ProfileNotFoundError  # isort: skip
from condprof.desktop import DesktopEnv  # isort: skip


@pytest.fixture
def firefox(tmp_path):
    path = tmp_path / "firefox"
    path.write_text("")
    return str(path)


@pytest.fixture
def geckodriver(tmp_path):
    path = tmp_path / "geckodriver"
    path.write_text("")
    return str(path)


@pytest.fixture(autouse=True)
def tmp_profiles(tmp_path, monkeypatch):
    created = []

    def mkdtemp():
        path = tmp_path / f"tmp{len(created)}"
        path.mkdir()
        created.append(path)
        return str(path)

    monkeypatch.setattr(util.tempfile, "mkdtemp", mkdtemp)
    return created


@pytest.fixture(autouse=True)
def no_network(monkeypatch):
    versions = []
    changelogs = []
    monkeypatch.setattr(runner.mozversion, "get_version", versions.append)

    def read_changelog(platform):
        changelogs.append(platform)
        raise ProfileNotFoundError(platform)

    monkeypatch.setattr(runner, "read_changelog", read_changelog)
    monkeypatch.delenv("MOZ_FETCHES_DIR", raising=False)
    return versions, changelogs


class FakeCreator:
    instances = []
    fail = set()

    def __init__(
        self,
        scenario,
        customization,
        archive,
        changelog,
        force_new,
        env,
        skip_logs,
        remote_test_root,
    ):
        self.args = (scenario, customization, archive, changelog, force_new, env)
        self.skip_logs = skip_logs
        self.remote_test_root = remote_test_root
        FakeCreator.instances.append(self)

    async def run(self, headless):
        self.headless = headless
        scenario, customization = self.args[:2]
        if (scenario, os.path.basename(customization)) in FakeCreator.fail:
            raise ValueError(scenario)
        changelog = self.args[3]
        changelog.append("update", name=scenario)
        return (scenario, os.path.basename(customization))


@pytest.fixture
def creators(monkeypatch):
    FakeCreator.instances = []
    FakeCreator.fail = set()
    monkeypatch.setattr(runner, "ProfileCreator", FakeCreator)
    return FakeCreator


def _runner(firefox, geckodriver, archive, **kw):
    args = {
        "profile": None,
        "device_name": None,
        "strict": True,
        "force_new": False,
        "visible": False,
    }
    args.update(kw)
    return runner.Runner(
        args.pop("profile"),
        firefox,
        geckodriver,
        archive,
        args.pop("device_name"),
        args.pop("strict"),
        args.pop("force_new"),
        args.pop("visible"),
        **args,
    )


def test_init_extracts_dmg(tmp_path, monkeypatch, geckodriver):
    extracted = []
    monkeypatch.setattr(
        runner, "extract_from_dmg", lambda dmg, target: extracted.append((dmg, target))
    )
    dmg = str(tmp_path / "nightly.dmg")
    run = _runner(dmg, geckodriver, None)
    target = str(tmp_path / "firefox.app")
    assert extracted == [(dmg, target)]
    assert run.firefox == os.path.join(target, "Contents", "MacOS", "firefox")
    assert run.android is False


@pytest.mark.parametrize(
    "firefox,android", [(None, False), ("org.mozilla.fenix", True), ("ff", False)]
)
def test_init_detects_android_from_firefox_package_name(firefox, android):
    assert _runner(firefox, "gd", None).android is android


def test_prepare_desktop_creates_archive_dir_and_fetches_changelog(
    tmp_path, firefox, geckodriver, no_network
):
    versions, changelogs = no_network
    archive = tmp_path / "archive" / "nested"
    run = _runner(firefox, geckodriver, str(archive))
    run.prepare("settled", "default")
    assert versions == [firefox]
    assert archive.is_dir()
    assert changelogs == [runner.get_current_platform()]
    assert isinstance(run.changelog, Changelog)
    assert run.changelog.location == str(archive / "changelog.json")


def test_prepare_android_reads_remote_changelog_for_device(
    tmp_path, geckodriver, no_network, monkeypatch
):
    versions, changelogs = no_network
    remote = Changelog(str(tmp_path))
    monkeypatch.setattr(runner, "read_changelog", lambda plat: (plat, remote))
    run = _runner("org.mozilla.fenix", geckodriver, str(tmp_path), device_name="p6")
    run.prepare("all", "all")
    assert versions == []
    assert run.changelog == ("p6-fenix", remote)


def test_prepare_skip_logs_avoids_changelog_fetch(firefox, geckodriver, no_network):
    run = _runner(firefox, geckodriver, None, skip_logs=True)
    run.prepare("settled", "default")
    assert run.changelog == []
    assert no_network[1] == []


def test_prepare_accepts_geckodriver_found_on_path(firefox, monkeypatch):
    monkeypatch.setattr(runner.shutil, "which", lambda name: "/usr/bin/" + name)
    run = _runner(firefox, "geckodriver", None, skip_logs=True)
    run.prepare("settled", "default")


def test_prepare_resolves_firefox_under_moz_fetches_dir(
    tmp_path, monkeypatch, geckodriver, no_network
):
    (tmp_path / "firefox").mkdir()
    binary = tmp_path / "firefox" / "firefox"
    binary.write_text("")
    monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    monkeypatch.chdir(tmp_path / "firefox")
    run = _runner(os.path.join("firefox", "firefox"), geckodriver, None, skip_logs=True)
    run.prepare("settled", "default")
    assert run.firefox == str(binary)
    assert no_network[0] == [str(binary)]


@pytest.mark.parametrize("fetches", [False, True])
def test_prepare_raises_ioerror_for_missing_firefox(
    tmp_path, monkeypatch, geckodriver, fetches
):
    if fetches:
        monkeypatch.setenv("MOZ_FETCHES_DIR", str(tmp_path))
    run = _runner(str(tmp_path / "missing"), geckodriver, None)
    with pytest.raises(IOError, match="missing"):
        run.prepare("settled", "default")


@pytest.mark.parametrize(
    "scenario,customization,message",
    [
        ("settled", "nope", "customization"),
        ("nope", "default", "scenario"),
        ("settled", "default", "geckodriver"),
    ],
)
def test_prepare_raises_ioerror_for_unknown_inputs(
    tmp_path, firefox, scenario, customization, message
):
    run = _runner(firefox, str(tmp_path / "geckodriver"), None)
    with pytest.raises(IOError, match=message):
        run.prepare(scenario, customization)


def test_create_env_picks_desktop_or_android_env(tmp_path, firefox, geckodriver):
    env = _runner(firefox, geckodriver, "archive", profile="p")._create_env()
    assert isinstance(env, DesktopEnv)
    assert (env.profile, env.firefox, env.archive) == ("p", firefox, "archive")
    env = _runner("org.mozilla.fenix", geckodriver, None, device_name="d")._create_env()
    assert isinstance(env, AndroidEnv)
    assert env.device_name == "d"


def test_run_all_runs_one_creator_with_runner_options(
    tmp_path, firefox, geckodriver, creators
):
    run = _runner(
        firefox, geckodriver, str(tmp_path), visible=True, remote_test_root="/r/"
    )
    run.prepare("settled", "default")
    failures, results = asyncio.run(run.run_all())
    assert (failures, results) == (0, [("settled", "default")])
    (creator,) = creators.instances
    assert creator.args[:5] == (
        "settled",
        "default",
        str(tmp_path),
        run.changelog,
        False,
    )
    assert creator.args[5] is run.env
    assert creator.headless is False
    assert creator.skip_logs is False
    assert creator.remote_test_root == "/r/"


def test_run_all_counts_failures_across_all_combinations(
    tmp_path, firefox, geckodriver, creators
):
    creators.fail = {("settled", "webext.json"), ("full", "default")}
    run = _runner(firefox, geckodriver, str(tmp_path))
    run.prepare("all", "all")
    failures, results = asyncio.run(run.run_all())
    assert failures == 1
    assert len(results) == len(runner.scenarii) * 3 - 1
    assert ("settled", "youtube.json") in results

    run.prepare("all", "default")
    failures, results = asyncio.run(run.run_all())
    assert failures == 1
    assert sorted(results) == sorted(
        (name, "default") for name in runner.scenarii if name != "full"
    )


def test_save_writes_changelog_with_created_profile_entry(
    tmp_path, firefox, geckodriver, creators
):
    run = _runner(firefox, geckodriver, str(tmp_path))
    run.prepare("settled", "default")
    asyncio.run(run.run_all())
    run.save()
    data = json.loads((tmp_path / "changelog.json").read_text())
    assert [entry["name"] for entry in data["changes"]] == ["settled"]


@pytest.fixture
def event_loop_policy():
    asyncio.set_event_loop(asyncio.new_event_loop())
    yield
    asyncio.set_event_loop(None)


def test_run_writes_changelog_and_runs_headless(
    tmp_path, firefox, geckodriver, creators, event_loop_policy
):
    runner.run(
        str(tmp_path),
        firefox=firefox,
        scenario="settled",
        customization="default",
        geckodriver=geckodriver,
    )
    data = json.loads((tmp_path / "changelog.json").read_text())
    assert [entry["name"] for entry in data["changes"]] == ["settled"]
    (creator,) = creators.instances
    assert creator.headless is True


def test_run_raises_when_a_scenario_fails_but_saves_changelog(
    tmp_path, firefox, geckodriver, creators, event_loop_policy
):
    creators.fail = {("settled", "default")}
    with pytest.raises(Exception, match="At least one scenario failed"):
        runner.run(
            str(tmp_path),
            firefox=firefox,
            scenario="settled",
            customization="default",
            geckodriver=geckodriver,
        )
    assert (tmp_path / "changelog.json").exists()


if __name__ == "__main__":
    mozunit.main()
