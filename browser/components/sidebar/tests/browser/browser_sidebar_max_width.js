/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_customize_sidebar_actions() {
  SpecialPowers.pushPrefEnv({
    set: [[VERTICAL_TABS_PREF, true]],
  });
  await waitForTabstripOrientation("vertical");
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await waitForTabstripOrientation("vertical", win);

  const { document } = win;
  const sidebar = document.querySelector("sidebar-main");
  ok(sidebar, "Sidebar is shown.");
  await toggleSidebarPanel(win, "viewCustomizeSidebar");

  const initialViewportOuterWidth = win.outerWidth;
  const initialViewportOuterHeight = win.outerHeight;
  const sidebarBox = document.getElementById("sidebar-box");
  const initialMaxWidth = parseInt(
    window.getComputedStyle(sidebarBox).getPropertyValue("max-width")
  );
  info(`The initial initialMaxWidth is ${initialMaxWidth}`);
  info(`The initial innerWidth is ${win.innerWidth}`);

  // Resize sidebar panel initially
  sidebarBox.style.width = `${(
    0.75 * initialViewportOuterWidth -
    sidebar.offsetWidth
  ).toString()}px`;

  // Resize window with new width value
  const newWidth = 540;
  win.resizeTo(newWidth, initialViewportOuterHeight);
  await waitForRepaint();

  await TestUtils.waitForCondition(
    async () =>
      (await sidebar.updateComplete) &&
      parseInt(
        window.getComputedStyle(sidebarBox).getPropertyValue("max-width")
      ) < initialMaxWidth,
    `The window has been resized with outer width of ${newWidth}px, and the max width of the sidebar box is now ${window
      .getComputedStyle(sidebarBox)
      .getPropertyValue("max-width")}.`
  );
  let newMaxWidth = parseInt(
    window.getComputedStyle(sidebarBox).getPropertyValue("max-width")
  );
  Assert.less(
    newMaxWidth,
    initialMaxWidth,
    `The max width on the sidebar box is now ${newMaxWidth}`
  );
  Assert.less(
    0.75 * win.innerWidth - (sidebar.offsetWidth + newMaxWidth),
    10,
    "The max-width of the sidebar is approximately 75% of the viewport width."
  );

  SpecialPowers.popPrefEnv();
  await BrowserTestUtils.closeWindow(win);
});
