/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Tests for Bug 1535210 - Set SSL STATE_IS_BROKEN flag for TLS1.0 and TLS 1.1 connections
 */

const HTTPS_TLS1_0 = "https://tls1.example.com";
const HTTPS_TLS1_1 = "https://tls11.example.com";
const HTTPS_TLS1_2 = "https://tls12.example.com";
const HTTPS_TLS1_3 = "https://tls13.example.com";

function getIdentityMode(aWindow = window) {
  return aWindow.document.getElementById("identity-box").className;
}

async function checkConnectionState(state) {
  await openIdentityPopup();
  is(getConnectionState(), state, "connectionState should be " + state);
  await closeIdentityPopup();
}

function getConnectionState() {
  return document.getElementById("identity-popup").getAttribute("connection");
}

registerCleanupFunction(function () {
  // Set preferences back to their original values
  Services.prefs.clearUserPref("security.tls.version.min");
  Services.prefs.clearUserPref("security.tls.version.max");
});

add_task(async function () {
  // Run with all versions enabled for this test.
  Services.prefs.setIntPref("security.tls.version.min", 1);
  Services.prefs.setIntPref("security.tls.version.max", 4);

  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    // Try deprecated versions
    BrowserTestUtils.startLoadingURIString(browser, HTTPS_TLS1_0);
    await BrowserTestUtils.browserLoaded(browser);
    isSecurityState(browser, "broken");
    is(
      getIdentityMode(),
      "unknownIdentity weakCipher",
      "Identity should be unknownIdentity"
    );
    await checkConnectionState("not-secure");

    BrowserTestUtils.startLoadingURIString(browser, HTTPS_TLS1_1);
    await BrowserTestUtils.browserLoaded(browser);
    isSecurityState(browser, "broken");
    is(
      getIdentityMode(),
      "unknownIdentity weakCipher",
      "Identity should be unknownIdentity"
    );
    await checkConnectionState("not-secure");

    // Transition to secure
    BrowserTestUtils.startLoadingURIString(browser, HTTPS_TLS1_2);
    await BrowserTestUtils.browserLoaded(browser);
    isSecurityState(browser, "secure");
    is(getIdentityMode(), "verifiedDomain", "Identity should be verified");
    await checkConnectionState("secure");

    // Transition back to broken
    BrowserTestUtils.startLoadingURIString(browser, HTTPS_TLS1_1);
    await BrowserTestUtils.browserLoaded(browser);
    isSecurityState(browser, "broken");
    is(
      getIdentityMode(),
      "unknownIdentity weakCipher",
      "Identity should be unknownIdentity"
    );
    await checkConnectionState("not-secure");

    // TLS1.3 for completeness
    BrowserTestUtils.startLoadingURIString(browser, HTTPS_TLS1_3);
    await BrowserTestUtils.browserLoaded(browser);
    isSecurityState(browser, "secure");
    is(getIdentityMode(), "verifiedDomain", "Identity should be verified");
    await checkConnectionState("secure");
  });
});
