# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import time
from unittest.util import safe_repr

from marionette_driver.by import By
from marionette_driver.keys import Keys
from marionette_harness import MarionetteTestCase

# This list shouldn't exist!
# DO NOT ADD NEW EXCEPTIONS HERE! (unless they are specifically caused by
# being run under marionette rather than in a "real" profile, or only occur
# for browser developers)
# The only reason this exists is that when this test was written we already
# created a bunch of errors on startup, and it wasn't feasible to fix all
# of them before landing the test.
known_errors = [
    {
        # From Remote settings, because it's intercepted by our test
        # infrastructure which serves text/plain rather than JSON.
        # Even if we fixed that we'd probably see a different error,
        # unless we mock a full-blown remote settings server in the
        # test infra, which doesn't seem worth it.
        # Either way this wouldn't happen on "real" profiles.
        "message": 'Error: Unexpected content-type "text/plain',
        "filename": "RemoteSettingsClient",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # The browser toolbox shortcut is not portable.
        "message": "key_browserToolbox",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # The developer-only restart shortcut is not portable.
        "message": "key_quickRestart",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # The reader mode shortcut is not portable on Linux.
        # Bug 1825431 to fix this.
        "message": "key_toggleReaderMode",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # Bug 1936426 to reconsider warning as we want ctrl-alt-x for chatbot.
        "message": "viewGenaiChatSidebarKb",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # Bug 1936426 to reconsider warning as we want ctrl-alt-u for open tabs.
        "message": "viewOpenTabsSidebarKb",
    },
    {
        # Triggered as soon as anything tries to use shortcut keys.
        # Bug 1936426 to reconsider warning as we want ctrl-z / ctrl-alt-z
        # for sidebar.
        "message": "toggleSidebarKb",
    },
]

# Same rules apply here - please don't add anything! - but headless runs
# produce more errors that aren't normal in regular runs, so we've separated
# them out.
headless_errors = [{"message": "TelemetryEnvironment::_isDefaultBrowser"}]


class TestNoErrorsNewProfile(MarionetteTestCase):
    def setUp(self):
        super(MarionetteTestCase, self).setUp()

        self.maxDiff = None
        self.marionette.set_context("chrome")

        # Create a fresh profile.
        self.marionette.restart(in_app=False, clean=True)

    def ensure_proper_startup(self):
        # First wait for the browser to settle:
        self.marionette.execute_async_script(
            """
            let resolve = arguments[0];
            let { BrowserInitState } = ChromeUtils.importESModule("moz-src:///browser/components/BrowserGlue.sys.mjs");
            let promises = [
              BrowserInitState.startupIdleTaskPromise,
              gBrowserInit.idleTasksFinished.promise,
            ];
            Promise.all(promises).then(resolve);
            """
        )

        if self.marionette.session_capabilities["platformName"] == "mac":
            self.mod_key = Keys.META
        else:
            self.mod_key = Keys.CONTROL
        # Focus the URL bar by keyboard
        url_bar = self.marionette.execute_script("return gURLBar.inputField")
        url_bar.send_keys(self.mod_key, "l")
        # and open a tab by mouse:
        new_tab_button = self.marionette.find_element(By.ID, "new-tab-button")
        new_tab_button.click()

        # Wait a bit more for async tasks to complete...
        time.sleep(5)

    def get_all_errors(self):
        return self.marionette.execute_async_script(
            """
            let resolve = arguments[0];
            // Get all the messages from the console service,
            // and then get all of the ones from the console API storage.
            let msgs = Services.console.getMessageArray();

            const ConsoleAPIStorage = Cc[
              "@mozilla.org/consoleAPI-storage;1"
            ].getService(Ci.nsIConsoleAPIStorage);
            const getCircularReplacer = () => {
              const seen = new WeakSet();
              return (key, value) => {
                if (typeof value === "object" && value !== null) {
                  if (seen.has(value)) {
                    return "<circular ref>";
                  }
                  seen.add(value);
                }
                return value;
              };
            };
            // Take cyclical values out, add a simplified 'message' prop
            // that matches how things work for the console service objects.
            const consoleApiMessages = ConsoleAPIStorage.getEvents().map(ev => {
              let rv;
              try {
                rv = structuredClone(ev);
              } catch (ex) {
                rv = JSON.parse(JSON.stringify(ev, getCircularReplacer()));
              }
              delete rv.wrappedJSObject;
              rv.message = ev.arguments.join(", ");
              return rv;
            });
            resolve(msgs.concat(consoleApiMessages));
            """
        )

    def should_ignore_error(self, error):
        if not "message" in error:
            print("Unparsable error:")
            print(safe_repr(error))
            return False

        error_filename = error.get("filename", "")
        error_msg = error["message"]
        headless = self.marionette.session_capabilities["moz:headless"]
        all_known_errors = known_errors + (headless_errors if headless else [])

        for known_error in all_known_errors:
            known_filename = known_error.get("filename", "")
            known_msg = known_error["message"]
            if known_msg in error_msg and known_filename in error_filename:
                print(
                    "Known error seen: %s (%s)"
                    % (error["message"], error.get("filename", "no filename"))
                )
                return True

        return False

    def short_error_display(self, errors):
        rv = []
        for error in errors:
            rv += [
                {
                    "message": error.get("message", "No message!?"),
                    "filename": error.get("filename", "No filename!?"),
                }
            ]
        return rv

    def test_no_errors(self):
        self.ensure_proper_startup()
        errors = self.get_all_errors()
        errors[:] = [error for error in errors if not self.should_ignore_error(error)]
        if len(errors) > 0:
            print("Unexpected errors encountered:")
            # Hack to get nice printing:
            for error in errors:
                print(safe_repr(error))
        self.assertEqual(self.short_error_display(errors), [])
