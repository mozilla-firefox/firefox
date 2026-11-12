/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

export const IS_NEWTAB =
  globalThis.document && globalThis.document.documentURI === "about:newtab";
export const NEWTAB_DARK_THEME = {
  ntp_background: {
    r: 42,
    g: 42,
    b: 46,
    a: 1,
  },
  ntp_card_background: {
    r: 66,
    g: 65,
    b: 77,
    a: 1,
  },
  ntp_text: {
    r: 249,
    g: 249,
    b: 250,
    a: 1,
  },
  sidebar: {
    r: 56,
    g: 56,
    b: 61,
    a: 1,
  },
  sidebar_text: {
    r: 249,
    g: 249,
    b: 250,
    a: 1,
  },
};

// Wallpaper category ids. These must match the `category` values the
// wallpaper feed provides; prefer this map over bare category strings.
export const WALLPAPER_CATEGORIES = {
  Abstracts: "abstracts",
  Celestial: "celestial",
  Photographs: "photographs",
  SolidColors: "solid-colors",
  Firefox: "firefox",
  CustomWallpaper: "custom-wallpaper",
};

// Ids of the customize panel's subpanels. New Tab telemetry records them as
// its `panel` value (Bug 2066344), so keep them stable.
export const CUSTOMIZE_SUBPANELS = Object.freeze({
  SECTIONS: "sections_management",
  WIDGETS: "widgets_management",
  THEMES: "themes_management",
  WALLPAPERS: "wallpaper_categories",
});

// What opened the customize panel. New Tab telemetry records these as the
// `source` value of `customize_panel_open` (Bug 2066847), so keep them stable.
export const CUSTOMIZE_PANEL_SOURCES = Object.freeze({
  CUSTOMIZE_BUTTON: "customize_button",
  MANAGE_WIDGETS: "manage_widgets",
  WALLPAPER_FEATURE_HIGHLIGHT: "wallpaper_feature_highlight",
  PROMO_CARD: "promo_card",
  PERSONALIZED_CARD: "personalized_card",
  ACTIVATION_WINDOW_MESSAGE: "activation_window_message",
  TOP_SITES_WEB_NOTIFICATIONS: "top_sites_web_notifications",
  SETTINGS: "settings",
  EXTERNAL_LINK: "external_link",
});
