/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Verify that a click listener or an aria-label added to an element while it
 * isn't rendered is taken into account once it is rendered.
 */
addAccessibleTask(
  `
<span id="click" style="display: none;">click</span>
<span id="label" style="display: none;">label</span>
  `,
  async function testRenderedAfterChange(browser, accDoc) {
    await invokeContentTask(browser, [], () => {
      const doc = content.document;
      doc.getElementById("click").addEventListener("click", () => {});
      doc.getElementById("label").setAttribute("aria-label", "Labelled");
    });
    const onShow = waitForEvents([
      [EVENT_SHOW, "click"],
      [EVENT_SHOW, "label"],
    ]);
    await invokeContentTask(browser, [], () => {
      const doc = content.document;
      doc.getElementById("click").style.display = "inline";
      doc.getElementById("label").style.display = "inline";
    });
    await onShow;

    const click = findAccessibleChildByID(accDoc, "click");
    is(click.actionCount, 1, "Rendered element has its click action");
    is(click.getActionName(0), "click", "Action is click");
    const label = findAccessibleChildByID(accDoc, "label");
    is(label.name, "Labelled", "Rendered element has its aria-label");
  },
  { chrome: true, topLevel: true }
);

/**
 * Verify that a display: contents element gets an accessible when an
 * aria-label is added to it.
 */
addAccessibleTask(
  `<span id="label" style="display: contents;">label</span>`,
  async function testDisplayContentsAriaLabel(browser, accDoc) {
    ok(
      !findAccessibleChildByID(accDoc, "label"),
      "No accessible for display: contents span"
    );

    const onShow = waitForEvent(EVENT_SHOW, "label");
    await invokeContentTask(browser, [], () => {
      content.document
        .getElementById("label")
        .setAttribute("aria-label", "Labelled");
    });
    await onShow;

    const label = findAccessibleChildByID(accDoc, "label");
    is(label.name, "Labelled", "display: contents element has its aria-label");
  },
  { chrome: true, topLevel: true }
);
