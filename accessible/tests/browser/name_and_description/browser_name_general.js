/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
requestLongerTimeout(2);

/* import-globals-from ../../mochitest/name.js */
/* import-globals-from ../../mochitest/attributes.js */
loadScripts(
  { name: "name.js", dir: MOCHITESTS_DIR },
  { name: "attributes.js", dir: MOCHITESTS_DIR }
);
/**
 * Test caching of the document title.
 */
addAccessibleTask(
  ``,
  async function (browser, docAcc) {
    let nameChanged = waitForEvent(EVENT_NAME_CHANGE, docAcc);
    await invokeContentTask(browser, [], () => {
      content.document.title = "new title";
    });
    await nameChanged;
    testName(docAcc, "new title");
  },
  { chrome: true, topLevel: true, iframe: true, remoteIframe: true }
);

/**
 * Test that the name is updated when the content of a hidden aria-labelledby
 * subtree changes.
 */
addAccessibleTask(
  `
<button id="button" aria-labelledby="label"></button>
<div id="label" hidden>a</div>
  `,
  async function (browser, docAcc) {
    const button = findAccessibleChildByID(docAcc, "button");
    testName(button, "a");
    info("Changing label textContent");
    let nameChanged = waitForEvent(EVENT_NAME_CHANGE, button);
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("label").textContent = "c";
    });
    await nameChanged;
    testName(button, "c");
    info("Change label text node's data");
    nameChanged = waitForEvent(EVENT_NAME_CHANGE, button);
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("label").firstChild.data = "d";
    });
    await nameChanged;
    testName(button, "d");
    info("Prepending text node to label");
    nameChanged = waitForEvent(EVENT_NAME_CHANGE, button);
    await invokeContentTask(browser, [], () => {
      content.document
        .getElementById("label")
        .prepend(content.document.createTextNode("b"));
    });
    await nameChanged;
    testName(button, "bd");
  },
  { chrome: true, topLevel: true, iframe: true, remoteIframe: true }
);

/**
 * Test that the name does not include aria-actions targets of itself.
 */
addAccessibleTask(
  `<div role="tablist">
    <div role="tab" id="tab" aria-actions="close pin">Title
      <button id="close">Close</button>
      <button id="pin">Pin</button>
    </div>
  </div>`,
  async function testAriaActionInSubtree(browser, docAcc) {
    const tab = findAccessibleChildByID(docAcc, "tab");
    testName(tab, "Title");

    let nameChanged = waitForEvent(EVENT_NAME_CHANGE, tab);
    invokeSetAttribute(browser, "tab", "aria-actions", "pin");
    await nameChanged;
    testName(tab, "Title Close");

    nameChanged = waitForEvent(EVENT_NAME_CHANGE, tab);
    invokeSetAttribute(browser, "pin", "id", "broken-pin");
    await nameChanged;
    testName(tab, "Title Close Pin");

    nameChanged = waitForEvent(EVENT_NAME_CHANGE, tab);
    invokeSetAttribute(browser, "tab", "aria-actions", "close pin");
    await nameChanged;
    testName(tab, "Title Pin");

    nameChanged = waitForEvent(EVENT_NAME_CHANGE, tab);
    invokeSetAttribute(browser, "broken-pin", "id", "pin");
    await nameChanged;
    testName(tab, "Title");
  },
  { chrome: true, topLevel: true }
);

/**
 * Test abbr name change notification
 */
