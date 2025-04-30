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
const ExpandCollapseState_Collapsed = 0;
const ExpandCollapseState_Expanded = 1;
const ToggleState_Off = 0;
const ToggleState_On = 1;
const ToggleState_Indeterminate = 2;
/* eslint-enable camelcase */

/**
 * Test the Invoke pattern.
 */
addUiaTask(
  `
<button id="button">button</button>
<p id="p">p</p>
<input id="checkbox" type="checkbox">
<input id="radio" type="radio">
  `,
  async function testInvoke() {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("button");
    await definePyVar("pattern", `getUiaPattern(button, "Invoke")`);
    ok(await runPython(`bool(pattern)`), "button has Invoke pattern");
    info("Calling Invoke on button");
    // The button will get focus when it is clicked.
    let focused = waitForEvent(EVENT_FOCUS, "button");
    // The UIA -> IA2 proxy doesn't fire the Invoked event.
    if (gIsUiaEnabled) {
      await setUpWaitForUiaEvent("Invoke_Invoked", "button");
    }
    await runPython(`pattern.Invoke()`);
    await focused;
    ok(true, "button got focus");
    if (gIsUiaEnabled) {
      await waitForUiaEvent();
      ok(true, "button got Invoked event");
    }

    await testPatternAbsent("p", "Invoke");
    // The Microsoft IA2 -> UIA proxy doesn't follow Microsoft's own rules.
    if (gIsUiaEnabled) {
      // Check boxes expose the Toggle pattern, so they should not expose the
      // Invoke pattern.
      await testPatternAbsent("checkbox", "Invoke");
      // Ditto for radio buttons.
      await testPatternAbsent("radio", "Invoke");
    }
  }
);

/**
 * Test the Toggle pattern.
 */
addUiaTask(
  `
<input id="checkbox" type="checkbox" checked>
<button id="toggleButton" aria-pressed="false">toggle</button>
<button id="button">button</button>
<p id="p">p</p>

<script>
  // When checkbox is clicked and it is not checked, make it indeterminate.
  document.getElementById("checkbox").addEventListener("click", evt => {
    // Within the event listener, .checked is reversed and you can't set
    // .indeterminate. Work around this by deferring and handling the changes
    // ourselves.
    evt.preventDefault();
    const target = evt.target;
    setTimeout(() => {
      if (target.checked) {
        target.checked = false;
      } else {
        target.indeterminate = true;
      }
    }, 0);
  });

  // When toggleButton is clicked, set aria-pressed to true.
  document.getElementById("toggleButton").addEventListener("click", evt => {
    evt.target.ariaPressed = "true";
  });
</script>
  `,
  async function testToggle() {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("checkbox");
    await definePyVar("pattern", `getUiaPattern(checkbox, "Toggle")`);
    ok(await runPython(`bool(pattern)`), "checkbox has Toggle pattern");
    is(
      await runPython(`pattern.CurrentToggleState`),
      ToggleState_On,
      "checkbox has ToggleState_On"
    );
    // The IA2 -> UIA proxy doesn't fire ToggleState prop change events.
    if (gIsUiaEnabled) {
      info("Calling Toggle on checkbox");
      await setUpWaitForUiaPropEvent("ToggleToggleState", "checkbox");
      await runPython(`pattern.Toggle()`);
      await waitForUiaEvent();
      ok(true, "Got ToggleState prop change event on checkbox");
      is(
        await runPython(`pattern.CurrentToggleState`),
        ToggleState_Off,
        "checkbox has ToggleState_Off"
      );
      info("Calling Toggle on checkbox");
      await setUpWaitForUiaPropEvent("ToggleToggleState", "checkbox");
      await runPython(`pattern.Toggle()`);
      await waitForUiaEvent();
      ok(true, "Got ToggleState prop change event on checkbox");
      is(
        await runPython(`pattern.CurrentToggleState`),
        ToggleState_Indeterminate,
        "checkbox has ToggleState_Indeterminate"
      );
    }

    await assignPyVarToUiaWithId("toggleButton");
    await definePyVar("pattern", `getUiaPattern(toggleButton, "Toggle")`);
    ok(await runPython(`bool(pattern)`), "toggleButton has Toggle pattern");
    is(
      await runPython(`pattern.CurrentToggleState`),
      ToggleState_Off,
      "toggleButton has ToggleState_Off"
    );
    if (gIsUiaEnabled) {
      info("Calling Toggle on toggleButton");
      await setUpWaitForUiaPropEvent("ToggleToggleState", "toggleButton");
      await runPython(`pattern.Toggle()`);
      await waitForUiaEvent();
      ok(true, "Got ToggleState prop change event on toggleButton");
      is(
        await runPython(`pattern.CurrentToggleState`),
        ToggleState_On,
        "toggleButton has ToggleState_Off"
      );
    }

    await testPatternAbsent("button", "Toggle");
    await testPatternAbsent("p", "Toggle");
  }
);

