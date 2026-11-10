# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# add this directory to the path
import os
import sys

sys.path.append(os.path.dirname(__file__))

from session_store_test_case import SessionStoreTestCase

# An XSMP session manager (ksmserver on KDE Plasma) sends SaveYourself at
# logout, which nsNativeAppSupportUnix turns into the "session-save"
# notification. We test the following combinations:
# - Start page = restore session, restarted manually (restore)
# - Start page = home
#   - restarted by the session manager with --sm-client-id (restore)
#   - restarted manually (no restore)


class SessionManagerLogoutTestCase(SessionStoreTestCase):
    def simulate_session_manager_logout(self):
        self.marionette.execute_script(
            """
            let didSaveSession = Cc["@mozilla.org/supports-PRBool;1"].createInstance(
              Ci.nsISupportsPRBool
            );
            Services.obs.notifyObservers(didSaveSession, "session-save");
            """
        )

    def logout_with_variety(self, restart_by_session_manager, expect_restore):
        current_windows_set = self.convert_open_windows_to_set()
        self.assertEqual(
            current_windows_set,
            self.all_windows,
            msg=f"Not all requested windows have been opened. Expected {self.all_windows}, got {current_windows_set}.",
        )

        self.simulate_session_manager_logout()
        self.marionette.quit()

        saved_args = self.marionette.instance.app_args
        try:
            if restart_by_session_manager:
                self.marionette.instance.app_args = ["--sm-client-id", "test"]

            self.marionette.start_session()
            self.marionette.set_context("chrome")
        finally:
            self.marionette.instance.app_args = saved_args

        if expect_restore:
            self.wait_for_windows(
                self.test_windows,
                "Non private browsing windows should have been restored",
            )
        else:
            self.assertEqual(
                len(self.marionette.chrome_window_handles),
                1,
                msg="Windows from last session shouldn`t have been restored.",
            )
            self.assertEqual(
                len(self.marionette.window_handles),
                1,
                msg="Tabs from last session shouldn`t have been restored.",
            )


class TestSessionManagerLogoutRestoreSession(SessionManagerLogoutTestCase):
    def setUp(self):
        super().setUp(startup_page=3)

    def test_manual_restart(self):
        """Test session restore selected by user."""
        self.logout_with_variety(restart_by_session_manager=False, expect_restore=True)


class TestSessionManagerLogoutNormal(SessionManagerLogoutTestCase):
    def setUp(self):
        super().setUp()

    def test_session_manager_restart(self):
        """Test that a session manager restart restores the session."""
        self.logout_with_variety(restart_by_session_manager=True, expect_restore=True)

    def test_manual_restart(self):
        """Test that a session manager logout is ignored on manual start."""
        self.logout_with_variety(restart_by_session_manager=False, expect_restore=False)
