/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../../mochitest/role.js */
/* import-globals-from ../../../mochitest/states.js */
loadScripts(
  { name: "role.js", dir: MOCHITESTS_DIR },
  { name: "states.js", dir: MOCHITESTS_DIR }
);

/* eslint-disable camelcase */
// From https://learn.microsoft.com/en-us/windows/win32/winauto/landmark-type-identifiers
const UIA_CustomLandmarkTypeId = 80000;
const UIA_MainLandmarkTypeId = 80002;
/* eslint-enable camelcase */

/**
 * Test the Name property.
 */
addAccessibleTask(
  `
<button id="button">before</button>
<div id="div">div</div>
  `,
  async function testName(browser) {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("button");
    is(
      await runPython(`button.CurrentName`),
      "before",
      "button has correct name"
    );
    await assignPyVarToUiaWithId("div");
    is(await runPython(`div.CurrentName`), "", "div has no name");

    info("Setting aria-label on button");
    await setUpWaitForUiaPropEvent("Name", "button");
    await invokeSetAttribute(browser, "button", "aria-label", "after");
    await waitForUiaEvent();
    ok(true, "Got Name prop change event on button");
    is(
      await runPython(`button.CurrentName`),
      "after",
      "button has correct name"
    );
  }
);

/**
 * Test the FullDescription property.
 */
addAccessibleTask(
  `
<button id="button" aria-description="before">button</button>
<div id="div">div</div>
  `,
  async function testFullDescription(browser) {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("button");
    is(
      await runPython(`button.CurrentFullDescription`),
      "before",
      "button has correct FullDescription"
    );
    await assignPyVarToUiaWithId("div");
    is(
      await runPython(`div.CurrentFullDescription`),
      "",
      "div has no FullDescription"
    );

    info("Setting aria-description on button");
    await setUpWaitForUiaPropEvent("FullDescription", "button");
    await invokeSetAttribute(browser, "button", "aria-description", "after");
    await waitForUiaEvent();
    ok(true, "Got FullDescription prop change event on button");
    is(
      await runPython(`button.CurrentFullDescription`),
      "after",
      "button has correct FullDescription"
    );
  }
);

/**
 * Test the IsEnabled property.
 */
addAccessibleTask(
  `
<button id="button">button</button>
<p id="p">p</p>
  `,
  async function testIsEnabled(browser) {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("button");
    ok(await runPython(`button.CurrentIsEnabled`), "button has IsEnabled true");
    info("Setting disabled on button");
    await setUpWaitForUiaPropEvent("IsEnabled", "button");
    await invokeSetAttribute(browser, "button", "disabled", true);
    await waitForUiaEvent();
    ok(true, "Got IsEnabled prop change event on button");
    ok(
      !(await runPython(`button.CurrentIsEnabled`)),
      "button has IsEnabled false"
    );

    await assignPyVarToUiaWithId("p");
    ok(await runPython(`p.CurrentIsEnabled`), "p has IsEnabled true");
  }
);

async function testGroupPos(id, level, pos, size) {
  await assignPyVarToUiaWithId(id);
  is(await runPython(`${id}.CurrentLevel`), level, `${id} Level correct`);
  is(
    await runPython(`${id}.CurrentPositionInSet`),
    pos,
    `${id} PositionInSet correct`
  );
  is(
    await runPython(`${id}.CurrentSizeOfSet`),
    size,
    `${id} SizeOfSet correct`
  );
}

/**
 * Test the Level, PositionInSet and SizeOfSet properties.
 */
addAccessibleTask(
  `
<ul>
  <li id="li1">li1<ul id="ul1">
    <li id="li2a">li2a</li>
    <li id="li2b" hidden>li2b</li>
    <li id="li2c">li2c</li>
  </ul></li>
</ul>
<h2 id="h2">h2</h2>
<button id="button">button</button>
  `,
  async function testGroupPosProps(browser) {
    await definePyVar("doc", `getDocUia()`);
    await testGroupPos("li1", 1, 1, 1);
    await testGroupPos("li2a", 2, 1, 2);
    await testGroupPos("li2c", 2, 2, 2);
    info("Showing li2b");
    // There aren't events in any API for a change to group position properties
    // because this would be too spammy and isn't particularly useful given
    // how frequently these can change.
    let shown = waitForEvent(EVENT_SHOW, "li2b");
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("li2b").hidden = false;
    });
    await shown;
    await testGroupPos("li2a", 2, 1, 3);
    await testGroupPos("li2b", 2, 2, 3);
    await testGroupPos("li2c", 2, 3, 3);

    await testGroupPos("h2", 2, 0, 0);
    await testGroupPos("button", 0, 0, 0);
  }
);

