# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os
import sys
from pathlib import PurePosixPath, PureWindowsPath
from types import SimpleNamespace

import pytest
from mozunit import main

from mozbuild import path_performance


def _fake_path_class(base, home):
    class FakePath(base):
        @classmethod
        def home(cls):
            return cls(home)

        def resolve(self):
            return self

    return FakePath


WINDOWS = _fake_path_class(PureWindowsPath, "C:\\Users\\kit")
MACOS = _fake_path_class(PurePosixPath, "/Users/kit")
LINUX = _fake_path_class(PurePosixPath, "/home/kit")

PLATFORMS = {
    "win32": (
        WINDOWS,
        [
            "D:\\mozilla-source\\firefox",
            "C:\\work\\mozilla-source\\gecko",
            "C:\\work\\mozsrc\\gecko",
            "C:\\Users\\kit\\firefox",
            "C:\\Users\\kit\\firefox-main",
            "D:\\firefox2",
            "C:\\Users\\kit\\.gradle\\gecko",
            "C:\\Users\\kit\\.mozbuild\\gecko",
            "C:\\mozilla-build\\gecko",
        ],
        [
            "D:\\work\\firefox",
            "C:\\Users\\kit\\work\\firefox",
            "C:\\Users\\kit\\src\\firefox",
            "D:\\src",
        ],
    ),
    "darwin": (
        MACOS,
        [
            "/Users/kit/firefox",
            "/Users/kit/firefox-main",
            "/firefox",
            "/mozilla-source/firefox",
            "/opt/mozsrc/gecko",
            "/Users/kit/.gradle/gecko",
            "/Users/kit/.mozbuild/gecko",
        ],
        [
            "/Users/kit/work/firefox",
            "/Users/kit/src/firefox",
            "/Users/kit/mozilla-build/gecko",
            "/opt/firefox",
            "/opt/gecko",
        ],
    ),
    "linux": (
        LINUX,
        [
            "/home/kit/firefox",
            "/home/kit/firefoxes",
            "/firefox",
            "/mozilla-source/gecko",
            "/opt/mozsrc/gecko",
            "/home/kit/.gradle/gecko",
            "/home/kit/.mozbuild/gecko",
        ],
        [
            "/home/kit/work/firefox",
            "/home/kit/mozilla-build/gecko",
            "/home/kit/src/gecko",
            "/home/kit/work/gecko",
            "/opt/gecko",
        ],
    ),
}


def _cases():
    for platform, (path_class, excluded, not_excluded) in PLATFORMS.items():
        for topsrcdir in excluded:
            yield pytest.param(
                platform, path_class, topsrcdir, False, id=f"{platform}:{topsrcdir}"
            )
        for topsrcdir in not_excluded:
            yield pytest.param(
                platform, path_class, topsrcdir, True, id=f"{platform}:{topsrcdir}"
            )


@pytest.fixture
def check(monkeypatch):
    def run(platform, path_class, topsrcdir):
        monkeypatch.setattr(sys, "platform", platform)
        monkeypatch.setattr(path_performance, "Path", path_class)
        monkeypatch.setattr(
            path_performance, "time", SimpleNamespace(sleep=lambda _: None)
        )
        monkeypatch.setattr(
            path_performance.check_path_performance,
            "already_checked",
            False,
            raising=False,
        )
        monkeypatch.setattr("mozbuild.telemetry.get_crowdstrike_running", lambda: True)
        monkeypatch.setenv("MACH_MAIN_PID", str(os.getpid()))
        monkeypatch.delenv("MOZ_SKIP_PATH_PERFORMANCE_CHECK", raising=False)
        monkeypatch.delenv("MOZ_AUTOMATION", raising=False)

        settings = SimpleNamespace(mach_telemetry=SimpleNamespace(is_employee=True))
        path_performance.check_path_performance(topsrcdir, settings)

    return run


@pytest.mark.parametrize("platform,path_class,topsrcdir,warns", list(_cases()))
def test_warns_only_outside_excluded_dirs(
    check, caplog, platform, path_class, topsrcdir, warns
):
    with caplog.at_level(logging.WARNING, logger="mach"):
        check(platform, path_class, topsrcdir)

    warned = any("Move your checkout" in record.msg for record in caplog.records)
    assert warned is warns


@pytest.mark.parametrize(
    "platform,path_class,topsrcdir,expected",
    [
        (
            "win32",
            WINDOWS,
            "D:\\work\\gecko",
            [
                "C:\\Users\\kit\\firefox*",
                "C:\\Users\\kit\\mozilla-source",
                "C:\\Users\\kit\\mozsrc",
                "D:\\firefox*",
                "D:\\mozilla-source",
                "D:\\mozsrc",
            ],
        ),
        (
            "darwin",
            MACOS,
            "/opt/gecko",
            ["/Users/kit/firefox*", "/Users/kit/mozilla-source", "/Users/kit/mozsrc"],
        ),
        (
            "linux",
            LINUX,
            "/opt/gecko",
            ["/home/kit/firefox*", "/home/kit/mozilla-source", "/home/kit/mozsrc"],
        ),
    ],
)
def test_suggested_dirs(monkeypatch, platform, path_class, topsrcdir, expected):
    monkeypatch.setattr(sys, "platform", platform)
    monkeypatch.setattr(path_performance, "Path", path_class)

    assert path_performance._suggested_dirs_for(path_class(topsrcdir)) == [
        path_class(p) for p in expected
    ]


if __name__ == "__main__":
    main()
