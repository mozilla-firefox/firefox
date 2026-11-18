/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);
const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);
const { PlacesUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesUtils.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);
const { DownloadHistory } = ChromeUtils.importESModule(
  "resource://gre/modules/DownloadHistory.sys.mjs"
);
const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { ExtensionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/ExtensionXPCShellUtils.sys.mjs"
);
const { formAutofillStorage } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillStorage.sys.mjs"
);
const { Sanitizer } = ChromeUtils.importESModule(
  "resource:///modules/Sanitizer.sys.mjs"
);
const { NewTabUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/NewTabUtils.sys.mjs"
);
const { CookieXPCShellUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CookieXPCShellUtils.sys.mjs"
);

ExtensionTestUtils.init(this);
AddonTestUtils.init(this);
AddonTestUtils.createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1");

const nsLoginInfo = new Components.Constructor(
  "@mozilla.org/login-manager/loginInfo;1",
  Ci.nsILoginInfo,
  "init"
);

/**
 * This suite of tests ensures that the current backup will be deleted, and
 * a new one generated when certain user actions occur.
 */

/**
 * Adds a history visit to the Places database.
 *
 * @param {string} uriString
 *   A string representation of the URI to add a visit for.
 * @param {number} [timestamp=Date.now()]
 *   The timestamp for the visit to be used. By default, this is the current
 *   date and time.
 * @returns {Promise<undefined>}
 */
async function addTestHistory(uriString, timestamp = Date.now()) {
  let uri = NetUtil.newURI(uriString);
  await PlacesTestUtils.addVisits({
    uri,
    transition: Ci.nsINavHistoryService.TRANSITION_TYPED,
    visitDate: timestamp * 1000,
  });
}

let gCookieCounter = 0;

const COOKIE_HOST = "example.com";
const COOKIE_PATH = "/";
const COOKIE_ORIGIN_ATTRIBUTES = Object.freeze({});

/**
 * Adds a new non-session cookie to the cookie database with the host set
 * as COOKIE_HOST, the path as COOKIE_PATH, with the origin attributes of
 * COOKIE_ORIGIN_ATTRIBUTES and a generated name and value.
 *
 * @param {boolean} isSessionCookie
 *   True if the cookie should be a session cookie.
 * @returns {string}
 *   The name of the cookie that was generated.
 */
function addTestCookie(isSessionCookie) {
  gCookieCounter++;
  let name = `Cookie name: ${gCookieCounter}`;

  const cv = Services.cookies.add(
    COOKIE_HOST,
    COOKIE_PATH,
    name,
    `Cookie value: ${gCookieCounter}`,
    false,
    false,
    isSessionCookie,
    Date.now() + 1000,
    COOKIE_ORIGIN_ATTRIBUTES,
    Ci.nsICookie.SAMESITE_UNSET,
    Ci.nsICookie.SCHEME_HTTP
  );
  Assert.equal(cv.result, Ci.nsICookieValidation.eOK);

  return name;
}

/**
 * A helper function that sets up a BackupService to be instrumented to detect
 * a backup regeneration, and then runs an async taskFn to ensure that the
 * regeneration occurs.
 *
 * @param {Function} taskFn
 *   The async function to run after the BackupService has been set up. It is
 *   not passed any arguments.
 * @param {string} msg
 *   The message to write in the assertion when the regeneration occurs.
 */
