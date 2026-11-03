# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

import mozinfo
import yaml
from marionette_driver.errors import JavascriptException, ScriptTimeoutException
from mozproxy import get_playback

AWSY_PATH = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
if AWSY_PATH not in sys.path:
    sys.path.append(AWSY_PATH)

from awsy import process_perf_data
from awsy.awsy_test_case import AwsyTestCase


class TestMemoryUsage(AwsyTestCase):
    """Provides a test that collects memory usage at various checkpoints:
    - "Start" - Just after startup
    - "StartSettled" - After an additional wait time
    - "TabsOpen" - After opening all provided URLs
    - "TabsOpenSettled" - After an additional wait time
    - "TabsOpenForceGC" - After forcibly invoking garbage collection
    - "TabsClosed" - After closing all tabs
    - "TabsClosedSettled" - After an additional wait time
    - "TabsClosedForceGC" - After forcibly invoking garbage collection
    """

    def urls(self):
        return self._urls

    def perf_suites(self):
        return process_perf_data.PERF_SUITES

    def perf_checkpoints(self):
        return process_perf_data.CHECKPOINTS

    def perf_extra_opts(self):
        return self._extra_opts

    def setupTp6(self):
        default_tp6_pages_manifest = os.path.join(AWSY_PATH, "conf", "tp6-pages.yml")
        tp6_pages_manifest = self.testvars.get(
            "pageManifest", default_tp6_pages_manifest
        )
        urls = []
        with open(tp6_pages_manifest) as f:
            d = yaml.safe_load(f)
            for r in d:
                url = r["url"]
                if isinstance(url, list):
                    urls.extend(url)
                else:
                    urls.append(url)

        self._urls = urls

        # Indicate that we're using tp6 in the perf data.
        self._extra_opts = ["tp6"]

        if self.marionette.get_pref("fission.autostart"):
            self._extra_opts.append("fission")

        # Now we setup the mitm proxy with our tp6 pageset.
        tp6_pageset_manifest = os.path.join(AWSY_PATH, "tp6-pageset.manifest")
        config = {
            "playback_tool": "mitmproxy",
            "playback_version": "5.1.1",
            "playback_files": [tp6_pageset_manifest],
            "platform": mozinfo.os,
            "obj_path": self._webroot_dir,
            "binary": self._binary,
            "run_local": self._run_local,
            "app": "firefox",
            "host": "127.0.0.1",
            "ignore_mitmdump_exit_failure": True,
        }

        self._playback = get_playback(config)
        self._playback.start()

        # We need to reload after the mitmproxy cert is installed
        self.marionette.restart(in_app=False, clean=False)

        # Setup WebDriver capabilities that we need
        self.marionette.delete_session()
        caps = {
            "unhandledPromptBehavior": "dismiss",  # Ignore page navigation warnings
        }
        self.marionette.start_session(caps)
        self.marionette.set_context("chrome")

    def setUp(self):
        AwsyTestCase.setUp(self)
        self.logger.info("setting up")
        self._webroot_dir = self.testvars["webRootDir"]
        self._urls = []
        self._extra_opts = None

        self.setupTp6()

        self.logger.info(
            f"areweslimyet run by {self._pages_to_load} pages, "
            f"{self._iterations} iterations,"
            f" {self._perTabPause} perTabPause, "
            f"{self._settleWaitTime} settleWaitTime"
        )
        self.logger.info("done setting up!")

    def tearDown(self):
        self.logger.info("tearing down!")

        self._playback.stop()

        AwsyTestCase.tearDown(self)

        self.logger.info("done tearing down!")

    def clear_preloaded_browser(self):
        """
        Clears out the preloaded browser.
        """
        self.logger.info("closing preloaded browser")
        script = """
            if (window.NewTabPagePreloading) {
                return NewTabPagePreloading.removePreloadedBrowser(window);
            }
            return "NewTabPagePreloading.removePreloadedBrowser not available";
            """
        try:
            result = self.marionette.execute_script(script, script_timeout=180000)
        except JavascriptException as e:
            self.logger.error(f"removePreloadedBrowser() JavaScript error: {e}")
        except ScriptTimeoutException:
            self.logger.error("removePreloadedBrowser() timed out")
        except Exception:
            self.logger.error(
                f"removePreloadedBrowser() Unexpected error: {sys.exc_info()[0]}"
            )
        else:
            if result:
                self.logger.info(result)

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

        # The first iteration gets Start and StartSettled entries before
        # opening tabs
        create_checkpoint("Start", 0)
        self.settle()
        create_checkpoint("StartSettled", 0)

        for itr in range(self.iterations()):
            self.open_pages()

            create_checkpoint("TabsOpen", itr)
            self.settle()
            create_checkpoint("TabsOpenSettled", itr)
            create_checkpoint("TabsOpenForceGC", itr, minimize=True)

            # Close all tabs
            self.reset_state()

            with self.marionette.using_context("content"):
                self.logger.info("navigating to about:blank")
                self.marionette.navigate("about:blank")
                self.logger.info("navigated to about:blank")
            self.signal_user_active()

            # Create checkpoint that may contain retained processes that will
            # be reused.
            create_checkpoint("TabsClosedExtraProcesses", itr)

            # Clear out the retained processes and measure again.
            self.clear_preloaded_browser()

            create_checkpoint("TabsClosed", itr)
            self.settle()
            create_checkpoint("TabsClosedSettled", itr)
            create_checkpoint("TabsClosedForceGC", itr, minimize=True)

        self.logger.info("setting results")
        self.testvars["results"] = results
