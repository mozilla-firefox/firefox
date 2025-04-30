/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is accessed from both content and system scopes.

export const MAIN_MESSAGE_TYPE = "ActivityStream:Main";
export const CONTENT_MESSAGE_TYPE = "ActivityStream:Content";
export const PRELOAD_MESSAGE_TYPE = "ActivityStream:PreloadedBrowser";
export const UI_CODE = 1;
export const BACKGROUND_PROCESS = 2;

/**
 * globalImportContext - Are we in UI code (i.e. react, a dom) or some kind of background process?
 *                       Use this in action creators if you need different logic
 *                       for ui/background processes.
 */
export const globalImportContext =
  typeof Window === "undefined" ? BACKGROUND_PROCESS : UI_CODE;

// Create an object that avoids accidental differing key/value pairs:
// {
//   INIT: "INIT",
//   UNINIT: "UNINIT"
// }
export const actionTypes = {};

for (const type of [
  "ABOUT_SPONSORED_TOP_SITES",
  "ADDONS_INFO_REQUEST",
  "ADDONS_INFO_RESPONSE",
  "ADS_FEED_UPDATE",
  "ADS_INIT",
  "ADS_UPDATE_DATA",
  "ARCHIVE_FROM_POCKET",
  "BLOCK_SECTION",
  "BLOCK_URL",
  "BOOKMARK_URL",
  "CARD_SECTION_IMPRESSION",
  "CLEAR_PREF",
  "COPY_DOWNLOAD_LINK",
  "DELETE_BOOKMARK_BY_ID",
  "DELETE_FROM_POCKET",
  "DELETE_HISTORY_URL",
  "DIALOG_CANCEL",
  "DIALOG_CLOSE",
  "DIALOG_OPEN",
  "DISABLE_SEARCH",
  "DISCOVERY_STREAM_COLLECTION_DISMISSIBLE_TOGGLE",
  "DISCOVERY_STREAM_CONFIG_CHANGE",
  "DISCOVERY_STREAM_CONFIG_RESET",
  "DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS",
  "DISCOVERY_STREAM_CONFIG_SETUP",
  "DISCOVERY_STREAM_CONFIG_SET_VALUE",
  "DISCOVERY_STREAM_DEV_BLOCKS",
  "DISCOVERY_STREAM_DEV_BLOCKS_RESET",
  "DISCOVERY_STREAM_DEV_EXPIRE_CACHE",
  "DISCOVERY_STREAM_DEV_IDLE_DAILY",
  "DISCOVERY_STREAM_DEV_IMPRESSIONS",
  "DISCOVERY_STREAM_DEV_SHOW_PLACEHOLDER",
  "DISCOVERY_STREAM_DEV_SYNC_RS",
  "DISCOVERY_STREAM_DEV_SYSTEM_TICK",
  "DISCOVERY_STREAM_EXPERIMENT_DATA",
  "DISCOVERY_STREAM_FEEDS_UPDATE",
  "DISCOVERY_STREAM_FEED_UPDATE",
  "DISCOVERY_STREAM_IMPRESSION_STATS",
  "DISCOVERY_STREAM_LAYOUT_RESET",
  "DISCOVERY_STREAM_LAYOUT_UPDATE",
  "DISCOVERY_STREAM_LINK_BLOCKED",
  "DISCOVERY_STREAM_LOADED_CONTENT",
  "DISCOVERY_STREAM_PERSONALIZATION_INIT",
  "DISCOVERY_STREAM_PERSONALIZATION_LAST_UPDATED",
  "DISCOVERY_STREAM_PERSONALIZATION_OVERRIDE",
  "DISCOVERY_STREAM_PERSONALIZATION_RESET",
  "DISCOVERY_STREAM_PERSONALIZATION_TOGGLE",
  "DISCOVERY_STREAM_PERSONALIZATION_UPDATED",
  "DISCOVERY_STREAM_POCKET_STATE_INIT",
  "DISCOVERY_STREAM_POCKET_STATE_SET",
  "DISCOVERY_STREAM_PREFS_SETUP",
  "DISCOVERY_STREAM_RECENT_SAVES",
  "DISCOVERY_STREAM_RETRY_FEED",
  "DISCOVERY_STREAM_SPOCS_CAPS",
  "DISCOVERY_STREAM_SPOCS_ENDPOINT",
  "DISCOVERY_STREAM_SPOCS_PLACEMENTS",
  "DISCOVERY_STREAM_SPOCS_UPDATE",
  "DISCOVERY_STREAM_SPOC_BLOCKED",
  "DISCOVERY_STREAM_SPOC_IMPRESSION",
  "DISCOVERY_STREAM_TOPICS_LOADING",
  "DISCOVERY_STREAM_USER_EVENT",
  "DOWNLOAD_CHANGED",
  "FAKESPOT_CTA_CLICK",
  "FAKESPOT_DISMISS",
  "FAKE_FOCUS_SEARCH",
  "FILL_SEARCH_TERM",
  "FOLLOW_SECTION",
  "HANDOFF_SEARCH_TO_AWESOMEBAR",
  "HIDE_PERSONALIZE",
  "HIDE_PRIVACY_INFO",
  "HIDE_TOAST_MESSAGE",
  "INIT",
  "INLINE_SELECTION_CLICK",
  "INLINE_SELECTION_IMPRESSION",
  "MESSAGE_BLOCK",
  "MESSAGE_CLICK",
  "MESSAGE_DISMISS",
  "MESSAGE_IMPRESSION",
  "MESSAGE_SET",
  "MESSAGE_TOGGLE_VISIBILITY",
  "NEW_TAB_INIT",
  "NEW_TAB_INITIAL_STATE",
  "NEW_TAB_LOAD",
  "NEW_TAB_REHYDRATED",
  "NEW_TAB_STATE_REQUEST",
  "NEW_TAB_UNLOAD",
  "OPEN_ABOUT_FAKESPOT",
  "OPEN_DOWNLOAD_FILE",
  "OPEN_LINK",
  "OPEN_NEW_WINDOW",
  "OPEN_PRIVATE_WINDOW",
  "OPEN_WEBEXT_SETTINGS",
  "PARTNER_LINK_ATTRIBUTION",
  "PLACES_BOOKMARKS_REMOVED",
  "PLACES_BOOKMARK_ADDED",
  "PLACES_HISTORY_CLEARED",
  "PLACES_LINKS_CHANGED",
  "PLACES_LINKS_DELETED",
  "PLACES_LINK_BLOCKED",
  "PLACES_SAVED_TO_POCKET",
  "POCKET_CTA",
  "POCKET_LINK_DELETED_OR_ARCHIVED",
  "POCKET_LOGGED_IN",
  "POCKET_THUMBS_DOWN",
  "POCKET_THUMBS_UP",
  "POCKET_WAITING_FOR_SPOC",
  "PREFS_INITIAL_VALUES",
  "PREF_CHANGED",
  "PREVIEW_REQUEST",
  "PREVIEW_REQUEST_CANCEL",
  "PREVIEW_RESPONSE",
  "REMOVE_DOWNLOAD_FILE",
  "REPORT_AD_OPEN",
  "REPORT_AD_SUBMIT",
  "REPORT_CLOSE",
  "REPORT_CONTENT_OPEN",
  "REPORT_CONTENT_SUBMIT",
  "RICH_ICON_MISSING",
  "SAVE_SESSION_PERF_DATA",
  "SAVE_TO_POCKET",
  "SCREENSHOT_UPDATED",
  "SECTION_DEREGISTER",
  "SECTION_DISABLE",
  "SECTION_ENABLE",
  "SECTION_OPTIONS_CHANGED",
  "SECTION_PERSONALIZATION_UPDATE",
  "SECTION_REGISTER",
  "SECTION_UPDATE",
  "SECTION_UPDATE_CARD",
  "SETTINGS_CLOSE",
  "SETTINGS_OPEN",
  "SET_PREF",
  "SHOW_DOWNLOAD_FILE",
  "SHOW_FIREFOX_ACCOUNTS",
  "SHOW_PERSONALIZE",
  "SHOW_PRIVACY_INFO",
  "SHOW_SEARCH",
  "SHOW_TOAST_MESSAGE",
  "SKIPPED_SIGNIN",
  "SOV_UPDATED",
  "SUBMIT_EMAIL",
  "SUBMIT_SIGNIN",
  "SYSTEM_TICK",
  "TELEMETRY_IMPRESSION_STATS",
  "TELEMETRY_USER_EVENT",
  "TOPIC_SELECTION_IMPRESSION",
  "TOPIC_SELECTION_MAYBE_LATER",
  "TOPIC_SELECTION_SPOTLIGHT_CLOSE",
  "TOPIC_SELECTION_SPOTLIGHT_OPEN",
  "TOPIC_SELECTION_USER_DISMISS",
  "TOPIC_SELECTION_USER_OPEN",
  "TOPIC_SELECTION_USER_SAVE",
  "TOP_SITES_CANCEL_EDIT",
  "TOP_SITES_CLOSE_SEARCH_SHORTCUTS_MODAL",
  "TOP_SITES_EDIT",
  "TOP_SITES_INSERT",
  "TOP_SITES_OPEN_SEARCH_SHORTCUTS_MODAL",
  "TOP_SITES_ORGANIC_IMPRESSION_STATS",
  "TOP_SITES_PIN",
  "TOP_SITES_PREFS_UPDATED",
  "TOP_SITES_SPONSORED_IMPRESSION_STATS",
  "TOP_SITES_UNPIN",
  "TOP_SITES_UPDATED",
  "TOTAL_BOOKMARKS_REQUEST",
  "TOTAL_BOOKMARKS_RESPONSE",
  "UNBLOCK_SECTION",
  "UNFOLLOW_SECTION",
  "UNINIT",
  "UPDATE_PINNED_SEARCH_SHORTCUTS",
  "UPDATE_SEARCH_SHORTCUTS",
  "WALLPAPERS_CATEGORY_SET",
  "WALLPAPERS_CUSTOM_SET",
  "WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT",
  "WALLPAPERS_FEATURE_HIGHLIGHT_CTA_CLICKED",
  "WALLPAPERS_FEATURE_HIGHLIGHT_DISMISSED",
  "WALLPAPERS_FEATURE_HIGHLIGHT_SEEN",
  "WALLPAPERS_SET",
  "WALLPAPER_CATEGORY_CLICK",
  "WALLPAPER_CLICK",
  "WALLPAPER_REMOVE_UPLOAD",
  "WALLPAPER_UPLOAD",
  "WEATHER_IMPRESSION",
  "WEATHER_LOAD_ERROR",
  "WEATHER_LOCATION_DATA_UPDATE",
  "WEATHER_LOCATION_SEARCH_UPDATE",
  "WEATHER_LOCATION_SUGGESTIONS_UPDATE",
  "WEATHER_OPEN_PROVIDER_URL",
  "WEATHER_QUERY_UPDATE",
  "WEATHER_SEARCH_ACTIVE",
  "WEATHER_UPDATE",
  "WEBEXT_CLICK",
  "WEBEXT_DISMISS",
]) {
  actionTypes[type] = type;
}

