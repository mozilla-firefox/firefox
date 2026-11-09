/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

async function resetCustomization() {
  let customizationReadyPromise = BrowserTestUtils.waitForEvent(
    window.gNavToolbox,
    "customizationready"
  );
  gCustomizeMode.enter();
  await customizationReadyPromise;

  await gCustomizeMode.reset();

  let afterCustomizationPromise = BrowserTestUtils.waitForEvent(
    window.gNavToolbox,
    "aftercustomization"
  );
  gCustomizeMode.exit();
  await afterCustomizationPromise;
}

/**
 * Tests that toolbar widget is added and removed based on
 * `browser.ipProtection.enabled` controlled by Nimbus.
 */
add_task(async function toolbar_added_and_removed() {
  let widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(widget),
    "IP Protection widget should be added to the navbar"
  );
  let position = CustomizableUI.getPlacementOfWidget(
    IPProtectionWidget.WIDGET_ID
  ).position;
  // By default, the button for revamped sidebar is placed at the beginning of the navbar.
  let expectedPosition = Services.prefs.getBoolPref("sidebar.revamp") ? 9 : 8;
  Assert.equal(
    position,
    expectedPosition,
    "IP Protection widget added in the correct position"
  );
  // Disable the feature
  Services.prefs.clearUserPref("browser.ipProtection.enabled");
  widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.equal(widget, null, "IP Protection widget is removed");

  // Reenable the feature
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });
  widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(widget),
    "IP Protection widget should be added back to the navbar"
  );
});

/**
 * Tests that the toolbar icon state updates when the connection status changes
 */
add_task(async function toolbar_icon_status() {
  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added to the navbar"
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  let panelInitPromise = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:Init"
  );
  button.click();
  await Promise.all([panelShownPromise, panelInitPromise]);

  let panelView = PanelMultiView.getViewNode(
    document,
    IPProtectionWidget.PANEL_ID
  );
  let content = panelView.querySelector(IPProtectionPanel.CONTENT_TAGNAME);
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await putServerInRemoteSettings();
  content.requestUpdate();
  await content.updateComplete;

  Assert.ok(content, "Panel content should be present");

  let statusCard = content.statusCardEl;
  let turnOnButton = statusCard.actionButtonEl;
  Assert.ok(turnOnButton, "Status card turn on button should be present");

  let vpnOnPromise = BrowserTestUtils.waitForEvent(
    lazy.IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => !!IPPProxyManager.activatedAt
  );
  // Turn the VPN on
  turnOnButton.click();
  await vpnOnPromise;
  Assert.ok(
    button.classList.contains("ipprotection-on"),
    "Toolbar icon should now show connected status"
  );
  // Regression guard for bug 2034698: state changes must not swap the toolbar
  // icon's image (which triggered a one-frame blank). Instead every state's
  // artwork is an always-painted overlay layer toggled via opacity, so the
  // "on" layer is revealed while the base (off) icon is hidden.
  let onLayer = button.querySelector(
    ".ipprotection-icon-layer[data-state='on']"
  );
  let baseIcon = button.querySelector(
    ".ipprotection-icon-stack > .toolbarbutton-icon"
  );
  let iconStack = button.querySelector(".ipprotection-icon-stack");
  Assert.ok(
    iconStack.classList.contains("toolbarbutton-badge-stack"),
    "Icon stack should have the toolbarbutton-badge-stack class for positioning and hover state"
  );
  Assert.ok(onLayer, "On-state overlay layer should exist");
  Assert.equal(
    getComputedStyle(onLayer).opacity,
    "1",
    "On layer should be revealed when connected"
  );
  Assert.equal(
    getComputedStyle(baseIcon).opacity,
    "0",
    "Base (off) icon should be hidden when connected"
  );
  let vpnOffPromise = BrowserTestUtils.waitForEvent(
    lazy.IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
  );
  // Turn the VPN off
  let turnOffButton = statusCard.actionButtonEl;
  turnOffButton.click();
  await vpnOffPromise;
  Assert.ok(
    !button.classList.contains("ipprotection-on"),
    "Toolbar icon should now show disconnected status"
  );
  Assert.equal(
    getComputedStyle(onLayer).opacity,
    "0",
    "On layer should be hidden when disconnected"
  );
  Assert.equal(
    getComputedStyle(baseIcon).opacity,
    "1",
    "Base (off) icon should be shown when disconnected"
  );

  cleanupService();

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;
});