/**
 * Test the ExpandCollapse pattern.
 */
addUiaTask(
  `
<details>
  <summary id="summary">summary</summary>
  details
</details>
<button id="popup" aria-haspopup="true">popup</button>
<button id="button">button</button>
<script>
  // When popup is clicked, set aria-expanded to true.
  document.getElementById("popup").addEventListener("click", evt => {
    evt.target.ariaExpanded = "true";
  });
</script>
  `,
  async function testExpandCollapse() {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("summary");
    await definePyVar("pattern", `getUiaPattern(summary, "ExpandCollapse")`);
    ok(await runPython(`bool(pattern)`), "summary has ExpandCollapse pattern");
    is(
      await runPython(`pattern.CurrentExpandCollapseState`),
      ExpandCollapseState_Collapsed,
      "summary has ExpandCollapseState_Collapsed"
    );
    // The IA2 -> UIA proxy doesn't fire ToggleState prop change events, nor
    // does it fail when Expand/Collapse is called on a control which is
    // already in the desired state.
    if (gIsUiaEnabled) {
      info("Calling Expand on summary");
      await setUpWaitForUiaPropEvent(
        "ExpandCollapseExpandCollapseState",
        "summary"
      );
      await runPython(`pattern.Expand()`);
      await waitForUiaEvent();
      ok(
        true,
        "Got ExpandCollapseExpandCollapseState prop change event on summary"
      );
      is(
        await runPython(`pattern.CurrentExpandCollapseState`),
        ExpandCollapseState_Expanded,
        "summary has ExpandCollapseState_Expanded"
      );
      info("Calling Expand on summary");
      await testPythonRaises(`pattern.Expand()`, "Expand on summary failed");
      info("Calling Collapse on summary");
      await setUpWaitForUiaPropEvent(
        "ExpandCollapseExpandCollapseState",
        "summary"
      );
      await runPython(`pattern.Collapse()`);
      await waitForUiaEvent();
      ok(
        true,
        "Got ExpandCollapseExpandCollapseState prop change event on summary"
      );
      is(
        await runPython(`pattern.CurrentExpandCollapseState`),
        ExpandCollapseState_Collapsed,
        "summary has ExpandCollapseState_Collapsed"
      );
      info("Calling Collapse on summary");
      await testPythonRaises(
        `pattern.Collapse()`,
        "Collapse on summary failed"
      );
    }

    await assignPyVarToUiaWithId("popup");
    // Initially, popup has aria-haspopup but not aria-expanded. That should
    // be exposed as collapsed.
    await definePyVar("pattern", `getUiaPattern(popup, "ExpandCollapse")`);
    ok(await runPython(`bool(pattern)`), "popup has ExpandCollapse pattern");
    // The IA2 -> UIA proxy doesn't expose ExpandCollapseState_Collapsed for
    // aria-haspopup without aria-expanded.
    if (gIsUiaEnabled) {
      is(
        await runPython(`pattern.CurrentExpandCollapseState`),
        ExpandCollapseState_Collapsed,
        "popup has ExpandCollapseState_Collapsed"
      );
      info("Calling Expand on popup");
      await setUpWaitForUiaPropEvent(
        "ExpandCollapseExpandCollapseState",
        "popup"
      );
      await runPython(`pattern.Expand()`);
      await waitForUiaEvent();
      ok(
        true,
        "Got ExpandCollapseExpandCollapseState prop change event on popup"
      );
      is(
        await runPython(`pattern.CurrentExpandCollapseState`),
        ExpandCollapseState_Expanded,
        "popup has ExpandCollapseState_Expanded"
      );
    }

    await testPatternAbsent("button", "ExpandCollapse");
  }
);

/**
 * Test the ScrollItem pattern.
 */
addUiaTask(
  `
<hr style="height: 100vh;">
<button id="button">button</button>
  `,
  async function testScrollItem(browser, docAcc) {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("button");
    await definePyVar("pattern", `getUiaPattern(button, "ScrollItem")`);
    ok(await runPython(`bool(pattern)`), "button has ScrollItem pattern");
    const button = findAccessibleChildByID(docAcc, "button");
    testStates(button, STATE_OFFSCREEN);
    info("Calling ScrollIntoView on button");
    // UIA doesn't have an event for this.
    let scrolled = waitForEvent(EVENT_SCROLLING_END, docAcc);
    await runPython(`pattern.ScrollIntoView()`);
    await scrolled;
    ok(true, "Document scrolled");
    testStates(button, 0, 0, STATE_OFFSCREEN);
  }
);

