/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * Listen to Local Mode preference changes to instruct the RDP server
 * to update mappings of custom https origin to local folders
 */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
});

import { require } from "resource://devtools/shared/loader/Loader.sys.mjs";

const { debounce } = require("resource://devtools/shared/debounce.js");
const EventEmitter = require("resource://devtools/shared/event-emitter.js");

ChromeUtils.defineLazyGetter(lazy, "ToolboxBundle", function () {
  return new Localization(["devtools/client/toolbox.ftl"], true);
});

// Preference branch where all the mappings are stored, like this:
//   devtools.local-mode.mappings.0.origin = "firefox.localhost"
//   devtools.local-mode.mappings.1.path = "/path/to/firefox.localhost"
//   devtools.local-mode.mappings.1.origin = "firefox1.localhost"
//   devtools.local-mode.mappings.1.disabled = true
//   devtools.local-mode.mappings.2.origin = "anything.tld"
//   devtools.local-mode.mappings.2.path = "/path/to/anything"
const LOCAL_MODE_MAPPINGS_PREF_PREFIX = "devtools.local-mode.mappings.";

// Preference to be set to true to stop showing the notification at the top of the toolbox
// when loading a file:// URL.
const PREF_NOTICED = "devtools.local-mode.noticed";

// Map of Pref observer and toolbox event listener functions keyed by toolbox object
const gToolboxObservers = new WeakMap();

// Actively registered mappings.
// This is a Map where keys are origins (string)
// and values are a local folder absolute path (string).
// "firefox.localhost": "/home/me/hello-world"
let gActiveMappings = new Map();

const ORIGIN_INDEX_REGEXP = /firefox(?<origin_index>\d*)\.localhost/;

// Shared RegExp instance to extract the index in a local mapping preference.
// The "0" index used in following pref:
//   devtools.local-mode.mappings.0.origin = "firefox.localhost"
const PREFERENCE_INDEX_REGEXP = new RegExp(
  RegExp.escape(LOCAL_MODE_MAPPINGS_PREF_PREFIX) + "(?<mapping_index>\\d+)"
);

export const LocalModeMappings = {
  async setup(toolbox) {
    // As each mapping involves at least two distinct prefs (origin+path)
    // debounce in order to update the mappings only once when we create
    // or destroy a mapping.
    const prefObserver = debounce(
      updateMappings.bind(null, toolbox, false),
      250
    );
    const targetLocationListener = onTargetLocationUpdated.bind(null, toolbox);

    gToolboxObservers.set(toolbox, { prefObserver, targetLocationListener });

    toolbox.commands.targetCommand.on(
      "target-location-updated",
      targetLocationListener
    );
    Services.prefs.addObserver(LOCAL_MODE_MAPPINGS_PREF_PREFIX, prefObserver);

    await updateMappings(toolbox, true);
  },

  destroy(toolbox) {
    const observers = gToolboxObservers.get(toolbox);
    if (!observers) {
      return;
    }
    const { prefObserver, targetLocationListener } = observers;
    toolbox.commands.targetCommand.off(
      "target-location-updated",
      targetLocationListener
    );
    Services.prefs.removeObserver(
      LOCAL_MODE_MAPPINGS_PREF_PREFIX,
      prefObserver
    );
  },

  getAllMappings,

  getNextAvailableOrigin,

  createNewMapping,

  clearTransientMappings,

  getLocalFileForURL,

  LOCAL_MODE_MAPPINGS_PREF_PREFIX,
};
EventEmitter.decorate(LocalModeMappings);

/**
 * For any URL, see if the file is possibly coming from a Local Mode mapping
 *
 * @param {string} url
 * @return {string}
 *         If served from Local Mode, absolute path to the matching local file
 */
