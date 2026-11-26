/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const GripMessageBody = require("resource://devtools/client/webconsole/components/Output/GripMessageBody.js");
const ConsoleTable = createFactory(
  require("resource://devtools/client/webconsole/components/Output/ConsoleTable.js")
);
const {
  isGroupType,
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

const Message = createFactory(
  require("resource://devtools/client/webconsole/components/Output/Message.js")
);

ConsoleApiCall.displayName = "ConsoleApiCall";

ConsoleApiCall.propTypes = {
  dispatch: PropTypes.func.isRequired,
  message: PropTypes.object.isRequired,
  open: PropTypes.bool,
  serviceContainer: PropTypes.object.isRequired,
  timestampsVisible: PropTypes.bool.isRequired,
  maybeScrollToBottom: PropTypes.func,
};

ConsoleApiCall.defaultProps = {
  open: false,
};

function ConsoleApiCall(props) {
  const {
    dispatch,
    message,
    open,
    serviceContainer,
    timestampsVisible,
    repeat,
    maybeScrollToBottom,
    setExpanded,
  } = props;
  const {
    id: messageId,
    indent,
    source,
    type,
    level,
    stacktrace,
    frame,
    timeStamp,
    parameters,
    messageText,
    prefix,
    userProvidedStyles,
  } = message;

  let messageBody;
  const messageBodyConfig = {
    dispatch,
    messageId,
    parameters,
    userProvidedStyles,
    serviceContainer,
    type,
    maybeScrollToBottom,
    setExpanded,
    // When the object is a parameter of a console.dir call, we always want to show its
    // properties, like regular object (i.e. not showing the DOM tree for an Element, or
    // only showing the message + stacktrace for Error object).
    customFormat: type !== "dir",
  };

  if (type === "trace") {
    const traceParametersBody =
      Array.isArray(parameters) && parameters.length
        ? [" "].concat(formatReps(messageBodyConfig))
        : [];

    messageBody = [
      dom.span({ className: "cm-variable" }, "console.trace()"),
      ...traceParametersBody,
    ];
  } else if (type === "assert") {
    const reps = formatReps(messageBodyConfig);
    messageBody = dom.span({}, "Assertion failed: ", reps);
  } else if (type === "table") {
    // TODO: Chrome does not output anything, see if we want to keep this
    messageBody = dom.span({ className: "cm-variable" }, "console.table()");
  } else if (parameters) {
    messageBody = formatReps(messageBodyConfig);
    if (prefix) {
      messageBody.unshift(
        dom.span(
          {
            className: "console-message-prefix",
          },
          `${prefix}: `
        )
      );
    }
  } else if (typeof messageText === "string") {
    messageBody = messageText;
  } else if (messageText) {
    messageBody = GripMessageBody({
      dispatch,
      messageId,
      grip: messageText,
      serviceContainer,
      useQuotes: false,
      transformEmptyString: true,
      setExpanded,
      type,
    });
  }

  let attachment = null;
  if (type === "table") {
    attachment = ConsoleTable({
      dispatch,
      id: message.id,
      serviceContainer,
      parameters: message.parameters,
    });
  }

  let collapseTitle = null;
  if (isGroupType(type)) {
    collapseTitle = l10n.getStr("webconsole-group-toggle");
  }

  const collapsible =
    isGroupType(type) ||
    ((type === "error" || type === "logPoint") && Array.isArray(stacktrace));
  const topLevelClasses = ["cm-s-mozilla"];

  return Message({
    messageId,
    open,
    collapsible,
    collapseTitle,
    source,
    type,
    level,
    topLevelClasses,
    messageBody,
    repeat,
    frame,
    stacktrace,
    attachment,
    serviceContainer,
    dispatch,
    indent,
    timeStamp,
    timestampsVisible,
    parameters,
    message,
    maybeScrollToBottom,
  });
}

function formatReps(options = {}) {
  const {
    dispatch,
    loadedObjectProperties,
    loadedObjectEntries,
    messageId,
    parameters,
    serviceContainer,
    userProvidedStyles,
    type,
    maybeScrollToBottom,
    setExpanded,
    customFormat,
  } = options;

  const elements = [];
  const parametersLength = parameters.length;
  for (let i = 0; i < parametersLength; i++) {
    elements.push(
      GripMessageBody({
        dispatch,
        messageId,
        grip: parameters[i],
        key: i,
        userProvidedStyle: userProvidedStyles ? userProvidedStyles[i] : null,
        serviceContainer,
        useQuotes: false,
        loadedObjectProperties,
        loadedObjectEntries,
        type,
        maybeScrollToBottom,
        setExpanded,
        customFormat,
      })
    );

    // We need to interleave a space if we are not on the last element AND
    // if we are not between 2 messages with user provided style.
    if (
      i !== parametersLength - 1 &&
      (!userProvidedStyles ||
        userProvidedStyles[i] === undefined ||
        userProvidedStyles[i + 1] === undefined)
    ) {
      elements.push(" ");
    }
  }

  return elements;
}

module.exports = ConsoleApiCall;
