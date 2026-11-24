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

function getSelectedIds(selectable) {
  return selectable
    .getAttributeValue("AXSelectedChildren")
    .map(c => c.getAttributeValue("AXDOMIdentifier"));
}

/**
 * Test aria tabs
 */
addAccessibleTask("mac/doc_aria_tabs.html", async (browser, accDoc) => {
  let tablist = getNativeInterface(accDoc, "tablist");
  is(
    tablist.getAttributeValue("AXRole"),
    "AXTabGroup",
    "Correct role for tablist"
  );

  let tabMacAccs = tablist.getAttributeValue("AXTabs");
  is(tabMacAccs.length, 3, "3 items in AXTabs");

  let selectedTabs = tablist.getAttributeValue("AXSelectedChildren");
  is(selectedTabs.length, 1, "one selected tab");

  let tab = selectedTabs[0];
  is(tab.getAttributeValue("AXRole"), "AXRadioButton", "Correct role for tab");
  is(
    tab.getAttributeValue("AXSubrole"),
    "AXTabButton",
    "Correct subrole for tab"
  );
  is(tab.getAttributeValue("AXTitle"), "First Tab", "Correct title for tab");

  let tabToSelect = tabMacAccs[1];
  is(
    tabToSelect.getAttributeValue("AXTitle"),
    "Second Tab",
    "Correct title for tab"
  );

  let actions = tabToSelect.actionNames;
  ok(true, actions);
  ok(actions.includes("AXPress"), "Has switch action");

  let evt = waitForMacEvent("AXSelectedChildrenChanged");
  tabToSelect.performAction("AXPress");
  await evt;

  selectedTabs = tablist.getAttributeValue("AXSelectedChildren");
  is(selectedTabs.length, 1, "one selected tab");
  is(
    selectedTabs[0].getAttributeValue("AXTitle"),
    "Second Tab",
    "Correct title for tab"
  );
});

addAccessibleTask('<p id="p">hello</p>', async (browser, accDoc) => {
  let p = getNativeInterface(accDoc, "p");
  ok(
    p.attributeNames.includes("AXSelected"),
    "html element includes 'AXSelected' attribute"
  );
  is(p.getAttributeValue("AXSelected"), 0, "AX selected is 'false'");
});

addAccessibleTask(
  `<select id="select" aria-label="Choose a number" multiple>
    <option id="one" selected>One</option>
    <option id="two">Two</option>
    <option id="three">Three</option>
    <option id="four" disabled>Four</option>
  </select>`,
  async (browser, accDoc) => {
    let select = getNativeInterface(accDoc, "select");
    let one = getNativeInterface(accDoc, "one");
    let two = getNativeInterface(accDoc, "two");
    let three = getNativeInterface(accDoc, "three");
    let four = getNativeInterface(accDoc, "four");

    is(
      select.getAttributeValue("AXDescription"),
      "Choose a number",
      "Select titled correctly"
    );
    ok(
      select.attributeNames.includes("AXOrientation"),
      "Have orientation attribute"
    );
    ok(
      select.isAttributeSettable("AXSelectedChildren"),
      "Select can have AXSelectedChildren set"
    );

    is(one.getAttributeValue("AXTitle"), "", "Option should not have a title");
    is(
      one.getAttributeValue("AXValue"),
      "One",
      "Option should have correct value"
    );
    is(
      one.getAttributeValue("AXRole"),
      "AXStaticText",
      "Options should have AXStaticText role"
    );
    ok(one.isAttributeSettable("AXSelected"), "Option can have AXSelected set");

    is(select.getAttributeValue("AXSelectedChildren").length, 1);
    let evt = waitForMacEvent("AXSelectedChildrenChanged");
    one.setAttributeValue("AXSelected", false);
    await evt;
    is(select.getAttributeValue("AXSelectedChildren").length, 0);
    evt = waitForMacEvent("AXSelectedChildrenChanged");
    three.setAttributeValue("AXSelected", true);
    await evt;
    is(select.getAttributeValue("AXSelectedChildren").length, 1);
    ok(getSelectedIds(select).includes("three"), "'three' is selected");
    evt = waitForMacEvent("AXSelectedChildrenChanged");
    select.setAttributeValue("AXSelectedChildren", [one, two]);
    await evt;
    await untilCacheOk(() => {
      let ids = getSelectedIds(select);
      return ids[0] == "one" && ids[1] == "two";
    }, "Got correct selected children");

    evt = waitForMacEvent("AXSelectedChildrenChanged");
    select.setAttributeValue("AXSelectedChildren", [three, two, four]);
    await evt;
    await untilCacheOk(() => {
      let ids = getSelectedIds(select);
      return ids[0] == "two" && ids[1] == "three";
    }, "Got correct selected children");

    ok(!four.getAttributeValue("AXEnabled"), "Disabled option is disabled");
  }
);

