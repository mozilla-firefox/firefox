# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import json
import os
import tarfile
import tempfile
import zipfile
from contextlib import nullcontext as does_not_raise
from unittest.mock import MagicMock, call

import mozpack.path as mozpath
import mozunit
import pytest

from mozbuild.repackaging import utils

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
    "number_of_application_ini_files, expectaction, expected_result",
    (
        (0, pytest.raises(ValueError), None),
        (1, does_not_raise(), _APPLICATION_INI_CONTENT_DATA),
        (2, pytest.raises(ValueError), None),
    ),
)
def test_extract_application_ini_data(
    number_of_application_ini_files, expectaction, expected_result
):
    with tempfile.TemporaryDirectory() as d:
        tar_path = os.path.join(d, "input.tar")
        with tarfile.open(tar_path, "w") as tar:
            application_ini_path = os.path.join(d, "application.ini")
            with open(application_ini_path, "w") as application_ini_file:
                application_ini_file.write(_APPLICATION_INI_CONTENT)

            for i in range(number_of_application_ini_files):
                tar.add(application_ini_path, f"{i}/application.ini")

        with expectaction:
            assert utils._extract_application_ini_data(tar_path) == expected_result


def test_extract_application_ini_data_from_directory():
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "application.ini"), "w") as f:
            f.write(_APPLICATION_INI_CONTENT)

        assert (
            utils._extract_application_ini_data_from_directory(d)
            == _APPLICATION_INI_CONTENT_DATA
        )


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
        if not package_name_suffix:
            depends = "${shlibs:Depends},"
        elif release_product == "devedition":
            depends = f"firefox-devedition (= {application_ini_data['pkg_version']})"
        else:
            depends = f"{application_ini_data['remoting_name']} (= {application_ini_data['pkg_version']})"

        build_variables = utils.get_build_variables(
            application_ini_data,
            "x86",
            version,
            depends=depends,
            package_name_suffix=package_name_suffix,
            description_suffix=description_suffix,
            release_product=release_product,
            build_number=build_number,
        )

        assert build_variables == {
            **{
                "CHANGELOG_DATE": "Wed, 22 Feb 2023 00:00:00 -0000",
                "ARCH_NAME": "x86",
                "DEPENDS": depends,
            },
            **expected,
        }


def test_copy_plain_config(monkeypatch):
    def mock_listdir(dir):
        assert dir == "/template_dir"
        return [
            "/template_dir/debian_file1.in",
            "/template_dir/debian_file2.in",
            "/template_dir/debian_file3",
            "/template_dir/debian_file4",
        ]

    monkeypatch.setattr(utils.os, "listdir", mock_listdir)

    def mock_makedirs(dir, exist_ok):
        assert dir == "/source_dir/debian"
        assert exist_ok is True

    monkeypatch.setattr(utils.os, "makedirs", mock_makedirs)

    mock_copy = MagicMock()
    monkeypatch.setattr(utils.shutil, "copy", mock_copy)

    utils.copy_plain_config("/template_dir", "/source_dir/debian")
    assert mock_copy.call_args_list == [
        call("/template_dir/debian_file3", "/source_dir/debian/debian_file3"),
        call("/template_dir/debian_file4", "/source_dir/debian/debian_file4"),
    ]


def test_render_templates():
    with tempfile.TemporaryDirectory() as template_dir, tempfile.TemporaryDirectory() as source_dir:
        with open(os.path.join(template_dir, "debian_file1.in"), "w") as f:
            f.write("${some_build_variable}")

        with open(os.path.join(template_dir, "debian_file2.in"), "w") as f:
            f.write("Some hardcoded value")

        with open(os.path.join(template_dir, "ignored_file.in"), "w") as f:
            f.write("Must not be copied")

        utils.render_templates(
            template_dir,
            mozpath.join(source_dir, "debian"),
            {"some_build_variable": "some_value"},
            exclude_file_names=["ignored_file.in"],
        )

        with open(os.path.join(source_dir, "debian", "debian_file1")) as f:
            assert f.read() == "some_value"

        with open(os.path.join(source_dir, "debian", "debian_file2")) as f:
            assert f.read() == "Some hardcoded value"

        assert not os.path.exists(os.path.join(source_dir, "debian", "ignored_file"))
        assert not os.path.exists(os.path.join(source_dir, "debian", "ignored_file.in"))


