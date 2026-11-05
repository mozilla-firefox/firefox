/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

add_setup(async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["security.qwacs.enabled", true],
      ["security.qwacs.enable_test_trust_anchors", true],
    ],
  });
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });
});

add_task(async function test_https() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-verifier-label")
    ),
    "'Verified by:' label visible"
  );
  let issuerOrganization = document.getElementById(
    "identity-popup-content-verifier"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(issuerOrganization),
    "issuer (verifier) organization text visible"
  );
  Assert.equal(
    issuerOrganization.textContent,
    "Mozilla Testing",
    "issuer (verifier) text as expected"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-verifier-unknown")
    ),
    "custom root warning in sub panel is visible"
  );

  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById(
        "identity-popup-content-cert-exception-overridden"
      )
    ),
    "user-added certificate error exception text is not visible"
  );

  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById("identity-popup-content-tls-key-logging")
    ),
    "TLS key logging warning should not be visible"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_https_with_tls_key_logging() {
  // Setting the environment variable at this point should not actually
  // enable TLS key logging, because NSS has already been initialized. This
  // just makes the front-end think it has.
  Services.env.set("SSLKEYLOGFILE", "nonexistent");

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://example.com",
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-tls-key-logging")
    ),
    "TLS key logging warning should be visible"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
  Services.env.set("SSLKEYLOGFILE", "");
});

add_task(async function test_http() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    // eslint-disable-next-line sdl/no-insecure-url
    opening: "http://example.com",
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById("identity-popup-content-verifier-unknown")
    ),
    "custom root warning in sub panel is hidden"
  );

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_1_qwac() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "https://1-qwac.example.com",
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  // Wait for the QWAC status to be determined.
  await gTrustPanelHandler.qwacStatusPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-etsi")
    ),
    "etsi qualified text visible"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-owner-label")
    ),
    "'Certificate issued to:' label visible"
  );

  let qwacOrganization = document.getElementById(
    "identity-popup-content-owner"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(qwacOrganization),
    "QWAC organization text visible"
  );
  Assert.equal(
    qwacOrganization.textContent,
    "Test 1-QWAC Organization",
    "QWAC organization text as expected"
  );

  let qwacLocation = document.getElementById(
    "identity-popup-content-supplemental"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(qwacLocation),
    "QWAC location text visible"
  );
  Assert.equal(
    qwacLocation.textContent,
    "1-QWAC Test Locality\nEX",
    "QWAC location text as expected"
  );

  let euTrustMark = document.getElementById(
    "identity-popup-content-eu-trust-mark"
  );
  Assert.ok(BrowserTestUtils.isVisible(euTrustMark), "EU trust mark visible");

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_2_qwac() {
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening:
      "https://bound-by-2-qwac.example.com/browser/browser/base/content/test/browser-siteIdentity/2-qwac.html",
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  // Wait for the QWAC status to be determined.
  await gTrustPanelHandler.qwacStatusPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-verifier-label")
    ),
    "'Verified by:' label visible"
  );
  let issuerOrganization = document.getElementById(
    "identity-popup-content-verifier"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(issuerOrganization),
    "issuer (verifier) organization text visible"
  );
  Assert.equal(
    issuerOrganization.textContent,
    "Test CA",
    "issuer (verifier) text as expected"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-etsi")
    ),
    "etsi qualified text visible"
  );

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-owner-label")
    ),
    "'Certificate issued to:' label visible"
  );

  let qwacOrganization = document.getElementById(
    "identity-popup-content-owner"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(qwacOrganization),
    "QWAC organization text visible"
  );
  Assert.equal(
    qwacOrganization.textContent,
    "Test 2-QWAC Organization",
    "QWAC organization text as expected"
  );

  let qwacLocation = document.getElementById(
    "identity-popup-content-supplemental"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(qwacLocation),
    "QWAC location text visible"
  );
  Assert.equal(
    qwacLocation.textContent,
    "2-QWAC Test Locality\nEX",
    "QWAC location text as expected"
  );

  let euTrustMark = document.getElementById(
    "identity-popup-content-eu-trust-mark"
  );
  Assert.ok(BrowserTestUtils.isVisible(euTrustMark), "EU trust mark visible");

  await UrlbarTestUtils.closeTrustPanel(window);
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_non_qwac() {
  let uris = [
    // eslint-disable-next-line sdl/no-insecure-url
    "http://example.com",
    "https://example.com",
    "data:,Hello%2C World!",
  ];
  for (let uri of uris) {
    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: uri,
      waitForLoad: true,
    });

    await UrlbarTestUtils.openTrustPanel(window);
    await UrlbarTestUtils.openTrustPanelSubview(
      window,
      "trustpanel-securityInformationView"
    );

    // Wait for the QWAC status to be determined.
    await gTrustPanelHandler.qwacStatusPromise;

    Assert.ok(
      BrowserTestUtils.isHidden(
        document.getElementById("identity-popup-content-etsi")
      ),
      "etsi qualified text not visible"
    );

    Assert.ok(
      BrowserTestUtils.isHidden(
        document.getElementById("identity-popup-content-owner-label")
      ),
      "'Certificate issued to:' label not visible"
    );

    Assert.ok(
      BrowserTestUtils.isHidden(
        document.getElementById("identity-popup-content-owner")
      ),
      "QWAC organization text not visible"
    );

    Assert.ok(
      BrowserTestUtils.isHidden(
        document.getElementById("identity-popup-content-supplemental")
      ),
      "QWAC location text not visible"
    );

    let euTrustMark = document.getElementById(
      "identity-popup-content-eu-trust-mark"
    );
    Assert.ok(
      !BrowserTestUtils.isVisible(euTrustMark),
      "EU trust mark not visible"
    );

    await UrlbarTestUtils.closeTrustPanel(window);
    await BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function test_no_qwac_ui_if_mixed_content() {
  let url =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://1-qwac.example.com"
    ) + "load_mixed_content_after_load.html";
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: url,
    waitForLoad: true,
  });

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  // Wait for the QWAC status to be determined.
  await gTrustPanelHandler.qwacStatusPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.getElementById("identity-popup-content-etsi")
    ),
    "etsi qualified text visible"
  );

  await UrlbarTestUtils.closeTrustPanel(window);

  // Cause the page to load mixed content.
  let httpSource =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      // eslint-disable-next-line sdl/no-insecure-url
      "http://example.com"
    ) + "silence.ogg";
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [httpSource],
    async function (source) {
      content.window.wrappedJSObject.loadTrack(source);
    }
  );

  await UrlbarTestUtils.openTrustPanel(window);
  await UrlbarTestUtils.openTrustPanelSubview(
    window,
    "trustpanel-securityInformationView"
  );

  // Wait for the QWAC status to be determined.
  await gTrustPanelHandler.qwacStatusPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(
      document.querySelector(
        "[data-l10n-id='identity-description-passive-loaded']"
      )
    ),
    "passive mixed content warning text should be visible"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(
      document.getElementById("identity-popup-content-etsi")
    ),
    "etsi qualified text should not be visible"
  );

  await BrowserTestUtils.removeTab(tab);
});