async function expectRegeneration(taskFn, msg) {
  let sandbox = sinon.createSandbox();
  // Override the default REGENERATION_DEBOUNCE_RATE_MS to 0 so that we don't
  // have to wait long for the debouncer to fire. We need to do this before
  // we construct the BackupService that we'll use for the test.
  sandbox.stub(BackupService, "REGENERATION_DEBOUNCE_RATE_MS").get(() => 0);

  let bs = new BackupService();

  // Now we set up a stub on the BackupService to detect calls to
  // createbackupOnIdleDispatch
  let createBackupDeferred = Promise.withResolvers();
  sandbox.stub(bs, "createBackupOnIdleDispatch").callsFake(options => {
    Assert.ok(true, "Saw createBackupOnIdleDispatch call");
    Assert.equal(
      options.reason,
      "user deleted some data",
      "Backup was recorded as being caused by user data deletion"
    );
    createBackupDeferred.resolve();
    return Promise.resolve();
  });

  // Creating a new backup will only occur if scheduled backups are enabled,
  // so let's set the pref...
  Services.prefs.setBoolPref("browser.backup.scheduled.enabled", true);
  Services.prefs.setBoolPref("browser.backup.archive.enabled", true);
  // But also stub out `onIdle` so that we don't get any interference during
  // our test by the idle service.
  sandbox.stub(bs, "onIdle").returns();

  bs.initBackupScheduler();

  await taskFn();

  // We'll wait for 1 second before considering the regeneration a bust.
  let timeoutPromise = new Promise((resolve, reject) =>
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(() => {
      reject();
    }, 1000)
  );

  try {
    await Promise.race([createBackupDeferred.promise, timeoutPromise]);
    Assert.ok(true, msg);
  } catch (e) {
    Assert.ok(false, "Timed out waiting for regeneration.");
  }

  bs.uninitBackupScheduler();
  Services.prefs.clearUserPref("browser.backup.scheduled.enabled");
  sandbox.restore();
}

/**
 * A helper function that sets up a BackupService to be instrumented to detect
 * a backup regeneration, and then runs an async taskFn to ensure that the
 * regeneration DOES NOT occur.
 *
 * @param {Function} taskFn
 *   The async function to run after the BackupService has been set up. It is
 *   not passed any arguments.
 * @param {string} msg
 *   The message to write in the assertion when the regeneration does not occur
 *   within the timeout.
 */
async function expectNoRegeneration(taskFn, msg) {
  let sandbox = sinon.createSandbox();
  // Override the default REGENERATION_DEBOUNCE_RATE_MS to 0 so that we don't
  // have to wait long for the debouncer to fire. We need to do this before
  // we construct the BackupService that we'll use for the test.
  sandbox.stub(BackupService, "REGENERATION_DEBOUNCE_RATE_MS").get(() => 0);

  let bs = new BackupService();

  // Now we set up some stubs on the BackupService to detect calls to
  // deleteLastBackup and createbackupOnIdleDispatch, which are both called
  // on regeneration. We share the same Promise here because in either of these
  // being called, this test is a failure.
  let regenerationPromise = Promise.withResolvers();
  sandbox.stub(bs, "deleteLastBackup").callsFake(() => {
    Assert.ok(false, "Unexpectedly saw deleteLastBackup call");
    regenerationPromise.reject();
    return Promise.resolve();
  });

  sandbox.stub(bs, "createBackupOnIdleDispatch").callsFake(() => {
    Assert.ok(false, "Unexpectedly saw createBackupOnIdleDispatch call");
    regenerationPromise.reject();
    return Promise.resolve();
  });

  // Creating a new backup will only occur if scheduled backups are enabled,
  // so let's set the pref...
  Services.prefs.setBoolPref("browser.backup.scheduled.enabled", true);
  // But also stub out `onIdle` so that we don't get any interference during
  // our test by the idle service.
  sandbox.stub(bs, "onIdle").resolves();

  bs.initBackupScheduler();

  await taskFn();

  // We'll wait for 1 second, and if we don't see the regeneration attempt,
  // we'll assume it's not coming.
  let timeoutPromise = new Promise(resolve =>
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(() => {
      Assert.ok(true, "Saw no regeneration within 1 second.");
      resolve();
    }, 1000)
  );

  try {
    await Promise.race([regenerationPromise.promise, timeoutPromise]);
    Assert.ok(true, msg);
  } catch (e) {
    Assert.ok(false, "Saw an unexpected regeneration.");
  }

  bs.uninitBackupScheduler();
  Services.prefs.clearUserPref("browser.backup.scheduled.enabled");
  sandbox.restore();
}

