# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main

from mozboot.android import (
    AndroidPackageInstallError,
    ensure_android_packages,
    ensure_gradle_jdk_installations,
    get_android_cli_path,
)

KEY = "org.gradle.java.installations.paths"
PACKAGES = {"platform-tools", "build-tools;37.0.0"}


@pytest.fixture
def mozbuild(tmp_path, monkeypatch):
    path = tmp_path / ".mozbuild"
    path.mkdir()
    monkeypatch.setattr("mozboot.android.MOZBUILD_PATH", path)
    return path


@pytest.fixture
def gradle_props(tmp_path):
    return tmp_path / "gradle.properties"


@pytest.fixture
def make_jdk(mozbuild):
    def _make_jdk(name):
        jdk = mozbuild / "jdk" / name
        jdk.mkdir(parents=True)
        return jdk

    return _make_jdk


@pytest.fixture
def jdk17(make_jdk):
    return make_jdk("jdk-17")


@pytest.fixture
def jdk17_posix(jdk17):
    return jdk17.resolve().as_posix()


@pytest.fixture
def run(gradle_props):
    def _run(new_jdk_home):
        ensure_gradle_jdk_installations(new_jdk_home, gradle_props)

    return _run


def read_paths(gradle_props):
    for line in gradle_props.read_text(encoding="utf-8").splitlines():
        if KEY in line:
            return [p.strip() for p in line.split("=", 1)[1].split(",")]
    return []


def write_jdk_paths(gradle_props, *paths):
    gradle_props.write_text(f"{KEY}={','.join(paths)}\n", encoding="utf-8")


def test_creates_file_if_missing(run, gradle_props, jdk17, jdk17_posix):
    run(jdk17)
    assert gradle_props.exists()
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_adds_to_empty_file(run, gradle_props, jdk17, jdk17_posix):
    gradle_props.write_text("", encoding="utf-8")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_preserves_other_properties(run, gradle_props, jdk17):
    gradle_props.write_text(
        "some.property=hello\nanother.property=world\n", encoding="utf-8"
    )
    run(jdk17)
    content = gradle_props.read_text(encoding="utf-8")
    assert "some.property" in content
    assert "hello" in content
    assert "another.property" in content
    assert "world" in content


def test_purges_stale_mozbuild_paths(run, gradle_props, mozbuild, jdk17, jdk17_posix):
    stale = mozbuild / "jdk" / "jdk-old"
    write_jdk_paths(gradle_props, stale.resolve().as_posix())
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1
    assert paths[0] == jdk17_posix


def test_keeps_existing_mozbuild_paths(run, gradle_props, make_jdk, jdk17, jdk17_posix):
    jdk21 = make_jdk("jdk-21")
    write_jdk_paths(gradle_props, jdk21.resolve().as_posix())
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 2
    assert jdk17_posix in paths
    assert jdk21.resolve().as_posix() in paths


def test_preserves_non_mozbuild_paths(run, gradle_props, jdk17, jdk17_posix):
    write_jdk_paths(gradle_props, "/usr/lib/jvm/java-17")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert "/usr/lib/jvm/java-17" in paths
    assert jdk17_posix in paths


def test_no_duplicates_on_repeated_calls(run, gradle_props, jdk17):
    run(jdk17)
    run(jdk17)
    paths = read_paths(gradle_props)
    assert len(paths) == 1


def test_handles_spaces_around_equals(run, gradle_props, jdk17, jdk17_posix):
    gradle_props.write_text(f"{KEY} = /usr/lib/jvm/java-17\n", encoding="utf-8")
    run(jdk17)
    paths = read_paths(gradle_props)
    assert "/usr/lib/jvm/java-17" in paths
    assert jdk17_posix in paths


@pytest.fixture
def android_cli(tmp_path, monkeypatch):
    calls = []

    def check_call(args):
        calls.append(args)
        if "install" in args:
            for package in args[args.index("install") + 1 :]:
                if not package.startswith("--"):
                    (tmp_path / package.replace(";", "/")).mkdir(parents=True)

    monkeypatch.setattr("mozboot.android.get_sdk_path", lambda os_name: tmp_path)
    monkeypatch.setattr("mozboot.android.subprocess.check_call", check_call)
    monkeypatch.setattr("mozboot.android.subprocess.call", lambda args: 0)
    monkeypatch.delenv("REPO_OS_OVERRIDE", raising=False)
    return calls


def test_install_updates_cli_first(android_cli, monkeypatch, tmp_path):
    updates = []

    def call(args):
        assert not android_cli
        updates.append(args)
        return 0xC0000409

    monkeypatch.setattr("mozboot.android.subprocess.call", call)
    ensure_android_packages("linux", set(PACKAGES))
    assert updates == [[str(get_android_cli_path(tmp_path)), "--no-metrics", "update"]]
    assert len(android_cli) == 1


def test_install_without_platform_override(android_cli, tmp_path):
    ensure_android_packages("linux", set(PACKAGES), list_packages=True)
    install, listing = android_cli
    assert f"--sdk={tmp_path}" in install
    assert "--no-metrics" in install
    assert "--force" in install
    assert set(install[install.index("--force") + 1 :]) == PACKAGES
    assert listing[-3:] == ["sdk", "list", "--all"]
    assert not any(arg.startswith("--platform") for arg in install + listing)


def test_install_with_platform_override(android_cli, monkeypatch):
    monkeypatch.setenv("REPO_OS_OVERRIDE", "macosx_arm64")
    ensure_android_packages("linux", set(PACKAGES), list_packages=True)
    install, listing = android_cli
    assert "--platform=macosx_arm64" in install
    assert listing[-1] == "--platform=macosx_arm64"


def test_install_adds_avd_system_image(android_cli):
    image = "system-images;android-34;google_apis;x86_64"
    ensure_android_packages(
        "linux", set(PACKAGES), avd_manifest={"emulator_package": image}
    )
    assert image in android_cli[0]


def test_install_fails_when_a_package_is_not_installed(
    android_cli, monkeypatch, tmp_path
):
    def check_call(args):
        android_cli.append(args)
        (tmp_path / "platform-tools").mkdir(exist_ok=True)

    monkeypatch.setattr("mozboot.android.subprocess.check_call", check_call)
    with pytest.raises(
        AndroidPackageInstallError, match=r"did not install: build-tools;37\.0\.0$"
    ):
        ensure_android_packages("linux", set(PACKAGES), list_packages=True)
    assert android_cli[-1][-2:] == ["list", "--all"]


if __name__ == "__main__":
    main()
