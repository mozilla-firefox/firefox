"use strict";

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

// Regression test for bug 2070028: window.confirm() called from a
// browser_action popup panel should show the dialog on top of the
// browser_action popup panel browser.
add_task(async function browserAction_popup_confirm_dialog_is_visible() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_action: {
        default_popup: "popup.html",
        browser_style: false,
      },
    },
    files: {
      "popup.html": `<!DOCTYPE html><html>
        <head>
          <style>
            body {
              width: 250px;
              height: 200px;
            }
          </style>
        </head>
        <body>
          popup
          <script src="popup.js"></script>
        </body>
      </html>`,
      "popup.js": function () {
        browser.test.onMessage.addListener(msg => {
          if (msg !== "call-confirm") {
            browser.test.fail(`Got unexpected test message ${msg}`);
            return;
          }
          confirm("WebExtension popup panel confirm call");
          browser.test.sendMessage("call-confirm:done");
        });
        browser.test.sendMessage("popup-ready");
      },
    },
  });

  await extension.startup();

  await clickBrowserAction(extension);
  let popupBrowser = await awaitExtensionPanel(extension);
  await extension.awaitMessage("popup-ready");

  let promptPromise = PromptTestUtils.waitForPrompt(popupBrowser, {
    modalType: Services.prompt.MODAL_TYPE_CONTENT,
    promptType: "confirm",
  });

  extension.sendMessage("call-confirm");

  info("Wait for prompt to be shown");
  let dialog = await promptPromise;

  let dialogStack = window.gBrowser
    .getTabDialogBox(popupBrowser)
    .getContentDialogManager()._dialogStack;

  is(dialogStack.hidden, false, "Popup's dialog stack is not hidden");

  // The dialog stack should be anchored exactly over the popup browser it
  // belongs to. Without the anchor-positioning fix it instead falls back to
  // shrink-to-fit sizing and overflows well past the popup browser's own
  // (typically small) dimensions, getting clipped by the popup panel.
  let browserRect = popupBrowser.getBoundingClientRect();
  let dialogRect = dialogStack.getBoundingClientRect();
  is(
    dialogRect.width,
    browserRect.width,
    "Popup's dialog stack width matches the popup browser's width"
  );
  is(
    dialogRect.height,
    browserRect.height,
    "Popup's dialog stack height matches the popup browser's height"
  );

  // Make sure that the dialogbuttons are visible (by being inside the
  // bounding client rect related to the panel browser element).
  for (let [name, button] of [
    ["accept", dialog.ui.button0],
    ["cancel", dialog.ui.button1],
  ]) {
    let buttonRect = button.getBoundingClientRect();
    ok(
      buttonRect.left >= 0 &&
        buttonRect.top >= 0 &&
        buttonRect.right <= browserRect.width &&
        buttonRect.bottom <= browserRect.height,
      `Dialog's ${name} button is inside the visible popup panel area`
    );
  }

  info("Handle the confirm prompt");
  await PromptTestUtils.handlePrompt(dialog);

  info("Wait for the popup panel to have got confirm model result");
  await extension.awaitMessage("call-confirm:done");

  await closeBrowserAction(extension);
  await extension.unload();
});
