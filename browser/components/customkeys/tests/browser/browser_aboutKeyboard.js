/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CustomKeys } = ChromeUtils.importESModule(
  "moz-src:///browser/components/customkeys/CustomKeys.sys.mjs"
);
const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

/**
 * Test the about:keyboard UI.
 */

registerCleanupFunction(async function () {
  CustomKeys.resetAll();
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
});

function addAboutKbTask(task) {
  const wrapped = function () {
    return BrowserTestUtils.withNewTab("about:keyboard", async tab => {
      await SpecialPowers.spawn(tab, [], async () => {
        if (!content.document.getElementById("table").firstElementChild) {
          await ContentTaskUtils.waitForEvent(
            content,
            "CustomKeysUpdate",
            false,
            null,
            true
          );
        }
        await content.customElements.whenDefined("moz-input-search");
      });
      await Services.fog.testFlushAllChildren();
      await task(tab);
    });
  };
  // Propagate the name of the task function to our wrapper function so it shows up in test run output.
  Object.defineProperty(wrapped, "name", { value: task.name });
  add_task(wrapped);
}

// Check telemetry before about:keyboard is first opened.
add_task(function testBeforeFirstOpen() {
  ok(!Glean.browserCustomkeys.opened.testGetValue(), "No telemetry for opened");
});

// Test initial loading of about:keyboard.
addAboutKbTask(async function testInit(tab) {
  is(
    Glean.browserCustomkeys.opened.testGetValue(),
    1,
    "Correct telemetry for opened"
  );
  await SpecialPowers.spawn(tab, [], () => {
    Assert.greater(
      content.document.querySelectorAll("moz-card.category").length,
      5,
      "At least 5 categories"
    );
    const numKeys = content.document.querySelectorAll(".key").length;
    Assert.greater(numKeys, 50, "At least 50 keys");
    is(
      content.document.querySelectorAll(
        "moz-card.category[hidden], .key[hidden]"
      ).length,
      0,
      "No hidden categories or keys"
    );
    is(
      content.document.querySelectorAll(".customized").length,
      0,
      "No shortcuts are customized"
    );
    const unassigned = content.document.querySelectorAll(".key:not(.assigned)");
    is(unassigned.length, 5, "Five keys are unassigned");
    is(
      unassigned[0].dataset.id,
      "key_duplicateTab",
      "key_duplicateTab is unassigned"
    );
    is(
      unassigned[1].dataset.id,
      "key_copyTabURL",
      "key_copyTabURL is unassigned"
    );
    is(
      unassigned[2].dataset.id,
      "key_addTabSplitView",
      "key_addTabSplitView is unassigned"
    );
    is(
      unassigned[3].dataset.id,
      "key_separateTabSplitView",
      "key_separateTabSplitView is unassigned"
    );
    is(
      unassigned[4].dataset.id,
      "key_undoCloseTab",
      "key_undoCloseTab is unassigned"
    );
    is(
      content.document.querySelectorAll(".editing").length,
      0,
      "No keys are being edited"
    );
    // Make sure that lazy DevTools items have been added.
    ok(
      content.document.querySelector('.key[data-id="key_browserConsole"]'),
      "key_browserConsole is present"
    );
  });
});

