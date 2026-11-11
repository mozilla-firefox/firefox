/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Session state shapes are documented as JSDoc typedefs in the modules that
// own them. Aliasing them here makes them available to every module in the
// project without a per-module `@import`.

type WindowID = import("../SessionStore.sys.mjs").WindowID;
type WindowStateData = import("../SessionStore.sys.mjs").WindowStateData;

type TabStateData = import("../TabState.sys.mjs").TabStateData;
type ClosedTabStateData = import("../TabState.sys.mjs").ClosedTabStateData;

type TabGroupId = import("../TabGroupState.sys.mjs").TabGroupId;
type TabGroupStateData = import("../TabGroupState.sys.mjs").TabGroupStateData;
type ClosedTabGroupStateData =
  import("../TabGroupState.sys.mjs").ClosedTabGroupStateData;
type SavedTabGroupStateData =
  import("../TabGroupState.sys.mjs").SavedTabGroupStateData;
type SavedGroupTabStateData =
  import("../TabGroupState.sys.mjs").SavedGroupTabStateData;

type SessionWriteStep = import("../SessionWriter.sys.mjs").SessionWriteStep;

type SessionStoreLogger =
  typeof import("../SessionLogger.sys.mjs").sessionStoreLogger;
type TabMetricsContext =
  import("../../tabbrowser/TabMetrics.sys.mjs").TabMetricsContext;
