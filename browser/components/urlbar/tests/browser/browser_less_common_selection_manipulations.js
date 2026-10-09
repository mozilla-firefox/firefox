/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests less common mouse/keyboard manipulations of the address bar input
 * field selection, for example:
 *  - Home/Del
 *  - Shift+Right/Left
 *  - Drag selection
 *  - Double-click on word
 *
 * All the tests set up some initial conditions, and check it. Then optionally
 * they can manipulate the selection further, and check the results again.
 * We want to ensure the final selection is the expected one, even if in the
 * future we change our trimming strategy for the input field value.
 *
 * Note: there's a few +-1 adjustments to text lengths that are apparently
 * necessary in --headless mode.
 */

const tests = [
  {
    description: "Test HOME starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      EventUtils.synthesizeKey("KEY_Home");
    },
    get modifiedSelection() {
      // Cursor must move to zero, regardless of any untrimming.
      return [0, 0];
    },
  },
  {
    description: "Test CTRL/META LEFT starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      if (AppConstants.platform == "macosx") {
        // Synthesized key events work differently from native ones, here
        // we simulate the native behavior.
        EventUtils.synthesizeKey("KEY_ArrowLeft", {
          type: "keydown",
          metaKey: true,
        });
        EventUtils.synthesizeKey("KEY_ArrowLeft", { type: "keyup" });
        EventUtils.synthesizeKey("KEY_Meta", { type: "keyup" });
      } else {
        EventUtils.synthesizeKey("KEY_ArrowLeft", { ctrlKey: true });
      }
    },
    get modifiedSelection() {
      return [0, 0];
    },
  },
  {
    description: "Test CTRL/ALT SHIFT LEFT starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      if (AppConstants.platform == "macosx") {
        // Synthesized key events work differently from native ones, here
        // we simulate the native behavior.
        EventUtils.synthesizeKey("KEY_ArrowLeft", {
          type: "keydown",
          altKey: true,
          shiftKey: true,
        });
        EventUtils.synthesizeKey("KEY_ArrowLeft", { type: "keyup" });
        EventUtils.synthesizeKey("KEY_Meta", { type: "keyup" });
      } else {
        EventUtils.synthesizeKey("KEY_ArrowLeft", {
          ctrlKey: true,
          shiftKey: true,
        });
      }
    },
    get modifiedSelection() {
      return [0, gURLBar.value.lastIndexOf(".") + 1];
    },
  },
  {
    description: "Test CTRL A starting from full selection",
    skipIf() {
      return AppConstants.platform != "macosx";
    },
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      EventUtils.synthesizeKey("A", { ctrlKey: true });
    },
    get modifiedSelection() {
      return [0, 0];
    },
  },
  {
    description: "Test END starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      if (AppConstants.platform == "macosx") {
        EventUtils.synthesizeKey("KEY_ArrowRight", { metaKey: true });
      } else {
        EventUtils.synthesizeKey("KEY_End", {});
      }
    },
    get modifiedSelection() {
      return [gURLBar.value.length, gURLBar.value.length];
    },
  },
  {
    description: "Test SHIFT+LEFT starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      EventUtils.synthesizeKey("KEY_ArrowLeft", { shiftKey: true });
    },
    get modifiedSelection() {
      return [0, gURLBar.value.length - 1];
    },
  },
  {
    description: "Test SHIFT+RIGHT starting from full selection",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      EventUtils.synthesizeKey("KEY_ArrowRight", { shiftKey: true });
    },
    get modifiedSelection() {
      return [0, gURLBar.value.length];
    },
  },
  {
    description: "Test Drag Selection from the first character - Partial host",
    async openPanel() {
      // Select partial string that doesn't complete to a URL.
      this._expectedSelectedText = gURLBar.value.substring(0, 5);
      await UrlbarTestUtils.selectWithMouseDrag(
        getTextWidth(gURLBar.value[0]) / 2 - 1,
        getTextWidth(gURLBar.value.substring(0, 5)) + 1,
        window
      );
    },
    get selection() {
      // When untrimming is enabled, the behavior differs depending on whether
      // the selected text can generate a valid URL, so there may be an offset.
      let startOffset = UrlbarPrefs.getScotchBonnetPref(
        "untrimOnUserInteraction.featureGate"
      )
        ? gURLBar.value.indexOf(this._expectedSelectedText)
        : 0;
      return [startOffset, startOffset + this._expectedSelectedText.length];
    },
  },
  {
    description: "Test Drag Selection from the first character - Full host",
    async openPanel() {
      // Select partial string that completes to a URL.
      let uri = Services.io.newURI(gURLBar._untrimmedValue);
      let endOfHost =
        gURLBar.value.indexOf(uri.displayHost) + uri.displayHost.length;
      this._expectedSelectedText = gURLBar.value.substring(0, endOfHost);
      await UrlbarTestUtils.selectWithMouseDrag(
        getTextWidth(gURLBar.value[0]) / 2 - 1,
        getTextWidth(this._expectedSelectedText),
        window
      );
    },
    get selection() {
      return [
        0,
        gURLBar.value.indexOf(this._expectedSelectedText) +
          this._expectedSelectedText.length,
      ];
    },
  },
  {
    description: "Test Drag Selection from the last character",
    async openPanel() {
      this._expectedSelectedText = gURLBar.value.substring(-5);
      await UrlbarTestUtils.selectWithMouseDrag(
        getTextWidth(gURLBar.value) + 1,
        getTextWidth(this._expectedSelectedText),
        window
      );
    },
    get selection() {
      return [
        gURLBar.value.indexOf(this._expectedSelectedText),
        gURLBar.value.length,
      ];
    },
  },
  {
    description: "Test Drag Selection in the middle of the string",
    async openPanel() {
      this._expectedSelectedText = gURLBar.value.substring(5, 10);
      await UrlbarTestUtils.selectWithMouseDrag(
        getTextWidth(gURLBar.value.substring(0, 5)) + 1,
        getTextWidth(gURLBar.value.substring(0, 10)) + 1,
        window
      );
    },
    get selection() {
      let start = gURLBar.value.indexOf(this._expectedSelectedText);
      return [start, start + this._expectedSelectedText.length];
    },
  },
  {
    description: "Test Double-click on word",
    async openPanel() {
      let wordBoundaryIndex = gURLBar.value.search(/\btest/);
      this._expectedSelectedText = "test";
      await UrlbarTestUtils.selectWithDoubleClick(
        getTextWidth(gURLBar.value.substring(0, wordBoundaryIndex)),
        window
      );
    },
    get selection() {
      let start = gURLBar.value.indexOf(this._expectedSelectedText);
      return [start, start + this._expectedSelectedText.length];
    },
  },
  {
    description: "Click at the right of the text",
    openPanel() {
      EventUtils.synthesizeKey("l", { accelKey: true });
    },
    get selection() {
      return [0, gURLBar.value.length];
    },
    manipulate() {
      let rect = gURLBar.inputField.getBoundingClientRect();
      EventUtils.synthesizeMouse(
        gURLBar.inputField,
        getTextWidth(gURLBar.value) + 10,
        rect.height / 2,
        {}
      );
    },
    get modifiedSelection() {
      return [gURLBar.value.length, gURLBar.value.length];
    },
  },
];

