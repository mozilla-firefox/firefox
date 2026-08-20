/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { UIState } = ChromeUtils.importESModule(
  "resource://services-sync/UIState.sys.mjs"
);

const REFERRALS_PREF = "browser.referrals.enabled";
const REFERRAL_CODE_PREF = "browser.referrals.code";
// The Share Firefox item lives in the FxA "try other protection tools" (PXI)
// menu, which is only shown when this pref is enabled.
const CTA_MENU_PREF = "identity.fxaccounts.toolbar.pxiToolbarEnabled";
const INVITE_URL_PREFIX = "https://www.firefox.com/invite";

// about:referrals is a redirector, so the tab settles on the invite page. Both
// URIs count as "the referrals page was opened" depending on how far along the
// load is.
function isReferralsURL(spec) {
  return spec === "about:referrals" || spec.startsWith(INVITE_URL_PREFIX);
}

add_setup(async function () {
  // gSync.init() is normally called in a requestIdleCallback; force it here.
  gSync.init();
  await SpecialPowers.pushPrefEnv({
    set: [
      [CTA_MENU_PREF, true],
      // Set on the first FxA menu open otherwise; setting it up front avoids a
      // state write racing with the panel opening.
      ["identity.fxaccounts.toolbar.accessed", true],
    ],
  });
  // Render the signed-out state so clicking the toolbar button opens the CTA
  // menu (rather than the sign-in flow).
  gSync.updateAllUI({ status: UIState.STATUS_NOT_CONFIGURED });
});

async function openFxaPanel() {
  document.getElementById("fxa-toolbar-menu-button").click();
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  await BrowserTestUtils.waitForEvent(fxaView, "ViewShown");
}

async function closeFxaPanel() {
  let fxaView = PanelMultiView.getViewNode(document, "PanelUI-fxa");
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  fxaView.closest("panel").hidePopup();
  await hidden;
}

add_task(async function share_firefox_hidden_when_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[REFERRALS_PREF, false]] });

  await openFxaPanel();
  let button = document.getElementById("PanelUI-fxa-menu-share-firefox");
  ok(button, "Share Firefox menu item exists");
  ok(button.hidden, "Share Firefox item is hidden when the pref is disabled");
  await closeFxaPanel();

  await SpecialPowers.popPrefEnv();
});

add_task(async function share_firefox_opens_referrals_when_enabled() {
  await SpecialPowers.pushPrefEnv({ set: [[REFERRALS_PREF, true]] });

  await openFxaPanel();
  let button = document.getElementById("PanelUI-fxa-menu-share-firefox");
  ok(!button.hidden, "Share Firefox item is visible when the pref is enabled");

  let tabOpened = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabOpen"
  );
  let hidden = BrowserTestUtils.waitForEvent(document, "popuphidden", true);
  Services.fog.testResetFOG();
  button.click();
  let tab = (await tabOpened).target;
  await hidden;

  await TestUtils.waitForCondition(
    () => isReferralsURL(tab.linkedBrowser.currentURI.displaySpec),
    "Waiting for the referrals page to be opened in the new tab"
  );

  let events = Glean.referrals.entrypointClicked.testGetValue();
  is(events.length, 1, "One entrypoint_clicked event was recorded");
  is(
    events[0].extra.entrypoint,
    "accounts_menu",
    "entrypoint_clicked recorded the accounts_menu entrypoint"
  );
  ok(
    isReferralsURL(tab.linkedBrowser.currentURI.displaySpec),
    "Share Firefox opened the referrals page in a new tab"
  );

  // openReferralsTab re-locks the code pref, and a locked pref reads its
  // (empty) default, so unlock before reading the user-branch value.
  Services.prefs.unlockPref(REFERRAL_CODE_PREF);
  isnot(
    Services.prefs.getStringPref(REFERRAL_CODE_PREF, ""),
    "",
    "A referral code was generated when opening the referrals tab"
  );

  BrowserTestUtils.removeTab(tab);
  Services.prefs.clearUserPref(REFERRAL_CODE_PREF);
  await SpecialPowers.popPrefEnv();
});
