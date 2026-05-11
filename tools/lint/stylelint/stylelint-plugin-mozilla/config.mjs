/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { SYSTEM_COLORS, createRawValuesObject } from "./helpers.mjs";

/**
 * @typedef {object} PropertyTypeConfig
 * @property {string[]} allow Allowed keyword values (e.g., "auto", "none", "transparent")
 * @property {string[]} allowAlias Allowed keyword values that should only be used via local variables
 * @property {string[]} [tokenTypes] Token categories from semantic-categories.mjs whose tokens are valid
 * @property {string[]} [aliasTokenTypes] Token categories from semantic-categories.mjs whose tokens are valid only when used through local custom properties
 * @property {string[]} [allowFunctions] Allowed CSS function names (e.g., "url", "linear-gradient")
 * @property {boolean} [allowUnits] Whether values with CSS units (e.g., "10px", "50%") are allowed
 * @property {string[]} [allowedUnits] Specific unit types allowed (e.g., ["em", "ch", "%"]). If provided, only these units are allowed when allowUnits is true
 * @property {Record<string, string>} [customFixes] Map of raw values to their token replacements for autofix
 * @property {Record<string, string>} [customSuggestions] Map of raw values to their token replacements for suggested fixes
 * @property {boolean} [warnSystemColors] Whether to warn about system colors when there are no suitable tokens to use.
 */

const customColorFixes = {
  "#000": "black",
  "#000000": "black",
  "#fff": "white",
  "#ffffff": "white",
};

const systemColorSuggestions = {
  accentcolor: "var(--color-accent-primary)",
  accentcolortext: "var(--button-text-color-primary)",
  activetext: "var(--link-color-active)",
  buttonborder: "var(--button-border-color)",
  buttonface: "var(--button-background-color)",
  buttontext: "var(--button-text-color)",
  canvas: "var(--background-color-canvas)",
  canvastext: "var(--text-color)",
  field: null,
  fieldtext: null,
  graytext: "var(--text-color-disabled)",
  highlight: null,
  highlighttext: null,
  linktext: "var(--link-color)",
  mark: null,
  marktext: null,
  selecteditem: "var(--color-accent-primary-selected)",
  selecteditemtext: "var(--text-color-accent-primary-selected)",
  visitedtext: "var(--link-color-visited)",
  // deprecated system colors, point to the same tokens as their modern equivalents
  activeborder: "var(--button-border-color)",
  activecaption: "var(--background-color-canvas)",
  appworkspace: "var(--background-color-canvas)",
  background: "var(--background-color-canvas)",
  buttonhighlight: "var(--button-background-color)",
  buttonshadow: "var(--button-background-color)",
  captiontext: "var(--text-color)",
  inactiveborder: "var(--button-border-color)",
  inactivecaption: "var(--background-color-canvas)",
  inactivecaptiontext: "var(--text-color-disabled)",
  infobackground: "var(--background-color-canvas)",
  infotext: "var(--text-color)",
  menu: "var(--background-color-canvas)",
  menutext: "var(--text-color)",
  scrollbar: "var(--background-color-canvas)",
  threeddarkshadow: "var(--button-border-color)",
  threedface: "var(--button-background-color)",
  threedhighlight: "var(--button-border-color)",
  threedlightshadow: "var(--button-border-color)",
  threedshadow: "var(--button-border-color)",
  window: "var(--background-color-canvas)",
  windowframe: "var(--button-border-color)",
  windowtext: "var(--text-color)",
};

// Some "primitive" color tokens can be used directly for background-color, color, fill, stroke, etc.
const versatileColorTokens = [
  "--color-accent-primary",
  "--color-accent-primary-hover",
  "--color-accent-primary-active",
  "--color-accent-primary-selected",
  "--color-accent-attention",
];