/**
 * Tests that the toolbar icon layers are built when the widget is re-added
 * while the proxy is already paused.
 */
add_task(async function toolbar_icon_paused_after_readded() {
  IPPProxyManager.reset();

  const usage = makeUsage("5368709120", "0", "2027-01-01T00:00:00.000Z");
  setupService({
    isReady: true,
    usageInfo: usage,
  });
  IPProtectionService.updateState();

  await openPanel();

  IPPProxyManager.refreshUsage();
  await waitForProxyState(IPPProxyStates.PAUSED);

  await closePanel();

  Services.prefs.clearUserPref("browser.ipProtection.enabled");
  Assert.equal(
    document.getElementById(IPProtectionWidget.WIDGET_ID),
    null,
    "IP Protection widget is removed"
  );

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });
  await waitForProxyState(IPPProxyStates.PAUSED);

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(button),
    "IP Protection widget should be added back to the navbar"
  );
  Assert.ok(
    button.classList.contains("ipprotection-paused"),
    "Toolbar icon should show paused status"
  );
  let pausedLayer = button.querySelector(
    ".ipprotection-icon-layer[data-state='paused']"
  );
  Assert.ok(pausedLayer, "Paused-state overlay layer should exist");
  Assert.equal(
    getComputedStyle(pausedLayer).opacity,
    "1",
    "Paused layer should be revealed when paused"
  );

  IPPProxyManager.reset();
  cleanupService();
  IPProtectionService.updateState();
});

/**
 * Tests that the panel opens when the toolbar button is activated via the
 * Enter or Space keys. Bug 2027922 — on macOS, native XUL toolbarbutton key
 * handling does not fire `command` for Enter, so we rely on the explicit
 * keypress handler in navigator-toolbox.js.
 */
add_task(async function toolbar_keyboard_activation() {
  for (let key of ["KEY_Enter", " "]) {
    let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
    Assert.ok(
      BrowserTestUtils.isVisible(button),
      "IP Protection widget should be visible"
    );

    button.setAttribute("tabindex", "-1");
    button.focus();

    let panelShownPromise = waitForPanelEvent(document, "popupshown");
    let panelInitPromise = BrowserTestUtils.waitForEvent(
      document,
      "IPProtection:Init"
    );
    EventUtils.synthesizeKey(key, {}, window);
    await Promise.all([panelShownPromise, panelInitPromise]);

    Assert.equal(
      button.getAttribute("open"),
      "true",
      `Panel should be open after pressing ${key}`
    );

    let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape");
    await panelHiddenPromise;

    button.removeAttribute("tabindex");
  }
});

/**
 * Tests that a connection kept up for the sites that always use the VPN does
 * not show the icon as on for a site without an inclusion.
 */
add_task(async function toolbar_icon_status_inclusion_mode() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await putServerInRemoteSettings();

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  let { started } = await IPPProxyManager.start(
    false,
    false,
    undefined,
    IPPMode.MODE_INCLUSION
  );
  Assert.ok(started, "Proxy should start in inclusion mode");
  Assert.ok(
    !button.classList.contains("ipprotection-on"),
    "Toolbar icon should not show connected status in inclusion mode"
  );

  IPPProxyManager.setMode(IPPMode.MODE_FULL);
  Assert.ok(
    button.classList.contains("ipprotection-on"),
    "Toolbar icon should show connected status once widened to full mode"
  );

  await IPPProxyManager.stop(false);
  cleanupService();
});

/**
 * Tests that a failed connection kept up for the sites that always use the VPN
 * only shows the error on tabs with an included site, while a failed full
 * connection shows it everywhere.
 */
