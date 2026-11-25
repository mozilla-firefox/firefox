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
  await SidebarTestUtils.waitForTabstripOrientation(window, "vertical");
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

  const tabbox = window.document.getElementById("tabbrowser-tabbox");
  ok(
    tabbox.hasAttribute("sidebar-shown"),
    "tabbrowser-tabbox has sidebar-shown attribute"
  );

  await BrowserTestUtils.withNewTab({ gBrowser, url }, async browser => {
    // the newly opened tab should have focus
    await DOMFullscreenTestUtils.changeFullscreen(browser, true);

    is(window.document.fullscreenElement, browser, "Entered DOM fullscreen");
    ok(
      BrowserTestUtils.isHidden(sidebarMain),
      "Sidebar main is hidden in DOMFullscreen"
    );
    ok(
      !tabbox.hasAttribute("sidebar-shown"),
      "tabbrowser-tabbox does not have sidebar-shown attribute in DOMFullscreen"
    );

    await DOMFullscreenTestUtils.changeFullscreen(browser, false);
    ok(
      BrowserTestUtils.isVisible(sidebarMain),
      "Sidebar main becomes visible when we exit DOMFullscreen"
    );
    ok(sidebarMain.expanded, "Sidebar main is still expanded");
    ok(
      tabbox.hasAttribute("sidebar-shown"),
      "tabbrowser-tabbox has sidebar-shown attribute after exiting DOMFullscreen"
    );
  });
});

add_task(async function dom_fullscreen_has_no_extra_margins() {
  // Ensure that DOM fullscreen takes up the entire window with no unnecessary
  // gaps. (Bug 2057087)
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.visibility", "expand-on-hover"]],
  });
  const tabbox = document.getElementById("tabbrowser-tabbox");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "https://example.com/" },
    async browser => {
      await DOMFullscreenTestUtils.changeFullscreen(browser, true);
      const { marginInlineStart, marginInlineEnd } = getComputedStyle(tabbox);
      Assert.equal(
        parseInt(marginInlineStart),
        0,
        "Tabbox has no start margins while in DOM fullscreen."
      );
      Assert.equal(
        parseInt(marginInlineEnd),
        0,
        "Tabbox has no end margins while in DOM fullscreen."
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});