// Test searching.
addAboutKbTask(async function testSearch(tab) {
  is(
    Glean.browserCustomkeys.opened.testGetValue(),
    2,
    "Correct telemetry for opened"
  );
  await SpecialPowers.spawn(tab, [], async () => {
    // Verify that visible category accordions are expanded
    function checkStateOfCategories(categories, init = false) {
      for (const card of categories) {
        const heading = card.wrappedJSObject.heading;
        if (card.hidden) {
          ok(true, `Category "${heading}" is not visible`);
        } else if (init && card != categories[0]) {
          // In initial state, only the first category is open.
          ok(
            !card.wrappedJSObject.expanded,
            `Category "${heading}" accordion is closed`
          );
        } else {
          ok(
            card.wrappedJSObject.expanded,
            `Category "${heading}" accordion is open`
          );
        }
      }
    }
    is(
      content.document.querySelectorAll(
        "moz-card.category[hidden], .key[hidden]"
      ).length,
      0,
      "No hidden categories or keys"
    );
    const categories = content.document.querySelectorAll("moz-card.category");
    checkStateOfCategories(categories, true /* initial state */);

    const search = content.document.getElementById("search").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(search, "focus");
    search.focus();
    await focused;

    info("Searching for zzz");
    let updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.sendString("zzz", content);
    await updated;
    is(
      content.document.querySelectorAll(
        "moz-card.category:not([hidden]), .key:not([hidden])"
      ).length,
      0,
      "No visible categories or keys"
    );
    checkStateOfCategories(categories);

    info("Clearing search");
    updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Escape", {}, content);
    await updated;
    is(
      content.document.querySelectorAll(
        "moz-card.category[hidden], .key[hidden]"
      ).length,
      0,
      "No hidden categories or keys"
    );
    checkStateOfCategories(categories, true /* return to initial state */);

    info("Searching for download");
    updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.sendString("download", content);
    await updated;
    let visibleKeys = content.document.querySelectorAll(".key:not([hidden])");
    is(visibleKeys.length, 1, "1 visible key");
    is(
      visibleKeys[0].dataset.id,
      "key_openDownloads",
      "Visible key is key_openDownloads"
    );
    let visibleCategories = content.document.querySelectorAll(
      "moz-card.category:not([hidden])"
    );
    is(visibleCategories.length, 1, "1 visible category");
    is(
      visibleKeys[0].closest("moz-card"),
      visibleCategories[0],
      "Visible key is inside visible category"
    );
    ok(!visibleCategories[0].hidden, "Category card (accordion) is visible");
    checkStateOfCategories(categories);

    info("Clearing search");
    updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Escape", {}, content);
    await updated;
    is(
      content.document.querySelectorAll(
        "moz-card.category[hidden], .key[hidden]"
      ).length,
      0,
      "No hidden categories or keys"
    );
    checkStateOfCategories(categories, true /* return to initial state */);

    info("Searching for history");
    updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.sendString("history", content);
    await updated;
    // This gives us results from both the Sidebars and History categories.
    visibleKeys = content.document.querySelectorAll(".key:not([hidden])");
    is(visibleKeys.length, 3, "3 visible keys");
    visibleCategories = content.document.querySelectorAll(
      "moz-card.category:not([hidden])"
    );
    is(visibleCategories.length, 2, "2 visible categories");
    checkStateOfCategories(categories);
  });
});

// Test a simple change.
addAboutKbTask(async function testChange(tab) {
  ok(
    !Glean.browserCustomkeys.actions.change.testGetValue(),
    "No telemetry for change action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    is(
      content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      _consts.downloadsDisplay,
      "Key is the default key"
    );
    is(
      content.getComputedStyle(
        content.downloadsRow.querySelector(".currentShortcut")
      ).display,
      "block",
      "Current shortcut field is visible"
    );
    info("Clicking Change for key_openDownloads");
    content.input =
      content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.downloadsRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    is(
      content.getComputedStyle(
        content.downloadsRow.querySelector(".currentShortcut")
      ).display,
      "none",
      "Current shortcut field is hidden in editing mode"
    );
    content.selected = ContentTaskUtils.waitForEvent(
      content.input.inputEl,
      "select"
    );
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    1,
    "Correct telemetry for change action"
  );
  info(`Pressing ${consts.unusedKey}`);
  EventUtils.synthesizeKey(consts.unusedKey, {}, window);
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.selected;
    is(content.input.value, "Invalid", "Input shows invalid");
  });
  // We can't test two invalid keys consecutively because the "invalid" text
  // won't change, so we can't detect whether the key was correctly treated as
  // invalid. So, this presses the unused modifiers first to clear the invalid
  // state and confirm the modifiers are shown, then presses the key being
  // tested and confirms it's treated as invalid.
  async function checkInvalid(label, keyArgs) {
    await SpecialPowers.spawn(tab, [], async () => {
      content.selected = ContentTaskUtils.waitForEvent(
        content.input.inputEl,
        "select"
      );
    });
    info(`Pressing ${consts.unusedModifiersDisplay}`);
    EventUtils.synthesizeKey(...consts.unusedModifiersArgs, window);
    await SpecialPowers.spawn(tab, [consts], async _consts => {
      await content.selected;
      is(
        content.input.value,
        _consts.unusedModifiersDisplay,
        "Input shows modifiers as they're pressed"
      );
      content.selected = ContentTaskUtils.waitForEvent(
        content.input.inputEl,
        "select"
      );
    });
    info(`Pressing ${label}`);
    EventUtils.synthesizeKey(...keyArgs, window);
    await SpecialPowers.spawn(tab, [consts], async _consts => {
      await content.selected;
      is(content.input.value, "Invalid", "Input shows invalid");
    });
  }
  await checkInvalid(`Shift+${consts.unusedKey}`, [
    consts.unusedKey,
    { shiftKey: true },
  ]);
  await checkInvalid("Backspace", ["KEY_Backspace", {}]);
  if (!isMac) {
    await checkInvalid("F10", ["KEY_F10", {}]);
  }
  await checkInvalid("Enter", ["KEY_Enter", {}]);
  await SpecialPowers.spawn(tab, [], async () => {
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  info(`Pressing ${consts.unusedDisplay}`);
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    is(
      content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      _consts.unusedDisplay,
      "Key is the customized key"
    );
    is(
      content.getComputedStyle(
        content.downloadsRow.querySelector(".currentShortcut")
      ).display,
      "block",
      "Current shortcut field is visible after editing mode is completed"
    );
  });
  // We deliberately let the result of this test leak into the next one.
});

