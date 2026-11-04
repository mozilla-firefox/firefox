/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function testShowQRCode(actionBrowser) {
  await BrowserTestUtils.withNewTab(EXAMPLE_URL, async browser => {
    let manager = gBrowser.getTabDialogBox(browser).getTabDialogManager();

    let resolved = false;
    let actionPromise = SMATestUtils.executeAndValidateAction(
      { type: "SHOW_QR_CODE", data: { args: EXAMPLE_URL } },
      actionBrowser ?? browser
    ).then(() => (resolved = true));

    await TestUtils.waitForCondition(
      () => manager._dialogs.length,
      "Waiting for the QR code dialog to open"
    );
    let [dialog] = manager._dialogs;
    await dialog._dialogReady;
    is(
      dialog._openedURL,
      "chrome://browser/content/qrcode/qrcode-dialog.html",
      "QR code dialog opened over the tab"
    );
    let { url, qrCodeDataURI } = dialog.frameContentWindow.arguments[0];
    is(url, EXAMPLE_URL, "QR code dialog received the action URL");
    ok(
      qrCodeDataURI.startsWith("data:image/png"),
      "QR code image was generated"
    );

    ok(!resolved, "Action is pending while the QR code dialog is open");

    dialog.close();
    await actionPromise;
    ok(!manager._dialogs.length, "Action resolved after the dialog closed");
  });
}

add_task(async function test_SHOW_QR_CODE() {
  await testShowQRCode();
});

add_task(async function test_SHOW_QR_CODE_with_tabbrowser() {
  await testShowQRCode(gBrowser);
});
