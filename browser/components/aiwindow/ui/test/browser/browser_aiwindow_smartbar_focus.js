/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests Smartbar focus ring behavior, which matches the address bar:
 * - The ring shows on any focus of the input (auto-focus, script, mouse or
 *   keyboard) and stays while the user types
 * - The ring stays while the view is open, until the user arrows into it
 * - The classic urlbar focus border never paints next to the ring
 */

"use strict";

/**
 * @param {{style: string, width: string, color: string}} outline
 *   Computed outline properties.
 * @returns {boolean} Whether the outline paints something visible.
 */
function isVisibleOutline({ style, width, color }) {
  return (
    style != "none" &&
    parseFloat(width) >= 1 &&
    InspectorUtils.colorToRGBA(color)?.a > 0
  );
}

/**
 * Blurs the Smartbar, focuses it again and reports how it is painted.
 *
 * @param {MozBrowser} browser
 * @param {"script"|"mouse"|"keyboard"} how
 *   How to focus the Smartbar input.
 * @returns {Promise<{outline: object, border: string, blurredBorder: string, open: boolean}>}
 */
async function focusSmartbar(browser, how) {
  return SpecialPowers.spawn(browser, [how], async focusMethod => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    const background = smartbar.querySelector(".urlbar-background");
    const getBorder = () => {
      const style = content.getComputedStyle(background);
      return `${style.borderTopWidth} ${style.borderTopColor}`;
    };

    smartbar.inputField.blur();
    await ContentTaskUtils.waitForCondition(
      () => !smartbar.focused,
      "Wait for Smartbar to lose focus"
    );
    const blurredBorder = getBorder();

    switch (focusMethod) {
      case "script":
        smartbar.focus();
        break;
      case "mouse":
        EventUtils.synthesizeMouseAtCenter(smartbar.inputField, {}, content);
        await smartbar.lastQueryContextPromise;
        await ContentTaskUtils.waitForCondition(
          () =>
            !smartbar.hasAttribute("suppress-focus-border") ||
            smartbar.hasAttribute("open"),
          "Wait for the view to decide whether to open"
        );
        break;
      case "keyboard":
        smartbar
          .querySelector("context-icon-button")
          .shadowRoot.querySelector("moz-button")
          .focus();
        EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true }, content);
        break;
    }

    await ContentTaskUtils.waitForCondition(
      () => smartbar.focused,
      "Wait for Smartbar to be focused"
    );

    const style = content.getComputedStyle(background);
    return {
      outline: {
        style: style.outlineStyle,
        width: style.outlineWidth,
        color: style.outlineColor,
      },
      border: getBorder(),
      blurredBorder,
      open: smartbar.hasAttribute("open"),
    };
  });
}

/**
 * Waits for the Smartbar to be auto-focused and reports its outline.
 *
 * @param {MozBrowser} browser
 * @returns {Promise<object>} Computed outline properties of the bar.
 */
async function getAutoFocusOutline(browser) {
  return SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );
    await ContentTaskUtils.waitForCondition(
      () => smartbar.focused,
      "Wait for initial auto-focus"
    );
    const style = content.getComputedStyle(
      smartbar.querySelector(".urlbar-background")
    );
    return {
      style: style.outlineStyle,
      width: style.outlineWidth,
      color: style.outlineColor,
    };
  });
}

let browser;

add_setup(async function () {
  const win = await openAIWindow();
  browser = win.gBrowser.selectedBrowser;
  registerCleanupFunction(() => BrowserTestUtils.closeWindow(win));
});

/**
 * Puts the shared window's Smartbar back into an empty, focused state with no
 * open view and queries allowed.
 */
async function resetSmartbar() {
  await SpecialPowers.spawn(browser, [], async () => {
    const smartbar = content.document
      .querySelector("ai-window")
      .shadowRoot.querySelector("#ai-window-smartbar");
    smartbar.unsuppressStartQuery();
    smartbar.view.close();
    smartbar.inputField.blur();
    await ContentTaskUtils.waitForCondition(
      () => !smartbar.focused,
      "Wait for Smartbar to lose focus"
    );
    smartbar.focus();
    await ContentTaskUtils.waitForCondition(
      () => smartbar.focused,
      "Wait for Smartbar to be focused"
    );
    if (smartbar.value) {
      EventUtils.synthesizeKey("a", { accelKey: true }, content);
      EventUtils.synthesizeKey("KEY_Backspace", {}, content);
    }
    await ContentTaskUtils.waitForCondition(
      () =>
        !smartbar.value &&
        !smartbar.hasAttribute("usertyping") &&
        !smartbar.hasAttribute("open"),
      "Wait for an empty Smartbar with the view closed"
    );
  });
}

