/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check that Escape closes an open dropdown first and then the bar, whether
 * the focus is in the page or in the browser window.
 */
add_task(async function test_escape_closes_dropdown_then_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    let panel = await showSelectionMenu(browser);
    let popup = await openAskDropdown(browser);

    let dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
    await dropdownHidden;
    await SpecialPowers.spawn(browser, [], () => {});
    Assert.equal(panel.state, "open", "The first Escape leaves the bar open");

    let barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, browser);
    await barHidden;

    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    panel = await showSelectionMenu(browser);
    popup = await openAskDropdown(browser);
    dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await dropdownHidden;
    Assert.equal(
      panel.state,
      "open",
      "The first Escape leaves the bar open from the window"
    );
    barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    EventUtils.synthesizeKey("KEY_Escape", {}, window);
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that Escape closes an open dropdown first when the selection is in a
 * cross-origin iframe, so the dropdown state reaches that frame's actor.
 */
add_task(async function test_escape_closes_dropdown_then_bar_in_iframe() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("https://example.com/", async browser => {
    await SpecialPowers.spawn(browser, ["https://example.org/"], async src => {
      const frame = content.document.createElement("iframe");
      frame.src = src;
      const loaded = ContentTaskUtils.waitForEvent(frame, "load");
      content.document.body.appendChild(frame);
      await loaded;
    });
    const frameContext = browser.browsingContext.children[0];
    await SimpleTest.promiseFocus(browser);
    await BrowserTestUtils.synthesizeMouse(
      "body",
      10,
      10,
      { type: "mousedown" },
      frameContext
    );
    await SpecialPowers.spawn(frameContext, [], () => {
      content.getSelection().selectAllChildren(content.document.body);
    });
    await BrowserTestUtils.synthesizeMouse(
      "body",
      60,
      10,
      { type: "mouseup" },
      frameContext
    );
    const panel = document.getElementById("selection-shortcut-action-panel");
    await TestUtils.waitForCondition(
      () => panel.getAttribute("panelopen") === "true",
      "Selection menu opened for the frame selection"
    );

    const popup = await openAskDropdown(frameContext);

    const dropdownHidden = BrowserTestUtils.waitForEvent(popup, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, frameContext);
    await dropdownHidden;
    await SpecialPowers.spawn(frameContext, [], () => {});
    Assert.equal(panel.state, "open", "The first Escape leaves the bar open");

    const barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeKey("KEY_Escape", {}, frameContext);
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that the later presses of a double click do not take the bar down.
 */
add_task(async function test_double_click_keeps_bar() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    let hiding = 0;
    const onHiding = event => {
      if (event.target == panel) {
        hiding++;
      }
    };
    panel.addEventListener("popuphiding", onHiding);

    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mousedown", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () => {});
    Assert.equal(panel.state, "open", "The second press keeps the bar open");
    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mouseup", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () => {});
    panel.removeEventListener("popuphiding", onHiding);
    Assert.equal(hiding, 0, "The bar never closed during the double click");

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check that a double click that ends with a different selection and nothing
 * new to show takes the bar down, rather than leaving it for stale text.
 */
add_task(async function test_click_burst_ending_without_selection_hides_bar() {
  await SpecialPowers.pushPrefEnv({
    set: [...MORE_ACTIONS_PREFS, ["browser.ml.chat.shortcuts.debounce", 50]],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);

    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mousedown", clickCount: 2 },
      browser
    );
    await SpecialPowers.spawn(browser, [], () =>
      content.getSelection().removeAllRanges()
    );
    Assert.equal(panel.state, "open", "The bar stays open during the burst");

    const barHidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
    await BrowserTestUtils.synthesizeMouse(
      browser,
      20,
      10,
      { type: "mouseup", clickCount: 2 },
      browser
    );
    await barHidden;
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the size of the strip, its buttons and the Copied pill.
 */
add_task(async function test_strip_sizes() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const rect = id => document.getElementById(id).getBoundingClientRect();

    Assert.equal(contentRect(panel).height, 42, "The strip is 42px tall");
    for (const id of [
      "search-action-button",
      "copy-action-button",
      "more-actions-button",
    ]) {
      Assert.deepEqual(
        [rect(id).width, rect(id).height],
        [36, 36],
        `${id} is a 36px circle`
      );
    }
    Assert.less(
      Math.abs(rect("ai-action-button").width - 51),
      3.5,
      "The AI action is about a 51px pill, depending on the font size"
    );
    Assert.equal(
      rect("ai-action-button").height,
      36,
      "The AI action is 36px tall"
    );
    const aiButtonPart = document
      .getElementById("ai-action-button")
      .shadowRoot.querySelector("[part=button]");
    Assert.equal(
      getComputedStyle(aiButtonPart, "::after").width,
      getComputedStyle(aiButtonPart, "::after").height,
      "The arrow is not squeezed narrower than its height"
    );

    const confirmation = document.getElementById("copy-action-confirmation");
    EventUtils.synthesizeMouseAtCenter(
      document.getElementById("copy-action-button"),
      {},
      window
    );
    await TestUtils.waitForCondition(
      () => confirmation.hasAttribute("copied"),
      "Confirmation is shown"
    );
    Assert.equal(
      confirmation.getBoundingClientRect().height,
      36,
      "The Copied pill is as tall as the buttons"
    );

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the size of the Ask AI dropdown.
 */
add_task(async function test_ask_dropdown_sizes() {
  await SpecialPowers.pushPrefEnv({ set: MORE_ACTIONS_PREFS });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    const panel = await showSelectionMenu(browser);
    const popup = await openAskDropdown(browser);

    const rows = popup.querySelectorAll("toolbarbutton");
    Assert.greater(rows.length, 0, "The dropdown has prompts");
    for (const row of rows) {
      Assert.equal(row.getBoundingClientRect().height, 32, "Rows are 32px");
    }
    Assert.greaterOrEqual(
      contentRect(popup).width,
      240,
      "The dropdown is at least 240px wide"
    );

    await hideSelectionMenu(panel);
  });

  await SpecialPowers.popPrefEnv();
});

/**
 * Check the placeholder of the Ask AI dropdown for a provider with no name.
 */
add_task(async function test_ask_dropdown_placeholder_without_provider_name() {
  for (const [gate, expected] of [
    [true, "Ask AI…"],
    [false, "Ask AI chatbot…"],
  ]) {
    await SpecialPowers.pushPrefEnv({
      set: [
        ...MORE_ACTIONS_PREFS,
        ["browser.ml.chat.provider", "https://unknown.example"],
        ["browser.highlightToSearch.featureGate", gate],
      ],
    });

    await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
      const panel = await showSelectionMenu(browser);
      const popup = await openAskDropdown(browser);
      const textarea = popup.querySelector("textarea");
      await document.l10n.translateElements([textarea]);
      Assert.equal(
        textarea.placeholder,
        expected,
        `The placeholder reads "${expected}" with the gate ${gate ? "on" : "off"}`
      );
      await hideSelectionMenu(panel);
    });

    await SpecialPowers.popPrefEnv();
  }
});
