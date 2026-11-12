/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// These tests send real CTAP traffic through a virtual authenticator, so they
// cover the whole path from authrs_bridge to the about:webauthn log.

const webauthnService = Cc["@mozilla.org/webauthn/service;1"].getService(
  Ci.nsIWebAuthnService
);
const DUMMY_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "dummy_page.html";

let authenticatorId;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["security.webauth.webauthn_enable_softtoken", true]],
  });
  authenticatorId = webauthnService.addVirtualAuthenticator(
    "ctap2_1",
    "internal",
    true,
    true,
    true,
    true
  );
  registerCleanupFunction(() => {
    webauthnService.removeVirtualAuthenticator(authenticatorId);
  });
});

async function openAboutWebauthn() {
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:webauthn",
    waitForLoad: true,
  });
  return { tab, doc: tab.linkedBrowser.contentDocument };
}

// Opens the dummy page, which registers a credential for `name`, and waits
// for the registration to settle.
async function registerCredential(name) {
  let url = DUMMY_PAGE + "?name=" + encodeURIComponent(name);
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: url,
    waitForLoad: true,
  });
  await TestUtils.waitForCondition(
    () => ["success", "error"].includes(tab.linkedBrowser.contentTitle),
    "Waiting for credentials.create() to settle"
  );
  is(tab.linkedBrowser.contentTitle, "success", "Registration succeeded");
  await BrowserTestUtils.removeTab(tab);
}

add_task(async function test_logging_make_credential() {
  let { tab, doc } = await openAboutWebauthn();
  let log = doc.getElementById("webauthn-log");

  doc.getElementById("start-logging-button").click();
  // A quote in a site-controlled string must not break the JSON transport.
  await registerCredential('Fox "Q"');
  await TestUtils.waitForCondition(
    () => log.textContent.includes("<-- [CTAP2] Ok(MakeCredentialsResult {"),
    "Waiting for the MakeCredentials response to be logged"
  );
  doc.getElementById("stop-logging-button").click();

  let text = log.textContent;
  ok(text.includes("--> [CTAP2] MakeCredentials {"), "Request was logged");
  ok(text.includes('id: "example.com"'), "RP id was logged");
  ok(text.includes('name: Some("Fox \\"Q\\"")'), "User name is intact");

  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_logging_stops_when_page_closes() {
  let logged = [];
  let observer = (_subject, _topic, data) => logged.push(data);
  Services.obs.addObserver(observer, "webauthn-logging");

  let { tab, doc } = await openAboutWebauthn();
  doc.getElementById("start-logging-button").click();
  await BrowserTestUtils.removeTab(tab);

  await registerCredential("AfterClose");
  await TestUtils.waitForTick();
  Services.obs.removeObserver(observer, "webauthn-logging");

  is(logged.length, 0, "Nothing was logged after the page was closed");
});
