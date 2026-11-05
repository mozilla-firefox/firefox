/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Types the tabbrowser modules name in their JSDoc. Defining them here saves
// every module an `@import` of its own.

type TabMetricsContext = import("../TabMetrics.sys.mjs").TabMetricsContext;
type TabGroupId = import("../../sessionstore/TabGroupState.sys.mjs").TabGroupId;
type TabGroupStateData =
  import("../../sessionstore/TabGroupState.sys.mjs").TabGroupStateData;
type SavedTabGroupStateData =
  import("../../sessionstore/TabGroupState.sys.mjs").SavedTabGroupStateData;

// Sessionstore names TabStateData in its JSDoc without defining it anywhere.
type TabStateData = any;

// nsBrowserStatusFilter has no interface of its own to create it through.
type BrowserStatusFilter = nsIWebProgress & nsIWebProgressListener;
