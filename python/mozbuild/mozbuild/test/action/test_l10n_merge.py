# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mozunit

from mozbuild.action import l10n_merge


def _fake_build(files):
    def run(argv, check):
        target = Path(argv[argv.index("--target") + 1])
        locale = argv[argv.index("--locales") + 1]
        for name, content in files.items():
            path = target / locale / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        return subprocess.CompletedProcess(argv, 0)

    return run


class TestMergeLocale(unittest.TestCase):
    def _merge(self, tmp, files):
        with mock.patch.object(
            l10n_merge.subprocess, "run", side_effect=_fake_build(files)
        ):
            return l10n_merge.merge_locale(
                locale="de",
                config="/src/browser/locales/l10n.toml",
                l10n_base=os.path.join(tmp, "l10n"),
                target=os.path.join(tmp, "merge-dir"),
            )

    def test_merge_locale_sync(self):
        with tempfile.TemporaryDirectory() as tmp:
            merge_dir = Path(tmp) / "merge-dir" / "de"
            self.assertEqual(
                self._merge(
                    tmp,
                    {
                        "browser/unchanged.ftl": "same",
                        "browser/changed.ftl": "old",
                        "browser/removed.ftl": "gone",
                    },
                ),
                0,
            )
            unchanged = merge_dir / "browser" / "unchanged.ftl"
            os.utime(unchanged, (1000000000, 1000000000))

            self.assertEqual(
                self._merge(
                    tmp,
                    {
                        "browser/unchanged.ftl": "same",
                        "browser/changed.ftl": "new",
                        "browser/added.ftl": "added",
                    },
                ),
                0,
            )

            self.assertEqual(
                sorted(
                    p.relative_to(merge_dir).as_posix()
                    for p in merge_dir.rglob("*")
                    if p.is_file()
                ),
                ["browser/added.ftl", "browser/changed.ftl", "browser/unchanged.ftl"],
            )
            self.assertEqual((merge_dir / "browser" / "changed.ftl").read_text(), "new")
            self.assertEqual(unchanged.stat().st_mtime, 1000000000)
            self.assertEqual(os.listdir(Path(tmp) / "merge-dir"), ["de"])

    def test_merge_locale_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            merge_dir = Path(tmp) / "merge-dir" / "de"
            merge_dir.mkdir(parents=True)
            (merge_dir / "kept.ftl").write_text("kept")
            with mock.patch.object(
                l10n_merge.subprocess,
                "run",
                return_value=subprocess.CompletedProcess([], 2),
            ):
                rc = l10n_merge.merge_locale(
                    locale="de",
                    config="/src/browser/locales/l10n.toml",
                    l10n_base=os.path.join(tmp, "l10n"),
                    target=os.path.join(tmp, "merge-dir"),
                )

            self.assertEqual(rc, 2)
            self.assertEqual((merge_dir / "kept.ftl").read_text(), "kept")
            self.assertEqual(os.listdir(Path(tmp) / "merge-dir"), ["de"])


if __name__ == "__main__":
    mozunit.main()
