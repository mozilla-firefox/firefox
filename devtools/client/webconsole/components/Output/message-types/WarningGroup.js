/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// React & Redux
const {
  createFactory,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const Message = createFactory(
  require("resource://devtools/client/webconsole/components/Output/Message.js")
);

const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

WarningGroup.displayName = "WarningGroup";

WarningGroup.propTypes = {
  dispatch: PropTypes.func.isRequired,
  message: PropTypes.object.isRequired,
  timestampsVisible: PropTypes.bool.isRequired,
  serviceContainer: PropTypes.object,
  badge: PropTypes.number.isRequired,
};

function WarningGroup(props) {
  const {
    dispatch,
    message,
    serviceContainer,
    timestampsVisible,
    badge,
    open,
  } = props;

  const { source, type, level, id: messageId, indent, timeStamp } = message;

  const messageBody = [
    message.messageText,
    " ",
    dom.span(
      {
        className: "warning-group-badge",
        title: l10n.getStr("webconsole-warning-group-message-count", {
          count: badge,
        }),
      },
      badge
    ),
  ];
  const topLevelClasses = ["cm-s-mozilla"];

  return Message({
    badge,
    collapsible: true,
    dispatch,
    indent,
    level,
    messageBody,
    messageId,
    open,
    serviceContainer,
    source,
    timeStamp,
    timestampsVisible,
    topLevelClasses,
    type,
    message,
  });
}

module.exports = WarningGroup;
