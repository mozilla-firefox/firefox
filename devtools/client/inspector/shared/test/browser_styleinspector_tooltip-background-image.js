/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that background-image URLs have image preview tooltips in the rule-view
// and computed-view
const TEST_URI = TEST_URL_ROOT + "doc_content_background_image.html";

add_task(async function () {
  await addTab(TEST_URI);
  const { inspector, view } = await openRuleView();

  await testCase({
    inspector,
    view,
    nodeSelector: "body",
    propertyName: "background-image",
    expectedUrl:
      "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHe",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".test-element",
    propertyName: "background",
    expectedUrl: "resource://content-accessible/media/audio.svg",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-stylesheet.relative",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
    // We're only doing extra assertions here as it can be quite long, and here we can cover
    // multiple cases (in the declaration value, in declaration expanded computed section,
    // in the computed panel)
    computedPropertyName: "background-image",
    checkUrlClick: true,
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-stylesheet.absolute",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-style-tag.absolute",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-style-tag.relative",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-constructed.absolute",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-constructed.relative",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-style-attribute.absolute",
    ruleSelector: "element",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  await testCase({
    inspector,
    view,
    nodeSelector: ".in-style-attribute.relative",
    ruleSelector: "element",
    propertyName: "background",
    expectedUrl:
      "https://example.com/browser/devtools/client/inspector/shared/test/test-image.png",
  });

  info(
    "Testing that image preview tooltips show even when there are " +
      "fields being edited"
  );
  await selectNode(".test-element", inspector);
  await testTooltipAppearsEvenInEditMode(view);
});

async function testCase({
  inspector,
  view,
  nodeSelector,
  ruleSelector = nodeSelector,
  propertyName,
  computedPropertyName,
  expectedUrl,
  checkUrlClick = false,
}) {
  await selectNode(nodeSelector, inspector);
  const property = await getRuleViewProperty(view, ruleSelector, propertyName, {
    wait: true,
  });
  let uriEl = property.valueSpan.querySelector(".theme-link");
  await testImagePreviewTooltip(
    view,
    uriEl,
    expectedUrl,
    `declaration value for "${propertyName}" in "${nodeSelector}"`
  );

  if (checkUrlClick) {
    await testOpenLink(
      uriEl,
      expectedUrl,
      `declaration value for "${propertyName}" in "${nodeSelector}"`
    );
  }

  if (!computedPropertyName) {
    return;
  }

  info(
    `Check the link in "${computedPropertyName}" in the expandable computed section in "${nodeSelector}"`
  );
  const ruleViewPropertyEl = property.valueSpan.closest(".ruleview-property");
  // Expand the computed section
  ruleViewPropertyEl.querySelector(".ruleview-expander").click();
  // Retrieve the actual property we want
  const computedItemEl = [
    ...ruleViewPropertyEl.querySelectorAll(".ruleview-computedlist li"),
  ].find(
    li =>
      li.querySelector(".ruleview-propertyname")?.textContent ===
      computedPropertyName
  );
  // And get the link that is displayed in it
  uriEl = computedItemEl.querySelector(".ruleview-propertyvalue .theme-link");
  await testImagePreviewTooltip(
    view,
    uriEl,
    expectedUrl,
    `"${computedPropertyName}" in "${nodeSelector}" computed section`
  );

  if (checkUrlClick) {
    await testOpenLink(
      uriEl,
      expectedUrl,
      `"${computedPropertyName}" in "${nodeSelector}" computed section`
    );
  }

  info("Switching over to the computed-view");
  const onComputedViewReady = inspector.once("computed-view-refreshed");
  const computedView = await selectComputedView(inspector);
  await onComputedViewReady;

  const computedProperty = getComputedViewProperty(
    computedView,
    computedPropertyName
  );
  uriEl = computedProperty.valueSpan.querySelector(".theme-link");

  await testImagePreviewTooltip(
    computedView,
    uriEl,
    expectedUrl,
    `"${computedPropertyName}" computed value for "${nodeSelector}"`
  );
  if (checkUrlClick) {
    await testOpenLink(
      uriEl,
      expectedUrl,
      `"${computedPropertyName}" computed value for "${nodeSelector}"`
    );
  }

  const computedPropertyEl = computedProperty.nameSpan.closest(
    ".computed-property-view"
  );

  // Expand the matched selectors section
  AccessibilityUtils.setEnv({
    // Focus is properly handled by the parent element, which will handle Enter to toggle
    // the item, so we can disable the accessibility check to avoid the test failure.
    focusableRule: false,
  });
  computedPropertyEl.querySelector(".computed-expandable").click();
  AccessibilityUtils.resetEnv();

  await waitFor(() => computedPropertyEl.querySelector(".rule-text"));
  // Retrieve the matched selector we want
  const computedRuleEl = [
    ...computedPropertyEl.querySelectorAll(".rule-text"),
  ].find(
    el =>
      el.querySelector(".computed-other-property-selector").textContent ===
      ruleSelector
  );

  // And the link inside it
  uriEl = computedRuleEl.querySelector(
    ".computed-other-property-value .theme-link"
  );

  info(
    `Check the link in "${computedPropertyName}" in the matched selectors section in "${nodeSelector}"`
  );
  await testImagePreviewTooltip(
    computedView,
    uriEl,
    expectedUrl,
    `"${computedPropertyName}" "${nodeSelector}" item in matched selectors section`
  );
  if (checkUrlClick) {
    await testOpenLink(
      uriEl,
      expectedUrl,
      `"${computedPropertyName}" "${nodeSelector}" item in matched selectors section`
    );
  }

  info("Switch back to the rule view");
  selectRuleView(inspector);
}

async function testImagePreviewTooltip(view, uriEl, expectedUrl, desc) {
  uriEl.scrollIntoView();

  if (expectedUrl.startsWith("data:")) {
    ok(
      uriEl.href.startsWith(expectedUrl),
      `Link has expected URL "${uriEl.href.substring(0, 100)}…" | ${desc}`
    );
  } else {
    is(uriEl.href, expectedUrl, `Link has expected URL | ${desc}`);
  }
  const previewTooltip = await assertShowPreviewTooltip(view, uriEl);

  const images = previewTooltip.panel.getElementsByTagName("img");
  is(images.length, 1, "Tooltip contains an image");
  const imgSrc = images[0].getAttribute("src");

  // If we're checking a data URL, only check the beginning of the URL so we don't spam
  // the output
  ok(
    imgSrc.startsWith("data:image"),
    `Tooltip contains a data-uri image as expected | ${desc}`
  );

  await assertTooltipHiddenOnMouseOut(previewTooltip, uriEl);
}

async function testOpenLink(uriEl, expectedUrl, desc) {
  info(
    `Check that middle-clicking on the link opens a new tab in the background | ${desc}`
  );
  let onTabOpen = BrowserTestUtils.waitForNewTab(
    gBrowser,
    expectedUrl,
    // waitForLoad
    true
  );
  uriEl.scrollIntoView();
  // uriEl can be a multi-line inline element, and since synthesizeMouse only get the
  // bounding rect, we might not click on the right place.
  // So here, use synthesizeMouseAtPoint and pass it the first quad position
  const uriElQuad = uriEl.getBoxQuads()[0];
  EventUtils.synthesizeMouseAtPoint(
    uriElQuad.p1.x + 2,
    uriElQuad.p1.y + 2,
    {
      button: 1,
    },
    uriEl.documentGlobal
  );
  let tab = await onTabOpen;
  is(
    tab.selected,
    false,
    `Tab was opened in the background with a middle click | ${desc}`
  );
  await removeTab(tab);

  info(
    `Check that ctrl/cmd clicking on the link opens a new tab in the background | ${desc}`
  );
  onTabOpen = BrowserTestUtils.waitForNewTab(
    gBrowser,
    expectedUrl,
    // waitForLoad
    true
  );
  EventUtils.synthesizeMouseAtPoint(
    uriElQuad.p1.x + 2,
    uriElQuad.p1.y + 2,
    {
      button: 1,
      [Services.appinfo.OS === "Darwin" ? "metaKey" : "ctrlKey"]: true,
    },
    uriEl.documentGlobal
  );
  tab = await onTabOpen;
  is(
    tab.selected,
    false,
    `Tab was opened in the background with ctrl/cmd + click | ${desc}`
  );
  await removeTab(tab);

  info(
    `Check that clicking on the link opens a new tab in the foreground | ${desc}`
  );
  onTabOpen = BrowserTestUtils.waitForNewTab(
    gBrowser,
    expectedUrl,
    // waitForLoad
    true
  );
  EventUtils.synthesizeMouseAtPoint(
    uriElQuad.p1.x + 2,
    uriElQuad.p1.y + 2,
    {},
    uriEl.documentGlobal
  );
  tab = await onTabOpen;
  is(
    tab.selected,
    true,
    `Tab was opened in the foreground when clicking on the link | ${desc}`
  );
  await removeTab(tab);
}

async function testTooltipAppearsEvenInEditMode(view) {
  info("Switching to edit mode in the rule view");
  const editor = await turnToEditMode(view);

  info("Now trying to show the preview tooltip");
  const { valueSpan } = getRuleViewProperty(
    view,
    ".test-element",
    "background"
  );
  const uriSpan = valueSpan.querySelector(".theme-link");

  const previewTooltip = await assertShowPreviewTooltip(view, uriSpan);

  is(
    view.styleDocument.activeElement,
    editor.input,
    "Tooltip was shown in edit mode, and inplace-editor still focused"
  );

  await assertTooltipHiddenOnMouseOut(previewTooltip, uriSpan);
}

function turnToEditMode(ruleView) {
  const brace = ruleView.styleDocument.querySelector(".ruleview-ruleclose");
  return focusEditableField(ruleView, brace);
}