add_setup(() => {
  CookieXPCShellUtils.createServer({ hosts: ["example.com"] });
  Services.prefs.setBoolPref("dom.security.https_first", false);

  // Allow all cookies.
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);
  Services.prefs.setBoolPref(
    "network.cookieJarSettings.unblocked_for_testing",
    true
  );
});

/**
 * Tests that backup regeneration occurs on the page-removed PlacesObserver
 * event that indicates manual user deletion of a page from history.
 */
add_task(async function test_page_removed_reason_deleted() {
  const PAGE_URI = "https://test.com";
  await addTestHistory(PAGE_URI);
  await expectRegeneration(async () => {
    await PlacesUtils.history.remove(PAGE_URI);
  }, "Saw regeneration on page-removed via user deletion.");
});

/**
 * Tests that backup regeneration does not occur on the page-removed
 * PlacesObserver event that indicates automatic deletion of a page from
 * history.
 */
add_task(async function test_page_removed_reason_expired() {
  const PAGE_URI = "https://test.com";
  await addTestHistory(
    PAGE_URI,
    0 /* Timestamp at 0 is wayyyyy in the past, in the 1960's - it's the UNIX epoch start date */
  );
  await expectNoRegeneration(async () => {
    // This is how the Places tests force expiration, so we'll do it the same
    // way.
    let promise = TestUtils.topicObserved(
      PlacesUtils.TOPIC_EXPIRATION_FINISHED
    );
    let expire = Cc["@mozilla.org/places/expiration;1"].getService(
      Ci.nsIObserver
    );
    expire.observe(null, "places-debug-start-expiration", -1);
    await promise;
  }, "Saw no regeneration on page-removed via expiration.");
});

/**
 * Tests that backup regeneration occurs on the history-cleared PlacesObserver
 * event that indicates clearing of all user history.
 */
add_task(async function test_history_cleared() {
  const PAGE_URI = "https://test.com";
  await addTestHistory(PAGE_URI);
  await expectRegeneration(async () => {
    await PlacesUtils.history.clear();
  }, "Saw regeneration on history-cleared.");
});

/**
 * Tests that backup regeneration occurs when removing a download from history.
 */
add_task(async function test_download_removed() {
  const FAKE_FILE_PATH = PathUtils.join(PathUtils.tempDir, "somefile.zip");
  let download = {
    source: {
      url: "https://test.com/somefile",
      isPrivate: false,
    },
    target: { path: FAKE_FILE_PATH },
  };
  await DownloadHistory.addDownloadToHistory(download);

  await expectRegeneration(async () => {
    await PlacesUtils.history.remove(download.source.url);
  }, "Saw regeneration on download removal.");
});

/**
 * Tests that backup regeneration occurs when clearing all downloads.
 */
add_task(async function test_all_downloads_removed() {
  const FAKE_FILE_PATH = PathUtils.join(PathUtils.tempDir, "somefile.zip");
  let download1 = {
    source: {
      url: "https://test.com/somefile",
      isPrivate: false,
    },
    target: { path: FAKE_FILE_PATH },
  };
  let download2 = {
    source: {
      url: "https://test.com/somefile2",
      isPrivate: false,
    },
    target: { path: FAKE_FILE_PATH },
  };
  await DownloadHistory.addDownloadToHistory(download1);
  await DownloadHistory.addDownloadToHistory(download2);

  await expectRegeneration(async () => {
    await PlacesUtils.history.removeVisitsByFilter({
      transition: PlacesUtils.history.TRANSITIONS.DOWNLOAD,
    });
  }, "Saw regeneration on all downloads removed.");
});

/**
 * Tests that backup regeneration occurs when removing a password.
 */
add_task(async function test_password_removed() {
  let login = new nsLoginInfo(
    "https://example.com",
    "https://example.com",
    null,
    "notifyu1",
    "notifyp1",
    "user",
    "pass"
  );
  await Services.logins.addLoginAsync(login);

  await expectRegeneration(async () => {
    await Services.logins.removeLoginAsync(login);
  }, "Saw regeneration on password removed.");
});

