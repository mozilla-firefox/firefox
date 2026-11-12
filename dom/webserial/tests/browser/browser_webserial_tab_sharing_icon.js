/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_URL =
  "https://example.com/document-builder.sjs?html=<h1>Test serial tab sharing icon</h1>";
const BLANK_URL =
  "https://example.com/browser/dom/webserial/tests/browser/blank.html";
async function requestAndOpenPort(browser, filters = []) {
  await SpecialPowers.spawn(browser, [filters], async aFilters => {
    content.navigator.serial.autoselectPorts = true;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.testPort = await content.navigator.serial.requestPort({
      filters: aFilters,
    });
    await content.testPort.open({ baudRate: 9600 });
  });
}

async function closePort(browser) {
  await SpecialPowers.spawn(browser, [], async () => {
    await content.testPort.close();
  });
}

function waitForTabSharing(tab, value, msg) {
  return TestUtils.waitForCondition(
    () => tab.getAttribute("sharing") == value,
    msg
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.webserial.gated", false]],
  });

  registerCleanupFunction(() => {
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
  });
});

add_task(async function testTabSharingIcon() {
  info("Test that an open serial port marks the tab as sharing");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  ok(!tab.hasAttribute("sharing"), "Tab should not be sharing initially");

  await requestAndOpenPort(tab.linkedBrowser);
  await waitForTabSharing(tab, "serial", "Tab should be sharing serial");
  is(
    tab.sharingIcon.getAttribute("sharing"),
    "serial",
    "Tab sharing icon overlay should reflect serial sharing"
  );
  ok(
    !BrowserTestUtils.isVisible(tab.sharingIcon),
    "Sharing icon overlay should be hidden on the selected tab"
  );

  let otherTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    BLANK_URL
  );
  ok(
    BrowserTestUtils.isVisible(tab.sharingIcon),
    "Sharing icon overlay should be visible on the unselected tab"
  );
  BrowserTestUtils.removeTab(otherTab);

  await closePort(tab.linkedBrowser);
  await waitForTabSharing(
    tab,
    null,
    "Tab should stop sharing after the port is closed"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testTabSharingIconHiddenOnNavigation() {
  info("Test that navigating away clears the tab sharing attribute");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  await requestAndOpenPort(tab.linkedBrowser);
  await waitForTabSharing(tab, "serial", "Tab should be sharing serial");

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, BLANK_URL);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await waitForTabSharing(
    tab,
    null,
    "Tab should stop sharing after navigating away"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testTabSharingIconAfterRevoke() {
  info("Test that revoking the permission clears the tab sharing attribute");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  await requestAndOpenPort(tab.linkedBrowser);
  await waitForTabSharing(tab, "serial", "Tab should be sharing serial");

  gPermissionPanel.openPopup();
  await BrowserTestUtils.waitForEvent(
    gPermissionPanel._permissionPopup,
    "popupshown"
  );
  let removeButton = document.querySelector(
    ".permission-popup-permission-item-serial .permission-popup-permission-remove-button"
  );
  ok(removeButton, "Serial permission remove button should exist");
  removeButton.click();

  await waitForTabSharing(
    tab,
    null,
    "Tab should stop sharing after the permission is revoked"
  );

  let hidden = BrowserTestUtils.waitForEvent(
    gPermissionPanel._permissionPopup,
    "popuphidden"
  );
  gPermissionPanel._permissionPopup.hidePopup();
  await hidden;

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testWebRTCSharingTakesPriority() {
  info("Test that WebRTC sharing takes priority over serial on the tab");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  let browser = tab.linkedBrowser;
  await requestAndOpenPort(browser);
  await waitForTabSharing(tab, "serial", "Tab should be sharing serial");

  gBrowser.updateBrowserSharing(browser, {
    webRTC: { sharing: "camera", camera: true },
  });
  is(tab.getAttribute("sharing"), "camera", "Camera should take priority");

  gBrowser.updateBrowserSharing(browser, {
    webRTC: { sharing: "camera", camera: true, paused: true },
  });
  is(
    tab.getAttribute("sharing"),
    "serial",
    "Serial should show while the camera is paused"
  );

  gBrowser.updateBrowserSharing(browser, { webRTC: null });
  is(
    tab.getAttribute("sharing"),
    "serial",
    "Serial should show after the camera stops"
  );

  gBrowser.updateBrowserSharing(browser, {
    webRTC: { sharing: "screen", screen: "Screen" },
  });
  await closePort(browser);
  await TestUtils.waitForCondition(
    () => !browser._sharingState.serial,
    "Serial sharing state should be cleared"
  );
  is(
    tab.getAttribute("sharing"),
    "screen",
    "Closing the serial port should not clear WebRTC sharing"
  );

  gBrowser.updateBrowserSharing(browser, { webRTC: null });
  ok(!tab.hasAttribute("sharing"), "Tab should no longer be sharing");

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testTabSharingIconAfterTearOff() {
  info("Test that the open port count follows a tab torn off into a window");

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, TEST_URL);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.navigator.serial.autoselectPorts = true;
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.testPort1 = await content.navigator.serial.requestPort({
      filters: [{ usbVendorId: 0x2341, usbProductId: 0x0043 }],
    });
    SpecialPowers.wrap(content.document).notifyUserGestureActivation();
    content.testPort2 = await content.navigator.serial.requestPort({
      filters: [{ usbVendorId: 0x0403, usbProductId: 0x6002 }],
    });
    await content.testPort1.open({ baudRate: 9600 });
    await content.testPort2.open({ baudRate: 9600 });
  });
  await waitForTabSharing(tab, "serial", "Tab should be sharing serial");

  let delayedStartup = TestUtils.topicObserved(
    "browser-delayed-startup-finished"
  );
  let win = gBrowser.replaceTabWithWindow(tab);
  await delayedStartup;
  let newTab = win.gBrowser.selectedTab;
  let newBrowser = newTab.linkedBrowser;
  await waitForTabSharing(
    newTab,
    "serial",
    "Torn off tab should still be sharing serial"
  );

  let portClosed = TestUtils.topicObserved(
    "serial-device-state-changed",
    subject =>
      !subject.QueryInterface(Ci.nsIPropertyBag2).getPropertyAsBool("connected")
  );
  await SpecialPowers.spawn(newBrowser, [], async () => {
    await content.testPort1.close();
  });
  await portClosed;
  await TestUtils.waitForTick();
  is(
    newTab.getAttribute("sharing"),
    "serial",
    "Tab should still be sharing while one port remains open"
  );

  await SpecialPowers.spawn(newBrowser, [], async () => {
    await content.testPort2.close();
  });
  await waitForTabSharing(
    newTab,
    null,
    "Tab should stop sharing after both ports are closed"
  );

  await BrowserTestUtils.closeWindow(win);
});
