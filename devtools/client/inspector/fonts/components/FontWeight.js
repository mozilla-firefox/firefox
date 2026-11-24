/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createFactory,
  PureComponent,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

const FontPropertyValue = createFactory(
  require("resource://devtools/client/inspector/fonts/components/FontPropertyValue.js")
);

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");

class FontWeight extends PureComponent {
  static get propTypes() {
    return {
      disabled: PropTypes.bool.isRequired,
      onChange: PropTypes.func.isRequired,
      value: PropTypes.string.isRequired,
    };
  }

  render() {
    return FontPropertyValue({
      disabled: this.props.disabled,
      label: getStr("font-inspector-font-weight-label"),
      min: 100,
      max: 900,
      name: "font-weight",
      onChange: this.props.onChange,
      step: 100,
      unit: null,
      value: parseFloat(this.props.value),
    });
  }
}

module.exports = FontWeight;
