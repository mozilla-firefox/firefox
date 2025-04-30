/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

/**
 * A CSS class.
 */
exports.classes = {
  // The CSS class name.
  name: PropTypes.string,

  // Whether or not the CSS class is applied.
  isApplied: PropTypes.bool,
};

/**
 * A CSS declaration.
 */
const declaration = (exports.declaration = {
  // Array of the computed properties for a CSS declaration.
  computedProperties: PropTypes.arrayOf(
    PropTypes.shape({
      // Whether or not the computed property is overridden.
      isOverridden: PropTypes.bool,
      // The computed property name.
      name: PropTypes.string,
      // The computed priority (either "important" or an empty string).
      priority: PropTypes.string,
      // The computed property value.
      value: PropTypes.string,
    })
  ),

  // An unique CSS declaration id.
  id: PropTypes.string,

  // Whether or not the declaration is valid. (Does it make sense for this value
  // to be assigned to this property name?)
  isDeclarationValid: PropTypes.bool,

  // Whether or not the declaration is enabled.
  isEnabled: PropTypes.bool,

  // Whether or not the declaration is invisible. In an inherited rule, only the
  // inherited declarations are shown and the rest are considered invisible.
  isInvisible: PropTypes.bool,

  // Whether or not the declaration's property name is known.
  isKnownProperty: PropTypes.bool,

  // Whether or not the property name is valid.
  isNameValid: PropTypes.bool,

  // Whether or not the the declaration is overridden.
  isOverridden: PropTypes.bool,

  // Whether or not the declaration is changed by the user.
  isPropertyChanged: PropTypes.bool,

  // The declaration's property name.
  name: PropTypes.string,

  // The declaration's priority (either "important" or an empty string).
  priority: PropTypes.string,

  // The declaration's property value.
  value: PropTypes.string,
});

/**
 * The pseudo classes redux structure.
 */
exports.pseudoClasses = {
  // An object containing the :active pseudo class toggle state.
  ":active": PropTypes.shape({
    // Whether or not the :active pseudo class is checked.
    isChecked: PropTypes.bool,
    // Whether or not the :active pseudo class is disabled.
    isDisabled: PropTypes.bool,
  }),

  // An object containing the :focus pseudo class toggle state.
  ":focus": PropTypes.shape({
    // Whether or not the :focus pseudo class is checked
    isChecked: PropTypes.bool,
    // Whether or not the :focus pseudo class is disabled.
    isDisabled: PropTypes.bool,
  }),

  // An object containing the :focus-within pseudo class toggle state.
  ":focus-within": PropTypes.shape({
    // Whether or not the :focus-within pseudo class is checked
    isChecked: PropTypes.bool,
    // Whether or not the :focus-within pseudo class is disabled.
    isDisabled: PropTypes.bool,
  }),

  // An object containing the :hover pseudo class toggle state.
  ":hover": PropTypes.shape({
    // Whether or not the :hover pseudo class is checked.
    isChecked: PropTypes.bool,
    // Whether or not the :hover pseudo class is disabled.
    isDisabled: PropTypes.bool,
  }),
};

/**
 * A CSS selector.
 */
const selector = (exports.selector = {
  // Function that returns a Promise containing an unique CSS selector.
  getUniqueSelector: PropTypes.func,
  // Array of the indexes in `selectors` that match the selected element.
  matchedSelectorIndexes: PropTypes.arrayOf(PropTypes.number),
  // The CSS rule's selector text content.
  selectorText: PropTypes.string,
  // Array of the CSS rule's selectors.
  selectors: PropTypes.arrayOf(PropTypes.string),
});

/**
 * A CSS Rule.
 */
exports.rule = {
  // Array of CSS declarations.
  declarations: PropTypes.arrayOf(PropTypes.shape(declaration)),

  // An unique CSS rule id.
  id: PropTypes.string,

  // Whether or not the rule does not match the current selected element.
  isUnmatched: PropTypes.bool,

  // Whether or not the rule is an user agent style.
  isUserAgentStyle: PropTypes.bool,

  // An object containing information about the CSS keyframes rules.
  keyframesRule: PropTypes.shape({
    // The actor ID of the keyframes rule.
    id: PropTypes.string,
    // The keyframes rule name.
    keyframesName: PropTypes.string,
  }),

  // The pseudo-element keyword used in the rule.
  pseudoElement: PropTypes.string,

  // An object containing information about the CSS rule's selector.
  selector: PropTypes.shape(selector),

  // The CSS rule type.
  type: PropTypes.number,
};
