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

class LetterSpacing extends PureComponent {
  static get propTypes() {
    return {
      disabled: PropTypes.bool.isRequired,
      onChange: PropTypes.func.isRequired,
      value: PropTypes.string.isRequired,
    };
  }

  constructor(props) {
    super(props);
    // Local state for min/max bounds indexed by unit to allow user input that
    // goes out-of-bounds while still providing a meaningful default range. The indexing
    // by unit is needed to account for unit conversion (ex: em to px) where the operation
    // may result in out-of-bounds values. Avoiding React's state and setState() because
    // `value` is a prop coming from the Redux store while min/max are local. Reconciling
    // value/unit changes is needlessly complicated and adds unnecessary re-renders.
    this.historicMin = {};
    this.historicMax = {};
  }

  getDefaultMinMax(unit) {
    let min;
    let max;
    switch (unit) {
      case "px":
        min = -10;
        max = 10;
        break;
      default:
        min = -0.2;
        max = 0.6;
        break;
    }

    return { min, max };
  }

  render() {
    // For a unitless or a NaN value, default unit to "em".
    const unit = getUnitFromValue(this.props.value) || "em";
    // When the initial value of "letter-spacing" is "normal", the parsed value
    // is not a number (NaN). Guard by setting the default value to 0.
    const isKeywordValue = this.props.value === "normal";
    const value = isKeywordValue ? 0 : parseFloat(this.props.value);

    let { min, max } = this.getDefaultMinMax(unit);
    min = Math.min(min, value);
    max = Math.max(max, value);
    // Allow lower and upper bounds to move to accomodate the incoming value.
    this.historicMin[unit] = this.historicMin[unit]
      ? Math.min(this.historicMin[unit], min)
      : min;
    this.historicMax[unit] = this.historicMax[unit]
      ? Math.max(this.historicMax[unit], max)
      : max;

    return FontPropertyValue({
      allowOverflow: true,
      allowUnderflow: true,
      disabled: this.props.disabled,
      label: getStr("font-inspector-letter-spacing-label"),
      min: this.historicMin[unit],
      max: this.historicMax[unit],
      name: "letter-spacing",
      onChange: this.props.onChange,
      // Increase the increment granularity because letter spacing is very sensitive.
      step: getStepForUnit(unit) / 100,
      // Show the value input and unit only when the value is not a keyword.
      showInput: !isKeywordValue,
      showUnit: !isKeywordValue,
      unit,
      unitOptions: ["em", "rem", "px"],
      value,
      // Show the value as a read-only label if it's a keyword.
      valueLabel: isKeywordValue ? this.props.value : null,
    });
  }
}

module.exports = LetterSpacing;
