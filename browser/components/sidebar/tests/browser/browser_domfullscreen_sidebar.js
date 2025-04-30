/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
const { DOMFullscreenTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/DOMFullscreenTestUtils.sys.mjs"
);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, true]],
  });
  DOMFullscreenTestUtils.init(this, window);
  await waitForTabstripOrientation("vertical");
});

add_task(async function test_dom_fullscreen() {
  // ensure the sidebar becomes hidden in dom fullscreen
  const url = "https://example.com/";
  const { sidebarMain } = SidebarController;
  await SidebarController.promiseInitialized;

  ok(
    BrowserTestUtils.isVisible(sidebarMain),
    "Sidebar main is initially visible"
  );

  sidebarMain.expanded = true;
  info("Waiting for sidebar main to be expanded");
  await BrowserTestUtils.waitForMutationCondition(
    sidebarMain,
    { attributes: true, attributeFilter: ["expanded"] },
    () => sidebarMain.expanded
  );
  ok(sidebarMain.expanded, "Sidebar main is expanded");

  await BrowserTestUtils.withNewTab({ gBrowser, url }, async browser => {
    // the newly opened tab should have focus
    await DOMFullscreenTestUtils.changeFullscreen(browser, true);

    is(window.document.fullscreenElement, browser, "Entered DOM fullscreen");
    ok(
      BrowserTestUtils.isHidden(sidebarMain),
      "Sidebar main is hidden in DOMFullscreen"
    );

    await DOMFullscreenTestUtils.changeFullscreen(browser, false);
    ok(
      BrowserTestUtils.isVisible(sidebarMain),
      "Sidebar main becomes visible when we exit DOMFullscreen"
    );
    ok(sidebarMain.expanded, "Sidebar main is still expanded");
  });
});
