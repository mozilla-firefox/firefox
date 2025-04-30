# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pathlib
import sys
import tempfile
import unittest
from contextlib import nullcontext as does_not_raise

import mozpack.path as mozpath
import mozunit
import pytest

from mozbuild.repackaging import rpm, utils

_APPLICATION_INI_CONTENT = """[App]
Vendor=Mozilla
Name=Firefox
RemotingName=firefox-nightly-try
CodeName=Firefox Nightly
BuildID=20230222000000
"""

_APPLICATION_INI_CONTENT_DATA = {
    "name": "Firefox",
    "display_name": "Firefox Nightly",
    "vendor": "Mozilla",
    "remoting_name": "firefox-nightly-try",
    "build_id": "20230222000000",
}


@pytest.mark.parametrize(
    "version, build_number, package_name_suffix, description_suffix, release_product, application_ini_data, expected, raises",
    (
        (
            "112.0a1",
            1,
            "",
            "",
            "firefox",
            {
                "name": "Firefox",
                "display_name": "Firefox",
                "vendor": "Mozilla",
                "remoting_name": "firefox-nightly-try",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox",
                "PKG_INSTALL_PATH": "usr/lib/firefox-nightly-try",
                "PKG_NAME": "firefox-nightly-try",
                "PKG_VERSION": "112.0a1",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-nightly-try",
            },
            does_not_raise(),
        ),
        (
            "112.0a1",
            1,
            "-l10n-fr",
            " - Language pack for Firefox Nightly for fr",
            "firefox",
            {
                "name": "Firefox",
                "display_name": "Firefox",
                "vendor": "Mozilla",
                "remoting_name": "firefox-nightly-try",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox - Language pack for Firefox Nightly for fr",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox",
                "PKG_INSTALL_PATH": "usr/lib/firefox-nightly-try",
                "PKG_NAME": "firefox-nightly-try-l10n-fr",
                "PKG_VERSION": "112.0a1",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-nightly-try-l10n-fr",
            },
            does_not_raise(),
        ),
        (
            "112.0b1",
            1,
            "",
            "",
            "firefox",
            {
                "name": "Firefox",
                "display_name": "Firefox",
                "vendor": "Mozilla",
                "remoting_name": "firefox-nightly-try",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox",
                "PKG_INSTALL_PATH": "usr/lib/firefox-nightly-try",
                "PKG_NAME": "firefox-nightly-try",
                "PKG_VERSION": "112.0b1",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-nightly-try",
            },
            does_not_raise(),
        ),
        (
            "112.0",
            2,
            "",
            "",
            "firefox",
            {
                "name": "Firefox",
                "display_name": "Firefox",
                "vendor": "Mozilla",
                "remoting_name": "firefox-nightly-try",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox",
                "PKG_INSTALL_PATH": "usr/lib/firefox-nightly-try",
                "PKG_NAME": "firefox-nightly-try",
                "PKG_VERSION": "112.0",
                "PKG_BUILD_NUMBER": 2,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-nightly-try",
            },
            does_not_raise(),
        ),
        (
            "120.0b9",
            1,
            "",
            "",
            "devedition",
            {
                "name": "Firefox",
                "display_name": "Firefox Developer Edition",
                "vendor": "Mozilla",
                "remoting_name": "firefox-aurora",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox Developer Edition",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox Developer Edition",
                "PKG_INSTALL_PATH": "usr/lib/firefox-devedition",
                "PKG_NAME": "firefox-devedition",
                "PKG_VERSION": "120.0b9",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-devedition",
            },
            does_not_raise(),
        ),
        (
            "120.0b9",
            1,
            "-l10n-ach",
            " - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
            "devedition",
            {
                "name": "Firefox",
                "display_name": "Firefox Developer Edition",
                "vendor": "Mozilla",
                "remoting_name": "firefox-aurora",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox Developer Edition - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox Developer Edition",
                "PKG_INSTALL_PATH": "usr/lib/firefox-devedition",
                "PKG_NAME": "firefox-devedition-l10n-ach",
                "PKG_VERSION": "120.0b9",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-devedition-l10n-ach",
            },
            does_not_raise(),
        ),
        (
            "120.0b9",
            1,
            "-l10n-ach",
            " - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
            "devedition",
            {
                "name": "Firefox",
                "display_name": "Firefox Developer Edition",
                "vendor": "Mozilla",
                "remoting_name": "firefox-aurora",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox Developer Edition - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox Developer Edition",
                "PKG_INSTALL_PATH": "usr/lib/firefox-devedition",
                "PKG_NAME": "firefox-devedition-l10n-ach",
                "PKG_VERSION": "120.0b9",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-devedition-l10n-ach",
            },
            does_not_raise(),
        ),
        (
            "120.0b9",
            1,
            "-l10n-ach",
            " - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
            "devedition",
            {
                "name": "Firefox",
                "display_name": "Firefox Developer Edition",
                "vendor": "Mozilla",
                "remoting_name": "firefox-aurora",
                "build_id": "20230222000000",
            },
            {
                "DESCRIPTION": "Mozilla Firefox Developer Edition - Firefox Developer Edition Language Pack for Acholi (ach) – Acoli",
                "PRODUCT_NAME": "Firefox",
                "DISPLAY_NAME": "Firefox Developer Edition",
                "PKG_INSTALL_PATH": "usr/lib/firefox-aurora",
                "PKG_NAME": "firefox-aurora-l10n-ach",
                "PKG_VERSION": "120.0b9",
                "PKG_BUILD_NUMBER": 1,
                "MANPAGE_DATE": "February 22, 2023",
                "Icon": "firefox-aurora-l10n-ach",
            },
            pytest.raises(AssertionError),
        ),
    ),
)
def test_get_build_variables(
    version,
    build_number,
    package_name_suffix,
    description_suffix,
    release_product,
    application_ini_data,
    expected,
    raises,
):
    application_ini_data = utils._parse_application_ini_data(
        application_ini_data,
        version,
        build_number,
    )
    with raises:
        build_variables = rpm._get_build_variables(
            application_ini_data,
            "x86",
            version,
            release_product=release_product,
            package_name_suffix=package_name_suffix,
            description_suffix=description_suffix,
            build_number=build_number,
        )

        assert build_variables == {
            **{
                "CHANGELOG_DATE": "Wed Feb 22 2023",
                "ARCH_NAME": "x86",
                "DEPENDS": "",
            },
            **expected,
        }


