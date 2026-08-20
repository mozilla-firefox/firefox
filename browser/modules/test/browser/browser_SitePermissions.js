/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const LOCAL_NETWORK_ACCESS_ENABLED = Services.prefs.getBoolPref(
  "network.lna.blocking"
);

// This tests the SitePermissions.getAllPermissionDetailsForBrowser function.
add_task(async function testGetAllPermissionDetailsForBrowser() {
  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.com"
    );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    principal.spec
  );

  Services.prefs.setIntPref("permissions.default.shortcuts", 2);

  let browser = tab.linkedBrowser;

  SitePermissions.setForPrincipal(principal, "camera", SitePermissions.ALLOW);

  SitePermissions.setForPrincipal(
    principal,
    "cookie",
    SitePermissions.ALLOW_COOKIES_FOR_SESSION
  );
  SitePermissions.setForPrincipal(principal, "popup", SitePermissions.ALLOW);
  SitePermissions.setForPrincipal(
    principal,
    "geo",
    SitePermissions.ALLOW,
    SitePermissions.SCOPE_SESSION
  );

  SitePermissions.setForPrincipal(
    principal,
    "loopback-network",
    SitePermissions.ALLOW,
    SitePermissions.SCOPE_SESSION
  );

  SitePermissions.setForPrincipal(
    principal,
    "local-network",
    SitePermissions.ALLOW,
    SitePermissions.SCOPE_SESSION
  );

  SitePermissions.setForPrincipal(
    principal,
    "shortcuts",
    SitePermissions.ALLOW
  );

  SitePermissions.setForPrincipal(principal, "xr", SitePermissions.ALLOW);

  let permissions = SitePermissions.getAllPermissionDetailsForBrowser(browser);

  let camera = permissions.find(({ id }) => id === "camera");
  Assert.deepEqual(camera, {
    id: "camera",
    label: "Use the camera",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  // Check that removed permissions (State.UNKNOWN) are skipped.
  SitePermissions.removeFromPrincipal(principal, "camera");
  permissions = SitePermissions.getAllPermissionDetailsForBrowser(browser);

  camera = permissions.find(({ id }) => id === "camera");
  Assert.equal(camera, undefined);

  let cookie = permissions.find(({ id }) => id === "cookie");
  Assert.deepEqual(cookie, {
    id: "cookie",
    label: "Set cookies",
    state: SitePermissions.ALLOW_COOKIES_FOR_SESSION,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  // The label of the popup permission changes based on the current pref
  // configuration. In testing, the default configuration is pop-up blocking
  // disabled by default, and framebusting ("third-party redirect") protection
  // enabled by default, so this is the first test case.
  let popup = permissions.find(({ id }) => id === "popup");
  Assert.deepEqual(popup, {
    id: "popup",
    label: "Third-party redirects",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  // Now enable the pop-up blocker and check the different label
  Services.prefs.setBoolPref("dom.disable_open_during_load", true);
  permissions = SitePermissions.getAllPermissionDetailsForBrowser(browser);
  popup = permissions.find(({ id }) => id === "popup");

  Assert.deepEqual(popup, {
    id: "popup",
    label: "Pop-ups and third-party redirects",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  // Disable framebusting protection
  Services.prefs.setBoolPref(
    "dom.security.framebusting_intervention.enabled",
    false
  );
  permissions = SitePermissions.getAllPermissionDetailsForBrowser(browser);
  popup = permissions.find(({ id }) => id === "popup");

  Assert.deepEqual(popup, {
    id: "popup",
    label: "Open pop-up windows",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  // Disable both pop-up blocker and framebusting protection
  Services.prefs.setBoolPref("dom.disable_open_during_load", false);
  permissions = SitePermissions.getAllPermissionDetailsForBrowser(browser);
  popup = permissions.find(({ id }) => id === "popup");

  Assert.equal(popup, undefined);

  Services.prefs.clearUserPref(
    "dom.security.framebusting_intervention.enabled"
  );

  let geo = permissions.find(({ id }) => id === "geo");
  Assert.deepEqual(geo, {
    id: "geo",
    label: "Access your location",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_SESSION,
  });

  if (LOCAL_NETWORK_ACCESS_ENABLED) {
    let loopbackNetwork = permissions.find(
      ({ id }) => id === "loopback-network"
    );
    Assert.deepEqual(loopbackNetwork, {
      id: "loopback-network",
      label: "Access this device",
      state: SitePermissions.ALLOW,
      scope: SitePermissions.SCOPE_SESSION,
    });

    let localNetwork = permissions.find(({ id }) => id === "local-network");
    Assert.deepEqual(localNetwork, {
      id: "local-network",
      label: "Access local network devices",
      state: SitePermissions.ALLOW,
      scope: SitePermissions.SCOPE_SESSION,
    });
  }

  let shortcuts = permissions.find(({ id }) => id === "shortcuts");
  Assert.deepEqual(shortcuts, {
    id: "shortcuts",
    label: "Override keyboard shortcuts",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  let xr = permissions.find(({ id }) => id === "xr");
  Assert.deepEqual(xr, {
    id: "xr",
    label: "Access virtual reality devices",
    state: SitePermissions.ALLOW,
    scope: SitePermissions.SCOPE_PERSISTENT,
  });

  SitePermissions.removeFromPrincipal(principal, "cookie");
  SitePermissions.removeFromPrincipal(principal, "popup");
  SitePermissions.removeFromPrincipal(principal, "geo");
  SitePermissions.removeFromPrincipal(principal, "loopback-network");
  SitePermissions.removeFromPrincipal(principal, "local-network");
  SitePermissions.removeFromPrincipal(principal, "shortcuts");

  SitePermissions.removeFromPrincipal(principal, "xr");

  Services.prefs.clearUserPref("permissions.default.shortcuts");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function testTemporaryChangeEvent() {
  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.com"
    );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    principal.spec
  );

  let browser = tab.linkedBrowser;

  let changeEventCount = 0;
  function listener() {
    changeEventCount++;
  }

  browser.addEventListener("PermissionStateChange", listener);

  // Test browser-specific permissions.
  SitePermissions.setForPrincipal(
    browser.contentPrincipal,
    "autoplay-media",
    SitePermissions.BLOCK,
    SitePermissions.SCOPE_GLOBAL,
    browser
  );
  is(changeEventCount, 1, "Should've changed");

  // Setting the same value shouldn't dispatch a change event.
  SitePermissions.setForPrincipal(
    browser.contentPrincipal,
    "autoplay-media",
    SitePermissions.BLOCK,
    SitePermissions.SCOPE_GLOBAL,
    browser
  );
  is(changeEventCount, 1, "Shouldn't have changed");

  browser.removeEventListener("PermissionStateChange", listener);

  BrowserTestUtils.removeTab(tab);
});

// Bulk clears of browser-scoped permissions report the affected tab as a bare
// browserId instead of an nsIPermission, so they need separate coverage from
// testTemporaryChangeEvent above.
add_task(async function testTemporaryBulkClearChangeEvent() {
  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.com"
    );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    principal.spec
  );

  let browser = tab.linkedBrowser;
  let browserId = browser.browserId;

  let changeEventCount = 0;
  function listener() {
    changeEventCount++;
  }

  browser.addEventListener("PermissionStateChange", listener);

  let setTemporaryBlock = () =>
    SitePermissions.setForPrincipal(
      browser.contentPrincipal,
      "geo",
      SitePermissions.BLOCK,
      SitePermissions.SCOPE_TEMPORARY,
      browser
    );

  setTemporaryBlock();
  is(changeEventCount, 1, "Should've changed on set");

  // Goes through removeByActionForBrowser.
  SitePermissions.clearTemporaryBlockPermissions(browser);
  is(
    changeEventCount,
    2,
    "Should've changed on clearTemporaryBlockPermissions"
  );

  setTemporaryBlock();
  is(changeEventCount, 3, "Should've changed on set");

  Services.perms.removeAllForBrowser(browserId);
  is(changeEventCount, 4, "Should've changed on removeAllForBrowser");

  browser.removeEventListener("PermissionStateChange", listener);

  BrowserTestUtils.removeTab(tab);
});

add_task(async function testInvalidPrincipal() {
  // Check that an error is thrown when an invalid principal argument is passed.
  try {
    SitePermissions.isSupportedPrincipal("file:///example.js");
  } catch (e) {
    Assert.equal(
      e.message,
      "Argument passed as principal is not an instance of Ci.nsIPrincipal"
    );
  }
  try {
    SitePermissions.removeFromPrincipal(null, "canvas");
  } catch (e) {
    Assert.equal(
      e.message,
      "Atleast one of the arguments, either principal or browser should not be null."
    );
  }
  try {
    SitePermissions.setForPrincipal(
      "blah",
      "camera",
      SitePermissions.ALLOW,
      SitePermissions.SCOPE_PERSISTENT,
      gBrowser.selectedBrowser
    );
  } catch (e) {
    Assert.equal(
      e.message,
      "Argument passed as principal is not an instance of Ci.nsIPrincipal"
    );
  }
  try {
    SitePermissions.getAllByPrincipal("blah");
  } catch (e) {
    Assert.equal(
      e.message,
      "Argument passed as principal is not an instance of Ci.nsIPrincipal"
    );
  }
  try {
    SitePermissions.getAllByPrincipal(null);
  } catch (e) {
    Assert.equal(e.message, "principal argument cannot be null.");
  }
  try {
    SitePermissions.getForPrincipal(5, "camera");
  } catch (e) {
    Assert.equal(
      e.message,
      "Argument passed as principal is not an instance of Ci.nsIPrincipal"
    );
  }
  // Check that no error is thrown when passing valid principal and browser arguments.
  Assert.deepEqual(
    SitePermissions.getForPrincipal(gBrowser.contentPrincipal, "camera"),
    {
      state: SitePermissions.UNKNOWN,
      scope: SitePermissions.SCOPE_PERSISTENT,
    }
  );
  Assert.deepEqual(
    SitePermissions.getForPrincipal(null, "camera", gBrowser.selectedBrowser),
    {
      state: SitePermissions.UNKNOWN,
      scope: SitePermissions.SCOPE_PERSISTENT,
    }
  );
});
