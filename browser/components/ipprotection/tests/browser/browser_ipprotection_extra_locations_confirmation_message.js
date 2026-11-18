/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const DISMISSED_PREF = "browser.ipProtection.locationUnlockMessageDismissed";
const BANDWIDTH_DISMISSED_PREF =
  "browser.ipProtection.bandwidthWarningDismissedThreshold";
const INCLUSIONS_PREF = "browser.ipProtection.features.siteInclusions";
const LOCATION_MESSAGE_ID = "ipprotection-message-default-browser-confirmation";
const BANDWIDTH_MESSAGE_ID = "ipprotection-message-bandwidth-warning";
const ONBOARDING_MESSAGE_ID =
  "ipprotection-message-continuous-onboarding-intro";

const MAX_BYTES = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;
const WARNING_USAGE = {
  max: MAX_BYTES,
  remaining: MAX_BYTES * BANDWIDTH.SECOND_THRESHOLD,
};

let sandbox;
let isDefaultStub;

add_setup(async function () {
  sandbox = sinon.createSandbox();
  isDefaultStub = sandbox.stub(ShellService, "isDefaultBrowserAsync");

  registerCleanupFunction(async () => {
    if (IPProtection.getPanel(window)?.active) {
      await closePanel();
    }
    sandbox.restore();
    Services.prefs.clearUserPref("browser.ipProtection.siteRulesNormalized");
    Services.prefs.clearUserPref("browser.ipProtection.bandwidthResetDate");
    Services.prefs.clearUserPref(DISMISSED_PREF);
    Services.prefs.clearUserPref(BANDWIDTH_DISMISSED_PREF);
    Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");
    cleanupService();
  });
});

function getMessageBar(content, messageId) {
  return [
    ...content.shadowRoot.querySelectorAll("ipprotection-message-bar"),
  ].find(bar => bar.messageId === messageId);
}

function getLocationMessageBar(content) {
  return getMessageBar(content, LOCATION_MESSAGE_ID);
}

async function waitForLocationMessageBar(content, shown) {
  await BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => !!getLocationMessageBar(content) === shown
  );
}

async function clickClose(messageBar) {
  await messageBar.updateComplete;
  await messageBar.mozMessageBarEl.updateComplete;
  messageBar.mozMessageBarEl.closeButton.click();
}

/**
 * Closes the panel if open, signs in, fakes the default browser check and
 * resets the dismissed and bandwidth prefs.
 *
 * @param {object} options
 * @param {boolean} [options.isDefault] Result of isDefaultBrowserAsync().
 * @param {boolean} [options.hasUpgraded] Whether the user has a paid VPN.
 */
async function setup({ isDefault = true, hasUpgraded = false } = {}) {
  if (IPProtection.getPanel(window).active) {
    await closePanel();
  }
  isDefaultStub.reset();
  isDefaultStub.resolves(isDefault);
  Services.prefs.setBoolPref(DISMISSED_PREF, false);
  Services.prefs.clearUserPref(BANDWIDTH_DISMISSED_PREF);
  Services.prefs.clearUserPref("browser.ipProtection.bandwidthThreshold");

  setupService({ isReady: true, hasUpgraded });
  IPProtectionService.updateState();
  await waitForServiceState(IPProtectionStates.READY);
}

async function openReadyPanel(state = {}) {
  return openPanel({ unauthenticated: false, error: "", ...state });
}

/**
 * Tests that the message shows when the service is ready, Firefox is the
 * default browser, the user hasn't upgraded and the message isn't dismissed.
 */
add_task(async function test_shown_when_eligible() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message when eligiblke"
  );
  let messageBar = getLocationMessageBar(content);
  Assert.ok(messageBar, "Location message bar should be shown when eligible");
  Assert.equal(
    messageBar.messageId,
    LOCATION_MESSAGE_ID,
    "Message bar should show the extra locations confirmation"
  );
});

/**
 * Tests that the message doesn't show when Firefox isn't the default browser.
 */
add_task(async function test_not_shown_when_not_default() {
  await setup({ isDefault: false });

  let content = await openReadyPanel();

  Assert.ok(isDefaultStub.called, "Default browser should be checked");
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message when not default"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown when not default"
  );
});

/**
 * Tests that the message doesn't show once it has been dismissed.
 */
add_task(async function test_not_shown_when_dismissed_pref_set() {
  await setup();
  Services.prefs.setBoolPref(DISMISSED_PREF, true);

  let content = await openReadyPanel();

  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message when dismissed pref set"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown when dismissed pref set"
  );
});

/**
 * Tests that the message doesn't show for users with a paid VPN.
 */
add_task(async function test_not_shown_when_upgraded() {
  await setup({ hasUpgraded: true });

  let content = await openReadyPanel({ hasUpgraded: true });

  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message when upgraded"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown when upgraded"
  );
});

