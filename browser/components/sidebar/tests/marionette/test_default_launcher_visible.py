# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from pathlib import Path

from marionette_driver import Wait
from marionette_harness import MarionetteTestCase
from mozfile import json


class TestDefaultLauncherVisible(MarionetteTestCase):
    def setUp(self):
        MarionetteTestCase.setUp(self)
        self.marionette.set_context("chrome")

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

    def restart_with_default_prefs(self, prefs, clean=False, in_app=True):
        pref_path = Path(self.marionette.profile_path) / "prefs.js"
        # shutdown the browser so we can update prefs while at rest and not trigger any pref observers
        self.marionette.quit(clean=clean, in_app=in_app)

        # remove any prefs with None as value
        remove_prefs = [
            f'user_pref("{name}"' for name, value in prefs.items() if value is None
        ]
        if len(remove_prefs) > 0:
            with open(pref_path, encoding="utf-8") as prefs_file:
                lines = prefs_file.readlines()
            keep_lines = [
                line for line in lines if not any(s in line for s in remove_prefs)
            ]
            with open(pref_path, "w", encoding="utf-8") as prefs_file:
                prefs_file.writelines(keep_lines)

        with open(pref_path, "a") as prefs_file:
            for name, value in prefs.items():
                prefs_file.write(f'user_pref("{name}", {json.dumps(value)});')
        self.marionette.start_session()

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

    def wait_for_sidebar_initialized(self):
        self.marionette.set_context("chrome")
        self.marionette.execute_async_script(
            """
            let resolve = arguments[0];
            let { BrowserInitState } = ChromeUtils.importESModule("resource:///modules/BrowserGlue.sys.mjs");

            (async () => {
                await BrowserInitState.startupIdleTaskPromise;
                const win = BrowserWindowTracker.getTopWindow();
                await win.SidebarController.promiseInitialized;
            })().then(resolve);
            """
        )

    def test_first_use_default_visible_pref_false(self):
        # We test with the default pre-148 pref values, then flip sidebar.revamp to true,
        # for a profile that has never enabled or seen the sidebar launcher.
        # We want to ensure the sidebar state is correctly persisted and restored

        self.restart_with_default_prefs({
            "sidebar.revamp": False,
            "browser.uiCustomization.state": None,
        })
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()

        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher is hidden",
        )
        self.assertFalse(
            self.is_button_visible(),
            "Sidebar toolbar button is hidden",
        )

        # Mimic an update which enables sidebar.revamp for the first time
        self.restart_with_default_prefs({
            "sidebar.revamp": True,
        })
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()

        # The sidebar-button is placed and rendered by CustomizableUI, which is
        # independent of the sidebar's initialization path. Just wait for it to show up.
        Wait(self.marionette).until(
            lambda _: self.is_button_visible(),
            message="Sidebar button should be visible",
        )

        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher is expected to be initially hidden when starting with sidebar.revamp",
        )

        # Click the button and verify that sticks
        self.click_toolbar_button()

        Wait(self.marionette).until(
            lambda _: self.is_launcher_visible(),
            message="Sidebar launcher should now be visible",
        )
        self.marionette.restart()
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()

        self.assertTrue(
            self.is_launcher_visible(),
            "Sidebar launcher remains visible because user showed it in the resumed session",
        )

    def test_new_sidebar_enabled_via_settings(self):
        self.restart_with_default_prefs({
            "sidebar.revamp": False,
            "browser.uiCustomization.state": None,
        })
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()
        self.assertFalse(
            self.marionette.get_pref("sidebar.revamp"),
            "Before enabling, sidebar.revamp pref should be false",
        )
        self.assertFalse(
            self.is_launcher_visible(),
            "Sidebar launcher is not visible",
        )
        self.assertFalse(
            self.is_button_visible(),
            "Sidebar toolbar button is not visible",
        )

        # Navigate to about:preferences and enable the new sidebar
        self.marionette.set_context("content")
        self.marionette.navigate("about:preferences")

        self.marionette.execute_script(
            """
            let el = document.getElementById("browserLayoutShowSidebar");
            el.click();
            """
        )

        self.marionette.set_context("chrome")
        self.assertTrue(
            self.marionette.get_pref("sidebar.revamp"),
            "The sidebar.revamp pref should now be true",
        )

        # We expect that to add the button to the toolbar
        Wait(self.marionette).until(
            lambda _: self.is_button_visible(),
            message="The toolbar button is visible",
        )

        # In this scenario, even when the defaultLauncherVisible is False, the launcher
        # should have been shown
        self.assertTrue(
            self.is_launcher_visible(),
            "The launcher is shown when revamp is enabled by the user",
        )

        # And it should stay visible on restart
        self.marionette.restart()
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()

        self.assertTrue(
            self.marionette.get_pref("sidebar.revamp"),
            "The sidebar.revamp pref should still be true",
        )

        self.assertTrue(
            self.is_launcher_visible(),
            "Sidebar launcher should still be shown after restart",
        )

    def test_vertical_tabs_default_hidden(self):
        # Verify initial sidebar launcher visibility when starting with:
        # - verticalTabs enabled, sidebar.visibility of always-show
        # - verticalTabs enabled, sidebar.visibility of hide-sidebar
        self.restart_with_default_prefs({
            "sidebar.revamp": True,
            "sidebar.verticalTabs": True,
            "sidebar.visibility": "always-show",
        })
        self.marionette.set_context("chrome")
        self.wait_for_sidebar_initialized()

        self.assertTrue(
            self.is_launcher_visible(),
            "Sidebar launcher should be initially visible",
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
            message="Sidebar launcher should become hidden when hide-sidebar visibility is set and defaultLauncherVisible2 is false",
        )
