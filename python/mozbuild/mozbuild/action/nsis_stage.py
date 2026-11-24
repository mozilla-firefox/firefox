# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Stage files and localized data for Windows NSIS installers."""

from __future__ import annotations

import argparse
import hashlib
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

import buildconfig
from mozfile import json

from mozbuild.preprocessor import Preprocessor
from mozbuild.util import FileAvoidWrite

SPEC_FILENAME = "nsis-stage.json"


def nsis_stage(
    config_dir: Path,
    installs: list[str],
    defines_in: str,
    defines_out: str,
    preprocessor_args: list[str],
    topsrcdir: Path,
    preprocess_locale_script: str,
    locale_args: list[str],
    ab_cd: str,
    preprocess_locale: bool,
    single_files: list[list[str]],
    convert_utf8: list[list[str]],
) -> int:
    shutil.rmtree(config_dir, ignore_errors=True)
    config_dir.mkdir(parents=True)

    for src in installs:
        shutil.copy2(src, config_dir / Path(src).name)

    if defines_in:
        pp = Preprocessor()
        pp.context.update(buildconfig.defines["ALLDEFINES"])
        pp.handleCommandLine(
            ["-Fsubstitution"] + preprocessor_args + [defines_in, "-o", defines_out]
        )

    if preprocess_locale or single_files or convert_utf8:
        ppl = str(preprocess_locale_script)

    try:
        if preprocess_locale:
            subprocess.check_call(
                [sys.executable, ppl, "--preprocess-locale", str(topsrcdir)]
                + locale_args
                + [ab_cd, str(config_dir)]
            )

        for ftl, nlf in single_files:
            subprocess.check_call(
                [sys.executable, ppl, "--preprocess-single-file", str(topsrcdir)]
                + locale_args
                + ([f"--ab-cd={ab_cd}"] if ab_cd else [])
                + [str(config_dir), ftl, nlf]
            )

        for src, dest in convert_utf8:
            subprocess.check_call([
                sys.executable,
                ppl,
                "--convert-utf8-utf16le",
                src,
                dest,
            ])
    except subprocess.CalledProcessError as e:
        return e.returncode

    return 0


