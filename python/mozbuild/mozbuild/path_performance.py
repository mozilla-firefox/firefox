# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import annotations

import fnmatch
import logging
import os
import sys
import time
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

from mach.util import get_state_dir

WARNING_DELAY_SECONDS = 20


# Where a directory of a given name has to sit to actually be excluded.
class Scope(Enum):
    ANY_DIRECTORY = auto()
    USER_HOME_OR_DRIVE_ROOT = auto()


class Category(Enum):
    TOPSRCDIR = auto()
    TOPOBJDIR = auto()
    STATE_DIR = auto()
    MOZILLA_BUILD = auto()


@dataclass(frozen=True)
class ExcludedDir:
    pattern: str
    scope: Scope
    suggested_for: tuple[Category, ...] = ()


@dataclass(frozen=True)
class Location:
    category: Category
    label: str
    path: Path


def _excluded_dirs(platform):
    dirs = (
        ExcludedDir(
            "firefox*",
            Scope.USER_HOME_OR_DRIVE_ROOT,
            (Category.TOPSRCDIR, Category.TOPOBJDIR),
        ),
        ExcludedDir(
            "mozilla-source",
            Scope.ANY_DIRECTORY,
            (Category.TOPSRCDIR, Category.TOPOBJDIR),
        ),
        ExcludedDir(
            "mozsrc", Scope.ANY_DIRECTORY, (Category.TOPSRCDIR, Category.TOPOBJDIR)
        ),
        ExcludedDir(".gradle", Scope.ANY_DIRECTORY),
        ExcludedDir(".mozbuild", Scope.ANY_DIRECTORY, (Category.STATE_DIR,)),
    )
    if platform == "win32":
        dirs += (
            ExcludedDir(
                "mozilla-build", Scope.ANY_DIRECTORY, (Category.MOZILLA_BUILD,)
            ),
        )
    return dirs


def _suggested_dirs_for(location):
    roots = [Path.home().resolve()]
    if sys.platform == "win32":
        roots.append(Path(location.path.anchor))
    excluded_dirs = _excluded_dirs(sys.platform)

    return [
        root / excluded.pattern
        for root in roots
        for excluded in excluded_dirs
        if location.category in excluded.suggested_for
    ]


def _is_in_excluded_dir(path):
    home = Path.home().resolve()
    excluded_dirs = _excluded_dirs(sys.platform)

    for ancestor in (path, *path.parents):
        for excluded in excluded_dirs:
            if not fnmatch.fnmatch(ancestor.name, excluded.pattern):
                continue
            if excluded.scope == Scope.ANY_DIRECTORY or ancestor.parent in (
                home,
                Path(ancestor.anchor),
            ):
                return True

    return False


def _explicit_topobjdir(topsrcdir):
    from mozboot.mozconfig import MozconfigFindException

    from mozbuild.base import MozbuildObject
    from mozbuild.mozconfig import MozconfigLoadException

    try:
        topobjdir = MozbuildObject(
            str(topsrcdir), None, None
        ).resolve_mozconfig_topobjdir()
    except (MozconfigFindException, MozconfigLoadException):
        return None

    if not topobjdir:
        return None
    return Path(topobjdir).resolve()


def check_path_performance(topsrcdir, settings):
    if getattr(check_path_performance, "already_checked", False):
        return
    check_path_performance.already_checked = True

    if os.environ.get("MOZ_SKIP_PATH_PERFORMANCE_CHECK") or os.environ.get(
        "MOZ_AUTOMATION"
    ):
        return

    if os.environ.get("MACH_MAIN_PID") != str(os.getpid()):
        return

    if not settings.mach_telemetry.is_employee:
        return

    from mozbuild.telemetry import get_crowdstrike_running

    if not get_crowdstrike_running():
        return

    topsrcdir = Path(topsrcdir).resolve()
    locations = [
        Location(Category.TOPSRCDIR, "your checkout", topsrcdir),
        Location(
            Category.STATE_DIR,
            "your state directory",
            Path(get_state_dir()).resolve(),
        ),
    ]
    topobjdir = _explicit_topobjdir(topsrcdir)
    if topobjdir and topsrcdir not in topobjdir.parents:
        locations.append(
            Location(Category.TOPOBJDIR, "your object directory", topobjdir)
        )
    if sys.platform == "win32":
        locations.append(
            Location(
                Category.MOZILLA_BUILD,
                "MozillaBuild",
                Path(os.environ["MOZILLABUILD"]).resolve(),
            )
        )

    misplaced = [loc for loc in locations if not _is_in_excluded_dir(loc.path)]
    if not misplaced:
        return

    mach_logger = logging.getLogger("mach")

    def log(msg, **params):
        mach_logger.log(
            logging.WARNING,
            msg,
            extra={"action": "path_performance", "params": params},
        )

    log(
        "A security tool is running and is known to cause performance "
        "regressions in Mach unless mitigated. "
        "To fix this, do the following:"
    )
    for location in misplaced:
        valid_paths = "\n               ".join(
            str(suggested) for suggested in _suggested_dirs_for(location)
        )
        log(
            "  Move {label} ({path}) under one of:\n               {valid_paths}",
            label=location.label,
            path=location.path,
            valid_paths=valid_paths,
        )
    log(
        "This check pauses for {delay} seconds. "
        "Set MOZ_SKIP_PATH_PERFORMANCE_CHECK=1 to bypass it.",
        delay=WARNING_DELAY_SECONDS,
    )

    time.sleep(WARNING_DELAY_SECONDS)