// Helper function for creating routed actions between content and main
// Not intended to be used by consumers
function _RouteMessage(action, options) {
  const meta = action.meta ? { ...action.meta } : {};
  if (!options || !options.from || !options.to) {
    throw new Error(
      "Routed Messages must have options as the second parameter, and must at least include a .from and .to property."
    );
  }
  // For each of these fields, if they are passed as an option,
  // add them to the action. If they are not defined, remove them.
  ["from", "to", "toTarget", "fromTarget", "skipMain", "skipLocal"].forEach(
    o => {
      if (typeof options[o] !== "undefined") {
        meta[o] = options[o];
      } else if (meta[o]) {
        delete meta[o];
      }
    }
  );
  return { ...action, meta };
}

/**
 * AlsoToMain - Creates a message that will be dispatched locally and also sent to the Main process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {object} options
 * @param  {bool}   skipLocal Used by OnlyToMain to skip the main reducer
 * @param  {string} fromTarget The id of the content port from which the action originated. (optional)
 * @return {object} An action with added .meta properties
 */
function AlsoToMain(action, fromTarget, skipLocal) {
  return _RouteMessage(action, {
    from: CONTENT_MESSAGE_TYPE,
    to: MAIN_MESSAGE_TYPE,
    fromTarget,
    skipLocal,
  });
}

