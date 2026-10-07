/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* eslint no-unused-vars: [2, {"vars": "local", caughtErrors: "none"}] */
/* import-globals-from ../../shared/test/shared-head.js */

// Sometimes HTML pages have a `clear` function that cleans up the storage they
// created. To make sure it's always called, we are registering as a cleanup
// function, but since this needs to run before tabs are closed, we need to
// do this registration before importing `shared-head`, since declaration
// order matters.
registerCleanupFunction(async () => {
  Services.cookies.removeAll();

  // Close tabs and force memory collection to happen
  while (gBrowser.tabs.length > 1) {
    const browser = gBrowser.selectedBrowser;
    const contexts = browser.browsingContext.getAllBrowsingContextsInSubtree();
    for (const context of contexts) {
      await SpecialPowers.spawn(context, [], async () => {
        const win = content.wrappedJSObject;

        // Some windows (e.g., about: URLs) don't have storage available
        try {
          win.localStorage.clear();
          win.sessionStorage.clear();
        } catch (ex) {
          // ignore
        }

        if (win.clear) {
          // Do not get hung into win.clear() forever
          await Promise.race([
            new Promise(r => win.setTimeout(r, 10000)),
            win.clear(),
          ]);
        }
      });
    }

    await closeTabAndToolbox(gBrowser.selectedTab);
  }
  forceCollections();
});

// shared-head.js handles imports, constants, and utility functions
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this
);

const {
  TableWidget,
} = require("resource://devtools/client/shared/widgets/TableWidget.js");
const {
  LocalTabCommandsFactory,
} = require("resource://devtools/client/framework/local-tab-commands-factory.js");
const STORAGE_PREF = "devtools.storage.enabled";
const DUMPEMIT_PREF = "devtools.dump.emit";

// Allows Cache API to be working on usage `http` test page
const CACHES_ON_HTTP_PREF = "dom.caches.testing.enabled";
const PATH = "browser/devtools/client/storage/test/";
const MAIN_DOMAIN = "example.org";
const MAIN_HOST = "test1.example.org";

const MAIN_ORIGIN = "http://test1.example.org";
const MAIN_ORIGIN_SECURED = "https://test1.example.org";
const MAIN_URL = MAIN_ORIGIN + "/" + PATH;
const MAIN_URL_SECURED = MAIN_ORIGIN_SECURED + "/" + PATH;
const MAIN_URL_WITH_PORT = MAIN_ORIGIN + ":8000/" + PATH;

const ALT_ORIGIN = "http://sectest1.example.org";
const ALT_ORIGIN_SECURED = "https://sectest1.example.org";
const ALT_URL = ALT_ORIGIN + "/" + PATH;
const ALT_URL_SECURED = ALT_ORIGIN_SECURED + ":443/" + PATH;

// GUID to be used as a separator in compound keys. This must match the same
// constant in devtools/server/actors/resources/storage/index.js,
// devtools/client/storage/ui.js and devtools/server/tests/browser/head.js
const SEPARATOR_GUID = "{9d414cc5-8319-0a04-0586-c0a6ae01670a}";

const getExtensionStorageUniqueKey = (area, name) =>
  name + SEPARATOR_GUID + area;

var gToolbox, gPanelWindow, gUI;

// Services.prefs.setBoolPref(DUMPEMIT_PREF, true);

Services.prefs.setBoolPref(STORAGE_PREF, true);
Services.prefs.setBoolPref(CACHES_ON_HTTP_PREF, true);
registerCleanupFunction(() => {
  gToolbox = gPanelWindow = gUI = null;
  Services.prefs.clearUserPref(CACHES_ON_HTTP_PREF);
  Services.prefs.clearUserPref(DUMPEMIT_PREF);
  Services.prefs.clearUserPref(STORAGE_PREF);
});

/**
 * This generator function opens the given url in a new tab, then sets up the
 * page by waiting for all cookies, indexedDB items etc.
 *
 * @param url {String} The url to be opened in the new tab
 * @param options {Object} The tab options for the new tab
 *
 * @return {Promise} A promise that resolves after the tab is ready
 */