@unittest.skipIf(sys.platform.startswith("win"), "Linux only test")
@pytest.mark.parametrize(
    "does_rpm_package_exist, expectation",
    (
        (True, does_not_raise()),
        (False, pytest.raises(rpm.NoRpmPackageFound)),
    ),
)
def test_generate_rpm_archive(monkeypatch, does_rpm_package_exist, expectation):
    temp_testing_dir = tempfile.TemporaryDirectory()
    temp_testing_dir_name = temp_testing_dir.name
    copy_call_history = []  # to record all calls to shutil.copy

    # Capture calls to shutil.copy
    def fake_copy(src, dst):
        copy_call_history.append(("copy", src, dst))

    monkeypatch.setattr(rpm.shutil, "copy", fake_copy)

    monkeypatch.setattr(rpm, "_get_command", lambda src, tgt, arch: ["mock_command"])
    monkeypatch.setattr(rpm.subprocess, "check_call", lambda command, cwd: None)

    upload_dir = mozpath.join(temp_testing_dir_name, "upload_dir")
    monkeypatch.setenv("UPLOAD_DIR", upload_dir)

    # Patch os.path.exists. We want to simulate:
    # - The expected RPM package file's existence is governed by our parameter.
    # - The upload_dir does not exist (so that os.mkdir is triggered).
    # - All other paths exist.
    def fake_exists(path):
        expected_rpm_path = mozpath.join(
            temp_testing_dir_name, "target_dir", "i386", "firefox-111.0-1.i386.rpm"
        )
        if path == expected_rpm_path:
            return does_rpm_package_exist
        if path == upload_dir:
            return False  # force creation of the upload_dir
        return True

    monkeypatch.setattr(rpm.os.path, "exists", fake_exists)

    mkdir_calls = []

    def fake_mkdir(path):
        mkdir_calls.append(path)

    monkeypatch.setattr(rpm.os, "mkdir", fake_mkdir)

    # Patch pathlib.Path.glob for the noarch directory.
    # We'll have the glob return a list with one dummy RPM file.
    original_glob = pathlib.Path.glob

    def fake_glob(self, pattern):
        if pattern == "*.rpm":
            # The function will copy a dummy langpack RPM file into UPLOAD_DIR.
            # Construct a dummy path that resembles the full path.
            return [
                str(
                    self.joinpath(
                        temp_testing_dir_name,
                        "target_dir",
                        "noarch",
                        "langpack.dummy.rpm",
                    )
                )
            ]
        return original_glob(self, pattern)

    monkeypatch.setattr(rpm.pathlib.Path, "glob", fake_glob)

    with expectation:
        rpm._generate_rpm_archive(
            source_dir=mozpath.join(temp_testing_dir_name, "source_dir", "rpm"),
            infile=mozpath.join(temp_testing_dir_name, "tmp", "firefox.tar.xz"),
            target_dir=mozpath.join(temp_testing_dir_name, "target_dir"),
            output_filename="target.rpm",
            build_variables={
                "PKG_NAME": "firefox",
                "PKG_VERSION": "111.0",
                "PKG_BUILD_NUMBER": 1,
            },
            arch="x86",
        )

    # If the RPM package exists, verify that all file copies and directory creation occurred.
    if does_rpm_package_exist:
        # The function first copies the tarball.
        expected_tar_dest = mozpath.join(
            temp_testing_dir_name, "source_dir", "rpm", "firefox.tar.xz"
        )
        assert (
            "copy",
            mozpath.join(temp_testing_dir_name, "tmp", "firefox.tar.xz"),
            expected_tar_dest,
        ) in copy_call_history

        # Then it copies the RPM package.
        expected_rpm_path = mozpath.join(
            temp_testing_dir_name, "target_dir", "i386", "firefox-111.0-1.i386.rpm"
        )
        assert ("copy", expected_rpm_path, "target.rpm") in copy_call_history

        # Finally, it copies any additional RPM files from the noarch directory.
        # Since our pathlib.Path.glob returns one dummy file from the noarch dir:
        expected_dummy_path = mozpath.join(
            temp_testing_dir_name, "target_dir", "noarch", "langpack.dummy.rpm"
        )
        # Look for a call that copies the dummy file to the upload dir:
        found = any(
            src == expected_dummy_path and dst == upload_dir
            for _, src, dst in copy_call_history
        )
        assert found, "The dummy langpack RPM file was not copied to the upload_dir."

        # Verify that the function created the upload directory.
        assert upload_dir in mkdir_calls

    temp_testing_dir.cleanup()


