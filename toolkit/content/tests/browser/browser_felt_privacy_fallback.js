/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for the fallback behavior during error resolution.
 *
 * These tests verify three scenarios:
 * 1. A registered SSL error resolves to its own config ID.
 * 2. An unregistered error does not trigger the nssFailure2 fallback when
 *    gErrorCode is a registered error other than "nssFailure2".
 * 3. The deniedPortAccess error shows the correct intro text.
 */

const TLS10_PAGE = "https://tls1.example.com/";

async function openErrorTab(url) {
  let browser;
  let tab;
  let pageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, url);
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await pageLoaded;
  return { browser, tab };
}

// Verify that a registered SSL error resolves directly to its own config,
// not to the nssFailure2 fallback.
add_task(async function test_registered_ssl_error() {
  info(
    "Testing that SSL_ERROR_RX_RECORD_TOO_LONG resolves to its own registry ID"
  );

  Services.prefs.setIntPref("security.tls.version.min", 3);
  Services.prefs.setIntPref("security.tls.version.max", 4);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("security.tls.version.min");
    Services.prefs.clearUserPref("security.tls.version.max");
  });

  const { browser, tab } = await openErrorTab(TLS10_PAGE);
  registerCleanupFunction(() => BrowserTestUtils.removeTab(tab));

  await SpecialPowers.spawn(browser, [], async () => {
    const doc = content.document;
    Assert.ok(
      doc.documentURI.startsWith("about:neterror"),
      "Should be on the error page"
    );

    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => doc.querySelector("net-error-card")?.wrappedJSObject,
      "net-error-card should be present"
    );
    await netErrorCard.getUpdateComplete();

    // Call resolveErrorID directly with a mocked SSL_ERROR_RX_RECORD_TOO_LONG
    // input. initializeRegistry populates the full registry so that scenario 1
    // (direct match) correctly resolves the registered error.
    const { resolveErrorID, getResolvedErrorConfig } =
      ChromeUtils.importESModule(
        "chrome://global/content/errors/error-lookup.mjs"
      );
    const { initializeRegistry } = ChromeUtils.importESModule(
      "chrome://global/content/errors/error-registry.mjs"
    );
    initializeRegistry();
    Assert.equal(
      resolveErrorID({
        errorCodeString: "SSL_ERROR_RX_RECORD_TOO_LONG",
        gErrorCode: "nssFailure2",
        noConnectivity: false,
      }),
      "SSL_ERROR_RX_RECORD_TOO_LONG",
      "Should resolve to the registered error, not fall back to nssFailure2"
    );

    // An unregistered SSL error falling through to nssFailure2 should expose
    // its runtime errorCodeString via mapCustomNetErrorConfigToParams.
    const UNREGISTERED_CODE = "SSL_ERROR_DECRYPT_ERROR_ALERT";
    const mockErrorInfo = new content.Object();
    mockErrorInfo.errorCodeString = UNREGISTERED_CODE;
    netErrorCard.errorInfo = mockErrorInfo;

    const config = getResolvedErrorConfig("nssFailure2", {
      hostname: "example.com",
      errorInfo: mockErrorInfo,
      noConnectivity: false,
      offline: false,
    });
    const params = netErrorCard.mapCustomNetErrorConfigToParams(
      config.customNetError,
      config
    );
    Assert.equal(
      params.errorCode,
      UNREGISTERED_CODE,
      "mapCustomNetErrorConfigToParams should use the runtime errorCodeString for unregistered SSL errors"
    );
  });

  BrowserTestUtils.removeTab(tab);
  Services.prefs.clearUserPref("security.tls.version.min");
  Services.prefs.clearUserPref("security.tls.version.max");
});

add_task(async function test_no_fallback_for_non_nssFailure2_gErrorCode() {
  info(
    "Testing that deniedPortAccess resolves directly to its own registry ID"
  );
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["network.security.ports.banned.override", ""],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  // eslint-disable-next-line sdl/no-insecure-url
  const { browser, tab } = await openErrorTab("http://example.com:25");
  registerCleanupFunction(() => BrowserTestUtils.removeTab(tab));

  await SpecialPowers.spawn(browser, [], async () => {
    const doc = content.document;
    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => doc.querySelector("net-error-card")?.wrappedJSObject,
      "net-error-card should be present for deniedPortAccess"
    );
    await netErrorCard.getUpdateComplete();

    Assert.equal(
      netErrorCard.resolvedErrorId,
      "deniedPortAccess",
      "resolvedErrorId should be deniedPortAccess"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_denied_port_access_intro_text() {
  info("Testing deniedPortAccess shows correct intro text with felt privacy");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["network.security.ports.banned.override", ""],
    ],
  });
  registerCleanupFunction(() => SpecialPowers.popPrefEnv());

  // Port 25 (SMTP) is on Firefox's blocked port list.
  // eslint-disable-next-line sdl/no-insecure-url
  const { browser, tab } = await openErrorTab("http://example.com:25");
  registerCleanupFunction(() => BrowserTestUtils.removeTab(tab));

  await SpecialPowers.spawn(browser, [], async () => {
    const doc = content.document;
    Assert.ok(
      doc.documentURI.startsWith("about:neterror"),
      "Should be on the error page"
    );
    Assert.ok(
      doc.documentURI.includes("deniedPortAccess"),
      "Error code should be deniedPortAccess"
    );

    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => doc.querySelector("net-error-card")?.wrappedJSObject,
      "net-error-card should be present for deniedPortAccess"
    );
    await netErrorCard.getUpdateComplete();

    const introEl = netErrorCard.errorIntro;
    Assert.ok(introEl, "Intro element should exist");
    Assert.equal(
      introEl.dataset.l10nId,
      "fp-neterror-denied-port-access",
      "Should show the denied port access intro, not the offline intro"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