async function openTab(url, options = {}) {
  const tab = await addTab(url, options);

  const browser = gBrowser.selectedBrowser;
  const contexts = browser.browsingContext.getAllBrowsingContextsInSubtree();

  for (const context of contexts) {
    await SpecialPowers.spawn(context, [], async () => {
      const win = content.wrappedJSObject;
      const readyState = win.document.readyState;
      info(`Found a window: ${readyState}`);
      if (readyState != "complete") {
        await new Promise(resolve => {
          const onLoad = () => {
            win.removeEventListener("load", onLoad);
            resolve();
          };
          win.addEventListener("load", onLoad);
        });
      }
      if (win.setup) {
        await win.setup();
      }
    });
  }

  return tab;
}

/**
 * This generator function opens the given url in a new tab, then sets up the
 * page by waiting for all cookies, indexedDB items etc. to be created; Then
 * opens the storage inspector and waits for the storage tree and table to be
 * populated.
 *
 * @param url {String} The url to be opened in the new tab
 * @param options {Object} The tab options for the new tab
 *
 * @return {Promise} A promise that resolves after storage inspector is ready
 */
async function openTabAndSetupStorage(url, options = {}) {
  // open tab
  await openTab(url, options);

  // open storage inspector
  return openStoragePanel();
}

/**
 * Open a toolbox with the storage panel opened by default
 * for a given Web Extension.
 *
 * @param {string} addonId
 *        The ID of the Web Extension to debug.
 */
var openStoragePanelForAddon = async function (addonId) {
  const toolbox = await gDevTools.showToolboxForWebExtension(addonId, {
    toolId: "storage",
  });

  info("Making sure that the toolbox's frame is focused");
  await SimpleTest.promiseFocus(toolbox.win);

  const storage = _setupStoragePanelForTest(toolbox);

  return {
    toolbox,
    storage,
  };
};

/**
 * Open the toolbox, with the storage tool visible.
 *
 * @param tab {XULTab} Optional, the tab for the toolbox; defaults to selected tab
 * @param commands {Object} Optional, the commands for the toolbox; defaults to a tab commands
 * @param hostType {Toolbox.HostType} Optional, type of host that will host the toolbox
 *
 * @return {Promise} a promise that resolves when the storage inspector is ready
 */
var openStoragePanel = async function ({ tab, hostType } = {}) {
  const toolbox = await openToolboxForTab(
    tab || gBrowser.selectedTab,
    "storage",
    hostType
  );

  const storage = _setupStoragePanelForTest(toolbox);

  return {
    toolbox,
    storage,
  };
};

/**
 * Set global variables needed in helper functions
 *
 * @param toolbox {Toolbox}
 * @return {StoragePanel}
 */
function _setupStoragePanelForTest(toolbox) {
  const storage = toolbox.getPanel("storage");
  gPanelWindow = storage.panelWindow;
  gUI = storage.UI;
  gToolbox = toolbox;

  // The table animation flash causes some timeouts on Linux debug tests,
  // so we disable it
  gUI.animationsEnabled = false;

  return storage;
}

/**
 * Forces GC, CC and Shrinking GC to get rid of disconnected docshells and
 * windows.
 */
function forceCollections() {
  Cu.forceGC();
  Cu.forceCC();
  Cu.forceShrinkingGC();
}

// Sends a click event on the passed DOM node in an async manner
function click(node) {
  node.scrollIntoView();

  return new Promise(resolve => {
    // We need setTimeout here to allow any scrolling to complete before clicking
    // the node.
    setTimeout(() => {
      node.click();
      resolve();
    }, 200);
  });
}

/**
 * Recursively expand the variables view up to a given property.
 *
 * @param options
 *        Options for view expansion:
 *        - rootVariable: start from the given scope/variable/property.
 *        - expandTo: string made up of property names you want to expand.
 *        For example: "body.firstChild.nextSibling" given |rootVariable:
 *        document|.
 * @return object
 *         A promise that is resolved only when the last property in |expandTo|
 *         is found, and rejected otherwise. Resolution reason is always the
 *         last property - |nextSibling| in the example above. Rejection is
 *         always the last property that was found.
 */
