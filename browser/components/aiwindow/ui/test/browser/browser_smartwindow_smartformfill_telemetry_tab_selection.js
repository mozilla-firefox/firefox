/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_telemetry.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_telemetry.js",
  this
);

// Pages for the model to pick between, opened before the form so the form
// stays the focused tab.
const CONTEXT_TABS = ["https://example.net/", "https://example.org/"];

/**
 * Opens the tab selector on the focused form the way the autocomplete row's
 * edit button does, so the selection a round runs with can be the user's
 * rather than the model's.
 *
 * @param {Window} win
 * @param {MozBrowser} browser
 * @param {object} actor The SmartFormFill parent actor
 * @returns {Promise<object>} The open selector, and the promise that resolves
 * once the user's pick has been taken from it
 */
async function openTabSelector(win, browser, actor) {
  const dialogManager = win.gBrowser
    .getTabDialogBox(browser)
    .getTabDialogManager();
  const edited = actor.onAutoCompleteEntrySelected("SmartFormFill:EditSources");

  // The request gives up rather than throwing when the form is not ready for
  // it, which without this would only show as a wait for a dialog that is
  // never coming.
  const opened = await Promise.race([
    edited.then(() => false),
    TestUtils.waitForCondition(() => dialogManager._dialogs.length === 1)
      .then(() => true)
      .catch(() => false),
  ]);
  Assert.ok(
    opened,
    "Expected editing the sources to open the tab selector, it gave up instead"
  );

  const dialog = dialogManager._dialogs[0];
  await dialog._dialogReady;

  const dialogWindow = dialog._frame.contentWindow;
  const tabSelector = dialogWindow.document.querySelector(
    "ai-sff-tab-selector"
  );
  await tabSelector.updateComplete;

  return { dialogWindow, tabSelector, edited };
}

function getSelectorTab(tabSelector, url) {
  const groups = tabSelector.renderRoot.querySelectorAll("moz-box-group");
  const tabLists = [tabSelector.suggestedTabs, tabSelector.otherTabs];
  for (const [listIndex, tabs] of tabLists.entries()) {
    const tabIndex = tabs.findIndex(tab => tab.url === url);
    if (tabIndex !== -1) {
      return {
        tab: tabs[tabIndex],
        toggle: groups[listIndex].querySelectorAll("moz-toggle")[tabIndex],
      };
    }
  }
  throw new Error(`Could not find dialog tab for ${url}`);
}

async function toggleSelectorTab(tabSelector, url) {
  const { tab, toggle } = getSelectorTab(tabSelector, url);
  const previousPressed = tab.pressed;
  await toggle.updateComplete;
  Assert.ok(
    !toggle.disabled,
    `Expected an offered tab to be toggleable, the toggle for ${url} is disabled`
  );

  const toggled = BrowserTestUtils.waitForEvent(toggle, "toggle");
  toggle.buttonEl.click();
  await toggled;
  await tabSelector.updateComplete;
  Assert.notEqual(
    getSelectorTab(tabSelector, url).tab.pressed,
    previousPressed,
    `Expected toggling to flip the pressed state, ${url} is still ${previousPressed}`
  );
}

async function acceptTabSelector({ dialogWindow, tabSelector, edited }) {
  const button = tabSelector.renderRoot.querySelector(
    'moz-button[data-l10n-id="ai-smart-form-fill-accept-tab-select-button"]'
  );
  EventUtils.synthesizeMouseAtCenter(button.buttonEl, {}, dialogWindow);

  await edited;
}

async function cancelTabSelector({ dialogWindow, tabSelector, edited }) {
  const button = tabSelector.renderRoot.querySelector(
    'moz-button[data-l10n-id="ai-smart-form-fill-cancel-tab-select-button"]'
  );
  EventUtils.synthesizeMouseAtCenter(button.buttonEl, {}, dialogWindow);

  await edited;
}

add_task(async function test_a_fill_records_what_became_of_the_model_tabs() {
  await withFormPage(
    { findRelevantTabs: selectTheFirstTab },
    async ({ browser, actor }) => {
      await runRoundOnForm(browser, actor);

      const [outcome] = await waitForEvents("formRelevantTabsOutcome", 1);
      const [request] = recordedExtras("formFillGenerateRequest");

      Assert.equal(
        outcome.flow_id,
        request.flow_id,
        "Expected the tab outcome to carry the flow id of the round it belongs to"
      );
      Assert.equal(
        outcome.tabs_final,
        "1",
        "Expected tabs_final to be the 1 tab the model picked"
      );
      Assert.equal(
        outcome.tabs_kept,
        "1",
        "Expected the model's 1 tab to count as kept, the selection was untouched"
      );
      Assert.equal(
        outcome.tabs_removed,
        "0",
        "Expected no removal, the selection was untouched"
      );
      Assert.equal(
        outcome.tabs_added,
        "0",
        "Expected no addition, the selection was untouched"
      );
      Assert.equal(
        outcome.tabs_final,
        request.tabs,
        "Expected tabs_final to match the tab count the generation request was given"
      );
      Assert.equal(
        outcome.editor_opens,
        "0",
        "Expected no editor open for a fill that ran on the model's picks"
      );
      Assert.strictEqual(
        outcome.editor_result,
        undefined,
        "Expected no editor_result where the editor was never opened"
      );
    }
  );
});

