/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Test that the eyedropper remains usable while scrolling and refreshes its screenshot
// once scrolling is complete.

const FIRST_COLOR = "#aa0000";
const SECOND_COLOR = "#00bb00";
const HTML = `
<!DOCTYPE html>
<style>
	body { margin: 0; }
	div { height: 1000px; }
</style>
<div style="background: ${FIRST_COLOR}"></div>
<div style="background: ${SECOND_COLOR}"></div>
<div style="background: #0000cc"></div>`;
const TEST_URI = `data:text/html;charset=utf-8,${encodeURIComponent(HTML)}`;

add_task(async function () {
  const { inspector, highlighterTestFront } =
    await openInspectorForURL(TEST_URI);
  const toggleButton = inspector.panelDoc.querySelector(
    "#inspector-eyedropper-toggle"
  );
  toggleButton.click();
  await TestUtils.waitForCondition(() =>
    highlighterTestFront.isEyeDropperVisible()
  );

  await checkEyeDropperColorAt(
    highlighterTestFront,
    50,
    50,
    FIRST_COLOR,
    "The eyedropper samples the first section before scrolling"
  );

  info("Scroll smoothly to the second color section");
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.scrollTo({ top: 1000, behavior: "smooth" });
  });

  await waitFor(async () => {
    const scrollingAttributeValue =
      await highlighterTestFront.getEyeDropperElementAttribute(
        "eye-dropper-root",
        "scrolling"
      );
    return scrollingAttributeValue !== null;
  });

  is(
    await highlighterTestFront.getEyeDropperElementAttribute(
      "eye-dropper-root",
      "scrolling"
    ),
    "",
    "The eyedropper enters its scrolling state"
  );

  const scrollY = async () =>
    SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => content.scrollY);
  await TestUtils.waitForCondition(async () => {
    const position = await scrollY();
    return position > 0 && position < 1000;
  }, "The page is midway through scrolling");

  Assert.greater(
    await scrollY(),
    0,
    "The page can scroll while the eyedropper is shown"
  );

  await TestUtils.waitForCondition(
    async () => (await scrollY()) >= 1000,
    "The page reaches the requested scroll position"
  );
  await TestUtils.waitForCondition(async () => {
    const scrolling = await highlighterTestFront.getEyeDropperElementAttribute(
      "eye-dropper-root",
      "scrolling"
    );
    const drawn = await highlighterTestFront.getEyeDropperElementAttribute(
      "eye-dropper-root",
      "drawn"
    );
    return scrolling === null && drawn === "true";
  }, "The screenshot is redrawn after scrolling ends");

  is(
    await scrollY(),
    1000,
    "The page finishes at the requested scroll position"
  );
  is(
    await highlighterTestFront.getEyeDropperElementAttribute(
      "eye-dropper-root",
      "scrolling"
    ),
    null,
    "The eyedropper leaves its scrolling state"
  );
  is(
    await highlighterTestFront.getEyeDropperColorValue(),
    SECOND_COLOR,
    "The refreshed screenshot shows the color at the scrolled position"
  );

  toggleButton.click();
  await TestUtils.waitForCondition(async () => {
    const visible = await highlighterTestFront.isEyeDropperVisible();
    return !visible;
  });
});