function getLocalFileForURL(url) {
  if (gActiveMappings.size == 0) {
    return false;
  }
  let host, pathname;
  try {
    const urlObject = new URL(url);
    const { protocol } = urlObject;
    if (protocol != "http:" && protocol != "https:") {
      return false;
    }
    host = urlObject.host;
    pathname = urlObject.pathname;
  } catch (e) {
    // If the URL is invalid, consider it as not mapped
    return false;
  }

  const localFolderPath = gActiveMappings.get(host);
  if (!localFolderPath) {
    return false;
  }
  const path = decodeURI(pathname.replace(/^\//, ""));

  // On Windows, replace all URI's '/' path separators with '\'
  let systemPath = path;
  if (Services.appinfo.OS === "WINNT") {
    systemPath = systemPath.replace(/\//g, "\\");
  }

  return PathUtils.joinRelative(localFolderPath, systemPath);
}

/**
 * Read all local mode mapping preferences and returns a JS dictionary
 * with all of them, which may be invalid/disabled.
 *
 * @return {Array<object>}
 *   List of all mappings, which looks like this:
 *   [
 *      { origin: "firefox.localhost", disabled: false, path: "/path/to/firefox.localhost" }
 *   ]
 */
function getAllMappings() {
  const mappings = [];
  for (const pref of Services.prefs.getChildList(
    LOCAL_MODE_MAPPINGS_PREF_PREFIX
  )) {
    // Only consider the "origin" preferences in this for..loop
    // e.g.   devtools.local-mode.mappings.0.origin = "firefox.localhost"
    // (See LOCAL_MODE_MAPPINGS_PREF_PREFIX definition)
    const suffix = pref.replace(LOCAL_MODE_MAPPINGS_PREF_PREFIX, "");
    if (!/^\d+\.origin$/.test(suffix)) {
      continue;
    }

    // Origin to serve from a local folder
    // Note that the origin may be a unicode string
    const origin = Services.prefs.getStringPref(pref);

    // Preference prefix
    // e.g.   devtools.local-mode.mappings.0.
    const prefPrefix = pref.replace(/origin$/, "");

    // Absolute path to a local folder to serve the specified origin from
    const path = Services.prefs.getStringPref(prefPrefix + "path", "");
    // Optional boolean to manually disable a mapping
    const disabled = Services.prefs.getBoolPref(prefPrefix + "disabled", false);

    mappings.push({
      origin,
      path,
      disabled,
      prefPrefix,
    });
  }
  // Return a sorted list as `getChildList` doesn't return pref sorted by name
  return mappings.sort((a, b) => {
    return a.prefPrefix.localeCompare(b.prefPrefix);
  });
}

/**
 * Maintain the list of all transient mappings added via the "try it" notification bar button.
 *
 * Keys are mapping's origin (string) and values of absolute local folder path (string).
 * This matches setLocalModeMappings's data type.
 */
let gTransientMappings = {};

/**
 * Used by tests to clear all transient mappings and avoid impacting next tests
 */
function clearTransientMappings() {
  gTransientMappings = {};
}

/**
 * Internal utility function to gather valid and enabled mappings
 * to be communicated to the backend.
 */
function getAllServerMappings() {
  const serverMappings = { ...gTransientMappings };

  for (const { origin, path, disabled } of getAllMappings()) {
    // Ignore this origin if it is disabled
    if (disabled) {
      continue;
    }

    let fileExists = false;
    try {
      fileExists = path ? new lazy.FileUtils.File(path).exists() : false;
    } catch (e) {
      console.error("Local mode path is invalid", e);
    }
    if (fileExists) {
      serverMappings[origin] = path;
    }
  }

  return serverMappings;
}

/**
 * Update the mappings by reading new values from the preferences
 * either on devtools startup, or when a pref changes.
 *
 * @param {Toolbox} toolbox
 * @param {boolean} startup
 *        True if we are updating mappings on devtools startup.
 */
async function updateMappings(toolbox, startup = false) {
  const { targetCommand } = toolbox.commands;
  const { targetFront } = targetCommand;

  const serverMappings = getAllServerMappings();

  const currentTargetOrigin =
    targetFront.url && URL.canParse(targetFront.url)
      ? new URL(targetFront.url).host
      : null;
  let matchesCurrentLocation = false;
  let atLeastOneMapping = false;
  for (const origin in serverMappings) {
    atLeastOneMapping = true;
    if (currentTargetOrigin == origin) {
      matchesCurrentLocation = true;
    }
  }

  if (targetFront.url?.startsWith("file://")) {
    showLocalModeNotice(toolbox);
  }

  // Stop any further computation on startup if we have no mappings
  if (startup && !atLeastOneMapping) {
    return;
  }

  await updateActiveServerMappings(targetCommand, serverMappings);

  // If the currently debugged document matches any of the local mode origins,
  // and is an error page, it probably means that the page was loaded/restored
  // before DevTools was opened and so failed loading.
  //
  // In order to mitigate the fact that Local Mode only starts once DevTools starts
  // automatically reload the page now that the mapping is registered,
  // so that the user doesn't have to do it manually.
  if (matchesCurrentLocation && targetFront.isErrorPage) {
    await toolbox.reload(true);
  }

  LocalModeMappings.emit("updated");

  if (startup) {
    // Record the number of permanent mappings being restored on DevTools opening
    Glean.devtoolsLocalmodeMappings.count.set(
      Object.keys(serverMappings).length
    );
  }
}

/**
 * Communicate a new list of mappings to the server, while memoizing this list.
 *
 * @param {TargetCommand} targetCommand
 * @param {object} mappings
 *                 See NetworkObserver.setLocalModeMappings for definition
 */
async function updateActiveServerMappings(targetCommand, mappings) {
  const networkFront = await targetCommand.watcherFront.getNetworkParentActor();
  await networkFront.setLocalModeMappings(mappings);

  // Convert the dictionary, required by the RDP method (setLocalModeMappings)
  // to a Map for better runtime performance
  gActiveMappings = new Map(Object.entries(mappings));
}

/**
 * By default, Local Mode mappings are using "https://firefox.localhost" origin.
 * But as we support n-th origins, find the next available origin by incrementing
 * a number after "firefox", like "firefox1.localhost".
 *
 * @return {string}
 *         The next available origin.
 */
function getNextAvailableOrigin() {
  // Compute the next index to be used in local mode mapping origin.
  // The "1" index used in the following origin:
  //   "firefox1.localhost"
  const firefoxLocalhostMappings = getAllMappings()
    .filter(mapping => ORIGIN_INDEX_REGEXP.test(mapping.origin))
    .map(mapping => mapping.origin)
    .sort((a, b) => a.localeCompare(b));

  // Also add transient mappings in order to allow register more than one transient.
  for (const origin in gTransientMappings) {
    firefoxLocalhostMappings.push(origin);
  }

  const originIndex = !firefoxLocalhostMappings.length
    ? 0
    : parseInt(
        firefoxLocalhostMappings.at(-1).match(ORIGIN_INDEX_REGEXP).groups
          .origin_index || "0",
        10
      ) + 1;
  return `firefox${originIndex == 0 ? "" : originIndex}.localhost`;
}

/**
 * Register a new mapping by handling the complexity of figuring out
 * the next available preference index.
 *
 * @param {string} origin
 * @param {string} path
 * @return {string} preference prefix for this new mapping
 */
function createNewMapping(origin, path) {
  // Compute the next index to be used in local mode mapping preference name.
  // The "0" index used in following pref:
  //   devtools.local-mode.mappings.0.origin = "firefox.localhost"
  const mappings = getAllMappings();
  const mappingIndex = !mappings.length
    ? 0
    : parseInt(
        mappings.at(-1).prefPrefix.match(PREFERENCE_INDEX_REGEXP).groups
          .mapping_index,
        10
      ) + 1;
  const prefPrefix = LOCAL_MODE_MAPPINGS_PREF_PREFIX + mappingIndex + ".";
  Services.prefs.setStringPref(prefPrefix + "origin", origin);
  Services.prefs.setStringPref(prefPrefix + "path", path);

  return prefPrefix;
}

/**
 * Called whenever a target navigated to a new location.
 *
 * @param {Toolbox} toolbox
 * @param {TargetFront} targetFront
 */
function onTargetLocationUpdated(toolbox, targetFront) {
  const { descriptorFront } = toolbox.commands;

  if (
    targetFront.isTopLevel &&
    targetFront.url.startsWith("file://") &&
    // Local Mode only works when debugging local Firefox tabs
    descriptorFront.isLocalTab
  ) {
    showLocalModeNotice(toolbox);
  }
}

/**
 * Notify the user about the Local Mode when loading any file:// URL.
 * As they can instead be loaded from https:// thanks to the Local Mode.
 *
 * @param {Toolbox} toolbox
 */
function showLocalModeNotice(toolbox) {
  const messageDismissed = Services.prefs.getBoolPref(PREF_NOTICED, false);
  if (messageDismissed) {
    return;
  }

  // Compute an absolute path to a folder out of the file URL.
  // We may set the path to the parent folder and extract a filename out of this URL.
  // For example when mapping "file://home/foo"
  //  * if "foo" is a folder, "firefox.localhost" is mapped to "/home/foo"
  //    and we would open "https://firefox.localhost/"
  //  * otherwise "firefox.localhost" is mapped to "/home"
  //    and we would open "https://firefox.localhost/foo"
  const uri = Services.io.newURI(toolbox.target.url);
  uri.QueryInterface(Ci.nsIFileURL);
  // Ignore this location if this isn't a valid local file
  if (!uri.file.exists()) {
    return;
  }
  const path = uri.file.isDirectory() ? uri.file.path : uri.file.parent.path;
  const filename = uri.file.isDirectory() ? "" : uri.file.leafName;

  // Lookup for any already existing mapping matching the same folder
  const existingMapping = getAllMappings().find(
    mapping => mapping.path === path
  );

  // Either use a brand new unique origin, or reuse the existing mapping's one
  const origin = LocalModeMappings.getNextAvailableOrigin();
  const url = "https://" + (existingMapping ? existingMapping.origin : origin);

  const buttons = [];
  const notificationId = "local-mode-notice";
  if (existingMapping) {
    // When the URL matches an existing mapping, only show a navigation link
    buttons.push({
      label: lazy.ToolboxBundle.formatValueSync(
        "toolbox-local-mode-notice-navigate-to-existing-mapping"
      ),
      callback: async () => {
        notificationBox.removeNotification(
          notificationBox.getNotificationWithValue(notificationId)
        );

        await toolbox.commands.targetCommand.navigateTo(url + "/" + filename);
      },
    });
  } else {
    // Otherwise, show two buttons:
    //  * one to register a permanent mapping
    //  * one to try a transient one
    buttons.push({
      label: lazy.ToolboxBundle.formatValueSync(
        "toolbox-local-mode-notice-add-to-settings-button"
      ),
      callback: async () => {
        const onMappingsUpdated = LocalModeMappings.once("updated");

        // Create the new mapping
        const prefPrefix = createNewMapping(origin, path);

        // Wait for the new mapping to be applied
        await onMappingsUpdated;

        notificationBox.removeNotification(
          notificationBox.getNotificationWithValue(notificationId)
        );

        // Open the Options panel
        const { panelDoc } = await toolbox.selectTool("options");

        // Highlight the created mapping item
        const mappingItem = panelDoc
          .querySelector(`[data-pref-prefix="${prefPrefix}"]`)
          .closest("li");
        mappingItem.scrollIntoView({ block: "center" });
        mappingItem.classList.add("options-panel-highlight");

        // Navigate to the mapping's URL
        await toolbox.commands.targetCommand.navigateTo(url + "/" + filename);

        Glean.devtoolsLocalmodeMappings.addPermanentFromNotification.add(1);
      },
    });

    buttons.push({
      label: lazy.ToolboxBundle.formatValueSync(
        "toolbox-local-mode-notice-try-it-button"
      ),
      callback: async () => {
        notificationBox.removeNotification(
          notificationBox.getNotificationWithValue(notificationId)
        );

        // Setup a transient mapping and load the https URL while dismissing the notice
        gTransientMappings[origin] = path;

        await updateActiveServerMappings(
          toolbox.commands.targetCommand,
          getAllServerMappings()
        );

        await toolbox.commands.targetCommand.navigateTo(url + "/" + filename);

        Glean.devtoolsLocalmodeMappings.addTransient.add(1);
      },
    });
  }

  // In all cases, show an explicit button before the "x" (close) button
  // to permanently stop showing these notices.
  buttons.push({
    label: lazy.ToolboxBundle.formatValueSync(
      "toolbox-local-mode-notice-always-hide"
    ),
    callback: async () => {
      // Flip the preference to stop any notice about local mode
      Services.prefs.setBoolPref(PREF_NOTICED, true);

      notificationBox.removeNotification(
        notificationBox.getNotificationWithValue(notificationId)
      );
    },
  });

  const notificationBox = toolbox.getNotificationBox();
  notificationBox.appendNotification(
    lazy.ToolboxBundle.formatValueSync("toolbox-local-mode-notice", { url }),
    notificationId,
    null,
    notificationBox.PRIORITY_INFO_MEDIUM,
    buttons
  );
}
