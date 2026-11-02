/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* globals Task, openToolboxForTab, gBrowser */

// shared-head.js handles imports, constants, and utility functions
// Load the shared-head file first.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/shared/test/shared-head.js",
  this
);

// Import helpers for the new debugger
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/debugger/test/mochitest/shared-head.js",
  this
);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/webconsole/test/browser/shared-head.js",
  this
);

var {
  BrowserConsoleManager,
} = require("resource://devtools/client/webconsole/browser-console-manager.js");

var WCUL10n = require("resource://devtools/shared/webconsole/l10n.js");
const DOCS_GA_PARAMS = `?${new URLSearchParams({
  utm_source: "devtools",
  utm_medium: "firefox-console-errors",
  utm_campaign: "default",
})}`;
const GA_PARAMS = `?${new URLSearchParams({
  utm_source: "devtools",
  utm_medium: "devtools-webconsole",
  utm_campaign: "default",
})}`;

const wcActions = require("resource://devtools/client/webconsole/actions/index.js");

registerCleanupFunction(async function () {
  // Reset all cookies, tests loading sjs_slow-response-test-server.sjs will
  // set a foo cookie which might have side effects on other tests.
  Services.cookies.removeAll();
});

const isCmNextEnabled = Services.prefs.getBoolPref(
  "devtools.webconsole.codemirrorNext"
);
const codemirrorSelectors = {
  cmScroller: isCmNextEnabled ? ".cm-scroller" : ".CodeMirror-scroll",
  cmContent: isCmNextEnabled ? ".cm-content" : ".CodeMirror-wrap",
  cmLine: isCmNextEnabled
    ? ".cm-content .cm-line"
    : ".CodeMirror-code pre.CodeMirror-line",
  cmLineNumbers: isCmNextEnabled
    ? ".cm-lineNumbers"
    : ".CodeMirror-linenumbers",
  cmEditor: `#response-panel .editor-row-container ${isCmNextEnabled ? ".cm-editor" : ".CodeMirror"}`,
};

/**
 * Add a new tab and open the toolbox in it, and select the webconsole.
 *
 * @param string url
 *        The URL for the tab to be opened.
 * @param Boolean clearJstermHistory
 *        true (default) if the jsterm history should be cleared.
 * @param String hostId (optional)
 *        The type of toolbox host to be used.
 * @return Promise
 *         Resolves when the tab has been added, loaded and the toolbox has been opened.
 *         Resolves to the hud.
 */
async function openNewTabAndConsole(url, clearJstermHistory = true, hostId) {
  const toolbox = await openNewTabAndToolbox(url, "webconsole", hostId);
  const hud = toolbox.getCurrentPanel().hud;

  if (clearJstermHistory) {
    // Clearing history that might have been set in previous tests.
    await hud.ui.wrapper.dispatchClearHistory();
  }

  return hud;
}

/**
 * Add a new tab with iframes, open the toolbox in it, and select the webconsole.
 *
 * @param string url
 *        The URL for the tab to be opened.
 * @param Arra<string> iframes
 *        An array of URLs that will be added to the top document.
 * @return Promise
 *         Resolves when the tab has been added, loaded, iframes loaded, and the toolbox
 *         has been opened. Resolves to the hud.
 */
async function openNewTabWithIframesAndConsole(tabUrl, iframes) {
  // We need to add the tab and the iframes before opening the console in case we want
  // to handle remote frames (we don't support creating frames target when the toolbox
  // is already open).
  await addTab(tabUrl);
  await SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [iframes],
    async function (urls) {
      const iframesLoadPromises = urls.map((url, i) => {
        const iframe = content.document.createElement("iframe");
        iframe.classList.add(`iframe-${i + 1}`);
        const onLoad = ContentTaskUtils.waitForEvent(iframe, "load");
        iframe.src = url;
        content.document.body.append(iframe);
        return onLoad;
      });

      await Promise.all(iframesLoadPromises);
    }
  );

  return openConsole();
}

/**
 * Open a new window with a tab,open the toolbox, and select the webconsole.
 *
 * @param string url
 *        The URL for the tab to be opened.
 * @return Promise<{win, hud, tab}>
 *         Resolves when the tab has been added, loaded and the toolbox has been opened.
 *         Resolves to the toolbox.
 */
async function openNewWindowAndConsole(url) {
  const win = await BrowserTestUtils.openNewBrowserWindow();
  const tab = await addTab(url, { window: win });
  win.gBrowser.selectedTab = tab;
  const hud = await openConsole(tab);
  return { win, hud, tab };
}

/**
 * Subscribe to the store and log out stringinfied versions of messages.
 * This is a helper function for debugging, to make is easier to see what
 * happened during the test in the log.
 *
 * @param object hud
 */
function logAllStoreChanges(hud) {
  const store = hud.ui.wrapper.getStore();
  // Adding logging each time the store is modified in order to check
  // the store state in case of failure.
  store.subscribe(() => {
    const messages = [
      ...store.getState().messages.mutableMessagesById.values(),
    ];
    const debugMessages = messages.map(
      ({ id, type, parameters, messageText }) => {
        return { id, type, parameters, messageText };
      }
    );
    info(
      "messages : " +
        JSON.stringify(debugMessages, function (key, value) {
          if (value && value.getGrip) {
            return value.getGrip();
          }
          return value;
        })
    );
  });
}

/**
 * Wait for messages with given message type in the web console output,
 * resolving once they are received.
 *
 * @param object options
 *        - hud: the webconsole
 *        - messages: Array[Object]. An array of messages to match.
 *          Current supported options:
 *            - text: {String} Partial text match in .message-body
 *            - typeSelector: {String} A part of selector for the message, to
 *                                     specify the message type.
 * @return promise
 *         A promise that is resolved to an array of the message nodes
 */
function waitForMessagesByType({ hud, messages }) {
  return new Promise(resolve => {
    const matchedMessages = [];
    hud.ui.on("new-messages", function messagesReceived(newMessages) {
      for (const message of messages) {
        if (message.matched) {
          continue;
        }

        const typeSelector = message.typeSelector;
        if (!typeSelector) {
          throw new Error("typeSelector property is required");
        }
        if (!typeSelector.startsWith(".")) {
          throw new Error(
            "typeSelector property start with a dot e.g. `.result`"
          );
        }
        const selector = ".message" + typeSelector;

        for (const newMessage of newMessages) {
          const messageBody = newMessage.node.querySelector(`.message-body`);
          if (
            messageBody &&
            newMessage.node.matches(selector) &&
            messageBody.textContent.includes(message.text)
          ) {
            matchedMessages.push(newMessage);
            message.matched = true;
            const messagesLeft = messages.length - matchedMessages.length;
            info(
              `Matched a message with text: "${message.text}", ` +
                (messagesLeft > 0
                  ? `still waiting for ${messagesLeft} messages.`
                  : `all messages received.`)
            );
            break;
          }
        }

        if (matchedMessages.length === messages.length) {
          hud.ui.off("new-messages", messagesReceived);
          resolve(matchedMessages);
          return;
        }
      }
    });
  });
}

