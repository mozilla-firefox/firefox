/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const kPasteMenuPopupId = "clipboardReadPasteMenuPopup";
const kPasteMenuItemId = "clipboardReadPasteMenuItem";

const kBaseUrlForContent = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

const kPasteCommandTests = [
  { description: "Test paste command without editing" },
  {
    description: "Test paste command on <textarea>",
    setupFn: aBrowser => {
      return SpecialPowers.spawn(aBrowser, [], () => {
        const textarea = content.document.createElement("textarea");
        content.document.body.appendChild(textarea);
        textarea.focus();
      });
    },
    additionalCheckFunc: (aBrowser, aClipboardData) => {
      return SpecialPowers.spawn(aBrowser, [aClipboardData], aClipboardData => {
        const textarea = content.document.querySelector("textarea");
        is(textarea.value, aClipboardData, "check <textarea> value");
      });
    },
  },
  {
    description: "Test paste command on <div contenteditable=true>",
    setupFn: aBrowser => {
      return SpecialPowers.spawn(aBrowser, [], () => {
        const div = content.document.createElement("div");
        div.setAttribute("contenteditable", "true");
        content.document.body.appendChild(div);
        div.focus();
      });
    },
    additionalCheckFunc: (aBrowser, aClipboardData) => {
      return SpecialPowers.spawn(aBrowser, [aClipboardData], aClipboardData => {
        const div = content.document.querySelector("div");
        is(div.innerText, aClipboardData, "check contenteditable innerText");
      });
    },
  },
];

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/gfx/layers/apz/test/mochitest/apz_test_native_event_utils.js",
  this
);

function promiseWritingRandomTextToClipboard() {
  const clipboardText = "X" + Math.random();
  return navigator.clipboard.writeText(clipboardText).then(() => {
    return clipboardText;
  });
}

function promiseBrowserReflow() {
  return new Promise(resolve =>
    requestAnimationFrame(() => requestAnimationFrame(resolve))
  );
}

function waitForPasteMenuPopupEvent(aEventSuffix, aDocument = document) {
  // The element with id `kPasteMenuPopupId` is inserted dynamically, hence
  // calling `BrowserTestUtils.waitForEvent` instead of
  // `BrowserTestUtils.waitForPopupEvent`.
  return BrowserTestUtils.waitForEvent(
    aDocument,
    "popup" + aEventSuffix,
    false /* capture */,
    e => {
      return e.target.getAttribute("id") == kPasteMenuPopupId;
    }
  );
}

function promisePasteButtonIsShown(aDocument = document) {
  return waitForPasteMenuPopupEvent("shown", aDocument).then(async () => {
    ok(true, "Witnessed 'popupshown' event for 'Paste' button.");

    const pasteButton = aDocument.getElementById(kPasteMenuItemId);
    if (Services.prefs.getIntPref("security.dialog_enable_delay") > 0) {
      ok(
        pasteButton.disabled,
        "Paste button should be shown with disabled by default"
      );
    }
    await BrowserTestUtils.waitForMutationCondition(
      pasteButton,
      { attributeFilter: ["disabled"] },
      () => !pasteButton.disabled,
      { msg: "Wait for paste button enabled" }
    );

    return promiseBrowserReflow().then(() => {
      return coordinatesRelativeToScreen({
        target: pasteButton,
        offsetX: 0,
        offsetY: 0,
      });
    });
  });
}

function promisePasteButtonIsHidden() {
  return waitForPasteMenuPopupEvent("hidden").then(() => {
    ok(true, "Witnessed 'popuphidden' event for 'Paste' button.");
    return promiseBrowserReflow();
  });
}

function promiseClickPasteButton() {
  const pasteButton = document.getElementById(kPasteMenuItemId);
  const popup = document.getElementById(kPasteMenuPopupId);
  let promise = BrowserTestUtils.waitForEvent(pasteButton, "command");
  popup.activateItem(pasteButton);
  return promise;
}

function getMouseCoordsRelativeToScreenInDevicePixels() {
  let mouseXInCSSPixels = {};
  let mouseYInCSSPixels = {};
  window.windowUtils.getLastOverWindowPointerLocationInCSSPixels(
    mouseXInCSSPixels,
    mouseYInCSSPixels
  );

  return {
    x:
      (mouseXInCSSPixels.value + window.mozInnerScreenX) *
      window.devicePixelRatio,
    y:
      (mouseYInCSSPixels.value + window.mozInnerScreenY) *
      window.devicePixelRatio,
  };
}

function isCloselyLeftOnTopOf(aCoordsP1, aCoordsP2, aDelta = 10) {
  return (
    Math.abs(aCoordsP2.x - aCoordsP1.x) <= aDelta &&
    Math.abs(aCoordsP2.y - aCoordsP1.y) <= aDelta
  );
}

async function promiseDismissPasteButton() {
  // We intentionally turn off this a11y check, because the following click
  // is send on the <body> to dismiss the pending popup using an alternative way
  // of the popup dismissal, where the other way like `Esc` key is available,
  // therefore this test can be ignored.
  AccessibilityUtils.setEnv({
    mustHaveAccessibleRule: false,
  });
  // nsXULPopupManager rollup is handled in widget code, so we have to
  // synthesize native mouse events.
  await EventUtils.promiseNativeMouseEvent({
    type: "click",
    target: document.body,
    // Relies on the assumption that the center of chrome document doesn't
    // overlay with the paste button showed for clipboard readText request.
    atCenter: true,
  });
  // Move mouse away to avoid subsequence tests showing paste button in
  // thie dismissing location.
  await EventUtils.promiseNativeMouseEvent({
    type: "mousemove",
    target: document.body,
    offsetX: 100,
    offsetY: 100,
  });
  AccessibilityUtils.resetEnv();
}

// @param aBrowser browser object of the content tab.
// @param aContentElementId the ID of the element to be clicked.
async function promiseClickContentElement(aBrowser, aContentElementId) {
  // We intentionally turn off this a11y check, because the following click
  // is send on an arbitrary web content that is not expected to be tested
  // by itself with the browser mochitests, therefore this rule check shall
  // be ignored by a11y-checks suite.
  AccessibilityUtils.setEnv({
    mustHaveAccessibleRule: false,
  });
  let result = await SpecialPowers.spawn(
    aBrowser,
    [aContentElementId],
    async _contentElementId => {
      const contentElement = content.document.getElementById(_contentElementId);
      let promise = new Promise(resolve => {
        contentElement.addEventListener(
          "click",
          function (e) {
            resolve({ x: e.screenX, y: e.screenY });
          },
          { once: true }
        );
      });

      EventUtils.synthesizeMouseAtCenter(contentElement, {}, content.window);

      return promise;
    }
  );
  AccessibilityUtils.resetEnv();
  return result;
}

// @param aBrowser browser object of the content tab.
// @param aContentElementId the ID of the element to observe.
function promiseMutatedTextContentFromContentElement(
  aBrowser,
  aContentElementId
) {
  return SpecialPowers.spawn(
    aBrowser,
    [aContentElementId],
    async _contentElementId => {
      const contentElement = content.document.getElementById(_contentElementId);

      const promiseTextContentResult = new Promise(resolve => {
        const mutationObserver = new content.MutationObserver(
          (aMutationRecord, aMutationObserver) => {
            info("Observed mutation.");
            aMutationObserver.disconnect();
            resolve(contentElement.textContent);
          }
        );

        mutationObserver.observe(contentElement, {
          childList: true,
        });
      });

      return await promiseTextContentResult;
    }
  );
}