/**
 * Tests that backup regeneration occurs when all passwords are removed.
 */
add_task(async function test_all_passwords_removed() {
  let login1 = new nsLoginInfo(
    "https://example.com",
    "https://example.com",
    null,
    "notifyu1",
    "notifyp1",
    "user",
    "pass"
  );
  let login2 = new nsLoginInfo(
    "https://example.com",
    "https://example.com",
    null,
    "",
    "notifyp1",
    "",
    "pass"
  );

  await Services.logins.addLoginAsync(login1);
  await Services.logins.addLoginAsync(login2);

  await expectRegeneration(async () => {
    await Services.logins.removeAllLoginsAsync();
  }, "Saw regeneration on all passwords removed.");
});

/**
 * Tests that backup regeneration occurs when removing a bookmark.
 */
add_task(async function test_bookmark_removed() {
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "data:text/plain,Content",
    title: "Regeneration Test Bookmark",
  });

  await expectRegeneration(async () => {
    await PlacesUtils.bookmarks.remove(bookmark);
  }, "Saw regeneration on bookmark removed.");
});

/**
 * Tests that backup regeneration occurs when all bookmarks are removed.
 */
add_task(async function test_all_bookmarks_removed() {
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "data:text/plain,Content",
    title: "Regeneration Test Bookmark 1",
  });
  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "data:text/plain,Content2",
    title: "Regeneration Test Bookmark 2",
  });

  await expectRegeneration(async () => {
    await PlacesUtils.bookmarks.eraseEverything();
  }, "Saw regeneration on all bookmarks removed.");
});

/**
 * Tests that backup regeneration occurs when an addon is uninstalled.
 */
add_task(async function test_addon_uninstalled() {
  await AddonTestUtils.promiseStartupManager();

  let testExtension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Some test extension",
      browser_specific_settings: {
        gecko: { id: "test-backup-regeneration@ext-0" },
      },
    },
    useAddonManager: "temporary",
  });

  await testExtension.startup();

  await expectRegeneration(async () => {
    await testExtension.unload();
  }, "Saw regeneration on test extension uninstall.");
});

/**
 * Tests that backup regeneration occurs when removing a payment method.
 */
add_task(async function test_payment_method_removed() {
  await formAutofillStorage.initialize();
  let guid = await formAutofillStorage.creditCards.add({
    "cc-name": "Foxy the Firefox",
    "cc-number": "5555555555554444",
    "cc-exp-month": 5,
    "cc-exp-year": 2099,
  });

  await expectRegeneration(async () => {
    await formAutofillStorage.creditCards.remove(guid);
  }, "Saw regeneration on payment method removal.");
});

/**
 * Tests that backup regeneration occurs when removing an address.
 */
add_task(async function test_address_removed() {
  await formAutofillStorage.initialize();
  let guid = await formAutofillStorage.addresses.add({
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
  });

  await expectRegeneration(async () => {
    await formAutofillStorage.addresses.remove(guid);
  }, "Saw regeneration on address removal.");
});

/**
 * Tests that backup regeneration occurs after any kind of data sanitization.
 */
add_task(async function test_sanitization() {
  await expectRegeneration(async () => {
    await Sanitizer.sanitize(["cookiesAndStorage"]);
  }, "Saw regeneration on sanitization of cookies and storage.");

  await expectRegeneration(async () => {
    await Sanitizer.sanitize(["siteSettings"]);
  }, "Saw regeneration on sanitization of site settings.");
});

/**
 * Tests that backup regeneration occurs after a permission is removed.
 */
add_task(async function test_permission_removed() {
  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://test-permission-site.com"
    );
  const PERMISSION_TYPE = "desktop-notification";
  Services.perms.addFromPrincipal(
    principal,
    PERMISSION_TYPE,
    Services.perms.ALLOW_ACTION
  );

  await expectRegeneration(async () => {
    Services.perms.removeFromPrincipal(principal, PERMISSION_TYPE);
  }, "Saw regeneration on permission removal.");
});

