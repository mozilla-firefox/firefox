/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from file_ime_state_test_helper.js */

async function runTest(aTIPWrapper, aBrowser, aCommitAsync) {
  aTIPWrapper.commitCompositionAsynchronously = aCommitAsync;

  function waitFor(aWaitingNotification) {
    aTIPWrapper.clearFocusBlurNotifications();
    return new Promise(resolve => {
      aTIPWrapper.onIMEFocusBlur = aNotification => {
        if (aNotification != aWaitingNotification) {
          return;
        }
        aTIPWrapper.onIMEFocusBlur = null;
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      };
    });
  }

  // First composition test
  // Initialize the content and wait for focus.
  const waitForInitialFocus = waitFor("notify-focus");
  if (aBrowser) {
    await SpecialPowers.spawn(aBrowser, [], () => {
      content.window.focus();
      content.document.body.innerHTML = `<input id="input1">`;
      const input = content.document.getElementById("input1");
      input.addEventListener(
        "compositionupdate",
        () => {
          input.blur();
        },
        { once: true }
      );
      input.focus();
    });
  } else {
    window.focus();
    const input = document.createElement("input");
    input.setAttribute("id", "input1");
    document.body.appendChild(input);
    input.addEventListener(
      "compositionupdate",
      () => {
        input.blur();
      },
      { once: true }
    );
    input.focus();
  }
  await waitForInitialFocus;
  ok(aTIPWrapper.IMEHasFocus, "IME should have focus");

  // Then, start composition but the first `compositionupdate` will move focus
  // and causes a request to commit the composition.
  const waitForBlur = waitFor("notify-blur");
  ok(aTIPWrapper.typeJapaneseA(), "Typing Japanese A should succeed");
  await waitForBlur;

  function getInputValue() {
    if (aBrowser) {
      return SpecialPowers.spawn(aBrowser, [], () => {
        return content.document.getElementById("input1").value;
      });
    }
    return document.getElementById("input1").value;
  }
  is(
    await getInputValue(),
    "\u3042",
    "The focused <input> should have the commit string"
  );

  // Second composition test, the composing state should not have been broken
  // Wait for focus the editor again.
  const waitForAnotherFocus = waitFor("notify-focus");
  if (aBrowser) {
    await SpecialPowers.spawn(aBrowser, [], () => {
      content.document.getElementById("input1").focus();
    });
  } else {
    document.getElementById("input1").focus();
  }
  await waitForAnotherFocus;

  // Then, start another composition which should not cause asserting
  // the composition state and should cause a new composition in the focused
  // editor.
  ok(aTIPWrapper.typeJapaneseA(), "Typing Japanese A should succeed again");
  is(
    await getInputValue(),
    "\u3042\u3042",
    "The focused <input> should have the both composed string"
  );

  // Clean up.
  aTIPWrapper.ensureNoComposition();
}
