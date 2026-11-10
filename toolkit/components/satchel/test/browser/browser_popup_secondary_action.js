/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { FormHistory } = ChromeUtils.importESModule(
  "resource://gre/modules/FormHistory.sys.mjs"
);

const PREF = "browser.autocomplete.removeRecords.enabled";
const URL = `data:text/html,<input type="text" name="field1">`;

async function withFormHistoryPopup(
  task,
  values = ["value1", "value2"],
  searchString = ""
) {
  await BrowserTestUtils.withNewTab({ gBrowser, url: URL }, async browser => {
    const {
      autoCompletePopup,
      autoCompletePopup: { richlistbox: itemsBox },
    } = browser;

    await FormHistory.update([
      { op: "remove" },
      ...values.map(value => ({ op: "add", fieldname: "field1", value })),
    ]);
    await SpecialPowers.spawn(browser, [], async () => {
      content.document.querySelector("input").focus();
    });

    if (searchString) {
      for (const char of searchString) {
        await BrowserTestUtils.sendChar(char, browser);
      }
    } else {
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    }
    await TestUtils.waitForCondition(() => autoCompletePopup.popupOpen);

    await task(browser, autoCompletePopup, itemsBox);

    await SpecialPowers.spawn(browser, [], async () => {
      content.document.querySelector("input").blur();
    });
  });
}

function getRowItem(itemsBox, index) {
  return itemsBox
    .querySelectorAll(".autocomplete-row-item")
    [index].querySelector("autocomplete-row-item");
}

function countEntries(value) {
  return FormHistory.count({ fieldname: "field1", value });
}

// The row's selected attribute is applied by an async Lit render, so the trash
// button can still be hidden when the row already reports itself as selected.
async function selectRowAndGetTrashButton(browser, itemsBox, index) {
  const rowItem = getRowItem(itemsBox, index);
  await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
  await TestUtils.waitForCondition(
    () => rowItem.selected,
    `row ${index} is selected`
  );

  const button = rowItem.shadowRoot.querySelector(
    "moz-button.secondary-action"
  );
  await TestUtils.waitForCondition(
    () => button.checkVisibility({ checkVisibilityCSS: true }),
    "the trash button is visible"
  );
  return button;
}