addAccessibleTask(
  `<abbr id="abbr" title="JavaScript Object Notation">JSON</abbr>
   <abbr id="labelled-abbr" aria-label="YML"
         title="Yet Another Markup Language">YAML</abbr>`,
  async function testAbbrName(browser, docAcc) {
    const abbr = findAccessibleChildByID(docAcc, "abbr");
    testName(abbr, "JavaScript Object Notation");

    let nameChanged = waitForEvent(EVENT_NAME_CHANGE, abbr);
    invokeSetAttribute(browser, "abbr", "title", "Jason's Other Nettle");
    await nameChanged;
    testName(abbr, "Jason's Other Nettle");

    nameChanged = waitForEvent(EVENT_NAME_CHANGE, abbr);
    invokeSetAttribute(browser, "abbr", "title");
    await nameChanged;
    testName(abbr, null);

    const labelledAbbr = findAccessibleChildByID(docAcc, "labelled-abbr");
    testName(labelledAbbr, "YML");

    let events = waitForEvents({
      expected: [[EVENT_DESCRIPTION_CHANGE, labelledAbbr]],
      unexpected: [[EVENT_NAME_CHANGE, labelledAbbr]],
    });
    invokeSetAttribute(
      browser,
      "labelled-abbr",
      "title",
      "Your Another Marker Lye"
    );
    await events;
    testName(labelledAbbr, "YML");
    is(labelledAbbr.description, "Your Another Marker Lye");

    events = waitForEvents([
      [EVENT_NAME_CHANGE, labelledAbbr],
      // Bug 1997464 - When losing ARIA name, we lose the description that is used
      // now for the name.
      // [EVENT_DESCRIPTION_CHANGE, labelledAbbr],
    ]);
    invokeSetAttribute(browser, "labelled-abbr", "aria-label");
    await events;
    testName(labelledAbbr, "Your Another Marker Lye");
  },
  { chrome: true, topLevel: true }
);

/**
 * Test consistent doc name for remote and local docs.
 */
addAccessibleTask(
  `<iframe id="iframe"></iframe>`,
  async function testDocName(browser, docAcc) {
    const iframe = findAccessibleChildByID(docAcc, "iframe");
    info("Setting iframe src");
    // This iframe won't finish loading. Thus, it will get the stale state and
    // won't fire a document load complete event. We use the reorder event on
    // the iframe to know when the document has been created.
    let reordered = waitForEvent(
      EVENT_REORDER,
      event => event.accessible === iframe && event.accessible.firstChild
    );
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("iframe").src =
        `data:text/html,<html><body>hey</body></html>`;
    });
    let iframeDoc = (await reordered).accessible.firstChild;
    is(iframeDoc.name, null, "Doc should have 'null' name");
    testAbsentAttrs(iframeDoc, { "explicit-name": "true" });

    reordered = waitForEvent(
      EVENT_REORDER,
      event => event.accessible === iframe && event.accessible.firstChild
    );
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("iframe").src =
        `data:text/html,<html><title>hello</title><body>hey</body></html>`;
    });
    iframeDoc = (await reordered).accessible.firstChild;
    is(iframeDoc.name, "hello", "Doc should have name");
    testAttrs(iframeDoc, { "explicit-name": "true" }, true);
  },
  { topLevel: true }
);

/*
 * All test cases taken from https://www.w3.org/TR/accname-1.1/
 * These were especially called out to demonstrate edge cases.
 * (migrated from mochitest/name/test_ARIACore_examples.html)
 */
addAccessibleTask(
  `
  <!-- el1 should be labeled, el2 should not. -->
  <div id="el1" aria-labelledby="el3"></div>
  <div id="el2" aria-labelledby="el1"></div>
  <div id="el3"> hello </div>

  <!-- The buttons should be labeled by themselves and the referenced link -->
  <ul>
    <li>
      <a id="file_row1" href="./files/Documentation.pdf">Documentation.pdf</a>
      <span role="button" tabindex="0" id="del_row1" aria-label="Delete"
            aria-labelledby="del_row1 file_row1"></span>
    </li>
    <li>
      <a id="file_row2" href="./files/HolidayLetter.pdf">HolidayLetter.pdf</a>
      <span role="button" tabindex="0" id="del_row2" aria-label="Delete"
            aria-labelledby="del_row2 file_row2"></span>
    </li>
  </ul>

  <!-- Label from combined text and subtree -->
  <div id="chkbx" role="checkbox" aria-checked="false">Flash the screen
       <span role="textbox" aria-multiline="false"> 5 </span> times</div>

  <!-- Label with name from content should include table -->
  <input id="input_with_html_label" />
  <label for="input_with_html_label" id="label">
    <div>foo</div>
    <table><tr><td>bar</td></tr></table>
    <div>baz</div>
  </label>
`,
  async function testARIACoreExamples(browser, docAcc) {
    function testName_(id, expected, cached) {
      const acc = findAccessibleChildByID(docAcc, id);
      if (browser.isRemoteBrowser) {
        is(
          acc.cache.has("name"),
          cached,
          `Name should ${cached ? "" : "not "}be in cache for '${id}'`
        );
      }
      testName(acc, expected);
    }
    // Example 1 from section 4.3.1 under 2.B.
    // Element1 should get its name from the text in element3.
    // Element2 should not get its name from element1 because that already
    // gets its name from another element.
    testName_("el1", "hello", false);
    testName_("el2", null, false);

    // Example 2 from section 4.3.1 under 2.C.
    // The buttons should get their name from their labels and the links.
    testName_("del_row1", "Delete Documentation.pdf", true);
    testName_("del_row2", "Delete HolidayLetter.pdf", true);

    // Example 3 from section 4.3.1 under 2.F.
    // Name should be own content text plus the value of the input plus
    // more own inner text, separated by 1 space.
    testName_("chkbx", "Flash the screen 5 times", false);

    // Example 4 from section 4.3.1 under 2.F.
    // Name from content should include all the child nodes, including
    // table cells.
    testName_("input_with_html_label", "foo bar baz", false);
  },
  { topLevel: true, chrome: true }
);

