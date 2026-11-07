/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../mochitest/selectable.js */
/* import-globals-from ../../mochitest/states.js */

// ////////////////////////////////////////////////////////////////////////
// select@size="4" aka single selectable listbox
addAccessibleTask(
  `<select id="listbox" size="4">
    <option id="item1">option1</option>
    <option id="item2">option2</option>
   </select>`,
  async function (browser, docAcc) {
    info("select@size='4' aka single selectable listbox");
    let select = findAccessibleChildByID(docAcc, "listbox", [
      nsIAccessibleSelectable,
    ]);
    testSelectableSelection(select, []);

    // select 2nd item
    let promise = waitForStateChange("item2", STATE_SELECTED, true);
    select.addItemToSelection(1);
    await promise;
    testSelectableSelection(select, ["item2"], "addItemToSelection(1): ");

    // unselect 2nd item, 1st item gets selected automatically
    promise = waitForStateChange("item2", STATE_SELECTED, false);
    select.removeItemFromSelection(1);
    await promise;
    testSelectableSelection(select, [], "removeItemFromSelection(1): ");

    // doesn't change selection
    is(
      select.selectAll(),
      false,
      "No way to select all items in single selectable listbox"
    );
    testSelectableSelection(select, [], "selectAll: ");

    // doesn't change selection
    select.unselectAll();
    testSelectableSelection(select, [], "unselectAll: ");
  },
  {
    chrome: true,
    topLevel: true,
    iframe: true,
    remoteIframe: true,
  }
);

// ////////////////////////////////////////////////////////////////////////
// select@size="4" with optgroups, single selectable
addAccessibleTask(
  `<select id="listbox" size="4">
    <option id="item1">option1</option>
    <optgroup>optgroup>
      <option id="item2">option2</option>
    </optgroup>
   </select>`,
  async function (browser, docAcc) {
    info("select@size='4' with optgroups, single selectable");
    let select = findAccessibleChildByID(docAcc, "listbox", [
      nsIAccessibleSelectable,
    ]);
    testSelectableSelection(select, []);

    let promise = waitForStateChange("item2", STATE_SELECTED, true);
    select.addItemToSelection(1);
    await promise;
    testSelectableSelection(select, ["item2"]);

    promise = waitForStateChange("item2", STATE_SELECTED, false);
    select.removeItemFromSelection(1);
    await promise;
    testSelectableSelection(select, []);

    is(
      select.selectAll(),
      false,
      "No way to select all items in single selectable listbox"
    );
    testSelectableSelection(select, []);

    select.unselectAll();
    testSelectableSelection(select, []);
  },
  {
    chrome: true,
    topLevel: true,
    iframe: true,
    remoteIframe: true,
  }
);

// ////////////////////////////////////////////////////////////////////////
// select@size="4" multiselect aka listbox
addAccessibleTask(
  `<select id="listbox" size="4" multiple="true">
    <option id="item1">option1</option>
    <option id="item2">option2</option>
   </select>`,
  async function (browser, docAcc) {
    info("select@size='4' multiselect aka listbox");
    let select = findAccessibleChildByID(docAcc, "listbox", [
      nsIAccessibleSelectable,
    ]);
    await testMultiSelectable(
      select,
      ["item1", "item2"],
      "select@size='4' multiselect aka listbox "
    );
  },
  {
    chrome: true,
    topLevel: true,
    iframe: true,
    remoteIframe: true,
  }
);

// ////////////////////////////////////////////////////////////////////////
// select@size="4" multiselect with optgroups
addAccessibleTask(
  `<select id="listbox" size="4" multiple="true">
    <option id="item1">option1</option>
    <optgroup>optgroup>
      <option id="item2">option2</option>
    </optgroup>
   </select>`,
  async function (browser, docAcc) {
    info("select@size='4' multiselect with optgroups");
    let select = findAccessibleChildByID(docAcc, "listbox", [
      nsIAccessibleSelectable,
    ]);
    await testMultiSelectable(
      select,
      ["item1", "item2"],
      "select@size='4' multiselect aka listbox "
    );
  },
  {
    chrome: true,
    topLevel: true,
    iframe: true,
    remoteIframe: true,
  }
);

// ////////////////////////////////////////////////////////////////////////
// multiselect with coalesced selection event
addAccessibleTask(
  `<select id="listbox" size="4" multiple="true">
    <option id="item1">option1</option>
    <option id="item2">option2</option>
    <option id="item3">option3</option>
    <option id="item4">option4</option>
    <option id="item5">option5</option>
    <option id="item6">option6</option>
    <option id="item7">option7</option>
    <option id="item8">option8</option>
    <option id="item9">option9</option>
   </select>`,
  async function (browser, docAcc) {
    info("select@size='4' multiselect with coalesced selection event");
    let select = findAccessibleChildByID(docAcc, "listbox", [
      nsIAccessibleSelectable,
    ]);
    await testMultiSelectable(
      select,
      [
        "item1",
        "item2",
        "item3",
        "item4",
        "item5",
        "item6",
        "item7",
        "item8",
        "item9",
      ],
      "select@size='4' multiselect with coalesced selection event "
    );
  },
  {
    chrome: false,
    topLevel: true,
    iframe: false,
    remoteIframe: false,
  }
);
