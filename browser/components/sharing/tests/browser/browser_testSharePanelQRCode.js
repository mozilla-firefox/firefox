/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL = "https://example.com/";

add_task(async function test_QRCode() {
  await BrowserTestUtils.withNewTab(TEST_URL, async browser => {
    const panel = await openSharePanel(window);
    const qrCodeButton = panel.querySelector("#share-panel-qr-code");

    ok(BrowserTestUtils.isVisible(qrCodeButton), "QR Code button is visible");

    const sharePanel = document.getElementById("share-panel");
    const panelHidden = BrowserTestUtils.waitForEvent(
      sharePanel,
      "popuphidden"
    );

    EventUtils.synthesizeMouseAtCenter(qrCodeButton, {});

    await panelHidden;
    is(sharePanel.state, "closed", "Share panel is closed");

    let dialogBox = gBrowser.getTabDialogBox(browser);
    let dialogManager = dialogBox.getTabDialogManager();

    await TestUtils.waitForCondition(
      () => dialogManager._dialogs.length,
      "Waiting for the QR code subdialog to open"
    );

    let dialog = dialogManager._dialogs[0];
    await dialog._dialogReady;

    let dialogDoc = dialog._frame.contentDocument;
    Assert.equal(
      dialogDoc.documentElement.id,
      "qrcode-dialog",
      "The QR code button opens the QR code dialog"
    );
  });
});
