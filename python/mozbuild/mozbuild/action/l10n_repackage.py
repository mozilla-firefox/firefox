# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Per-locale repackage action.

Re-packs the en-US dist as a single-locale package. Orchestrates:

1. Unpack the en-US package into <l10n_stage>/<pkg_dir>/
2. l10n-repack: swap localized parts in-place
3. macOS lproj rename: en.lproj to <lproj_root>.lproj for non-en
4. (opt) Windows helper.exe rebuild and copy into the staged tree's uninstall/
5. Package the staged tree via the package action
6. Move output to <output>

Invoked in make via $(call py_action,l10n_repackage,...).
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path

import buildconfig
from mozpack.packager import l10n as packager_l10n

from mozbuild.action import package as action_package

# Glob patterns excluded from resource and chrome repacking.
_NON_CHROME = frozenset((
    "dictionaries",
    "defaultagent_localized.ini",
    "defaults/profile",
    "defaults/pref*/*-l10n.js",
    "default.locale",
    "updater.ini",
    "extensions/langpack-*@*",
    "distribution/extensions/langpack-*@*",
    "**/multilocale.txt",
))


def l10n_repackage(
    locale: str,
    mach: Path,
    l10n_stage: Path,
    unpack_distdir: Path,
    en_us_package: Path,
    stagedist: Path,
    xpi_stage: Path,
    pkg_dir: str,
    pkg_format: str,
    pkg_filename: str,
    tar: Path,
    output: Path,
    moz_widget_toolkit: str,
    os_arch: str,
    installer_dir: Path | None,
    real_locale_mergedir: Path | None,
    extra_l10n: dict[str, str],
    non_resources: list[str],
    minify: bool,
    package_extra_args: list[str],
) -> int:
    is_cocoa = moz_widget_toolkit == "cocoa"
    is_winnt = os_arch == "WINNT"

    if (
        uses_local_package(buildconfig.substs)
        and en_us_package.resolve() == output.resolve()
    ):
        print(
            f"{output} is both the en-US package to unpack and the output for "
            f"{locale}, so the repack would overwrite its own input. Set "
            "MOZ_ARTIFACT_FILE to a copy of the en-US package, or run "
            "`./mach repackage-single-locales`, which does that.",
            file=sys.stderr,
        )
        return 1

    if result := _unpack(mach, l10n_stage, unpack_distdir, en_us_package):
        return result

    _do_l10n_repack(stagedist, xpi_stage, extra_l10n, non_resources, minify)

    renamed_lproj = _maybe_rename_lproj(stagedist, locale, is_cocoa)

    if is_winnt:
        if installer_dir is None:
            raise ValueError("--installer-dir is required on WINNT")
        if result := _build_uninstaller(
            installer_dir, locale, real_locale_mergedir, stagedist
        ):
            return result

    suffix = action_package.FORMAT_SUFFIX.get(pkg_format)
    if suffix is None:
        raise ValueError(f"Unknown package format: {pkg_format}")
    if not pkg_filename.endswith(suffix):
        raise ValueError(
            f"Package filename {pkg_filename} does not match format "
            f"{pkg_format} (expected suffix {suffix!r})"
        )
    basename = pkg_filename[: len(pkg_filename) - len(suffix)]
    result = action_package.main(
        [
            "--format",
            pkg_format,
            "--cwd",
            str(l10n_stage),
            "--pkg-dir",
            pkg_dir,
            "--basename",
            basename,
            "--tar",
            str(tar),
        ]
        + list(package_extra_args)
    )

    if result:
        return result

    if renamed_lproj is not None:
        renamed_lproj.rename(stagedist / "en.lproj")

    src = l10n_stage / pkg_filename
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(src, output)
    asc = src.with_name(src.name + ".asc")
    if asc.exists():
        shutil.move(asc, output.with_name(output.name + ".asc"))
    return 0


def uses_local_package(
    substs: Mapping[str, object], environ: Mapping[str, str] = os.environ
) -> bool:
    if not substs.get("COMPILE_ENVIRONMENT"):
        return False
    if any(
        name in environ
        for name in ("MOZ_ARTIFACT_FILE", "MOZ_ARTIFACT_URL", "MOZ_ARTIFACT_REVISION")
    ):
        return False
    return not any(var.startswith("MOZ_ARTIFACT_TASK") for var in environ)