/**
 * Test the Value pattern.
 */
addUiaTask(
  `
<input id="text" value="before">
<input id="textRo" readonly value="textRo">
<input id="textDis" disabled value="textDis">
<select id="select"><option selected>a</option><option>b</option></select>
<progress id="progress" value="0.5"></progress>
<input id="range" type="range" aria-valuetext="02:00:00">
<a id="link" href="https://example.com/">Link</a>
<div id="ariaTextbox" contenteditable role="textbox">before</div>
<button id="button">button</button>
  `,
  async function testValue() {
    await definePyVar("doc", `getDocUia()`);
    await assignPyVarToUiaWithId("text");
    await definePyVar("pattern", `getUiaPattern(text, "Value")`);
    ok(await runPython(`bool(pattern)`), "text has Value pattern");
    ok(
      !(await runPython(`pattern.CurrentIsReadOnly`)),
      "text has IsReadOnly false"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "before",
      "text has correct Value"
    );
    info("SetValue on text");
    await setUpWaitForUiaPropEvent("ValueValue", "text");
    await runPython(`pattern.SetValue("after")`);
    await waitForUiaEvent();
    ok(true, "Got ValueValue prop change event on text");
    is(
      await runPython(`pattern.CurrentValue`),
      "after",
      "text has correct Value"
    );

    await assignPyVarToUiaWithId("textRo");
    await definePyVar("pattern", `getUiaPattern(textRo, "Value")`);
    ok(await runPython(`bool(pattern)`), "textRo has Value pattern");
    ok(
      await runPython(`pattern.CurrentIsReadOnly`),
      "textRo has IsReadOnly true"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "textRo",
      "textRo has correct Value"
    );
    info("SetValue on textRo");
    await testPythonRaises(
      `pattern.SetValue("after")`,
      "SetValue on textRo failed"
    );

    await assignPyVarToUiaWithId("textDis");
    await definePyVar("pattern", `getUiaPattern(textDis, "Value")`);
    ok(await runPython(`bool(pattern)`), "textDis has Value pattern");
    ok(
      !(await runPython(`pattern.CurrentIsReadOnly`)),
      "textDis has IsReadOnly false"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "textDis",
      "textDis has correct Value"
    );
    // The IA2 -> UIA proxy doesn't fail SetValue for a disabled element.
    if (gIsUiaEnabled) {
      info("SetValue on textDis");
      await testPythonRaises(
        `pattern.SetValue("after")`,
        "SetValue on textDis failed"
      );
    }

    await assignPyVarToUiaWithId("select");
    await definePyVar("pattern", `getUiaPattern(select, "Value")`);
    ok(await runPython(`bool(pattern)`), "select has Value pattern");
    ok(
      !(await runPython(`pattern.CurrentIsReadOnly`)),
      "select has IsReadOnly false"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "a",
      "select has correct Value"
    );
    info("SetValue on select");
    await testPythonRaises(
      `pattern.SetValue("b")`,
      "SetValue on select failed"
    );

    await assignPyVarToUiaWithId("progress");
    await definePyVar("pattern", `getUiaPattern(progress, "Value")`);
    ok(await runPython(`bool(pattern)`), "progress has Value pattern");
    ok(
      await runPython(`pattern.CurrentIsReadOnly`),
      "progress has IsReadOnly true"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "50%",
      "progress has correct Value"
    );
    info("SetValue on progress");
    await testPythonRaises(
      `pattern.SetValue("60%")`,
      "SetValue on progress failed"
    );

    await assignPyVarToUiaWithId("range");
    await definePyVar("pattern", `getUiaPattern(range, "Value")`);
    ok(await runPython(`bool(pattern)`), "range has Value pattern");
    is(
      await runPython(`pattern.CurrentValue`),
      "02:00:00",
      "range has correct Value"
    );

    await assignPyVarToUiaWithId("link");
    await definePyVar("pattern", `getUiaPattern(link, "Value")`);
    ok(await runPython(`bool(pattern)`), "link has Value pattern");
    is(
      await runPython(`pattern.CurrentValue`),
      "https://example.com/",
      "link has correct Value"
    );

    await assignPyVarToUiaWithId("ariaTextbox");
    await definePyVar("pattern", `getUiaPattern(ariaTextbox, "Value")`);
    ok(await runPython(`bool(pattern)`), "ariaTextbox has Value pattern");
    ok(
      !(await runPython(`pattern.CurrentIsReadOnly`)),
      "ariaTextbox has IsReadOnly false"
    );
    is(
      await runPython(`pattern.CurrentValue`),
      "before",
      "ariaTextbox has correct Value"
    );
    info("SetValue on ariaTextbox");
    await setUpWaitForUiaPropEvent("ValueValue", "ariaTextbox");
    await runPython(`pattern.SetValue("after")`);
    await waitForUiaEvent();
    ok(true, "Got ValueValue prop change event on ariaTextbox");
    is(
      await runPython(`pattern.CurrentValue`),
      "after",
      "ariaTextbox has correct Value"
    );

    await testPatternAbsent("button", "Value");
  }
);

