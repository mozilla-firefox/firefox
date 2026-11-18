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
 * Test different HTML elements for their roles and subroles
 */
function testRoleAndSubRole(accDoc, id, axRole, axSubRole, axRoleDescription) {
  let el = getNativeInterface(accDoc, id);
  if (axRole) {
    is(
      el.getAttributeValue("AXRole"),
      axRole,
      "AXRole for " + id + " is " + axRole
    );
  }
  if (axSubRole) {
    is(
      el.getAttributeValue("AXSubrole"),
      axSubRole,
      "Subrole for " + id + " is " + axSubRole
    );
  }
  if (axRoleDescription) {
    is(
      el.getAttributeValue("AXRoleDescription"),
      axRoleDescription,
      "Subrole for " + id + " is " + axRoleDescription
    );
  }
}

addAccessibleTask(
  `
  <!-- WAI-ARIA landmark roles -->
  <div id="application" role="application"></div>
  <div id="banner" role="banner"></div>
  <div id="complementary" role="complementary"></div>
  <div id="contentinfo" role="contentinfo"></div>
  <div id="form" role="form"></div>
  <div id="form_label" aria-label="form" role="form"></div>
  <div id="main" role="main"></div>
  <div id="navigation" role="navigation"></div>
  <div id="search" role="search"></div>
  <div id="searchbox" role="searchbox"></div>

  <!-- DPub landmarks -->
  <div id="dPubNavigation" role="doc-index"></div>
  <div id="dPubRegion" role="doc-introduction"></div>

  <!-- Other WAI-ARIA widget roles -->
  <div id="alert" role="alert"></div>
  <div id="alertdialog" role="alertdialog"></div>
  <div id="article" role="article"></div>
  <div id="code" role="code"></div>
  <div id="dialog" role="dialog"></div>
  <div id="ariaDocument" role="document"></div>
  <div id="log" role="log"></div>
  <div id="marquee" role="marquee"></div>
  <div id="ariaMath" role="math"></div>
  <div id="note" role="note"></div>
  <div id="ariaRegion" aria-label="region" role="region"></div>
  <div id="ariaStatus" role="status"></div>
  <div id="switch" role="switch"></div>
  <div id="timer" role="timer"></div>
  <div id="tooltip" role="tooltip"></div>
  <div role="menu"><input type="radio" role="menuitemradio" id="menuitemradio"></div>
  <div role="menu"><input type="checkbox" role="menuitemcheckbox" id="menuitemcheckbox"></div>
  <input type="datetime-local" id="datetime">

  <!-- text entries -->
  <div id="textbox_multiline" role="textbox" aria-multiline="true"></div>
  <div id="textbox_singleline" role="textbox" aria-multiline="false"></div>
  <textarea id="textArea"></textarea>
  <input id="textInput">

  <!-- True HTML5 search box -->
  <input type="search" id="htmlSearch" />

  <!-- Password input -->
  <input type="password" id="password" />

  <!-- A button morphed into a toggle via ARIA -->
  <button id="toggle" aria-pressed="false"></button>

  <!-- A button with a 'banana' role description -->
  <button id="banana" aria-roledescription="banana"></button>

  <!-- Other elements -->
  <del id="deletion">Deleted text</del>
  <dl id="dl"><dt id="dt">term</dt><dd id="dd">definition</dd></dl>
  <hr id="hr" />
  <ins id="insertion">Inserted text</ins>
  <meter id="meter" min="0" max="100" value="24">meter text here</meter>
  <sub id="sub">sub text here</sub>
  <sup id="sup">sup text here</sup>

  <!-- Some SVG stuff -->
  <svg xmlns="http://www.w3.org/2000/svg" version="1.1" id="svg"
       xmlns:xlink="http://www.w3.org/1999/xlink">
    <g id="g">
      <title>g</title>
    </g>
    <rect width="300" height="100" id="rect"
          style="fill:rgb(0,0,255);stroke-width:1;stroke:rgb(0,0,0)">
      <title>rect</title>
    </rect>
    <circle cx="100" cy="50" r="40" stroke="black" id="circle"
            stroke-width="2" fill="red">
      <title>circle</title>
    </circle>
    <ellipse cx="300" cy="80" rx="100" ry="50" id="ellipse"
             style="fill:yellow;stroke:purple;stroke-width:2">
      <title>ellipse</title>
    </ellipse>
    <line x1="0" y1="0" x2="200" y2="200" id="line"
          style="stroke:rgb(255,0,0);stroke-width:2">
      <title>line</title>
    </line>
    <polygon points="200,10 250,190 160,210" id="polygon"
             style="fill:lime;stroke:purple;stroke-width:1">
      <title>polygon</title>
    </polygon>
    <polyline points="20,20 40,25 60,40 80,120 120,140 200,180" id="polyline"
              style="fill:none;stroke:black;stroke-width:3" >
      <title>polyline</title>
    </polyline>
    <path d="M150 0 L75 200 L225 200 Z" id="path">
      <title>path</title>
    </path>
    <image x1="25" y1="80" width="50" height="20" id="image"
           xlink:href="../moz.png">
      <title>image</title>
    </image>
  </svg>`,
  (browser, accDoc) => {
    // WAI-ARIA landmark subroles, regardless of AXRole
    testRoleAndSubRole(accDoc, "application", null, "AXLandmarkApplication");
    testRoleAndSubRole(accDoc, "banner", null, "AXLandmarkBanner");
    testRoleAndSubRole(
      accDoc,
      "complementary",
      null,
      "AXLandmarkComplementary"
    );
    testRoleAndSubRole(accDoc, "contentinfo", null, "AXLandmarkContentInfo");
    testRoleAndSubRole(accDoc, "form", null, "AXApplicationGroup");
    testRoleAndSubRole(accDoc, "form_label", null, "AXLandmarkForm");
    testRoleAndSubRole(accDoc, "main", null, "AXLandmarkMain");
    testRoleAndSubRole(accDoc, "navigation", null, "AXLandmarkNavigation");
    testRoleAndSubRole(accDoc, "search", null, "AXLandmarkSearch");
    testRoleAndSubRole(accDoc, "searchbox", null, "AXSearchField");

    // DPub roles map into two categories, sample one of each
    testRoleAndSubRole(
      accDoc,
      "dPubNavigation",
      "AXGroup",
      "AXLandmarkNavigation"
    );
    testRoleAndSubRole(accDoc, "dPubRegion", "AXGroup", "AXLandmarkRegion");

    // ARIA widget roles
    testRoleAndSubRole(accDoc, "alert", null, "AXApplicationAlert");
    testRoleAndSubRole(
      accDoc,
      "alertdialog",
      "AXGroup",
      "AXApplicationAlertDialog",
      "alert dialog"
    );
    testRoleAndSubRole(accDoc, "article", null, "AXDocumentArticle");
    testRoleAndSubRole(accDoc, "code", "AXGroup", "AXCodeStyleGroup");
    testRoleAndSubRole(accDoc, "dialog", null, "AXApplicationDialog", "dialog");
    testRoleAndSubRole(accDoc, "ariaDocument", null, "AXDocument");
    testRoleAndSubRole(accDoc, "log", null, "AXApplicationLog");
    testRoleAndSubRole(accDoc, "marquee", null, "AXApplicationMarquee");
    testRoleAndSubRole(accDoc, "ariaMath", null, "AXDocumentMath");
    testRoleAndSubRole(accDoc, "note", null, "AXDocumentNote");
    testRoleAndSubRole(accDoc, "ariaRegion", null, "AXLandmarkRegion");
    testRoleAndSubRole(accDoc, "ariaStatus", "AXGroup", "AXApplicationStatus");
    testRoleAndSubRole(accDoc, "switch", "AXCheckBox", "AXSwitch");
    testRoleAndSubRole(accDoc, "timer", null, "AXApplicationTimer");
    testRoleAndSubRole(accDoc, "tooltip", "AXGroup", "AXUserInterfaceTooltip");
    testRoleAndSubRole(accDoc, "menuitemradio", "AXMenuItem", null);
    testRoleAndSubRole(accDoc, "menuitemcheckbox", "AXMenuItem", null);
    testRoleAndSubRole(accDoc, "datetime", "AXGroup", null);
    // XXX for datetime elements, we spoof the role via the title, since
    // providing the correct role results in the internal elements being
    // unreachable by VO
    is(
      getNativeInterface(accDoc, "datetime").getAttributeValue("AXTitle"),
      "date field"
    );

    // Text boxes
    testRoleAndSubRole(accDoc, "textbox_multiline", "AXTextArea");
    testRoleAndSubRole(accDoc, "textbox_singleline", "AXTextField");
    testRoleAndSubRole(accDoc, "textArea", "AXTextArea");
    testRoleAndSubRole(accDoc, "textInput", "AXTextField");

    // True HTML5 search field
    testRoleAndSubRole(accDoc, "htmlSearch", "AXTextField", "AXSearchField");

    // Password input
    testRoleAndSubRole(accDoc, "password", "AXTextField", "AXSecureTextField");

    // A button morphed into a toggle by ARIA
    testRoleAndSubRole(accDoc, "toggle", "AXCheckBox", "AXToggle");

    // A banana button
    testRoleAndSubRole(accDoc, "banana", "AXButton", null, "banana");

    // Other elements
    testRoleAndSubRole(accDoc, "deletion", "AXGroup", "AXDeleteStyleGroup");
    testRoleAndSubRole(accDoc, "dl", "AXList", "AXDefinitionList");
    testRoleAndSubRole(accDoc, "dt", "AXGroup", "AXTerm");
    testRoleAndSubRole(accDoc, "dd", "AXGroup", "AXDefinition");
    testRoleAndSubRole(accDoc, "hr", "AXSplitter", "AXContentSeparator");
    testRoleAndSubRole(accDoc, "insertion", "AXGroup", "AXInsertStyleGroup");
    testRoleAndSubRole(
      accDoc,
      "meter",
      "AXLevelIndicator",
      "AXMeter",
      "level indicator"
    );
    testRoleAndSubRole(accDoc, "sub", "AXGroup", "AXSubscriptStyleGroup");
    testRoleAndSubRole(accDoc, "sup", "AXGroup", "AXSuperscriptStyleGroup");

    // Some SVG stuff
    testRoleAndSubRole(accDoc, "svg", "AXImage");
    testRoleAndSubRole(accDoc, "g", "AXGroup");
    testRoleAndSubRole(accDoc, "rect", "AXImage");
    testRoleAndSubRole(accDoc, "circle", "AXImage");
    testRoleAndSubRole(accDoc, "ellipse", "AXImage");
    testRoleAndSubRole(accDoc, "line", "AXImage");
    testRoleAndSubRole(accDoc, "polygon", "AXImage");
    testRoleAndSubRole(accDoc, "polyline", "AXImage");
    testRoleAndSubRole(accDoc, "path", "AXImage");
    testRoleAndSubRole(accDoc, "image", "AXImage");
  }
);