add_task(async function toolbar_icon_error_inclusion_mode() {
  const INCLUDED_SITE = "https://example.org";
  const sandbox = sinon.createSandbox();
  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ERROR);
  sandbox.stub(IPPProxyManager, "mode").value(IPPMode.MODE_INCLUSION);
  sandbox
    .stub(IPPSiteRuleManager, "getRule")
    .callsFake(principal =>
      principal?.origin === INCLUDED_SITE
        ? IPPPrincipalRules.INCLUDED
        : IPPPrincipalRules.DEFAULT
    );

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  let otherTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  Assert.ok(
    !button.classList.contains("ipprotection-error"),
    "No error on a site the connection does not carry"
  );

  let includedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    INCLUDED_SITE
  );
  Assert.ok(
    button.classList.contains("ipprotection-error"),
    "Error on an included site"
  );

  sandbox.stub(IPPProxyManager, "mode").value(IPPMode.MODE_FULL);
  await BrowserTestUtils.switchTab(gBrowser, otherTab);
  Assert.ok(
    button.classList.contains("ipprotection-error"),
    "Error on every site in full mode"
  );

  BrowserTestUtils.removeTab(includedTab);
  BrowserTestUtils.removeTab(otherTab);
  sandbox.restore();
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:StateChanged")
  );
});

/**
 * Tests that an included site only shows as included while a connection
 * carries it, and not in a tab excluded on its own.
 */
add_task(async function toolbar_icon_included_only_while_carried() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", true]],
  });
  const INCLUDED_SITE = "https://example.org";
  const excludedTabs = new Set();
  const sandbox = sinon.createSandbox();
  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.READY);
  sandbox.stub(IPPProxyManager, "mode").value(IPPMode.MODE_INCLUSION);
  sandbox.stub(IPPSiteRuleManager, "canManage").returns(true);
  sandbox
    .stub(IPPSiteRuleManager, "getRule")
    .callsFake((principal, browserId) => {
      if (excludedTabs.has(browserId)) {
        return IPPPrincipalRules.EXCLUDED;
      }
      return principal?.origin === INCLUDED_SITE
        ? IPPPrincipalRules.INCLUDED
        : IPPPrincipalRules.DEFAULT;
    });

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    INCLUDED_SITE
  );
  Assert.ok(
    !button.classList.contains("ipprotection-included"),
    "Not included while no connection carries the site"
  );

  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:StateChanged")
  );
  Assert.ok(
    button.classList.contains("ipprotection-included"),
    "Included while the connection carries the site"
  );

  excludedTabs.add(tab.linkedBrowser.browserId);
  IPPSiteRuleManager.dispatchEvent(
    new CustomEvent("SiteRuleManager:RuleChanged")
  );
  Assert.ok(
    !button.classList.contains("ipprotection-included"),
    "Not included in a tab excluded on its own"
  );

  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
  IPPProxyManager.dispatchEvent(
    new CustomEvent("IPPProxyManager:StateChanged")
  );
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the toolbar icon in a new window has the previous status.
 */
add_task(async function toolbar_icon_status_new_window() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();

  let content = await openPanel();

  let vpnOnPromise = BrowserTestUtils.waitForEvent(
    lazy.IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => !!IPPProxyManager.activatedAt
  );
  // Turn the VPN on
  let statusCard = content.statusCardEl;
  let turnOnButton = statusCard.actionButtonEl;
  turnOnButton.click();
  await vpnOnPromise;

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    button.classList.contains("ipprotection-on"),
    "Toolbar icon should now show connected status"
  );

  // Check the icon status is set for new windows
  let newWindow = await BrowserTestUtils.openNewBrowserWindow({
    url: "about:newtab",
  });
  let newButton = newWindow.document.getElementById(
    IPProtectionWidget.WIDGET_ID
  );
  Assert.ok(
    newButton.classList.contains("ipprotection-on"),
    "New toolbar icon should show connected status"
  );
  await BrowserTestUtils.closeWindow(newWindow);

  await setPanelState();
  // Clear userEnabled pref to avoid breaking tests
  Services.prefs.clearUserPref("browser.ipProtection.userEnabled");
  cleanupService();
});

add_task(async function customize_toolbar_remove_widget() {
  let widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    BrowserTestUtils.isVisible(widget),
    "IP Protection toolbaritem should be visible"
  );
  let prevPosition = CustomizableUI.getPlacementOfWidget(
    IPProtectionWidget.WIDGET_ID
  ).position;

  let stoppedEventPromise = BrowserTestUtils.waitForEvent(
    lazy.IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
  );
  CustomizableUI.removeWidgetFromArea(IPProtectionWidget.WIDGET_ID);
  // VPN should disconect when the toolbaritem is removed
  await stoppedEventPromise;
  Assert.ok(
    !BrowserTestUtils.isVisible(widget),
    "Toolbaritem is no longer visible"
  );

  CustomizableUI.addWidgetToArea(
    IPProtectionWidget.WIDGET_ID,
    CustomizableUI.AREA_NAVBAR,
    prevPosition
  );
});