def test_inject_distribution_folder(monkeypatch):
    def mock_check_call(command):
        global clone_dir
        clone_dir = command[-1]
        os.makedirs(os.path.join(clone_dir, "desktop/deb/distribution"))

    monkeypatch.setattr(utils.subprocess, "check_call", mock_check_call)

    def mock_copytree(source_tree, destination_tree):
        global clone_dir
        assert source_tree == mozpath.join(clone_dir, "desktop/deb/distribution")
        assert destination_tree == "/source_dir/firefox/distribution"

    monkeypatch.setattr(utils.shutil, "copytree", mock_copytree)

    utils.inject_distribution_folder("/source_dir", "debian", "Firefox")


ZH_TW_FTL = """\
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


# These messages are used by the Firefox ".desktop" file on Linux.
# https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html

# The entry name is the label on the desktop icon, among other things.
desktop-entry-name = { -brand-shortcut-name }
# The comment usually appears as a tooltip when hovering over application menu entry.
desktop-entry-comment = 瀏覽全球資訊網
desktop-entry-generic-name = 網頁瀏覽器
# Keywords are search terms used to find this application.
# The string is a list of keywords separated by semicolons:
# - Do NOT replace semicolons with other punctuation signs.
# - The list MUST end with a semicolon.
desktop-entry-keywords = 網際網路;網路;瀏覽器;網頁;上網;Internet;WWW;Browser;Web;Explorer;

## Actions are visible in a context menu after right clicking the
## taskbar icon, possibly other places depending on the environment.

desktop-action-new-window-name = 開新視窗
desktop-action-new-private-window-name = 開新隱私視窗
"""

NIGHTLY_DESKTOP_ENTRY_FILE_TEXT = """\
[Desktop Entry]
Version=1.0
Type=Application
Exec=firefox-nightly %u
Terminal=false
X-MultipleArgs=false
Icon=firefox-nightly
StartupWMClass=firefox-nightly
Categories=GNOME;GTK;Network;WebBrowser;
MimeType=application/json;application/pdf;application/rdf+xml;application/rss+xml;application/x-xpinstall;application/xhtml+xml;application/xml;audio/flac;audio/ogg;audio/webm;image/avif;image/gif;image/jpeg;image/png;image/svg+xml;image/webp;text/html;text/xml;video/ogg;video/webm;x-scheme-handler/chrome;x-scheme-handler/http;x-scheme-handler/https;x-scheme-handler/mailto;
StartupNotify=true
Actions=new-window;new-private-window;open-profile-manager;
Name=en-US-desktop-entry-name
Name[zh_TW]=zh-TW-desktop-entry-name
Comment=en-US-desktop-entry-comment
Comment[zh_TW]=zh-TW-desktop-entry-comment
GenericName=en-US-desktop-entry-generic-name
GenericName[zh_TW]=zh-TW-desktop-entry-generic-name
Keywords=en-US-desktop-entry-keywords
Keywords[zh_TW]=zh-TW-desktop-entry-keywords
X-GNOME-FullName=en-US-desktop-entry-x-gnome-full-name
X-GNOME-FullName[zh_TW]=zh-TW-desktop-entry-x-gnome-full-name

[Desktop Action new-window]
Exec=firefox-nightly --new-window %u
Name=en-US-desktop-action-new-window-name
Name[zh_TW]=zh-TW-desktop-action-new-window-name

[Desktop Action new-private-window]
Exec=firefox-nightly --private-window %u
Name=en-US-desktop-action-new-private-window-name
Name[zh_TW]=zh-TW-desktop-action-new-private-window-name

[Desktop Action open-profile-manager]
Exec=firefox-nightly --ProfileManager
Name=en-US-desktop-action-open-profile-manager
Name[zh_TW]=zh-TW-desktop-action-open-profile-manager
"""