def _unpack(
    mach: Path,
    l10n_stage: Path,
    distdir: Path,
    en_us_package: Path,
) -> int:
    shutil.rmtree(l10n_stage, ignore_errors=True)
    cmd = [sys.executable, mach, "--log-no-times", "artifact", "install"]
    local_package = uses_local_package(buildconfig.substs)
    if local_package:
        if not en_us_package.is_file():
            print(
                f"{en_us_package} does not exist. Run `./mach package` to build "
                "the en-US package before repackaging a locale.",
                file=sys.stderr,
            )
            return 1
        cmd.append(str(en_us_package))
    if local_package or "MOZ_ARTIFACT_FILE" in os.environ:
        # The processed archive is cached under the package's file name, and a
        # rebuilt local package keeps its name, so the cache would hide it.
        cmd.append("--skip-cache")
    cmd += ["--unfiltered-project-package", "--distdir", distdir, "--verbose"]
    result = subprocess.run(cmd, check=False)
    return result.returncode


def _do_l10n_repack(
    stagedist: Path,
    xpi_stage: Path,
    extra_l10n: dict[str, str],
    non_resources: list[str],
    minify: bool,
) -> None:
    # USE_ELF_HACK and PKG_STRIP are global build options. Force them
    # off so the in-process repack doesn't touch binaries.
    buildconfig.substs["USE_ELF_HACK"] = False
    buildconfig.substs["PKG_STRIP"] = False
    packager_l10n.repack(
        str(stagedist),
        str(xpi_stage),
        extra_l10n=extra_l10n,
        non_resources=non_resources,
        non_chrome=_NON_CHROME,
        minify=minify,
    )


def _lproj_root(locale: str, is_cocoa: bool) -> str:
    # macOS resolves an app's localized system resources by matching the
    # user's language to an <lang>.lproj dir. Since Yosemite a bare
    # "zh.lproj" reads as Simplified Chinese, so zh-TW uses "zh_TW"
    # (Apple's language_region convention) to keep Traditional strings.
    # See bug 1089363.
    if is_cocoa and locale == "zh-TW":
        return locale.replace("-", "_")
    return locale.split("-", 1)[0]


def _maybe_rename_lproj(
    stagedist: Path,
    locale: str,
    is_cocoa: bool,
) -> Path | None:
    # The en-US package ships its macOS system resources in en.lproj.
    # macOS resolves them by matching the directory name to the user's
    # language, so for a single non-en locale we rename the dir to that
    # locale's lproj name. en/en-US already have the right name, and
    # only macOS bundles have .lproj dirs at all.
    lproj = _lproj_root(locale, is_cocoa)
    if not (is_cocoa and lproj != "en"):
        return None
    en = stagedist / "en.lproj"
    if en.exists():
        renamed = stagedist / (lproj + ".lproj")
        en.rename(renamed)
        return renamed
    return None