// Test resetting a key. This also tests that the change from the previous test
// is reflected when the page is reloaded.
addAboutKbTask(async function testReset(tab) {
  ok(
    !Glean.browserCustomkeys.actions.reset.testGetValue(),
    "No telemetry for reset action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    const downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await downloadsRow.closest(".category").wrappedJSObject.updateComplete;
    ok(
      downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    is(
      downloadsRow.querySelector(".currentShortcut").wrappedJSObject.value,
      _consts.unusedDisplay,
      "Key is the customized key"
    );
    info("Clicking Reset for key_openDownloads");
    const reset = downloadsRow.querySelector(".reset");
    let updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    reset.click();
    await updated;
    ok(
      !downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    is(
      downloadsRow.querySelector(".currentShortcut").wrappedJSObject.value,
      _consts.downloadsDisplay,
      "Key is the default key"
    );
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.reset.testGetValue(),
    1,
    "Correct telemetry for reset action"
  );
});

// Test clearing a key.
addAboutKbTask(async function testClear(tab) {
  ok(
    !Glean.browserCustomkeys.actions.clear.testGetValue(),
    "No telemetry for clear action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    const downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await downloadsRow.closest(".category").wrappedJSObject.updateComplete;
    ok(
      !downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    ok(
      downloadsRow.classList.contains("assigned"),
      "key_openDownloads is assigned"
    );
    info("Clicking Clear for key_openDownloads");
    const clear = downloadsRow.querySelector(".clear");
    let updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    clear.click();
    await updated;
    ok(
      downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      !downloadsRow.classList.contains("assigned"),
      "key_openDownloads is not assigned"
    );
    is(
      downloadsRow.querySelector(".currentShortcut").wrappedJSObject.value,
      "",
      "Key is empty"
    );
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.clear.testGetValue(),
    1,
    "Correct telemetry for clear action"
  );
  // We deliberately let the result of this test leak into the next one.
});

// Test resetting all keys. This depends on the state set up by the previous
// test.
addAboutKbTask(async function testResetAll(tab) {
  ok(
    !Glean.browserCustomkeys.actions.reset_all.testGetValue(),
    "No telemetry for reset all action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      !content.downloadsRow.classList.contains("assigned"),
      "key_openDownloads is not assigned"
    );
  });

  info("Clicking Reset all, then Cancel");
  let handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 1 }
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.resetAll = content.document.getElementById("resetAll");
    content.resetAll.click();
  });
  await handled;
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.reset_all.testGetValue(),
    1,
    "Correct telemetry for reset all action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      !content.downloadsRow.classList.contains("assigned"),
      "key_openDownloads is not assigned"
    );

    info("Clicking Reset all, then OK");
    content.updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
  });

  handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 0 }
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.resetAll.click();
  });
  await handled;
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.reset_all.testGetValue(),
    2,
    "Correct telemetry for reset all action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.updated;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    ok(
      content.downloadsRow.classList.contains("assigned"),
      "key_openDownloads is assigned"
    );
  });
});