add_task(async function test_smartbar_ring_on_any_focus() {
  Assert.ok(
    isVisibleOutline(await getAutoFocusOutline(browser)),
    "Focus ring after auto-focus"
  );

  for (const how of ["script", "mouse", "keyboard"]) {
    const { outline, border, blurredBorder, open } = await focusSmartbar(
      browser,
      how
    );

    Assert.ok(!open, `Sanity check: the view is closed after ${how} focus`);
    Assert.ok(isVisibleOutline(outline), `Focus ring after ${how} focus`);
    Assert.equal(
      border,
      blurredBorder,
      `The urlbar focus border should not paint on ${how} focus`
    );
  }
});

add_task(async function test_smartbar_ring_hidden_when_tabbing_to_button() {
  await resetSmartbar();

  const { outline } = await focusSmartbar(browser, "keyboard");
  Assert.ok(
    isVisibleOutline(outline),
    "Sanity check: keyboard focus shows the ring"
  );

  const outlineOnButton = await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    await ContentTaskUtils.waitForCondition(
      () =>
        aiWindowElement.shadowRoot.activeElement ==
        smartbar.querySelector("context-icon-button"),
      "Wait for focus to move to the button"
    );
    const style = content.getComputedStyle(
      smartbar.querySelector(".urlbar-background")
    );
    return {
      style: style.outlineStyle,
      width: style.outlineWidth,
      color: style.outlineColor,
    };
  });

  Assert.ok(
    !isVisibleOutline(outlineOnButton),
    "The Smartbar ring should not show when a button inside it is focused"
  );
});

add_task(async function test_smartbar_ring_while_view_open_until_arrowing() {
  await resetSmartbar();

  await promiseSmartbarSuggestionsOpen(browser, () =>
    typeInSmartbar(browser, "aaaaaaaaaaaaaa")
  );

  const result = await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );
    const background = smartbar.querySelector(".urlbar-background");
    const state = () => {
      const style = content.getComputedStyle(background);
      return {
        bar: {
          style: style.outlineStyle,
          width: style.outlineWidth,
          color: style.outlineColor,
        },
        gradientBorder:
          content.getComputedStyle(background, "::after").display != "none",
        open: smartbar.hasAttribute("open"),
      };
    };
    const hasActiveDescendant = () =>
      smartbar.inputField.hasAttribute("aria-activedescendant");

    const whileTyping = state();

    EventUtils.synthesizeKey("KEY_ArrowDown", {}, content);
    await ContentTaskUtils.waitForCondition(
      hasActiveDescendant,
      "Wait for a result to get keyboard selection"
    );
    const afterArrowDown = state();

    EventUtils.sendString("b", content);
    await ContentTaskUtils.waitForCondition(
      () => !hasActiveDescendant(),
      "Wait for typing to move the selection back to the input"
    );
    const afterTypingAgain = state();

    smartbar.view.close();
    await ContentTaskUtils.waitForCondition(
      () => !smartbar.hasAttribute("open"),
      "Wait for the view to close"
    );
    return {
      whileTyping,
      afterArrowDown,
      afterTypingAgain,
      afterClose: state(),
      focused: smartbar.focused,
    };
  });

  Assert.ok(result.whileTyping.open, "Sanity check: the view is open");
  Assert.ok(
    isVisibleOutline(result.whileTyping.bar),
    "The ring stays while the user types with the view open"
  );
  Assert.ok(
    !result.whileTyping.gradientBorder,
    "The ring replaces the gradient border while the view is open"
  );
  Assert.ok(
    !isVisibleOutline(result.afterArrowDown.bar),
    "The ring goes away once the user arrows into the results"
  );
  Assert.ok(
    result.afterArrowDown.gradientBorder,
    "The gradient border shows while a result has keyboard selection"
  );
  Assert.ok(
    isVisibleOutline(result.afterTypingAgain.bar),
    "The ring comes back when the user types again"
  );
  Assert.ok(result.focused, "Sanity check: the input keeps focus");
  Assert.ok(
    isVisibleOutline(result.afterClose.bar),
    "The ring stays after the view closes"
  );
});