function variablesViewExpandTo(options) {
  const root = options.rootVariable;
  const expandTo = options.expandTo.split(".");

  return new Promise((resolve, reject) => {
    function getNext(prop) {
      const name = expandTo.shift();
      const newProp = prop.get(name);

      if (expandTo.length) {
        ok(newProp, "found property " + name);
        if (newProp && newProp.expand) {
          newProp.expand();
          getNext(newProp);
        } else {
          reject(prop);
        }
      } else if (newProp) {
        resolve(newProp);
      } else {
        reject(prop);
      }
    }

    if (root && root.expand) {
      root.expand();
      getNext(root);
    } else {
      resolve(root);
    }
  });
}

/**
 * Find variables or properties in a VariablesView instance.
 *
 * @param array ruleArray
 *        The array of rules you want to match. Each rule is an object with:
 *        - name (string|regexp): property name to match.
 *        - value (string|regexp): property value to match.
 *        - dontMatch (boolean): make sure the rule doesn't match any property.
 * @param boolean parsed
 *        true if we want to test the rules in the parse value section of the
 *        storage sidebar
 * @return object
 *         A promise object that is resolved when all the rules complete
 *         matching. The resolved callback is given an array of all the rules
 *         you wanted to check. Each rule has a new property: |matchedProp|
 *         which holds a reference to the Property object instance from the
 *         VariablesView. If the rule did not match, then |matchedProp| is
 *         undefined.
 */
function findVariableViewProperties(ruleArray, parsed) {
  // Initialize the search.
  function init() {
    // If parsed is true, we are checking rules in the parsed value section of
    // the storage sidebar. That scope uses a blank variable as a placeholder
    // Thus, adding a blank parent to each name
    if (parsed) {
      ruleArray = ruleArray.map(({ name, value, dontMatch }) => {
        return { name: "." + name, value, dontMatch };
      });
    }
    // Separate out the rules that require expanding properties throughout the
    // view.
    const expandRules = [];
    const rules = ruleArray.filter(rule => {
      if (typeof rule.name == "string" && rule.name.indexOf(".") > -1) {
        expandRules.push(rule);
        return false;
      }
      return true;
    });

    // Search through the view those rules that do not require any properties to
    // be expanded. Build the array of matchers, outstanding promises to be
    // resolved.
    const outstanding = [];

    finder(rules, gUI.view, outstanding);

    // Process the rules that need to expand properties.
    const lastStep = processExpandRules.bind(null, expandRules);

    // Return the results - a promise resolved to hold the updated ruleArray.
    const returnResults = onAllRulesMatched.bind(null, ruleArray);

    return Promise.all(outstanding).then(lastStep).then(returnResults);
  }

  function onMatch(prop, rule, matched) {
    if (matched && !rule.matchedProp) {
      rule.matchedProp = prop;
    }
  }

  function finder(rules, view, promises) {
    for (const scope of view) {
      for (const [, prop] of scope) {
        for (const rule of rules) {
          const matcher = matchVariablesViewProperty(prop, rule);
          promises.push(matcher.then(onMatch.bind(null, prop, rule)));
        }
      }
    }
  }

  function processExpandRules(rules) {
    return new Promise(resolve => {
      const rule = rules.shift();
      if (!rule) {
        resolve(null);
      }

      const expandOptions = {
        rootVariable: gUI.view.getScopeAtIndex(parsed ? 1 : 0),
        expandTo: rule.name,
      };

      variablesViewExpandTo(expandOptions)
        .then(
          function onSuccess(prop) {
            const name = rule.name;
            const lastName = name.split(".").pop();
            rule.name = lastName;

            const matched = matchVariablesViewProperty(prop, rule);
            return matched
              .then(onMatch.bind(null, prop, rule))
              .then(function () {
                rule.name = name;
              });
          },
          function onFailure() {
            resolve(null);
          }
        )
        .then(processExpandRules.bind(null, rules))
        .then(function () {
          resolve(null);
        });
    });
  }

  function onAllRulesMatched(rules) {
    for (const rule of rules) {
      const matched = rule.matchedProp;
      if (matched && !rule.dontMatch) {
        ok(true, "rule " + rule.name + " matched for property " + matched.name);
      } else if (matched && rule.dontMatch) {
        ok(
          false,
          "rule " + rule.name + " should not match property " + matched.name
        );
      } else {
        ok(rule.dontMatch, "rule " + rule.name + " did not match any property");
      }
    }
    return rules;
  }

  return init();
}

