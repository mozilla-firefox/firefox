/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const asyncStoreHelper = require("resource://devtools/client/shared/async-store-helper.js");
const { validateBreakpointLocation } = ChromeUtils.importESModule(
  "resource://devtools/shared/validate-breakpoint.sys.mjs",
  { global: "contextual" }
);

const asyncStore = asyncStoreHelper("debugger", {
  pendingBreakpoints: ["pending-breakpoints", {}],
  tabs: ["tabs", []],
  xhrBreakpoints: ["xhr-breakpoints", []],
  eventListenerBreakpoints: ["event-listener-breakpoints", undefined],
  blackboxedRanges: ["blackboxedRanges", {}],
  directoryRoots: ["directory-roots", {}],
});
exports.asyncStore = asyncStore;

exports.getThreadOptions = async function () {
  return {
    shouldPauseOnDebuggerStatement: Services.prefs.getBoolPref(
      "devtools.debugger.pause-on-debugger-statement"
    ),
    pauseOnExceptions: Services.prefs.getBoolPref(
      "devtools.debugger.pause-on-exceptions"
    ),
    ignoreCaughtExceptions: Services.prefs.getBoolPref(
      "devtools.debugger.ignore-caught-exceptions"
    ),
    shouldIncludeSavedFrames: Services.prefs.getBoolPref(
      "devtools.debugger.features.async-captured-stacks"
    ),
    shouldIncludeAsyncLiveFrames: Services.prefs.getBoolPref(
      "devtools.debugger.features.async-live-stacks"
    ),
    skipBreakpoints: Services.prefs.getBoolPref(
      "devtools.debugger.skip-pausing"
    ),
    logEventBreakpoints: Services.prefs.getBoolPref(
      "devtools.debugger.log-event-breakpoints"
    ),
    // This option is always true. See Bug 1654590 for removal.
    observeAsmJS: true,
    breakpoints: sanitizeBreakpoints(await asyncStore.pendingBreakpoints),
    // XXX: `event-listener-breakpoints` is a copy of the event-listeners state
    // of the debugger panel. The `active` property is therefore linked to
    // the `active` property of the state.
    // See devtools/client/debugger/src/reducers/event-listeners.js
    eventBreakpoints:
      ((await asyncStore.eventListenerBreakpoints) || {}).active || [],
  };
};

/**
 * Bug 1720512 - We used to store invalid breakpoints, leading to blank debugger.
 * Filter out only the one that look invalid.
 */
function sanitizeBreakpoints(breakpoints) {
  if (typeof breakpoints != "object") {
    return {};
  }
  // We are not doing any assertion against keys,
  // as it looks like we are never using them anywhere in frontend, nor backend.
  const validBreakpoints = {};
  for (const key in breakpoints) {
    const bp = breakpoints[key];
    try {
      if (!bp) {
        throw new Error("Undefined breakpoint");
      }
      // Debugger's main.js's `syncBreakpoints` will only use generatedLocation
      // when restoring breakpoints.
      validateBreakpointLocation(bp.generatedLocation);
      // But Toolbox will still pass location to thread actor's reconfigure
      // for target that don't support watcher+BreakpointListActor
      validateBreakpointLocation(bp.location);
      validBreakpoints[key] = bp;
    } catch (e) {
      console.error(
        "Ignore invalid breakpoint from debugger store",
        bp,
        e.message
      );
    }
  }
  return validBreakpoints;
}
exports.sanitizeBreakpoints = sanitizeBreakpoints;
