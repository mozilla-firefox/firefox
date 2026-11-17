/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { FormHistory } = ChromeUtils.importESModule(
  "resource://gre/modules/FormHistory.sys.mjs"
);

const PREF = "browser.autocomplete.removeRecords.enabled";
const URL = `data:text/html,<input type="text" name="field1">`;
const LONG_VALUE = "W".repeat(200);
const MEDIUM_VALUE = "W".repeat(20);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({ set: [[PREF, true]] });
  registerCleanupFunction(() => FormHistory.update([{ op: "remove" }]));
});

async function withPopupFor(win, value, task) {
  await FormHistory.update([
    { op: "remove" },
    { op: "add", fieldname: "field1", value },
  ]);

  await BrowserTestUtils.withNewTab(
    { gBrowser: win.gBrowser, url: URL },
    async browser => {
      const { autoCompletePopup } = browser;
      await SpecialPowers.spawn(browser, [], async () => {
        content.document.querySelector("input").focus();
      });
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await TestUtils.waitForCondition(() => autoCompletePopup.popupOpen);

      const rowItem = autoCompletePopup.richlistbox.querySelector(
        ".autocomplete-row-item autocomplete-row-item"
      );
      await BrowserTestUtils.synthesizeKey("VK_DOWN", {}, browser);
      await TestUtils.waitForCondition(() => rowItem.selected, "row selected");

      const button = rowItem.shadowRoot.querySelector(
        "moz-button.secondary-action"
      );
      await TestUtils.waitForCondition(
        () => button.checkVisibility({ checkVisibilityCSS: true }),
        "the trash button is visible"
      );

      await task({
        win,
        autoCompletePopup,
        rowItem,
        label: rowItem.shadowRoot.querySelector(".label"),
        button,
      });

      await SpecialPowers.spawn(browser, [], async () => {
        content.document.querySelector("input").blur();
      });
    }
  );
}

function assertButtonInside(button, rowItem, label, popup) {
  const buttonRect = button.getBoundingClientRect();
  Assert.lessOrEqual(
    buttonRect.right,
    rowItem.getBoundingClientRect().right + 1,
    "The trash button ends inside the row"
  );
  Assert.lessOrEqual(
    buttonRect.right,
    popup.getBoundingClientRect().right + 1,
    "The trash button ends inside the panel"
  );
  Assert.greaterOrEqual(
    buttonRect.left,
    label.getBoundingClientRect().right - 1,
    "The trash button comes after the label"
  );
}

add_task(async function test_panel_grows_to_fit_medium_entry() {
  await withPopupFor(
    window,
    MEDIUM_VALUE,
    async ({ autoCompletePopup, label }) => {
      Assert.lessOrEqual(
        label.scrollWidth,
        label.clientWidth,
        "The medium label is not truncated"
      );
      Assert.greater(
        autoCompletePopup.richlistbox.getBoundingClientRect().width,
        250,
        "The panel grew past its minimum width"
      );
    }
  );
});

add_task(async function test_long_entry_is_truncated_in_capped_panel() {
  await withPopupFor(
    window,
    LONG_VALUE,
    async ({ autoCompletePopup, rowItem, label, button }) => {
      Assert.lessOrEqual(
        autoCompletePopup.richlistbox.getBoundingClientRect().width,
        500 + 1,
        "The list is no wider than its cap"
      );
      Assert.greater(
        label.scrollWidth,
        label.clientWidth,
        "The long label is truncated"
      );
      assertButtonInside(button, rowItem, label, autoCompletePopup);
    }
  );
});

add_task(async function test_long_entry_in_narrow_window() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const resized = BrowserTestUtils.waitForEvent(win, "resize");
  win.resizeTo(500, 600);
  await resized;
  await TestUtils.waitForCondition(
    () => win.innerWidth < 600,
    "the window is narrow"
  );

  await withPopupFor(
    win,
    LONG_VALUE,
    async ({ autoCompletePopup, rowItem, label, button }) => {
      // The popup includes the shadow margin on each side.
      const shadowMargin = parseFloat(
        win
          .getComputedStyle(autoCompletePopup)
          .getPropertyValue("--panel-box-shadow-margin")
      );
      Assert.lessOrEqual(
        autoCompletePopup.getBoundingClientRect().width - 2 * shadowMargin,
        win.innerWidth,
        "The panel is no wider than the window"
      );
      Assert.greater(
        label.scrollWidth,
        label.clientWidth,
        "The long label is truncated"
      );
      assertButtonInside(button, rowItem, label, autoCompletePopup);
    }
  );

  await BrowserTestUtils.closeWindow(win);
});
