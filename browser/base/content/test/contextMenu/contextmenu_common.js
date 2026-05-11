// This file expects contextMenu to be defined in the scope it is loaded into.
/* global contextMenu:true */

var lastElement;
const FRAME_OS_PID = "context-frameOsPid";

function openContextMenuFor(element, shiftkey, waitForSpellCheck) {
  // Context menu should be closed before we open it again.
  is(
    SpecialPowers.wrap(contextMenu).state,
    "closed",
    "checking if popup is closed"
  );

  if (lastElement) {
    lastElement.blur();
  }
  element.focus();

  // Some elements need time to focus and spellcheck before any tests are
  // run on them.
  function actuallyOpenContextMenuFor() {
    lastElement = element;
    var eventDetails = { type: "contextmenu", button: 2, shiftKey: shiftkey };
    synthesizeMouse(element, 2, 2, eventDetails, element.documentGlobal);
  }

  if (waitForSpellCheck) {
    var { onSpellCheck } = SpecialPowers.ChromeUtils.importESModule(
      "resource://testing-common/AsyncSpellCheckTestHelper.sys.mjs"
    );
    onSpellCheck(element, actuallyOpenContextMenuFor);
  } else {
    actuallyOpenContextMenuFor();
  }
}

function closeContextMenu() {
  contextMenu.hidePopup();
}

function getVisibleMenuItems(aMenu) {
  var items = [];
  var accessKeys = {};
  for (var i = 0; i < aMenu.children.length; i++) {
    var item = aMenu.children[i];
    if (item.hidden) {
      continue;
    }

    var key = item.accessKey;
    if (key) {
      key = key.toLowerCase();
    }

    if (item.nodeName == "menuitem") {
      var isGenerated =
        item.classList.contains("spell-suggestion") ||
        item.classList.contains("sendtab-target");
      if (isGenerated) {
        is(item.id, "", "child menuitem #" + i + " is generated");
      } else {
        ok(item.id, "child menuitem #" + i + " has an ID");
      }
      var label = item.getAttribute("label");
      ok(label.length, "menuitem " + item.id + " has a label");
      if (isGenerated) {
        is(key, null, "Generated items shouldn't have an access key");
        items.push("*" + label);
      } else if (
        item.id.indexOf("spell-check-dictionary-") != 0 &&
        item.id != "spell-no-suggestions" &&
        item.id != "spell-add-dictionaries-main" &&
        item.id != "fill-login-no-logins" &&
        // Inspect accessibility properties does not have an access key. See
        // bug 1630717 for more details.
        item.id != "context-inspect-a11y" &&
        !item.id.includes("context-media-playbackrate") &&
        item.id != "context-copy-link-to-highlight" &&
        item.id != "context-copy-clean-link-to-highlight"
      ) {
        if (item.id != FRAME_OS_PID) {
          ok(key, "menuitem " + item.id + " has an access key");
        }
        // These entries already had overlapping access keys and we are keeping
        // their existing ones to avoid disorienting users by changing them.
        // The duplicate access keys do work, you just have to hit the access
        // key more than once to use them.
        // This test did not catch the duplicate access keys initially because
        // it only tests the entries in the context menu by default and the
        // Send Tab entry was originally only in the context menu if the user is
        // signed in. See Bug 2035895.
        const allowedAccessKeyDuplicates = [
          "context-translate-selection",
          "context-sendlinktodevice",
        ];

        if (
          accessKeys[key] &&
          !(
            allowedAccessKeyDuplicates.includes(item.id) &&
            allowedAccessKeyDuplicates.includes(accessKeys[key])
          )
        ) {
          ok(
            false,
            "menuitem " + item.id + " has same accesskey as " + accessKeys[key]
          );
        } else {
          accessKeys[key] = item.id;
        }
      }
      if (!isGenerated) {
        items.push(item.id);
      }
      items.push(!item.disabled);
    } else if (item.nodeName == "menuseparator") {
      ok(true, "--- seperator id is " + item.id);
      items.push("---");
      items.push(null);
    } else if (item.nodeName == "menu") {
      ok(item.id, "child menu #" + i + " has an ID");
      ok(key, "menu has an access key");
      if (accessKeys[key]) {
        ok(
          false,
          "menu " + item.id + " has same accesskey as " + accessKeys[key]
        );
      } else {
        accessKeys[key] = item.id;
      }
      items.push(item.id);
      items.push(!item.disabled);
      // Add a dummy item so that the indexes in checkMenu are the same
      // for expectedItems and actualItems.
      items.push([]);
      items.push(null);
    } else if (item.nodeName == "menugroup") {
      ok(item.id, "child menugroup #" + i + " has an ID");
      items.push(item.id);
      items.push(!item.disabled);
      var menugroupChildren = [];
      for (var child of item.children) {
        if (child.hidden) {
          continue;
        }

        menugroupChildren.push([child.id, !child.disabled]);
      }
      items.push(menugroupChildren);
      items.push(null);
    } else {
      ok(
        false,
        "child #" +
          i +
          " of menu ID " +
          aMenu.id +
          " has an unknown type (" +
          item.nodeName +
          ")"
      );
    }
  }
  return items;
}

