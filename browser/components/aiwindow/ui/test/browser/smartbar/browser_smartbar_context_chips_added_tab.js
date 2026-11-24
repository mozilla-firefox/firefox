/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A page no other request in the file mentions, to find its starter request.
const NAVIGATED_PAGE = "https://example.org/";

let win;
let mockEngine;

add_setup(async function () {
  mockEngine = mockEngineForFile();
  win = await openSharedAIWindow();
});

describe("Smartbar context chip for a tab added with the + button", () => {
  let sidebarBrowser;

  beforeEach(async () => {
    sidebarBrowser = await openOnlyPageTab(win, EXAMPLE_DOMAIN_PAGE);
    const tab = BrowserTestUtils.addTab(win.gBrowser, WEBSITE_TITLE_PAGE);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
      wantLoad: WEBSITE_TITLE_PAGE,
    });
    await TestUtils.waitForCondition(
      () => tab.label === "Website Title",
      "Wait for second tab title to update"
    );

    await openTabContextMenuAndClickTabByLabel(sidebarBrowser, "Website Title");
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("is in the starter prompt request after the current tab navigates", async () => {
    Assert.deepEqual(
      await getSmartbarContextChipLabels(sidebarBrowser, WEBSITE_TITLE_PAGE),
      ["Example Domain", "Website Title"],
      "Both tabs have chips"
    );

    await promiseNavigateAndLoad(win.gBrowser.selectedBrowser, NAVIGATED_PAGE);

    Assert.ok(
      (await captureStarterPrompt(NAVIGATED_PAGE)).includes(WEBSITE_TITLE_PAGE),
      "The starter prompt request includes the added tab"
    );
  });

  it("labels the submitted message with both tabs", async () => {
    await typeInSmartbar(sidebarBrowser, "test");

    await submitSmartbar(sidebarBrowser);

    Assert.deepEqual(
      await getUserMessageChipLabels(sidebarBrowser),
      ["Example Domain", "Website Title"],
      "The message has a chip for each tab"
    );
  });
});

/**
 * Waits for the sidebar's starter prompt request that mentions a page. The
 * requests for the tab's earlier pages can arrive after the navigation, so
 * answer those first.
 *
 * @param {string} url
 * @returns {Promise<string>} The request's prompt.
 */
async function captureStarterPrompt(url) {
  for (;;) {
    const { request, respond } = await mockEngine.captureRequest({
      purpose: "convo-starters-sidebar",
    });
    respond("");
    const { content } = request.args.findLast(({ role }) => role === "user");
    if (content.includes(url)) {
      return content;
    }
  }
}
