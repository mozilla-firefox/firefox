/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Tests that focus remains on the action button after the VPN toggle is activated.
 */
add_task(async function test_focus_preserved_after_toggle() {
  let content = await openPanel({
    isEnrolledAndEntitled: true,
    isProtectionEnabled: false,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection-content component should be present"
  );

  await BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.statusCardEl
  );

  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  actionButton.focus();
  actionButton.click();

  // Simulate the service responding with the updated state.
  statusCard.protectionEnabled = true;
  await statusCard.updateComplete;
  await statusCard.actionButtonEl.updateComplete;

  Assert.ok(
    actionButton.matches(":focus-within"),
    "Focus stays on the action button after toggled"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that focus is restored to the action button after the VPN goes through
 * the activating state (button disabled then re-enabled).
 */
add_task(async function test_focus_restored_after_activating() {
  let content = await openPanel({
    isEnrolledAndEntitled: true,
    isProtectionEnabled: false,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection-content component should be present"
  );

  await BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.statusCardEl
  );

  let statusCard = content.statusCardEl;
  let actionButton = statusCard.actionButtonEl;

  actionButton.focus();
  actionButton.click();

  statusCard.isActivating = true;
  await statusCard.updateComplete;

  statusCard.isActivating = false;
  statusCard.protectionEnabled = true;
  await statusCard.updateComplete;
  await statusCard.actionButtonEl.updateComplete;

  Assert.ok(
    actionButton.matches(":focus-within"),
    "Focus moves back to the action button after activating completes"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that the panel can be navigated with Tab and Arrow keys
 * and that the help button responds to the Enter key
 */
add_task(async function test_keyboard_navigation_in_panel() {
  const isSiteInclusionsEnabled = Services.prefs.getBoolPref(
    "browser.ipProtection.features.siteInclusions"
  );
  const openLinkStub = sinon.stub(window, "openWebLinkIn");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  let content = await openPanel({
    isReady: true,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection-content component should be present"
  );

  await expectFocusAfterKey(
    "Tab",
    content.ownerDocument.querySelector(
      `#${IPProtectionPanel.HEADER_BUTTON_ID}`
    )
  );

  await BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.statusCardEl
  );

  let statusCard = content.statusCardEl;
  let turnOnButton = statusCard.actionButtonEl;
  let locationButton = content.locationButtonEl;
  let siteRulesButton = content.siteRulesEl;

  Assert.equal(
    !!siteRulesButton,
    isSiteInclusionsEnabled,
    "Site rules button should only be present when site inclusions are enabled"
  );

  await expectFocusAfterKey("Tab", turnOnButton);

  await expectFocusAfterKey("Tab", locationButton);

  if (isSiteInclusionsEnabled) {
    await expectFocusAfterKey("Tab", siteRulesButton);
  }

  await expectFocusAfterKey("Tab", content.settingsButtonEl);

  // Loop back around
  await expectFocusAfterKey(
    "Tab",
    content.ownerDocument.querySelector(
      `#${IPProtectionPanel.HEADER_BUTTON_ID}`
    )
  );
  await expectFocusAfterKey("Tab", turnOnButton);

  await expectFocusAfterKey("Tab", locationButton);

  if (isSiteInclusionsEnabled) {
    await expectFocusAfterKey("Tab", siteRulesButton);
  }

  await expectFocusAfterKey("Tab", content.settingsButtonEl);

  // Loop back around with ArrowDown
  let headerButton = content.ownerDocument.querySelector(
    `#${IPProtectionPanel.HEADER_BUTTON_ID}`
  );
  await expectFocusAfterKey("ArrowDown", headerButton);
  await expectFocusAfterKey("ArrowDown", turnOnButton);
  await expectFocusAfterKey("ArrowDown", locationButton);

  // Test ArrowUp (backward)
  await expectFocusAfterKey("ArrowUp", turnOnButton);
  await expectFocusAfterKey("ArrowUp", headerButton);

  // Navigate forward to turnOnButton to set up for Shift+Tab test
  await expectFocusAfterKey("ArrowDown", turnOnButton);

  // Loop backwards with Shift+Tab
  await expectFocusAfterKey("Shift+Tab", headerButton);

  // Check that header button responds to enter key
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  await panelHiddenPromise;
  Assert.ok(openLinkStub.calledOnce, "help button should open a link");
  openLinkStub.restore();
  BrowserTestUtils.removeTab(tab);
  cleanupService();
});