def stage_digest(config_dir: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(config_dir.rglob("*")):
        if path.is_file():
            data = path.read_bytes()
            name = path.relative_to(config_dir).as_posix()
            digest.update(f"{len(name)}:{name}:{len(data)}:".encode())
            digest.update(data)
    return digest.hexdigest()


def write_spec(
    path: Path, args: argparse.Namespace, preprocessor_args: list[str]
) -> None:
    spec = {
        "installs": args.installs,
        "defines_in": args.defines_in,
        "defines_out": Path(args.defines_out).name if args.defines_out else "",
        "preprocessor_args": preprocessor_args,
        "topsrcdir": str(args.topsrcdir) if args.topsrcdir else "",
        "preprocess_locale_script": args.preprocess_locale_script,
        "repack_locale_args": args.repack_locale_args,
        "preprocess_locale": args.preprocess_locale,
        "single_files": args.single_files,
        "convert_utf8": [[src, Path(dest).name] for src, dest in args.convert_utf8],
        "repack_l10n_dir": args.repack_l10n_dir,
    }
    with FileAvoidWrite(str(path)) as fh:
        fh.write(json.dumps(spec, indent=2, sort_keys=True))


def stage_repack(
    spec_path: Path,
    config_dir: Path,
    ab_cd: str,
    real_locale_mergedir: Path,
) -> int:
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    repack_l10n_dir = spec.get("repack_l10n_dir")
    if not repack_l10n_dir:
        raise ValueError(f"{spec_path} does not record a repack l10n dir")

    locale_args = [f"--l10n-dir={real_locale_mergedir / repack_l10n_dir}"] + [
        f"--l10n-dir={arg.removeprefix('--l10n-dir=')}"
        for arg in spec["repack_locale_args"]
    ]
    preprocessor_args = [
        f"-DAB_CD={ab_cd}" if arg.startswith("-DAB_CD=") else arg
        for arg in spec["preprocessor_args"]
    ]
    if f"-DAB_CD={ab_cd}" not in preprocessor_args:
        preprocessor_args.append(f"-DAB_CD={ab_cd}")

    return nsis_stage(
        config_dir=config_dir,
        installs=spec["installs"],
        defines_in=spec["defines_in"],
        defines_out=str(config_dir / spec["defines_out"])
        if spec["defines_out"]
        else "",
        preprocessor_args=preprocessor_args,
        topsrcdir=Path(spec["topsrcdir"]),
        preprocess_locale_script=spec["preprocess_locale_script"],
        locale_args=locale_args,
        ab_cd=ab_cd,
        preprocess_locale=spec["preprocess_locale"],
        single_files=spec["single_files"],
        convert_utf8=[
            [src, str(config_dir / dest)] for src, dest in spec["convert_utf8"]
        ],
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stage a CONFIG_DIR with the files makensis needs."
    )
    parser.add_argument(
        "--config-dir",
        required=True,
        type=Path,
        metavar="CONFIG_DIR",
        help="Directory to stage into. Wiped and recreated.",
    )
    parser.add_argument(
        "--install",
        action="append",
        default=[],
        dest="installs",
        help="File to copy into CONFIG_DIR (basename preserved). Repeatable.",
    )
    parser.add_argument("--defines-in", default="", help="defines.nsi.in input")
    parser.add_argument("--defines-out", default="", help="defines.nsi output")
    parser.add_argument(
        "--defines-string",
        action="append",
        default=[],
        dest="defines_strings",
        metavar="DEFINES",
        help="A shell quoted string of preprocessor arguments, split with "
        "shlex. These precede any arguments given verbatim. Repeatable.",
    )
    parser.add_argument("--topsrcdir", type=Path, help="Top source directory")
    parser.add_argument(
        "--preprocess-locale-script",
        metavar="PATH",
        help="Path to preprocess-locale.py, run for the locale steps below.",
    )
    parser.add_argument(
        "--locale-arg",
        action="append",
        default=[],
        dest="locale_args",
        help="Verbatim PPL_LOCALE_ARGS token for the preprocess-locale.py "
        "invocation (a locale dir, or a --l10n-dir=... entry). Repeatable.",
    )
    parser.add_argument("--ab-cd", default="", help="The ab_cd locale code")
    parser.add_argument(
        "--preprocess-locale",
        action="store_true",
        help="Run preprocess-locale.py --preprocess-locale",
    )
    parser.add_argument(
        "--single-file",
        action="append",
        nargs=2,
        default=[],
        dest="single_files",
        metavar=("FTL", "NLF"),
        help="Run preprocess-locale.py --preprocess-single-file. Repeatable.",
    )
    parser.add_argument(
        "--convert-utf8",
        action="append",
        nargs=2,
        default=[],
        metavar=("SRC", "DEST"),
        help="Run preprocess-locale.py --convert-utf8-utf16le. Repeatable.",
    )
    parser.add_argument(
        "--spec-out",
        type=Path,
        metavar="PATH",
        help="Record the staging inputs as JSON at PATH so l10n_repackage can "
        "restage them for another locale.",
    )
    parser.add_argument(
        "--repack-l10n-dir",
        metavar="DIR",
        help="Locale repository relative directory holding the installer "
        "strings, joined to the locale merge dir on repack. Required with "
        "--spec-out.",
    )
    parser.add_argument(
        "--repack-locale-arg",
        action="append",
        default=[],
        dest="repack_locale_args",
        help="--locale-arg token a repack reads after its own locale, in place "
        "of the --locale-arg tokens of this stage. Repeatable, and required with "
        "--spec-out.",
    )
    return parser


def _stage(argv: list[str]) -> tuple[int, argparse.Namespace]:
    parser = _parser()
    args, remaining = parser.parse_known_args(argv)
    if (
        args.preprocess_locale or args.single_files or args.convert_utf8
    ) and not args.preprocess_locale_script:
        parser.error("--preprocess-locale-script is required for the locale steps")
    if args.spec_out and not args.repack_l10n_dir:
        parser.error("--repack-l10n-dir is required with --spec-out")
    if args.spec_out and not args.repack_locale_args:
        parser.error("--repack-locale-arg is required with --spec-out")

    preprocessor_args = []
    for defines in args.defines_strings:
        preprocessor_args += shlex.split(defines)
    preprocessor_args += remaining

    ret = nsis_stage(
        config_dir=args.config_dir,
        installs=args.installs,
        defines_in=args.defines_in,
        defines_out=args.defines_out,
        preprocessor_args=preprocessor_args,
        topsrcdir=args.topsrcdir,
        preprocess_locale_script=args.preprocess_locale_script,
        locale_args=args.locale_args,
        ab_cd=args.ab_cd,
        preprocess_locale=args.preprocess_locale,
        single_files=args.single_files,
        convert_utf8=args.convert_utf8,
    )
    if ret == 0 and args.spec_out:
        write_spec(args.spec_out, args, preprocessor_args)
    return ret, args


def main(argv: list[str]) -> int:
    return _stage(argv)[0]


def generate(output, *argv: str) -> int | set[str]:
    ret, args = _stage([*argv])
    if ret:
        return ret

    output.write(stage_digest(args.config_dir))
    l10n_dirs = [
        d
        for arg in args.locale_args
        if (d := Path(arg.removeprefix("--l10n-dir="))).is_dir()
    ]
    return {str(d) for d in l10n_dirs} | {
        str(p) for d in l10n_dirs for p in d.iterdir() if p.is_file()
    }


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