def _build_uninstaller(
    installer_dir: Path,
    locale: str,
    real_locale_mergedir: Path,
    stagedist: Path,
) -> int:
    from mozbuild.action import nsis_build, nsis_stage

    substs = buildconfig.substs
    config_dir = installer_dir / "l10ngen"
    if result := nsis_stage.stage_repack(
        spec_path=installer_dir / nsis_stage.SPEC_FILENAME,
        config_dir=config_dir,
        ab_cd=locale,
        real_locale_mergedir=real_locale_mergedir,
    ):
        return result
    return nsis_build.nsis_build(
        config_dir=config_dir,
        nsi="uninstaller.nsi",
        makensis=substs["MAKENSISU"],
        makensis_flags=shlex.split(substs.get("MAKENSISU_FLAGS", "")),
        output=str(stagedist / "uninstall" / "helper.exe"),
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Re-pack the en-US dist as a single-locale package."
    )
    parser.add_argument("--locale", required=True, help="The ab_cd locale code")
    parser.add_argument(
        "--mach",
        required=True,
        type=Path,
        help="Path to the topsrcdir mach executable",
    )
    parser.add_argument(
        "--l10n-stage",
        required=True,
        type=Path,
        help="L10n staging root, wiped and re-populated each run "
        "(typically <topobjdir>/dist/l10n-stage/<ab_cd>/)",
    )
    parser.add_argument(
        "--unpack-distdir",
        required=True,
        type=Path,
        help="Where to unpack the en-US package "
        "(typically <l10n-stage>/<MOZ_PKG_DIR>/)",
    )
    parser.add_argument(
        "--en-us-package",
        required=True,
        type=Path,
        help="The en-US package built in this objdir, unpacked in place of a "
        "downloaded one when the build has a compile environment",
    )
    parser.add_argument(
        "--stagedist",
        required=True,
        type=Path,
        help="The staged dist subtree the repack/lproj/helper steps "
        "operate on (matches the legacy STAGEDIST variable)",
    )
    parser.add_argument(
        "--xpi-stage",
        required=True,
        type=Path,
        help="Per-locale staging dir from l10n_stage (typically "
        "<topobjdir>/dist/xpi-stage/locale-<ab_cd>/)",
    )
    parser.add_argument("--pkg-dir", required=True, help="MOZ_PKG_DIR")
    parser.add_argument("--pkg-format", required=True, help="MOZ_PKG_FORMAT")
    parser.add_argument(
        "--pkg-filename",
        required=True,
        help="Output filename written by the package action (relative to <l10n-stage>)",
    )
    parser.add_argument(
        "--tar", required=True, type=Path, help="Path to the tar binary"
    )
    parser.add_argument("--output", required=True, type=Path, help="Final package path")
    parser.add_argument("--moz-widget-toolkit", default="", help="MOZ_WIDGET_TOOLKIT")
    parser.add_argument("--os-arch", default="", help="OS_ARCH")
    parser.add_argument(
        "--installer-dir",
        type=Path,
        default=None,
        help="WINNT-only: the installer directory in the object directory",
    )
    parser.add_argument(
        "--real-locale-mergedir",
        type=Path,
        default=None,
        help="WINNT-only: the merged locale directory the uninstaller is staged from",
    )
    parser.add_argument(
        "--extra-l10n",
        action="append",
        default=[],
        help="BASE=PATH entries from MOZ_PKG_EXTRAL10N. Repeatable.",
    )
    parser.add_argument(
        "--non-resource",
        action="append",
        default=[],
        help="NON_OMNIJAR_FILES entries (used when MOZ_PACKAGER_FORMAT is "
        "omni). Repeatable.",
    )
    parser.add_argument(
        "--minify",
        action="store_true",
        help="Set when MOZ_PACKAGER_MINIFY is true",
    )
    # Any unrecognized argv tokens are forwarded verbatim to the package
    # action. This is how MOZ_PACKAGE_EXTRA_ARGS (--strong-compression,
    # --dsstore <path>, --app-name 'Firefox Developer Edition.app', etc.)
    # gets through with shell quoting intact. Repeatable --flag=value
    # forms can't preserve spaces inside values, but a positional list
    # the shell already split for us can.
    args, package_extra_args = parser.parse_known_args(argv)

    extra_l10n = dict(arg.split("=", 1) for arg in args.extra_l10n if "=" in arg)

    return l10n_repackage(
        locale=args.locale,
        mach=args.mach,
        l10n_stage=args.l10n_stage,
        unpack_distdir=args.unpack_distdir,
        en_us_package=args.en_us_package,
        stagedist=args.stagedist,
        xpi_stage=args.xpi_stage,
        pkg_dir=args.pkg_dir,
        pkg_format=args.pkg_format,
        pkg_filename=args.pkg_filename,
        tar=args.tar,
        output=args.output,
        moz_widget_toolkit=args.moz_widget_toolkit,
        os_arch=args.os_arch,
        installer_dir=args.installer_dir,
        real_locale_mergedir=args.real_locale_mergedir,
        extra_l10n=extra_l10n,
        non_resources=args.non_resource,
        minify=args.minify,
        package_extra_args=package_extra_args,
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