addAccessibleTask(
  `<select id="select" aria-label="Choose a thing" multiple>
    <optgroup label="Fruits">
      <option id="banana" selected>Banana</option>
      <option id="apple">Apple</option>
      <option id="orange">Orange</option>
    </optgroup>
    <optgroup label="Vegetables">
      <option id="lettuce" selected>Lettuce</option>
      <option id="tomato">Tomato</option>
      <option id="onion">Onion</option>
    </optgroup>
    <optgroup label="Spices">
      <option id="cumin">Cumin</option>
      <option id="coriander">Coriander</option>
      <option id="allspice" selected>Allspice</option>
    </optgroup>
    <option id="everything">Everything</option>
  </select>`,
  async function testGroupLabels(browser, accDoc) {
    let select = getNativeInterface(accDoc, "select");

    is(
      select.getAttributeValue("AXDescription"),
      "Choose a thing",
      "Select titled correctly"
    );
    ok(
      select.attributeNames.includes("AXOrientation"),
      "Have orientation attribute"
    );
    ok(
      select.isAttributeSettable("AXSelectedChildren"),
      "Select can have AXSelectedChildren set"
    );
    let children = select.getAttributeValue("AXChildren");
    for (const [i, expected] of [
      ["​", false, false],
      ["Fruits", false, false],
      ["Banana", true, true],
      ["Apple", true, true],
      ["Orange", true, true],
      ["​", false, false],
      ["Vegetables", false, false],
      ["Lettuce", true, true],
      ["Tomato", true, true],
      ["Onion", true, true],
      ["​", false, false],
      ["Spices", false, false],
      ["Cumin", true, true],
      ["Coriander", true, true],
      ["Allspice", true, true],
      ["Everything", true, true],
    ].entries()) {
      Assert.deepEqual(
        [
          children[i].getAttributeValue("AXValue"),
          children[i].isAttributeSettable("AXSelected"),
          children[i].getAttributeValue("AXEnabled"),
        ],
        expected,
        `Child ${i} (${expected[0]}) has correct value, selectability, and enabled state`
      );
    }

    let allspice = getNativeInterface(accDoc, "allspice");
    is(
      allspice.getAttributeValue("AXTitle"),
      "",
      "Option should not have a title"
    );
    is(
      allspice.getAttributeValue("AXValue"),
      "Allspice",
      "Option should have a value"
    );
    is(
      allspice.getAttributeValue("AXRole"),
      "AXStaticText",
      "Options should have AXStaticText role"
    );
    ok(
      allspice.isAttributeSettable("AXSelected"),
      "Option can have AXSelected set"
    );
    is(
      allspice
        .getAttributeValue("AXParent")
        .getAttributeValue("AXDOMIdentifier"),
      "select",
      "Select is direct parent of nested option"
    );

    let groupLabel = select.getAttributeValue("AXChildren")[1];
    ok(
      !groupLabel.isAttributeSettable("AXSelected"),
      "Group label should not be selectable"
    );
    is(
      groupLabel.getAttributeValue("AXValue"),
      "Fruits",
      "Group label should have a value"
    );
    is(
      groupLabel.getAttributeValue("AXTitle"),
      null,
      "Group label should not have a title"
    );
    is(
      groupLabel.getAttributeValue("AXRole"),
      "AXStaticText",
      "Group label should have AXStaticText role"
    );
    is(
      groupLabel
        .getAttributeValue("AXParent")
        .getAttributeValue("AXDOMIdentifier"),
      "select",
      "Select is direct parent of group label"
    );

    Assert.deepEqual(getSelectedIds(select), ["banana", "lettuce", "allspice"]);
  }
);