add_task(
  {
    // Test EV information is debug-only.
    skip_if: () => !AppConstants.DEBUG,
  },
  async function test_no_ev_ui_if_mixed_content() {
    let url =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        "https://ev.example.com"
      ) + "load_mixed_content_after_load.html";
    const tab = await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      opening: url,
      waitForLoad: true,
    });

    await UrlbarTestUtils.openTrustPanel(window);
    await UrlbarTestUtils.openTrustPanelSubview(
      window,
      "trustpanel-securityInformationView"
    );

    Assert.ok(
      BrowserTestUtils.isVisible(
        document.getElementById("identity-popup-content-owner")
      ),
      "ev owner text should be visible"
    );

    await UrlbarTestUtils.closeTrustPanel(window);

    // Cause the page to load mixed content.
    let httpSource =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        // eslint-disable-next-line sdl/no-insecure-url
        "http://example.com"
      ) + "silence.ogg";
    await SpecialPowers.spawn(
      tab.linkedBrowser,
      [httpSource],
      async function (source) {
        content.window.wrappedJSObject.loadTrack(source);
      }
    );

    await UrlbarTestUtils.openTrustPanel(window);
    await UrlbarTestUtils.openTrustPanelSubview(
      window,
      "trustpanel-securityInformationView"
    );

    Assert.ok(
      BrowserTestUtils.isVisible(
        document.querySelector(
          "[data-l10n-id='identity-description-passive-loaded']"
        )
      ),
      "passive mixed content warning text should be visible"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(
        document.getElementById("identity-popup-content-owner")
      ),
      "ev owner text should not be visible"
    );

    await BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_override_ui_with_mixed_content() {
  registerCleanupFunction(() => {
    let certOverrideService = Cc[
      "@mozilla.org/security/certoverride;1"
    ].getService(Ci.nsICertOverrideService);
    certOverrideService.clearValidityOverride("expired.example.com", 443, {});
  });

  let url =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://expired.example.com"
    ) + "load_mixed_content_after_load.html";

  // Load https://expired.example.com. It should result in a certificate error page.
  let certErrorLoaded;
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, url);
      let browser = gBrowser.selectedBrowser;
      certErrorLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await certErrorLoaded;
  // Spawn a task to open the advanced pane and click the button to add an exception.
  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, url);
  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("net-error-card")?.wrappedJSObject,
      "Waiting for net-error-card"
    );
    await netErrorCard.getUpdateComplete();
    const advancedButton = netErrorCard.advancedButton;
    advancedButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(advancedButton, {}, content);
    await ContentTaskUtils.waitForCondition(
      () =>
        netErrorCard.exceptionButton && !netErrorCard.exceptionButton.disabled,
      "Wait for the exception button to be created."
    );
    netErrorCard.exceptionButton.scrollIntoView(true);
    EventUtils.synthesizeMouseAtCenter(
      netErrorCard.exceptionButton,
      {},
      content
    );
  });
  await loaded;

  await UrlbarTestUtils.openTrustPanel(window);
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
    "certificate exception overridden text should be visible"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(
      document.querySelector(
        "[data-l10n-id='identity-description-passive-loaded']"
      )
    ),
    "passive mixed content warning text should not be visible"
  );

  await UrlbarTestUtils.closeTrustPanel(window);

  // Cause the page to load mixed content.
  let httpSource =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      // eslint-disable-next-line sdl/no-insecure-url
      "http://example.com"
    ) + "silence.ogg";
  await SpecialPowers.spawn(
    tab.linkedBrowser,
    [httpSource],
    async function (source) {
      content.window.wrappedJSObject.loadTrack(source);
    }
  );

  await UrlbarTestUtils.openTrustPanel(window);
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
    "certificate exception overridden text should still be visible"
  );
  Assert.ok(
    BrowserTestUtils.isVisible(
      document.querySelector(
        "[data-l10n-id='identity-description-passive-loaded']"
      )
    ),
    "passive mixed content warning text should be visible"
  );

  await BrowserTestUtils.removeTab(tab);
});
