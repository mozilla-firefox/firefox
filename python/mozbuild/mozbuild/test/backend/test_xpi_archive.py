# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""What an extension archive is built from."""

import os

import mozpack.path as mozpath
import mozunit
from mozpack.copier import FileRegistry
from mozpack.manifests import InstallManifest

from mozbuild.backend.recursivemake import RecursiveMakeBackend
from mozbuild.test.backend.common import BackendTester

STAGED = "$(topobjdir)/_build_manifests/built/dist/xpi-stage/genext"


class TestXPIArchiveEdge(BackendTester):
    def _root_deps(self, srcdir):
        env = self._consume(srcdir, RecursiveMakeBackend)

        with open(mozpath.join(env.topobjdir, "root-deps.mk")) as fh:
            return env, [l.rstrip() for l in fh.readlines()]

    def test_xpi_stage_built_file(self):
        env, lines = self._root_deps("xpi-archive-generated")

        start = lines.index(f"{STAGED}.track: {STAGED} $(DEPTH)/extension/made.js")
        self.assertEqual(
            lines[start + 1 : start + 3],
            [
                f"\t$(call py_action,process_install_manifest dist/xpi-stage/genext,"
                f"--track {STAGED}.track $(topobjdir) {STAGED})",
                "\t@$(TOUCH) $@",
            ],
        )
        m = InstallManifest(
            path=mozpath.join(
                env.topobjdir,
                "_build_manifests",
                "built",
                "dist",
                "xpi-stage",
                "genext",
            )
        )
        registry = FileRegistry()
        m.populate_registry(registry)
        made = mozpath.join(env.topobjdir, "extension", "made.js")
        self.assertEqual(
            {dest: f.path for dest, f in registry},
            {
                "dist/xpi-stage/genext/made.js": made,
                "dist/xpi-stage/genext/renamed.js": made,
            },
        )

    def test_xpi_stage_renamed_built_file(self):
        env = self._consume("xpi-archive-generated", RecursiveMakeBackend)
        m = InstallManifest(
            path=mozpath.join(
                env.topobjdir, "_build_manifests", "install", "dist_xpi-stage"
            )
        )

        self.assertIn("genext/made.js", m)
        self.assertIn("genext/renamed.js", m)

    def test_xpi_stage_misc_tier(self):
        _, lines = self._root_deps("xpi-archive-generated")

        rule = [l for l in lines if l.startswith("recurse_misc:")]
        self.assertTrue(any(f"{STAGED}.track" in l for l in rule))

    def test_xpi_stage_no_archive(self):
        _, lines = self._root_deps("xpi-stage-no-archive")
        staged = "$(topobjdir)/_build_manifests/built/dist/xpi-stage/stageonly"

        self.assertIn(
            f"{staged}.track: {staged} $(DEPTH)/staged/made.js",
            lines,
        )
        rule = [l for l in lines if l.startswith("recurse_misc:")]
        self.assertTrue(any(f"{staged}.track" in l for l in rule))

    def test_xpi_stage_single_install(self):
        env = self._consume("xpi-archive-generated", RecursiveMakeBackend)

        with open(mozpath.join(env.topobjdir, "extension", "backend.mk")) as fh:
            body = fh.read()
        self.assertNotIn("process_install_manifest", body)
        self.assertFalse(
            os.path.exists(
                mozpath.join(env.topobjdir, "_build_manifests", "built", "extension")
            )
        )

    def test_xpi_stage_needs_top_level_rule(self):
        with self.assertRaisesRegex(
            Exception, "The copy into dist/xpi-stage/compiled/made.inc reads"
        ):
            self._consume("xpi-stage-compile-output", RecursiveMakeBackend)


if __name__ == "__main__":
    mozunit.main()
