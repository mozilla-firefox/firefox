/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests for <smartwindow-footer> component.
 */

async function openFooterInAIWindow() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.enabled", true]],
  });

  const win = await openAIWindow();
  const browser = win.gBrowser.selectedBrowser;

  return { win, browser };
}

add_task(async function test_smartwindow_footer_valid_actions() {
  const { win, browser } = await openFooterInAIWindow();

  try {
    await SpecialPowers.spawn(browser, [], async () => {
      const smartWindowElement = content.document.querySelector("ai-window");
      Assert.ok(smartWindowElement, "ai-window element should exist");

      const footer = await ContentTaskUtils.waitForCondition(
        () =>
          smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Wait for smartwindow-footer element"
      );
      Assert.ok(footer, "smartwindow-footer element should be found");

      await ContentTaskUtils.waitForCondition(
        () => footer.shadowRoot,
        "Footer should have shadowRoot"
      );

      const footerJS = footer.wrappedJSObject || footer;

      const topWin = content.browsingContext.topChromeWindow;

      // Save originals
      const hadHandler = !!topWin.FirefoxViewHandler;
      const originalHandler = topWin.FirefoxViewHandler;
      const originalOpenTab = topWin.FirefoxViewHandler?.openTab;

      const openTabCalls = [];

      // Install mock
      if (!topWin.FirefoxViewHandler) {
        topWin.FirefoxViewHandler = {};
      }
      topWin.FirefoxViewHandler.openTab = action => {
        openTabCalls.push(action);
      };

      try {
        footerJS.handleActionClick("history");
        Assert.equal(openTabCalls.length, 1, "openTab called once for history");
        Assert.equal(openTabCalls[0], "history", "openTab called with history");

        footerJS.handleActionClick("chats");
        Assert.equal(
          openTabCalls.length,
          2,
          "openTab called twice after chats"
        );
        Assert.equal(openTabCalls[1], "chats", "openTab called with chats");
      } finally {
        // Restore exactly what was there before
        if (hadHandler) {
          topWin.FirefoxViewHandler = originalHandler;
          if (originalOpenTab) {
            topWin.FirefoxViewHandler.openTab = originalOpenTab;
          }
        } else {
          delete topWin.FirefoxViewHandler;
        }
      }
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_smartwindow_footer_invalid_actions() {
  const { win, browser } = await openFooterInAIWindow();

  try {
    await SpecialPowers.spawn(browser, [], async () => {
      const smartWindowElement = content.document.querySelector("ai-window");
      Assert.ok(smartWindowElement, "ai-window element should exist");

      const footer = await ContentTaskUtils.waitForCondition(
        () =>
          smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Wait for smartwindow-footer element"
      );
      Assert.ok(footer, "smartwindow-footer element should be found");

      await ContentTaskUtils.waitForCondition(
        () => footer.shadowRoot,
        "Footer should have shadowRoot"
      );

      const footerJS = footer.wrappedJSObject || footer;
      const topWin = content.browsingContext.topChromeWindow;

      // Save originals
      const hadHandler = !!topWin.FirefoxViewHandler;
      const originalHandler = topWin.FirefoxViewHandler;
      const originalOpenTab = topWin.FirefoxViewHandler?.openTab;

      const originalWarn = content.console.warn;

      const openTabCalls = [];
      const warnCalls = [];

      // Install mocks
      if (!topWin.FirefoxViewHandler) {
        topWin.FirefoxViewHandler = {};
      }
      topWin.FirefoxViewHandler.openTab = action => {
        openTabCalls.push(action);
      };

      content.console.warn = (...args) => {
        warnCalls.push(args);
      };

      try {
        footerJS.handleActionClick("invalid-action");
        Assert.equal(openTabCalls.length, 0, "no openTab for invalid action");
        Assert.equal(
          warnCalls.length,
          1,
          "warn called once for invalid action"
        );
        Assert.equal(
          warnCalls[0][0],
          "Action not allowed",
          "warn message matches"
        );

        footerJS.handleActionClick("forbidden");
        Assert.equal(openTabCalls.length, 0, "still no openTab for forbidden");
        Assert.equal(warnCalls.length, 2, "warn called twice total");

        footerJS.handleActionClick("");
        Assert.equal(openTabCalls.length, 0, "no openTab for empty string");
        Assert.equal(warnCalls.length, 3, "warn called three times total");

        footerJS.handleActionClick(null);
        Assert.equal(openTabCalls.length, 0, "no openTab for null");
        Assert.equal(warnCalls.length, 4, "warn called four times total");
      } finally {
        // Restore console.warn
        content.console.warn = originalWarn;

        // Restore FirefoxViewHandler / openTab
        if (hadHandler) {
          topWin.FirefoxViewHandler = originalHandler;
          if (originalOpenTab) {
            topWin.FirefoxViewHandler.openTab = originalOpenTab;
          }
        } else {
          delete topWin.FirefoxViewHandler;
        }
      }
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_smartwindow_footer_visibility() {
  const { win, browser } = await openFooterInAIWindow();

  try {
    await SpecialPowers.spawn(browser, [], async () => {
      const smartWindowElement = content.document.querySelector("ai-window");
      Assert.ok(smartWindowElement, "ai-window element should exist");

      // Test that footer visibility is controlled by parent
      const footer = await ContentTaskUtils.waitForCondition(
        () =>
          smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Wait for smartwindow-footer element"
      );

      // Initially should be visible in full page mode
      Assert.ok(footer, "Footer should exist in full page mode");

      // Verify footer is removed when showFooter is false
      smartWindowElement.showFooter = false;
      await ContentTaskUtils.waitForCondition(
        () =>
          !smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Footer should be removed when showFooter is false"
      );

      // Verify footer reappears when showFooter is true
      smartWindowElement.showFooter = true;
      await ContentTaskUtils.waitForCondition(
        () =>
          smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Footer should reappear when showFooter is true"
      );
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_smartwindow_footer_button_clicks() {
  const { win, browser } = await openFooterInAIWindow();

  try {
    await SpecialPowers.spawn(browser, [], async () => {
      const smartWindowElement = content.document.querySelector("ai-window");
      Assert.ok(smartWindowElement, "ai-window element should exist");

      const footer = await ContentTaskUtils.waitForCondition(
        () =>
          smartWindowElement.shadowRoot?.querySelector("smartwindow-footer"),
        "Wait for smartwindow-footer element"
      );
      Assert.ok(footer, "smartwindow-footer element should be found");

      await ContentTaskUtils.waitForCondition(
        () => footer.shadowRoot,
        "Footer should have shadowRoot"
      );

      await ContentTaskUtils.waitForCondition(() => {
        const buttons = footer.shadowRoot?.querySelectorAll("moz-button");
        return buttons && buttons.length === 2;
      }, "Footer should render two moz-button elements");

      const topWin = content.browsingContext.topChromeWindow;

      // Save originals
      const hadHandler = !!topWin.FirefoxViewHandler;
      const originalHandler = topWin.FirefoxViewHandler;
      const originalOpenTab = topWin.FirefoxViewHandler?.openTab;

      const openTabCalls = [];

      // Install mock
      if (!topWin.FirefoxViewHandler) {
        topWin.FirefoxViewHandler = {};
      }
      topWin.FirefoxViewHandler.openTab = action => {
        openTabCalls.push(action);
      };

      try {
        const buttons = footer.shadowRoot.querySelectorAll("moz-button");
        Assert.equal(buttons.length, 2, "Should have exactly 2 buttons");

        const historyButton = Array.from(buttons).find(
          btn =>
            btn.getAttribute("data-l10n-id") === "smartwindow-footer-history"
        );
        const chatsButton = Array.from(buttons).find(
          btn => btn.getAttribute("data-l10n-id") === "smartwindow-footer-chats"
        );

        Assert.ok(historyButton, "Should find history button");
        Assert.ok(chatsButton, "Should find chats button");

        historyButton.click();
        await ContentTaskUtils.waitForCondition(
          () => openTabCalls.length === 1,
          "openTab should be called after clicking history"
        );
        Assert.equal(openTabCalls[0], "history", "history click opens history");

        chatsButton.click();
        await ContentTaskUtils.waitForCondition(
          () => openTabCalls.length === 2,
          "openTab should be called after clicking chats"
        );
        Assert.equal(openTabCalls[1], "chats", "chats click opens chats");
      } finally {
        // Restore FirefoxViewHandler / openTab
        if (hadHandler) {
          topWin.FirefoxViewHandler = originalHandler;
          if (originalOpenTab) {
            topWin.FirefoxViewHandler.openTab = originalOpenTab;
          }
        } else {
          delete topWin.FirefoxViewHandler;
        }
      }
    });
  } finally {
    await BrowserTestUtils.closeWindow(win);
    await SpecialPowers.popPrefEnv();
  }
});

add_task(async function test_smartwindow_promo_fullpage_layout() {
  let win;
  try {
    win = await openAIWindow();
    const browser = win.gBrowser.selectedBrowser;
    const aiWindow = browser.contentDocument.querySelector("ai-window");
    Assert.ok(aiWindow, "ai-window element should exist");

    aiWindow.promoMessage = {
      heading: "Test",
      message: "Test body",
      primaryActionText: "Go",
      secondaryActionText: "Not now",
    };
    await aiWindow.updateComplete;

    const promo = aiWindow.shadowRoot.querySelector("smartwindow-promo");
    const footer = aiWindow.shadowRoot.querySelector("smartwindow-footer");
    const resumeSection = aiWindow.shadowRoot.querySelector(
      "smartwindow-resume-section"
    );
    Assert.ok(promo, "Promo should render");
    Assert.ok(footer, "Footer should still render alongside the promo");
    Assert.ok(resumeSection, "Resume section should be present");

    const children = [...aiWindow.shadowRoot.children];
    Assert.less(
      children.indexOf(promo),
      children.indexOf(resumeSection),
      "Promo should appear before the resume section"
    );

    aiWindow.showFooter = false;
    await aiWindow.updateComplete;

    Assert.ok(
      !aiWindow.shadowRoot.querySelector("smartwindow-footer"),
      "Footer should be removed when showFooter is false"
    );
    Assert.ok(
      aiWindow.shadowRoot.querySelector("smartwindow-promo"),
      "Promo should remain when showFooter is false"
    );

    aiWindow.mode = "sidebar";
    await aiWindow.updateComplete;

    Assert.ok(
      !aiWindow.shadowRoot.querySelector("smartwindow-promo"),
      "Promo should not render in sidebar mode"
    );
  } finally {
    if (win) {
      await BrowserTestUtils.closeWindow(win);
    }
  }
});
