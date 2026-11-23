/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * The maximum allowed difference between measured widths in CSS pixels.
 *
 * @type {number}
 */
const WIDTH_TOLERANCE_PX = 1;

add_setup(() =>
  SpecialPowers.pushPrefEnv({
    set: [["ui.prefersReducedMotion", 1]],
  })
);

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("browser.tabs.splitview.hasUsed");
});

/**
 * This test case ensures that the initial width survives tab switches and moves,
 * and that the splitter can resize the panel afterward.
 */
add_task(async function test_initial_first_panel_width() {
  const originalTab = gBrowser.selectedTab;
  const firstTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const secondTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tabs = [firstTab, secondTab];
  let splitView = null;

  try {
    const pageURL =
      getRootDirectory(gTestPath).replace(
        "chrome://mochitests/content",
        "https://example.com"
      ) + "dummy_page.html";
    await Promise.all(
      tabs.map(tab => {
        const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
          wantLoad: pageURL,
        });
        BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, pageURL);

        return loaded;
      })
    );

    const splitViewCreated = BrowserTestUtils.waitForEvent(
      gBrowser.tabContainer,
      "SplitViewCreated"
    );
    splitView = gBrowser.addTabSplitView(tabs, {
      firstPanelInitialWidthPercent: 33,
    });
    await splitViewCreated;
    const firstPanel = gBrowser.getPanel(firstTab.linkedBrowser);

    Assert.ok(splitView, "An inactive split view was created");
    Assert.equal(
      gBrowser.selectedTab,
      originalTab,
      "Creating the split view did not select either of its tabs"
    );

    const splitterReady = waitForActiveSplitViewSplitter();
    const initialSwitchDone = BrowserTestUtils.waitForEvent(
      gBrowser,
      "TabSwitchDone"
    );
    const browsersReady = Promise.all(
      tabs.map(({ linkedBrowser }) =>
        !linkedBrowser.isRemoteBrowser || linkedBrowser.hasLayers
          ? Promise.resolve()
          : BrowserTestUtils.waitForEvent(linkedBrowser, "MozLayerTreeReady")
      )
    );
    await BrowserTestUtils.switchTab(gBrowser, firstTab);
    await Promise.all([initialSwitchDone, browsersReady]);
    const splitter = await splitterReady;
    await assertSplitViewPanelWidth(firstPanel, 33);

    const initialPixelWidth = String(
      Math.round((gBrowser.tabpanels.clientWidth * 33) / 100)
    );
    Assert.equal(
      firstPanel.getAttribute("width"),
      initialPixelWidth,
      "The initial width is stored in pixels"
    );
    Assert.equal(
      firstPanel.style.width,
      `${initialPixelWidth}px`,
      "The initial CSS width is set in pixels"
    );
    Assert.lessOrEqual(
      Math.abs(
        Number(splitter.getAttribute("aria-valuenow")) - firstPanel.clientWidth
      ),
      WIDTH_TOLERANCE_PX,
      "The splitter reports the first panel's rendered width"
    );

    info("Switching away from Split View and awaiting tab-switch completion");
    const switchAwayDone = BrowserTestUtils.waitForEvent(
      gBrowser,
      "TabSwitchDone"
    );
    await BrowserTestUtils.switchTab(gBrowser, originalTab);
    await switchAwayDone;

    info("Switching back to Split View and checking its preserved width");
    await BrowserTestUtils.switchTab(gBrowser, firstTab);
    await assertSplitViewPanelWidth(firstPanel, 33);

    const movedBefore = BrowserTestUtils.waitForEvent(firstTab, "TabMove");
    gBrowser.moveTabTo(firstTab, { tabIndex: originalTab.index });
    await movedBefore;
    await assertSplitViewPanelWidth(firstPanel, 33);
    Assert.less(
      firstTab.index,
      originalTab.index,
      "The Split View moved before the original tab"
    );
    Assert.equal(
      firstPanel.getAttribute("width"),
      initialPixelWidth,
      "Moving the Split View preserves the initial pixel width attribute"
    );
    Assert.equal(
      firstPanel.style.width,
      `${initialPixelWidth}px`,
      "Moving the Split View preserves the initial pixel CSS width"
    );

    const widthBeforeResize = firstPanel.getBoundingClientRect().width;
    const splitterValueBeforeResize = splitter.getAttribute("aria-valuenow");
    const commandPromise = BrowserTestUtils.waitForEvent(splitter, "command");
    const splitterValueChanged = BrowserTestUtils.waitForMutationCondition(
      splitter,
      { attributes: true, attributeFilter: ["aria-valuenow"] },
      () => splitter.getAttribute("aria-valuenow") != splitterValueBeforeResize
    );
    Services.focus.setFocus(splitter, Services.focus.FLAG_BYKEY);
    EventUtils.synthesizeKey("KEY_ArrowRight");
    await Promise.all([commandPromise, splitterValueChanged]);

    Assert.greater(
      firstPanel.getBoundingClientRect().width,
      widthBeforeResize,
      "The native splitter can resize a panel initialized with a percentage"
    );
    const resizedWidth = firstPanel.getBoundingClientRect().width;
    const storedPixelWidth = firstPanel.getAttribute("width");
    Assert.ok(
      firstPanel.hasAttribute("width") &&
        Number.isFinite(Number(storedPixelWidth)),
      "The native splitter stores the resized panel width in pixels"
    );
    Assert.equal(
      firstPanel.style.width,
      `${storedPixelWidth}px`,
      "The native splitter stores the resized CSS width in pixels"
    );

    const movedAfter = BrowserTestUtils.waitForEvent(firstTab, "TabMove");
    gBrowser.moveTabTo(firstTab, { tabIndex: originalTab.index });
    await movedAfter;
    const restoredWidth = await window.promiseDocumentFlushed(
      () => firstPanel.getBoundingClientRect().width
    );
    Assert.lessOrEqual(
      Math.abs(restoredWidth - resizedWidth),
      WIDTH_TOLERANCE_PX,
      "Moving the Split View restores its rendered pixel width"
    );
    Assert.greater(
      firstTab.index,
      originalTab.index,
      "The Split View moved after the original tab"
    );
    Assert.equal(
      firstPanel.getAttribute("width"),
      storedPixelWidth,
      "Moving the Split View preserves the pixel width attribute"
    );
    Assert.equal(
      firstPanel.style.width,
      `${storedPixelWidth}px`,
      "Moving the Split View preserves the pixel CSS width"
    );
  } finally {
    if (splitView) {
      splitView.close();
    } else {
      BrowserTestUtils.removeTab(firstTab);
      BrowserTestUtils.removeTab(secondTab);
    }
  }
});

