const { AddonManagerPrivate } = ChromeUtils.importESModule(
  "resource://gre/modules/AddonManager.sys.mjs"
);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

AddonTestUtils.initMochitest(this);

const ID = "update2@tests.mozilla.org";
const ID_ICON = "update_icon2@tests.mozilla.org";
const ID_PERMS = "update_perms@tests.mozilla.org";
const ID_LEGACY = "legacy_update@tests.mozilla.org";
const FAKE_INSTALL_TELEMETRY_SOURCE = "fake-install-source";

requestLongerTimeout(2);

function promiseViewLoaded(tab, viewid) {
  let win = tab.linkedBrowser.contentWindow;
  if (
    win.gViewController &&
    !win.gViewController.isLoading &&
    win.gViewController.currentViewId == viewid
  ) {
    return Promise.resolve();
  }

  return waitAboutAddonsViewLoaded(win.document);
}

function getBadgeStatus() {
  let menuButton = document.getElementById("PanelUI-menu-button");
  return menuButton.getAttribute("badge-status");
}

// Set some prefs that apply to all the tests in this file
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // We don't have pre-pinned certificates for the local mochitest server
      ["extensions.install.requireBuiltInCerts", false],
      ["extensions.update.requireBuiltInCerts", false],
    ],
  });

  // Navigate away from the initial page so that about:addons always
  // opens in a new tab during tests
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    "about:robots"
  );
  await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

  registerCleanupFunction(async function () {
    // Return to about:blank when we're done
    BrowserTestUtils.startLoadingURIString(
      gBrowser.selectedBrowser,
      "about:blank"
    );
    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser, {
      wantLoad: "about:blank",
    });
  });
});