/**
 * Wait for a message with the provided text and showing the provided repeat count.
 *
 * @param {object} hud : the webconsole
 * @param {string} text : text included in .message-body
 * @param {string} typeSelector : A part of selector for the message, to
 *                                specify the message type.
 * @param {number} repeat : expected repeat count in .message-repeats
 */
function waitForRepeatedMessageByType(hud, text, typeSelector, repeat) {
  return waitFor(() => {
    // Wait for a message matching the provided text.
    const node = findMessageByType(hud, text, typeSelector);
    if (!node) {
      return false;
    }

    // Check if there is a repeat node with the expected count.
    const repeatNode = node.querySelector(".message-repeats");
    if (repeatNode && parseInt(repeatNode.textContent, 10) === repeat) {
      return node;
    }

    return false;
  });
}

/**
 * Wait for a single message with given message type in the web console output,
 * resolving with the first message that matches the query once it is received.
 *
 * @param {object} hud : the webconsole
 * @param {string} text : text included in .message-body
 * @param {string} typeSelector : A part of selector for the message, to
 *                                specify the message type.
 * @return promise
 *         A promise that is resolved to the message node
 */
async function waitForMessageByType(hud, text, typeSelector) {
  const messages = await waitForMessagesByType({
    hud,
    messages: [{ text, typeSelector }],
  });
  return messages[0];
}

/**
 * Wait for the Source editor to be available.
 *
 * @param {object} panel
 * @returns
 */
async function waitForSourceEditor(panel) {
  return waitUntil(() => {
    return !!panel.querySelector(".cm-editor");
  });
}

/**
 * Execute an input expression.
 *
 * @param {object} hud : The webconsole.
 * @param {string} input : The input expression to execute.
 */
function execute(hud, input) {
  return hud.ui.wrapper.dispatchEvaluateExpression(input);
}

/**
 * Execute an input expression and wait for a message with the expected text
 * with given message type to be displayed in the output.
 *
 * @param {object} hud : The webconsole.
 * @param {string} input : The input expression to execute.
 * @param {string} matchingText : A string that should match the message body content.
 * @param {string} typeSelector : A part of selector for the message, to
 *                                specify the message type.
 */
function executeAndWaitForMessageByType(
  hud,
  input,
  matchingText,
  typeSelector
) {
  const onMessage = waitForMessageByType(hud, matchingText, typeSelector);
  execute(hud, input);
  return onMessage;
}

/**
 * Type-specific wrappers for executeAndWaitForMessageByType
 *
 * @param {object} hud : The webconsole.
 * @param {string} input : The input expression to execute.
 * @param {string} matchingText : A string that should match the message body
 *                                content.
 */
function executeAndWaitForResultMessage(hud, input, matchingText) {
  return executeAndWaitForMessageByType(hud, input, matchingText, ".result");
}

function executeAndWaitForErrorMessage(hud, input, matchingText) {
  return executeAndWaitForMessageByType(hud, input, matchingText, ".error");
}

/**
 * Set the input value, simulates the right keyboard event to evaluate it,
 * depending on if the console is in editor mode or not, and wait for a message
 * with the expected text with given message type to be displayed in the output.
 *
 * @param {object} hud : The webconsole.
 * @param {string} input : The input expression to execute.
 * @param {string} matchingText : A string that should match the message body
 *                                content.
 * @param {string} typeSelector : A part of selector for the message, to
 *                                specify the message type.
 */
function keyboardExecuteAndWaitForMessageByType(
  hud,
  input,
  matchingText,
  typeSelector
) {
  hud.jsterm.focus();
  setInputValue(hud, input);
  const onMessage = waitForMessageByType(hud, matchingText, typeSelector);
  if (isEditorModeEnabled(hud)) {
    EventUtils.synthesizeKey("KEY_Enter", {
      [Services.appinfo.OS === "Darwin" ? "metaKey" : "ctrlKey"]: true,
    });
  } else {
    EventUtils.synthesizeKey("VK_RETURN");
  }
  return onMessage;
}

/**
 * Type-specific wrappers for keyboardExecuteAndWaitForMessageByType
 *
 * @param {object} hud : The webconsole.
 * @param {string} input : The input expression to execute.
 * @param {string} matchingText : A string that should match the message body
 *                                content.
 */
function keyboardExecuteAndWaitForResultMessage(hud, input, matchingText) {
  return keyboardExecuteAndWaitForMessageByType(
    hud,
    input,
    matchingText,
    ".result"
  );
}

/**
 * Wait for a message to be logged and ensure it is logged only once.
 *
 * @param object hud
 *        The web console.
 * @param string text
 *        A substring that can be found in the message.
 * @param string typeSelector
 *        A part of selector for the message, to specify the message type.
 * @return {Node} the node corresponding the found message
 */
async function checkUniqueMessageExists(hud, msg, typeSelector) {
  info(`Checking "${msg}" was logged`);
  let messages;
  try {
    messages = await waitFor(async () => {
      const msgs = await findMessagesVirtualizedByType({
        hud,
        text: msg,
        typeSelector,
      });
      return msgs.length ? msgs : null;
    });
  } catch (e) {
    ok(false, `Message "${msg}" wasn't logged\n`);
    return null;
  }

  is(messages.length, 1, `"${msg}" was logged once`);
  const [messageEl] = messages;
  const repeatNode = messageEl.querySelector(".message-repeats");
  is(repeatNode, null, `"${msg}" wasn't repeated`);
  return messageEl;
}

/**
 * Simulate a context menu event on the provided element, and wait for the console context
 * menu to open. Returns a promise that resolves the menu popup element.
 *
 * @param object hud
 *        The web console.
 * @param element element
 *        The dom element on which the context menu event should be synthesized.
 * @return promise
 */
async function openContextMenu(hud, element) {
  const onConsoleMenuOpened = hud.ui.wrapper.once("menu-open");
  synthesizeContextMenuEvent(element);
  await onConsoleMenuOpened;
  return _getContextMenu(hud);
}

/**
 * Hide the webconsole context menu popup. Returns a promise that will resolve when the
 * context menu popup is hidden or immediately if the popup can't be found.
 *
 * @param object hud
 *        The web console.
 * @return promise
 */
function hideContextMenu(hud) {
  const popup = _getContextMenu(hud);
  if (!popup || popup.state == "hidden") {
    return Promise.resolve();
  }

  const onPopupHidden = once(popup, "popuphidden");
  popup.hidePopup();
  return onPopupHidden;
}

function _getContextMenu(hud) {
  const toolbox = hud.toolbox;
  const doc = toolbox ? toolbox.topWindow.document : hud.chromeWindow.document;
  return doc.getElementById("webconsole-menu");
}

/**
 * Toggle Enable network monitoring setting
 *
 *  @param object hud
 *         The web console.
 *  @param boolean shouldBeSwitchedOn
 *         The expected state the setting should be in after the toggle.
 */
