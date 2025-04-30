/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

const { PrefsHelper } = require("resource://devtools/client/shared/prefs.js");

import { isNode } from "./environment";

// Schema version to bump when the async store format has changed incompatibly
// and old stores should be cleared.
const prefsSchemaVersion = 11;
const { pref } = Services;

if (isNode()) {
  pref("devtools.debugger.logging", false);
  pref("devtools.debugger.alphabetize-outline", false);
  pref("devtools.debugger.auto-pretty-print", false);
  pref("devtools.source-map.client-service.enabled", true);
  pref("devtools.chrome.enabled", false);
  pref("devtools.debugger.pause-on-debugger-statement", true);
  pref("devtools.debugger.pause-on-exceptions", false);
  pref("devtools.debugger.pause-on-caught-exceptions", false);
  pref("devtools.debugger.ignore-caught-exceptions", true);
  pref("devtools.debugger.call-stack-visible", true);
  pref("devtools.debugger.scopes-visible", true);
  pref("devtools.debugger.threads-visible", true);
  pref("devtools.debugger.expressions-visible", false);
  pref("devtools.debugger.xhr-breakpoints-visible", false);
  pref("devtools.debugger.breakpoints-visible", true);
  pref("devtools.debugger.event-listeners-visible", false);
  pref("devtools.debugger.dom-mutation-breakpoints-visible", false);
  pref("devtools.debugger.start-panel-collapsed", false);
  pref("devtools.debugger.end-panel-collapsed", false);
  pref("devtools.debugger.start-panel-size", 300);
  pref("devtools.debugger.end-panel-size", 300);
  pref("devtools.debugger.ui.editor-wrapping", false);
  pref("devtools.debugger.ui.framework-grouping-on", true);
  pref("devtools.debugger.pending-selected-location", "{}");
  pref("devtools.debugger.expressions", "[]");
  pref("devtools.debugger.search-options", "{}");
  pref("devtools.debugger.project-directory-root", "");
  pref("devtools.debugger.map-scopes-enabled", false);
  pref("devtools.debugger.prefs-schema-version", prefsSchemaVersion);
  pref("devtools.debugger.skip-pausing", false);
  pref("devtools.debugger.log-actions", true);
  pref("devtools.debugger.log-event-breakpoints", false);
  pref("devtools.debugger.javascript-tracing-log-method", "debugger-sidebar");
  pref("devtools.debugger.javascript-tracing-values", false);
  pref("devtools.debugger.javascript-tracing-on-next-interaction", false);
  pref("devtools.debugger.javascript-tracing-on-next-load", false);
  pref("devtools.debugger.javascript-tracing-function-return", false);
  pref("devtools.debugger.show-content-scripts", false);
  pref("devtools.debugger.hide-ignored-sources", false);
  pref("devtools.debugger.source-map-ignore-list-enabled", true);
  pref("devtools.debugger.features.wasm", true);
  pref("devtools.debugger.features.code-folding", false);
  pref("devtools.debugger.features.autocomplete-expressions", false);
  pref("devtools.debugger.features.map-expression-bindings", true);
  pref("devtools.debugger.features.map-await-expression", true);
  pref("devtools.debugger.features.log-points", true);
  pref("devtools.debugger.features.inline-preview", true);
  pref("devtools.debugger.features.javascript-tracing", false);
  pref("devtools.debugger.features.codemirror-next", true);
  pref("devtools.editor.tabsize", 2);
  pref("devtools.editor.expandtab", false);
  pref("devtools.editor.autoclosebrackets", false);
}

