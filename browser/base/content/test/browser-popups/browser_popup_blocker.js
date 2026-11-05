/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const baseURL = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

function clearAllPermissionsByPrefix(aPrefix) {
  for (let perm of Services.perms.all) {
    if (perm.type.startsWith(aPrefix)) {
      Services.perms.removePermission(perm);
    }
  }
}

add_setup(async function () {
  // Enable the popup blocker.
  await SpecialPowers.pushPrefEnv({
    set: [["dom.disable_open_during_load", true]],
  });
});

// Tests that we show a special message when popup blocking exceeds
// a certain maximum of popups per page.
add_task(async function test_maximum_reported_blocks() {
  Services.prefs.setIntPref("privacy.popups.maxReported", 5);

  // Open the test page.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    baseURL + "popup_blocker_10_popups.html"
  );

  // Wait for the popup-blocked notification.
  let notification = await TestUtils.waitForCondition(() => {
    let tempNotif = gBrowser
      .getNotificationBox()
      .getNotificationWithValue("popup-blocked");

    // The textContent in the notification object is contains
    // blank spaces as placeholder if there is no value in it
    if (tempNotif?.messageText.textContent.trim().length) {
      return tempNotif;
    }
    return false;
  });

  // Slightly hacky way to ensure we show the correct message in this case.
  ok(
    notification.messageText.textContent.includes("more than"),
    "Notification label has 'more than'"
  );
  ok(
    notification.messageText.textContent.includes("5"),
    "Notification label shows the maximum number of popups"
  );

  gBrowser.removeTab(tab);

  Services.prefs.clearUserPref("privacy.popups.maxReported");
});

add_task(async function test_opening_blocked_popups() {
  // Open the test page.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    baseURL + "popup_blocker.html"
  );

  await testPopupBlockingToolbar(tab);
});

add_task(async function test_opening_blocked_popups_privateWindow() {
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  // Open the test page.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    baseURL + "popup_blocker.html"
  );
  await testPopupBlockingToolbar(tab);
  await BrowserTestUtils.closeWindow(win);
});

// This is a test for Bug 1988311.
// Make sure that everything also functions correctly on special pages,
// such as "about:privatebrowsing".
add_task(async function test_opening_blocked_popups_about_privatebrowsing() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:privatebrowsing"
  );

  const browser = tab.linkedBrowser;
  const uri = Services.io.newURI(
    "javascript:" +
      `window.open("${baseURL}" + "popup_blocker_a.html");` +
      `window.open("${baseURL}" + "popup_blocker_b.html");`
  );
  const triggeringPrincipal =
    Services.scriptSecurityManager.getSystemPrincipal();

  browser.loadURI(uri, { triggeringPrincipal });
  await testPopupBlockingToolbar(tab);
});

// Bug 2006600.
// When a notification has been dismissed by a user, it should not appear
// again when switching to a different tab and back.
add_task(async function test_dismissed_notification_switch_tabs() {
  // Open the test page.
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    baseURL + "popup_blocker.html"
  );

  // Wait for the notification.
  let notification;
  await TestUtils.waitForCondition(
    () =>
      (notification = gBrowser
        .getNotificationBox()
        .getNotificationWithValue("popup-blocked"))
  );

  // Click dismiss button.
  const mozButton = notification.shadowRoot.querySelector("moz-button.close");
  mozButton.click();

  // Open a new (foreground) tab and switch back.
  const differentTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  await BrowserTestUtils.switchTab(gBrowser, tab);

  // Make sure no notification appears.
  try {
    await TestUtils.waitForCondition(
      () =>
        (notification = gBrowser
          .getNotificationBox()
          .getNotificationWithValue("popup-blocked")),
      null,
      50,
      10
    );
  } catch (e) {
    notification = null;
  }
  ok(!notification, "Notification should not reappear");

  gBrowser.removeTab(tab);
  gBrowser.removeTab(differentTab);
});

// Bug 2025170.
// "Allow" should unblock all popups across multiple browsing contexts
// without erroring on out-of-bounds per-document indices.
add_task(async function test_bug2025170_allow_all() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [baseURL + "popup_blocker.html"],
    uri => {
      for (let i = 0; i < 2; i++) {
        let iframe = content.document.createElement("iframe");
        iframe.src = uri;
        content.document.body.appendChild(iframe);
      }
    }
  );

  // Because popup_blocker.html is calling window.open() with target
  // window names, we expect only two new tabs.
  await testPopupBlockingToolbar(
    tab,
    /*expectedBlocked=*/ 4,
    /*expectedOpened=*/ 2
  );
});