/**
 * OnlyToMain - Creates a message that will be sent to the Main process and skip the local reducer.
 *
 * @param  {object} action Any redux action (required)
 * @param  {object} options
 * @param  {string} fromTarget The id of the content port from which the action originated. (optional)
 * @return {object} An action with added .meta properties
 */
function OnlyToMain(action, fromTarget) {
  return AlsoToMain(action, fromTarget, true);
}

/**
 * BroadcastToContent - Creates a message that will be dispatched to main and sent to ALL content processes.
 *
 * @param  {object} action Any redux action (required)
 * @return {object} An action with added .meta properties
 */
function BroadcastToContent(action) {
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE,
  });
}

/**
 * AlsoToOneContent - Creates a message that will be will be dispatched to the main store
 *                    and also sent to a particular Content process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {string} target The id of a content port
 * @param  {bool} skipMain Used by OnlyToOneContent to skip the main process
 * @return {object} An action with added .meta properties
 */
function AlsoToOneContent(action, target, skipMain) {
  if (!target) {
    throw new Error(
      "You must provide a target ID as the second parameter of AlsoToOneContent. If you want to send to all content processes, use BroadcastToContent"
    );
  }
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE,
    toTarget: target,
    skipMain,
  });
}

/**
 * OnlyToOneContent - Creates a message that will be sent to a particular Content process
 *                    and skip the main reducer.
 *
 * @param  {object} action Any redux action (required)
 * @param  {string} target The id of a content port
 * @return {object} An action with added .meta properties
 */