/*
 * Test labels that use ARIA naming in different ways.
 */
addAccessibleTask(
  `<div id="label1">bye <a href="#" aria-labelledby="btn1">world</a></div>
   <button aria-labelledby="label1" id="btn1">hello</button>
   <div id="label2">bye <a href="#" aria-label="mars">world</a></div>
   <button aria-labelledby="label2" id="btn2">hello</button>
   <div id="label3" aria-label="mars">bye</div>
   <button aria-labelledby="label3" id="btn3">hello</button>`,
  async function testSomeInterestingLabels(browser, docAcc) {
    const btn1 = findAccessibleChildByID(docAcc, "btn1");
    const btn2 = findAccessibleChildByID(docAcc, "btn2");
    const btn3 = findAccessibleChildByID(docAcc, "btn3");
    // A button whose label subtree references the button
    testName(btn1, "bye world");
    // A button whose label subtree has an explicit aria-label
    testName(btn2, "bye mars");
    // A button whose label has an explicit aria-label of its own
    testName(btn3, "mars");
  },
  { topLevel: true, chrome: true }
);

addAccessibleTask(
  `<input id="input" type="text" title="title" placeholder="placeholder"></input>`,
  async function testInputPlaceHolder(browser, docAcc) {
    const input = findAccessibleChildByID(docAcc, "input");
    testName(input, "title");
  },
  { topLevel: true, chrome: true }
);

/**
 * Test the name of image map areas.
 */
addAccessibleTask(
  `
<map name="map">
  <area id="noName" href="https://example.com/">
  <area id="alt" href="https://example.com/" alt="alt">
  <area id="title" href="https://example.com/" title="title">
  <area id="altTitle" href="https://example.com/" alt="alt" title="title">
  <area id="noNameForm" href="https://example.com/" role="form">
</map>
<img src="https://example.com/a11y/accessible/tests/mochitest/moz.png" usemap="#map">
  `,
  async function testArea(browser, docAcc) {
    const noName = findAccessibleChildByID(docAcc, "noName");
    testName(noName, null);
    const alt = findAccessibleChildByID(docAcc, "alt");
    testName(alt, "alt");
    const title = findAccessibleChildByID(docAcc, "title");
    testName(title, "title");
    const altTitle = findAccessibleChildByID(docAcc, "altTitle");
    testName(altTitle, "alt");
    const noNameForm = findAccessibleChildByID(docAcc, "noNameForm");
    testName(noNameForm, null);
  },
  { chrome: true, topLevel: true }
);

/**
 * Test the name of an HTML select which becomes the document element. This
 * isn't something that anyone should actually do, but we still need to cope
 * with it.
 */
addAccessibleTask(
  `content`,
  async function testSelectAtRoot(browser, docAcc) {
    let changed = waitForEvent(EVENT_NAME_CHANGE, docAcc);
    await invokeContentTask(browser, [], () => {
      const a = content.document.createElement("select");
      a.role = "application";
      content.document.replaceChild(a, content.document.childNodes[1]);
      content.document.title = "after";
    });
    await changed;
    testName(docAcc, null);
  },
  { topLevel: true, chrome: true }
);