// Test a change which conflicts with another key.
addAboutKbTask(async function testConflictingChange(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    content.historyRow = content.document.querySelector(
      '.key[data-id="key_gotoHistory"]'
    );
    ok(
      !content.historyRow.classList.contains("customized"),
      "key_gotoHistory is not customized"
    );

    info("Clicking Change for key_openDownloads");
    content.input =
      content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.downloadsRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    2,
    "Correct telemetry for change action"
  );
  info(`Pressing ${consts.historyDisplay}, then clicking Cancel`);
  let handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 1 }
  );
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await handled;
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    // Change button got focus and content is updated:
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    ok(
      !content.historyRow.classList.contains("customized"),
      "key_gotoHistory is not customized"
    );

    info("Clicking Change for key_openDownloads");
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    3,
    "Correct telemetry for change action"
  );
  info(`Pressing ${consts.historyDisplay}, then clicking OK`);
  handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 0 }
  );
  EventUtils.synthesizeKey("H", consts.historyOptions, window);
  await handled;
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    // Change button got focus and content is updated:
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      content.downloadsRow.classList.contains("assigned"),
      "key_openDownloads is assigned"
    );
    is(
      content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      _consts.historyDisplay,
      "Key is the customized key"
    );
    ok(
      content.historyRow.classList.contains("customized"),
      "key_gotoHistory is customized"
    );
    ok(
      !content.historyRow.classList.contains("assigned"),
      "key_gotoHistory is not assigned"
    );
    is(
      content.historyRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      "",
      "Key is empty"
    );
  });
  // We deliberately let the result of this test leak into the next one.
});

// Test a reset which conflicts with another key. This depends on the state set
// up by the previous test.
addAboutKbTask(async function testConflictingReset(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    content.historyRow = content.document.querySelector(
      '.key[data-id="key_gotoHistory"]'
    );
    ok(
      content.historyRow.classList.contains("customized"),
      "key_gotoHistory is customized"
    );
  });

  info("Clicking Reset for key_gotoHistory, then Cancel");
  let handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 1 }
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.reset = content.historyRow.querySelector(".reset");
    content.reset.click();
  });
  await handled;
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.reset.testGetValue(),
    2,
    "Correct telemetry for reset action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      content.historyRow.classList.contains("customized"),
      "key_gotoHistory is customized"
    );
  });

  info("Clicking Reset for key_gotoHistory, then OK");
  handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 0 }
  );
  await SpecialPowers.spawn(tab, [], async () => {
    content.updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    content.reset.click();
  });
  await handled;
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.reset.testGetValue(),
    3,
    "Correct telemetry for reset action"
  );
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.updated;
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    ok(
      !content.downloadsRow.classList.contains("assigned"),
      "key_openDownloads is not assigned"
    );
    is(
      content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      "",
      "Key is empty"
    );
    ok(
      !content.historyRow.classList.contains("customized"),
      "key_gotoHistory is not customized"
    );
    ok(
      content.historyRow.classList.contains("assigned"),
      "key_gotoHistory is assigned"
    );
    is(
      content.historyRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      _consts.historyDisplay,
      "Key is the default key"
    );
  });

  CustomKeys.resetAll();
});

// Test that a reserved key is captured correctly.
addAboutKbTask(async function testReservedKey(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    info("Clicking Change for key_openDownloads");
    content.input =
      content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.downloadsRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    4,
    "Correct telemetry for change action"
  );
  info(`Pressing ${consts.newWindowDisplay}, then clicking Cancel`);
  let handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 1 }
  );
  EventUtils.synthesizeKey("N", { accelKey: true }, window);
  await handled;
  await SpecialPowers.spawn(tab, [], async () => {
    // Change button got focus and content is updated:
    await content.focused;
    ok(true, "Change button got focus");
  });
});

// Test that changing to a function key works correctly; i.e. that we handle key
// vs keycode.
addAboutKbTask(async function testFunctionKey(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    info("Clicking Change for key_openDownloads");
    content.input =
      content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.downloadsRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    5,
    "Correct telemetry for change action"
  );
  info("Pressing F1");
  EventUtils.synthesizeKey("KEY_F1", {}, window);
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    // Change button got focus and content is updated:
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is customized"
    );
    is(
      content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
        .value,
      "F1",
      "Key is the customized key"
    );
  });
  CustomKeys.resetAll();
});