function checkContextMenu(expectedItems) {
  is(contextMenu.state, "open", "checking if popup is open");
  var data = { generatedSubmenuId: 1 };
  checkMenu(contextMenu, expectedItems, data);
}

function checkMenuItem(
  actualItem,
  actualEnabled,
  expectedItem,
  expectedEnabled,
  index
) {
  is(
    `${actualItem}`,
    expectedItem,
    "checking item #" + index / 2 + " (" + expectedItem + ") name"
  );

  if (
    (typeof expectedEnabled == "object" && expectedEnabled != null) ||
    (typeof actualEnabled == "object" && actualEnabled != null)
  ) {
    ok(!(actualEnabled == null), "actualEnabled is not null");
    ok(!(expectedEnabled == null), "expectedEnabled is not null");
    is(typeof actualEnabled, typeof expectedEnabled, "checking types");

    if (
      typeof actualEnabled != typeof expectedEnabled ||
      actualEnabled == null ||
      expectedEnabled == null
    ) {
      return;
    }

    is(
      actualEnabled.type,
      expectedEnabled.type,
      "checking item #" + index / 2 + " (" + expectedItem + ") type attr value"
    );
    var icon = actualEnabled.icon;
    if (icon) {
      var tmp = "";
      var j = icon.length - 1;
      while (j && icon[j] != "/") {
        tmp = icon[j--] + tmp;
      }
      icon = tmp;
    }
    is(
      icon,
      expectedEnabled.icon,
      "checking item #" + index / 2 + " (" + expectedItem + ") icon attr value"
    );
    is(
      actualEnabled.checked,
      expectedEnabled.checked,
      "checking item #" + index / 2 + " (" + expectedItem + ") has checked attr"
    );
    is(
      actualEnabled.disabled,
      expectedEnabled.disabled,
      "checking item #" +
        index / 2 +
        " (" +
        expectedItem +
        ") has disabled attr"
    );
  } else if (expectedEnabled != null) {
    is(
      actualEnabled,
      expectedEnabled,
      "checking item #" + index / 2 + " (" + expectedItem + ") enabled state"
    );
  }
}

/*
 * checkMenu - checks to see if the specified <menupopup> contains the
 * expected items and state.
 * expectedItems is a array of (1) item IDs and (2) a boolean specifying if
 * the item is enabled or not (or null to ignore it). Submenus can be checked
 * by providing a nested array entry after the expected <menu> ID.
 * For example: ["blah", true,              // item enabled
 *               "submenu", null,           // submenu
 *                   ["sub1", true,         // submenu contents
 *                    "sub2", false], null, // submenu contents
 *               "lol", false]              // item disabled
 *
 */
function checkMenu(menu, expectedItems, data) {
  var actualItems = getVisibleMenuItems(menu, data);
  // ok(false, "Items are: " + actualItems);
  for (var i = 0; i < expectedItems.length; i += 2) {
    var actualItem = actualItems[i];
    var actualEnabled = actualItems[i + 1];
    var expectedItem = expectedItems[i];
    var expectedEnabled = expectedItems[i + 1];
    if (expectedItem instanceof Array) {
      ok(true, "Checking submenu/menugroup...");
      var previousId = expectedItems[i - 2]; // The last item was the menu ID.
      var previousItem = menu.getElementsByAttribute("id", previousId)[0];
      ok(
        previousItem,
        (previousItem ? previousItem.nodeName : "item") +
          " with previous id (" +
          previousId +
          ") found"
      );
      if (previousItem && previousItem.nodeName == "menu") {
        ok(previousItem, "got a submenu element of id='" + previousId + "'");
        is(
          previousItem.nodeName,
          "menu",
          "submenu element of id='" + previousId + "' has expected nodeName"
        );
        checkMenu(previousItem.menupopup, expectedItem, data, i);
      } else if (previousItem && previousItem.nodeName == "menugroup") {
        ok(expectedItem.length, "menugroup must not be empty");
        for (var j = 0; j < expectedItem.length / 2; j++) {
          checkMenuItem(
            actualItems[i][j][0],
            actualItems[i][j][1],
            expectedItem[j * 2],
            expectedItem[j * 2 + 1],
            i + j * 2
          );
        }
        i += j;
      } else {
        ok(false, "previous item is not a menu or menugroup");
      }
    } else {
      checkMenuItem(
        actualItem,
        actualEnabled,
        expectedItem,
        expectedEnabled,
        i
      );
    }
  }
  // Could find unexpected extra items at the end...
  is(
    actualItems.length,
    expectedItems.length,
    "checking expected number of menu entries"
  );
}

