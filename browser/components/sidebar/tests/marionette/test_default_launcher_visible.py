# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from marionette_driver import Wait
from marionette_harness import MarionetteTestCase

default_visible_pref = "sidebar.revamp.defaultLauncherVisible"

initial_prefs = {
    "sidebar.revamp": False,
    default_visible_pref: False,
    # Set browser restore previous session pref
    # we'll need to examine behavior using restored sidebar properties
    "browser.startup.page": 3,
}


class TestDefaultLauncherVisible(MarionetteTestCase):

    def tearDown(self):
        try:
            # Make sure subsequent tests get a clean profile
            self.marionette.restart(in_app=False, clean=True)
        finally:
            super().tearDown()

    def _close_last_tab(self):
        # "self.marionette.close" cannot be used because it doesn't
        # allow closing the very last tab.
        self.marionette.execute_script("window.close()")

    def restart_with_prefs(self, prefs):
        # set the prefs then restart the browser
        self.marionette.set_prefs(prefs)
        self.marionette.restart()

        # Restore the context as used before the restart
        self.marionette.set_context("chrome")

        self.wait_for_startup_idle_promise()

    def is_launcher_visible(self):
        hidden = self.marionette.execute_script(
            """
            const window = BrowserWindowTracker.getTopWindow();
            return window.SidebarController.sidebarContainer.hidden;
            """
        )
        return not hidden

    def is_button_visible(self):
        visible = self.marionette.execute_script(
            """
            const window = BrowserWindowTracker.getTopWindow();
            const placement = window.CustomizableUI.getPlacementOfWidget('sidebar-button');
            if (!placement) {
                return false;
            }
            const node = window.document.getElementById("sidebar-button");
            return node && !node.hidden;
            """
        )
        return visible

    def click_toolbar_button(self):
        # Click the button to show the launcher
        self.marionette.execute_script(
            """
            const window = BrowserWindowTracker.getTopWindow();
            return window.document.getElementById("sidebar-button").click()
            """
        )

    def wait_for_startup_idle_promise(self):
        self.marionette.set_context("chrome")
        self.marionette.execute_async_script(
            """
            let resolve = arguments[0];
            let { BrowserInitState } = ChromeUtils.importESModule("resource:///modules/BrowserGlue.sys.mjs");
            BrowserInitState.startupIdleTaskPromise.then(resolve);
            """
        )

    def test_first_use_default_visible_pref_false(self):
        # We flip sidebar.revamp to true, with defaultLauncherVisible=false for a profile
        # that has never enabled or seen the sidebar launcher.

        self.wait_for_startup_idle_promise()

        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher is hidden",
        )
        self.assertFalse(
            self.is_button_visible(),
            "Sidebar toolbar button is hidden",
        )

        # Mimic an update which enables sidebar.revamp for the first time
        # ,with defaultLauncherVisible false
        self.restart_with_prefs(
            {
                "sidebar.revamp": True,
                "browser.startup.page": 3,
                default_visible_pref: False,
            }
        )

        Wait(self.marionette).until(
            lambda _: self.is_button_visible(),
            message="Sidebar button should be visible",
        )
        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher remains hidden because defaultLauncherVisible=false",
        )
        # Click the button and verify that sticks
        self.click_toolbar_button()

        Wait(self.marionette).until(
            lambda _: self.is_launcher_visible(),
            message="Sidebar button should be visible",
        )
        self.marionette.restart()
        self.marionette.set_context("chrome")
        self.wait_for_startup_idle_promise()

        self.assertTrue(
            self.is_launcher_visible(),
            "Sidebar launcher remains visible because user un-hid it in the resumed session",
        )

    def test_new_sidebar_enabled_default_visible_pref_false(self):
        # Set up the profile with session restore and new sidebar enabled
        self.restart_with_prefs(
            {
                "sidebar.revamp": True,
                "browser.startup.page": 3,
                default_visible_pref: False,
            }
        )

        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher is hidden",
        )
        self.assertTrue(
            self.is_button_visible(),
            "Sidebar toolbar button is visible",
        )

        # Mimic an update which enables sidebar.revamp with defaultLauncherVisible true
        # The restart with session restore enabled while the launcher is visible should persist
        # the launcherVisible=true and we don't want to override that
        self.restart_with_prefs(
            {
                "sidebar.revamp": True,
                "browser.startup.page": 3,
                default_visible_pref: True,
            }
        )

        Wait(self.marionette).until(
            lambda _: self.is_button_visible(),
            message="Sidebar button is still visible",
        )

        self.assertTrue(
            self.is_launcher_visible(),
            "Sidebar launcher is still visible",
        )

        # Click the button and verify that sticks
        self.click_toolbar_button()

        Wait(self.marionette).until(
            lambda _: not self.is_launcher_visible(),
            message="Sidebar launcher should be hidden",
        )
        self.marionette.restart()
        self.marionette.set_context("chrome")
        self.wait_for_startup_idle_promise()

        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher remains hidden on restart",
        )

    def test_vertical_tabs_default_hidden(self):
        # Verify that starting with verticalTabs enabled and default visibility false results in a visible
        # launcher with the vertical tabstrip
        self.restart_with_prefs(
            {
                "sidebar.revamp": True,
                "sidebar.verticalTabs": True,
                default_visible_pref: False,
            }
        )

        Wait(self.marionette).until(
            lambda _: self.is_launcher_visible(),
            message="Sidebar launcher should be initially visible",
        )
        tabsWidth = self.marionette.execute_script(
            """
            const window = BrowserWindowTracker.getTopWindow();
            return document.getElementById("vertical-tabs").getBoundingClientRect().width;
            """
        )
        self.assertGreater(tabsWidth, 0, "#vertical-tabs element has width")

        # switch to 'hide-sidebar' visibility mode and confirm the launcher becomes hidden
        self.marionette.set_pref("sidebar.visibility", "hide-sidebar")
        Wait(self.marionette).until(
            lambda _: not self.is_launcher_visible(),
            message="Sidebar launcher should become hidden when hide-sidebar visibility is set and defaultLauncherVisible is false",
        )
