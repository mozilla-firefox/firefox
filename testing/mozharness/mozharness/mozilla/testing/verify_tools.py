#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from mozharness.base.script import PostScriptAction
from mozharness.mozilla.testing.per_test_base import SingleTestMixin

verify_config_options = [
    [
        ["--verify"],
        {
            "action": "store_true",
            "dest": "verify",
            "default": False,
            "help": "Run additional verification on modified tests.",
        },
    ],
    [
        ["--verify-paths"],
        {
            "action": "store_true",
            "dest": "verify_paths",
            "default": False,
            "help": "Verify every test under the requested paths, rather than "
            "only the tests modified on this push.",
        },
    ],
]


class VerifyToolsMixin(SingleTestMixin):
    """Utility functions for test verification."""

    def __init__(self):
        super().__init__()

    @property
    def verify_enabled(self):
        try:
            return bool(self.config.get("verify"))
        except (AttributeError, KeyError, TypeError):
            return False

    @PostScriptAction("download-and-extract")
    def find_tests_for_verification(self, action, success=None):
        """
        For each file modified on this push, determine if the modified file
        is a test, by searching test manifests. Populate self.verify_suites
        with test files, organized by suite.

        This depends on test manifests, so can only run after test zips have
        been downloaded and extracted.
        """

        if not self.verify_enabled:
            return

        self.find_modified_tests()

    @property
    def verify_args(self):
        if not self.verify_enabled:
            return []

        # Limit each test harness run to 15 minutes, to avoid task timeouts
        # when executing long-running tests.
        MAX_TIME_PER_TEST = 900

        if self.config.get("per_test_category") == "web-platform":
            args = ["--verify-log-full"]
        else:
            args = ["--verify-max-time=%d" % MAX_TIME_PER_TEST]

        args.append("--verify")

        return args
