/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");

const {
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");
const messageToggleDetails = l10n.getStr("webconsole-message-toggle-details");

function CollapseButton(props) {
  const { open, onClick, title = messageToggleDetails } = props;

  return dom.button({
    "aria-expanded": open ? "true" : "false",
    "aria-label": title,
    className: "arrow collapse-button",
    onClick,
    onMouseDown: e => {
      // prevent focus from moving to the disclosure if clicked,
      // which is annoying if on the input
      e.preventDefault();
      // Clearing the text selection to allow the message to collpase.
      e.target.ownerDocument.defaultView.getSelection().removeAllRanges();
    },
    title,
  });
}

module.exports = CollapseButton;