add_task(async function test_no_secondary_action_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const rowItem = getRowItem(itemsBox, 0);
    Assert.equal(
      rowItem.actions.secondary,
      null,
      "Form history rows have no secondary action when the pref is off"
    );
    Assert.ok(
      !rowItem.shadowRoot.querySelector("moz-button.secondary-action"),
      "No secondary action button is rendered"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_trash_button_when_pref_enabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  Services.fog.testResetFOG();
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
    Assert.equal(
      shownEvents?.length,
      1,
      "One dropdown shown event is recorded"
    );
    Assert.equal(
      shownEvents[0].extra.record_type,
      "formhistory",
      "The event names the formhistory record type"
    );

    const rowItem = getRowItem(itemsBox, 0);
    Assert.equal(
      rowItem.actions.secondary.type,
      "delete",
      "Form history rows show a delete secondary action when the pref is on"
    );
    Assert.ok(
      !rowItem.actions.secondary.actions,
      "The trash is a single action, not a flyout"
    );

    const button = rowItem.shadowRoot.querySelector(
      "moz-button.secondary-action"
    );
    Assert.ok(button, "The trash button is rendered");
    Assert.ok(
      button.iconSrc.endsWith("delete.svg"),
      "The secondary action shows the trash icon"
    );

    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await TestUtils.waitForCondition(() => rowItem.selected);

    EventUtils.synthesizeMouseAtCenter(button, {});
    await TestUtils.waitForTick();
    Assert.ok(
      autoCompletePopup.popupOpen,
      "The popup stays open when the trash button is clicked"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_trash_button_shows_no_confirmation() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  let sawDialog = false;
  const observer = () => {
    sawDialog = true;
  };
  Services.obs.addObserver(observer, "common-dialog-loaded");

  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const rowItem = getRowItem(itemsBox, 0);
    const button = rowItem.shadowRoot.querySelector(
      "moz-button.secondary-action"
    );

    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await TestUtils.waitForCondition(() => rowItem.selected);

    EventUtils.synthesizeMouseAtCenter(button, {});
    await TestUtils.waitForTick();

    Assert.ok(
      !sawDialog,
      "Form history entries are removed without a confirmation prompt"
    );
    Assert.ok(
      autoCompletePopup.popupOpen,
      "The popup stays open after the trash button is clicked"
    );
  });

  Services.obs.removeObserver(observer, "common-dialog-loaded");
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_escape_leaves_secondary_action_before_closing() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  Services.fog.testResetFOG();
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const rowItem = getRowItem(itemsBox, 0);

    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await TestUtils.waitForCondition(() => rowItem.selected);

    await BrowserTestUtils.synthesizeKey("VK_TAB", {}, browser);
    await TestUtils.waitForCondition(
      () => rowItem.subfocused,
      "Tab moves the sub-selection onto the secondary action"
    );

    await BrowserTestUtils.synthesizeKey("VK_ESCAPE", {}, browser);
    await TestUtils.waitForCondition(
      () => !rowItem.subfocused,
      "Escape drops the sub-selection back to the row"
    );
    Assert.ok(
      autoCompletePopup.popupOpen,
      "The popup stays open when Escape leaves the secondary action"
    );

    await BrowserTestUtils.synthesizeKey("VK_ESCAPE", {}, browser);
    await TestUtils.waitForCondition(
      () => !autoCompletePopup.popupOpen,
      "A second Escape closes the popup"
    );

    const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
    const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
    Assert.equal(
      shownEvents?.length,
      1,
      "One dropdown shown event is recorded"
    );
    Assert.equal(
      closedEvents?.length,
      1,
      "One dropdown closed event is recorded"
    );
    Assert.equal(
      closedEvents[0].extra.record_type,
      "formhistory",
      "The event names the formhistory record type"
    );
    Assert.equal(
      closedEvents[0].extra.close_reason,
      "dismissed",
      "Escape records a dismissed close"
    );
    Assert.ok(closedEvents[0].extra.flow_id, "The event carries a flow id");
    Assert.equal(
      closedEvents[0].extra.flow_id,
      shownEvents[0].extra.flow_id,
      "The closed event's flow id matches the shown event for the same dropdown"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_selecting_an_entry_records_dropdown_closed() {
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const rowItem = getRowItem(itemsBox, 0);
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await TestUtils.waitForCondition(() => rowItem.selected);

    Services.fog.testResetFOG();
    await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);
    await TestUtils.waitForCondition(
      () => !autoCompletePopup.popupOpen,
      "Selecting an entry closes the popup"
    );

    const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
    Assert.equal(
      closedEvents?.length,
      1,
      "One dropdown closed event is recorded"
    );
    Assert.equal(
      closedEvents[0].extra.record_type,
      "formhistory",
      "The event names the formhistory record type"
    );
    Assert.equal(
      closedEvents[0].extra.close_reason,
      "selected",
      "Picking an entry records a selected close"
    );
  });
});