add_task(async function test_a_selection_the_user_changed_is_recorded() {
  await withFormPage(
    {
      findRelevantTabs: selectTheFirstTab,
      contextTabs: CONTEXT_TABS,
    },
    async ({ win, browser, actor }) => {
      await SimpleTest.promiseFocus(browser);
      await focusField(browser, "#email");

      const selector = await openTabSelector(win, browser, actor);
      const { tabSelector } = selector;

      Assert.equal(
        tabSelector.suggestedTabs.length,
        1,
        "Expected the selector to suggest the 1 tab the fake picks"
      );
      Assert.ok(
        tabSelector.otherTabs.length,
        "Expected at least one unsuggested tab for the user to select in its place"
      );

      await toggleSelectorTab(tabSelector, tabSelector.suggestedTabs[0].url);
      await toggleSelectorTab(tabSelector, tabSelector.otherTabs[0].url);
      await acceptTabSelector(selector);

      await actor.triggerAutofill();

      const [outcome] = await waitForEvents("formRelevantTabsOutcome", 1);

      Assert.equal(
        outcome.tabs_final,
        "1",
        "Expected tabs_final to be the 1 tab the user left selected"
      );
      Assert.equal(
        outcome.tabs_kept,
        "0",
        "Expected no tab kept, the user deselected the model's only pick"
      );
      Assert.equal(
        outcome.tabs_removed,
        "1",
        "Expected the model's deselected pick to count as removed"
      );
      Assert.equal(
        outcome.tabs_added,
        "1",
        "Expected the tab the user selected instead to count as added"
      );
      Assert.equal(
        outcome.editor_opens,
        "1",
        "Expected the one open the user made the change in to be counted"
      );
      Assert.equal(
        outcome.editor_result,
        "done",
        "Expected editor_result to be done for a selection the user confirmed"
      );
    }
  );
});

add_task(async function test_every_open_of_the_source_editor_is_counted() {
  await withFormPage(
    {
      findRelevantTabs: selectTheFirstTab,
      contextTabs: CONTEXT_TABS,
    },
    async ({ win, browser, actor }) => {
      await SimpleTest.promiseFocus(browser);
      await focusField(browser, "#email");

      // A change the user then cancels, so the fill runs on the model's picks
      // even though the editor was opened twice.
      const cancelled = await openTabSelector(win, browser, actor);
      await toggleSelectorTab(
        cancelled.tabSelector,
        cancelled.tabSelector.suggestedTabs[0].url
      );
      await toggleSelectorTab(
        cancelled.tabSelector,
        cancelled.tabSelector.otherTabs[0].url
      );
      await cancelTabSelector(cancelled);

      await acceptTabSelector(await openTabSelector(win, browser, actor));

      await actor.triggerAutofill();

      const [outcome] = await waitForEvents("formRelevantTabsOutcome", 1);

      Assert.equal(
        outcome.editor_opens,
        "2",
        "Expected editor_opens to count both opens in the flow"
      );
      Assert.equal(
        outcome.editor_result,
        "done",
        "Expected editor_result to come from the last open, which was accepted"
      );
      Assert.equal(
        outcome.tabs_kept,
        "1",
        "Expected the model's tab to count as kept, its deselection was cancelled"
      );
      Assert.equal(
        outcome.tabs_removed,
        "0",
        "Expected a deselection the user cancelled not to count as a removal"
      );
      Assert.equal(
        outcome.tabs_added,
        "0",
        "Expected a selection the user cancelled not to count as an addition"
      );
    }
  );
});

add_task(async function test_a_cancelled_source_editor_reports_the_cancel() {
  await withFormPage(
    {
      findRelevantTabs: selectTheFirstTab,
      contextTabs: ["https://example.net/"],
    },
    async ({ win, browser, actor }) => {
      await SimpleTest.promiseFocus(browser);
      await focusField(browser, "#email");

      await cancelTabSelector(await openTabSelector(win, browser, actor));

      await actor.triggerAutofill();

      const [outcome] = await waitForEvents("formRelevantTabsOutcome", 1);

      Assert.equal(
        outcome.editor_opens,
        "1",
        "Expected an editor the user cancelled to still count as an open"
      );
      Assert.equal(
        outcome.editor_result,
        "cancel",
        "Expected editor_result to be cancel for an editor the user backed out of"
      );
    }
  );
});

add_task(async function test_an_aborted_source_editor_is_not_a_cancel() {
  await withFormPage(
    {
      findRelevantTabs: selectTheFirstTab,
      contextTabs: ["https://example.net/"],
    },
    async ({ win, browser, actor }) => {
      await SimpleTest.promiseFocus(browser);
      await focusField(browser, "#email");

      const selector = await openTabSelector(win, browser, actor);

      // A tab opened while the editor is up is what takes it away: the list it
      // was offering is no longer the list the round would run with. Opened in
      // the background so the form stays the focused tab.
      const aborted = BrowserTestUtils.addTab(
        win.gBrowser,
        "https://example.com/"
      );
      await selector.edited;
      BrowserTestUtils.removeTab(aborted);

      await actor.triggerAutofill();

      const [outcome] = await waitForEvents("formRelevantTabsOutcome", 1);

      Assert.equal(
        outcome.editor_opens,
        "1",
        "Expected an editor the client closed to still count as an open"
      );
      Assert.equal(
        outcome.editor_result,
        "aborted",
        "Expected editor_result to be aborted for an editor the client closed, not cancel"
      );
    }
  );
});