// Test that changing to an arrow key works correctly.
addAboutKbTask(async function testFunctionKey(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.backRow = content.document.querySelector(
      '.key[data-id="goBackKb"]'
    );
    content.backRow.closest(".category").wrappedJSObject.expanded = true;
    await content.backRow.closest(".category").wrappedJSObject.updateComplete;
    ok(
      !content.backRow.classList.contains("customized"),
      "goBackKb is not customized"
    );
    info("Clicking Change for goBackKb");
    content.input = content.backRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.backRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  await Services.fog.testFlushAllChildren();
  is(
    Glean.browserCustomkeys.actions.change.testGetValue(),
    6,
    "Correct telemetry for change action"
  );
  info(`Pressing ${consts.backDisplay}`);
  EventUtils.synthesizeKey(...consts.backArgs, window);
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    // Change button got focus and content is updated:
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      !content.backRow.classList.contains("customized"),
      "goBackKb is not customized"
    );
    is(
      content.backRow.querySelector(".currentShortcut").wrappedJSObject.value,
      _consts.backDisplay,
      "Key is the default key"
    );
  });
});

// Test that the Meta (super) key works on Linux.
if (isLinux) {
  addAboutKbTask(async function testMetaKeyLinux(tab) {
    await SpecialPowers.spawn(tab, [], async () => {
      content.downloadsRow = content.document.querySelector(
        '.key[data-id="key_openDownloads"]'
      );
      content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
      await content.downloadsRow.closest(".category").wrappedJSObject
        .updateComplete;
      ok(
        !content.downloadsRow.classList.contains("customized"),
        "key_openDownloads is not customized"
      );
      info("Clicking Change for key_openDownloads");
      content.input =
        content.downloadsRow.querySelector(".newKey").wrappedJSObject;
      let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
      content.change = content.downloadsRow.querySelector(".change");
      content.change.click();
      await focused;
      ok(true, "New key input got focus");
      content.selected = ContentTaskUtils.waitForEvent(
        content.input.inputEl,
        "select"
      );
    });
    info("Pressing Meta (super) key");
    EventUtils.synthesizeKey("KEY_Meta", {}, window);
    await SpecialPowers.spawn(tab, [], async () => {
      await content.selected;
      is(content.input.value, "Win+", "Input shows Meta modifier");
      content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
    });
    info("Pressing Meta+Y");
    EventUtils.synthesizeKey("Y", { metaKey: true }, window);
    await SpecialPowers.spawn(tab, [], async () => {
      // Change button got focus and content is updated:
      await content.focused;
      ok(true, "Change button got focus");
      ok(
        content.downloadsRow.classList.contains("customized"),
        "key_openDownloads is customized"
      );
      is(
        content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
          .value,
        "Win+Y",
        "Key is the customized key"
      );
    });
    CustomKeys.resetAll();
  });
}

