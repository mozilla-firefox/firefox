# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import time

from marionette_driver import By, Wait, expected
from marionette_harness import MarionetteTestCase, WindowManagerMixin


class TestSafeBrowsingNotificationBar(WindowManagerMixin, MarionetteTestCase):
    def setUp(self):
        super().setUp()

        self.test_data = [
            # Unwanted software URL
            {"unsafe_page": "https://www.itisatrap.org/firefox/unwanted.html"},
            # Phishing URL info
            {"unsafe_page": "https://www.itisatrap.org/firefox/its-a-trap.html"},
            # Malware URL object
            {"unsafe_page": "https://www.itisatrap.org/firefox/its-an-attack.html"},
        ]

        self.default_homepage = self.marionette.get_pref("browser.startup.homepage")

        self.marionette.set_pref("browser.safebrowsing.phishing.enabled", True)
        self.marionette.set_pref("browser.safebrowsing.malware.enabled", True)

        # Give the browser a little time, because SafeBrowsing.sys.mjs takes a while
        # between start up and adding the example urls to the db.
        # hg.mozilla.org/mozilla-central/file/46aebcd9481e/browser/base/content/browser.js#l1194
        time.sleep(3)

        # Run this test in a new tab.
        new_tab = self.open_tab()
        self.marionette.switch_to_window(new_tab)

    def tearDown(self):
        try:
            self.marionette.clear_pref("browser.safebrowsing.phishing.enabled")
            self.marionette.clear_pref("browser.safebrowsing.malware.enabled")

            self.remove_permission("https://www.itisatrap.org", "safe-browsing")
            self.close_all_tabs()
        finally:
            super().tearDown()

    def test_notification_bar(self):
        for item in self.test_data:
            unsafe_page = item["unsafe_page"]

            # Return to the unsafe page
            # Check "ignore warning" link then notification bar's "get me out" button
            self.marionette.navigate(unsafe_page)
            # Wait for the DOM to receive events for about:blocked
            time.sleep(1)
            self.check_ignore_warning_link(unsafe_page)
            self.check_get_me_out_of_here_button()

            # Return to the unsafe page
            # Check "ignore warning" link then notification bar's "X" button
            self.marionette.navigate(unsafe_page)
            # Wait for the DOM to receive events for about:blocked
            time.sleep(1)
            self.check_ignore_warning_link(unsafe_page)
            self.check_x_button()

    def get_final_url(self, url):
        self.marionette.navigate(url)
        return self.marionette.get_url()

    def remove_permission(self, host, permission):
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                """
              let uri = Services.io.newURI(arguments[0], null, null);
              let principal = Services.scriptSecurityManager.createContentPrincipal(uri, {});
              Services.perms.removeFromPrincipal(principal, arguments[1]);
            """,
                script_args=[host, permission],
            )

    def check_ignore_warning_link(self, unsafe_page):
        button = self.marionette.find_element(By.ID, "seeDetailsButton")
        button.click()
        time.sleep(1)
        link = self.marionette.find_element(By.ID, "ignore_warning_link")
        link.click()

        Wait(self.marionette, timeout=self.marionette.timeout.page_load).until(
            expected.element_present(By.ID, "main-feature"),
            message='Expected target element "#main-feature" has not been found',
        )
        self.assertEqual(self.marionette.get_url(), self.get_final_url(unsafe_page))

        # Clean up here since the permission gets set in this function
        self.remove_permission("https://www.itisatrap.org", "safe-browsing")

    def check_get_me_out_of_here_button(self):
        with self.marionette.using_context("chrome"):
            notification_box = self.marionette.find_element(
                By.CSS_SELECTOR, 'vbox.notificationbox-stack[slot="selected"]'
            )
            message = notification_box.find_element(
                By.CSS_SELECTOR, "notification-message"
            )
            button_container = message.get_property("buttonContainer")
            button = button_container.find_element(
                By.CSS_SELECTOR, '.notification-button[label="Get me out of here!"]'
            )
            button.click()

        Wait(self.marionette, timeout=self.marionette.timeout.page_load).until(
            lambda mn: self.default_homepage in mn.get_url(),
            message="The default home page has not been loaded",
        )

    def check_x_button(self):
        with self.marionette.using_context("chrome"):
            notification_box = self.marionette.find_element(
                By.CSS_SELECTOR, 'vbox.notificationbox-stack[slot="selected"]'
            )
            message = notification_box.find_element(
                By.CSS_SELECTOR, "notification-message[value=blocked-badware-page]"
            )
            button = message.get_property("closeButton")
            button.click()

            Wait(self.marionette, timeout=self.marionette.timeout.page_load).until(
                expected.element_stale(button),
                message="The notification bar has not been closed",
            )