/**
 * Check if a given Property object from the variables view matches the given
 * rule.
 *
 * @param object prop
 *        The variable's view Property instance.
 * @param object rule
 *        Rules for matching the property. See findVariableViewProperties() for
 *        details.
 * @return object
 *         A promise that is resolved when all the checks complete. Resolution
 *         result is a boolean that tells your promise callback the match
 *         result: true or false.
 */
function matchVariablesViewProperty(prop, rule) {
  function resolve(result) {
    return Promise.resolve(result);
  }

  if (!prop) {
    return resolve(false);
  }

  // Any kind of string is accepted as name, including empty ones
  if (typeof rule.name == "string") {
    const match =
      rule.name instanceof RegExp
        ? rule.name.test(prop.name)
        : prop.name == rule.name;
    if (!match) {
      return resolve(false);
    }
  }

  if ("value" in rule) {
    let displayValue = prop.displayValue;
    if (prop.displayValueClassName == "token-string") {
      displayValue = displayValue.substring(1, displayValue.length - 1);
    }

    const match =
      rule.value instanceof RegExp
        ? rule.value.test(displayValue)
        : displayValue == rule.value;
    if (!match) {
      info(
        "rule " +
          rule.name +
          " did not match value, expected '" +
          rule.value +
          "', found '" +
          displayValue +
          "'"
      );
      return resolve(false);
    }
  }

  return resolve(true);
}

/**
 * Click selects a row in the table.
 *
 * @param {[string]} ids
 *        The array id of the item in the tree
 * @param {object=} options
 * @param {boolean=} waitForItem
 *        Wait for the item to be available in the tree. Defaults to true. Force
 *        to false if selecting the item is optional AND the item is not
 *        guaranteed to be available.
 */
async function selectTreeItem(ids, options = {}) {
  const { waitForItem = true } = options;

  if (gUI.tree.isSelected(ids)) {
    info(`"${ids}" is already selected, returning.`);
    return;
  }

  if (!gUI.tree.exists(ids)) {
    if (!waitForItem) {
      info(`${ids} is unavailable and waitForItem=false, returning`);
      return;
    }

    info(`Wait until ${ids} the expected item appears in the tree`);
    await waitFor(
      () => gUI.tree.exists(ids),
      `Waiting until ${ids} exists in the storage tree`
    );
  }

  // The item exists but is not selected... select it.
  info(`Selecting "${ids}".`);
  if (ids.length > 1) {
    const updated = gUI.once("store-objects-updated");
    gUI.tree.selectedItem = ids;
    await updated;
  } else {
    // If the length of the IDs array is 1, a storage type
    // gets selected and no 'store-objects-updated' event
    // will be fired in that case.
    gUI.tree.selectedItem = ids;
  }
}

/**
 * Click selects a row in the table.
 *
 * @param {string} id
 *        The id of the row in the table widget
 */
async function selectTableItem(id) {
  const table = gUI.table;
  const selector =
    ".table-widget-column#" +
    table.uniqueId +
    " .table-widget-cell[value='" +
    id +
    "']";
  const target = gPanelWindow.document.querySelector(selector);

  ok(target, `row found with id "${id}"`);

  if (!target) {
    showAvailableIds();
  }

  const updated = gUI.once("sidebar-updated");

  info(`selecting row "${id}"`);
  await click(target);
  await updated;
}

/**
 * Wait for eventName on target.
 *
 * @param {object} target An observable object that either supports on/off or
 * addEventListener/removeEventListener
 * @param {string} eventName
 * @param {boolean} useCapture Optional, for addEventListener/removeEventListener
 * @return A promise that resolves when the event has been handled
 */
function once(target, eventName, useCapture = false) {
  info("Waiting for event: '" + eventName + "' on " + target + ".");

  return new Promise(resolve => {
    for (const [add, remove] of [
      ["addEventListener", "removeEventListener"],
      ["addListener", "removeListener"],
      ["on", "off"],
    ]) {
      if (add in target && remove in target) {
        target[add](
          eventName,
          function onEvent(...aArgs) {
            info("Got event: '" + eventName + "' on " + target + ".");
            target[remove](eventName, onEvent, useCapture);
            resolve(...aArgs);
          },
          useCapture
        );
        break;
      }
    }
  });
}