// Test accordion components expanding/collapsing with keyboard alone, and
// that action buttons are navigable with keyboard.
addAboutKbTask(async function testKeyboardAccess(tab) {
  await SpecialPowers.spawn(tab, [], async () => {
    info("Verify an expanded accordion states are toggled with Space/Enter");
    const firstCard =
      content.document.querySelector("moz-card.category").wrappedJSObject;

    ok(firstCard.expanded, "First category is initially expanded");
    firstCard.summaryEl.focus();
    is(
      content.document.activeElement.wrappedJSObject,
      firstCard,
      "Category card host is an activeElement when its summary is focused"
    );
    let firstCardToggled = ContentTaskUtils.waitForEvent(
      firstCard,
      "toggle",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
    await firstCardToggled;
    ok(!firstCard.expanded, "Accordion collapses with Enter");

    firstCardToggled = ContentTaskUtils.waitForEvent(
      firstCard,
      "toggle",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey(" ", {}, content);
    await firstCardToggled;
    ok(firstCard.expanded, "Accordion expands with Space");

    info("Verify a collapsed accordion states are toggled with Space/Enter");
    const secondCard =
      content.document.querySelectorAll("moz-card.category")[1].wrappedJSObject;
    ok(!secondCard.expanded, "Second category is initially collapsed");
    firstCardToggled = ContentTaskUtils.waitForEvent(
      firstCard,
      "toggle",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey(" ", {}, content);
    await firstCardToggled;
    ok(!firstCard.expanded, "First accordion collapses with Space");
    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    is(
      secondCard,
      content.document.activeElement.wrappedJSObject,
      "Second accordion is focused"
    );
    let secondCardToggled = ContentTaskUtils.waitForEvent(
      secondCard,
      "toggle",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey(" ", {}, content);
    await secondCardToggled;
    ok(secondCard.expanded, "Second accordion expands with Space");
    secondCardToggled = ContentTaskUtils.waitForEvent(
      secondCard,
      "toggle",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
    await secondCardToggled;
    ok(!secondCard.expanded, "Second accordion collapses with Enter");

    info(
      `Verify buttons are in the tab order and correctly are visible/hidden
          depending on a row state`
    );
    const downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );

    ok(!downloadsRow.classList.contains("customized"), "Row is not customized");
    ok(downloadsRow.classList.contains("assigned"), "Shortcut is assigned");

    info("Verify a card for key_openDownloads row is expanded");
    const toolsCard = downloadsRow.closest("moz-card").wrappedJSObject;
    toolsCard.summaryEl.focus();
    is(
      content.document.activeElement.wrappedJSObject,
      toolsCard,
      "Category card host is an activeElement when its summary is focused"
    );
    if (toolsCard !== firstCard) {
      info("Expanding Tools card for key_openDownloads row");
      ok(!toolsCard.expanded, "Tools card accordion is collapsed by default");
      const cardToggled = ContentTaskUtils.waitForEvent(
        toolsCard,
        "toggle",
        false,
        null,
        true
      );
      EventUtils.synthesizeKey("KEY_Enter", {}, content);
      const openEvent = await cardToggled;
      is(openEvent.newState, "open", "Tools card's new state is open");
      is(openEvent.oldState, "closed", "Tools card's old state is closed");
    }
    ok(
      toolsCard.expanded,
      "key_openDownloads included in a card accordion that is expanded"
    );

    info("Testing visibility of controls within a row");
    const currentInput = downloadsRow.querySelector(".currentShortcut");
    const changeBtn = downloadsRow.querySelector(".change");
    const clearBtn = downloadsRow.querySelector(".clear");
    const resetBtn = downloadsRow.querySelector(".reset");

    // Change and Clear are visible, Reset (Undo) is hidden until customized.
    isnot(
      content.getComputedStyle(changeBtn).display,
      "none",
      "Change button is visible for keyboard users"
    );
    isnot(
      content.getComputedStyle(clearBtn).display,
      "none",
      "Clear button is visible when shortcut is assigned"
    );
    is(
      content.getComputedStyle(resetBtn).display,
      "none",
      "Reset button is hidden when shortcut is not customized"
    );
    ok(!changeBtn.disabled, "Change button is not disabled");
    ok(!clearBtn.disabled, "Clear button is not disabled");

    info("Testing keyboard navigation within a row");
    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    is(
      currentInput,
      content.document.activeElement,
      "Current Shortcut text input is focused"
    );
    ok(
      currentInput.hasAttribute("readonly"),
      "Current Shortcut text input is in read-only mode"
    );
    ok(
      !currentInput.hasAttribute("placeholder"),
      "Current Shortcut text input does not have a placeholder"
    );

    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    is(
      changeBtn,
      content.document.activeElement,
      "Change/Edit button is focused"
    );
    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    is(clearBtn, content.document.activeElement, "Clear button is focused");

    info("Clearing the shortcut with keyboard");
    let updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
    await updated;
    is(
      currentInput.getAttribute("data-l10n-id"),
      "customkeys-shortcut-unassigned",
      "Current Shortcut text input receives an unassigned localization ID"
    );
    ok(downloadsRow.classList.contains("customized"), "Row is customized");
    ok(
      !downloadsRow.classList.contains("assigned"),
      "Shortcut is not assigned"
    );
    is(
      content.getComputedStyle(clearBtn).display,
      "none",
      "Clear button is not visible when shortcut is customized"
    );
    isnot(
      content.getComputedStyle(resetBtn).display,
      "none",
      "Reset/Restore button is visible when shortcut is customized"
    );
    is(resetBtn, content.document.activeElement, "Reset button is focused");

    info("Restoring the shortcut with keyboard");
    updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    EventUtils.synthesizeKey("KEY_Enter", {}, content);
    await updated;
    isnot(
      currentInput.getAttribute("data-l10n-id"),
      "customkeys-shortcut-unassigned",
      "Current Shortcut text input no longer has an unassigned localization ID"
    );
    ok(!downloadsRow.classList.contains("customized"), "Row is not customized");
    ok(downloadsRow.classList.contains("assigned"), "Shortcut is assigned");
    is(
      content.getComputedStyle(resetBtn).display,
      "none",
      "Reset/Restore button is not visible when shortcut is restored"
    );
    isnot(
      content.getComputedStyle(clearBtn).display,
      "none",
      "Clear button is not visible when shortcut is restored"
    );
    is(clearBtn, content.document.activeElement, "Clear button is focused");

    info(
      "Checking the focus is leaving the Downloads row from the Clear button"
    );
    EventUtils.synthesizeKey("KEY_Tab", {}, content);
    ok(
      !downloadsRow.contains(content.document.activeElement),
      "Focus has left the key_openDownloads row"
    );
  });
});

// Test a change which conflicts with an internal key.
addAboutKbTask(async function testInternalConflictingChange(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    content.pasteRow = content.document.querySelector(
      '.key[data-id="key_paste"]'
    );
    ok(
      !content.pasteRow.classList.contains("customized"),
      "key_paste is not customized"
    );
    ok(
      content.pasteRow.classList.contains("internal"),
      "key_paste is internal"
    );
    ok(
      ContentTaskUtils.isHidden(content.pasteRow.querySelector(".change")),
      "key_paste Change button is hidden"
    );
    ok(
      ContentTaskUtils.isHidden(content.pasteRow.querySelector(".clear")),
      "key_paste Clear button is hidden"
    );

    info("Clicking Change for key_openDownloads");
    const input = content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(input, "focus");
    const change = content.downloadsRow.querySelector(".change");
    change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(change, "focus");
  });
  info(`Pressing ${consts.pasteDisplay}, then clicking OK`);
  let handled = PromptTestUtils.handleNextPrompt(
    window,
    { modalType: Services.prompt.MODAL_TYPE_CONTENT },
    { buttonNumClick: 0 }
  );
  EventUtils.synthesizeKey("V", { accelKey: true }, window);
  await handled;
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    ok(
      !content.pasteRow.classList.contains("customized"),
      "key_paste is not customized"
    );
  });
});

