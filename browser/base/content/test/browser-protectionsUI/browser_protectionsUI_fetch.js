const URL =
  "http://mochi.test:8888/browser/browser/base/content/test/browser-protectionsUI/file_protectionsUI_fetch.html";

add_setup(async function () {
  await UrlClassifierTestUtils.addBuiltInTestTrackers();
  registerCleanupFunction(async () => {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });
});

add_task(async function test_fetch() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.trackingprotection.enabled", true]],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: URL },
    async function (newTabBrowser) {
      const win = newTabBrowser.documentGlobal;
      await openProtectionsPanel(false, win);
      let contentBlockingEvent = waitForContentBlockingEvent();
      await SpecialPowers.spawn(newTabBrowser, [], async function () {
        await content.wrappedJSObject
          .test_fetch()
          .then(() => Assert.ok(false, "should have denied the request"))
          .catch(e => Assert.ok(true, `Caught exception: ${e}`));
      });
      await contentBlockingEvent;

      const gProtectionsHandler = win.gProtectionsHandler;
      ok(gProtectionsHandler, "got CB object");

      ok(
        gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
        "has detected content blocking"
      );
      ok(
        gProtectionsHandler.iconBox.hasAttribute("active"),
        "icon box is active"
      );
      is(
        gProtectionsHandler._trackingProtectionIconTooltipLabel.getAttribute(
          "data-l10n-id"
        ),
        "tracking-protection-icon-active",
        "correct tooltip"
      );
    }
  );
});
