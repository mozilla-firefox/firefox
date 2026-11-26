"use strict";

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PERMISSIONS_URL =
  "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml";
const URL = "http://www.example.com";
const URI = Services.io.newURI(URL);
var sitePermissionsDialog;
let settingsButtonMap = {
  "desktop-notification": "notificationSettingsButton",
  speaker: "speakerSettingsButton",
};

function checkMenulistPermissionItem(origin, state) {
  let doc = sitePermissionsDialog.document;
  let richlistitem = doc.getElementById("permissionsBox").itemChildren[0];

  Assert.equal(richlistitem.querySelector(".website-name").textContent, origin);
  Assert.equal(richlistitem.querySelector("menulist").value, state);
}

async function openPermissionsDialog(permissionType) {
  let dialogOpened = promiseLoadSubDialog(PERMISSIONS_URL);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [settingsButtonMap[permissionType]],
    function (settingsButtonId) {
      let doc = content.document;
      let settingsButton = doc.getElementById(settingsButtonId);
      settingsButton.click();
    }
  );

  sitePermissionsDialog = await dialogOpened;
  await sitePermissionsDialog.document.mozSubdialogReady;
}

add_task(async function openSitePermissionsDialog() {
  // Opening about:preferences takes a long time
  requestLongerTimeout(2);

  await openPermissionsPane({ leaveOpen: true });
  await openPermissionsDialog("desktop-notification");
});

add_task(async function addPermission() {
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  // First item in the richlistbox contains column headers.
  Assert.equal(
    richlistbox.itemCount,
    0,
    "Number of permission items is 0 initially"
  );

  // Add notification permission for a website.
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Observe the added permission changes in the dialog UI.
  Assert.equal(richlistbox.itemCount, 1);
  checkMenulistPermissionItem(URL, Services.perms.ALLOW_ACTION);

  PermissionTestUtils.remove(URI, "desktop-notification");
});

add_task(async function addPermissionPrivateBrowsing() {
  let privateBrowsingPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(URI, {
      privateBrowsingId: 1,
    });
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  Assert.equal(
    richlistbox.itemCount,
    0,
    "Number of permission items is 0 initially"
  );

  // Add a session permission for private browsing.
  PermissionTestUtils.add(
    privateBrowsingPrincipal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    Services.perms.EXPIRE_SESSION
  );

  // The permission should not show in the dialog UI.
  Assert.equal(richlistbox.itemCount, 0);

  PermissionTestUtils.remove(privateBrowsingPrincipal, "desktop-notification");

  // Add a permanent permission for private browsing
  // The permission manager will store it as EXPIRE_SESSION
  PermissionTestUtils.add(
    privateBrowsingPrincipal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // The permission should not show in the dialog UI.
  Assert.equal(richlistbox.itemCount, 0);

  PermissionTestUtils.remove(privateBrowsingPrincipal, "desktop-notification");
});

add_task(async function observePermissionChange() {
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Change the permission.
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.DENY_ACTION
  );

  checkMenulistPermissionItem(URL, Services.perms.DENY_ACTION);

  PermissionTestUtils.remove(URI, "desktop-notification");
});

add_task(async function observePermissionDelete() {
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  Assert.equal(
    richlistbox.itemCount,
    1,
    "The box contains one permission item initially"
  );

  PermissionTestUtils.remove(URI, "desktop-notification");

  Assert.equal(richlistbox.itemCount, 0);
});

