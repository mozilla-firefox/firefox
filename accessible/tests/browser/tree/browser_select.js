/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/states.js */
loadScripts({ name: "states.js", dir: MOCHITESTS_DIR });

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Verify that the local accessible created in the parent process for the
 * id=ContentSelectDropdown node has its parent spoofed to the relevant
 * expanded select.
 * Note: id=select2 is unused and serves only to ensure we don't return
 * _just any_ select inside the document :)
 */
addAccessibleTask(
  `
<select id="select">
  <option id="a">optiona</option>
</select>
<select id="select2">
  <option id="b">optionb</option>
</select>
  `,
  async function testSelectAncestorChain(browser, accDoc) {
    const LOCAL_DROPDOWN_ID = "ContentSelectDropdown";

    const rootAcc = getRootAccessible(document);
    ok(rootAcc, "Root Accessible exists");

    const optA = findAccessibleChildByID(accDoc, "a");
    const select = findAccessibleChildByID(accDoc, "select");
    let remoteAccDropdown = select.firstChild;
    ok(remoteAccDropdown, "Remote acc dropdown exists");
    let isRemote = true;
    try {
      remoteAccDropdown.id;
    } catch (e) {
      isRemote = false;
    }

    // Verify the remote dropdown:
    // - is the role we expect
    // - is not identical to the local dropdown via ID
    // - is the parent of the remote accessible for the
    //   option the select contains
    if (isRemote) {
      is(
        remoteAccDropdown.role,
        ROLE_COMBOBOX_LIST,
        "Select's first child is the combobox list"
      );
      isnot(
        remoteAccDropdown.id,
        LOCAL_DROPDOWN_ID,
        "Remote dropdown does not match local dropdown's ID."
      );
      is(
        remoteAccDropdown.firstChild,
        optA,
        "Remote dropdown contains remote acc of option A."
      );
    }

    // Attempt to fetch the local dropdown
    let localAccDropdown = findAccessibleChildByID(rootAcc, LOCAL_DROPDOWN_ID);
    is(
      localAccDropdown,
      null,
      "Local dropdown cannot be reached while select is collapsed"
    );

    // Focus the combo box.
    await invokeFocus(browser, "select");

    // Expand the combobox dropdown.
    let p = waitForEvent(EVENT_STATE_CHANGE, LOCAL_DROPDOWN_ID);
    EventUtils.synthesizeKey("VK_SPACE");
    await p;

    // Attempt to fetch the local dropdown
    localAccDropdown = findAccessibleChildByID(rootAcc, LOCAL_DROPDOWN_ID);
    ok(localAccDropdown, "Local dropdown exists when select is expanded.");

    // Verify the dropdown rendered in the parent
    // process is a child of the select
    is(localAccDropdown.parent, select, "Dropdown is a child of the select");

    // Verify walking from the select produces the
    // appropriate option
    remoteAccDropdown = select.firstChild;

    // Verify the remote dropdown:
    // - is the role we expect
    // - is not identical to the local dropdown via ID
    // - is the parent of the remote accessible for the
    //   option the select contains
    if (isRemote) {
      is(
        remoteAccDropdown.role,
        ROLE_COMBOBOX_LIST,
        "Select's first child is the combobox list"
      );
      isnot(
        remoteAccDropdown.id,
        LOCAL_DROPDOWN_ID,
        "Remote dropdown does not match local dropdown's ID."
      );
      is(
        remoteAccDropdown.firstChild,
        optA,
        "Remote dropdown contains remote acc of option A."
      );
    }

    // Close the dropdown.
    p = waitForEvents({
      expected: [[EVENT_HIDE, LOCAL_DROPDOWN_ID]],
    });

    const popup = window.document.getElementById(LOCAL_DROPDOWN_ID).menupopup;
    if (popup.isNativeMenu) {
      // Synthesized events are not available with native menus
      popup.hidePopup();
    } else {
      EventUtils.synthesizeKey("VK_ESCAPE");
    }
    await p;

    // Verify walking down from the select produces the
    // appropriate option
    remoteAccDropdown = select.firstChild;

    // Verify the remote dropdown:
    // - is the role we expect
    // - is not identical to the local dropdown via ID
    // - is the parent of the remote accessible for the
    //   option the select contains
    if (isRemote) {
      is(
        remoteAccDropdown.role,
        ROLE_COMBOBOX_LIST,
        "Select's first child is the combobox list"
      );
      isnot(
        remoteAccDropdown.id,
        LOCAL_DROPDOWN_ID,
        "Remote dropdown does not match local dropdown's ID."
      );
      is(
        remoteAccDropdown.firstChild,
        optA,
        "Remote dropdown contains remote acc of option A."
      );
    }

    // Attempt to fetch the local dropdown
    localAccDropdown = findAccessibleChildByID(rootAcc, LOCAL_DROPDOWN_ID);
    is(
      localAccDropdown,
      null,
      "Local dropdown cannot be reached while select is collapsed"
    );
  },
  {
    chrome: true,
    topLevel: true,
    iframe: true,
    remoteIframe: true,
  }
);
