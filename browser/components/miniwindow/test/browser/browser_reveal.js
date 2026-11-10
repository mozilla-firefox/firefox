/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Timer-driven auto-hide isn't exercised here to avoid flaky timing; instead
// this asserts the reveal wiring reacts to the events it listens for, toggling
// mini-window-revealed on #navigator-toolbox.
add_task(async function test_toolbox_reveal_wiring() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let miniWin = await popTabForTest(tab);
  let doc = miniWin.document;
  let toolbox = doc.getElementById("navigator-toolbox");

  Assert.ok(
    toolbox.classList.contains("mini-window-revealed"),
    "toolbox is revealed right after open"
  );

  // Notifications announce themselves with AlertActive, which bubbles out of
  // the notification box to the toolbox the reveal is wired to.
  toolbox.classList.remove("mini-window-revealed");
  doc
    .getElementById("notifications-toolbar")
    .dispatchEvent(new miniWin.CustomEvent("AlertActive", { bubbles: true }));
  Assert.ok(
    toolbox.classList.contains("mini-window-revealed"),
    "a notification appearing keeps the toolbox shown"
  );

  let popup = [...MiniWindowManager._miniwindows][0];
  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();

  removeTestTabs();
});

// Before the pointer moves in a new window, MousePosTracker reports (0, 0),
// inside the reveal strip. That must not hold the first-open reveal up, or
// switching away right after opening leaves the bar on screen for good.
add_task(async function test_first_open_reveal_hides_without_pointer() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let miniWin = await popTabForTest(tab);
  let toolbox = miniWin.document.getElementById("navigator-toolbox");

  Assert.ok(
    toolbox.classList.contains("mini-window-revealed"),
    "toolbox is revealed right after open"
  );
  await TestUtils.waitForCondition(
    () => !toolbox.classList.contains("mini-window-revealed"),
    "the first-open reveal hides again with no pointer movement",
    100,
    100
  );

  let popup = [...MiniWindowManager._miniwindows][0];
  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();

  removeTestTabs();
});

add_task(async function test_top_edge_reveals_toolbox() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let miniWin = await popTabForTest(tab);
  let toolbox = miniWin.document.getElementById("navigator-toolbox");

  // Park the pointer below the reveal strip first: MousePosTracker only
  // reports transitions, so entering has to be an actual change.
  await moveMouseTo(miniWin, 50, 100);
  toolbox.classList.remove("mini-window-revealed");

  await moveMouseTo(miniWin, 50, 2);
  await TestUtils.waitForCondition(
    () => toolbox.classList.contains("mini-window-revealed"),
    "pointer at the top edge reveals the toolbox"
  );

  let popup = [...MiniWindowManager._miniwindows][0];
  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();

  removeTestTabs();
});

const TALL_PAGE_URL =
  "https://example.com/document-builder.sjs?html=" +
  encodeURIComponent('<div style="height: 5000px"></div>');

/**
 * Scroll the page in `browser` to `y`, resolving once it has moved.
 *
 * @param {XULBrowserElement} browser
 * @param {number} y
 */
function scrollPageTo(browser, y) {
  return SpecialPowers.spawn(browser, [y], async targetY => {
    content.scrollTo(0, targetY);
    await ContentTaskUtils.waitForCondition(
      () => Math.abs(content.visualViewport.pageTop - targetY) < 1,
      "the page scrolled"
    );
  });
}

add_task(async function test_scrolling_reveals_and_hides_toolbox() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TALL_PAGE_URL
  );

  // A full-tab mini window, so no crop framing scrolls the page back.
  let miniWin = await popWholeTabForTest(tab);
  let popup = [...MiniWindowManager._miniwindows][0];
  let toolbox = miniWin.document.getElementById("navigator-toolbox");

  // Keep the pointer clear of the top edge, which reveals the bar on its own.
  await moveMouseTo(miniWin, 50, 100);

  await scrollPageTo(popup.browser, 500);
  await TestUtils.waitForCondition(
    () => !toolbox.classList.contains("mini-window-revealed"),
    "scrolling down hides the toolbox"
  );

  await scrollPageTo(popup.browser, 100);
  await TestUtils.waitForCondition(
    () => toolbox.classList.contains("mini-window-revealed"),
    "scrolling up reveals the toolbox"
  );

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});