// Helper function to test background updates.
async function backgroundUpdateTest(url, id, checkIconFn) {
  await SpecialPowers.pushPrefEnv({
    set: [
      // Turn on background updates
      ["extensions.update.enabled", true],

      // Point updates to the local mochitest server
      [
        "extensions.update.background.url",
        `${BASE}/browser_webext_update.json`,
      ],
    ],
  });

  Services.fog.testResetFOG();

  // Install version 1.0 of the test extension
  let addon = await promiseInstallAddon(url, {
    source: FAKE_INSTALL_TELEMETRY_SOURCE,
  });
  let addonId = addon.id;

  ok(addon, "Addon was installed");
  is(getBadgeStatus(), null, "Should not start out with an addon alert badge");

  // Trigger an update check and wait for the update for this addon
  // to be downloaded.
  let updatePromise = promiseInstallEvent(addon, "onDownloadEnded");

  AddonManagerPrivate.backgroundUpdateCheck();
  await updatePromise;
  await TestUtils.waitForCondition(
    () => ExtensionsUI.updates.size == 1,
    "Wait for the pending update notification"
  );

  is(getBadgeStatus(), "addon-alert", "Should have addon alert badge");

  // Find the menu entry for the update
  await gCUITestUtils.openMainMenu();

  let addons = PanelUI.addonNotificationContainer;
  is(addons.children.length, 1, "Have a menu entry for the update");

  // Click the menu item
  let tabPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:addons");
  let popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  addons.children[0].click();

  // The click should hide the main menu. This is currently synchronous.
  Assert.notEqual(
    PanelUI.panel.state,
    "open",
    "Main menu is closed or closing."
  );

  // about:addons should load and go to the list of extensions
  let tab = await tabPromise;
  is(
    tab.linkedBrowser.currentURI.spec,
    "about:addons",
    "Browser is at about:addons"
  );

  const VIEW = "addons://list/extension";
  await promiseViewLoaded(tab, VIEW);
  let win = tab.linkedBrowser.contentWindow;
  ok(!win.gViewController.isLoading, "about:addons view is fully loaded");
  is(
    win.gViewController.currentViewId,
    VIEW,
    "about:addons is at extensions list"
  );

  // Wait for the permission prompt, check the contents
  let panel = await popupPromise;
  checkIconFn(panel.getAttribute("icon"));

  // The original extension has 1 promptable permission and the new one
  // has 2 (history and <all_urls>) plus 1 non-promptable permission (cookies).
  // So we should only see the 1 new promptable permission in the notification.
  let permissionsListEl = document.getElementById(
    "addon-webext-perm-list-required"
  );
  ok(!permissionsListEl.hidden, "Permissions list is visible");
  ok(permissionsListEl.textContent, "Permissions list contains text");
  is(
    permissionsListEl.childElementCount,
    1,
    "Expect only 1 permission entry in the Permissions list"
  );

  // Cancel the update.
  panel.secondaryButton.click();

  addon = await AddonManager.getAddonByID(id);
  is(addon.version, "1.0", "Should still be running the old version");

  BrowserTestUtils.removeTab(tab);

  // Alert badge and hamburger menu items should be gone
  is(getBadgeStatus(), null, "Addon alert badge should be gone");

  await gCUITestUtils.openMainMenu();
  addons = PanelUI.addonNotificationContainer;
  is(addons.children.length, 0, "Update menu entries should be gone");
  await gCUITestUtils.hideMainMenu();

  // Re-check for an update
  updatePromise = promiseInstallEvent(addon, "onDownloadEnded");
  await AddonManagerPrivate.backgroundUpdateCheck();
  await updatePromise;
  await TestUtils.waitForCondition(
    () => ExtensionsUI.updates.size == 1,
    "Wait for the pending update notification"
  );

  is(getBadgeStatus(), "addon-alert", "Should have addon alert badge");

  // Find the menu entry for the update
  await gCUITestUtils.openMainMenu();

  addons = PanelUI.addonNotificationContainer;
  is(addons.children.length, 1, "Have a menu entry for the update");

  // Click the menu item
  tabPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:addons", true);
  popupPromise = promisePopupNotificationShown("addon-webext-permissions");

  addons.children[0].click();

  // Wait for about:addons to load
  tab = await tabPromise;
  is(tab.linkedBrowser.currentURI.spec, "about:addons");

  await promiseViewLoaded(tab, VIEW);
  win = tab.linkedBrowser.contentWindow;
  ok(!win.gViewController.isLoading, "about:addons view is fully loaded");
  is(
    win.gViewController.currentViewId,
    VIEW,
    "about:addons is at extensions list"
  );

  // Wait for the permission prompt and accept it this time
  updatePromise = waitForUpdate(addon);
  panel = await popupPromise;
  panel.button.click();

  addon = await updatePromise;
  is(addon.version, "2.0", "Should have upgraded to the new version");

  BrowserTestUtils.removeTab(tab);

  is(getBadgeStatus(), null, "Addon alert badge should be gone");

  await addon.uninstall();
  await SpecialPowers.popPrefEnv();

  let gleanUpdates = AddonTestUtils.getAMGleanEvents("update");

  const expectedSteps = [
    // First update (cancelled).
    "started",
    "download_started",
    "download_completed",
    "permissions_prompt",
    "cancelled",
    // Second update (completed).
    "started",
    "download_started",
    "download_completed",
    "permissions_prompt",
    "completed",
  ];

  Assert.deepEqual(
    expectedSteps,
    gleanUpdates.map(evt => evt.step),
    "Got the steps from the collected Glean events."
  );

  const object = "extension";
  const baseExtra = {
    addon_id: addonId,
    source: FAKE_INSTALL_TELEMETRY_SOURCE,
    step: "permissions_prompt",
    updated_from: "app",
  };

  // Expect the telemetry events to have num_strings set to 1, as only the origin permissions is going
  // to be listed in the permission prompt.
  Assert.deepEqual(
    gleanUpdates.filter(e => e.step === "permissions_prompt"),
    [
      { ...baseExtra, addon_type: object, num_strings: "1" },
      { ...baseExtra, addon_type: object, num_strings: "1" },
    ],
    "Got the expected permission_prompt events from Glean."
  );
}

function checkDefaultIcon(icon) {
  is(
    icon,
    "chrome://mozapps/skin/extensions/extensionGeneric.svg",
    "Popup has the default extension icon"
  );
}

add_task(() =>
  backgroundUpdateTest(
    `${BASE}/browser_webext_update1.xpi`,
    ID,
    checkDefaultIcon
  )
);
function checkNonDefaultIcon(icon) {
  // The icon should come from the extension, don't bother with the precise
  // path, just make sure we've got a jar url pointing to the right path
  // inside the jar.
  ok(icon.startsWith("jar:file://"), "Icon is a jar url");
  ok(icon.endsWith("/icon.png"), "Icon is icon.png inside a jar");
}

