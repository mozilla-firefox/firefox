/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SERVER_ERROR_PAGE =
  "https://example.com/browser/browser/base/content/test/about/server_error.sjs";

add_setup(async function () {
  await setSecurityCertErrorsFeltPrivacyToTrue();
});

registerCleanupFunction(() => {
  Services.io.offline = false;
});

add_task(async function test_noConnection_illustration() {
  let browser, tab;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        SERVER_ERROR_PAGE
      );
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
    },
    false
  );
  await BrowserTestUtils.waitForErrorPage(browser);

  await SpecialPowers.spawn(browser, [], async () => {
    const netErrorCard = content.document.querySelector("net-error-card");
    await netErrorCard.wrappedJSObject.getUpdateComplete();
    const img = netErrorCard.shadowRoot.querySelector("img");
    Assert.ok(img, "illustration img element exists");
    Assert.equal(
      img.getAttribute("src"),
      "chrome://global/skin/illustrations/no-connection.svg",
      "noConnection illustration src is correct"
    );
    Assert.equal(
      img.getAttribute("alt"),
      "",
      "noConnection illustration is decorative"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_workOffline_showsNoConnectionIllustration() {
  Services.io.offline = true;

  let browser, tab, errorPageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        "https://does-not-exist.test"
      );
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
      errorPageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await errorPageLoaded;

  await SpecialPowers.spawn(browser, [], async () => {
    const netErrorCard = content.document.querySelector("net-error-card");
    await netErrorCard.wrappedJSObject.getUpdateComplete();
    const img = netErrorCard.shadowRoot.querySelector("img");
    Assert.ok(img, "illustration img element exists");
    Assert.equal(
      img.getAttribute("src"),
      "chrome://global/skin/illustrations/no-connection.svg",
      "work offline illustration src is correct"
    );
    Assert.equal(
      img.getAttribute("alt"),
      "",
      "work offline illustration is decorative"
    );
  });

  Services.io.offline = false;
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_securityError_illustration() {
  const tab = await openErrorPage("https://expired.example.com/");
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const netErrorCard = content.document.querySelector("net-error-card");
    await netErrorCard.wrappedJSObject.getUpdateComplete();
    const img = netErrorCard.shadowRoot.querySelector("img");
    Assert.ok(img, "illustration img element exists");
    Assert.equal(
      img.getAttribute("src"),
      "chrome://global/skin/illustrations/security-error.svg",
      "securityError illustration src is correct"
    );
    Assert.equal(
      img.getAttribute("alt"),
      "",
      "securityError illustration is decorative"
    );
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_illustrationPrefOff_securityError() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.netError.illustration.enabled", false]],
  });

  const tab = await openErrorPage("https://expired.example.com/");
  const browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], async () => {
    const netErrorCard = content.document.querySelector("net-error-card");
    await netErrorCard.wrappedJSObject.getUpdateComplete();
    Assert.ok(
      !netErrorCard.shadowRoot.querySelector(".img-container"),
      "no illustration container when the pref is off"
    );
    Assert.ok(
      netErrorCard.shadowRoot.querySelector(".container"),
      "error content still renders without the illustration"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_illustrationPrefOff_noConnection() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.netError.illustration.enabled", false]],
  });

  let browser, tab;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        SERVER_ERROR_PAGE
      );
      browser = gBrowser.selectedBrowser;
      tab = gBrowser.selectedTab;
    },
    false
  );
  await BrowserTestUtils.waitForErrorPage(browser);

  await SpecialPowers.spawn(browser, [], async () => {
    const netErrorCard = content.document.querySelector("net-error-card");
    await netErrorCard.wrappedJSObject.getUpdateComplete();
    Assert.ok(
      !netErrorCard.shadowRoot.querySelector(".img-container"),
      "no illustration container when the pref is off"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