/**
 * Tests that the message doesn't show when the service isn't ready.
 */
add_task(async function test_not_shown_when_not_ready() {
  await setup();
  setupService({ isReady: false });
  IPProtectionService.updateState();
  await waitForServiceState(IPProtectionStates.UNAUTHENTICATED);

  let content = await openReadyPanel();

  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message when not ready"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown when not ready"
  );
});

/**
 * Tests that a failing default browser check hides the message instead of
 * breaking the panel, and that the message shows once the check works again.
 */
add_task(async function test_not_shown_when_default_check_throws() {
  await setup();
  // showing() doesn't catch errors from this check, so only fail the call
  // made for the location message.
  let rejectedCount = 0;
  isDefaultStub.callsFake(async () => {
    if (
      !new Error().stack.includes("shouldShowAdditionalLocationConfirmation")
    ) {
      return false;
    }
    rejectedCount++;
    throw new Error("Default browser check failed");
  });

  let content = await openReadyPanel();

  Assert.greater(
    rejectedCount,
    0,
    "Default browser check should fail for the message"
  );
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  await closePanel();

  isDefaultStub.callsFake(async () => true);
  content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
});

/**
 * Tests that the message appears once Firefox becomes the default browser.
 */
add_task(async function test_shown_after_becoming_default() {
  await setup({ isDefault: false });

  let content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message before becoming default"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown before becoming default"
  );
  await closePanel();

  isDefaultStub.resolves(true);
  content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message after becoming default"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown after becoming default"
  );
});

/**
 * Tests that the message goes away when the service stops being ready while
 * the panel is open.
 */
add_task(async function test_hidden_when_service_leaves_ready() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  let panel = IPProtection.getPanel(window);
  setupService({ isReady: false });
  IPProtectionService.updateState();
  await waitForServiceState(IPProtectionStates.UNAUTHENTICATED);
  await TestUtils.waitForCondition(
    () => !panel.state.showAdditionalLocationConfirmation,
    "Panel state should stop allowing the message"
  );
  await content.updateComplete;

  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message when no longer ready"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown when no longer ready"
  );
});

/**
 * Tests that the bandwidth warning is shown with the message, and the message still appears
 * once the warning is dismissed.
 */
add_task(async function test_shown_with_bandwidth_warning() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await setPanelState({
    bandwidthWarning: true,
    bandwidthUsage: WARNING_USAGE,
  });

  let warningBar = getMessageBar(content, BANDWIDTH_MESSAGE_ID);
  Assert.ok(warningBar, "Bandwidth warning should be shown");
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );

  await clickClose(warningBar);
  await waitForLocationMessageBar(content, true);

  Assert.ok(
    !getMessageBar(content, BANDWIDTH_MESSAGE_ID),
    "Bandwidth warning should be dismissed"
  );
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
});

/**
 * Tests that onboarding messages are shown with the message.
 */
add_task(async function test_shown_with_onboarding_message() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await setPanelState({ onboardingMessage: ONBOARDING_MESSAGE_ID });

  Assert.ok(
    getMessageBar(content, ONBOARDING_MESSAGE_ID),
    "Onboarding message should be shown"
  );
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );

  await setPanelState({ onboardingMessage: "" });

  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
});

/**
 * Tests that the site inclusion prompt is shown with the message.
 */
add_task(async function test_shown_with_site_inclusion_prompt() {
  await SpecialPowers.pushPrefEnv({ set: [[INCLUSIONS_PREF, true]] });
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await setPanelState({
    isSiteInclusionsEnabled: true,
    isProtectionEnabled: true,
    showSiteInclusionPrompt: true,
    siteData: {
      host: "example.com",
      isInclusion: false,
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    content.shadowRoot.querySelector("#site-inclusion-prompt"),
    "Site inclusion prompt should be shown"
  );
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the message isn't shown on the paused, error and unauthenticated
 * screens.
 */
add_task(async function test_hidden_on_other_screens() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await setPanelState({ paused: true, bandwidthUsage: WARNING_USAGE });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should still allow the message when showing bandwidth warning"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should be hidden when showing bandwidth warning"
  );
  await setPanelState({ paused: false });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );

  await setPanelState({ error: "generic-error" });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should still allow the message with error"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should be hidden with error"
  );
  await setPanelState({ error: "" });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );

  await setPanelState({ unauthenticated: true });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should still allow the message when unauthenticated"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should be hidden when unauthenticated"
  );
  await setPanelState({ unauthenticated: false });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
});

/**
 * Tests that the message renders with and without site inclusions enabled.
 */