/**
 * Test the FrameworkId property.
 */
addAccessibleTask(
  `<button id="button">button</button>`,
  async function testFrameworkId() {
    await definePyVar("doc", `getDocUia()`);
    is(
      await runPython(`doc.CurrentFrameworkId`),
      "Gecko",
      "doc FrameworkId is correct"
    );
    await assignPyVarToUiaWithId("button");
    is(
      await runPython(`button.CurrentFrameworkId`),
      "Gecko",
      "button FrameworkId is correct"
    );
  }
);

/**
 * Test the ClassName property.
 */
addAccessibleTask(
  `
<p id="p">p</p>
<button id="button" class="c1">button</button>
  `,
  async function testClassName(browser, docAcc) {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("p");
    ok(!(await runPython(`p.CurrentClassName`)), "p has no ClassName");

    await assignPyVarToUiaWithId("button");
    is(
      await runPython(`button.CurrentClassName`),
      "c1",
      "button has correct ClassName"
    );
    info("Changing button class");
    await invokeSetAttribute(browser, "button", "class", "c2 c3");
    // Gecko doesn't fire an event for class changes, as this isn't useful for
    // clients.
    const button = findAccessibleChildByID(docAcc, "button");
    await untilCacheIs(
      () => button.attributes.getStringProperty("class"),
      "c2 c3",
      "button class updated"
    );
    is(
      await runPython(`button.CurrentClassName`),
      "c2 c3",
      "button has correct ClassName"
    );
  }
);

/**
 * Test the AriaRole property.
 */
addAccessibleTask(
  `
<div id="button" role="button">button</div>
<div id="main" role="main">main</div>
<div id="unknown" role="unknown">unknown</div>
<button id="computedButton">computedButton</button>
<h1 id="computedHeading">computedHeading</h1>
<main id="computedMain">computedMain</main>
<div id="generic">generic</div>
  `,
  async function testAriaRole() {
    await definePyVar("doc", `getDocUia()`);
    is(
      await runPython(`findUiaByDomId(doc, "button").CurrentAriaRole`),
      "button",
      "button has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "main").CurrentAriaRole`),
      "main",
      "main has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "unknown").CurrentAriaRole`),
      "unknown",
      "unknown has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "computedButton").CurrentAriaRole`),
      "button",
      "computedButton has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "computedMain").CurrentAriaRole`),
      "main",
      "computedMain has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "computedHeading").CurrentAriaRole`),
      "heading",
      "computedHeading has correct AriaRole"
    );
    is(
      await runPython(`findUiaByDomId(doc, "generic").CurrentAriaRole`),
      "generic",
      "generic has correct AriaRole"
    );
  }
);

/**
 * Test the LocalizedControlType property. We don't support this ourselves, but
 * the system provides it based on ControlType and AriaRole.
 */
addAccessibleTask(
  `
<button id="button">button</button>
<h1 id="h1">h1</h1>
<main id="main">main</main>
<math id="math"><mi>x</mi></math>
  `,
  async function testLocalizedControlType() {
    await definePyVar("doc", `getDocUia()`);
    is(
      await runPython(
        `findUiaByDomId(doc, "button").CurrentLocalizedControlType`
      ),
      "button",
      "button has correct LocalizedControlType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "h1").CurrentLocalizedControlType`),
      "heading",
      "h1 has correct LocalizedControlType"
    );
    is(
      await runPython(
        `findUiaByDomId(doc, "main").CurrentLocalizedControlType`
      ),
      "main",
      "main has correct LocalizedControlType"
    );
    is(
      await runPython(
        `findUiaByDomId(doc, "math").CurrentLocalizedControlType`
      ),
      "math",
      "math has correct LocalizedControlType"
    );
  }
);

