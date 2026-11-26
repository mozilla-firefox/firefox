/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Menu = require("resource://devtools/client/framework/menu.js");
const MenuItem = require("resource://devtools/client/framework/menu-item.js");

const {
  MESSAGE_SOURCE,
} = require("resource://devtools/client/webconsole/constants.js");

const clipboardHelper = require("resource://devtools/shared/platform/clipboard.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const actions = require("resource://devtools/client/webconsole/actions/index.js");

loader.lazyRequireGetter(
  this,
  "saveAs",
  "resource://devtools/shared/DevToolsUtils.js",
  true
);
loader.lazyRequireGetter(
  this,
  "openContentLink",
  "resource://devtools/client/shared/link.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getElementText",
  "resource://devtools/client/webconsole/utils/clipboard.js",
  true
);

/**
 * Create a Menu instance for the webconsole.
 *
 * @param {Event} context menu event
 *        {object} message (optional) message object containing metadata such as:
 *        - {String} source
 *        - {String} request
 * @param {object} options
 *        - {Actions} bound actions
 *        - {WebConsoleWrapper} wrapper instance used for accessing properties like the store
 *          and window.
 */
function createContextMenu(event, message, webConsoleWrapper) {
  const { target } = event;
  const { parentNode, toolbox, hud } = webConsoleWrapper;
  const store = webConsoleWrapper.getStore();
  const { dispatch } = store;

  const messageEl = target.closest(".message");
  const clipboardText = getElementText(messageEl);

  const linkEl = target.closest("a[href]");

  const messageVariable = target.closest(".objectBox");
  // Ensure that console.group and console.groupCollapsed commands are not captured
  const variableText =
    messageVariable &&
    !messageEl.classList.contains("startGroup") &&
    !messageEl.classList.contains("startGroupCollapsed")
      ? messageVariable.textContent
      : null;

  // Retrieve closes actor id from the DOM.
  const actorEl =
    target.closest("[data-link-actor-id]") ||
    target.querySelector("[data-link-actor-id]");
  const actor = actorEl ? actorEl.dataset.linkActorId : null;

  const rootObjectInspector = target.closest(".object-inspector");
  const rootActor = rootObjectInspector
    ? rootObjectInspector.querySelector("[data-link-actor-id]")
    : null;
  // We can have object which are not displayed inside an ObjectInspector (e.g. Errors),
  // so let's default to `actor`.
  const rootActorId = rootActor ? rootActor.dataset.linkActorId : actor;

  const elementNode =
    target.closest(".objectBox-node") || target.closest(".objectBox-textNode");
  const isConnectedElement =
    elementNode && elementNode.querySelector(".open-inspector") !== null;

  const win = parentNode.ownerDocument.defaultView;
  const selection = win.getSelection();

  const { source, request, messageId } = message || {};

  const menu = new Menu({
    id: "webconsole-menu",
    accesskeyConflictsBug: 2073896,
  });

  // Copy URL.
  menu.append(
    new MenuItem({
      id: "console-menu-copy-url",
      ...l10n.getAttributes("webconsole-menu-copy-url"),
      visible: source === MESSAGE_SOURCE.NETWORK || linkEl !== null,
      click: () => {
        if (!request && linkEl == null) {
          return;
        }
        const url = request?.url || linkEl?.href;
        clipboardHelper.copyString(url);
      },
    })
  );

  // Open URL in a new tab.
  menu.append(
    new MenuItem({
      id: "console-menu-open-url",
      ...l10n.getAttributes("webconsole-menu-open-url"),
      visible: source === MESSAGE_SOURCE.NETWORK || linkEl !== null,
      click: () => {
        if (!request && linkEl == null) {
          return;
        }
        openContentLink(request?.url || linkEl?.href, {
          inBackground: true,
          relatedToCurrent: true,
        });
      },
    })
  );

  if (toolbox && request) {
    // Open Network message in the Network panel.
    menu.append(
      new MenuItem({
        id: "console-menu-open-in-network-panel",
        ...l10n.getAttributes("webconsole-menu-open-in-network-panel"),
        visible: source === MESSAGE_SOURCE.NETWORK,
        click: () => dispatch(actions.openNetworkPanel(message.messageId)),
      })
    );
    // Resend Network message.
    menu.append(
      new MenuItem({
        id: "console-menu-resend-network-request",
        ...l10n.getAttributes("webconsole-menu-resend-network-request"),
        visible: source === MESSAGE_SOURCE.NETWORK,
        click: () => dispatch(actions.resendNetworkRequest(messageId)),
      })
    );
  }

  // Open DOM node in the Inspector panel.
  const contentDomReferenceEl = target.closest(
    "[data-link-content-dom-reference]"
  );
  if (isConnectedElement && contentDomReferenceEl) {
    const contentDomReference = contentDomReferenceEl.getAttribute(
      "data-link-content-dom-reference"
    );

    menu.append(
      new MenuItem({
        id: "console-menu-open-node",
        ...l10n.getAttributes("webconsole-menu-open-node-in-inspector"),
        disabled: false,
        click: () =>
          dispatch(
            actions.openNodeInInspector(JSON.parse(contentDomReference))
          ),
      })
    );
  }

  // Store as global variable.
  menu.append(
    new MenuItem({
      id: "console-menu-store",
      ...l10n.getAttributes("webconsole-menu-store-as-global-var"),
      disabled: !actor,
      click: () => dispatch(actions.storeAsGlobal(actor)),
    })
  );

  // Copy message or grip.
  menu.append(
    new MenuItem({
      id: "console-menu-copy",
      ...l10n.getAttributes("webconsole-menu-copy-message"),
      // Disabled if there is no selection and no message element available to copy.
      disabled: selection.isCollapsed && !clipboardText,
      click: () => {
        if (selection.isCollapsed) {
          // If the selection is empty/collapsed, copy the text content of the
          // message for which the context menu was opened.
          clipboardHelper.copyString(clipboardText);
        } else {
          clipboardHelper.copyString(selection.toString());
        }
      },
    })
  );

  // Copy message object.
  menu.append(
    new MenuItem({
      id: "console-menu-copy-object",
      ...l10n.getAttributes("webconsole-menu-copy-object"),
      // Disabled if there is no actor and no variable text associated.
      disabled: !actor && !variableText,
      click: () => dispatch(actions.copyMessageObject(actor, variableText)),
    })
  );

  // Export to clipboard
  menu.append(
    new MenuItem({
      id: "console-menu-export-clipboard",
      ...l10n.getAttributes("webconsole-menu-copy-all-messages"),
      disabled: false,
      async click() {
        const outputText =
          await getUnvirtualizedConsoleOutputText(webConsoleWrapper);
        clipboardHelper.copyString(outputText);
      },
    })
  );

  // Export to file
  menu.append(
    new MenuItem({
      id: "console-menu-export-file",
      ...l10n.getAttributes("webconsole-menu-save-all-messages-file"),
      disabled: false,
      // Note: not async, but returns a promise for the actual save.
      click: async () => {
        const date = new Date();
        const suggestedName =
          `console-export-${date.getFullYear()}-` +
          `${date.getMonth() + 1}-${date.getDate()}_${date.getHours()}-` +
          `${date.getMinutes()}-${date.getSeconds()}.log`;
        const outputText =
          await getUnvirtualizedConsoleOutputText(webConsoleWrapper);
        const data = new TextEncoder().encode(outputText);
        saveAs(window, data, suggestedName);
      },
    })
  );

  // Open object in sidebar.
  const shouldOpenSidebar = store.getState().prefs.sidebarToggle;
  if (shouldOpenSidebar) {
    menu.append(
      new MenuItem({
        id: "console-menu-open-sidebar",
        ...l10n.getAttributes("webconsole-menu-open-in-sidebar"),
        disabled: !rootActorId,
        click: () => dispatch(actions.openSidebar(messageId, rootActorId)),
      })
    );
  }

  // Emit the "menu-open" event for testing.
  const { screenX, screenY } = event;
  menu.once("open", () => webConsoleWrapper.emitForTests("menu-open"));
  menu.popup(screenX, screenY, hud.chromeWindow.document);

  return menu;
}

exports.createContextMenu = createContextMenu;

/**
 * Returns the whole text content of the console output.
 * We're creating a new ConsoleOutput using the current store, turning off virtualization
 * so we can have access to all the messages.
 *
 * @param {WebConsoleWrapper} webConsoleWrapper
 * @returns Promise<String>
 */
async function getUnvirtualizedConsoleOutputText(webConsoleWrapper) {
  return new Promise(resolve => {
    const ReactDOM = require("resource://devtools/client/shared/vendor/react-dom.mjs");
    const {
      createElement,
      createFactory,
    } = require("resource://devtools/client/shared/vendor/react.mjs");
    const ConsoleOutput = createFactory(
      require("resource://devtools/client/webconsole/components/Output/ConsoleOutput.js")
    );
    const {
      Provider,
      createProvider,
    } = require("resource://devtools/client/shared/vendor/react-redux.js");

    const { parentNode, toolbox } = webConsoleWrapper;
    const doc = parentNode.ownerDocument;

    // Create an element that won't impact the layout of the console
    const singleUseElement = doc.createElement("section");
    singleUseElement.classList.add("clipboard-only");
    doc.body.append(singleUseElement);

    const consoleOutput = ConsoleOutput({
      serviceContainer: {
        ...webConsoleWrapper.getServiceContainer(),
        preventStacktraceInitialRenderDelay: true,
      },
      disableVirtualization: true,
    });

    ReactDOM.render(
      createElement(
        Provider,
        {
          store: webConsoleWrapper.getStore(),
        },
        toolbox
          ? createElement(
              createProvider(toolbox.commands.targetCommand.storeId),
              { store: toolbox.commands.targetCommand.store },
              consoleOutput
            )
          : consoleOutput
      ),
      singleUseElement,
      () => {
        resolve(getElementText(singleUseElement));
        singleUseElement.remove();
        ReactDOM.unmountComponentAtNode(singleUseElement);
      }
    );
  });
}