async function toggleNetworkMonitoringConsoleSetting(hud, shouldBeSwitchedOn) {
  const selector =
    ".webconsole-console-settings-menu-item-enableNetworkMonitoring";
  const settingChanged = waitFor(() => {
    const el = getConsoleSettingElement(hud, selector);
    return shouldBeSwitchedOn
      ? el.getAttribute("aria-checked") === "true"
      : el.getAttribute("aria-checked") !== "true";
  });
  await toggleConsoleSetting(hud, selector);
  await settingChanged;
}

async function toggleConsoleSetting(hud, selector) {
  const toolbox = hud.toolbox;
  const doc = toolbox ? toolbox.doc : hud.chromeWindow.document;

  const menuItem = doc.querySelector(selector);
  menuItem.click();
}

function getConsoleSettingElement(hud, selector) {
  const toolbox = hud.toolbox;
  const doc = toolbox ? toolbox.doc : hud.chromeWindow.document;

  return doc.querySelector(selector);
}

function checkConsoleSettingState(hud, selector, enabled) {
  const el = getConsoleSettingElement(hud, selector);
  const checked = el.getAttribute("aria-checked") === "true";

  if (enabled) {
    ok(checked, "setting is enabled");
  } else {
    ok(!checked, "setting is disabled");
  }
}

/**
 * Returns a promise that resolves when the node passed as an argument mutate
 * according to the passed configuration.
 *
 * @param {Node} node - The node to observe mutations on.
 * @param {object} observeConfig - A configuration object for MutationObserver.observe.
 * @returns {Promise}
 */
function waitForNodeMutation(node, observeConfig = {}) {
  return new Promise(resolve => {
    const observer = new MutationObserver(mutations => {
      resolve(mutations);
      observer.disconnect();
    });
    observer.observe(node, observeConfig);
  });
}

/**
 * Search for a given message. When found, simulate a click on the
 * message's location, checking to make sure that the debugger opens
 * the corresponding URL. If the message was generated by a logpoint,
 * check if the corresponding logpoint editing panel is opened.
 *
 * @param {object} hud
 *        The webconsole
 * @param {object} options
 *        - text: {String} The text to search for. This should be contained in
 *                         the message. The searching is done with
 *                         @see findMessageByType.
 *        - typeSelector: {string} A part of selector for the message, to
 *                                 specify the message type.
 *        - url : {String|null} URL expected to be opened.
 *        - line : {Number|null} Line expected to be opened.
 *        - column : {Number|null} Column expected to be opened.
 *        - logPointExpr: {String} The logpoint expression
 */
async function testOpenInDebugger(
  hud,
  { text, typeSelector, url, column, line, logPointExpr = undefined }
) {
  info(`Finding message for open-in-debugger test; text is "${text}"`);
  const messageNode = await waitFor(() =>
    findMessageByType(hud, text, typeSelector)
  );
  const locationNode = messageNode.querySelector(".message-location");
  ok(locationNode, "The message does have a location link");

  await clickAndAssertFrameLinkNode(
    hud.toolbox,
    locationNode,
    { url, column, line },
    logPointExpr
  );
}

/**
 * Returns true if the give node is currently focused.
 */
function hasFocus(node) {
  return (
    node.ownerDocument.activeElement == node && node.ownerDocument.hasFocus()
  );
}

/**
 * Get the value of the console input .
 *
 * @param {WebConsole} hud: The webconsole
 * @returns {string}: The value of the console input.
 */
function getInputValue(hud) {
  return hud.jsterm._getValue();
}

/**
 * Set the value of the console input .
 *
 * @param {WebConsole} hud: The webconsole
 * @param {string} value : The value to set the console input to.
 */
function setInputValue(hud, value) {
  const onValueSet = hud.jsterm.once("set-input-value");
  hud.jsterm._setValue(value);
  return onValueSet;
}

/**
 * Set the value of the console input and its caret position, and wait for the
 * autocompletion to be updated.
 *
 * @param {WebConsole} hud: The webconsole
 * @param {string} value : The value to set the jsterm to.
 * @param {Integer} caretPosition : The index where to place the cursor. A negative
 *                  number will place the caret at (value.length - offset) position.
 *                  Default to value.length (caret set at the end).
 * @returns {Promise} resolves when the jsterm is completed.
 */
async function setInputValueForAutocompletion(
  hud,
  value,
  caretPosition = value.length
) {
  const { jsterm } = hud;

  const initialPromises = [];
  if (jsterm.autocompletePopup.isOpen) {
    initialPromises.push(jsterm.autocompletePopup.once("popup-closed"));
  }
  setInputValue(hud, "");
  await Promise.all(initialPromises);

  // Wait for next tick. Tooltip tests sometimes fail to successively hide and
  // show tooltips on Win32 debug.
  await waitForTick();

  jsterm.focus();

  const updated = jsterm.once("autocomplete-updated");
  EventUtils.sendString(value, hud.iframeWindow);
  await updated;

  // Wait for next tick. Tooltip tests sometimes fail to successively hide and
  // show tooltips on Win32 debug.
  await waitForTick();

  if (caretPosition < 0) {
    caretPosition = value.length + caretPosition;
  }

  if (Number.isInteger(caretPosition)) {
    jsterm.editor.setCursor(jsterm.editor.getPosition(caretPosition));
  }
}

/**
 * Set the value of the console input and wait for the confirm dialog to be displayed.
 *
 * @param {Toolbox} toolbox
 * @param {WebConsole} hud
 * @param {string} value : The value to set the jsterm to.
 *                  Default to value.length (caret set at the end).
 * @returns {Promise<HTMLElement>} resolves with dialog element when it is opened.
 */
async function setInputValueForGetterConfirmDialog(toolbox, hud, value) {
  await setInputValueForAutocompletion(hud, value);
  await waitFor(() => isConfirmDialogOpened(toolbox));
  ok(true, "The confirm dialog is displayed");
  return getConfirmDialog(toolbox);
}

/**
 * Checks if the console input has the expected completion value.
 *
 * @param {WebConsole} hud
 * @param {string} expectedValue
 * @param {string} assertionInfo: Description of the assertion passed to `is`.
 */
function checkInputCompletionValue(hud, expectedValue, assertionInfo) {
  const completionValue = getInputCompletionValue(hud);
  if (completionValue === null) {
    ok(false, "Couldn't retrieve the completion value");
  }

  info(`Expects "${expectedValue}", is "${completionValue}"`);
  is(completionValue, expectedValue, assertionInfo);
}

/**
 * Checks if the cursor on console input is at expected position.
 *
 * @param {WebConsole} hud
 * @param {Integer} expectedCursorIndex
 * @param {string} assertionInfo: Description of the assertion passed to `is`.
 */
function checkInputCursorPosition(hud, expectedCursorIndex, assertionInfo) {
  const { jsterm } = hud;
  is(jsterm.editor.getCursor().ch, expectedCursorIndex, assertionInfo);
}

