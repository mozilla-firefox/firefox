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

// Test dropdown select element
addAccessibleTask(
  `<select id="select" aria-label="Choose a number">
    <option id="one" selected>One</option>
    <option id="two">Two</option>
    <option id="three">Three</option>
    <option id="four" disabled>Four</option>
  </select>`,
  async (browser, accDoc) => {
    if (AppConstants.platform == "macosx") {
      // The OS handles accessibility events for native menus, so disable them
      await SpecialPowers.pushPrefEnv({
        set: [["widget.macos.allow-native-select", false]],
      });
    }
    // Test combobox
    let select = getNativeInterface(accDoc, "select");
    is(
      select.getAttributeValue("AXRole"),
      "AXPopUpButton",
      "select has AXPopupButton role"
    );
    ok(select.attributeNames.includes("AXValue"), "select advertises AXValue");
    is(
      select.getAttributeValue("AXValue"),
      "One",
      "select has correctt initial value"
    );
    ok(
      !select.attributeNames.includes("AXHasPopup"),
      "select does not advertise AXHasPopup"
    );
    is(
      select.getAttributeValue("AXHasPopup"),
      null,
      "select does not provide value for AXHasPopup"
    );

    ok(select.actionNames.includes("AXPress"), "Select has press action");
    // These four events happen in quick succession when select is pressed
    let events = Promise.all([
      waitForMacEvent("AXMenuOpened"),
      waitForMacEvent("AXSelectedChildrenChanged"),
      waitForMacEvent(
        "AXFocusedUIElementChanged",
        e => e.getAttributeValue("AXRole") == "AXPopUpButton"
      ),
      waitForMacEvent(
        "AXFocusedUIElementChanged",
        e => e.getAttributeValue("AXRole") == "AXMenuItem"
      ),
    ]);
    select.performAction("AXPress");
    // Only capture the target of AXMenuOpened (first element)
    let [menu] = await events;

    is(menu.getAttributeValue("AXRole"), "AXMenu", "dropdown has AXMenu role");
    is(
      menu.getAttributeValue("AXSelectedChildren").length,
      1,
      "dropdown has single selected child"
    );

    let selectedChildren = menu.getAttributeValue("AXSelectedChildren");
    is(selectedChildren.length, 1, "Only one child is selected");
    is(selectedChildren[0].getAttributeValue("AXRole"), "AXMenuItem");
    is(selectedChildren[0].getAttributeValue("AXTitle"), "One");

    let menuParent = menu.getAttributeValue("AXParent");
    is(
      menuParent.getAttributeValue("AXRole"),
      "AXPopUpButton",
      "dropdown parent is a popup button"
    );

    let menuItems = menu.getAttributeValue("AXChildren").map(c => {
      return [
        c.getAttributeValue("AXMenuItemMarkChar"),
        c.getAttributeValue("AXRole"),
        c.getAttributeValue("AXTitle"),
        c.getAttributeValue("AXEnabled"),
      ];
    });

    Assert.deepEqual(
      menuItems,
      [
        ["✓", "AXMenuItem", "One", true],
        [null, "AXMenuItem", "Two", true],
        [null, "AXMenuItem", "Three", true],
        [null, "AXMenuItem", "Four", false],
      ],
      "Menu items have correct checkmark on current value, correctt roles, correct titles, and correct AXEnabled value"
    );

    events = Promise.all([
      waitForMacEvent("AXSelectedChildrenChanged"),
      waitForMacEvent("AXFocusedUIElementChanged"),
    ]);
    EventUtils.synthesizeKey("KEY_ArrowDown");
    let [, menuItem] = await events;
    is(
      menuItem.getAttributeValue("AXTitle"),
      "Two",
      "Focused menu item has correct title"
    );

    selectedChildren = menu.getAttributeValue("AXSelectedChildren");
    is(selectedChildren.length, 1, "Only one child is selected");
    is(
      selectedChildren[0].getAttributeValue("AXTitle"),
      "Two",
      "Selected child matches focused item"
    );

    events = Promise.all([
      waitForMacEvent("AXSelectedChildrenChanged"),
      waitForMacEvent("AXFocusedUIElementChanged"),
    ]);
    EventUtils.synthesizeKey("KEY_ArrowDown");
    [, menuItem] = await events;
    is(
      menuItem.getAttributeValue("AXTitle"),
      "Three",
      "Focused menu item has correct title"
    );

    selectedChildren = menu.getAttributeValue("AXSelectedChildren");
    is(selectedChildren.length, 1, "Only one child is selected");
    is(
      selectedChildren[0].getAttributeValue("AXTitle"),
      "Three",
      "Selected child matches focused item"
    );

    events = Promise.all([
      waitForMacEvent("AXMenuClosed"),
      waitForMacEvent("AXFocusedUIElementChanged"),
      waitForMacEvent("AXSelectedChildrenChanged"),
    ]);
    menuItem.performAction("AXPress");
    let [, newFocus] = await events;
    is(
      newFocus.getAttributeValue("AXRole"),
      "AXPopUpButton",
      "Newly focused element is AXPopupButton"
    );
    is(
      newFocus.getAttributeValue("AXDOMIdentifier"),
      "select",
      "Should return focus to select"
    );
    is(
      newFocus.getAttributeValue("AXValue"),
      "Three",
      "select has correct new value"
    );
  }
);
