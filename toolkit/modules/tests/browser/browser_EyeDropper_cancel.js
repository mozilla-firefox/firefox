/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_EyeDropper.js",
  this
);

add_task(async function cancel_with_abort() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    const abortController = new AbortController();
    const { pickedResult } = await openEyeDropper(browser, {
      signal: abortController.signal,
    });
    ok(getActiveEyeDropperForTests(window), "The eyedropper is opened");

    abortController.abort();
    is(await pickedResult, null, "Nothing was picked");
    ok(
      !getActiveEyeDropperForTests(window),
      "The eyedropper should be dismissed"
    );
  });
});

add_task(async function cancel_with_escape() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    await watchPageInput(browser);
    const { pickedResult } = await openEyeDropper(browser);
    ok(getActiveEyeDropperForTests(window), "The eyedropper is opened");

    EventUtils.synthesizeKey("KEY_Escape");

    is(await pickedResult, null, "Nothing was picked");
    ok(!getActiveEyeDropperForTests(window), "The eyedropper is dismissed");
    Assert.deepEqual(
      await readPageInput(browser),
      [],
      "The page did not see the key"
    );
  });
});

add_task(async function cancel_on_mousedown_over_browser_ui() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    const { pickedResult } = await openEyeDropper(browser);
    ok(getActiveEyeDropperForTests(window), "The eyedropper is opened");

    const navBar = document.getElementById("nav-bar");
    EventUtils.synthesizeMouseAtCenter(navBar, {});

    is(await pickedResult, null, "Nothing was picked");
    ok(!getActiveEyeDropperForTests(window), "The eyedropper is dismissed");
  });
});

add_task(async function cancel_when_switching_tabs() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    const { pickedResult } = await openEyeDropper(browser);
    ok(getActiveEyeDropperForTests(window), "The eyedropper is opened");

    const otherTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "about:blank"
    );

    is(await pickedResult, null, "Nothing was picked");
    ok(!getActiveEyeDropperForTests(window), "The eyedropper is dismissed");

    BrowserTestUtils.removeTab(otherTab);
  });
});

add_task(async function cancel_when_resizing_window() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.withNewTab(
    { gBrowser: win.gBrowser, url: PAGE_URL },
    async browser => {
      const { pickedResult } = await openEyeDropper(browser);
      ok(getActiveEyeDropperForTests(win), "The eyedropper is opened");

      const resized = BrowserTestUtils.waitForEvent(win, "resize");
      win.resizeTo(win.outerWidth - 100, win.outerHeight - 100);
      await resized;

      is(await pickedResult, null, "Nothing was picked");
      ok(!getActiveEyeDropperForTests(win), "The eyedropper is dismissed");
    }
  );
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function cancel_when_resizing_page_without_window() {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.withNewTab(
    { gBrowser: win.gBrowser, url: PAGE_URL },
    async browser => {
      const { pickedResult } = await openEyeDropper(browser);
      ok(getActiveEyeDropperForTests(win), "The eyedropper is opened");

      const { width, height } = browser.getBoundingClientRect();
      // The sidebar takes room from the page, but not from the window.
      await win.SidebarController.show("viewBookmarksSidebar");

      is(await pickedResult, null, "Nothing was picked");
      ok(!getActiveEyeDropperForTests(win), "The eyedropper is dismissed");
      const rect = browser.getBoundingClientRect();
      ok(rect.width != width || rect.height != height, "The page was resized");

      win.SidebarController.hide();
    }
  );
  await BrowserTestUtils.closeWindow(win);
});
