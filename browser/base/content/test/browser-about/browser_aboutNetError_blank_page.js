/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BLANK_PAGE =
  "https://example.com/browser/browser/base/content/test/browser-about/blank_page.sjs";

async function getConnectionState() {
  document.getElementById("trust-icon-container").click();
  let popup = await TestUtils.waitForCondition(
    () => document.getElementById("trustpanel-popup"),
    "Waiting for trustpanel-popup to be instantiated"
  );
  await BrowserTestUtils.waitForEvent(popup, "popupshown");

  return popup.getAttribute("connection");
}

async function test_blankPage(
  page,
  expectedL10nID,
  responseStatus,
  responseStatusText,
  header = "show" // show (zero content-length), hide (no content-length), lie (non-empty content-length), or brotli/gzip (encoded body that decodes to nothing)
) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.http.blank_page_with_error_response.enabled", false]],
  });

  let browser;
  let pageLoaded;
  const uri = `${page}?status=${encodeURIComponent(
    responseStatus
  )}&message=${encodeURIComponent(responseStatusText)}&header=${encodeURIComponent(header)}`;

  // Simulating loading the page
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser, uri);
      browser = gBrowser.selectedBrowser;
      pageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    },
    false
  );

  info("Loading and waiting for the net error");
  await pageLoaded;

  is(
    await getConnectionState(),
    "secure",
    "httpErrorPage/serverError should be a secure neterror"
  );

  await SpecialPowers.spawn(
    browser,
    [expectedL10nID, responseStatus, responseStatusText],
    async function (l10nID, expectedStatus, expectedText) {
      const doc = content.document;
      ok(
        doc.documentURI.startsWith("about:neterror"),
        "Should be showing error page"
      );

      let titleEl;
      let actualDataL10nID;

      const netErrorCard = doc.querySelector("net-error-card");
      if (netErrorCard) {
        const card = netErrorCard.wrappedJSObject;
        await card.getUpdateComplete();

        titleEl = card.errorTitle;

        const introEl = card.shadowRoot.getElementById("error-intro");
        is(
          introEl?.getAttribute("data-l10n-id"),
          "fp-neterror-http-error-intro",
          "Intro element has correct l10n id"
        );
        const introArgs = JSON.parse(introEl?.getAttribute("data-l10n-args"));
        ok(introArgs?.hostname, "Intro has hostname arg");
        is(
          introArgs?.responsestatus,
          expectedStatus,
          "Intro has correct responsestatus"
        );
        is(
          introArgs?.responsestatustext,
          expectedText,
          "Intro has correct responsestatustext"
        );
      } else {
        titleEl = doc.querySelector(".title-text");

        const expectedLabel =
          "Error code: " + expectedStatus.toString() + " " + expectedText;
        const responseStatusLabel = await ContentTaskUtils.waitForCondition(
          () => doc.getElementById("response-status-label"),
          "Waiting for response-status-label"
        );
        is(
          responseStatusLabel.textContent,
          expectedLabel,
          "Correct response status message is set"
        );
      }

      actualDataL10nID = titleEl.getAttribute("data-l10n-id");
      is(actualDataL10nID, l10nID, "Correct error page title is set");
    }
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_blankPage_4xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    400,
    "Bad Request"
  );
});

add_task(async function test_blankPage_5xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    503,
    "Service Unavailable"
  );
});

add_task(async function test_blankPage_withoutHeader_4xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    400,
    "Bad Request",
    "hide"
  );
});

add_task(async function test_blankPage_withoutHeader_5xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    503,
    "Service Unavailable",
    "hide"
  );
});

add_task(async function test_blankPage_lyingHeader_4xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    400,
    "Bad Request",
    "lie"
  );
});

add_task(async function test_blankPage_lyingHeader_5xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    503,
    "Service Unavailable",
    "lie"
  );
});

add_task(async function test_blankPage_emptyBrotliBody_4xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    403,
    "Forbidden",
    "brotli"
  );
});

add_task(async function test_blankPage_emptyGzipBody_5xx() {
  await test_blankPage(
    BLANK_PAGE,
    "problem-with-this-site-title",
    503,
    "Service Unavailable",
    "gzip"
  );
});

add_task(async function test_encodedNonEmptyBody_4xx() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.http.blank_page_with_error_response.enabled", false]],
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `${BLANK_PAGE}?status=404&message=Not%20Found&header=gzip-content`,
    true // wait for the load to complete
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {
    const doc = content.document;
    ok(
      !doc.documentURI.startsWith("about:neterror"),
      "Should not be showing error page for an encoded non-empty body"
    );
    is(doc.body.textContent, "Not Found", "Decoded body should be shown");
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_emptyPage_viewSource() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.http.blank_page_with_error_response.enabled", false]],
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    `view-source:${BLANK_PAGE}?status=503&message=Service%20Unavailable&header=show`,
    true // wait for the load to complete
  );
  let browser = tab.linkedBrowser;

  await SpecialPowers.spawn(browser, [], () => {
    const doc = content.document;
    ok(
      !doc.documentURI.startsWith("about:neterror"),
      "Should not be showing error page since the scheme is view-source"
    );
  });

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
