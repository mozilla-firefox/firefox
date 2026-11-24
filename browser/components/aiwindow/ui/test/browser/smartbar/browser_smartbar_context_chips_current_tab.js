/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests that context url for user messages and smartbar context chips, which
 * are retrieved from the same function, work in both places. Context urls
 * come from aiwindow/ui/modules/ChatUtils.sys.mjs::getCurrentTabUrl()
 */

let win;
let mockEngine;

add_setup(async function () {
  mockEngine = mockEngineForFile();
  win = await openSharedAIWindow();
});

describe("Smartbar current tab context chip", () => {
  let sidebarBrowser;

  beforeEach(async () => {
    sidebarBrowser = await openOnlyPageTab(win, EXAMPLE_DOMAIN_PAGE);
  });

  afterEach(async () => {
    await mockEngine.rejectAllRequests();
  });

  it("shows the page title", async () => {
    const labels = await getSmartbarContextChipLabels(
      sidebarBrowser,
      EXAMPLE_DOMAIN_PAGE
    );

    Assert.ok(AIWindowUI.isSidebarOpen(win), "The page opens the sidebar");
    Assert.deepEqual(labels, ["Example Domain"], "The chip shows the title");
  });

  it("follows the tab to a new page", async () => {
    Assert.deepEqual(
      await getSmartbarContextChipLabels(sidebarBrowser, EXAMPLE_DOMAIN_PAGE),
      ["Example Domain"],
      "The chip shows the first page's title"
    );

    await promiseNavigateAndLoad(
      win.gBrowser.selectedBrowser,
      WEBSITE_TITLE_PAGE
    );

    await TestUtils.waitForCondition(
      () => win.gBrowser.selectedTab.label === "Website Title",
      "Wait for tab title to update to 'Website Title'"
    );
    const labels = await getSmartbarContextChipLabels(
      sidebarBrowser,
      WEBSITE_TITLE_PAGE
    );
    Assert.deepEqual(
      labels,
      ["Website Title"],
      "The only chip shows the new page's title"
    );
  });

  it("labels the submitted message with the page", async () => {
    await typeInSmartbar(sidebarBrowser, "test");

    await submitSmartbar(sidebarBrowser);

    Assert.deepEqual(
      await getUserMessageChipLabels(sidebarBrowser),
      ["Example Domain"],
      "The message has a chip for the page"
    );
  });
});