add_task(async function test_trash_button_removes_the_entry() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  Services.fog.testResetFOG();
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const button = await selectRowAndGetTrashButton(browser, itemsBox, 0);

    EventUtils.synthesizeMouseAtCenter(button, {});

    await TestUtils.waitForCondition(
      async () => !(await countEntries("value1")),
      "the entry is removed from form history"
    );
    await TestUtils.waitForCondition(
      () => autoCompletePopup.matchCount == 1,
      "the deleted row leaves the popup"
    );
    Assert.equal(
      await countEntries("value1"),
      0,
      "The clicked entry is removed from form history"
    );
    const events = Glean.autocomplete.recordInteraction.testGetValue();
    Assert.equal(events?.length, 1, "One interaction event is recorded");
    Assert.equal(
      events[0].extra.interaction_type,
      "delete",
      "The trash button records a delete interaction"
    );
    Assert.equal(
      events[0].extra.record_type,
      "formhistory",
      "The event names the formhistory record type"
    );
    const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
    Assert.equal(shownEvents?.length, 1, "The dropdown is shown only once");
    Assert.ok(events[0].extra.flow_id, "The interaction has a flow id");
    Assert.equal(
      events[0].extra.flow_id,
      shownEvents[0].extra.flow_id,
      "The interaction shares the dropdown's flow id"
    );
    Assert.equal(
      await countEntries("value2"),
      1,
      "The entry that was not clicked is still saved"
    );
    Assert.ok(
      autoCompletePopup.popupOpen,
      "The popup stays open while an entry remains"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_deleting_the_last_entry_closes_the_popup() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  Services.fog.testResetFOG();
  await withFormHistoryPopup(
    async (browser, autoCompletePopup, itemsBox) => {
      const button = await selectRowAndGetTrashButton(browser, itemsBox, 0);

      EventUtils.synthesizeMouseAtCenter(button, {});

      await TestUtils.waitForCondition(
        async () => !(await countEntries("value1")),
        "the entry is removed from form history"
      );
      await TestUtils.waitForCondition(
        () => !autoCompletePopup.popupOpen,
        "the popup closes once nothing is left to show"
      );
      Assert.equal(
        await countEntries("value1"),
        0,
        "The only entry is removed from form history"
      );
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.equal(closedEvents?.length, 1, "One dropdown closed event");
      Assert.equal(
        closedEvents[0].extra.close_reason,
        "dismissed",
        "Emptying the dropdown is not a removal prompt close"
      );
      const [interaction] = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.ok(interaction.extra.flow_id, "The interaction has a flow id");
      Assert.equal(
        closedEvents[0].extra.flow_id,
        interaction.extra.flow_id,
        "The closed event shares the interaction's flow id"
      );
    },
    ["value1"]
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_removals_in_one_dropdown_share_a_flow_id() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  Services.fog.testResetFOG();
  await withFormHistoryPopup(
    async (browser, autoCompletePopup, itemsBox) => {
      for (const remaining of [2, 1]) {
        const rowItem = getRowItem(itemsBox, 0);
        if (!rowItem.selected) {
          await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
          await TestUtils.waitForCondition(
            () => rowItem.selected,
            "the first row is selected"
          );
        }
        const button = rowItem.shadowRoot.querySelector(
          "moz-button.secondary-action"
        );
        await TestUtils.waitForCondition(
          () => button.checkVisibility({ checkVisibilityCSS: true }),
          "the trash button is visible"
        );
        EventUtils.synthesizeMouseAtCenter(button, {});
        await TestUtils.waitForCondition(
          () => autoCompletePopup.matchCount == remaining,
          "the deleted row leaves the popup"
        );
      }
      Assert.equal(
        await FormHistory.count({ fieldname: "field1" }),
        1,
        "Two entries are removed"
      );

      const popupHidden = BrowserTestUtils.waitForEvent(
        autoCompletePopup,
        "popuphidden"
      );
      await BrowserTestUtils.synthesizeKey("VK_ESCAPE", {}, browser);
      await popupHidden;

      const shownEvents = Glean.autocomplete.dropdownShown.testGetValue();
      Assert.equal(shownEvents?.length, 1, "The dropdown is shown only once");
      const flowId = shownEvents[0].extra.flow_id;
      Assert.ok(flowId, "The dropdown has a flow id");
      const events = Glean.autocomplete.recordInteraction.testGetValue();
      Assert.deepEqual(
        events?.map(e => e.extra.flow_id),
        [flowId, flowId],
        "Both interactions share the dropdown's flow id"
      );
      const closedEvents = Glean.autocomplete.dropdownClosed.testGetValue();
      Assert.deepEqual(
        closedEvents?.map(e => [e.extra.close_reason, e.extra.flow_id]),
        [["dismissed", flowId]],
        "Escape closes the dropdown with the same flow id"
      );
    },
    ["value1", "value2", "value3"]
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_keyboard_activation_removes_the_entry() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    await selectRowAndGetTrashButton(browser, itemsBox, 0);

    await BrowserTestUtils.synthesizeKey("VK_TAB", {}, browser);
    await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);

    await TestUtils.waitForCondition(
      async () => !(await countEntries("value1")),
      "the entry is removed from form history"
    );
    await TestUtils.waitForCondition(
      () => autoCompletePopup.matchCount == 1,
      "the deleted row leaves the popup"
    );
    Assert.equal(
      await countEntries("value1"),
      0,
      "Activating the trash button from the keyboard removes the entry"
    );
    Assert.equal(
      await countEntries("value2"),
      1,
      "The entry that was not selected is still saved"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_delete_key_still_removes_with_pref_disabled() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, false]] });
  await withFormHistoryPopup(async (browser, autoCompletePopup, itemsBox) => {
    const rowItem = getRowItem(itemsBox, 0);
    await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
    await TestUtils.waitForCondition(
      () => rowItem.selected,
      "the first row is selected"
    );

    Services.fog.testResetFOG();
    // nsFormFillController routes Delete to the controller everywhere except
    // macOS, where the shortcut is Shift+Backspace instead.
    if (AppConstants.platform == "macosx") {
      await BrowserTestUtils.synthesizeKey(
        "VK_BACK_SPACE",
        { shiftKey: true },
        browser
      );
    } else {
      await BrowserTestUtils.synthesizeKey("VK_DELETE", {}, browser);
    }

    await TestUtils.waitForCondition(
      async () => !(await countEntries("value1")),
      "the entry is removed from form history"
    );
    await TestUtils.waitForCondition(
      () => autoCompletePopup.matchCount == 1,
      "the deleted row leaves the popup"
    );
    Assert.equal(
      await countEntries("value1"),
      0,
      "The keyboard shortcut still removes the entry without the pref"
    );
    Assert.equal(
      Glean.autocomplete.recordInteraction.testGetValue(),
      null,
      "The keyboard shortcut does not record a secondary action interaction"
    );
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_removal_is_not_served_from_the_search_cache() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await withFormHistoryPopup(
    async (browser, autoCompletePopup, itemsBox) => {
      await TestUtils.waitForCondition(
        () => autoCompletePopup.matchCount == 2,
        "both entries match the typed prefix"
      );

      const removed = getRowItem(itemsBox, 0).value;
      const kept = getRowItem(itemsBox, 1).value;

      await selectRowAndGetTrashButton(browser, itemsBox, 0);
      await BrowserTestUtils.synthesizeKey("VK_TAB", {}, browser);
      await BrowserTestUtils.synthesizeKey("VK_RETURN", {}, browser);

      await TestUtils.waitForCondition(
        async () => !(await countEntries(removed)),
        "the entry is removed from form history"
      );
      await TestUtils.waitForCondition(
        () => autoCompletePopup.matchCount == 1,
        "the deleted row leaves a popup opened by a typed prefix"
      );
      await TestUtils.waitForCondition(
        () => getRowItem(itemsBox, 0).value == kept,
        "the remaining row is the entry that was not removed"
      );
      Assert.equal(
        await countEntries(kept),
        1,
        "The entry that was not activated is still saved"
      );
    },
    ["abcdefg", "abcdxyz"],
    "abcd"
  );
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_trash_button_works_on_the_row_that_moves_up() {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  await withFormHistoryPopup(
    async (browser, autoCompletePopup, itemsBox) => {
      const getTrashButton = () =>
        getRowItem(itemsBox, 0).shadowRoot.querySelector(
          "moz-button.secondary-action"
        );
      const first = getRowItem(itemsBox, 0).value;
      const second = getRowItem(itemsBox, 1).value;

      EventUtils.synthesizeMouseAtCenter(getTrashButton(), {
        type: "mousemove",
      });
      await TestUtils.waitForCondition(
        () => getTrashButton().checkVisibility({ checkVisibilityCSS: true }),
        "the trash button is visible on hover"
      );
      EventUtils.synthesizeMouseAtCenter(getTrashButton(), {});
      await TestUtils.waitForCondition(
        () =>
          autoCompletePopup.matchCount === 2 &&
          getRowItem(itemsBox, 0).value === second,
        "the next row moves up"
      );

      // Click again without moving the pointer.
      await TestUtils.waitForCondition(
        () => getTrashButton().checkVisibility({ checkVisibilityCSS: true }),
        "the moved-up row's trash button is visible"
      );
      EventUtils.synthesizeMouseAtCenter(getTrashButton(), {});
      await TestUtils.waitForCondition(
        async () => !(await countEntries(second)),
        "the moved-up entry is removed"
      );
      Assert.equal(await countEntries(first), 0, "The first entry is removed");
      Assert.equal(
        await FormHistory.count({ fieldname: "field1" }),
        1,
        "Only the two clicked entries are removed"
      );
    },
    ["value1", "value2", "value3"]
  );
  await SpecialPowers.popPrefEnv();
});