add_task(async function test_smartbar_ring_on_click() {
  await resetSmartbar();

  for (const suppressQueries of [false, true]) {
    const result = await SpecialPowers.spawn(
      browser,
      [suppressQueries],
      async suppress => {
        const aiWindowElement = content.document.querySelector("ai-window");
        const smartbar = aiWindowElement.shadowRoot.querySelector(
          "#ai-window-smartbar"
        );
        const background = smartbar.querySelector(".urlbar-background");
        const outlineOf = () => {
          const style = content.getComputedStyle(background);
          return {
            style: style.outlineStyle,
            width: style.outlineWidth,
            color: style.outlineColor,
          };
        };

        if (suppress) {
          smartbar.suppressStartQuery({ permanent: true });
        }
        smartbar.inputField.blur();
        await ContentTaskUtils.waitForCondition(
          () => !smartbar.focused,
          "Wait for Smartbar to lose focus"
        );

        EventUtils.synthesizeMouseAtCenter(smartbar.inputField, {}, content);
        const rightAfterClick = outlineOf();

        EventUtils.sendString("draft", content);
        await ContentTaskUtils.waitForCondition(
          () => smartbar.hasAttribute("usertyping"),
          "Wait for the typed text"
        );
        await smartbar.lastQueryContextPromise;
        return {
          rightAfterClick,
          whileTyping: outlineOf(),
          open: smartbar.hasAttribute("open"),
        };
      }
    );

    const label = suppressQueries ? "with queries suppressed" : "by default";
    Assert.ok(
      isVisibleOutline(result.rightAfterClick),
      `Focus ring right after a click ${label}`
    );
    Assert.ok(
      isVisibleOutline(result.whileTyping),
      `The ring stays while the user types ${label}`
    );
    if (suppressQueries) {
      Assert.ok(!result.open, "Sanity check: no view opens");
    }

    await resetSmartbar();
  }
});

// The accessibility theme means forced colors on Windows and an increased
// contrast preference elsewhere.
add_task(async function test_smartbar_contrast_indicator_on_any_focus() {
  await SpecialPowers.pushPrefEnv({ set: [["ui.useAccessibilityTheme", 1]] });
  await resetSmartbar();

  const { prefersContrast, forcedColors, focusOutlineColor } =
    await SpecialPowers.spawn(browser, [], async () => {
      const aiWindowElement = content.document.querySelector("ai-window");
      const smartbar = await ContentTaskUtils.waitForCondition(
        () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
        "Wait for Smartbar to be rendered"
      );
      const probe = content.document.createElement("div");
      probe.style.outlineColor = "var(--focus-outline-color)";
      smartbar.querySelector(".urlbar-background").append(probe);
      const color = content.getComputedStyle(probe).outlineColor;
      probe.remove();
      return {
        prefersContrast: content.matchMedia("(prefers-contrast)").matches,
        forcedColors: content.matchMedia("(forced-colors)").matches,
        focusOutlineColor: color,
      };
    });
  Assert.ok(prefersContrast, "Sanity check: a contrast mode is active");

  for (const how of ["script", "mouse", "keyboard"]) {
    const { outline, border, blurredBorder } = await focusSmartbar(
      browser,
      how
    );
    Assert.ok(
      isVisibleOutline(outline) || border != blurredBorder,
      `Focus indicator after ${how} focus`
    );
    if (!forcedColors) {
      Assert.equal(
        outline.color,
        focusOutlineColor,
        `The ring uses the focus outline color after ${how} focus`
      );
    }
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_smartbar_forced_colors_indicator_on_any_focus() {
  await resetSmartbar();
  browser.browsingContext.forcedColorsOverride = "active";

  try {
    const forcedColors = await SpecialPowers.spawn(
      browser,
      [],
      () => content.matchMedia("(forced-colors)").matches
    );
    Assert.ok(forcedColors, "Sanity check: forced colors are active");

    for (const how of ["script", "mouse", "keyboard"]) {
      const { outline, border, blurredBorder } = await focusSmartbar(
        browser,
        how
      );
      Assert.ok(
        isVisibleOutline(outline) || border != blurredBorder,
        `Forced-colors focus indicator after ${how} focus`
      );
    }
  } finally {
    browser.browsingContext.forcedColorsOverride = "none";
  }
});

// Bug 2028662: action-button shadow targets must keep the suggestions
// view open through mousedown (preventDefault to hold focus) and blur
// (don't close when focus moves into the button's shadow tree).
add_task(async function test_smartbar_action_buttons_keep_view_open() {
  await resetSmartbar();

  await promiseSmartbarSuggestionsOpen(browser, () =>
    typeInSmartbar(browser, "aaaaaaaaaaaaaa")
  );

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = aiWindowElement.shadowRoot.querySelector(
      "#ai-window-smartbar"
    );

    const buttonHosts = [
      smartbar.querySelector("context-icon-button"),
      smartbar.querySelector("memories-icon-button"),
      smartbar.querySelector("input-cta"),
    ];

    for (const host of buttonHosts) {
      Assert.ok(host, `${host?.localName ?? "host"} must exist`);
      const innerTarget = host.shadowRoot?.querySelector("moz-button") ?? host;

      const mousedown = new content.MouseEvent("mousedown", {
        bubbles: true,
        composed: true,
        cancelable: true,
      });
      Object.defineProperty(mousedown, "composedTarget", {
        value: innerTarget,
        writable: false,
      });
      host.dispatchEvent(mousedown);

      Assert.ok(
        mousedown.defaultPrevented,
        `mousedown on ${host.localName} should be preventDefault'd ` +
          "so focus stays on the input"
      );
      host.dispatchEvent(
        new content.MouseEvent("mouseup", { bubbles: true, composed: true })
      );

      const blur = new content.FocusEvent("blur", {
        bubbles: false,
        composed: true,
        relatedTarget: innerTarget,
      });
      smartbar.inputField.dispatchEvent(blur);

      Assert.ok(
        smartbar.view.isOpen,
        `View should stay open after blur into ${host.localName}'s shadow`
      );

      if (host.localName == "context-icon-button") {
        const innerMousedown = new content.MouseEvent("mousedown", {
          bubbles: true,
          composed: true,
          cancelable: true,
        });
        innerTarget.dispatchEvent(innerMousedown);
        Assert.ok(
          innerMousedown.defaultPrevented,
          "context-icon-button's mousedown handler should preventDefault"
        );
        innerTarget.dispatchEvent(
          new content.MouseEvent("mouseup", { bubbles: true, composed: true })
        );
      }
    }
  });
});