function OnlyToOneContent(action, target) {
  return AlsoToOneContent(action, target, true);
}

/**
 * AlsoToPreloaded - Creates a message that dispatched to the main reducer and also sent to the preloaded tab.
 *
 * @param  {object} action Any redux action (required)
 * @return {object} An action with added .meta properties
 */
function AlsoToPreloaded(action) {
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: PRELOAD_MESSAGE_TYPE,
  });
}

/**
 * UserEvent - A telemetry ping indicating a user action. This should only
 *                   be sent from the UI during a user session.
 *
 * @param  {object} data Fields to include in the ping (source, etc.)
 * @return {object} An AlsoToMain action
 */
function UserEvent(data) {
  return AlsoToMain({
    type: actionTypes.TELEMETRY_USER_EVENT,
    data,
  });
}

/**
 * DiscoveryStreamUserEvent - A telemetry ping indicating a user action from Discovery Stream. This should only
 *                     be sent from the UI during a user session.
 *
 * @param  {object} data Fields to include in the ping (source, etc.)
 * @return {object} An AlsoToMain action
 */
function DiscoveryStreamUserEvent(data) {
  return AlsoToMain({
    type: actionTypes.DISCOVERY_STREAM_USER_EVENT,
    data,
  });
}

/**
 * ImpressionStats - A telemetry ping indicating an impression stats.
 *
 * @param  {object} data Fields to include in the ping
 * @param  {int} importContext (For testing) Override the import context for testing.
 * #return {object} An action. For UI code, a AlsoToMain action.
 */
function ImpressionStats(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_IMPRESSION_STATS,
    data,
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

/**
 * DiscoveryStreamImpressionStats - A telemetry ping indicating an impression stats in Discovery Stream.
 *
 * @param  {object} data Fields to include in the ping
 * @param  {int} importContext (For testing) Override the import context for testing.
 * #return {object} An action. For UI code, a AlsoToMain action.
 */
function DiscoveryStreamImpressionStats(
  data,
  importContext = globalImportContext
) {
  const action = {
    type: actionTypes.DISCOVERY_STREAM_IMPRESSION_STATS,
    data,
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

/**
 * DiscoveryStreamLoadedContent - A telemetry ping indicating a content gets loaded in Discovery Stream.
 *
 * @param  {object} data Fields to include in the ping
 * @param  {int} importContext (For testing) Override the import context for testing.
 * #return {object} An action. For UI code, a AlsoToMain action.
 */
function DiscoveryStreamLoadedContent(
  data,
  importContext = globalImportContext
) {
  const action = {
    type: actionTypes.DISCOVERY_STREAM_LOADED_CONTENT,
    data,
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

function SetPref(prefName, value, importContext = globalImportContext) {
  const action = {
    type: actionTypes.SET_PREF,
    data: { name: prefName, value },
  };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

function WebExtEvent(type, data, importContext = globalImportContext) {
  if (!data || !data.source) {
    throw new Error(
      'WebExtEvent actions should include a property "source", the id of the webextension that should receive the event.'
    );
  }
  const action = { type, data };
  return importContext === UI_CODE ? AlsoToMain(action) : action;
}

export const actionCreators = {
  BroadcastToContent,
  UserEvent,
  DiscoveryStreamUserEvent,
  ImpressionStats,
  AlsoToOneContent,
  OnlyToOneContent,
  AlsoToMain,
  OnlyToMain,
  AlsoToPreloaded,
  SetPref,
  WebExtEvent,
  DiscoveryStreamImpressionStats,
  DiscoveryStreamLoadedContent,
};

// These are helpers to test for certain kinds of actions
export const actionUtils = {
  isSendToMain(action) {
    if (!action.meta) {
      return false;
    }
    return (
      action.meta.to === MAIN_MESSAGE_TYPE &&
      action.meta.from === CONTENT_MESSAGE_TYPE
    );
  },
  isBroadcastToContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && !action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isSendToOneContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isSendToPreloaded(action) {
    if (!action.meta) {
      return false;
    }
    return (
      action.meta.to === PRELOAD_MESSAGE_TYPE &&
      action.meta.from === MAIN_MESSAGE_TYPE
    );
  },
  isFromMain(action) {
    if (!action.meta) {
      return false;
    }
    return (
      action.meta.from === MAIN_MESSAGE_TYPE &&
      action.meta.to === CONTENT_MESSAGE_TYPE
    );
  },
  getPortIdOfSender(action) {
    return (action.meta && action.meta.fromTarget) || null;
  },
  _RouteMessage,
};
