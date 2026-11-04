/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

addAccessibleTask(
  '<select id="select"></select>',
  async function (browser, accDoc) {
    const isSelectListBox = e => {
      return getAccessibleDOMNodeID(e.accessible.parent) == "select";
    };

    let select = findAccessibleChildByID(accDoc, "select");

    let onEvent = waitForEvent(EVENT_REORDER, isSelectListBox);
    // Create a combobox with grouping and 2 standalone options
    await invokeContentTask(browser, [], () => {
      let doc = content.document;
      let contentSelect = doc.getElementById("select");
      let optGroup = doc.createElement("optgroup");

      for (let i = 0; i < 2; i++) {
        let opt = doc.createElement("option");
        opt.value = i;
        opt.text = "Option: Value " + i;
        optGroup.appendChild(opt);
      }
      contentSelect.add(optGroup, null);

      for (let i = 0; i < 2; i++) {
        let opt = doc.createElement("option");
        contentSelect.add(opt, null);
      }
      contentSelect.firstChild.firstChild.id = "option1Node";
    });
    let event = await onEvent;
    let option1Node = findAccessibleChildByID(event.accessible, "option1Node");

    let tree = {
      COMBOBOX: [
        {
          COMBOBOX_LIST: [
            {
              GROUPING: [{ COMBOBOX_OPTION: [] }, { COMBOBOX_OPTION: [] }],
            },
            {
              COMBOBOX_OPTION: [],
            },
            {
              COMBOBOX_OPTION: [],
            },
          ],
        },
      ],
    };
    testAccessibleTree(select, tree);
    ok(!isDefunct(option1Node), "option shouldn't be defunct");

    onEvent = waitForEvent(EVENT_REORDER, isSelectListBox);
    // Remove grouping from combobox
    await invokeContentTask(browser, [], () => {
      let contentSelect = content.document.getElementById("select");
      contentSelect.firstChild.remove();
    });
    await onEvent;

    tree = {
      COMBOBOX: [
        {
          COMBOBOX_LIST: [{ COMBOBOX_OPTION: [] }, { COMBOBOX_OPTION: [] }],
        },
      ],
    };
    testAccessibleTree(select, tree);
    ok(
      isDefunct(option1Node),
      "removed option shouldn't be accessible anymore!"
    );

    onEvent = waitForEvent(EVENT_REORDER, isSelectListBox);
    // Remove all options from combobox
    await invokeContentTask(browser, [], () => {
      let contentSelect = content.document.getElementById("select");
      while (contentSelect.length) {
        contentSelect.remove(0);
      }
    });
    await onEvent;

    tree = {
      COMBOBOX: [
        {
          COMBOBOX_LIST: [],
        },
      ],
    };
    testAccessibleTree(select, tree);
  },
  { iframe: true, remoteIframe: true }
);
