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
const {
  getUnitFromValue,
  getStepForUnit,
} = require("resource://devtools/client/inspector/fonts/utils/font-utils.js");

class FontSize extends PureComponent {
  static get propTypes() {
    return {
      disabled: PropTypes.bool.isRequired,
      onChange: PropTypes.func.isRequired,
      value: PropTypes.string.isRequired,
    };
  }

  constructor(props) {
    super(props);
    this.historicMax = {};
  }

  render() {
    const value = parseFloat(this.props.value);
    const unit = getUnitFromValue(this.props.value);
    let max;
    switch (unit) {
      case "em":
      case "rem":
        max = 4;
        break;
      case "vh":
      case "vw":
      case "vmin":
      case "vmax":
        max = 10;
        break;
      case "%":
        max = 200;
        break;
      default:
        max = 72;
        break;
    }

    // Allow the upper bound to increase so it accomodates the out-of-bounds value.
    max = Math.max(max, value);
    // Ensure we store the max value ever reached for this unit type. This will be the
    // max value of the input and slider. Without this memoization, the value and slider
    // thumb get clamped at the upper bound while decrementing an out-of-bounds value.
    this.historicMax[unit] = this.historicMax[unit]
      ? Math.max(this.historicMax[unit], max)
      : max;

    return FontPropertyValue({
      allowOverflow: true,
      disabled: this.props.disabled,
      label: getStr("font-inspector-font-size-label"),
      min: 0,
      max: this.historicMax[unit],
      name: "font-size",
      onChange: this.props.onChange,
      step: getStepForUnit(unit),
      unit,
      unitOptions: ["em", "rem", "%", "px", "pt", "vh", "vw"],
      value,
    });
  }
}

module.exports = FontSize;