/**
 * Checks the console input value and the cursor position given an expected string
 * containing a "|" to indicate the expected cursor position.
 *
 * @param {WebConsole} hud
 * @param {string} expectedStringWithCursor:
 *                  String with a "|" to indicate the expected cursor position.
 *                  For example, this is how you assert an empty value with the focus "|",
 *                  and this indicates the value should be "test" and the cursor at the
 *                  end of the input: "test|".
 * @param {string} assertionInfo: Description of the assertion passed to `is`.
 */
function checkInputValueAndCursorPosition(
  hud,
  expectedStringWithCursor,
  assertionInfo
) {
  info(`Checking jsterm state: \n${expectedStringWithCursor}`);
  if (!expectedStringWithCursor.includes("|")) {
    ok(
      false,
      `expectedStringWithCursor must contain a "|" char to indicate cursor position`
    );
  }

  const inputValue = expectedStringWithCursor.replace("|", "");
  const { jsterm } = hud;

  is(getInputValue(hud), inputValue, "console input has expected value");
  const lines = expectedStringWithCursor.split("\n");
  const lineWithCursor = lines.findIndex(line => line.includes("|"));
  const { ch, line } = jsterm.editor.getCursor();
  is(line, lineWithCursor, assertionInfo + " - correct line");
  is(ch, lines[lineWithCursor].indexOf("|"), assertionInfo + " - correct ch");
}

/**
 * Returns the console input completion value.
 *
 * @param {WebConsole} hud
 * @returns {string}
 */
function getInputCompletionValue(hud) {
  const { jsterm } = hud;
  return jsterm.editor.getAutoCompletionText();
}

function closeAutocompletePopup(hud) {
  const { jsterm } = hud;

  if (!jsterm.autocompletePopup.isOpen) {
    return Promise.resolve();
  }

  const onPopupClosed = jsterm.autocompletePopup.once("popup-closed");
  const onAutocompleteUpdated = jsterm.once("autocomplete-updated");
  EventUtils.synthesizeKey("KEY_Escape");
  return Promise.all([onPopupClosed, onAutocompleteUpdated]);
}

/**
 * Returns a boolean indicating if the console input is focused.
 *
 * @param {WebConsole} hud
 * @returns {boolean}
 */
function isInputFocused(hud) {
  const { jsterm } = hud;
  const document = hud.ui.outputNode.ownerDocument;
  const documentIsFocused = document.hasFocus();
  return documentIsFocused && jsterm.editor.hasFocus();
}

/**
 * Open the JavaScript debugger.
 *
 * @param object options
 *        Options for opening the debugger:
 *        - tab: the tab you want to open the debugger for.
 * @return object
 *         A promise that is resolved once the debugger opens, or rejected if
 *         the open fails. The resolution callback is given one argument, an
 *         object that holds the following properties:
 *         - target: the Target object for the Tab.
 *         - toolbox: the Toolbox instance.
 *         - panel: the jsdebugger panel instance.
 */
async function openDebugger(options = {}) {
  if (!options.tab) {
    options.tab = gBrowser.selectedTab;
  }

  let toolbox = gDevTools.getToolboxForTab(options.tab);
  const dbgPanelAlreadyOpen = toolbox && toolbox.getPanel("jsdebugger");
  if (dbgPanelAlreadyOpen) {
    await toolbox.selectTool("jsdebugger");

    return {
      target: toolbox.target,
      toolbox,
      panel: toolbox.getCurrentPanel(),
    };
  }

  toolbox = await gDevTools.showToolboxForTab(options.tab, {
    toolId: "jsdebugger",
  });
  const panel = toolbox.getCurrentPanel();

  await toolbox.threadFront.getSources();

  return { target: toolbox.target, toolbox, panel };
}

async function openInspector(options = {}) {
  if (!options.tab) {
    options.tab = gBrowser.selectedTab;
  }

  const toolbox = await gDevTools.showToolboxForTab(options.tab, {
    toolId: "inspector",
  });

  return toolbox.getCurrentPanel();
}

/**
 * Open the netmonitor for the given tab, or the current one if none given.
 *
 * @param Element tab
 *        Optional tab element for which you want open the netmonitor.
 *        Defaults to current selected tab.
 * @return Promise
 *         A promise that is resolved with the netmonitor panel once the netmonitor is open.
 */
async function openNetMonitor(tab) {
  tab = tab || gBrowser.selectedTab;
  let toolbox = gDevTools.getToolboxForTab(tab);
  if (!toolbox) {
    toolbox = await gDevTools.showToolboxForTab(tab);
  }
  await toolbox.selectTool("netmonitor");
  return toolbox.getCurrentPanel();
}

/**
 * Open the Web Console for the given tab, or the current one if none given.
 *
 * @param Element tab
 *        Optional tab element for which you want open the Web Console.
 *        Defaults to current selected tab.
 * @return Promise
 *         A promise that is resolved with the console hud once the web console is open.
 */
async function openConsole(tab) {
  tab = tab || gBrowser.selectedTab;
  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "webconsole",
  });
  return toolbox.getCurrentPanel().hud;
}

/**
 * Close the Web Console for the given tab.
 *
 * @param Element [tab]
 *        Optional tab element for which you want close the Web Console.
 *        Defaults to current selected tab.
 * @return object
 *         A promise that is resolved once the web console is closed.
 */
async function closeConsole(tab = gBrowser.selectedTab) {
  const toolbox = gDevTools.getToolboxForTab(tab);
  if (toolbox) {
    await toolbox.destroy();
  }
}

/**
 * Open a network request logged in the webconsole in the netmonitor panel.
 *
 * @param {object} toolbox
 * @param {object} hud
 * @param {string} url
 *        URL of the request as logged in the netmonitor.
 * @param {string} urlInConsole
 *        (optional) Use if the logged URL in webconsole is different from the real URL.
 */
async function openMessageInNetmonitor(toolbox, hud, url, urlInConsole) {
  // By default urlInConsole should be the same as the complete url.
  urlInConsole = urlInConsole || url;

  const message = await waitFor(() =>
    findMessageByType(hud, urlInConsole, ".network")
  );

  const onNetmonitorSelected = toolbox.once(
    "netmonitor-selected",
    (event, panel) => {
      return panel;
    }
  );

  const menuPopup = await openContextMenu(hud, message);
  const openInNetMenuItem = menuPopup.querySelector(
    "#console-menu-open-in-network-panel"
  );
  ok(openInNetMenuItem, "open in network panel item is enabled");
  menuPopup.activateItem(openInNetMenuItem);

  const { panelWin } = await onNetmonitorSelected;
  ok(
    true,
    "The netmonitor panel is selected when clicking on the network message"
  );

  const { store, windowRequire } = panelWin;
  const nmActions = windowRequire(
    "devtools/client/netmonitor/src/actions/index"
  );
  const { getSelectedRequest } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );

  store.dispatch(nmActions.batchEnable(false));

  await waitFor(() => {
    const selected = getSelectedRequest(store.getState());
    return selected && selected.url === url;
  }, `network entry for the URL "${url}" wasn't found`);

  ok(true, "The attached url is correct.");

  info(
    "Wait for the netmonitor headers panel to appear as it spawns RDP requests"
  );
  await waitFor(() =>
    panelWin.document.querySelector("#headers-panel .headers-overview")
  );
}

