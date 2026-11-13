# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import time
from urllib.parse import urlsplit

# add this directory to the path
sys.path.append(os.path.dirname(__file__))

from marionette_driver import Wait, errors
from session_store_test_case import SessionStoreTestCase, inline

# How long the blocker ties up its content process. Must outlast
# CRASH_DELAY_MS, and stay well under the loose timer (10s, see setUp) so the
# control's flush completes on its own.
BLOCKER_BUSY_MS = 5000

# Time to let the blocker start spinning before we quit.
BLOCKER_SETTLE_S = 0.3

# How long after being scheduled to crash the victim. Must land after the quit
# begins but while the blocker is still holding the flush open.
CRASH_DELAY_MS = 1000

# Logged by SessionStore when the ipc:content-shutdown observer wins the
# shutdown flush race, i.e. the crash landed inside the flush window.
ABNORMAL_OBSERVED_LOG = "Observed abnormal ipc:content-shutdown during shutdown"

# Navigated to by the blocker just before the quit. It spins as soon as it is
# parsed, so it is busy for the whole flush.
BLOCKER_BUSY_URL = inline(
    "<script>"
    f"const end = Date.now() + {BLOCKER_BUSY_MS};"
    " while (Date.now() < end) {}"
    "</script>"
)

# Each role gets its own site so that it lands in its own content process.
# network.dns.localDomains points these hosts at Marionette's fixture server.
BLOCKER_HOST = "example.net"
SURVIVOR_HOST = "example.org"
VICTIM_HOST = "example.com"


