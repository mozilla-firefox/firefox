"use strict";

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  })
);

/**
 * Verify Tab key traversal order: first panel -> splitter -> second panel,
 * and Shift+Tab in reverse.
 */
async function check_SplitViewTabOrder() {
  const tab1 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 1</title>"
  );
  const tab2 = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab 2</title>"
  );
  await withSplitView(tab1, tab2, async ({ splitter }) => {
    const firstBrowser = tab1.linkedBrowser;
    const secondBrowser = tab2.linkedBrowser;

    Assert.ok(
      splitter && BrowserTestUtils.isVisible(splitter),
      "The splitter is visible"
    );
    // There's no focusable elements in the content documents, this test
    // just verifies the focus sequence on chrome UI elements when tabbing through
    // the splitview.
    info("Focus the first panel's browser");
    Services.focus.setFocus(firstBrowser, Services.focus.FLAG_BYKEY);
    Assert.equal(
      document.activeElement,
      firstBrowser,
      "First panel browser has focus"
    );

    info("Tab from first panel -> splitter");
    await expectFocusAfterKey(splitter, "KEY_Tab");

    info("Tab from splitter -> second panel");
    await expectFocusAfterKey(secondBrowser, "KEY_Tab");

    info("Shift+Tab from second panel -> splitter");
    await expectFocusAfterKey(splitter, "KEY_Tab", { shiftKey: true });

    info("Shift+Tab from splitter -> first panel");
    await expectFocusAfterKey(firstBrowser, "KEY_Tab", { shiftKey: true });
  });
  // Closing the splitview also closes the tabs
}

add_task(async function test_SplitViewTabOrder_LTR() {
  await check_SplitViewTabOrder();
});

add_task(async function test_SplitViewTabOrder_RTL() {
  await SpecialPowers.pushPrefEnv({ set: [["intl.l10n.pseudo", "bidi"]] });
  await check_SplitViewTabOrder();
  await SpecialPowers.popPrefEnv();
});

async function assertContentActiveElement(browser, selector, message) {
  await SpecialPowers.spawn(browser, [selector, message], async (sel, msg) => {
    await ContentTaskUtils.waitForCondition(
      () =>
        content.document.activeElement === content.document.querySelector(sel),
      msg
    );
  });
}

add_task(async function test_SplitViewContentFocusableTabOrder() {
  // tabInitial is the already-open tab
  const tabInitial = gBrowser.selectedTab;
  const tabCharm = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab Charm</title><body>Text, and <a href='https://example.org/'>a link</a></body>"
  );
  const tabStrange = BrowserTestUtils.addTab(
    gBrowser,
    "data:text/html,<title>Tab Strange: Opened from Initial</title><body>Text, and input: <input></body>",
    // should open next to tabInitial
    { openerBrowser: tabInitial.linkedBrowser }
  );
  Assert.equal(
    [tabInitial, tabStrange, tabCharm].map(tab => tab.label).join("\n"),
    gBrowser.visibleTabs.map(tab => tab.label).join("\n"),
    "Visible tabs are in the expected order"
  );

  await withSplitView(
    tabStrange,
    tabCharm,
    async ({ tab1, tab2, splitter }) => {
      Assert.equal(tab1, tabStrange, "First column is tabStrange");
      Assert.equal(tab2, tabCharm, "2nd column is tabCharm");
      const firstBrowser = tab1.linkedBrowser;
      const secondBrowser = tab2.linkedBrowser;

      info("Focus the first panel's browser");
      Services.focus.setFocus(firstBrowser, Services.focus.FLAG_BYKEY);
      Assert.equal(
        document.activeElement,
        firstBrowser,
        "First panel browser has focus"
      );

      info("Tab into first panel content -> input");
      EventUtils.synthesizeKey("KEY_Tab");
      await assertContentActiveElement(
        firstBrowser,
        "input",
        "The input in tab1 content should be focused"
      );
      Assert.equal(
        document.activeElement,
        firstBrowser,
        "Chrome activeElement is still the first browser"
      );

      info("Tab from first panel content -> splitter");
      await expectFocusAfterKey(splitter, "KEY_Tab");

      info("Tab from splitter -> second panel content -> anchor");
      EventUtils.synthesizeKey("KEY_Tab");
      await assertContentActiveElement(
        secondBrowser,
        "a",
        "The anchor in tab2 content should be focused"
      );
      Assert.equal(
        document.activeElement,
        secondBrowser,
        "Chrome activeElement is the second browser"
      );

      info("Tab out of the splitview");
      EventUtils.synthesizeKey("KEY_Tab");
      await TestUtils.waitForCondition(() => {
        return document.activeElement !== secondBrowser;
      });

      info("Return focus to the 2nd browser");
      await expectFocusAfterKey(secondBrowser, "KEY_Tab", { shiftKey: true });

      // focus should land directly on the content focusable
      info("Check if the content activeElement is the anchor");
      await assertContentActiveElement(
        secondBrowser,
        "a",
        "The a in tab2 content should be focused"
      );
      info("Shift+tab to move focus back to the splitter");
      await expectFocusAfterKey(splitter, "KEY_Tab", { shiftKey: true });
      info("Shift+tab to move focus back to the 1st browser");
      await expectFocusAfterKey(firstBrowser, "KEY_Tab", { shiftKey: true });
      await assertContentActiveElement(
        firstBrowser,
        "input",
        "The input in tab1 content should be focused"
      );
    }
  );
  // Closing the splitview also closes the tabs
});