function selectNode(hud, node) {
  const outputContainer = hud.ui.outputNode.querySelector(".webconsole-output");

  // We must first blur the input or else we can't select anything.
  outputContainer.ownerDocument.activeElement.blur();

  const selection = outputContainer.ownerDocument.getSelection();
  const range = document.createRange();
  range.selectNodeContents(node);
  selection.removeAllRanges();
  selection.addRange(range);

  return selection;
}

async function waitForBrowserConsole() {
  return new Promise(resolve => {
    Services.obs.addObserver(function observer(subject) {
      Services.obs.removeObserver(observer, "web-console-created");
      subject.QueryInterface(Ci.nsISupportsString);

      const hud = BrowserConsoleManager.getBrowserConsole();
      ok(hud, "browser console is open");
      is(subject.data, hud.hudId, "notification hudId is correct");

      executeSoon(() => resolve(hud));
    }, "web-console-created");
  });
}

/**
 * Get the state of a console filter.
 *
 * @param {object} hud
 */
async function getFilterState(hud) {
  const { outputNode } = hud.ui;
  const filterBar = outputNode.querySelector(".webconsole-filterbar-secondary");
  const buttons = filterBar.querySelectorAll("button");
  const result = {};

  for (const button of buttons) {
    result[button.dataset.category] =
      button.getAttribute("aria-pressed") === "true";
  }

  return result;
}

/**
 * Return the filter input element.
 *
 * @param {object} hud
 * @return {HTMLInputElement}
 */
function getFilterInput(hud) {
  return hud.ui.outputNode.querySelector(".devtools-searchbox input");
}

/**
 * Set the state of a console filter.
 *
 * @param {object} hud
 * @param {object} settings
 *        Category settings in the following format:
 *          {
 *            error: true,
 *            warn: true,
 *            log: true,
 *            info: true,
 *            debug: true,
 *            css: false,
 *            netxhr: false,
 *            net: false,
 *            text: ""
 *          }
 */
async function setFilterState(hud, settings) {
  const { outputNode } = hud.ui;
  const filterBar = outputNode.querySelector(".webconsole-filterbar-secondary");

  for (const category in settings) {
    const value = settings[category];
    const button = filterBar.querySelector(`[data-category="${category}"]`);

    if (category === "text") {
      const filterInput = getFilterInput(hud);
      filterInput.focus();
      filterInput.select();
      const win = outputNode.ownerDocument.defaultView;
      if (!value) {
        EventUtils.synthesizeKey("KEY_Delete", {}, win);
      } else {
        EventUtils.sendString(value, win);
      }
      await waitFor(() => filterInput.value === value);
      continue;
    }

    if (!button) {
      ok(
        false,
        `setFilterState() called with a category of ${category}, ` +
          `which doesn't exist.`
      );
    }

    info(
      `Setting the ${category} category to ${value ? "checked" : "disabled"}`
    );

    const isPressed = button.getAttribute("aria-pressed");

    if ((!value && isPressed === "true") || (value && isPressed !== "true")) {
      button.click();

      await waitFor(() => {
        const pressed = button.getAttribute("aria-pressed");
        if (!value) {
          return pressed === "false" || pressed === null;
        }
        return pressed === "true";
      });
    }
  }
}

/**
 * Reset the filters at the end of a test that has changed them. This is
 * important when using the `--verify` test option as when it is used you need
 * to manually reset the filters.
 *
 * The css, netxhr and net filters are disabled by default.
 *
 * @param {object} hud
 */
async function resetFilters(hud) {
  info("Resetting filters to their default state");

  const store = hud.ui.wrapper.getStore();
  store.dispatch(wcActions.filtersClear());
}

/**
 * Open the reverse search input by simulating the appropriate keyboard shortcut.
 *
 * @param {object} hud
 * @returns {DOMNode} The reverse search dom node.
 */
async function openReverseSearch(hud) {
  info("Open the reverse search UI with a keyboard shortcut");
  const onReverseSearchUiOpen = waitFor(() => getReverseSearchElement(hud));
  const isMacOS = AppConstants.platform === "macosx";
  if (isMacOS) {
    EventUtils.synthesizeKey("r", { ctrlKey: true });
  } else {
    EventUtils.synthesizeKey("VK_F9");
  }

  const element = await onReverseSearchUiOpen;
  return element;
}

function getReverseSearchElement(hud) {
  const { outputNode } = hud.ui;
  return outputNode.querySelector(".reverse-search");
}

function getReverseSearchInfoElement(hud) {
  const reverseSearchElement = getReverseSearchElement(hud);
  if (!reverseSearchElement) {
    return null;
  }

  return reverseSearchElement.querySelector(".reverse-search-info");
}

/**
 * Returns a boolean indicating if the reverse search input is focused.
 *
 * @param {WebConsole} hud
 * @returns {boolean}
 */
function isReverseSearchInputFocused(hud) {
  const { outputNode } = hud.ui;
  const document = outputNode.ownerDocument;
  const documentIsFocused = document.hasFocus();
  const reverseSearchInput = outputNode.querySelector(".reverse-search-input");

  return document.activeElement == reverseSearchInput && documentIsFocused;
}

function getEagerEvaluationElement(hud) {
  return hud.ui.outputNode.querySelector(".eager-evaluation-result");
}

async function waitForEagerEvaluationResult(hud, text) {
  await waitUntil(() => {
    const elem = getEagerEvaluationElement(hud);
    if (elem) {
      if (text instanceof RegExp) {
        return text.test(elem.innerText);
      }
      return elem.innerText == text;
    }
    return false;
  });
  ok(true, `Got eager evaluation result ${text}`);
}

// This just makes sure the eager evaluation result disappears. This will pass
// even for inputs which eventually have a result because nothing will be shown
// while the evaluation happens. Waiting here does make sure that a previous
// input was processed and sent down to the server for evaluating.
async function waitForNoEagerEvaluationResult(hud) {
  await waitUntil(() => {
    const elem = getEagerEvaluationElement(hud);
    return elem && elem.innerText == "";
  });
  ok(true, `Eager evaluation result disappeared`);
}

/**
 * Selects a node in the inspector.
 *
 * @param {object} toolbox
 * @param {string} selector: The selector for the node we want to select.
 */
async function selectNodeWithPicker(toolbox, selector) {
  const inspector = toolbox.getPanel("inspector");

  const onPickerStarted = toolbox.nodePicker.once("picker-started");
  toolbox.nodePicker.start();
  await onPickerStarted;

  info(
    `Picker mode started, now clicking on "${selector}" to select that node`
  );
  const onPickerStopped = toolbox.nodePicker.once("picker-stopped");
  const onInspectorUpdated = inspector.once("inspector-updated");

  await safeSynthesizeMouseEventAtCenterInContentPage(selector);

  await onPickerStopped;
  await onInspectorUpdated;
}

