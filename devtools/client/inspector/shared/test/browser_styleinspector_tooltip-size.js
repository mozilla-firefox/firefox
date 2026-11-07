/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Checking tooltips dimensions, to make sure their big enough to display their
// content

const TEST_URI = `
  <style type="text/css">
    div {
      width: 300px;height: 300px;border-radius: 50%;
      background: red url(resource://content-accessible/media/audio.svg);
    }
  </style>
  <div></div>
`;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();
  await selectNode("div", inspector);
  await testImageDimension(view);
  await testPickerDimension(view);
});

async function testImageDimension(ruleView) {
  info("Testing background-image tooltip dimensions");

  const tooltip = ruleView.tooltips.getTooltip("previewTooltip");
  const panel = tooltip.panel;
  const { valueSpan } = getRuleViewProperty(ruleView, "div", "background");
  const uriSpan = valueSpan.querySelector(".theme-link");

  // Make sure there is a hover tooltip for this property, this also will fill
  // the tooltip with its content
  const previewTooltip = await assertShowPreviewTooltip(ruleView, uriSpan);

  // Let's not test for a specific size, but instead let's make sure it's at
  // least as big as the image
  const imageRect = panel.querySelector("img").getBoundingClientRect();
  const panelRect = panel.getBoundingClientRect();

  Assert.greaterOrEqual(
    panelRect.width,
    imageRect.width,
    "The panel is wide enough to show the image"
  );
  Assert.greaterOrEqual(
    panelRect.height,
    imageRect.height,
    "The panel is high enough to show the image"
  );

  await assertTooltipHiddenOnMouseOut(previewTooltip, uriSpan);
}

async function testPickerDimension(ruleView) {
  info("Testing color-picker tooltip dimensions");

  const { valueSpan } = getRuleViewProperty(ruleView, "div", "background");
  const swatch = valueSpan.querySelector(".inspector-colorswatch");
  const cPicker = ruleView.tooltips.getTooltip("colorPicker");

  const onReady = cPicker.once("ready");
  swatch.click();
  await onReady;

  // The colorpicker spectrum's iframe has a fixed width height, so let's
  // make sure the tooltip is at least as big as that
  const spectrumRect = cPicker.spectrum.element.getBoundingClientRect();
  const panelRect = cPicker.tooltip.container.getBoundingClientRect();

  Assert.greaterOrEqual(
    panelRect.width,
    spectrumRect.width,
    "The panel is wide enough to show the picker"
  );
  Assert.greaterOrEqual(
    panelRect.height,
    spectrumRect.height,
    "The panel is high enough to show the picker"
  );

  const onHidden = cPicker.tooltip.once("hidden");
  const onModifications = ruleView.once("property-value-updated");
  cPicker.hide();
  await onHidden;
  await onModifications;
}