add_task(async function onPermissionChange() {
  let doc = sitePermissionsDialog.document;
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Change the permission state in the UI.
  doc.getElementsByAttribute("value", SitePermissions.BLOCK)[0].doCommand();

  Assert.equal(
    PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION,
    "Permission state does not change before saving changes"
  );

  doc.querySelector("dialog").getButton("accept").click();

  await TestUtils.waitForCondition(
    () =>
      PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
        .capability == Services.perms.DENY_ACTION
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
});

add_task(async function onPermissionDelete() {
  await openPermissionsDialog("desktop-notification");

  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  richlistbox.selectItem(richlistbox.getItemAtIndex(0));
  doc.getElementById("removePermission").click();

  await TestUtils.waitForCondition(() => richlistbox.itemCount == 0);

  Assert.equal(
    PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION,
    "Permission is not deleted before saving changes"
  );

  doc.querySelector("dialog").getButton("accept").click();

  await TestUtils.waitForCondition(
    () =>
      PermissionTestUtils.getPermissionObject(URI, "desktop-notification") ==
      null
  );
});

add_task(async function onAllPermissionsDelete() {
  await openPermissionsDialog("desktop-notification");

  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  let u = Services.io.newURI("http://www.test.com");
  PermissionTestUtils.add(
    u,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  doc.getElementById("removeAllPermissions").click();
  await TestUtils.waitForCondition(() => richlistbox.itemCount == 0);

  Assert.equal(
    PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(u, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION,
    "Permissions are not deleted before saving changes"
  );

  doc.querySelector("dialog").getButton("accept").click();

  await TestUtils.waitForCondition(
    () =>
      PermissionTestUtils.getPermissionObject(URI, "desktop-notification") ==
        null &&
      PermissionTestUtils.getPermissionObject(u, "desktop-notification") == null
  );
});

add_task(async function onPermissionChangeAndDelete() {
  await openPermissionsDialog("desktop-notification");

  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Change the permission state in the UI.
  doc.getElementsByAttribute("value", SitePermissions.BLOCK)[0].click();

  // Remove that permission by clicking the "Remove" button.
  richlistbox.selectItem(richlistbox.getItemAtIndex(0));
  doc.getElementById("removePermission").click();

  await TestUtils.waitForCondition(() => richlistbox.itemCount == 0);

  doc.querySelector("dialog").getButton("accept").click();

  await TestUtils.waitForCondition(
    () =>
      PermissionTestUtils.getPermissionObject(URI, "desktop-notification") ==
      null
  );
});

add_task(async function onPermissionChangeCancel() {
  await openPermissionsDialog("desktop-notification");

  let doc = sitePermissionsDialog.document;
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Change the permission state in the UI.
  doc.getElementsByAttribute("value", SitePermissions.BLOCK)[0].click();

  doc.querySelector("dialog").getButton("cancel").click();

  Assert.equal(
    PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION,
    "Permission state does not change on clicking cancel"
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
});

add_task(async function onPermissionDeleteCancel() {
  await openPermissionsDialog("desktop-notification");

  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Remove that permission by clicking the "Remove" button.
  richlistbox.selectItem(richlistbox.getItemAtIndex(0));
  doc.getElementById("removePermission").click();

  await TestUtils.waitForCondition(() => richlistbox.itemCount == 0);

  doc.querySelector("dialog").getButton("cancel").click();

  Assert.equal(
    PermissionTestUtils.getPermissionObject(URI, "desktop-notification")
      .capability,
    Services.perms.ALLOW_ACTION,
    "Permission state does not change on clicking cancel"
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
});

add_task(async function onSearch() {
  await openPermissionsDialog("desktop-notification");
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");
  let searchBox = doc.getElementById("searchBox");

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  searchBox.value = "www.example.com";

  let u = Services.io.newURI("http://www.test.com");
  PermissionTestUtils.add(
    u,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  Assert.equal(
    doc.getElementsByAttribute("origin", "http://www.test.com")[0],
    null
  );
  Assert.equal(
    doc.getElementsByAttribute("origin", "http://www.example.com")[0],
    richlistbox.getItemAtIndex(0)
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
  PermissionTestUtils.remove(u, "desktop-notification");

  doc.querySelector("dialog").getButton("cancel").click();
});

add_task(async function onPermissionsSort() {
  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  let u = Services.io.newURI("http://www.test.com");
  PermissionTestUtils.add(
    u,
    "desktop-notification",
    Services.perms.DENY_ACTION
  );

  await openPermissionsDialog("desktop-notification");
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  // Test default arrangement(Allow followed by Block).
  Assert.equal(
    richlistbox.getItemAtIndex(0).getAttribute("origin"),
    "http://www.example.com"
  );
  Assert.equal(
    richlistbox.getItemAtIndex(1).getAttribute("origin"),
    "http://www.test.com"
  );

  doc.getElementById("statusCol").click();

  // Test the rearrangement(Block followed by Allow).
  Assert.equal(
    richlistbox.getItemAtIndex(0).getAttribute("origin"),
    "http://www.test.com"
  );
  Assert.equal(
    richlistbox.getItemAtIndex(1).getAttribute("origin"),
    "http://www.example.com"
  );

  doc.getElementById("siteCol").click();

  // Test the rearrangement(Website names arranged in alphabhetical order).
  Assert.equal(
    richlistbox.getItemAtIndex(0).getAttribute("origin"),
    "http://www.example.com"
  );
  Assert.equal(
    richlistbox.getItemAtIndex(1).getAttribute("origin"),
    "http://www.test.com"
  );

  doc.getElementById("siteCol").click();

  // Test the rearrangement(Website names arranged in reverse alphabhetical order).
  Assert.equal(
    richlistbox.getItemAtIndex(0).getAttribute("origin"),
    "http://www.test.com"
  );
  Assert.equal(
    richlistbox.getItemAtIndex(1).getAttribute("origin"),
    "http://www.example.com"
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
  PermissionTestUtils.remove(u, "desktop-notification");

  doc.querySelector("dialog").getButton("cancel").click();
});

add_task(async function onPermissionDisable() {
  // Enable desktop-notification permission prompts.
  Services.prefs.setIntPref(
    "permissions.default.desktop-notification",
    SitePermissions.UNKNOWN
  );

  await openPermissionsDialog("desktop-notification");
  let doc = sitePermissionsDialog.document;

  // Check if the enabled state is reflected in the checkbox.
  let checkbox = doc.getElementById("permissionsDisableCheckbox");
  Assert.equal(checkbox.checked, false);

  // Disable permission and click on "Cancel".
  checkbox.checked = true;
  doc.querySelector("dialog").getButton("cancel").click();

  // Check that the permission is not disabled yet.
  let perm = Services.prefs.getIntPref(
    "permissions.default.desktop-notification"
  );
  Assert.equal(perm, SitePermissions.UNKNOWN);

  // Open the dialog once again.
  await openPermissionsDialog("desktop-notification");
  doc = sitePermissionsDialog.document;

  // Disable permission and save changes.
  checkbox = doc.getElementById("permissionsDisableCheckbox");
  checkbox.checked = true;
  doc.querySelector("dialog").getButton("accept").click();

  // Check if the permission is now disabled.
  perm = Services.prefs.getIntPref("permissions.default.desktop-notification");
  Assert.equal(perm, SitePermissions.BLOCK);

  // Open the dialog once again and check if the disabled state is still reflected in the checkbox.
  await openPermissionsDialog("desktop-notification");
  doc = sitePermissionsDialog.document;
  checkbox = doc.getElementById("permissionsDisableCheckbox");
  Assert.equal(checkbox.checked, true);

  // Close the dialog and clean up.
  doc.querySelector("dialog").getButton("cancel").click();
  Services.prefs.setIntPref(
    "permissions.default.desktop-notification",
    SitePermissions.UNKNOWN
  );
});

add_task(async function checkDefaultPermissionState() {
  // Set default permission state to ALLOW.
  Services.prefs.setIntPref(
    "permissions.default.desktop-notification",
    SitePermissions.ALLOW
  );

  await openPermissionsDialog("desktop-notification");
  let doc = sitePermissionsDialog.document;

  // Check if the enabled state is reflected in the checkbox.
  let checkbox = doc.getElementById("permissionsDisableCheckbox");
  Assert.equal(checkbox.checked, false);

  // Check the checkbox and then uncheck it.
  checkbox.checked = true;
  checkbox.checked = false;

  // Save changes.
  doc.querySelector("dialog").getButton("accept").click();

  // Check if the default permission state is retained (and not automatically set to SitePermissions.UNKNOWN).
  let state = Services.prefs.getIntPref(
    "permissions.default.desktop-notification"
  );
  Assert.equal(state, SitePermissions.ALLOW);

  // Clean up.
  Services.prefs.setIntPref(
    "permissions.default.desktop-notification",
    SitePermissions.UNKNOWN
  );
});

add_task(async function closedDeliveryCheckbox() {
  const AVAILABLE_PREF = "app.backgroundNotifications.helper.available";
  const ENABLED_PREF = "app.backgroundNotifications.helper.enabled";
  // The checkbox is only shown on Windows, where PushNotificationHelper is
  // packaged.
  const isWindows = AppConstants.platform == "win";

  let sinon, setEnabled;
  if (isWindows) {
    ({ sinon } = ChromeUtils.importESModule(
      "resource://testing-common/Sinon.sys.mjs"
    ));
    const { PushNotificationHelper } = ChromeUtils.importESModule(
      "resource://gre/modules/PushNotificationHelper.sys.mjs"
    );
    // Keep the real helper process from launching when both prefs are true.
    sinon.stub(PushNotificationHelper, "start");
    sinon.stub(PushNotificationHelper, "stop");
    setEnabled = sinon.spy(PushNotificationHelper, "setEnabled");
  }

  try {
    await openPermissionsDialog("desktop-notification");
    let doc = sitePermissionsDialog.document;
    ok(
      doc.getElementById("closedDeliveryBox").hidden,
      "Hidden where closed-browser delivery is unavailable"
    );
    doc.querySelector("dialog").getButton("cancel").click();

    await SpecialPowers.pushPrefEnv({ set: [[AVAILABLE_PREF, true]] });

    await openPermissionsDialog("desktop-notification");
    doc = sitePermissionsDialog.document;
    if (!isWindows) {
      ok(
        doc.getElementById("closedDeliveryBox").hidden,
        "Hidden off Windows even when available"
      );
      doc.querySelector("dialog").getButton("cancel").click();
      return;
    }

    ok(!doc.getElementById("closedDeliveryBox").hidden, "Shown when available");
    let checkbox = doc.getElementById("closedDeliveryCheckbox");
    ok(!checkbox.checked, "Unchecked by default");

    Services.prefs.setBoolPref(ENABLED_PREF, true);
    ok(checkbox.checked, "Follows a write made while the dialog is open");
    Services.prefs.setBoolPref(ENABLED_PREF, false);
    ok(!checkbox.checked, "Follows it back off");

    checkbox.checked = true;
    Services.prefs.setBoolPref(AVAILABLE_PREF, false);
    ok(
      doc.getElementById("closedDeliveryBox").hidden,
      "Hides when availability is withdrawn"
    );
    Services.prefs.setBoolPref(AVAILABLE_PREF, true);
    ok(checkbox.checked, "Availability changes keep the unsaved choice");

    doc.querySelector("dialog").getButton("cancel").click();
    ok(
      !Services.prefs.getBoolPref(ENABLED_PREF, false),
      "Cancel discards the change"
    );
    ok(setEnabled.notCalled, "Cancel does not call setEnabled");

    await openPermissionsDialog("desktop-notification");
    doc = sitePermissionsDialog.document;
    doc.getElementById("closedDeliveryCheckbox").checked = true;
    doc.querySelector("dialog").getButton("accept").click();
    ok(
      setEnabled.calledOnceWithExactly(true),
      "Save writes through setEnabled"
    );
    ok(Services.prefs.getBoolPref(ENABLED_PREF), "Pref is on after Save");

    await openPermissionsDialog("desktop-notification");
    doc = sitePermissionsDialog.document;
    ok(
      doc.getElementById("closedDeliveryCheckbox").checked,
      "Reflects the saved state"
    );
    doc.querySelector("dialog").getButton("accept").click();
    ok(setEnabled.calledOnce, "Saving without a change does not write again");
  } finally {
    Services.prefs.clearUserPref(ENABLED_PREF);
    await SpecialPowers.popPrefEnv();
    sinon?.restore();
  }
});

add_task(async function testTabBehaviour() {
  // Test tab behaviour inside the permissions setting dialog when site permissions are selected.
  // Only selected items in the richlistbox should be tabable for accessibility reasons.

  // Force tabfocus for all elements on OSX.
  SpecialPowers.pushPrefEnv({ set: [["accessibility.tabfocus", 7]] });

  PermissionTestUtils.add(
    URI,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  let u = Services.io.newURI("http://www.test.com");
  PermissionTestUtils.add(
    u,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await openPermissionsDialog("desktop-notification");
  let doc = sitePermissionsDialog.document;

  EventUtils.synthesizeKey("KEY_Tab", {}, sitePermissionsDialog);
  let richlistbox = doc.getElementById("permissionsBox");
  is(
    richlistbox,
    doc.activeElement.closest("#permissionsBox"),
    "The richlistbox is focused after pressing tab once."
  );

  EventUtils.synthesizeKey("KEY_ArrowDown", {}, sitePermissionsDialog);
  EventUtils.synthesizeKey("KEY_Tab", {}, sitePermissionsDialog);
  let menulist = doc
    .getElementById("permissionsBox")
    .itemChildren[1].getElementsByTagName("menulist")[0];
  is(
    menulist,
    doc.activeElement,
    "The menulist inside the selected richlistitem is focused now"
  );

  EventUtils.synthesizeKey("KEY_Tab", {}, sitePermissionsDialog);
  let removeButton = doc.getElementById("removePermission");
  is(
    removeButton,
    doc.activeElement,
    "The focus moves outside the richlistbox and onto the remove button"
  );

  PermissionTestUtils.remove(URI, "desktop-notification");
  PermissionTestUtils.remove(u, "desktop-notification");

  doc.querySelector("dialog").getButton("cancel").click();
});

add_task(async function addSpeakerPermission() {
  let enabled = Services.prefs.getBoolPref("media.setsinkid.enabled", false);
  let speakerButton = gBrowser.contentDocument.getElementById(
    "speakerSettingsButton"
  );
  Assert.equal(
    BrowserTestUtils.isVisible(speakerButton),
    enabled,
    "speakerButton visible"
  );
  if (!enabled) {
    return;
  }

  await openPermissionsDialog("speaker");
  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");
  Assert.equal(
    richlistbox.itemCount,
    0,
    "Number of permission items is 0 initially"
  );
  // Add an allow permission for a device.
  let deviceId = "DEVICE-ID";
  let devicePermissionId = `speaker^${deviceId}`;
  PermissionTestUtils.add(URI, devicePermissionId, Services.perms.ALLOW_ACTION);

  // Observe the added permission changes in the dialog UI.
  Assert.equal(richlistbox.itemCount, 1, "itemCount with allow");
  checkMenulistPermissionItem(URL, Services.perms.ALLOW_ACTION);

  // Check that an all-device deny permission overrides the device-specific
  // allow permission.
  PermissionTestUtils.add(URI, "speaker", Services.perms.DENY_ACTION);

  Assert.equal(richlistbox.itemCount, 1, "itemCount with deny and allow");
  let richlistitem = richlistbox.itemChildren[0];
  let siteStatus = richlistitem.querySelector(".website-status");
  Assert.equal(
    siteStatus.value,
    Services.perms.DENY_ACTION,
    "website status with deny and allow"
  );
  // The website status element is not a menulist because all-device allow is
  // not an option.
  Assert.equal(siteStatus.tagName, "label");

  PermissionTestUtils.remove(URI, devicePermissionId);
  PermissionTestUtils.remove(URI, "speaker");

  doc.querySelector("dialog").getButton("cancel").click();
});

add_task(async function testLocalNetworkAccessPermissionVisibility() {
  let enabled = Services.prefs.getBoolPref("network.lna.blocking", false);
  let loopbackNetworkSettingsButton = gBrowser.contentDocument.getElementById(
    "loopbackNetworkSettingsButton"
  );
  let localNetworkSettingsButton = gBrowser.contentDocument.getElementById(
    "localNetworkSettingsButton"
  );

  Assert.equal(
    BrowserTestUtils.isVisible(localNetworkSettingsButton),
    enabled,
    "localhost permissions visible"
  );
  Assert.equal(
    BrowserTestUtils.isVisible(loopbackNetworkSettingsButton),
    enabled,
    "localhost permissions visible"
  );

  let changeLocalHostButton = waitForSettingControlChange(
    loopbackNetworkSettingsButton
  );
  let changeLocalNetworkButton = waitForSettingControlChange(
    localNetworkSettingsButton
  );

  enabled = !enabled;
  Services.prefs.setBoolPref("network.lna.blocking", enabled);

  await changeLocalHostButton;
  await changeLocalNetworkButton;

  Assert.equal(
    BrowserTestUtils.isVisible(loopbackNetworkSettingsButton),
    enabled,
    "localhost permissions toggle"
  );

  Assert.equal(
    BrowserTestUtils.isVisible(localNetworkSettingsButton),
    enabled,
    "localhost permissions toggle"
  );

  Services.prefs.setBoolPref("network.lna.blocking", !enabled);
});

add_task(async function removeTab() {
  gBrowser.removeCurrentTab();
});