add_task(() =>
  backgroundUpdateTest(
    `${BASE}/browser_webext_update_icon1.xpi`,
    ID_ICON,
    checkNonDefaultIcon
  )
);

function runShowPermissionsPrompt(browser) {
  return ExtensionsUI.showPermissionsPrompt(browser, {
    msgs: [],
    header: "Test permission prompt",
    addonName: "Test add-on",
    acceptText: "Accept",
    acceptKey: "A",
    cancelText: "Cancel",
    cancelKey: "C",
  });
}

add_task(async function test_closed_tab_rejects_queued_permission_prompt() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
  let browser = tab.linkedBrowser;

  let popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  let firstPrompt = runShowPermissionsPrompt(browser);
  await popupPromise;
  let queuedPrompt = runShowPermissionsPrompt(browser);

  BrowserTestUtils.removeTab(tab);

  is(await firstPrompt, false, "The shown prompt is declined on tab close");
  is(
    await queuedPrompt,
    false,
    "A queued prompt is rejected after its tab closes"
  );
  is(
    PopupNotifications.getNotification("addon-webext-permissions", browser),
    null,
    "No permissions notifications should be found for the closed tab"
  );
  await TestUtils.waitForCondition(
    () => !ExtensionsUI.pendingNotifications.has(browser),
    "The closed tab does not retain a reserved slot in ExtensionsUI.pendingNotifications"
  );
});

add_task(async function test_notification_show_exception_releases_queue_slot() {
  let browser = gBrowser.selectedBrowser;

  let sandbox = sinon.createSandbox();
  sandbox
    .stub(PopupNotifications, "show")
    .throws(new Error("Mock PopupNotifications.show exception"));
  let failedPrompt = runShowPermissionsPrompt(browser);
  await Assert.rejects(
    failedPrompt,
    /Mock PopupNotifications.show exception/,
    "The prompt error is propagated"
  );
  sandbox.restore();
  await TestUtils.waitForCondition(
    () => !ExtensionsUI.pendingNotifications.has(browser),
    "The rejected prompt releases its queue slot"
  );

  let popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  let prompt = runShowPermissionsPrompt(browser);
  await popupPromise;
  let notification = PopupNotifications.getNotification(
    "addon-webext-permissions",
    browser
  );
  notification.owner.remove(notification, true);
  is(await prompt, false, "A later prompt can use the released queue slot");
});

function findUpdatePermissionNotification(addonId, targetWindow = null) {
  let windows = targetWindow
    ? [targetWindow]
    : Services.wm.getEnumerator("navigator:browser");
  for (let win of windows) {
    for (let browser of win.gBrowser.browsers) {
      let notification = win.PopupNotifications.getNotification(
        "addon-webext-permissions",
        browser
      );
      if (notification?.options.addonId == addonId) {
        return notification;
      }
    }
  }
  return null;
}

async function verifyLatePromptRejected(existingAddon, message) {
  let promptHandled = Promise.withResolvers();
  Services.obs.notifyObservers(
    {
      wrappedJSObject: {
        addon: existingAddon,
        existingAddon,
        permissions: {
          permissions: [],
          origins: ["<all_urls>"],
          data_collection: [],
        },
        resolve: () => promptHandled.resolve("resolved"),
        reject: () => promptHandled.resolve("rejected"),
      },
    },
    "webextension-update-permission-prompt"
  );
  is(await promptHandled.promise, "rejected", message);
}

async function installTestAddon() {
  let addon = await promiseInstallAddon(`${BASE}/browser_webext_update1.xpi`);
  let { id } = addon;
  registerCleanupFunction(async () => {
    let installed = await AddonManager.getAddonByID(id);
    await installed?.uninstall();
  });
  return addon;
}

async function installAddonWithPendingUpdate() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.update.enabled", true],
      [
        "extensions.update.background.url",
        `${BASE}/browser_webext_update.json`,
      ],
    ],
  });

  let addon = await installTestAddon();
  let updatePromise = promiseInstallEvent(addon, "onDownloadEnded");
  AddonManagerPrivate.backgroundUpdateCheck();
  await updatePromise;
  await TestUtils.waitForCondition(
    () => ExtensionsUI.updates.size == 1,
    "Wait for the pending update notification"
  );
  let cleanup = async () => {
    let installed = await AddonManager.getAddonByID(addon.id);
    await installed?.uninstall();
    await SpecialPowers.popPrefEnv();
  };
  return { addon, update: Array.from(ExtensionsUI.updates)[0], cleanup };
}

