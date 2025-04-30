/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that the css transform highlighter is created only when asked and only one
// instance exists across the inspector

const TEST_URI = `
  <style type="text/css">
    body {
      transform: skew(16deg);
      color: blue;
    }
  </style>
  Test the css transform highlighter
`;

const { TYPES } = ChromeUtils.importESModule(
  "resource://devtools/shared/highlighters.mjs"
);
const TYPE = TYPES.TRANSFORM;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();

  let overlay = view.highlighters;

  ok(!overlay.highlighters[TYPE], "No highlighter exists in the rule-view");

  let onHighlighterShown = overlay.once("css-transform-highlighter-shown");
  const rulesViewTarget = getRuleViewProperty(
    view,
    "body",
    "transform"
  ).valueSpan;
  EventUtils.synthesizeMouse(
    rulesViewTarget,
    2,
    2,
    { type: "mousemove" },
    rulesViewTarget.ownerGlobal
  );
  const h = await onHighlighterShown;

  ok(
    overlay.highlighters[TYPE],
    "The highlighter has been created in the rule-view"
  );
  is(h, overlay.highlighters[TYPE], "The right highlighter has been created");

  info("Hide the highlighter");
  const onHighlighterHidden = overlay.once("css-transform-highlighter-hidden");
  EventUtils.synthesizeMouse(
    getRuleViewProperty(view, "body", "color").valueSpan,
    2,
    2,
    { type: "mousemove" },
    rulesViewTarget.ownerGlobal
  );
  await onHighlighterHidden;

  info("Show the highlighter again and check we got the same instance");
  onHighlighterShown = overlay.once("css-transform-highlighter-shown");
  EventUtils.synthesizeMouse(
    rulesViewTarget,
    2,
    2,
    { type: "mousemove" },
    rulesViewTarget.ownerGlobal
  );
  const h2 = await onHighlighterShown;

  is(
    h,
    h2,
    "The same instance of highlighter is returned everytime in the rule-view"
  );

  const onComputedViewReady = inspector.once("computed-view-refreshed");
  const cView = selectComputedView(inspector);
  await onComputedViewReady;
  overlay = cView.highlighters;

  ok(overlay.highlighters[TYPE], "The highlighter exists in the computed-view");

  onHighlighterShown = overlay.once("css-transform-highlighter-shown");
  const computedViewTarget = getComputedViewProperty(
    cView,
    "transform"
  ).valueSpan;
  EventUtils.synthesizeMouse(
    computedViewTarget,
    2,
    2,
    { type: "mousemove" },
    computedViewTarget.ownerGlobal
  );

  const h3 = await onHighlighterShown;
  is(
    h,
    h3,
    "The same instance of highlighter is returned everytime " +
      "in the computed-view"
  );
});
