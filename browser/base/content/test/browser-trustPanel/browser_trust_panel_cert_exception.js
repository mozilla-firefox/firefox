/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test TrustPanel on pages with certificate override.
 */

"use strict";

function fetchIconUrl(doc, id) {
  let icon = doc.defaultView.getComputedStyle(
    doc.getElementById(id)
  ).listStyleImage;
  return icon.match(/url\("([^"]+)"\)/)?.[1] ?? null;
}

add_task(async () => {
  registerCleanupFunction(() => {
    // Remove Exception
    let certOverrideService = Cc[
      "@mozilla.org/security/certoverride;1"
    ].getService(Ci.nsICertOverrideService);
    certOverrideService.clearValidityOverride(
      "self-signed.example.com",
      -1,
      {}
    );
  });
  await loadBadCertPage("https://self-signed.example.com");

  Assert.equal(
    fetchIconUrl(window.document, "trust-icon"),
    "chrome://browser/skin/trust-icon-warning.svg",
    "Trustpanel urlbar icon shows insecure"
  );
  await UrlbarTestUtils.openTrustPanel(window);
  Assert.ok(
    !BrowserTestUtils.isVisible(
      document.getElementById("trustpanel-insecure-section")
    ),
    "Don't show unencryped warning on self signed certs"
  );

  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById(
        "identity-popup-content-cert-exception-overridden"
      )
    ),
    "Show user-added certificate error exception text"
  );
  await UrlbarTestUtils.closeTrustPanel(window);
});
