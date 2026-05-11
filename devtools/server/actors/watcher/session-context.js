/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Module to create all the Session Context objects.
//
// These are static JSON serializable object that help describe
// the debugged context. It is passed around to most of the server codebase
// in order to know which object to consider inspecting and communicating back to the client.
//
// These objects are all instantiated by the Descriptor actors
// and passed as a constructor argument to the Watcher actor.
//
// These objects have attributes used by all the Session contexts:
// - type: String
//   Describes which type of context we are debugging.
//   See SESSION_TYPES for all possible values.
//   See each create* method for more info about each type and their specific attributes.
// - isServerTargetSwitchingEnabled: Boolean
//   If true, targets should all be spawned by the server codebase.
//   Especially the first, top level target.
// - supportedTargets: Boolean
//   An object keyed by target type, whose value indicates if we have watcher support
//   for the target.
// - supportedResources: Boolean
//   An object keyed by resource type, whose value indicates if we have watcher support
//   for the resource.
// - enableWindowGlobalThreadActors: Boolean
//   If false (the default), Resource Watchers will avoid spawning the thread actors of
//   WindowGlobal targets. In such configuration, the WindowGlobal scripts will be
//   debugged by the Content Process targets.
//   This is used by the Browser Toolbox, and explicitly not used by VS.Code
//   which reuse the Browser Toolbox codepath, but doesn't use the Content Process targets.

const Targets = require("resource://devtools/server/actors/targets/index.js");
const Resources = require("resource://devtools/server/actors/resources/index.js");

const SESSION_TYPES = {
  ALL: "all",
  BROWSER_ELEMENT: "browser-element",
  CONTENT_PROCESS: "content-process",
  WEBEXTENSION: "webextension",
  WORKER: "worker",
};

/**
 * Create the SessionContext used by the Browser Toolbox and Browser Console.
 *
 * This context means debugging everything.
 * The whole browser:
 * - all processes: parent and content,
 * - all privileges: privileged/chrome and content/web,
 * - all components/targets: HTML documents, processes, workers, add-ons,...
 *
 * @param {object} config
 *        An object with optional configuration. Only supports "enableWindowGlobalThreadActors" attribute.
 *        See jsdoc in this file header for more info.
 */
function createBrowserSessionContext({
  enableWindowGlobalThreadActors = false,
} = {}) {
  const type = SESSION_TYPES.ALL;

  return {
    type,
    // For now, the top level target (ParentProcessTargetActor) is created via ProcessDescriptor.getTarget
    // and is never replaced by any other, nor is it created by the WatcherActor.
    isServerTargetSwitchingEnabled: false,
    supportedTargets: getWatcherSupportedTargets(type),
    supportedResources: getWatcherSupportedResources(type),

    enableWindowGlobalThreadActors,
  };
}

/**
 * Create the SessionContext used by the regular web page toolboxes as well as remote debugging android device tabs.
 *
 * @param {BrowserElement} browserElement
 *        The tab to debug. It should be a reference to a <browser> element.
 * @param {object} config
 *        An object with optional configuration. Only supports "isServerTargetSwitchingEnabled" attribute.
 *        See jsdoc in this file header for more info.
 */
function createBrowserElementSessionContext(browserElement, config) {
  const type = SESSION_TYPES.BROWSER_ELEMENT;
  return {
    type,
    browserId: browserElement.browserId,
    // Nowaday, it should always be enabled except for WebExtension special
    // codepath and some tests.
    isServerTargetSwitchingEnabled: config.isServerTargetSwitchingEnabled,
    // Should we instantiate targets for popups opened in distinct tabs/windows?
    // Driven by devtools.popups.debug=true preference.
    isPopupDebuggingEnabled: config.isPopupDebuggingEnabled,
    supportedTargets: getWatcherSupportedTargets(type),
    supportedResources: getWatcherSupportedResources(type),
  };
}

/**
 * Create the SessionContext used by the web extension toolboxes.
 *
 * @param {object} addon
 *        First object argument to describe the add-on.
 * @param {string} addon.addonId
 *        The web extension ID, to uniquely identify the debugged add-on.
 * @param {object} config
 *        An object with optional configuration. Only supports "isServerTargetSwitchingEnabled" attribute.
 *        See jsdoc in this file header for more info.
 */