/**
 * Clicks on the arrow of a single object inspector node if it exists.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 */
async function expandObjectInspectorNode(node) {
  if (!node.classList.contains("tree-node")) {
    ok(false, "Node should be a .tree-node");
    return;
  }
  const arrow = getObjectInspectorNodeArrow(node);
  if (!arrow) {
    ok(false, "Node can't be expanded");
    return;
  }
  if (arrow.classList.contains("open")) {
    ok(false, "Node already expanded");
    return;
  }
  const isLongString = node.querySelector(".node > .objectBox-string");
  let onMutation;
  let textContentBeforeExpand;
  if (!isLongString) {
    const objectInspector = node.closest(".object-inspector");
    onMutation = waitForNodeMutation(objectInspector, {
      childList: true,
    });
  } else {
    textContentBeforeExpand = node.textContent;
  }
  arrow.click();

  // Long strings are not going to be expanded into children element.
  // Instead the tree node will update itself to show the long string.
  // So that we can't wait for the childList mutation.
  if (isLongString) {
    // Reps will expand on click...
    await waitFor(() => arrow.classList.contains("open"));
    // ...but it will fetch the long string content asynchronously after having expanded the TreeNode.
    // So also wait for the string to be updated and be longer.
    await waitFor(
      () => node.textContent.length > textContentBeforeExpand.length
    );
  } else {
    await onMutation;
    // Waiting for the object inspector mutation isn't enough,
    // also wait for the children element, with higher aria-level to be added to the DOM.
    await waitFor(() => !!getObjectInspectorChildrenNodes(node).length);
  }

  ok(
    arrow.classList.contains("open"),
    "The arrow of the root node of the tree is expanded after clicking on it"
  );
}

/**
 * Retrieve the arrow of a single object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {HTMLElement|null} the arrow element
 */
function getObjectInspectorNodeArrow(node) {
  return node.querySelector(".theme-twisty");
}

/**
 * Check if a single object inspector node is expandable.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {boolean} true if the node can be expanded
 */
function isObjectInspectorNodeExpandable(node) {
  return !!getObjectInspectorNodeArrow(node);
}

/**
 * Retrieve the nodes for a given object inspector element.
 *
 * @param {HTMLElement} oi: Object inspector element
 * @return {NodeList} the object inspector nodes
 */
function getObjectInspectorNodes(oi) {
  return oi.querySelectorAll(".tree-node");
}

/**
 * Retrieve the "children" nodes for a given object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {Array<HTMLElement>} the direct children (i.e. the ones that are one level
 *                              deeper than the passed node)
 */
function getObjectInspectorChildrenNodes(node) {
  const getLevel = n => parseInt(n.getAttribute("aria-level") || "0", 10);
  const level = getLevel(node);
  const childLevel = level + 1;
  const children = [];

  let currentNode = node;
  while (
    currentNode.nextSibling &&
    getLevel(currentNode.nextSibling) === childLevel
  ) {
    currentNode = currentNode.nextSibling;
    children.push(currentNode);
  }

  return children;
}

/**
 * Retrieve the invoke getter button for a given object inspector node.
 *
 * @param {HTMLElement} node: Object inspector node (.tree-node)
 * @return {HTMLElement|null} the invoke button element
 */
function getObjectInspectorInvokeGetterButton(node) {
  return node.querySelector(".invoke-getter");
}

/**
 * Retrieve the first node that match the passed node label, for a given object inspector
 * element.
 *
 * @param {HTMLElement} oi: Object inspector element
 * @param {string} nodeLabel: label of the searched node
 * @return {HTMLElement|null} the Object inspector node with the matching label
 */
function findObjectInspectorNode(oi, nodeLabel) {
  return [...oi.querySelectorAll(".tree-node")].find(node => {
    const label = node.querySelector(".object-label");
    if (!label) {
      return false;
    }
    return label.textContent === nodeLabel;
  });
}

/**
 * Return an array of the label of the autocomplete popup items.
 *
 * @param {AutocompletPopup} popup
 * @returns {Array<string>}
 */
function getAutocompletePopupLabels(popup) {
  return popup.getItems().map(item => item.label);
}

/**
 * Check if the retrieved list of autocomplete labels of the specific popup
 * includes all of the expected labels.
 *
 * @param {AutocompletPopup} popup
 * @param {Array<string>} expected the array of expected labels
 */
function hasExactPopupLabels(popup, expected) {
  return hasPopupLabels(popup, expected, true);
}

/**
 * Check if the expected label is included in the list of autocomplete labels
 * of the specific popup.
 *
 * @param {AutocompletPopup} popup
 * @param {string} label the label to check
 */
function hasPopupLabel(popup, label) {
  return hasPopupLabels(popup, [label]);
}

/**
 * Validate the expected labels against the autocomplete labels.
 *
 * @param {AutocompletPopup} popup
 * @param {Array<string>} expectedLabels
 * @param {boolean} checkAll
 */
function hasPopupLabels(popup, expectedLabels, checkAll = false) {
  const autocompleteLabels = getAutocompletePopupLabels(popup);
  if (checkAll) {
    return (
      autocompleteLabels.length === expectedLabels.length &&
      autocompleteLabels.every((autoLabel, idx) => {
        return expectedLabels.indexOf(autoLabel) === idx;
      })
    );
  }
  return expectedLabels.every(expectedLabel => {
    return autocompleteLabels.includes(expectedLabel);
  });
}

/**
 * Return the "Confirm Dialog" element.
 *
 * @param toolbox
 * @returns {HTMLElement|null}
 */
function getConfirmDialog(toolbox) {
  const { doc } = toolbox;
  return doc.querySelector(".invoke-confirm");
}

/**
 * Returns true if the Confirm Dialog is opened.
 *
 * @param toolbox
 * @returns {boolean}
 */
function isConfirmDialogOpened(toolbox) {
  const tooltip = getConfirmDialog(toolbox);
  if (!tooltip) {
    return false;
  }

  return tooltip.classList.contains("tooltip-visible");
}

async function selectFrame(dbg, frame) {
  const onScopes = waitForDispatch(dbg.store, "ADD_SCOPES");
  await dbg.actions.selectFrame(frame);
  await onScopes;
}

async function pauseDebugger(dbg, options) {
  info("Waiting for debugger to pause");
  const onPaused = waitForPaused(dbg, null, options);
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    content.wrappedJSObject.firstCall();
  }).catch(() => {});
  await onPaused;
}

/**
 * Check that the passed HTMLElement vertically overflows.
 *
 * @param {HTMLElement} container
 * @returns {boolean}
 */
function hasVerticalOverflow(container) {
  return container.scrollHeight > container.clientHeight;
}

/**
 * Check that the passed HTMLElement is scrolled to the bottom.
 *
 * @param {HTMLElement} container
 * @returns {boolean}
 */
