/*
 * Bug 1253771 - check mixed content blocking in combination with overriden certificates
 */

"use strict";

// Revealing the advanced panel is gated behind ten animation frames, and frame
// callbacks are throttled to 1Hz while the window is occluded.
requestLongerTimeout(2);

const MIXED_CONTENT_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://self-signed.example.com"
  ) + "test-mixedcontent-securityerrors.html";

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["security.dialog_enable_delay", 0]],
  });
});

function getConnectionState() {
  return document.getElementById("identity-popup").getAttribute("connection");
}

function getPopupContentVerifier() {
  return document.getElementById("identity-popup-content-verifier");
}

function getIdentityIcon() {
  return window.getComputedStyle(document.getElementById("identity-icon"))
    .listStyleImage;
}

async function checkIdentityPopup(icon) {
  await openIdentityPopup();
  gIdentityHandler.refreshIdentityPopup();
  is(getIdentityIcon(), `url("chrome://global/skin/icons/${icon}")`);
  is(getConnectionState(), "secure-cert-user-overridden");
  isnot(
    getPopupContentVerifier().style.display,
    "none",
    "Overridden certificate warning is shown"
  );
  ok(
    getPopupContentVerifier().textContent.includes("security exception"),
    "Text shows overridden certificate warning."
  );

  await closeIdentityPopup();
}

async function checkMixedContentCertOverride(feltPrivacyV1) {
  await SpecialPowers.pushPrefEnv({
    set: [["security.certerrors.felt-privacy-v1", feltPrivacyV1]],
  });
  let tab;
  try {
    tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);
    // check that a warning is shown when loading a page with mixed content and an overridden certificate
    await loadBadCertPage(MIXED_CONTENT_URL, feltPrivacyV1);
    await checkIdentityPopup("security-warning.svg");

    // check that a warning is shown even without mixed content
    BrowserTestUtils.startLoadingURIString(
      gBrowser.selectedBrowser,
      "https://self-signed.example.com"
    );
    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    await checkIdentityPopup("security-warning.svg");
  } finally {
    // remove cert exception
    let certOverrideService = Cc[
      "@mozilla.org/security/certoverride;1"
    ].getService(Ci.nsICertOverrideService);
    certOverrideService.clearValidityOverride(
      "self-signed.example.com",
      -1,
      {}
    );
    if (tab) {
      BrowserTestUtils.removeTab(tab);
    }
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async () => await checkMixedContentCertOverride(true));
add_task(async () => await checkMixedContentCertOverride(false));
