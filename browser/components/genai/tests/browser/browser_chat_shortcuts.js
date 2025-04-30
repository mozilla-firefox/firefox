/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { GenAI } = ChromeUtils.importESModule(
  "resource:///modules/GenAI.sys.mjs"
);
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

/**
 * Check that shortcuts aren't shown by default
 */
add_task(async function test_no_shortcuts() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.provider", ""]],
  });
  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    browser.focus();
    goDoCommand("cmd_selectAll");

    const selectionShortcutActionPanel = document.getElementById(
      "selection-shortcut-action-panel"
    );

    Assert.ok(
      !selectionShortcutActionPanel.hasAttribute("panelopen"),
      "No shortcuts found"
    );
  });
});

/**
 * Check that shortcuts get shown on selection and open popup and sidebar
 */
add_task(async function test_show_shortcuts() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts", true],
      ["browser.ml.chat.provider", "http://localhost:8080"],
    ],
  });

  await BrowserTestUtils.withNewTab("data:text/plain,hi", async browser => {
    await SimpleTest.promiseFocus(browser);
    const selectPromise = SpecialPowers.spawn(browser, [], () => {
      ContentTaskUtils.waitForCondition(() => content.getSelection());
    });
    goDoCommand("cmd_selectAll");
    await selectPromise;
    BrowserTestUtils.synthesizeMouseAtCenter(
      browser,
      { type: "mouseup" },
      browser
    );

    await TestUtils.waitForCondition(() => {
      const panelElement = document.getElementById(
        "selection-shortcut-action-panel"
      );
      return panelElement.getAttribute("panelopen") === "true";
    });

    const shortcuts = document.querySelector("#ai-action-button");
    Assert.ok(shortcuts, "Shortcuts added on select");
    let events = Glean.genaiChatbot.shortcutsDisplayed.testGetValue();
    Assert.equal(events.length, 1, "Shortcuts shown once");
    Assert.ok(events[0].extra.delay, "Waited some time");
    Assert.equal(events[0].extra.inputType, "", "Not in input");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");

    const popup = document.getElementById("chat-shortcuts-options-panel");
    Assert.equal(popup.state, "closed", "Popup is closed");

    EventUtils.sendMouseEvent({ type: "mouseover" }, shortcuts);
    await BrowserTestUtils.waitForEvent(popup, "popupshown");

    Assert.equal(popup.state, "open", "Popup is open");
    events = Glean.genaiChatbot.shortcutsExpanded.testGetValue();
    Assert.equal(events.length, 1, "One shortcuts opened");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");
    Assert.equal(
      events[0].extra.warning,
      "false",
      "Warning lable value is correct"
    );

    const custom = popup.querySelector("textarea");
    Assert.ok(custom, "Got custom prompt entry");
    Assert.ok(custom.style.height, "Dynamic height for custom");

    Assert.ok(!SidebarController.isOpen, "Sidebar is closed");
    popup.querySelector("toolbarbutton").click();
    const isOpen = await TestUtils.waitForCondition(
      () => SidebarController.isOpen
    );

    Assert.ok(isOpen, "Chat opened sidebar");
    events = Glean.genaiChatbot.shortcutsPromptClick.testGetValue();
    Assert.equal(events.length, 1, "One shortcut clicked");
    Assert.equal(events[0].extra.prompt, "summarize", "Picked summarize");
    Assert.equal(events[0].extra.provider, "localhost", "With localhost");
    Assert.equal(events[0].extra.selection, 2, "Selected hi");

    SidebarController.hide();
  });
});

/**
 * Check that shortcuts shown on second tab
 */
add_task(async function test_show_shortcuts_second_tab() {
  // NB: this test runs after test_show_shortcuts, which showed shortcuts
  await BrowserTestUtils.withNewTab(
    "data:text/html,<title>second</title>page",
    async browser => {
      await SimpleTest.promiseFocus(browser);
      const selectPromise = SpecialPowers.spawn(browser, [], () => {
        ContentTaskUtils.waitForCondition(() => content.getSelection());
      });
      goDoCommand("cmd_selectAll");
      await selectPromise;
      BrowserTestUtils.synthesizeMouseAtCenter(
        browser,
        { type: "mouseup" },
        browser
      );

      await TestUtils.waitForCondition(() => {
        const panelElement = document.getElementById(
          "selection-shortcut-action-panel"
        );
        return panelElement.getAttribute("panelopen") === "true";
      });
      const sandbox = sinon.createSandbox();
      const stub = sandbox.stub(GenAI, "addAskChatItems");

      const shortcuts = document.querySelector("#ai-action-button");
      EventUtils.sendMouseEvent({ type: "mouseover" }, shortcuts);

      Assert.equal(stub.callCount, 1, "Shortcuts added on select");
      Assert.equal(stub.firstCall.args[0], browser, "Got correct browser");

      sandbox.restore();
    }
  );
});

/**
 * Check that the warning label is shown when too much text is selected
 */
