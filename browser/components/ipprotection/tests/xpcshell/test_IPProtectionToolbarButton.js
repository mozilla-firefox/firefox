/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { IPProtectionToolbarButton } = ChromeUtils.importESModule(
  "moz-src:///browser/components/ipprotection/IPProtectionToolbarButton.sys.mjs"
);

/**
 * Creates a toolbar button backed by a windowless browser, along with a
 * detached toolbaritem to apply icon states to.
 *
 * @returns {{ toolbarButton: IPProtectionToolbarButton, toolbarItem: XULElement }}
 */
function createFakeToolbarButton() {
  const browser = Services.appShell.createWindowlessBrowser(true);
  const principal = Services.scriptSecurityManager.getSystemPrincipal();
  browser.docShell.createAboutBlankDocumentViewer(principal, principal);
  const document = browser.docShell.docViewer.DOMDocument;

  // Create a fake window with minimal gBrowser for the toolbar button
  const fakeWindow = {
    gBrowser: {
      addTabsProgressListener: () => {},
      removeTabsProgressListener: () => {},
      contentPrincipal: principal,
    },
    document,
  };

  const toolbarItem = document.createXULElement("toolbaritem");
  const icon = document.createXULElement("image");
  icon.classList.add("toolbarbutton-icon");
  toolbarItem.appendChild(icon);

  return {
    toolbarButton: new IPProtectionToolbarButton(
      fakeWindow,
      "test-toolbarbutton"
    ),
    toolbarItem,
  };
}

/**
 * Tests that we can set a state, pass it to a fake element,
 * and correctly update CSS classes based on the state.
 */
add_task(function test_update_icon_status() {
  Services.prefs.setBoolPref(
    "browser.ipProtection.features.siteExceptions",
    true
  );

  let { toolbarButton: fakeToolbarButton, toolbarItem: fakeToolbarItem } =
    createFakeToolbarButton();

  Assert.equal(
    fakeToolbarItem.classList.length,
    0,
    "Toolbaritem class list should be empty"
  );

  // IP Protection is on
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: true,
    isError: false,
    isExcluded: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should include ipprotection-on"
  );

  // IP Protection is excluded
  // isExcluded should override the active status even if isActive is set to true
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: true,
    isError: false,
    isExcluded: true,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-excluded"),
    "Toolbaritem classlist should include ipprotection-excluded"
  );

  // IP Protection error
  // isError should override the active status even if isActive is set to true
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: true,
    isError: true,
    isExcluded: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-error"),
    "Toolbaritem classlist should include ipprotection-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should not include ipprotection-on"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-excluded"),
    "Toolbaritem classlist should not include ipprotection-excluded"
  );

  // IP Protection network error
  // isNetworkError should take priority over active status
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: true,
    isError: false,
    isNetworkError: true,
    isExcluded: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-network-error"),
    "Toolbaritem classlist should include ipprotection-network-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-error"),
    "Toolbaritem classlist should not include ipprotection-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should not include ipprotection-on"
  );

  // Network error takes priority over generic error
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isError: true,
    isNetworkError: true,
    isExcluded: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-network-error"),
    "Toolbaritem classlist should include ipprotection-network-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-error"),
    "Toolbaritem classlist should not include ipprotection-error when network error"
  );

  // IP Protection is off
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
  });

  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-network-error"),
    "Toolbaritem classlist should not include ipprotection-network-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-error"),
    "Toolbaritem classlist should not include ipprotection-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should not include ipprotection-on"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-excluded"),
    "Toolbaritem classlist should not include ipprotection-excluded"
  );

  // Cleanup
  fakeToolbarButton.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.features.siteExceptions");
});

/**
 * Tests the included icon state, which only applies while the VPN is off and
 * only when the site inclusions feature is enabled.
 */
add_task(function test_update_icon_status_included() {
  Services.prefs.setBoolPref(
    "browser.ipProtection.features.siteInclusions",
    true
  );

  let { toolbarButton: fakeToolbarButton, toolbarItem: fakeToolbarItem } =
    createFakeToolbarButton();

  // IP Protection is off and the site is included
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isProxyActive: true,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-included"),
    "Toolbaritem classlist should include ipprotection-included"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should not include ipprotection-on"
  );

  // The proxy isn't connected, so the inclusion isn't taking effect
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isProxyActive: false,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: false,
  });

  Assert.equal(
    fakeToolbarItem.classList.length,
    0,
    "Toolbaritem should be in the off state when the proxy is not active"
  );

  // An active VPN already protects the site, so the inclusion is not surfaced
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: true,
    isProxyActive: true,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-on"),
    "Toolbaritem classlist should include ipprotection-on"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-included"),
    "Toolbaritem classlist should not include ipprotection-included when active"
  );

  // isPaused should override the included status
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: true,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-paused"),
    "Toolbaritem classlist should include ipprotection-paused"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-included"),
    "Toolbaritem classlist should not include ipprotection-included when paused"
  );

  // isError should override the included status
  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isError: true,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: false,
  });

  Assert.ok(
    fakeToolbarItem.classList.contains("ipprotection-error"),
    "Toolbaritem classlist should include ipprotection-error"
  );
  Assert.ok(
    !fakeToolbarItem.classList.contains("ipprotection-included"),
    "Toolbaritem classlist should not include ipprotection-included when errored"
  );

  // The included state is gated behind the site inclusions pref
  Services.prefs.setBoolPref(
    "browser.ipProtection.features.siteInclusions",
    false
  );

  fakeToolbarButton.updateIconStatus(fakeToolbarItem, {
    isActive: false,
    isProxyActive: true,
    isError: false,
    isNetworkError: false,
    isExcluded: false,
    isIncluded: true,
    isPaused: false,
  });

  Assert.equal(
    fakeToolbarItem.classList.length,
    0,
    "Toolbaritem classlist should be empty when site inclusions are disabled"
  );

  // Cleanup
  fakeToolbarButton.uninit();
  Services.prefs.clearUserPref("browser.ipProtection.features.siteInclusions");
});

/**
 * Tests that every non-default icon state has a matching overlay layer, so
 * switching to it does not fall back to a blank button.
 */
add_task(function test_icon_layer_states_cover_included() {
  Assert.ok(
    IPProtectionToolbarButton.ICON_LAYER_STATES.includes("included"),
    "ICON_LAYER_STATES should contain the included state"
  );
});
