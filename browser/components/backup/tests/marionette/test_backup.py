# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import filecmp
import os
import shutil
import tempfile

import mozfile
from marionette_harness import MarionetteTestCase


class BackupTest(MarionetteTestCase):
    # This is the DB key that will be computed for the http2-ca.pem certificate
    # that's included in a support-file for this test.
    _cert_db_key = "AAAAAAAAAAAAAAAUAAAAG0Wbze8lahTcE4RhwEqMtTpThrzjMBkxFzAVBgNVBAMMDiBIVFRQMiBUZXN0IENB"

    def setUp(self):
        MarionetteTestCase.setUp(self)

        # Profile backup is disabled while SQLite at-rest encryption is on, so
        # the backup feature under test is unavailable then. We cannot just
        # force the pref off: under a global encryption-on build the initial
        # session already created an encrypted profile, and restarting with
        # encryption off cannot reopen it. Skip instead -- on the shipping
        # (encryption-off) configuration this is a no-op and the test runs.
        if self.marionette.get_pref("security.storage.encryption.sqlite.enabled"):
            self.skipTest(
                "Profile backup is disabled when SQLite at-rest encryption is enabled"
            )

        # We need to force the service to be enabled because it's disabled
        # by default for Marionette. Also "browser.backup.log" has to be set
        # to true before Firefox starts in order for it to be displayed.
        self.marionette.enforce_gecko_prefs({
            "browser.backup.enabled": True,
            "browser.backup.log": True,
            "browser.backup.archive.enabled": True,
            "browser.backup.restore.enabled": True,
            # Necessary to test Session Restore from backup, which relies on
            # the crash restore mechanism.
            "browser.sessionstore.resume_from_crash": True,
            "browser.newtabpage.activity-stream.testing.shouldInitializeFeeds": True,
            # Prevent WallpaperFeed from fetching Remote Settings attachments
            # from the CDN, which is blocked in CI test environments.
            "browser.newtabpage.activity-stream.newtabWallpapers.enabled": False,
        })

        self.marionette.set_context("chrome")

    def tearDown(self):
        # Restart Firefox with a new profile to get rid from all modifications.
        self.marionette.quit()
        self.marionette.instance.switch_profile()
        self.marionette.start_session()

        MarionetteTestCase.tearDown(self)

    def test_backup(self):
        self.add_test_cookie()
        self.add_test_login()
        self.add_test_certificate()
        self.add_test_saved_address()
        self.add_test_identity_credential()
        self.add_test_form_history()
        self.add_test_asrouter_snippets_data()
        self.add_test_protections_data()
        self.add_test_bookmarks()
        self.add_test_history()
        self.add_test_preferences()
        self.add_test_permissions()
        self.add_test_newtab_wallpaper()

        # We want to make sure that any payment methods in this testing profile
        # are properly encrypted using OSKeyStore, and that the encrypted
        # backup will properly extract and recover from the original OSKeyStore
        # secret.
        #
        # What we _don't_ want to do is encrypt or extract the OSKeyStore secret
        # used by this machine's _actual_ Firefox instance, if one exists
        # (since they're all shared). We also definitely do not want to
        # accidentally overwrite that secret.
        #
        # We solve this by poking a new STORE_LABEL value into the OSKeyStore
        # module before we do the following:
        #
        # 1. Store payment methods
        # 2. Enable encryption
        #
        # Once that'd one, we delete the temporary OSKeyStore row that we
        # created. This technique is similar to the one used in
        # OSKeyStoreTestUtils, which is unfortunately not a module that is
        # available to Marionette tests.
        backupOSKeyStoreLabel = self.marionette.execute_script(
            """
          const { OSKeyStore } = ChromeUtils.importESModule(
            "resource://gre/modules/OSKeyStore.sys.mjs"
          );

          const BACKUP_OSKEYSTORE_LABEL = "test-" + Math.random().toString(36).substr(2);
          OSKeyStore.STORE_LABEL = BACKUP_OSKEYSTORE_LABEL;
          return BACKUP_OSKEYSTORE_LABEL;
        """
        )

        # Now that we've got the fake OSKeyStore set up, we can insert our
        # testing payment methods.
        self.add_test_payment_methods()

        # Restart the browser to force all of the test data we just added
        # to be flushed to disk and to be made ready for backup
        self.marionette.restart()

        # We want to validate that TabState is flushed before serializing the
        # backup, so run this test in the same browser instance we invoke the
        # backup in.
        self.add_test_sessionstore()

        # Put the OSKeyStore label back, since it would have been cleared
        # from memory during the restart.
        self.marionette.execute_script(
            """
          const { OSKeyStore } = ChromeUtils.importESModule(
            "resource://gre/modules/OSKeyStore.sys.mjs"
          );

          const BACKUP_OSKEYSTORE_LABEL = arguments[0];
          OSKeyStore.STORE_LABEL = BACKUP_OSKEYSTORE_LABEL;
        """,
            script_args=[backupOSKeyStoreLabel],
        )

        archiveDestPath = os.path.join(tempfile.gettempdir(), "backup-dest")
        recoveryCode = "This is a test password"
        archivePath = self.marionette.execute_async_script(
            """

          const { BackupService } = ChromeUtils.importESModule("resource:///modules/backup/BackupService.sys.mjs");
          let bs = BackupService.init();
          if (!bs) {
            throw new Error("Could not get initialized BackupService.");
          }

          let [archiveDestPath, recoveryCode, outerResolve] = arguments;
          (async () => {
            await bs.setParentDirPath(archiveDestPath);
            await bs.enableEncryption(recoveryCode);

            let { archivePath } = await bs.createBackup();
            if (!archivePath) {
              throw new Error("Could not create backup.");
            }
            return archivePath;
          })().then(outerResolve);
        """,
            script_args=[archiveDestPath, recoveryCode],
        )

        # Now we clean up our temporary OSKeyStore from the OS's secure storage.
        # We won't need it anymore.
        self.marionette.execute_async_script(
            """
           const { OSKeyStore } = ChromeUtils.importESModule(
             "resource://gre/modules/OSKeyStore.sys.mjs"
           );

           let [outerResolve] = arguments;
           (async () => {
              await OSKeyStore.cleanup();
           })().then(outerResolve);
        """
        )

        recoveryPath = os.path.join(tempfile.gettempdir(), "recovery")
        shutil.rmtree(recoveryPath, ignore_errors=True)

        # Start a brand new profile, one without any of the data we created or
        # backed up. This is the one that we'll be starting recovery from.
        self.marionette.quit()
        self.marionette.instance.switch_profile()
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        # Recover the created backup into a new profile directory. Also get out
        # the client ID of this profile, because we're going to want to make
        # sure that this client ID is not inherited from the intermediate profile.
        [
            newProfileName,
            newProfilePath,
            intermediateClientID,
            osKeyStoreLabel,
        ] = self.marionette.execute_async_script(
            """
          const { OSKeyStore } = ChromeUtils.importESModule("resource://gre/modules/OSKeyStore.sys.mjs");
          const { ClientID } = ChromeUtils.importESModule("resource://gre/modules/ClientID.sys.mjs");
          const { BackupService } = ChromeUtils.importESModule("resource:///modules/backup/BackupService.sys.mjs");
          let bs = BackupService.get();
          if (!bs) {
            throw new Error("Could not get initialized BackupService.");
          }

          let [archivePath, recoveryCode, recoveryPath, outerResolve] = arguments;
          (async () => {
            let newProfileRootPath = await IOUtils.createUniqueDirectory(
              PathUtils.tempDir,
              "recoverFromBackupArchiveTest-newProfileRoot"
            );

            // This is some hackery to make it so that OSKeyStore doesn't kick
            // off an OS authentication dialog in our test, and also to make
            // sure we don't blow away the _real_ OSKeyStore key for the browser
            // on the system that this test is running on. Normally, I'd use
            // OSKeyStoreTestUtils.setup to do this, but apparently the
            // testing-common modules aren't available in Marionette tests.
            const ORIGINAL_STORE_LABEL = OSKeyStore.STORE_LABEL;
            OSKeyStore.STORE_LABEL = "test-" + Math.random().toString(36).substr(2);

            let newProfile = await bs.recoverFromBackupArchive(archivePath, recoveryCode, false, recoveryPath, newProfileRootPath, true);

            if (!newProfile) {
              throw new Error("Could not create recovery profile.");
            }

            let intermediateClientID = await ClientID.getClientID();

            return [newProfile.name, newProfile.rootDir.path, intermediateClientID, OSKeyStore.STORE_LABEL];
          })().then(outerResolve);
        """,
            script_args=[archivePath, recoveryCode, recoveryPath],
        )

        print(f"Recovery name: {newProfileName}")
        print(f"Recovery path: {newProfilePath}")
        print(f"Intermediate clientID: {intermediateClientID}")
        print(f"Persisting fake OSKeyStore label: {osKeyStoreLabel}")

        self.marionette.quit()
        originalProfile = self.marionette.instance.profile
        self.marionette.instance.profile = newProfilePath
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        # Ensure that all postRecovery actions have completed.
        self.marionette.execute_async_script(
            """
          const { BackupService } = ChromeUtils.importESModule("resource:///modules/backup/BackupService.sys.mjs");
          let bs = BackupService.get();
          if (!bs) {
            throw new Error("Could not get initialized BackupService.");
          }

          let [outerResolve] = arguments;
          (async () => {
            await bs.postRecoveryComplete;
          })().then(outerResolve);
        """
        )

        self.verify_recovered_test_cookie()
        self.verify_recovered_test_login()
        self.verify_recovered_test_certificate()
        self.verify_recovered_saved_address()
        self.verify_recovered_identity_credential()
        self.verify_recovered_form_history()
        self.verify_recovered_asrouter_snippets_data()
        self.verify_recovered_protections_data()
        self.verify_recovered_bookmarks()
        self.verify_recovered_history()
        self.verify_recovered_preferences()
        self.verify_recovered_permissions()
        self.verify_recovered_payment_methods(osKeyStoreLabel)
        self.verify_recovered_sessionstore()
        self.verify_recovered_newtab_wallpaper()

        # Clean up the temporary OSKeyStore label
        self.marionette.execute_async_script(
            """
          const { OSKeyStore } = ChromeUtils.importESModule("resource://gre/modules/OSKeyStore.sys.mjs");
          let [osKeyStoreLabel, outerResolve] = arguments;

          OSKeyStore.STORE_LABEL = osKeyStoreLabel;

          (async () => {
            await OSKeyStore.cleanup();
          })().then(outerResolve);
        """,
            script_args=[osKeyStoreLabel],
        )

        # Now also ensure that the recovered profile new client ID and not that
        # one from the intermediate profile that initiated recovery.
        recoveredClientID = self.marionette.execute_async_script(
            """
          const { ClientID } = ChromeUtils.importESModule("resource://gre/modules/ClientID.sys.mjs");
          let [outerResolve] = arguments;
          (async () => {
            return ClientID.getClientID();
          })().then(outerResolve);
        """
        )
        self.assertNotEqual(recoveredClientID, intermediateClientID)

        self.marionette.quit()
        self.marionette.instance.profile = originalProfile
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        # Don't pollute the profile list by getting rid of the one we just created.
        self.marionette.execute_async_script(
            """
          let [newProfileName, outerResolve] = arguments;
          let profileSvc = Cc["@mozilla.org/toolkit/profile-service;1"].getService(
            Ci.nsIToolkitProfileService
          );
          let profile = profileSvc.getProfileByName(newProfileName);
          profile.remove(true);
          profileSvc.asyncFlush().then(outerResolve);
        """,
            script_args=[newProfileName],
        )

        # Cleanup the archive we moved, and the recovery folder we decompressed to.
        mozfile.remove(archivePath)
        mozfile.remove(recoveryPath)

    def test_backup_disablement_in_new_session(self):
        archiveDestPath = os.path.join(
            tempfile.gettempdir(), "backup-dest-disable-test"
        )

        [archivePath, lastBackupFileName] = self.marionette.execute_async_script(
            """
          const { BackupService } = ChromeUtils.importESModule("resource:///modules/backup/BackupService.sys.mjs");
          let bs = BackupService.init();
          if (!bs) {
            throw new Error("Could not get initialized BackupService.");
          }

          let [archiveDestPath, outerResolve] = arguments;

          (async () => {
            await bs.setParentDirPath(archiveDestPath);
            bs.setScheduledBackups(true);
            let { archivePath } = await bs.createBackup();
            if (!archivePath) {
              throw new Error("Could not create backup.");
            }

            let lastBackupFileName = Services.prefs.getStringPref("browser.backup.scheduled.last-backup-file", "");
            return [archivePath, lastBackupFileName];
          })().then(outerResolve);
        """,
            script_args=[archiveDestPath],
        )

        print(f"Created backup at: {archivePath}")
        print(f"Last backup filename: {lastBackupFileName}")

        self.marionette.quit()
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        if os.path.exists(archivePath):
            print(f"File size: {os.path.getsize(archivePath)} bytes")

        self.marionette.execute_async_script(
            """

          ChromeUtils.defineESModuleGetters(this, {
            BackupService: "resource:///modules/backup/BackupService.sys.mjs",
            ASRouterTargeting: "resource:///modules/asrouter/ASRouterTargeting.sys.mjs",
          });

          let bs = BackupService.init();
          if (!bs) {
            throw new Error("Could not get initialized BackupService.");
          }

          let [outerResolve] = arguments;
          (async () => {
            await ASRouterTargeting.Environment.backupsInfo;
            await bs.cleanupBackupFiles();
            bs.setScheduledBackups(false);
          })().then(outerResolve);
        """,
            script_args=[],
        )

        archiveDeletedAfterDisable = not os.path.exists(archivePath)
        self.assertTrue(
            archiveDeletedAfterDisable,
            f"Backup file should be deleted after disabling backups. Path: {archivePath}, exists: {os.path.exists(archivePath)}",
        )

    def add_test_cookie(self):
        self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;

          (async () => {
            // We'll just add a single cookie, and then make sure that it shows
            // up on the other side.
            Services.cookies.removeAll();
            Services.cookies.add(
              ".example.com",
              "/",
              "first",
              "one",
              false,
              false,
              false,
              Date.now() + 1000,
              {},
              Ci.nsICookie.SAMESITE_UNSET,
              Ci.nsICookie.SCHEME_HTTP
            );
          })().then(outerResolve);
        """
        )

    def verify_recovered_test_cookie(self):
        cookiesLength = self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;
          (async () => {
            let cookies = Services.cookies.getCookiesFromHost("example.com", {});
            return cookies.length;
          })().then(outerResolve);
        """
        )
        # Expect cookies to be removed from the backup.
        self.assertEqual(cookiesLength, 0)

    def add_test_login(self):
        self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;
          (async () => {
            // Let's start with adding a single password
            await Services.logins.removeAllLoginsAsync();

            const nsLoginInfo = new Components.Constructor(
              "@mozilla.org/login-manager/loginInfo;1",
              Ci.nsILoginInfo,
              "init"
            );

            const login1 = new nsLoginInfo(
              "https://example.com",
              "https://example.com",
              null,
              "notifyu1",
              "notifyp1",
              "user",
              "pass"
            );
            await Services.logins.addLoginAsync(login1);
          })().then(outerResolve);
        """
        )

    def verify_recovered_test_login(self):
        loginsLength = self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;
          (async () => {
            let logins = await Services.logins.searchLoginsAsync({
              origin: "https://example.com",
            });
            return logins.length;
          })().then(outerResolve);
        """
        )
        self.assertEqual(loginsLength, 1)

    def add_test_certificate(self):
        certPath = os.path.join(os.path.dirname(__file__), "http2-ca.pem")
        self.marionette.execute_async_script(
            """
          let [certPath, certDbKey, outerResolve] = arguments;
          (async () => {
            const { NetUtil } = ChromeUtils.importESModule(
              "resource://gre/modules/NetUtil.sys.mjs"
            );

            let certDb = Cc["@mozilla.org/security/x509certdb;1"].getService(
              Ci.nsIX509CertDB
            );

            if (certDb.findCertByDBKey(certDbKey)) {
              throw new Error("Should not have this certificate yet!");
            }

            let certFile = await IOUtils.getFile(certPath);
            let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
              Ci.nsIFileInputStream
            );
            fstream.init(certFile, -1, 0, 0);
            let data = NetUtil.readInputStreamToString(fstream, fstream.available());
            fstream.close();

            let pem = data.replace(/-----BEGIN CERTIFICATE-----/, "")
                          .replace(/-----END CERTIFICATE-----/, "")
                          .replace(/[\\r\\n]/g, "");
            let cert = certDb.addCertFromBase64(pem, "CTu,u,u");

            if (cert.dbKey != certDbKey) {
              throw new Error("The inserted certificate DB key is unexpected.");
            }
          })().then(outerResolve);
        """,
            script_args=[certPath, self._cert_db_key],
        )

    def verify_recovered_test_certificate(self):
        certExists = self.marionette.execute_async_script(
            """
          let [certDbKey, outerResolve] = arguments;
          (async () => {
            let certDb = Cc["@mozilla.org/security/x509certdb;1"].getService(
              Ci.nsIX509CertDB
            );
            return certDb.findCertByDBKey(certDbKey) != null;
          })().then(outerResolve);
        """,
            script_args=[self._cert_db_key],
        )
        self.assertTrue(certExists)

    def add_test_saved_address(self):
        self.marionette.execute_async_script(
            """
          const { formAutofillStorage } = ChromeUtils.importESModule(
            "resource://autofill/FormAutofillStorage.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
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
            await formAutofillStorage.initialize();
            formAutofillStorage.addresses.removeAll();
            await formAutofillStorage.addresses.add(TEST_ADDRESS_1);
          })().then(outerResolve);
        """
        )

    def verify_recovered_saved_address(self):
        addressesLength = self.marionette.execute_async_script(
            """
          const { formAutofillStorage } = ChromeUtils.importESModule(
            "resource://autofill/FormAutofillStorage.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            await formAutofillStorage.initialize();
            let addresses = await formAutofillStorage.addresses.getAll();
            return addresses.length;
          })().then(outerResolve);
        """
        )
        self.assertEqual(addressesLength, 1)

    def add_test_identity_credential(self):
        self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;
          (async () => {
            let service = Cc["@mozilla.org/browser/identity-credential-storage-service;1"]
                            .getService(Ci.nsIIdentityCredentialStorageService);
            service.clear();

            let testPrincipal = Services.scriptSecurityManager.createContentPrincipal(
              Services.io.newURI("https://test.com/"),
              {}
            );
            let idpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
              Services.io.newURI("https://idp-test.com/"),
              {}
            );

            service.setState(
              testPrincipal,
              idpPrincipal,
              "ID",
              true,
              true
            );

          })().then(outerResolve);
        """
        )

    def verify_recovered_identity_credential(self):
        [registered, allowLogout] = self.marionette.execute_async_script(
            """
          let [outerResolve] = arguments;
          (async () => {
            let service = Cc["@mozilla.org/browser/identity-credential-storage-service;1"]
                            .getService(Ci.nsIIdentityCredentialStorageService);

            let testPrincipal = Services.scriptSecurityManager.createContentPrincipal(
              Services.io.newURI("https://test.com/"),
              {}
            );
            let idpPrincipal = Services.scriptSecurityManager.createContentPrincipal(
              Services.io.newURI("https://idp-test.com/"),
              {}
            );

            let registered = {};
            let allowLogout = {};

            service.getState(
              testPrincipal,
              idpPrincipal,
              "ID",
              registered,
              allowLogout
            );

            return [registered.value, allowLogout.value];
          })().then(outerResolve);
        """
        )
        self.assertTrue(registered)
        self.assertTrue(allowLogout)

    def add_test_form_history(self):
        self.marionette.execute_async_script(
            """
          const { FormHistory } = ChromeUtils.importESModule(
            "resource://gre/modules/FormHistory.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            await FormHistory.update({
              op: "add",
              fieldname: "some-test-field",
              value: "I was recovered!",
              timesUsed: 1,
              firstUsed: 0,
              lastUsed: 0,
            });

          })().then(outerResolve);
        """
        )

    def verify_recovered_form_history(self):
        formHistoryResultsLength = self.marionette.execute_async_script(
            """
          const { FormHistory } = ChromeUtils.importESModule(
            "resource://gre/modules/FormHistory.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            let results = await FormHistory.search(
              ["guid"],
              { fieldname: "some-test-field" }
            );
            return results.length;
          })().then(outerResolve);
        """
        )
        self.assertEqual(formHistoryResultsLength, 1)

    def add_test_asrouter_snippets_data(self):
        self.marionette.execute_async_script(
            """
          const { ASRouterStorage } = ChromeUtils.importESModule(
            "resource:///modules/asrouter/ASRouterStorage.sys.mjs",
          );
          const SNIPPETS_TABLE_NAME = "snippets";

          let [outerResolve] = arguments;
          (async () => {
            let storage = new ASRouterStorage({
              storeNames: [SNIPPETS_TABLE_NAME],
            });
            let snippetsTable = await storage.getDbTable(SNIPPETS_TABLE_NAME);
            await snippetsTable.set("backup-test", "some-test-value");
          })().then(outerResolve);
        """
        )

    def verify_recovered_asrouter_snippets_data(self):
        snippetsResult = self.marionette.execute_async_script(
            """
          const { ASRouterStorage } = ChromeUtils.importESModule(
            "resource:///modules/asrouter/ASRouterStorage.sys.mjs",
          );
          const SNIPPETS_TABLE_NAME = "snippets";

          let [outerResolve] = arguments;
          (async () => {
            let storage = new ASRouterStorage({
              storeNames: [SNIPPETS_TABLE_NAME],
            });
            let snippetsTable = await storage.getDbTable(SNIPPETS_TABLE_NAME);
            return await snippetsTable.get("backup-test");
          })().then(outerResolve);
        """
        )
        self.assertEqual(snippetsResult, "some-test-value")

    def add_test_protections_data(self):
        self.marionette.execute_async_script(
            """
          const TrackingDBService = Cc["@mozilla.org/tracking-db-service;1"]
                                      .getService(Ci.nsITrackingDBService);

          let [outerResolve] = arguments;
          (async () => {
            let entry = {
              "https://test.com": [
                [Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT, true, 1],
              ],
            };
            await TrackingDBService.clearAll();
            await TrackingDBService.saveEvents(JSON.stringify(entry));
          })().then(outerResolve);
        """
        )

    def verify_recovered_protections_data(self):
        eventsSum = self.marionette.execute_async_script(
            """
          const TrackingDBService = Cc["@mozilla.org/tracking-db-service;1"]
                                      .getService(Ci.nsITrackingDBService);

          let [outerResolve] = arguments;
          (async () => {
            return TrackingDBService.sumAllEvents();
          })().then(outerResolve);
        """
        )
        self.assertEqual(eventsSum, 1)

    def add_test_bookmarks(self):
        self.marionette.execute_async_script(
            """
          const { PlacesUtils } = ChromeUtils.importESModule(
            "resource://gre/modules/PlacesUtils.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            await PlacesUtils.bookmarks.eraseEverything();
            await PlacesUtils.bookmarks.insert({
              parentGuid: PlacesUtils.bookmarks.toolbarGuid,
              title: "Some test page",
              url: Services.io.newURI("https://www.backup.test/"),
            });
          })().then(outerResolve);
        """
        )

    def verify_recovered_bookmarks(self):
        bookmarkExists = self.marionette.execute_async_script(
            """
          const { PlacesUtils } = ChromeUtils.importESModule(
            "resource://gre/modules/PlacesUtils.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            let url = Services.io.newURI("https://www.backup.test/");
            let bookmark = await PlacesUtils.bookmarks.fetch({ url });
            return bookmark != null;
          })().then(outerResolve);
        """
        )
        self.assertTrue(bookmarkExists)

    def add_test_history(self):
        self.marionette.execute_async_script(
            """
          const { PlacesUtils } = ChromeUtils.importESModule(
            "resource://gre/modules/PlacesUtils.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            await PlacesUtils.history.clear();

            let entry = {
              url: "http://my-restored-history.com",
              visits: [{ transition: PlacesUtils.history.TRANSITION_LINK }],
            };

            await PlacesUtils.history.insertMany([entry]);
          })().then(outerResolve);
        """
        )

    def verify_recovered_history(self):
        historyExists = self.marionette.execute_async_script(
            """
          const { PlacesUtils } = ChromeUtils.importESModule(
            "resource://gre/modules/PlacesUtils.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            let entry = await PlacesUtils.history.fetch("http://my-restored-history.com");
            return entry != null;
          })().then(outerResolve);
        """
        )
        self.assertTrue(historyExists)

    def add_test_preferences(self):
        self.marionette.execute_script(
            """
          Services.prefs.setBoolPref("test-pref-for-backup", true)
        """
        )

    def verify_recovered_preferences(self):
        prefExists = self.marionette.execute_script(
            """
          return Services.prefs.getBoolPref("test-pref-for-backup", false);
        """
        )
        self.assertTrue(prefExists)

    def add_test_permissions(self):
        self.marionette.execute_script(
            """
          let principal = Services.scriptSecurityManager.createContentPrincipalFromOrigin(
            "https://test-permission-site.com"
          );
          Services.perms.addFromPrincipal(
            principal,
            "desktop-notification",
            Services.perms.ALLOW_ACTION
          );
        """
        )

    def add_test_newtab_wallpaper(self):
        wallpaperPath = os.path.join(os.path.dirname(__file__), "newtab-wallpaper.png")
        self.marionette.execute_async_script(
            """
          let [wallpaperPath, outerResolve] = arguments;
          (async () => {
            let feed = AboutNewTab.activityStream.store.feeds.get("feeds.wallpaperfeed");
            let wallpaperFile = await File.createFromNsIFile(await IOUtils.getFile(wallpaperPath));
            await feed.wallpaperUpload(wallpaperFile, "light");
            Services.prefs.setStringPref("browser.newtabpage.activity-stream.newtabWallpapers.wallpaper", "custom");
          })().then(outerResolve);
        """,
            script_args=[wallpaperPath],
        )

    def verify_recovered_permissions(self):
        permissionExists = self.marionette.execute_script(
            """
          let principal = Services.scriptSecurityManager.createContentPrincipalFromOrigin(
            "https://test-permission-site.com"
          );
          let perms = Services.perms.getAllForPrincipal(principal);
          if (perms.length != 1) {
            throw new Error("Got an unexpected number of permissions");
          }
          return perms[0].type == "desktop-notification"
        """
        )
        self.assertTrue(permissionExists)

    def add_test_payment_methods(self):
        self.marionette.execute_async_script(
            """
          const { formAutofillStorage } = ChromeUtils.importESModule(
            "resource://autofill/FormAutofillStorage.sys.mjs"
          );

          let [outerResolve] = arguments;
          (async () => {
            await formAutofillStorage.initialize();
            await formAutofillStorage.creditCards.add({
              "cc-name": "Foxy the Firefox",
              "cc-number": "5555555555554444",
              "cc-exp-month": 5,
              "cc-exp-year": 2099,
            });
          })().then(outerResolve);
        """
        )

    def verify_recovered_payment_methods(self, osKeyStoreLabel):
        cardExists = self.marionette.execute_async_script(
            """
          const { formAutofillStorage } = ChromeUtils.importESModule(
            "resource://autofill/FormAutofillStorage.sys.mjs"
          );
          let nativeOSKeyStore = Cc["@mozilla.org/security/oskeystore;1"].getService(
            Ci.nsIOSKeyStore
          );

          let [osKeyStoreLabel, outerResolve] = arguments;

          (async () => {
            await formAutofillStorage.initialize();
            let cards = await formAutofillStorage.creditCards.getAll();

            if (cards.length != 1) {
              return false;
            }
            let card = cards[0];
            if (card["cc-name"] != "Foxy the Firefox") {
              return false;
            }

            if (card["cc-exp-month"] != "5") {
              return false;
            }

            if (card["cc-exp-year"] != "2099") {
              return false;
            }

            if (!card["cc-number-encrypted"]) {
              return false;
            }

            // Hack around OSKeyStore's insistence on asking for
            // reauthentication by using the underlying nativeOSKeyStore
            // to decrypt the credit card number to check it.
            let plaintextCardBytes =
              await nativeOSKeyStore.asyncDecryptBytes(
                osKeyStoreLabel,
                card["cc-number-encrypted"]
              );
            let plaintextCard = String.fromCharCode.apply(
              String,
              plaintextCardBytes
            );
            if (plaintextCard != "5555555555554444") {
              return false;
            }

            return true;
          })().then(outerResolve);
        """,
            script_args=[osKeyStoreLabel],
        )
        self.assertTrue(cardExists)

    def add_test_sessionstore(self):
        with self.marionette.using_context("content"):
            self.marionette.navigate("about:mozilla")

    def verify_recovered_sessionstore(self):
        [tabCount, url] = self.marionette.execute_script(
            """
          const { SessionStore } = ChromeUtils.importESModule(
            "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs"
          );
          const session = SessionStore.getCurrentState(true);
          const win = session.windows[0];
          const tabLen = win.tabs.length;
          const tab = win.tabs[0];
          const entry = tab.entries[0];
          const url = entry.url;
          return [tabLen, url];
        """
        )

        self.assertEqual(tabCount, 1)
        self.assertEqual(url, "about:mozilla")

    def verify_recovered_newtab_wallpaper(self):
        [isCustom, wallpaperPath] = self.marionette.execute_script(
            """
          const isCustom = Services.prefs.getStringPref("browser.newtabpage.activity-stream.newtabWallpapers.wallpaper", "") == "custom";
          const wallpaperUUID = Services.prefs.getStringPref("browser.newtabpage.activity-stream.newtabWallpapers.customWallpaper.uuid", "");
          const wallpaperPath = PathUtils.join(PathUtils.profileDir, "wallpaper", wallpaperUUID);
          return [isCustom, wallpaperPath];
        """
        )

        self.assertTrue(isCustom)
        expectedWallpaperPath = os.path.join(
            os.path.dirname(__file__), "newtab-wallpaper.png"
        )
        self.assertTrue(
            filecmp.cmp(wallpaperPath, expectedWallpaperPath, shallow=False)
        )
