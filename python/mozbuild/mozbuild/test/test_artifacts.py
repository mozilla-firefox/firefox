# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import TestCase, mock

import buildconfig
import mozpack.path as mozpath
import mozunit

from mozbuild.artifacts import (
    AndroidArtifactJob,
    ArtifactJob,
    GeckoJobConfiguration,
    LinuxArtifactJob,
    MacArtifactJob,
    ThunderbirdJobConfiguration,
    WinArtifactJob,
)


class FakeArtifactJob(ArtifactJob):
    package_re = r""
    job_configuration = GeckoJobConfiguration


class TestArtifactJob(TestCase):
    def _assert_candidate_trees(self, version_display, expected_trees):
        with mock.patch.object(
            buildconfig, "substs", {"MOZ_APP_VERSION_DISPLAY": version_display}
        ):
            job = FakeArtifactJob()
            self.assertGreater(len(job.candidate_trees), 0)
            self.assertEqual(job.candidate_trees, expected_trees)

    def test_candidate_trees_with_empty_file(self):
        self._assert_candidate_trees(
            version_display="",
            expected_trees=GeckoJobConfiguration.default_candidate_trees,
        )

    def test_candidate_trees_with_beta_version(self):
        self._assert_candidate_trees(
            version_display="92.1b2",
            expected_trees=GeckoJobConfiguration.beta_candidate_trees,
        )

    def test_candidate_trees_with_esr_version(self):
        self._assert_candidate_trees(
            version_display="91.3.0esr",
            expected_trees=GeckoJobConfiguration.esr_candidate_trees,
        )

    def test_candidate_trees_with_nightly_version(self):
        self._assert_candidate_trees(
            version_display="95.0a1",
            expected_trees=GeckoJobConfiguration.nightly_candidate_trees,
        )

    def test_candidate_trees_with_release_version(self):
        self._assert_candidate_trees(
            version_display="93.0.1",
            expected_trees=GeckoJobConfiguration.default_candidate_trees,
        )

    def test_candidate_trees_with_newline_before_version(self):
        self._assert_candidate_trees(
            version_display="\n\n91.3.0esr",
            expected_trees=GeckoJobConfiguration.esr_candidate_trees,
        )

    def test_property_is_cached(self):
        job = FakeArtifactJob()
        expected_trees = GeckoJobConfiguration.esr_candidate_trees

        buildconfig.substs["MOZ_APP_VERSION_DISPLAY"] = "91.3.0.esr"
        self.assertEqual(job.candidate_trees, expected_trees)
        # Because the property is cached, changing the
        # `MOZ_APP_VERSION_DISPLAY` won't have any impact.
        buildconfig.substs["MOZ_APP_VERSION_DISPLAY"] = ""
        self.assertEqual(job.candidate_trees, expected_trees)


class TestTestArtifactPatterns(TestCase):
    JOB_CLASSES = (
        ArtifactJob,
        AndroidArtifactJob,
        LinuxArtifactJob,
        MacArtifactJob,
        WinArtifactJob,
    )

    def _resolve(self, job_class, entry):
        """Mimic how `process_tests_*_artifact` maps an archive entry: the first
        matching pattern wins."""
        for pattern, (src_prefix, dest_prefix) in job_class.test_artifact_patterns:
            if mozpath.match(entry, pattern):
                return mozpath.join(dest_prefix, mozpath.relpath(entry, src_prefix))
        return None

    def test_patterns_are_ordered(self):
        # Several patterns overlap, so an unordered container (e.g. a set) makes
        # the mapping depend on PYTHONHASHSEED.
        for job_class in self.JOB_CLASSES:
            self.assertIsInstance(
                job_class.test_artifact_patterns, (list, tuple), job_class.__name__
            )

    def test_gmp_plugins_land_in_bin(self):
        # The fake GMP plugin libraries must end up in dist/bin so that
        # test_archive.py packages them; landing them in dist/plugins silently
        # drops them from the test archive and crashes the GMP child process.
        for job_class in self.JOB_CLASSES:
            for entry in (
                "bin/plugins/gmp-fake/1.0/libfake.so",
                "bin/plugins/gmp-fakeopenh264/1.0/libfakeopenh264.so",
                "bin/plugins/gmp-clearkey/0.1/libclearkey.so",
            ):
                self.assertEqual(
                    self._resolve(job_class, entry),
                    mozpath.join("bin", mozpath.relpath(entry, "bin/plugins")),
                    f"{job_class.__name__}: {entry}",
                )

    def test_other_plugins_land_in_plugins(self):
        for job_class in self.JOB_CLASSES:
            self.assertEqual(
                self._resolve(job_class, "bin/plugins/libnptest.so"),
                "plugins/libnptest.so",
                job_class.__name__,
            )


class TestThunderbirdMixin(TestCase):
    def _assert_candidate_trees(self, version_display, source_repo, expected_trees):
        with mock.patch.object(
            buildconfig,
            "substs",
            {
                "MOZ_APP_VERSION_DISPLAY": version_display,
                "MOZ_SOURCE_REPO": source_repo,
            },
        ):
            job = FakeArtifactJob(
                override_job_configuration=ThunderbirdJobConfiguration
            )
            self.assertGreater(len(job.candidate_trees), 0)
            self.assertEqual(job.candidate_trees, expected_trees)

    def test_candidate_trees_with_beta_version(self):
        self._assert_candidate_trees(
            version_display="92.1b2",
            source_repo="https://hg.mozilla.org/releases/comm-beta",
            expected_trees=ThunderbirdJobConfiguration.beta_candidate_trees,
        )

    def test_candidate_trees_with_esr_version(self):
        self._assert_candidate_trees(
            version_display="91.3.0esr",
            source_repo="https://hg.mozilla.org/releases/comm-esr91",
            expected_trees=ThunderbirdJobConfiguration.esr_candidate_trees,
        )

    def test_candidate_trees_with_nightly_version(self):
        self._assert_candidate_trees(
            version_display="95.0a1",
            source_repo="https://hg.mozilla.org/comm-central",
            expected_trees=ThunderbirdJobConfiguration.nightly_candidate_trees,
        )

    def test_candidate_trees_with_release_version(self):
        self._assert_candidate_trees(
            version_display="93.0.1",
            source_repo="https://hg.mozilla.org/releases/comm-release",
            expected_trees=ThunderbirdJobConfiguration.default_candidate_trees,
        )

    def test_property_is_cached(self):
        job = FakeArtifactJob(override_job_configuration=ThunderbirdJobConfiguration)
        expected_trees = ThunderbirdJobConfiguration.esr_candidate_trees

        buildconfig.substs["MOZ_APP_VERSION_DISPLAY"] = "91.3.0.esr"
        self.assertEqual(job.candidate_trees, expected_trees)
        # Because the property is cached, changing the
        # `MOZ_APP_VERSION_DISPLAY` won't have any impact.
        buildconfig.substs["MOZ_APP_VERSION_DISPLAY"] = ""
        self.assertEqual(job.candidate_trees, expected_trees)


if __name__ == "__main__":
    mozunit.main()
