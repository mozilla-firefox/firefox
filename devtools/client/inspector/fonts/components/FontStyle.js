/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");

class FontStyle extends PureComponent {
  static get propTypes() {
    return {
      disabled: PropTypes.bool.isRequired,
      onChange: PropTypes.func.isRequired,
      value: PropTypes.string.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.name = "font-style";
    this.onToggle = this.onToggle.bind(this);
  }

  onToggle(e) {
    this.props.onChange(
      this.name,
      e.target.checked ? "italic" : "normal",
      null
    );
  }

  render() {
    return dom.label(
      {
        className: "font-control",
      },
      dom.span(
        {
          className: "font-control-label",
        },
        getStr("font-inspector-font-italic-label")
      ),
      dom.div(
        {
          className: "font-control-input",
        },
        dom.input({
          checked:
            this.props.value === "italic" || this.props.value === "oblique",
          className: "devtools-checkbox-toggle",
          disabled: this.props.disabled,
          name: this.name,
          onChange: this.onToggle,
          type: "checkbox",
        })
      )
    );
  }
}

module.exports = FontStyle;
