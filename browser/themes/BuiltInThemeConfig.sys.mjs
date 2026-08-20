/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A Map of themes built in to the browser. Params for the objects contained
 * within the map:
 *
 * @param {string} id
 *   The unique identifier for the theme. The map's key.
 * @param {string} version
 *   The theme add-on's semantic version, as defined in its manifest.
 * @param {string} path
 *   Path to the add-on files.
 * @param {boolean} inApp
 *   Optional, whether the theme uses the app's CSS, just forcing it to a
 *   particular color scheme or variant.
 * @param {boolean} nonNative
 *   Whether this inApp theme should force the native theme, but with
 *   non-native appearance. See Document.forceNonNativeTheme and the
 *   (-moz-native-theme) media query.
 */
export const BuiltInThemeConfig = new Map([
  [
    "firefox-compact-light@mozilla.org",
    {
      version: "1.3.4",
      path: "resource://builtin-themes/light/",
      inApp: true,
    },
  ],
  [
    "firefox-compact-dark@mozilla.org",
    {
      version: "1.3.4",
      path: "resource://builtin-themes/dark/",
      inApp: true,
    },
  ],
  [
    "firefox-alpenglow@mozilla.org",
    {
      version: "1.5.3",
      path: "resource://builtin-themes/alpenglow/",
    },
  ],
]);