/**
 * Get values for a row.
 *
 * @param  {string}  id
 *         The uniqueId of the given row.
 * @param  {boolean} includeHidden
 *         Include hidden columns.
 *
 * @return {object}
 *         An object of column names to values for the given row.
 */
function getRowValues(id, includeHidden = false) {
  const cells = getRowCells(id, includeHidden);
  const values = {};

  for (const name in cells) {
    const cell = cells[name];

    values[name] = cell.value;
  }

  return values;
}

/**
 * Get the row element for a given id
 *
 * @param  {string}  id
 *         The uniqueId of the given row.
 * @returns {Element|null}
 */
function getRowItem(id) {
  const doc = gPanelWindow.document;
  const table = gUI.table;
  return doc.querySelector(
    `.table-widget-column#${table.uniqueId} .table-widget-cell[value='${id}']`
  );
}

/**
 * Get cells for a row.
 *
 * @param  {string}  id
 *         The uniqueId of the given row.
 * @param  {boolean} includeHidden
 *         Include hidden columns.
 *
 * @return {object}
 *         An object of column names to cells for the given row.
 */
function getRowCells(id, includeHidden = false) {
  const table = gUI.table;
  const item = getRowItem(id);

  if (!item) {
    ok(
      false,
      `The row id '${id}' that was passed to getRowCells() does not ` +
        `exist. ${getAvailableIds()}`
    );
  }

  const index = table.columns.get(table.uniqueId).cellNodes.indexOf(item);
  const cells = {};

  for (const [name, column] of [...table.columns]) {
    if (!includeHidden && column.column.parentNode.hidden) {
      continue;
    }
    cells[name] = column.cellNodes[index];
  }

  return cells;
}

/**
 * Check for an empty table.
 */
function isTableEmpty() {
  const doc = gPanelWindow.document;
  const table = gUI.table;
  const cells = doc.querySelectorAll(
    ".table-widget-column#" + table.uniqueId + " .table-widget-cell"
  );
  return cells.length === 0;
}

/**
 * Get available ids... useful for error reporting.
 */
function getAvailableIds() {
  const doc = gPanelWindow.document;
  const table = gUI.table;

  let out = "Available ids:\n";
  const cells = doc.querySelectorAll(
    ".table-widget-column#" + table.uniqueId + " .table-widget-cell"
  );
  for (const cell of cells) {
    out += `  - ${cell.getAttribute("value")}\n`;
  }

  return out;
}

/**
 * Show available ids.
 */
function showAvailableIds() {
  info(getAvailableIds());
}

/**
 * Get a cell value.
 *
 * @param {string} id
 *        The uniqueId of the row.
 * @param {string} column
 *        The id of the column
 *
 * @yield {string}
 *        The cell value.
 */
function getCellValue(id, column) {
  const row = getRowValues(id, true);

  if (typeof row[column] === "undefined") {
    let out = "";
    for (const key in row) {
      const value = row[key];

      out += `  - ${key} = ${value}\n`;
    }

    ok(
      false,
      `The column name '${column}' that was passed to ` +
        `getCellValue() does not exist. Current column names and row ` +
        `values are:\n${out}`
    );
  }

  return row[column];
}

/**
 * Edit a cell value. The cell is assumed to be in edit mode, see startCellEdit.
 *
 * @param {string} id
 *        The uniqueId of the row.
 * @param {string} column
 *        The id of the column
 * @param {string} newValue
 *        Replacement value.
 * @param {boolean} validate
 *        Validate result? Default true.
 *
 * @yield {string}
 *        The uniqueId of the changed row.
 */
async function editCell(id, column, newValue, validate = true) {
  const row = getRowCells(id, true);
  const editableFieldsEngine = gUI.table.editableFieldsEngine;

  editableFieldsEngine.edit(row[column]);

  await typeWithTerminator(newValue, "KEY_Enter", validate);
}

/**
 * Begin edit mode for a cell.
 *
 * @param {string} id
 *        The uniqueId of the row.
 * @param {string} column
 *        The id of the column
 * @param {boolean} selectText
 *        Select text? Default true.
 */
function startCellEdit(id, column, selectText = true) {
  const row = getRowCells(id, true);
  const editableFieldsEngine = gUI.table.editableFieldsEngine;
  const cell = row[column];

  info("Selecting row " + id);
  gUI.table.selectedRow = id;

  info("Starting cell edit (" + id + ", " + column + ")");
  editableFieldsEngine.edit(cell);

  if (!selectText) {
    const textbox = gUI.table.editableFieldsEngine.textbox;
    textbox.selectionEnd = textbox.selectionStart;
  }
}

