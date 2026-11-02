# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import logging
import os
import sys
from pathlib import Path, PurePosixPath, PureWindowsPath
from types import SimpleNamespace

import pytest
from mozunit import main

from mozbuild import path_performance
from mozbuild.base import MachCommandBase, MozbuildObject
from mozbuild.mozconfig import MozconfigLoadException


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
    def run(
        platform,
        path_class,
        topsrcdir,
        state_dir=None,
        mozillabuild=None,
        topobjdir=None,
    ):
        def resolve_mozconfig_topobjdir(self):
            if isinstance(topobjdir, Exception):
                raise topobjdir
            return topobjdir

        monkeypatch.setattr(
            MozbuildObject, "resolve_mozconfig_topobjdir", resolve_mozconfig_topobjdir
        )
        monkeypatch.setattr(sys, "platform", platform)
        monkeypatch.setattr(path_performance, "Path", path_class)
        monkeypatch.setattr(
            path_performance,
            "get_state_dir",
            lambda: state_dir or str(path_class.home() / ".mozbuild"),
        )
        monkeypatch.setenv("MOZILLABUILD", mozillabuild or "C:\\mozilla-build")
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


def _moved(caplog):
    return [
        str(record.params["path"])
        for record in caplog.records
        if "path" in getattr(record, "params", {})
    ]


@pytest.mark.parametrize("platform,path_class,topsrcdir,warns", list(_cases()))
def test_warns_only_outside_excluded_dirs(
    check, caplog, platform, path_class, topsrcdir, warns
):
    with caplog.at_level(logging.WARNING, logger="mach"):
        check(platform, path_class, topsrcdir)

    assert _moved(caplog) == ([topsrcdir] if warns else [])


@pytest.mark.parametrize(
    "platform,path_class,topsrcdir,state_dir,mozillabuild,moved",
    [
        (
            "win32",
            WINDOWS,
            "D:\\mozilla-source\\firefox",
            "D:\\state",
            "C:\\mozilla-build",
            ["D:\\state"],
        ),
        (
            "win32",
            WINDOWS,
            "D:\\mozilla-source\\firefox",
            "C:\\Users\\kit\\.mozbuild",
            "D:\\tools\\mozbuild",
            ["D:\\tools\\mozbuild"],
        ),
        (
            "win32",
            WINDOWS,
            "D:\\work\\firefox",
            "D:\\state",
            "D:\\tools\\mozbuild",
            ["D:\\work\\firefox", "D:\\state", "D:\\tools\\mozbuild"],
        ),
        (
            "linux",
            LINUX,
            "/home/kit/firefox",
            "/opt/state",
            "/opt/tools",
            ["/opt/state"],
        ),
    ],
)
def test_warns_for_each_misplaced_location(
    check, caplog, platform, path_class, topsrcdir, state_dir, mozillabuild, moved
):
    with caplog.at_level(logging.WARNING, logger="mach"):
        check(platform, path_class, topsrcdir, state_dir, mozillabuild)

    assert _moved(caplog) == moved


@pytest.mark.parametrize(
    "platform,path_class,topsrcdir,topobjdir,moved",
    [
        ("win32", WINDOWS, "D:\\mozilla-source\\firefox", "E:\\obj", ["E:\\obj"]),
        (
            "win32",
            WINDOWS,
            "D:\\mozilla-source\\firefox",
            "D:\\mozilla-source\\obj",
            [],
        ),
        ("linux", LINUX, "/home/kit/firefox", "/home/kit/obj", ["/home/kit/obj"]),
        ("linux", LINUX, "/home/kit/firefox", "/home/kit/firefox/obj-ff", []),
        (
            "linux",
            LINUX,
            "/home/kit/work/firefox",
            "/home/kit/work/firefox/obj-ff",
            ["/home/kit/work/firefox"],
        ),
        ("linux", LINUX, "/home/kit/firefox", None, []),
        (
            "linux",
            LINUX,
            "/home/kit/firefox",
            MozconfigLoadException("/home/kit/firefox/mozconfig", "bad"),
            [],
        ),
    ],
)
def test_warns_for_misplaced_topobjdir(
    check, caplog, platform, path_class, topsrcdir, topobjdir, moved
):
    with caplog.at_level(logging.WARNING, logger="mach"):
        check(platform, path_class, topsrcdir, topobjdir=topobjdir)

    assert _moved(caplog) == moved


def test_shares_mozconfig_cache_with_command(monkeypatch):
    calls = []

    def get_base_mozconfig_info(*args):
        calls.append(args)
        return {"mozconfig": {"topobjdir": None}}

    monkeypatch.setattr(
        MozbuildObject, "get_base_mozconfig_info", staticmethod(get_base_mozconfig_info)
    )
    topsrcdir = Path(__file__).resolve().parents[4]

    path_performance._explicit_topobjdir(topsrcdir)
    context = SimpleNamespace(
        topdir=str(topsrcdir), cwd=str(topsrcdir), settings=None, log_manager=None
    )
    MachCommandBase(context, no_auto_log=True)

    assert len(calls) == 2
    assert calls[0] == calls[1]


@pytest.mark.parametrize(
    "platform,path_class,path,category,suggested",
    [
        (
            "win32",
            WINDOWS,
            "D:\\work\\gecko",
            path_performance.Category.TOPSRCDIR,
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
            "win32",
            WINDOWS,
            "D:\\work\\gecko",
            path_performance.Category.TOPOBJDIR,
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
            "win32",
            WINDOWS,
            "D:\\work\\gecko",
            path_performance.Category.STATE_DIR,
            ["C:\\Users\\kit\\.mozbuild", "D:\\.mozbuild"],
        ),
        (
            "win32",
            WINDOWS,
            "D:\\work\\gecko",
            path_performance.Category.MOZILLA_BUILD,
            ["C:\\Users\\kit\\mozilla-build", "D:\\mozilla-build"],
        ),
        (
            "darwin",
            MACOS,
            "/opt/gecko",
            path_performance.Category.TOPSRCDIR,
            ["/Users/kit/firefox*", "/Users/kit/mozilla-source", "/Users/kit/mozsrc"],
        ),
        (
            "linux",
            LINUX,
            "/opt/gecko",
            path_performance.Category.TOPSRCDIR,
            ["/home/kit/firefox*", "/home/kit/mozilla-source", "/home/kit/mozsrc"],
        ),
        (
            "linux",
            LINUX,
            "/opt/state",
            path_performance.Category.STATE_DIR,
            ["/home/kit/.mozbuild"],
        ),
    ],
)
def test_suggested_dirs(monkeypatch, platform, path_class, path, category, suggested):
    monkeypatch.setattr(sys, "platform", platform)
    monkeypatch.setattr(path_performance, "Path", path_class)

    location = path_performance.Location(category, "", path_class(path))
    assert path_performance._suggested_dirs_for(location) == [
        path_class(p) for p in suggested
    ]


if __name__ == "__main__":
    main()