/**
 * This test case ensures that requests outside the supported percentage range
 * open the first panel at the nearest supported width.
 */
add_task(async function test_initial_width_is_clamped() {
  for (const [requestedPercent, expectedPercent] of [
    [0, 20],
    [100, 80],
  ]) {
    const firstTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    const secondTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
    const tabs = [firstTab, secondTab];
    let splitView = null;

    try {
      await BrowserTestUtils.switchTab(gBrowser, firstTab);
      const splitterReady = waitForActiveSplitViewSplitter();
      const splitViewCreated = BrowserTestUtils.waitForEvent(
        gBrowser.tabContainer,
        "SplitViewCreated"
      );
      splitView = gBrowser.addTabSplitView(tabs, {
        firstPanelInitialWidthPercent: requestedPercent,
      });
      await Promise.all([splitViewCreated, splitterReady]);

      const firstPanel = gBrowser.getPanel(firstTab.linkedBrowser);
      await assertSplitViewPanelWidth(firstPanel, expectedPercent);
      Assert.equal(
        firstPanel.getAttribute("width"),
        String(
          Math.round((gBrowser.tabpanels.clientWidth * expectedPercent) / 100)
        ),
        `${requestedPercent}% is clamped to ${expectedPercent}% before conversion`
      );
    } finally {
      if (splitView) {
        splitView.close();
      } else {
        BrowserTestUtils.removeTab(firstTab);
        BrowserTestUtils.removeTab(secondTab);
      }
    }
  }
});

/**
 * This test case ensures that an initial width is applied when a lazily
 * created first panel becomes available on activation.
 */
add_task(async function test_initial_width_with_lazy_tab() {
  const firstTab = gBrowser.addTrustedTab("about:blank", {
    createLazyBrowser: true,
  });
  const secondTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  let splitView = null;

  try {
    Assert.ok(!firstTab.linkedPanel, "The first tab has no panel yet");
    splitView = gBrowser.addTabSplitView([firstTab, secondTab], {
      firstPanelInitialWidthPercent: 33,
    });
    Assert.ok(!firstTab.linkedPanel, "The inactive split leaves the tab lazy");

    await BrowserTestUtils.switchTab(gBrowser, secondTab);
    const firstPanel = gBrowser.getPanel(firstTab.linkedBrowser);
    await assertSplitViewPanelWidth(firstPanel, 33);
    Assert.equal(
      firstPanel.getAttribute("width"),
      String(Math.round((gBrowser.tabpanels.clientWidth * 33) / 100)),
      "The formerly lazy panel stores its initial width in pixels"
    );
  } finally {
    if (splitView) {
      splitView.close();
    } else {
      BrowserTestUtils.removeTab(firstTab);
      BrowserTestUtils.removeTab(secondTab);
    }
  }
});

/**
 * This test case ensures that invalid width requests leave both tabs outside
 * Split View.
 */
add_task(async function test_invalid_initial_width() {
  const firstTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const secondTab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  const tabs = [firstTab, secondTab];

  try {
    for (const percent of [NaN, Infinity, "33"]) {
      Assert.throws(
        () =>
          gBrowser.addTabSplitView(tabs, {
            firstPanelInitialWidthPercent: percent,
          }),
        /firstPanelInitialWidthPercent must be a finite number/,
        `${String(percent)} is rejected as a non-finite number`
      );
    }
    Assert.ok(!firstTab.splitview, "The first tab was not moved");
    Assert.ok(!secondTab.splitview, "The second tab was not moved");
  } finally {
    BrowserTestUtils.removeTab(firstTab);
    BrowserTestUtils.removeTab(secondTab);
  }
});
