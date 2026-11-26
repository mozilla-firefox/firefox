/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createFactory,
  createElement,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const ToolboxProvider = require("resource://devtools/client/framework/store-provider.js");
const Message = createFactory(
  require("resource://devtools/client/webconsole/components/Output/Message.js")
);
const actions = require("resource://devtools/client/webconsole/actions/index.js");
const {
  isMessageNetworkError,
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

loader.lazyRequireGetter(
  this,
  "TabboxPanel",
  "resource://devtools/client/netmonitor/src/components/TabboxPanel.js"
);
const {
  getHTTPStatusCodeURL,
} = require("resource://devtools/client/netmonitor/src/utils/doc-utils.js");
const {
  getUnicodeUrl,
} = require("resource://devtools/client/shared/unicode-url.js");
loader.lazyRequireGetter(
  this,
  "getBlockedReasonString",
  "resource://devtools/client/netmonitor/src/utils/l10n.js",
  true
);

const LEARN_MORE = l10n.getStr("webconsole-learn-more");

const isMacOS = Services.appinfo.OS === "Darwin";

NetworkEventMessage.displayName = "NetworkEventMessage";

NetworkEventMessage.propTypes = {
  message: PropTypes.object.isRequired,
  serviceContainer: PropTypes.shape({
    openNetworkPanel: PropTypes.func.isRequired,
    resendNetworkRequest: PropTypes.func.isRequired,
  }),
  timestampsVisible: PropTypes.bool.isRequired,
  networkMessageUpdate: PropTypes.object.isRequired,
};

/**
 * This component is responsible for rendering network messages
 * in the Console panel.
 *
 * Network logs are expandable and the user can inspect it inline
 * within the Console panel (no need to switch to the Network panel).
 *
 * HTTP details are rendered using `TabboxPanel` component used to
 * render contents of the side bar in the Network panel.
 *
 * All HTTP details data are fetched from the backend on-demand
 * when the user is expanding network log for the first time.
 */
function NetworkEventMessage({
  message = {},
  serviceContainer,
  timestampsVisible,
  networkMessageUpdate = {},
  networkMessageActiveTabId,
  dispatch,
  open,
  disabled,
}) {
  const {
    id,
    indent,
    source,
    type,
    level,
    url,
    method,
    isXHR,
    timeStamp,
    blockedReason,
    extension,
    httpVersion,
    status,
    statusText,
    totalTime,
  } = message;

  const topLevelClasses = ["cm-s-mozilla"];
  if (isMessageNetworkError(message)) {
    topLevelClasses.push("error");
  }

  let statusCode, statusInfo;

  if (httpVersion && status && statusText !== undefined) {
    const statusCodeDocURL = getHTTPStatusCodeURL(
      status.toString(),
      "webconsole"
    );
    statusCode = dom.span(
      {
        className: "status-code",
        "data-code": status,
        title: LEARN_MORE,
        onClick: e => {
          e.stopPropagation();
          e.preventDefault();
          serviceContainer.openLink(statusCodeDocURL, e);
        },
      },
      status
    );
    statusInfo = dom.span(
      { className: "status-info" },
      `[${httpVersion} `,
      statusCode,
      ` ${statusText}${
        // We might not get a valid totalTime, in such case, don't display anything
        typeof totalTime === "number" ? ` ${totalTime}ms` : ""
      }]`
    );
  }

  if (blockedReason) {
    statusInfo = dom.span(
      { className: "status-info" },
      getBlockedReasonString(blockedReason, extension)
    );
    topLevelClasses.push("network-message-blocked");
  }

  // Message body components.
  const requestMethod = dom.span({ className: "method" }, method);
  const xhr = isXHR
    ? dom.span({ className: "xhr" }, l10n.getStr("webconsole-xhr-indicator"))
    : null;
  const unicodeURL = getUnicodeUrl(url);
  const requestUrl = dom.a(
    {
      className: "url",
      title: unicodeURL,
      href: url,
      onClick: e => {
        // The href of the <a> is the actual URL, so we need to prevent the navigation
        // within the console panel.
        // We only want to handle Ctrl/Cmd + click to open the link in a new tab.
        e.preventDefault();
        const shouldOpenLink =
          (isMacOS && e.metaKey) || (!isMacOS && e.ctrlKey);
        if (shouldOpenLink) {
          e.stopPropagation();
          serviceContainer.openLink(url, e);
        }
      },
    },
    unicodeURL
  );
  const statusBody = statusInfo
    ? dom.span({ className: "status" }, statusInfo)
    : null;

  const messageBody = [
    xhr,
    // Add whitespace between "XHR" and method for formatting when copying to the clipboard.
    xhr ? " " : null,
    requestMethod,
    requestUrl,
    statusBody,
  ];

  // API consumed by Net monitor UI components. Most of the method
  // are not needed in context of the Console panel (atm) and thus
  // let's just provide empty implementation.
  // Individual methods might be implemented step by step as needed.
  const connector = {
    viewSourceInDebugger: (srcUrl, line, column) => {
      serviceContainer.onViewSourceInDebugger({ url: srcUrl, line, column });
    },
    getLongString: grip => {
      return serviceContainer.getLongString(grip);
    },
    triggerActivity: () => {},
    requestData: (requestId, dataType) => {
      return serviceContainer.requestData(requestId, dataType);
    },
  };

  // The Tabbox element can be rendered in the Browser Console, which will not
  // have a toolbox store available. In this configuration, use the regular
  // TabboxPanel component instead of the ConnectedTabboxPanel.
  const createTabboxElement = function () {
    const toolboxStore = serviceContainer.getToolboxStore();
    const tabboxPanelProps = {
      connector,
      activeTabId: networkMessageActiveTabId,
      request: networkMessageUpdate,
      sourceMapURLService: serviceContainer.sourceMapURLService,
      openLink: serviceContainer.openLink,
      selectTab: tabId => {
        dispatch(actions.selectNetworkMessageTab(tabId));
      },
      openNetworkDetails: enabled => {
        if (!enabled) {
          dispatch(actions.messageClose(id));
        }
      },
      hideToggleButton: true,
      showMessagesView: false,
      targetSearchResult: null,
    };

    return toolboxStore
      ? createElement(
          ToolboxProvider,
          { store: serviceContainer.getToolboxStore() },
          createElement(TabboxPanel.ConnectedTabboxPanel, tabboxPanelProps)
        )
      : createElement(TabboxPanel.TabboxPanel, tabboxPanelProps);
  };

  // Only render the attachment if the network-event is
  // actually opened (performance optimization) and its not disabled.
  const attachment =
    open &&
    !disabled &&
    dom.div(
      {
        className: "network-info network-monitor",
      },
      createTabboxElement()
    );

  const request = { url, method };
  return Message({
    dispatch,
    messageId: id,
    source,
    type,
    level,
    indent,
    collapsible: true,
    open,
    disabled,
    attachment,
    topLevelClasses,
    timeStamp,
    messageBody,
    serviceContainer,
    request,
    timestampsVisible,
    isBlockedNetworkMessage: !!blockedReason,
    message,
  });
}

module.exports = NetworkEventMessage;