function isScrolledToBottom(container) {
  if (!container.lastChild) {
    return true;
  }
  const lastNodeHeight = container.lastChild.clientHeight;
  return (
    container.scrollTop + container.clientHeight >=
    container.scrollHeight - lastNodeHeight / 2
  );
}

/**
 *
 * @param {WebConsole} hud
 * @param {Array<string>} expectedMessages: An array of string representing the messages
 *                        from the output. This can only be a part of the string of the
 *                        message.
 *                        Start the string with "▶︎⚠ " or "▼⚠ " to indicate that the
 *                        message is a warningGroup (with respectively an open or
 *                        collapsed arrow).
 *                        Start the string with "|︎ " to indicate that the message is
 *                        inside a group and should be indented.
 */
async function checkConsoleOutputForWarningGroup(hud, expectedMessages) {
  const messages = await findAllMessagesVirtualized(hud);
  is(
    messages.length,
    expectedMessages.length,
    "Got the expected number of messages"
  );

  const isInWarningGroup = index => {
    const message = expectedMessages[index];
    if (!message.startsWith("|")) {
      return false;
    }
    const groups = expectedMessages
      .slice(0, index)
      .reverse()
      .filter(m => !m.startsWith("|"));
    if (groups.length === 0) {
      ok(false, "Unexpected structure: an indented message isn't in a group");
    }

    return groups[0].startsWith("▼︎⚠");
  };

  for (let [i, expectedMessage] of expectedMessages.entries()) {
    // Refresh the reference to the message, as it may have been scrolled out of existence.
    const message = await findMessageVirtualizedById({
      hud,
      messageId: messages[i].getAttribute("data-message-id"),
    });
    info(`Checking "${expectedMessage}"`);

    // Collapsed Warning group
    if (expectedMessage.startsWith("▶︎⚠")) {
      is(
        message.querySelector(".arrow").getAttribute("aria-expanded"),
        "false",
        "There's a collapsed arrow"
      );
      is(
        message.getAttribute("data-indent"),
        "0",
        "The warningGroup has the expected indent"
      );
      expectedMessage = expectedMessage.replace("▶︎⚠ ", "");
    }

    // Expanded Warning group
    if (expectedMessage.startsWith("▼︎⚠")) {
      is(
        message.querySelector(".arrow").getAttribute("aria-expanded"),
        "true",
        "There's an expanded arrow"
      );
      is(
        message.getAttribute("data-indent"),
        "0",
        "The warningGroup has the expected indent"
      );
      expectedMessage = expectedMessage.replace("▼︎⚠ ", "");
    }

    // Collapsed console.group
    if (expectedMessage.startsWith("▶︎")) {
      is(
        message.querySelector(".arrow").getAttribute("aria-expanded"),
        "false",
        "There's a collapsed arrow"
      );
      expectedMessage = expectedMessage.replace("▶︎ ", "");
    }

    // Expanded console.group
    if (expectedMessage.startsWith("▼")) {
      is(
        message.querySelector(".arrow").getAttribute("aria-expanded"),
        "true",
        "There's an expanded arrow"
      );
      expectedMessage = expectedMessage.replace("▼ ", "");
    }

    // In-group message
    if (expectedMessage.startsWith("|")) {
      if (isInWarningGroup(i)) {
        ok(
          message.querySelector(".warning-indent"),
          "The message has the expected indent"
        );
      }

      expectedMessage = expectedMessage.replace("| ", "");
    } else {
      is(
        message.getAttribute("data-indent"),
        "0",
        "The message has the expected indent"
      );
    }

    ok(
      message.textContent.trim().includes(expectedMessage.trim()),
      `Message includes ` +
        `the expected "${expectedMessage}" content - "${message.textContent.trim()}"`
    );
  }
}

/**
 * Check that there is a message with the specified text that has the specified
 * stack information.  Self-hosted frames are ignored.
 *
 * @param {WebConsole} hud
 * @param {string} text
 *        message substring to look for
 * @param {Array<number>} expectedFrameLines
 *        line numbers of the frames expected in the stack
 */
async function checkMessageStack(hud, text, expectedFrameLines) {
  info(`Checking message stack for "${text}"`);
  const msgNode = await waitFor(
    () => findErrorMessage(hud, text),
    `Couln't find message including "${text}"`
  );
  ok(!msgNode.classList.contains("open"), `Error logged not expanded`);

  const button = await waitFor(
    () => msgNode.querySelector(".collapse-button"),
    `Couldn't find the expand button on "${text}" message`
  );
  button.click();

  const framesNode = await waitFor(
    () => msgNode.querySelector(".message-body-wrapper > .stacktrace .frames"),
    `Couldn't find stacktrace frames on "${text}" message`
  );
  const frameNodes = Array.from(framesNode.querySelectorAll(".frame")).filter(
    el => {
      const fileName = el.querySelector(".filename").textContent;
      return (
        fileName !== "self-hosted" &&
        !fileName.startsWith("chrome:") &&
        !fileName.startsWith("resource:")
      );
    }
  );

  for (let i = 0; i < frameNodes.length; i++) {
    const frameNode = frameNodes[i];
    is(
      frameNode.querySelector(".line").textContent,
      expectedFrameLines[i].toString(),
      `Found line ${expectedFrameLines[i]} for frame #${i}`
    );
  }

  is(
    frameNodes.length,
    expectedFrameLines.length,
    `Found ${frameNodes.length} frames`
  );
}

/**
 * Reload the content page.
 *
 * @returns {Promise} A promise that will return when the page is fully loaded (i.e., the
 *                    `load` event was fired).
 */
function reloadPage() {
  const onLoad = BrowserTestUtils.waitForContentEvent(
    gBrowser.selectedBrowser,
    "load",
    true
  );
  SpecialPowers.spawn(gBrowser.selectedBrowser, [], () => {
    content.location.reload();
  });
  return onLoad;
}

/**
 * Check if the editor mode is enabled (i.e. .webconsole-app has the expected class).
 *
 * @param {WebConsole} hud
 * @returns {boolean}
 */
function isEditorModeEnabled(hud) {
  const { outputNode } = hud.ui;
  const appNode = outputNode.querySelector(".webconsole-app");
  return appNode.classList.contains("jsterm-editor");
}

/**
 * Toggle the layout between in-line and editor.
 *
 * @param {WebConsole} hud
 * @returns {Promise} A promise that resolves once the layout change was rendered.
 */
function toggleLayout(hud) {
  const isMacOS = Services.appinfo.OS === "Darwin";
  const enabled = isEditorModeEnabled(hud);

  EventUtils.synthesizeKey("b", {
    [isMacOS ? "metaKey" : "ctrlKey"]: true,
  });
  return waitFor(() => isEditorModeEnabled(hud) === !enabled);
}

/**
 * Wait until all lazily fetch requests in netmonitor get finished.
 * Otherwise test will be shutdown too early and cause failure.
 */