DEVEDITION_DESKTOP_ENTRY_FILE_TEXT = """\
[Desktop Entry]
Version=1.0
Type=Application
Exec=firefox-devedition %u
Terminal=false
X-MultipleArgs=false
Icon=firefox-devedition
StartupWMClass=firefox-aurora
Categories=GNOME;GTK;Network;WebBrowser;
MimeType=application/json;application/pdf;application/rdf+xml;application/rss+xml;application/x-xpinstall;application/xhtml+xml;application/xml;audio/flac;audio/ogg;audio/webm;image/avif;image/gif;image/jpeg;image/png;image/svg+xml;image/webp;text/html;text/xml;video/ogg;video/webm;x-scheme-handler/chrome;x-scheme-handler/http;x-scheme-handler/https;x-scheme-handler/mailto;
StartupNotify=true
Actions=new-window;new-private-window;open-profile-manager;
Name=en-US-desktop-entry-name
Name[zh_TW]=zh-TW-desktop-entry-name
Comment=en-US-desktop-entry-comment
Comment[zh_TW]=zh-TW-desktop-entry-comment
GenericName=en-US-desktop-entry-generic-name
GenericName[zh_TW]=zh-TW-desktop-entry-generic-name
Keywords=en-US-desktop-entry-keywords
Keywords[zh_TW]=zh-TW-desktop-entry-keywords
X-GNOME-FullName=en-US-desktop-entry-x-gnome-full-name
X-GNOME-FullName[zh_TW]=zh-TW-desktop-entry-x-gnome-full-name

[Desktop Action new-window]
Exec=firefox-devedition --new-window %u
Name=en-US-desktop-action-new-window-name
Name[zh_TW]=zh-TW-desktop-action-new-window-name

[Desktop Action new-private-window]
Exec=firefox-devedition --private-window %u
Name=en-US-desktop-action-new-private-window-name
Name[zh_TW]=zh-TW-desktop-action-new-private-window-name

[Desktop Action open-profile-manager]
Exec=firefox-devedition --ProfileManager
Name=en-US-desktop-action-open-profile-manager
Name[zh_TW]=zh-TW-desktop-action-open-profile-manager
"""


def test_inject_desktop_entry_file(monkeypatch):
    source_dir = "/source_dir"
    build_variables = {
        "PRODUCT_NAME": "Firefox",
        "PKG_NAME": "firefox-nightly",
    }
    release_product = "firefox"
    release_type = "nightly"

    desktop_entry_template_path = mozpath.join(
        source_dir, "debian", f"{build_variables['PRODUCT_NAME'].lower()}.desktop"
    )
    desktop_entry_file_filename = f"{build_variables['PKG_NAME']}.desktop"

    # Check if pre-supplied .desktop file is being copied to the correct location
    def mock_move(source_path, destination_path):
        assert source_path == desktop_entry_template_path
        assert destination_path == f"/source_dir/debian/{desktop_entry_file_filename}"

    monkeypatch.setattr(utils.shutil, "move", mock_move)

    # Bypass generating the .desktop file's contents,
    # since that is tested in test_generate_deb_desktop_entry_file_text()
    def mock_generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    ):
        return DEVEDITION_DESKTOP_ENTRY_FILE_TEXT

    monkeypatch.setattr(
        utils,
        "generate_browser_desktop_entry_file_text",
        mock_generate_browser_desktop_entry_file_text,
    )

    # Check if the .desktop file exists in its final location
    with tempfile.TemporaryDirectory() as source_dir:
        utils.inject_desktop_entry_file(
            None,
            mozpath.join(source_dir, "debian"),
            build_variables,
            release_product,
            release_type,
            None,
            None,
        )

        assert os.path.exists(
            os.path.join(source_dir, "debian", desktop_entry_file_filename)
        )


