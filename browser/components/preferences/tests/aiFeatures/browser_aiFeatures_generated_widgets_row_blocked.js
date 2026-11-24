/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_generated_widgets.js", gTestPath).href,
  this
);

requestLongerTimeout(2);

describe("settings ai features", () => {
  describe("generated widgets AI control", () => {
    useSharedAiPane();
    beforeEach(setUpGeneratedWidgetsTest);
    afterEach(tearDownGeneratedWidgetsTest);

    describe("custom widgets row", () => {
      it("keeps the group and reads Blocked when AI is blocked", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        await enrolInLabsAndWait(doc);
        const setting = win.Preferences.getSetting(
          "aiControlGeneratedWidgetsSelect"
        );
        const select = doc.getElementById("aiControlGeneratedWidgetsSelect");
        assertVisibility(doc, true);
        try {
          const settingChanged = waitForSettingChange(setting);
          await blockAllAi(doc);
          await settingChanged;
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          assertVisibility(doc, true);
          Assert.equal(select.value, "blocked");
          Assert.ok(isEnrolled(), "Labs enrolment remains active");
        } finally {
          await unblockAllAi(doc);
        }
        await new Promise(resolve => win.requestAnimationFrame(resolve));
        assertVisibility(doc, true);
        Assert.equal(select.value, "available");
      });

      it("reads Blocked for a user who rejoined Labs while AI is blocked", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        try {
          await blockAllAi(doc);
          const settingChanged = waitForSettingChange(
            win.Preferences.getSetting("aiControlGeneratedWidgetsSelect")
          );
          await enrolInLabs();
          await settingChanged;
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          assertVisibility(doc, true);
          Assert.equal(
            doc.getElementById("aiControlGeneratedWidgetsSelect").value,
            "blocked"
          );
        } finally {
          await unblockAllAi(doc);
        }
      });

      it("lists custom widgets in the block dialog only for Labs users", async () => {
        const doc = paneDoc;
        const win = doc.documentGlobal;
        const toggle = doc.getElementById("aiControlDefaultToggle");
        const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
        await dialogEl.updateComplete;
        for (const enrolled of [false, true]) {
          if (enrolled) {
            await enrolInLabs();
          }
          toggle.buttonEl.scrollIntoView();
          await new Promise(resolve => win.requestAnimationFrame(resolve));
          const dialogShown = BrowserTestUtils.waitForEvent(
            dialogEl.dialog,
            "toggle"
          );
          EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {}, win);
          await dialogShown;
          Assert.ok(dialogEl.dialog.open, "Block confirmation is open");
          Assert.equal(
            !!dialogEl.renderRoot.querySelector(
              '[data-l10n-id="preferences-ai-controls-block-confirmation-custom-widgets"]'
            ),
            enrolled,
            "Custom widgets are listed only for Labs users"
          );
          const dialogClosed = BrowserTestUtils.waitForEvent(
            dialogEl.dialog,
            "toggle"
          );
          EventUtils.synthesizeMouseAtCenter(dialogEl.cancelButton, {}, win);
          await dialogClosed;
          Assert.ok(!dialogEl.dialog.open, "Block confirmation is closed");
        }
      });
    });
  });
});
