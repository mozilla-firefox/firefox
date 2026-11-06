/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// In fullscreen, both views should move with their inputs when the macOS
// menubar shifts the toolbox down.

"use strict";

const { CustomizableUITestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/CustomizableUITestUtils.sys.mjs"
);

async function toggleFullscreen() {
  let onFullscreen = BrowserTestUtils.waitForEvent(window, "fullscreen");
  document.getElementById("View:FullScreen").doCommand();
  await onFullscreen;
}

function getGap(input) {
  let container = input.querySelector(".urlbar-input-container");
  return (
    input.panel.getBoundingClientRect().top -
    container.getBoundingClientRect().bottom
  );
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.fullscreen.autohide", false],
      ["browser.search.suggest.enabled", false],
      ["browser.search.widget.new", true],
    ],
  });
});

async function checkViewFollowsMenubarShift(
  input,
  testUtils,
  shouldFollowTransforms
) {
  info(`Testing ${input.id} with anchor transforms: ${shouldFollowTransforms}`);
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "layout.css.anchor-positioning.follows-transforms.enabled",
        shouldFollowTransforms,
      ],
    ],
  });
  await toggleFullscreen();
  try {
    await SimpleTest.promiseFocus(window);
    Assert.ok(!FullScreen.navToolboxHidden, "The toolbox is shown");

    await testUtils.promiseAutocompleteResultPopup({
      window,
      value: "example",
    });
    let gap = getGap(input);
    let top = input.panel.getBoundingClientRect().top;

    FullScreen.shiftMacToolbarDown(20);
    await new Promise(resolve => requestAnimationFrame(resolve));
    Assert.equal(
      Math.round(input.panel.getBoundingClientRect().top - top),
      20,
      `The ${input.id} view moves down with the shifted toolbox`
    );
    Assert.equal(
      Math.round(getGap(input)),
      Math.round(gap),
      `The ${input.id} view keeps its position relative to the input`
    );

    FullScreen.shiftMacToolbarDown(0);
    await new Promise(resolve => requestAnimationFrame(resolve));
    Assert.equal(
      Math.round(input.panel.getBoundingClientRect().top),
      Math.round(top),
      `The ${input.id} view moves back once the menubar is gone`
    );
  } finally {
    FullScreen.shiftMacToolbarDown(0);
    await testUtils.promisePopupClose(window);
    if (window.fullScreen) {
      await toggleFullscreen();
    }
    await SpecialPowers.popPrefEnv();
  }
}

add_task(async function urlbarViewFollowsMenubarShift() {
  await checkViewFollowsMenubarShift(gURLBar, UrlbarTestUtils, false);
  await checkViewFollowsMenubarShift(gURLBar, UrlbarTestUtils, true);
});

add_task(async function searchbarViewFollowsMenubarShift() {
  let cuiTestUtils = new CustomizableUITestUtils(window);
  let searchbar = await cuiTestUtils.addSearchBar();
  try {
    await checkViewFollowsMenubarShift(searchbar, SearchbarTestUtils, false);
    await checkViewFollowsMenubarShift(searchbar, SearchbarTestUtils, true);
  } finally {
    searchbar.view.queryContextCache.clear();
    cuiTestUtils.removeSearchBar();
  }
});
