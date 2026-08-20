import os
import time

from marionette_driver.errors import NoAlertPresentException
from marionette_harness import MarionetteTestCase


# Holds info about things we need to cleanup after the tests are done.
class PendingCleanup:
    desktop_backup_path = None
    reset_profile_path = None
    reset_profile_local_path = None

    def __init__(self, profile_name_to_remove):
        self.profile_name_to_remove = profile_name_to_remove


class TestFirefoxRefresh(MarionetteTestCase):
    _sandbox = "firefox-refresh"

    _username = "marionette-test-login"
    _password = "marionette-test-password"
    _bookmarkURL = "about:mozilla"
    _bookmarkText = "Some bookmark from Marionette"

    _cookieHost = "firefox-refresh.marionette-test.mozilla.org"
    _cookiePath = "some/cookie/path"
    _cookieName = "somecookie"
    _cookieValue = "some cookie value"

    _historyURL = "http://firefox-refresh.marionette-test.mozilla.org/"
    _historyTitle = "Test visit for Firefox Reset"

    _formHistoryFieldName = "some-very-unique-marionette-only-firefox-reset-field"
    _formHistoryValue = "special-pumpkin-value"

    _formAutofillAvailable = False
    _formAutofillAddressGuid = None

    _expectedURLs = ["about:robots", "about:mozilla"]
    _expectedOpenGroupID = "test-open-group"
    _expectedSavedGroups = ["test-saved-group"]

    def savePassword(self):
        self.runAsyncCode(
            """
          let [username, password, resolve] = arguments;
          let myLogin = new global.LoginInfo(
            "http://test.marionette.mozilla.com",
            "http://test.marionette.mozilla.com/some/form/",
            null,
            username,
            password,
            "username",
            "password"
          );
          Services.logins.addLoginAsync(myLogin)
            .then(() => resolve(false), resolve);
        """,
            script_args=(self._username, self._password),
        )

    def createBookmarkInMenu(self):
        error = self.runAsyncCode(
            """
          // let url = arguments[0];
          // let title = arguments[1];
          // let resolve = arguments[arguments.length - 1];
          let [url, title, resolve] = arguments;
          PlacesUtils.bookmarks.insert({
            parentGuid: PlacesUtils.bookmarks.menuGuid, url, title
          }).then(() => resolve(false), resolve);
        """,
            script_args=(self._bookmarkURL, self._bookmarkText),
        )
        if error:
            print(error)

    def createBookmarksOnToolbar(self):
        error = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          let children = [];
          for (let i = 1; i <= 5; i++) {
            children.push({url: `about:rights?p=${i}`, title: `Bookmark ${i}`});
          }
          PlacesUtils.bookmarks.insertTree({
            guid: PlacesUtils.bookmarks.toolbarGuid,
            children
          }).then(() => resolve(false), resolve);
        """
        )
        if error:
            print(error)

    def createHistory(self):
        error = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          PlacesUtils.history.insert({
            url: arguments[0],
            title: arguments[1],
            visits: [{
              date: new Date(Date.now() - 5000),
              referrer: "about:mozilla"
            }]
          }).then(() => resolve(false),
                  ex => resolve("Unexpected error in adding visit: " + ex));
        """,
            script_args=(self._historyURL, self._historyTitle),
        )
        if error:
            print(error)

    def createFormHistory(self):
        error = self.runAsyncCode(
            """
          let updateDefinition = {
            op: "add",
            fieldname: arguments[0],
            value: arguments[1],
            firstUsed: (Date.now() - 5000) * 1000,
          };
          let resolve = arguments[arguments.length - 1];
          global.FormHistory.update(updateDefinition).then(() => {
            resolve(false);
          }, error => {
            resolve("Unexpected error in adding formhistory: " + error);
          });
        """,
            script_args=(self._formHistoryFieldName, self._formHistoryValue),
        )
        if error:
            print(error)

    def createFormAutofill(self):
        if not self._formAutofillAvailable:
            return
        self._formAutofillAddressGuid = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          const TEST_ADDRESS_1 = {
            "given-name": "John",
            "additional-name": "R.",
            "family-name": "Smith",
            organization: "World Wide Web Consortium",
            "street-address": "32 Vassar Street\\\nMIT Room 32-G524",
            "address-level2": "Cambridge",
            "address-level1": "MA",
            "postal-code": "02139",
            country: "US",
            tel: "+15195555555",
            email: "user@example.com",
          };
          return global.formAutofillStorage.initialize().then(() => {
            return global.formAutofillStorage.addresses.add(TEST_ADDRESS_1);
          }).then(resolve);
        """
        )

    def createCookie(self):
        self.runCode(
            """
          // Expire in 15 minutes:
          let expireTime = Date.now() + 1000 * 15 * 60;
          Services.cookies.add(arguments[0], arguments[1], arguments[2], arguments[3],
                               true, false, false, expireTime, {},
                               Ci.nsICookie.SAMESITE_UNSET, Ci.nsICookie.SCHEME_UNSET);
        """,
            script_args=(
                self._cookieHost,
                self._cookiePath,
                self._cookieName,
                self._cookieValue,
            ),
        )

    def createSession(self):
        self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          const COMPLETE_STATE = Ci.nsIWebProgressListener.STATE_STOP +
                                 Ci.nsIWebProgressListener.STATE_IS_NETWORK;
          let { TabStateFlusher } = ChromeUtils.importESModule(
            "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
          );
          let expectedURLs = Array.from(arguments[0])
          let expectedOpenGroupID = arguments[1];
          gBrowser.addTabsProgressListener({
            onStateChange(browser, webprogress, request, flags, status) {
              try {
                request && request.QueryInterface(Ci.nsIChannel);
              } catch (ex) {}
              let uriLoaded = request.originalURI && request.originalURI.spec;
              if ((flags & COMPLETE_STATE == COMPLETE_STATE) && uriLoaded &&
                  expectedURLs.includes(uriLoaded)) {
                TabStateFlusher.flush(browser).then(function() {
                  expectedURLs.splice(expectedURLs.indexOf(uriLoaded), 1);
                  if (!expectedURLs.length) {
                    gBrowser.removeTabsProgressListener(this);
                    resolve();
                  }
                });
              }
            }
          });
          let expectedTabs = new Set();
          for (let url of expectedURLs) {
            expectedTabs.add(gBrowser.addTab(url, {
              triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
            }));
          }
          if (expectedOpenGroupID) {
            gBrowser.addTabGroup([gBrowser.tabs[gBrowser.tabs.length - 1]], { id: expectedOpenGroupID });
          }
          // Close any other tabs that might be open:
          let allTabs = Array.from(gBrowser.tabs);
          for (let tab of allTabs) {
            if (!expectedTabs.has(tab)) {
              gBrowser.removeTab(tab);
            }
          }
        """,  # NOQA: E501
            script_args=(self._expectedURLs, self._expectedOpenGroupID),
        )

        self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          let expectedSavedGroups = Array.from(arguments[0]);
          let { TabStateFlusher } = ChromeUtils.importESModule(
            "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
          );

          let savePromises = [];
          let groups = [];
          for (let id of expectedSavedGroups) {
            let tab = gBrowser.addTab("about:mozilla", {
              triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
            });
            let group = gBrowser.addTabGroup([tab], { id });
            groups.push(group);
            let groupSaved = new Promise(res => {
              window.addEventListener("TabGroupSaved", (e) => {
                if (e.target === group) {
                  res()
                }
              });
            });
            let groupRemoved = new Promise(res => {
              window.addEventListener("TabGroupRemoved", (e) => {
                if (e.target === group) {
                  res()
                }
              });
            });
            savePromises.push(Promise.allSettled([groupSaved, groupRemoved]));
          }
          TabStateFlusher.flushWindow(gBrowser.documentGlobal).then(() => {
            groups.forEach(group => {
              group.saveAndClose();
            })
            Promise.allSettled(savePromises).then(resolve);
          });

        """,
            script_args=(self._expectedSavedGroups,),
        )

        self.assertEqual(
            self.marionette.execute_script(
                """
                return SessionStore.savedGroups.length;
                """
            ),
            len(self._expectedSavedGroups),
            "Correct number of groups were saved",
        )

    def createFxa(self):
        # This script will write an entry to the login manager and create
        # a signedInUser.json in the profile dir.
        self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          let { FxAccountsStorageManager } = ChromeUtils.importESModule(
            "resource://gre/modules/FxAccountsStorage.sys.mjs"
          );
          let storage = new FxAccountsStorageManager();
          let data = {email: "test@test.com", uid: "uid", keyFetchToken: "top-secret"};
          storage.initialize(data);
          storage.finalize().then(resolve);
        """
        )

    def createSync(self):
        # This script will write the canonical preference which indicates a user
        # is signed into sync.
        self.marionette.execute_script(
            """
            Services.prefs.setStringPref("services.sync.username", "test@test.com");
        """
        )

    def checkPassword(self):
        loginInfo = self.runAsyncCode(
            """
          let [resolve] = arguments;
          Services.logins.searchLoginsAsync({
            origin: "http://test.marionette.mozilla.com",
            formActionOrigin: "http://test.marionette.mozilla.com",
          }).then(ary => resolve(ary.length ? ary : {username: "null", password: "null"}));
        """
        )
        self.assertEqual(len(loginInfo), 1)
        self.assertEqual(loginInfo[0]["username"], self._username)
        self.assertEqual(loginInfo[0]["password"], self._password)

        loginCount = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          Services.logins.getAllLogins().then(logins => resolve(logins.length));
        """
        )
        # Note that we expect 2 logins - one from us, one from sync.
        self.assertEqual(loginCount, 2, "No other logins are present")

    def checkBookmarkInMenu(self):
        titleInBookmarks = self.runAsyncCode(
            """
          let [url, resolve] = arguments;
          PlacesUtils.bookmarks.fetch({url}).then(
            bookmark => resolve(bookmark ? bookmark.title : ""),
            ex => resolve(ex)
          );
        """,
            script_args=(self._bookmarkURL,),
        )
        self.assertEqual(titleInBookmarks, self._bookmarkText)

    def checkBookmarkToolbarVisibility(self):
        toolbarVisible = self.marionette.execute_script(
            """
          const BROWSER_DOCURL = AppConstants.BROWSER_CHROME_URL;
          return Services.xulStore.getValue(BROWSER_DOCURL, "PersonalToolbar", "collapsed");
        """
        )
        if toolbarVisible == "":
            toolbarVisible = "false"
        self.assertEqual(toolbarVisible, "false")

    def checkHistory(self):
        historyResult = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          PlacesUtils.history.fetch(arguments[0]).then(pageInfo => {
            if (!pageInfo) {
              resolve("No visits found");
            } else {
              resolve(pageInfo);
            }
          }).catch(e => {
            resolve("Unexpected error in fetching page: " + e);
          });
        """,
            script_args=(self._historyURL,),
        )
        if type(historyResult) is str:
            self.fail(historyResult)
            return

        self.assertEqual(historyResult["title"], self._historyTitle)

    def checkFormHistory(self):
        formFieldResults = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          let results = [];
          global.FormHistory.search(["value"], {fieldname: arguments[0]})
            .then(resolve);
        """,
            script_args=(self._formHistoryFieldName,),
        )
        if type(formFieldResults) is str:
            self.fail(formFieldResults)
            return

        formFieldResultCount = len(formFieldResults)
        self.assertEqual(
            formFieldResultCount,
            1,
            f"Should have exactly 1 entry for this field, got {formFieldResultCount}",
        )
        if formFieldResultCount == 1:
            self.assertEqual(formFieldResults[0]["value"], self._formHistoryValue)

        formHistoryCount = self.runAsyncCode(
            """
          let [resolve] = arguments;
          global.FormHistory.count({}).then(resolve);
        """
        )
        self.assertEqual(
            formHistoryCount, 1, "There should be only 1 entry in the form history"
        )

    def checkFormAutofill(self):
        if not self._formAutofillAvailable:
            return

        formAutofillResults = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          return global.formAutofillStorage.initialize().then(() => {
            return global.formAutofillStorage.addresses.getAll()
          }).then(resolve);
        """,
        )
        if type(formAutofillResults) is str:
            self.fail(formAutofillResults)
            return

        formAutofillAddressCount = len(formAutofillResults)
        self.assertEqual(
            formAutofillAddressCount,
            1,
            f"Should have exactly 1 saved address, got {formAutofillAddressCount}",
        )
        if formAutofillAddressCount == 1:
            self.assertEqual(
                formAutofillResults[0]["guid"], self._formAutofillAddressGuid
            )

    def checkCookie(self):
        cookieInfo = self.runCode(
            """
          try {
            let cookies = Services.cookies.getCookiesFromHost(arguments[0], {});
            let cookie = null;
            for (let hostCookie of cookies) {
              // getCookiesFromHost returns any cookie from the BASE host.
              if (hostCookie.rawHost != arguments[0])
                continue;
              if (cookie != null) {
                return "more than 1 cookie! That shouldn't happen!";
              }
              cookie = hostCookie;
            }
            return {path: cookie.path, name: cookie.name, value: cookie.value};
          } catch (ex) {
            return "got exception trying to fetch cookie: " + ex;
          }
        """,
            script_args=(self._cookieHost,),
        )
        if not isinstance(cookieInfo, dict):
            self.fail(cookieInfo)
            return
        self.assertEqual(cookieInfo["path"], self._cookiePath)
        self.assertEqual(cookieInfo["value"], self._cookieValue)
        self.assertEqual(cookieInfo["name"], self._cookieName)

    def checkSession(self):
        tabURIs = self.runCode(
            """
          return [... gBrowser.browsers].map(b => b.currentURI && b.currentURI.spec)
        """
        )
        self.assertSequenceEqual(tabURIs, ["about:welcomeback"])

        # Dismiss modal dialog if any. This is mainly to dismiss the check for
        # default browser dialog if it shows up.
        try:
            alert = self.marionette.switch_to_alert()
            alert.dismiss()
        except NoAlertPresentException:
            pass

        tabURIs = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1]
          let mm = gBrowser.selectedBrowser.messageManager;

          window.addEventListener("SSWindowStateReady", function() {
            window.addEventListener("SSTabRestored", function() {
              resolve(Array.from(gBrowser.browsers, b => b.currentURI?.spec));
            }, { capture: false, once: true });
          }, { capture: false, once: true });

          let fs = function() {
            if (content.document.readyState === "complete") {
              content.document.getElementById("errorTryAgain").click();
            } else {
              content.window.addEventListener("load", function(event) {
                content.document.getElementById("errorTryAgain").click();
              }, { once: true });
            }
          };

          Services.prefs.setBoolPref("security.allow_parent_unrestricted_js_loads", true);
          mm.loadFrameScript("data:application/javascript,(" + fs.toString() + ")()", true);
          Services.prefs.setBoolPref("security.allow_parent_unrestricted_js_loads", false);
        """  # NOQA: E501
        )
        self.assertSequenceEqual(tabURIs, self._expectedURLs)

        if self._expectedOpenGroupID:
            groupExists = self.runCode(
                """
              let groupID = arguments[0];
              let group = gBrowser.getTabGroupById(groupID);
              return !!group;
            """,
                script_args=(self._expectedOpenGroupID,),
            )
            self.assertTrue(groupExists, "open group was restored")

        savedGroupIDs = self.runAsyncCode(
            """
          let resolve = arguments[arguments.length - 1];
          let groupIDs = [];
          for (let group of window.SessionStore.savedGroups) {
            groupIDs.push(group.id);
          }
          resolve(groupIDs);
        """,
            script_args=(self._expectedSavedGroups,),
        )
        self.assertSequenceEqual(savedGroupIDs, self._expectedSavedGroups)

    def checkFxA(self):
        result = self.runAsyncCode(
            """
          let { FxAccountsStorageManager } = ChromeUtils.importESModule(
            "resource://gre/modules/FxAccountsStorage.sys.mjs"
          );
          let resolve = arguments[arguments.length - 1];
          let storage = new FxAccountsStorageManager();
          let result = {};
          storage.initialize();
          storage.getAccountData().then(data => {
            result.accountData = data;
            return storage.finalize();
          }).then(() => {
            resolve(result);
          }).catch(err => {
            resolve(err.toString());
          });
        """
        )
        if type(result) is not dict:
            self.fail(result)
            return
        self.assertEqual(result["accountData"]["email"], "test@test.com")
        self.assertEqual(result["accountData"]["uid"], "uid")
        self.assertEqual(result["accountData"]["keyFetchToken"], "top-secret")

    def checkSync(self, expect_sync_user):
        pref_value = self.marionette.execute_script(
            """
            return Services.prefs.getStringPref("services.sync.username", null);
        """
        )
        expected_value = "test@test.com" if expect_sync_user else None
        self.assertEqual(pref_value, expected_value)

    def checkStartupMigrationStateCleared(self):
        result = self.runCode(
            """
          let { MigrationUtils } = ChromeUtils.importESModule(
            "resource:///modules/MigrationUtils.sys.mjs"
          );
          return MigrationUtils.isStartupMigration;
        """
        )
        self.assertFalse(result)

    def checkRefreshPromptDisabled(self):
        refreshPromptDisabled = self.runCode(
            """
          return Services.prefs.getStringPref("browser.disableResetPrompt", false);
      """
        )
        self.assertTrue(refreshPromptDisabled)

    def checkProfileSource(self, expected_source):
        result = self.runAsyncCode(
            """
            let resolve = arguments[arguments.length - 1]
            let { ProfileAge } = ChromeUtils.importESModule(
              "resource://gre/modules/ProfileAge.sys.mjs"
            );
            ProfileAge().then(age => age.source).then(resolve)
            """
        )
        self.assertEqual(result, expected_source)

    def checkProfile(self, has_migrated=False, expect_sync_user=True):
        self.checkPassword()
        self.checkBookmarkInMenu()
        self.checkHistory()
        self.checkFormHistory()
        self.checkFormAutofill()
        self.checkCookie()
        self.checkFxA()
        self.checkSync(expect_sync_user)
        self.checkProfileSource("reset" if has_migrated else "pre-reset")
        if has_migrated:
            self.checkBookmarkToolbarVisibility()
            self.checkSession()
            self.checkStartupMigrationStateCleared()
            self.checkRefreshPromptDisabled()

    def createProfileData(self):
        self.setProfileSource()
        self.savePassword()
        self.createBookmarkInMenu()
        self.createBookmarksOnToolbar()
        self.createHistory()
        self.createFormHistory()
        self.createFormAutofill()
        self.createCookie()
        self.createSession()
        self.createFxa()
        self.createSync()

    def setProfileSource(self):
        self.runAsyncCode(
            """
            let resolve = arguments[arguments.length - 1];
            let { ProfileAge } = ChromeUtils.importESModule(
              "resource://gre/modules/ProfileAge.sys.mjs"
            );

            ProfileAge().then(times => {
              times._times.source = "pre-reset";
              return times.writeTimes();
            }).then(resolve).catch(console.error);
            """
        )

    def setUpScriptData(self):
        self.marionette.set_context(self.marionette.CONTEXT_CHROME)
        self.runCode(
            """
          window.global = {};
          global.LoginInfo = Components.Constructor("@mozilla.org/login-manager/loginInfo;1", "nsILoginInfo", "init");
          global.profSvc = Cc["@mozilla.org/toolkit/profile-service;1"].getService(Ci.nsIToolkitProfileService);
          global.Preferences = ChromeUtils.importESModule(
            "resource://gre/modules/Preferences.sys.mjs"
          ).Preferences;
          global.FormHistory = ChromeUtils.importESModule(
            "resource://gre/modules/FormHistory.sys.mjs"
          ).FormHistory;
        """  # NOQA: E501
        )
        self._formAutofillAvailable = self.runCode(
            """
          try {
            global.formAutofillStorage = ChromeUtils.importESModule(
              "resource://formautofill/FormAutofillStorage.sys.mjs"
            ).formAutofillStorage;
          } catch(e) {
            return false;
          }
          return true;
        """  # NOQA: E501
        )

    def runCode(self, script, *args, **kwargs):
        return self.marionette.execute_script(
            script, new_sandbox=False, sandbox=self._sandbox, *args, **kwargs
        )

    def runAsyncCode(self, script, *args, **kwargs):
        return self.marionette.execute_async_script(
            script, new_sandbox=False, sandbox=self._sandbox, *args, **kwargs
        )

    def setUp(self):
        MarionetteTestCase.setUp(self)
        self.setUpScriptData()

        self.cleanups = []

    def tearDown(self):
        # Force yet another restart with a clean profile to disconnect from the
        # profile and environment changes we've made, to leave a more or less
        # blank slate for the next person.
        self.marionette.restart(in_app=False, clean=True)
        self.setUpScriptData()

        # Super
        MarionetteTestCase.tearDown(self)

        # A helper to deal with removing a load of files
        import mozfile

        for cleanup in self.cleanups:
            if cleanup.desktop_backup_path:
                mozfile.remove(cleanup.desktop_backup_path)

            if cleanup.reset_profile_path:
                # Remove ourselves from profiles.ini
                self.runCode(
                    """
                  let name = arguments[0];
                  let profile = global.profSvc.getProfileByName(name);
                  profile.remove(false)
                  global.profSvc.flush();
                """,
                    script_args=(cleanup.profile_name_to_remove,),
                )
                # Remove the local profile dir if it's not the same as the profile dir:
                different_path = (
                    cleanup.reset_profile_local_path != cleanup.reset_profile_path
                )
                if cleanup.reset_profile_local_path and different_path:
                    mozfile.remove(cleanup.reset_profile_local_path)

                # And delete all the files.
                mozfile.remove(cleanup.reset_profile_path)

    def doReset(self):
        profileName = "marionette-test-profile-" + str(int(time.time() * 1000))
        cleanup = PendingCleanup(profileName)
        self.runCode(
            """
          // Ensure the current (temporary) profile is in profiles.ini:
          let profD = Services.dirsvc.get("ProfD", Ci.nsIFile);
          let profileName = arguments[1];
          let myProfile = global.profSvc.createProfile(profD, profileName, "pre-reset");
          global.profSvc.flush()

          // Now add the reset parameters:
          let prefsToKeep = Array.from(Services.prefs.getChildList("marionette."));
          // Add all the modified preferences set from geckoinstance.py to avoid
          // non-local connections.
          prefsToKeep = prefsToKeep.concat(JSON.parse(
              Services.env.get("MOZ_MARIONETTE_REQUIRED_PREFS")));
          let prefObj = {};
          for (let pref of prefsToKeep) {
            prefObj[pref] = global.Preferences.get(pref);
          }
          Services.env.set("MOZ_MARIONETTE_PREF_STATE_ACROSS_RESTARTS", JSON.stringify(prefObj));
          Services.env.set("MOZ_RESET_PROFILE_RESTART", "1");
          Services.env.set("XRE_PROFILE_PATH", arguments[0]);
        """,
            script_args=(
                self.marionette.instance.profile.profile,
                profileName,
            ),
        )

        profileLeafName = os.path.basename(
            os.path.normpath(self.marionette.instance.profile.profile)
        )

        # Now restart the browser to get it reset:
        self.marionette.restart(clean=False, in_app=True)
        self.setUpScriptData()

        # Determine the new profile path (we'll need to remove it when we're done)
        [cleanup.reset_profile_path, cleanup.reset_profile_local_path] = self.runCode(
            """
          let profD = Services.dirsvc.get("ProfD", Ci.nsIFile);
          let localD = Services.dirsvc.get("ProfLD", Ci.nsIFile);
          return [profD.path, localD.path];
        """
        )

        # Determine the backup path
        cleanup.desktop_backup_path = self.runCode(
            """
          let container;
          try {
            container = Services.dirsvc.get("Desk", Ci.nsIFile);
          } catch (ex) {
            container = Services.dirsvc.get("Home", Ci.nsIFile);
          }
          let bundle = Services.strings.createBundle("chrome://mozapps/locale/profile/profileSelection.properties");
          let dirName = bundle.formatStringFromName("resetBackupDirectory", [Services.appinfo.name]);
          container.append(dirName);
          container.append(arguments[0]);
          return container.path;
        """,  # NOQA: E501
            script_args=(profileLeafName,),
        )

        self.assertTrue(
            os.path.isdir(cleanup.reset_profile_path),
            "Reset profile path should be present",
        )
        self.assertTrue(
            os.path.isdir(cleanup.desktop_backup_path),
            "Backup profile path should be present",
        )
        self.assertIn(cleanup.profile_name_to_remove, cleanup.reset_profile_path)
        return cleanup

    def testResetEverything(self):
        self.createProfileData()

        self.checkProfile(expect_sync_user=True)

        this_cleanup = self.doReset()
        self.cleanups.append(this_cleanup)

        # Now check that we're doing OK...
        self.checkProfile(has_migrated=True, expect_sync_user=True)

    def testFxANoSync(self):
        # This test doesn't need to repeat all the non-sync tests...
        # Setup FxA but *not* sync
        self.createFxa()

        self.checkFxA()
        self.checkSync(False)

        this_cleanup = self.doReset()
        self.cleanups.append(this_cleanup)

        self.checkFxA()
        self.checkSync(False)


class TestSelectableProfileFirefoxRefresh(TestFirefoxRefresh):
    def runAsync(self, script, *args, **kwargs):
        wrappedScript = """
            let args = Array.from(arguments);
            let outerResolve = args.pop();
            (async () => {
                try {
                    let fn = async () => {

            """
        wrappedScript += f"{script}"
        wrappedScript += """

                    };
                    let innerResults = await fn(args);
                    if (!Array.isArray(innerResults)) {
                        innerResults = [innerResults];
                    }
                    innerResults.unshift("OK");
                    return innerResults;
                } catch(ex) {
                    return ["ERROR", ex.name, ex.message, ex.stack];
                }
            })().then(outerResolve);
        """

        results = self.runAsyncCode(wrappedScript, *args, **kwargs)
        self.assertEqual("OK", results[0], "something went wrong: " + str(results))
        del results[0]
        return results

    def check_selectable_profile_service(self):
        pref_value = self.runCode(
            """
            return Services.prefs.getStringPref("toolkit.profiles.storeID", null);
        """,
            script_args=(self.marionette.instance.profile.profile,),
        )

        self.assertEqual(pref_value, self.storeID)

        [profile_path, profile_name, storeID, profile_count, profile_id] = (
            self.runAsync(
                """
                let profileCount = (await SelectableProfileService.getAllProfiles()).length;
                let profile = SelectableProfileService.currentProfile;

                return [profile.path, profile.name, SelectableProfileService.storeID, profileCount, profile.id];
            """,
                script_args=(self.marionette.instance.profile.profile,),
            )
        )

        self.assertNotEqual(
            profile_path, self.profile_path
        )  # The path should be different after migration
        self.assertEqual(profile_name, self.profile_name)
        self.assertEqual(storeID, self.storeID)
        self.assertEqual(profile_count, self.profile_count)
        self.assertEqual(profile_id, self.profile_id)

    def doReset(self):
        profileName = "marionette-test-profile-" + str(int(time.time() * 1000))
        cleanup = PendingCleanup(profileName)

        self.runCode(
            """
          // Ensure the current (temporary) profile is in profiles.ini:
          let profD = Services.dirsvc.get("ProfD", Ci.nsIFile);
          let profileName = arguments[1];
          let myProfile = global.profSvc.createProfile(profD, profileName, "pre-reset");
          global.profSvc.flush()

          // Now add the reset parameters:
          let prefsToKeep = Array.from(Services.prefs.getChildList("marionette."));
          // Add all the modified preferences set from geckoinstance.py to avoid
          // non-local connections.
          prefsToKeep = prefsToKeep.concat(JSON.parse(
              Services.env.get("MOZ_MARIONETTE_REQUIRED_PREFS")));
          let prefObj = {};
          for (let pref of prefsToKeep) {
            prefObj[pref] = global.Preferences.get(pref);
          }
          Services.env.set("MOZ_MARIONETTE_PREF_STATE_ACROSS_RESTARTS", JSON.stringify(prefObj));
        """,
            script_args=(
                self.marionette.instance.profile.profile,
                profileName,
            ),
        )
        # restart so we can create some profiles
        self.marionette.restart(clean=False, in_app=True)

        [profile_path, profile_name, storeID, profile_count, profile_id] = (
            self.runAsync(
                """
                let newProfile = await SelectableProfileService.createNewProfile(false, null, "tests");

                let profileCount = (await SelectableProfileService.getAllProfiles()).length;

                let profile = SelectableProfileService.currentProfile;

                return [profile.path, profile.name, SelectableProfileService.storeID, profileCount, profile.id];
        """,
                script_args=(self.marionette.instance.profile.profile,),
            )
        )

        self.profile_path = profile_path
        self.profile_name = profile_name
        self.storeID = storeID
        self.profile_count = profile_count
        self.profile_id = profile_id

        self.runCode(
            """
          Services.env.set("SELECTABLE_PROFILE_RESET_PATH", SelectableProfileService?.currentProfile.path);
          Services.env.set("SELECTABLE_PROFILE_RESET_STORE_ID", SelectableProfileService?.storeID);
          Services.env.set("MOZ_RESET_PROFILE_RESTART", "1");
          Services.env.set("XRE_PROFILE_PATH", arguments[0]);
        """,
            script_args=(
                self.marionette.instance.profile.profile,
                profileName,
            ),
        )

        profileLeafName = os.path.basename(
            os.path.normpath(self.marionette.instance.profile.profile)
        )

        # Now restart the browser to get it reset:
        self.marionette.restart(clean=False, in_app=True)
        self.setUpScriptData()

        # Determine the new profile path (we'll need to remove it when we're done)
        [cleanup.reset_profile_path, cleanup.reset_profile_local_path] = self.runCode(
            """
          let profD = Services.dirsvc.get("ProfD", Ci.nsIFile);
          let localD = Services.dirsvc.get("ProfLD", Ci.nsIFile);
          return [profD.path, localD.path];
        """
        )

        # Determine the backup path
        cleanup.desktop_backup_path = self.runCode(
            """
          let container;
          try {
            container = Services.dirsvc.get("Desk", Ci.nsIFile);
          } catch (ex) {
            container = Services.dirsvc.get("Home", Ci.nsIFile);
          }
          let bundle = Services.strings.createBundle("chrome://mozapps/locale/profile/profileSelection.properties");
          let dirName = bundle.formatStringFromName("resetBackupDirectory", [Services.appinfo.name]);
          container.append(dirName);
          container.append(arguments[0]);
          return container.path;
        """,  # NOQA: E501
            script_args=(profileLeafName,),
        )

        self.assertTrue(
            os.path.isdir(cleanup.reset_profile_path),
            "Reset profile path should be present",
        )
        self.assertTrue(
            os.path.isdir(cleanup.desktop_backup_path),
            "Backup profile path should be present",
        )
        self.assertIn(cleanup.profile_name_to_remove, cleanup.reset_profile_path)
        return cleanup

    def checkProfile(self, has_migrated=False, expect_sync_user=True):
        self.checkPassword()
        self.checkBookmarkInMenu()
        self.checkHistory()
        self.checkFormHistory()
        self.checkFormAutofill()
        self.checkCookie()
        self.checkFxA()
        self.checkSync(expect_sync_user)
        if has_migrated:
            self.checkBookmarkToolbarVisibility()
            self.checkSession()
            self.checkStartupMigrationStateCleared()
            self.checkRefreshPromptDisabled()
