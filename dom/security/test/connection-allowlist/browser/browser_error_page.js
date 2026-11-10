"use strict";

const TEST_PAGE =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "file_error_page.html";
const BLOCKED_URL = "https://example.org/";

function checkErrorPage(uri) {
  ok(
    uri.startsWith("about:neterror?e=blockedByConnectionAllowlist"),
    `Expected the Connection-Allowlist error page, got ${uri}`
  );
}

add_task(async function test_same_tab() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    const errorPageLoaded = BrowserTestUtils.waitForErrorPage(browser);
    await BrowserTestUtils.synthesizeMouseAtCenter("#self", {}, browser);
    await errorPageLoaded;
    checkErrorPage(browser.documentURI.spec);
  });
});

for (const id of ["#blank", "#open"]) {
  add_task(async function test_new_tab() {
    info(`Clicking ${id}`);
    await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
      const newTabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        BLOCKED_URL,
        true,
        false,
        true
      );
      await BrowserTestUtils.synthesizeMouseAtCenter(id, {}, browser);
      const tab = await newTabPromise;
      checkErrorPage(tab.linkedBrowser.documentURI.spec);
      BrowserTestUtils.removeTab(tab);
    });
  });
}

add_task(async function test_iframe() {
  await BrowserTestUtils.withNewTab(TEST_PAGE, async browser => {
    const errorPageLoaded = BrowserTestUtils.browserLoaded(browser, {
      includeSubFrames: true,
      wantLoad: BLOCKED_URL,
      maybeErrorPage: true,
    });
    await BrowserTestUtils.synthesizeMouseAtCenter("#iframe", {}, browser);
    await errorPageLoaded;
    checkErrorPage(
      await SpecialPowers.spawn(
        browser.browsingContext.children[0],
        [],
        () => content.document.documentURI
      )
    );
  });
});
