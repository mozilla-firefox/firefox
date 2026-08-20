/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const TEST_URL = "https://example.com/";
const MAIL_BUTTON_ID = "share-panel-mail";

add_task(async function test_mailButtonVisibility() {
  await BrowserTestUtils.withNewTab(TEST_URL, async () => {
    const panel = await openSharePanel(window);
    const mailButton = panel.querySelector(`#${MAIL_BUTTON_ID}`);

    if (AppConstants.platform === "linux") {
      ok(
        BrowserTestUtils.isVisible(mailButton),
        "Mail button is visible on Linux for a shareable page"
      );
    } else {
      ok(
        BrowserTestUtils.isHidden(mailButton),
        "Mail button is hidden on non-Linux platforms"
      );
    }

    await closeSharePanel(window);
  });
});

add_task(async function test_mailButtonHiddenForNonShareablePage() {
  await BrowserTestUtils.withNewTab("about:blank", async () => {
    const panel = await openSharePanel(window);
    const mailButton = panel.querySelector(`#${MAIL_BUTTON_ID}`);

    ok(
      BrowserTestUtils.isHidden(mailButton),
      "Mail button is hidden for a non-shareable page"
    );

    await closeSharePanel(window);
  });
});

add_task(async function test_sendEmail() {
  if (AppConstants.platform !== "linux") {
    info("Mail button is Linux-only; skipping launch test.");
    return;
  }

  let launchedURI = null;
  SharingUtils.testOnlyMockExternalProtocolService({
    loadURI(uri) {
      launchedURI = uri.spec;
    },
  });
  registerCleanupFunction(() =>
    SharingUtils.testOnlyMockExternalProtocolService(null)
  );

  await BrowserTestUtils.withNewTab(TEST_URL, async () => {
    const panel = await openSharePanel(window);
    const mailButton = panel.querySelector(`#${MAIL_BUTTON_ID}`);

    const sharePanel = document.getElementById("share-panel");
    const panelHidden = BrowserTestUtils.waitForEvent(
      sharePanel,
      "popuphidden"
    );

    EventUtils.synthesizeMouseAtCenter(mailButton, {});

    await panelHidden;

    ok(launchedURI, "A URL was launched to the OS");
    ok(launchedURI.startsWith("mailto:?"), "Launched a mailto URL");
    ok(
      launchedURI.includes(`body=${encodeURIComponent(TEST_URL)}`),
      "mailto body contains the page link"
    );
  });
});