// Test assigning a key with no default assignment, then resetting it.
addAboutKbTask(async function testChangeAndResetUnassigned(tab) {
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    content.dupTabRow = content.document.querySelector(
      '.key[data-id="key_duplicateTab"]'
    );
    content.dupTabRow.closest(".category").wrappedJSObject.expanded = true;
    await content.dupTabRow.closest(".category").wrappedJSObject.updateComplete;
    ok(
      !content.dupTabRow.classList.contains("customized"),
      "key_duplicateTab is not customized"
    );
    ok(
      !content.dupTabRow.classList.contains("assigned"),
      "key_duplicateTab is not assigned"
    );

    info("Clicking Change for key_duplicateTab");
    content.input = content.dupTabRow.querySelector(".newKey").wrappedJSObject;
    let focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.change = content.dupTabRow.querySelector(".change");
    content.change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(content.change, "focus");
  });
  info(`Pressing ${consts.unusedDisplay}`);
  EventUtils.synthesizeKey(consts.unusedKey, consts.unusedOptions, window);
  await SpecialPowers.spawn(tab, [consts], async _consts => {
    await content.focused;
    ok(true, "Change button got focus");
    ok(
      content.dupTabRow.classList.contains("customized"),
      "key_duplicateTab is customized"
    );
    ok(
      content.dupTabRow.classList.contains("assigned"),
      "key_duplicateTab is assigned"
    );
    is(
      content.dupTabRow.querySelector(".currentShortcut").wrappedJSObject.value,
      _consts.unusedDisplay,
      "Key is the customized key"
    );

    info("Clicking Reset for key_duplicateTab");
    const reset = content.dupTabRow.querySelector(".reset");
    const updated = ContentTaskUtils.waitForEvent(
      content,
      "CustomKeysUpdate",
      false,
      null,
      true
    );
    reset.click();
    await updated;
    ok(
      !content.dupTabRow.classList.contains("customized"),
      "key_duplicateTab is not customized"
    );
    ok(
      !content.dupTabRow.classList.contains("assigned"),
      "key_duplicateTab is not assigned"
    );
  });
});

