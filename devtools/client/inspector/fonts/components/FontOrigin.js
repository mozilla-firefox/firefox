/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

loader.lazyRequireGetter(
  this,
  "clipboardHelper",
  "resource://devtools/shared/platform/clipboard.js"
);

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const Types = require("resource://devtools/client/inspector/fonts/types.js");

class FontOrigin extends PureComponent {
  static get propTypes() {
    return {
      font: PropTypes.shape(Types.font).isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.onCopyURL = this.onCopyURL.bind(this);
  }

  clipTitle(title, maxLength = 512) {
    if (title.length > maxLength) {
      return title.substring(0, maxLength - 2) + "…";
    }
    return title;
  }

  onCopyURL() {
    clipboardHelper.copyString(this.props.font.URI);
  }

  render() {
    const url = this.props.font.URI;

    if (!url) {
      return dom.p(
        {
          className: "font-origin system",
        },
        getStr("font-inspector-system")
      );
    }

    return dom.p(
      {
        className: "font-origin remote",
      },
      dom.span(
        {
          className: "url",
          title: this.clipTitle(url),
        },
        url
      ),
      dom.button({
        className: "copy-icon",
        onClick: this.onCopyURL,
        title: getStr("font-inspector-copy-url-title"),
      })
    );
  }
}

module.exports = FontOrigin;