/**
 * Tests that toolbar widget can be moved and will not reset
 * back to the initial area on re-init.
 */
add_task(async function toolbar_placement_customized() {
  setupService({
    isReady: true,
  });

  let start = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(
    start.area,
    CustomizableUI.AREA_NAVBAR,
    "IP Protection widget is initially added to the nav bar"
  );

  // Move widget to overflow
  CustomizableUI.addWidgetToArea(
    IPProtectionWidget.WIDGET_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );

  let end = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(
    end.area,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
    "IP Protection widget moved to the overflow area"
  );

  // Disable the feature
  Services.prefs.clearUserPref("browser.ipProtection.enabled");

  let widget = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.equal(widget, null, "IP Protection widget is removed");

  const waitForStateChange = BrowserTestUtils.waitForEvent(
    lazy.IPProtectionService,
    "IPProtectionService:StateChanged",
    false,
    () => lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
  );

  // Reenable the feature
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  await waitForStateChange;

  let restored = CustomizableUI.getPlacementOfWidget(
    IPProtectionWidget.WIDGET_ID
  );
  Assert.equal(
    restored.area,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
    "IP Protection widget is still in the overflow area"
  );

  CustomizableUI.addWidgetToArea(
    IPProtectionWidget.WIDGET_ID,
    start.area,
    start.position
  );
});

/**
 * Tests that toolbar widget can be removed and will not be re-added.
 */
add_task(async function toolbar_removed() {
  setupService({
    isReady: true,
  });

  // Ensure that the added pref is still set, as it is unset at the end of each test.
  await SpecialPowers.pushPrefEnv({
    set: [[IPProtectionWidget.ADDED_PREF, true]],
  });

  let start = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(
    start.area,
    CustomizableUI.AREA_NAVBAR,
    "IP Protection widget is initially added to the nav bar"
  );

  // Remove from the toolbar
  CustomizableUI.removeWidgetFromArea(IPProtectionWidget.WIDGET_ID);

  let end = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(end, null, "IP Protection widget is removed");

  // Disable the feature
  Services.prefs.clearUserPref("browser.ipProtection.enabled");

  const waitForStateChange = BrowserTestUtils.waitForEvent(
    lazy.IPProtectionService,
    "IPProtectionService:StateChanged",
    false,
    () => lazy.IPProtectionService.state === lazy.IPProtectionStates.READY
  );

  // Reenable the feature
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.enabled", true]],
  });

  await waitForStateChange;

  let restored = CustomizableUI.getPlacementOfWidget(
    IPProtectionWidget.WIDGET_ID
  );
  Assert.equal(restored, null, "IP Protection widget is still removed");

  CustomizableUI.addWidgetToArea(
    IPProtectionWidget.WIDGET_ID,
    start.area,
    start.position
  );
});

/**
 * Tests that toolbar widget can be moved and will reset
 * back to the initial area on customize mode reset.
 */
add_task(async function toolbar_placement_reset() {
  setupService({
    isReady: true,
  });

  let start = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(
    start.area,
    CustomizableUI.AREA_NAVBAR,
    "IP Protection widget is initially added to the nav bar"
  );

  // Move widget to overflow
  CustomizableUI.addWidgetToArea(
    IPProtectionWidget.WIDGET_ID,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL
  );

  let end = CustomizableUI.getPlacementOfWidget(IPProtectionWidget.WIDGET_ID);
  Assert.equal(
    end.area,
    CustomizableUI.AREA_FIXED_OVERFLOW_PANEL,
    "IP Protection widget moved to the overflow area"
  );

  await resetCustomization();

  let restored = CustomizableUI.getPlacementOfWidget(
    IPProtectionWidget.WIDGET_ID
  );
  Assert.equal(
    restored.area,
    start.area,
    "IP Protection widget is reset to the initial area after customize mode reset"
  );
  Assert.equal(
    restored.position,
    start.position,
    "IP Protection widget is reset to the initial position after customize mode reset"
  );
});
