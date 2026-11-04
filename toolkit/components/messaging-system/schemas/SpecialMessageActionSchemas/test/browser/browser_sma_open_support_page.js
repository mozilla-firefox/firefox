/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SUPPORT_URL =
  Services.urlFormatter.formatURLPref("app.support.baseURL") + "refresh";

add_task(async function test_OPEN_SUPPORT_PAGE() {
  await BrowserTestUtils.withNewTab("about:blank", async browser => {
    const loaded = BrowserTestUtils.browserLoaded(browser, false, SUPPORT_URL);
    await SMATestUtils.executeAndValidateAction({
      type: "OPEN_SUPPORT_PAGE",
      data: { args: "refresh" },
    });
    await loaded;
    is(
      browser.currentURI.spec,
      SUPPORT_URL,
      "should open the support page in the current tab by default"
    );
  });
});

add_task(async function test_OPEN_SUPPORT_PAGE_tab() {
  const tabPromise = BrowserTestUtils.waitForNewTab(gBrowser, SUPPORT_URL);
  await SMATestUtils.executeAndValidateAction({
    type: "OPEN_SUPPORT_PAGE",
    data: { args: "refresh", where: "tab" },
  });

  const tab = await tabPromise;
  ok(tab, "should open the support page in a new tab");
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_OPEN_SUPPORT_PAGE_outside_support_site() {
  for (const args of [
    "../../../../example.com",
    "https://example.com/refresh",
    "//example.com/refresh",
  ]) {
    await Assert.rejects(
      SpecialMessageActions.handleAction(
        { type: "OPEN_SUPPORT_PAGE", data: { args } },
        gBrowser
      ),
      /OPEN_SUPPORT_PAGE disallows/,
      `should reject ${args}`
    );
  }
});