/** @type {PropertyTypeConfig} */
const BackgroundColor = {
  allow: [
    "transparent",
    "currentColor",
    "auto",
    "normal",
    "none",
    "white",
    "black",
  ],
  allowAlias: [...SYSTEM_COLORS],
  allowedTokens: [
    "--dragover-tab-group-color-invert",
    "--dragover-tab-group-color-pale",
    "--dragover-tab-group-color",
    "--fc-accent-color",
    "--fc-background",
    "--fc-button-background-active",
    "--fc-button-background-hover",
    "--fc-button-background",
    "--fc-dismiss-button-background-active",
    "--fc-dismiss-button-background-hover",
    "--fc-dismiss-button-background",
    "--fc-primary-button-background-active",
    "--fc-primary-button-background-hover",
    "--fc-primary-button-background",
    "--fxview-background-color-secondary",
    "--fxview-element-background-active",
    "--fxview-element-background-hover",
    "--fxviewtabrow-element-background-active",
    "--fxviewtabrow-element-background-hover",
    "--identity-tab-color",
    "--lwt-accent-color",
    "--newtab-background-card",
    "--newtab-background-color-secondary",
    "--newtab-background-color",
    "--newtab-button-active-background",
    "--newtab-button-background",
    "--newtab-button-focus-background",
    "--newtab-button-hover-background",
    "--newtab-button-secondary-color",
    "--newtab-button-static-active-background",
    "--newtab-button-static-background",
    "--newtab-button-static-focus-background",
    "--newtab-button-static-hover-background",
    "--newtab-element-active-color",
    "--newtab-element-hover-color",
    "--newtab-element-secondary-active-color",
    "--newtab-element-secondary-color",
    "--newtab-element-secondary-hover-color",
    "--newtab-overlay-color",
    "--newtab-primary-action-background-pocket",
    "--newtab-primary-action-background",
    "--newtab-primary-element-active-color",
    "--newtab-primary-element-hover-color",
    "--newtab-primary-element-hover-pocket-color",
    "--newtab-status-error",
    "--newtab-text-primary-color",
    "--newtab-text-secondary-color",
    "--newtab-weather-background-color",
    "--sidebar-background-color",
    "--sidebar-box-background",
    "--tab-group-color-invert",
    "--tab-group-color-pale",
    "--tab-group-color",
    "--tab-loading-fill",
    "--tabgroup-swatch-color-invert",
    "--tabgroup-swatch-color",
    "--toolbar-background-color",
    "--toolbarbutton-background-color-active",
    "--toolbarbutton-background-color-hover",
    "--toolbox-background-color-inactive",
    "--toolbox-background-color",
    "--urlbar-box-background-color",
    "--urlbar-box-background-color-focus",
    "--urlbar-box-background-color-hover",
    "--urlbar-box-background-color-active",
    "--urlbarview-background-color-hover",
    "--urlbarview-background-color-selected",
    "--urlbarView-result-button-hover-background-color",
    "--urlbarView-result-button-selected-background-color",
    ...versatileColorTokens,
  ],
  tokenTypes: ["background-color"],
  aliasTokenTypes: ["color", "text-color", "border-color", "icon-color"],
  customFixes: customColorFixes,
  customSuggestions: systemColorSuggestions,
  warnSystemColors: true,
};

/** @type {PropertyTypeConfig} */
const BackgroundAttachment = {
  allow: ["scroll", "fixed", "local"],
};

/** @type {PropertyTypeConfig} */
const BackgroundImage = {
  allow: ["none"],
  allowFunctions: [
    "url",
    "linear-gradient",
    "radial-gradient",
    "conic-gradient",
    "repeating-linear-gradient",
    "repeating-radial-gradient",
    "repeating-conic-gradient",
    "image-set",
  ],
};

/** @type {PropertyTypeConfig} */
const BackgroundPosition = {
  allow: ["0", "top", "bottom", "left", "right", "center"],
  tokenTypes: ["size", "space"],
  aliasTokenTypes: ["dimension"],
  allowUnits: true,
};

/** @type {PropertyTypeConfig} */
const BackgroundSize = {
  allow: ["auto", "cover", "contain"],
  allowFunctions: ["max"],
  tokenTypes: ["size", "space", "icon-size"],
  aliasTokenTypes: ["dimension"],
  allowUnits: true,
};