/**
 * Check a cell value.
 *
 * @param {string} id
 *        The uniqueId of the row.
 * @param {string} column
 *        The id of the column
 * @param {string} expected
 *        Expected value.
 */
function checkCell(id, column, expected) {
  is(
    getCellValue(id, column),
    expected,
    column + " column has the right value for " + id
  );
}

/**
 * Check that a cell is not in edit mode.
 *
 * @param {string} id
 *        The uniqueId of the row.
 * @param {string} column
 *        The id of the column
 */
function checkCellUneditable(id, column) {
  const row = getRowCells(id, true);
  const cell = row[column];

  const editableFieldsEngine = gUI.table.editableFieldsEngine;
  const textbox = editableFieldsEngine.textbox;

  // When a field is being edited, the cell is hidden, and the textbox is made visible.
  ok(
    !cell.hidden && textbox.hidden,
    `The cell located in column ${column} and row ${id} is not editable.`
  );
}

/**
 * Show or hide a column.
 *
 * @param  {string} id
 *         The uniqueId of the given column.
 * @param  {boolean} state
 *         true = show, false = hide
 */
function showColumn(id, state) {
  const columns = gUI.table.columns;
  const column = columns.get(id);
  column.column.hidden = !state;
}

/**
 * Toggle sort direction on a column by clicking on the column header.
 *
 * @param  {string} id
 *         The uniqueId of the given column.
 */
function clickColumnHeader(id) {
  const columns = gUI.table.columns;
  const column = columns.get(id);
  const header = column.header;

  header.click();
}

/**
 * Show or hide all columns.
 *
 * @param  {boolean} state
 *         true = show, false = hide
 */
function showAllColumns(state) {
  const columns = gUI.table.columns;

  for (const [id] of columns) {
    showColumn(id, state);
  }
}

/**
 * Type a string in the currently selected editor and then wait for the row to
 * be updated.
 *
 * @param  {string} str
 *         The string to type.
 * @param  {string} terminator
 *         The terminating key e.g. KEY_Enter or KEY_Tab
 * @param  {boolean} validate
 *         Validate result? Default true.
 */
async function typeWithTerminator(str, terminator, validate = true) {
  const editableFieldsEngine = gUI.table.editableFieldsEngine;
  const textbox = editableFieldsEngine.textbox;
  const colName = textbox.closest(".table-widget-column").id;

  const changeExpected = str !== textbox.value;

  if (!changeExpected) {
    return editableFieldsEngine.currentTarget.getAttribute("data-id");
  }

  info("Typing " + str);
  EventUtils.sendString(str, gPanelWindow);

  info("Pressing " + terminator);
  EventUtils.synthesizeKey(terminator, null, gPanelWindow);

  if (validate) {
    info("Validating results... waiting for ROW_EDIT event.");
    const uniqueId = await gUI.table.once(TableWidget.EVENTS.ROW_EDIT);

    checkCell(uniqueId, colName, str);
    return uniqueId;
  }

  return gUI.table.once(TableWidget.EVENTS.ROW_EDIT);
}

function getCurrentEditorValue() {
  const editableFieldsEngine = gUI.table.editableFieldsEngine;
  const textbox = editableFieldsEngine.textbox;

  return textbox.value;
}

/**
 * Press a key x times.
 *
 * @param  {string} key
 *         The key to press e.g. VK_RETURN or VK_TAB
 * @param {number} x
 *         The number of times to press the key.
 * @param {object} modifiers
 *         The event modifier e.g. {shiftKey: true}
 */
function PressKeyXTimes(key, x, modifiers = {}) {
  for (let i = 0; i < x; i++) {
    EventUtils.synthesizeKey(key, modifiers);
  }
}

/**
 * Verify the storage inspector state: check that given type/host exists
 * in the tree, and that the table contains rows with specified names.
 *
 * @param {Array} state Array of state specifications. For example,
 *        [["cookies", "example.com"], ["c1", "c2"]] means to select the
 *        "example.com" host in cookies and then verify there are "c1" and "c2"
 *        cookies (and no other ones).
 */