def test_generate_rpm_archive_missing_upload_dir(monkeypatch):
    # Ensure UPLOAD_DIR is not defined.
    monkeypatch.delenv("UPLOAD_DIR", raising=False)

    monkeypatch.setattr(rpm.shutil, "copy", lambda src, dst: None)
    monkeypatch.setattr(rpm, "_get_command", lambda src, tgt, arch: ["mock_command"])
    monkeypatch.setattr(rpm.subprocess, "check_call", lambda command, cwd: None)

    # For this test, let the expected RPM file exist.
    def fake_exists(path):
        expected_rpm_path = mozpath.join(
            "/target_dir", "i386", "firefox-111.0-1.i386.rpm"
        )
        if path == expected_rpm_path:
            return True
        return True

    monkeypatch.setattr(rpm.os.path, "exists", fake_exists)

    with pytest.raises(
        OSError, match="The 'UPLOAD_DIR' environment variable is not set."
    ):
        rpm._generate_rpm_archive(
            source_dir="/source_dir/rpm",
            infile="/tmp/firefox.tar.xz",
            target_dir="/target_dir",
            output_filename="target.rpm",
            build_variables={
                "PKG_NAME": "firefox",
                "PKG_VERSION": "111.0",
                "PKG_BUILD_NUMBER": 1,
            },
            arch="x86",
        )


RPMBUILD_COMMAND = [
    "rpmbuild",
    "-ba",
    "/src/firefox.spec",
    "--define",
    "_builddir /target/build",
    "--define",
    "_rpmdir /target",
    "--define",
    "_sourcedir /src",
    "--define",
    "_srcrpmdir /target",
    "--target",
]


@pytest.mark.parametrize(
    "arch, is_chroot_available, expected",
    (
        (
            "all",
            True,
            [
                "chroot",
                "/srv/rpm-all",
                "bash",
                "-c",
                f"cd /tmp/*/source; {' '.join(RPMBUILD_COMMAND)} noarch",
            ],
        ),
        ("all", False, RPMBUILD_COMMAND + ["noarch"]),
        (
            "x86",
            True,
            [
                "chroot",
                "/srv/rpm-x86",
                "bash",
                "-c",
                f"cd /tmp/*/source; {' '.join(RPMBUILD_COMMAND)} i386",
            ],
        ),
        ("x86", False, RPMBUILD_COMMAND + ["i386"]),
        (
            "x86_64",
            True,
            [
                "chroot",
                "/srv/rpm-x86_64",
                "bash",
                "-c",
                f"cd /tmp/*/source; {' '.join(RPMBUILD_COMMAND)} x86_64",
            ],
        ),
        (
            "x86_64",
            False,
            RPMBUILD_COMMAND + ["x86_64"],
        ),
    ),
)
def test_get_command(monkeypatch, arch, is_chroot_available, expected):
    monkeypatch.setattr(rpm, "_is_chroot_available", lambda _: is_chroot_available)
    assert rpm._get_command("/src", "/target", arch) == expected


@pytest.mark.parametrize(
    "arch, does_dir_exist, expected_path, expected_result",
    (
        ("all", False, "/srv/rpm-all", False),
        ("all", True, "/srv/rpm-all", True),
        ("x86", False, "/srv/rpm-x86", False),
        ("x86_64", False, "/srv/rpm-x86_64", False),
        ("x86", True, "/srv/rpm-x86", True),
        ("x86_64", True, "/srv/rpm-x86_64", True),
    ),
)
def test_is_chroot_available(
    monkeypatch, arch, does_dir_exist, expected_path, expected_result
):
    def _mock_is_dir(path):
        assert path == expected_path
        return does_dir_exist

    monkeypatch.setattr(rpm.os.path, "isdir", _mock_is_dir)
    assert rpm._is_chroot_available(arch) == expected_result


@pytest.mark.parametrize(
    "arch, expected",
    (
        ("all", "/srv/rpm-all"),
        ("x86", "/srv/rpm-x86"),
        ("x86_64", "/srv/rpm-x86_64"),
    ),
)
def test_get_chroot_path(arch, expected):
    assert rpm._get_chroot_path(arch) == expected


if __name__ == "__main__":
    mozunit.main()