add_task(async function test_smartbar_button_container_click_no_focus() {
  await resetSmartbar();

  await SpecialPowers.spawn(browser, [], async () => {
    const aiWindowElement = content.document.querySelector("ai-window");
    const smartbar = await ContentTaskUtils.waitForCondition(
      () => aiWindowElement.shadowRoot?.querySelector("#ai-window-smartbar"),
      "Wait for Smartbar to be rendered"
    );

    // Ensure the smartbar is not focused initially
    smartbar.inputField.blur();
    await ContentTaskUtils.waitForCondition(
      () => !smartbar.focused,
      "Smartbar should not be focused"
    );

    // Find the smartbar button container
    const buttonContainer = smartbar.querySelector(
      ".smartbar-button-container"
    );

    if (!buttonContainer) {
      throw new Error("Smartbar button container not found");
    }

    // Store initial focus state
    const initialFocused = smartbar.focused;
    Assert.ok(!initialFocused, "Smartbar should not be initially focused");

    // Create mousedown event with proper composedTarget
    const mousedownEvent = new content.MouseEvent("mousedown", {
      bubbles: true,
      composed: true,
    });
    // Manually set composedTarget to match what the browser would set
    Object.defineProperty(mousedownEvent, "composedTarget", {
      value: buttonContainer,
      writable: false,
    });

    buttonContainer.dispatchEvent(mousedownEvent);
    buttonContainer.dispatchEvent(
      new content.MouseEvent("mouseup", { bubbles: true, composed: true })
    );

    // Wait a moment for any focus changes to occur
    await new Promise(resolve => content.setTimeout(resolve, 50));

    // Verify the smartbar is still not focused
    Assert.ok(
      !smartbar.focused,
      "Smartbar should remain unfocused after clicking button container"
    );
    Assert.equal(
      content.document.activeElement === smartbar.inputField,
      false,
      "Input field should not be the active element"
    );
  });
});

add_task(async function test_smartbar_autofocus_can_be_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.newtab.focusSmartbar", false]],
  });

  const win = browser.documentGlobal;
  const opened = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    AIWINDOW_URL,
    true
  );
  win.BrowserCommands.openTab();
  const tab = await opened;

  const smartbarFocused = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    async () => {
      const smartbar = await ContentTaskUtils.waitForCondition(
        () =>
          content.document
            .querySelector("ai-window")
            ?.shadowRoot?.querySelector("#ai-window-smartbar"),
        "Wait for Smartbar to be rendered"
      );
      return smartbar.focused;
    }
  );
  Assert.ok(!smartbarFocused, "Smartbar should not take focus on a new tab");
  Assert.ok(win.gURLBar.focused, "Address bar should retain focus");

  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