async function checkState(state) {
  for (const [store, names] of state) {
    const storeName = store.join(" > ");
    info(`Selecting tree item ${storeName}`);
    // Item might be unavailable, set waitForItem=false.
    await selectTreeItem(store, { waitForItem: false });

    const items = gUI.table.items;

    is(
      items.size,
      names.length,
      `There is correct number of rows in ${storeName}`
    );

    if (names.length === 0) {
      showAvailableIds();
    }

    for (const name of names) {
      if (!items.has(name)) {
        showAvailableIds();
      }
      ok(items.has(name), `There is item with name '${name}' in ${storeName}`);
    }
  }
}

/**
 * Checks if document's active element is within the given element.
 *
 * @param  {HTMLDocument}  doc document with active element in question
 * @param  {DOMNode}       container element tested on focus containment
 * @return {boolean}
 */
function containsFocus(doc, container) {
  let elm = doc.activeElement;
  while (elm) {
    if (elm === container) {
      return true;
    }
    elm = elm.parentNode;
  }
  return false;
}

var focusSearchBoxUsingShortcut = async function (panelWin, callback) {
  info("Focusing search box");
  const searchBox = panelWin.document.getElementById("storage-searchbox");
  const focused = once(searchBox, "focus");

  panelWin.focus();

  const shortcut =
    await panelWin.document.l10n.formatValue("storage-filter-key");
  synthesizeKeyShortcut(shortcut);

  await focused;

  if (callback) {
    callback();
  }
};

function getCookieId(name, domain, path, partitionKey = "") {
  return `${name}${SEPARATOR_GUID}${domain}${SEPARATOR_GUID}${path}${SEPARATOR_GUID}${partitionKey}`;
}

function setPermission(url, permission) {
  const nsIPermissionManager = Ci.nsIPermissionManager;

  const uri = Services.io.newURI(url);
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );

  Cc["@mozilla.org/permissionmanager;1"]
    .getService(nsIPermissionManager)
    .addFromPrincipal(principal, permission, nsIPermissionManager.ALLOW_ACTION);
  registerCleanupFunction(() =>
    Services.perms.removeFromPrincipal(principal, permission)
  );
}

function toggleSidebar() {
  gUI.sidebarToggleBtn.click();
}

function sidebarToggleVisible() {
  return !gUI.sidebarToggleBtn.hidden;
}

/**
 * Check whether the variables view in the sidebar contains a tree.
 *
 * @param  {boolean} state
 *         Should a tree be visible?
 */
function sidebarParseTreeVisible(state) {
  if (state) {
    Assert.greater(
      gUI.view._testOnlyHierarchy.size,
      2,
      "Parse tree should be visible."
    );
  } else {
    Assert.lessOrEqual(
      gUI.view._testOnlyHierarchy.size,
      2,
      "Parse tree should not be visible."
    );
  }
}

/**
 * Add an item.
 *
 * @param  {Array} store
 *         An array containing the path to the store to which we wish to add an
 *         item.
 * @return {Promise} A Promise that resolves to the row id of the added item.
 */
async function performAdd(store) {
  const storeName = store.join(" > ");
  const toolbar = gPanelWindow.document.getElementById("storage-toolbar");
  const type = store[0];

  // Set waitForItem=false, there might not be any item in the table before
  // using add.
  await selectTreeItem(store, { waitForItem: false });

  const menuAdd = toolbar.querySelector("#add-button");

  if (menuAdd.hidden) {
    is(
      menuAdd.hidden,
      false,
      `performAdd called for ${storeName} but it is not supported`
    );
    return "";
  }

  const eventEdit = gUI.table.once("row-edit");
  const eventWait = gUI.once("store-objects-edit");

  menuAdd.click();

  const rowId = await eventEdit;
  await eventWait;

  const key = type === "cookies" ? "uniqueKey" : "name";
  const value = getCellValue(rowId, key);

  is(rowId, value, `Row '${rowId}' was successfully added.`);

  return rowId;
}

/**
 * Remove all items from a store.
 *
 * @param  {Array} store
 *         An array containing the path to the store from which we wish to remove all items.
 */
