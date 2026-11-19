# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Per-locale merge action.

Builds <target>/<ab_cd>/ (e.g. <topobjdir>/<reldir>/merge-dir/<ab_cd>/)
from <l10n-base>/<ab_cd>/. Wraps python -m moz.l10n.bin.build and
copies hunspell dictionaries that moz.l10n doesn't handle.

Invoked in make via $(call py_action,l10n_merge,...).
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from mozbuild.util import FileAvoidWrite


def merge_locale(
    locale: str,
    config: str,
    l10n_base: str,
    target: str,
) -> int:
    merge_dir = Path(target) / locale
    Path(target).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target, prefix=f".{locale}-") as tmp:
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "moz.l10n.bin.build",
                "--config",
                config,
                "--base",
                l10n_base,
                "--target",
                tmp,
                "--locales",
                locale,
                "--coverage",
            ],
            check=False,
        )
        if result.returncode:
            return result.returncode

        tmp_locale = Path(tmp) / locale
        hunspell_rel = Path("extensions") / "spellcheck" / "hunspell"
        spellcheck_src = Path(l10n_base) / locale / hunspell_rel
        if spellcheck_src.is_dir():
            spellcheck_dst = tmp_locale / hunspell_rel
            spellcheck_dst.mkdir(parents=True, exist_ok=True)
            for entry in spellcheck_src.iterdir():
                if entry.is_file():
                    shutil.copy2(entry, spellcheck_dst / entry.name)

        produced = set()
        for src in tmp_locale.rglob("*"):
            if src.is_file():
                rel = src.relative_to(tmp_locale)
                produced.add(rel)
                dest = merge_dir / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                with FileAvoidWrite(str(dest), readmode="rb") as fh:
                    fh.write(src.read_bytes())

    for path in sorted(merge_dir.rglob("*"), reverse=True):
        if path.is_dir():
            if not any(path.iterdir()):
                path.rmdir()
        elif path.relative_to(merge_dir) not in produced:
            path.unlink()
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Build the per-locale merge tree for a single locale."
    )
    parser.add_argument("--locale", required=True, help="The ab_cd locale code")
    parser.add_argument(
        "--config",
        required=True,
        help="Path to the app's l10n.toml (e.g. browser/locales/l10n.toml)",
    )
    parser.add_argument(
        "--l10n-base",
        required=True,
        help="L10NBASEDIR (the locale source repo)",
    )
    parser.add_argument(
        "--target",
        required=True,
        help="Output merge root (e.g. <topobjdir>/<reldir>/merge-dir/)",
    )
    args = parser.parse_args(argv)

    return merge_locale(
        locale=args.locale,
        config=args.config,
        l10n_base=args.l10n_base,
        target=args.target,
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
