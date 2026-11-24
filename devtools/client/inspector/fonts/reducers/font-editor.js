/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  getStr,
} = require("resource://devtools/client/inspector/fonts/utils/l10n.js");
const {
  parseFontVariationAxes,
} = require("resource://devtools/client/inspector/fonts/utils/font-utils.js");

const {
  APPLY_FONT_VARIATION_INSTANCE,
  RESET_EDITOR,
  SET_FONT_EDITOR_DISABLED,
  UPDATE_AXIS_VALUE,
  UPDATE_EDITOR_STATE,
  UPDATE_PROPERTY_VALUE,
  UPDATE_WARNING_MESSAGE,
} = require("resource://devtools/client/inspector/fonts/actions/index.js");

const CUSTOM_INSTANCE_NAME = getStr("font-inspector-custom-instance-name");

const INITIAL_STATE = {
  // Variable font axes.
  axes: {},
  // Copy of the most recent axes values. Used to revert from a named instance.
  customInstanceValues: [],
  // When true, prevent users from interacting with inputs in the font editor.
  disabled: false,
  // Fonts used on the selected element.
  fonts: [],
  // Current selected font variation instance.
  instance: {
    name: CUSTOM_INSTANCE_NAME,
    values: [],
  },
  // CSS font properties defined on the selected rule.
  properties: {},
  // Unique identifier for the selected element.
  id: "",
  // Warning message with the reason why the font editor cannot be shown.
  warning: getStr("font-inspector-no-fonts-used"),
};

const reducers = {
  // Update font editor with the axes and values defined by a font variation instance.
  [APPLY_FONT_VARIATION_INSTANCE](state, { name, values }) {
    const newState = { ...state };
    newState.instance.name = name;
    newState.instance.values = values;

    if (Array.isArray(values) && values.length) {
      newState.axes = values.reduce((acc, value) => {
        acc[value.axis] = value.value;
        return acc;
      }, {});
    }

    return newState;
  },

  [RESET_EDITOR]() {
    return { ...INITIAL_STATE };
  },

  [UPDATE_AXIS_VALUE](state, { axis, value }) {
    const newState = { ...state };
    newState.axes[axis] = value;

    // Cache the latest axes and their values to restore them when switching back from
    // a named font variation instance to the custom font variation instance.
    newState.customInstanceValues = Object.keys(state.axes).map(axisName => {
      return { axis: [axisName], value: state.axes[axisName] };
    });

    // As soon as an axis value is manually updated, mark the custom font variation
    // instance as selected.
    newState.instance.name = CUSTOM_INSTANCE_NAME;

    return newState;
  },

  [SET_FONT_EDITOR_DISABLED](state, { disabled }) {
    return { ...state, disabled };
  },

  [UPDATE_EDITOR_STATE](state, { fonts, properties, id }) {
    const axes = parseFontVariationAxes(properties["font-variation-settings"]);

    // If not defined in font-variation-settings, setup "wght" axis with the value of
    // "font-weight" if it is numeric and not a keyword.
    const weight = properties["font-weight"];
    if (
      axes.wght === undefined &&
      parseFloat(weight).toString() === weight.toString()
    ) {
      axes.wght = parseFloat(weight);
    }

    // If not defined in font-variation-settings, setup "wdth" axis with the percentage
    // number from the value of "font-stretch" if it is not a keyword.
    const stretch = properties["font-stretch"];
    // Match the number part from values like: 10%, 10.55%, 0.2%
    // If there's a match, the number is the second item in the match array.
    const match = stretch.trim().match(/^(\d+(.\d+)?)%$/);
    if (axes.wdth === undefined && match && match[1]) {
      axes.wdth = parseFloat(match[1]);
    }

    // If not defined in font-variation-settings, setup "slnt" axis with the negative
    // of the "font-style: oblique" angle, if any.
    const style = properties["font-style"];
    const obliqueMatch = style.trim().match(/^oblique(?:\s*(\d+(.\d+)?)deg)?$/);
    if (axes.slnt === undefined && obliqueMatch) {
      if (obliqueMatch[1]) {
        // Negate the angle because CSS and OpenType measure in opposite directions.
        axes.slnt = -parseFloat(obliqueMatch[1]);
      } else {
        // Lack of an <angle> for "font-style: oblique" represents "14deg".
        axes.slnt = -14;
      }
    }

    // If not defined in font-variation-settings, setup "ital" axis with 0 for
    // "font-style: normal" or 1 for "font-style: italic".
    if (axes.ital === undefined) {
      if (style === "normal") {
        axes.ital = 0;
      } else if (style === "italic") {
        axes.ital = 1;
      }
    }

    return { ...state, axes, fonts, properties, id };
  },

  [UPDATE_PROPERTY_VALUE](state, { property, value }) {
    const newState = { ...state };
    newState.properties[property] = value;
    return newState;
  },

  [UPDATE_WARNING_MESSAGE](state, { warning }) {
    return { ...state, warning };
  },
};

module.exports = function (state = INITIAL_STATE, action) {
  const reducer = reducers[action.type];
  if (!reducer) {
    return state;
  }
  return reducer(state, action);
};
