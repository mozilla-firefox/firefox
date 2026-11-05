/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

async function notifyStoragePressure(usage = 100) {
  let notifyPromise = TestUtils.topicObserved(
    "QuotaManager::StoragePressure",
    () => true
  );
  let usageWrapper = Cc["@mozilla.org/supports-PRUint64;1"].createInstance(
    Ci.nsISupportsPRUint64
  );
  usageWrapper.data = usage;
  Services.obs.notifyObservers(usageWrapper, "QuotaManager::StoragePressure");
  return notifyPromise;
}

function openAboutPrefPromise(win) {
  let promises = [
    BrowserTestUtils.waitForLocationChange(
      win.gBrowser,
      "about:preferences#privacy"
    ),
    TestUtils.topicObserved("privacy-pane-loaded", () => true),
    TestUtils.topicObserved("sync-pane-loaded", () => true),
  ];
  return Promise.all(promises);
}
add_setup(async function () {
  let win = await BrowserTestUtils.openNewBrowserWindow();
  // Open a new tab to keep the window open.
  await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "https://example.com"
  );
});

// Test only displaying notification once within the given interval
add_task(async function () {
  const win = Services.wm.getMostRecentWindow("navigator:browser");
  const TEST_NOTIFICATION_INTERVAL_MS = 2000;
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.storageManager.pressureNotification.minIntervalMS",
        TEST_NOTIFICATION_INTERVAL_MS,
      ],
    ],
  });
  // Commenting this to see if we really need it
  // await SpecialPowers.pushPrefEnv({set: [["privacy.reduceTimerPrecision", false]]});

  await notifyStoragePressure();
  await TestUtils.waitForCondition(() =>
    win.gNotificationBox.getNotificationWithValue(
      "storage-pressure-notification"
    )
  );
  let notification = win.gNotificationBox.getNotificationWithValue(
    "storage-pressure-notification"
  );
  is(
    notification.localName,
    "notification-message",
    "Should display storage pressure notification"
  );
  notification.close();

  await notifyStoragePressure();
  notification = win.gNotificationBox.getNotificationWithValue(
    "storage-pressure-notification"
  );
  is(
    notification,
    null,
    "Should not display storage pressure notification more than once within the given interval"
  );

  await new Promise(resolve =>
    setTimeout(resolve, TEST_NOTIFICATION_INTERVAL_MS + 1)
  );
  await notifyStoragePressure();
  await TestUtils.waitForCondition(() =>
    win.gNotificationBox.getNotificationWithValue(
      "storage-pressure-notification"
    )
  );
  notification = win.gNotificationBox.getNotificationWithValue(
    "storage-pressure-notification"
  );
  is(
    notification.localName,
    "notification-message",
    "Should display storage pressure notification after the given interval"
  );
  notification.close();
});

// Test guiding user to the about:preferences when usage exceeds the given threshold
add_task(async function () {
  const win = Services.wm.getMostRecentWindow("navigator:browser");
  await SpecialPowers.pushPrefEnv({
    set: [["browser.storageManager.pressureNotification.minIntervalMS", 0]],
  });
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "https://example.com"
  );

  const BYTES_IN_GIGABYTE = 1073741824;
  const USAGE_THRESHOLD_BYTES =
    BYTES_IN_GIGABYTE *
    Services.prefs.getIntPref(
      "browser.storageManager.pressureNotification.usageThresholdGB"
    );
  await notifyStoragePressure(USAGE_THRESHOLD_BYTES);
  await TestUtils.waitForCondition(() =>
    win.gNotificationBox.getNotificationWithValue(
      "storage-pressure-notification"
    )
  );
  let notification = win.gNotificationBox.getNotificationWithValue(
    "storage-pressure-notification"
  );
  is(
    notification.localName,
    "notification-message",
    "Should display storage pressure notification"
  );
  await new Promise(r => setTimeout(r, 1000));

  let prefBtn = notification.buttonContainer.getElementsByTagName("button")[0];
  ok(prefBtn, "Should have an open preferences button");
  let aboutPrefPromise = openAboutPrefPromise(win);
  EventUtils.synthesizeMouseAtCenter(prefBtn, {}, win);
  await aboutPrefPromise;
  let aboutPrefTab = win.gBrowser.selectedTab;
  let prefDoc = win.gBrowser.selectedBrowser.contentDocument;
  let siteDataGroup;
  await TestUtils.waitForCondition(() => {
    siteDataGroup = prefDoc.querySelector(
      'setting-group[groupid="cookiesAndSiteData"]'
    );
    return siteDataGroup && BrowserTestUtils.isVisible(siteDataGroup);
  }, "siteDataGroup should be visible on the privacy pane");
  is_element_visible(
    siteDataGroup,
    "Should open to the siteDataGroup section in about:preferences"
  );
  BrowserTestUtils.removeTab(aboutPrefTab);
  BrowserTestUtils.removeTab(tab);
});

// Test not displaying the 2nd notification if one is already being displayed
add_task(async function () {
  const win = Services.wm.getMostRecentWindow("navigator:browser");
  const TEST_NOTIFICATION_INTERVAL_MS = 0;
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.storageManager.pressureNotification.minIntervalMS",
        TEST_NOTIFICATION_INTERVAL_MS,
      ],
    ],
  });

  await notifyStoragePressure();
  await notifyStoragePressure();
  let allNotifications = win.gNotificationBox.allNotifications;
  let pressureNotificationCount = 0;
  allNotifications.forEach(notification => {
    if (notification.getAttribute("value") == "storage-pressure-notification") {
      pressureNotificationCount++;
    }
  });
  is(
    pressureNotificationCount,
    1,
    "Should not display the 2nd notification when there is already one"
  );
  win.gNotificationBox.removeAllNotifications();
});

add_task(async function cleanup() {
  const win = Services.wm.getMostRecentWindow("navigator:browser");
  await BrowserTestUtils.closeWindow(win);
});