/** @type {PropertyTypeConfig} */
const BackgroundRepeat = {
  allow: ["repeat", "repeat-x", "repeat-y", "no-repeat", "space", "round"],
};

/** @type {PropertyTypeConfig} */
const BackgroundClip = {
  allow: ["border-box", "padding-box", "content-box"],
};

/** @type {PropertyTypeConfig} */
const BoxShadow = {
  allow: ["none"],
  tokenTypes: ["box-shadow"],
};

/** @type {PropertyTypeConfig} */
const Fill = {
  allow: [
    "none",
    "context-fill",
    "context-stroke",
    "currentColor",
    "transparent",
    "white",
    "black",
  ],
  allowedTokens: [...versatileColorTokens],
  allowFunctions: ["url"],
  tokenTypes: ["icon-color"],
  aliasTokenTypes: [
    "color",
    "background-color",
    "border-color",
    "text-color",
    "outline",
  ],
  customFixes: customColorFixes,
};

/** @type {PropertyTypeConfig} */
const FontSize = {
  allow: [
    "xx-small",
    "x-small",
    "small",
    "medium",
    "large",
    "x-large",
    "xx-large",
    "xxx-large",
    "smaller",
    "larger",
    "1em",
  ],
  tokenTypes: ["font-size"],
};

/** @type {PropertyTypeConfig} */
const FontWeight = {
  allow: ["normal"],
  tokenTypes: ["font-weight"],
  customFixes: {
    ...createRawValuesObject(["font-weight"]),
    200: "normal",
    300: "normal",
    400: "normal",
    lighter: "normal",
    500: "var(--font-weight-semibold)",
    510: "var(--font-weight-semibold)",
    800: "var(--font-weight-bold)",
    bold: "var(--font-weight-bold)",
    bolder: "var(--font-weight-bold)",
  },
};

/** @type {PropertyTypeConfig} */
const BorderColor = {
  allow: [
    "transparent",
    "currentColor",
    "white",
    "black",
    "auto",
    "normal",
    "none",
    "0",
  ],
  allowAlias: [...SYSTEM_COLORS],
  allowedTokens: [...versatileColorTokens],
  tokenTypes: ["border-color", "border", "outline-color", "outline"],
  aliasTokenTypes: ["color", "background-color", "text-color"],
  customFixes: customColorFixes,
  customSuggestions: systemColorSuggestions,
  warnSystemColors: true,
};

/** @type {PropertyTypeConfig} */
const BorderStyle = {
  allow: [
    "solid",
    "dashed",
    "dotted",
    "double",
    "groove",
    "ridge",
    "inset",
    "outset",
    "none",
    "hidden",
  ],
};

/** @type {PropertyTypeConfig} */
const BorderWidth = {
  allow: ["0"],
  tokenTypes: ["border-width", "outline-width", "outline"],
  allowUnits: true,
};

/** @type {PropertyTypeConfig} */
const BorderRadius = {
  allow: ["0"],
  tokenTypes: ["border-radius"],
  customFixes: {
    ...createRawValuesObject(["border-radius"]),
    "50%": "var(--border-radius-circle)",
    "100%": "var(--border-radius-circle)",
    "1000px": "var(--border-radius-circle)",
  },
};

/** @type {PropertyTypeConfig} */
const FlexBasis = {
  allow: ["auto", "fit-content", "min-content", "max-content"],
  allowUnits: true,
  allowedUnits: ["%"],
  tokenTypes: ["size", "icon-size"],
};

/** @type {PropertyTypeConfig} */
const FlexShorthand = {
  allow: [
    "none",
    "0",
    "1",
    "2",
    "3",
    "4",
    "100",
    "1000",
    "10000",
    ...FlexBasis.allow,
  ],
  allowUnits: true,
  allowedUnits: ["%"],
  tokenTypes: ["size", "icon-size"],
};

const Opacity = {
  allow: ["0", "1"],
  tokenTypes: ["opacity"],
};

