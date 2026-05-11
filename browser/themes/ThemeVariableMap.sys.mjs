/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const ThemeVariableMap = [
  [
    "--lwt-accent-color-inactive",
    {
      lwtProperty: "accentcolorInactive",
    },
  ],
  [
    "--lwt-background-alignment",
    {
      isColor: false,
      lwtProperty: "backgroundsAlignment",
    },
  ],
  [
    "--lwt-background-tiling",
    {
      isColor: false,
      lwtProperty: "backgroundsTiling",
    },
  ],
  [
    "--tab-loading-fill",
    {
      lwtProperty: "tab_loading",
    },
  ],
  [
    "--tab-background-color-selected",
    {
      lwtProperty: "tab_selected",
    },
  ],
  [
    "--tab-selected-textcolor",
    {
      lwtProperty: "tab_text",
    },
  ],
  [
    "--lwt-tab-line-color",
    {
      lwtProperty: "tab_line",
    },
  ],
  [
    "--lwt-background-tab-separator-color",
    {
      lwtProperty: "tab_background_separator",
    },
  ],
  [
    "--tabs-navbar-separator-color",
    {
      lwtProperty: "toolbar_top_separator",
    },
  ],
  [
    "--tabs-navbar-separator-style",
    {
      lwtProperty: "toolbar_top_separator",
      processColor(rgbaChannels) {
        // If the separator is transparent, we don't want it to take space.
        return rgbaChannels?.a === 0 ? "none" : null;
      },
    },
  ],
  [
    "--toolbarseparator-color",
    {
      lwtProperty: "toolbar_vertical_separator",
    },
  ],
  [
    "--chrome-content-separator-color",
    {
      lwtProperty: "toolbar_bottom_separator",
    },
  ],
  [
    "--toolbarbutton-background-color-hover",
    {
      lwtProperty: "button_background_hover",
    },
  ],
  [
    "--toolbarbutton-background-color-active",
    {
      lwtProperty: "button_background_active",
    },
  ],
  [
    "--urlbarview-background-color-selected",
    {
      lwtProperty: "popup_highlight",
    },
  ],
  [
    "--urlbarview-text-color-selected",
    {
      lwtProperty: "popup_highlight_text",
    },
  ],
  [
    "--sidebar-background-color",
    {
      lwtProperty: "sidebar",
      processColor(rgbaChannels) {
        if (!rgbaChannels) {
          return null;
        }
        const { r, g, b } = rgbaChannels;
        // Drop alpha channel
        return `rgb(${r}, ${g}, ${b})`;
      },
    },
  ],
  [
    "--sidebar-text-color",
    {
      lwtProperty: "sidebar_text",
    },
  ],
  [
    "--sidebar-border-color",
    {
      lwtProperty: "sidebar_border",
    },
  ],
  [
    "--tabpanel-background-color",
    {
      lwtProperty: "ntp_background",
      processColor(rgbaChannels) {
        if (
          !rgbaChannels ||
          !Services.prefs.getBoolPref("browser.newtabpage.enabled")
        ) {
          // We only set the tabpanel background to the new tab background color
          // if the user uses about:home for new tabs. Otherwise, we flash a
          // colorful background when a new tab is opened. We will flash the
          // newtab color in new windows if the user uses about:home for new
          // tabs but not new windows. However, the flash is concealed by the OS
          // window-open animation.
          return null;
        }
        // Drop alpha channel
        let { r, g, b } = rgbaChannels;
        return `rgb(${r}, ${g}, ${b})`;
      },
    },
  ],
];

export const ThemeContentPropertyList = [
  "ntp_background",
  "ntp_card_background",
  "ntp_text",
  "sidebar",
  "sidebar_highlight",
  "sidebar_highlight_text",
  "sidebar_text",
  "zap_gradient",
  "ai_background_color",
];