/**
 * Assign a shortcut to key_openDownloads and check how it is recorded.
 *
 * @param {object} tab The about:keyboard tab.
 * @param {Array} keyArgs Arguments for EventUtils.synthesizeKey.
 * @param {string} display The shortcut expected to be shown afterwards.
 * @param {string} message The message for the assertion on the shortcut.
 */
async function checkRecordedKey(tab, keyArgs, display, message) {
  await SpecialPowers.spawn(tab, [], async () => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
    info("Clicking Change for key_openDownloads");
    const input = content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    const focused = ContentTaskUtils.waitForEvent(input, "focus");
    const change = content.downloadsRow.querySelector(".change");
    change.click();
    await focused;
    ok(true, "New key input got focus");
    content.focused = ContentTaskUtils.waitForEvent(change, "focus");
  });
  info(`Pressing ${display}`);
  EventUtils.synthesizeKey(...keyArgs, window);
  await SpecialPowers.spawn(
    tab,
    [display, message],
    async (_display, _message) => {
      await content.focused;
      ok(true, "Change button got focus");
      ok(
        content.downloadsRow.classList.contains("customized"),
        "key_openDownloads is customized"
      );
      is(
        content.downloadsRow.querySelector(".currentShortcut").wrappedJSObject
          .value,
        _display,
        _message
      );
    }
  );
  CustomKeys.resetAll();
}

// Test that a key which Alt remaps to a different character is assigned as the
// key that was pressed, not as the character it produces.
addAboutKbTask(async function testAltRemappedKey(tab) {
  await checkRecordedKey(
    tab,
    consts.remappedArgs,
    consts.remappedDisplay,
    "Key is the key that was pressed, not the character it produced"
  );
});

// Test that a digit pressed with Alt and Shift is assigned as the character it
// produces, since that is the only character which can match the shortcut.
addAboutKbTask(async function testAltShiftRemappedDigit(tab) {
  await checkRecordedKey(
    tab,
    consts.remappedDigitArgs,
    consts.remappedDigitDisplay,
    "Key is the character that the key produced"
  );
});

// Test that a character which takes two UTF-16 code units is assigned as that
// character rather than as a keycode.
addAboutKbTask(async function testTwoCodeUnitKey(tab) {
  await checkRecordedKey(
    tab,
    consts.twoCodeUnitArgs,
    consts.twoCodeUnitDisplay,
    "Key is the two code unit character"
  );
});

// Test that a letter pressed with only AltGraph is not a valid shortcut, since
// AltGraph is not a shortcut modifier.
addAboutKbTask(async function testAltGraphAloneInvalid(tab) {
  await SpecialPowers.spawn(tab, [], async () => {
    content.downloadsRow = content.document.querySelector(
      '.key[data-id="key_openDownloads"]'
    );
    content.downloadsRow.closest(".category").wrappedJSObject.expanded = true;
    await content.downloadsRow.closest(".category").wrappedJSObject
      .updateComplete;
    info("Clicking Change for key_openDownloads");
    content.input =
      content.downloadsRow.querySelector(".newKey").wrappedJSObject;
    const focused = ContentTaskUtils.waitForEvent(content.input, "focus");
    content.downloadsRow.querySelector(".change").click();
    await focused;
    ok(true, "New key input got focus");
    content.selected = ContentTaskUtils.waitForEvent(
      content.input.inputEl,
      "select"
    );
  });
  info("Pressing AltGraph+M");
  EventUtils.synthesizeKey(
    "µ",
    { altGraphKey: true, keyCode: KeyEvent.DOM_VK_M },
    window
  );
  await SpecialPowers.spawn(tab, [], async () => {
    await content.selected;
    is(content.input.value, "Invalid", "Input shows invalid");
    // Editing ends when the input loses focus.
    content.input.inputEl.blur();
    await ContentTaskUtils.waitForCondition(
      () => !content.downloadsRow.classList.contains("editing"),
      "Editing ended"
    );
    ok(
      !content.downloadsRow.classList.contains("customized"),
      "key_openDownloads is not customized"
    );
  });
});