async function waitForLazyRequests(toolbox) {
  const ui = toolbox.getCurrentPanel().hud.ui;
  return waitUntil(() => {
    return (
      !ui.networkDataProvider.lazyRequestData.size &&
      // Make sure that batched request updates are all complete
      // as they trigger late lazy data requests.
      !ui.wrapper.queuedRequestUpdates.length
    );
  });
}

/**
 * Clear the console output and wait for eventual object actors to be released.
 *
 * @param {WebConsole} hud
 * @param {object} An options object with the following properties:
 *                 - {Boolean} keepStorage: true to prevent clearing the messages storage.
 */
async function clearOutput(hud, { keepStorage = false } = {}) {
  const { ui } = hud;
  const promises = [ui.once("messages-cleared")];

  // If there's an object inspector, we need to wait for the actors to be released.
  if (ui.outputNode.querySelector(".object-inspector")) {
    promises.push(ui.once("fronts-released"));
  }

  ui.clearOutput(!keepStorage);
  await Promise.all(promises);
}

/**
 * Retrieve all the items of the context selector menu.
 *
 * @param {WebConsole} hud
 * @return Array<Element>
 */
function getContextSelectorItems(hud) {
  const toolbox = hud.toolbox;
  const doc = toolbox ? toolbox.doc : hud.chromeWindow.document;
  const list = doc.getElementById(
    "webconsole-console-evaluation-context-selector-menu-list"
  );
  return Array.from(list.querySelectorAll("li.menuitem button, hr"));
}

/**
 * Check that the evaluation context selector menu has the expected item, in the expected
 * state.
 *
 * @param {WebConsole} hud
 * @param {Array<object>} expected: An array of object (see checkContextSelectorMenuItemAt
 *                        for expected properties)
 */
function checkContextSelectorMenu(hud, expected) {
  const items = getContextSelectorItems(hud);

  is(
    items.length,
    expected.length,
    "The context selector menu has the expected number of items"
  );

  expected.forEach((expectedItem, i) => {
    checkContextSelectorMenuItemAt(hud, i, expectedItem);
  });
}

/**
 * Check that the evaluation context selector menu has the expected item at the specified index.
 *
 * @param {WebConsole} hud
 * @param {number} index
 * @param {object} expected
 * @param {string} expected.label: The label of the target
 * @param {string} expected.tooltip: The tooltip of the target element in the menu
 * @param {boolean} expected.checked: if the target should be selected or not
 * @param {boolean} expected.separator: if the element is a simple separator
 * @param {boolean} expected.indented: if the element is indented
 */
function checkContextSelectorMenuItemAt(hud, index, expected) {
  const el = getContextSelectorItems(hud).at(index);

  if (expected.separator === true) {
    is(el.getAttribute("role"), "menuseparator", "The element is a separator");
    return;
  }

  const elChecked = el.getAttribute("aria-checked") === "true";
  const elTooltip = el.getAttribute("title");
  const elLabel = el.querySelector(".label").innerText;
  const indented = el.classList.contains("indented");

  is(elLabel, expected.label, `The item has the expected label`);
  is(elTooltip, expected.tooltip, `Item "${elLabel}" has the expected tooltip`);
  is(
    elChecked,
    expected.checked,
    `Item "${elLabel}" is ${expected.checked ? "checked" : "unchecked"}`
  );
  is(
    indented,
    expected.indented ?? false,
    `Item "${elLabel}" is ${!indented ? " not" : ""} indented`
  );
}

/**
 * Select a target in the context selector.
 *
 * @param {WebConsole} hud
 * @param {string} targetLabel: The label of the target to select.
 */
function selectTargetInContextSelector(hud, targetLabel) {
  const items = getContextSelectorItems(hud);
  const itemToSelect = items.find(
    item => item.querySelector(".label")?.innerText === targetLabel
  );
  if (!itemToSelect) {
    ok(false, `Couldn't find target with "${targetLabel}" label`);
    return;
  }

  itemToSelect.click();
}

/**
 * A helper that returns the size of the image that was just put into the clipboard by the
 * :screenshot command.
 *
 * @return The {width, height} dimension object.
 */
async function getImageSizeFromClipboard() {
  const clipid = Ci.nsIClipboard;
  const clip = Cc["@mozilla.org/widget/clipboard;1"].getService(clipid);
  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  const flavor = "image/png";
  trans.init(null);
  trans.addDataFlavor(flavor);

  clip.getData(
    trans,
    clipid.kGlobalClipboard,
    SpecialPowers.wrap(window).browsingContext.currentWindowContext
  );
  const data = {};
  trans.getTransferData(flavor, data);

  ok(data.value, "screenshot exists");

  let image = data.value;

  // Due to the differences in how images could be stored in the clipboard the
  // checks below are needed. The clipboard could already provide the image as
  // byte streams or as image container. If it's not possible obtain a
  // byte stream, the function throws.

  if (image instanceof Ci.imgIContainer) {
    image = Cc["@mozilla.org/image/tools;1"]
      .getService(Ci.imgITools)
      .encodeImage(image, flavor);
  }

  if (!(image instanceof Ci.nsIInputStream)) {
    throw new Error("Unable to read image data");
  }

  const binaryStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
    Ci.nsIBinaryInputStream
  );
  binaryStream.setInputStream(image);
  const available = binaryStream.available();
  const buffer = new ArrayBuffer(available);
  is(
    binaryStream.readArrayBuffer(available, buffer),
    available,
    "Read expected amount of data"
  );

  // We are going to load the image in the content page to measure its size.
  // We don't want to insert the image directly in the browser's document
  // (which is value of the global `document` here). Doing so might push the
  // toolbox upwards, shrink the content page and fail the fullpage screenshot
  // test.
  return SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [buffer],
    async function (_buffer) {
      const img = content.document.createElement("img");
      const loaded = new Promise(r => {
        img.addEventListener("load", r, { once: true });
      });

      // Build a URL from the buffer passed to the ContentTask
      const url = content.URL.createObjectURL(
        new Blob([_buffer], { type: "image/png" })
      );

      // Load the image
      img.src = url;
      content.document.documentElement.appendChild(img);

      info("Waiting for the clipboard image to load in the content page");
      await loaded;

      // Remove the image and revoke the URL.
      img.remove();
      content.URL.revokeObjectURL(url);

      return {
        width: img.width,
        height: img.height,
      };
    }
  );
}

/**
 * Perform default setup for URL classifier tests (eg used for ETP warnings).
 *
 * @param {object=} options
 * @param {boolean=} options.enableTrackingProtection
 *        If true, sets the preference privacy.trackingprotection.enabled=true.
 *        the test. Defaults to true.
 */
async function setupUrlClassifierTest(options = {}) {
  const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
  );
  await UrlClassifierTestUtils.addTestTrackers();
  registerCleanupFunction(async function () {
    await UrlClassifierTestUtils.cleanupTestTrackers();
  });

  const { enableTrackingProtection = true } = options;
  if (enableTrackingProtection) {
    await pushPref("privacy.trackingprotection.enabled", true);
  }
}