let lastElementSelector = null;
/**
 * Right-clicks on the element that matches `selector` and checks the
 * context menu that appears against the `menuItems` array.
 *
 * @param {string} selector
 *        A selector passed to querySelector to find
 *        the element that will be referenced.
 * @param {Array} menuItems
 *        An array of menuitem ids and their associated enabled state. A state
 *        of null means that it will be ignored. Ids of '---' are used for
 *        menuseparators.
 * @param {object} options, optional
 *        skipFocusChange: don't move focus to the element before test, useful
 *                         if you want to delay spell-check initialization
 *        offsetX: horizontal mouse offset from the top-left corner of
 *                 the element, optional
 *        offsetY: vertical mouse offset from the top-left corner of the
 *                 element, optional
 *        centered: if true, mouse position is centered in element, defaults
 *                  to true if offsetX and offsetY are not provided
 *        waitForSpellCheck: wait until spellcheck is initialized before
 *                           starting test
 *        preCheckContextMenuFn: callback to run before opening menu
 *        onContextMenuShown: callback to run when the context menu is shown
 *        postCheckContextMenuFn: callback to run after opening menu
 *        keepMenuOpen: if true, we do not call hidePopup, the consumer is
 *                      responsible for calling it.
 * @return {Promise} resolved after the test finishes
 */
async function test_contextmenu(selector, menuItems, options = {}) {
  contextMenu = document.getElementById("contentAreaContextMenu");
  is(contextMenu.state, "closed", "checking if popup is closed");

  // Default to centered if no positioning is defined.
  if (!options.offsetX && !options.offsetY) {
    options.centered = true;
  }

  if (!options.skipFocusChange) {
    await SpecialPowers.spawn(
      gBrowser.selectedBrowser,
      [lastElementSelector, selector],
      async function (contentLastElementSelector, contentSelector) {
        if (contentLastElementSelector) {
          let contentLastElement = content.document.querySelector(
            contentLastElementSelector
          );
          contentLastElement.blur();
        }
        let element = content.document.querySelector(contentSelector);
        element.focus();
      }
    );
    lastElementSelector = selector;
    info(`Moved focus to ${selector}`);
  }

  if (options.preCheckContextMenuFn) {
    await options.preCheckContextMenuFn();
    info("Completed preCheckContextMenuFn");
  }

  if (options.waitForSpellCheck) {
    info("Waiting for spell check");
    await SpecialPowers.spawn(
      gBrowser.selectedBrowser,
      [selector],
      async function (contentSelector) {
        let { onSpellCheck } = ChromeUtils.importESModule(
          "resource://testing-common/AsyncSpellCheckTestHelper.sys.mjs"
        );
        let element = content.document.querySelector(contentSelector);
        await new Promise(resolve => onSpellCheck(element, resolve));
        info("Spell check running");
      }
    );
  }

  let awaitPopupShown = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popupshown"
  );
  await BrowserTestUtils.synthesizeMouse(
    selector,
    options.offsetX || 0,
    options.offsetY || 0,
    {
      type: "contextmenu",
      button: 2,
      shiftkey: options.shiftkey,
      centered: options.centered,
    },
    gBrowser.selectedBrowser
  );
  await awaitPopupShown;
  info("Popup Shown");

  if (options.onContextMenuShown) {
    await options.onContextMenuShown();
    info("Completed onContextMenuShown");
  }

  if (
    typeof options.awaitOnMenuBuilt === "object" &&
    options.awaitOnMenuBuilt.id
  ) {
    const elementId = options.awaitOnMenuBuilt.id;
    const menu = document.getElementById(elementId);
    await TestUtils.waitForCondition(
      () => menu && !menu.hidden,
      `Menu ${elementId} did not appear in time`
    );
    info(`Menu "${elementId}" was built and is now visible`);
  }

  if (menuItems) {
    if (Services.prefs.getBoolPref("devtools.inspector.enabled", true)) {
      let inspectItems = [];
      let hasSeparatorAboveAskChat = false;
      let viewSourceIndex = menuItems.indexOf("context-viewsource");
      if (viewSourceIndex == -1) {
        viewSourceIndex = menuItems.indexOf(
          "context-viewpartialsource-selection"
        );
      }

      const askChatIndex = menuItems.indexOf("context-ask-chat");
      const isAskChatLastItem = menuItems.at(-6) === "context-ask-chat";
      if (askChatIndex >= 2) {
        hasSeparatorAboveAskChat = menuItems[askChatIndex - 2] === "---";
      }

      if (
        viewSourceIndex == -1 &&
        !(isAskChatLastItem && hasSeparatorAboveAskChat)
      ) {
        inspectItems.push("---", null);
      }

      if (
        Services.prefs.getBoolPref("devtools.accessibility.enabled", true) &&
        (Services.prefs.getBoolPref("devtools.everOpened", false) ||
          Services.prefs.getIntPref("devtools.selfxss.count", 0) > 0)
      ) {
        inspectItems.push("context-inspect-a11y", true);
      }
      inspectItems.push("context-inspect", true);

      if (viewSourceIndex == -1) {
        menuItems = menuItems.concat(inspectItems);
      } else {
        menuItems.splice(viewSourceIndex + 2, 0, ...inspectItems);
      }
    }

    checkContextMenu(menuItems);
  }

  let awaitPopupHidden = BrowserTestUtils.waitForEvent(
    contextMenu,
    "popuphidden"
  );

  if (options.postCheckContextMenuFn) {
    await options.postCheckContextMenuFn();
    info("Completed postCheckContextMenuFn");
  }

  if (!options.keepMenuOpen) {
    contextMenu.hidePopup();
    await awaitPopupHidden;
  }
}
