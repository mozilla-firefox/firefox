/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { create } from "storybook/theming/create";

// CSS variables are not accepted values in the object passed into the
// create() method. The values used here are hard-coded and based on
// values from `toolkit/themes/shared/design-system/tokens-brand.css`,
// for the dark theme.
//
// Storybook config files do not recognize OKLCH colors, so the closest
// corresponding HEX value has been substituted.
//
// The corresponding CSS variable name has been commented, if it exists.
export default create({
  // Base theme, mandatory
  base: "dark",

  // Branding
  brandTitle: "Storybook for Firefox",

  // Colors
  colorPrimary: "#00cadb", // --color-accent-primary: oklch(76% 0.14 205)
  appBg: "#1c1b22", // --background-color-canvas
  appBorderColor: "#5b5b66", // --border-color
  appBorderRadius: 4,

  // Typography
  fontBase: "system-ui",
  fontCode: "monospace",
  textColor: "#fbfbfe", // ---text-color

  // Toolbar default and active colors
  barBg: "#23222b", // --table-row-background-color-alternate
  barTextColor: "#fbfbfe", // --button-text-color
  barHoverColor: "#61dce9", // --color-accent-primary-hover: oklch(83% 0.11 205)
  barSelectedColor: "#a6ecf4", // --color-accent-primary-active: oklch(90% 0.07 205)
});
