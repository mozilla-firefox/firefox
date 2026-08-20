/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../file_ime_state_test_helper.js */

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/widget/tests/browser/file_ime_state_test_helper.js",
  this
);

add_task(async function () {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    const tipWrapper = new TIPWrapper(window);
    Assert.ok(
      tipWrapper.isAvailable(),
      "TextInputProcessor should've been initialized"
    );

    function waitFor(aWaitingNotification) {
      tipWrapper.clearFocusBlurNotifications();
      return new Promise(resolve => {
        tipWrapper.onIMEFocusBlur = aNotification => {
          if (aNotification != aWaitingNotification) {
            return;
          }
          tipWrapper.onIMEFocusBlur = null;
          resolve();
        };
      });
    }
    const waitForInitialFocus = waitFor("notify-focus");
    await SpecialPowers.spawn(browser, [], () => {
      content.window.focus();
      content.document.body.innerHTML = `<input id="input1" oninput="document.getElementById('input2').focus()"><input id="input2">`;
      content.document.getElementById("input1").focus();
    });
    await waitForInitialFocus;

    Assert.ok(tipWrapper.IMEHasFocus, "IME should have focus");

    const waitForAnotherFocus = waitFor("notify-focus");
    Assert.ok(tipWrapper.typeJapaneseA(), "Typing Japanese A should succeed");

    await waitForAnotherFocus;
    for (let i = 0; i < 30; i++) {
      await new Promise(resolve => window.requestAnimationFrame(resolve));
    }
    Assert.ok(tipWrapper.IMEHasFocus, "IME should keep having focus");
    const inputValues = await SpecialPowers.spawn(browser, [], () => {
      return [
        content.document.getElementById("input1").value,
        content.document.getElementById("input2").value,
      ];
    });
    Assert.equal(
      inputValues[0],
      "\u3042",
      "The focused <input> should have the committed string"
    );
    Assert.equal(
      inputValues[1],
      "",
      "The focused <input> should not have the committed string"
    );
  });
});
