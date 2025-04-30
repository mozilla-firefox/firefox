/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A Map of themes built in to the browser, alongwith a Map of collections those themes belong to. Params for the objects contained
 * within the map:
 *
 * @param {string} id
 *   The unique identifier for the theme. The map's key.
 * @param {string} version
 *   The theme add-on's semantic version, as defined in its manifest.
 * @param {string} path
 *   Path to the add-on files.
 * @param {string} [expiry]
 *  Date in YYYY-MM-DD format. Optional. If defined, the themes in the collection can no longer be
 *  used after this date, unless the user has permission to retain it.
 * @param {string} [collection]
 *  The collection id that the theme is a part of. Optional.
 */
export const BuiltInThemeConfig = new Map([
  [
    "firefox-compact-light@mozilla.org",
    {
      version: "1.3.2",
      path: "resource://builtin-themes/light/",
    },
  ],
  [
    "firefox-compact-dark@mozilla.org",
    {
      version: "1.3.3",
      path: "resource://builtin-themes/dark/",
    },
  ],
  [
    "firefox-alpenglow@mozilla.org",
    {
      version: "1.5",
      path: "resource://builtin-themes/alpenglow/",
    },
  ],
]);