/** @type {PropertyTypeConfig} */
const TextColor = {
  allow: ["currentColor", "white", "black"],
  allowAlias: [...SYSTEM_COLORS],
  allowedTokens: [...versatileColorTokens, "--toolbarseparator-color"],
  tokenTypes: ["text-color", "icon-color"],
  aliasTokenTypes: ["color", "background-color", "border-color"],
  customFixes: customColorFixes,
  customSuggestions: systemColorSuggestions,
  warnSystemColors: true,
};

/** @type {PropertyTypeConfig} */
const Space = {
  allow: ["-1px", "0", "1px", "auto"],
  tokenTypes: ["space"],
  aliasTokenTypes: ["dimension"],
  allowUnits: true,
  allowedUnits: ["ch", "em", "lh"],
  customFixes: {
    "2px": "var(--space-xxsmall)",
    "4px": "var(--space-xsmall)",
    "8px": "var(--space-small)",
    "12px": "var(--space-medium)",
    "16px": "var(--space-large)",
    "24px": "var(--space-xlarge)",
    "32px": "var(--space-xxlarge)",
  },
};

/** @type {PropertyTypeConfig} */
const Size = {
  allow: [
    "0",
    "1px",
    "auto",
    "none",
    "fit-content",
    "min-content",
    "max-content",
    "stretch",
    "-moz-available",
  ],
  allowFunctions: ["max"],
  tokenTypes: ["size", "icon-size"],
  aliasTokenTypes: ["dimension"],
  allowUnits: true,
  allowedUnits: ["%", "ch", "em", "vh", "vw"],
  customFixes: {
    ...createRawValuesObject(["size", "icon-size"]),
    "0.75rem": "var(--size-item-xsmall)",
    "12px": "var(--size-item-xsmall)",
    "1rem": "var(--size-item-small)",
    "16px": "var(--size-item-small)",
    "1.5rem": "var(--size-item-medium)",
    "24px": "var(--size-item-medium)",
    "2rem": "var(--size-item-large)",
    "32px": "var(--size-item-large)",
    "3rem": "var(--size-item-xlarge)",
    "48px": "var(--size-item-xlarge)",
  },
};

/** @type {PropertyTypeConfig} */
const Stroke = {
  allow: [
    "none",
    "context-stroke",
    "currentColor",
    "transparent",
    "white",
    "black",
  ],
  allowFunctions: ["url"],
  allowedTokens: [...versatileColorTokens],
  tokenTypes: ["icon-color"],
  aliasTokenTypes: [
    "color",
    "background-color",
    "border-color",
    "text-color",
    "outline",
  ],
  customFixes: customColorFixes,
};

/**
 * @typedef {object} PropertyConfig
 * @property {PropertyTypeConfig[]} validTypes Valid type configurations for this property, ordered by precedence (first item is highest precedence)
 * @property {boolean} [shorthand] Whether this property accepts multiple space-separated values
 * @property {boolean} [multiple] Whether this property accepts comma-separated value groups
 * @property {boolean} [slash] Whether this property accepts slash-separated values (e.g., position/size)
 */

