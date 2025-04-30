# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import logging
import os
import tempfile
import zipfile
from contextlib import nullcontext as does_not_raise
from io import StringIO
from unittest.mock import Mock

import mozunit
import pytest

from mozbuild.repackaging import deb, desktop_file

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


def test_generate_deb_desktop_entry_file_text(monkeypatch):
    def responsive(url):
        assert "zh-TW" in url
        return Mock(
            **{
                "status_code": 200,
                "text": ZH_TW_FTL,
            }
        )

    monkeypatch.setattr(desktop_file.requests, "get", responsive)

    output_stream = StringIO()
    logger = logging.getLogger("mozbuild:test:repackaging")
    logger.setLevel(logging.DEBUG)
    stream_handler = logging.StreamHandler(output_stream)
    logger.addHandler(stream_handler)

    def log(level, action, params, format_str):
        logger.log(
            level,
            format_str.format(**params),
            extra={"action": action, "params": params},
        )

    def fluent_localization(locales, resources, loader):
        def format_value(resource):
            return f"{locales[0]}-{resource}"

        return Mock(**{"format_value": format_value})

    fluent_resource_loader = Mock()

    monkeypatch.setattr(
        desktop_file.json,
        "load",
        lambda f: {"zh-TW": {"platforms": ["linux"], "revision": "default"}},
    )

    build_variables = {
        "PKG_NAME": "firefox-nightly",
        "Icon": "firefox-nightly",
    }
    release_product = "firefox"
    release_type = "nightly"

    desktop_entry_file_text = desktop_file.generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )

    assert desktop_entry_file_text == NIGHTLY_DESKTOP_ENTRY_FILE_TEXT

    build_variables = {
        "PKG_NAME": "firefox-devedition",
        "Icon": "firefox-devedition",
    }
    release_product = "devedition"
    release_type = "beta"

    desktop_entry_file_text = desktop_file.generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )

    assert desktop_entry_file_text == DEVEDITION_DESKTOP_ENTRY_FILE_TEXT

    def outage(url):
        return Mock(**{"status_code": 500})

    monkeypatch.setattr(desktop_file.requests, "get", outage)

    with pytest.raises(desktop_file.RemoteVCSError):
        desktop_entry_file_text = desktop_file.generate_browser_desktop_entry_file_text(
            log,
            build_variables,
            release_product,
            release_type,
            fluent_localization,
            fluent_resource_loader,
        )


@pytest.mark.parametrize(
    "does_path_exits, expectation",
    (
        (True, does_not_raise()),
        (False, pytest.raises(deb.NoDebPackageFound)),
    ),
)
def test_generate_deb_archive(
    monkeypatch,
    does_path_exits,
    expectation,
):
    monkeypatch.setattr(deb, "_get_command", lambda _: ["mock_command"])
    monkeypatch.setattr(deb.subprocess, "check_call", lambda *_, **__: None)

    def mock_exists(path):
        assert path == "/target_dir/firefox_111.0_amd64.deb"
        return does_path_exits

    monkeypatch.setattr(deb.os.path, "exists", mock_exists)

    def mock_move(source_path, destination_path):
        assert source_path == "/target_dir/firefox_111.0_amd64.deb"
        assert destination_path == "/output/target.deb"

    monkeypatch.setattr(deb.shutil, "move", mock_move)

    with expectation:
        deb._generate_deb_archive(
            source_dir="/source_dir",
            target_dir="/target_dir",
            output_file_path="/output/target.deb",
            build_variables={
                "PKG_NAME": "firefox",
                "PKG_VERSION": "111.0",
            },
            arch="x86_64",
        )


@pytest.mark.parametrize(
    "arch, is_chroot_available, expected",
    (
        (
            "all",
            True,
            [
                "chroot",
                "/srv/jessie-amd64",
                "bash",
                "-c",
                "cd /tmp/*/source; dpkg-buildpackage -us -uc -b",
            ],
        ),
        ("all", False, ["dpkg-buildpackage", "-us", "-uc", "-b"]),
        (
            "x86",
            True,
            [
                "chroot",
                "/srv/jessie-i386",
                "bash",
                "-c",
                "cd /tmp/*/source; dpkg-buildpackage -us -uc -b --host-arch=i386",
            ],
        ),
        ("x86", False, ["dpkg-buildpackage", "-us", "-uc", "-b", "--host-arch=i386"]),
        (
            "x86_64",
            True,
            [
                "chroot",
                "/srv/jessie-amd64",
                "bash",
                "-c",
                "cd /tmp/*/source; dpkg-buildpackage -us -uc -b --host-arch=amd64",
            ],
        ),
        (
            "x86_64",
            False,
            ["dpkg-buildpackage", "-us", "-uc", "-b", "--host-arch=amd64"],
        ),
    ),
)
def test_get_command(monkeypatch, arch, is_chroot_available, expected):
    monkeypatch.setattr(deb, "_is_chroot_available", lambda _: is_chroot_available)
    assert deb._get_command(arch) == expected