async function showUpdatePromptInNewWindow(addon, update) {
  let tabPromise = BrowserTestUtils.waitForNewTab(gBrowser, "about:addons");
  let popupPromise = promisePopupNotificationShown("addon-webext-permissions");
  let showUpdatePromise = ExtensionsUI.showUpdate(gBrowser, update);
  await popupPromise;
  let tab = await tabPromise;
  let originalBrowser = tab.linkedBrowser;

  let notification = PopupNotifications.getNotification(
    "addon-webext-permissions",
    originalBrowser
  );
  // Keep the prompt while tab tear-off dismisses the notification panel.
  notification.options.removeOnDismissal = false;
  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  gBrowser.replaceTabWithWindow(tab);
  let newWindow = await newWindowPromise;
  // Pending idle tasks keep the window alive and are reported as a shutdown
  // leak if it is closed first.
  await newWindow.gBrowserInit.idleTasksFinished.promise;
  await TestUtils.waitForCondition(
    () => findUpdatePermissionNotification(addon.id, newWindow),
    "Wait for the update permission prompt to move to the new window"
  );
  return { newWindow, originalBrowser, showUpdatePromise };
}

async function assertNoUpdateMenuEntries() {
  is(getBadgeStatus(), null, "Addon alert badge should be gone");
  is(ExtensionsUI.updates.size, 0, "Should have no pending updates");
  await gCUITestUtils.openMainMenu();
  is(
    PanelUI.addonNotificationContainer.children.length,
    0,
    "Update menu entries should be gone"
  );
  await gCUITestUtils.hideMainMenu();
}

add_task(async function test_uninstall_dismisses_moved_update_prompt() {
  let { addon, update, cleanup } = await installAddonWithPendingUpdate();

  is(getBadgeStatus(), "addon-alert", "Should have addon alert badge");
  await gCUITestUtils.openMainMenu();
  is(
    PanelUI.addonNotificationContainer.children.length,
    1,
    "Should have one update menu entry"
  );
  await gCUITestUtils.hideMainMenu();

  let { newWindow, showUpdatePromise } = await showUpdatePromptInNewWindow(
    addon,
    update
  );
  ok(
    findUpdatePermissionNotification(addon.id, newWindow),
    "The update permission prompt is in the new window"
  );
  is(
    findUpdatePermissionNotification(addon.id, window),
    null,
    "The update permission prompt is no longer in the original window"
  );

  await addon.uninstall(true);
  await showUpdatePromise;
  is(
    findUpdatePermissionNotification(addon.id),
    null,
    "Should close an open permission prompt after its tab moves windows"
  );
  await assertNoUpdateMenuEntries();

  await BrowserTestUtils.closeWindow(newWindow);
  await cleanup();
});

add_task(async function test_late_update_prompt_during_pending_uninstall() {
  let addon = await installTestAddon();
  await addon.uninstall(true);
  await verifyLatePromptRejected(
    addon,
    "Should reject an update prompt received while uninstall is pending"
  );
  await assertNoUpdateMenuEntries();
  await addon.uninstall();
});

add_task(async function test_late_update_prompt_after_uninstall() {
  let addon = await installTestAddon();
  await addon.uninstall();
  await verifyLatePromptRejected(
    addon,
    "Should reject an update prompt received after uninstall completes"
  );
  await assertNoUpdateMenuEntries();
});

add_task(async function test_closing_prompt_window_settles_pending_update() {
  let { addon, update, cleanup } = await installAddonWithPendingUpdate();
  let { newWindow, originalBrowser, showUpdatePromise } =
    await showUpdatePromptInNewWindow(addon, update);

  await BrowserTestUtils.closeWindow(newWindow);
  await showUpdatePromise;
  await TestUtils.waitForCondition(
    () => !ExtensionsUI.pendingNotifications.has(originalBrowser),
    "Closing the prompt window releases its queue slot"
  );
  await assertNoUpdateMenuEntries();

  await cleanup();
});
