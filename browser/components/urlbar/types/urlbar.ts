/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// These types are commonly passed as parameters with the Urlbar code. We
// define them here to avoid having to `@import` them into each module.
// TypeScript will still warn about attempting to call
// `new UrlbarParentController()` and similar actions because these are only
// defined as types and not values.

type UrlbarChildController =
  import("../content/UrlbarChildController.mjs").UrlbarChildController;
type UrlbarParentController =
  import("../UrlbarParentController.sys.mjs").UrlbarParentController;
type UrlbarInput = import("../content/UrlbarInput.mjs").UrlbarInput;
type UrlbarQueryContext =
  import("../content/UrlbarQueryContext.mjs").UrlbarQueryContext;
type UrlbarResult = import("../content/UrlbarResult.mjs").UrlbarResult;

/**
 * The search access point name of a UrlbarInput.
 * It is used to check for capabilities, for logging and for telemetry.
 *
 * In search telemetry, it is related to the `source` extra key whose possible
 * values are a superset of UrlbarSapName, see `KNOWN_SEARCH_SOURCES`.
 *
 * In urlbar telemetry, it is related to the `sap` extra key whose possible
 * values are a different superset of UrlbarSapName, @see UrlbarTelemetrySapName.
 */
type UrlbarSapName = "newtab_searchbar" | "searchbar" | "smartbar" | "urlbar";

/**
 * Extension of UrlbarSapName to be used for urlbar.* telemetry.
 * For search telemetry, see BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES.
 */
type UrlbarTelemetrySapName =
  | UrlbarSapName
  | "handoff"
  | "urlbar_newtab"
  | "urlbar_addonpage";

/**
 * A structure that holds the details of commands for results.
 */
type UrlbarResultCommand = {
  /**
   * The name of the command. When a command is picked, its name will be passed
   * as `details.selType` to `onEngagement()`. The special name "separator" will
   * create a menu separator.
   */
  name?: string;
  /**
   * An l10n object for the command's label. Must be specified unless `name`
   * is "separator".
   */
  l10n?: { id: string; args?: L10nArgs };
  /**
   * The menu item's type. "checkbox" renders a checkmark whose state `checked`
   * supplies.
   */
  type?: "checkbox";
  /**
   * Whether a "checkbox" command's menu item starts checked.
   */
  checked?: boolean;
  /**
   * Where the command opens the result, for the view's own commands. Passed to
   * `pickResult()` in place of a `name`, so that these picks are recorded as
   * ordinary result picks.
   */
  openIn?: "tab" | "container-tab" | "window" | "private-window";
  /**
   * Whether the command's menu item holds a submenu, populated when it's about
   * to be shown.
   */
  submenu?: boolean;
};

/**
 * Convert every method of an object to return void.
 */
type VoidMethods<T extends Record<keyof T, (...args: any) => any>> = {
  [F in keyof T]: (...args: Parameters<T[F]>) => void;
};