/** @type {Record<string, PropertyConfig>} */
export const propertyConfig = {
  "background-color": {
    validTypes: [BackgroundColor],
  },
  background: {
    validTypes: [
      BackgroundColor,
      BackgroundClip,
      BackgroundAttachment,
      BackgroundRepeat,
      BackgroundSize,
      BackgroundPosition,
      BackgroundImage,
    ],
    shorthand: true,
    multiple: true,
    slash: true,
  },
  "background-position": {
    validTypes: [BackgroundPosition],
    shorthand: true,
    multiple: true,
  },
  "background-position-x": {
    validTypes: [BackgroundPosition],
    shorthand: true,
    multiple: true,
  },
  "background-position-y": {
    validTypes: [BackgroundPosition],
    shorthand: true,
    multiple: true,
  },
  "background-size": {
    validTypes: [BackgroundSize],
    shorthand: true,
  },
  "box-shadow": {
    validTypes: [BoxShadow],
    multiple: true,
  },
  border: {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-width": {
    validTypes: [BorderWidth],
  },
  "border-color": {
    validTypes: [BorderColor],
  },
  "border-block": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-block-width": {
    validTypes: [BorderWidth],
  },
  "border-block-color": {
    validTypes: [BorderColor],
  },
  "border-block-end": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-block-end-width": {
    validTypes: [BorderWidth],
  },
  "border-block-end-color": {
    validTypes: [BorderColor],
  },
  "border-block-start": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-block-start-width": {
    validTypes: [BorderWidth],
  },
  "border-block-start-color": {
    validTypes: [BorderColor],
  },
  "border-bottom": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-bottom-width": {
    validTypes: [BorderWidth],
  },
  "border-bottom-color": {
    validTypes: [BorderColor],
  },
  "border-inline": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-inline-width": {
    validTypes: [BorderWidth],
  },
  "border-inline-color": {
    validTypes: [BorderColor],
  },
  "border-inline-end": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-inline-end-width": {
    validTypes: [BorderWidth],
  },
  "border-inline-end-color": {
    validTypes: [BorderColor],
  },
  "border-inline-start": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-inline-start-width": {
    validTypes: [BorderWidth],
  },
  "border-inline-start-color": {
    validTypes: [BorderColor],
  },
  "border-left": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-left-width": {
    validTypes: [BorderWidth],
  },
  "border-left-color": {
    validTypes: [BorderColor],
  },
  "border-right": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-right-width": {
    validTypes: [BorderWidth],
  },
  "border-right-color": {
    validTypes: [BorderColor],
  },
  "border-top": {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "border-top-width": {
    validTypes: [BorderWidth],
  },
  "border-top-color": {
    validTypes: [BorderColor],
  },
  outline: {
    validTypes: [BorderColor, BorderStyle, BorderWidth],
    shorthand: true,
  },
  "outline-width": {
    validTypes: [BorderWidth],
  },
  "outline-color": {
    validTypes: [BorderColor],
  },
  "border-radius": {
    validTypes: [BorderRadius],
    shorthand: true,
  },
  "border-top-left-radius": {
    validTypes: [BorderRadius],
  },
  "border-top-right-radius": {
    validTypes: [BorderRadius],
  },
  "border-bottom-right-radius": {
    validTypes: [BorderRadius],
  },
  "border-bottom-left-radius": {
    validTypes: [BorderRadius],
  },
  "border-start-start-radius": {
    validTypes: [BorderRadius],
  },
  "border-start-end-radius": {
    validTypes: [BorderRadius],
  },
  "border-end-start-radius": {
    validTypes: [BorderRadius],
  },
  "border-end-end-radius": {
    validTypes: [BorderRadius],
  },
  "border-spacing": {
    validTypes: [Space],
    shorthand: true,
  },
  color: {
    validTypes: [TextColor],
  },
  fill: {
    validTypes: [Fill],
    shorthand: true,
  },
  flex: {
    validTypes: [FlexShorthand],
    shorthand: true,
  },
  "flex-basis": {
    validTypes: [FlexBasis],
  },
  "font-size": {
    validTypes: [FontSize],
  },
  "font-weight": {
    validTypes: [FontWeight],
  },
  margin: {
    validTypes: [Space],
    shorthand: true,
  },
  "margin-block": {
    validTypes: [Space],
    shorthand: true,
  },
  "margin-block-end": {
    validTypes: [Space],
  },
  "margin-block-start": {
    validTypes: [Space],
  },
  "margin-inline": {
    validTypes: [Space],
    shorthand: true,
  },
  "margin-inline-end": {
    validTypes: [Space],
  },
  "margin-inline-start": {
    validTypes: [Space],
  },
  "margin-top": {
    validTypes: [Space],
  },
  "margin-right": {
    validTypes: [Space],
  },
  "margin-bottom": {
    validTypes: [Space],
  },
  "margin-left": {
    validTypes: [Space],
  },
  padding: {
    validTypes: [Space],
    shorthand: true,
  },
  "padding-block": {
    validTypes: [Space],
    shorthand: true,
  },
  "padding-block-end": {
    validTypes: [Space],
  },
  "padding-block-start": {
    validTypes: [Space],
  },
  "padding-inline": {
    validTypes: [Space],
    shorthand: true,
  },
  "padding-inline-end": {
    validTypes: [Space],
  },
  "padding-inline-start": {
    validTypes: [Space],
  },
  "padding-top": {
    validTypes: [Space],
  },
  "padding-right": {
    validTypes: [Space],
  },
  "padding-bottom": {
    validTypes: [Space],
  },
  "padding-left": {
    validTypes: [Space],
  },
  gap: {
    validTypes: [Space],
    shorthand: true,
  },
  "grid-gap": {
    validTypes: [Space],
    shorthand: true,
  },
  "column-gap": {
    validTypes: [Space],
  },
  "row-gap": {
    validTypes: [Space],
  },
  "grid-column-gap": {
    validTypes: [Space],
  },
  "grid-row-gap": {
    validTypes: [Space],
  },
  width: {
    validTypes: [Size],
  },
  "min-width": {
    validTypes: [Size],
  },
  "max-width": {
    validTypes: [Size],
  },
  height: {
    validTypes: [Size],
  },
  "min-height": {
    validTypes: [Size],
  },
  "max-height": {
    validTypes: [Size],
  },
  "inline-size": {
    validTypes: [Size],
  },
  "min-inline-size": {
    validTypes: [Size],
  },
  "max-inline-size": {
    validTypes: [Size],
  },
  "block-size": {
    validTypes: [Size],
  },
  "min-block-size": {
    validTypes: [Size],
  },
  "max-block-size": {
    validTypes: [Size],
  },
  stroke: {
    validTypes: [Stroke],
    shorthand: true,
  },
  inset: {
    validTypes: [Space, Size],
    shorthand: true,
  },
  "inset-block": {
    validTypes: [Space, Size],
    shorthand: true,
  },
  "inset-block-end": {
    validTypes: [Space, Size],
  },
  "inset-block-start": {
    validTypes: [Space, Size],
  },
  "inset-inline": {
    validTypes: [Space, Size],
    shorthand: true,
  },
  "inset-inline-end": {
    validTypes: [Space, Size],
  },
  "inset-inline-start": {
    validTypes: [Space, Size],
  },
  left: {
    validTypes: [Space, Size],
  },
  right: {
    validTypes: [Space, Size],
  },
  top: {
    validTypes: [Space, Size],
  },
  bottom: {
    validTypes: [Space, Size],
  },
  "scroll-margin": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-margin-block": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-margin-block-end": {
    validTypes: [Space],
  },
  "scroll-margin-block-start": {
    validTypes: [Space],
  },
  "scroll-margin-bottom": {
    validTypes: [Space],
  },
  "scroll-margin-inline": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-margin-inline-end": {
    validTypes: [Space],
  },
  "scroll-margin-inline-start": {
    validTypes: [Space],
  },
  "scroll-margin-left": {
    validTypes: [Space],
  },
  "scroll-margin-right": {
    validTypes: [Space],
  },
  "scroll-margin-top": {
    validTypes: [Space],
  },
  "scroll-padding": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-padding-block": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-padding-block-end": {
    validTypes: [Space],
  },
  "scroll-padding-block-start": {
    validTypes: [Space],
  },
  "scroll-padding-bottom": {
    validTypes: [Space],
  },
  "scroll-padding-inline": {
    validTypes: [Space],
    shorthand: true,
  },
  "scroll-padding-inline-end": {
    validTypes: [Space],
  },
  "scroll-padding-inline-start": {
    validTypes: [Space],
  },
  "scroll-padding-left": {
    validTypes: [Space],
  },
  "scroll-padding-right": {
    validTypes: [Space],
  },
  "scroll-padding-top": {
    validTypes: [Space],
  },
  opacity: {
    validTypes: [Opacity],
  },
};
