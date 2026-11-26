/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const {
  MESSAGE_TYPE,
} = require("resource://devtools/client/webconsole/constants.js");

const l10nLevels = {
  error: "webconsole-level-error",
  warn: "webconsole-level-warn",
  info: "webconsole-level-info",
  log: "webconsole-level-log",
  debug: "webconsole-level-debug",
  jstracer: "webconsole-level-jstracer",
};

// Store common icons so they can be used without recreating the element
// during render.
const CONSTANT_ICONS = Object.entries(l10nLevels).reduce((acc, [key]) => {
  acc[key] = getIconElement(key);
  return acc;
}, {});

function getIconElement(level, type, title) {
  if (!title && l10nLevels[level]) {
    title = l10n.getStr(l10nLevels[level]);
  }
  const classnames = ["icon"];

  if (type === "logPoint") {
    title = l10n.getStr("webconsole-logpoint-title");
    classnames.push("logpoint");
  } else if (type === "blockedReason") {
    title = l10n.getStr("webconsole-blocked-request");
  } else if (type === MESSAGE_TYPE.COMMAND) {
    title = l10n.getStr("webconsole-command-title");
  } else if (type === MESSAGE_TYPE.RESULT) {
    title = l10n.getStr("webconsole-result-title");
  } else if (level == "jstracer" || type == MESSAGE_TYPE.JSTRACER) {
    // Actual traces uses JSTracerTrace objects and `level` attribute,
    // while log messages relating to tracing uses ConsoleApiCall, where level == "log" and so rather uses `type` attribute.
    classnames.push("logtrace");
  }

  return dom.span({
    className: classnames.join(" "),
    title,
    "aria-live": "off",
  });
}

MessageIcon.displayName = "MessageIcon";
MessageIcon.propTypes = {
  level: PropTypes.string.isRequired,
  type: PropTypes.string,
  title: PropTypes.string,
};

function MessageIcon(props) {
  const { level, type, title } = props;

  if (type) {
    return getIconElement(level, type, title);
  }

  return CONSTANT_ICONS[level] || getIconElement(level);
}

module.exports = MessageIcon;