class TestShutdownFlushTabCrash(SessionStoreTestCase):
    """Crashes the victim tab while the blocker holds the shutdown flush open,
    then restarts and checks that the survivor's late navigation was still
    saved. The control runs the same steps without the crash."""

    def setUp(self):
        # enforce_gecko_prefs restarts with a clean profile and replaces the
        # pref set outright, so every pref goes in one call before any tabs
        # are opened.
        super().setUp(include_private=False, test_windows=set())
        self.marionette.enforce_gecko_prefs({
            "browser.startup.page": 3,
            "browser.sessionstore.restore_on_demand": False,
            # Content sends no session updates unless the parent asks via the
            # TabStateFlusher, so the survivor's late navigation is only
            # captured if the shutdown flush succeeds.
            "browser.sessionstore.debug.no_auto_updates": True,
            "browser.sessionstore.loglevel": "Debug",
            "browser.sessionstore.log.appender.file.logOnSuccess": True,
            # Puts the loose timer at DELAY_CRASH_MS - 10000 = 10s.
            "toolkit.asyncshutdown.crash_timeout": 20000,
            "network.dns.localDomains": ",".join([
                BLOCKER_HOST,
                SURVIVOR_HOST,
                VICTIM_HOST,
            ]),
        })
        self.marionette.set_context("chrome")

        port = urlsplit(self.marionette.absolute_url("")).port

        def url(host, path):
            return f"http://{host}:{port}/{path}"

        # Blocker: busy-loops its content process during shutdown so the
        # flush is still waiting when the victim crashes.
        self.blocker_url = url(BLOCKER_HOST, "empty.html")
        # Survivor: a healthy tab whose late navigation should be saved
        # despite the crash.
        self.survivor_url_early = url(SURVIVOR_HOST, "empty.html")
        self.survivor_url_late = url(SURVIVOR_HOST, "test.html")
        # Victim: the tab we crash, via about:crashcontent.
        self.victim_url = url(VICTIM_HOST, "empty.html")

        # A single window holding all three tabs.
        self.test_windows = set([
            (self.blocker_url, self.survivor_url_early, self.victim_url),
        ])
        self.all_windows = self.test_windows.copy()
        self.open_windows(self.test_windows)

    def tearDown(self):
        self.clear_minidumps()
        super().tearDown()

    def clear_minidumps(self):
        """Remove minidumps left by the intentional crash so Marionette's
        crash check doesn't fail the test. Returns how many were removed."""
        removed = 0
        profile_path = self.marionette.instance.profile.profile
        if profile_path is None:
            return removed
        dir_path = os.path.join(profile_path, "minidumps")
        if not os.path.isdir(dir_path):
            return removed
        for entry in os.scandir(dir_path):
            if entry.is_file():
                os.remove(entry.path)
                if entry.name.endswith(".dmp"):
                    removed += 1
        return removed

    def get_log_contents(self):
        profile_path = self.marionette.instance.profile.profile
        if profile_path is None:
            return ""
        dir_path = os.path.join(profile_path, "sessionstore-logs")
        if not os.path.isdir(dir_path):
            return ""
        contents = []
        for entry in os.scandir(dir_path):
            if entry.is_file():
                with open(entry.path) as f:
                    contents.append(f.read())
        return "\n".join(contents)

    # Finds the one tab, across all windows, whose URL starts with `prefix`.
    FIND_BROWSER_JS = """
        function findBrowser(prefix) {
          let seen = [];
          let found = [];
          for (let win of Services.wm.getEnumerator("navigator:browser")) {
            for (let browser of win.gBrowser.browsers) {
              let spec = browser.currentURI.spec;
              seen.push(spec);
              if (spec.startsWith(prefix)) {
                found.push(browser);
              }
            }
          }
          if (found.length != 1) {
            throw new Error(
              `Expected exactly one tab matching ${prefix}, got ` +
              `${found.length}. Saw: ${JSON.stringify(seen)}`
            );
          }
          return found[0];
        }
    """

    def get_content_child_ids(self):
        """Map each tab's URL to the childID of its content process."""
        with self.marionette.using_context("chrome"):
            return self.marionette.execute_script(
                """
                let result = {};
                for (let win of Services.wm.getEnumerator("navigator:browser")) {
                  for (let browser of win.gBrowser.browsers) {
                    result[browser.currentURI.spec] =
                      browser.browsingContext.currentWindowGlobal?.domProcess?.childID;
                  }
                }
                return result;
                """
            )

    def navigate_survivor_late(self):
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                self.FIND_BROWSER_JS
                + """
                let [early, late] = arguments;
                findBrowser(early).loadURI(Services.io.newURI(late), {
                  triggeringPrincipal:
                    Services.scriptSecurityManager.getSystemPrincipal(),
                });
                """,
                script_args=(self.survivor_url_early, self.survivor_url_late),
            )

    def schedule_crash(self):
        """Load about:crashcontent in the victim after CRASH_DELAY_MS. It only
        loads with the system principal, so this has to come from chrome.
        Timers still fire while session store spins the event loop waiting
        for flushes."""
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                self.FIND_BROWSER_JS
                + """
                let [prefix, delay] = arguments;
                let victim = findBrowser(prefix);
                // Rooted on the element so it isn't collected with this
                // sandbox before it fires.
                let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
                victim.__crashTimer = timer;
                timer.initWithCallback(
                  () => {
                    victim.loadURI(Services.io.newURI("about:crashcontent"), {
                      triggeringPrincipal:
                        Services.scriptSecurityManager.getSystemPrincipal(),
                    });
                  },
                  delay,
                  Ci.nsITimer.TYPE_ONE_SHOT
                );
                """,
                script_args=(self.victim_url, CRASH_DELAY_MS),
            )

    def start_blocker_spinning(self):
        """Select the blocker tab and navigate it to a page that spins as soon
        as it is parsed. It has to be the selected tab: background tabs defer
        the work until after shutdown has finished."""
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                self.FIND_BROWSER_JS
                + """
                let [prefix, url] = arguments;
                let blocker = findBrowser(prefix);
                let gBrowser = blocker.getTabBrowser();
                gBrowser.selectedTab = gBrowser.getTabForBrowser(blocker);
                blocker.loadURI(Services.io.newURI(url), {
                  triggeringPrincipal:
                    Services.scriptSecurityManager.getSystemPrincipal(),
                });
                """,
                script_args=(self.blocker_url, BLOCKER_BUSY_URL),
            )

    def quit_from_parent(self):
        with self.marionette.using_context("chrome"):
            self.marionette.execute_script(
                "Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);"
            )

    def survivor_late_restored(self):
        return any(self.survivor_url_late in urls for urls in self.restored)

    def wait_for_survivor(self):
        """Session restore is asynchronous, so poll for the survivor."""
        self.restored = None

        def check(_):
            self.restored = self.convert_open_windows_to_set()
            return self.survivor_late_restored()

        try:
            Wait(self.marionette, timeout=20, interval=0.5).until(check)
        except errors.TimeoutException:
            pass

    def run_shutdown_scenario(self, crash):
        child_ids = self.get_content_child_ids()
        self.assertEqual(len(child_ids), 3, msg=f"Expected 3 tabs, got {child_ids}.")
        self.assertEqual(
            len(set(child_ids.values())),
            3,
            msg=f"Each tab must be in its own content process. Got {child_ids}.",
        )

        self.navigate_survivor_late()
        self.start_blocker_spinning()
        time.sleep(BLOCKER_SETTLE_S)
        child_ids = self.get_content_child_ids()
        self.assertEqual(
            len(set(child_ids.values())),
            3,
            msg=(
                "The busy blocker must not share a process with the other "
                f"tabs. Got {child_ids}."
            ),
        )

        # Scheduled last, as the steps above are slow enough to let the crash
        # fire before the quit.
        if crash:
            self.schedule_crash()
        time.sleep(BLOCKER_SETTLE_S)

        quit_started = time.monotonic()
        self.marionette.quit(callback=self.quit_from_parent)
        quit_duration_s = time.monotonic() - quit_started

        # Read these before restarting, while the profile is still around.
        log_contents = self.get_log_contents()
        crash_dumps = self.clear_minidumps()
        if crash:
            self.assertGreater(
                crash_dumps,
                0,
                msg="No minidump was written, so the victim did not crash.",
            )
        else:
            self.assertEqual(crash_dumps, 0, msg="The control should not crash.")
            self.assertGreater(
                quit_duration_s,
                BLOCKER_BUSY_MS / 2000,
                msg=(
                    f"Shutdown took {quit_duration_s:.2f}s, so the blocker did "
                    "not hold the flush open."
                ),
            )
            self.assertNotIn(
                ABNORMAL_OBSERVED_LOG,
                log_contents,
                msg="The control should not have aborted the flush.",
            )

        self.marionette.start_session()
        self.marionette.set_context("chrome")
        self.wait_for_survivor()

    def test_control_no_crash(self):
        self.run_shutdown_scenario(crash=False)
        self.assertTrue(
            self.survivor_late_restored(),
            msg=f"Survivor should have been flushed. Restored: {self.restored}.",
        )

    def test_other_tabs_are_flushed_when_a_tab_crashes(self):
        self.run_shutdown_scenario(crash=True)
        self.assertTrue(
            self.survivor_late_restored(),
            msg=(
                "The survivor's late navigation should have been flushed "
                f"before shutdown. Restored: {self.restored}."
            ),
        )
