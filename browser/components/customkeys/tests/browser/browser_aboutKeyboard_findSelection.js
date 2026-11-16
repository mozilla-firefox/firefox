/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { CustomKeys } = ChromeUtils.importESModule(
  "moz-src:///browser/components/customkeys/CustomKeys.sys.mjs"
);
const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

add_task(async function testFindSelectionShortcut() {
  try {
    await BrowserTestUtils.withNewTab("about:keyboard", async browser => {
      await SpecialPowers.spawn(browser, [isMac], async mac => {
        const doc = content.document;
        if (!doc.getElementById("table").firstElementChild) {
          await ContentTaskUtils.waitForEvent(
            content,
            "CustomKeysUpdate",
            false,
            null,
            true
          );
        }
        const row = doc.querySelector('.key[data-id="key_findSelection"]');
        if (!mac) {
          ok(!row, "Use Selection for Find is only listed on macOS");
          return;
        }
        ok(row, "Use Selection for Find is listed");
        is(
          row.closest("moz-card"),
          doc.querySelector('.key[data-id="key_find"]').closest("moz-card"),
          "Use Selection for Find is in the Edit category"
        );
        is(
          row.dataset.label,
          await doc.l10n.formatValue("customkeys-edit-find-selection"),
          "The shortcut has a localized label"
        );
        ok(!row.classList.contains("internal"), "The shortcut can be changed");
        is(
          row.querySelector(".currentShortcut").wrappedJSObject.value,
          "⌘E",
          "The default shortcut is preserved"
        );
      });
      if (!isMac) {
        return;
      }

      for (const accept of [false, true]) {
        await SpecialPowers.spawn(browser, [], async () => {
          const row = content.document.querySelector(
            '.key[data-id="key_find"]'
          );
          const card = row.closest("moz-card").wrappedJSObject;
          card.expanded = true;
          await card.updateComplete;
          const input = row.querySelector(".newKey").wrappedJSObject;
          const focused = ContentTaskUtils.waitForEvent(input, "focus");
          row.querySelector(".change").click();
          await focused;
          content.updated = ContentTaskUtils.waitForEvent(
            content,
            "CustomKeysUpdate",
            false,
            null,
            true
          );
        });
        const handled = PromptTestUtils.handleNextPrompt(
          window,
          { modalType: Services.prompt.MODAL_TYPE_CONTENT },
          { buttonNumClick: accept ? 0 : 1 }
        );
        EventUtils.synthesizeKey("e", { accelKey: true }, window);
        await handled;
        await SpecialPowers.spawn(browser, [], async () => {
          await content.updated;
        });
        const selectionKey = document.getElementById("key_findSelection");
        if (!accept) {
          is(selectionKey.getAttribute("key"), "E", "Cancel preserves Cmd+E");
          is(CustomKeys.getDefaultKey("key_find"), null, "Find is unchanged");
          is(
            CustomKeys.getDefaultKey("key_findSelection"),
            null,
            "Use Selection for Find is unchanged"
          );
        } else {
          ok(!selectionKey.hasAttribute("key"), "The competing key is cleared");
          is(
            document.getElementById("key_find").getAttribute("key"),
            "E",
            "Find now uses Cmd+E"
          );
        }
      }

      const loaded = BrowserTestUtils.browserLoaded(
        browser,
        false,
        "about:blank"
      );
      BrowserTestUtils.startLoadingURIString(browser, "about:blank");
      await loaded;
      await SimpleTest.promiseFocus(browser);
      const findbar = await gBrowser.getFindBar();
      const opened = BrowserTestUtils.waitForEvent(findbar, "findbaropen");
      EventUtils.synthesizeKey("e", { accelKey: true }, window);
      await opened;
      ok(!findbar.hidden, "Cmd+E executes the reassigned Find command");
      findbar.close();
    });
  } finally {
    CustomKeys.resetAll();
  }
  if (isMac) {
    is(
      document.getElementById("key_findSelection").getAttribute("key"),
      "E",
      "Restoring defaults restores Use Selection for Find"
    );
  }
});