/**
 * Test the LandmarkType property.
 */
addAccessibleTask(
  `
<div id="main" role="main">main</div>
<main id="htmlMain">htmlMain</main>
<div id="banner" role="banner">banner</div>
<header id="header">header</header>
<div id="region" role="region" aria-label="region">region</div>
<div id="unnamedRegion" role="region">unnamedRegion</div>
<main id="mainBanner" role="banner">mainBanner</main>
<div id="none">none</div>
  `,
  async function testLandmarkType() {
    await definePyVar("doc", `getDocUia()`);
    is(
      await runPython(`findUiaByDomId(doc, "main").CurrentLandmarkType`),
      UIA_MainLandmarkTypeId,
      "main has correct LandmarkType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "htmlMain").CurrentLandmarkType`),
      UIA_MainLandmarkTypeId,
      "htmlMain has correct LandmarkType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "banner").CurrentLandmarkType`),
      UIA_CustomLandmarkTypeId,
      "banner has correct LandmarkType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "header").CurrentLandmarkType`),
      UIA_CustomLandmarkTypeId,
      "header has correct LandmarkType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "region").CurrentLandmarkType`),
      UIA_CustomLandmarkTypeId,
      "region has correct LandmarkType"
    );
    is(
      await runPython(
        `findUiaByDomId(doc, "unnamedRegion").CurrentLandmarkType`
      ),
      0,
      "unnamedRegion has correct LandmarkType"
    );
    // ARIA role takes precedence.
    is(
      await runPython(`findUiaByDomId(doc, "mainBanner").CurrentLandmarkType`),
      UIA_CustomLandmarkTypeId,
      "mainBanner has correct LandmarkType"
    );
    is(
      await runPython(`findUiaByDomId(doc, "none").CurrentLandmarkType`),
      0,
      "none has correct LandmarkType"
    );
  }
);

/**
 * Test the LocalizedLandmarkType property.
 */
addAccessibleTask(
  `
<div id="main" role="main">main</div>
<div id="contentinfo" role="contentinfo">contentinfo</div>
<div id="region" role="region" aria-label="region">region</div>
<div id="unnamedRegion" role="region">unnamedRegion</div>
<main id="mainBanner" role="banner">mainBanner</main>
<div id="none">none</div>
  `,
  async function testLocalizedLandmarkType() {
    await definePyVar("doc", `getDocUia()`);
    // Provided by the system.
    is(
      await runPython(
        `findUiaByDomId(doc, "main").CurrentLocalizedLandmarkType`
      ),
      "main",
      "main has correct LocalizedLandmarkType"
    );
    // Provided by us.
    is(
      await runPython(
        `findUiaByDomId(doc, "contentinfo").CurrentLocalizedLandmarkType`
      ),
      "content information",
      "contentinfo has correct LocalizedLandmarkType"
    );
    is(
      await runPython(
        `findUiaByDomId(doc, "region").CurrentLocalizedLandmarkType`
      ),
      "region",
      "region has correct LocalizedLandmarkType"
    );
    // Invalid landmark.
    is(
      await runPython(
        `findUiaByDomId(doc, "unnamedRegion").CurrentLocalizedLandmarkType`
      ),
      "",
      "unnamedRegion has correct LocalizedLandmarkType"
    );
    // ARIA role takes precedence.
    is(
      await runPython(
        `findUiaByDomId(doc, "mainBanner").CurrentLocalizedLandmarkType`
      ),
      "banner",
      "mainBanner has correct LocalizedLandmarkType"
    );
    is(
      await runPython(
        `findUiaByDomId(doc, "none").CurrentLocalizedLandmarkType`
      ),
      "",
      "none has correct LocalizedLandmarkType"
    );
  }
);

/**
 * Test the AcceleratorKey property.
 */