addAccessibleTask(
  `
  <figure id="figure">
    <img id="img" src="http://example.com/a11y/accessible/tests/mochitest/moz.png" alt="Logo">
    <p>Non-image figure content</p>
    <figcaption id="figcaption">Old Mozilla logo</figcaption>
  </figure>`,
  (browser, accDoc) => {
    let figure = getNativeInterface(accDoc, "figure");
    ok(!figure.getAttributeValue("AXTitle"), "Figure should not have a title");
    is(
      figure.getAttributeValue("AXDescription"),
      "Old Mozilla logo",
      "Correct figure label"
    );
    is(figure.getAttributeValue("AXRole"), "AXGroup", "Correct figure role");
    is(
      figure.getAttributeValue("AXRoleDescription"),
      "figure",
      "Correct figure role description"
    );

    let img = getNativeInterface(accDoc, "img");
    ok(!img.getAttributeValue("AXTitle"), "img should not have a title");
    is(img.getAttributeValue("AXDescription"), "Logo", "Correct img label");
    is(img.getAttributeValue("AXRole"), "AXImage", "Correct img role");
    is(
      img.getAttributeValue("AXRoleDescription"),
      "image",
      "Correct img role description"
    );

    let figcaption = getNativeInterface(accDoc, "figcaption");
    ok(
      !figcaption.getAttributeValue("AXTitle"),
      "figcaption should not have a title"
    );
    ok(
      !figcaption.getAttributeValue("AXDescription"),
      "figcaption should not have a label"
    );
    is(
      figcaption.getAttributeValue("AXRole"),
      "AXGroup",
      "Correct figcaption role"
    );
    is(
      figcaption.getAttributeValue("AXRoleDescription"),
      "group",
      "Correct figcaption role description"
    );
  }
);

addAccessibleTask(`<button>hello world</button>`, async (browser, accDoc) => {
  const webArea = accDoc.nativeInterface.QueryInterface(
    Ci.nsIAccessibleMacInterface
  );

  is(
    webArea.getAttributeValue("AXRole"),
    "AXWebArea",
    "web area should be an AXWebArea"
  );
  ok(
    !webArea.attributeNames.includes("AXSubrole"),
    "AXWebArea should not have a subrole"
  );

  let roleChanged = waitForMacEvent("AXMozRoleChanged");
  await SpecialPowers.spawn(browser, [], () => {
    content.document.body.setAttribute("role", "application");
  });
  await roleChanged;

  is(
    webArea.getAttributeValue("AXRole"),
    "AXWebArea",
    "web area should retain AXWebArea role"
  );
  ok(
    !webArea.attributeNames.includes("AXSubrole"),
    "AXWebArea should not have a subrole"
  );

  let rootGroup = webArea.getAttributeValue("AXChildren")[0];
  is(rootGroup.getAttributeValue("AXRole"), "AXGroup");
  is(rootGroup.getAttributeValue("AXSubrole"), "AXLandmarkApplication");
});