// Bug 2025170.
// Make sure the correct popup is opened when the blocked popup list
// spans multiple browsing contexts.
add_task(async function test_bug2025170_unblock_popup() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [baseURL + "popup_blocker.html"],
    uri => {
      for (let i = 0; i < 2; i++) {
        let iframe = content.document.createElement("iframe");
        iframe.src = uri;
        content.document.body.appendChild(iframe);
      }
    }
  );

  // Wait for the popup-blocked notification.
  let notification;
  await TestUtils.waitForCondition(
    () =>
      (notification = gBrowser
        .getNotificationBox()
        .getNotificationWithValue("popup-blocked"))
  );

  // Show the menu.
  const popupShown = BrowserTestUtils.waitForEvent(window, "popupshown");
  const popupFilled = waitForBlockedPopups(4, { doc: document });
  EventUtils.synthesizeMouseAtCenter(
    notification.buttonContainer.querySelector("button"),
    {},
    window
  );

  // Wait for the menu.
  const popupEvent = await popupShown;
  const menu = popupEvent.target;
  is(menu.id, "blockedPopupOptions", "Blocked popup menu shown");

  await popupFilled;

  const popupItems = menu.querySelectorAll("[popupReportIndex]");
  is(popupItems.length, 4, "Should have 4 blocked popup items");

  // Track new tabs.
  const popupTabs = [];
  const onTabOpen = e => popupTabs.push(e.target);
  gBrowser.tabContainer.addEventListener("TabOpen", onTabOpen);

  // The last item in the flattened list appears first in DOM order.
  // Without the fix, its popupReportIndex would be out of bounds.
  popupItems[0].doCommand();

  await TestUtils.waitForCondition(
    () =>
      popupTabs.length == 1 &&
      popupTabs[0].linkedBrowser.currentURI.spec != "about:blank",
    "Waiting for popup tab to open"
  );
  ok(
    popupTabs[0].linkedBrowser.currentURI.spec.endsWith("popup_blocker_b.html"),
    "Should have opened popup_blocker_b.html"
  );

  gBrowser.tabContainer.removeEventListener("TabOpen", onTabOpen);

  // Clean up.
  menu.hidePopup();
  BrowserTestUtils.removeTab(popupTabs[0]);
  BrowserTestUtils.removeTab(tab);
});

async function testPopupBlockingToolbar(
  tab,
  expectedBlocked = 2,
  expectedOpened = 2
) {
  let win = tab.documentGlobal;
  // Wait for the popup-blocked notification.
  let notification;
  await TestUtils.waitForCondition(
    () =>
      (notification = win.gBrowser
        .getNotificationBox()
        .getNotificationWithValue("popup-blocked"))
  );

  // Show the menu.
  let popupShown = BrowserTestUtils.waitForEvent(win, "popupshown");
  let popupFilled = waitForBlockedPopups(expectedBlocked, {
    doc: win.document,
  });
  EventUtils.synthesizeMouseAtCenter(
    notification.buttonContainer.querySelector("button"),
    {},
    win
  );
  let popup_event = await popupShown;
  let menu = popup_event.target;
  is(menu.id, "blockedPopupOptions", "Blocked popup menu shown");

  await popupFilled;

  // Pressing "allow" should open all blocked popups.
  let popupTabs = [];
  function onTabOpen(event) {
    popupTabs.push(event.target);
  }
  win.gBrowser.tabContainer.addEventListener("TabOpen", onTabOpen);

  // Press the button.
  let allow = win.document.getElementById("blockedPopupAllowSite");
  allow.doCommand();
  await TestUtils.waitForCondition(
    () =>
      popupTabs.length == expectedOpened &&
      popupTabs.every(
        aTab => aTab.linkedBrowser.currentURI.spec != "about:blank"
      )
  );

  win.gBrowser.tabContainer.removeEventListener("TabOpen", onTabOpen);

  ok(
    popupTabs[0].linkedBrowser.currentURI.spec.endsWith("popup_blocker_a.html"),
    "Popup a"
  );
  ok(
    popupTabs[1].linkedBrowser.currentURI.spec.endsWith("popup_blocker_b.html"),
    "Popup b"
  );

  let popupPerms = Services.perms.getAllByTypeSince("popup", 0);
  is(popupPerms.length, 1, "One popup permission added");
  let popupPerm = popupPerms[0];
  let expectedExpireType = PrivateBrowsingUtils.isWindowPrivate(win)
    ? Services.perms.EXPIRE_SESSION
    : Services.perms.EXPIRE_NEVER;
  is(
    popupPerm.expireType,
    expectedExpireType,
    "Check expireType is appropriate for the window"
  );

  // Clean up.
  win.gBrowser.removeTab(tab);
  for (let popup of popupTabs) {
    win.gBrowser.removeTab(popup);
  }
  clearAllPermissionsByPrefix("popup");
  // Ensure the menu closes.
  menu.hidePopup();
}