addAccessibleTask(
  `
  <div id="button" role="button" aria-keyshortcuts="Alt+Shift+f">foo</div>
  `,
  async function testAcceleratorKey() {
    await definePyVar("doc", `getDocUia()`);
    is(
      await runPython(`findUiaByDomId(doc, "button").CurrentAcceleratorKey`),
      "Alt+Shift+f",
      "button has correct AcceleratorKey"
    );
  }
);

/**
 * Test the IsOffscreen property.
 */
addAccessibleTask(
  `
<button id="onscreen">onscreen</button>
<button id="offscreen" style="position: absolute; left: -10000px;">offscreen</button>
  `,
  async function testIsOffscreen(browser, docAcc) {
    await definePyVar("doc", `getDocUia()`);
    ok(
      !(await runPython(`findUiaByDomId(doc, "onscreen").CurrentIsOffscreen`)),
      "onscreen has correct IsOffscreen"
    );
    ok(
      await runPython(`findUiaByDomId(doc, "offscreen").CurrentIsOffscreen`),
      "offscreen has correct IsOffscreen"
    );
    ok(
      !(await runPython(`doc.CurrentIsOffscreen`)),
      "doc has correct IsOffscreen"
    );
    info("Opening a new tab");
    await BrowserTestUtils.withNewTab("", async () => {
      // withNewTab (nor a focus event) isn't enough to guarantee the new tab is
      // fully active in the foreground yet.
      await untilCacheOk(() => {
        const [state] = getStates(docAcc);
        return state & STATE_OFFSCREEN;
      }, "doc is offscreen in cross-platform tree");
      // doc is now a background tab, so it should be offscreen.
      ok(
        await runPython(`doc.CurrentIsOffscreen`),
        "doc has correct IsOffscreen"
      );
    });
  }
);

/**
 * Test the IsPassword property.
 */
addAccessibleTask(
  `
<input type="text" id="text">
<input type="password" id="password">
  `,
  async function testIsPassword() {
    await definePyVar("doc", `getDocUia()`);
    ok(
      !(await runPython(`findUiaByDomId(doc, "text").CurrentIsPassword`)),
      "text has correct IsPassword"
    );
    ok(
      await runPython(`findUiaByDomId(doc, "password").CurrentIsPassword`),
      "password has correct IsPassword"
    );
    ok(
      !(await runPython(`doc.CurrentIsPassword`)),
      "doc has correct IsPassword"
    );
  }
);

/**
 * Test exposure of aria-current via the AriaProperties property.
 */
addAccessibleTask(
  `
<button id="missing">missing</button>
<button id="false" aria-current="false">false</button>
<button id="undefined" aria-current="undefined">undefined</button>
<button id="true" aria-current="true">false</button>
<button id="page" aria-current="page">page</button>
<button id="unrecognized" aria-current="unrecognized">unrecognized</button>
  `,
  async function testCurrent() {
    await definePyVar("doc", `getDocUia()`);
    let result = await runPython(
      `findUiaByDomId(doc, "missing").CurrentAriaProperties`
    );
    is(
      result.indexOf("current="),
      -1,
      "AriaProperties for missing doesn't contain current"
    );
    result = await runPython(
      `findUiaByDomId(doc, "false").CurrentAriaProperties`
    );
    is(
      result.indexOf("current="),
      -1,
      "AriaProperties for false doesn't contain current"
    );
    result = await runPython(
      `findUiaByDomId(doc, "undefined").CurrentAriaProperties`
    );
    is(
      result.indexOf("current="),
      -1,
      "AriaProperties for undefined doesn't contain current"
    );
    result = await runPython(
      `findUiaByDomId(doc, "true").CurrentAriaProperties`
    );
    isnot(
      result.indexOf("current=true"),
      -1,
      "AriaProperties for true contains current=true"
    );
    result = await runPython(
      `findUiaByDomId(doc, "page").CurrentAriaProperties`
    );
    isnot(
      result.indexOf("current=page"),
      -1,
      "AriaProperties for page contains current=page"
    );
    result = await runPython(
      `findUiaByDomId(doc, "unrecognized").CurrentAriaProperties`
    );
    isnot(
      result.indexOf("current=true"),
      -1,
      "AriaProperties for unrecognized contains current=true"
    );
  }
);
