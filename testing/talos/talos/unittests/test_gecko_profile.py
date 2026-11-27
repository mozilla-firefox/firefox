# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import pathlib
import zipfile
from unittest import mock

import mozunit
import pytest

from talos import gecko_profile


@pytest.fixture(autouse=True)
def env(tmp_path):
    upload_dir = tmp_path / "upload"
    upload_dir.mkdir()
    created = []
    with mock.patch.object(
        gecko_profile, "LOG", new=mock.MagicMock()
    ) as log, mock.patch.dict(os.environ):
        yield mock.Mock(log=log, upload_dir=upload_dir, created=created)
    for profile in created:
        profile.clean()


def make(env, **config):
    config.setdefault("name", "tsvg")
    profile = gecko_profile.GeckoProfile(str(env.upload_dir), config)
    env.created.append(profile)
    env.profile_dir = pathlib.Path(profile.option("dir"))
    return profile


def test_defaults_use_standard_interval_entries_features_and_threads(env):
    profile = make(env)
    assert profile.profiling_info == {
        "gecko_profile_interval": 1,
        "gecko_profile_entries": 16 * 1024 * 1024,
        "gecko_profile_dir": str(env.profile_dir),
        "gecko_profile_features": "js,stackwalk,screenshots,memory",
        "gecko_profile_threads": "GeckoMain,Compositor,Renderer",
    }
    assert profile.profile_arcname == os.path.join(
        str(env.upload_dir), "profile_tsvg.zip"
    )
    assert profile.option("interval") == 1


def test_custom_options_and_suite_name(env):
    profile = make(
        env,
        suite="svgr",
        gecko_profile_interval=2,
        gecko_profile_entries=100,
        gecko_profile_features="js",
        gecko_profile_threads="GeckoMain",
        gecko_profile_extra_threads="DOM Worker",
    )
    assert profile.option("threads") == "GeckoMain,DOM Worker"
    assert profile.option("entries") == 100
    assert profile.option("features") == "js"
    assert profile.profile_arcname.endswith("profile_svgr.zip")


def test_first_test_clears_existing_archive(env):
    archive = env.upload_dir / "profile_tsvg.zip"
    archive.write_bytes(b"old")
    make(env)
    assert archive.exists()
    make(env, is_first_test=True)
    assert not archive.exists()


def test_update_env_sets_startup_variables_only_when_startup_enabled(env):
    environ = {}
    make(env).update_env(environ)
    assert environ == {}

    make(env, gecko_profile_startup=True, gecko_profile_interval=3).update_env(environ)
    assert environ == {
        "MOZ_PROFILER_STARTUP": "1",
        "MOZ_PROFILER_STARTUP_INTERVAL": "3",
        "MOZ_PROFILER_STARTUP_ENTRIES": str(16 * 1024 * 1024),
        "MOZ_PROFILER_STARTUP_FEATURES": "js,stackwalk,screenshots,memory",
        "MOZ_PROFILER_STARTUP_FILTERS": "GeckoMain,Compositor,Renderer",
    }


def fake_symbolicate(src, dest):
    with open(dest, "w") as f:
        f.write("symbolicated")
    return True


def read_archive(path):
    with zipfile.ZipFile(path) as arc:
        return {name: arc.read(name) for name in arc.namelist()}


def test_symbolicate_archives_profiles(env):
    profile = make(env)
    (env.profile_dir / "page.svg.profile").write_text("raw1")
    (env.profile_dir / "other").write_text("raw2")
    with mock.patch.object(
        gecko_profile, "symbolicate_profile_file", side_effect=fake_symbolicate
    ):
        profile.symbolicate(0)
        profile.symbolicate(1)
    assert read_archive(profile.profile_arcname) == {
        "profile_tsvg/page.svg/cycle_0.profile": b"symbolicated",
        "profile_tsvg/other/cycle_0.profile": b"symbolicated",
        "profile_tsvg/page.svg/cycle_1.profile": b"symbolicated",
        "profile_tsvg/other/cycle_1.profile": b"symbolicated",
    }
    assert os.environ["TALOS_LATEST_GECKO_PROFILE_ARCHIVE"] == profile.profile_arcname


@pytest.mark.parametrize(
    "kwargs", [{"return_value": False}, {"side_effect": RuntimeError("boom")}]
)
def test_symbolicate_failure_archives_raw_profile(env, kwargs):
    profile = make(env)
    (env.profile_dir / "a.profile").write_text("raw")
    with mock.patch.object(gecko_profile, "symbolicate_profile_file", **kwargs):
        profile.symbolicate(3)
    assert read_archive(profile.profile_arcname) == {
        "profile_tsvg/a/cycle_3.profile": b"raw"
    }
    assert env.log.critical.called == ("side_effect" in kwargs)


def test_symbolicate_archive_write_failure_is_logged(env):
    profile = make(env)
    (env.profile_dir / "a.profile").write_text("raw")
    with mock.patch.object(
        gecko_profile, "symbolicate_profile_file", return_value=True
    ):
        profile.symbolicate(0)
    env.log.exception.assert_called_once()
    assert read_archive(profile.profile_arcname) == {}


def test_clean_removes_profile_dir(env):
    profile = make(env)
    (env.profile_dir / "a.profile").write_text("raw")
    profile.clean()
    assert not env.profile_dir.exists()


if __name__ == "__main__":
    mozunit.main()
