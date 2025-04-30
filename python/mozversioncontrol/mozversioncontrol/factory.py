# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this,
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import re
import subprocess
import sys
from pathlib import Path
from typing import (
    Optional,
    Union,
)

from packaging.version import Version

from mozversioncontrol.errors import (
    InvalidRepoPath,
    MissingConfigureInfo,
    MissingVCSInfo,
    MissingVCSTool,
)
from mozversioncontrol.repo.git import GitRepository
from mozversioncontrol.repo.jj import JujutsuRepository
from mozversioncontrol.repo.mercurial import HgRepository
from mozversioncontrol.repo.source import SrcRepository

MINIMUM_SUPPORTED_JJ_VERSION = Version("0.28")
USING_JJ_WARNING = """\
Using JujutsuRepository because a ".jj/" directory was detected!

Warning: jj support is currently experimental, and may be disabled by setting the
environment variable MOZ_AVOID_JJ_VCS=1. (This warning may be suppressed by
setting MOZ_AVOID_JJ_VCS=0.)"""


class UnsupportedJujutsuVersionError(Exception):
    """Raised when the detected jj version is below the required minimum."""

    pass


def get_repository_object(
    path: Optional[Union[str, Path]], hg="hg", git="git", jj="jj", src="src"
):
    """Get a repository object for the repository at `path`.
    If `path` is not a known VCS repository, raise an exception.
    """
    # If we provide a path to hg that does not match the on-disk casing (e.g.,
    # because `path` was normcased), then the hg fsmonitor extension will call
    # watchman with that path and watchman will spew errors.
    path = Path(path).resolve()
    if (path / ".hg").is_dir():
        return HgRepository(path, hg=hg)
    if (path / ".jj").is_dir() and jj is not None:
        avoid = os.getenv("MOZ_AVOID_JJ_VCS")
        try_using_jj = avoid in (None, "0", "")
        if try_using_jj:
            try:
                result = subprocess.run(
                    ["jj", "--version"],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                raw_jj_version = result.stdout.strip()
                match = re.search(r"\b(\d+\.\d+\.\d+)\b", raw_jj_version)

                if not match:
                    raise ValueError(
                        f"Could not parse jj version from output: {raw_jj_version}"
                    )

                current_jj_version = Version(match.group(1))

                if current_jj_version < MINIMUM_SUPPORTED_JJ_VERSION:
                    raise UnsupportedJujutsuVersionError(
                        f"Detected jj version {current_jj_version}, "
                        f"but version {MINIMUM_SUPPORTED_JJ_VERSION} or newer is required.\n"
                        f'Full "jj --version" output was: "{raw_jj_version}"'
                    )

                avoid_is_unset = avoid not in ("0", "")
                if avoid_is_unset and not hasattr(get_repository_object, "_warned"):
                    # Warn (once) if MOZ_AVOID_JJ_VCS is unset. If it is set to 0, then use
                    # jj without warning. If it is set to anything else, do not use jj (so
                    # eg fall back to git if .git exists.)
                    get_repository_object._warned = True
                    print(USING_JJ_WARNING, file=sys.stderr)

                return JujutsuRepository(path, jj=jj, git=git)

            except OSError:
                print(".jj/ directory exists but jj binary not usable", file=sys.stderr)
    if (path / ".git").exists():
        return GitRepository(path, git=git)
    if (path / "config" / "milestone.txt").exists():
        return SrcRepository(path, src=src)
    raise InvalidRepoPath(f"Unknown VCS, or not a source checkout: {path}")


def get_repository_from_build_config(config):
    """Obtain a repository from the build configuration.

    Accepts an object that has a ``topsrcdir`` and ``subst`` attribute.
    """
    flavor = config.substs.get("VCS_CHECKOUT_TYPE")

    # If in build mode, only use what configure found. That way we ensure
    # that everything in the build system can be controlled via configure.
    if not flavor:
        raise MissingConfigureInfo(
            "could not find VCS_CHECKOUT_TYPE "
            "in build config; check configure "
            "output and verify it could find a "
            "VCS binary"
        )

    if flavor == "hg":
        return HgRepository(Path(config.topsrcdir), hg=config.substs["HG"])
    elif flavor == "jj":
        return JujutsuRepository(
            Path(config.topsrcdir), jj=config.substs["JJ"], git=config.substs["GIT"]
        )
    elif flavor == "git":
        return GitRepository(Path(config.topsrcdir), git=config.substs["GIT"])
    elif flavor == "src":
        return SrcRepository(Path(config.topsrcdir), src=config.substs["SRC"])
    else:
        raise MissingVCSInfo(f"unknown VCS_CHECKOUT_TYPE value: {flavor}")


def get_repository_from_env():
    """Obtain a repository object by looking at the environment.

    If inside a build environment (denoted by presence of a ``buildconfig``
    module), VCS info is obtained from it, as found via configure. This allows
    us to respect what was passed into configure. Otherwise, we fall back to
    scanning the filesystem.
    """
    try:
        import buildconfig

        return get_repository_from_build_config(buildconfig)
    except (ImportError, MissingVCSTool):
        pass

    paths_to_check = [Path.cwd(), *Path.cwd().parents]

    for path in paths_to_check:
        try:
            return get_repository_object(path)
        except InvalidRepoPath:
            continue

    raise MissingVCSInfo(
        f"Could not find Mercurial / Git / JJ checkout for {Path.cwd()}"
    )