/**
 * Tests that backup regeneration does not occur when persistent and session
 * cookies are removed, since cookies are not included in backups.
 */
add_task(async function test_cookies_removed() {
  for (let isSessionCookie of [false, true]) {
    Services.cookies.removeAll();

    // First, let's remove a single cookie by host, path, name and origin
    // attrbutes.
    let name = addTestCookie(isSessionCookie);

    if (isSessionCookie) {
      Assert.equal(
        Services.cookies.sessionCookies.length,
        1,
        "Make sure we actually added a session cookie."
      );
    } else {
      Assert.equal(
        Services.cookies.sessionCookies.length,
        0,
        "Make sure we actually added a persistent cookie."
      );
    }

    await expectNoRegeneration(async () => {
      Services.cookies.remove(
        COOKIE_HOST,
        name,
        COOKIE_PATH,
        COOKIE_ORIGIN_ATTRIBUTES
      );
    }, "Saw no regeneration on single cookie removal.");

    // Now remove all cookies for a particular host.
    addTestCookie(isSessionCookie);
    addTestCookie(isSessionCookie);

    await expectNoRegeneration(async () => {
      Services.cookies.removeCookiesFromExactHost(COOKIE_HOST, "{}");
    }, "Saw no regeneration on cookie removal by host.");

    // Now remove all cookies.
    const COOKIES_TO_ADD = 10;
    for (let i = 0; i < COOKIES_TO_ADD; ++i) {
      addTestCookie(isSessionCookie);
    }

    await expectNoRegeneration(async () => {
      Services.cookies.removeAll();
    }, "Saw no regeneration on all cookie removal.");
  }
});

/**
 * Tests that backup regeneration does not occur if a cookie is removed due
 * to expiry.
 */
add_task(async function test_cookies_not_removed_expiry() {
  await expectNoRegeneration(async () => {
    const COOKIE = "test=test";
    await CookieXPCShellUtils.setCookieToDocument(
      "http://example.com/",
      COOKIE
    );

    // Now expire that cookie by using the same value, but setting the expires
    // directive to sometime wayyyyy in the past.
    const EXPIRE = `${COOKIE}; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/`;
    await CookieXPCShellUtils.setCookieToDocument(
      "http://example.com/",
      EXPIRE
    );
  }, "Saw no regeneration on cookie expiry.");
});

/**
 * Tests that backup regeneration occurs when newtab links are blocked.
 */
add_task(async function test_newtab_link_blocked() {
  NewTabUtils.init();

  await expectRegeneration(async () => {
    NewTabUtils.activityStreamLinks.blockURL("https://example.com");
  }, "Saw regeneration on the blocking of a newtab link");
});

/**
 * Tests that backup regeneration occurs when sanitizeOnShutdown is enabled
 * or disabled
 */
add_task(async function test_sanitizeOnShutdown_regeneration() {
  await expectRegeneration(() => {
    Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", true);
  }, "Saw a regeneration when sanitizeOnShutdown was enabled");

  await expectRegeneration(() => {
    Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", false);
  }, "Saw a regeneration when sanitizeOnShutdown was disabled");

  Services.prefs.clearUserPref("privacy.sanitize.sanitizeOnShutdown");
});

/**
 * Tests that setting up a regeneration without archiveEnabledStatus being true
 * leads to NO regeneration upon data mutation
 */
add_task(async function test_enabledStatus_no_regeneration() {
  let bookmark = await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "data:text/plain,Content",
    title: "Regeneration Test Bookmark",
  });

  Services.prefs.setBoolPref("browser.backup.archive.enabled", false);

  await expectNoRegeneration(async () => {
    await PlacesUtils.bookmarks.remove(bookmark);
  }, "Saw no regeneration on bookmark removed.");

  Services.prefs.clearUserPref("browser.backup.archive.enabled");
});
