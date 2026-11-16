# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy

from marionette_driver import Wait
from marionette_harness import MarionetteTestCase

BROWSER_DOC = "chrome://browser/content/browser.xhtml"
MAIN_WINDOW = "main-window"

PERSISTED_WIDTH = "999"
PERSISTED_HEIGHT = "888"
CLI_WIDTH = 723
CLI_HEIGHT = 456


class TestCLIWindowSize(MarionetteTestCase):
    def setUp(self):
        super().setUp()
        self.orig_args = copy.copy(self.marionette.instance.app_args)

    def tearDown(self):
        self.marionette.instance.app_args = self.orig_args
        self.marionette.quit(in_app=False, clean=True)
        super().tearDown()

    def test_cli_overrides_persisted_size(self):
        with self.marionette.using_context("chrome"):
            readback = self.marionette.execute_script(
                """
                let [doc, id, w, h] = arguments;
                Services.xulStore.setValue(doc, id, "width", w);
                Services.xulStore.setValue(doc, id, "height", h);
                return [
                  Services.xulStore.getValue(doc, id, "width"),
                  Services.xulStore.getValue(doc, id, "height"),
                ];
                """,
                script_args=(
                    BROWSER_DOC,
                    MAIN_WINDOW,
                    PERSISTED_WIDTH,
                    PERSISTED_HEIGHT,
                ),
            )
            self.assertEqual(readback, [PERSISTED_WIDTH, PERSISTED_HEIGHT])

        self.marionette.quit()
        self.marionette.instance.app_args = self.orig_args + [
            "-width",
            str(CLI_WIDTH),
            "-height",
            str(CLI_HEIGHT),
        ]
        self.marionette.start_session()

        # -width/-height set the browser chrome window's content viewport size
        # (window.open's "width"/"height" features), not the OS-level outer
        # window size, which also includes platform-dependent chrome (window
        # frame, etc).
        with self.marionette.using_context("chrome"):
            width = self.marionette.execute_script("return window.innerWidth;")
            height = self.marionette.execute_script("return window.innerHeight;")

        self.assertAlmostEqual(width, CLI_WIDTH, delta=5)
        self.assertAlmostEqual(height, CLI_HEIGHT, delta=5)

    def test_start_maximized_overrides_persisted_sizemode(self):
        with self.marionette.using_context("chrome"):
            readback = self.marionette.execute_script(
                """
                let [doc, id] = arguments;
                Services.xulStore.setValue(doc, id, "sizemode", "normal");
                return Services.xulStore.getValue(doc, id, "sizemode");
                """,
                script_args=(BROWSER_DOC, MAIN_WINDOW),
            )
            self.assertEqual(readback, "normal")

        self.marionette.quit()
        self.marionette.instance.app_args = self.orig_args + ["-start-maximized"]
        self.marionette.start_session()

        # On X11 the maximize is a request the window manager acknowledges
        # asynchronously, so poll rather than reading the state once.
        with self.marionette.using_context("chrome"):
            Wait(self.marionette, timeout=30).until(
                lambda m: m.execute_script(
                    "return window.windowState == window.STATE_MAXIMIZED;"
                ),
                message="window was not maximized with -start-maximized",
            )