addAccessibleTask(
  `<div role="listbox" id="select" aria-label="Choose a number" aria-multiselectable="true">
    <div role="option" id="one" aria-selected="true">One</div>
    <div role="option" id="two">Two</div>
    <div role="option" id="three">Three</div>
    <div role="option" id="four" aria-disabled="true">Four</div>
</div>`,
  async (browser, accDoc) => {
    let select = getNativeInterface(accDoc, "select");
    let one = getNativeInterface(accDoc, "one");

    is(
      select.getAttributeValue("AXDescription"),
      "Choose a number",
      "Select titled correctly"
    );
    ok(
      select.attributeNames.includes("AXOrientation"),
      "Have orientation attribute"
    );
    ok(
      select.isAttributeSettable("AXSelectedChildren"),
      "Select can have AXSelectedChildren set"
    );

    is(one.getAttributeValue("AXTitle"), "", "Option should not have a title");
    is(
      one.getAttributeValue("AXValue"),
      "One",
      "Option should have correct value"
    );
    is(
      one.getAttributeValue("AXRole"),
      "AXStaticText",
      "Options should have AXStaticText role"
    );
    ok(one.isAttributeSettable("AXSelected"), "Option can have AXSelected set");

    is(select.getAttributeValue("AXSelectedChildren").length, 1);
    let evt = waitForMacEvent("AXSelectedChildrenChanged");
    // Change selection from content.
    await SpecialPowers.spawn(browser, [], () => {
      content.document.getElementById("one").removeAttribute("aria-selected");
    });
    await evt;
    is(select.getAttributeValue("AXSelectedChildren").length, 0);
  }
);

addAccessibleTask(
  `<div role="listbox" id="select" aria-label="Choose a number">
    <div id="groupOne" role="group">
      <div role="option" id="one" aria-selected="true">One</div>
    </div>
    <div id="groupTwo" role="group">
      <div role="option" id="two" aria-selected="false">Two</div>
    </div>
    <div id="groupThree" role="group">
      <div role="option" id="three" aria-selected="false">Three</div>
    </div>
  </div>`,
  async function testGroupedListbox(browser, accDoc) {
    let select = getNativeInterface(accDoc, "select");
    let one = getNativeInterface(accDoc, "one");
    let two = getNativeInterface(accDoc, "two");

    // Check the listbox
    is(
      select.getAttributeValue("AXDescription"),
      "Choose a number",
      "Select titled correctly"
    );
    ok(
      select.isAttributeSettable("AXSelectedChildren"),
      "Select can have AXSelectedChildren set"
    );
    // Check the first option
    is(
      one.getAttributeValue("AXValue"),
      "One",
      "First option has correct value"
    );
    is(
      one.getAttributeValue("AXSelected"),
      1,
      "The first option has selected state"
    );
    // Check the second option
    is(
      two.getAttributeValue("AXValue"),
      "Two",
      "Second option has correct value"
    );
    is(
      two.getAttributeValue("AXSelected"),
      0,
      "The second option has no selected state"
    );
    // Check the relationship between the listbox and its selected children
    is(
      select.getAttributeValue("AXSelectedChildren").length,
      1,
      "Listbox has one selected child"
    );
    let selectedChild = select.getAttributeValue("AXSelectedChildren")[0];
    is(
      selectedChild.getAttributeValue("AXValue"),
      "One",
      "The first option is the selected child"
    );

    let evt = waitForMacEvent("AXSelectedChildrenChanged");
    // Remove all selection.
    await SpecialPowers.spawn(browser, [], () => {
      content.document
        .getElementById("one")
        .setAttribute("aria-selected", "false");
    });
    await evt;
    // Check the relationship between the listbox and its selected children
    // after removing all selection.
    is(
      select.getAttributeValue("AXSelectedChildren").length,
      0,
      "Listbox has no selected child"
    );
    is(
      one.getAttributeValue("AXSelected"),
      0,
      "The first option has no selected state"
    );

    evt = waitForMacEvent("AXSelectedChildrenChanged");
    // Modify listbox so the second item is selected.
    await SpecialPowers.spawn(browser, [], () => {
      content.document
        .getElementById("two")
        .setAttribute("aria-selected", "true");
    });
    await evt;
    // Check the relationship between the listbox and its selected children
    // after selecting the second option.
    is(
      select.getAttributeValue("AXSelectedChildren").length,
      1,
      "Listbox has one selected child"
    );
    selectedChild = select.getAttributeValue("AXSelectedChildren")[0];
    is(
      selectedChild.getAttributeValue("AXValue"),
      "Two",
      "The second option is the selected child"
    );
    is(
      two.getAttributeValue("AXSelected"),
      1,
      "The second option has selected state"
    );
  }
);

