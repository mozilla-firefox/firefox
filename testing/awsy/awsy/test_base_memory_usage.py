# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

AWSY_PATH = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
if AWSY_PATH not in sys.path:
    sys.path.append(AWSY_PATH)

from awsy.awsy_test_case import AwsyTestCase

# A description of each checkpoint and the root path to it.
CHECKPOINTS = [
    {
        "name": "After tabs open [+30s, forced GC]",
        "path": "memory-report-TabsOpenForceGC-4.json.gz",
        "name_filter": ["web ", "Web Content"],  # We only want the content process
        "median": True,  # We want the median from all content processes
    },
]

# A description of each perfherder suite and the path to its values.
PERF_SUITES = [
    {"name": "Base Content Resident Unique Memory", "node": "resident-unique"},
    {"name": "Base Content Heap Unclassified", "node": "explicit/heap-unclassified"},
    {"name": "Base Content JS", "node": "js-main-runtime/", "alertThreshold": 0.25},
    {"name": "Base Content Explicit", "node": "explicit/"},
]


class TestMemoryUsage(AwsyTestCase):
    """
    Provides a base case test that just loads about:memory and reports the
    memory usage of a single content process.
    """

    def urls(self):
        return self._urls

    def perf_suites(self):
        return PERF_SUITES

    def perf_checkpoints(self):
        return CHECKPOINTS

    def perf_extra_opts(self):
        return self._extra_opts

    def setUp(self):
        AwsyTestCase.setUp(self)
        self.logger.info("setting up!")

        # Override AwsyTestCase value, this is always going to be 1 iteration.
        self._iterations = 1

        # Override "entities" from our configuration.
        #
        # We aim to load a number of about:blank pages exactly matching the
        # number of content processes we can have. After this we should no
        # longer have a preallocated content process (although to be sure we
        # explicitly drop it at the end of the test).
        process_count = self.marionette.get_pref("dom.ipc.processCount")
        self._pages_to_load = process_count
        self._urls = ["about:blank"] * process_count

        if self.marionette.get_pref("fission.autostart"):
            self._extra_opts = ["fission"]
        else:
            self._extra_opts = None

        self.logger.info(
            f"areweslimyet run by {self._pages_to_load} pages, "
            f"{self._iterations} iterations, {self._perTabPause} perTabPause, "
            f"{self._settleWaitTime} settleWaitTime, "
            f"{process_count} content processes"
        )
        self.logger.info("done setting up!")

    def tearDown(self):
        self.logger.info("tearing down!")
        AwsyTestCase.tearDown(self)
        self.logger.info("done tearing down!")

    def set_preallocated_process_enabled_state(self, enabled):
        """Sets the pref that controls whether we have a preallocated content
        process to the given value.

        This will cause the preallocated process to be destroyed or created
        as appropriate.
        """
        if enabled:
            self.logger.info("re-enabling preallocated process")
        else:
            self.logger.info("disabling preallocated process")
        self.marionette.set_pref("dom.ipc.processPrelaunch.enabled", enabled)

    def test_open_tabs(self):
        """Marionette test entry that returns an array of checkpoint arrays.

        This will generate a set of checkpoints for each iteration requested.
        Upon successful completion the results will be stored in
        |self.testvars["results"]| and accessible to the test runner via the
        |testvars| object it passed in.
        """
        # setup the results array
        results = [[] for _ in range(self.iterations())]

        def create_checkpoint(name, iteration, minimize=False):
            checkpoint = self.do_memory_report(name, iteration, minimize)
            self.assertIsNotNone(checkpoint, "Checkpoint was recorded")
            results[iteration].append(checkpoint)

        # As long as we force the number of iterations to 1 in setUp() above,
        # we don't need to loop over this work.
        assert self._iterations == 1
        self.open_pages()
        self.set_preallocated_process_enabled_state(False)
        self.settle()
        self.settle()
        create_checkpoint("TabsOpenForceGC", 0, minimize=True)
        self.set_preallocated_process_enabled_state(True)
        # (If we wanted to do something after the preallocated process has been
        # recreated, we should call self.settle() again to wait for it.)

        self.logger.info("setting results")
        self.testvars["results"] = results
