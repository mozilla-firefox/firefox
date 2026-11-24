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

class LineHeight extends PureComponent {
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
    // When the initial value of "line-height" is "normal", the parsed value
    // is not a number (NaN). Guard by setting the default value to 1.2.
    // This will be the starting point for changing the value by dragging the slider.
    // @see https://searchfox.org/mozilla-central/rev/1133b6716d9a8131c09754f3f29288484896b8b6/layout/generic/ReflowInput.cpp#2786
    const isKeywordValue = this.props.value === "normal";
    const value = isKeywordValue ? 1.2 : parseFloat(this.props.value);

    // When values for line-height are be unitless, getUnitFromValue() returns null.
    // In that case, set the unit to an empty string for special treatment in conversion.
    const unit = getUnitFromValue(this.props.value) || "";
    let max;
    switch (unit) {
      case "":
      case "em":
      case "rem":
        max = 2;
        break;
      case "vh":
      case "vw":
      case "vmin":
      case "vmax":
        max = 15;
        break;
      case "%":
        max = 200;
        break;
      default:
        max = 108;
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
      label: getStr("font-inspector-line-height-label"),
      min: 0,
      max: this.historicMax[unit],
      name: "line-height",
      onChange: this.props.onChange,
      step: getStepForUnit(unit),
      // Show the value input and unit only when the value is not a keyword.
      showInput: !isKeywordValue,
      showUnit: !isKeywordValue,
      unit,
      unitOptions: ["", "em", "%", "px"],
      value,
      // Show the value as a read-only label if it's a keyword.
      valueLabel: isKeywordValue ? this.props.value : null,
    });
  }
}

module.exports = LineHeight;