addAccessibleTask(
  `
  <div role="listbox" id="listbox">
    <div role="group">
      <div role="group">
        <div role="option" id="optionA" aria-selected="true">
          <div role="group">
            <button id="one">hi</button>
          </div>
        </div>
        <div role="option" id="optionB" aria-selected="false">
          <div role="group">
            <button id="two">hello</button>
          </div>
        </div>
      </div>
    </div>
    `,
  async function testMultiGroupedListbox(browser, accDoc) {
    let listbox = getNativeInterface(accDoc, "listbox");
    let optionA = getNativeInterface(accDoc, "optionA");
    let optionB = getNativeInterface(accDoc, "optionB");

    ok(
      listbox.isAttributeSettable("AXSelectedChildren"),
      "Select can have AXSelectedChildren set"
    );
    // Check the first option
    is(
      optionA.getAttributeValue("AXValue"),
      "hi",
      "First option has correct value"
    );
    is(
      optionA.getAttributeValue("AXSelected"),
      1,
      "The first option has selected state"
    );
    // Check the second option
    is(
      optionB.getAttributeValue("AXValue"),
      "hello",
      "Second option has correct value"
    );
    is(
      optionB.getAttributeValue("AXSelected"),
      0,
      "The second option has no selected state"
    );
    // Check the relationship between the listbox and its selected children
    is(
      listbox.getAttributeValue("AXSelectedChildren").length,
      1,
      "Listbox has one selected child"
    );
    let selectedChild = listbox.getAttributeValue("AXSelectedChildren")[0];
    is(
      selectedChild.getAttributeValue("AXValue"),
      "hi",
      "The first option is the selected child"
    );

    let evt = waitForMacEvent("AXSelectedChildrenChanged");
    // Remove all selection.
    await SpecialPowers.spawn(browser, [], () => {
      content.document
        .getElementById("optionA")
        .setAttribute("aria-selected", "false");
    });
    await evt;
    // Check the relationship between the listbox and its selected children
    // after removing all selection.
    is(
      listbox.getAttributeValue("AXSelectedChildren").length,
      0,
      "Listbox has no selected child"
    );
    is(
      optionA.getAttributeValue("AXSelected"),
      0,
      "The first option has no selected state"
    );

    evt = waitForMacEvent("AXSelectedChildrenChanged");
    // Modify listbox so the second item is selected.
    await SpecialPowers.spawn(browser, [], () => {
      content.document
        .getElementById("optionB")
        .setAttribute("aria-selected", "true");
    });
    await evt;
    // Check the relationship between the listbox and its selected children
    // after selecting the second option.
    is(
      listbox.getAttributeValue("AXSelectedChildren").length,
      1,
      "Listbox has one selected child"
    );
    selectedChild = listbox.getAttributeValue("AXSelectedChildren")[0];
    is(
      selectedChild.getAttributeValue("AXValue"),
      "hello",
      "The second option is the selected child"
    );
    is(
      optionB.getAttributeValue("AXSelected"),
      1,
      "The second option has selected state"
    );
  }
);
