/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const AUTH_ROUTE =
  // eslint-disable-next-line sdl/no-insecure-url
  "http://example.com/browser/browser/base/content/test/browser-about/basic_auth_route.sjs";

// From appstrings.properties
const EXPECTED_SHORT_DESC =
  "The authentication method requested by the site has been disabled by your connection settings.";

async function loadDisabledSchemeErrorPage(feltPrivacy) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.https_first", false],
      ["network.http.auth.disabled-schemes", "basic"],
      ["browser.http.blank_page_with_error_response.enabled", true],
      ["security.certerrors.felt-privacy-v1", feltPrivacy],
    ],
  });

  let browser;
  let pageLoaded;
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, AUTH_ROUTE);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );
  await pageLoaded;
  return browser;
}

add_task(async function test_httpAuthSchemeDisabled_legacy() {
  const browser = await loadDisabledSchemeErrorPage(false);

  await SpecialPowers.spawn(
    browser,
    [EXPECTED_SHORT_DESC],
    async function (expectedShortDesc) {
      const doc = content.document;
      ok(
        doc.documentURI.startsWith("about:neterror?e=httpAuthSchemeDisabled"),
        "Should be showing the httpAuthSchemeDisabled error page"
      );

      is(
        doc.querySelector(".title-text").getAttribute("data-l10n-id"),
        "neterror-http-auth-scheme-disabled-title",
        "Correct error page title is set"
      );

      const shortDesc = doc.getElementById("errorShortDesc");
      await ContentTaskUtils.waitForCondition(
        () => !!shortDesc.textContent.trim().length
      );
      ok(
        shortDesc.textContent.startsWith(expectedShortDesc),
        "Correct short description is set"
      );

      ok(
        doc.querySelector(
          '#errorLongDesc [data-l10n-id="neterror-http-auth-scheme-disabled"]'
        ),
        "Long description includes the disabled scheme hint"
      );
      ok(
        doc.getElementById("netErrorButtonContainer").hidden,
        "Try Again button is hidden"
      );
    }
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_httpAuthSchemeDisabled_feltPrivacy() {
  const browser = await loadDisabledSchemeErrorPage(true);

  await SpecialPowers.spawn(browser, [], async function () {
    const doc = content.document;
    ok(
      doc.documentURI.startsWith("about:neterror?e=httpAuthSchemeDisabled"),
      "Should be showing the httpAuthSchemeDisabled error page"
    );

    const netErrorCard = await ContentTaskUtils.waitForCondition(
      () => doc.querySelector("net-error-card")?.wrappedJSObject,
      "net-error-card should be rendered"
    );
    await netErrorCard.getUpdateComplete();

    is(
      netErrorCard.errorTitle.dataset.l10nId,
      "neterror-http-auth-scheme-disabled-title",
      "Using the httpAuthSchemeDisabled title"
    );
    is(
      netErrorCard.errorIntro.dataset.l10nId,
      "fp-neterror-http-auth-scheme-disabled-intro",
      "Using the httpAuthSchemeDisabled intro"
    );
    ok(
      netErrorCard.renderRoot.querySelector(
        '[data-l10n-id="neterror-http-auth-scheme-disabled"]'
      ),
      "Description includes the disabled scheme hint"
    );
    ok(!netErrorCard.tryAgainButton, "Try Again button is not shown");
    ok(!netErrorCard.exceptionButton, "Proceed button is not shown");
  });

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});