@pytest.mark.parametrize(
    "version, build_number, expected",
    (
        (
            "112.0a1",
            1,
            {
                "build_id": "20230222000000",
                "display_name": "Firefox Nightly",
                "name": "Firefox",
                "pkg_version": "112.0a1-1",
                "remoting_name": "firefox-nightly-try",
                "timestamp": datetime.datetime(2023, 2, 22, 0, 0),
                "vendor": "Mozilla",
            },
        ),
        (
            "112.0b1",
            1,
            {
                "build_id": "20230222000000",
                "display_name": "Firefox Nightly",
                "name": "Firefox",
                "pkg_version": "112.0b1-1",
                "remoting_name": "firefox-nightly-try",
                "timestamp": datetime.datetime(2023, 2, 22, 0, 0),
                "vendor": "Mozilla",
            },
        ),
        (
            "112.0",
            2,
            {
                "build_id": "20230222000000",
                "display_name": "Firefox Nightly",
                "name": "Firefox",
                "pkg_version": "112.0-2",
                "remoting_name": "firefox-nightly-try",
                "timestamp": datetime.datetime(2023, 2, 22, 0, 0),
                "vendor": "Mozilla",
            },
        ),
    ),
)
def test_load_application_ini_data(version, build_number, expected):
    with tempfile.TemporaryDirectory() as d:
        tar_path = os.path.join(d, "input.tar")
        with tarfile.open(tar_path, "w") as tar:
            application_ini_path = os.path.join(d, "application.ini")
            with open(application_ini_path, "w") as application_ini_file:
                application_ini_file.write(_APPLICATION_INI_CONTENT)
            tar.add(application_ini_path)
        application_ini_data = utils.load_application_ini_data(
            tar_path, version, build_number
        )
        assert application_ini_data == expected


_MINIMAL_MANIFEST_JSON = """{
  "langpack_id": "%(lang)s",
  "manifest_version": 2,
  "browser_specific_settings": {},
  "name": "Language: %(lang)s",
  "description": "Firefox Language Pack for %(lang)s",
  "version": "136.0.20250326.231000",
  "languages": {},
  "sources": {},
  "author": "mozilla.org"
}"""


def test_get_manifest_from_langpack():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "dummy.langpack.xpi")

        with zipfile.ZipFile(path, "w") as zip_file:
            zip_file.writestr("manifest.json", "")
        assert utils.get_manifest_from_langpack(path, d) is None

        with zipfile.ZipFile(path, "w") as zip_file:
            zip_file.writestr(
                "manifest.json", _MINIMAL_MANIFEST_JSON % {"lang": "dummy"}
            )
        assert utils.get_manifest_from_langpack(path, d) == json.loads(
            _MINIMAL_MANIFEST_JSON % {"lang": "dummy"}
        )


@pytest.mark.parametrize(
    "languages, expected",
    (
        ([], {}),
        (
            ["ach", "fr"],
            {
                "ach": "Firefox Language Pack for ach",
                "fr": "Firefox Language Pack for fr",
            },
        ),
    ),
)
def test_prepare_langpack_files(monkeypatch, languages, expected):
    def _mock_copy(source, destination):
        pass

    monkeypatch.setattr(utils.shutil, "copy", _mock_copy)

    with tempfile.TemporaryDirectory() as xpi_dir, tempfile.TemporaryDirectory() as output_dir:
        for language in languages:
            path = os.path.join(xpi_dir, f"{language}.langpack.xpi")
            with zipfile.ZipFile(path, "w") as zip_file:
                zip_file.writestr(
                    "manifest.json", _MINIMAL_MANIFEST_JSON % {"lang": language}
                )

        assert utils.prepare_langpack_files(output_dir, xpi_dir) == expected


if __name__ == "__main__":
    mozunit.main()
