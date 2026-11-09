# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import os
import shutil
import subprocess
import sys

import buildconfig


def dump_symbols(
    target, tracking_file, count_ctors=False, symbols_dir="crashreporter-symbols"
):
    symbols_path = os.path.join(buildconfig.topobjdir, "dist", symbols_dir)

    # Our tracking file, if present, will contain path(s) to the previously generated
    # symbols. Remove them in this case so we don't simply accumulate old symbols
    # during incremental builds.
    if os.path.isfile(os.path.normpath(tracking_file)):
        with open(tracking_file) as fh:
            files = fh.read().splitlines()
        dirs = set(os.path.dirname(f) for f in files)
        for d in dirs:
            shutil.rmtree(os.path.join(symbols_path, d), ignore_errors=True)

    # Build default args for symbolstore.py based on platform.
    sym_store_args = []

    dump_syms_bin = buildconfig.substs["DUMP_SYMS"]
    os_arch = buildconfig.substs["OS_ARCH"]
    if os_arch == "WINNT":
        sym_store_args.extend(["-c", "--vcs-info"])
        if "PDBSTR" in buildconfig.substs:
            sym_store_args.append("-i")
    elif os_arch == "Darwin":
        cpu = {
            "x86": "i386",
            "aarch64": "arm64",
        }.get(buildconfig.substs["TARGET_CPU"], buildconfig.substs["TARGET_CPU"])
        sym_store_args.extend(["-c", "-a", cpu, "--vcs-info"])
    elif os_arch == "Linux":
        sym_store_args.extend(["-c", "--vcs-info"])

    install_manifest = os.path.join(
        buildconfig.topobjdir, "_build_manifests", "install", "dist_include"
    )
    dist_include = os.path.join(buildconfig.topobjdir, "dist", "include")
    sym_store_args.append(f"--install-manifest={install_manifest},{dist_include}")
    objcopy = buildconfig.substs.get("OBJCOPY")
    if objcopy:
        os.environ["OBJCOPY"] = objcopy

    if buildconfig.substs.get("MOZ_THUNDERBIRD"):
        sym_store_args.extend(["-s", os.path.join(buildconfig.topsrcdir, "comm")])

    args = (
        [
            sys.executable,
            os.path.join(
                buildconfig.topsrcdir,
                "toolkit",
                "crashreporter",
                "tools",
                "symbolstore.py",
            ),
        ]
        + sym_store_args
        + [
            "-s",
            buildconfig.topsrcdir,
            dump_syms_bin,
            symbols_path,
            os.path.abspath(target),
        ]
    )
    if count_ctors:
        args.append("--count-ctors")
    print(f"Running: {' '.join(args)}")
    # symbolstore.py's stderr is sometimes dropped from build logs (see bug 2027747),
    # so capture and write it to stdout, which is logged reliably.
    result = subprocess.run(
        args,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stderr:
        sys.stdout.write(result.stderr)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(
            result.returncode, args, output=result.stdout
        )
    out_files = result.stdout
    with open(tracking_file, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out_files)
        fh.flush()


def main(argv):
    parser = argparse.ArgumentParser(
        usage="Usage: dumpsymbols.py <library or program> <tracking file>"
    )
    parser.add_argument(
        "--count-ctors",
        action="store_true",
        default=False,
        help="Count static initializers",
    )
    parser.add_argument(
        "--symbols-dir",
        default="crashreporter-symbols",
        help="Directory under dist/ to store the symbols in",
    )
    parser.add_argument("library_or_program", help="Path to library or program")
    parser.add_argument("tracking_file", help="Tracking file")
    args = parser.parse_args(argv)

    dump_symbols(
        args.library_or_program,
        args.tracking_file,
        args.count_ctors,
        args.symbols_dir,
    )

    substs = buildconfig.substs
    if substs.get("OS_ARCH") != "WINNT" or not substs.get("WINCHECKSEC"):
        return 0
    try:
        subprocess.check_call([
            sys.executable,
            os.path.join(buildconfig.topsrcdir, "build", "win32", "autowinchecksec.py"),
            args.library_or_program,
        ])
    except subprocess.CalledProcessError as e:
        return e.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