export const prefs = new PrefsHelper("devtools", {
  logging: ["Bool", "debugger.logging"],
  editorWrapping: ["Bool", "debugger.ui.editor-wrapping"],
  alphabetizeOutline: ["Bool", "debugger.alphabetize-outline"],
  autoPrettyPrint: ["Bool", "debugger.auto-pretty-print"],
  clientSourceMapsEnabled: ["Bool", "source-map.client-service.enabled"],
  chromeAndExtensionsEnabled: ["Bool", "chrome.enabled"],
  pauseOnDebuggerStatement: ["Bool", "debugger.pause-on-debugger-statement"],
  pauseOnExceptions: ["Bool", "debugger.pause-on-exceptions"],
  pauseOnCaughtExceptions: ["Bool", "debugger.pause-on-caught-exceptions"],
  ignoreCaughtExceptions: ["Bool", "debugger.ignore-caught-exceptions"],
  callStackVisible: ["Bool", "debugger.call-stack-visible"],
  scopesVisible: ["Bool", "debugger.scopes-visible"],
  threadsVisible: ["Bool", "debugger.threads-visible"],
  breakpointsVisible: ["Bool", "debugger.breakpoints-visible"],
  expressionsVisible: ["Bool", "debugger.expressions-visible"],
  xhrBreakpointsVisible: ["Bool", "debugger.xhr-breakpoints-visible"],
  eventListenersVisible: ["Bool", "debugger.event-listeners-visible"],
  domMutationBreakpointsVisible: [
    "Bool",
    "debugger.dom-mutation-breakpoints-visible",
  ],
  startPanelCollapsed: ["Bool", "debugger.start-panel-collapsed"],
  endPanelCollapsed: ["Bool", "debugger.end-panel-collapsed"],
  startPanelSize: ["Int", "debugger.start-panel-size"],
  endPanelSize: ["Int", "debugger.end-panel-size"],
  frameworkGroupingOn: ["Bool", "debugger.ui.framework-grouping-on"],
  pendingSelectedLocation: ["Json", "debugger.pending-selected-location", {}],
  expressions: ["Json", "debugger.expressions", []],
  searchOptions: ["Json", "debugger.search-options"],
  debuggerPrefsSchemaVersion: ["Int", "debugger.prefs-schema-version"],
  skipPausing: ["Bool", "debugger.skip-pausing"],
  mapScopes: ["Bool", "debugger.map-scopes-enabled"],
  logActions: ["Bool", "debugger.log-actions"],
  logEventBreakpoints: ["Bool", "debugger.log-event-breakpoints"],
  indentSize: ["Int", "editor.tabsize"],
  javascriptTracingLogMethod: [
    "String",
    "debugger.javascript-tracing-log-method",
  ],
  javascriptTracingValues: ["Bool", "debugger.javascript-tracing-values"],
  javascriptTracingOnNextInteraction: [
    "Bool",
    "debugger.javascript-tracing-on-next-interaction",
  ],
  javascriptTracingOnNextLoad: [
    "Bool",
    "debugger.javascript-tracing-on-next-load",
  ],
  javascriptTracingFunctionReturn: [
    "Bool",
    "debugger.javascript-tracing-function-return",
  ],
  showContentScripts: ["Bool", "debugger.show-content-scripts"],
  hideIgnoredSources: ["Bool", "debugger.hide-ignored-sources"],
  sourceMapIgnoreListEnabled: [
    "Bool",
    "debugger.source-map-ignore-list-enabled",
  ],
});

// The pref may not be defined. Defaulting to null isn't viable (cursor never blinks).
// Can't use CodeMirror.defaults here because it's loaded later.
// Hardcode the fallback value to that of CodeMirror.defaults.cursorBlinkRate.
prefs.cursorBlinkRate = Services.prefs.getIntPref("ui.caretBlinkTime", 530);

export const features = new PrefsHelper("devtools.debugger.features", {
  wasm: ["Bool", "wasm"],
  outline: ["Bool", "outline"],
  codeFolding: ["Bool", "code-folding"],
  autocompleteExpression: ["Bool", "autocomplete-expressions"],
  mapExpressionBindings: ["Bool", "map-expression-bindings"],
  mapAwaitExpression: ["Bool", "map-await-expression"],
  logPoints: ["Bool", "log-points"],
  inlinePreview: ["Bool", "inline-preview"],
  windowlessServiceWorkers: ["Bool", "windowless-service-workers"],
  javascriptTracing: ["Bool", "javascript-tracing"],
  codemirrorNext: ["Bool", "codemirror-next"],
});

// Import the asyncStore already spawned by the TargetMixin class
const ThreadUtils = require("resource://devtools/client/shared/thread-utils.js");
export const asyncStore = ThreadUtils.asyncStore;

export function resetSchemaVersion() {
  prefs.debuggerPrefsSchemaVersion = prefsSchemaVersion;
}

export function verifyPrefSchema() {
  if (prefs.debuggerPrefsSchemaVersion < prefsSchemaVersion) {
    asyncStore.pendingBreakpoints = {};
    asyncStore.tabs = [];
    asyncStore.xhrBreakpoints = [];
    asyncStore.eventListenerBreakpoints = undefined;
    asyncStore.blackboxedRanges = {};
    prefs.debuggerPrefsSchemaVersion = prefsSchemaVersion;
  }
}