@pytest.mark.parametrize(
    "arch, does_dir_exist, expected_path, expected_result",
    (
        ("all", False, "/srv/jessie-amd64", False),
        ("all", True, "/srv/jessie-amd64", True),
        ("x86", False, "/srv/jessie-i386", False),
        ("x86_64", False, "/srv/jessie-amd64", False),
        ("x86", True, "/srv/jessie-i386", True),
        ("x86_64", True, "/srv/jessie-amd64", True),
    ),
)
def test_is_chroot_available(
    monkeypatch, arch, does_dir_exist, expected_path, expected_result
):
    def _mock_is_dir(path):
        assert path == expected_path
        return does_dir_exist

    monkeypatch.setattr(deb.os.path, "isdir", _mock_is_dir)
    assert deb._is_chroot_available(arch) == expected_result


@pytest.mark.parametrize(
    "arch, expected",
    (
        ("all", "/srv/jessie-amd64"),
        ("x86", "/srv/jessie-i386"),
        ("x86_64", "/srv/jessie-amd64"),
    ),
)
def test_get_chroot_path(arch, expected):
    assert deb._get_chroot_path(arch) == expected


_MANIFEST_JSON_DATA = {
    "langpack_id": "fr",
    "manifest_version": 2,
    "browser_specific_settings": {
        "gecko": {
            "id": "langpack-fr@devedition.mozilla.org",
            "strict_min_version": "112.0a1",
            "strict_max_version": "112.0a1",
        }
    },
    "name": "Language: Français (French)",
    "description": "Firefox Developer Edition Language Pack for Français (fr) – French",
    "version": "112.0.20230227.181253",
    "languages": {
        "fr": {
            "version": "20230223164410",
            "chrome_resources": {
                "app-marketplace-icons": "browser/chrome/browser/locale/fr/app-marketplace-icons/",
                "branding": "browser/chrome/fr/locale/branding/",
                "browser": "browser/chrome/fr/locale/browser/",
                "browser-region": "browser/chrome/fr/locale/browser-region/",
                "devtools": "browser/chrome/fr/locale/fr/devtools/client/",
                "devtools-shared": "browser/chrome/fr/locale/fr/devtools/shared/",
                "alerts": "chrome/fr/locale/fr/alerts/",
                "autoconfig": "chrome/fr/locale/fr/autoconfig/",
                "global": "chrome/fr/locale/fr/global/",
                "global-platform": {
                    "macosx": "chrome/fr/locale/fr/global-platform/mac/",
                    "linux": "chrome/fr/locale/fr/global-platform/unix/",
                    "android": "chrome/fr/locale/fr/global-platform/unix/",
                    "win": "chrome/fr/locale/fr/global-platform/win/",
                },
                "mozapps": "chrome/fr/locale/fr/mozapps/",
                "necko": "chrome/fr/locale/fr/necko/",
                "passwordmgr": "chrome/fr/locale/fr/passwordmgr/",
                "pdf.js": "chrome/fr/locale/pdfviewer/",
                "pipnss": "chrome/fr/locale/fr/pipnss/",
                "pippki": "chrome/fr/locale/fr/pippki/",
                "places": "chrome/fr/locale/fr/places/",
                "weave": "chrome/fr/locale/fr/services/",
            },
        }
    },
    "sources": {"browser": {"base_path": "browser/"}},
    "author": "mozfr.org (contributors: L’équipe francophone)",
}


def test_extract_langpack_metadata():
    with tempfile.TemporaryDirectory() as d:
        langpack_path = os.path.join(d, "langpack.xpi")
        with zipfile.ZipFile(langpack_path, "w") as zip:
            zip.writestr("manifest.json", json.dumps(_MANIFEST_JSON_DATA))

        assert deb._extract_langpack_metadata(langpack_path) == _MANIFEST_JSON_DATA


if __name__ == "__main__":
    mozunit.main()
