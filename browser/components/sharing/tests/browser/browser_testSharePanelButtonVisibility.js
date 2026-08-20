/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SHAREABLE_URL = "https://example.com";
const NON_SHAREABLE_URL = "about:robots";

add_task(async function test_shareablePage() {
  await BrowserTestUtils.withNewTab(SHAREABLE_URL, async () => {
    const panel = await openSharePanel(window);

    const buttonVisibility = [
      { id: "share-panel-copy-link", visible: true, name: "Copy link" },
      {
        id: "share-panel-os-share",
        visible: AppConstants.platform !== "linux",
        name: "OS share",
      },
      {
        id: "share-panel-mail",
        visible: AppConstants.platform === "linux",
        name: "Mail share",
      },
      { id: "share-panel-screenshot", visible: true, name: "Screenshots" },
      { id: "share-panel-qr-code", visible: true, name: "QR code" },
      {
        id: "share-panel-send-to-device",
        visible: true,
        name: "Send to device",
      },
    ];

    for (let { id, visible, name } of buttonVisibility) {
      let button = panel.querySelector(`#${id}`);
      if (visible) {
        ok(BrowserTestUtils.isVisible(button), `${name} button is visible`);
      } else {
        ok(BrowserTestUtils.isHidden(button), `${name} button is hidden`);
      }
    }

    await closeSharePanel(window);
  });
});

add_task(async function test_nonShareablePage() {
  await BrowserTestUtils.withNewTab(NON_SHAREABLE_URL, async () => {
    const panel = await openSharePanel(window);

    const buttonVisibility = [
      { id: "share-panel-copy-link", visible: false, name: "Copy link" },
      {
        id: "share-panel-os-share",
        visible: false,
        name: "OS share",
      },
      {
        id: "share-panel-mail",
        visible: false,
        name: "Mail share",
      },
      { id: "share-panel-screenshot", visible: true, name: "Screenshots" },
      { id: "share-panel-qr-code", visible: false, name: "QR code" },
      {
        id: "share-panel-send-to-device",
        visible: false,
        name: "Send to device",
      },
    ];

    for (let { id, visible, name } of buttonVisibility) {
      let button = panel.querySelector(`#${id}`);
      if (visible) {
        ok(BrowserTestUtils.isVisible(button), `${name} button is visible`);
      } else {
        ok(BrowserTestUtils.isHidden(button), `${name} button is hidden`);
      }
    }

    await closeSharePanel(window);
  });
});
