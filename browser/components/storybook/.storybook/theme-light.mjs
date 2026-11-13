/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { create } from "storybook/theming/create";

// CSS variables are not accepted values in the object passed into the
// create() method. The values used here are hard-coded and based on
// values from `toolkit/themes/shared/design-system/tokens-brand.css`,
// for the light theme.
//
// Storybook config files do not recognize OKLCH colors, so the closest
// corresponding HEX value has been substituted.
//
// The corresponding CSS variable name has been commented, if it exists.
export default create({
  // Base theme, mandatory
  base: "light",

  // Branding
  brandTitle: "Storybook for Firefox",

  // Colors
  colorPrimary: "#0062f9", // --color-accent-primary: oklch(55% 0.24 260)
  appBg: "#ffffff", // --background-color-canvas
  appBorderColor: "#bfbfc9", // --border-color
  appBorderRadius: 4,

  // Typography
  fontBase: "system-ui",
  fontCode: "monospace",
  textColor: "#15141a", // --text-color

  // Toolbar default and active colors
  barBg: "#f0f0f4", // --table-row-background-color-alternate
  barTextColor: "#15141a", // --button-text-color
  barHoverColor: "##006d79", // --color-accent-primary-hover: oklch(48% 0.2 205)
  barSelectedColor: "#005761", // --color-accent-primary-active: oklch(41% 0.17 205)
});
