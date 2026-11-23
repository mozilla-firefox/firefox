/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test the Connection subview of the redesigned trust panel.
 */

"use strict";

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

const REDESIGN_PREF = "browser.protectionsPanelRedesign.enabled";
const VIEW_ID = "trustpanel-securityInformationView";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: [[REDESIGN_PREF, true]] });
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

async function openConnectionSubview() {
  await UrlbarTestUtils.openTrustPanel(window);
  let view = document.getElementById(VIEW_ID);
  let shown = BrowserTestUtils.waitForEvent(view, "ViewShown");
  // The redesigned main view (bug 2084203) has its own button; until it
  // lands, the current main view's button opens the redesigned subview.
  let button =
    (Services.prefs.getBoolPref(REDESIGN_PREF) &&
      document.getElementById("trustpanel-popup-connection-redesign")) ||
    document.getElementById("trustpanel-popup-connection");
  EventUtils.synthesizeMouseAtCenter(button, {}, window);
  await shown;
  return view;
}

function l10nIdOf(view, selector) {
  return view.querySelector(selector).dataset.l10nId;
}

function connectionIconOf(view) {
  return window.getComputedStyle(
    view.querySelector(".identity-popup-security-connection")
  ).listStyleImage;
}

add_task(async function test_secure() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  let view = await openConnectionSubview();
  Assert.ok(view.hasAttribute("redesign"), "The view uses the redesign");
  Assert.ok(
    view.hasAttribute("shrink-to-fit"),
    "The redesigned view opts out of the main view height"
  );
  Assert.deepEqual(
    document.l10n.getAttributes(view),
    {
      id: "trustpanel-connection-info-header",
      args: { host: "example.com" },
    },
    "The subview title names the site"
  );
  await TestUtils.waitForCondition(
    () => view.getAttribute("title") == "Connection info for example.com",
    "Waiting for the subview title to be translated"
  );

  let secure = view.querySelector(
    ".identity-popup-connection-secure.security-view"
  );
  Assert.ok(BrowserTestUtils.isVisible(secure), "Secure status is shown");
  Assert.equal(
    secure.dataset.l10nId,
    "trustpanel-connection-secure",
    "Secure status uses the redesigned string"
  );
  Assert.equal(
    document.getElementById("identity-popup-content-verifier").textContent,
    "Mozilla Testing",
    "The certificate issuer is shown"
  );
  Assert.equal(
    document
      .getElementById("trustpanel-siteinformation-morelink")
      .getAttribute("shortcut"),
    ShortcutUtils.prettifyShortcut(document.getElementById("key_viewInfo")),
    "More site information shows the Page Info shortcut"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_not_secure() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    // eslint-disable-next-line sdl/no-insecure-url
    opening: "http://example.com",
    waitForLoad: true,
  });

  let view = await openConnectionSubview();
  let notSecure = view.querySelector(
    ".identity-popup-connection-not-secure.security-view"
  );
  Assert.ok(BrowserTestUtils.isVisible(notSecure), "Not secure status shown");
  Assert.equal(
    notSecure.dataset.l10nId,
    "trustpanel-connection-not-secure",
    "Not secure status uses the redesigned string"
  );
  Assert.stringContains(
    connectionIconOf(view),
    "security-warning.svg",
    "Not secure status uses the lock with a warning icon"
  );
  let description = view.querySelector(
    ".identity-popup-description-insecure > label"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(description),
    "Not secure description is shown"
  );
  Assert.equal(
    description.dataset.l10nId,
    "trustpanel-connection-not-secure-description",
    "Not secure description uses the redesigned string"
  );
  let learnMore = view.querySelector(".trustpanel-connection-learn-more");
  Assert.ok(BrowserTestUtils.isVisible(learnMore), "Learn more is shown");
  Assert.ok(
    learnMore.href.endsWith("/connection-not-secure"),
    "Learn more points to the not secure connection support page"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_pref_off_restores_previous_design() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    // eslint-disable-next-line sdl/no-insecure-url
    opening: "http://example.com",
    waitForLoad: true,
  });

  let view = await openConnectionSubview();
  Assert.ok(view.hasAttribute("redesign"), "The redesign is used first");
  await UrlbarTestUtils.closeTrustPanel(window);

  await SpecialPowers.pushPrefEnv({ set: [[REDESIGN_PREF, false]] });
  view = await openConnectionSubview();
  Assert.ok(!view.hasAttribute("redesign"), "The redesign is turned off");
  Assert.ok(
    !view.hasAttribute("shrink-to-fit"),
    "The previous design keeps the main view height"
  );
  Assert.equal(
    document.l10n.getAttributes(view).id,
    "trustpanel-site-information-header",
    "The previous title is back"
  );
  Assert.equal(
    l10nIdOf(view, ".identity-popup-connection-secure.security-view"),
    "identity-connection-verified",
    "The previous secure status string is back"
  );
  Assert.equal(
    l10nIdOf(view, ".identity-popup-connection-not-secure.security-view"),
    "identity-connection-not-secure-security-view",
    "The previous not secure status string is back"
  );
  Assert.equal(
    l10nIdOf(view, ".identity-popup-description-insecure > label"),
    "identity-description-insecure",
    "The previous not secure description is back"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(
      view.querySelector(".trustpanel-connection-learn-more")
    ),
    "Learn more is hidden in the previous design"
  );
  Assert.stringContains(
    connectionIconOf(view),
    "security-broken.svg",
    "The previous not secure icon is back"
  );
  Assert.ok(
    !document
      .getElementById("trustpanel-siteinformation-morelink")
      .hasAttribute("shortcut"),
    "More site information has no shortcut"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await SpecialPowers.popPrefEnv();
  BrowserTestUtils.removeTab(tab);
});