function createWebExtensionSessionContext({ addonId }, config) {
  const type = SESSION_TYPES.WEBEXTENSION;
  return {
    type,
    addonId,
    isServerTargetSwitchingEnabled: config.isServerTargetSwitchingEnabled,
    supportedTargets: getWatcherSupportedTargets(type),
    supportedResources: getWatcherSupportedResources(type),
  };
}

/**
 * Create the SessionContext used by the Browser Content Toolbox, to debug only one content process.
 * Or when debugging XpcShell via about:debugging, where we instantiate only one content process target.
 */
function createContentProcessSessionContext() {
  const type = SESSION_TYPES.CONTENT_PROCESS;
  return {
    type,
    supportedTargets: getWatcherSupportedTargets(type),
    supportedResources: getWatcherSupportedResources(type),
  };
}

/**
 * Create the SessionContext used when debugging one specific Service Worker or special chrome worker.
 * This is only used from about:debugging.
 */
function createWorkerSessionContext() {
  const type = SESSION_TYPES.WORKER;
  return {
    type,
    supportedTargets: getWatcherSupportedTargets(type),
    supportedResources: getWatcherSupportedResources(type),
  };
}

/**
 * Get the supported targets by the watcher given a session context type.
 *
 * @param {string} type
 * @returns {object}
 */
function getWatcherSupportedTargets(type) {
  return {
    [Targets.TYPES.FRAME]: true,
    [Targets.TYPES.PROCESS]: true,
    [Targets.TYPES.WORKER]: true,
    [Targets.TYPES.SERVICE_WORKER]:
      type == SESSION_TYPES.BROWSER_ELEMENT || type == SESSION_TYPES.ALL,

    // Bug 1607778 - Shared workers aren't yet exposed in tab toolboxes
    [Targets.TYPES.SHARED_WORKER]: type == SESSION_TYPES.ALL,

    // Content scripts may only be exposed to tab and browser toolboxes
    // (and not the extension toolboxes)
    [Targets.TYPES.CONTENT_SCRIPT]:
      type == SESSION_TYPES.BROWSER_ELEMENT || type == SESSION_TYPES.ALL,
  };
}

/**
 * Get the supported resources by the watcher given a session context type.
 *
 * @param {string} _type
 * @returns {object}
 */
function getWatcherSupportedResources(_type) {
  return {
    [Resources.TYPES.CONSOLE_MESSAGE]: true,
    [Resources.TYPES.CSS_CHANGE]: true,
    [Resources.TYPES.CSS_MESSAGE]: true,
    [Resources.TYPES.CSS_REGISTERED_PROPERTIES]: true,
    [Resources.TYPES.DOCUMENT_EVENT]: true,
    [Resources.TYPES.CACHE_STORAGE]: true,
    [Resources.TYPES.COOKIE]: true,
    [Resources.TYPES.ERROR_MESSAGE]: true,
    [Resources.TYPES.EXTENSION_STORAGE]: true,
    [Resources.TYPES.INDEXED_DB]: true,
    [Resources.TYPES.LOCAL_STORAGE]: true,
    [Resources.TYPES.SESSION_STORAGE]: true,
    [Resources.TYPES.PLATFORM_MESSAGE]: true,
    [Resources.TYPES.NETWORK_EVENT]: true,
    [Resources.TYPES.NETWORK_EVENT_DECODED_BODY_SIZE]: true,
    [Resources.TYPES.NETWORK_EVENT_STACKTRACE]: true,
    [Resources.TYPES.REFLOW]: true,
    [Resources.TYPES.STYLESHEET]: true,
    [Resources.TYPES.SOURCE]: true,
    [Resources.TYPES.THREAD_STATE]: true,
    [Resources.TYPES.SERVER_SENT_EVENT]: true,
    [Resources.TYPES.SESSION_HISTORY]: true,
    [Resources.TYPES.WEBSOCKET]: true,
    [Resources.TYPES.WEBTRANSPORT]: true,
    [Resources.TYPES.JSTRACER_TRACE]: true,
    [Resources.TYPES.JSTRACER_STATE]: true,
    [Resources.TYPES.LAST_PRIVATE_CONTEXT_EXIT]: true,
  };
}

module.exports = {
  createBrowserSessionContext,
  createBrowserElementSessionContext,
  createWebExtensionSessionContext,
  createContentProcessSessionContext,
  createWorkerSessionContext,
  SESSION_TYPES,
};
