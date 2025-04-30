/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../mochitest/role.js */
/* import-globals-from ../../mochitest/states.js */
loadScripts(
  { name: "role.js", dir: MOCHITESTS_DIR },
  { name: "states.js", dir: MOCHITESTS_DIR }
);

/**
 * Test different labeling/titling schemes for text fields
 */
addAccessibleTask(
  `<label for="n1">Label</label> <input id="n1">
   <label for="n2">Two</label> <label for="n2">Labels</label> <input id="n2">
   <input aria-label="ARIA Label" id="n3">`,
  (browser, accDoc) => {
    let n1 = getNativeInterface(accDoc, "n1");
    let n1Label = n1.getAttributeValue("AXTitleUIElement");
    // XXX: In Safari the label is an AXText with an AXValue,
    // we emulate that so VoiceOver does not speak the label twice.
    is(n1Label.getAttributeValue("AXTitle"), "");

    let n2 = getNativeInterface(accDoc, "n2");
    is(n2.getAttributeValue("AXDescription"), "Two Labels");

    let n3 = getNativeInterface(accDoc, "n3");
    is(n3.getAttributeValue("AXDescription"), "ARIA Label");
  }
);

/**
 * Test to see that named groups get labels
 */
addAccessibleTask(
  `<fieldset id="fieldset"><legend>Fields</legend><input aria-label="hello"></fieldset>`,
  (browser, accDoc) => {
    let fieldset = getNativeInterface(accDoc, "fieldset");
    is(fieldset.getAttributeValue("AXDescription"), "Fields");
  }
);

/**
 * Test to see that list items don't get titled groups
 */
addAccessibleTask(
  `<ul style="list-style: none;"><li id="unstyled-item">Hello</li></ul>
   <ul><li id="styled-item">World</li></ul>`,
  (browser, accDoc) => {
    let unstyledItem = getNativeInterface(accDoc, "unstyled-item");
    is(unstyledItem.getAttributeValue("AXTitle"), "");

    let styledItem = getNativeInterface(accDoc, "unstyled-item");
    is(styledItem.getAttributeValue("AXTitle"), "");
  }
);

/**
 * Test that we fire a title changed notification
 */
addAccessibleTask(
  `<div id="elem" aria-label="Hello world"></div>`,
  async (browser, accDoc) => {
    let elem = getNativeInterface(accDoc, "elem");
    is(elem.getAttributeValue("AXTitle"), "Hello world");
    let evt = waitForMacEvent("AXTitleChanged", "elem");
    await SpecialPowers.spawn(browser, [], () => {
      content.document
        .getElementById("elem")
        .setAttribute("aria-label", "Hello universe");
    });
    await evt;
    is(elem.getAttributeValue("AXTitle"), "Hello universe");
  }
);

/**
 * Test articles supply only labels not titles
 */
addAccessibleTask(
  `<article id="article" aria-label="Hello world"></article>`,
  async (browser, accDoc) => {
    let article = getNativeInterface(accDoc, "article");
    is(article.getAttributeValue("AXDescription"), "Hello world");
    ok(!article.getAttributeValue("AXTitle"));
  }
);

/**
 * Test text and number inputs supply only labels not titles
 */
addAccessibleTask(
  `<label for="input">Your favorite number?</label><input type="text" name="input" value="11" id="input" aria-label="The best number you know of">`,
  async (browser, accDoc) => {
    let input = getNativeInterface(accDoc, "input");
    is(input.getAttributeValue("AXDescription"), "The best number you know of");
    ok(!input.getAttributeValue("AXTitle"));
    let evt = waitForEvent(EVENT_SHOW, "input");
    await SpecialPowers.spawn(browser, [], () => {
      content.document.getElementById("input").setAttribute("type", "number");
    });
    await evt;
    input = getNativeInterface(accDoc, "input");
    is(input.getAttributeValue("AXDescription"), "The best number you know of");
    ok(!input.getAttributeValue("AXTitle"));
  }
);