add_setup(async function () {
  // Tests get no default Top Sites, and the view is opened on an empty
  // string here, which would otherwise leave it closed with no results.
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });

  gURLBar.inputField.style.font = "14px monospace";
  registerCleanupFunction(() => {
    gURLBar.inputField.style.font = null;
  });
});

add_task(async function https() {
  await doTest("https://example.com/test/some/page.htm");
});

add_task(async function http() {
  // eslint-disable-next-line sdl/no-insecure-url
  await doTest("http://example.com/test/other/page.htm");
});

async function doTest(url) {
  await BrowserTestUtils.withNewTab(url, async () => {
    for (let test of tests) {
      if (test.skipIf?.()) {
        continue;
      }
      gURLBar.blur();
      info(test.description);
      await UrlbarTestUtils.promisePopupOpen(window, async () => {
        await test.openPanel();
      });
      info(
        `Selected text is <${gURLBar.value.substring(
          gURLBar.selectionStart,
          gURLBar.selectionEnd
        )}>`
      );
      Assert.deepEqual(
        test.selection,
        [gURLBar.selectionStart, gURLBar.selectionEnd],
        "Check initial selection"
      );

      if (test.manipulate) {
        await test.manipulate();
        info(
          `Selected text after manipulation is <${gURLBar.value.substring(
            gURLBar.selectionStart,
            gURLBar.selectionEnd
          )}>`
        );
        Assert.deepEqual(
          test.modifiedSelection,
          [gURLBar.selectionStart, gURLBar.selectionEnd],
          "Check selection after manipulation"
        );
      }
    }
  });
}

function getTextWidth(inputText) {
  let span = document.createElement("span");
  document.documentElement.appendChild(span);
  span.style.all = "initial";
  span.style.font = window
    .getComputedStyle(gURLBar.inputField)
    .getPropertyValue("font");
  span.textContent = inputText;
  let result = span.offsetWidth;
  span.remove();
  return result;
}