add_task(async function test_shown_in_both_layouts() {
  for (let inclusionsEnabled of [true, false]) {
    await SpecialPowers.pushPrefEnv({
      set: [[INCLUSIONS_PREF, inclusionsEnabled]],
    });
    await setup();

    let content = await openReadyPanel();
    await waitForLocationMessageBar(content, true);

    Assert.ok(
      IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
      "Panel state should allow the message"
    );
    let messageBar = getLocationMessageBar(content);
    Assert.ok(
      messageBar,
      `Location message bar should be shown with inclusions ${inclusionsEnabled}`
    );
    Assert.equal(
      !!messageBar.closest(".vpn-bottom-content"),
      inclusionsEnabled,
      `Message bar should only be inside the bottom content with inclusions ${inclusionsEnabled}`
    );

    await SpecialPowers.popPrefEnv();
  }
});

/**
 * Tests that closing the message sets the dismissed pref, removes the bar and
 * keeps it hidden after reopening the panel.
 */
add_task(async function test_dismiss_persists() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await clickClose(getLocationMessageBar(content));
  await waitForLocationMessageBar(content, false);

  Assert.ok(
    Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should be set"
  );
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );

  await closePanel();
  content = await openReadyPanel();

  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
});

/**
 * Tests that a dismissed message stays dismissed after Firefox stops and
 * starts being the default browser again.
 */
add_task(async function test_dismiss_persists_when_default_toggles() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await clickClose(getLocationMessageBar(content));
  await waitForLocationMessageBar(content, false);
  Assert.ok(
    Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should be set"
  );
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  await closePanel();

  isDefaultStub.resolves(false);
  content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  await closePanel();

  isDefaultStub.resolves(true);
  content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  Assert.ok(
    Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should still be set after becoming default again"
  );
});

/**
 * Tests that a message that was never dismissed shows again after Firefox
 * stops and starts being the default browser again.
 */
add_task(async function test_shown_again_when_default_toggles() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
  await closePanel();

  isDefaultStub.resolves(false);
  content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  await closePanel();

  isDefaultStub.resolves(true);
  content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
  Assert.ok(
    !Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should stay unset when the message was never dismissed"
  );
});

/**
 * Tests that becoming the default browser while the panel is open doesn't
 * show the message until the panel is reopened.
 */
add_task(async function test_waits_for_reopen_when_changes_while_open() {
  await setup({ isDefault: false });

  let content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );

  isDefaultStub.resolves(true);
  await TestUtils.waitForTick();
  await content.updateComplete;
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
  await closePanel();

  content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );
});

/**
 * Tests that closing the message doesn't stop the top message bar from
 * showing afterwards.
 */
add_task(async function test_dismiss_does_not_hide_top_message_bar() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await clickClose(getLocationMessageBar(content));
  await waitForLocationMessageBar(content, false);

  await setPanelState({
    bandwidthWarning: true,
    bandwidthUsage: WARNING_USAGE,
  });

  Assert.ok(
    getMessageBar(content, BANDWIDTH_MESSAGE_ID),
    "Bandwidth warning should still be able to show"
  );
});

/**
 * Tests that opening the locations list while the message is shown dismisses
 * it.
 */
add_task(async function test_locations_list_dismisses_shown_message() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await IPProtection.getPanel(window).showLocationSelector();
  Assert.ok(
    Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should be set after opening the locations list"
  );
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );

  await closePanel();
  content = await openReadyPanel();
  Assert.ok(
    !IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should not allow the message"
  );
  Assert.ok(
    !getLocationMessageBar(content),
    "Location message bar should not be shown"
  );
});

/**
 * Tests that opening the locations list doesn't dismiss the message for users
 * who aren't eligible for it yet.
 */
add_task(async function test_locations_list_keeps_message_when_not_eligible() {
  await setup({ isDefault: false });

  await openReadyPanel();
  await IPProtection.getPanel(window).showLocationSelector();

  Assert.ok(
    !Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should stay unset"
  );
});

/**
 * Tests that opening the locations list dismisses the message even if it
 * is shown with another message.
 */
add_task(async function test_locations_list_dismisses_message_with_others() {
  await setup();

  let content = await openReadyPanel();
  await waitForLocationMessageBar(content, true);

  await setPanelState({
    bandwidthWarning: true,
    bandwidthUsage: WARNING_USAGE,
  });
  Assert.ok(
    IPProtection.getPanel(window).state.showAdditionalLocationConfirmation,
    "Panel state should allow the message"
  );
  Assert.ok(
    getLocationMessageBar(content),
    "Location message bar should be shown"
  );

  await IPProtection.getPanel(window).showLocationSelector();

  Assert.ok(
    Services.prefs.getBoolPref(DISMISSED_PREF),
    "Dismissed pref should be set even if the message is hidden"
  );
});