async function performRemoveAll(store) {
  const storeName = store.join(" > ");
  const toolbar = gPanelWindow.document.getElementById("storage-toolbar");

  // Set waitForItem=false, there might not be any item in the table before
  // using remove all.
  await selectTreeItem(store, { waitForItem: false });

  const menuDeleteAll = toolbar.querySelector("#delete-all-button");

  if (menuDeleteAll.hidden) {
    is(
      menuDeleteAll.hidden,
      false,
      `performRemoveAll called for ${storeName} but it is not supported`
    );
    return;
  }

  menuDeleteAll.click();

  // Wait for the table to become empty
  await BrowserTestUtils.waitForCondition(
    () => getCellLength() === 0,
    `All items removed from ${storeName}`,
    500,
    100
  );

  is(
    getCellLength(),
    0,
    `All items were successfully removed from ${storeName}.`
  );
}

// Cell css selector that can be used to count or select cells.
// The selector is restricted to a single column to avoid counting duplicates.
const CELL_SELECTOR =
  "#storage-table .table-widget-column:first-child .table-widget-cell";

function getCellLength() {
  return gPanelWindow.document.querySelectorAll(CELL_SELECTOR).length;
}

function checkCellLength(len) {
  is(getCellLength(), len, `Table should contain ${len} items`);
}

async function scroll() {
  const $ = id => gPanelWindow.document.querySelector(id);
  const table = $("#storage-table .table-widget-body");
  const cell = $(CELL_SELECTOR);
  const cellHeight = cell.getBoundingClientRect().height;

  const onStoresUpdate = gUI.once("store-objects-updated");
  table.scrollTop += cellHeight * 50;
  await onStoresUpdate;
}

/**
 * Asserts that the given tree path exists
 *
 * @param {Document} doc
 * @param {Array} path
 * @param {boolean} isExpected
 */
function checkTree(doc, path, isExpected = true) {
  const doesExist = isInTree(doc, path);
  ok(
    isExpected ? doesExist : !doesExist,
    `${path.join(" > ")} is ${isExpected ? "" : "not "}in the tree`
  );
}

/**
 * Returns whether a tree path exists
 *
 * @param {Document} doc
 * @param {Array} path
 */
function isInTree(doc, path) {
  const treeId = JSON.stringify(path);
  return !!doc.querySelector(`[data-id='${treeId}']`);
}

/**
 * Returns the label of the node for the provided tree path
 *
 * @param {Document} doc
 * @param {Array} path
 * @returns {string}
 */
function getTreeNodeLabel(doc, path) {
  const treeId = JSON.stringify(path);
  return doc.querySelector(`[data-id='${treeId}'] .tree-widget-item`)
    .textContent;
}

/**
 * Checks that the pair <name, value> is displayed at the data table
 *
 * @param {string} name
 * @param {any} value
 */
function checkStorageData(name, value) {
  ok(
    hasStorageData(name, value),
    `Table row has an entry for: ${name} with value: ${value}`
  );
}

async function waitForStorageData(name, value) {
  info("Waiting for data to appear in the table");
  await waitFor(() => hasStorageData(name, value));
  ok(true, `Table row has an entry for: ${name} with value: ${value}`);
}

/**
 * Returns whether the pair <name, value> is displayed at the data table
 *
 * @param {string} name
 * @param {any} value
 */
function hasStorageData(name, value) {
  return gUI.table.items.get(name)?.value === value;
}

/**
 * Returns an URL of a page that uses the document-builder to generate its content
 *
 * @param {string} domain
 * @param {string} html
 * @param {string} protocol
 */
function buildURLWithContent(domain, html, protocol = "https") {
  return `${protocol}://${domain}/document-builder.sjs?html=${encodeURI(html)}`;
}

/**
 * Asserts that the given cookie holds the provided value in the data table
 *
 * @param {string} name
 * @param {string} value
 */
function checkCookieData(name, value) {
  ok(
    hasCookieData(name, value),
    `Table row has an entry for: ${name} with value: ${value}`
  );
}

/**
 * Returns whether the given cookie holds the provided value in the data table
 *
 * @param {string} name
 * @param {string} value
 */
function hasCookieData(name, value) {
  const rows = Array.from(gUI.table.items);
  const cookie = rows.map(([, data]) => data).find(x => x.name === name);

  info(`found ${cookie?.value}`);
  return cookie?.value === value;
}
