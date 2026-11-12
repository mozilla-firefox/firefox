/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// This test verifies that when in fullscreen mode, and a new tab is opened,
// fullscreen mode is exited and the url bar is focused.
add_task(async function test_fullscreen_display_none() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["full-screen-api.enabled", true],
      ["full-screen-api.allow-trusted-requests-only", false],
    ],
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.org/browser/browser/base/content/test/browser-fullscreen/fullscreen.html"
  );

  let fullScreenEntered = BrowserTestUtils.waitForEvent(
    document,
    "fullscreenchange",
    false,
    () => document.fullscreenElement
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    content.document.getElementById("request").click();
  });

  await fullScreenEntered;

  let fullScreenExited = BrowserTestUtils.waitForEvent(
    document,
    "fullscreenchange",
    false,
    () => !document.fullscreenElement
  );

  let focusPromise = BrowserTestUtils.waitForEvent(window, "focus");
  EventUtils.synthesizeKey("T", { accelKey: true });
  await focusPromise;

  is(
    document.activeElement,
    gURLBar.inputField,
    "url bar is focused after new tab opened"
  );

  await fullScreenExited;

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_fullscreen_cross_origin_iframe() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["full-screen-api.enabled", true],
      ["full-screen-api.allow-trusted-requests-only", false],
      ["full-screen-api.transition-duration.enter", "0 0"],
      ["full-screen-api.transition-duration.leave", "0 0"],
    ],
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/browser/browser/base/content/test/browser-fullscreen/fullscreen_frame.html"
  );

  let fullScreenEntered = BrowserTestUtils.waitForEvent(
    document,
    "fullscreenchange",
    false,
    () => document.fullscreenElement
  );

  await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
    let frame = content.document.getElementById("frameAllowed");
    frame.focus();
    await SpecialPowers.spawn(frame, [], () => {
      content.document.getElementById("request").click();
    });
  });

  await fullScreenEntered;

  let newTabPromise = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "TabSelect"
  );
  EventUtils.synthesizeKey("T", { accelKey: true });
  await newTabPromise;

  await TestUtils.waitForCondition(
    () => !document.documentElement.hasAttribute("inDOMFullscreen"),
    "Waiting for DOM fullscreen to be cleaned up"
  );
  await TestUtils.waitForTick();

  Assert.ok(gURLBar.focused, "url bar is focused after new tab opened");
  Assert.equal(
    document.activeElement,
    gURLBar.inputField,
    "url bar input field is the active element"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  BrowserTestUtils.removeTab(tab);
});