add_task(async function test_show_warning_label() {
  Services.fog.testResetFOG();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts", true]],
  });
  await BrowserTestUtils.withNewTab(
    "data:text/plain,hi".repeat(1000),
    async browser => {
      await SimpleTest.promiseFocus(browser);
      const selectPromise = SpecialPowers.spawn(browser, [], () => {
        ContentTaskUtils.waitForCondition(() => content.getSelection());
      });
      goDoCommand("cmd_selectAll");
      await selectPromise;
      BrowserTestUtils.synthesizeMouseAtCenter(
        browser,
        { type: "mouseup" },
        browser
      );

      const selectionShortcutActionPanel = document.getElementById(
        "selection-shortcut-action-panel"
      );

      // Wait for shortcut actions panel to open
      await BrowserTestUtils.waitForEvent(
        selectionShortcutActionPanel,
        "popupshown"
      );

      Assert.ok(selectionShortcutActionPanel, "Shortcuts panel shown");
      const aiActionButton =
        selectionShortcutActionPanel.querySelector("#ai-action-button");

      let events = Glean.genaiChatbot.shortcutsDisplayed.testGetValue();
      Assert.equal(events.length, 1, "Shortcuts shown once");
      Assert.ok(events[0].extra.delay, "Waited some time");
      Assert.equal(events[0].extra.inputType, "", "Not in input");
      Assert.ok(events[0].extra.selection > 8192, "Selected enough text");
      // Hover button
      EventUtils.sendMouseEvent({ type: "mouseover" }, aiActionButton);

      const chatShortcutsOptionsPanel = document.getElementById(
        "chat-shortcuts-options-panel"
      );

      // Wait for wait for shortcut options panel to open
      await BrowserTestUtils.waitForEvent(
        chatShortcutsOptionsPanel,
        "popupshown"
      );

      Assert.ok(
        chatShortcutsOptionsPanel.querySelector(".ask-chat-shortcut-warning"),
        "Warning label shown"
      );

      events = Glean.genaiChatbot.shortcutsExpanded.testGetValue();
      Assert.equal(
        events[0].extra.warning,
        "true",
        "Warning lable value is correct"
      );

      Assert.ok(
        !aiActionButton.getAttribute("type").includes("ghost"),
        "button is active"
      );

      const hidden = BrowserTestUtils.waitForEvent(
        chatShortcutsOptionsPanel,
        "popuphidden"
      );
      chatShortcutsOptionsPanel.hidePopup();
      await hidden;

      Assert.ok(
        aiActionButton.getAttribute("type").includes("ghost"),
        "button is inactive"
      );
    }
  );
});

/**
 * Check that only plain clicks would show shortcuts
 */
add_task(async function test_plain_clicks() {
  // Set longpress to 0 to prevent delay condition from failing
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ml.chat.shortcuts.longPress", 0]],
  });

  const sandbox = sinon.createSandbox();
  const stub = sandbox
    .stub(GenAI, "handleShortcutsMessage")
    .withArgs("GenAI:ShowShortcuts");

  await BrowserTestUtils.withNewTab("data:text/plain,click", async browser => {
    await BrowserTestUtils.synthesizeMouseAtCenter(
      browser,
      { type: "mouseup" },
      browser
    );

    await TestUtils.waitForCondition(() => stub.callCount !== 0);

    Assert.equal(stub.callCount, 1, "Plain click handled");

    await BrowserTestUtils.synthesizeMouseAtCenter(
      browser,
      { button: 1, type: "mouseup" },
      browser
    );

    Assert.equal(stub.callCount, 1, "Middle click ignored");

    await BrowserTestUtils.synthesizeMouseAtCenter(
      browser,
      { shiftKey: true, type: "mouseup" },
      browser
    );

    Assert.equal(stub.callCount, 1, "Modified click ignored");
  });

  await SpecialPowers.popPrefEnv();

  sandbox.restore();
});

/**
 * Check that input selection can show shortcuts
 */
add_task(async function test_input_selection() {
  Assert.equal(GenAI.ignoredInputs.size, 1, "Default ignore 1 type of field");
  Assert.ok(GenAI.ignoredInputs.has("input"), "Default ignore inputs");
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ml.chat.shortcuts.ignoreFields", "contenteditable,textarea"],
    ],
  });
  Assert.equal(GenAI.ignoredInputs.size, 2, "Ignoring other fields not input");
  Assert.ok(GenAI.ignoredInputs.has("textarea"), "Now ignore textarea");
  Assert.ok(!GenAI.ignoredInputs.has("input"), "Not ignoring input for test");

  const sandbox = sinon.createSandbox();
  const stub = sandbox.stub(GenAI, "handleShortcutsMessage");

  await BrowserTestUtils.withNewTab(
    `data:text/html,<input value="input"/>`,
    async browser => {
      await SpecialPowers.spawn(browser, [], () => {
        const input = content.document.querySelector("input");
        input.focus();
        input.selectionEnd = 3;
      });

      await BrowserTestUtils.synthesizeMouseAtCenter(
        browser,
        { type: "mouseup" },
        browser
      );
      await TestUtils.waitForCondition(() => stub.callCount !== 0);

      Assert.equal(stub.callCount, 1, "Show shortcuts once");
      Assert.notEqual(
        stub.firstCall.args[0],
        "GenAI:HideShortcuts",
        "No unnecessary hides"
      );
      Assert.equal(
        stub.firstCall.args[1].selection,
        "inp",
        "Got selected text from input"
      );
      Assert.equal(stub.firstCall.args[1].inputType, "input", "Got input type");
    }
  );

  sandbox.restore();
});
