/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const VALUE_CHANGE_EVENT = "object:property-change:accessible-value";

function getValuetext(id) {
  return runPython(
    `findByDomId(doc, "${id}").get_attributes().get("valuetext")`
  );
}

/**
 * Call mutate and wait for an AT-SPI accessible-value property change event on
 * the Accessible with the given DOM id.
 */
async function mutateAndWaitForValueChange(id, mutate) {
  await runPython(`
    global onValueChange
    onValueChange = WaitForEvent("${VALUE_CHANGE_EVENT}", "${id}")
  `);
  await mutate();
  await runPython(`
    onValueChange.wait()
  `);
}

/**
 * Test valuetext on a slider.
 */
addAccessibleTask(
  `<div id="slider" role="slider" aria-valuenow="30"></div>`,
  async function testSlider(browser) {
    await runPython(`
      global doc
      doc = getDoc()
    `);
    is(await getValuetext("slider"), "30", "slider has correct valuetext");

    info("Changing aria-valuenow");
    await mutateAndWaitForValueChange("slider", () =>
      invokeSetAttribute(browser, "slider", "aria-valuenow", "40")
    );
    is(await getValuetext("slider"), "40", "slider has correct valuetext");

    info("Adding aria-valuetext");
    await mutateAndWaitForValueChange("slider", () =>
      invokeSetAttribute(browser, "slider", "aria-valuetext", "40%")
    );
    is(await getValuetext("slider"), "40%", "slider has correct valuetext");

    info("Changing aria-valuetext");
    await mutateAndWaitForValueChange("slider", () =>
      invokeSetAttribute(browser, "slider", "aria-valuetext", "50%")
    );
    is(await getValuetext("slider"), "50%", "slider has correct valuetext");
  }
);

/**
 * Test valuetext on a select element.
 */
addAccessibleTask(
  `
<select id="select" size="1">
  <option>a</option>
  <option>b</option>
</select>
  `,
  async function testSelect(browser) {
    await runPython(`
      global doc
      doc = getDoc()
    `);
    is(await getValuetext("select"), "a", "select has correct valuetext");

    info("Selecting b");
    await mutateAndWaitForValueChange("select", () =>
      invokeContentTask(browser, [], () => {
        content.document.getElementById("select").selectedIndex = 1;
      })
    );
    is(await getValuetext("select"), "b", "select has correct valuetext");

    info("Changing select to a list box");
    const shown = waitForEvent(EVENT_SHOW, "select");
    await invokeSetAttribute(browser, "select", "size", "2");
    await shown;
    is(await getValuetext("select"), null, "list box has no valuetext");
  }
);

/**
 * Test valuetext on an ARIA combobox.
 */
addAccessibleTask(
  `<div id="combobox" role="combobox">someValue</div>`,
  async function testAriaCombobox(browser) {
    await runPython(`
      global doc
      doc = getDoc()
    `);
    is(
      await getValuetext("combobox"),
      "someValue",
      "combobox has correct valuetext"
    );

    info("Changing combobox text");
    await mutateAndWaitForValueChange("combobox", () =>
      invokeContentTask(browser, [], () => {
        content.document.getElementById("combobox").firstChild.data =
          "otherValue";
      })
    );
    is(
      await getValuetext("combobox"),
      "otherValue",
      "combobox has correct valuetext"
    );
  }
);

/**
 * Test that valuetext isn't exposed on Accessibles which have a value that
 * shouldn't be exposed this way.
 */
addAccessibleTask(
  `
<button id="button">button</button>
<input id="input" value="abc">
<a id="link" href="https://example.com/">link</a>
<input id="editableCombobox" role="combobox" value="abc">
<div id="contentEditableCombobox" role="combobox" contenteditable>abc</div>
<div id="slider" role="slider" aria-valuenow="30"></div>
  `,
  async function testNoValuetext(browser) {
    await runPython(`
      global doc
      doc = getDoc()
    `);
    for (const id of [
      "button",
      "input",
      "link",
      "editableCombobox",
      "contentEditableCombobox",
    ]) {
      is(await getValuetext(id), null, `${id} has no valuetext`);
    }

    info("Editing input and editableCombobox, then changing slider");
    // We can't easily wait for an event not to happen. Instead, we record all
    // value change events until we get one for the slider. Value change events
    // for the inputs would arrive before the slider's.
    await runPython(`
      global onValueChange, valueChangeIds
      valueChangeIds = []
      def match(event):
        id = event.source.get_attributes().get("id")
        valueChangeIds.append(id)
        return id == "slider"
      onValueChange = WaitForEvent("${VALUE_CHANGE_EVENT}", match)
    `);
    const inputsChanged = waitForEvents([
      [EVENT_TEXT_VALUE_CHANGE, "input"],
      [EVENT_TEXT_VALUE_CHANGE, "editableCombobox"],
    ]);
    await invokeContentTask(browser, [], () => {
      content.document.getElementById("input").value = "def";
      content.document.getElementById("editableCombobox").value = "def";
    });
    await inputsChanged;
    await invokeSetAttribute(browser, "slider", "aria-valuenow", "40");
    const ids = await runPython(`
      onValueChange.wait()
      return valueChangeIds
    `);
    for (const id of ["input", "editableCombobox"]) {
      ok(!ids.includes(id), `No value change event for ${id}`);
      is(await getValuetext(id), null, `${id} has no valuetext`);
    }
  }
);