/**
 * Test the Value pattern on a document.
 */
addUiaTask(``, async function testValueDoc(browser) {
  // A test snippet is a data: URI. The accessibility engine won't return these.
  let url = new URL("https://example.net/document-builder.sjs");
  url.searchParams.append("html", `<body id=${DEFAULT_CONTENT_DOC_BODY_ID}>`);
  let loaded = waitForEvent(
    EVENT_DOCUMENT_LOAD_COMPLETE,
    DEFAULT_CONTENT_DOC_BODY_ID
  );
  BrowserTestUtils.startLoadingURIString(browser, url.href);
  await loaded;
  await definePyVar("doc", `getDocUia()`);
  await definePyVar("pattern", `getUiaPattern(doc, "Value")`);
  ok(await runPython(`bool(pattern)`), "doc has Value pattern");
  is(
    await runPython(`pattern.CurrentValue`),
    url.href,
    "doc has correct Value"
  );
});

async function testRangeValueProps(id, ro, val, min, max, small, large) {
  await assignPyVarToUiaWithId(id);
  await definePyVar("pattern", `getUiaPattern(${id}, "RangeValue")`);
  ok(await runPython(`bool(pattern)`), `${id} has RangeValue pattern`);
  is(
    !!(await runPython(`pattern.CurrentIsReadOnly`)),
    ro,
    `${id} has IsReadOnly ${ro}`
  );
  is(await runPython(`pattern.CurrentValue`), val, `${id} has correct Value`);
  is(
    await runPython(`pattern.CurrentMinimum`),
    min,
    `${id} has correct Minimum`
  );
  is(
    await runPython(`pattern.CurrentMaximum`),
    max,
    `${id} has correct Maximum`
  );
  // IA2 doesn't support small/large change, so the IA2 -> UIA proxy can't
  // either.
  if (gIsUiaEnabled) {
    is(
      await runPython(`pattern.CurrentSmallChange`),
      small,
      `${id} has correct SmallChange`
    );
    is(
      await runPython(`pattern.CurrentLargeChange`),
      large,
      `${id} has correct LargeChange`
    );
  }
}

/**
 * Test the RangeValue pattern.
 */
addUiaTask(
  `
<input id="range" type="range">
<input id="rangeBig" type="range" max="1000">
<progress id="progress" value="0.5"></progress>
<input id="numberRo" type="number" min="0" max="10" value="5" readonly>
<div id="ariaSlider" role="slider">slider</div>
<button id="button">button</button>
  `,
  async function testRangeValue(browser) {
    await definePyVar("doc", `getDocUia()`);
    await testRangeValueProps("range", false, 50, 0, 100, 1, 10);
    info("SetValue on range");
    await setUpWaitForUiaPropEvent("RangeValueValue", "range");
    await runPython(`pattern.SetValue(20)`);
    await waitForUiaEvent();
    ok(true, "Got RangeValueValue prop change event on range");
    is(await runPython(`pattern.CurrentValue`), 20, "range has correct Value");

    await testRangeValueProps("rangeBig", false, 500, 0, 1000, 1, 100);

    await testRangeValueProps("progress", true, 0.5, 0, 1, 0, 0.1);
    info("Calling SetValue on progress");
    await testPythonRaises(
      `pattern.SetValue(0.6)`,
      "SetValue on progress failed"
    );

    await testRangeValueProps("numberRo", true, 5, 0, 10, 1, 1);
    info("Calling SetValue on numberRo");
    await testPythonRaises(
      `pattern.SetValue(6)`,
      "SetValue on numberRo failed"
    );

    await testRangeValueProps("ariaSlider", false, 50, 0, 100, null, null);
    info("Setting aria-valuenow on ariaSlider");
    await setUpWaitForUiaPropEvent("RangeValueValue", "ariaSlider");
    await invokeSetAttribute(browser, "ariaSlider", "aria-valuenow", "60");
    await waitForUiaEvent();
    ok(true, "Got RangeValueValue prop change event on ariaSlider");
    is(
      await runPython(`pattern.CurrentValue`),
      60,
      "ariaSlider has correct Value"
    );

    await testPatternAbsent("button", "RangeValue");
  }
);
