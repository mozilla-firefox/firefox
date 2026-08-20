/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_click_findbar_to_select_panel() {
  const tab1 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 1</title>"
  );
  const tab2 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 2</title>"
  );
  const panel1 = document.getElementById(tab1.linkedPanel);
  const panel2 = document.getElementById(tab2.linkedPanel);

  info("Activate split view with the first panel selected.");
  await withSplitView(tab1, tab2, async () => {
    Assert.ok(
      panel1.classList.contains("deck-selected"),
      "First panel is selected."
    );

    info("Activate Find in Page within the second panel.");
    const findbar = await gBrowser.getFindBar(tab2);
    const promiseFindbarOpen = BrowserTestUtils.waitForEvent(
      findbar,
      "findbaropen"
    );
    findbar.open();
    await promiseFindbarOpen;

    info("Select the second panel by clicking the find bar.");
    findbar.getElement("findbar-textbox").click();
    await BrowserTestUtils.waitForMutationCondition(
      panel2,
      { attributeFilter: ["class"] },
      () => panel2.classList.contains("deck-selected")
    );
  });
});

add_task(async function test_findbar_persists_on_both_panels_after_reverse() {
  const tab1 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 1</title>"
  );
  const tab2 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 2</title>"
  );
  await withSplitView(tab1, tab2, async ({ splitView }) => {
    const findbar1 = await gBrowser.getFindBar();
    gFindBar.open();

    await BrowserTestUtils.switchTab(gBrowser, tab2);
    const findbar2 = await gBrowser.getFindBar();
    gFindBar.open();

    Assert.ok(BrowserTestUtils.isVisible(findbar1), "findbar1 is visible");
    Assert.ok(BrowserTestUtils.isVisible(findbar2), "findbar2 is visible");

    info("Reverse the split view tabs.");
    splitView.reverseTabs();

    Assert.ok(BrowserTestUtils.isVisible(findbar1), "findbar1 remains visible");
    Assert.ok(BrowserTestUtils.isVisible(findbar2), "findbar2 remains visible");
  });
});
