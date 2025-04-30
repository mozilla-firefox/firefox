/*! THIS FILE IS AUTO-GENERATED: webpack.system-addon.config.js */
var NewtabRenderUtils;
/******/ (() => { // webpackBootstrap
/******/ 	"use strict";
/******/ 	// The require scope
/******/ 	var __webpack_require__ = {};
/******/ 	
/************************************************************************/
/******/ 	/* webpack/runtime/compat get default export */
/******/ 	(() => {
/******/ 		// getDefaultExport function for compatibility with non-harmony modules
/******/ 		__webpack_require__.n = (module) => {
/******/ 			var getter = module && module.__esModule ?
/******/ 				() => (module['default']) :
/******/ 				() => (module);
/******/ 			__webpack_require__.d(getter, { a: getter });
/******/ 			return getter;
/******/ 		};
/******/ 	})();
/******/ 	
/******/ 	/* webpack/runtime/define property getters */
/******/ 	(() => {
/******/ 		// define getter functions for harmony exports
/******/ 		__webpack_require__.d = (exports, definition) => {
/******/ 			for(var key in definition) {
/******/ 				if(__webpack_require__.o(definition, key) && !__webpack_require__.o(exports, key)) {
/******/ 					Object.defineProperty(exports, key, { enumerable: true, get: definition[key] });
/******/ 				}
/******/ 			}
/******/ 		};
/******/ 	})();
/******/ 	
/******/ 	/* webpack/runtime/global */
/******/ 	(() => {
/******/ 		__webpack_require__.g = (function() {
/******/ 			if (typeof globalThis === 'object') return globalThis;
/******/ 			try {
/******/ 				return this || new Function('return this')();
/******/ 			} catch (e) {
/******/ 				if (typeof window === 'object') return window;
/******/ 			}
/******/ 		})();
/******/ 	})();
/******/ 	
/******/ 	/* webpack/runtime/hasOwnProperty shorthand */
/******/ 	(() => {
/******/ 		__webpack_require__.o = (obj, prop) => (Object.prototype.hasOwnProperty.call(obj, prop))
/******/ 	})();
/******/ 	
/******/ 	/* webpack/runtime/make namespace object */
/******/ 	(() => {
/******/ 		// define __esModule on exports
/******/ 		__webpack_require__.r = (exports) => {
/******/ 			if(typeof Symbol !== 'undefined' && Symbol.toStringTag) {
/******/ 				Object.defineProperty(exports, Symbol.toStringTag, { value: 'Module' });
/******/ 			}
/******/ 			Object.defineProperty(exports, '__esModule', { value: true });
/******/ 		};
/******/ 	})();
/******/ 	
/************************************************************************/
var __webpack_exports__ = {};
// ESM COMPAT FLAG
__webpack_require__.r(__webpack_exports__);

// EXPORTS
__webpack_require__.d(__webpack_exports__, {
  NewTab: () => (/* binding */ NewTab),
  renderCache: () => (/* binding */ renderCache),
  renderWithoutState: () => (/* binding */ renderWithoutState)
});

;// CONCATENATED MODULE: ./common/Actions.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file is accessed from both content and system scopes.

const MAIN_MESSAGE_TYPE = "ActivityStream:Main";
const CONTENT_MESSAGE_TYPE = "ActivityStream:Content";
const PRELOAD_MESSAGE_TYPE = "ActivityStream:PreloadedBrowser";
const UI_CODE = 1;
const BACKGROUND_PROCESS = 2;

/**
 * globalImportContext - Are we in UI code (i.e. react, a dom) or some kind of background process?
 *                       Use this in action creators if you need different logic
 *                       for ui/background processes.
 */
const globalImportContext =
  typeof Window === "undefined" ? BACKGROUND_PROCESS : UI_CODE;

// Create an object that avoids accidental differing key/value pairs:
// {
//   INIT: "INIT",
//   UNINIT: "UNINIT"
// }
const actionTypes = {};

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

const actionCreators = {
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
const actionUtils = {
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

;// CONCATENATED MODULE: external "ReactRedux"
const external_ReactRedux_namespaceObject = ReactRedux;
;// CONCATENATED MODULE: external "React"
const external_React_namespaceObject = React;
var external_React_default = /*#__PURE__*/__webpack_require__.n(external_React_namespaceObject);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamAdmin/SimpleHashRouter.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class SimpleHashRouter extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onHashChange = this.onHashChange.bind(this);
    this.state = {
      hash: globalThis.location.hash
    };
  }
  onHashChange() {
    this.setState({
      hash: globalThis.location.hash
    });
  }
  componentWillMount() {
    globalThis.addEventListener("hashchange", this.onHashChange);
  }
  componentWillUnmount() {
    globalThis.removeEventListener("hashchange", this.onHashChange);
  }
  render() {
    const [, ...routes] = this.state.hash.split("-");
    return /*#__PURE__*/external_React_default().cloneElement(this.props.children, {
      location: {
        hash: this.state.hash,
        routes
      }
    });
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamAdmin/DiscoveryStreamAdmin.jsx
function _extends() { _extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return _extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






// Pref Constants
const PREF_AD_SIZE_MEDIUM_RECTANGLE = "newtabAdSize.mediumRectangle";
const PREF_AD_SIZE_BILLBOARD = "newtabAdSize.billboard";
const PREF_AD_SIZE_LEADERBOARD = "newtabAdSize.leaderboard";
const PREF_CONTEXTUAL_CONTENT_SELECTED_FEED = "discoverystream.contextualContent.selectedFeed";
const PREF_CONTEXTUAL_CONTENT_FEEDS = "discoverystream.contextualContent.feeds";
const PREF_SECTIONS_ENABLED = "discoverystream.sections.enabled";
const PREF_SPOC_PLACEMENTS = "discoverystream.placements.spocs";
const PREF_SPOC_COUNTS = "discoverystream.placements.spocs.counts";
const Row = props => /*#__PURE__*/external_React_default().createElement("tr", _extends({
  className: "message-item"
}, props), props.children);
function relativeTime(timestamp) {
  if (!timestamp) {
    return "";
  }
  const seconds = Math.floor((Date.now() - timestamp) / 1000);
  const minutes = Math.floor((Date.now() - timestamp) / 60000);
  if (seconds < 2) {
    return "just now";
  } else if (seconds < 60) {
    return `${seconds} seconds ago`;
  } else if (minutes === 1) {
    return "1 minute ago";
  } else if (minutes < 600) {
    return `${minutes} minutes ago`;
  }
  return new Date(timestamp).toLocaleString();
}
class ToggleStoryButton extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.handleClick = this.handleClick.bind(this);
  }
  handleClick() {
    this.props.onClick(this.props.story);
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("button", {
      onClick: this.handleClick
    }, "collapse/open");
  }
}
class TogglePrefCheckbox extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onChange = this.onChange.bind(this);
  }
  onChange(event) {
    this.props.onChange(this.props.pref, event.target.checked);
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("input", {
      type: "checkbox",
      checked: this.props.checked,
      onChange: this.onChange,
      disabled: this.props.disabled
    }), " ", this.props.pref, " ");
  }
}
class Personalization extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.togglePersonalization = this.togglePersonalization.bind(this);
  }
  togglePersonalization() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.DISCOVERY_STREAM_PERSONALIZATION_TOGGLE
    }));
  }
  render() {
    const {
      lastUpdated,
      initialized
    } = this.props.state.Personalization;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      colSpan: "2"
    }, /*#__PURE__*/external_React_default().createElement(TogglePrefCheckbox, {
      checked: this.props.personalized,
      pref: "personalized",
      onChange: this.togglePersonalization
    }))), /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Personalization Last Updated"), /*#__PURE__*/external_React_default().createElement("td", null, relativeTime(lastUpdated) || "(no data)")), /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Personalization Initialized"), /*#__PURE__*/external_React_default().createElement("td", null, initialized ? "true" : "false")))));
  }
}
class DiscoveryStreamAdminUI extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.restorePrefDefaults = this.restorePrefDefaults.bind(this);
    this.setConfigValue = this.setConfigValue.bind(this);
    this.expireCache = this.expireCache.bind(this);
    this.refreshCache = this.refreshCache.bind(this);
    this.showPlaceholder = this.showPlaceholder.bind(this);
    this.idleDaily = this.idleDaily.bind(this);
    this.systemTick = this.systemTick.bind(this);
    this.syncRemoteSettings = this.syncRemoteSettings.bind(this);
    this.onStoryToggle = this.onStoryToggle.bind(this);
    this.handleWeatherSubmit = this.handleWeatherSubmit.bind(this);
    this.handleWeatherUpdate = this.handleWeatherUpdate.bind(this);
    this.resetBlocks = this.resetBlocks.bind(this);
    this.refreshTopicSelectionCache = this.refreshTopicSelectionCache.bind(this);
    this.toggleTBRFeed = this.toggleTBRFeed.bind(this);
    this.handleSectionsToggle = this.handleSectionsToggle.bind(this);
    this.toggleIABBanners = this.toggleIABBanners.bind(this);
    this.state = {
      toggledStories: {},
      weatherQuery: ""
    };
  }
  setConfigValue(configName, configValue) {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.DISCOVERY_STREAM_CONFIG_SET_VALUE,
      data: {
        name: configName,
        value: configValue
      }
    }));
  }
  restorePrefDefaults() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.DISCOVERY_STREAM_CONFIG_RESET_DEFAULTS
    }));
  }
  refreshCache() {
    const {
      config
    } = this.props.state.DiscoveryStream;
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.DISCOVERY_STREAM_CONFIG_CHANGE,
      data: config
    }));
  }
  refreshTopicSelectionCache() {
    this.props.dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.displayCount", 0));
    this.props.dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", true));
  }
  dispatchSimpleAction(type) {
    this.props.dispatch(actionCreators.OnlyToMain({
      type
    }));
  }
  resetBlocks() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.DISCOVERY_STREAM_DEV_BLOCKS_RESET
    }));
  }
  systemTick() {
    this.dispatchSimpleAction(actionTypes.DISCOVERY_STREAM_DEV_SYSTEM_TICK);
  }
  expireCache() {
    this.dispatchSimpleAction(actionTypes.DISCOVERY_STREAM_DEV_EXPIRE_CACHE);
  }
  showPlaceholder() {
    this.dispatchSimpleAction(actionTypes.DISCOVERY_STREAM_DEV_SHOW_PLACEHOLDER);
  }
  toggleTBRFeed(e) {
    const feed = e.target.value;
    const selectedFeed = PREF_CONTEXTUAL_CONTENT_SELECTED_FEED;
    this.props.dispatch(actionCreators.SetPref(selectedFeed, feed));
  }
  idleDaily() {
    this.dispatchSimpleAction(actionTypes.DISCOVERY_STREAM_DEV_IDLE_DAILY);
  }
  syncRemoteSettings() {
    this.dispatchSimpleAction(actionTypes.DISCOVERY_STREAM_DEV_SYNC_RS);
  }
  handleWeatherUpdate(e) {
    this.setState({
      weatherQuery: e.target.value || ""
    });
  }
  handleWeatherSubmit(e) {
    e.preventDefault();
    const {
      weatherQuery
    } = this.state;
    this.props.dispatch(actionCreators.SetPref("weather.query", weatherQuery));
  }
  toggleIABBanners(e) {
    const {
      pressed,
      id
    } = e.target;

    // Set the active pref to true/false
    switch (id) {
      case "newtab_billboard":
        // Update boolean pref for billboard ad size
        this.props.dispatch(actionCreators.SetPref(PREF_AD_SIZE_BILLBOARD, pressed));
        break;
      case "newtab_leaderboard":
        // Update boolean pref for billboard ad size
        this.props.dispatch(actionCreators.SetPref(PREF_AD_SIZE_LEADERBOARD, pressed));
        break;
      case "newtab_rectangle":
        // Update boolean pref for mediumRectangle (MREC) ad size
        this.props.dispatch(actionCreators.SetPref(PREF_AD_SIZE_MEDIUM_RECTANGLE, pressed));
        break;
    }

    // Note: The counts array is passively updated whenever the placements array is updated.
    // The default pref values for each are:
    // PREF_SPOC_PLACEMENTS: "newtab_spocs"
    // PREF_SPOC_COUNTS: "6"
    const generateSpocPrefValues = () => {
      const placements = this.props.otherPrefs[PREF_SPOC_PLACEMENTS]?.split(",").map(item => item.trim()).filter(item => item) || [];
      const counts = this.props.otherPrefs[PREF_SPOC_COUNTS]?.split(",").map(item => item.trim()).filter(item => item) || [];

      // Confirm that the IAB type will have a count value of "1"
      const supportIABAdTypes = ["newtab_leaderboard", "newtab_rectangle", "newtab_billboard"];
      let countValue;
      if (supportIABAdTypes.includes(id)) {
        countValue = "1"; // Default count value for all IAB ad types
      } else {
        throw new Error("IAB ad type not supported");
      }
      if (pressed) {
        // If pressed is true, add the id to the placements array
        if (!placements.includes(id)) {
          placements.push(id);
          counts.push(countValue);
        }
      } else {
        // If pressed is false, remove the id from the placements array
        const index = placements.indexOf(id);
        if (index !== -1) {
          placements.splice(index, 1);
          counts.splice(index, 1);
        }
      }
      return {
        placements: placements.join(", "),
        counts: counts.join(", ")
      };
    };
    const {
      placements,
      counts
    } = generateSpocPrefValues();

    // Update prefs with new values
    this.props.dispatch(actionCreators.SetPref(PREF_SPOC_PLACEMENTS, placements));
    this.props.dispatch(actionCreators.SetPref(PREF_SPOC_COUNTS, counts));
  }
  handleSectionsToggle(e) {
    const {
      pressed
    } = e.target;
    this.props.dispatch(actionCreators.SetPref(PREF_SECTIONS_ENABLED, pressed));
    this.props.dispatch(actionCreators.SetPref("discoverystream.sections.cards.enabled", pressed));
    this.props.dispatch(actionCreators.SetPref("discoverystream.sections.cards.thumbsUpDown.enabled", pressed));
  }
  renderComponent(width, component) {
    return /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Type"), /*#__PURE__*/external_React_default().createElement("td", null, component.type)), /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Width"), /*#__PURE__*/external_React_default().createElement("td", null, width)), component.feed && this.renderFeed(component.feed)));
  }
  renderWeatherData() {
    const {
      suggestions
    } = this.props.state.Weather;
    let weatherTable;
    if (suggestions) {
      weatherTable = /*#__PURE__*/external_React_default().createElement("div", {
        className: "weather-section"
      }, /*#__PURE__*/external_React_default().createElement("form", {
        onSubmit: this.handleWeatherSubmit
      }, /*#__PURE__*/external_React_default().createElement("label", {
        htmlFor: "weather-query"
      }, "Weather query"), /*#__PURE__*/external_React_default().createElement("input", {
        type: "text",
        min: "3",
        max: "10",
        id: "weather-query",
        onChange: this.handleWeatherUpdate,
        value: this.weatherQuery
      }), /*#__PURE__*/external_React_default().createElement("button", {
        type: "submit"
      }, "Submit")), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, suggestions.map(suggestion => /*#__PURE__*/external_React_default().createElement("tr", {
        className: "message-item",
        key: suggestion.city_name
      }, /*#__PURE__*/external_React_default().createElement("td", {
        className: "message-id"
      }, /*#__PURE__*/external_React_default().createElement("span", null, suggestion.city_name, " ", /*#__PURE__*/external_React_default().createElement("br", null))), /*#__PURE__*/external_React_default().createElement("td", {
        className: "message-summary"
      }, /*#__PURE__*/external_React_default().createElement("pre", null, JSON.stringify(suggestion, null, 2))))))));
    }
    return weatherTable;
  }
  renderFeedData(url) {
    const {
      feeds
    } = this.props.state.DiscoveryStream;
    const feed = feeds.data[url].data;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h4", null, "Feed url: ", url), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, feed.recommendations?.map(story => this.renderStoryData(story)))));
  }
  renderFeedsData() {
    const {
      feeds
    } = this.props.state.DiscoveryStream;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, Object.keys(feeds.data).map(url => this.renderFeedData(url)));
  }
  renderImpressionsData() {
    const {
      impressions
    } = this.props.state.DiscoveryStream;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h4", null, "Feed Impressions"), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, Object.keys(impressions.feed).map(key => {
      return /*#__PURE__*/external_React_default().createElement(Row, {
        key: key
      }, /*#__PURE__*/external_React_default().createElement("td", {
        className: "min"
      }, key), /*#__PURE__*/external_React_default().createElement("td", null, relativeTime(impressions.feed[key]) || "(no data)"));
    }))));
  }
  renderBlocksData() {
    const {
      blocks
    } = this.props.state.DiscoveryStream;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h4", null, "Blocks"), /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.resetBlocks
    }, "Reset Blocks"), " ", /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, Object.keys(blocks).map(key => {
      return /*#__PURE__*/external_React_default().createElement(Row, {
        key: key
      }, /*#__PURE__*/external_React_default().createElement("td", {
        className: "min"
      }, key));
    }))));
  }
  renderSpocs() {
    const {
      spocs
    } = this.props.state.DiscoveryStream;
    const unifiedAdsSpocsEnabled = this.props.otherPrefs["unifiedAds.spocs.enabled"];
    const unifiedAdsEndpoint = this.props.otherPrefs["unifiedAds.endpoint"];
    let spocsData = [];
    if (spocs.data && spocs.data.newtab_spocs && spocs.data.newtab_spocs.items) {
      spocsData = spocs.data.newtab_spocs.items || [];
    }
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "spocs_endpoint"), /*#__PURE__*/external_React_default().createElement("td", null, unifiedAdsSpocsEnabled ? unifiedAdsEndpoint : spocs.spocs_endpoint)), /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Data last fetched"), /*#__PURE__*/external_React_default().createElement("td", null, relativeTime(spocs.lastUpdated))))), /*#__PURE__*/external_React_default().createElement("h4", null, "Spoc data"), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, spocsData.map(spoc => this.renderStoryData(spoc)))), /*#__PURE__*/external_React_default().createElement("h4", null, "Spoc frequency caps"), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, spocs.frequency_caps.map(spoc => this.renderStoryData(spoc)))));
  }
  onStoryToggle(story) {
    const {
      toggledStories
    } = this.state;
    this.setState({
      toggledStories: {
        ...toggledStories,
        [story.id]: !toggledStories[story.id]
      }
    });
  }
  renderStoryData(story) {
    let storyData = "";
    if (this.state.toggledStories[story.id]) {
      storyData = JSON.stringify(story, null, 2);
    }
    return /*#__PURE__*/external_React_default().createElement("tr", {
      className: "message-item",
      key: story.id
    }, /*#__PURE__*/external_React_default().createElement("td", {
      className: "message-id"
    }, /*#__PURE__*/external_React_default().createElement("span", null, story.id, " ", /*#__PURE__*/external_React_default().createElement("br", null)), /*#__PURE__*/external_React_default().createElement(ToggleStoryButton, {
      story: story,
      onClick: this.onStoryToggle
    })), /*#__PURE__*/external_React_default().createElement("td", {
      className: "message-summary"
    }, /*#__PURE__*/external_React_default().createElement("pre", null, storyData)));
  }
  renderFeed(feed) {
    const {
      feeds
    } = this.props.state.DiscoveryStream;
    if (!feed.url) {
      return null;
    }
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Feed url"), /*#__PURE__*/external_React_default().createElement("td", null, feed.url)), /*#__PURE__*/external_React_default().createElement(Row, null, /*#__PURE__*/external_React_default().createElement("td", {
      className: "min"
    }, "Data last fetched"), /*#__PURE__*/external_React_default().createElement("td", null, relativeTime(feeds.data[feed.url] ? feeds.data[feed.url].lastUpdated : null) || "(no data)")));
  }
  render() {
    const prefToggles = "enabled collapsible".split(" ");
    const {
      config,
      layout
    } = this.props.state.DiscoveryStream;
    const personalized = this.props.otherPrefs["discoverystream.personalization.enabled"];
    const selectedFeed = this.props.otherPrefs[PREF_CONTEXTUAL_CONTENT_SELECTED_FEED];
    const sectionsEnabled = this.props.otherPrefs[PREF_SECTIONS_ENABLED];
    const TBRFeeds = this.props.otherPrefs[PREF_CONTEXTUAL_CONTENT_FEEDS].split(",").map(s => s.trim()).filter(item => item);

    // Prefs for IAB Banners
    const mediumRectangleEnabled = this.props.otherPrefs[PREF_AD_SIZE_MEDIUM_RECTANGLE];
    const billboardsEnabled = this.props.otherPrefs[PREF_AD_SIZE_BILLBOARD];
    const leaderboardEnabled = this.props.otherPrefs[PREF_AD_SIZE_LEADERBOARD];
    const spocPlacements = this.props.otherPrefs[PREF_SPOC_PLACEMENTS];
    const mediumRectangleEnabledPressed = mediumRectangleEnabled && spocPlacements.includes("newtab_rectangle");
    const billboardPressed = billboardsEnabled && spocPlacements.includes("newtab_billboard");
    const leaderboardPressed = leaderboardEnabled && spocPlacements.includes("newtab_leaderboard");
    return /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.restorePrefDefaults
    }, "Restore Pref Defaults"), " ", /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.refreshCache
    }, "Refresh Cache"), /*#__PURE__*/external_React_default().createElement("br", null), /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.expireCache
    }, "Expire Cache"), " ", /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.systemTick
    }, "Trigger System Tick"), " ", /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.idleDaily
    }, "Trigger Idle Daily"), /*#__PURE__*/external_React_default().createElement("br", null), /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.syncRemoteSettings
    }, "Sync Remote Settings"), " ", /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.refreshTopicSelectionCache
    }, "Refresh Topic selection count"), /*#__PURE__*/external_React_default().createElement("br", null), /*#__PURE__*/external_React_default().createElement("button", {
      className: "button",
      onClick: this.showPlaceholder
    }, "Show Placeholder Cards"), " ", /*#__PURE__*/external_React_default().createElement("select", {
      className: "button",
      onChange: this.toggleTBRFeed,
      value: selectedFeed
    }, TBRFeeds.map(feed => /*#__PURE__*/external_React_default().createElement("option", {
      key: feed,
      value: feed
    }, feed))), /*#__PURE__*/external_React_default().createElement("div", {
      className: "toggle-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "sections-toggle",
      pressed: sectionsEnabled || null,
      onToggle: this.handleSectionsToggle,
      label: "Toggle DS Sections"
    })), /*#__PURE__*/external_React_default().createElement("details", {
      className: "details-section"
    }, /*#__PURE__*/external_React_default().createElement("summary", null, "IAB Banner Ad Sizes"), /*#__PURE__*/external_React_default().createElement("div", {
      className: "toggle-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "newtab_leaderboard",
      pressed: leaderboardPressed || null,
      onToggle: this.toggleIABBanners,
      label: "Enable IAB Leaderboard"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "toggle-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "newtab_billboard",
      pressed: billboardPressed || null,
      onToggle: this.toggleIABBanners,
      label: "Enable IAB Billboard"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "toggle-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "newtab_rectangle",
      pressed: mediumRectangleEnabledPressed || null,
      onToggle: this.toggleIABBanners,
      label: "Enable IAB Medium Rectangle (MREC)"
    }))), /*#__PURE__*/external_React_default().createElement("table", null, /*#__PURE__*/external_React_default().createElement("tbody", null, prefToggles.map(pref => /*#__PURE__*/external_React_default().createElement(Row, {
      key: pref
    }, /*#__PURE__*/external_React_default().createElement("td", null, /*#__PURE__*/external_React_default().createElement(TogglePrefCheckbox, {
      checked: config[pref],
      pref: pref,
      onChange: this.setConfigValue
    })))))), /*#__PURE__*/external_React_default().createElement("h3", null, "Layout"), layout.map((row, rowIndex) => /*#__PURE__*/external_React_default().createElement("div", {
      key: `row-${rowIndex}`
    }, row.components.map((component, componentIndex) => /*#__PURE__*/external_React_default().createElement("div", {
      key: `component-${componentIndex}`,
      className: "ds-component"
    }, this.renderComponent(row.width, component))))), /*#__PURE__*/external_React_default().createElement("h3", null, "Personalization"), /*#__PURE__*/external_React_default().createElement(Personalization, {
      personalized: personalized,
      dispatch: this.props.dispatch,
      state: {
        Personalization: this.props.state.Personalization
      }
    }), /*#__PURE__*/external_React_default().createElement("h3", null, "Spocs"), this.renderSpocs(), /*#__PURE__*/external_React_default().createElement("h3", null, "Feeds Data"), /*#__PURE__*/external_React_default().createElement("div", {
      className: "large-data-container"
    }, this.renderFeedsData()), /*#__PURE__*/external_React_default().createElement("h3", null, "Impressions Data"), /*#__PURE__*/external_React_default().createElement("div", {
      className: "large-data-container"
    }, this.renderImpressionsData()), /*#__PURE__*/external_React_default().createElement("h3", null, "Blocked Data"), /*#__PURE__*/external_React_default().createElement("div", {
      className: "large-data-container"
    }, this.renderBlocksData()), /*#__PURE__*/external_React_default().createElement("h3", null, "Weather Data"), this.renderWeatherData());
  }
}
class DiscoveryStreamAdminInner extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.setState = this.setState.bind(this);
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: `discoverystream-admin ${this.props.collapsed ? "collapsed" : "expanded"}`
    }, /*#__PURE__*/external_React_default().createElement("main", {
      className: "main-panel"
    }, /*#__PURE__*/external_React_default().createElement("h1", null, "Discovery Stream Admin"), /*#__PURE__*/external_React_default().createElement("p", {
      className: "helpLink"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-small-spacer icon-info"
    }), " ", /*#__PURE__*/external_React_default().createElement("span", null, "Need to access the ASRouter Admin dev tools?", " ", /*#__PURE__*/external_React_default().createElement("a", {
      target: "blank",
      href: "about:asrouter"
    }, "Click here"))), /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement(DiscoveryStreamAdminUI, {
      state: {
        DiscoveryStream: this.props.DiscoveryStream,
        Personalization: this.props.Personalization,
        Weather: this.props.Weather
      },
      otherPrefs: this.props.Prefs.values,
      dispatch: this.props.dispatch
    }))));
  }
}
class CollapseToggle extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onCollapseToggle = this.onCollapseToggle.bind(this);
    this.state = {
      collapsed: false
    };
  }
  get renderAdmin() {
    const {
      props
    } = this;
    return props.location.hash && props.location.hash.startsWith("#devtools");
  }
  onCollapseToggle(e) {
    e.preventDefault();
    this.setState(state => ({
      collapsed: !state.collapsed
    }));
  }
  setBodyClass() {
    if (this.renderAdmin && !this.state.collapsed) {
      globalThis.document.body.classList.add("no-scroll");
    } else {
      globalThis.document.body.classList.remove("no-scroll");
    }
  }
  componentDidMount() {
    this.setBodyClass();
  }
  componentDidUpdate() {
    this.setBodyClass();
  }
  componentWillUnmount() {
    globalThis.document.body.classList.remove("no-scroll");
  }
  render() {
    const {
      props
    } = this;
    const {
      renderAdmin
    } = this;
    const isCollapsed = this.state.collapsed || !renderAdmin;
    const label = `${isCollapsed ? "Expand" : "Collapse"} devtools`;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("a", {
      href: "#devtools",
      title: label,
      "aria-label": label,
      className: `discoverystream-admin-toggle ${isCollapsed ? "collapsed" : "expanded"}`,
      onClick: this.renderAdmin ? this.onCollapseToggle : null
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-devtools"
    })), renderAdmin ? /*#__PURE__*/external_React_default().createElement(DiscoveryStreamAdminInner, _extends({}, props, {
      collapsed: this.state.collapsed
    })) : null);
  }
}
const _DiscoveryStreamAdmin = props => /*#__PURE__*/external_React_default().createElement(SimpleHashRouter, null, /*#__PURE__*/external_React_default().createElement(CollapseToggle, props));
const DiscoveryStreamAdmin = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Sections: state.Sections,
  DiscoveryStream: state.DiscoveryStream,
  Personalization: state.Personalization,
  Prefs: state.Prefs,
  Weather: state.Weather
}))(_DiscoveryStreamAdmin);
;// CONCATENATED MODULE: ./content-src/components/ConfirmDialog/ConfirmDialog.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





/**
 * ConfirmDialog component.
 * One primary action button, one cancel button.
 *
 * Content displayed is controlled by `data` prop the component receives.
 * Example:
 * data: {
 *   // Any sort of data needed to be passed around by actions.
 *   payload: site.url,
 *   // Primary button AlsoToMain action.
 *   action: "DELETE_HISTORY_URL",
 *   // Primary button USerEvent action.
 *   userEvent: "DELETE",
 *   // Array of locale ids to display.
 *   message_body: ["confirm_history_delete_p1", "confirm_history_delete_notice_p2"],
 *   // Text for primary button.
 *   confirm_button_string_id: "menu_action_delete"
 * },
 */
class _ConfirmDialog extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this._handleCancelBtn = this._handleCancelBtn.bind(this);
    this._handleConfirmBtn = this._handleConfirmBtn.bind(this);
  }
  _handleCancelBtn() {
    this.props.dispatch({
      type: actionTypes.DIALOG_CANCEL
    });
    this.props.dispatch(actionCreators.UserEvent({
      event: actionTypes.DIALOG_CANCEL,
      source: this.props.data.eventSource
    }));
  }
  _handleConfirmBtn() {
    this.props.data.onConfirm.forEach(this.props.dispatch);
  }
  _renderModalMessage() {
    const message_body = this.props.data.body_string_id;
    if (!message_body) {
      return null;
    }
    return /*#__PURE__*/external_React_default().createElement("span", null, message_body.map(msg => /*#__PURE__*/external_React_default().createElement("p", {
      key: msg,
      "data-l10n-id": msg
    })));
  }
  render() {
    if (!this.props.visible) {
      return null;
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "confirmation-dialog"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "modal-overlay",
      onClick: this._handleCancelBtn,
      role: "presentation"
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "modal"
    }, /*#__PURE__*/external_React_default().createElement("section", {
      className: "modal-message"
    }, this.props.data.icon && /*#__PURE__*/external_React_default().createElement("span", {
      className: `icon icon-spacer icon-${this.props.data.icon}`
    }), this._renderModalMessage()), /*#__PURE__*/external_React_default().createElement("section", {
      className: "actions"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      onClick: this._handleCancelBtn,
      "data-l10n-id": this.props.data.cancel_button_string_id
    }), /*#__PURE__*/external_React_default().createElement("button", {
      className: "done",
      onClick: this._handleConfirmBtn,
      "data-l10n-id": this.props.data.confirm_button_string_id,
      "data-l10n-args": JSON.stringify(this.props.data.confirm_button_string_args)
    }))));
  }
}
const ConfirmDialog = (0,external_ReactRedux_namespaceObject.connect)(state => state.Dialog)(_ConfirmDialog);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSImage/DSImage.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


const PLACEHOLDER_IMAGE_DATA_ARRAY = [{
  rotation: "0deg",
  offsetx: "20px",
  offsety: "8px",
  scale: "45%"
}, {
  rotation: "54deg",
  offsetx: "-26px",
  offsety: "62px",
  scale: "55%"
}, {
  rotation: "-30deg",
  offsetx: "78px",
  offsety: "30px",
  scale: "68%"
}, {
  rotation: "-22deg",
  offsetx: "0",
  offsety: "92px",
  scale: "60%"
}, {
  rotation: "-65deg",
  offsetx: "66px",
  offsety: "28px",
  scale: "60%"
}, {
  rotation: "22deg",
  offsetx: "-35px",
  offsety: "62px",
  scale: "52%"
}, {
  rotation: "-25deg",
  offsetx: "86px",
  offsety: "-15px",
  scale: "68%"
}];
const PLACEHOLDER_IMAGE_COLORS_ARRAY = "#0090ED #FF4F5F #2AC3A2 #FF7139 #A172FF #FFA437 #FF2A8A".split(" ");
function generateIndex({
  keyCode,
  max
}) {
  if (!keyCode) {
    // Just grab a random index if we cannot generate an index from a key.
    return Math.floor(Math.random() * max);
  }
  const hashStr = str => {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
      let charCode = str.charCodeAt(i);
      hash += charCode;
    }
    return hash;
  };
  const hash = hashStr(keyCode);
  return hash % max;
}
function PlaceholderImage({
  urlKey,
  titleKey
}) {
  const dataIndex = generateIndex({
    keyCode: urlKey,
    max: PLACEHOLDER_IMAGE_DATA_ARRAY.length
  });
  const colorIndex = generateIndex({
    keyCode: titleKey,
    max: PLACEHOLDER_IMAGE_COLORS_ARRAY.length
  });
  const {
    rotation,
    offsetx,
    offsety,
    scale
  } = PLACEHOLDER_IMAGE_DATA_ARRAY[dataIndex];
  const color = PLACEHOLDER_IMAGE_COLORS_ARRAY[colorIndex];
  const style = {
    "--placeholderBackgroundColor": color,
    "--placeholderBackgroundRotation": rotation,
    "--placeholderBackgroundOffsetx": offsetx,
    "--placeholderBackgroundOffsety": offsety,
    "--placeholderBackgroundScale": scale
  };
  return /*#__PURE__*/external_React_default().createElement("div", {
    style: style,
    className: "placeholder-image"
  });
}
class DSImage extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onOptimizedImageError = this.onOptimizedImageError.bind(this);
    this.onNonOptimizedImageError = this.onNonOptimizedImageError.bind(this);
    this.onLoad = this.onLoad.bind(this);
    this.state = {
      isLoaded: false,
      optimizedImageFailed: false,
      useTransition: false
    };
  }
  onIdleCallback() {
    if (!this.state.isLoaded) {
      this.setState({
        useTransition: true
      });
    }
  }
  reformatImageURL(url, width, height) {
    const smart = this.props.smartCrop ? "smart/" : "";
    // Change the image URL to request a size tailored for the parent container width
    // Also: force JPEG, quality 60, no upscaling, no EXIF data
    // Uses Thumbor: https://thumbor.readthedocs.io/en/latest/usage.html
    return `https://img-getpocket.cdn.mozilla.net/${width}x${height}/${smart}filters:format(jpeg):quality(60):no_upscale():strip_exif()/${encodeURIComponent(url)}`;
  }
  componentDidMount() {
    this.idleCallbackId = this.props.windowObj.requestIdleCallback(this.onIdleCallback.bind(this));
  }
  componentWillUnmount() {
    if (this.idleCallbackId) {
      this.props.windowObj.cancelIdleCallback(this.idleCallbackId);
    }
  }
  render() {
    let classNames = `ds-image
      ${this.props.extraClassNames ? ` ${this.props.extraClassNames}` : ``}
      ${this.state && this.state.useTransition ? ` use-transition` : ``}
      ${this.state && this.state.isLoaded ? ` loaded` : ``}
    `;
    let img;
    if (this.state) {
      if (this.props.optimize && this.props.rawSource && !this.state.optimizedImageFailed) {
        let baseSource = this.props.rawSource;
        let sizeRules = [];
        let srcSetRules = [];
        for (let rule of this.props.sizes) {
          let {
            mediaMatcher,
            width,
            height
          } = rule;
          let sizeRule = `${mediaMatcher} ${width}px`;
          sizeRules.push(sizeRule);
          let srcSetRule = `${this.reformatImageURL(baseSource, width, height)} ${width}w`;
          let srcSetRule2x = `${this.reformatImageURL(baseSource, width * 2, height * 2)} ${width * 2}w`;
          srcSetRules.push(srcSetRule);
          srcSetRules.push(srcSetRule2x);
        }
        if (this.props.sizes.length) {
          // We have to supply a fallback in the very unlikely event that none of
          // the media queries match. The smallest dimension was chosen arbitrarily.
          sizeRules.push(`${this.props.sizes[this.props.sizes.length - 1].width}px`);
        }
        img = /*#__PURE__*/external_React_default().createElement("img", {
          loading: "lazy",
          alt: this.props.alt_text,
          crossOrigin: "anonymous",
          onLoad: this.onLoad,
          onError: this.onOptimizedImageError,
          sizes: sizeRules.join(","),
          src: baseSource,
          srcSet: srcSetRules.join(",")
        });
      } else if (this.props.source && !this.state.nonOptimizedImageFailed) {
        img = /*#__PURE__*/external_React_default().createElement("img", {
          loading: "lazy",
          alt: this.props.alt_text,
          crossOrigin: "anonymous",
          onLoad: this.onLoad,
          onError: this.onNonOptimizedImageError,
          src: this.props.source
        });
      } else {
        // We consider a failed to load img or source without an image as loaded.
        classNames = `${classNames} loaded`;
        // Remove the img element if we have no source. Render a placeholder instead.
        // This only happens for recent saves without a source.
        if (this.props.isRecentSave && !this.props.rawSource && !this.props.source) {
          img = /*#__PURE__*/external_React_default().createElement(PlaceholderImage, {
            urlKey: this.props.url,
            titleKey: this.props.title
          });
        } else {
          img = /*#__PURE__*/external_React_default().createElement("div", {
            className: "broken-image"
          });
        }
      }
    }
    return /*#__PURE__*/external_React_default().createElement("picture", {
      className: classNames
    }, img);
  }
  onOptimizedImageError() {
    // This will trigger a re-render and the unoptimized 450px image will be used as a fallback
    this.setState({
      optimizedImageFailed: true
    });
  }
  onNonOptimizedImageError() {
    this.setState({
      nonOptimizedImageFailed: true
    });
  }
  onLoad() {
    this.setState({
      isLoaded: true
    });
  }
}
DSImage.defaultProps = {
  source: null,
  // The current source style from Pocket API (always 450px)
  rawSource: null,
  // Unadulterated image URL to filter through Thumbor
  extraClassNames: null,
  // Additional classnames to append to component
  optimize: true,
  // Measure parent container to request exact sizes
  alt_text: null,
  windowObj: window,
  // Added to support unit tests
  sizes: []
};
;// CONCATENATED MODULE: ./content-src/components/ContextMenu/ContextMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



class ContextMenu extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.hideContext = this.hideContext.bind(this);
    this.onShow = this.onShow.bind(this);
    this.onClick = this.onClick.bind(this);
  }
  hideContext() {
    this.props.onUpdate(false);
  }
  onShow() {
    if (this.props.onShow) {
      this.props.onShow();
    }
  }
  componentDidMount() {
    this.onShow();
    setTimeout(() => {
      globalThis.addEventListener("click", this.hideContext);
    }, 0);
  }
  componentWillUnmount() {
    globalThis.removeEventListener("click", this.hideContext);
  }
  onClick(event) {
    // Eat all clicks on the context menu so they don't bubble up to window.
    // This prevents the context menu from closing when clicking disabled items
    // or the separators.
    event.stopPropagation();
  }
  render() {
    // Disabling focus on the menu span allows the first tab to focus on the first menu item instead of the wrapper.
    return (
      /*#__PURE__*/
      // eslint-disable-next-line jsx-a11y/interactive-supports-focus
      external_React_default().createElement("span", {
        className: "context-menu"
      }, /*#__PURE__*/external_React_default().createElement("ul", {
        role: "menu",
        onClick: this.onClick,
        onKeyDown: this.onClick,
        className: "context-menu-list"
      }, this.props.options.map((option, i) => option.type === "separator" ? /*#__PURE__*/external_React_default().createElement("li", {
        key: i,
        className: "separator",
        role: "separator"
      }) : option.type !== "empty" && /*#__PURE__*/external_React_default().createElement(ContextMenuItem, {
        key: i,
        option: option,
        hideContext: this.hideContext,
        keyboardAccess: this.props.keyboardAccess
      }))))
    );
  }
}
class _ContextMenuItem extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onClick = this.onClick.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onKeyUp = this.onKeyUp.bind(this);
    this.focusFirst = this.focusFirst.bind(this);
  }
  onClick(event) {
    this.props.hideContext();
    this.props.option.onClick(event);
  }

  // Focus the first menu item if the menu was accessed via the keyboard.
  focusFirst(button) {
    if (this.props.keyboardAccess && button) {
      button.focus();
    }
  }

  // This selects the correct node based on the key pressed
  focusSibling(target, key) {
    const {
      parentNode
    } = target;
    const closestSiblingSelector = key === "ArrowUp" ? "previousSibling" : "nextSibling";
    if (!parentNode[closestSiblingSelector]) {
      return;
    }
    if (parentNode[closestSiblingSelector].firstElementChild) {
      parentNode[closestSiblingSelector].firstElementChild.focus();
    } else {
      parentNode[closestSiblingSelector][closestSiblingSelector].firstElementChild.focus();
    }
  }
  onKeyDown(event) {
    const {
      option
    } = this.props;
    switch (event.key) {
      case "Tab":
        // tab goes down in context menu, shift + tab goes up in context menu
        // if we're on the last item, one more tab will close the context menu
        // similarly, if we're on the first item, one more shift + tab will close it
        if (event.shiftKey && option.first || !event.shiftKey && option.last) {
          this.props.hideContext();
        }
        break;
      case "ArrowUp":
      case "ArrowDown":
        event.preventDefault();
        this.focusSibling(event.target, event.key);
        break;
      case "Enter":
      case " ":
        event.preventDefault();
        this.props.hideContext();
        option.onClick();
        break;
      case "Escape":
        this.props.hideContext();
        break;
    }
  }

  // Prevents the default behavior of spacebar
  // scrolling the page & auto-triggering buttons.
  onKeyUp(event) {
    if (event.key === " ") {
      event.preventDefault();
    }
  }
  render() {
    const {
      option
    } = this.props;
    const className = [option.disabled ? "disabled" : ""].join(" ");
    return /*#__PURE__*/external_React_default().createElement("li", {
      role: "presentation",
      className: "context-menu-item"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      role: "menuitem",
      className: className,
      onClick: this.onClick,
      onKeyDown: this.onKeyDown,
      onKeyUp: this.onKeyUp,
      ref: option.first ? this.focusFirst : null,
      "aria-haspopup": option.id === "newtab-menu-edit-topsites" ? "dialog" : null
    }, /*#__PURE__*/external_React_default().createElement("span", {
      "data-l10n-id": option.string_id || option.id
    })));
  }
}
const ContextMenuItem = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs
}))(_ContextMenuItem);
;// CONCATENATED MODULE: ./content-src/lib/link-menu-options.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



const _OpenInPrivateWindow = site => ({
  id: "newtab-menu-open-new-private-window",
  icon: "new-window-private",
  action: actionCreators.OnlyToMain({
    type: actionTypes.OPEN_PRIVATE_WINDOW,
    data: { url: site.url, referrer: site.referrer },
  }),
  userEvent: "OPEN_PRIVATE_WINDOW",
});

/**
 * List of functions that return items that can be included as menu options in a
 * LinkMenu. All functions take the site as the first parameter, and optionally
 * the index of the site.
 */
const LinkMenuOptions = {
  Separator: () => ({ type: "separator" }),
  EmptyItem: () => ({ type: "empty" }),
  ShowPrivacyInfo: () => ({
    id: "newtab-menu-show-privacy-info",
    icon: "info",
    action: {
      type: actionTypes.SHOW_PRIVACY_INFO,
    },
    userEvent: "SHOW_PRIVACY_INFO",
  }),
  AboutSponsored: site => ({
    id: "newtab-menu-show-privacy-info",
    icon: "info",
    action: actionCreators.AlsoToMain({
      type: actionTypes.ABOUT_SPONSORED_TOP_SITES,
      data: {
        advertiser_name: (site.label || site.hostname).toLocaleLowerCase(),
        position: site.sponsored_position,
        tile_id: site.sponsored_tile_id,
        block_key: site.block_key,
      },
    }),
    userEvent: "TOPSITE_SPONSOR_INFO",
  }),
  RemoveBookmark: site => ({
    id: "newtab-menu-remove-bookmark",
    icon: "bookmark-added",
    action: actionCreators.AlsoToMain({
      type: actionTypes.DELETE_BOOKMARK_BY_ID,
      data: site.bookmarkGuid,
    }),
    userEvent: "BOOKMARK_DELETE",
  }),
  AddBookmark: site => ({
    id: "newtab-menu-bookmark",
    icon: "bookmark-hollow",
    action: actionCreators.AlsoToMain({
      type: actionTypes.BOOKMARK_URL,
      data: { url: site.url, title: site.title, type: site.type },
    }),
    userEvent: "BOOKMARK_ADD",
  }),
  OpenInNewWindow: site => ({
    id: "newtab-menu-open-new-window",
    icon: "new-window",
    action: actionCreators.AlsoToMain({
      type: actionTypes.OPEN_NEW_WINDOW,
      data: {
        referrer: site.referrer,
        typedBonus: site.typedBonus,
        url: site.url,
        sponsored_tile_id: site.sponsored_tile_id,
        ...(site.section
          ? {
              section: site.section,
              section_position: site.section_position,
              is_section_followed: site.is_section_followed,
            }
          : {}),
      },
    }),
    userEvent: "OPEN_NEW_WINDOW",
  }),
  // This blocks the url for regular stories,
  // but also sends a message to DiscoveryStream with flight_id.
  // If DiscoveryStream sees this message for a flight_id
  // it also blocks it on the flight_id.
  BlockUrl: (site, index, eventSource) => {
    return LinkMenuOptions.BlockUrls([site], index, eventSource);
  },
  // Same as BlockUrl, except can work on an array of sites.
  BlockUrls: (tiles, pos, eventSource) => ({
    id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: actionCreators.AlsoToMain({
      type: actionTypes.BLOCK_URL,
      data: tiles.map(site => ({
        url: site.original_url || site.open_url || site.url,
        // pocket_id is only for pocket stories being in highlights, and then dismissed.
        pocket_id: site.pocket_id,
        tile_id: site.tile_id,
        ...(site.block_key ? { block_key: site.block_key } : {}),
        recommendation_id: site.recommendation_id,
        scheduled_corpus_item_id: site.scheduled_corpus_item_id,
        corpus_item_id: site.corpus_item_id,
        received_rank: site.received_rank,
        recommended_at: site.recommended_at,
        // used by PlacesFeed and TopSitesFeed for sponsored top sites blocking.
        isSponsoredTopSite: site.sponsored_position,
        type: site.type,
        card_type: site.card_type,
        ...(site.shim && site.shim.delete ? { shim: site.shim.delete } : {}),
        ...(site.flight_id ? { flight_id: site.flight_id } : {}),
        // If not sponsored, hostname could be anything (Cat3 Data!).
        // So only put in advertiser_name for sponsored topsites.
        ...(site.sponsored_position
          ? {
              advertiser_name: (
                site.label || site.hostname
              )?.toLocaleLowerCase(),
            }
          : {}),
        position: pos,
        ...(site.sponsored_tile_id ? { tile_id: site.sponsored_tile_id } : {}),
        is_pocket_card: site.type === "CardGrid",
        is_list_card: site.is_list_card,
        ...(site.format ? { format: site.format } : {}),
        ...(site.section
          ? {
              section: site.section,
              section_position: site.section_position,
              is_section_followed: site.is_section_followed,
            }
          : {}),
      })),
    }),
    impression: actionCreators.ImpressionStats({
      source: eventSource,
      block: 0,
      tiles: tiles.map((site, index) => ({
        id: site.guid,
        pos: pos + index,
        ...(site.shim && site.shim.delete ? { shim: site.shim.delete } : {}),
      })),
    }),
    userEvent: "BLOCK",
  }),

  // This is the "Dismiss" action for leaderboard/billboard ads.
  BlockAdUrl: (site, pos, eventSource) => ({
    id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: actionCreators.AlsoToMain({
      type: actionTypes.BLOCK_URL,
      data: [site],
    }),
    impression: actionCreators.ImpressionStats({
      source: eventSource,
      block: 0,
      tiles: [
        {
          id: site.guid,
          pos,
          ...(site.shim && site.shim.save ? { shim: site.shim.save } : {}),
        },
      ],
    }),
    userEvent: "BLOCK",
  }),

  // This is an option for web extentions which will result in remove items from
  // memory and notify the web extenion, rather than using the built-in block list.
  WebExtDismiss: (site, index, eventSource) => ({
    id: "menu_action_webext_dismiss",
    string_id: "newtab-menu-dismiss",
    icon: "dismiss",
    action: actionCreators.WebExtEvent(actionTypes.WEBEXT_DISMISS, {
      source: eventSource,
      url: site.url,
      action_position: index,
    }),
  }),
  DeleteUrl: (site, index, eventSource, isEnabled, siteInfo) => ({
    id: "newtab-menu-delete-history",
    icon: "delete",
    action: {
      type: actionTypes.DIALOG_OPEN,
      data: {
        onConfirm: [
          actionCreators.AlsoToMain({
            type: actionTypes.DELETE_HISTORY_URL,
            data: {
              url: site.url,
              pocket_id: site.pocket_id,
              forceBlock: site.bookmarkGuid,
            },
          }),
          actionCreators.UserEvent(
            Object.assign(
              { event: "DELETE", source: eventSource, action_position: index },
              siteInfo
            )
          ),
          // Also broadcast that this url has been deleted so that
          // the confirmation dialog knows it needs to disappear now.
          actionCreators.AlsoToMain({
            type: actionTypes.DIALOG_CLOSE,
          }),
        ],
        eventSource,
        body_string_id: [
          "newtab-confirm-delete-history-p1",
          "newtab-confirm-delete-history-p2",
        ],
        confirm_button_string_id: "newtab-topsites-delete-history-button",
        cancel_button_string_id: "newtab-topsites-cancel-button",
        icon: "modal-delete",
      },
    },
    userEvent: "DIALOG_OPEN",
  }),
  ShowFile: site => ({
    id: "newtab-menu-show-file",
    icon: "search",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SHOW_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  OpenFile: site => ({
    id: "newtab-menu-open-file",
    icon: "open-file",
    action: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  CopyDownloadLink: site => ({
    id: "newtab-menu-copy-download-link",
    icon: "copy",
    action: actionCreators.OnlyToMain({
      type: actionTypes.COPY_DOWNLOAD_LINK,
      data: { url: site.url },
    }),
  }),
  GoToDownloadPage: site => ({
    id: "newtab-menu-go-to-download-page",
    icon: "download",
    action: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_LINK,
      data: { url: site.referrer },
    }),
    disabled: !site.referrer,
  }),
  RemoveDownload: site => ({
    id: "newtab-menu-remove-download",
    icon: "delete",
    action: actionCreators.OnlyToMain({
      type: actionTypes.REMOVE_DOWNLOAD_FILE,
      data: { url: site.url },
    }),
  }),
  PinTopSite: (site, index) => ({
    id: "newtab-menu-pin",
    icon: "pin",
    action: actionCreators.AlsoToMain({
      type: actionTypes.TOP_SITES_PIN,
      data: {
        site,
        index,
      },
    }),
    userEvent: "PIN",
  }),
  UnpinTopSite: site => ({
    id: "newtab-menu-unpin",
    icon: "unpin",
    action: actionCreators.AlsoToMain({
      type: actionTypes.TOP_SITES_UNPIN,
      data: { site: { url: site.url } },
    }),
    userEvent: "UNPIN",
  }),
  SaveToPocket: (site, index, eventSource = "CARDGRID") => ({
    id: "newtab-menu-save-to-pocket",
    icon: "pocket-save",
    action: actionCreators.AlsoToMain({
      type: actionTypes.SAVE_TO_POCKET,
      data: {
        site: { url: site.url, title: site.title },
      },
    }),
    impression: actionCreators.ImpressionStats({
      source: eventSource,
      pocket: 0,
      tiles: [
        {
          id: site.guid,
          pos: index,
          ...(site.shim && site.shim.save ? { shim: site.shim.save } : {}),
        },
      ],
    }),
    userEvent: "SAVE_TO_POCKET",
  }),
  DeleteFromPocket: site => ({
    id: "newtab-menu-delete-pocket",
    icon: "pocket-delete",
    action: actionCreators.AlsoToMain({
      type: actionTypes.DELETE_FROM_POCKET,
      data: { pocket_id: site.pocket_id },
    }),
    userEvent: "DELETE_FROM_POCKET",
  }),
  ArchiveFromPocket: site => ({
    id: "newtab-menu-archive-pocket",
    icon: "pocket-archive",
    action: actionCreators.AlsoToMain({
      type: actionTypes.ARCHIVE_FROM_POCKET,
      data: { pocket_id: site.pocket_id },
    }),
    userEvent: "ARCHIVE_FROM_POCKET",
  }),
  EditTopSite: (site, index) => ({
    id: "newtab-menu-edit-topsites",
    icon: "edit",
    action: {
      type: actionTypes.TOP_SITES_EDIT,
      data: { index },
    },
  }),
  CheckBookmark: site =>
    site.bookmarkGuid
      ? LinkMenuOptions.RemoveBookmark(site)
      : LinkMenuOptions.AddBookmark(site),
  CheckPinTopSite: (site, index) =>
    site.isPinned
      ? LinkMenuOptions.UnpinTopSite(site)
      : LinkMenuOptions.PinTopSite(site, index),
  CheckSavedToPocket: (site, index, source) =>
    site.pocket_id
      ? LinkMenuOptions.DeleteFromPocket(site)
      : LinkMenuOptions.SaveToPocket(site, index, source),
  CheckBookmarkOrArchive: site =>
    site.pocket_id
      ? LinkMenuOptions.ArchiveFromPocket(site)
      : LinkMenuOptions.CheckBookmark(site),
  CheckArchiveFromPocket: site =>
    site.pocket_id
      ? LinkMenuOptions.ArchiveFromPocket(site)
      : LinkMenuOptions.EmptyItem(),
  CheckDeleteFromPocket: site =>
    site.pocket_id
      ? LinkMenuOptions.DeleteFromPocket(site)
      : LinkMenuOptions.EmptyItem(),
  OpenInPrivateWindow: (site, index, eventSource, isEnabled) =>
    isEnabled ? _OpenInPrivateWindow(site) : LinkMenuOptions.EmptyItem(),
  ChangeWeatherLocation: () => ({
    id: "newtab-weather-menu-change-location",
    action: actionCreators.BroadcastToContent({
      type: actionTypes.WEATHER_SEARCH_ACTIVE,
      data: true,
    }),
  }),
  ChangeWeatherDisplaySimple: () => ({
    id: "newtab-weather-menu-change-weather-display-simple",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "weather.display",
        value: "simple",
      },
    }),
  }),
  ChangeWeatherDisplayDetailed: () => ({
    id: "newtab-weather-menu-change-weather-display-detailed",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "weather.display",
        value: "detailed",
      },
    }),
  }),
  ChangeTempUnitFahrenheit: () => ({
    id: "newtab-weather-menu-change-temperature-units-fahrenheit",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "weather.temperatureUnits",
        value: "f",
      },
    }),
  }),
  ChangeTempUnitCelsius: () => ({
    id: "newtab-weather-menu-change-temperature-units-celsius",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "weather.temperatureUnits",
        value: "c",
      },
    }),
  }),
  HideWeather: () => ({
    id: "newtab-weather-menu-hide-weather",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "showWeather",
        value: false,
      },
    }),
  }),
  OpenLearnMoreURL: site => ({
    id: "newtab-weather-menu-learn-more",
    action: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_LINK,
      data: { url: site.url },
    }),
  }),
  FakespotDismiss: () => ({
    id: "newtab-menu-dismiss",
    action: actionCreators.OnlyToMain({
      type: actionTypes.SET_PREF,
      data: {
        name: "discoverystream.contextualContent.fakespot.enabled",
        value: false,
      },
    }),
    impression: actionCreators.OnlyToMain({
      type: actionTypes.FAKESPOT_DISMISS,
    }),
  }),
  AboutFakespot: site => ({
    id: "newtab-menu-about-fakespot",
    action: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_LINK,
      data: { url: site.url },
    }),
    impression: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_ABOUT_FAKESPOT,
    }),
  }),
  SectionBlock: ({
    sectionPersonalization,
    sectionKey,
    sectionPosition,
    title,
  }) => ({
    id: "newtab-menu-section-block",
    icon: "delete",
    action: {
      // Open the confirmation dialog to block a section.
      type: actionTypes.DIALOG_OPEN,
      data: {
        onConfirm: [
          // Once the user confirmed their intention to block this section,
          // update their preferences.
          actionCreators.AlsoToMain({
            type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
            data: {
              ...sectionPersonalization,
              [sectionKey]: {
                isBlocked: true,
                isFollowed: false,
              },
            },
          }),
          // Telemetry
          actionCreators.OnlyToMain({
            type: actionTypes.BLOCK_SECTION,
            data: {
              section: sectionKey,
              section_position: sectionPosition,
              event_source: "CONTEXT_MENU",
            },
          }),
          // Also broadcast that this section has been blocked so that
          // the confirmation dialog knows it needs to disappear now.
          actionCreators.AlsoToMain({
            type: actionTypes.DIALOG_CLOSE,
          }),
        ],
        // Pass Fluent strings to ConfirmDialog component for the copy
        // of the prompt to block sections.
        body_string_id: [
          "newtab-section-confirm-block-topic-p1",
          "newtab-section-confirm-block-topic-p2",
        ],
        confirm_button_string_id: "newtab-section-block-topic-button",
        confirm_button_string_args: { topic: title },
        cancel_button_string_id: "newtab-section-cancel-button",
      },
    },
    userEvent: "DIALOG_OPEN",
  }),
  SectionUnfollow: ({
    sectionPersonalization,
    sectionKey,
    sectionPosition,
  }) => ({
    id: "newtab-menu-section-unfollow",
    action: actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: (({ sectionKey: _sectionKey, ...remaining }) => remaining)(
        sectionPersonalization
      ),
    }),
    impression: actionCreators.OnlyToMain({
      type: actionTypes.UNFOLLOW_SECTION,
      data: {
        section: sectionKey,
        section_position: sectionPosition,
        event_source: "CONTEXT_MENU",
      },
    }),
  }),
  ManageSponsoredContent: () => ({
    id: "newtab-menu-manage-sponsored-content",
    action: actionCreators.OnlyToMain({ type: actionTypes.SETTINGS_OPEN }),
    userEvent: "OPEN_NEWTAB_PREFS",
  }),
  OurSponsorsAndYourPrivacy: () => ({
    id: "newtab-menu-our-sponsors-and-your-privacy",
    action: actionCreators.OnlyToMain({
      type: actionTypes.OPEN_LINK,
      data: {
        url: "https://support.mozilla.org/kb/pocket-sponsored-stories-new-tabs",
      },
    }),
    userEvent: "CLICK_PRIVACY_INFO",
  }),
  ReportAd: site => {
    return {
      id: "newtab-menu-report-this-ad",
      action: actionCreators.AlsoToMain({
        type: actionTypes.REPORT_AD_OPEN,
        data: {
          card_type: site.card_type,
          position: site.position,
          reporting_url: site.shim.report,
          url: site.url,
        },
      }),
    };
  },

  ReportContent: site => {
    return {
      id: "newtab-menu-report-content",
      action: actionCreators.AlsoToMain({
        type: actionTypes.REPORT_CONTENT_OPEN,
        data: {
          card_type: site.card_type,
          corpus_item_id: site.corpus_item_id,
          is_section_followed: site.is_section_followed,
          received_rank: site.received_rank,
          recommended_at: site.recommended_at,
          scheduled_corpus_item_id: site.scheduled_corpus_item_id,
          section_position: site.section_position,
          section: site.section,
          title: site.title,
          topic: site.topic,
          url: site.url,
        },
      }),
    };
  },
};

;// CONCATENATED MODULE: ./content-src/components/LinkMenu/LinkMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






const DEFAULT_SITE_MENU_OPTIONS = ["CheckPinTopSite", "EditTopSite", "Separator", "OpenInNewWindow", "OpenInPrivateWindow", "Separator", "BlockUrl"];
class _LinkMenu extends (external_React_default()).PureComponent {
  getOptions() {
    const {
      props
    } = this;
    const {
      site,
      index,
      source,
      isPrivateBrowsingEnabled,
      siteInfo,
      platform,
      dispatch,
      options,
      shouldSendImpressionStats,
      userEvent = actionCreators.UserEvent
    } = props;

    // Handle special case of default site
    const propOptions = site.isDefault && !site.searchTopSite && !site.sponsored_position ? DEFAULT_SITE_MENU_OPTIONS : options;
    const linkMenuOptions = propOptions.map(o => LinkMenuOptions[o](site, index, source, isPrivateBrowsingEnabled, siteInfo, platform)).map(option => {
      const {
        action,
        impression,
        id,
        type,
        userEvent: eventName
      } = option;
      if (!type && id) {
        option.onClick = (event = {}) => {
          const {
            ctrlKey,
            metaKey,
            shiftKey,
            button
          } = event;
          // Only send along event info if there's something non-default to send
          if (ctrlKey || metaKey || shiftKey || button === 1) {
            action.data = Object.assign({
              event: {
                ctrlKey,
                metaKey,
                shiftKey,
                button
              }
            }, action.data);
          }
          dispatch(action);
          if (eventName) {
            const userEventData = Object.assign({
              event: eventName,
              source,
              action_position: index,
              value: {
                card_type: site.flight_id ? "spoc" : "organic"
              }
            }, siteInfo);
            dispatch(userEvent(userEventData));
          }
          if (impression && shouldSendImpressionStats) {
            dispatch(impression);
          }
        };
      }
      return option;
    });

    // This is for accessibility to support making each item tabbable.
    // We want to know which item is the first and which item
    // is the last, so we can close the context menu accordingly.
    linkMenuOptions[0].first = true;
    linkMenuOptions[linkMenuOptions.length - 1].last = true;
    return linkMenuOptions;
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement(ContextMenu, {
      onUpdate: this.props.onUpdate,
      onShow: this.props.onShow,
      options: this.getOptions(),
      keyboardAccess: this.props.keyboardAccess
    });
  }
}
const getState = state => ({
  isPrivateBrowsingEnabled: state.Prefs.values.isPrivateBrowsingEnabled,
  platform: state.Prefs.values.platform
});
const LinkMenu = (0,external_ReactRedux_namespaceObject.connect)(getState)(_LinkMenu);
;// CONCATENATED MODULE: ./content-src/components/ContextMenu/ContextMenuButton.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class ContextMenuButton extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      showContextMenu: false,
      contextMenuKeyboard: false
    };
    this.onClick = this.onClick.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onUpdate = this.onUpdate.bind(this);
  }
  openContextMenu(isKeyBoard) {
    if (this.props.onUpdate) {
      this.props.onUpdate(true);
    }
    this.setState({
      showContextMenu: true,
      contextMenuKeyboard: isKeyBoard
    });
  }
  onClick(event) {
    event.preventDefault();
    this.openContextMenu(false, event);
  }
  onKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.openContextMenu(true, event);
    }
  }
  onUpdate(showContextMenu) {
    if (this.props.onUpdate) {
      this.props.onUpdate(showContextMenu);
    }
    this.setState({
      showContextMenu
    });
  }
  render() {
    const {
      tooltipArgs,
      tooltip,
      children,
      refFunction
    } = this.props;
    const {
      showContextMenu,
      contextMenuKeyboard
    } = this.state;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("button", {
      "aria-haspopup": "true",
      "data-l10n-id": tooltip,
      "data-l10n-args": tooltipArgs ? JSON.stringify(tooltipArgs) : null,
      className: "context-menu-button icon",
      onKeyDown: this.onKeyDown,
      onClick: this.onClick,
      ref: refFunction,
      tabIndex: this.props.tabIndex || 0,
      onFocus: this.props.onFocus
    }), showContextMenu ? /*#__PURE__*/external_React_default().cloneElement(children, {
      keyboardAccess: contextMenuKeyboard,
      onUpdate: this.onUpdate
    }) : null);
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSLinkMenu/DSLinkMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






class _DSLinkMenu extends (external_React_default()).PureComponent {
  render() {
    const {
      index,
      dispatch
    } = this.props;
    let TOP_STORIES_CONTEXT_MENU_OPTIONS;
    const PREF_REPORT_ADS_ENABLED = "discoverystream.reportAds.enabled";
    const prefs = this.props.Prefs.values;
    const showAdsReporting = prefs[PREF_REPORT_ADS_ENABLED];
    const isSpoc = this.props.card_type === "spoc";
    if (isSpoc) {
      TOP_STORIES_CONTEXT_MENU_OPTIONS = ["BlockUrl", ...(showAdsReporting ? ["ReportAd"] : []), "ManageSponsoredContent", "OurSponsorsAndYourPrivacy"];
    } else {
      const saveToPocketOptions = this.props.pocket_button_enabled ? ["CheckArchiveFromPocket", "CheckSavedToPocket"] : [];
      TOP_STORIES_CONTEXT_MENU_OPTIONS = ["CheckBookmark", ...(this.props.section ? ["ReportContent"] : []), ...saveToPocketOptions, "Separator", "OpenInNewWindow", "OpenInPrivateWindow", "Separator", "BlockUrl"];
    }
    const type = this.props.type || "DISCOVERY_STREAM";
    const title = this.props.title || this.props.source;
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "context-menu-position-container"
    }, /*#__PURE__*/external_React_default().createElement(ContextMenuButton, {
      tooltip: "newtab-menu-content-tooltip",
      tooltipArgs: {
        title
      },
      onUpdate: this.props.onMenuUpdate
    }, /*#__PURE__*/external_React_default().createElement(LinkMenu, {
      dispatch: dispatch,
      index: index,
      source: type.toUpperCase(),
      onShow: this.props.onMenuShow,
      options: TOP_STORIES_CONTEXT_MENU_OPTIONS,
      shouldSendImpressionStats: true,
      userEvent: actionCreators.DiscoveryStreamUserEvent,
      site: {
        referrer: "https://getpocket.com/recommendations",
        title: this.props.title,
        type: this.props.type,
        url: this.props.url,
        guid: this.props.id,
        pocket_id: this.props.pocket_id,
        card_type: this.props.card_type,
        shim: this.props.shim,
        bookmarkGuid: this.props.bookmarkGuid,
        flight_id: this.props.flightId,
        tile_id: this.props.tile_id,
        recommendation_id: this.props.recommendation_id,
        corpus_item_id: this.props.corpus_item_id,
        scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
        recommended_at: this.props.recommended_at,
        received_rank: this.props.received_rank,
        topic: this.props.topic,
        is_list_card: this.props.is_list_card,
        position: index,
        ...(this.props.format ? {
          format: this.props.format
        } : {}),
        ...(this.props.section ? {
          section: this.props.section,
          section_position: this.props.section_position,
          is_section_followed: this.props.is_section_followed
        } : {})
      }
    })));
  }
}
const DSLinkMenu = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs
}))(_DSLinkMenu);
;// CONCATENATED MODULE: ./content-src/lib/utils.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */


/**
 * A custom react hook that sets up an IntersectionObserver to observe a single
 * or list of elements and triggers a callback when the element comes into the viewport
 * Note: The refs used should be an array type
 * @function useIntersectionObserver
 * @param {function} callback - The function to call when an element comes into the viewport
 * @param {Object} options - Options object passed to Intersection Observer:
 * https://developer.mozilla.org/en-US/docs/Web/API/IntersectionObserver/IntersectionObserver#options
 * @param {Boolean} [isSingle = false] Boolean if the elements are an array or single element
 *
 * @returns {React.MutableRefObject} a ref containing an array of elements or single element
 *
 *
 *
 */
function useIntersectionObserver(callback, threshold = 0.3) {
  const elementsRef = (0,external_React_namespaceObject.useRef)([]);
  (0,external_React_namespaceObject.useEffect)(() => {
    const observer = new IntersectionObserver(entries => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          callback(entry.target);
          observer.unobserve(entry.target);
        }
      });
    }, {
      threshold
    });
    elementsRef.current.forEach(el => {
      if (el) {
        observer.observe(el);
      }
    });

    // Cleanup function to disconnect observer on unmount
    return () => observer.disconnect();
  }, [callback, threshold]);
  return elementsRef;
}

/**
 * Determines the active card size ("small", "medium", or "large") based on the screen width
 * and class names applied to the card element at the time of an event (example: click)
 *
 * @param {number} screenWidth - The current window width (in pixels).
 * @param {string | string[]} classNames - A string or array of class names applied to the sections card.
 * @param {boolean[]} sectionsEnabled - If sections is not enabled, all cards are `medium-card`
 * @param {number} flightId - Error ege case: This function should not be called on spocs, which have flightId
 * @returns {"small-card" | "medium-card" | "large-card" | null} The active card type, or null if none is matched.
 */
function getActiveCardSize(screenWidth, classNames, sectionsEnabled, flightId) {
  // Only applies to sponsored content
  if (flightId) {
    return "spoc";
  }

  // Default layout only supports `medium-card`
  if (!sectionsEnabled) {
    // Missing arguments
    return "medium-card";
  }

  // Return null if no values are available
  if (!screenWidth || !classNames) {
    // Missing arguments
    return null;
  }
  const classList = classNames.split(" ");

  // Each breakpoint corresponds to a minimum screen width and its associated column class
  const breakpoints = [{
    min: 1374,
    column: "col-4"
  },
  // $break-point-sections-variant
  {
    min: 1122,
    column: "col-3"
  },
  // $break-point-widest
  {
    min: 724,
    column: "col-2"
  },
  // $break-point-layout-variant
  {
    min: 0,
    column: "col-1"
  } // (default layout)
  ];
  const cardTypes = ["small", "medium", "large"];

  // Determine which column is active based on the current screen width
  const currColumnCount = breakpoints.find(bp => screenWidth >= bp.min).column;

  // Match the card type for that column count
  for (let type of cardTypes) {
    const className = `${currColumnCount}-${type}`;
    if (classList.includes(className)) {
      // Special case: below $break-point-medium (610px), report `col-1-small` as medium
      if (screenWidth < 610 && currColumnCount === "col-1" && type === "small") {
        return "medium-card";
      }
      // Will be either "small-card", "medium-card", or "large-card"
      return `${type}-card`;
    }
  }
  return null;
}

;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSitesConstants.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const TOP_SITES_SOURCE = "TOP_SITES";
const TOP_SITES_CONTEXT_MENU_OPTIONS = [
  "CheckPinTopSite",
  "EditTopSite",
  "Separator",
  "OpenInNewWindow",
  "OpenInPrivateWindow",
  "Separator",
  "BlockUrl",
  "DeleteUrl",
];
const TOP_SITES_SPOC_CONTEXT_MENU_OPTIONS = [
  "OpenInNewWindow",
  "OpenInPrivateWindow",
  "Separator",
  "BlockUrl",
  "ShowPrivacyInfo",
];
const TOP_SITES_SPONSORED_POSITION_CONTEXT_MENU_OPTIONS = [
  "OpenInNewWindow",
  "OpenInPrivateWindow",
  "Separator",
  "BlockUrl",
  "AboutSponsored",
];
// the special top site for search shortcut experiment can only have the option to unpin (which removes) the topsite
const TOP_SITES_SEARCH_SHORTCUTS_CONTEXT_MENU_OPTIONS = [
  "CheckPinTopSite",
  "Separator",
  "BlockUrl",
];
// minimum size necessary to show a rich icon instead of a screenshot
const MIN_RICH_FAVICON_SIZE = 96;
// minimum size necessary to show any icon
const MIN_SMALL_FAVICON_SIZE = 16;

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamImpressionStats/ImpressionStats.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





const VISIBLE = "visible";
const VISIBILITY_CHANGE_EVENT = "visibilitychange";

// Per analytical requirement, we set the minimal intersection ratio to
// 0.5, and an impression is identified when the wrapped item has at least
// 50% visibility.
//
// This constant is exported for unit test
const INTERSECTION_RATIO = 0.5;

/**
 * Impression wrapper for Discovery Stream related React components.
 *
 * It makes use of the Intersection Observer API to detect the visibility,
 * and relies on page visibility to ensure the impression is reported
 * only when the component is visible on the page.
 *
 * Note:
 *   * This wrapper used to be used either at the individual card level,
 *     or by the card container components.
 *     It is now only used for individual card level.
 *   * Each impression will be sent only once as soon as the desired
 *     visibility is detected
 *   * Batching is not yet implemented, hence it might send multiple
 *     impression pings separately
 */
class ImpressionStats_ImpressionStats extends (external_React_default()).PureComponent {
  // This checks if the given cards are the same as those in the last impression ping.
  // If so, it should not send the same impression ping again.
  _needsImpressionStats(cards) {
    if (!this.impressionCardGuids || this.impressionCardGuids.length !== cards.length) {
      return true;
    }
    for (let i = 0; i < cards.length; i++) {
      if (cards[i].id !== this.impressionCardGuids[i]) {
        return true;
      }
    }
    return false;
  }
  _dispatchImpressionStats() {
    const {
      props
    } = this;
    const {
      isFakespot
    } = props;
    const cards = props.rows;
    if (this.props.flightId) {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.DISCOVERY_STREAM_SPOC_IMPRESSION,
        data: {
          flightId: this.props.flightId
        }
      }));

      // Record sponsored topsites impressions if the source is `TOP_SITES_SOURCE`.
      if (this.props.source === TOP_SITES_SOURCE) {
        for (const card of cards) {
          this.props.dispatch(actionCreators.OnlyToMain({
            type: actionTypes.TOP_SITES_SPONSORED_IMPRESSION_STATS,
            data: {
              type: "impression",
              tile_id: card.id,
              source: "newtab",
              advertiser: card.advertiser,
              // Keep the 0-based position, can be adjusted by the telemetry
              // sender if necessary.
              position: card.pos
            }
          }));
        }
      }
    }
    if (this._needsImpressionStats(cards)) {
      if (isFakespot) {
        props.dispatch(actionCreators.DiscoveryStreamImpressionStats({
          source: props.source.toUpperCase(),
          window_inner_width: window.innerWidth,
          window_inner_height: window.innerHeight,
          tiles: cards.map(link => ({
            id: link.id,
            type: "fakespot",
            category: link.category
          }))
        }));
      } else {
        props.dispatch(actionCreators.DiscoveryStreamImpressionStats({
          source: props.source.toUpperCase(),
          window_inner_width: window.innerWidth,
          window_inner_height: window.innerHeight,
          tiles: cards.map(link => ({
            id: link.id,
            pos: link.pos,
            type: props.flightId ? "spoc" : "organic",
            ...(link.shim ? {
              shim: link.shim
            } : {}),
            recommendation_id: link.recommendation_id,
            fetchTimestamp: link.fetchTimestamp,
            corpus_item_id: link.corpus_item_id,
            scheduled_corpus_item_id: link.scheduled_corpus_item_id,
            recommended_at: link.recommended_at,
            received_rank: link.received_rank,
            topic: link.topic,
            is_list_card: link.is_list_card,
            ...(link.format ? {
              format: link.format
            } : {
              format: getActiveCardSize(window.innerWidth, link.class_names, link.section, link.flightId)
            }),
            ...(link.section ? {
              section: link.section,
              section_position: link.section_position,
              is_section_followed: link.is_section_followed
            } : {})
          })),
          firstVisibleTimestamp: props.firstVisibleTimestamp
        }));
        this.impressionCardGuids = cards.map(link => link.id);
      }
    }
  }

  // This checks if the given cards are the same as those in the last loaded content ping.
  // If so, it should not send the same loaded content ping again.
  _needsLoadedContent(cards) {
    if (!this.loadedContentGuids || this.loadedContentGuids.length !== cards.length) {
      return true;
    }
    for (let i = 0; i < cards.length; i++) {
      if (cards[i].id !== this.loadedContentGuids[i]) {
        return true;
      }
    }
    return false;
  }
  _dispatchLoadedContent() {
    const {
      props
    } = this;
    const cards = props.rows;
    if (this._needsLoadedContent(cards)) {
      props.dispatch(actionCreators.DiscoveryStreamLoadedContent({
        source: props.source.toUpperCase(),
        tiles: cards.map(link => ({
          id: link.id,
          pos: link.pos
        }))
      }));
      this.loadedContentGuids = cards.map(link => link.id);
    }
  }
  setImpressionObserverOrAddListener() {
    const {
      props
    } = this;
    if (!props.dispatch) {
      return;
    }
    if (props.document.visibilityState === VISIBLE) {
      // Send the loaded content ping once the page is visible.
      this._dispatchLoadedContent();
      this.setImpressionObserver();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
      }
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === VISIBLE) {
          // Send the loaded content ping once the page is visible.
          this._dispatchLoadedContent();
          this.setImpressionObserver();
          props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
        }
      };
      props.document.addEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }

  /**
   * Set an impression observer for the wrapped component. It makes use of
   * the Intersection Observer API to detect if the wrapped component is
   * visible with a desired ratio, and only sends impression if that's the case.
   *
   * See more details about Intersection Observer API at:
   * https://developer.mozilla.org/en-US/docs/Web/API/Intersection_Observer_API
   */
  setImpressionObserver() {
    const {
      props
    } = this;
    if (!props.rows.length) {
      return;
    }
    this._handleIntersect = entries => {
      if (entries.some(entry => entry.isIntersecting && entry.intersectionRatio >= INTERSECTION_RATIO)) {
        this._dispatchImpressionStats();
        this.impressionObserver.unobserve(this.refs.impression);
      }
    };
    const options = {
      threshold: INTERSECTION_RATIO
    };
    this.impressionObserver = new props.IntersectionObserver(this._handleIntersect, options);
    this.impressionObserver.observe(this.refs.impression);
  }
  componentDidMount() {
    if (this.props.rows.length) {
      this.setImpressionObserverOrAddListener();
    }
  }
  componentWillUnmount() {
    if (this._handleIntersect && this.impressionObserver) {
      this.impressionObserver.unobserve(this.refs.impression);
    }
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      ref: "impression",
      className: "impression-observer"
    }, this.props.children);
  }
}
ImpressionStats_ImpressionStats.defaultProps = {
  IntersectionObserver: globalThis.IntersectionObserver,
  document: globalThis.document,
  rows: [],
  source: ""
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/SafeAnchor/SafeAnchor.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



class SafeAnchor extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onClick = this.onClick.bind(this);
  }
  onClick(event) {
    // Use dispatch instead of normal link click behavior to include referrer
    if (this.props.dispatch) {
      event.preventDefault();
      const {
        altKey,
        button,
        ctrlKey,
        metaKey,
        shiftKey
      } = event;
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.OPEN_LINK,
        data: {
          event: {
            altKey,
            button,
            ctrlKey,
            metaKey,
            shiftKey
          },
          referrer: this.props.referrer || "https://getpocket.com/recommendations",
          // Use the anchor's url, which could have been cleaned up
          url: event.currentTarget.href
        }
      }));
    }

    // Propagate event if there's a handler
    if (this.props.onLinkClick) {
      this.props.onLinkClick(event);
    }
  }
  safeURI(url) {
    let protocol = null;
    try {
      protocol = new URL(url).protocol;
    } catch (e) {
      return "";
    }
    const isAllowed = ["http:", "https:"].includes(protocol);
    if (!isAllowed) {
      console.warn(`${url} is not allowed for anchor targets.`); // eslint-disable-line no-console
      return "";
    }
    return url;
  }
  render() {
    const {
      url,
      className,
      title
    } = this.props;
    return /*#__PURE__*/external_React_default().createElement("a", {
      href: this.safeURI(url),
      title: title,
      className: className,
      onClick: this.onClick
    }, this.props.children);
  }
}
;// CONCATENATED MODULE: ./content-src/components/Card/types.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const cardContextTypes = {
  history: {
    fluentID: "newtab-label-visited",
    icon: "history-item",
  },
  removedBookmark: {
    fluentID: "newtab-label-removed-bookmark",
    icon: "bookmark-removed",
  },
  bookmark: {
    fluentID: "newtab-label-bookmarked",
    icon: "bookmark-added",
  },
  trending: {
    fluentID: "newtab-label-recommended",
    icon: "trending",
  },
  pocket: {
    fluentID: "newtab-label-saved",
    icon: "pocket",
  },
  download: {
    fluentID: "newtab-label-download",
    icon: "download",
  },
};

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/FeatureHighlight/FeatureHighlight.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



function FeatureHighlight({
  message,
  icon,
  toggle,
  position = "top-left",
  title,
  ariaLabel,
  feature = "FEATURE_HIGHLIGHT_DEFAULT",
  dispatch = () => {},
  windowObj = __webpack_require__.g,
  openedOverride = false,
  showButtonIcon = true,
  dismissCallback = () => {},
  outsideClickCallback = () => {}
}) {
  const [opened, setOpened] = (0,external_React_namespaceObject.useState)(openedOverride);
  const ref = (0,external_React_namespaceObject.useRef)(null);
  (0,external_React_namespaceObject.useEffect)(() => {
    const handleOutsideClick = e => {
      if (!ref?.current?.contains(e.target)) {
        setOpened(false);
        outsideClickCallback();
      }
    };
    const handleKeyDown = e => {
      if (e.key === "Escape") {
        outsideClickCallback();
      }
    };
    windowObj.document.addEventListener("click", handleOutsideClick);
    windowObj.document.addEventListener("keydown", handleKeyDown);
    return () => {
      windowObj.document.removeEventListener("click", handleOutsideClick);
      windowObj.document.removeEventListener("keydown", handleKeyDown);
    };
  }, [windowObj, outsideClickCallback]);
  const onToggleClick = (0,external_React_namespaceObject.useCallback)(() => {
    if (!opened) {
      dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        source: "FEATURE_HIGHLIGHT",
        value: {
          feature
        }
      }));
    }
    setOpened(!opened);
  }, [dispatch, feature, opened]);
  const onDismissClick = (0,external_React_namespaceObject.useCallback)(() => {
    setOpened(false);
    dismissCallback();
  }, [dismissCallback]);
  const hideButtonClass = showButtonIcon ? `` : `isHidden`;
  const openedClassname = opened ? `opened` : `closed`;
  return /*#__PURE__*/external_React_default().createElement("div", {
    ref: ref,
    className: "feature-highlight"
  }, /*#__PURE__*/external_React_default().createElement("button", {
    title: title,
    "aria-haspopup": "true",
    "aria-label": ariaLabel,
    className: `toggle-button ${hideButtonClass}`,
    onClick: onToggleClick
  }, toggle), /*#__PURE__*/external_React_default().createElement("div", {
    className: `feature-highlight-modal ${position} ${openedClassname}`
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "message-icon"
  }, icon), /*#__PURE__*/external_React_default().createElement("p", {
    className: "content-wrapper"
  }, message), /*#__PURE__*/external_React_default().createElement("button", {
    "data-l10n-id": "feature-highlight-dismiss-button",
    className: "icon icon-dismiss",
    onClick: onDismissClick
  })));
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/FeatureHighlight/SponsoredContentHighlight.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




function SponsoredContentHighlight({
  position,
  dispatch
}) {
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "sponsored-content-highlight"
  }, /*#__PURE__*/external_React_default().createElement(FeatureHighlight, {
    position: position,
    ariaLabel: "Sponsored content supports our mission to build a better web.",
    title: "Sponsored content more info",
    feature: "SPONSORED_CONTENT_INFO",
    dispatch: dispatch,
    message: /*#__PURE__*/external_React_default().createElement("span", null, "Sponsored content supports our mission to build a better web.", " ", /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      dispatch: dispatch,
      url: "https://support.mozilla.org/kb/pocket-sponsored-stories-new-tabs"
    }, "Find out how")),
    icon: /*#__PURE__*/external_React_default().createElement("div", {
      className: "sponsored-message-icon"
    }),
    toggle: /*#__PURE__*/external_React_default().createElement("div", {
      className: "icon icon-help"
    })
  }));
}
;// CONCATENATED MODULE: external "ReactTransitionGroup"
const external_ReactTransitionGroup_namespaceObject = ReactTransitionGroup;
;// CONCATENATED MODULE: ./content-src/components/FluentOrText/FluentOrText.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



/**
 * Set text on a child element/component depending on if the message is already
 * translated plain text or a fluent id with optional args.
 */
class FluentOrText extends (external_React_default()).PureComponent {
  render() {
    // Ensure we have a single child to attach attributes
    const {
      children,
      message
    } = this.props;
    const child = children ? external_React_default().Children.only(children) : /*#__PURE__*/external_React_default().createElement("span", null);

    // For a string message, just use it as the child's text
    let grandChildren = message;
    let extraProps;

    // Convert a message object to set desired fluent-dom attributes
    if (typeof message === "object") {
      const args = message.args || message.values;
      extraProps = {
        "data-l10n-args": args && JSON.stringify(args),
        "data-l10n-id": message.id || message.string_id
      };

      // Use original children potentially with data-l10n-name attributes
      grandChildren = child.props.children;
    }

    // Add the message to the child via fluent attributes or text node
    return /*#__PURE__*/external_React_default().cloneElement(child, extraProps, grandChildren);
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSContextFooter/DSContextFooter.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



// eslint-disable-next-line no-shadow




// Animation time is mirrored in DSContextFooter.scss
const ANIMATION_DURATION = 3000;
const DSMessageLabel = props => {
  const {
    context,
    context_type,
    mayHaveSectionsCards
  } = props;
  const {
    icon,
    fluentID
  } = cardContextTypes[context_type] || {};
  if (!context && context_type && !mayHaveSectionsCards) {
    return /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.TransitionGroup, {
      component: null
    }, /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.CSSTransition, {
      key: fluentID,
      timeout: ANIMATION_DURATION,
      classNames: "story-animate"
    }, /*#__PURE__*/external_React_default().createElement(StatusMessage, {
      icon: icon,
      fluentID: fluentID
    })));
  }
  return null;
};
const StatusMessage = ({
  icon,
  fluentID
}) => /*#__PURE__*/external_React_default().createElement("div", {
  className: "status-message"
}, /*#__PURE__*/external_React_default().createElement("span", {
  "aria-haspopup": "true",
  className: `story-badge-icon icon icon-${icon}`
}), /*#__PURE__*/external_React_default().createElement("div", {
  className: "story-context-label",
  "data-l10n-id": fluentID
}));
const SponsorLabel = ({
  sponsored_by_override,
  sponsor,
  context,
  newSponsoredLabel
}) => {
  const classList = `story-sponsored-label ${newSponsoredLabel || ""} clamp`;
  // If override is not false or an empty string.
  if (sponsored_by_override) {
    return /*#__PURE__*/external_React_default().createElement("p", {
      className: classList
    }, sponsored_by_override);
  } else if (sponsored_by_override === "") {
    // We specifically want to display nothing if the server returns an empty string.
    // So the server can turn off the label.
    // This is to support the use cases where the sponsored context is displayed elsewhere.
    return null;
  } else if (sponsor) {
    return /*#__PURE__*/external_React_default().createElement("p", {
      className: classList
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: {
        id: `newtab-label-sponsored-by`,
        values: {
          sponsor
        }
      }
    }));
  } else if (context) {
    return /*#__PURE__*/external_React_default().createElement("p", {
      className: classList
    }, context);
  }
  return null;
};
class DSContextFooter extends (external_React_default()).PureComponent {
  render() {
    const {
      context,
      context_type,
      sponsor,
      sponsored_by_override,
      cta_button_variant,
      source,
      spocMessageVariant,
      dispatch,
      mayHaveSectionsCards
    } = this.props;
    const sponsorLabel = SponsorLabel({
      sponsored_by_override,
      sponsor,
      context
    });
    const dsMessageLabel = DSMessageLabel({
      context,
      context_type,
      mayHaveSectionsCards
    });
    if (cta_button_variant === "variant-a") {
      return /*#__PURE__*/external_React_default().createElement("div", {
        className: "story-footer"
      }, /*#__PURE__*/external_React_default().createElement("button", {
        "aria-hidden": "true",
        className: "story-cta-button"
      }, "Shop Now"), sponsorLabel);
    }
    if (cta_button_variant === "variant-b") {
      return /*#__PURE__*/external_React_default().createElement("div", {
        className: "story-footer"
      }, sponsorLabel, /*#__PURE__*/external_React_default().createElement("span", {
        className: "source clamp cta-footer-source"
      }, source));
    }
    if (sponsorLabel || dsMessageLabel && context_type !== "pocket") {
      return /*#__PURE__*/external_React_default().createElement("div", {
        className: "story-footer"
      }, sponsorLabel, sponsorLabel && spocMessageVariant === "variant-b" && /*#__PURE__*/external_React_default().createElement(SponsoredContentHighlight, {
        dispatch: dispatch,
        position: "inset-block-end inset-inline-start"
      }), dsMessageLabel);
    }
    return null;
  }
}
const DSMessageFooter = props => {
  const {
    context,
    context_type,
    saveToPocketCard
  } = props;
  const dsMessageLabel = DSMessageLabel({
    context,
    context_type
  });

  // This case is specific and already displayed to the user elsewhere.
  if (!dsMessageLabel || saveToPocketCard && context_type === "pocket") {
    return null;
  }
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "story-footer"
  }, dsMessageLabel);
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSThumbsUpDownButtons/DSThumbsUpDownButtons.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function DSThumbsUpDownButtons({
  sponsor,
  onThumbsUpClick,
  onThumbsDownClick,
  isThumbsUpActive,
  isThumbsDownActive
}) {
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "card-stp-thumbs-buttons-wrapper"
  }, !sponsor && /*#__PURE__*/external_React_default().createElement("div", {
    className: "card-stp-thumbs-buttons"
  }, /*#__PURE__*/external_React_default().createElement("button", {
    onClick: onThumbsUpClick,
    className: `card-stp-thumbs-button icon icon-thumbs-up ${isThumbsUpActive ? "is-active" : null}`,
    "data-l10n-id": "newtab-pocket-thumbs-up-tooltip"
  }), /*#__PURE__*/external_React_default().createElement("button", {
    onClick: onThumbsDownClick,
    className: `card-stp-thumbs-button icon icon-thumbs-down ${isThumbsDownActive ? "is-active" : null}`,
    "data-l10n-id": "newtab-pocket-thumbs-down-tooltip"
  })));
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSCard/DSCard.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */













const READING_WPM = 220;

/**
 * READ TIME FROM WORD COUNT
 * @param {int} wordCount number of words in an article
 * @returns {int} number of words per minute in minutes
 */
function readTimeFromWordCount(wordCount) {
  if (!wordCount) {
    return false;
  }
  return Math.ceil(parseInt(wordCount, 10) / READING_WPM);
}
const DSSource = ({
  source,
  timeToRead,
  newSponsoredLabel,
  context,
  sponsor,
  sponsored_by_override,
  icon_src
}) => {
  // First try to display sponsored label or time to read here.
  if (newSponsoredLabel) {
    // If we can display something for spocs, do so.
    if (sponsored_by_override || sponsor || context) {
      return /*#__PURE__*/external_React_default().createElement(SponsorLabel, {
        context: context,
        sponsor: sponsor,
        sponsored_by_override: sponsored_by_override,
        newSponsoredLabel: "new-sponsored-label"
      });
    }
  }

  // If we are not a spoc, and can display a time to read value.
  if (source && timeToRead) {
    return /*#__PURE__*/external_React_default().createElement("p", {
      className: "source clamp time-to-read"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: {
        id: `newtab-label-source-read-time`,
        values: {
          source,
          timeToRead
        }
      }
    }));
  }

  // Otherwise display a default source.
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "source-wrapper"
  }, icon_src && /*#__PURE__*/external_React_default().createElement("img", {
    src: icon_src,
    height: "16",
    width: "16",
    alt: ""
  }), /*#__PURE__*/external_React_default().createElement("p", {
    className: "source clamp"
  }, source));
};
const DefaultMeta = ({
  source,
  title,
  excerpt,
  timeToRead,
  newSponsoredLabel,
  context,
  context_type,
  sponsor,
  sponsored_by_override,
  saveToPocketCard,
  ctaButtonVariant,
  dispatch,
  spocMessageVariant,
  mayHaveSectionsCards,
  mayHaveThumbsUpDown,
  onThumbsUpClick,
  onThumbsDownClick,
  isListCard,
  state,
  format,
  topic,
  isSectionsCard,
  showTopics,
  icon_src
}) => {
  const shouldHaveThumbs = !isListCard && format !== "rectangle" && mayHaveSectionsCards && mayHaveThumbsUpDown;
  const shouldHaveFooterSection = isSectionsCard && (shouldHaveThumbs || showTopics);
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "meta"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "info-wrap"
  }, ctaButtonVariant !== "variant-b" && format !== "rectangle" && /*#__PURE__*/external_React_default().createElement(DSSource, {
    source: source,
    timeToRead: timeToRead,
    newSponsoredLabel: newSponsoredLabel,
    context: context,
    sponsor: sponsor,
    sponsored_by_override: sponsored_by_override,
    icon_src: icon_src
  }), format !== "rectangle" && /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h3", {
    className: "title clamp"
  }, title), excerpt && /*#__PURE__*/external_React_default().createElement("p", {
    className: "excerpt clamp"
  }, excerpt)), format === "rectangle" && /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h3", {
    className: "title clamp"
  }, "Sponsored"), /*#__PURE__*/external_React_default().createElement("p", {
    className: "excerpt clamp"
  }, "Sponsored content supports our mission to build a better web."))), !isListCard && format !== "rectangle" && !mayHaveSectionsCards && mayHaveThumbsUpDown && /*#__PURE__*/external_React_default().createElement(DSThumbsUpDownButtons, {
    onThumbsDownClick: onThumbsDownClick,
    onThumbsUpClick: onThumbsUpClick,
    sponsor: sponsor,
    isThumbsDownActive: state.isThumbsDownActive,
    isThumbsUpActive: state.isThumbsUpActive
  }), shouldHaveFooterSection && /*#__PURE__*/external_React_default().createElement("div", {
    className: "sections-card-footer"
  }, shouldHaveThumbs && /*#__PURE__*/external_React_default().createElement(DSThumbsUpDownButtons, {
    onThumbsDownClick: onThumbsDownClick,
    onThumbsUpClick: onThumbsUpClick,
    sponsor: sponsor,
    isThumbsDownActive: state.isThumbsDownActive,
    isThumbsUpActive: state.isThumbsUpActive
  }), showTopics && /*#__PURE__*/external_React_default().createElement("span", {
    className: "ds-card-topic",
    "data-l10n-id": `newtab-topic-label-${topic}`
  })), !newSponsoredLabel && /*#__PURE__*/external_React_default().createElement(DSContextFooter, {
    context_type: context_type,
    context: context,
    sponsor: sponsor,
    sponsored_by_override: sponsored_by_override,
    cta_button_variant: ctaButtonVariant,
    source: source,
    dispatch: dispatch,
    spocMessageVariant: spocMessageVariant,
    mayHaveSectionsCards: mayHaveSectionsCards
  }), newSponsoredLabel && /*#__PURE__*/external_React_default().createElement(DSMessageFooter, {
    context_type: context_type,
    context: null,
    saveToPocketCard: saveToPocketCard
  }));
};
class _DSCard extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onLinkClick = this.onLinkClick.bind(this);
    this.doesLinkTopicMatchSelectedTopic = this.doesLinkTopicMatchSelectedTopic.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.onMenuShow = this.onMenuShow.bind(this);
    this.onThumbsUpClick = this.onThumbsUpClick.bind(this);
    this.onThumbsDownClick = this.onThumbsDownClick.bind(this);
    this.setContextMenuButtonHostRef = element => {
      this.contextMenuButtonHostElement = element;
    };
    this.setPlaceholderRef = element => {
      this.placeholderElement = element;
    };
    this.state = {
      isSeen: false,
      isThumbsUpActive: false,
      isThumbsDownActive: false
    };

    // If this is for the about:home startup cache, then we always want
    // to render the DSCard, regardless of whether or not its been seen.
    if (props.App.isForStartupCache) {
      this.state.isSeen = true;
    }

    // We want to choose the optimal thumbnail for the underlying DSImage, but
    // want to do it in a performant way. The breakpoints used in the
    // CSS of the page are, unfortuntely, not easy to retrieve without
    // causing a style flush. To avoid that, we hardcode them here.
    //
    // The values chosen here were the dimensions of the card thumbnails as
    // computed by getBoundingClientRect() for each type of viewport width
    // across both high-density and normal-density displays.
    this.dsImageSizes = [{
      mediaMatcher: "(min-width: 1122px)",
      width: 296,
      height: 148
    }, {
      mediaMatcher: "(min-width: 866px)",
      width: 218,
      height: 109
    }, {
      mediaMatcher: "(max-width: 610px)",
      width: 202,
      height: 101
    }];
    this.standardCardImageSizes = [{
      mediaMatcher: "default",
      width: 296,
      height: 148
    }];
    this.listCardImageSizes = [{
      mediaMatcher: "(min-width: 1122px)",
      width: 75,
      height: 75
    }, {
      mediaMatcher: "default",
      width: 50,
      height: 50
    }];
    this.sectionsCardImagesSizes = {
      small: {
        width: 100,
        height: 120
      },
      medium: {
        width: 300,
        height: 150
      },
      large: {
        width: 265,
        height: 265
      }
    };
    this.sectionsColumnMediaMatcher = {
      1: "default",
      2: "(min-width: 724px)",
      3: "(min-width: 1122px)",
      4: "(min-width: 1390px)"
    };
  }
  getSectionImageSize(column, size) {
    const cardImageSize = {
      mediaMatcher: this.sectionsColumnMediaMatcher[column],
      width: this.sectionsCardImagesSizes[size].width,
      height: this.sectionsCardImagesSizes[size].height
    };
    return cardImageSize;
  }
  doesLinkTopicMatchSelectedTopic() {
    // Edge case for clicking on a card when topic selections have not be set
    if (!this.props.selectedTopics) {
      return "not-set";
    }

    // Edge case the topic of the card is not one of the available topics
    if (!this.props.availableTopics.includes(this.props.topic)) {
      return "topic-not-selectable";
    }
    if (this.props.selectedTopics.includes(this.props.topic)) {
      return "true";
    }
    return "false";
  }
  onLinkClick() {
    const matchesSelectedTopic = this.doesLinkTopicMatchSelectedTopic();
    if (this.props.dispatch) {
      if (this.props.isFakespot) {
        this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
          event: "FAKESPOT_CLICK",
          value: {
            product_id: this.props.id,
            category: this.props.category || ""
          }
        }));
      } else {
        this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
          event: "CLICK",
          source: this.props.type.toUpperCase(),
          action_position: this.props.pos,
          value: {
            card_type: this.props.flightId ? "spoc" : "organic",
            recommendation_id: this.props.recommendation_id,
            tile_id: this.props.id,
            ...(this.props.shim && this.props.shim.click ? {
              shim: this.props.shim.click
            } : {}),
            fetchTimestamp: this.props.fetchTimestamp,
            firstVisibleTimestamp: this.props.firstVisibleTimestamp,
            corpus_item_id: this.props.corpus_item_id,
            scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
            recommended_at: this.props.recommended_at,
            received_rank: this.props.received_rank,
            topic: this.props.topic,
            matches_selected_topic: matchesSelectedTopic,
            selected_topics: this.props.selectedTopics,
            is_list_card: this.props.isListCard,
            ...(this.props.format ? {
              format: this.props.format
            } : {
              format: getActiveCardSize(window.innerWidth, this.props.sectionsClassNames, this.props.section, this.props.flightId)
            }),
            ...(this.props.section ? {
              section: this.props.section,
              section_position: this.props.sectionPosition,
              is_section_followed: this.props.sectionFollowed
            } : {})
          }
        }));
        this.props.dispatch(actionCreators.ImpressionStats({
          source: this.props.type.toUpperCase(),
          click: 0,
          window_inner_width: this.props.windowObj.innerWidth,
          window_inner_height: this.props.windowObj.innerHeight,
          tiles: [{
            id: this.props.id,
            pos: this.props.pos,
            ...(this.props.shim && this.props.shim.click ? {
              shim: this.props.shim.click
            } : {}),
            type: this.props.flightId ? "spoc" : "organic",
            recommendation_id: this.props.recommendation_id,
            topic: this.props.topic,
            selected_topics: this.props.selectedTopics,
            is_list_card: this.props.isListCard,
            ...(this.props.format ? {
              format: this.props.format
            } : {
              format: getActiveCardSize(window.innerWidth, this.props.sectionsClassNames, this.props.section, this.props.flightId)
            }),
            ...(this.props.section ? {
              section: this.props.section,
              section_position: this.props.sectionPosition,
              is_section_followed: this.props.sectionFollowed
            } : {})
          }]
        }));
      }
    }
  }
  onThumbsUpClick(event) {
    event.stopPropagation();
    event.preventDefault();

    // Toggle active state for thumbs up button to show CSS animation
    const currentState = this.state.isThumbsUpActive;

    // If thumbs up has been clicked already, do nothing.
    if (currentState) {
      return;
    }
    this.setState({
      isThumbsUpActive: !currentState
    });

    // Record thumbs up telemetry event
    this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "POCKET_THUMBS_UP",
      source: "THUMBS_UI",
      value: {
        recommendation_id: this.props.recommendation_id,
        tile_id: this.props.id,
        corpus_item_id: this.props.corpus_item_id,
        scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
        recommended_at: this.props.recommended_at,
        received_rank: this.props.received_rank,
        thumbs_up: true,
        thumbs_down: false,
        topic: this.props.topic,
        format: getActiveCardSize(window.innerWidth, this.props.sectionsClassNames, this.props.section, false // (thumbs up/down only exist on organic content)
        ),
        ...(this.props.section ? {
          section: this.props.section,
          section_position: this.props.sectionPosition,
          is_section_followed: this.props.sectionFollowed
        } : {})
      }
    }));

    // Show Toast
    this.props.dispatch(actionCreators.OnlyToOneContent({
      type: actionTypes.SHOW_TOAST_MESSAGE,
      data: {
        showNotifications: true,
        toastId: "thumbsUpToast"
      }
    }, "ActivityStream:Content"));
  }
  onThumbsDownClick(event) {
    event.stopPropagation();
    event.preventDefault();

    // Toggle active state for thumbs down button to show CSS animation
    const currentState = this.state.isThumbsDownActive;
    this.setState({
      isThumbsDownActive: !currentState
    });

    // Run dismiss event after 0.5 second delay
    if (this.props.dispatch && this.props.type && this.props.id && this.props.url) {
      const index = this.props.pos;
      const source = this.props.type.toUpperCase();
      const spocData = {
        url: this.props.url,
        guid: this.props.id,
        type: "CardGrid",
        card_type: "organic",
        recommendation_id: this.props.recommendation_id,
        tile_id: this.props.id,
        corpus_item_id: this.props.corpus_item_id,
        scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
        recommended_at: this.props.recommended_at,
        received_rank: this.props.received_rank
      };
      const blockUrlOption = LinkMenuOptions.BlockUrl(spocData, index, source);
      const {
        action,
        impression,
        userEvent
      } = blockUrlOption;
      setTimeout(() => {
        this.props.dispatch(action);
        this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
          event: userEvent,
          source,
          action_position: index
        }));
      }, 500);
      if (impression) {
        this.props.dispatch(impression);
      }

      // Record thumbs down telemetry event
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "POCKET_THUMBS_DOWN",
        source: "THUMBS_UI",
        value: {
          recommendation_id: this.props.recommendation_id,
          tile_id: this.props.id,
          corpus_item_id: this.props.corpus_item_id,
          scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
          recommended_at: this.props.recommended_at,
          received_rank: this.props.received_rank,
          thumbs_up: false,
          thumbs_down: true,
          topic: this.props.topic,
          format: getActiveCardSize(window.innerWidth, this.props.sectionsClassNames, this.props.section, false // (thumbs up/down only exist on organic content)
          ),
          ...(this.props.section ? {
            section: this.props.section,
            section_position: this.props.sectionPosition,
            is_section_followed: this.props.sectionFollowed
          } : {})
        }
      }));

      // Show Toast
      this.props.dispatch(actionCreators.OnlyToOneContent({
        type: actionTypes.SHOW_TOAST_MESSAGE,
        data: {
          showNotifications: true,
          toastId: "thumbsDownToast"
        }
      }, "ActivityStream:Content"));
    }
  }
  onMenuUpdate(showContextMenu) {
    if (!showContextMenu) {
      const dsLinkMenuHostDiv = this.contextMenuButtonHostElement;
      if (dsLinkMenuHostDiv) {
        dsLinkMenuHostDiv.classList.remove("active", "last-item");
      }
    }
  }
  async onMenuShow() {
    const dsLinkMenuHostDiv = this.contextMenuButtonHostElement;
    if (dsLinkMenuHostDiv) {
      // Force translation so we can be sure it's ready before measuring.
      await this.props.windowObj.document.l10n.translateFragment(dsLinkMenuHostDiv);
      if (this.props.windowObj.scrollMaxX > 0) {
        dsLinkMenuHostDiv.classList.add("last-item");
      }
      dsLinkMenuHostDiv.classList.add("active");
    }
  }
  onSeen(entries) {
    if (this.state) {
      const entry = entries.find(e => e.isIntersecting);
      if (entry) {
        if (this.placeholderElement) {
          this.observer.unobserve(this.placeholderElement);
        }

        // Stop observing since element has been seen
        this.setState({
          isSeen: true
        });
      }
    }
  }
  onIdleCallback() {
    if (!this.state.isSeen) {
      if (this.observer && this.placeholderElement) {
        this.observer.unobserve(this.placeholderElement);
      }
      this.setState({
        isSeen: true
      });
    }
  }
  componentDidMount() {
    this.idleCallbackId = this.props.windowObj.requestIdleCallback(this.onIdleCallback.bind(this));
    if (this.placeholderElement) {
      this.observer = new IntersectionObserver(this.onSeen.bind(this));
      this.observer.observe(this.placeholderElement);
    }
  }
  componentWillUnmount() {
    // Remove observer on unmount
    if (this.observer && this.placeholderElement) {
      this.observer.unobserve(this.placeholderElement);
    }
    if (this.idleCallbackId) {
      this.props.windowObj.cancelIdleCallback(this.idleCallbackId);
    }
  }
  render() {
    const {
      isRecentSave,
      DiscoveryStream,
      Prefs,
      saveToPocketCard,
      isListCard,
      isFakespot,
      mayHaveSectionsCards,
      format,
      alt_text
    } = this.props;
    if (this.props.placeholder || !this.state.isSeen) {
      // placeholder-seen is used to ensure the loading animation is only used if the card is visible.
      const placeholderClassName = this.state.isSeen ? `placeholder-seen` : ``;
      return /*#__PURE__*/external_React_default().createElement("div", {
        className: `ds-card placeholder ${placeholderClassName} ${isListCard ? "list-card-placeholder" : ""}`,
        ref: this.setPlaceholderRef
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "placeholder-image placeholder-fill"
      }), /*#__PURE__*/external_React_default().createElement("div", {
        className: "placeholder-label placeholder-fill"
      }), /*#__PURE__*/external_React_default().createElement("div", {
        className: "placeholder-header placeholder-fill"
      }), /*#__PURE__*/external_React_default().createElement("div", {
        className: "placeholder-description placeholder-fill"
      }));
    }
    let source = this.props.source || this.props.publisher;
    if (!source) {
      try {
        source = new URL(this.props.url).hostname;
      } catch (e) {}
    }
    const {
      pocketButtonEnabled,
      hideDescriptions,
      compactImages,
      imageGradient,
      newSponsoredLabel,
      titleLines = 3,
      descLines = 3,
      readTime: displayReadTime
    } = DiscoveryStream;
    const layoutsVariantAEnabled = Prefs.values["newtabLayouts.variant-a"];
    const layoutsVariantBEnabled = Prefs.values["newtabLayouts.variant-b"];
    const sectionsEnabled = Prefs.values["discoverystream.sections.enabled"];
    const layoutsVariantAorB = layoutsVariantAEnabled || layoutsVariantBEnabled;
    const smartCrop = Prefs.values["images.smart"];
    const faviconEnabled = Prefs.values["discoverystream.publisherFavicon.enabled"];
    const excerpt = !hideDescriptions ? this.props.excerpt : "";
    let timeToRead;
    if (displayReadTime) {
      timeToRead = this.props.time_to_read || readTimeFromWordCount(this.props.word_count);
    }
    const ctaButtonEnabled = this.props.ctaButtonSponsors?.includes(this.props.sponsor?.toLowerCase());
    let ctaButtonVariant = "";
    if (ctaButtonEnabled) {
      ctaButtonVariant = this.props.ctaButtonVariant;
    }
    let ctaButtonVariantClassName = ctaButtonVariant;
    const ctaButtonClassName = ctaButtonEnabled ? `ds-card-cta-button` : ``;
    const compactImagesClassName = compactImages ? `ds-card-compact-image` : ``;
    const imageGradientClassName = imageGradient ? `ds-card-image-gradient` : ``;
    const listCardClassName = isListCard ? `list-feed-card` : ``;
    const fakespotClassName = isFakespot ? `fakespot` : ``;
    const sectionsCardsClassName = [mayHaveSectionsCards ? `sections-card-ui` : ``, this.props.sectionsClassNames].join(" ");
    const sectionsCardsImageSizes = this.props.sectionsCardImageSizes;
    const titleLinesName = `ds-card-title-lines-${titleLines}`;
    const descLinesClassName = `ds-card-desc-lines-${descLines}`;
    const isMediumRectangle = format === "rectangle";
    const spocFormatClassName = isMediumRectangle ? `ds-spoc-rectangle` : ``;
    let sizes = [];
    if (!isMediumRectangle) {
      sizes = this.dsImageSizes;
      if (sectionsEnabled) {
        sizes = [this.getSectionImageSize("4", sectionsCardsImageSizes["4"]), this.getSectionImageSize("3", sectionsCardsImageSizes["3"]), this.getSectionImageSize("2", sectionsCardsImageSizes["2"]), this.getSectionImageSize("1", sectionsCardsImageSizes["1"])];
      } else if (layoutsVariantAorB) {
        sizes = this.standardCardImageSizes;
      }
      if (isListCard) {
        sizes = this.listCardImageSizes;
      }
    }
    return /*#__PURE__*/external_React_default().createElement("article", {
      className: `ds-card ${listCardClassName} ${fakespotClassName} ${sectionsCardsClassName} ${compactImagesClassName} ${imageGradientClassName} ${titleLinesName} ${descLinesClassName} ${spocFormatClassName} ${ctaButtonClassName} ${ctaButtonVariantClassName}`,
      ref: this.setContextMenuButtonHostRef,
      "data-position-one": this.props["data-position-one"],
      "data-position-two": this.props["data-position-one"],
      "data-position-three": this.props["data-position-one"],
      "data-position-four": this.props["data-position-one"]
    }, /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      className: "ds-card-link",
      dispatch: this.props.dispatch,
      onLinkClick: !this.props.placeholder ? this.onLinkClick : undefined,
      url: this.props.url,
      title: this.props.title
    }, this.props.showTopics && !this.props.mayHaveSectionsCards && this.props.topic && !isListCard && /*#__PURE__*/external_React_default().createElement("span", {
      className: "ds-card-topic",
      "data-l10n-id": `newtab-topic-label-${this.props.topic}`
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "img-wrapper"
    }, /*#__PURE__*/external_React_default().createElement(DSImage, {
      extraClassNames: "img",
      source: this.props.image_src,
      rawSource: this.props.raw_image_src,
      sizes: sizes,
      url: this.props.url,
      title: this.props.title,
      isRecentSave: isRecentSave,
      alt_text: alt_text,
      smartCrop: smartCrop
    })), /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
      flightId: this.props.flightId,
      rows: [{
        id: this.props.id,
        pos: this.props.pos,
        ...(this.props.shim && this.props.shim.impression ? {
          shim: this.props.shim.impression
        } : {}),
        recommendation_id: this.props.recommendation_id,
        fetchTimestamp: this.props.fetchTimestamp,
        corpus_item_id: this.props.corpus_item_id,
        scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
        recommended_at: this.props.recommended_at,
        received_rank: this.props.received_rank,
        topic: this.props.topic,
        is_list_card: isListCard,
        ...(format ? {
          format
        } : {}),
        isFakespot,
        category: this.props.category,
        ...(this.props.section ? {
          section: this.props.section,
          section_position: this.props.sectionPosition,
          is_section_followed: this.props.sectionFollowed
        } : {}),
        ...(!format && this.props.section ?
        // Note: sectionsCardsClassName is passed to ImpressionStats.jsx in order to calculate format
        {
          class_names: sectionsCardsClassName
        } : {})
      }],
      dispatch: this.props.dispatch,
      isFakespot: isFakespot,
      source: this.props.type,
      firstVisibleTimestamp: this.props.firstVisibleTimestamp
    }), ctaButtonVariant === "variant-b" && /*#__PURE__*/external_React_default().createElement("div", {
      className: "cta-header"
    }, "Shop Now"), isFakespot ? /*#__PURE__*/external_React_default().createElement("div", {
      className: "meta"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "info-wrap"
    }, /*#__PURE__*/external_React_default().createElement("h3", {
      className: "title clamp"
    }, this.props.title))) : /*#__PURE__*/external_React_default().createElement(DefaultMeta, {
      source: source,
      title: this.props.title,
      excerpt: excerpt,
      newSponsoredLabel: newSponsoredLabel,
      timeToRead: timeToRead,
      context: this.props.context,
      context_type: this.props.context_type,
      sponsor: this.props.sponsor,
      sponsored_by_override: this.props.sponsored_by_override,
      saveToPocketCard: saveToPocketCard,
      ctaButtonVariant: ctaButtonVariant,
      dispatch: this.props.dispatch,
      spocMessageVariant: this.props.spocMessageVariant,
      mayHaveThumbsUpDown: this.props.mayHaveThumbsUpDown,
      mayHaveSectionsCards: this.props.mayHaveSectionsCards,
      onThumbsUpClick: this.onThumbsUpClick,
      onThumbsDownClick: this.onThumbsDownClick,
      state: this.state,
      isListCard: isListCard,
      showTopics: this.props.showTopics,
      isSectionsCard: this.props.mayHaveSectionsCards && this.props.topic && !isListCard,
      format: format,
      topic: this.props.topic,
      icon_src: faviconEnabled && this.props.icon_src
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-stp-button-hover-background"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-stp-button-position-wrapper"
    }, !isFakespot && /*#__PURE__*/external_React_default().createElement(DSLinkMenu, {
      id: this.props.id,
      index: this.props.pos,
      dispatch: this.props.dispatch,
      url: this.props.url,
      title: this.props.title,
      source: source,
      type: this.props.type,
      card_type: this.props.flightId ? "spoc" : "organic",
      pocket_id: this.props.pocket_id,
      shim: this.props.shim,
      bookmarkGuid: this.props.bookmarkGuid,
      flightId: !this.props.is_collection ? this.props.flightId : undefined,
      showPrivacyInfo: !!this.props.flightId,
      onMenuUpdate: this.onMenuUpdate,
      onMenuShow: this.onMenuShow,
      saveToPocketCard: saveToPocketCard,
      pocket_button_enabled: pocketButtonEnabled,
      isRecentSave: isRecentSave,
      recommendation_id: this.props.recommendation_id,
      tile_id: this.props.id,
      block_key: this.props.id,
      corpus_item_id: this.props.corpus_item_id,
      scheduled_corpus_item_id: this.props.scheduled_corpus_item_id,
      recommended_at: this.props.recommended_at,
      received_rank: this.props.received_rank,
      is_list_card: this.props.isListCard,
      section: this.props.section,
      section_position: this.props.sectionPosition,
      is_section_followed: this.props.sectionFollowed,
      format: format,
      isSectionsCard: this.props.mayHaveSectionsCards,
      topic: this.props.topic,
      selected_topics: this.props.selected_topics
    }))));
  }
}
_DSCard.defaultProps = {
  windowObj: window // Added to support unit tests
};
const DSCard = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  App: state.App,
  DiscoveryStream: state.DiscoveryStream,
  Prefs: state.Prefs
}))(_DSCard);
const PlaceholderDSCard = () => /*#__PURE__*/external_React_default().createElement(DSCard, {
  placeholder: true
});
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSEmptyState/DSEmptyState.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



class DSEmptyState extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onReset = this.onReset.bind(this);
    this.state = {};
  }
  componentWillUnmount() {
    if (this.timeout) {
      clearTimeout(this.timeout);
    }
  }
  onReset() {
    if (this.props.dispatch && this.props.feed) {
      const {
        feed
      } = this.props;
      const {
        url
      } = feed;
      this.props.dispatch({
        type: actionTypes.DISCOVERY_STREAM_FEED_UPDATE,
        data: {
          feed: {
            ...feed,
            data: {
              ...feed.data,
              status: "waiting"
            }
          },
          url
        }
      });
      this.setState({
        waiting: true
      });
      this.timeout = setTimeout(() => {
        this.timeout = null;
        this.setState({
          waiting: false
        });
      }, 300);
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.DISCOVERY_STREAM_RETRY_FEED,
        data: {
          feed
        }
      }));
    }
  }
  renderButton() {
    if (this.props.status === "waiting" || this.state.waiting) {
      return /*#__PURE__*/external_React_default().createElement("button", {
        className: "try-again-button waiting",
        "data-l10n-id": "newtab-discovery-empty-section-topstories-loading"
      });
    }
    return /*#__PURE__*/external_React_default().createElement("button", {
      className: "try-again-button",
      onClick: this.onReset,
      "data-l10n-id": "newtab-discovery-empty-section-topstories-try-again-button"
    });
  }
  renderState() {
    if (this.props.status === "waiting" || this.props.status === "failed") {
      return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h2", {
        "data-l10n-id": "newtab-discovery-empty-section-topstories-timed-out"
      }), this.renderButton());
    }
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("h2", {
      "data-l10n-id": "newtab-discovery-empty-section-topstories-header"
    }), /*#__PURE__*/external_React_default().createElement("p", {
      "data-l10n-id": "newtab-discovery-empty-section-topstories-content"
    }));
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "section-empty-state"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "empty-state-message"
    }, this.renderState()));
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSDismiss/DSDismiss.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class DSDismiss extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onDismissClick = this.onDismissClick.bind(this);
    this.onHover = this.onHover.bind(this);
    this.offHover = this.offHover.bind(this);
    this.state = {
      hovering: false
    };
  }
  onDismissClick() {
    if (this.props.onDismissClick) {
      this.props.onDismissClick();
    }
  }
  onHover() {
    this.setState({
      hovering: true
    });
  }
  offHover() {
    this.setState({
      hovering: false
    });
  }
  render() {
    let className = `ds-dismiss
      ${this.state.hovering ? ` hovering` : ``}
      ${this.props.extraClasses ? ` ${this.props.extraClasses}` : ``}`;
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: className
    }, this.props.children, /*#__PURE__*/external_React_default().createElement("button", {
      className: "ds-dismiss-button",
      "data-l10n-id": "newtab-dismiss-button-tooltip",
      onClick: this.onDismissClick,
      onMouseEnter: this.onHover,
      onMouseLeave: this.offHover
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-dismiss"
    })));
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/TopicsWidget/TopicsWidget.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






function _TopicsWidget(props) {
  const {
    id,
    source,
    position,
    DiscoveryStream,
    dispatch
  } = props;
  const {
    utmCampaign,
    utmContent,
    utmSource
  } = DiscoveryStream.experimentData;
  let queryParams = `?utm_source=${utmSource}`;
  if (utmCampaign && utmContent) {
    queryParams += `&utm_content=${utmContent}&utm_campaign=${utmCampaign}`;
  }
  const topics = [{
    label: "Technology",
    name: "technology"
  }, {
    label: "Science",
    name: "science"
  }, {
    label: "Self-Improvement",
    name: "self-improvement"
  }, {
    label: "Travel",
    name: "travel"
  }, {
    label: "Career",
    name: "career"
  }, {
    label: "Entertainment",
    name: "entertainment"
  }, {
    label: "Food",
    name: "food"
  }, {
    label: "Health",
    name: "health"
  }, {
    label: "Must-Reads",
    name: "must-reads",
    url: `https://getpocket.com/collections${queryParams}`
  }];
  function onLinkClick(topic, positionInCard) {
    if (dispatch) {
      dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        source,
        action_position: position,
        value: {
          card_type: "topics_widget",
          topic,
          ...(positionInCard || positionInCard === 0 ? {
            position_in_card: positionInCard
          } : {}),
          section_position: position
        }
      }));
      dispatch(actionCreators.ImpressionStats({
        source,
        click: 0,
        window_inner_width: props.windowObj.innerWidth,
        window_inner_height: props.windowObj.innerHeight,
        tiles: [{
          id,
          pos: position
        }]
      }));
    }
  }
  function mapTopicItem(topic, index) {
    return /*#__PURE__*/external_React_default().createElement("li", {
      key: topic.name,
      className: topic.overflow ? "ds-topics-widget-list-overflow-item" : ""
    }, /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      url: topic.url || `https://getpocket.com/explore/${topic.name}${queryParams}`,
      dispatch: dispatch,
      onLinkClick: () => onLinkClick(topic.name, index)
    }, topic.label));
  }
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-topics-widget"
  }, /*#__PURE__*/external_React_default().createElement("header", {
    className: "ds-topics-widget-header"
  }, "Popular Topics"), /*#__PURE__*/external_React_default().createElement("hr", null), /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-topics-widget-list-container"
  }, /*#__PURE__*/external_React_default().createElement("ul", null, topics.map(mapTopicItem))), /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
    className: "ds-topics-widget-button button primary",
    url: `https://getpocket.com/${queryParams}`,
    dispatch: dispatch,
    onLinkClick: () => onLinkClick("more-topics")
  }, "More Topics"), /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
    dispatch: dispatch,
    rows: [{
      id,
      pos: position
    }],
    source: source
  }));
}
_TopicsWidget.defaultProps = {
  windowObj: window // Added to support unit tests
};
const TopicsWidget = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  DiscoveryStream: state.DiscoveryStream
}))(_TopicsWidget);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/ListFeed/ListFeed.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */








const PREF_LISTFEED_TITLE = "discoverystream.contextualContent.listFeedTitle";
const PREF_FAKESPOT_CATEGROY = "discoverystream.contextualContent.fakespot.defaultCategoryTitle";
const PREF_FAKESPOT_FOOTER = "discoverystream.contextualContent.fakespot.footerCopy";
const PREF_FAKESPOT_CTA_COPY = "discoverystream.contextualContent.fakespot.ctaCopy";
const PREF_FAKESPOT_CTA_URL = "discoverystream.contextualContent.fakespot.ctaUrl";
const ListFeed_PREF_CONTEXTUAL_CONTENT_SELECTED_FEED = "discoverystream.contextualContent.selectedFeed";
function ListFeed({
  type,
  firstVisibleTimestamp,
  recs,
  categories,
  dispatch
}) {
  const [selectedFakespotFeed, setSelectedFakespotFeed] = (0,external_React_namespaceObject.useState)("");
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const listFeedTitle = prefs[PREF_LISTFEED_TITLE];
  const categoryTitle = prefs[PREF_FAKESPOT_CATEGROY];
  const footerCopy = prefs[PREF_FAKESPOT_FOOTER];
  const ctaCopy = prefs[PREF_FAKESPOT_CTA_COPY];
  const ctaUrl = prefs[PREF_FAKESPOT_CTA_URL];
  const isFakespot = prefs[ListFeed_PREF_CONTEXTUAL_CONTENT_SELECTED_FEED] === "fakespot";
  // Todo: need to remove ads while using default recommendations, remove this line once API has been updated.
  let listFeedRecs = selectedFakespotFeed ? recs.filter(rec => rec.category === selectedFakespotFeed) : recs;
  function handleCtaClick() {
    dispatch(actionCreators.OnlyToMain({
      type: "FAKESPOT_CTA_CLICK"
    }));
  }
  function handleChange(e) {
    setSelectedFakespotFeed(e.target.value);
    dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "FAKESPOT_CATEGORY",
      value: {
        category: e.target.value || ""
      }
    }));
  }
  const contextMenuOptions = ["FakespotDismiss", "AboutFakespot"];
  const {
    length: listLength
  } = listFeedRecs;
  // determine if the list should take up all availible height or not
  const fullList = listLength >= 5;
  return listLength > 0 && /*#__PURE__*/external_React_default().createElement("div", {
    className: `list-feed ${fullList ? "full-height" : ""} ${listLength > 2 ? "span-2" : "span-1"}`
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "list-feed-inner-wrapper"
  }, isFakespot ? /*#__PURE__*/external_React_default().createElement("div", {
    className: "fakespot-heading"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "dropdown-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("select", {
    className: "fakespot-dropdown",
    name: "fakespot-categories",
    value: selectedFakespotFeed,
    onChange: handleChange
  }, /*#__PURE__*/external_React_default().createElement("option", {
    value: ""
  }, categoryTitle || "Holiday Gift Guide"), categories.map(category => /*#__PURE__*/external_React_default().createElement("option", {
    key: category,
    value: category
  }, category))), /*#__PURE__*/external_React_default().createElement("div", {
    className: "context-menu-wrapper"
  }, /*#__PURE__*/external_React_default().createElement(ContextMenuButton, null, /*#__PURE__*/external_React_default().createElement(LinkMenu, {
    dispatch: dispatch,
    options: contextMenuOptions,
    shouldSendImpressionStats: true,
    site: {
      url: "https://www.fakespot.com"
    }
  })))), /*#__PURE__*/external_React_default().createElement("p", {
    className: "fakespot-desc"
  }, listFeedTitle)) : /*#__PURE__*/external_React_default().createElement("h1", {
    className: "list-feed-title",
    id: "list-feed-title"
  }, /*#__PURE__*/external_React_default().createElement("span", {
    className: "icon icon-newsfeed"
  }), listFeedTitle), /*#__PURE__*/external_React_default().createElement("div", {
    className: "list-feed-content",
    role: "menu",
    "aria-labelledby": "list-feed-title"
  }, listFeedRecs.slice(0, 5).map((rec, index) => {
    if (!rec || rec.placeholder) {
      return /*#__PURE__*/external_React_default().createElement(DSCard, {
        key: `list-card-${index}`,
        placeholder: true,
        isListCard: true
      });
    }
    return /*#__PURE__*/external_React_default().createElement(DSCard, {
      key: `list-card-${index}`,
      pos: index,
      flightId: rec.flight_id,
      image_src: rec.image_src,
      raw_image_src: rec.raw_image_src,
      word_count: rec.word_count,
      time_to_read: rec.time_to_read,
      title: rec.title,
      topic: rec.topic,
      excerpt: rec.excerpt,
      url: rec.url,
      id: rec.id,
      shim: rec.shim,
      type: type,
      context: rec.context,
      sponsor: rec.sponsor,
      sponsored_by_override: rec.sponsored_by_override,
      dispatch: dispatch,
      source: rec.domain,
      publisher: rec.publisher,
      pocket_id: rec.pocket_id,
      context_type: rec.context_type,
      bookmarkGuid: rec.bookmarkGuid,
      firstVisibleTimestamp: firstVisibleTimestamp,
      corpus_item_id: rec.corpus_item_id,
      scheduled_corpus_item_id: rec.scheduled_corpus_item_id,
      recommended_at: rec.recommended_at,
      received_rank: rec.received_rank,
      isListCard: true,
      isFakespot: isFakespot,
      category: rec.category
    });
  }), isFakespot && /*#__PURE__*/external_React_default().createElement("div", {
    className: "fakespot-footer"
  }, /*#__PURE__*/external_React_default().createElement("p", null, footerCopy), /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
    className: "fakespot-cta",
    url: ctaUrl,
    referrer: "",
    onLinkClick: handleCtaClick,
    dispatch: dispatch
  }, ctaCopy)))));
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/AdBannerContextMenu/AdBannerContextMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */





/**
 * A context menu for IAB banners (e.g. billboard, leaderboard).
 *
 * Note: MREC ad formats and sponsored stories share the context menu with
 * other cards: make sure you also look at DSLinkMenu component
 * to keep any updates to ad-related context menu items in sync.
 *
 * @param dispatch
 * @param spoc
 * @param position
 * @param type
 * @param showAdReporting
 * @returns {Element}
 * @constructor
 */
function AdBannerContextMenu({
  dispatch,
  spoc,
  position,
  type,
  showAdReporting
}) {
  const ADBANNER_CONTEXT_MENU_OPTIONS = ["BlockAdUrl", ...(showAdReporting ? ["ReportAd"] : []), "ManageSponsoredContent", "OurSponsorsAndYourPrivacy"];
  const [showContextMenu, setShowContextMenu] = (0,external_React_namespaceObject.useState)(false);
  const [contextMenuClassNames, setContextMenuClassNames] = (0,external_React_namespaceObject.useState)("ads-context-menu");

  // The keyboard access parameter is passed down to LinkMenu component
  // that uses it to focus on the first context menu option for accessibility.
  const [isKeyboardAccess, setIsKeyboardAccess] = (0,external_React_namespaceObject.useState)(false);

  /**
   * Toggles the style fix for context menu hover/active styles.
   * This allows us to have unobtrusive, transparent button background by default,
   * yet flip it over to semi-transparent grey when the menu is visible.
   *
   * @param contextMenuOpen
   */
  const toggleContextMenuStyleSwitch = contextMenuOpen => {
    if (contextMenuOpen) {
      setContextMenuClassNames("ads-context-menu context-menu-open");
    } else {
      setContextMenuClassNames("ads-context-menu");
    }
  };

  /**
   * Toggles the context menu to open or close. Sets state depending on whether
   * the context menu is accessed by mouse or keyboard.
   *
   * @param isKeyBoard
   */
  const toggleContextMenu = isKeyBoard => {
    toggleContextMenuStyleSwitch(!showContextMenu);
    setShowContextMenu(!showContextMenu);
    setIsKeyboardAccess(isKeyBoard);
  };
  const onClick = e => {
    e.preventDefault();
    toggleContextMenu(false);
  };
  const onKeyDown = e => {
    if (e.key === "Enter" || e.key === " ") {
      e.preventDefault();
      toggleContextMenu(true);
    }
  };
  const onUpdate = () => {
    toggleContextMenuStyleSwitch(!showContextMenu);
    setShowContextMenu(!showContextMenu);
  };
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "ads-context-menu-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: contextMenuClassNames
  }, /*#__PURE__*/external_React_default().createElement("moz-button", {
    type: "icon",
    size: "default",
    iconsrc: "chrome://global/skin/icons/more.svg",
    onClick: onClick,
    onKeyDown: onKeyDown
  }), showContextMenu && /*#__PURE__*/external_React_default().createElement(LinkMenu, {
    onUpdate: onUpdate,
    dispatch: dispatch,
    keyboardAccess: isKeyboardAccess,
    options: ADBANNER_CONTEXT_MENU_OPTIONS,
    shouldSendImpressionStats: true,
    userEvent: actionCreators.DiscoveryStreamUserEvent,
    site: {
      // Props we want to pass on for new ad types that come from Unified Ads API
      block_key: spoc.block_key,
      fetchTimestamp: spoc.fetchTimestamp,
      flight_id: spoc.flight_id,
      format: spoc.format,
      id: spoc.id,
      guid: spoc.guid,
      card_type: "spoc",
      // required to record telemetry for an action, see handleBlockUrl in TelemetryFeed.sys.mjs
      is_pocket_card: true,
      position,
      sponsor: spoc.sponsor,
      title: spoc.title,
      url: spoc.url || spoc.shim.url,
      personalization_models: spoc.personalization_models,
      priority: spoc.priority,
      score: spoc.score,
      alt_text: spoc.alt_text,
      shim: spoc.shim
    },
    index: position,
    source: type.toUpperCase()
  })));
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/AdBanner/AdBanner.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */







/**
 * A new banner ad that appears between rows of stories: leaderboard or billboard size.
 *
 * @param spoc
 * @param dispatch
 * @param firstVisibleTimestamp
 * @param row
 * @param type
 * @param prefs
 * @returns {Element}
 * @constructor
 */
const AdBanner = ({
  spoc,
  dispatch,
  firstVisibleTimestamp,
  row,
  type,
  prefs
}) => {
  const getDimensions = format => {
    switch (format) {
      case "leaderboard":
        return {
          width: "728",
          height: "90"
        };
      case "billboard":
        return {
          width: "970",
          height: "250"
        };
    }
    return {
      // image will still render with default values
      width: undefined,
      height: undefined
    };
  };
  const sectionsEnabled = prefs["discoverystream.sections.enabled"];
  const showAdReporting = prefs["discoverystream.reportAds.enabled"];
  const {
    width: imgWidth,
    height: imgHeight
  } = getDimensions(spoc.format);
  const onLinkClick = () => {
    dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: type.toUpperCase(),
      // Banner ads don't have a position, but a row number
      action_position: parseInt(row, 10),
      value: {
        card_type: "spoc",
        tile_id: spoc.id,
        ...(spoc.shim?.click ? {
          shim: spoc.shim.click
        } : {}),
        fetchTimestamp: spoc.fetchTimestamp,
        firstVisibleTimestamp,
        format: spoc.format,
        ...(sectionsEnabled ? {
          section: spoc.format,
          section_position: parseInt(row, 10)
        } : {})
      }
    }));
  };

  // in the default card grid 1 would come before the 1st row of cards and 9 comes after the last row
  // using clamp to make sure its between valid values (1-9)
  const clampedRow = Math.max(1, Math.min(9, row));
  return /*#__PURE__*/external_React_default().createElement("aside", {
    className: "ad-banner-wrapper",
    style: {
      gridRow: clampedRow
    }
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: `ad-banner-inner ${spoc.format}`
  }, /*#__PURE__*/external_React_default().createElement(AdBannerContextMenu, {
    dispatch: dispatch,
    spoc: spoc,
    position: row,
    type: type,
    showAdReporting: showAdReporting
  }), /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
    className: "ad-banner-link",
    url: spoc.url,
    title: spoc.title,
    onLinkClick: onLinkClick,
    dispatch: dispatch
  }, /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
    flightId: spoc.flight_id,
    rows: [{
      id: spoc.id,
      card_type: "spoc",
      pos: row,
      recommended_at: spoc.recommended_at,
      received_rank: spoc.received_rank,
      format: spoc.format,
      ...(spoc.shim?.impression ? {
        shim: spoc.shim.impression
      } : {})
    }],
    dispatch: dispatch,
    firstVisibleTimestamp: firstVisibleTimestamp
  }), /*#__PURE__*/external_React_default().createElement("div", {
    className: "ad-banner-content"
  }, /*#__PURE__*/external_React_default().createElement("img", {
    src: spoc.raw_image_src,
    alt: spoc.alt_text,
    loading: "eager",
    width: imgWidth,
    height: imgHeight
  }))), /*#__PURE__*/external_React_default().createElement("div", {
    className: "ad-banner-sponsored"
  }, /*#__PURE__*/external_React_default().createElement("span", {
    className: "ad-banner-sponsored-label",
    "data-l10n-id": "newtab-topsite-sponsored"
  }))));
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/CardGrid/CardGrid.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */












const PREF_ONBOARDING_EXPERIENCE_DISMISSED = "discoverystream.onboardingExperience.dismissed";
const PREF_SECTIONS_CARDS_ENABLED = "discoverystream.sections.cards.enabled";
const PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_TOPICS_ENABLED = "discoverystream.topicLabels.enabled";
const PREF_TOPICS_SELECTED = "discoverystream.topicSelection.selectedTopics";
const PREF_TOPICS_AVAILABLE = "discoverystream.topicSelection.topics";
const PREF_SPOCS_STARTUPCACHE_ENABLED = "discoverystream.spocs.startupCache.enabled";
const PREF_LIST_FEED_ENABLED = "discoverystream.contextualContent.enabled";
const PREF_LIST_FEED_SELECTED_FEED = "discoverystream.contextualContent.selectedFeed";
const PREF_FAKESPOT_ENABLED = "discoverystream.contextualContent.fakespot.enabled";
const PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const CardGrid_INTERSECTION_RATIO = 0.5;
const CardGrid_VISIBLE = "visible";
const CardGrid_VISIBILITY_CHANGE_EVENT = "visibilitychange";
const WIDGET_IDS = {
  TOPICS: 1
};
function DSSubHeader({
  children
}) {
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-top-bar ds-sub-header"
  }, /*#__PURE__*/external_React_default().createElement("h3", {
    className: "section-title-container"
  }, children));
}
function OnboardingExperience({
  dispatch,
  windowObj = globalThis
}) {
  const [dismissed, setDismissed] = (0,external_React_namespaceObject.useState)(false);
  const [maxHeight, setMaxHeight] = (0,external_React_namespaceObject.useState)(null);
  const heightElement = (0,external_React_namespaceObject.useRef)(null);
  const onDismissClick = (0,external_React_namespaceObject.useCallback)(() => {
    // We update this as state and redux.
    // The state update is for this newtab,
    // and the redux update is for other tabs, offscreen tabs, and future tabs.
    // We need the state update for this tab to support the transition.
    setDismissed(true);
    dispatch(actionCreators.SetPref(PREF_ONBOARDING_EXPERIENCE_DISMISSED, true));
    dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "BLOCK",
      source: "POCKET_ONBOARDING"
    }));
  }, [dispatch]);
  (0,external_React_namespaceObject.useEffect)(() => {
    const resizeObserver = new windowObj.ResizeObserver(() => {
      if (heightElement.current) {
        setMaxHeight(heightElement.current.offsetHeight);
      }
    });
    const options = {
      threshold: CardGrid_INTERSECTION_RATIO
    };
    const intersectionObserver = new windowObj.IntersectionObserver(entries => {
      if (entries.some(entry => entry.isIntersecting && entry.intersectionRatio >= CardGrid_INTERSECTION_RATIO)) {
        dispatch(actionCreators.DiscoveryStreamUserEvent({
          event: "IMPRESSION",
          source: "POCKET_ONBOARDING"
        }));
        // Once we have observed an impression, we can stop for this instance of newtab.
        intersectionObserver.unobserve(heightElement.current);
      }
    }, options);
    const onVisibilityChange = () => {
      intersectionObserver.observe(heightElement.current);
      windowObj.document.removeEventListener(CardGrid_VISIBILITY_CHANGE_EVENT, onVisibilityChange);
    };
    if (heightElement.current) {
      resizeObserver.observe(heightElement.current);
      // Check visibility or setup a visibility event to make
      // sure we don't fire this for off screen pre loaded tabs.
      if (windowObj.document.visibilityState === CardGrid_VISIBLE) {
        intersectionObserver.observe(heightElement.current);
      } else {
        windowObj.document.addEventListener(CardGrid_VISIBILITY_CHANGE_EVENT, onVisibilityChange);
      }
      setMaxHeight(heightElement.current.offsetHeight);
    }

    // Return unmount callback to clean up observers.
    return () => {
      resizeObserver?.disconnect();
      intersectionObserver?.disconnect();
      windowObj.document.removeEventListener(CardGrid_VISIBILITY_CHANGE_EVENT, onVisibilityChange);
    };
  }, [dispatch, windowObj]);
  const style = {};
  if (dismissed) {
    style.maxHeight = "0";
    style.opacity = "0";
    style.transition = "max-height 0.26s ease, opacity 0.26s ease";
  } else if (maxHeight) {
    style.maxHeight = `${maxHeight}px`;
  }
  return /*#__PURE__*/external_React_default().createElement("div", {
    style: style
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-onboarding-ref",
    ref: heightElement
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-onboarding-container"
  }, /*#__PURE__*/external_React_default().createElement(DSDismiss, {
    onDismissClick: onDismissClick,
    extraClasses: `ds-onboarding`
  }, /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("header", null, /*#__PURE__*/external_React_default().createElement("span", {
    className: "icon icon-pocket"
  }), /*#__PURE__*/external_React_default().createElement("span", {
    "data-l10n-id": "newtab-pocket-onboarding-discover"
  })), /*#__PURE__*/external_React_default().createElement("p", {
    "data-l10n-id": "newtab-pocket-onboarding-cta"
  })), /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-onboarding-graphic"
  })))));
}

// eslint-disable-next-line no-shadow
function CardGrid_IntersectionObserver({
  children,
  windowObj = window,
  onIntersecting
}) {
  const intersectionElement = (0,external_React_namespaceObject.useRef)(null);
  (0,external_React_namespaceObject.useEffect)(() => {
    let observer;
    if (!observer && onIntersecting && intersectionElement.current) {
      observer = new windowObj.IntersectionObserver(entries => {
        const entry = entries.find(e => e.isIntersecting);
        if (entry) {
          // Stop observing since element has been seen
          if (observer && intersectionElement.current) {
            observer.unobserve(intersectionElement.current);
          }
          onIntersecting();
        }
      });
      observer.observe(intersectionElement.current);
    }
    // Cleanup
    return () => observer?.disconnect();
  }, [windowObj, onIntersecting]);
  return /*#__PURE__*/external_React_default().createElement("div", {
    ref: intersectionElement
  }, children);
}
function RecentSavesContainer({
  gridClassName = "",
  dispatch,
  windowObj = window,
  items = 3,
  source = "CARDGRID_RECENT_SAVES"
}) {
  const {
    recentSavesData,
    isUserLoggedIn,
    experimentData: {
      utmCampaign,
      utmContent,
      utmSource
    }
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const [visible, setVisible] = (0,external_React_namespaceObject.useState)(false);
  const onIntersecting = (0,external_React_namespaceObject.useCallback)(() => setVisible(true), []);
  (0,external_React_namespaceObject.useEffect)(() => {
    if (visible) {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.DISCOVERY_STREAM_POCKET_STATE_INIT
      }));
    }
  }, [visible, dispatch]);

  // The user has not yet scrolled to this section,
  // so wait before potentially requesting Pocket data.
  if (!visible) {
    return /*#__PURE__*/external_React_default().createElement(CardGrid_IntersectionObserver, {
      windowObj: windowObj,
      onIntersecting: onIntersecting
    });
  }

  // Intersection observer has finished, but we're not yet logged in.
  if (visible && !isUserLoggedIn) {
    return null;
  }
  let queryParams = `?utm_source=${utmSource}`;
  // We really only need to add these params to urls we own.
  if (utmCampaign && utmContent) {
    queryParams += `&utm_content=${utmContent}&utm_campaign=${utmCampaign}`;
  }
  function renderCard(rec, index) {
    const url = new URL(rec.url);
    const urlSearchParams = new URLSearchParams(queryParams);
    if (rec?.id && !url.href.match(/getpocket\.com\/read/)) {
      url.href = `https://getpocket.com/read/${rec.id}`;
    }
    for (let [key, val] of urlSearchParams.entries()) {
      url.searchParams.set(key, val);
    }
    return /*#__PURE__*/external_React_default().createElement(DSCard, {
      key: `dscard-${rec?.id || index}`,
      id: rec.id,
      pos: index,
      type: source,
      image_src: rec.image_src,
      raw_image_src: rec.raw_image_src,
      icon_src: rec.icon_src,
      word_count: rec.word_count,
      time_to_read: rec.time_to_read,
      title: rec.title,
      excerpt: rec.excerpt,
      url: url.href,
      source: rec.domain,
      isRecentSave: true,
      dispatch: dispatch
    });
  }
  function onMyListClicked() {
    dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: `${source}_VIEW_LIST`
    }));
  }
  const recentSavesCards = [];
  // We fill the cards with a for loop over an inline map because
  // we want empty placeholders if there are not enough cards.
  for (let index = 0; index < items; index++) {
    const recentSave = recentSavesData[index];
    if (!recentSave) {
      recentSavesCards.push( /*#__PURE__*/external_React_default().createElement(PlaceholderDSCard, {
        key: `dscard-${index}`
      }));
    } else {
      recentSavesCards.push(renderCard({
        id: recentSave.id,
        image_src: recentSave.top_image_url,
        raw_image_src: recentSave.top_image_url,
        word_count: recentSave.word_count,
        time_to_read: recentSave.time_to_read,
        title: recentSave.resolved_title || recentSave.given_title,
        url: recentSave.resolved_url || recentSave.given_url,
        domain: recentSave.domain_metadata?.name,
        excerpt: recentSave.excerpt
      }, index));
    }
  }

  // We are visible and logged in.
  return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement(DSSubHeader, null, /*#__PURE__*/external_React_default().createElement("span", {
    className: "section-title"
  }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
    message: "Recently Saved to your List"
  })), /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
    onLinkClick: onMyListClicked,
    className: "section-sub-link",
    url: `https://getpocket.com/a${queryParams}`
  }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
    message: "View My List"
  }))), /*#__PURE__*/external_React_default().createElement("div", {
    className: `ds-card-grid-recent-saves ${gridClassName}`
  }, recentSavesCards));
}
class _CardGrid extends (external_React_default()).PureComponent {
  // eslint-disable-next-line max-statements
  renderCards() {
    const prefs = this.props.Prefs.values;
    const {
      items,
      fourCardLayout,
      essentialReadsHeader,
      editorsPicksHeader,
      onboardingExperience,
      ctaButtonSponsors,
      ctaButtonVariant,
      spocMessageVariant,
      widgets,
      recentSavesEnabled,
      DiscoveryStream
    } = this.props;
    const {
      saveToPocketCard,
      topicsLoading
    } = DiscoveryStream;
    const showRecentSaves = prefs.showRecentSaves && recentSavesEnabled;
    const isOnboardingExperienceDismissed = prefs[PREF_ONBOARDING_EXPERIENCE_DISMISSED];
    const mayHaveSectionsCards = prefs[PREF_SECTIONS_CARDS_ENABLED];
    const mayHaveThumbsUpDown = prefs[PREF_THUMBS_UP_DOWN_ENABLED];
    const showTopics = prefs[PREF_TOPICS_ENABLED];
    const selectedTopics = prefs[PREF_TOPICS_SELECTED];
    const availableTopics = prefs[PREF_TOPICS_AVAILABLE];
    const spocsStartupCacheEnabled = prefs[PREF_SPOCS_STARTUPCACHE_ENABLED];
    const listFeedEnabled = prefs[PREF_LIST_FEED_ENABLED];
    const listFeedSelectedFeed = prefs[PREF_LIST_FEED_SELECTED_FEED];
    const billboardEnabled = prefs[PREF_BILLBOARD_ENABLED];
    const leaderboardEnabled = prefs[PREF_LEADERBOARD_ENABLED];
    // filter out recs that should be in ListFeed
    const recs = this.props.data.recommendations.filter(item => !item.feedName).slice(0, items);
    const cards = [];
    let essentialReadsCards = [];
    let editorsPicksCards = [];
    for (let index = 0; index < items; index++) {
      const rec = recs[index];
      cards.push(topicsLoading || !rec || rec.placeholder || rec.flight_id && !spocsStartupCacheEnabled && this.props.App.isForStartupCache ? /*#__PURE__*/external_React_default().createElement(PlaceholderDSCard, {
        key: `dscard-${index}`
      }) : /*#__PURE__*/external_React_default().createElement(DSCard, {
        key: `dscard-${rec.id}`,
        pos: rec.pos,
        flightId: rec.flight_id,
        image_src: rec.image_src,
        raw_image_src: rec.raw_image_src,
        icon_src: rec.icon_src,
        word_count: rec.word_count,
        time_to_read: rec.time_to_read,
        title: rec.title,
        topic: rec.topic,
        showTopics: showTopics,
        selectedTopics: selectedTopics,
        availableTopics: availableTopics,
        excerpt: rec.excerpt,
        url: rec.url,
        id: rec.id,
        shim: rec.shim,
        fetchTimestamp: rec.fetchTimestamp,
        type: this.props.type,
        context: rec.context,
        sponsor: rec.sponsor,
        sponsored_by_override: rec.sponsored_by_override,
        dispatch: this.props.dispatch,
        source: rec.domain,
        publisher: rec.publisher,
        pocket_id: rec.pocket_id,
        context_type: rec.context_type,
        bookmarkGuid: rec.bookmarkGuid,
        is_collection: this.props.is_collection,
        saveToPocketCard: saveToPocketCard,
        ctaButtonSponsors: ctaButtonSponsors,
        ctaButtonVariant: ctaButtonVariant,
        spocMessageVariant: spocMessageVariant,
        recommendation_id: rec.recommendation_id,
        firstVisibleTimestamp: this.props.firstVisibleTimestamp,
        mayHaveThumbsUpDown: mayHaveThumbsUpDown,
        mayHaveSectionsCards: mayHaveSectionsCards,
        corpus_item_id: rec.corpus_item_id,
        scheduled_corpus_item_id: rec.scheduled_corpus_item_id,
        recommended_at: rec.recommended_at,
        received_rank: rec.received_rank,
        format: rec.format,
        alt_text: rec.alt_text,
        isTimeSensitive: rec.isTimeSensitive
      }));
    }
    if (widgets?.positions?.length && widgets?.data?.length) {
      let positionIndex = 0;
      const source = "CARDGRID_WIDGET";
      for (const widget of widgets.data) {
        let widgetComponent = null;
        const position = widgets.positions[positionIndex];

        // Stop if we run out of positions to place widgets.
        if (!position) {
          break;
        }
        switch (widget?.type) {
          case "TopicsWidget":
            widgetComponent = /*#__PURE__*/external_React_default().createElement(TopicsWidget, {
              position: position.index,
              dispatch: this.props.dispatch,
              source: source,
              id: WIDGET_IDS.TOPICS
            });
            break;
        }
        if (widgetComponent) {
          // We found a widget, so up the position for next try.
          positionIndex++;
          // We replace an existing card with the widget.
          cards.splice(position.index, 1, widgetComponent);
        }
      }
    }
    if (listFeedEnabled) {
      const isFakespot = listFeedSelectedFeed === "fakespot";
      const fakespotEnabled = prefs[PREF_FAKESPOT_ENABLED];
      if (!isFakespot || isFakespot && fakespotEnabled) {
        // Place the list feed as the 3rd element in the card grid
        cards.splice(2, 1, this.renderListFeed(this.props.data.recommendations, listFeedSelectedFeed));
      }
    }

    // if a banner ad is enabled and we have any available, place them in the grid
    const {
      spocs
    } = this.props.DiscoveryStream;
    if ((billboardEnabled || leaderboardEnabled) && spocs?.data?.newtab_spocs?.items) {
      // Only render one AdBanner in the grid -
      // Prioritize rendering a leaderboard if it exists,
      // otherwise render a billboard
      const spocToRender = spocs.data.newtab_spocs.items.find(({
        format
      }) => format === "leaderboard" && leaderboardEnabled) || spocs.data.newtab_spocs.items.find(({
        format
      }) => format === "billboard" && billboardEnabled);
      if (spocToRender && !spocs.blocked.includes(spocToRender.url)) {
        const row = spocToRender.format === "leaderboard" ? prefs[PREF_LEADERBOARD_POSITION] : prefs[PREF_BILLBOARD_POSITION];
        function displayCardsPerRow() {
          // Determines the number of cards per row based on the window width:
          // width <= 1122px: 2 cards per row
          // width 1123px to 1697px: 3 cards per row
          // width >= 1698px: 4 cards per row
          if (window.innerWidth <= 1122) {
            return 2;
          } else if (window.innerWidth > 1122 && window.innerWidth < 1698) {
            return 3;
          }
          return 4;
        }
        const injectAdBanner = bannerIndex => {
          // .splice() inserts the AdBanner at the desired index, ensuring correct DOM order for accessibility and keyboard navigation.
          // .push() would place it at the end, which is visually incorrect even if adjusted with CSS.
          cards.splice(bannerIndex, 0, /*#__PURE__*/external_React_default().createElement(AdBanner, {
            spoc: spocToRender,
            key: `dscard-${spocToRender.id}`,
            dispatch: this.props.dispatch,
            type: this.props.type,
            firstVisibleTimestamp: this.props.firstVisibleTimestamp,
            row: row,
            prefs: prefs
          }));
        };
        const getBannerIndex = () => {
          // Calculate the index for where the AdBanner should be added, depending on number of cards per row on the grid
          const cardsPerRow = displayCardsPerRow();
          let bannerIndex = (row - 1) * cardsPerRow;
          return bannerIndex;
        };
        injectAdBanner(getBannerIndex());
      }
    }
    let moreRecsHeader = "";
    // For now this is English only.
    if (showRecentSaves || essentialReadsHeader && editorsPicksHeader) {
      let spliceAt = 6;
      // For 4 card row layouts, second row is 8 cards, and regular it is 6 cards.
      if (fourCardLayout) {
        spliceAt = 8;
      }
      // If we have a custom header, ensure the more recs section also has a header.
      moreRecsHeader = "More Recommendations";
      // Put the first 2 rows into essentialReadsCards.
      essentialReadsCards = [...cards.splice(0, spliceAt)];
      // Put the rest into editorsPicksCards.
      if (essentialReadsHeader && editorsPicksHeader) {
        editorsPicksCards = [...cards.splice(0, cards.length)];
      }
    }
    const gridClassName = this.renderGridClassName();
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, !isOnboardingExperienceDismissed && onboardingExperience && /*#__PURE__*/external_React_default().createElement(OnboardingExperience, {
      dispatch: this.props.dispatch
    }), essentialReadsCards?.length > 0 && /*#__PURE__*/external_React_default().createElement("div", {
      className: gridClassName
    }, essentialReadsCards), showRecentSaves && /*#__PURE__*/external_React_default().createElement(RecentSavesContainer, {
      gridClassName: gridClassName,
      dispatch: this.props.dispatch
    }), editorsPicksCards?.length > 0 && /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement(DSSubHeader, null, /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-title"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: "Editor\u2019s Picks"
    }))), /*#__PURE__*/external_React_default().createElement("div", {
      className: gridClassName
    }, editorsPicksCards)), cards?.length > 0 && /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, moreRecsHeader && /*#__PURE__*/external_React_default().createElement(DSSubHeader, null, /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-title"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: moreRecsHeader
    }))), /*#__PURE__*/external_React_default().createElement("div", {
      className: gridClassName
    }, cards)));
  }
  renderListFeed(recommendations, selectedFeed) {
    const recs = recommendations.filter(item => item.feedName === selectedFeed);
    const isFakespot = selectedFeed === "fakespot";
    // remove duplicates from category list
    const categories = [...new Set(recs.map(({
      category
    }) => category))];
    const listFeed = /*#__PURE__*/external_React_default().createElement(ListFeed
    // only display recs that match selectedFeed for ListFeed
    , {
      recs: recs,
      categories: isFakespot ? categories : [],
      firstVisibleTimestamp: this.props.firstVisibleTimestamp,
      type: this.props.type,
      dispatch: this.props.dispatch
    });
    return listFeed;
  }
  renderGridClassName() {
    const prefs = this.props.Prefs.values;
    const {
      hybridLayout,
      hideCardBackground,
      fourCardLayout,
      compactGrid,
      hideDescriptions
    } = this.props;
    const adSizingVariantAEnabled = prefs["newtabAdSize.variant-a"];
    const adSizingVariantBEnabled = prefs["newtabAdSize.variant-b"];
    const adSizingVariantEnabled = adSizingVariantAEnabled || adSizingVariantBEnabled;
    let adSizingVariantClassName = "";
    if (adSizingVariantEnabled) {
      // Ad sizing experiment variant, we want to ensure only 1 of these is ever enabled.
      adSizingVariantClassName = adSizingVariantAEnabled ? `ad-sizing-variant-a` : `ad-sizing-variant-b`;
    }
    const hideCardBackgroundClass = hideCardBackground ? `ds-card-grid-hide-background` : ``;
    const fourCardLayoutClass = fourCardLayout ? `ds-card-grid-four-card-variant` : ``;
    const hideDescriptionsClassName = !hideDescriptions ? `ds-card-grid-include-descriptions` : ``;
    const compactGridClassName = compactGrid ? `ds-card-grid-compact` : ``;
    const hybridLayoutClassName = hybridLayout ? `ds-card-grid-hybrid-layout` : ``;
    const gridClassName = `ds-card-grid ${hybridLayoutClassName} ${hideCardBackgroundClass} ${fourCardLayoutClass} ${hideDescriptionsClassName} ${compactGridClassName} ${adSizingVariantClassName}`;
    return gridClassName;
  }
  render() {
    const {
      data
    } = this.props;

    // Handle a render before feed has been fetched by displaying nothing
    if (!data) {
      return null;
    }

    // Handle the case where a user has dismissed all recommendations
    const isEmpty = data.recommendations.length === 0;
    return /*#__PURE__*/external_React_default().createElement("div", null, this.props.title && /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-header"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "title"
    }, this.props.title), this.props.context && /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: this.props.context
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-context"
    }))), isEmpty ? /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-card-grid empty"
    }, /*#__PURE__*/external_React_default().createElement(DSEmptyState, {
      status: data.status,
      dispatch: this.props.dispatch,
      feed: this.props.feed
    })) : this.renderCards());
  }
}
_CardGrid.defaultProps = {
  items: 4 // Number of stories to display
};
const CardGrid = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs,
  App: state.App,
  DiscoveryStream: state.DiscoveryStream
}))(_CardGrid);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/CollectionCardGrid/CollectionCardGrid.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






class CollectionCardGrid extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onDismissClick = this.onDismissClick.bind(this);
    this.state = {
      dismissed: false
    };
  }
  onDismissClick() {
    const {
      data
    } = this.props;
    if (this.props.dispatch && data && data.spocs && data.spocs.length) {
      this.setState({
        dismissed: true
      });
      const pos = 0;
      const source = this.props.type.toUpperCase();
      // Grab the available items in the array to dismiss.
      // This fires a ping for all items available, even if below the fold.
      const spocsData = data.spocs.map(item => ({
        url: item.url,
        guid: item.id,
        shim: item.shim,
        flight_id: item.flightId
      }));
      const blockUrlOption = LinkMenuOptions.BlockUrls(spocsData, pos, source);
      const {
        action,
        impression,
        userEvent
      } = blockUrlOption;
      this.props.dispatch(action);
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: userEvent,
        source,
        action_position: pos
      }));
      if (impression) {
        this.props.dispatch(impression);
      }
    }
  }
  render() {
    const {
      data,
      dismissible,
      pocket_button_enabled
    } = this.props;
    if (this.state.dismissed || !data || !data.spocs || !data.spocs[0] ||
    // We only display complete collections.
    data.spocs.length < 3) {
      return null;
    }
    const {
      spocs,
      placement,
      feed
    } = this.props;
    // spocs.data is spocs state data, and not an array of spocs.
    const {
      title,
      context,
      sponsored_by_override,
      sponsor
    } = spocs.data[placement.name] || {};
    // Just in case of bad data, don't display a broken collection.
    if (!title) {
      return null;
    }
    let sponsoredByMessage = "";

    // If override is not false or an empty string.
    if (sponsored_by_override || sponsored_by_override === "") {
      // We specifically want to display nothing if the server returns an empty string.
      // So the server can turn off the label.
      // This is to support the use cases where the sponsored context is displayed elsewhere.
      sponsoredByMessage = sponsored_by_override;
    } else if (sponsor) {
      sponsoredByMessage = {
        id: `newtab-label-sponsored-by`,
        values: {
          sponsor
        }
      };
    } else if (context) {
      sponsoredByMessage = context;
    }

    // Generally a card grid displays recs with spocs already injected.
    // Normally it doesn't care which rec is a spoc and which isn't,
    // it just displays content in a grid.
    // For collections, we're only displaying a list of spocs.
    // We don't need to tell the card grid that our list of cards are spocs,
    // it shouldn't need to care. So we just pass our spocs along as recs.
    // Think of it as injecting all rec positions with spocs.
    // Consider maybe making recommendations in CardGrid use a more generic name.
    const recsData = {
      recommendations: data.spocs
    };

    // All cards inside of a collection card grid have a slightly different type.
    // For the case of interactions to the card grid, we use the type "COLLECTIONCARDGRID".
    // Example, you dismiss the whole collection, we use the type "COLLECTIONCARDGRID".
    // For interactions inside the card grid, example, you dismiss a single card in the collection,
    // we use the type "COLLECTIONCARDGRID_CARD".
    const type = `${this.props.type}_card`;
    const collectionGrid = /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-collection-card-grid"
    }, /*#__PURE__*/external_React_default().createElement(CardGrid, {
      pocket_button_enabled: pocket_button_enabled,
      title: title,
      context: sponsoredByMessage,
      data: recsData,
      feed: feed,
      type: type,
      is_collection: true,
      dispatch: this.props.dispatch,
      items: this.props.items
    }));
    if (dismissible) {
      return /*#__PURE__*/external_React_default().createElement(DSDismiss, {
        onDismissClick: this.onDismissClick,
        extraClasses: `ds-dismiss-ds-collection`
      }, collectionGrid);
    }
    return collectionGrid;
  }
}
;// CONCATENATED MODULE: ./content-src/components/A11yLinkButton/A11yLinkButton.jsx
function A11yLinkButton_extends() { A11yLinkButton_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return A11yLinkButton_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function A11yLinkButton(props) {
  // function for merging classes, if necessary
  let className = "a11y-link-button";
  if (props.className) {
    className += ` ${props.className}`;
  }
  return /*#__PURE__*/external_React_default().createElement("button", A11yLinkButton_extends({
    type: "button"
  }, props, {
    className: className
  }), props.children);
}
;// CONCATENATED MODULE: ./content-src/components/ErrorBoundary/ErrorBoundary.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



class ErrorBoundaryFallback extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.windowObj = this.props.windowObj || window;
    this.onClick = this.onClick.bind(this);
  }

  /**
   * Since we only get here if part of the page has crashed, do a
   * forced reload to give us the best chance at recovering.
   */
  onClick() {
    this.windowObj.location.reload(true);
  }
  render() {
    const defaultClass = "as-error-fallback";
    let className;
    if ("className" in this.props) {
      className = `${this.props.className} ${defaultClass}`;
    } else {
      className = defaultClass;
    }

    // "A11yLinkButton" to force normal link styling stuff (eg cursor on hover)
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: className
    }, /*#__PURE__*/external_React_default().createElement("div", {
      "data-l10n-id": "newtab-error-fallback-info"
    }), /*#__PURE__*/external_React_default().createElement("span", null, /*#__PURE__*/external_React_default().createElement(A11yLinkButton, {
      className: "reload-button",
      onClick: this.onClick,
      "data-l10n-id": "newtab-error-fallback-refresh-link"
    })));
  }
}
ErrorBoundaryFallback.defaultProps = {
  className: "as-error-fallback"
};
class ErrorBoundary extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      hasError: false
    };
  }
  componentDidCatch() {
    this.setState({
      hasError: true
    });
  }
  render() {
    if (!this.state.hasError) {
      return this.props.children;
    }
    return /*#__PURE__*/external_React_default().createElement(this.props.FallbackComponent, {
      className: this.props.className
    });
  }
}
ErrorBoundary.defaultProps = {
  FallbackComponent: ErrorBoundaryFallback
};
;// CONCATENATED MODULE: ./content-src/components/CollapsibleSection/CollapsibleSection.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */








/**
 * A section that can collapse. As of bug 1710937, it can no longer collapse.
 * See bug 1727365 for follow-up work to simplify this component.
 */
class _CollapsibleSection extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onBodyMount = this.onBodyMount.bind(this);
    this.onMenuButtonMouseEnter = this.onMenuButtonMouseEnter.bind(this);
    this.onMenuButtonMouseLeave = this.onMenuButtonMouseLeave.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
    this.setContextMenuButtonRef = this.setContextMenuButtonRef.bind(this);
    this.handleTopicSelectionButtonClick = this.handleTopicSelectionButtonClick.bind(this);
    this.state = {
      menuButtonHover: false,
      showContextMenu: false
    };
  }
  setContextMenuButtonRef(element) {
    this.contextMenuButtonRef = element;
  }
  onBodyMount(node) {
    this.sectionBody = node;
  }
  onMenuButtonMouseEnter() {
    this.setState({
      menuButtonHover: true
    });
  }
  onMenuButtonMouseLeave() {
    this.setState({
      menuButtonHover: false
    });
  }
  onMenuUpdate(showContextMenu) {
    this.setState({
      showContextMenu
    });
  }
  handleTopicSelectionButtonClick() {
    const maybeDisplay = this.props.Prefs.values["discoverystream.topicSelection.onboarding.maybeDisplay"];
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.TOPIC_SELECTION_USER_OPEN
    }));
    if (maybeDisplay) {
      // if still part of onboarding, remove user from onboarding flow
      this.props.dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", false));
    }
    this.props.dispatch(actionCreators.BroadcastToContent({
      type: actionTypes.TOPIC_SELECTION_SPOTLIGHT_OPEN
    }));
  }
  render() {
    const {
      isAnimating,
      maxHeight,
      menuButtonHover,
      showContextMenu
    } = this.state;
    const {
      id,
      collapsed,
      title,
      subTitle,
      mayHaveSponsoredStories,
      mayHaveTopicsSelection,
      sectionsEnabled
    } = this.props;
    const active = menuButtonHover || showContextMenu;
    let bodyStyle;
    if (isAnimating && !collapsed) {
      bodyStyle = {
        maxHeight
      };
    } else if (!isAnimating && collapsed) {
      bodyStyle = {
        display: "none"
      };
    }
    let titleStyle;
    if (this.props.hideTitle) {
      titleStyle = {
        visibility: "hidden"
      };
    }
    const hasSubtitleClassName = subTitle ? `has-subtitle` : ``;
    const hasBeenUpdatedPreviously = this.props.Prefs.values["discoverystream.topicSelection.hasBeenUpdatedPreviously"];
    const selectedTopics = this.props.Prefs.values["discoverystream.topicSelection.selectedTopics"];
    const topicsHaveBeenPreviouslySet = hasBeenUpdatedPreviously || selectedTopics;
    return /*#__PURE__*/external_React_default().createElement("section", {
      className: `collapsible-section ${this.props.className}${active ? " active" : ""}`
      // Note: data-section-id is used for web extension api tests in mozilla central
      ,
      "data-section-id": id
    }, !sectionsEnabled && /*#__PURE__*/external_React_default().createElement("div", {
      className: "section-top-bar"
    }, /*#__PURE__*/external_React_default().createElement("h2", {
      className: `section-title-container ${hasSubtitleClassName}`,
      style: titleStyle
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-title"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: title
    })), subTitle && /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-sub-title"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: subTitle
    })), mayHaveSponsoredStories && this.props.spocMessageVariant === "variant-a" && /*#__PURE__*/external_React_default().createElement(SponsoredContentHighlight, {
      position: "inset-block-start inset-inline-start",
      dispatch: this.props.dispatch
    })), mayHaveTopicsSelection && /*#__PURE__*/external_React_default().createElement("div", {
      className: "button-topic-selection"
    }, /*#__PURE__*/external_React_default().createElement("moz-button", {
      "data-l10n-id": topicsHaveBeenPreviouslySet ? "newtab-topic-selection-button-update-interests" : "newtab-topic-selection-button-pick-interests",
      type: topicsHaveBeenPreviouslySet ? "default" : "primary",
      onClick: this.handleTopicSelectionButtonClick
    }))), /*#__PURE__*/external_React_default().createElement(ErrorBoundary, {
      className: "section-body-fallback"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      ref: this.onBodyMount,
      style: bodyStyle
    }, this.props.children)));
  }
}
_CollapsibleSection.defaultProps = {
  document: globalThis.document || {
    addEventListener: () => {},
    removeEventListener: () => {},
    visibilityState: "hidden"
  }
};
const CollapsibleSection = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs
}))(_CollapsibleSection);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSMessage/DSMessage.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class DSMessage extends (external_React_default()).PureComponent {
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-message"
    }, /*#__PURE__*/external_React_default().createElement("header", {
      className: "title"
    }, this.props.icon && /*#__PURE__*/external_React_default().createElement("div", {
      className: "glyph",
      style: {
        backgroundImage: `url(${this.props.icon})`
      }
    }), this.props.title && /*#__PURE__*/external_React_default().createElement("span", {
      className: "title-text"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: this.props.title
    })), this.props.link_text && this.props.link_url && /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      className: "link",
      url: this.props.link_url
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: this.props.link_text
    }))));
  }
}
;// CONCATENATED MODULE: ./content-src/components/ModalOverlay/ModalOverlay.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function ModalOverlayWrapper({
  // eslint-disable-next-line no-shadow
  document = globalThis.document,
  unstyled,
  innerClassName,
  onClose,
  children,
  headerId,
  id
}) {
  const modalRef = (0,external_React_namespaceObject.useRef)(null);
  let className = unstyled ? "" : "modalOverlayInner active";
  if (innerClassName) {
    className += ` ${innerClassName}`;
  }

  // The intended behaviour is to listen for an escape key
  // but not for a click; see Bug 1582242
  const onKeyDown = (0,external_React_namespaceObject.useCallback)(event => {
    if (event.key === "Escape") {
      onClose(event);
    }
  }, [onClose]);
  (0,external_React_namespaceObject.useEffect)(() => {
    document.addEventListener("keydown", onKeyDown);
    document.body.classList.add("modal-open");
    return () => {
      document.removeEventListener("keydown", onKeyDown);
      document.body.classList.remove("modal-open");
    };
  }, [document, onKeyDown]);
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "modalOverlayOuter active",
    onKeyDown: onKeyDown,
    role: "presentation"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: className,
    "aria-labelledby": headerId,
    id: id,
    role: "dialog",
    ref: modalRef
  }, children));
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSPrivacyModal/DSPrivacyModal.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class DSPrivacyModal extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.closeModal = this.closeModal.bind(this);
    this.onLearnLinkClick = this.onLearnLinkClick.bind(this);
    this.onManageLinkClick = this.onManageLinkClick.bind(this);
  }
  onLearnLinkClick() {
    this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK_PRIVACY_INFO",
      source: "DS_PRIVACY_MODAL"
    }));
  }
  onManageLinkClick() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.SETTINGS_OPEN
    }));
  }
  closeModal() {
    this.props.dispatch({
      type: `HIDE_PRIVACY_INFO`,
      data: {}
    });
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement(ModalOverlayWrapper, {
      onClose: this.closeModal,
      innerClassName: "ds-privacy-modal"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "privacy-notice"
    }, /*#__PURE__*/external_React_default().createElement("h3", {
      "data-l10n-id": "newtab-privacy-modal-header"
    }), /*#__PURE__*/external_React_default().createElement("p", {
      "data-l10n-id": "newtab-privacy-modal-paragraph-2"
    }), /*#__PURE__*/external_React_default().createElement("a", {
      className: "modal-link modal-link-privacy",
      "data-l10n-id": "newtab-privacy-modal-link",
      onClick: this.onLearnLinkClick,
      href: "https://support.mozilla.org/kb/pocket-recommendations-firefox-new-tab"
    }), /*#__PURE__*/external_React_default().createElement("button", {
      className: "modal-link modal-link-manage",
      "data-l10n-id": "newtab-privacy-modal-button-manage",
      onClick: this.onManageLinkClick
    })), /*#__PURE__*/external_React_default().createElement("section", {
      className: "actions"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "done",
      type: "submit",
      onClick: this.closeModal,
      "data-l10n-id": "newtab-privacy-modal-button-done"
    })));
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/ReportContent/ReportContent.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */



const ReportContent = spocs => {
  const dispatch = (0,external_ReactRedux_namespaceObject.useDispatch)();
  const modal = (0,external_React_namespaceObject.useRef)(null);
  const radioGroupRef = (0,external_React_namespaceObject.useRef)(null);
  const submitButtonRef = (0,external_React_namespaceObject.useRef)(null);
  const report = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream.report);
  const [valueSelected, setValueSelected] = (0,external_React_namespaceObject.useState)(false);
  const [selectedReason, setSelectedReason] = (0,external_React_namespaceObject.useState)(null);
  const spocData = spocs.spocs.data;

  // Sends a dispatch to update the redux store when modal is cancelled
  const handleCancel = () => {
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.REPORT_CLOSE
    }));
  };
  const handleSubmit = (0,external_React_namespaceObject.useCallback)(() => {
    const {
      card_type,
      corpus_item_id,
      is_section_followed,
      position,
      received_rank,
      recommended_at,
      reporting_url,
      scheduled_corpus_item_id,
      section_position,
      section,
      title,
      topic,
      url
    } = report;
    if (card_type === "organic") {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.REPORT_CONTENT_SUBMIT,
        data: {
          card_type,
          corpus_item_id,
          is_section_followed,
          received_rank,
          recommended_at,
          report_reason: selectedReason,
          scheduled_corpus_item_id,
          section_position,
          section,
          title,
          topic,
          url
        }
      }));
    } else if (card_type === "spoc") {
      // Retrieve placement_id by comparing spocData with the ad that was reported
      const getPlacementId = () => {
        if (!spocData || !report.url) {
          return null;
        }
        for (const [placementId, spocList] of Object.entries(spocData)) {
          for (const spoc of Object.values(spocList)) {
            if (spoc?.url === report.url) {
              return placementId;
            }
          }
        }
        return null;
      };
      const placement_id = getPlacementId();
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.REPORT_AD_SUBMIT,
        data: {
          report_reason: selectedReason,
          placement_id,
          position,
          reporting_url,
          url
        }
      }));
    }
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.BLOCK_URL,
      data: [{
        ...report
      }]
    }));
    dispatch(actionCreators.OnlyToOneContent({
      type: actionTypes.SHOW_TOAST_MESSAGE,
      data: {
        toastId: "reportSuccessToast",
        showNotifications: true
      }
    }, "ActivityStream:Content"));
  }, [dispatch, selectedReason, report, spocData]);

  // Opens and closes the modal based on user interaction
  (0,external_React_namespaceObject.useEffect)(() => {
    if (report.visible && modal?.current) {
      modal.current.showModal();
    } else if (!report.visible && modal?.current?.open) {
      modal.current.close();
    }
  }, [report.visible]);

  // Updates the submit button's state based on if a value is selected
  (0,external_React_namespaceObject.useEffect)(() => {
    const radioGroup = radioGroupRef.current;
    const submitButton = submitButtonRef.current;
    const handleRadioChange = e => {
      const reasonValue = e?.target?.value;
      if (reasonValue) {
        setValueSelected(true);
        setSelectedReason(reasonValue);
      }
    };
    if (radioGroup) {
      radioGroup.addEventListener("change", handleRadioChange);
    }

    // Handle submit button state on valueSelected change
    const updateSubmitState = () => {
      if (valueSelected) {
        submitButton.removeAttribute("disabled");
      } else {
        submitButton.setAttribute("disabled", "");
      }
    };
    updateSubmitState();
    return () => {
      if (radioGroup) {
        radioGroup.removeEventListener("change", handleRadioChange);
      }
    };
  }, [valueSelected, selectedReason]);
  return /*#__PURE__*/external_React_default().createElement("dialog", {
    className: "report-content-form",
    id: "dialog-report",
    ref: modal,
    onClose: () => dispatch({
      type: actionTypes.REPORT_CLOSE
    })
  }, /*#__PURE__*/external_React_default().createElement("form", {
    action: ""
  }, report.card_type === "spoc" ? /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("moz-radio-group", {
    name: "report",
    ref: radioGroupRef,
    id: "report-group",
    "data-l10n-id": "newtab-report-ads-why-reporting",
    className: "report-ads-options"
  }, /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-ads-reason-not-interested",
    value: "not_interested"
  }), /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-ads-reason-inappropriate",
    value: "inappropriate"
  }), /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-ads-reason-seen-it-too-many-times",
    value: "seen_too_many_times"
  }))) : /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("moz-radio-group", {
    name: "report",
    ref: radioGroupRef,
    id: "report-group",
    "data-l10n-id": "newtab-report-content-why-reporting-this",
    className: "report-content-options"
  }, /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-content-wrong-category",
    value: "wrong_category"
  }), /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-content-outdated",
    value: "outdated"
  }), /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-content-inappropriate-offensive",
    value: "inappropriate_or_offensive"
  }), /*#__PURE__*/external_React_default().createElement("moz-radio", {
    "data-l10n-id": "newtab-report-content-spam-misleading",
    value: "spam_or_misleading"
  }))), /*#__PURE__*/external_React_default().createElement("moz-button-group", null, /*#__PURE__*/external_React_default().createElement("moz-button", {
    "data-l10n-id": "newtab-report-cancel",
    onClick: handleCancel,
    className: "cancel-report-btn"
  }), /*#__PURE__*/external_React_default().createElement("moz-button", {
    type: "primary",
    "data-l10n-id": "newtab-report-submit",
    ref: submitButtonRef,
    onClick: handleSubmit,
    className: "submit-report-btn"
  }))));
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSSignup/DSSignup.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */







class DSSignup extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      active: false,
      lastItem: false
    };
    this.onMenuButtonUpdate = this.onMenuButtonUpdate.bind(this);
    this.onLinkClick = this.onLinkClick.bind(this);
    this.onMenuShow = this.onMenuShow.bind(this);
  }
  onMenuButtonUpdate(showContextMenu) {
    if (!showContextMenu) {
      this.setState({
        active: false,
        lastItem: false
      });
    }
  }
  nextAnimationFrame() {
    return new Promise(resolve => this.props.windowObj.requestAnimationFrame(resolve));
  }
  async onMenuShow() {
    let {
      lastItem
    } = this.state;
    // Wait for next frame before computing scrollMaxX to allow fluent menu strings to be visible
    await this.nextAnimationFrame();
    if (this.props.windowObj.scrollMaxX > 0) {
      lastItem = true;
    }
    this.setState({
      active: true,
      lastItem
    });
  }
  onLinkClick() {
    const {
      data
    } = this.props;
    if (this.props.dispatch && data && data.spocs && data.spocs.length) {
      const source = this.props.type.toUpperCase();
      // Grab the first item in the array as we only have 1 spoc position.
      const [spoc] = data.spocs;
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        source,
        action_position: 0
      }));
      this.props.dispatch(actionCreators.ImpressionStats({
        source,
        click: 0,
        tiles: [{
          id: spoc.id,
          pos: 0,
          ...(spoc.shim && spoc.shim.click ? {
            shim: spoc.shim.click
          } : {})
        }]
      }));
    }
  }
  render() {
    const {
      data,
      dispatch,
      type
    } = this.props;
    if (!data || !data.spocs || !data.spocs[0]) {
      return null;
    }
    // Grab the first item in the array as we only have 1 spoc position.
    const [spoc] = data.spocs;
    const {
      title,
      url,
      excerpt,
      flight_id,
      id,
      shim
    } = spoc;
    const SIGNUP_CONTEXT_MENU_OPTIONS = ["OpenInNewWindow", "OpenInPrivateWindow", "Separator", "BlockUrl", ...(flight_id ? ["ShowPrivacyInfo"] : [])];
    const outerClassName = ["ds-signup", this.state.active && "active", this.state.lastItem && "last-item"].filter(v => v).join(" ");
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: outerClassName
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-signup-content"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-small-spacer icon-mail"
    }), /*#__PURE__*/external_React_default().createElement("span", null, title, " ", /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      className: "ds-chevron-link",
      dispatch: dispatch,
      onLinkClick: this.onLinkClick,
      url: url
    }, excerpt)), /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
      flightId: flight_id,
      rows: [{
        id,
        pos: 0,
        shim: shim && shim.impression
      }],
      dispatch: dispatch,
      source: type
    })), /*#__PURE__*/external_React_default().createElement(ContextMenuButton, {
      tooltip: "newtab-menu-content-tooltip",
      tooltipArgs: {
        title
      },
      onUpdate: this.onMenuButtonUpdate
    }, /*#__PURE__*/external_React_default().createElement(LinkMenu, {
      dispatch: dispatch,
      index: 0,
      source: type.toUpperCase(),
      onShow: this.onMenuShow,
      options: SIGNUP_CONTEXT_MENU_OPTIONS,
      shouldSendImpressionStats: true,
      userEvent: actionCreators.DiscoveryStreamUserEvent,
      site: {
        referrer: "https://getpocket.com/recommendations",
        title,
        type,
        url,
        guid: id,
        shim,
        flight_id
      }
    })));
  }
}
DSSignup.defaultProps = {
  windowObj: window // Added to support unit tests
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/DSTextPromo/DSTextPromo.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */








class DSTextPromo extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onLinkClick = this.onLinkClick.bind(this);
    this.onDismissClick = this.onDismissClick.bind(this);
  }
  onLinkClick() {
    const {
      data
    } = this.props;
    if (this.props.dispatch && data && data.spocs && data.spocs.length) {
      const source = this.props.type.toUpperCase();
      // Grab the first item in the array as we only have 1 spoc position.
      const [spoc] = data.spocs;
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        source,
        action_position: 0
      }));
      this.props.dispatch(actionCreators.ImpressionStats({
        source,
        click: 0,
        tiles: [{
          id: spoc.id,
          pos: 0,
          ...(spoc.shim && spoc.shim.click ? {
            shim: spoc.shim.click
          } : {})
        }]
      }));
    }
  }
  onDismissClick() {
    const {
      data
    } = this.props;
    if (this.props.dispatch && data && data.spocs && data.spocs.length) {
      const index = 0;
      const source = this.props.type.toUpperCase();
      // Grab the first item in the array as we only have 1 spoc position.
      const [spoc] = data.spocs;
      const spocData = {
        url: spoc.url,
        guid: spoc.id,
        shim: spoc.shim
      };
      const blockUrlOption = LinkMenuOptions.BlockUrl(spocData, index, source);
      const {
        action,
        impression,
        userEvent
      } = blockUrlOption;
      this.props.dispatch(action);
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: userEvent,
        source,
        action_position: index
      }));
      if (impression) {
        this.props.dispatch(impression);
      }
    }
  }
  render() {
    const {
      data
    } = this.props;
    if (!data || !data.spocs || !data.spocs[0]) {
      return null;
    }
    // Grab the first item in the array as we only have 1 spoc position.
    const [spoc] = data.spocs;
    const {
      image_src,
      raw_image_src,
      alt_text,
      title,
      url,
      context,
      cta,
      flight_id,
      id,
      shim
    } = spoc;
    return /*#__PURE__*/external_React_default().createElement(DSDismiss, {
      onDismissClick: this.onDismissClick,
      extraClasses: `ds-dismiss-ds-text-promo`
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-text-promo"
    }, /*#__PURE__*/external_React_default().createElement(DSImage, {
      alt_text: alt_text,
      source: image_src,
      rawSource: raw_image_src
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "text"
    }, /*#__PURE__*/external_React_default().createElement("h3", null, `${title}\u2003`, /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      className: "ds-chevron-link",
      dispatch: this.props.dispatch,
      onLinkClick: this.onLinkClick,
      url: url
    }, cta)), /*#__PURE__*/external_React_default().createElement("p", {
      className: "subtitle"
    }, context)), /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
      flightId: flight_id,
      rows: [{
        id,
        pos: 0,
        shim: shim && shim.impression
      }],
      dispatch: this.props.dispatch,
      source: this.props.type
    })));
  }
}
;// CONCATENATED MODULE: ./content-src/lib/screenshot-utils.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * List of helper functions for screenshot-based images.
 *
 * There are two kinds of images:
 * 1. Remote Image: This is the image from the main process and it refers to
 *    the image in the React props. This can either be an object with the `data`
 *    and `path` properties, if it is a blob, or a string, if it is a normal image.
 * 2. Local Image: This is the image object in the content process and it refers
 *    to the image *object* in the React component's state. All local image
 *    objects have the `url` property, and an additional property `path`, if they
 *    are blobs.
 */
const ScreenshotUtils = {
  isBlob(isLocal, image) {
    return !!(
      image &&
      image.path &&
      ((!isLocal && image.data) || (isLocal && image.url))
    );
  },

  // This should always be called with a remote image and not a local image.
  createLocalImageObject(remoteImage) {
    if (!remoteImage) {
      return null;
    }
    if (this.isBlob(false, remoteImage)) {
      return {
        url: globalThis.URL.createObjectURL(remoteImage.data),
        path: remoteImage.path,
      };
    }
    return { url: remoteImage };
  },

  // Revokes the object URL of the image if the local image is a blob.
  // This should always be called with a local image and not a remote image.
  maybeRevokeBlobObjectURL(localImage) {
    if (this.isBlob(true, localImage)) {
      globalThis.URL.revokeObjectURL(localImage.url);
    }
  },

  // Checks if remoteImage and localImage are the same.
  isRemoteImageLocal(localImage, remoteImage) {
    // Both remoteImage and localImage are present.
    if (remoteImage && localImage) {
      return this.isBlob(false, remoteImage)
        ? localImage.path === remoteImage.path
        : localImage.url === remoteImage;
    }

    // This will only handle the remaining three possible outcomes.
    // (i.e. everything except when both image and localImage are present)
    return !remoteImage && !localImage;
  },
};

;// CONCATENATED MODULE: ./content-src/components/Card/Card.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */









// Keep track of pending image loads to only request once
const gImageLoading = new Map();

/**
 * Card component.
 * Cards are found within a Section component and contain information about a link such
 * as preview image, page title, page description, and some context about if the page
 * was visited, bookmarked, trending etc...
 * Each Section can make an unordered list of Cards which will create one instane of
 * this class. Each card will then get a context menu which reflects the actions that
 * can be done on this Card.
 */
class _Card extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      activeCard: null,
      imageLoaded: false,
      cardImage: null
    };
    this.onMenuButtonUpdate = this.onMenuButtonUpdate.bind(this);
    this.onLinkClick = this.onLinkClick.bind(this);
  }

  /**
   * Helper to conditionally load an image and update state when it loads.
   */
  async maybeLoadImage() {
    // No need to load if it's already loaded or no image
    const {
      cardImage
    } = this.state;
    if (!cardImage) {
      return;
    }
    const imageUrl = cardImage.url;
    if (!this.state.imageLoaded) {
      // Initialize a promise to share a load across multiple card updates
      if (!gImageLoading.has(imageUrl)) {
        const loaderPromise = new Promise((resolve, reject) => {
          const loader = new Image();
          loader.addEventListener("load", resolve);
          loader.addEventListener("error", reject);
          loader.src = imageUrl;
        });

        // Save and remove the promise only while it's pending
        gImageLoading.set(imageUrl, loaderPromise);
        loaderPromise.catch(ex => ex).then(() => gImageLoading.delete(imageUrl));
      }

      // Wait for the image whether just started loading or reused promise
      try {
        await gImageLoading.get(imageUrl);
      } catch (ex) {
        // Ignore the failed image without changing state
        return;
      }

      // Only update state if we're still waiting to load the original image
      if (ScreenshotUtils.isRemoteImageLocal(this.state.cardImage, this.props.link.image) && !this.state.imageLoaded) {
        this.setState({
          imageLoaded: true
        });
      }
    }
  }

  /**
   * Helper to obtain the next state based on nextProps and prevState.
   *
   * NOTE: Rename this method to getDerivedStateFromProps when we update React
   *       to >= 16.3. We will need to update tests as well. We cannot rename this
   *       method to getDerivedStateFromProps now because there is a mismatch in
   *       the React version that we are using for both testing and production.
   *       (i.e. react-test-render => "16.3.2", react => "16.2.0").
   *
   * See https://github.com/airbnb/enzyme/blob/master/packages/enzyme-adapter-react-16/package.json#L43.
   */
  static getNextStateFromProps(nextProps, prevState) {
    const {
      image
    } = nextProps.link;
    const imageInState = ScreenshotUtils.isRemoteImageLocal(prevState.cardImage, image);
    let nextState = null;

    // Image is updating.
    if (!imageInState && nextProps.link) {
      nextState = {
        imageLoaded: false
      };
    }
    if (imageInState) {
      return nextState;
    }

    // Since image was updated, attempt to revoke old image blob URL, if it exists.
    ScreenshotUtils.maybeRevokeBlobObjectURL(prevState.cardImage);
    nextState = nextState || {};
    nextState.cardImage = ScreenshotUtils.createLocalImageObject(image);
    return nextState;
  }
  onMenuButtonUpdate(isOpen) {
    if (isOpen) {
      this.setState({
        activeCard: this.props.index
      });
    } else {
      this.setState({
        activeCard: null
      });
    }
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    // Filter out "history" type for being the default
    if (this.props.link.type !== "history") {
      return {
        value: {
          card_type: this.props.link.type
        }
      };
    }
    return null;
  }
  onLinkClick(event) {
    event.preventDefault();
    const {
      altKey,
      button,
      ctrlKey,
      metaKey,
      shiftKey
    } = event;
    if (this.props.link.type === "download") {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.OPEN_DOWNLOAD_FILE,
        data: Object.assign(this.props.link, {
          event: {
            button,
            ctrlKey,
            metaKey,
            shiftKey
          }
        })
      }));
    } else {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.OPEN_LINK,
        data: Object.assign(this.props.link, {
          event: {
            altKey,
            button,
            ctrlKey,
            metaKey,
            shiftKey
          }
        })
      }));
    }
    if (this.props.isWebExtension) {
      this.props.dispatch(actionCreators.WebExtEvent(actionTypes.WEBEXT_CLICK, {
        source: this.props.eventSource,
        url: this.props.link.url,
        action_position: this.props.index
      }));
    } else {
      this.props.dispatch(actionCreators.UserEvent(Object.assign({
        event: "CLICK",
        source: this.props.eventSource,
        action_position: this.props.index
      }, this._getTelemetryInfo())));
      if (this.props.shouldSendImpressionStats) {
        this.props.dispatch(actionCreators.ImpressionStats({
          source: this.props.eventSource,
          click: 0,
          tiles: [{
            id: this.props.link.guid,
            pos: this.props.index
          }]
        }));
      }
    }
  }
  componentDidMount() {
    this.maybeLoadImage();
  }
  componentDidUpdate() {
    this.maybeLoadImage();
  }

  // NOTE: Remove this function when we update React to >= 16.3 since React will
  //       call getDerivedStateFromProps automatically. We will also need to
  //       rename getNextStateFromProps to getDerivedStateFromProps.
  componentWillMount() {
    const nextState = _Card.getNextStateFromProps(this.props, this.state);
    if (nextState) {
      this.setState(nextState);
    }
  }

  // NOTE: Remove this function when we update React to >= 16.3 since React will
  //       call getDerivedStateFromProps automatically. We will also need to
  //       rename getNextStateFromProps to getDerivedStateFromProps.
  componentWillReceiveProps(nextProps) {
    const nextState = _Card.getNextStateFromProps(nextProps, this.state);
    if (nextState) {
      this.setState(nextState);
    }
  }
  componentWillUnmount() {
    ScreenshotUtils.maybeRevokeBlobObjectURL(this.state.cardImage);
  }
  render() {
    const {
      index,
      className,
      link,
      dispatch,
      contextMenuOptions,
      eventSource,
      shouldSendImpressionStats
    } = this.props;
    const {
      props
    } = this;
    const title = link.title || link.hostname;
    const isContextMenuOpen = this.state.activeCard === index;
    // Display "now" as "trending" until we have new strings #3402
    const {
      icon,
      fluentID
    } = cardContextTypes[link.type === "now" ? "trending" : link.type] || {};
    const hasImage = this.state.cardImage || link.hasImage;
    const imageStyle = {
      backgroundImage: this.state.cardImage ? `url(${this.state.cardImage.url})` : "none"
    };
    const outerClassName = ["card-outer", className, isContextMenuOpen && "active", props.placeholder && "placeholder"].filter(v => v).join(" ");
    return /*#__PURE__*/external_React_default().createElement("li", {
      className: outerClassName
    }, /*#__PURE__*/external_React_default().createElement("a", {
      href: link.type === "pocket" ? link.open_url : link.url,
      onClick: !props.placeholder ? this.onLinkClick : undefined
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "card"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-preview-image-outer"
    }, hasImage && /*#__PURE__*/external_React_default().createElement("div", {
      className: `card-preview-image${this.state.imageLoaded ? " loaded" : ""}`,
      style: imageStyle
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-details"
    }, link.type === "download" && /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-host-name alternate",
      "data-l10n-id": "newtab-menu-open-file"
    }), link.hostname && /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-host-name"
    }, link.hostname.slice(0, 100), link.type === "download" && `  \u2014 ${link.description}`), /*#__PURE__*/external_React_default().createElement("div", {
      className: ["card-text", icon ? "" : "no-context", link.description ? "" : "no-description", link.hostname ? "" : "no-host-name"].join(" ")
    }, /*#__PURE__*/external_React_default().createElement("h4", {
      className: "card-title",
      dir: "auto"
    }, link.title), /*#__PURE__*/external_React_default().createElement("p", {
      className: "card-description",
      dir: "auto"
    }, link.description)), /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-context"
    }, icon && !link.context && /*#__PURE__*/external_React_default().createElement("span", {
      "aria-haspopup": "true",
      className: `card-context-icon icon icon-${icon}`
    }), link.icon && link.context && /*#__PURE__*/external_React_default().createElement("span", {
      "aria-haspopup": "true",
      className: "card-context-icon icon",
      style: {
        backgroundImage: `url('${link.icon}')`
      }
    }), fluentID && !link.context && /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-context-label",
      "data-l10n-id": fluentID
    }), link.context && /*#__PURE__*/external_React_default().createElement("div", {
      className: "card-context-label"
    }, link.context))))), !props.placeholder && /*#__PURE__*/external_React_default().createElement(ContextMenuButton, {
      tooltip: "newtab-menu-content-tooltip",
      tooltipArgs: {
        title
      },
      onUpdate: this.onMenuButtonUpdate
    }, /*#__PURE__*/external_React_default().createElement(LinkMenu, {
      dispatch: dispatch,
      index: index,
      source: eventSource,
      options: link.contextMenuOptions || contextMenuOptions,
      site: link,
      siteInfo: this._getTelemetryInfo(),
      shouldSendImpressionStats: shouldSendImpressionStats
    })));
  }
}
_Card.defaultProps = {
  link: {}
};
const Card = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  platform: state.Prefs.values.platform
}))(_Card);
const PlaceholderCard = props => /*#__PURE__*/external_React_default().createElement(Card, {
  placeholder: true,
  className: props.className
});
;// CONCATENATED MODULE: ./content-src/lib/perf-service.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

let usablePerfObj = window.performance;

function _PerfService(options) {
  // For testing, so that we can use a fake Window.performance object with
  // known state.
  if (options && options.performanceObj) {
    this._perf = options.performanceObj;
  } else {
    this._perf = usablePerfObj;
  }
}

_PerfService.prototype = {
  /**
   * Calls the underlying mark() method on the appropriate Window.performance
   * object to add a mark with the given name to the appropriate performance
   * timeline.
   *
   * @param  {String} name  the name to give the current mark
   * @return {void}
   */
  mark: function mark(str) {
    this._perf.mark(str);
  },

  /**
   * Calls the underlying getEntriesByName on the appropriate Window.performance
   * object.
   *
   * @param  {String} name
   * @param  {String} type eg "mark"
   * @return {Array}       Performance* objects
   */
  getEntriesByName: function getEntriesByName(entryName, type) {
    return this._perf.getEntriesByName(entryName, type);
  },

  /**
   * The timeOrigin property from the appropriate performance object.
   * Used to ensure that timestamps from the add-on code and the content code
   * are comparable.
   *
   * @note If this is called from a context without a window
   * (eg a JSM in chrome), it will return the timeOrigin of the XUL hidden
   * window, which appears to be the first created window (and thus
   * timeOrigin) in the browser.  Note also, however, there is also a private
   * hidden window, presumably for private browsing, which appears to be
   * created dynamically later.  Exactly how/when that shows up needs to be
   * investigated.
   *
   * @return {Number} A double of milliseconds with a precision of 0.5us.
   */
  get timeOrigin() {
    return this._perf.timeOrigin;
  },

  /**
   * Returns the "absolute" version of performance.now(), i.e. one that
   * should ([bug 1401406](https://bugzilla.mozilla.org/show_bug.cgi?id=1401406)
   * be comparable across both chrome and content.
   *
   * @return {Number}
   */
  absNow: function absNow() {
    return this.timeOrigin + this._perf.now();
  },

  /**
   * This returns the absolute startTime from the most recent performance.mark()
   * with the given name.
   *
   * @param  {String} name  the name to lookup the start time for
   *
   * @return {Number}       the returned start time, as a DOMHighResTimeStamp
   *
   * @throws {Error}        "No Marks with the name ..." if none are available
   *
   * @note Always surround calls to this by try/catch.  Otherwise your code
   * may fail when the `privacy.resistFingerprinting` pref is true.  When
   * this pref is set, all attempts to get marks will likely fail, which will
   * cause this method to throw.
   *
   * See [bug 1369303](https://bugzilla.mozilla.org/show_bug.cgi?id=1369303)
   * for more info.
   */
  getMostRecentAbsMarkStartByName(entryName) {
    let entries = this.getEntriesByName(entryName, "mark");

    if (!entries.length) {
      throw new Error(`No marks with the name ${entryName}`);
    }

    let mostRecentEntry = entries[entries.length - 1];
    return this._perf.timeOrigin + mostRecentEntry.startTime;
  },
};

const perfService = new _PerfService();

;// CONCATENATED MODULE: ./content-src/components/ComponentPerfTimer/ComponentPerfTimer.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





// Currently record only a fixed set of sections. This will prevent data
// from custom sections from showing up or from topstories.
const RECORDED_SECTIONS = ["highlights", "topsites"];
class ComponentPerfTimer extends (external_React_default()).Component {
  constructor(props) {
    super(props);
    // Just for test dependency injection:
    this.perfSvc = this.props.perfSvc || perfService;
    this._sendBadStateEvent = this._sendBadStateEvent.bind(this);
    this._sendPaintedEvent = this._sendPaintedEvent.bind(this);
    this._reportMissingData = false;
    this._timestampHandled = false;
    this._recordedFirstRender = false;
  }
  componentDidMount() {
    if (!RECORDED_SECTIONS.includes(this.props.id)) {
      return;
    }
    this._maybeSendPaintedEvent();
  }
  componentDidUpdate() {
    if (!RECORDED_SECTIONS.includes(this.props.id)) {
      return;
    }
    this._maybeSendPaintedEvent();
  }

  /**
   * Call the given callback after the upcoming frame paints.
   *
   * @note Both setTimeout and requestAnimationFrame are throttled when the page
   * is hidden, so this callback may get called up to a second or so after the
   * requestAnimationFrame "paint" for hidden tabs.
   *
   * Newtabs hidden while loading will presumably be fairly rare (other than
   * preloaded tabs, which we will be filtering out on the server side), so such
   * cases should get lost in the noise.
   *
   * If we decide that it's important to find out when something that's hidden
   * has "painted", however, another option is to post a message to this window.
   * That should happen even faster than setTimeout, and, at least as of this
   * writing, it's not throttled in hidden windows in Firefox.
   *
   * @param {Function} callback
   *
   * @returns void
   */
  _afterFramePaint(callback) {
    requestAnimationFrame(() => setTimeout(callback, 0));
  }
  _maybeSendBadStateEvent() {
    // Follow up bugs:
    // https://github.com/mozilla/activity-stream/issues/3691
    if (!this.props.initialized) {
      // Remember to report back when data is available.
      this._reportMissingData = true;
    } else if (this._reportMissingData) {
      this._reportMissingData = false;
      // Report how long it took for component to become initialized.
      this._sendBadStateEvent();
    }
  }
  _maybeSendPaintedEvent() {
    // If we've already handled a timestamp, don't do it again.
    if (this._timestampHandled || !this.props.initialized) {
      return;
    }

    // And if we haven't, we're doing so now, so remember that. Even if
    // something goes wrong in the callback, we can't try again, as we'd be
    // sending back the wrong data, and we have to do it here, so that other
    // calls to this method while waiting for the next frame won't also try to
    // handle it.
    this._timestampHandled = true;
    this._afterFramePaint(this._sendPaintedEvent);
  }

  /**
   * Triggered by call to render. Only first call goes through due to
   * `_recordedFirstRender`.
   */
  _ensureFirstRenderTsRecorded() {
    // Used as t0 for recording how long component took to initialize.
    if (!this._recordedFirstRender) {
      this._recordedFirstRender = true;
      // topsites_first_render_ts, highlights_first_render_ts.
      const key = `${this.props.id}_first_render_ts`;
      this.perfSvc.mark(key);
    }
  }

  /**
   * Creates `SAVE_SESSION_PERF_DATA` with timestamp in ms
   * of how much longer the data took to be ready for display than it would
   * have been the ideal case.
   * https://github.com/mozilla/ping-centre/issues/98
   */
  _sendBadStateEvent() {
    // highlights_data_ready_ts, topsites_data_ready_ts.
    const dataReadyKey = `${this.props.id}_data_ready_ts`;
    this.perfSvc.mark(dataReadyKey);
    try {
      const firstRenderKey = `${this.props.id}_first_render_ts`;
      // value has to be Int32.
      const value = parseInt(this.perfSvc.getMostRecentAbsMarkStartByName(dataReadyKey) - this.perfSvc.getMostRecentAbsMarkStartByName(firstRenderKey), 10);
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.SAVE_SESSION_PERF_DATA,
        // highlights_data_late_by_ms, topsites_data_late_by_ms.
        data: {
          [`${this.props.id}_data_late_by_ms`]: value
        }
      }));
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.
    }
  }
  _sendPaintedEvent() {
    // Record first_painted event but only send if topsites.
    if (this.props.id !== "topsites") {
      return;
    }

    // topsites_first_painted_ts.
    const key = `${this.props.id}_first_painted_ts`;
    this.perfSvc.mark(key);
    try {
      const data = {};
      data[key] = this.perfSvc.getMostRecentAbsMarkStartByName(key);
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.SAVE_SESSION_PERF_DATA,
        data
      }));
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.  We should at least not blow up, and should continue
      // to set this._timestampHandled to avoid going through this again.
    }
  }
  render() {
    if (RECORDED_SECTIONS.includes(this.props.id)) {
      this._ensureFirstRenderTsRecorded();
      this._maybeSendBadStateEvent();
    }
    return this.props.children;
  }
}
;// CONCATENATED MODULE: ./content-src/components/MoreRecommendations/MoreRecommendations.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class MoreRecommendations extends (external_React_default()).PureComponent {
  render() {
    const {
      read_more_endpoint
    } = this.props;
    if (read_more_endpoint) {
      return /*#__PURE__*/external_React_default().createElement("a", {
        className: "more-recommendations",
        href: read_more_endpoint,
        "data-l10n-id": "newtab-pocket-more-recommendations"
      });
    }
    return null;
  }
}
;// CONCATENATED MODULE: ./content-src/components/PocketLoggedInCta/PocketLoggedInCta.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



class _PocketLoggedInCta extends (external_React_default()).PureComponent {
  render() {
    const {
      pocketCta
    } = this.props.Pocket;
    return /*#__PURE__*/external_React_default().createElement("span", {
      className: "pocket-logged-in-cta"
    }, /*#__PURE__*/external_React_default().createElement("a", {
      className: "pocket-cta-button",
      href: pocketCta.ctaUrl ? pocketCta.ctaUrl : "https://getpocket.com/"
    }, pocketCta.ctaButton ? pocketCta.ctaButton : /*#__PURE__*/external_React_default().createElement("span", {
      "data-l10n-id": "newtab-pocket-cta-button"
    })), /*#__PURE__*/external_React_default().createElement("a", {
      href: pocketCta.ctaUrl ? pocketCta.ctaUrl : "https://getpocket.com/"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "cta-text"
    }, pocketCta.ctaText ? pocketCta.ctaText : /*#__PURE__*/external_React_default().createElement("span", {
      "data-l10n-id": "newtab-pocket-cta-text"
    }))));
  }
}
const PocketLoggedInCta = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Pocket: state.Pocket
}))(_PocketLoggedInCta);
;// CONCATENATED MODULE: ./content-src/components/TopSites/SearchShortcutsForm.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class SelectableSearchShortcut extends (external_React_default()).PureComponent {
  render() {
    const {
      shortcut,
      selected
    } = this.props;
    const imageStyle = {
      backgroundImage: `url("${shortcut.tippyTopIcon}")`
    };
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-outer search-shortcut"
    }, /*#__PURE__*/external_React_default().createElement("input", {
      type: "checkbox",
      id: shortcut.keyword,
      name: shortcut.keyword,
      checked: selected,
      onChange: this.props.onChange
    }), /*#__PURE__*/external_React_default().createElement("label", {
      htmlFor: shortcut.keyword
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-inner"
    }, /*#__PURE__*/external_React_default().createElement("span", null, /*#__PURE__*/external_React_default().createElement("div", {
      className: "tile"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-icon rich-icon",
      style: imageStyle,
      "data-fallback": "@"
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-icon search-topsite"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "title"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      dir: "auto"
    }, shortcut.keyword))))));
  }
}
class SearchShortcutsForm extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.handleChange = this.handleChange.bind(this);
    this.onCancelButtonClick = this.onCancelButtonClick.bind(this);
    this.onSaveButtonClick = this.onSaveButtonClick.bind(this);

    // clone the shortcuts and add them to the state so we can add isSelected property
    const shortcuts = [];
    const {
      rows,
      searchShortcuts
    } = props.TopSites;
    searchShortcuts.forEach(shortcut => {
      shortcuts.push({
        ...shortcut,
        isSelected: !!rows.find(row => row && row.isPinned && row.searchTopSite && row.label === shortcut.keyword)
      });
    });
    this.state = {
      shortcuts
    };
  }
  handleChange(event) {
    const {
      target
    } = event;
    const {
      name: targetName,
      checked
    } = target;
    this.setState(prevState => {
      const shortcuts = prevState.shortcuts.slice();
      let shortcut = shortcuts.find(({
        keyword
      }) => keyword === targetName);
      shortcut.isSelected = checked;
      return {
        shortcuts
      };
    });
  }
  onCancelButtonClick(ev) {
    ev.preventDefault();
    this.props.onClose();
  }
  onSaveButtonClick(ev) {
    ev.preventDefault();

    // Check if there were any changes and act accordingly
    const {
      rows
    } = this.props.TopSites;
    const pinQueue = [];
    const unpinQueue = [];
    this.state.shortcuts.forEach(shortcut => {
      const alreadyPinned = rows.find(row => row && row.isPinned && row.searchTopSite && row.label === shortcut.keyword);
      if (shortcut.isSelected && !alreadyPinned) {
        pinQueue.push(this._searchTopSite(shortcut));
      } else if (!shortcut.isSelected && alreadyPinned) {
        unpinQueue.push({
          url: alreadyPinned.url,
          searchVendor: shortcut.shortURL
        });
      }
    });

    // Tell the feed to do the work.
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.UPDATE_PINNED_SEARCH_SHORTCUTS,
      data: {
        addedShortcuts: pinQueue,
        deletedShortcuts: unpinQueue
      }
    }));

    // Send the Telemetry pings.
    pinQueue.forEach(shortcut => {
      this.props.dispatch(actionCreators.UserEvent({
        source: TOP_SITES_SOURCE,
        event: "SEARCH_EDIT_ADD",
        value: {
          search_vendor: shortcut.searchVendor
        }
      }));
    });
    unpinQueue.forEach(shortcut => {
      this.props.dispatch(actionCreators.UserEvent({
        source: TOP_SITES_SOURCE,
        event: "SEARCH_EDIT_DELETE",
        value: {
          search_vendor: shortcut.searchVendor
        }
      }));
    });
    this.props.onClose();
  }
  _searchTopSite(shortcut) {
    return {
      url: shortcut.url,
      searchTopSite: true,
      label: shortcut.keyword,
      searchVendor: shortcut.shortURL
    };
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("form", {
      className: "topsite-form"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "search-shortcuts-container"
    }, /*#__PURE__*/external_React_default().createElement("h3", {
      className: "section-title grey-title",
      "data-l10n-id": "newtab-topsites-add-search-engine-header"
    }), /*#__PURE__*/external_React_default().createElement("div", null, this.state.shortcuts.map(shortcut => /*#__PURE__*/external_React_default().createElement(SelectableSearchShortcut, {
      key: shortcut.keyword,
      shortcut: shortcut,
      selected: shortcut.isSelected,
      onChange: this.handleChange
    })))), /*#__PURE__*/external_React_default().createElement("section", {
      className: "actions"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "cancel",
      type: "button",
      onClick: this.onCancelButtonClick,
      "data-l10n-id": "newtab-topsites-cancel-button"
    }), /*#__PURE__*/external_React_default().createElement("button", {
      className: "done",
      type: "submit",
      onClick: this.onSaveButtonClick,
      "data-l10n-id": "newtab-topsites-save-button"
    })));
  }
}
;// CONCATENATED MODULE: ../../modules/Dedupe.sys.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

class Dedupe {
  constructor(createKey) {
    this.createKey = createKey || this.defaultCreateKey;
  }

  defaultCreateKey(item) {
    return item;
  }

  /**
   * Dedupe any number of grouped elements favoring those from earlier groups.
   *
   * @param {Array} groups Contains an arbitrary number of arrays of elements.
   * @returns {Array} A matching array of each provided group deduped.
   */
  group(...groups) {
    const globalKeys = new Set();
    const result = [];
    for (const values of groups) {
      const valueMap = new Map();
      for (const value of values) {
        const key = this.createKey(value);
        if (!globalKeys.has(key) && !valueMap.has(key)) {
          valueMap.set(key, value);
        }
      }
      result.push(valueMap);
      valueMap.forEach((value, key) => globalKeys.add(key));
    }
    return result.map(m => Array.from(m.values()));
  }
}

;// CONCATENATED MODULE: ../../components/topsites/constants.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TOP_SITES_DEFAULT_ROWS = 1;
const TOP_SITES_MAX_SITES_PER_ROW = 8;

;// CONCATENATED MODULE: ./common/Reducers.sys.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */






const PREF_COLLECTION_DISMISSIBLE = "discoverystream.isCollectionDismissible";

const dedupe = new Dedupe(site => site && site.url);

const INITIAL_STATE = {
  App: {
    // Have we received real data from the app yet?
    initialized: false,
    locale: "",
    isForStartupCache: false,
    customizeMenuVisible: false,
  },
  Ads: {
    initialized: false,
    lastUpdated: null,
    topsites: {},
  },
  TopSites: {
    // Have we received real data from history yet?
    initialized: false,
    // The history (and possibly default) links
    rows: [],
    // Used in content only to dispatch action to TopSiteForm.
    editForm: null,
    // Used in content only to open the SearchShortcutsForm modal.
    showSearchShortcutsForm: false,
    // The list of available search shortcuts.
    searchShortcuts: [],
    // The "Share-of-Voice" allocations generated by TopSitesFeed
    sov: {
      ready: false,
      positions: [
        // {position: 0, assignedPartner: "amp"},
        // {position: 1, assignedPartner: "moz-sales"},
      ],
    },
  },
  Prefs: {
    initialized: false,
    values: { featureConfig: {} },
  },
  Dialog: {
    visible: false,
    data: {},
  },
  Sections: [],
  Pocket: {
    isUserLoggedIn: null,
    pocketCta: {},
    waitingForSpoc: true,
  },
  // This is the new pocket configurable layout state.
  DiscoveryStream: {
    // This is a JSON-parsed copy of the discoverystream.config pref value.
    config: { enabled: false },
    layout: [],
    isPrivacyInfoModalVisible: false,
    isCollectionDismissible: false,
    topicsLoading: false,
    feeds: {
      data: {
        // "https://foo.com/feed1": {lastUpdated: 123, data: [], personalized: false}
      },
      loaded: false,
    },
    // Used to show impressions in newtab devtools.
    impressions: {
      feed: {},
    },
    // Used to show blocks in newtab devtools.
    blocks: {},
    spocs: {
      spocs_endpoint: "",
      lastUpdated: null,
      data: {
        // "spocs": {title: "", context: "", items: [], personalized: false},
        // "placement1": {title: "", context: "", items: [], personalized: false},
      },
      loaded: false,
      frequency_caps: [],
      blocked: [],
      placements: [],
    },
    experimentData: {
      utmSource: "pocket-newtab",
      utmCampaign: undefined,
      utmContent: undefined,
    },
    recentSavesData: [],
    isUserLoggedIn: false,
    recentSavesEnabled: false,
    showTopicSelection: false,
    report: {
      visible: false,
      data: {},
    },
    sectionPersonalization: {},
  },
  // Messages received from ASRouter to render in newtab
  Messages: {
    // messages received from ASRouter are initially visible
    isHidden: false,
    // portID for that tab that was sent the message
    portID: "",
    // READONLY Message data received from ASRouter
    messageData: {},
  },
  Notifications: {
    showNotifications: false,
    toastCounter: 0,
    toastId: "",
    // This queue is reset each time SHOW_TOAST_MESSAGE is ran.
    // For can be a queue in the future, but for now is one item
    toastQueue: [],
  },
  Personalization: {
    lastUpdated: null,
    initialized: false,
  },
  Search: {
    // When search hand-off is enabled, we render a big button that is styled to
    // look like a search textbox. If the button is clicked, we style
    // the button as if it was a focused search box and show a fake cursor but
    // really focus the awesomebar without the focus styles ("hidden focus").
    fakeFocus: false,
    // Hide the search box after handing off to AwesomeBar and user starts typing.
    hide: false,
  },
  Wallpapers: {
    wallpaperList: [],
    highlightSeenCounter: 0,
    categories: [],
    uploadedWallpaper: "",
  },
  Weather: {
    initialized: false,
    lastUpdated: null,
    query: "",
    suggestions: [],
    locationData: {
      city: "",
      adminArea: "",
      country: "",
    },
    // Display search input in Weather widget
    searchActive: false,
    locationSearchString: "",
    suggestedLocations: [],
  },
};

function App(prevState = INITIAL_STATE.App, action) {
  switch (action.type) {
    case actionTypes.INIT:
      return Object.assign({}, prevState, action.data || {}, {
        initialized: true,
      });
    case actionTypes.TOP_SITES_UPDATED:
      // Toggle `isForStartupCache` when receiving the `TOP_SITES_UPDATE` action
      // so that sponsored tiles can be rendered as usual. See Bug 1826360.
      return Object.assign({}, prevState, action.data || {}, {
        isForStartupCache: false,
      });
    case actionTypes.DISCOVERY_STREAM_SPOCS_UPDATE:
      // Toggle `isForStartupCache` when receiving the `DISCOVERY_STREAM_SPOCS_UPDATE_STARTUPCACHE` action
      // so that spoc cards can be rendered as usual.
      return Object.assign({}, prevState, action.data || {}, {
        isForStartupCache: false,
      });
    case actionTypes.SHOW_PERSONALIZE:
      return Object.assign({}, prevState, {
        customizeMenuVisible: true,
      });
    case actionTypes.HIDE_PERSONALIZE:
      return Object.assign({}, prevState, {
        customizeMenuVisible: false,
      });
    default:
      return prevState;
  }
}

function TopSites(prevState = INITIAL_STATE.TopSites, action) {
  let hasMatch;
  let newRows;
  switch (action.type) {
    case actionTypes.TOP_SITES_UPDATED:
      if (!action.data || !action.data.links) {
        return prevState;
      }
      return Object.assign(
        {},
        prevState,
        { initialized: true, rows: action.data.links },
        action.data.pref ? { pref: action.data.pref } : {}
      );
    case actionTypes.TOP_SITES_PREFS_UPDATED:
      return Object.assign({}, prevState, { pref: action.data.pref });
    case actionTypes.TOP_SITES_EDIT:
      return Object.assign({}, prevState, {
        editForm: {
          index: action.data.index,
          previewResponse: null,
        },
      });
    case actionTypes.TOP_SITES_CANCEL_EDIT:
      return Object.assign({}, prevState, { editForm: null });
    case actionTypes.TOP_SITES_OPEN_SEARCH_SHORTCUTS_MODAL:
      return Object.assign({}, prevState, { showSearchShortcutsForm: true });
    case actionTypes.TOP_SITES_CLOSE_SEARCH_SHORTCUTS_MODAL:
      return Object.assign({}, prevState, { showSearchShortcutsForm: false });
    case actionTypes.PREVIEW_RESPONSE:
      if (
        !prevState.editForm ||
        action.data.url !== prevState.editForm.previewUrl
      ) {
        return prevState;
      }
      return Object.assign({}, prevState, {
        editForm: {
          index: prevState.editForm.index,
          previewResponse: action.data.preview,
          previewUrl: action.data.url,
        },
      });
    case actionTypes.PREVIEW_REQUEST:
      if (!prevState.editForm) {
        return prevState;
      }
      return Object.assign({}, prevState, {
        editForm: {
          index: prevState.editForm.index,
          previewResponse: null,
          previewUrl: action.data.url,
        },
      });
    case actionTypes.PREVIEW_REQUEST_CANCEL:
      if (!prevState.editForm) {
        return prevState;
      }
      return Object.assign({}, prevState, {
        editForm: {
          index: prevState.editForm.index,
          previewResponse: null,
        },
      });
    case actionTypes.SCREENSHOT_UPDATED:
      newRows = prevState.rows.map(row => {
        if (row && row.url === action.data.url) {
          hasMatch = true;
          return Object.assign({}, row, { screenshot: action.data.screenshot });
        }
        return row;
      });
      return hasMatch
        ? Object.assign({}, prevState, { rows: newRows })
        : prevState;
    case actionTypes.PLACES_BOOKMARK_ADDED:
      if (!action.data) {
        return prevState;
      }
      newRows = prevState.rows.map(site => {
        if (site && site.url === action.data.url) {
          const { bookmarkGuid, bookmarkTitle, dateAdded } = action.data;
          return Object.assign({}, site, {
            bookmarkGuid,
            bookmarkTitle,
            bookmarkDateCreated: dateAdded,
          });
        }
        return site;
      });
      return Object.assign({}, prevState, { rows: newRows });
    case actionTypes.PLACES_BOOKMARKS_REMOVED:
      if (!action.data) {
        return prevState;
      }
      newRows = prevState.rows.map(site => {
        if (site && action.data.urls.includes(site.url)) {
          const newSite = Object.assign({}, site);
          delete newSite.bookmarkGuid;
          delete newSite.bookmarkTitle;
          delete newSite.bookmarkDateCreated;
          return newSite;
        }
        return site;
      });
      return Object.assign({}, prevState, { rows: newRows });
    case actionTypes.PLACES_LINKS_DELETED:
      if (!action.data) {
        return prevState;
      }
      newRows = prevState.rows.filter(
        site => !action.data.urls.includes(site.url)
      );
      return Object.assign({}, prevState, { rows: newRows });
    case actionTypes.UPDATE_SEARCH_SHORTCUTS:
      return { ...prevState, searchShortcuts: action.data.searchShortcuts };
    case actionTypes.SOV_UPDATED: {
      const sov = {
        ready: action.data.ready,
        positions: action.data.positions,
      };
      return { ...prevState, sov };
    }
    default:
      return prevState;
  }
}

function Dialog(prevState = INITIAL_STATE.Dialog, action) {
  switch (action.type) {
    case actionTypes.DIALOG_OPEN:
      return Object.assign({}, prevState, { visible: true, data: action.data });
    case actionTypes.DIALOG_CANCEL:
      return Object.assign({}, prevState, { visible: false });
    case actionTypes.DIALOG_CLOSE:
      // Reset and hide the confirmation dialog once the action is complete.
      return Object.assign({}, INITIAL_STATE.Dialog);
    default:
      return prevState;
  }
}

function Prefs(prevState = INITIAL_STATE.Prefs, action) {
  let newValues;
  switch (action.type) {
    case actionTypes.PREFS_INITIAL_VALUES:
      return Object.assign({}, prevState, {
        initialized: true,
        values: action.data,
      });
    case actionTypes.PREF_CHANGED:
      newValues = Object.assign({}, prevState.values);
      newValues[action.data.name] = action.data.value;
      return Object.assign({}, prevState, { values: newValues });
    default:
      return prevState;
  }
}

function Sections(prevState = INITIAL_STATE.Sections, action) {
  let hasMatch;
  let newState;
  switch (action.type) {
    case actionTypes.SECTION_DEREGISTER:
      return prevState.filter(section => section.id !== action.data);
    case actionTypes.SECTION_REGISTER:
      // If section exists in prevState, update it
      newState = prevState.map(section => {
        if (section && section.id === action.data.id) {
          hasMatch = true;
          return Object.assign({}, section, action.data);
        }
        return section;
      });
      // Otherwise, append it
      if (!hasMatch) {
        const initialized = !!(action.data.rows && !!action.data.rows.length);
        const section = Object.assign(
          { title: "", rows: [], enabled: false },
          action.data,
          { initialized }
        );
        newState.push(section);
      }
      return newState;
    case actionTypes.SECTION_UPDATE:
      newState = prevState.map(section => {
        if (section && section.id === action.data.id) {
          // If the action is updating rows, we should consider initialized to be true.
          // This can be overridden if initialized is defined in the action.data
          const initialized = action.data.rows ? { initialized: true } : {};

          // Make sure pinned cards stay at their current position when rows are updated.
          // Disabling a section (SECTION_UPDATE with empty rows) does not retain pinned cards.
          if (
            action.data.rows &&
            !!action.data.rows.length &&
            section.rows.find(card => card.pinned)
          ) {
            const rows = Array.from(action.data.rows);
            section.rows.forEach((card, index) => {
              if (card.pinned) {
                // Only add it if it's not already there.
                if (rows[index].guid !== card.guid) {
                  rows.splice(index, 0, card);
                }
              }
            });
            return Object.assign(
              {},
              section,
              initialized,
              Object.assign({}, action.data, { rows })
            );
          }

          return Object.assign({}, section, initialized, action.data);
        }
        return section;
      });

      if (!action.data.dedupeConfigurations) {
        return newState;
      }

      action.data.dedupeConfigurations.forEach(dedupeConf => {
        newState = newState.map(section => {
          if (section.id === dedupeConf.id) {
            const dedupedRows = dedupeConf.dedupeFrom.reduce(
              (rows, dedupeSectionId) => {
                const dedupeSection = newState.find(
                  s => s.id === dedupeSectionId
                );
                const [, newRows] = dedupe.group(dedupeSection.rows, rows);
                return newRows;
              },
              section.rows
            );

            return Object.assign({}, section, { rows: dedupedRows });
          }

          return section;
        });
      });

      return newState;
    case actionTypes.SECTION_UPDATE_CARD:
      return prevState.map(section => {
        if (section && section.id === action.data.id && section.rows) {
          const newRows = section.rows.map(card => {
            if (card.url === action.data.url) {
              return Object.assign({}, card, action.data.options);
            }
            return card;
          });
          return Object.assign({}, section, { rows: newRows });
        }
        return section;
      });
    case actionTypes.PLACES_BOOKMARK_ADDED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.map(item => {
            // find the item within the rows that is attempted to be bookmarked
            if (item.url === action.data.url) {
              const { bookmarkGuid, bookmarkTitle, dateAdded } = action.data;
              return Object.assign({}, item, {
                bookmarkGuid,
                bookmarkTitle,
                bookmarkDateCreated: dateAdded,
                type: "bookmark",
              });
            }
            return item;
          }),
        })
      );
    case actionTypes.PLACES_SAVED_TO_POCKET:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.map(item => {
            if (item.url === action.data.url) {
              return Object.assign({}, item, {
                open_url: action.data.open_url,
                pocket_id: action.data.pocket_id,
                title: action.data.title,
                type: "pocket",
              });
            }
            return item;
          }),
        })
      );
    case actionTypes.PLACES_BOOKMARKS_REMOVED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.map(item => {
            // find the bookmark within the rows that is attempted to be removed
            if (action.data.urls.includes(item.url)) {
              const newSite = Object.assign({}, item);
              delete newSite.bookmarkGuid;
              delete newSite.bookmarkTitle;
              delete newSite.bookmarkDateCreated;
              if (!newSite.type || newSite.type === "bookmark") {
                newSite.type = "history";
              }
              return newSite;
            }
            return item;
          }),
        })
      );
    case actionTypes.PLACES_LINKS_DELETED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.filter(
            site => !action.data.urls.includes(site.url)
          ),
        })
      );
    case actionTypes.PLACES_LINK_BLOCKED:
      if (!action.data) {
        return prevState;
      }
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.filter(site => site.url !== action.data.url),
        })
      );
    case actionTypes.DELETE_FROM_POCKET:
    case actionTypes.ARCHIVE_FROM_POCKET:
      return prevState.map(section =>
        Object.assign({}, section, {
          rows: section.rows.filter(
            site => site.pocket_id !== action.data.pocket_id
          ),
        })
      );
    default:
      return prevState;
  }
}

function Messages(prevState = INITIAL_STATE.Messages, action) {
  switch (action.type) {
    case actionTypes.MESSAGE_SET:
      if (prevState.messageData.messageType) {
        return prevState;
      }
      return {
        ...prevState,
        messageData: action.data.message,
        portID: action.data.portID || "",
      };
    case actionTypes.MESSAGE_TOGGLE_VISIBILITY:
      return { ...prevState, isHidden: action.data };
    default:
      return prevState;
  }
}

function Pocket(prevState = INITIAL_STATE.Pocket, action) {
  switch (action.type) {
    case actionTypes.POCKET_WAITING_FOR_SPOC:
      return { ...prevState, waitingForSpoc: action.data };
    case actionTypes.POCKET_LOGGED_IN:
      return { ...prevState, isUserLoggedIn: !!action.data };
    case actionTypes.POCKET_CTA:
      return {
        ...prevState,
        pocketCta: {
          ctaButton: action.data.cta_button,
          ctaText: action.data.cta_text,
          ctaUrl: action.data.cta_url,
          useCta: action.data.use_cta,
        },
      };
    default:
      return prevState;
  }
}

function Reducers_sys_Personalization(prevState = INITIAL_STATE.Personalization, action) {
  switch (action.type) {
    case actionTypes.DISCOVERY_STREAM_PERSONALIZATION_LAST_UPDATED:
      return {
        ...prevState,
        lastUpdated: action.data.lastUpdated,
      };
    case actionTypes.DISCOVERY_STREAM_PERSONALIZATION_INIT:
      return {
        ...prevState,
        initialized: true,
      };
    case actionTypes.DISCOVERY_STREAM_PERSONALIZATION_RESET:
      return { ...INITIAL_STATE.Personalization };
    default:
      return prevState;
  }
}

// eslint-disable-next-line complexity
function DiscoveryStream(prevState = INITIAL_STATE.DiscoveryStream, action) {
  // Return if action data is empty, or spocs or feeds data is not loaded
  const isNotReady = () =>
    !action.data || !prevState.spocs.loaded || !prevState.feeds.loaded;

  const handlePlacements = handleSites => {
    const { data, placements } = prevState.spocs;
    const result = {};

    const forPlacement = placement => {
      const placementSpocs = data[placement.name];

      if (
        !placementSpocs ||
        !placementSpocs.items ||
        !placementSpocs.items.length
      ) {
        return;
      }

      result[placement.name] = {
        ...placementSpocs,
        items: handleSites(placementSpocs.items),
      };
    };

    if (!placements || !placements.length) {
      [{ name: "spocs" }].forEach(forPlacement);
    } else {
      placements.forEach(forPlacement);
    }
    return result;
  };

  const nextState = handleSites => ({
    ...prevState,
    spocs: {
      ...prevState.spocs,
      data: handlePlacements(handleSites),
    },
    feeds: {
      ...prevState.feeds,
      data: Object.keys(prevState.feeds.data).reduce(
        (accumulator, feed_url) => {
          accumulator[feed_url] = {
            data: {
              ...prevState.feeds.data[feed_url].data,
              recommendations: handleSites(
                prevState.feeds.data[feed_url].data.recommendations
              ),
            },
          };
          return accumulator;
        },
        {}
      ),
    },
  });

  switch (action.type) {
    case actionTypes.DISCOVERY_STREAM_CONFIG_CHANGE:
    // Fall through to a separate action is so it doesn't trigger a listener update on init
    case actionTypes.DISCOVERY_STREAM_CONFIG_SETUP:
      return { ...prevState, config: action.data || {} };
    case actionTypes.DISCOVERY_STREAM_EXPERIMENT_DATA:
      return { ...prevState, experimentData: action.data || {} };
    case actionTypes.DISCOVERY_STREAM_LAYOUT_UPDATE:
      return {
        ...prevState,
        layout: action.data.layout || [],
      };
    case actionTypes.DISCOVERY_STREAM_COLLECTION_DISMISSIBLE_TOGGLE:
      return {
        ...prevState,
        isCollectionDismissible: action.data.value,
      };
    case actionTypes.DISCOVERY_STREAM_TOPICS_LOADING:
      return {
        ...prevState,
        topicsLoading: action.data,
      };
    case actionTypes.DISCOVERY_STREAM_PREFS_SETUP:
      return {
        ...prevState,
        recentSavesEnabled: action.data.recentSavesEnabled,
        pocketButtonEnabled: action.data.pocketButtonEnabled,
        saveToPocketCard: action.data.saveToPocketCard,
        hideDescriptions: action.data.hideDescriptions,
        compactImages: action.data.compactImages,
        imageGradient: action.data.imageGradient,
        newSponsoredLabel: action.data.newSponsoredLabel,
        titleLines: action.data.titleLines,
        descLines: action.data.descLines,
        readTime: action.data.readTime,
      };
    case actionTypes.DISCOVERY_STREAM_RECENT_SAVES:
      return {
        ...prevState,
        recentSavesData: action.data.recentSaves,
      };
    case actionTypes.DISCOVERY_STREAM_POCKET_STATE_SET:
      return {
        ...prevState,
        isUserLoggedIn: action.data.isUserLoggedIn,
      };
    case actionTypes.HIDE_PRIVACY_INFO:
      return {
        ...prevState,
        isPrivacyInfoModalVisible: false,
      };
    case actionTypes.SHOW_PRIVACY_INFO:
      return {
        ...prevState,
        isPrivacyInfoModalVisible: true,
      };
    case actionTypes.DISCOVERY_STREAM_LAYOUT_RESET:
      return { ...INITIAL_STATE.DiscoveryStream, config: prevState.config };
    case actionTypes.DISCOVERY_STREAM_FEEDS_UPDATE:
      return {
        ...prevState,
        feeds: {
          ...prevState.feeds,
          loaded: true,
        },
      };
    case actionTypes.DISCOVERY_STREAM_FEED_UPDATE: {
      const newData = {};
      newData[action.data.url] = action.data.feed;
      return {
        ...prevState,
        feeds: {
          ...prevState.feeds,
          data: {
            ...prevState.feeds.data,
            ...newData,
          },
        },
      };
    }
    case actionTypes.DISCOVERY_STREAM_DEV_IMPRESSIONS:
      return {
        ...prevState,
        impressions: {
          ...prevState.impressions,
          feed: action.data,
        },
      };
    case actionTypes.DISCOVERY_STREAM_DEV_BLOCKS:
      return {
        ...prevState,
        blocks: action.data,
      };
    case actionTypes.DISCOVERY_STREAM_SPOCS_CAPS:
      return {
        ...prevState,
        spocs: {
          ...prevState.spocs,
          frequency_caps: [...prevState.spocs.frequency_caps, ...action.data],
        },
      };
    case actionTypes.DISCOVERY_STREAM_SPOCS_ENDPOINT:
      return {
        ...prevState,
        spocs: {
          ...INITIAL_STATE.DiscoveryStream.spocs,
          spocs_endpoint:
            action.data.url ||
            INITIAL_STATE.DiscoveryStream.spocs.spocs_endpoint,
        },
      };
    case actionTypes.DISCOVERY_STREAM_SPOCS_PLACEMENTS:
      return {
        ...prevState,
        spocs: {
          ...prevState.spocs,
          placements:
            action.data.placements ||
            INITIAL_STATE.DiscoveryStream.spocs.placements,
        },
      };
    case actionTypes.DISCOVERY_STREAM_SPOCS_UPDATE:
      if (action.data) {
        return {
          ...prevState,
          spocs: {
            ...prevState.spocs,
            lastUpdated: action.data.lastUpdated,
            data: action.data.spocs,
            loaded: true,
          },
        };
      }
      return prevState;
    case actionTypes.DISCOVERY_STREAM_SPOC_BLOCKED:
      return {
        ...prevState,
        spocs: {
          ...prevState.spocs,
          blocked: [...prevState.spocs.blocked, action.data.url],
        },
      };
    case actionTypes.DISCOVERY_STREAM_LINK_BLOCKED:
      return isNotReady()
        ? prevState
        : nextState(items =>
            items.filter(item => item.url !== action.data.url)
          );

    case actionTypes.PLACES_SAVED_TO_POCKET: {
      const addPocketInfo = item => {
        if (item.url === action.data.url) {
          return Object.assign({}, item, {
            open_url: action.data.open_url,
            pocket_id: action.data.pocket_id,
            context_type: "pocket",
          });
        }
        return item;
      };
      return isNotReady()
        ? prevState
        : nextState(items => items.map(addPocketInfo));
    }
    case actionTypes.DELETE_FROM_POCKET:
    case actionTypes.ARCHIVE_FROM_POCKET:
      return isNotReady()
        ? prevState
        : nextState(items =>
            items.filter(item => item.pocket_id !== action.data.pocket_id)
          );

    case actionTypes.PLACES_BOOKMARK_ADDED: {
      const updateBookmarkInfo = item => {
        if (item.url === action.data.url) {
          const { bookmarkGuid, bookmarkTitle, dateAdded } = action.data;
          return Object.assign({}, item, {
            bookmarkGuid,
            bookmarkTitle,
            bookmarkDateCreated: dateAdded,
            context_type: "bookmark",
          });
        }
        return item;
      };
      return isNotReady()
        ? prevState
        : nextState(items => items.map(updateBookmarkInfo));
    }
    case actionTypes.PLACES_BOOKMARKS_REMOVED: {
      const removeBookmarkInfo = item => {
        if (action.data.urls.includes(item.url)) {
          const newSite = Object.assign({}, item);
          delete newSite.bookmarkGuid;
          delete newSite.bookmarkTitle;
          delete newSite.bookmarkDateCreated;
          if (!newSite.context_type || newSite.context_type === "bookmark") {
            newSite.context_type = "removedBookmark";
          }
          return newSite;
        }
        return item;
      };
      return isNotReady()
        ? prevState
        : nextState(items => items.map(removeBookmarkInfo));
    }
    case actionTypes.PREF_CHANGED:
      if (action.data.name === PREF_COLLECTION_DISMISSIBLE) {
        return {
          ...prevState,
          isCollectionDismissible: action.data.value,
        };
      }
      return prevState;
    case actionTypes.TOPIC_SELECTION_SPOTLIGHT_OPEN:
      return {
        ...prevState,
        showTopicSelection: true,
      };
    case actionTypes.TOPIC_SELECTION_SPOTLIGHT_CLOSE:
      return {
        ...prevState,
        showTopicSelection: false,
      };
    case actionTypes.SECTION_BLOCKED:
      return {
        ...prevState,
        showBlockSectionConfirmation: true,
        sectionPersonalization: action.data,
      };
    case actionTypes.REPORT_AD_OPEN:
      return {
        ...prevState,
        report: {
          ...prevState.report,
          card_type: action.data?.card_type,
          position: action.data?.position,
          placement_id: action.data?.placement_id,
          reporting_url: action.data?.reporting_url,
          url: action.data?.url,
          visible: true,
        },
      };
    case actionTypes.REPORT_CONTENT_OPEN:
      return {
        ...prevState,
        report: {
          ...prevState.report,
          card_type: action.data?.card_type,
          corpus_item_id: action.data?.corpus_item_id,
          is_section_followed: action.data?.is_section_followed,
          received_rank: action.data?.received_rank,
          recommended_at: action.data?.recommended_at,
          scheduled_corpus_item_id: action.data?.scheduled_corpus_item_id,
          section_position: action.data?.section_position,
          section: action.data?.section,
          title: action.data?.title,
          topic: action.data?.topic,
          url: action.data?.url,
          visible: true,
        },
      };
    case actionTypes.REPORT_CLOSE:
    case actionTypes.REPORT_AD_SUBMIT:
    case actionTypes.REPORT_CONTENT_SUBMIT:
      return {
        ...prevState,
        report: {
          ...prevState.report,
          visible: false,
        },
      };
    case actionTypes.SECTION_PERSONALIZATION_UPDATE:
      return { ...prevState, sectionPersonalization: action.data };
    default:
      return prevState;
  }
}

function Search(prevState = INITIAL_STATE.Search, action) {
  switch (action.type) {
    case actionTypes.DISABLE_SEARCH:
      return Object.assign({ ...prevState, disable: true });
    case actionTypes.FAKE_FOCUS_SEARCH:
      return Object.assign({ ...prevState, fakeFocus: true });
    case actionTypes.SHOW_SEARCH:
      return Object.assign({ ...prevState, disable: false, fakeFocus: false });
    default:
      return prevState;
  }
}

function Wallpapers(prevState = INITIAL_STATE.Wallpapers, action) {
  switch (action.type) {
    case actionTypes.WALLPAPERS_SET:
      return {
        ...prevState,
        wallpaperList: action.data,
      };
    case actionTypes.WALLPAPERS_FEATURE_HIGHLIGHT_COUNTER_INCREMENT:
      return {
        ...prevState,
        highlightSeenCounter: action.data,
      };
    case actionTypes.WALLPAPERS_CATEGORY_SET:
      return { ...prevState, categories: action.data };
    case actionTypes.WALLPAPERS_CUSTOM_SET:
      return { ...prevState, uploadedWallpaper: action.data };
    default:
      return prevState;
  }
}

function Notifications(prevState = INITIAL_STATE.Notifications, action) {
  switch (action.type) {
    case actionTypes.SHOW_TOAST_MESSAGE:
      return {
        ...prevState,
        showNotifications: action.data.showNotifications,
        toastCounter: prevState.toastCounter + 1,
        toastId: action.data.toastId,
        toastQueue: [action.data.toastId],
      };
    case actionTypes.HIDE_TOAST_MESSAGE: {
      const { showNotifications, toastId: hiddenToastId } = action.data;
      const queuedToasts = [...prevState.toastQueue].filter(
        toastId => toastId !== hiddenToastId
      );
      return {
        ...prevState,
        toastCounter: queuedToasts.length,
        toastQueue: queuedToasts,
        toastId: "",
        showNotifications,
      };
    }
    default:
      return prevState;
  }
}

function Weather(prevState = INITIAL_STATE.Weather, action) {
  switch (action.type) {
    case actionTypes.WEATHER_UPDATE:
      return {
        ...prevState,
        suggestions: action.data.suggestions,
        lastUpdated: action.data.date,
        locationData: action.data.locationData || prevState.locationData,
        initialized: true,
      };
    case actionTypes.WEATHER_SEARCH_ACTIVE:
      return { ...prevState, searchActive: action.data };
    case actionTypes.WEATHER_LOCATION_SEARCH_UPDATE:
      return { ...prevState, locationSearchString: action.data };
    case actionTypes.WEATHER_LOCATION_SUGGESTIONS_UPDATE:
      return { ...prevState, suggestedLocations: action.data };
    case actionTypes.WEATHER_LOCATION_DATA_UPDATE:
      return { ...prevState, locationData: action.data };
    default:
      return prevState;
  }
}

function Ads(prevState = INITIAL_STATE.Ads, action) {
  switch (action.type) {
    case actionTypes.ADS_INIT:
      return {
        ...prevState,
        initialized: true,
      };
    case actionTypes.ADS_UPDATE_DATA:
      return {
        ...prevState,
        topsites: action.data,
      };
    default:
      return prevState;
  }
}

const reducers = {
  TopSites,
  App,
  Ads,
  Prefs,
  Dialog,
  Sections,
  Messages,
  Notifications,
  Pocket,
  Personalization: Reducers_sys_Personalization,
  DiscoveryStream,
  Search,
  Wallpapers,
  Weather,
};

;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSiteFormInput.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class TopSiteFormInput extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      validationError: this.props.validationError
    };
    this.onChange = this.onChange.bind(this);
    this.onMount = this.onMount.bind(this);
    this.onClearIconPress = this.onClearIconPress.bind(this);
  }
  componentWillReceiveProps(nextProps) {
    if (nextProps.shouldFocus && !this.props.shouldFocus) {
      this.input.focus();
    }
    if (nextProps.validationError && !this.props.validationError) {
      this.setState({
        validationError: true
      });
    }
    // If the component is in an error state but the value was cleared by the parent
    if (this.state.validationError && !nextProps.value) {
      this.setState({
        validationError: false
      });
    }
  }
  onClearIconPress(event) {
    // If there is input in the URL or custom image URL fields,
    // and we hit 'enter' while tabbed over the clear icon,
    // we should execute the function to clear the field.
    if (event.key === "Enter") {
      this.props.onClear();
    }
  }
  onChange(ev) {
    if (this.state.validationError) {
      this.setState({
        validationError: false
      });
    }
    this.props.onChange(ev);
  }
  onMount(input) {
    this.input = input;
  }
  renderLoadingOrCloseButton() {
    const showClearButton = this.props.value && this.props.onClear;
    if (this.props.loading) {
      return /*#__PURE__*/external_React_default().createElement("div", {
        className: "loading-container"
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "loading-animation"
      }));
    } else if (showClearButton) {
      return /*#__PURE__*/external_React_default().createElement("button", {
        type: "button",
        className: "icon icon-clear-input icon-button-style",
        onClick: this.props.onClear,
        onKeyPress: this.onClearIconPress
      });
    }
    return null;
  }
  render() {
    const {
      typeUrl
    } = this.props;
    const {
      validationError
    } = this.state;
    return /*#__PURE__*/external_React_default().createElement("label", null, /*#__PURE__*/external_React_default().createElement("span", {
      "data-l10n-id": this.props.titleId
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: `field ${typeUrl ? "url" : ""}${validationError ? " invalid" : ""}`
    }, /*#__PURE__*/external_React_default().createElement("input", {
      type: "text",
      value: this.props.value,
      ref: this.onMount,
      onChange: this.onChange,
      "data-l10n-id": this.props.placeholderId
      // Set focus on error if the url field is valid or when the input is first rendered and is empty
      // eslint-disable-next-line jsx-a11y/no-autofocus
      ,
      autoFocus: this.props.autoFocusOnOpen,
      disabled: this.props.loading
    }), this.renderLoadingOrCloseButton(), validationError && /*#__PURE__*/external_React_default().createElement("aside", {
      className: "error-tooltip",
      "data-l10n-id": this.props.errorMessageId
    })));
  }
}
TopSiteFormInput.defaultProps = {
  showClearButton: false,
  value: "",
  validationError: false
};
;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSiteImpressionWrapper.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



const TopSiteImpressionWrapper_VISIBLE = "visible";
const TopSiteImpressionWrapper_VISIBILITY_CHANGE_EVENT = "visibilitychange";

// Per analytical requirement, we set the minimal intersection ratio to
// 0.5, and an impression is identified when the wrapped item has at least
// 50% visibility.
//
// This constant is exported for unit test
const TopSiteImpressionWrapper_INTERSECTION_RATIO = 0.5;

/**
 * Impression wrapper for a TopSite tile.
 *
 * It makses use of the Intersection Observer API to detect the visibility,
 * and relies on page visibility to ensure the impression is reported
 * only when the component is visible on the page.
 */
class TopSiteImpressionWrapper extends (external_React_default()).PureComponent {
  _dispatchImpressionStats() {
    const {
      actionType,
      tile
    } = this.props;
    if (!actionType) {
      return;
    }
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionType,
      data: {
        type: "impression",
        ...tile
      }
    }));
  }
  setImpressionObserverOrAddListener() {
    const {
      props
    } = this;
    if (!props.dispatch) {
      return;
    }
    if (props.document.visibilityState === TopSiteImpressionWrapper_VISIBLE) {
      this.setImpressionObserver();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(TopSiteImpressionWrapper_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
      }
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === TopSiteImpressionWrapper_VISIBLE) {
          this.setImpressionObserver();
          props.document.removeEventListener(TopSiteImpressionWrapper_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
        }
      };
      props.document.addEventListener(TopSiteImpressionWrapper_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }

  /**
   * Set an impression observer for the wrapped component. It makes use of
   * the Intersection Observer API to detect if the wrapped component is
   * visible with a desired ratio, and only sends impression if that's the case.
   *
   * See more details about Intersection Observer API at:
   * https://developer.mozilla.org/en-US/docs/Web/API/Intersection_Observer_API
   */
  setImpressionObserver() {
    const {
      props
    } = this;
    if (!props.tile) {
      return;
    }
    this._handleIntersect = entries => {
      if (entries.some(entry => entry.isIntersecting && entry.intersectionRatio >= TopSiteImpressionWrapper_INTERSECTION_RATIO)) {
        this._dispatchImpressionStats();
        this.impressionObserver.unobserve(this.refs.topsite_impression_wrapper);
      }
    };
    const options = {
      threshold: TopSiteImpressionWrapper_INTERSECTION_RATIO
    };
    this.impressionObserver = new props.IntersectionObserver(this._handleIntersect, options);
    this.impressionObserver.observe(this.refs.topsite_impression_wrapper);
  }
  componentDidMount() {
    if (this.props.tile) {
      this.setImpressionObserverOrAddListener();
    }
  }
  componentWillUnmount() {
    if (this._handleIntersect && this.impressionObserver) {
      this.impressionObserver.unobserve(this.refs.topsite_impression_wrapper);
    }
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(TopSiteImpressionWrapper_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      ref: "topsite_impression_wrapper",
      className: "topsite-impression-observer"
    }, this.props.children);
  }
}
TopSiteImpressionWrapper.defaultProps = {
  IntersectionObserver: globalThis.IntersectionObserver,
  document: globalThis.document,
  actionType: null,
  tile: null
};
;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSite.jsx
function TopSite_extends() { TopSite_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return TopSite_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */











const SPOC_TYPE = "SPOC";
const NEWTAB_SOURCE = "newtab";

// For cases if we want to know if this is sponsored by either sponsored_position or type.
// We have two sources for sponsored topsites, and
// sponsored_position is set by one sponsored source, and type is set by another.
// This is not called in all cases, sometimes we want to know if it's one source
// or the other. This function is only applicable in cases where we only care if it's either.
function isSponsored(link) {
  return link?.sponsored_position || link?.type === SPOC_TYPE;
}
class TopSiteLink extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      screenshotImage: null
    };
    this.onDragEvent = this.onDragEvent.bind(this);
    this.onKeyPress = this.onKeyPress.bind(this);
  }

  /*
   * Helper to determine whether the drop zone should allow a drop. We only allow
   * dropping top sites for now. We don't allow dropping on sponsored top sites
   * as their position is fixed.
   */
  _allowDrop(e) {
    return (this.dragged || !isSponsored(this.props.link)) && e.dataTransfer.types.includes("text/topsite-index");
  }
  onDragEvent(event) {
    switch (event.type) {
      case "click":
        // Stop any link clicks if we started any dragging
        if (this.dragged) {
          event.preventDefault();
        }
        break;
      case "dragstart":
        event.target.blur();
        if (isSponsored(this.props.link)) {
          event.preventDefault();
          break;
        }
        this.dragged = true;
        event.dataTransfer.effectAllowed = "move";
        event.dataTransfer.setData("text/topsite-index", this.props.index);
        this.props.onDragEvent(event, this.props.index, this.props.link, this.props.title);
        break;
      case "dragend":
        this.props.onDragEvent(event);
        break;
      case "dragenter":
      case "dragover":
      case "drop":
        if (this._allowDrop(event)) {
          event.preventDefault();
          this.props.onDragEvent(event, this.props.index);
        }
        break;
      case "mousedown":
        // Block the scroll wheel from appearing for middle clicks on search top sites
        if (event.button === 1 && this.props.link.searchTopSite) {
          event.preventDefault();
        }
        // Reset at the first mouse event of a potential drag
        this.dragged = false;
        break;
    }
  }

  /**
   * Helper to obtain the next state based on nextProps and prevState.
   *
   * NOTE: Rename this method to getDerivedStateFromProps when we update React
   *       to >= 16.3. We will need to update tests as well. We cannot rename this
   *       method to getDerivedStateFromProps now because there is a mismatch in
   *       the React version that we are using for both testing and production.
   *       (i.e. react-test-render => "16.3.2", react => "16.2.0").
   *
   * See https://github.com/airbnb/enzyme/blob/master/packages/enzyme-adapter-react-16/package.json#L43.
   */
  static getNextStateFromProps(nextProps, prevState) {
    const {
      screenshot
    } = nextProps.link;
    const imageInState = ScreenshotUtils.isRemoteImageLocal(prevState.screenshotImage, screenshot);
    if (imageInState) {
      return null;
    }

    // Since image was updated, attempt to revoke old image blob URL, if it exists.
    ScreenshotUtils.maybeRevokeBlobObjectURL(prevState.screenshotImage);
    return {
      screenshotImage: ScreenshotUtils.createLocalImageObject(screenshot)
    };
  }

  // NOTE: Remove this function when we update React to >= 16.3 since React will
  //       call getDerivedStateFromProps automatically. We will also need to
  //       rename getNextStateFromProps to getDerivedStateFromProps.
  componentWillMount() {
    const nextState = TopSiteLink.getNextStateFromProps(this.props, this.state);
    if (nextState) {
      this.setState(nextState);
    }
  }

  // NOTE: Remove this function when we update React to >= 16.3 since React will
  //       call getDerivedStateFromProps automatically. We will also need to
  //       rename getNextStateFromProps to getDerivedStateFromProps.
  componentWillReceiveProps(nextProps) {
    const nextState = TopSiteLink.getNextStateFromProps(nextProps, this.state);
    if (nextState) {
      this.setState(nextState);
    }
  }
  componentWillUnmount() {
    ScreenshotUtils.maybeRevokeBlobObjectURL(this.state.screenshotImage);
  }
  onKeyPress(event) {
    // If we have tabbed to a search shortcut top site, and we click 'enter',
    // we should execute the onClick function. This needs to be added because
    // search top sites are anchor tags without an href. See bug 1483135
    if (event.key === "Enter" && (this.props.link.searchTopSite || this.props.isAddButton)) {
      this.props.onClick(event);
    }
  }

  /*
   * Takes the url as a string, runs it through a simple (non-secure) hash turning it into a random number
   * Apply that random number to the color array. The same url will always generate the same color.
   */
  generateColor() {
    let {
      title,
      colors
    } = this.props;
    if (!colors) {
      return "";
    }
    let colorArray = colors.split(",");
    const hashStr = str => {
      let hash = 0;
      for (let i = 0; i < str.length; i++) {
        let charCode = str.charCodeAt(i);
        hash += charCode;
      }
      return hash;
    };
    let hash = hashStr(title);
    let index = hash % colorArray.length;
    return colorArray[index];
  }
  calculateStyle() {
    const {
      defaultStyle,
      link
    } = this.props;
    const {
      tippyTopIcon,
      faviconSize
    } = link;
    let imageClassName;
    let imageStyle;
    let showSmallFavicon = false;
    let smallFaviconStyle;
    let hasScreenshotImage = this.state.screenshotImage && this.state.screenshotImage.url;
    let selectedColor;
    if (defaultStyle) {
      // force no styles (letter fallback) even if the link has imagery
      selectedColor = this.generateColor();
    } else if (link.searchTopSite) {
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: `url(${tippyTopIcon})`
      };
      smallFaviconStyle = {
        backgroundImage: `url(${tippyTopIcon})`
      };
    } else if (link.customScreenshotURL) {
      // assume high quality custom screenshot and use rich icon styles and class names
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: hasScreenshotImage ? `url(${this.state.screenshotImage.url})` : ""
      };
    } else if (tippyTopIcon || link.type === SPOC_TYPE || faviconSize >= MIN_RICH_FAVICON_SIZE) {
      // styles and class names for top sites with rich icons
      imageClassName = "top-site-icon rich-icon";
      imageStyle = {
        backgroundColor: link.backgroundColor,
        backgroundImage: `url(${tippyTopIcon || link.favicon})`
      };
    } else if (faviconSize >= MIN_SMALL_FAVICON_SIZE) {
      showSmallFavicon = true;
      smallFaviconStyle = {
        backgroundImage: `url(${link.favicon})`
      };
    } else {
      selectedColor = this.generateColor();
      imageClassName = "";
    }
    return {
      showSmallFavicon,
      smallFaviconStyle,
      imageStyle,
      imageClassName,
      selectedColor
    };
  }
  render() {
    const {
      children,
      className,
      isDraggable,
      link,
      onClick,
      title,
      isAddButton,
      shortcutsRefresh
    } = this.props;
    const topSiteOuterClassName = `top-site-outer${className ? ` ${className}` : ""}${link.isDragged ? " dragged" : ""}${link.searchTopSite ? " search-shortcut" : ""}`;
    const [letterFallback] = title;
    const {
      showSmallFavicon,
      smallFaviconStyle,
      imageStyle,
      imageClassName,
      selectedColor
    } = this.calculateStyle();
    let addButtonl10n = {
      "data-l10n-id": "newtab-topsites-add-shortcut-label"
    };
    let draggableProps = {};
    if (isDraggable) {
      draggableProps = {
        onClick: this.onDragEvent,
        onDragEnd: this.onDragEvent,
        onDragStart: this.onDragEvent,
        onMouseDown: this.onDragEvent
      };
    }
    let impressionStats = null;
    if (link.type === SPOC_TYPE) {
      // Record impressions for Pocket tiles.
      impressionStats = /*#__PURE__*/external_React_default().createElement(ImpressionStats_ImpressionStats, {
        flightId: link.flightId,
        rows: [{
          id: link.id,
          pos: link.pos,
          shim: link.shim && link.shim.impression,
          advertiser: title.toLocaleLowerCase()
        }],
        dispatch: this.props.dispatch,
        source: TOP_SITES_SOURCE
      });
    } else if (isSponsored(link)) {
      // Record impressions for non-Pocket sponsored tiles.
      impressionStats = /*#__PURE__*/external_React_default().createElement(TopSiteImpressionWrapper, {
        actionType: actionTypes.TOP_SITES_SPONSORED_IMPRESSION_STATS,
        tile: {
          position: this.props.index,
          tile_id: link.sponsored_tile_id || -1,
          reporting_url: link.sponsored_impression_url,
          advertiser: title.toLocaleLowerCase(),
          source: NEWTAB_SOURCE
        }
        // For testing.
        ,
        IntersectionObserver: this.props.IntersectionObserver,
        document: this.props.document,
        dispatch: this.props.dispatch
      });
    } else {
      // Record impressions for organic tiles.
      impressionStats = /*#__PURE__*/external_React_default().createElement(TopSiteImpressionWrapper, {
        actionType: actionTypes.TOP_SITES_ORGANIC_IMPRESSION_STATS,
        tile: {
          position: this.props.index,
          source: NEWTAB_SOURCE
        }
        // For testing.
        ,
        IntersectionObserver: this.props.IntersectionObserver,
        document: this.props.document,
        dispatch: this.props.dispatch
      });
    }
    return /*#__PURE__*/external_React_default().createElement("li", TopSite_extends({
      className: topSiteOuterClassName,
      onDrop: this.onDragEvent,
      onDragOver: this.onDragEvent,
      onDragEnter: this.onDragEvent,
      onDragLeave: this.onDragEvent,
      ref: this.props.setRef
    }, draggableProps), /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-inner"
    }, /*#__PURE__*/external_React_default().createElement("a", {
      className: "top-site-button",
      href: link.searchTopSite ? undefined : link.url,
      tabIndex: this.props.tabIndex,
      onKeyPress: this.onKeyPress,
      onClick: onClick,
      draggable: true,
      "data-is-sponsored-link": !!link.sponsored_tile_id,
      title: title,
      onFocus: this.props.onFocus
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "tile",
      "aria-hidden": true
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: selectedColor ? "icon-wrapper letter-fallback" : "icon-wrapper",
      "data-fallback": letterFallback,
      style: selectedColor ? {
        backgroundColor: selectedColor
      } : {}
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: imageClassName,
      style: imageStyle
    }), showSmallFavicon && /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-icon default-icon",
      "data-fallback": smallFaviconStyle ? "" : letterFallback,
      style: smallFaviconStyle
    })), shortcutsRefresh && link.isPinned && /*#__PURE__*/external_React_default().createElement("div", {
      className: "icon icon-pin-small"
    }), !shortcutsRefresh && link.searchTopSite && /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-icon search-topsite"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: `title${link.isPinned ? " has-icon pinned" : ""}${link.type === SPOC_TYPE || link.show_sponsored_label ? " sponsored" : ""}`
    }, /*#__PURE__*/external_React_default().createElement("span", TopSite_extends({
      className: "title-label",
      dir: "auto"
    }, isAddButton && {
      ...addButtonl10n
    }), !shortcutsRefresh && link.isPinned && /*#__PURE__*/external_React_default().createElement("div", {
      className: "icon icon-pin-small"
    }), shortcutsRefresh && link.searchTopSite && /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-site-icon search-topsite"
    }), title || /*#__PURE__*/external_React_default().createElement("br", null)), /*#__PURE__*/external_React_default().createElement("span", {
      className: "sponsored-label",
      "data-l10n-id": "newtab-topsite-sponsored"
    }))), children, impressionStats));
  }
}
TopSiteLink.defaultProps = {
  title: "",
  link: {},
  isDraggable: true
};
class TopSite extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      showContextMenu: false
    };
    this.onLinkClick = this.onLinkClick.bind(this);
    this.onMenuUpdate = this.onMenuUpdate.bind(this);
  }

  /**
   * Report to telemetry additional information about the item.
   */
  _getTelemetryInfo() {
    const value = {
      icon_type: this.props.link.iconType
    };
    // Filter out "not_pinned" type for being the default
    if (this.props.link.isPinned) {
      value.card_type = "pinned";
    }
    if (this.props.link.searchTopSite) {
      // Set the card_type as "search" regardless of its pinning status
      value.card_type = "search";
      value.search_vendor = this.props.link.hostname;
    }
    if (isSponsored(this.props.link)) {
      value.card_type = "spoc";
    }
    return {
      value
    };
  }
  userEvent(event) {
    this.props.dispatch(actionCreators.UserEvent(Object.assign({
      event,
      source: TOP_SITES_SOURCE,
      action_position: this.props.index
    }, this._getTelemetryInfo())));
  }
  onLinkClick(event) {
    this.userEvent("CLICK");

    // Specially handle a top site link click for "typed" frecency bonus as
    // specified as a property on the link.
    event.preventDefault();
    const {
      altKey,
      button,
      ctrlKey,
      metaKey,
      shiftKey
    } = event;
    if (!this.props.link.searchTopSite) {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.OPEN_LINK,
        data: Object.assign(this.props.link, {
          event: {
            altKey,
            button,
            ctrlKey,
            metaKey,
            shiftKey
          }
        })
      }));
      if (this.props.link.type === SPOC_TYPE) {
        // Record a Pocket-specific click.
        this.props.dispatch(actionCreators.ImpressionStats({
          source: TOP_SITES_SOURCE,
          click: 0,
          tiles: [{
            id: this.props.link.id,
            pos: this.props.link.pos,
            shim: this.props.link.shim && this.props.link.shim.click
          }]
        }));

        // Record a click for a Pocket sponsored tile.
        // This first event is for the shim property
        // and is used by our ad service provider.
        this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
          event: "CLICK",
          source: TOP_SITES_SOURCE,
          action_position: this.props.link.pos,
          value: {
            card_type: "spoc",
            tile_id: this.props.link.id,
            shim: this.props.link.shim && this.props.link.shim.click
          }
        }));

        // A second event is recoded for internal usage.
        const title = this.props.link.label || this.props.link.hostname;
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.TOP_SITES_SPONSORED_IMPRESSION_STATS,
          data: {
            type: "click",
            position: this.props.link.pos,
            tile_id: this.props.link.id,
            advertiser: title.toLocaleLowerCase(),
            source: NEWTAB_SOURCE
          }
        }));
      } else if (isSponsored(this.props.link)) {
        // Record a click for a non-Pocket sponsored tile.
        const title = this.props.link.label || this.props.link.hostname;
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.TOP_SITES_SPONSORED_IMPRESSION_STATS,
          data: {
            type: "click",
            position: this.props.index,
            tile_id: this.props.link.sponsored_tile_id || -1,
            reporting_url: this.props.link.sponsored_click_url,
            advertiser: title.toLocaleLowerCase(),
            source: NEWTAB_SOURCE
          }
        }));
      } else {
        // Record a click for an organic tile.
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.TOP_SITES_ORGANIC_IMPRESSION_STATS,
          data: {
            type: "click",
            position: this.props.index,
            source: NEWTAB_SOURCE
          }
        }));
      }
      if (this.props.link.sendAttributionRequest) {
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.PARTNER_LINK_ATTRIBUTION,
          data: {
            targetURL: this.props.link.url,
            source: "newtab"
          }
        }));
      }
    } else {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.FILL_SEARCH_TERM,
        data: {
          label: this.props.link.label
        }
      }));
    }
  }
  onMenuUpdate(isOpen) {
    if (isOpen) {
      this.props.onActivate(this.props.index);
    } else {
      this.props.onActivate();
    }
  }
  render() {
    const {
      props
    } = this;
    const {
      link
    } = props;
    const isContextMenuOpen = props.activeIndex === props.index;
    const title = link.label || link.title || link.hostname;
    let menuOptions;
    if (link.sponsored_position) {
      menuOptions = TOP_SITES_SPONSORED_POSITION_CONTEXT_MENU_OPTIONS;
    } else if (link.searchTopSite) {
      menuOptions = TOP_SITES_SEARCH_SHORTCUTS_CONTEXT_MENU_OPTIONS;
    } else if (link.type === SPOC_TYPE) {
      menuOptions = TOP_SITES_SPOC_CONTEXT_MENU_OPTIONS;
    } else {
      menuOptions = TOP_SITES_CONTEXT_MENU_OPTIONS;
    }
    return /*#__PURE__*/external_React_default().createElement(TopSiteLink, TopSite_extends({}, props, {
      onClick: this.onLinkClick,
      onDragEvent: this.props.onDragEvent,
      className: `${props.className || ""}${isContextMenuOpen ? " active" : ""}`,
      title: title,
      setPref: this.props.setPref,
      tabIndex: this.props.tabIndex,
      onFocus: this.props.onFocus
    }), /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement(ContextMenuButton, {
      tooltip: "newtab-menu-content-tooltip",
      tooltipArgs: {
        title
      },
      onUpdate: this.onMenuUpdate,
      tabIndex: this.props.tabIndex,
      onFocus: this.props.onFocus
    }, /*#__PURE__*/external_React_default().createElement(LinkMenu, {
      dispatch: props.dispatch,
      index: props.index,
      onUpdate: this.onMenuUpdate,
      options: menuOptions,
      site: link,
      shouldSendImpressionStats: link.type === SPOC_TYPE,
      siteInfo: this._getTelemetryInfo(),
      source: TOP_SITES_SOURCE
    }))));
  }
}
TopSite.defaultProps = {
  link: {},
  onActivate() {}
};
class TopSitePlaceholder extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onEditButtonClick = this.onEditButtonClick.bind(this);
  }
  onEditButtonClick() {
    this.props.dispatch({
      type: actionTypes.TOP_SITES_EDIT,
      data: {
        index: this.props.index
      }
    });
  }
  render() {
    let addButtonProps = {};
    if (this.props.isAddButton) {
      addButtonProps = {
        title: "newtab-topsites-add-shortcut-label",
        onClick: this.onEditButtonClick
      };
    }
    return /*#__PURE__*/external_React_default().createElement(TopSiteLink, TopSite_extends({}, this.props, this.props.isAddButton ? {
      ...addButtonProps
    } : {}, {
      className: `placeholder ${this.props.className || ""} ${this.props.isAddButton ? "add-button" : ""}`,
      setPref: this.props.setPref,
      isDraggable: false,
      tabIndex: this.props.tabIndex
    }));
  }
}
class _TopSiteList extends (external_React_default()).PureComponent {
  static get DEFAULT_STATE() {
    return {
      activeIndex: null,
      draggedIndex: null,
      draggedSite: null,
      draggedTitle: null,
      topSitesPreview: null,
      focusedIndex: 0
    };
  }
  constructor(props) {
    super(props);
    this.state = _TopSiteList.DEFAULT_STATE;
    this.onDragEvent = this.onDragEvent.bind(this);
    this.onActivate = this.onActivate.bind(this);
    this.onWrapperFocus = this.onWrapperFocus.bind(this);
    this.onTopsiteFocus = this.onTopsiteFocus.bind(this);
    this.onWrapperBlur = this.onWrapperBlur.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
  }
  componentWillReceiveProps(nextProps) {
    if (this.state.draggedSite) {
      const prevTopSites = this.props.TopSites && this.props.TopSites.rows;
      const newTopSites = nextProps.TopSites && nextProps.TopSites.rows;
      if (prevTopSites && prevTopSites[this.state.draggedIndex] && prevTopSites[this.state.draggedIndex].url === this.state.draggedSite.url && (!newTopSites[this.state.draggedIndex] || newTopSites[this.state.draggedIndex].url !== this.state.draggedSite.url)) {
        // We got the new order from the redux store via props. We can clear state now.
        this.setState(_TopSiteList.DEFAULT_STATE);
      }
    }
  }
  userEvent(event, index) {
    this.props.dispatch(actionCreators.UserEvent({
      event,
      source: TOP_SITES_SOURCE,
      action_position: index
    }));
  }
  onDragEvent(event, index, link, title) {
    switch (event.type) {
      case "dragstart":
        this.dropped = false;
        this.setState({
          draggedIndex: index,
          draggedSite: link,
          draggedTitle: title,
          activeIndex: null
        });
        this.userEvent("DRAG", index);
        break;
      case "dragend":
        if (!this.dropped) {
          // If there was no drop event, reset the state to the default.
          this.setState(_TopSiteList.DEFAULT_STATE);
        }
        break;
      case "dragenter":
        if (index === this.state.draggedIndex) {
          this.setState({
            topSitesPreview: null
          });
        } else {
          this.setState({
            topSitesPreview: this._makeTopSitesPreview(index)
          });
        }
        break;
      case "drop":
        if (index !== this.state.draggedIndex) {
          this.dropped = true;
          this.props.dispatch(actionCreators.AlsoToMain({
            type: actionTypes.TOP_SITES_INSERT,
            data: {
              site: {
                url: this.state.draggedSite.url,
                label: this.state.draggedTitle,
                customScreenshotURL: this.state.draggedSite.customScreenshotURL,
                // Only if the search topsites experiment is enabled
                ...(this.state.draggedSite.searchTopSite && {
                  searchTopSite: true
                })
              },
              index,
              draggedFromIndex: this.state.draggedIndex
            }
          }));
          this.userEvent("DROP", index);
        }
        break;
    }
  }
  _getTopSites() {
    // Make a copy of the sites to truncate or extend to desired length
    let topSites = this.props.TopSites.rows.slice();
    topSites.length = this.props.TopSitesRows * TOP_SITES_MAX_SITES_PER_ROW;
    // if topSites do not fill an entire row add 'Add shortcut' button to array of topSites
    // (there should only be one of these)
    let firstPlaceholder = topSites.findIndex(Object.is.bind(null, undefined));
    // make sure placeholder exists and there already isnt a add button
    if (firstPlaceholder && !topSites.includes(site => site.isAddButton)) {
      topSites[firstPlaceholder] = {
        isAddButton: true
      };
    } else if (topSites.includes(site => site.isAddButton)) {
      topSites.push(topSites.splice(topSites.indexOf({
        isAddButton: true
      }), 1)[0]);
    }
    return topSites;
  }

  /**
   * Make a preview of the topsites that will be the result of dropping the currently
   * dragged site at the specified index.
   */
  _makeTopSitesPreview(index) {
    const topSites = this._getTopSites();
    topSites[this.state.draggedIndex] = null;
    const preview = topSites.map(site => site && (site.isPinned || isSponsored(site)) ? site : null);
    const unpinned = topSites.filter(site => site && !site.isPinned && !isSponsored(site));
    const siteToInsert = Object.assign({}, this.state.draggedSite, {
      isPinned: true,
      isDragged: true
    });
    if (!preview[index]) {
      preview[index] = siteToInsert;
    } else {
      // Find the hole to shift the pinned site(s) towards. We shift towards the
      // hole left by the site being dragged.
      let holeIndex = index;
      const indexStep = index > this.state.draggedIndex ? -1 : 1;
      while (preview[holeIndex]) {
        holeIndex += indexStep;
      }

      // Shift towards the hole.
      const shiftingStep = index > this.state.draggedIndex ? 1 : -1;
      while (index > this.state.draggedIndex ? holeIndex < index : holeIndex > index) {
        let nextIndex = holeIndex + shiftingStep;
        while (isSponsored(preview[nextIndex])) {
          nextIndex += shiftingStep;
        }
        preview[holeIndex] = preview[nextIndex];
        holeIndex = nextIndex;
      }
      preview[index] = siteToInsert;
    }

    // Fill in the remaining holes with unpinned sites.
    for (let i = 0; i < preview.length; i++) {
      if (!preview[i]) {
        preview[i] = unpinned.shift() || null;
      }
    }
    return preview;
  }
  onActivate(index) {
    this.setState({
      activeIndex: index
    });
  }
  onKeyDown(e) {
    if (this.state.activeIndex || this.state.activeIndex === 0) {
      return;
    }
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      // prevent the page from scrolling up/down while navigating.
      e.preventDefault();
    }
    if (this.focusedRef?.nextSibling?.querySelector("a") && e.key === "ArrowDown") {
      this.focusedRef.nextSibling.querySelector("a").tabIndex = 0;
      this.focusedRef.nextSibling.querySelector("a").focus();
    }
    if (this.focusedRef?.previousSibling?.querySelector("a") && e.key === "ArrowUp") {
      this.focusedRef.previousSibling.querySelector("a").tabIndex = 0;
      this.focusedRef.previousSibling.querySelector("a").focus();
    }
  }
  onWrapperFocus() {
    this.focusRef?.addEventListener("keydown", this.onKeyDown);
  }
  onWrapperBlur() {
    this.focusRef?.removeEventListener("keydown", this.onKeyDown);
  }
  onTopsiteFocus(focusIndex) {
    this.setState(() => ({
      focusedIndex: focusIndex
    }));
  }
  render() {
    const {
      props
    } = this;
    const prefs = props.Prefs.values;
    const shortcutsRefresh = prefs["newtabShortcuts.refresh"];
    const topSites = this.state.topSitesPreview || this._getTopSites();
    const topSitesUI = [];
    const commonProps = {
      onDragEvent: this.onDragEvent,
      dispatch: props.dispatch
    };
    // We assign a key to each placeholder slot. We need it to be independent
    // of the slot index (i below) so that the keys used stay the same during
    // drag and drop reordering and the underlying DOM nodes are reused.
    // This mostly (only?) affects linux so be sure to test on linux before changing.
    let holeIndex = 0;

    // On narrow viewports, we only show 6 sites per row. We'll mark the rest as
    // .hide-for-narrow to hide in CSS via @media query.
    const maxNarrowVisibleIndex = props.TopSitesRows * 6;
    for (let i = 0, l = topSites.length; i < l; i++) {
      const link = topSites[i] && Object.assign({}, topSites[i], {
        iconType: this.props.topSiteIconType(topSites[i])
      });
      const slotProps = {
        key: link ? link.url : holeIndex++,
        index: i
      };
      if (i >= maxNarrowVisibleIndex) {
        slotProps.className = "hide-for-narrow";
      }
      let topSiteLink;
      // Use a placeholder if the link is empty or it's rendering a sponsored
      // tile for the about:home startup cache.
      if (!link || props.App.isForStartupCache && isSponsored(link) || topSites[i]?.isAddButton) {
        if (link) {
          topSiteLink = /*#__PURE__*/external_React_default().createElement(TopSitePlaceholder, TopSite_extends({}, slotProps, commonProps, {
            isAddButton: topSites[i] && topSites[i].isAddButton,
            setRef: i === this.state.focusedIndex ? el => {
              this.focusedRef = el;
            } : () => {},
            tabIndex: i === this.state.focusedIndex ? 0 : -1,
            onFocus: () => {
              this.onTopsiteFocus(i);
            }
          }));
        }
      } else {
        topSiteLink = /*#__PURE__*/external_React_default().createElement(TopSite, TopSite_extends({
          link: link,
          activeIndex: this.state.activeIndex,
          onActivate: this.onActivate
        }, slotProps, commonProps, {
          colors: props.colors,
          shortcutsRefresh: shortcutsRefresh,
          setRef: i === this.state.focusedIndex ? el => {
            this.focusedRef = el;
          } : () => {},
          tabIndex: i === this.state.focusedIndex ? 0 : -1,
          onFocus: () => {
            this.onTopsiteFocus(i);
          }
        }));
      }
      topSitesUI.push(topSiteLink);
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-sites-list-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("ul", {
      role: "group",
      "aria-label": "Shortcuts",
      onFocus: this.onWrapperFocus,
      onBlur: this.onWrapperBlur,
      ref: el => {
        this.focusRef = el;
      },
      className: `top-sites-list${this.state.draggedSite ? " dnd-active" : ""}`
    }, topSitesUI));
  }
}
const TopSiteList = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  App: state.App,
  Prefs: state.Prefs
}))(_TopSiteList);
;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSiteForm.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */







class TopSiteForm extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    const {
      site
    } = props;
    this.state = {
      label: site ? site.label || site.hostname : "",
      url: site ? site.url : "",
      validationError: false,
      customScreenshotUrl: site ? site.customScreenshotURL : "",
      showCustomScreenshotForm: site ? site.customScreenshotURL : false
    };
    this.onClearScreenshotInput = this.onClearScreenshotInput.bind(this);
    this.onLabelChange = this.onLabelChange.bind(this);
    this.onUrlChange = this.onUrlChange.bind(this);
    this.onCancelButtonClick = this.onCancelButtonClick.bind(this);
    this.onClearUrlClick = this.onClearUrlClick.bind(this);
    this.onDoneButtonClick = this.onDoneButtonClick.bind(this);
    this.onCustomScreenshotUrlChange = this.onCustomScreenshotUrlChange.bind(this);
    this.onPreviewButtonClick = this.onPreviewButtonClick.bind(this);
    this.onEnableScreenshotUrlForm = this.onEnableScreenshotUrlForm.bind(this);
    this.validateUrl = this.validateUrl.bind(this);
  }
  onLabelChange(event) {
    this.setState({
      label: event.target.value
    });
  }
  onUrlChange(event) {
    this.setState({
      url: event.target.value,
      validationError: false
    });
  }
  onClearUrlClick() {
    this.setState({
      url: "",
      validationError: false
    });
  }
  onEnableScreenshotUrlForm() {
    this.setState({
      showCustomScreenshotForm: true
    });
  }
  _updateCustomScreenshotInput(customScreenshotUrl) {
    this.setState({
      customScreenshotUrl,
      validationError: false
    });
    this.props.dispatch({
      type: actionTypes.PREVIEW_REQUEST_CANCEL
    });
  }
  onCustomScreenshotUrlChange(event) {
    this._updateCustomScreenshotInput(event.target.value);
  }
  onClearScreenshotInput() {
    this._updateCustomScreenshotInput("");
  }
  onCancelButtonClick(ev) {
    ev.preventDefault();
    this.props.onClose();
  }
  onDoneButtonClick(ev) {
    ev.preventDefault();
    if (this.validateForm()) {
      const site = {
        url: this.cleanUrl(this.state.url)
      };
      const {
        index
      } = this.props;
      if (this.state.label !== "") {
        site.label = this.state.label;
      }
      if (this.state.customScreenshotUrl) {
        site.customScreenshotURL = this.cleanUrl(this.state.customScreenshotUrl);
      } else if (this.props.site && this.props.site.customScreenshotURL) {
        // Used to flag that previously cached screenshot should be removed
        site.customScreenshotURL = null;
      }
      this.props.dispatch(actionCreators.AlsoToMain({
        type: actionTypes.TOP_SITES_PIN,
        data: {
          site,
          index
        }
      }));
      this.props.dispatch(actionCreators.UserEvent({
        source: TOP_SITES_SOURCE,
        event: "TOP_SITES_EDIT",
        action_position: index
      }));
      this.props.onClose();
    }
  }
  onPreviewButtonClick(event) {
    event.preventDefault();
    if (this.validateForm()) {
      this.props.dispatch(actionCreators.AlsoToMain({
        type: actionTypes.PREVIEW_REQUEST,
        data: {
          url: this.cleanUrl(this.state.customScreenshotUrl)
        }
      }));
      this.props.dispatch(actionCreators.UserEvent({
        source: TOP_SITES_SOURCE,
        event: "PREVIEW_REQUEST"
      }));
    }
  }
  cleanUrl(url) {
    // If we are missing a protocol, prepend http://
    if (!url.startsWith("http:") && !url.startsWith("https:")) {
      return `http://${url}`;
    }
    return url;
  }
  _tryParseUrl(url) {
    try {
      return new URL(url);
    } catch (e) {
      return null;
    }
  }
  validateUrl(url) {
    const validProtocols = ["http:", "https:"];
    const urlObj = this._tryParseUrl(url) || this._tryParseUrl(this.cleanUrl(url));
    return urlObj && validProtocols.includes(urlObj.protocol);
  }
  validateCustomScreenshotUrl() {
    const {
      customScreenshotUrl
    } = this.state;
    return !customScreenshotUrl || this.validateUrl(customScreenshotUrl);
  }
  validateForm() {
    const validate = this.validateUrl(this.state.url) && this.validateCustomScreenshotUrl();
    if (!validate) {
      this.setState({
        validationError: true
      });
    }
    return validate;
  }
  _renderCustomScreenshotInput() {
    const {
      customScreenshotUrl
    } = this.state;
    const requestFailed = this.props.previewResponse === "";
    const validationError = this.state.validationError && !this.validateCustomScreenshotUrl() || requestFailed;
    // Set focus on error if the url field is valid or when the input is first rendered and is empty
    const shouldFocus = validationError && this.validateUrl(this.state.url) || !customScreenshotUrl;
    const isLoading = this.props.previewResponse === null && customScreenshotUrl && this.props.previewUrl === this.cleanUrl(customScreenshotUrl);
    if (!this.state.showCustomScreenshotForm) {
      return /*#__PURE__*/external_React_default().createElement(A11yLinkButton, {
        onClick: this.onEnableScreenshotUrlForm,
        className: "enable-custom-image-input",
        "data-l10n-id": "newtab-topsites-use-image-link"
      });
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "custom-image-input-container"
    }, /*#__PURE__*/external_React_default().createElement(TopSiteFormInput, {
      errorMessageId: requestFailed ? "newtab-topsites-image-validation" : "newtab-topsites-url-validation",
      loading: isLoading,
      onChange: this.onCustomScreenshotUrlChange,
      onClear: this.onClearScreenshotInput,
      shouldFocus: shouldFocus,
      typeUrl: true,
      value: customScreenshotUrl,
      validationError: validationError,
      titleId: "newtab-topsites-image-url-label",
      placeholderId: "newtab-topsites-url-input"
    }));
  }
  render() {
    const {
      customScreenshotUrl
    } = this.state;
    const requestFailed = this.props.previewResponse === "";
    // For UI purposes, editing without an existing link is "add"
    const showAsAdd = !this.props.site;
    const previous = this.props.site && this.props.site.customScreenshotURL || "";
    const changed = customScreenshotUrl && this.cleanUrl(customScreenshotUrl) !== previous;
    // Preview mode if changes were made to the custom screenshot URL and no preview was received yet
    // or the request failed
    const previewMode = changed && !this.props.previewResponse;
    const previewLink = Object.assign({}, this.props.site);
    if (this.props.previewResponse) {
      previewLink.screenshot = this.props.previewResponse;
      previewLink.customScreenshotURL = this.props.previewUrl;
    }
    // Handles the form submit so an enter press performs the correct action
    const onSubmit = previewMode ? this.onPreviewButtonClick : this.onDoneButtonClick;
    const addTopsitesHeaderL10nId = "newtab-topsites-add-shortcut-header";
    const editTopsitesHeaderL10nId = "newtab-topsites-edit-shortcut-header";
    return /*#__PURE__*/external_React_default().createElement("form", {
      className: "topsite-form",
      onSubmit: onSubmit
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "form-input-container"
    }, /*#__PURE__*/external_React_default().createElement("h3", {
      className: "section-title grey-title",
      "data-l10n-id": showAsAdd ? addTopsitesHeaderL10nId : editTopsitesHeaderL10nId
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "fields-and-preview"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "form-wrapper"
    }, /*#__PURE__*/external_React_default().createElement(TopSiteFormInput, {
      onChange: this.onLabelChange,
      value: this.state.label,
      titleId: "newtab-topsites-title-label",
      placeholderId: "newtab-topsites-title-input",
      autoFocusOnOpen: true
    }), /*#__PURE__*/external_React_default().createElement(TopSiteFormInput, {
      onChange: this.onUrlChange,
      shouldFocus: this.state.validationError && !this.validateUrl(this.state.url),
      value: this.state.url,
      onClear: this.onClearUrlClick,
      validationError: this.state.validationError && !this.validateUrl(this.state.url),
      titleId: "newtab-topsites-url-label",
      typeUrl: true,
      placeholderId: "newtab-topsites-url-input",
      errorMessageId: "newtab-topsites-url-validation"
    }), this._renderCustomScreenshotInput()), /*#__PURE__*/external_React_default().createElement(TopSiteLink, {
      link: previewLink,
      defaultStyle: requestFailed,
      title: this.state.label
    }))), /*#__PURE__*/external_React_default().createElement("section", {
      className: "actions"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "cancel",
      type: "button",
      onClick: this.onCancelButtonClick,
      "data-l10n-id": "newtab-topsites-cancel-button"
    }), previewMode ? /*#__PURE__*/external_React_default().createElement("button", {
      className: "done preview",
      type: "submit",
      "data-l10n-id": "newtab-topsites-preview-button"
    }) : /*#__PURE__*/external_React_default().createElement("button", {
      className: "done",
      type: "submit",
      "data-l10n-id": showAsAdd ? "newtab-topsites-add-button" : "newtab-topsites-save-button"
    })));
  }
}
TopSiteForm.defaultProps = {
  site: null,
  index: -1
};
;// CONCATENATED MODULE: ./content-src/components/TopSites/TopSites.jsx
function TopSites_extends() { TopSites_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return TopSites_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */












function topSiteIconType(link) {
  if (link.customScreenshotURL) {
    return "custom_screenshot";
  }
  if (link.tippyTopIcon || link.faviconRef === "tippytop") {
    return "tippytop";
  }
  if (link.faviconSize >= MIN_RICH_FAVICON_SIZE) {
    return "rich_icon";
  }
  if (link.screenshot) {
    return "screenshot";
  }
  return "no_image";
}

/**
 * Iterates through TopSites and counts types of images.
 * @param acc Accumulator for reducer.
 * @param topsite Entry in TopSites.
 */
function countTopSitesIconsTypes(topSites) {
  const countTopSitesTypes = (acc, link) => {
    acc[topSiteIconType(link)]++;
    return acc;
  };
  return topSites.reduce(countTopSitesTypes, {
    custom_screenshot: 0,
    screenshot: 0,
    tippytop: 0,
    rich_icon: 0,
    no_image: 0
  });
}
class _TopSites extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onEditFormClose = this.onEditFormClose.bind(this);
    this.onSearchShortcutsFormClose = this.onSearchShortcutsFormClose.bind(this);
  }

  /**
   * Dispatch session statistics about the quality of TopSites icons and pinned count.
   */
  _dispatchTopSitesStats() {
    const topSites = this._getVisibleTopSites().filter(topSite => topSite !== null && topSite !== undefined);
    const topSitesIconsStats = countTopSitesIconsTypes(topSites);
    const topSitesPinned = topSites.filter(site => !!site.isPinned).length;
    const searchShortcuts = topSites.filter(site => !!site.searchTopSite).length;
    // Dispatch telemetry event with the count of TopSites images types.
    this.props.dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SAVE_SESSION_PERF_DATA,
      data: {
        topsites_icon_stats: topSitesIconsStats,
        topsites_pinned: topSitesPinned,
        topsites_search_shortcuts: searchShortcuts
      }
    }));
  }

  /**
   * Return the TopSites that are visible based on prefs and window width.
   */
  _getVisibleTopSites() {
    // We hide 2 sites per row when not in the wide layout.
    let sitesPerRow = TOP_SITES_MAX_SITES_PER_ROW;
    // $break-point-widest = 1072px (from _variables.scss)
    if (!globalThis.matchMedia(`(min-width: 1072px)`).matches) {
      sitesPerRow -= 2;
    }
    return this.props.TopSites.rows.slice(0, this.props.TopSitesRows * sitesPerRow);
  }
  componentDidUpdate() {
    this._dispatchTopSitesStats();
  }
  componentDidMount() {
    this._dispatchTopSitesStats();
  }
  onEditFormClose() {
    this.props.dispatch(actionCreators.UserEvent({
      source: TOP_SITES_SOURCE,
      event: "TOP_SITES_EDIT_CLOSE"
    }));
    this.props.dispatch({
      type: actionTypes.TOP_SITES_CANCEL_EDIT
    });
  }
  onSearchShortcutsFormClose() {
    this.props.dispatch(actionCreators.UserEvent({
      source: TOP_SITES_SOURCE,
      event: "SEARCH_EDIT_CLOSE"
    }));
    this.props.dispatch({
      type: actionTypes.TOP_SITES_CLOSE_SEARCH_SHORTCUTS_MODAL
    });
  }
  render() {
    const {
      props
    } = this;
    const {
      editForm,
      showSearchShortcutsForm
    } = props.TopSites;
    const extraMenuOptions = ["AddTopSite"];
    const colors = props.Prefs.values["newNewtabExperience.colors"];
    if (props.Prefs.values["improvesearch.topSiteSearchShortcuts"]) {
      extraMenuOptions.push("AddSearchShortcut");
    }
    return /*#__PURE__*/external_React_default().createElement(ComponentPerfTimer, {
      id: "topsites",
      initialized: props.TopSites.initialized,
      dispatch: props.dispatch
    }, /*#__PURE__*/external_React_default().createElement(CollapsibleSection, {
      className: "top-sites",
      id: "topsites",
      title: props.title || {
        id: "newtab-section-header-topsites"
      },
      hideTitle: true,
      extraMenuOptions: extraMenuOptions,
      showPrefName: "feeds.topsites",
      eventSource: TOP_SITES_SOURCE,
      collapsed: false,
      isFixed: props.isFixed,
      isFirst: props.isFirst,
      isLast: props.isLast,
      dispatch: props.dispatch
    }, /*#__PURE__*/external_React_default().createElement(TopSiteList, {
      TopSites: props.TopSites,
      TopSitesRows: props.TopSitesRows,
      dispatch: props.dispatch,
      topSiteIconType: topSiteIconType,
      colors: colors
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "edit-topsites-wrapper"
    }, editForm && /*#__PURE__*/external_React_default().createElement("div", {
      className: "edit-topsites"
    }, /*#__PURE__*/external_React_default().createElement(ModalOverlayWrapper, {
      unstyled: true,
      onClose: this.onEditFormClose,
      innerClassName: "modal"
    }, /*#__PURE__*/external_React_default().createElement(TopSiteForm, TopSites_extends({
      site: props.TopSites.rows[editForm.index],
      onClose: this.onEditFormClose,
      dispatch: this.props.dispatch
    }, editForm)))), showSearchShortcutsForm && /*#__PURE__*/external_React_default().createElement("div", {
      className: "edit-search-shortcuts"
    }, /*#__PURE__*/external_React_default().createElement(ModalOverlayWrapper, {
      unstyled: true,
      onClose: this.onSearchShortcutsFormClose,
      innerClassName: "modal"
    }, /*#__PURE__*/external_React_default().createElement(SearchShortcutsForm, {
      TopSites: props.TopSites,
      onClose: this.onSearchShortcutsFormClose,
      dispatch: this.props.dispatch
    }))))));
  }
}
const TopSites_TopSites = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  TopSites: state.TopSites,
  Prefs: state.Prefs,
  TopSitesRows: state.Prefs.values.topSitesRows
}))(_TopSites);
;// CONCATENATED MODULE: ./content-src/components/Sections/Sections.jsx
function Sections_extends() { Sections_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return Sections_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */











const Sections_VISIBLE = "visible";
const Sections_VISIBILITY_CHANGE_EVENT = "visibilitychange";
const CARDS_PER_ROW_DEFAULT = 3;
const CARDS_PER_ROW_COMPACT_WIDE = 4;
class Section extends (external_React_default()).PureComponent {
  get numRows() {
    const {
      rowsPref,
      maxRows,
      Prefs
    } = this.props;
    return rowsPref ? Prefs.values[rowsPref] : maxRows;
  }
  _dispatchImpressionStats() {
    const {
      props
    } = this;
    let cardsPerRow = CARDS_PER_ROW_DEFAULT;
    if (props.compactCards && globalThis.matchMedia(`(min-width: 1072px)`).matches) {
      // If the section has compact cards and the viewport is wide enough, we show
      // 4 columns instead of 3.
      // $break-point-widest = 1072px (from _variables.scss)
      cardsPerRow = CARDS_PER_ROW_COMPACT_WIDE;
    }
    const maxCards = cardsPerRow * this.numRows;
    const cards = props.rows.slice(0, maxCards);
    if (this.needsImpressionStats(cards)) {
      props.dispatch(actionCreators.ImpressionStats({
        source: props.eventSource,
        tiles: cards.map(link => ({
          id: link.guid
        }))
      }));
      this.impressionCardGuids = cards.map(link => link.guid);
    }
  }

  // This sends an event when a user sees a set of new content. If content
  // changes while the page is hidden (i.e. preloaded or on a hidden tab),
  // only send the event if the page becomes visible again.
  sendImpressionStatsOrAddListener() {
    const {
      props
    } = this;
    if (!props.shouldSendImpressionStats || !props.dispatch) {
      return;
    }
    if (props.document.visibilityState === Sections_VISIBLE) {
      this._dispatchImpressionStats();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(Sections_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
      }

      // When the page becomes visible, send the impression stats ping if the section isn't collapsed.
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === Sections_VISIBLE) {
          if (!this.props.pref.collapsed) {
            this._dispatchImpressionStats();
          }
          props.document.removeEventListener(Sections_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
        }
      };
      props.document.addEventListener(Sections_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  componentWillMount() {
    this.sendNewTabRehydrated(this.props.initialized);
  }
  componentDidMount() {
    if (this.props.rows.length && !this.props.pref.collapsed) {
      this.sendImpressionStatsOrAddListener();
    }
  }
  componentDidUpdate(prevProps) {
    const {
      props
    } = this;
    const isCollapsed = props.pref.collapsed;
    const wasCollapsed = prevProps.pref.collapsed;
    if (
    // Don't send impression stats for the empty state
    props.rows.length && (
    // We only want to send impression stats if the content of the cards has changed
    // and the section is not collapsed...
    props.rows !== prevProps.rows && !isCollapsed ||
    // or if we are expanding a section that was collapsed.
    wasCollapsed && !isCollapsed)) {
      this.sendImpressionStatsOrAddListener();
    }
  }
  componentWillUpdate(nextProps) {
    this.sendNewTabRehydrated(nextProps.initialized);
  }
  componentWillUnmount() {
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(Sections_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  needsImpressionStats(cards) {
    if (!this.impressionCardGuids || this.impressionCardGuids.length !== cards.length) {
      return true;
    }
    for (let i = 0; i < cards.length; i++) {
      if (cards[i].guid !== this.impressionCardGuids[i]) {
        return true;
      }
    }
    return false;
  }

  // The NEW_TAB_REHYDRATED event is used to inform feeds that their
  // data has been consumed e.g. for counting the number of tabs that
  // have rendered that data.
  sendNewTabRehydrated(initialized) {
    if (initialized && !this.renderNotified) {
      this.props.dispatch(actionCreators.AlsoToMain({
        type: actionTypes.NEW_TAB_REHYDRATED,
        data: {}
      }));
      this.renderNotified = true;
    }
  }
  render() {
    const {
      id,
      eventSource,
      title,
      rows,
      Pocket,
      emptyState,
      dispatch,
      compactCards,
      read_more_endpoint,
      contextMenuOptions,
      initialized,
      learnMore,
      pref,
      privacyNoticeURL,
      isFirst,
      isLast
    } = this.props;
    const waitingForSpoc = id === "topstories" && this.props.Pocket.waitingForSpoc;
    const maxCardsPerRow = compactCards ? CARDS_PER_ROW_COMPACT_WIDE : CARDS_PER_ROW_DEFAULT;
    const {
      numRows
    } = this;
    const maxCards = maxCardsPerRow * numRows;
    const maxCardsOnNarrow = CARDS_PER_ROW_DEFAULT * numRows;
    const {
      pocketCta,
      isUserLoggedIn
    } = Pocket || {};
    const {
      useCta
    } = pocketCta || {};
    const shouldShowPocketCta = id === "topstories" && useCta && isUserLoggedIn === false;
    const shouldShowReadMore = read_more_endpoint;
    const realRows = rows.slice(0, maxCards);

    // The empty state should only be shown after we have initialized and there is no content.
    // Otherwise, we should show placeholders.
    const shouldShowEmptyState = initialized && !rows.length;
    const cards = [];
    if (!shouldShowEmptyState) {
      for (let i = 0; i < maxCards; i++) {
        const link = realRows[i];
        // On narrow viewports, we only show 3 cards per row. We'll mark the rest as
        // .hide-for-narrow to hide in CSS via @media query.
        const className = i >= maxCardsOnNarrow ? "hide-for-narrow" : "";
        let usePlaceholder = !link;
        // If we are in the third card and waiting for spoc,
        // use the placeholder.
        if (!usePlaceholder && i === 2 && waitingForSpoc) {
          usePlaceholder = true;
        }
        cards.push(!usePlaceholder ? /*#__PURE__*/external_React_default().createElement(Card, {
          key: i,
          index: i,
          className: className,
          dispatch: dispatch,
          link: link,
          contextMenuOptions: contextMenuOptions,
          eventSource: eventSource,
          shouldSendImpressionStats: this.props.shouldSendImpressionStats,
          isWebExtension: this.props.isWebExtension
        }) : /*#__PURE__*/external_React_default().createElement(PlaceholderCard, {
          key: i,
          className: className
        }));
      }
    }
    const sectionClassName = ["section", compactCards ? "compact-cards" : "normal-cards"].join(" ");

    // <Section> <-- React component
    // <section> <-- HTML5 element
    return /*#__PURE__*/external_React_default().createElement(ComponentPerfTimer, this.props, /*#__PURE__*/external_React_default().createElement(CollapsibleSection, {
      className: sectionClassName,
      title: title,
      id: id,
      eventSource: eventSource,
      collapsed: this.props.pref.collapsed,
      showPrefName: pref && pref.feed || id,
      privacyNoticeURL: privacyNoticeURL,
      Prefs: this.props.Prefs,
      isFixed: this.props.isFixed,
      isFirst: isFirst,
      isLast: isLast,
      learnMore: learnMore,
      dispatch: this.props.dispatch,
      isWebExtension: this.props.isWebExtension
    }, !shouldShowEmptyState && /*#__PURE__*/external_React_default().createElement("ul", {
      className: "section-list",
      style: {
        padding: 0
      }
    }, cards), shouldShowEmptyState && /*#__PURE__*/external_React_default().createElement("div", {
      className: "section-empty-state"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "empty-state"
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: emptyState.message
    }, /*#__PURE__*/external_React_default().createElement("p", {
      className: "empty-state-message"
    })))), id === "topstories" && /*#__PURE__*/external_React_default().createElement("div", {
      className: "top-stories-bottom-container"
    }, shouldShowPocketCta && /*#__PURE__*/external_React_default().createElement("div", {
      className: "wrapper-cta"
    }, /*#__PURE__*/external_React_default().createElement(PocketLoggedInCta, null)), /*#__PURE__*/external_React_default().createElement("div", {
      className: "wrapper-more-recommendations"
    }, shouldShowReadMore && /*#__PURE__*/external_React_default().createElement(MoreRecommendations, {
      read_more_endpoint: read_more_endpoint
    })))));
  }
}
Section.defaultProps = {
  document: globalThis.document,
  rows: [],
  emptyState: {},
  pref: {},
  title: ""
};
const SectionIntl = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs,
  Pocket: state.Pocket
}))(Section);
class _Sections extends (external_React_default()).PureComponent {
  renderSections() {
    const sections = [];
    const enabledSections = this.props.Sections.filter(section => section.enabled);
    const {
      sectionOrder,
      "feeds.topsites": showTopSites
    } = this.props.Prefs.values;
    // Enabled sections doesn't include Top Sites, so we add it if enabled.
    const expectedCount = enabledSections.length + ~~showTopSites;
    for (const sectionId of sectionOrder.split(",")) {
      const commonProps = {
        key: sectionId,
        isFirst: sections.length === 0,
        isLast: sections.length === expectedCount - 1
      };
      if (sectionId === "topsites" && showTopSites) {
        sections.push( /*#__PURE__*/external_React_default().createElement(TopSites_TopSites, commonProps));
      } else {
        const section = enabledSections.find(s => s.id === sectionId);
        if (section) {
          sections.push( /*#__PURE__*/external_React_default().createElement(SectionIntl, Sections_extends({}, section, commonProps)));
        }
      }
    }
    return sections;
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "sections-list"
    }, this.renderSections());
  }
}
const Sections_Sections = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Sections: state.Sections,
  Prefs: state.Prefs
}))(_Sections);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/Highlights/Highlights.jsx
function Highlights_extends() { Highlights_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return Highlights_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class _Highlights extends (external_React_default()).PureComponent {
  render() {
    const section = this.props.Sections.find(s => s.id === "highlights");
    if (!section || !section.enabled) {
      return null;
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-highlights sections-list"
    }, /*#__PURE__*/external_React_default().createElement(SectionIntl, Highlights_extends({}, section, {
      isFixed: true
    })));
  }
}
const Highlights = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Sections: state.Sections
}))(_Highlights);
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/HorizontalRule/HorizontalRule.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class HorizontalRule extends (external_React_default()).PureComponent {
  render() {
    return /*#__PURE__*/external_React_default().createElement("hr", {
      className: "ds-hr"
    });
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/Navigation/Navigation.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





class Topic extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onLinkClick = this.onLinkClick.bind(this);
  }
  onLinkClick(event) {
    if (this.props.dispatch) {
      this.props.dispatch(actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        source: "POPULAR_TOPICS",
        action_position: 0,
        value: {
          topic: event.target.text.toLowerCase().replace(` `, `-`)
        }
      }));
    }
  }
  render() {
    const {
      url,
      name: topicName
    } = this.props;
    return /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      onLinkClick: this.onLinkClick,
      className: this.props.className,
      url: url
    }, topicName);
  }
}
class Navigation extends (external_React_default()).PureComponent {
  render() {
    let links = this.props.links || [];
    const alignment = this.props.alignment || "centered";
    const header = this.props.header || {};
    const english = this.props.locale.startsWith("en-");
    const privacyNotice = this.props.privacyNoticeURL || {};
    const {
      newFooterSection
    } = this.props;
    const className = `ds-navigation ds-navigation-${alignment} ${newFooterSection ? `ds-navigation-new-topics` : ``}`;
    let {
      title
    } = header;
    if (newFooterSection) {
      title = {
        id: "newtab-pocket-new-topics-title"
      };
      if (this.props.extraLinks) {
        links = [...links.slice(0, links.length - 1), ...this.props.extraLinks, links[links.length - 1]];
      }
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: className
    }, title && english ? /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: title
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "ds-navigation-header"
    })) : null, english ? /*#__PURE__*/external_React_default().createElement("ul", null, links && links.map(t => /*#__PURE__*/external_React_default().createElement("li", {
      key: t.name
    }, /*#__PURE__*/external_React_default().createElement(Topic, {
      url: t.url,
      name: t.name,
      dispatch: this.props.dispatch
    })))) : null, !newFooterSection ? /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      className: "ds-navigation-privacy",
      url: privacyNotice.url
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: privacyNotice.title
    })) : null, newFooterSection ? /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-navigation-family"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon firefox-logo"
    }), /*#__PURE__*/external_React_default().createElement("span", null, "|"), /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon pocket-logo"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "ds-navigation-family-message",
      "data-l10n-id": "newtab-pocket-pocket-firefox-family"
    })) : null);
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/PrivacyLink/PrivacyLink.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class PrivacyLink extends (external_React_default()).PureComponent {
  render() {
    const {
      properties
    } = this.props;
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-privacy-link"
    }, /*#__PURE__*/external_React_default().createElement(SafeAnchor, {
      url: properties.url
    }, /*#__PURE__*/external_React_default().createElement(FluentOrText, {
      message: properties.title
    })));
  }
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/SectionTitle/SectionTitle.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


class SectionTitle extends (external_React_default()).PureComponent {
  render() {
    const {
      header: {
        title,
        subtitle
      }
    } = this.props;
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-section-title"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "title"
    }, title), subtitle ? /*#__PURE__*/external_React_default().createElement("div", {
      className: "subtitle"
    }, subtitle) : null);
  }
}
;// CONCATENATED MODULE: ./content-src/lib/selectLayoutRender.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const selectLayoutRender = ({ state = {}, prefs = {} }) => {
  const { layout, feeds, spocs } = state;
  let spocIndexPlacementMap = {};

  /* This function fills spoc positions on a per placement basis with available spocs.
   * It does this by looping through each position for a placement and replacing a rec with a spoc.
   * If it runs out of spocs or positions, it stops.
   * If it sees the same placement again, it remembers the previous spoc index, and continues.
   * If it sees a blocked spoc, it skips that position leaving in a regular story.
   */
  function fillSpocPositionsForPlacement(
    data,
    spocsPositions,
    spocsData,
    placementName
  ) {
    if (
      !spocIndexPlacementMap[placementName] &&
      spocIndexPlacementMap[placementName] !== 0
    ) {
      spocIndexPlacementMap[placementName] = 0;
    }
    const results = [...data];
    for (let position of spocsPositions) {
      const spoc = spocsData[spocIndexPlacementMap[placementName]];
      // If there are no spocs left, we can stop filling positions.
      if (!spoc) {
        break;
      }

      // A placement could be used in two sections.
      // In these cases, we want to maintain the index of the previous section.
      // If we didn't do this, it might duplicate spocs.
      spocIndexPlacementMap[placementName]++;

      // A spoc that's blocked is removed from the source for subsequent newtab loads.
      // If we have a spoc in the source that's blocked, it means it was *just* blocked,
      // and in this case, we skip this position, and show a regular spoc instead.
      if (!spocs.blocked.includes(spoc.url)) {
        results.splice(position.index, 0, spoc);
      }
    }

    return results;
  }

  const positions = {};
  const DS_COMPONENTS = [
    "Message",
    "TextPromo",
    "SectionTitle",
    "Signup",
    "Navigation",
    "CardGrid",
    "CollectionCardGrid",
    "HorizontalRule",
    "PrivacyLink",
  ];

  const filterArray = [];

  if (!prefs["feeds.topsites"]) {
    filterArray.push("TopSites");
  }

  const pocketEnabled =
    prefs["feeds.section.topstories"] && prefs["feeds.system.topstories"];
  if (!pocketEnabled) {
    filterArray.push(...DS_COMPONENTS);
  }

  // function to determine amount of tiles shown per section per viewport
  function getMaxTiles(responsiveLayouts) {
    return responsiveLayouts
      .flatMap(responsiveLayout => responsiveLayout)
      .reduce((acc, t) => {
        acc[t.columnCount] = t.tiles.length;

        // Update maxTile if current tile count is greater
        if (!acc.maxTile || t.tiles.length > acc.maxTile) {
          acc.maxTile = t.tiles.length;
        }
        return acc;
      }, {});
  }

  const placeholderComponent = component => {
    if (!component.feed) {
      // TODO we now need a placeholder for topsites and textPromo.
      return {
        ...component,
        data: {
          spocs: [],
        },
      };
    }
    const data = {
      recommendations: [],
      sections: [
        {
          layout: {
            responsiveLayouts: [],
          },
          data: [],
        },
      ],
    };

    let items = 0;
    if (component.properties && component.properties.items) {
      items = component.properties.items;
    }
    for (let i = 0; i < items; i++) {
      data.recommendations.push({ placeholder: true });
    }

    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    if (sectionsEnabled) {
      for (let i = 0; i < items; i++) {
        data.sections[0].data.push({ placeholder: true });
      }
    }

    return { ...component, data };
  };

  // TODO update devtools to show placements
  const handleSpocs = (data = [], spocsPositions, spocsPlacement) => {
    let result = [...data];
    // Do we ever expect to possibly have a spoc.
    if (spocsPositions?.length) {
      const placement = spocsPlacement || {};
      const placementName = placement.name || "newtab_spocs";
      const spocsData = spocs.data[placementName];

      // We expect a spoc, spocs are loaded, and the server returned spocs.
      if (spocs.loaded && spocsData?.items?.length) {
        // Since banner-type ads are placed by row and don't use the normal spoc position,
        // dont combine with content
        const excludedSpocs = ["billboard", "leaderboard"];
        const filteredSpocs = spocsData?.items?.filter(
          item => !excludedSpocs.includes(item.format)
        );
        result = fillSpocPositionsForPlacement(
          result,
          spocsPositions,
          filteredSpocs,
          placementName
        );
      }
    }
    return result;
  };

  const handleSections = (sections = [], recommendations = []) => {
    let result = sections.sort((a, b) => a.receivedRank - b.receivedRank);

    const sectionsMap = recommendations.reduce((acc, recommendation) => {
      const { section } = recommendation;
      acc[section] = acc[section] || [];
      acc[section].push(recommendation);
      return acc;
    }, {});

    result.forEach(section => {
      const { sectionKey } = section;
      section.data = sectionsMap[sectionKey];
    });

    return result;
  };

  const handleComponent = component => {
    if (component?.spocs?.positions?.length) {
      const placement = component.placement || {};
      const placementName = placement.name || "newtab_spocs";
      const spocsData = spocs.data[placementName];
      if (spocs.loaded && spocsData?.items?.length) {
        return {
          ...component,
          data: {
            spocs: spocsData.items
              .filter(spoc => spoc && !spocs.blocked.includes(spoc.url))
              .map((spoc, index) => ({
                ...spoc,
                pos: index,
              })),
          },
        };
      }
    }
    return {
      ...component,
      data: {
        spocs: [],
      },
    };
  };

  const handleComponentWithFeed = component => {
    positions[component.type] = positions[component.type] || 0;
    let data = {
      recommendations: [],
      sections: [],
    };

    const feed = feeds.data[component.feed.url];
    if (feed?.data) {
      data = {
        ...feed.data,
        recommendations: [...(feed.data.recommendations || [])],
        sections: [...(feed.data.sections || [])],
      };
    }

    if (component && component.properties && component.properties.offset) {
      data = {
        ...data,
        recommendations: data.recommendations.slice(
          component.properties.offset
        ),
      };
    }
    const spocsPositions = component?.spocs?.positions;
    const spocsPlacement = component?.placement;

    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    data = {
      ...data,
      ...(sectionsEnabled
        ? {
            sections: handleSections(data.sections, data.recommendations).map(
              section => {
                const sectionsSpocsPositions = [];
                section.layout.responsiveLayouts
                  // Initial position for spocs is going to be for the smallest breakpoint.
                  // We can then move it from there via breakpoints.
                  .find(item => item.columnCount === 1)
                  .tiles.forEach(tile => {
                    if (tile.hasAd) {
                      sectionsSpocsPositions.push({ index: tile.position });
                    }
                  });
                return {
                  ...section,
                  data: handleSpocs(
                    section.data,
                    sectionsSpocsPositions,
                    spocsPlacement
                  ),
                };
              }
            ),
            // We don't fill spocs in recs if sections are enabled,
            // because recs are not going to be seen.
            recommendations: data.recommendations,
          }
        : {
            recommendations: handleSpocs(
              data.recommendations,
              spocsPositions,
              spocsPlacement
            ),
          }),
    };

    let items = 0;
    if (component.properties && component.properties.items) {
      items = Math.min(component.properties.items, data.recommendations.length);
    }

    // loop through a component items
    // Store the items position sequentially for multiple components of the same type.
    // Example: A second card grid starts pos offset from the last card grid.
    for (let i = 0; i < items; i++) {
      data.recommendations[i] = {
        ...data.recommendations[i],
        pos: positions[component.type]++,
      };
    }

    // Setup absolute positions for sections layout.
    if (sectionsEnabled) {
      let currentPosition = 0;
      data.sections.forEach(section => {
        // We assume the count for the breakpoint with the most tiles.
        const { maxTile } = getMaxTiles(section?.layout?.responsiveLayouts);
        for (let i = 0; i < maxTile; i++) {
          if (section.data[i]) {
            section.data[i] = {
              ...section.data[i],
              pos: currentPosition++,
            };
          }
        }
      });
    }

    return { ...component, data };
  };

  const renderLayout = () => {
    const renderedLayoutArray = [];
    for (const row of layout.filter(
      r => r.components.filter(c => !filterArray.includes(c.type)).length
    )) {
      let components = [];
      renderedLayoutArray.push({
        ...row,
        components,
      });
      for (const component of row.components.filter(
        c => !filterArray.includes(c.type)
      )) {
        const spocsConfig = component.spocs;
        if (spocsConfig || component.feed) {
          if (
            (component.feed && !feeds.data[component.feed.url]) ||
            (spocsConfig &&
              spocsConfig.positions &&
              spocsConfig.positions.length &&
              !spocs.loaded)
          ) {
            components.push(placeholderComponent(component));
          } else if (component.feed) {
            components.push(handleComponentWithFeed(component));
          } else {
            components.push(handleComponent(component));
          }
        } else {
          components.push(component);
        }
      }
    }
    return renderedLayoutArray;
  };

  const layoutRender = renderLayout();

  return { layoutRender };
};

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/SectionContextMenu/SectionContextMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */




/**
 * A context menu for blocking, following and unfollowing sections.
 *
 * @param props
 * @returns {React.FunctionComponent}
 */
function SectionContextMenu({
  type = "DISCOVERY_STREAM",
  title,
  source,
  index,
  dispatch,
  sectionKey,
  following,
  sectionPersonalization,
  sectionPosition
}) {
  // Initial context menu options: block this section only.
  const SECTIONS_CONTEXT_MENU_OPTIONS = ["SectionBlock"];
  const [showContextMenu, setShowContextMenu] = (0,external_React_namespaceObject.useState)(false);
  if (following) {
    SECTIONS_CONTEXT_MENU_OPTIONS.push("SectionUnfollow");
  }
  const onClick = e => {
    e.preventDefault();
    setShowContextMenu(!showContextMenu);
  };
  const onUpdate = () => {
    setShowContextMenu(!showContextMenu);
  };
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-context-menu"
  }, /*#__PURE__*/external_React_default().createElement("moz-button", {
    type: "icon",
    size: "default",
    iconsrc: "chrome://global/skin/icons/more.svg",
    title: title || source,
    onClick: onClick
  }), showContextMenu && /*#__PURE__*/external_React_default().createElement(LinkMenu, {
    onUpdate: onUpdate,
    dispatch: dispatch,
    index: index,
    source: type.toUpperCase(),
    options: SECTIONS_CONTEXT_MENU_OPTIONS,
    shouldSendImpressionStats: true,
    site: {
      sectionPersonalization,
      sectionKey,
      sectionPosition,
      title
    }
  }));
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/InterestPicker/InterestPicker.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





const PREF_VISIBLE_SECTIONS = "discoverystream.sections.interestPicker.visibleSections";

/**
 * Shows a list of recommended topics with visual indication whether
 * the user follows some of the topics (active, blue, selected topics)
 * or is yet to do so (neutrally-coloured topics with a "plus" button).
 *
 * @returns {React.Element}
 */
function InterestPicker({
  title,
  subtitle,
  interests,
  receivedFeedRank
}) {
  const dispatch = (0,external_ReactRedux_namespaceObject.useDispatch)();
  const focusedRef = (0,external_React_namespaceObject.useRef)(null);
  const focusRef = (0,external_React_namespaceObject.useRef)(null);
  const [focusedIndex, setFocusedIndex] = (0,external_React_namespaceObject.useState)(0);
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const {
    sectionPersonalization
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const visibleSections = prefs[PREF_VISIBLE_SECTIONS]?.split(",").map(item => item.trim()).filter(item => item);
  const handleIntersection = (0,external_React_namespaceObject.useCallback)(() => {
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.INLINE_SELECTION_IMPRESSION,
      data: {
        section_position: receivedFeedRank
      }
    }));
  }, [dispatch, receivedFeedRank]);
  const ref = useIntersectionObserver(handleIntersection);
  const onKeyDown = (0,external_React_namespaceObject.useCallback)(e => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      // prevent the page from scrolling up/down while navigating.
      e.preventDefault();
    }
    if (focusedRef.current?.nextSibling?.querySelector("input") && e.key === "ArrowDown") {
      focusedRef.current.nextSibling.querySelector("input").tabIndex = 0;
      focusedRef.current.nextSibling.querySelector("input").focus();
    }
    if (focusedRef.current?.previousSibling?.querySelector("input") && e.key === "ArrowUp") {
      focusedRef.current.previousSibling.querySelector("input").tabIndex = 0;
      focusedRef.current.previousSibling.querySelector("input").focus();
    }
  }, []);
  function onWrapperFocus() {
    focusRef.current?.addEventListener("keydown", onKeyDown);
  }
  function onWrapperBlur() {
    focusRef.current?.removeEventListener("keydown", onKeyDown);
  }
  function onItemFocus(index) {
    setFocusedIndex(index);
  }

  // Updates user preferences as they follow or unfollow topics
  // by selecting them from the list
  function handleChange(e, index) {
    const {
      name: topic,
      checked
    } = e.target;
    let updatedSections = {
      ...sectionPersonalization
    };
    if (checked) {
      updatedSections[topic] = {
        isFollowed: true,
        isBlocked: false,
        followedAt: new Date().toISOString()
      };
      if (!visibleSections.includes(topic)) {
        // add section to visible sections and place after the inline picker
        // subtract 1 from the rank so that it is normalized with array index
        visibleSections.splice(receivedFeedRank - 1, 0, topic);
        dispatch(actionCreators.SetPref(PREF_VISIBLE_SECTIONS, visibleSections.join(", ")));
      }
    } else {
      delete updatedSections[topic];
    }
    dispatch(actionCreators.OnlyToMain({
      type: actionTypes.INLINE_SELECTION_CLICK,
      data: {
        topic,
        is_followed: checked,
        topic_position: index,
        section_position: receivedFeedRank
      }
    }));
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: updatedSections
    }));
  }
  return /*#__PURE__*/external_React_default().createElement("section", {
    className: "inline-selection-wrapper ds-section",
    ref: el => {
      ref.current = [el];
    }
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-heading"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-title-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("h2", {
    className: "section-title"
  }, title), /*#__PURE__*/external_React_default().createElement("p", {
    className: "section-subtitle"
  }, subtitle))), /*#__PURE__*/external_React_default().createElement("ul", {
    className: "topic-list",
    onFocus: onWrapperFocus,
    onBlur: onWrapperBlur,
    ref: focusRef
  }, interests.map((interest, index) => {
    const checked = sectionPersonalization[interest.sectionId]?.isFollowed;
    return /*#__PURE__*/external_React_default().createElement("li", {
      key: interest.sectionId,
      ref: index === focusedIndex ? focusedRef : null
    }, /*#__PURE__*/external_React_default().createElement("label", null, /*#__PURE__*/external_React_default().createElement("input", {
      type: "checkbox",
      id: interest.sectionId,
      name: interest.sectionId,
      checked: checked,
      "aria-checked": checked,
      onChange: e => handleChange(e, index),
      tabIndex: index === focusedIndex ? 0 : -1,
      onFocus: () => {
        onItemFocus(index);
      }
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "topic-item-label"
    }, interest.title || ""), /*#__PURE__*/external_React_default().createElement("div", {
      className: `topic-item-icon icon ${checked ? "icon-check-filled" : "icon-add-circle-fill"}`
    })));
  })), /*#__PURE__*/external_React_default().createElement("p", {
    className: "learn-more-copy"
  }, /*#__PURE__*/external_React_default().createElement("a", {
    href: prefs["support.url"],
    "data-l10n-id": "newtab-topic-selection-privacy-link"
  })));
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/PersonalizedCard/PersonalizedCard.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


const PersonalizedCard = ({
  onDismiss
}) => {
  const wavingFox = "chrome://newtab/content/data/content/assets/waving-fox.svg";
  return /*#__PURE__*/external_React_default().createElement("aside", {
    className: "personalized-card-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "personalized-card-dismiss"
  }, /*#__PURE__*/external_React_default().createElement("moz-button", {
    type: "icon ghost",
    iconSrc: "chrome://global/skin/icons/close.svg",
    onClick: onDismiss,
    "data-l10n-id": "newtab-toast-dismiss-button"
  })), /*#__PURE__*/external_React_default().createElement("div", {
    className: "personalized-card-inner"
  }, /*#__PURE__*/external_React_default().createElement("img", {
    src: wavingFox,
    alt: ""
  }), /*#__PURE__*/external_React_default().createElement("h2", null, "Personalized Just for You"), /*#__PURE__*/external_React_default().createElement("p", null, "We\u2019re customizing your feed to show content that matters to you, while ensuring your privacy is always respected."), /*#__PURE__*/external_React_default().createElement("moz-button", {
    type: "primary",
    class: "personalized-card-cta"
  }, "Manage your settings"), /*#__PURE__*/external_React_default().createElement("a", {
    href: "https://www.mozilla.org/en-US/privacy/firefox/#notice"
  }, "Learn how we protect and manage data")));
};
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/CardSections/CardSections.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */












// Prefs
const CardSections_PREF_SECTIONS_CARDS_ENABLED = "discoverystream.sections.cards.enabled";
const PREF_SECTIONS_CARDS_THUMBS_UP_DOWN_ENABLED = "discoverystream.sections.cards.thumbsUpDown.enabled";
const PREF_SECTIONS_PERSONALIZATION_ENABLED = "discoverystream.sections.personalization.enabled";
const CardSections_PREF_TOPICS_ENABLED = "discoverystream.topicLabels.enabled";
const CardSections_PREF_TOPICS_SELECTED = "discoverystream.topicSelection.selectedTopics";
const CardSections_PREF_TOPICS_AVAILABLE = "discoverystream.topicSelection.topics";
const CardSections_PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_INTEREST_PICKER_ENABLED = "discoverystream.sections.interestPicker.enabled";
const CardSections_PREF_VISIBLE_SECTIONS = "discoverystream.sections.interestPicker.visibleSections";
const PREF_MEDIUM_RECTANGLE_ENABLED = "newtabAdSize.mediumRectangle";
const CardSections_PREF_BILLBOARD_ENABLED = "newtabAdSize.billboard";
const CardSections_PREF_LEADERBOARD_ENABLED = "newtabAdSize.leaderboard";
const CardSections_PREF_LEADERBOARD_POSITION = "newtabAdSize.leaderboard.position";
const CardSections_PREF_BILLBOARD_POSITION = "newtabAdSize.billboard.position";
const PREF_INFERRED_PERSONALIZATION_ENABLED = "discoverystream.sections.personalization.inferred.enabled";
const PREF_INFERRED_PERSONALIZATION_USER_ENABLED = "discoverystream.sections.personalization.inferred.user.enabled";
const PREF_INFERRED_PERSONALIZATION_POSITION = "discoverystream.sections.personalization.inferred.position";
const PREF_INFERRED_PERSONALIZATION_BLOCKED = "discoverystream.sections.personalization.inferred.blocked";
function getLayoutData(responsiveLayouts, index) {
  let layoutData = {
    classNames: [],
    imageSizes: {}
  };
  responsiveLayouts.forEach(layout => {
    layout.tiles.forEach((tile, tileIndex) => {
      if (tile.position === index) {
        layoutData.classNames.push(`col-${layout.columnCount}-${tile.size}`);
        layoutData.classNames.push(`col-${layout.columnCount}-position-${tileIndex}`);
        layoutData.imageSizes[layout.columnCount] = tile.size;

        // The API tells us whether the tile should show the excerpt or not.
        // Apply extra styles accordingly.
        if (tile.hasExcerpt) {
          layoutData.classNames.push(`col-${layout.columnCount}-show-excerpt`);
        } else {
          layoutData.classNames.push(`col-${layout.columnCount}-hide-excerpt`);
        }
      }
    });
  });
  return layoutData;
}

// function to determine amount of tiles shown per section per viewport
function getMaxTiles(responsiveLayouts) {
  return responsiveLayouts.flatMap(responsiveLayout => responsiveLayout).reduce((acc, t) => {
    acc[t.columnCount] = t.tiles.length;

    // Update maxTile if current tile count is greater
    if (!acc.maxTile || t.tiles.length > acc.maxTile) {
      acc.maxTile = t.tiles.length;
    }
    return acc;
  }, {});
}

/**
 * Transforms a comma-separated string in user preferences
 * into a cleaned-up array.
 *
 * @param {string} pref - The comma-separated pref to be converted.
 * @returns {string[]} An array of trimmed strings, excluding empty values.
 */

const prefToArray = (pref = "") => {
  return pref.split(",").map(item => item.trim()).filter(item => item);
};
function CardSection({
  sectionPosition,
  section,
  dispatch,
  type,
  firstVisibleTimestamp,
  is_collection,
  spocMessageVariant,
  ctaButtonVariant,
  ctaButtonSponsors
}) {
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const {
    sectionPersonalization
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const showTopics = prefs[CardSections_PREF_TOPICS_ENABLED];
  const mayHaveSectionsCards = prefs[CardSections_PREF_SECTIONS_CARDS_ENABLED];
  const mayHaveSectionsCardsThumbsUpDown = prefs[PREF_SECTIONS_CARDS_THUMBS_UP_DOWN_ENABLED];
  const mayHaveThumbsUpDown = prefs[CardSections_PREF_THUMBS_UP_DOWN_ENABLED];
  const selectedTopics = prefs[CardSections_PREF_TOPICS_SELECTED];
  const availableTopics = prefs[CardSections_PREF_TOPICS_AVAILABLE];
  const {
    saveToPocketCard
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const mayHaveSectionsPersonalization = prefs[PREF_SECTIONS_PERSONALIZATION_ENABLED];
  const {
    sectionKey,
    title,
    subtitle
  } = section;
  const {
    responsiveLayouts
  } = section.layout;
  const following = sectionPersonalization[sectionKey]?.isFollowed;
  const handleIntersection = (0,external_React_namespaceObject.useCallback)(() => {
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.CARD_SECTION_IMPRESSION,
      data: {
        section: sectionKey,
        section_position: sectionPosition,
        is_section_followed: following
      }
    }));
  }, [dispatch, sectionKey, sectionPosition, following]);

  // Ref to hold the section element
  const sectionRefs = useIntersectionObserver(handleIntersection);

  // Only show thumbs up/down buttons if both default thumbs and sections thumbs prefs are enabled
  const mayHaveCombinedThumbsUpDown = mayHaveSectionsCardsThumbsUpDown && mayHaveThumbsUpDown;
  const onFollowClick = (0,external_React_namespaceObject.useCallback)(() => {
    const updatedSectionData = {
      ...sectionPersonalization,
      [sectionKey]: {
        isFollowed: true,
        isBlocked: false,
        followedAt: new Date().toISOString()
      }
    };
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: updatedSectionData
    }));
    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "FOLLOW_SECTION",
      data: {
        section: sectionKey,
        section_position: sectionPosition,
        event_source: "MOZ_BUTTON"
      }
    }));
  }, [dispatch, sectionPersonalization, sectionKey, sectionPosition]);
  const onUnfollowClick = (0,external_React_namespaceObject.useCallback)(() => {
    const updatedSectionData = {
      ...sectionPersonalization
    };
    delete updatedSectionData[sectionKey];
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: updatedSectionData
    }));

    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "UNFOLLOW_SECTION",
      data: {
        section: sectionKey,
        section_position: sectionPosition,
        event_source: "MOZ_BUTTON"
      }
    }));
  }, [dispatch, sectionPersonalization, sectionKey, sectionPosition]);
  const {
    maxTile
  } = getMaxTiles(responsiveLayouts);
  const displaySections = section.data.slice(0, maxTile);
  const isSectionEmpty = !displaySections?.length;
  const shouldShowLabels = sectionKey === "top_stories_section" && showTopics;
  if (isSectionEmpty) {
    return null;
  }
  const sectionContextWrapper = /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-context-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: following ? "section-follow following" : "section-follow"
  }, /*#__PURE__*/external_React_default().createElement("moz-button", {
    onClick: following ? onUnfollowClick : onFollowClick,
    type: "default",
    index: sectionPosition,
    section: sectionKey
  }, /*#__PURE__*/external_React_default().createElement("span", {
    className: "section-button-follow-text",
    "data-l10n-id": "newtab-section-follow-button"
  }), /*#__PURE__*/external_React_default().createElement("span", {
    className: "section-button-following-text",
    "data-l10n-id": "newtab-section-following-button"
  }), /*#__PURE__*/external_React_default().createElement("span", {
    className: "section-button-unfollow-text",
    "data-l10n-id": "newtab-section-unfollow-button"
  }))), /*#__PURE__*/external_React_default().createElement(SectionContextMenu, {
    dispatch: dispatch,
    index: sectionPosition,
    following: following,
    sectionPersonalization: sectionPersonalization,
    sectionKey: sectionKey,
    title: title,
    type: type,
    sectionPosition: sectionPosition
  }));

  // Determine to display first medium-sized in MREC IAB format
  const mediumRectangleEnabled = prefs[PREF_MEDIUM_RECTANGLE_ENABLED];
  let adSizingVariantClassName = "";
  if (mediumRectangleEnabled) {
    adSizingVariantClassName = "ad-sizing-variant-a";
  }
  return /*#__PURE__*/external_React_default().createElement("section", {
    className: "ds-section",
    ref: el => {
      sectionRefs.current[0] = el;
    }
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-heading"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "section-title-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("h2", {
    className: "section-title"
  }, title), subtitle && /*#__PURE__*/external_React_default().createElement("p", {
    className: "section-subtitle"
  }, subtitle)), mayHaveSectionsPersonalization ? sectionContextWrapper : null), /*#__PURE__*/external_React_default().createElement("div", {
    className: `ds-section-grid ds-card-grid ${adSizingVariantClassName}`
  }, section.data.slice(0, maxTile).map((rec, index) => {
    const {
      classNames,
      imageSizes
    } = getLayoutData(responsiveLayouts, index);
    if (!rec || rec.placeholder) {
      return /*#__PURE__*/external_React_default().createElement(PlaceholderDSCard, {
        key: `dscard-${index}`
      });
    }
    return /*#__PURE__*/external_React_default().createElement(DSCard, {
      key: `dscard-${rec.id}`,
      pos: rec.pos,
      flightId: rec.flight_id,
      image_src: rec.image_src,
      raw_image_src: rec.raw_image_src,
      icon_src: rec.icon_src,
      word_count: rec.word_count,
      time_to_read: rec.time_to_read,
      title: rec.title,
      topic: rec.topic,
      excerpt: rec.excerpt,
      url: rec.url,
      id: rec.id,
      shim: rec.shim,
      fetchTimestamp: rec.fetchTimestamp,
      type: type,
      context: rec.context,
      sponsor: rec.sponsor,
      sponsored_by_override: rec.sponsored_by_override,
      dispatch: dispatch,
      source: rec.domain,
      publisher: rec.publisher,
      pocket_id: rec.pocket_id,
      context_type: rec.context_type,
      bookmarkGuid: rec.bookmarkGuid,
      recommendation_id: rec.recommendation_id,
      firstVisibleTimestamp: firstVisibleTimestamp,
      corpus_item_id: rec.corpus_item_id,
      scheduled_corpus_item_id: rec.scheduled_corpus_item_id,
      recommended_at: rec.recommended_at,
      received_rank: rec.received_rank,
      format: rec.format,
      alt_text: rec.alt_text,
      mayHaveThumbsUpDown: mayHaveCombinedThumbsUpDown,
      mayHaveSectionsCards: mayHaveSectionsCards,
      showTopics: shouldShowLabels,
      selectedTopics: selectedTopics,
      availableTopics: availableTopics,
      is_collection: is_collection,
      saveToPocketCard: saveToPocketCard,
      ctaButtonSponsors: ctaButtonSponsors,
      ctaButtonVariant: ctaButtonVariant,
      spocMessageVariant: spocMessageVariant,
      sectionsClassNames: classNames.join(" "),
      sectionsCardImageSizes: imageSizes,
      section: sectionKey,
      sectionPosition: sectionPosition,
      sectionFollowed: following,
      isTimeSensitive: rec.isTimeSensitive
    });
  })));
}
function CardSections({
  data,
  feed,
  dispatch,
  type,
  firstVisibleTimestamp,
  is_collection,
  spocMessageVariant,
  ctaButtonVariant,
  ctaButtonSponsors
}) {
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const {
    spocs,
    sectionPersonalization
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const personalizationEnabled = prefs[PREF_SECTIONS_PERSONALIZATION_ENABLED];
  const interestPickerEnabled = prefs[PREF_INTEREST_PICKER_ENABLED];

  // Handle a render before feed has been fetched by displaying nothing
  if (!data) {
    return null;
  }
  const visibleSections = prefToArray(prefs[CardSections_PREF_VISIBLE_SECTIONS]);
  const {
    interestPicker
  } = data;
  let filteredSections = data.sections.filter(section => !sectionPersonalization[section.sectionKey]?.isBlocked);
  if (interestPickerEnabled && visibleSections.length) {
    filteredSections = visibleSections.reduce((acc, visibleSection) => {
      const found = filteredSections.find(({
        sectionKey
      }) => sectionKey === visibleSection);
      if (found) {
        acc.push(found);
      }
      return acc;
    }, []);
  }
  let sectionsToRender = filteredSections.map((section, sectionPosition) => /*#__PURE__*/external_React_default().createElement(CardSection, {
    key: `section-${section.sectionKey}`,
    sectionPosition: sectionPosition,
    section: section,
    dispatch: dispatch,
    type: type,
    firstVisibleTimestamp: firstVisibleTimestamp,
    is_collection: is_collection,
    spocMessageVariant: spocMessageVariant,
    ctaButtonVariant: ctaButtonVariant,
    ctaButtonSponsors: ctaButtonSponsors
  }));

  // Add a billboard/leaderboard IAB ad to the sectionsToRender array (if enabled/possible).
  const billboardEnabled = prefs[CardSections_PREF_BILLBOARD_ENABLED];
  const leaderboardEnabled = prefs[CardSections_PREF_LEADERBOARD_ENABLED];
  if ((billboardEnabled || leaderboardEnabled) && spocs?.data?.newtab_spocs?.items) {
    const spocToRender = spocs.data.newtab_spocs.items.find(({
      format
    }) => format === "leaderboard" && leaderboardEnabled) || spocs.data.newtab_spocs.items.find(({
      format
    }) => format === "billboard" && billboardEnabled);
    if (spocToRender && !spocs.blocked.includes(spocToRender.url)) {
      const row = spocToRender.format === "leaderboard" ? prefs[CardSections_PREF_LEADERBOARD_POSITION] : prefs[CardSections_PREF_BILLBOARD_POSITION];
      sectionsToRender.splice(
      // Math.min is used here to ensure the given row stays within the bounds of the sectionsToRender array.
      Math.min(sectionsToRender.length - 1, row), 0, /*#__PURE__*/external_React_default().createElement(AdBanner, {
        spoc: spocToRender,
        key: `dscard-${spocToRender.id}`,
        dispatch: dispatch,
        type: type,
        firstVisibleTimestamp: firstVisibleTimestamp,
        row: row,
        prefs: prefs
      }));
    }
  }

  // Add the interest picker to the sectionsToRender array (if enabled/possible).
  if (interestPickerEnabled && personalizationEnabled && interestPicker?.sections) {
    const index = interestPicker.receivedFeedRank - 1;
    sectionsToRender.splice(
    // Math.min is used here to ensure the given row stays within the bounds of the sectionsToRender array.
    Math.min(sectionsToRender.length - 1, index), 0, /*#__PURE__*/external_React_default().createElement(InterestPicker, {
      title: interestPicker.title,
      subtitle: interestPicker.subtitle,
      interests: interestPicker.sections || [],
      receivedFeedRank: interestPicker.receivedFeedRank
    }));
  }
  const handleDismissP13nCard = () => {
    dispatch(actionCreators.SetPref(PREF_INFERRED_PERSONALIZATION_BLOCKED, true));
  };
  function displayP13nCard() {
    const row = prefs[PREF_INFERRED_PERSONALIZATION_POSITION];
    const cardBlocked = prefs[PREF_INFERRED_PERSONALIZATION_BLOCKED];
    const cardEnabled = prefs[PREF_INFERRED_PERSONALIZATION_ENABLED];
    const userEnabled = prefs[PREF_INFERRED_PERSONALIZATION_USER_ENABLED];
    if (!cardBlocked && cardEnabled && userEnabled) {
      sectionsToRender.splice(row, 0, /*#__PURE__*/external_React_default().createElement(PersonalizedCard, {
        row: row,
        onDismiss: handleDismissP13nCard
      }));
    }
  }
  displayP13nCard();
  const isEmpty = sectionsToRender.length === 0;
  return isEmpty ? /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-card-grid empty"
  }, /*#__PURE__*/external_React_default().createElement(DSEmptyState, {
    status: data.status,
    dispatch: dispatch,
    feed: feed
  })) : /*#__PURE__*/external_React_default().createElement("div", {
    className: "ds-section-wrapper"
  }, sectionsToRender);
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamBase/DiscoveryStreamBase.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



















const ALLOWED_CSS_URL_PREFIXES = ["chrome://", "resource://", "https://img-getpocket.cdn.mozilla.net/"];
const DUMMY_CSS_SELECTOR = "DUMMY#CSS.SELECTOR";

/**
 * Validate a CSS declaration. The values are assumed to be normalized by CSSOM.
 */
function isAllowedCSS(property, value) {
  // Bug 1454823: INTERNAL properties, e.g., -moz-context-properties, are
  // exposed but their values aren't resulting in getting nothing. Fortunately,
  // we don't care about validating the values of the current set of properties.
  if (value === undefined) {
    return true;
  }

  // Make sure all urls are of the allowed protocols/prefixes
  const urls = value.match(/url\("[^"]+"\)/g);
  return !urls || urls.every(url => ALLOWED_CSS_URL_PREFIXES.some(prefix => url.slice(5).startsWith(prefix)));
}
class _DiscoveryStreamBase extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onStyleMount = this.onStyleMount.bind(this);
  }
  onStyleMount(style) {
    // Unmounting style gets rid of old styles, so nothing else to do
    if (!style) {
      return;
    }
    const {
      sheet
    } = style;
    const styles = JSON.parse(style.dataset.styles);
    styles.forEach((row, rowIndex) => {
      row.forEach((component, componentIndex) => {
        // Nothing to do without optional styles overrides
        if (!component) {
          return;
        }
        Object.entries(component).forEach(([selectors, declarations]) => {
          // Start with a dummy rule to validate declarations and selectors
          sheet.insertRule(`${DUMMY_CSS_SELECTOR} {}`);
          const [rule] = sheet.cssRules;

          // Validate declarations and remove any offenders. CSSOM silently
          // discards invalid entries, so here we apply extra restrictions.
          rule.style = declarations;
          [...rule.style].forEach(property => {
            const value = rule.style[property];
            if (!isAllowedCSS(property, value)) {
              console.error(`Bad CSS declaration ${property}: ${value}`);
              rule.style.removeProperty(property);
            }
          });

          // Set the actual desired selectors scoped to the component
          const prefix = `.ds-layout > .ds-column:nth-child(${rowIndex + 1}) .ds-column-grid > :nth-child(${componentIndex + 1})`;
          // NB: Splitting on "," doesn't work with strings with commas, but
          // we're okay with not supporting those selectors
          rule.selectorText = selectors.split(",").map(selector => prefix + (
          // Assume :pseudo-classes are for component instead of descendant
          selector[0] === ":" ? "" : " ") + selector).join(",");

          // CSSOM silently ignores bad selectors, so we'll be noisy instead
          if (rule.selectorText === DUMMY_CSS_SELECTOR) {
            console.error(`Bad CSS selector ${selectors}`);
          }
        });
      });
    });
  }
  renderComponent(component) {
    switch (component.type) {
      case "Highlights":
        return /*#__PURE__*/external_React_default().createElement(Highlights, null);
      case "TopSites":
        return /*#__PURE__*/external_React_default().createElement("div", {
          className: "ds-top-sites"
        }, /*#__PURE__*/external_React_default().createElement(TopSites_TopSites, {
          isFixed: true,
          title: component.header?.title
        }));
      case "TextPromo":
        return /*#__PURE__*/external_React_default().createElement(DSTextPromo, {
          dispatch: this.props.dispatch,
          type: component.type,
          data: component.data
        });
      case "Signup":
        return /*#__PURE__*/external_React_default().createElement(DSSignup, {
          dispatch: this.props.dispatch,
          type: component.type,
          data: component.data
        });
      case "Message":
        return /*#__PURE__*/external_React_default().createElement(DSMessage, {
          title: component.header && component.header.title,
          subtitle: component.header && component.header.subtitle,
          link_text: component.header && component.header.link_text,
          link_url: component.header && component.header.link_url,
          icon: component.header && component.header.icon,
          essentialReadsHeader: component.essentialReadsHeader,
          editorsPicksHeader: component.editorsPicksHeader
        });
      case "SectionTitle":
        return /*#__PURE__*/external_React_default().createElement(SectionTitle, {
          header: component.header
        });
      case "Navigation":
        return /*#__PURE__*/external_React_default().createElement(Navigation, {
          dispatch: this.props.dispatch,
          links: component.properties.links,
          extraLinks: component.properties.extraLinks,
          alignment: component.properties.alignment,
          explore_topics: component.properties.explore_topics,
          header: component.header,
          locale: this.props.App.locale,
          newFooterSection: component.newFooterSection,
          privacyNoticeURL: component.properties.privacyNoticeURL
        });
      case "CollectionCardGrid":
        {
          const {
            DiscoveryStream
          } = this.props;
          return /*#__PURE__*/external_React_default().createElement(CollectionCardGrid, {
            data: component.data,
            feed: component.feed,
            spocs: DiscoveryStream.spocs,
            placement: component.placement,
            type: component.type,
            items: component.properties.items,
            dismissible: this.props.DiscoveryStream.isCollectionDismissible,
            dispatch: this.props.dispatch
          });
        }
      case "CardGrid":
        {
          const sectionsEnabled = this.props.Prefs.values["discoverystream.sections.enabled"];
          if (sectionsEnabled) {
            return /*#__PURE__*/external_React_default().createElement(CardSections, {
              feed: component.feed,
              data: component.data,
              dispatch: this.props.dispatch,
              type: component.type,
              firstVisibleTimestamp: this.props.firstVisibleTimestamp,
              is_collection: true,
              ctaButtonSponsors: component.properties.ctaButtonSponsors,
              ctaButtonVariant: component.properties.ctaButtonVariant,
              spocMessageVariant: component.properties.spocMessageVariant
            });
          }
          return /*#__PURE__*/external_React_default().createElement(CardGrid, {
            title: component.header && component.header.title,
            data: component.data,
            feed: component.feed,
            widgets: component.widgets,
            type: component.type,
            dispatch: this.props.dispatch,
            items: component.properties.items,
            hybridLayout: component.properties.hybridLayout,
            hideCardBackground: component.properties.hideCardBackground,
            fourCardLayout: component.properties.fourCardLayout,
            compactGrid: component.properties.compactGrid,
            essentialReadsHeader: component.properties.essentialReadsHeader,
            onboardingExperience: component.properties.onboardingExperience,
            ctaButtonSponsors: component.properties.ctaButtonSponsors,
            ctaButtonVariant: component.properties.ctaButtonVariant,
            spocMessageVariant: component.properties.spocMessageVariant,
            editorsPicksHeader: component.properties.editorsPicksHeader,
            recentSavesEnabled: this.props.DiscoveryStream.recentSavesEnabled,
            hideDescriptions: this.props.DiscoveryStream.hideDescriptions,
            firstVisibleTimestamp: this.props.firstVisibleTimestamp
          });
        }
      case "HorizontalRule":
        return /*#__PURE__*/external_React_default().createElement(HorizontalRule, null);
      case "PrivacyLink":
        return /*#__PURE__*/external_React_default().createElement(PrivacyLink, {
          properties: component.properties
        });
      default:
        return /*#__PURE__*/external_React_default().createElement("div", null, component.type);
    }
  }
  renderStyles(styles) {
    // Use json string as both the key and styles to render so React knows when
    // to unmount and mount a new instance for new styles.
    const json = JSON.stringify(styles);
    return /*#__PURE__*/external_React_default().createElement("style", {
      key: json,
      "data-styles": json,
      ref: this.onStyleMount
    });
  }
  render() {
    const {
      locale,
      mayHaveSponsoredStories
    } = this.props;
    // Select layout render data by adding spocs and position to recommendations
    const {
      layoutRender
    } = selectLayoutRender({
      state: this.props.DiscoveryStream,
      prefs: this.props.Prefs.values,
      locale
    });
    const sectionsEnabled = this.props.Prefs.values["discoverystream.sections.enabled"];
    const {
      config
    } = this.props.DiscoveryStream;
    const topicSelectionEnabled = this.props.Prefs.values["discoverystream.topicSelection.enabled"];
    const reportAdsEnabled = this.props.Prefs.values["discoverystream.reportAds.enabled"];
    const spocsEnabled = this.props.Prefs.values["unifiedAds.spocs.enabled"];

    // Allow rendering without extracting special components
    if (!config.collapsible) {
      return this.renderLayout(layoutRender);
    }

    // Find the first component of a type and remove it from layout
    const extractComponent = type => {
      for (const [rowIndex, row] of Object.entries(layoutRender)) {
        for (const [index, component] of Object.entries(row.components)) {
          if (component.type === type) {
            // Remove the row if it was the only component or the single item
            if (row.components.length === 1) {
              layoutRender.splice(rowIndex, 1);
            } else {
              row.components.splice(index, 1);
            }
            return component;
          }
        }
      }
      return null;
    };

    // Get "topstories" Section state for default values
    const topStories = this.props.Sections.find(s => s.id === "topstories");
    if (!topStories) {
      return null;
    }

    // Extract TopSites to render before the rest and Message to use for header
    const topSites = extractComponent("TopSites");
    const sponsoredCollection = extractComponent("CollectionCardGrid");
    const message = extractComponent("Message") || {
      header: {
        link_text: topStories.learnMore.link.message,
        link_url: topStories.learnMore.link.href,
        title: topStories.title
      }
    };
    const privacyLinkComponent = extractComponent("PrivacyLink");
    let learnMore = {
      link: {
        href: message.header.link_url,
        message: message.header.link_text
      }
    };
    let sectionTitle = message.header.title;
    let subTitle = "";

    // If we're in one of these experiments, override the default message.
    // For now this is English only.
    if (message.essentialReadsHeader || message.editorsPicksHeader) {
      learnMore = null;
      subTitle = "Recommended By Pocket";
      if (message.essentialReadsHeader) {
        sectionTitle = "Today’s Essential Reads";
      } else if (message.editorsPicksHeader) {
        sectionTitle = "Editor’s Picks";
      }
    }
    const {
      DiscoveryStream
    } = this.props;
    return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, this.props.DiscoveryStream.isPrivacyInfoModalVisible && /*#__PURE__*/external_React_default().createElement(DSPrivacyModal, {
      dispatch: this.props.dispatch
    }), (reportAdsEnabled && spocsEnabled || sectionsEnabled) && /*#__PURE__*/external_React_default().createElement(ReportContent, {
      spocs: DiscoveryStream.spocs
    }), topSites && this.renderLayout([{
      width: 12,
      components: [topSites],
      sectionType: "topsites"
    }]), sponsoredCollection && this.renderLayout([{
      width: 12,
      components: [sponsoredCollection]
    }]), !!layoutRender.length && /*#__PURE__*/external_React_default().createElement(CollapsibleSection, {
      className: "ds-layout",
      collapsed: topStories.pref.collapsed,
      dispatch: this.props.dispatch,
      id: topStories.id,
      isFixed: true,
      learnMore: learnMore,
      privacyNoticeURL: topStories.privacyNoticeURL,
      showPrefName: topStories.pref.feed,
      title: sectionTitle,
      subTitle: subTitle,
      mayHaveSponsoredStories: mayHaveSponsoredStories,
      mayHaveTopicsSelection: topicSelectionEnabled,
      sectionsEnabled: sectionsEnabled,
      spocMessageVariant: message?.properties?.spocMessageVariant,
      eventSource: "CARDGRID"
    }, this.renderLayout(layoutRender)), this.renderLayout([{
      width: 12,
      components: [{
        type: "Highlights"
      }]
    }]), privacyLinkComponent && this.renderLayout([{
      width: 12,
      components: [privacyLinkComponent]
    }]));
  }
  renderLayout(layoutRender) {
    const styles = [];
    let [data] = layoutRender;
    // Add helper class for topsites
    const topsitesClass = data.sectionType ? "ds-layout-topsites" : "";
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: `discovery-stream ds-layout ${topsitesClass}`
    }, layoutRender.map((row, rowIndex) => /*#__PURE__*/external_React_default().createElement("div", {
      key: `row-${rowIndex}`,
      className: `ds-column ds-column-${row.width}`
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "ds-column-grid"
    }, row.components.map((component, componentIndex) => {
      if (!component) {
        return null;
      }
      styles[rowIndex] = [...(styles[rowIndex] || []), component.styles];
      return /*#__PURE__*/external_React_default().createElement("div", {
        key: `component-${componentIndex}`
      }, this.renderComponent(component, row.width));
    })))), this.renderStyles(styles));
  }
}
const DiscoveryStreamBase = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  DiscoveryStream: state.DiscoveryStream,
  Prefs: state.Prefs,
  Sections: state.Sections,
  document: globalThis.document,
  App: state.App
}))(_DiscoveryStreamBase);
;// CONCATENATED MODULE: ./content-src/components/CustomizeMenu/SectionsMgmtPanel/SectionsMgmtPanel.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




// eslint-disable-next-line no-shadow

function SectionsMgmtPanel({
  exitEventFired
}) {
  const [showPanel, setShowPanel] = (0,external_React_namespaceObject.useState)(false); // State management with useState
  const {
    sectionPersonalization
  } = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream);
  const layoutComponents = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream.layout[0].components);
  const sections = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.DiscoveryStream.feeds.data);
  const dispatch = (0,external_ReactRedux_namespaceObject.useDispatch)();

  // TODO: Wrap sectionsFeedName -> sectionsList logic in try...catch?
  let sectionsFeedName;
  const cardGridEntry = layoutComponents.find(item => item.type === "CardGrid");
  if (cardGridEntry) {
    sectionsFeedName = cardGridEntry.feed.url;
  }
  let sectionsList;
  if (sectionsFeedName) {
    sectionsList = sections[sectionsFeedName].data.sections;
  }
  const [sectionsState, setSectionState] = (0,external_React_namespaceObject.useState)(sectionPersonalization); // State management with useState

  let followedSectionsData = sectionsList.filter(item => sectionsState[item.sectionKey]?.isFollowed);
  let blockedSectionsData = sectionsList.filter(item => sectionsState[item.sectionKey]?.isBlocked);
  function updateCachedData() {
    // Reset cached followed/blocked list data while panel is open
    setSectionState(sectionPersonalization);
    followedSectionsData = sectionsList.filter(item => sectionsState[item.sectionKey]?.isFollowed);
    blockedSectionsData = sectionsList.filter(item => sectionsState[item.sectionKey]?.isBlocked);
  }
  const onFollowClick = (0,external_React_namespaceObject.useCallback)((sectionKey, receivedRank) => {
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: {
        ...sectionPersonalization,
        [sectionKey]: {
          isFollowed: true,
          isBlocked: false,
          followedAt: new Date().toISOString()
        }
      }
    }));
    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "FOLLOW_SECTION",
      data: {
        section: sectionKey,
        section_position: receivedRank,
        event_source: "CUSTOMIZE_PANEL"
      }
    }));
  }, [dispatch, sectionPersonalization]);
  const onBlockClick = (0,external_React_namespaceObject.useCallback)((sectionKey, receivedRank) => {
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: {
        ...sectionPersonalization,
        [sectionKey]: {
          isFollowed: false,
          isBlocked: true
        }
      }
    }));

    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "BLOCK_SECTION",
      data: {
        section: sectionKey,
        section_position: receivedRank,
        event_source: "CUSTOMIZE_PANEL"
      }
    }));
  }, [dispatch, sectionPersonalization]);
  const onUnblockClick = (0,external_React_namespaceObject.useCallback)((sectionKey, receivedRank) => {
    const updatedSectionData = {
      ...sectionPersonalization
    };
    delete updatedSectionData[sectionKey];
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: updatedSectionData
    }));
    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "UNBLOCK_SECTION",
      data: {
        section: sectionKey,
        section_position: receivedRank,
        event_source: "CUSTOMIZE_PANEL"
      }
    }));
  }, [dispatch, sectionPersonalization]);
  const onUnfollowClick = (0,external_React_namespaceObject.useCallback)((sectionKey, receivedRank) => {
    const updatedSectionData = {
      ...sectionPersonalization
    };
    delete updatedSectionData[sectionKey];
    dispatch(actionCreators.AlsoToMain({
      type: actionTypes.SECTION_PERSONALIZATION_UPDATE,
      data: updatedSectionData
    }));
    // Telemetry Event Dispatch
    dispatch(actionCreators.OnlyToMain({
      type: "UNFOLLOW_SECTION",
      data: {
        section: sectionKey,
        section_position: receivedRank,
        event_source: "CUSTOMIZE_PANEL"
      }
    }));
  }, [dispatch, sectionPersonalization]);

  // Close followed/blocked topic subpanel when parent menu is closed
  (0,external_React_namespaceObject.useEffect)(() => {
    if (exitEventFired) {
      setShowPanel(false);
    }
  }, [exitEventFired]);
  const togglePanel = () => {
    setShowPanel(prevShowPanel => !prevShowPanel);

    // Fire when the panel is open
    if (!showPanel) {
      updateCachedData();
    }
  };
  const followedSectionsList = followedSectionsData.map(({
    sectionKey,
    title,
    receivedRank
  }) => {
    const following = sectionPersonalization[sectionKey]?.isFollowed;
    return /*#__PURE__*/external_React_default().createElement("li", {
      key: sectionKey
    }, /*#__PURE__*/external_React_default().createElement("label", {
      htmlFor: `follow-topic-${sectionKey}`
    }, title), /*#__PURE__*/external_React_default().createElement("div", {
      className: following ? "section-follow following" : "section-follow"
    }, /*#__PURE__*/external_React_default().createElement("moz-button", {
      onClick: () => following ? onUnfollowClick(sectionKey, receivedRank) : onFollowClick(sectionKey, receivedRank),
      type: "default",
      index: receivedRank,
      section: sectionKey,
      id: `follow-topic-${sectionKey}`
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-follow-text",
      "data-l10n-id": "newtab-section-follow-button"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-following-text",
      "data-l10n-id": "newtab-section-following-button"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-unfollow-text",
      "data-l10n-id": "newtab-section-unfollow-button"
    }))));
  });
  const blockedSectionsList = blockedSectionsData.map(({
    sectionKey,
    title,
    receivedRank
  }) => {
    const blocked = sectionPersonalization[sectionKey]?.isBlocked;
    return /*#__PURE__*/external_React_default().createElement("li", {
      key: sectionKey
    }, /*#__PURE__*/external_React_default().createElement("label", {
      htmlFor: `blocked-topic-${sectionKey}`
    }, title), /*#__PURE__*/external_React_default().createElement("div", {
      className: blocked ? "section-block blocked" : "section-block"
    }, /*#__PURE__*/external_React_default().createElement("moz-button", {
      onClick: () => blocked ? onUnblockClick(sectionKey, receivedRank) : onBlockClick(sectionKey, receivedRank),
      type: "default",
      index: receivedRank,
      section: sectionKey,
      id: `blocked-topic-${sectionKey}`
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-block-text",
      "data-l10n-id": "newtab-section-block-button"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-blocked-text",
      "data-l10n-id": "newtab-section-blocked-button"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "section-button-unblock-text",
      "data-l10n-id": "newtab-section-unblock-button"
    }))));
  });
  return /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("moz-box-button", {
    onClick: togglePanel,
    "data-l10n-id": "newtab-section-manage-topics-button-v2"
  }), /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.CSSTransition, {
    in: showPanel,
    timeout: 300,
    classNames: "sections-mgmt-panel",
    unmountOnExit: true
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "sections-mgmt-panel"
  }, /*#__PURE__*/external_React_default().createElement("button", {
    className: "arrow-button",
    onClick: togglePanel
  }, /*#__PURE__*/external_React_default().createElement("h1", {
    "data-l10n-id": "newtab-section-mangage-topics-title"
  })), /*#__PURE__*/external_React_default().createElement("h3", {
    "data-l10n-id": "newtab-section-mangage-topics-followed-topics"
  }), followedSectionsData.length ? /*#__PURE__*/external_React_default().createElement("ul", {
    className: "topic-list"
  }, followedSectionsList) : /*#__PURE__*/external_React_default().createElement("span", {
    className: "topic-list-empty-state",
    "data-l10n-id": "newtab-section-mangage-topics-followed-topics-empty-state"
  }), /*#__PURE__*/external_React_default().createElement("h3", {
    "data-l10n-id": "newtab-section-mangage-topics-blocked-topics"
  }), blockedSectionsData.length ? /*#__PURE__*/external_React_default().createElement("ul", {
    className: "topic-list"
  }, blockedSectionsList) : /*#__PURE__*/external_React_default().createElement("span", {
    className: "topic-list-empty-state",
    "data-l10n-id": "newtab-section-mangage-topics-blocked-topics-empty-state"
  }))));
}

;// CONCATENATED MODULE: ./content-src/components/WallpapersSection/WallpapersSection.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




class _WallpapersSection extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.handleChange = this.handleChange.bind(this);
    this.handleReset = this.handleReset.bind(this);
    this.prefersHighContrastQuery = null;
    this.prefersDarkQuery = null;
  }
  componentDidMount() {
    this.prefersDarkQuery = globalThis.matchMedia("(prefers-color-scheme: dark)");
  }
  handleChange(event) {
    const {
      id
    } = event.target;
    const prefs = this.props.Prefs.values;
    if (prefs["newtabWallpapers.v2.enabled"]) {
      // If we don't care about color mode, set both to the same wallpaper.
      this.props.setPref(`newtabWallpapers.wallpaper`, id);
    }
    this.handleUserEvent({
      selected_wallpaper: id,
      hadPreviousWallpaper: !!this.props.activeWallpaper
    });
  }
  handleReset() {
    const prefs = this.props.Prefs.values;
    if (prefs["newtabWallpapers.v2.enabled"]) {
      this.props.setPref("newtabWallpapers.wallpaper", "");
    }
    this.handleUserEvent({
      selected_wallpaper: "none",
      hadPreviousWallpaper: !!this.props.activeWallpaper
    });
  }

  // Record user interaction when changing wallpaper and reseting wallpaper to default
  handleUserEvent(data) {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.WALLPAPER_CLICK,
      data
    }));
  }
  render() {
    const {
      wallpaperList
    } = this.props.Wallpapers;
    const {
      activeWallpaper
    } = this.props;
    const prefs = this.props.Prefs.values;
    let fieldsetClassname = `wallpaper-list`;
    if (prefs["newtabWallpapers.v2.enabled"]) {
      fieldsetClassname += " ignore-color-mode";
    }
    return /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("fieldset", {
      className: fieldsetClassname
    }, wallpaperList.map(({
      title,
      theme,
      fluent_id
    }) => {
      return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("input", {
        onChange: this.handleChange,
        type: "radio",
        name: `wallpaper-${title}`,
        id: title,
        value: title,
        checked: title === activeWallpaper,
        "aria-checked": title === activeWallpaper,
        className: `wallpaper-input theme-${theme} ${title}`
      }), /*#__PURE__*/external_React_default().createElement("label", {
        htmlFor: title,
        className: "sr-only",
        "data-l10n-id": fluent_id
      }, fluent_id));
    })), /*#__PURE__*/external_React_default().createElement("button", {
      className: "wallpapers-reset",
      onClick: this.handleReset,
      "data-l10n-id": "newtab-wallpaper-reset"
    }));
  }
}
const WallpapersSection = (0,external_ReactRedux_namespaceObject.connect)(state => {
  return {
    Wallpapers: state.Wallpapers,
    Prefs: state.Prefs
  };
})(_WallpapersSection);
;// CONCATENATED MODULE: ./content-src/components/WallpapersSection/WallpaperCategories.jsx
function WallpaperCategories_extends() { WallpaperCategories_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return WallpaperCategories_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




// eslint-disable-next-line no-shadow

const PREF_WALLPAPER_UPLOADED_PREVIOUSLY = "newtabWallpapers.customWallpaper.uploadedPreviously";
const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE = "newtabWallpapers.customWallpaper.fileSize";
const PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE_ENABLED = "newtabWallpapers.customWallpaper.fileSize.enabled";

// Returns a function will not be continuously triggered when called. The
// function will be triggered if called again after `wait` milliseconds.
function debounce(func, wait) {
  let timer;
  return (...args) => {
    if (timer) {
      return;
    }
    let wakeUp = () => {
      timer = null;
    };
    timer = setTimeout(wakeUp, wait);
    func.apply(this, args);
  };
}
class _WallpaperCategories extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.handleColorInput = this.handleColorInput.bind(this);
    this.debouncedHandleChange = debounce(this.handleChange.bind(this), 999);
    this.handleChange = this.handleChange.bind(this);
    this.handleReset = this.handleReset.bind(this);
    this.handleCategory = this.handleCategory.bind(this);
    this.handleUpload = this.handleUpload.bind(this);
    this.handleBack = this.handleBack.bind(this);
    this.getRGBColors = this.getRGBColors.bind(this);
    this.prefersHighContrastQuery = null;
    this.prefersDarkQuery = null;
    this.categoryRef = []; // store references for wallpaper category list
    this.wallpaperRef = []; // store reference for wallpaper selection list
    this.customColorPickerRef = /*#__PURE__*/external_React_default().createRef(); // Used to determine contrast icon color for custom color picker
    this.customColorInput = /*#__PURE__*/external_React_default().createRef(); // Used to determine contrast icon color for custom color picker
    this.state = {
      activeCategory: null,
      activeCategoryFluentID: null,
      showColorPicker: false,
      inputType: "radio",
      activeId: null,
      isCustomWallpaperError: false
    };
  }
  componentDidMount() {
    this.prefersDarkQuery = globalThis.matchMedia("(prefers-color-scheme: dark)");
  }
  componentDidUpdate(prevProps) {
    // Walllpaper category subpanel should close when parent menu is closed
    if (this.props.exitEventFired && this.props.exitEventFired !== prevProps.exitEventFired) {
      this.handleBack();
    }
  }
  handleColorInput(event) {
    let {
      id
    } = event.target;
    // Set ID to include hex value of custom color
    id = `solid-color-picker-${event.target.value}`;
    const rgbColors = this.getRGBColors(event.target.value);

    // Set background color to custom color
    event.target.style.backgroundColor = `rgb(${rgbColors.toString()})`;
    if (this.customColorPickerRef.current) {
      const colorInputBackground = this.customColorPickerRef.current.children[0].style.backgroundColor;
      this.customColorPickerRef.current.style.backgroundColor = colorInputBackground;
    }

    // Set icon color based on the selected color
    const isColorDark = this.isWallpaperColorDark(rgbColors);
    if (this.customColorPickerRef.current) {
      if (isColorDark) {
        this.customColorPickerRef.current.classList.add("is-dark");
      } else {
        this.customColorPickerRef.current.classList.remove("is-dark");
      }

      // Remove any possible initial classes
      this.customColorPickerRef.current.classList.remove("custom-color-set", "custom-color-dark", "default-color-set");
    }

    // Setting this now so when we remove v1 we don't have to migrate v1 values.
    this.props.setPref("newtabWallpapers.wallpaper", id);
  }

  // Note: There's a separate event (debouncedHandleChange) that fires the handleChange
  // event but is delayed so that it doesn't fire multiple events when a user
  // is selecting a custom color background
  handleChange(event) {
    let {
      id
    } = event.target;

    // Set ID to include hex value of custom color
    if (id === "solid-color-picker") {
      id = `solid-color-picker-${event.target.value}`;
    }
    this.props.setPref("newtabWallpapers.wallpaper", id);
    const uploadedPreviously = this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];
    this.handleUserEvent(actionTypes.WALLPAPER_CLICK, {
      selected_wallpaper: id,
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously
    });
  }

  // function implementing arrow navigation for wallpaper category selection
  handleCategoryKeyDown(event, category) {
    const getIndex = this.categoryRef.findIndex(cat => cat.id === category);
    if (getIndex === -1) {
      return; // prevents errors if wallpaper index isn't found when navigating with arrow keys
    }
    const isRTL = document.dir === "rtl"; // returns true is page language is right-to-left
    let eventKey = event.key;
    if (eventKey === "ArrowRight" || eventKey === "ArrowLeft") {
      if (isRTL) {
        eventKey = eventKey === "ArrowRight" ? "ArrowLeft" : "ArrowRight";
      }
    }
    let nextIndex = getIndex;
    if (eventKey === "ArrowRight") {
      nextIndex = getIndex + 1 < this.categoryRef.length ? getIndex + 1 : getIndex;
    } else if (eventKey === "ArrowLeft") {
      nextIndex = getIndex - 1 >= 0 ? getIndex - 1 : getIndex;
    }
    this.categoryRef[nextIndex].focus();
  }

  // function implementing arrow navigation for wallpaper selection
  handleWallpaperKeyDown(event, title) {
    if (event.key === "Tab") {
      if (event.shiftKey) {
        event.preventDefault();
        this.backToMenuButton?.focus();
      } else {
        event.preventDefault(); // prevent tabbing within wallpaper selection. We should only be using the Tab key to tab between groups
      }
      return;
    }
    const isRTL = document.dir === "rtl"; // returns true if page language is right-to-left
    let eventKey = event.key;
    if (eventKey === "ArrowRight" || eventKey === "ArrowLeft") {
      if (isRTL) {
        eventKey = eventKey === "ArrowRight" ? "ArrowLeft" : "ArrowRight";
      }
    }
    const getIndex = this.wallpaperRef.findIndex(wallpaper => wallpaper.id === title);
    if (getIndex === -1) {
      return; // prevents errors if wallpaper index isn't found when navigating with arrow keys
    }

    // the set layout of columns per row for the wallpaper selection
    const columnCount = 3;
    let nextIndex = getIndex;
    if (eventKey === "ArrowRight") {
      nextIndex = getIndex + 1 < this.wallpaperRef.length ? getIndex + 1 : getIndex;
    } else if (eventKey === "ArrowLeft") {
      nextIndex = getIndex - 1 >= 0 ? getIndex - 1 : getIndex;
    } else if (eventKey === "ArrowDown") {
      nextIndex = getIndex + columnCount < this.wallpaperRef.length ? getIndex + columnCount : getIndex;
    } else if (eventKey === "ArrowUp") {
      nextIndex = getIndex - columnCount >= 0 ? getIndex - columnCount : getIndex;
    }
    this.wallpaperRef[nextIndex].tabIndex = 0;
    this.wallpaperRef[getIndex].tabIndex = -1;
    this.wallpaperRef[nextIndex].focus();
    this.wallpaperRef[nextIndex].click();
  }
  handleReset() {
    const uploadedPreviously = this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];
    const selectedWallpaper = this.props.Prefs.values["newtabWallpapers.wallpaper"];

    // If a custom wallpaper is set, remove it
    if (selectedWallpaper === "custom") {
      this.props.dispatch(actionCreators.OnlyToMain({
        type: actionTypes.WALLPAPER_REMOVE_UPLOAD
      }));
    }

    // Reset active wallpaper
    this.props.setPref("newtabWallpapers.wallpaper", "");

    // Fire WALLPAPER_CLICK telemetry event
    this.handleUserEvent(actionTypes.WALLPAPER_CLICK, {
      selected_wallpaper: "none",
      had_previous_wallpaper: !!this.props.activeWallpaper,
      had_uploaded_previously: !!uploadedPreviously
    });
  }
  handleCategory = event => {
    this.setState({
      activeCategory: event.target.id
    });
    this.handleUserEvent(actionTypes.WALLPAPER_CATEGORY_CLICK, event.target.id);
    let fluent_id;
    switch (event.target.id) {
      case "abstracts":
        fluent_id = "newtab-wallpaper-category-title-abstract";
        break;
      case "celestial":
        fluent_id = "newtab-wallpaper-category-title-celestial";
        break;
      case "photographs":
        fluent_id = "newtab-wallpaper-category-title-photographs";
        break;
      case "solid-colors":
        fluent_id = "newtab-wallpaper-category-title-colors";
    }
    this.setState({
      activeCategoryFluentID: fluent_id
    });
  };

  // Custom wallpaper image upload
  async handleUpload() {
    const wallpaperUploadMaxFileSizeEnabled = this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE_ENABLED];
    const wallpaperUploadMaxFileSize = this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE];
    const uploadedPreviously = this.props.Prefs.values[PREF_WALLPAPER_UPLOADED_PREVIOUSLY];

    // Create a file input since category buttons are radio inputs
    const fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = "image/*"; // only allow image files

    // Catch cancel events
    fileInput.oncancel = async () => {
      this.setState({
        isCustomWallpaperError: false
      });
    };

    // Reset error state when user begins file selection
    this.setState({
      isCustomWallpaperError: false
    });

    // Fire when user selects a file
    fileInput.onchange = async event => {
      const [file] = event.target.files;

      // Limit image uploaded to a maximum file size if enabled
      // Note: The max file size pref (customWallpaper.fileSize) is converted to megabytes (MB)
      // Example: if pref value is 5, max file size is 5 MB
      const maxSize = wallpaperUploadMaxFileSize * 1024 * 1024;
      if (wallpaperUploadMaxFileSizeEnabled && file && file.size > maxSize) {
        console.error("File size exceeds limit");
        this.setState({
          isCustomWallpaperError: true
        });
        return;
      }
      if (file) {
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.WALLPAPER_UPLOAD,
          data: file
        }));

        // Set active wallpaper ID to "custom"
        this.props.setPref("newtabWallpapers.wallpaper", "custom");

        // Update the uploadedPreviously pref to TRUE
        // Note: this pref used for telemetry. Do not reset to false.
        this.props.setPref(PREF_WALLPAPER_UPLOADED_PREVIOUSLY, true);
        this.handleUserEvent(actionTypes.WALLPAPER_CLICK, {
          selected_wallpaper: "custom",
          had_previous_wallpaper: !!this.props.activeWallpaper,
          had_uploaded_previously: !!uploadedPreviously
        });
      }
    };
    fileInput.click();
  }
  handleBack() {
    this.setState({
      activeCategory: null
    });
    this.categoryRef[0]?.focus();
  }

  // Record user interaction when changing wallpaper and reseting wallpaper to default
  handleUserEvent(type, data) {
    this.props.dispatch(actionCreators.OnlyToMain({
      type,
      data
    }));
  }
  setActiveId = id => {
    this.setState({
      activeId: id
    }); // Set the active ID
  };
  getRGBColors(input) {
    if (input.length !== 7) {
      return [];
    }
    const r = parseInt(input.substr(1, 2), 16);
    const g = parseInt(input.substr(3, 2), 16);
    const b = parseInt(input.substr(5, 2), 16);
    return [r, g, b];
  }
  isWallpaperColorDark([r, g, b]) {
    return 0.2125 * r + 0.7154 * g + 0.0721 * b <= 110;
  }
  render() {
    const prefs = this.props.Prefs.values;
    const {
      wallpaperList,
      categories
    } = this.props.Wallpapers;
    const {
      activeWallpaper
    } = this.props;
    const {
      activeCategory,
      showColorPicker
    } = this.state;
    const {
      activeCategoryFluentID
    } = this.state;
    let filteredWallpapers = wallpaperList.filter(wallpaper => wallpaper.category === activeCategory);
    const wallpaperUploadMaxFileSize = this.props.Prefs.values[PREF_WALLPAPER_UPLOAD_MAX_FILE_SIZE];
    function reduceColorsToFitCustomColorInput(arr) {
      // Reduce the amount of custom colors to make space for the custom color picker
      while (arr.length % 3 !== 2) {
        arr.pop();
      }
      return arr;
    }
    let categorySectionClassname = "category wallpaper-list";
    if (prefs["newtabWallpapers.v2.enabled"]) {
      categorySectionClassname += " ignore-color-mode";
    }
    let wallpaperCustomSolidColorHex = null;
    const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];

    // User has previous selected a custom color
    if (selectedWallpaper.includes("solid-color-picker")) {
      this.setState({
        showColorPicker: true
      });
      const regex = /#([a-fA-F0-9]{6})/;
      [wallpaperCustomSolidColorHex] = selectedWallpaper.match(regex);
    }

    // Enable custom color select if pref'ed on
    this.setState({
      showColorPicker: prefs["newtabWallpapers.customColor.enabled"]
    });

    // Remove last item of solid colors to make space for custom color picker
    if (prefs["newtabWallpapers.customColor.enabled"] && activeCategory === "solid-colors") {
      filteredWallpapers = reduceColorsToFitCustomColorInput(filteredWallpapers);
    }

    // Bug 1953012 - If nothing selected, default to color of customize panel
    // --color-blue-70 : #054096
    // --color-blue-05 : #deeafc
    const starterColorHex = this.prefersDarkQuery?.matches ? "#054096" : "#deeafc";

    // Set initial state of the color picker (depending if the user has already set a custom color)
    let initStateClassname = wallpaperCustomSolidColorHex ? "custom-color-set" : "default-color-set";

    // If a custom color picker is set, make sure the icon has the correct contrast
    if (wallpaperCustomSolidColorHex) {
      const rgbColors = this.getRGBColors(wallpaperCustomSolidColorHex);
      const isColorDark = this.isWallpaperColorDark(rgbColors);
      if (isColorDark) {
        initStateClassname += " custom-color-dark";
      }
    }
    let colorPickerInput = showColorPicker && activeCategory === "solid-colors" ? /*#__PURE__*/external_React_default().createElement("div", {
      className: `theme-custom-color-picker ${initStateClassname}`,
      ref: this.customColorPickerRef
    }, /*#__PURE__*/external_React_default().createElement("input", {
      onInput: this.handleColorInput,
      onChange: this.debouncedHandleChange,
      onClick: () => this.setActiveId("solid-color-picker") //
      ,
      type: "color",
      name: `wallpaper-solid-color-picker`,
      id: "solid-color-picker"
      // aria-checked is not applicable for input[type="color"] elements
      ,
      "aria-current": this.state.activeId === "solid-color-picker",
      value: wallpaperCustomSolidColorHex || starterColorHex,
      className: `wallpaper-input
              ${this.state.activeId === "solid-color-picker" ? "active" : ""}`,
      ref: this.customColorInput
    }), /*#__PURE__*/external_React_default().createElement("label", {
      htmlFor: "solid-color-picker",
      "data-l10n-id": "newtab-wallpaper-custom-color"
    })) : "";
    return /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("div", {
      className: "category-header"
    }, /*#__PURE__*/external_React_default().createElement("h2", {
      "data-l10n-id": "newtab-wallpaper-title"
    }), /*#__PURE__*/external_React_default().createElement("button", {
      className: "wallpapers-reset",
      onClick: this.handleReset,
      "data-l10n-id": "newtab-wallpaper-reset"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      role: "grid",
      "aria-label": "Wallpaper category selection. Use arrow keys to navigate."
    }, /*#__PURE__*/external_React_default().createElement("fieldset", {
      className: "category-list"
    }, categories.map((category, index) => {
      const filteredList = wallpaperList.filter(wallpaper => wallpaper.category === category);
      const activeWallpaperObj = activeWallpaper && filteredList.find(wp => wp.title === activeWallpaper);
      const thumbnail = activeWallpaperObj || filteredList[0];
      let fluent_id;
      switch (category) {
        case "abstracts":
          fluent_id = "newtab-wallpaper-category-title-abstract";
          break;
        case "celestial":
          fluent_id = "newtab-wallpaper-category-title-celestial";
          break;
        case "custom-wallpaper":
          fluent_id = "newtab-wallpaper-upload-image";
          break;
        case "photographs":
          fluent_id = "newtab-wallpaper-category-title-photographs";
          break;
        case "solid-colors":
          fluent_id = "newtab-wallpaper-category-title-colors";
      }
      let style = {};
      if (thumbnail?.wallpaperUrl) {
        style.backgroundImage = `url(${thumbnail.wallpaperUrl})`;
      } else {
        style.backgroundColor = thumbnail?.solid_color || "";
      }
      return /*#__PURE__*/external_React_default().createElement("div", {
        key: category
      }, /*#__PURE__*/external_React_default().createElement("button", WallpaperCategories_extends({
        ref: el => {
          if (el) {
            this.categoryRef[index] = el;
          }
        },
        id: category,
        style: style,
        onKeyDown: e => this.handleCategoryKeyDown(e, category)
        // Add overrides for custom wallpaper upload UI
        ,
        onClick: category !== "custom-wallpaper" ? this.handleCategory : this.handleUpload,
        className: category !== "custom-wallpaper" ? `wallpaper-input` : `wallpaper-input theme-custom-wallpaper`,
        tabIndex: index === 0 ? 0 : -1
      }, category === "custom-wallpaper" ? {
        "aria-errormessage": "customWallpaperError"
      } : {})), /*#__PURE__*/external_React_default().createElement("label", {
        htmlFor: category,
        "data-l10n-id": fluent_id
      }, fluent_id));
    })), this.state.isCustomWallpaperError && /*#__PURE__*/external_React_default().createElement("div", {
      className: "custom-wallpaper-error",
      id: "customWallpaperError"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-info"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      "data-l10n-id": "newtab-wallpaper-error-max-file-size",
      "data-l10n-args": `{"file_size": ${wallpaperUploadMaxFileSize}}`
    }))), /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.CSSTransition, {
      in: !!activeCategory,
      timeout: 300,
      classNames: "wallpaper-list",
      unmountOnExit: true
    }, /*#__PURE__*/external_React_default().createElement("section", {
      className: categorySectionClassname
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "arrow-button",
      "data-l10n-id": activeCategoryFluentID,
      onClick: this.handleBack,
      ref: el => {
        this.backToMenuButton = el;
      }
    }), /*#__PURE__*/external_React_default().createElement("div", {
      role: "grid",
      "aria-label": "Wallpaper selection. Use arrow keys to navigate."
    }, /*#__PURE__*/external_React_default().createElement("fieldset", null, filteredWallpapers.map(({
      title,
      theme,
      fluent_id,
      solid_color,
      wallpaperUrl
    }, index) => {
      let style = {};
      if (wallpaperUrl) {
        style.backgroundImage = `url(${wallpaperUrl})`;
      } else {
        style.backgroundColor = solid_color || "";
      }
      return /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("input", {
        ref: el => {
          if (el) {
            this.wallpaperRef[index] = el;
          }
        },
        onChange: this.handleChange,
        onKeyDown: e => this.handleWallpaperKeyDown(e, title),
        style: style,
        type: "radio",
        name: `wallpaper-${title}`,
        id: title,
        value: title,
        checked: title === activeWallpaper,
        "aria-checked": title === activeWallpaper,
        className: `wallpaper-input theme-${theme} ${this.state.activeId === title ? "active" : ""}`,
        onClick: () => this.setActiveId(title) //
        ,
        tabIndex: index === 0 ? 0 : -1 //the first wallpaper in the array will have a tabindex of 0 so we can tab into it. The rest will have a tabindex of -1
      }), /*#__PURE__*/external_React_default().createElement("label", {
        htmlFor: title,
        className: "sr-only",
        "data-l10n-id": fluent_id
      }, fluent_id));
    }), colorPickerInput)))));
  }
}
const WallpaperCategories = (0,external_ReactRedux_namespaceObject.connect)(state => {
  return {
    Wallpapers: state.Wallpapers,
    Prefs: state.Prefs
  };
})(_WallpaperCategories);
;// CONCATENATED MODULE: ./content-src/components/CustomizeMenu/ContentSection/ContentSection.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






class ContentSection extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onPreferenceSelect = this.onPreferenceSelect.bind(this);

    // Refs are necessary for dynamically measuring drawer heights for slide animations
    this.topSitesDrawerRef = /*#__PURE__*/external_React_default().createRef();
    this.pocketDrawerRef = /*#__PURE__*/external_React_default().createRef();
  }
  inputUserEvent(eventSource, eventValue) {
    this.props.dispatch(actionCreators.UserEvent({
      event: "PREF_CHANGED",
      source: eventSource,
      value: {
        status: eventValue,
        menu_source: "CUSTOMIZE_MENU"
      }
    }));
  }
  onPreferenceSelect(e) {
    // eventSource: WEATHER | TOP_SITES | TOP_STORIES
    const {
      preference,
      eventSource
    } = e.target.dataset;
    let value;
    if (e.target.nodeName === "SELECT") {
      value = parseInt(e.target.value, 10);
    } else if (e.target.nodeName === "INPUT") {
      value = e.target.checked;
      if (eventSource) {
        this.inputUserEvent(eventSource, value);
      }
    } else if (e.target.nodeName === "MOZ-TOGGLE") {
      value = e.target.pressed;
      if (eventSource) {
        this.inputUserEvent(eventSource, value);
      }
    }
    this.props.setPref(preference, value);
  }
  componentDidMount() {
    this.setDrawerMargins();
  }
  componentDidUpdate() {
    this.setDrawerMargins();
  }
  setDrawerMargins() {
    this.setDrawerMargin(`TOP_SITES`, this.props.enabledSections.topSitesEnabled);
    this.setDrawerMargin(`TOP_STORIES`, this.props.enabledSections.pocketEnabled);
  }
  setDrawerMargin(drawerID, isOpen) {
    let drawerRef;
    if (drawerID === `TOP_SITES`) {
      drawerRef = this.topSitesDrawerRef.current;
    } else if (drawerID === `TOP_STORIES`) {
      drawerRef = this.pocketDrawerRef.current;
    } else {
      return;
    }
    if (drawerRef) {
      let drawerHeight = parseFloat(window.getComputedStyle(drawerRef)?.height) || 0;
      if (isOpen) {
        drawerRef.style.marginTop = "var(--space-large)";
      } else {
        drawerRef.style.marginTop = `-${drawerHeight + 3}px`;
      }
    }
  }
  render() {
    const {
      enabledSections,
      pocketRegion,
      mayHaveInferredPersonalization,
      mayHaveRecentSaves,
      mayHaveWeather,
      openPreferences,
      wallpapersEnabled,
      wallpapersV2Enabled,
      activeWallpaper,
      setPref,
      mayHaveTopicSections,
      exitEventFired
    } = this.props;
    const {
      topSitesEnabled,
      pocketEnabled,
      weatherEnabled,
      showInferredPersonalizationEnabled,
      showRecentSavesEnabled,
      topSitesRowsCount
    } = enabledSections;
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: "home-section"
    }, !wallpapersV2Enabled && wallpapersEnabled && /*#__PURE__*/external_React_default().createElement("div", {
      className: "wallpapers-section"
    }, /*#__PURE__*/external_React_default().createElement(WallpapersSection, {
      setPref: setPref,
      activeWallpaper: activeWallpaper
    })), wallpapersV2Enabled && /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement("div", {
      className: "wallpapers-section"
    }, /*#__PURE__*/external_React_default().createElement(WallpaperCategories, {
      setPref: setPref,
      activeWallpaper: activeWallpaper,
      exitEventFired: exitEventFired
    })), /*#__PURE__*/external_React_default().createElement("span", {
      className: "divider",
      role: "separator"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      className: "settings-toggles"
    }, mayHaveWeather && /*#__PURE__*/external_React_default().createElement("div", {
      id: "weather-section",
      className: "section"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "weather-toggle",
      pressed: weatherEnabled || null,
      onToggle: this.onPreferenceSelect,
      "data-preference": "showWeather",
      "data-eventSource": "WEATHER",
      "data-l10n-id": "newtab-custom-weather-toggle"
    })), /*#__PURE__*/external_React_default().createElement("div", {
      id: "shortcuts-section",
      className: "section"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "shortcuts-toggle",
      pressed: topSitesEnabled || null,
      onToggle: this.onPreferenceSelect,
      "data-preference": "feeds.topsites",
      "data-eventSource": "TOP_SITES",
      "data-l10n-id": "newtab-custom-shortcuts-toggle"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      slot: "nested"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "more-info-top-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "more-information",
      ref: this.topSitesDrawerRef
    }, /*#__PURE__*/external_React_default().createElement("select", {
      id: "row-selector",
      className: "selector",
      name: "row-count",
      "data-preference": "topSitesRows",
      value: topSitesRowsCount,
      onChange: this.onPreferenceSelect,
      disabled: !topSitesEnabled,
      "aria-labelledby": "custom-shortcuts-title"
    }, /*#__PURE__*/external_React_default().createElement("option", {
      value: "1",
      "data-l10n-id": "newtab-custom-row-selector",
      "data-l10n-args": "{\"num\": 1}"
    }), /*#__PURE__*/external_React_default().createElement("option", {
      value: "2",
      "data-l10n-id": "newtab-custom-row-selector",
      "data-l10n-args": "{\"num\": 2}"
    }), /*#__PURE__*/external_React_default().createElement("option", {
      value: "3",
      "data-l10n-id": "newtab-custom-row-selector",
      "data-l10n-args": "{\"num\": 3}"
    }), /*#__PURE__*/external_React_default().createElement("option", {
      value: "4",
      "data-l10n-id": "newtab-custom-row-selector",
      "data-l10n-args": "{\"num\": 4}"
    }))))))), pocketRegion && /*#__PURE__*/external_React_default().createElement("div", {
      id: "pocket-section",
      className: "section"
    }, /*#__PURE__*/external_React_default().createElement("moz-toggle", {
      id: "pocket-toggle",
      pressed: pocketEnabled || null,
      onToggle: this.onPreferenceSelect,
      "aria-describedby": "custom-pocket-subtitle",
      "data-preference": "feeds.section.topstories",
      "data-eventSource": "TOP_STORIES",
      "data-l10n-id": "newtab-custom-stories-toggle"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      slot: "nested"
    }, (mayHaveRecentSaves || mayHaveInferredPersonalization || mayHaveTopicSections) && /*#__PURE__*/external_React_default().createElement("div", {
      className: "more-info-pocket-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "more-information",
      ref: this.pocketDrawerRef
    }, mayHaveInferredPersonalization && /*#__PURE__*/external_React_default().createElement("div", {
      className: "check-wrapper",
      role: "presentation"
    }, /*#__PURE__*/external_React_default().createElement("input", {
      id: "inferred-personalization",
      className: "customize-menu-checkbox",
      disabled: !pocketEnabled,
      checked: showInferredPersonalizationEnabled,
      type: "checkbox",
      onChange: this.onPreferenceSelect,
      "data-preference": "discoverystream.sections.personalization.inferred.user.enabled",
      "data-eventSource": "INFERRED_PERSONALIZATION"
    }), /*#__PURE__*/external_React_default().createElement("label", {
      className: "customize-menu-checkbox-label",
      htmlFor: "inferred-personalization"
    }, "Recommendations inferred from your activity with the feed")), mayHaveTopicSections && /*#__PURE__*/external_React_default().createElement(SectionsMgmtPanel, {
      exitEventFired: exitEventFired
    }), mayHaveRecentSaves && /*#__PURE__*/external_React_default().createElement("div", {
      className: "check-wrapper",
      role: "presentation"
    }, /*#__PURE__*/external_React_default().createElement("input", {
      id: "recent-saves-pocket",
      className: "customize-menu-checkbox",
      disabled: !pocketEnabled,
      checked: showRecentSavesEnabled,
      type: "checkbox",
      onChange: this.onPreferenceSelect,
      "data-preference": "showRecentSaves",
      "data-eventSource": "POCKET_RECENT_SAVES"
    }), /*#__PURE__*/external_React_default().createElement("label", {
      className: "customize-menu-checkbox-label",
      htmlFor: "recent-saves-pocket",
      "data-l10n-id": "newtab-custom-pocket-show-recent-saves"
    })))))))), /*#__PURE__*/external_React_default().createElement("span", {
      className: "divider",
      role: "separator"
    }), /*#__PURE__*/external_React_default().createElement("div", null, /*#__PURE__*/external_React_default().createElement("button", {
      id: "settings-link",
      className: "external-link",
      onClick: openPreferences,
      "data-l10n-id": "newtab-custom-settings"
    })));
  }
}
;// CONCATENATED MODULE: ./content-src/components/CustomizeMenu/CustomizeMenu.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




// eslint-disable-next-line no-shadow

class _CustomizeMenu extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onEntered = this.onEntered.bind(this);
    this.onExited = this.onExited.bind(this);
    this.state = {
      exitEventFired: false
    };
  }
  onEntered() {
    this.setState({
      exitEventFired: false
    });
    if (this.closeButton) {
      this.closeButton.focus();
    }
  }
  onExited() {
    this.setState({
      exitEventFired: true
    });
    if (this.openButton) {
      this.openButton.focus();
    }
  }
  render() {
    return /*#__PURE__*/external_React_default().createElement("span", null, /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.CSSTransition, {
      timeout: 300,
      classNames: "personalize-animate",
      in: !this.props.showing,
      appear: true
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "icon icon-settings personalize-button",
      onClick: () => this.props.onOpen(),
      onKeyDown: e => {
        if (e.key === "Enter") {
          this.props.onOpen();
        }
      },
      "data-l10n-id": "newtab-personalize-settings-icon-label",
      ref: c => this.openButton = c
    })), /*#__PURE__*/external_React_default().createElement(external_ReactTransitionGroup_namespaceObject.CSSTransition, {
      timeout: 250,
      classNames: "customize-animate",
      in: this.props.showing,
      onEntered: this.onEntered,
      onExited: this.onExited,
      appear: true
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "customize-menu",
      role: "dialog",
      "data-l10n-id": "newtab-settings-dialog-label"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "close-button-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      onClick: () => this.props.onClose(),
      className: "close-button",
      "data-l10n-id": "newtab-custom-close-button",
      ref: c => this.closeButton = c
    })), /*#__PURE__*/external_React_default().createElement(ContentSection, {
      openPreferences: this.props.openPreferences,
      setPref: this.props.setPref,
      enabledSections: this.props.enabledSections,
      wallpapersEnabled: this.props.wallpapersEnabled,
      wallpapersV2Enabled: this.props.wallpapersV2Enabled,
      activeWallpaper: this.props.activeWallpaper,
      pocketRegion: this.props.pocketRegion,
      mayHaveTopicSections: this.props.mayHaveTopicSections,
      mayHaveInferredPersonalization: this.props.mayHaveInferredPersonalization,
      mayHaveRecentSaves: this.props.DiscoveryStream.recentSavesEnabled,
      mayHaveWeather: this.props.mayHaveWeather,
      dispatch: this.props.dispatch,
      exitEventFired: this.state.exitEventFired
    }))));
  }
}
const CustomizeMenu = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  DiscoveryStream: state.DiscoveryStream
}))(_CustomizeMenu);
;// CONCATENATED MODULE: ./content-src/lib/constants.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const IS_NEWTAB =
  globalThis.document && globalThis.document.documentURI === "about:newtab";
const NEWTAB_DARK_THEME = {
  ntp_background: {
    r: 42,
    g: 42,
    b: 46,
    a: 1,
  },
  ntp_card_background: {
    r: 66,
    g: 65,
    b: 77,
    a: 1,
  },
  ntp_text: {
    r: 249,
    g: 249,
    b: 250,
    a: 1,
  },
  sidebar: {
    r: 56,
    g: 56,
    b: 61,
    a: 1,
  },
  sidebar_text: {
    r: 249,
    g: 249,
    b: 250,
    a: 1,
  },
};

;// CONCATENATED MODULE: ./content-src/components/Logo/Logo.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function Logo() {
  return /*#__PURE__*/external_React_default().createElement("h1", {
    className: "logo-and-wordmark-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "logo-and-wordmark",
    role: "img",
    "data-l10n-id": "newtab-logo-and-wordmark"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "logo"
  }), /*#__PURE__*/external_React_default().createElement("div", {
    className: "wordmark"
  })));
}

;// CONCATENATED MODULE: ./content-src/components/Search/Search.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals ContentSearchUIController, ContentSearchHandoffUIController */






class _Search extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.onSearchClick = this.onSearchClick.bind(this);
    this.onSearchHandoffClick = this.onSearchHandoffClick.bind(this);
    this.onSearchHandoffPaste = this.onSearchHandoffPaste.bind(this);
    this.onSearchHandoffDrop = this.onSearchHandoffDrop.bind(this);
    this.onInputMount = this.onInputMount.bind(this);
    this.onInputMountHandoff = this.onInputMountHandoff.bind(this);
    this.onSearchHandoffButtonMount = this.onSearchHandoffButtonMount.bind(this);
  }
  handleEvent(event) {
    // Also track search events with our own telemetry
    if (event.detail.type === "Search") {
      this.props.dispatch(actionCreators.UserEvent({
        event: "SEARCH"
      }));
    }
  }
  onSearchClick(event) {
    window.gContentSearchController.search(event);
  }
  doSearchHandoff(text) {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.HANDOFF_SEARCH_TO_AWESOMEBAR,
      data: {
        text
      }
    }));
    this.props.dispatch({
      type: actionTypes.FAKE_FOCUS_SEARCH
    });
    this.props.dispatch(actionCreators.UserEvent({
      event: "SEARCH_HANDOFF"
    }));
    if (text) {
      this.props.dispatch({
        type: actionTypes.DISABLE_SEARCH
      });
    }
  }
  onSearchHandoffClick(event) {
    // When search hand-off is enabled, we render a big button that is styled to
    // look like a search textbox. If the button is clicked, we style
    // the button as if it was a focused search box and show a fake cursor but
    // really focus the awesomebar without the focus styles ("hidden focus").
    event.preventDefault();
    this.doSearchHandoff();
  }
  onSearchHandoffPaste(event) {
    event.preventDefault();
    this.doSearchHandoff(event.clipboardData.getData("Text"));
  }
  onSearchHandoffDrop(event) {
    event.preventDefault();
    let text = event.dataTransfer.getData("text");
    if (text) {
      this.doSearchHandoff(text);
    }
  }
  componentDidMount() {
    const caret = this.fakeCaret;
    const {
      caretBlinkCount,
      caretBlinkTime
    } = this.props.Prefs.values;
    if (caret) {
      // If caret blink count isn't defined, use the default infinite behavior for animation
      caret.style.setProperty("--caret-blink-count", caretBlinkCount > -1 ? caretBlinkCount : "infinite");

      // Apply custom blink rate if set, else fallback to default (567ms on/off --> 1134ms total)
      caret.style.setProperty("--caret-blink-time", caretBlinkTime > 0 ? `${caretBlinkTime * 2}ms` : `${1134}ms`);
    }
  }
  componentWillUnmount() {
    delete window.gContentSearchController;
  }
  onInputMount(input) {
    if (input) {
      // The "healthReportKey" and needs to be "newtab" or "abouthome" so that
      // BrowserUsageTelemetry.sys.mjs knows to handle events with this name, and
      // can add the appropriate telemetry probes for search. Without the correct
      // name, certain tests like browser_UsageTelemetry_content.js will fail
      // (See github ticket #2348 for more details)
      const healthReportKey = IS_NEWTAB ? "newtab" : "abouthome";

      // The "searchSource" needs to be "newtab" or "homepage" and is sent with
      // the search data and acts as context for the search request (See
      // nsISearchEngine.getSubmission). It is necessary so that search engine
      // plugins can correctly atribute referrals. (See github ticket #3321 for
      // more details)
      const searchSource = IS_NEWTAB ? "newtab" : "homepage";

      // gContentSearchController needs to exist as a global so that tests for
      // the existing about:home can find it; and so it allows these tests to pass.
      // In the future, when activity stream is default about:home, this can be renamed
      window.gContentSearchController = new ContentSearchUIController(input, input.parentNode, healthReportKey, searchSource);
      addEventListener("ContentSearchClient", this);
    } else {
      window.gContentSearchController = null;
      removeEventListener("ContentSearchClient", this);
    }
  }
  onInputMountHandoff(input) {
    if (input) {
      // The handoff UI controller helps us set the search icon and reacts to
      // changes to default engine to keep everything in sync.
      this._handoffSearchController = new ContentSearchHandoffUIController();
    }
  }
  onSearchHandoffButtonMount(button) {
    // Keep a reference to the button for use during "paste" event handling.
    this._searchHandoffButton = button;
  }

  /*
   * Do not change the ID on the input field, as legacy newtab code
   * specifically looks for the id 'newtab-search-text' on input fields
   * in order to execute searches in various tests
   */
  render() {
    const wrapperClassName = ["search-wrapper", this.props.disable && "search-disabled", this.props.fakeFocus && "fake-focus"].filter(v => v).join(" ");
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: wrapperClassName
    }, this.props.showLogo && /*#__PURE__*/external_React_default().createElement(Logo, null), !this.props.handoffEnabled && /*#__PURE__*/external_React_default().createElement("div", {
      className: "search-inner-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("input", {
      id: "newtab-search-text",
      "data-l10n-id": "newtab-search-box-input",
      maxLength: "256",
      ref: this.onInputMount,
      type: "search"
    }), /*#__PURE__*/external_React_default().createElement("button", {
      id: "searchSubmit",
      className: "search-button",
      "data-l10n-id": "newtab-search-box-search-button",
      onClick: this.onSearchClick
    })), this.props.handoffEnabled && /*#__PURE__*/external_React_default().createElement("div", {
      className: "search-inner-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      className: "search-handoff-button",
      ref: this.onSearchHandoffButtonMount,
      onClick: this.onSearchHandoffClick,
      tabIndex: "-1"
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "fake-textbox"
    }), /*#__PURE__*/external_React_default().createElement("input", {
      type: "search",
      className: "fake-editable",
      tabIndex: "-1",
      "aria-hidden": "true",
      onDrop: this.onSearchHandoffDrop,
      onPaste: this.onSearchHandoffPaste,
      ref: this.onInputMountHandoff
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: "fake-caret",
      ref: el => {
        this.fakeCaret = el;
      }
    }))));
  }
}
const Search_Search = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Prefs: state.Prefs
}))(_Search);
;// CONCATENATED MODULE: ./content-src/components/Weather/LocationSearch.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




function LocationSearch({
  outerClassName
}) {
  // should be the location object from suggestedLocations
  const [selectedLocation, setSelectedLocation] = (0,external_React_namespaceObject.useState)("");
  const suggestedLocations = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Weather.suggestedLocations);
  const locationSearchString = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Weather.locationSearchString);
  const [userInput, setUserInput] = (0,external_React_namespaceObject.useState)(locationSearchString || "");
  const inputRef = (0,external_React_namespaceObject.useRef)(null);
  const dispatch = (0,external_ReactRedux_namespaceObject.useDispatch)();
  (0,external_React_namespaceObject.useEffect)(() => {
    if (selectedLocation) {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.WEATHER_LOCATION_DATA_UPDATE,
        data: {
          city: selectedLocation.localized_name,
          adminName: selectedLocation.administrative_area,
          country: selectedLocation.country
        }
      }));
      dispatch(actionCreators.SetPref("weather.query", selectedLocation.key));
      dispatch(actionCreators.BroadcastToContent({
        type: actionTypes.WEATHER_SEARCH_ACTIVE,
        data: false
      }));
    }
  }, [selectedLocation, dispatch]);

  // when component mounts, set focus to input
  (0,external_React_namespaceObject.useEffect)(() => {
    inputRef?.current?.focus();
  }, [inputRef]);
  function handleChange(event) {
    const {
      value
    } = event.target;
    setUserInput(value);
    // if the user input contains less than three characters and suggestedLocations is not an empty array,
    // reset suggestedLocations to [] so there arent incorrect items in the datalist
    if (value.length < 3 && suggestedLocations.length) {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.WEATHER_LOCATION_SUGGESTIONS_UPDATE,
        data: []
      }));
    }
    // find match in suggestedLocation array
    const match = suggestedLocations?.find(({
      key
    }) => key === value);
    if (match) {
      setSelectedLocation(match);
      setUserInput(`${match.localized_name}, ${match.administrative_area.localized_name}`);
    } else if (value.length >= 3 && !match) {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.WEATHER_LOCATION_SEARCH_UPDATE,
        data: value
      }));
    }
  }
  function handleCloseSearch() {
    dispatch(actionCreators.BroadcastToContent({
      type: actionTypes.WEATHER_SEARCH_ACTIVE,
      data: false
    }));
    setUserInput("");
  }
  function handleKeyDown(e) {
    if (e.key === "Escape") {
      handleCloseSearch();
    }
  }
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: `${outerClassName} location-search`
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "location-input-wrapper"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "search-icon"
  }), /*#__PURE__*/external_React_default().createElement("input", {
    ref: inputRef,
    list: "merino-location-list",
    type: "text",
    "data-l10n-id": "newtab-weather-change-location-search-input-placeholder",
    onChange: handleChange,
    value: userInput,
    onKeyDown: handleKeyDown
  }), /*#__PURE__*/external_React_default().createElement("moz-button", {
    class: "close-icon",
    type: "icon ghost",
    size: "small",
    iconSrc: "chrome://global/skin/icons/close.svg",
    onClick: handleCloseSearch
  }), /*#__PURE__*/external_React_default().createElement("datalist", {
    id: "merino-location-list"
  }, (suggestedLocations || []).map(merinoLcation => /*#__PURE__*/external_React_default().createElement("option", {
    value: merinoLcation.key,
    key: merinoLcation.key
  }, merinoLcation.localized_name, ",", " ", merinoLcation.administrative_area.localized_name)))));
}

;// CONCATENATED MODULE: ./content-src/components/Weather/Weather.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */







const Weather_VISIBLE = "visible";
const Weather_VISIBILITY_CHANGE_EVENT = "visibilitychange";
function WeatherPlaceholder() {
  const [isSeen, setIsSeen] = (0,external_React_namespaceObject.useState)(false);

  // We are setting up a visibility and intersection event
  // so animations don't happen with headless automation.
  // The animations causes tests to fail beause they never stop,
  // and many tests wait until everything has stopped before passing.
  const ref = useIntersectionObserver(() => setIsSeen(true), 1);
  const isSeenClassName = isSeen ? `placeholder-seen` : ``;
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: `weather weather-placeholder ${isSeenClassName}`,
    ref: el => {
      ref.current = [el];
    }
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "placeholder-image placeholder-fill"
  }), /*#__PURE__*/external_React_default().createElement("div", {
    className: "placeholder-context"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "placeholder-header placeholder-fill"
  }), /*#__PURE__*/external_React_default().createElement("div", {
    className: "placeholder-description placeholder-fill"
  })));
}
class _Weather extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      contextMenuKeyboard: false,
      showContextMenu: false,
      url: "https://example.com",
      impressionSeen: false,
      errorSeen: false
    };
    this.setImpressionRef = element => {
      this.impressionElement = element;
    };
    this.setErrorRef = element => {
      this.errorElement = element;
    };
    this.onClick = this.onClick.bind(this);
    this.onKeyDown = this.onKeyDown.bind(this);
    this.onUpdate = this.onUpdate.bind(this);
    this.onProviderClick = this.onProviderClick.bind(this);
  }
  componentDidMount() {
    const {
      props
    } = this;
    if (!props.dispatch) {
      return;
    }
    if (props.document.visibilityState === Weather_VISIBLE) {
      // Setup the impression observer once the page is visible.
      this.setImpressionObservers();
    } else {
      // We should only ever send the latest impression stats ping, so remove any
      // older listeners.
      if (this._onVisibilityChange) {
        props.document.removeEventListener(Weather_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
      }
      this._onVisibilityChange = () => {
        if (props.document.visibilityState === Weather_VISIBLE) {
          // Setup the impression observer once the page is visible.
          this.setImpressionObservers();
          props.document.removeEventListener(Weather_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
        }
      };
      props.document.addEventListener(Weather_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  componentWillUnmount() {
    // Remove observers on unmount
    if (this.observer && this.impressionElement) {
      this.observer.unobserve(this.impressionElement);
    }
    if (this.observer && this.errorElement) {
      this.observer.unobserve(this.errorElement);
    }
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(Weather_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  setImpressionObservers() {
    if (this.impressionElement) {
      this.observer = new IntersectionObserver(this.onImpression.bind(this));
      this.observer.observe(this.impressionElement);
    }
    if (this.errorElement) {
      this.observer = new IntersectionObserver(this.onError.bind(this));
      this.observer.observe(this.errorElement);
    }
  }
  onImpression(entries) {
    if (this.state) {
      const entry = entries.find(e => e.isIntersecting);
      if (entry) {
        if (this.impressionElement) {
          this.observer.unobserve(this.impressionElement);
        }
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.WEATHER_IMPRESSION
        }));

        // Stop observing since element has been seen
        this.setState({
          impressionSeen: true
        });
      }
    }
  }
  onError(entries) {
    if (this.state) {
      const entry = entries.find(e => e.isIntersecting);
      if (entry) {
        if (this.errorElement) {
          this.observer.unobserve(this.errorElement);
        }
        this.props.dispatch(actionCreators.OnlyToMain({
          type: actionTypes.WEATHER_LOAD_ERROR
        }));

        // Stop observing since element has been seen
        this.setState({
          errorSeen: true
        });
      }
    }
  }
  openContextMenu(isKeyBoard) {
    if (this.props.onUpdate) {
      this.props.onUpdate(true);
    }
    this.setState({
      showContextMenu: true,
      contextMenuKeyboard: isKeyBoard
    });
  }
  onClick(event) {
    event.preventDefault();
    this.openContextMenu(false, event);
  }
  onKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.openContextMenu(true, event);
    }
  }
  onUpdate(showContextMenu) {
    if (this.props.onUpdate) {
      this.props.onUpdate(showContextMenu);
    }
    this.setState({
      showContextMenu
    });
  }
  onProviderClick() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.WEATHER_OPEN_PROVIDER_URL,
      data: {
        source: "WEATHER"
      }
    }));
  }
  render() {
    // Check if weather should be rendered
    const isWeatherEnabled = this.props.Prefs.values["system.showWeather"];
    if (!isWeatherEnabled) {
      return false;
    }
    if (!this.props.Weather.initialized) {
      return /*#__PURE__*/external_React_default().createElement(WeatherPlaceholder, null);
    }
    const {
      showContextMenu
    } = this.state;
    const {
      props
    } = this;
    const {
      dispatch,
      Prefs,
      Weather
    } = props;
    const WEATHER_SUGGESTION = Weather.suggestions?.[0];
    const outerClassName = ["weather", Weather.searchActive && "search"].filter(v => v).join(" ");
    const showDetailedView = Prefs.values["weather.display"] === "detailed";

    // Note: The temperature units/display options will become secondary menu items
    const WEATHER_SOURCE_CONTEXT_MENU_OPTIONS = [...(Prefs.values["weather.locationSearchEnabled"] ? ["ChangeWeatherLocation"] : []), ...(Prefs.values["weather.temperatureUnits"] === "f" ? ["ChangeTempUnitCelsius"] : ["ChangeTempUnitFahrenheit"]), ...(Prefs.values["weather.display"] === "simple" ? ["ChangeWeatherDisplayDetailed"] : ["ChangeWeatherDisplaySimple"]), "HideWeather", "OpenLearnMoreURL"];
    const WEATHER_SOURCE_ERROR_CONTEXT_MENU_OPTIONS = [...(Prefs.values["weather.locationSearchEnabled"] ? ["ChangeWeatherLocation"] : []), "HideWeather", "OpenLearnMoreURL"];
    const contextMenu = contextOpts => /*#__PURE__*/external_React_default().createElement("div", {
      className: "weatherButtonContextMenuWrapper"
    }, /*#__PURE__*/external_React_default().createElement("button", {
      "aria-haspopup": "true",
      onKeyDown: this.onKeyDown,
      onClick: this.onClick,
      "data-l10n-id": "newtab-menu-section-tooltip",
      className: "weatherButtonContextMenu"
    }, showContextMenu ? /*#__PURE__*/external_React_default().createElement(LinkMenu, {
      dispatch: dispatch,
      index: 0,
      source: "WEATHER",
      onUpdate: this.onUpdate,
      options: contextOpts,
      site: {
        url: "https://support.mozilla.org/kb/customize-items-on-firefox-new-tab-page"
      },
      link: "https://support.mozilla.org/kb/customize-items-on-firefox-new-tab-page",
      shouldSendImpressionStats: false
    }) : null));
    if (Weather.searchActive) {
      return /*#__PURE__*/external_React_default().createElement(LocationSearch, {
        outerClassName: outerClassName
      });
    } else if (WEATHER_SUGGESTION) {
      return /*#__PURE__*/external_React_default().createElement("div", {
        ref: this.setImpressionRef,
        className: outerClassName
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherCard"
      }, /*#__PURE__*/external_React_default().createElement("a", {
        "data-l10n-id": "newtab-weather-see-forecast",
        "data-l10n-args": "{\"provider\": \"AccuWeather\xAE\"}",
        href: WEATHER_SUGGESTION.forecast.url,
        className: "weatherInfoLink",
        onClick: this.onProviderClick
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherIconCol"
      }, /*#__PURE__*/external_React_default().createElement("span", {
        className: `weatherIcon iconId${WEATHER_SUGGESTION.current_conditions.icon_id}`
      })), /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherText"
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherForecastRow"
      }, /*#__PURE__*/external_React_default().createElement("span", {
        className: "weatherTemperature"
      }, WEATHER_SUGGESTION.current_conditions.temperature[Prefs.values["weather.temperatureUnits"]], "\xB0", Prefs.values["weather.temperatureUnits"])), /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherCityRow"
      }, /*#__PURE__*/external_React_default().createElement("span", {
        className: "weatherCity"
      }, Weather.locationData.city)), showDetailedView ? /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherDetailedSummaryRow"
      }, /*#__PURE__*/external_React_default().createElement("div", {
        className: "weatherHighLowTemps"
      }, /*#__PURE__*/external_React_default().createElement("span", null, WEATHER_SUGGESTION.forecast.high[Prefs.values["weather.temperatureUnits"]], "\xB0", Prefs.values["weather.temperatureUnits"]), /*#__PURE__*/external_React_default().createElement("span", null, "\u2022"), /*#__PURE__*/external_React_default().createElement("span", null, WEATHER_SUGGESTION.forecast.low[Prefs.values["weather.temperatureUnits"]], "\xB0", Prefs.values["weather.temperatureUnits"])), /*#__PURE__*/external_React_default().createElement("span", {
        className: "weatherTextSummary"
      }, WEATHER_SUGGESTION.current_conditions.summary)) : null)), contextMenu(WEATHER_SOURCE_CONTEXT_MENU_OPTIONS)), /*#__PURE__*/external_React_default().createElement("span", {
        className: "weatherSponsorText"
      }, /*#__PURE__*/external_React_default().createElement("span", {
        "data-l10n-id": "newtab-weather-sponsored",
        "data-l10n-args": "{\"provider\": \"AccuWeather\xAE\"}"
      })));
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      ref: this.setErrorRef,
      className: outerClassName
    }, /*#__PURE__*/external_React_default().createElement("div", {
      className: "weatherNotAvailable"
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "icon icon-info-warning"
    }), " ", /*#__PURE__*/external_React_default().createElement("p", {
      "data-l10n-id": "newtab-weather-error-not-available"
    }), contextMenu(WEATHER_SOURCE_ERROR_CONTEXT_MENU_OPTIONS)));
  }
}
const Weather_Weather = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  Weather: state.Weather,
  Prefs: state.Prefs,
  IntersectionObserver: globalThis.IntersectionObserver,
  document: globalThis.document
}))(_Weather);
;// CONCATENATED MODULE: ./content-src/components/DownloadModalToggle/DownloadModalToggle.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function DownloadModalToggle({
  onClick,
  isActive
}) {
  return /*#__PURE__*/external_React_default().createElement("button", {
    className: `mobile-download-promo ${isActive ? " is-active" : ""}`,
    onClick: onClick
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "icon icon-device-phone"
  }));
}

;// CONCATENATED MODULE: ./content-src/components/Notifications/Toasts/ThumbUpThumbDownToast.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function ThumbUpThumbDownToast({
  onDismissClick,
  onAnimationEnd
}) {
  const mozMessageBarRef = (0,external_React_namespaceObject.useRef)(null);
  (0,external_React_namespaceObject.useEffect)(() => {
    const {
      current: mozMessageBarElement
    } = mozMessageBarRef;
    mozMessageBarElement.addEventListener("message-bar:user-dismissed", onDismissClick, {
      once: true
    });
    return () => {
      mozMessageBarElement.removeEventListener("message-bar:user-dismissed", onDismissClick);
    };
  }, [onDismissClick]);
  return /*#__PURE__*/external_React_default().createElement("moz-message-bar", {
    type: "success",
    class: "notification-feed-item",
    dismissable: true,
    "data-l10n-id": "newtab-toast-thumbs-up-or-down2",
    ref: mozMessageBarRef,
    onAnimationEnd: onAnimationEnd
  });
}

;// CONCATENATED MODULE: ./content-src/components/Notifications/Toasts/ReportContentToast.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */


function ReportContentToast({
  onDismissClick,
  onAnimationEnd
}) {
  const mozMessageBarRef = (0,external_React_namespaceObject.useRef)(null);
  (0,external_React_namespaceObject.useEffect)(() => {
    const {
      current: mozMessageBarElement
    } = mozMessageBarRef;
    mozMessageBarElement.addEventListener("message-bar:user-dismissed", onDismissClick, {
      once: true
    });
    return () => {
      mozMessageBarElement.removeEventListener("message-bar:user-dismissed", onDismissClick);
    };
  }, [onDismissClick]);
  return /*#__PURE__*/external_React_default().createElement("moz-message-bar", {
    type: "success",
    class: "notification-feed-item",
    dismissable: true,
    "data-l10n-id": "newtab-toast-thanks-for-reporting",
    ref: mozMessageBarRef,
    onAnimationEnd: onAnimationEnd
  });
}

;// CONCATENATED MODULE: ./content-src/components/Notifications/Notifications.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */






function Notifications_Notifications({
  dispatch
}) {
  const toastQueue = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Notifications.toastQueue);
  const toastCounter = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Notifications.toastCounter);

  /**
   * Syncs {@link toastQueue} array so it can be used to
   * remove the toasts wrapper if there are none after a
   * toast is auto-hidden (animated out) via CSS.
   */
  const syncHiddenToastData = (0,external_React_namespaceObject.useCallback)(() => {
    const toastId = toastQueue[toastQueue.length - 1];
    const queuedToasts = [...toastQueue].slice(1);
    dispatch(actionCreators.OnlyToOneContent({
      type: actionTypes.HIDE_TOAST_MESSAGE,
      data: {
        toastQueue: queuedToasts,
        toastCounter: queuedToasts.length,
        toastId,
        showNotifications: false
      }
    }, "ActivityStream:Content"));
  }, [dispatch, toastQueue]);
  const getToast = (0,external_React_namespaceObject.useCallback)(() => {
    // Note: This architecture could expand to support multiple toast notifications at once
    const latestToastItem = toastQueue[toastQueue.length - 1];
    if (!latestToastItem) {
      throw new Error("No toast found");
    }
    switch (latestToastItem) {
      case "reportSuccessToast":
        return /*#__PURE__*/external_React_default().createElement(ReportContentToast, {
          onDismissClick: syncHiddenToastData,
          onAnimationEnd: syncHiddenToastData,
          key: toastCounter
        });
      case "thumbsUpToast":
      case "thumbsDownToast":
        return /*#__PURE__*/external_React_default().createElement(ThumbUpThumbDownToast, {
          onDismissClick: syncHiddenToastData,
          onAnimationEnd: syncHiddenToastData,
          key: toastCounter
        });
      default:
        throw new Error(`Unexpected toast type: ${latestToastItem}`);
    }
  }, [syncHiddenToastData, toastCounter, toastQueue]);
  (0,external_React_namespaceObject.useEffect)(() => {
    getToast();
  }, [toastQueue, getToast]);
  return toastQueue.length ? /*#__PURE__*/external_React_default().createElement("div", {
    className: "notification-wrapper"
  }, getToast()) : "";
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/TopicSelection/TopicSelection.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





const EMOJI_LABELS = {
  business: "💼",
  arts: "🎭",
  food: "🍕",
  health: "🩺",
  finance: "💰",
  government: "🏛️",
  sports: "⚽️",
  tech: "💻",
  travel: "✈️",
  "education-science": "🧪",
  society: "💡"
};
function TopicSelection({
  supportUrl
}) {
  const dispatch = (0,external_ReactRedux_namespaceObject.useDispatch)();
  const inputRef = (0,external_React_namespaceObject.useRef)(null);
  const modalRef = (0,external_React_namespaceObject.useRef)(null);
  const checkboxWrapperRef = (0,external_React_namespaceObject.useRef)(null);
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const topics = prefs["discoverystream.topicSelection.topics"].split(", ");
  const selectedTopics = prefs["discoverystream.topicSelection.selectedTopics"];
  const suggestedTopics = prefs["discoverystream.topicSelection.suggestedTopics"]?.split(", ");
  const displayCount = prefs["discoverystream.topicSelection.onboarding.displayCount"];
  const topicsHaveBeenPreviouslySet = prefs["discoverystream.topicSelection.hasBeenUpdatedPreviously"];
  const [isFirstRun] = (0,external_React_namespaceObject.useState)(displayCount === 0);
  const displayCountRef = (0,external_React_namespaceObject.useRef)(displayCount);
  const preselectedTopics = () => {
    if (selectedTopics) {
      return selectedTopics.split(", ");
    }
    return isFirstRun ? suggestedTopics : [];
  };
  const [topicsToSelect, setTopicsToSelect] = (0,external_React_namespaceObject.useState)(preselectedTopics);
  function isFirstSave() {
    // Only return true if the user has not previous set prefs
    // and the selected topics pref is empty
    if (selectedTopics === "" && !topicsHaveBeenPreviouslySet) {
      return true;
    }
    return false;
  }
  function handleModalClose() {
    dispatch(actionCreators.OnlyToMain({
      type: actionTypes.TOPIC_SELECTION_USER_DISMISS
    }));
    dispatch(actionCreators.BroadcastToContent({
      type: actionTypes.TOPIC_SELECTION_SPOTLIGHT_CLOSE
    }));
  }
  function handleUserClose(e) {
    const id = e?.target?.id;
    if (id === "first-run") {
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.TOPIC_SELECTION_MAYBE_LATER
      }));
      dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", true));
    } else {
      dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", false));
    }
    handleModalClose();
  }

  // By doing this, the useEffect that sets up the IntersectionObserver
  // will not re-run every time displayCount changes,
  // but the observer callback will always have access
  // to the latest displayCount value through the ref.
  (0,external_React_namespaceObject.useEffect)(() => {
    displayCountRef.current = displayCount;
  }, [displayCount]);
  (0,external_React_namespaceObject.useEffect)(() => {
    const {
      current
    } = modalRef;
    let observer;
    if (current) {
      observer = new IntersectionObserver(([entry]) => {
        if (entry.isIntersecting) {
          // if the user has seen the modal more than 3 times,
          // automatically remove them from onboarding
          if (displayCountRef.current > 3) {
            dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", false));
          }
          observer.unobserve(modalRef.current);
          dispatch(actionCreators.AlsoToMain({
            type: actionTypes.TOPIC_SELECTION_IMPRESSION
          }));
        }
      });
      observer.observe(current);
    }
    return () => {
      if (current) {
        observer.unobserve(current);
      }
    };
  }, [modalRef, dispatch]);

  // when component mounts, set focus to input
  (0,external_React_namespaceObject.useEffect)(() => {
    inputRef?.current?.focus();
  }, [inputRef]);
  const handleFocus = (0,external_React_namespaceObject.useCallback)(e => {
    // this list will have to be updated with other reusable components that get used inside of this modal
    const tabbableElements = modalRef.current.querySelectorAll('a[href], button, moz-button, input[tabindex="0"]');
    const [firstTabableEl] = tabbableElements;
    const lastTabbableEl = tabbableElements[tabbableElements.length - 1];
    let isTabPressed = e.key === "Tab" || e.keyCode === 9;
    let isArrowPressed = e.key === "ArrowUp" || e.key === "ArrowDown";
    if (isTabPressed) {
      if (e.shiftKey) {
        if (document.activeElement === firstTabableEl) {
          lastTabbableEl.focus();
          e.preventDefault();
        }
      } else if (document.activeElement === lastTabbableEl) {
        firstTabableEl.focus();
        e.preventDefault();
      }
    } else if (isArrowPressed && checkboxWrapperRef.current.contains(document.activeElement)) {
      const checkboxElements = checkboxWrapperRef.current.querySelectorAll("input");
      const [firstInput] = checkboxElements;
      const lastInput = checkboxElements[checkboxElements.length - 1];
      const inputArr = Array.from(checkboxElements);
      const currentIndex = inputArr.indexOf(document.activeElement);
      let nextEl;
      if (e.key === "ArrowUp") {
        nextEl = document.activeElement === firstInput ? lastInput : checkboxElements[currentIndex - 1];
      } else if (e.key === "ArrowDown") {
        nextEl = document.activeElement === lastInput ? firstInput : checkboxElements[currentIndex + 1];
      }
      nextEl.tabIndex = 0;
      document.activeElement.tabIndex = -1;
      nextEl.focus();
    }
  }, []);
  (0,external_React_namespaceObject.useEffect)(() => {
    const ref = modalRef.current;
    ref.addEventListener("keydown", handleFocus);
    inputRef.current.tabIndex = 0;
    return () => {
      ref.removeEventListener("keydown", handleFocus);
    };
  }, [handleFocus]);
  function handleChange(e) {
    const topic = e.target.name;
    const isChecked = e.target.checked;
    if (isChecked) {
      setTopicsToSelect([...topicsToSelect, topic]);
    } else {
      const updatedTopics = topicsToSelect.filter(t => t !== topic);
      setTopicsToSelect(updatedTopics);
    }
  }
  function handleSubmit() {
    const topicsString = topicsToSelect.join(", ");
    dispatch(actionCreators.SetPref("discoverystream.topicSelection.selectedTopics", topicsString));
    dispatch(actionCreators.SetPref("discoverystream.topicSelection.onboarding.maybeDisplay", false));
    if (!topicsHaveBeenPreviouslySet) {
      dispatch(actionCreators.SetPref("discoverystream.topicSelection.hasBeenUpdatedPreviously", true));
    }
    dispatch(actionCreators.OnlyToMain({
      type: actionTypes.TOPIC_SELECTION_USER_SAVE,
      data: {
        topics: topicsString,
        previous_topics: selectedTopics,
        first_save: isFirstSave()
      }
    }));
    handleModalClose();
  }
  return /*#__PURE__*/external_React_default().createElement(ModalOverlayWrapper, {
    onClose: handleUserClose,
    innerClassName: "topic-selection-container"
  }, /*#__PURE__*/external_React_default().createElement("div", {
    className: "topic-selection-form",
    ref: modalRef
  }, /*#__PURE__*/external_React_default().createElement("button", {
    className: "dismiss-button",
    title: "dismiss",
    onClick: handleUserClose
  }), /*#__PURE__*/external_React_default().createElement("h1", {
    className: "title",
    "data-l10n-id": "newtab-topic-selection-title"
  }), /*#__PURE__*/external_React_default().createElement("p", {
    className: "subtitle",
    "data-l10n-id": "newtab-topic-selection-subtitle"
  }), /*#__PURE__*/external_React_default().createElement("div", {
    className: "topic-list",
    ref: checkboxWrapperRef
  }, topics.map((topic, i) => {
    const checked = topicsToSelect.includes(topic);
    return /*#__PURE__*/external_React_default().createElement("label", {
      className: `topic-item`,
      key: topic
    }, /*#__PURE__*/external_React_default().createElement("input", {
      type: "checkbox",
      id: topic,
      name: topic,
      ref: i === 0 ? inputRef : null,
      onChange: handleChange,
      checked: checked,
      "aria-checked": checked,
      tabIndex: -1
    }), /*#__PURE__*/external_React_default().createElement("div", {
      className: `topic-custom-checkbox`
    }, /*#__PURE__*/external_React_default().createElement("span", {
      className: "topic-icon"
    }, EMOJI_LABELS[`${topic}`]), /*#__PURE__*/external_React_default().createElement("span", {
      className: "topic-checked"
    })), /*#__PURE__*/external_React_default().createElement("span", {
      className: "topic-item-label",
      "data-l10n-id": `newtab-topic-label-${topic}`
    }));
  })), /*#__PURE__*/external_React_default().createElement("div", {
    className: "modal-footer"
  }, /*#__PURE__*/external_React_default().createElement("a", {
    href: supportUrl,
    "data-l10n-id": "newtab-topic-selection-privacy-link"
  }), /*#__PURE__*/external_React_default().createElement("moz-button-group", {
    className: "button-group"
  }, /*#__PURE__*/external_React_default().createElement("moz-button", {
    id: isFirstRun ? "first-run" : "",
    "data-l10n-id": isFirstRun ? "newtab-topic-selection-button-maybe-later" : "newtab-topic-selection-cancel-button",
    onClick: handleUserClose
  }), /*#__PURE__*/external_React_default().createElement("moz-button", {
    "data-l10n-id": "newtab-topic-selection-save-button",
    type: "primary",
    onClick: handleSubmit
  })))));
}

;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/FeatureHighlight/DownloadMobilePromoHighlight.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_A = "mobileDownloadModal.variant-a";
const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_B = "mobileDownloadModal.variant-b";
const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_C = "mobileDownloadModal.variant-c";
function DownloadMobilePromoHighlight({
  position,
  dispatch,
  handleDismiss,
  handleBlock
}) {
  const onDismiss = (0,external_React_namespaceObject.useCallback)(() => {
    handleDismiss();
    handleBlock();
  }, [handleDismiss, handleBlock]);
  const prefs = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Prefs.values);
  const mobileDownloadPromoVarA = prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_A];
  const mobileDownloadPromoVarB = prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_B];
  const mobileDownloadPromoVarC = prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_C];
  function getActiveVariant() {
    if (mobileDownloadPromoVarA) {
      return "A";
    }
    if (mobileDownloadPromoVarB) {
      return "B";
    }
    if (mobileDownloadPromoVarC) {
      return "C";
    }
    return null;
  }
  function getVariantQRCodeImg() {
    const variant = getActiveVariant();
    switch (variant) {
      case "A":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-a.png";
      case "B":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-b.png";
      case "C":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-c.png";
      default:
        return null;
    }
  }
  function getVariantCopy() {
    const variant = getActiveVariant();
    switch (variant) {
      case "A":
        return "newtab-download-mobile-highlight-body-variant-a";
      case "B":
        return "newtab-download-mobile-highlight-body-variant-b";
      case "C":
        return "newtab-download-mobile-highlight-body-variant-c";
      default:
        return null;
    }
  }
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "download-firefox-feature-highlight"
  }, /*#__PURE__*/external_React_default().createElement(FeatureHighlight, {
    position: position,
    feature: "FEATURE_DOWNLOAD_MOBILE_PROMO",
    dispatch: dispatch,
    message: /*#__PURE__*/external_React_default().createElement("div", {
      className: "download-firefox-feature-highlight-content"
    }, /*#__PURE__*/external_React_default().createElement("img", {
      src: getVariantQRCodeImg(),
      "data-l10n-id": "newtab-download-mobile-highlight-image",
      width: "120",
      height: "191",
      alt: ""
    }), /*#__PURE__*/external_React_default().createElement("p", {
      className: "title",
      "data-l10n-id": "newtab-download-mobile-highlight-title"
    }), /*#__PURE__*/external_React_default().createElement("p", {
      className: "subtitle",
      "data-l10n-id": getVariantCopy()
    })),
    openedOverride: true,
    showButtonIcon: false,
    dismissCallback: onDismiss,
    outsideClickCallback: handleDismiss
  }));
}
;// CONCATENATED MODULE: ./content-src/components/DiscoveryStreamComponents/FeatureHighlight/WallpaperFeatureHighlight.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




function WallpaperFeatureHighlight({
  position,
  dispatch,
  handleDismiss,
  handleClick,
  handleBlock
}) {
  const onDismiss = (0,external_React_namespaceObject.useCallback)(() => {
    handleDismiss();
    handleBlock();
  }, [handleDismiss, handleBlock]);
  const onToggleClick = (0,external_React_namespaceObject.useCallback)(elementId => {
    dispatch({
      type: actionTypes.SHOW_PERSONALIZE
    });
    dispatch(actionCreators.UserEvent({
      event: "SHOW_PERSONALIZE"
    }));
    handleClick(elementId);
    onDismiss();
  }, [dispatch, onDismiss, handleClick]);
  return /*#__PURE__*/external_React_default().createElement("div", {
    className: "wallpaper-feature-highlight"
  }, /*#__PURE__*/external_React_default().createElement(FeatureHighlight, {
    position: position,
    "data-l10n-id": "feature-highlight-wallpaper",
    feature: "FEATURE_HIGHLIGHT_WALLPAPER",
    dispatch: dispatch,
    message: /*#__PURE__*/external_React_default().createElement("div", {
      className: "wallpaper-feature-highlight-content"
    }, /*#__PURE__*/external_React_default().createElement("img", {
      src: "chrome://newtab/content/data/content/assets/custom-wp-highlight.png",
      alt: "",
      width: "320",
      height: "195"
    }), /*#__PURE__*/external_React_default().createElement("p", {
      className: "title",
      "data-l10n-id": "newtab-custom-wallpaper-title"
    }), /*#__PURE__*/external_React_default().createElement("p", {
      className: "subtitle",
      "data-l10n-id": "newtab-custom-wallpaper-subtitle"
    }), /*#__PURE__*/external_React_default().createElement("span", {
      className: "button-wrapper"
    }, /*#__PURE__*/external_React_default().createElement("moz-button", {
      type: "default",
      onClick: () => onToggleClick("open-customize-menu"),
      "data-l10n-id": "newtab-custom-wallpaper-cta"
    }))),
    toggle: /*#__PURE__*/external_React_default().createElement("div", {
      className: "icon icon-help"
    }),
    openedOverride: true,
    showButtonIcon: false,
    dismissCallback: onDismiss,
    outsideClickCallback: handleDismiss
  }));
}
;// CONCATENATED MODULE: ./content-src/components/MessageWrapper/MessageWrapper.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */





function MessageWrapper({
  children,
  dispatch,
  hiddenOverride,
  onDismiss
}) {
  const message = (0,external_ReactRedux_namespaceObject.useSelector)(state => state.Messages);
  const [isIntersecting, setIsIntersecting] = (0,external_React_namespaceObject.useState)(false);
  const [hasRun, setHasRun] = (0,external_React_namespaceObject.useState)();
  const handleIntersection = (0,external_React_namespaceObject.useCallback)(() => {
    setIsIntersecting(true);
    const isVisible = document?.visibilityState && document.visibilityState === "visible";
    // only send impression if messageId is defined and tab is visible
    if (isVisible && message.messageData.id) {
      setHasRun(true);
      dispatch(actionCreators.AlsoToMain({
        type: actionTypes.MESSAGE_IMPRESSION,
        data: message.messageData
      }));
    }
  }, [dispatch, message]);
  (0,external_React_namespaceObject.useEffect)(() => {
    const handleVisibilityChange = () => {
      if (document.visibilityState === "visible" && !hasRun) {
        handleIntersection();
      }
    };
    document.addEventListener("visibilitychange", handleVisibilityChange);
    return () => {
      document.removeEventListener("visibilitychange", handleVisibilityChange);
    };
  }, [handleIntersection, hasRun]);
  const ref = useIntersectionObserver(handleIntersection);
  const handleClose = (0,external_React_namespaceObject.useCallback)(() => {
    const action = {
      type: actionTypes.MESSAGE_TOGGLE_VISIBILITY,
      data: true
    };
    if (message.portID) {
      dispatch(actionCreators.OnlyToOneContent(action, message.portID));
    } else {
      dispatch(actionCreators.AlsoToMain(action));
    }
    onDismiss();
  }, [dispatch, message, onDismiss]);
  function handleDismiss() {
    const {
      id
    } = message.messageData;
    if (id) {
      dispatch(actionCreators.OnlyToMain({
        type: actionTypes.MESSAGE_DISMISS,
        data: {
          message: message.messageData
        }
      }));
    }
    handleClose();
  }
  function handleBlock() {
    const {
      id
    } = message.messageData;
    if (id) {
      dispatch(actionCreators.OnlyToMain({
        type: actionTypes.MESSAGE_BLOCK,
        data: id
      }));
    }
  }
  function handleClick(elementId) {
    const {
      id
    } = message.messageData;
    if (id) {
      dispatch(actionCreators.OnlyToMain({
        type: actionTypes.MESSAGE_CLICK,
        data: {
          message: message.messageData,
          source: elementId || ""
        }
      }));
    }
  }

  // only display the message if `isHidden` is false
  return (!message.isHidden || hiddenOverride) && /*#__PURE__*/external_React_default().createElement("div", {
    ref: el => {
      ref.current = [el];
    },
    className: "message-wrapper"
  }, /*#__PURE__*/external_React_default().cloneElement(children, {
    isIntersecting,
    handleDismiss,
    handleClick,
    handleBlock,
    handleClose
  }));
}

;// CONCATENATED MODULE: ./content-src/components/Base/Base.jsx
function Base_extends() { Base_extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return Base_extends.apply(this, arguments); }
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */



















const Base_VISIBLE = "visible";
const Base_VISIBILITY_CHANGE_EVENT = "visibilitychange";
const Base_PREF_THUMBS_UP_DOWN_ENABLED = "discoverystream.thumbsUpDown.enabled";
const PREF_THUMBS_UP_DOWN_LAYOUT_ENABLED = "discoverystream.thumbsUpDown.searchTopsitesCompact";
const PREF_INFERRED_PERSONALIZATION_SYSTEM = "discoverystream.sections.personalization.inferred.enabled";
const PREF_INFERRED_PERSONALIZATION_USER = "discoverystream.sections.personalization.inferred.user.enabled";
const PrefsButton = ({
  onClick,
  icon
}) => /*#__PURE__*/external_React_default().createElement("div", {
  className: "prefs-button"
}, /*#__PURE__*/external_React_default().createElement("button", {
  className: `icon ${icon || "icon-settings"}`,
  onClick: onClick,
  "data-l10n-id": "newtab-settings-button"
}));

// Returns a function will not be continuously triggered when called. The
// function will be triggered if called again after `wait` milliseconds.
function Base_debounce(func, wait) {
  let timer;
  return (...args) => {
    if (timer) {
      return;
    }
    let wakeUp = () => {
      timer = null;
    };
    timer = setTimeout(wakeUp, wait);
    func.apply(this, args);
  };
}
class _Base extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.state = {
      message: {}
    };
    this.notifyContent = this.notifyContent.bind(this);
  }
  notifyContent(state) {
    this.setState(state);
  }
  componentWillUnmount() {
    this.updateTheme();
  }
  componentWillUpdate() {
    this.updateTheme();
  }
  updateTheme() {
    const bodyClassName = ["activity-stream",
    // If we skipped the about:welcome overlay and removed the CSS classes
    // we don't want to add them back to the Activity Stream view
    document.body.classList.contains("inline-onboarding") ? "inline-onboarding" : ""].filter(v => v).join(" ");
    globalThis.document.body.className = bodyClassName;
  }
  render() {
    const {
      props
    } = this;
    const {
      App
    } = props;
    const isDevtoolsEnabled = props.Prefs.values["asrouter.devtoolsEnabled"];
    if (!App.initialized) {
      return null;
    }
    return /*#__PURE__*/external_React_default().createElement(ErrorBoundary, {
      className: "base-content-fallback"
    }, /*#__PURE__*/external_React_default().createElement((external_React_default()).Fragment, null, /*#__PURE__*/external_React_default().createElement(BaseContent, Base_extends({}, this.props, {
      adminContent: this.state
    })), isDevtoolsEnabled ? /*#__PURE__*/external_React_default().createElement(DiscoveryStreamAdmin, {
      notifyContent: this.notifyContent
    }) : null));
  }
}
class BaseContent extends (external_React_default()).PureComponent {
  constructor(props) {
    super(props);
    this.openPreferences = this.openPreferences.bind(this);
    this.openCustomizationMenu = this.openCustomizationMenu.bind(this);
    this.closeCustomizationMenu = this.closeCustomizationMenu.bind(this);
    this.handleOnKeyDown = this.handleOnKeyDown.bind(this);
    this.onWindowScroll = Base_debounce(this.onWindowScroll.bind(this), 5);
    this.setPref = this.setPref.bind(this);
    this.shouldShowOMCHighlight = this.shouldShowOMCHighlight.bind(this);
    this.updateWallpaper = this.updateWallpaper.bind(this);
    this.prefersDarkQuery = null;
    this.handleColorModeChange = this.handleColorModeChange.bind(this);
    this.shouldDisplayTopicSelectionModal = this.shouldDisplayTopicSelectionModal.bind(this);
    this.toggleDownloadHighlight = this.toggleDownloadHighlight.bind(this);
    this.handleDismissDownloadHighlight = this.handleDismissDownloadHighlight.bind(this);
    this.state = {
      fixedSearch: false,
      firstVisibleTimestamp: null,
      colorMode: "",
      fixedNavStyle: {},
      wallpaperTheme: "",
      showDownloadHighlight: this.shouldShowOMCHighlight("DownloadMobilePromoHighlight")
    };
  }
  setFirstVisibleTimestamp() {
    if (!this.state.firstVisibleTimestamp) {
      this.setState({
        firstVisibleTimestamp: Date.now()
      });
    }
  }
  componentDidMount() {
    __webpack_require__.g.addEventListener("scroll", this.onWindowScroll);
    __webpack_require__.g.addEventListener("keydown", this.handleOnKeyDown);
    const prefs = this.props.Prefs.values;
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    if (this.props.document.visibilityState === Base_VISIBLE) {
      this.setFirstVisibleTimestamp();
      this.shouldDisplayTopicSelectionModal();
    } else {
      this._onVisibilityChange = () => {
        if (this.props.document.visibilityState === Base_VISIBLE) {
          this.setFirstVisibleTimestamp();
          this.shouldDisplayTopicSelectionModal();
          this.props.document.removeEventListener(Base_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
          this._onVisibilityChange = null;
        }
      };
      this.props.document.addEventListener(Base_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
    // track change event to dark/light mode
    this.prefersDarkQuery = globalThis.matchMedia("(prefers-color-scheme: dark)");
    this.prefersDarkQuery.addEventListener("change", this.handleColorModeChange);
    this.handleColorModeChange();
    if (wallpapersV2Enabled) {
      this.updateWallpaper();
    }
  }
  componentDidUpdate(prevProps) {
    const prefs = this.props.Prefs.values;
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    if (wallpapersV2Enabled) {
      // destructure current and previous props with fallbacks
      // (preventing undefined errors)
      const {
        Wallpapers: {
          uploadedWallpaper = null,
          wallpaperList = null
        } = {}
      } = this.props;
      const {
        Wallpapers: {
          uploadedWallpaper: prevUploadedWallpaper = null,
          wallpaperList: prevWallpaperList = null
        } = {},
        Prefs: {
          values: prevPrefs = {}
        } = {}
      } = prevProps;
      const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];
      const prevSelectedWallpaper = prevPrefs["newtabWallpapers.wallpaper"];

      // don't update wallpaper unless the wallpaper is being changed.
      if (selectedWallpaper !== prevSelectedWallpaper ||
      // selecting a new wallpaper
      uploadedWallpaper !== prevUploadedWallpaper ||
      // uploading a new wallpaper
      wallpaperList !== prevWallpaperList // remote settings wallpaper list updates
      ) {
        this.updateWallpaper();
      }
    }
  }
  handleColorModeChange() {
    const colorMode = this.prefersDarkQuery?.matches ? "dark" : "light";
    this.setState({
      colorMode
    });
  }
  componentWillUnmount() {
    this.prefersDarkQuery?.removeEventListener("change", this.handleColorModeChange);
    __webpack_require__.g.removeEventListener("scroll", this.onWindowScroll);
    __webpack_require__.g.removeEventListener("keydown", this.handleOnKeyDown);
    if (this._onVisibilityChange) {
      this.props.document.removeEventListener(Base_VISIBILITY_CHANGE_EVENT, this._onVisibilityChange);
    }
  }
  onWindowScroll() {
    if (window.innerHeight <= 700) {
      // Bug 1937296: Only apply fixed-search logic
      // if the page is tall enough to support it.
      return;
    }
    const prefs = this.props.Prefs.values;
    const {
      showSearch
    } = prefs;
    if (!showSearch) {
      // Bug 1944718: Only apply fixed-search logic
      // if search is visible.
      return;
    }
    const logoAlwaysVisible = prefs["logowordmark.alwaysVisible"];
    const layoutsVariantAEnabled = prefs["newtabLayouts.variant-a"];
    const layoutsVariantBEnabled = prefs["newtabLayouts.variant-b"];
    const layoutsVariantAorB = layoutsVariantAEnabled || layoutsVariantBEnabled;
    const thumbsUpDownEnabled = prefs[Base_PREF_THUMBS_UP_DOWN_ENABLED];
    // For the compact layout to be active,
    // thumbs also has to be enabled until Bug 1932242 is fixed
    const thumbsUpDownLayoutEnabled = prefs[PREF_THUMBS_UP_DOWN_LAYOUT_ENABLED] && thumbsUpDownEnabled;

    /* Bug 1917937: The logic presented below is fragile but accurate to the pixel. As new tab experiments with layouts, we have a tech debt of competing styles and classes the slightly modify where the search bar sits on the page. The larger solution for this is to replace everything with an intersection observer, but would require a larger refactor of this file. In the interim, we can programmatically calculate when to fire the fixed-scroll event and account for the moved elements so that topsites/etc stays in the same place. The CSS this references has been flagged to reference this logic so (hopefully) keep them in sync. */

    let SCROLL_THRESHOLD = 0; // When the fixed-scroll event fires
    let MAIN_OFFSET_PADDING = 0; // The padding to compensate for the moved elements

    let layout = {
      outerWrapperPaddingTop: 30,
      searchWrapperPaddingTop: 34,
      searchWrapperPaddingBottom: 38,
      searchWrapperFixedScrollPaddingTop: 27,
      searchWrapperFixedScrollPaddingBottom: 27,
      searchInnerWrapperMinHeight: 52,
      logoAndWordmarkWrapperHeight: 64,
      logoAndWordmarkWrapperMarginBottom: 48
    };
    const CSS_VAR_SPACE_XXLARGE = 34.2; // Custom Acorn themed variable (8 * 0.267rem);

    // Experimental layouts
    // (Note these if statements are ordered to match the CSS cascade)
    if (thumbsUpDownLayoutEnabled || layoutsVariantAorB) {
      // Thumbs Compact View Layout
      if (thumbsUpDownLayoutEnabled) {
        layout.logoAndWordmarkWrapperMarginBottom = CSS_VAR_SPACE_XXLARGE;
        if (!logoAlwaysVisible) {
          layout.searchWrapperPaddingTop = CSS_VAR_SPACE_XXLARGE;
          layout.searchWrapperPaddingBottom = CSS_VAR_SPACE_XXLARGE;
        }
      }

      // Variant B Layout
      if (layoutsVariantAEnabled) {
        layout.outerWrapperPaddingTop = 24;
        if (!thumbsUpDownLayoutEnabled) {
          layout.searchWrapperPaddingTop = 0;
          layout.searchWrapperPaddingBottom = 32;
          layout.logoAndWordmarkWrapperMarginBottom = 32;
        }
      }

      // Variant B Layout
      if (layoutsVariantBEnabled) {
        layout.outerWrapperPaddingTop = 24;
        // Logo is positioned absolute, so remove it
        layout.logoAndWordmarkWrapperHeight = 0;
        layout.logoAndWordmarkWrapperMarginBottom = 0;
        layout.searchWrapperPaddingTop = 16;
        layout.searchWrapperPaddingBottom = CSS_VAR_SPACE_XXLARGE;
        if (!thumbsUpDownLayoutEnabled) {
          layout.searchWrapperPaddingBottom = 32;
        }
      }
    }

    // Logo visibility applies to all layouts
    if (!logoAlwaysVisible) {
      layout.logoAndWordmarkWrapperHeight = 0;
      layout.logoAndWordmarkWrapperMarginBottom = 0;
    }
    SCROLL_THRESHOLD = layout.outerWrapperPaddingTop + layout.searchWrapperPaddingTop + layout.logoAndWordmarkWrapperHeight + layout.logoAndWordmarkWrapperMarginBottom - layout.searchWrapperFixedScrollPaddingTop;
    MAIN_OFFSET_PADDING = layout.searchWrapperPaddingTop + layout.searchWrapperPaddingBottom + layout.searchInnerWrapperMinHeight + layout.logoAndWordmarkWrapperHeight + layout.logoAndWordmarkWrapperMarginBottom;

    // Edge case if logo and thums are turned off, but Var A is enabled
    if (SCROLL_THRESHOLD < 1) {
      SCROLL_THRESHOLD = 1;
    }
    if (__webpack_require__.g.scrollY > SCROLL_THRESHOLD && !this.state.fixedSearch) {
      this.setState({
        fixedSearch: true,
        fixedNavStyle: {
          paddingBlockStart: `${MAIN_OFFSET_PADDING}px`
        }
      });
    } else if (__webpack_require__.g.scrollY <= SCROLL_THRESHOLD && this.state.fixedSearch) {
      this.setState({
        fixedSearch: false,
        fixedNavStyle: {}
      });
    }
  }
  openPreferences() {
    this.props.dispatch(actionCreators.OnlyToMain({
      type: actionTypes.SETTINGS_OPEN
    }));
    this.props.dispatch(actionCreators.UserEvent({
      event: "OPEN_NEWTAB_PREFS"
    }));
  }
  openCustomizationMenu() {
    this.props.dispatch({
      type: actionTypes.SHOW_PERSONALIZE
    });
    this.props.dispatch(actionCreators.UserEvent({
      event: "SHOW_PERSONALIZE"
    }));
  }
  closeCustomizationMenu() {
    if (this.props.App.customizeMenuVisible) {
      this.props.dispatch({
        type: actionTypes.HIDE_PERSONALIZE
      });
      this.props.dispatch(actionCreators.UserEvent({
        event: "HIDE_PERSONALIZE"
      }));
    }
  }
  handleOnKeyDown(e) {
    if (e.key === "Escape") {
      this.closeCustomizationMenu();
    }
  }
  setPref(pref, value) {
    this.props.dispatch(actionCreators.SetPref(pref, value));
  }
  renderWallpaperAttribution() {
    const {
      wallpaperList
    } = this.props.Wallpapers;
    const activeWallpaper = this.props.Prefs.values[`newtabWallpapers.wallpaper`];
    const selected = wallpaperList.find(wp => wp.title === activeWallpaper);
    // make sure a wallpaper is selected and that the attribution also exists
    if (!selected?.attribution) {
      return null;
    }
    const {
      name: authorDetails,
      webpage
    } = selected.attribution;
    if (activeWallpaper && wallpaperList && authorDetails.url) {
      return /*#__PURE__*/external_React_default().createElement("p", {
        className: `wallpaper-attribution`,
        key: authorDetails.string,
        "data-l10n-id": "newtab-wallpaper-attribution",
        "data-l10n-args": JSON.stringify({
          author_string: authorDetails.string,
          author_url: authorDetails.url,
          webpage_string: webpage.string,
          webpage_url: webpage.url
        })
      }, /*#__PURE__*/external_React_default().createElement("a", {
        "data-l10n-name": "name-link",
        href: authorDetails.url
      }, authorDetails.string), /*#__PURE__*/external_React_default().createElement("a", {
        "data-l10n-name": "webpage-link",
        href: webpage.url
      }, webpage.string));
    }
    return null;
  }
  async updateWallpaper() {
    const prefs = this.props.Prefs.values;
    const selectedWallpaper = prefs["newtabWallpapers.wallpaper"];
    const {
      wallpaperList,
      uploadedWallpaper
    } = this.props.Wallpapers;
    let lightWallpaper = {};
    let darkWallpaper = {};
    if (selectedWallpaper === "custom" && uploadedWallpaper) {
      // revoke ObjectURL to prevent memory leaks
      if (this.uploadedWallpaperUrl) {
        URL.revokeObjectURL(this.uploadedWallpaperUrl);
      }
      const uploadedWallpaperUrl = URL.createObjectURL(uploadedWallpaper);
      __webpack_require__.g.document?.body.style.setProperty("--newtab-wallpaper", `url(${uploadedWallpaperUrl})`);
      __webpack_require__.g.document?.body.style.setProperty("--newtab-wallpaper-color", "transparent");
      return;
    }
    if (wallpaperList) {
      let wallpaper = wallpaperList.find(wp => wp.title === selectedWallpaper);
      if (selectedWallpaper && wallpaper) {
        // if selectedWallpaper exists - we override what light and dark prefs are to match that
        lightWallpaper = wallpaper;
        darkWallpaper = wallpaper;
      }

      // solid-color-picker-#00d100
      const regexRGB = /#([a-fA-F0-9]{6})/;

      // Override Remote Settings to set custom HEX bg color
      if (selectedWallpaper.includes("solid-color-picker")) {
        wallpaper = {
          theme: wallpaper?.theme || "light",
          title: "solid-color-picker",
          category: "solid-colors",
          solid_color: selectedWallpaper.match(regexRGB)?.[0]
        };
      }
      const wallpaperColor = wallpaper?.solid_color || "";
      __webpack_require__.g.document?.body.style.setProperty("--newtab-wallpaper", `url(${wallpaper?.wallpaperUrl || ""})`);
      __webpack_require__.g.document?.body.style.setProperty("--newtab-wallpaper-color", wallpaperColor || "transparent");
      let wallpaperTheme = "";

      // If we have a solid colour set, let's see how dark it is.
      if (wallpaperColor) {
        const rgbColors = this.getRGBColors(wallpaperColor);
        const isColorDark = this.isWallpaperColorDark(rgbColors);
        wallpaperTheme = isColorDark ? "dark" : "light";
      } else {
        // Grab the contrast of the currently displayed wallpaper.
        const {
          theme
        } = this.state.colorMode === "light" ? lightWallpaper : darkWallpaper;
        if (theme) {
          wallpaperTheme = theme;
        }
      }
      this.setState({
        wallpaperTheme
      });
    }
  }
  shouldShowOMCHighlight(componentId) {
    const messageData = this.props.Messages?.messageData;
    if (!messageData || Object.keys(messageData).length === 0) {
      return false;
    }
    return messageData?.content?.messageType === componentId;
  }
  toggleDownloadHighlight() {
    this.setState(prevState => ({
      showDownloadHighlight: !prevState.showDownloadHighlight
    }));
  }
  handleDismissDownloadHighlight() {
    this.setState({
      showDownloadHighlight: false
    });
  }
  getRGBColors(input) {
    if (input.length !== 7) {
      return [];
    }
    const r = parseInt(input.substr(1, 2), 16);
    const g = parseInt(input.substr(3, 2), 16);
    const b = parseInt(input.substr(5, 2), 16);
    return [r, g, b];
  }
  isWallpaperColorDark([r, g, b]) {
    return 0.2125 * r + 0.7154 * g + 0.0721 * b <= 110;
  }
  shouldDisplayTopicSelectionModal() {
    const prefs = this.props.Prefs.values;
    const pocketEnabled = prefs["feeds.section.topstories"] && prefs["feeds.system.topstories"];
    const topicSelectionOnboardingEnabled = prefs["discoverystream.topicSelection.onboarding.enabled"] && pocketEnabled;
    const maybeShowModal = prefs["discoverystream.topicSelection.onboarding.maybeDisplay"];
    const displayTimeout = prefs["discoverystream.topicSelection.onboarding.displayTimeout"];
    const lastDisplayed = prefs["discoverystream.topicSelection.onboarding.lastDisplayed"];
    const displayCount = prefs["discoverystream.topicSelection.onboarding.displayCount"];
    if (!maybeShowModal || !prefs["discoverystream.topicSelection.enabled"] || !topicSelectionOnboardingEnabled) {
      return;
    }
    const day = 24 * 60 * 60 * 1000;
    const now = new Date().getTime();
    const timeoutOccured = now - parseFloat(lastDisplayed) > displayTimeout;
    if (displayCount < 3) {
      if (displayCount === 0 || timeoutOccured) {
        this.props.dispatch(actionCreators.BroadcastToContent({
          type: actionTypes.TOPIC_SELECTION_SPOTLIGHT_OPEN
        }));
        this.setPref("discoverystream.topicSelection.onboarding.displayTimeout", day);
      }
    }
  }

  // eslint-disable-next-line max-statements, complexity
  render() {
    const {
      props
    } = this;
    const {
      App,
      DiscoveryStream
    } = props;
    const {
      initialized,
      customizeMenuVisible
    } = App;
    const prefs = props.Prefs.values;
    const layoutsVariantAEnabled = prefs["newtabLayouts.variant-a"];
    const layoutsVariantBEnabled = prefs["newtabLayouts.variant-b"];
    const shortcutsRefresh = prefs["newtabShortcuts.refresh"];
    const layoutsVariantAorB = layoutsVariantAEnabled || layoutsVariantBEnabled;
    const activeWallpaper = prefs[`newtabWallpapers.wallpaper`];
    const wallpapersV2Enabled = prefs["newtabWallpapers.v2.enabled"];
    const weatherEnabled = prefs.showWeather;
    const {
      showTopicSelection
    } = DiscoveryStream;
    const mayShowTopicSelection = showTopicSelection && prefs["discoverystream.topicSelection.enabled"];
    const {
      pocketConfig
    } = prefs;
    const isDiscoveryStream = props.DiscoveryStream.config && props.DiscoveryStream.config.enabled;
    let filteredSections = props.Sections.filter(section => section.id !== "topstories");
    let spocMessageVariant = "";
    if (props.App.locale?.startsWith("en-") && pocketConfig?.spocMessageVariant === "variant-c") {
      spocMessageVariant = pocketConfig.spocMessageVariant;
    }
    const pocketEnabled = prefs["feeds.section.topstories"] && prefs["feeds.system.topstories"];
    const noSectionsEnabled = !prefs["feeds.topsites"] && !pocketEnabled && filteredSections.filter(section => section.enabled).length === 0;
    const searchHandoffEnabled = prefs["improvesearch.handoffToAwesomebar"];
    const enabledSections = {
      topSitesEnabled: prefs["feeds.topsites"],
      pocketEnabled: prefs["feeds.section.topstories"],
      showInferredPersonalizationEnabled: prefs[PREF_INFERRED_PERSONALIZATION_USER],
      showRecentSavesEnabled: prefs.showRecentSaves,
      topSitesRowsCount: prefs.topSitesRows,
      weatherEnabled: prefs.showWeather
    };
    const pocketRegion = prefs["feeds.system.topstories"];
    const mayHaveSponsoredStories = prefs["system.showSponsored"];
    const mayHaveInferredPersonalization = prefs[PREF_INFERRED_PERSONALIZATION_SYSTEM];
    const mayHaveWeather = prefs["system.showWeather"];
    const {
      mayHaveSponsoredTopSites
    } = prefs;
    const supportUrl = prefs["support.url"];

    // Mobile Download Promo Pref Checks
    const mobileDownloadPromoEnabled = prefs["mobileDownloadModal.enabled"];
    const mobileDownloadPromoVariantAEnabled = prefs["mobileDownloadModal.variant-a"];
    const mobileDownloadPromoVariantBEnabled = prefs["mobileDownloadModal.variant-b"];
    const mobileDownloadPromoVariantCEnabled = prefs["mobileDownloadModal.variant-c"];
    const mobileDownloadPromoVariantABorC = mobileDownloadPromoVariantAEnabled || mobileDownloadPromoVariantBEnabled || mobileDownloadPromoVariantCEnabled;
    const mobileDownloadPromoWrapperHeightModifier = prefs["weather.display"] === "detailed" && weatherEnabled && mayHaveWeather ? "is-tall" : "";
    const hasThumbsUpDownLayout = prefs["discoverystream.thumbsUpDown.searchTopsitesCompact"];
    const hasThumbsUpDown = prefs["discoverystream.thumbsUpDown.enabled"];
    const sectionsEnabled = prefs["discoverystream.sections.enabled"];
    const topicLabelsEnabled = prefs["discoverystream.topicLabels.enabled"];
    const sectionsCustomizeMenuPanelEnabled = prefs["discoverystream.sections.customizeMenuPanel.enabled"];
    const sectionsPersonalizationEnabled = prefs["discoverystream.sections.personalization.enabled"];
    // Logic to show follow/block topic mgmt panel in Customize panel
    const mayHavePersonalizedTopicSections = sectionsPersonalizationEnabled && topicLabelsEnabled && sectionsEnabled && sectionsCustomizeMenuPanelEnabled && DiscoveryStream.feeds.loaded;
    const featureClassName = [mobileDownloadPromoEnabled && mobileDownloadPromoVariantABorC && "has-mobile-download-promo",
    // Mobile download promo modal is enabled/visible
    weatherEnabled && mayHaveWeather && "has-weather",
    // Weather widget is enabled/visible
    prefs.showSearch ? "has-search" : "no-search", layoutsVariantAEnabled ? "layout-variant-a" : "",
    // Layout experiment variant A
    layoutsVariantBEnabled ? "layout-variant-b" : "",
    // Layout experiment variant B
    shortcutsRefresh ? "shortcuts-refresh" : "",
    // Shortcuts refresh experiment
    pocketEnabled ? "has-recommended-stories" : "no-recommended-stories", sectionsEnabled ? "has-sections-grid" : ""].filter(v => v).join(" ");
    const outerClassName = ["outer-wrapper", isDiscoveryStream && pocketEnabled && "ds-outer-wrapper-search-alignment", isDiscoveryStream && "ds-outer-wrapper-breakpoint-override", prefs.showSearch && this.state.fixedSearch && !noSectionsEnabled && "fixed-search", prefs.showSearch && noSectionsEnabled && "only-search", prefs["feeds.topsites"] && !pocketEnabled && !prefs.showSearch && "only-topsites", noSectionsEnabled && "no-sections", prefs["logowordmark.alwaysVisible"] && "visible-logo", hasThumbsUpDownLayout && hasThumbsUpDown && "thumbs-ui-compact"].filter(v => v).join(" ");
    if (wallpapersV2Enabled) {
      // Add helper class to body if user has a wallpaper selected
      if (this.state.wallpaperTheme === "light") {
        __webpack_require__.g.document?.body.classList.add("lightWallpaper");
        __webpack_require__.g.document?.body.classList.remove("darkWallpaper");
      }
      if (this.state.wallpaperTheme === "dark") {
        __webpack_require__.g.document?.body.classList.add("darkWallpaper");
        __webpack_require__.g.document?.body.classList.remove("lightWallpaper");
      }
    }
    return /*#__PURE__*/external_React_default().createElement("div", {
      className: featureClassName
    }, /*#__PURE__*/external_React_default().createElement("menu", {
      className: "personalizeButtonWrapper"
    }, /*#__PURE__*/external_React_default().createElement(CustomizeMenu, {
      onClose: this.closeCustomizationMenu,
      onOpen: this.openCustomizationMenu,
      openPreferences: this.openPreferences,
      setPref: this.setPref,
      enabledSections: enabledSections,
      wallpapersV2Enabled: wallpapersV2Enabled,
      activeWallpaper: activeWallpaper,
      pocketRegion: pocketRegion,
      mayHaveTopicSections: mayHavePersonalizedTopicSections,
      mayHaveSponsoredTopSites: mayHaveSponsoredTopSites,
      mayHaveSponsoredStories: mayHaveSponsoredStories,
      mayHaveInferredPersonalization: mayHaveInferredPersonalization,
      mayHaveWeather: mayHaveWeather,
      spocMessageVariant: spocMessageVariant,
      showing: customizeMenuVisible
    }), this.shouldShowOMCHighlight("CustomWallpaperHighlight") && /*#__PURE__*/external_React_default().createElement(MessageWrapper, {
      dispatch: this.props.dispatch
    }, /*#__PURE__*/external_React_default().createElement(WallpaperFeatureHighlight, {
      position: "inset-block-start inset-inline-start",
      dispatch: this.props.dispatch
    }))), /*#__PURE__*/external_React_default().createElement("div", {
      className: "weatherWrapper"
    }, weatherEnabled && /*#__PURE__*/external_React_default().createElement(ErrorBoundary, null, /*#__PURE__*/external_React_default().createElement(Weather_Weather, null))), /*#__PURE__*/external_React_default().createElement("div", {
      className: `mobileDownloadPromoWrapper ${mobileDownloadPromoWrapperHeightModifier}`
    }, mobileDownloadPromoEnabled && mobileDownloadPromoVariantABorC && /*#__PURE__*/external_React_default().createElement(ErrorBoundary, null, /*#__PURE__*/external_React_default().createElement(DownloadModalToggle, {
      isActive: this.state.showDownloadHighlight,
      onClick: this.toggleDownloadHighlight
    }), this.state.showDownloadHighlight && /*#__PURE__*/external_React_default().createElement(MessageWrapper, {
      hiddenOverride: this.state.showDownloadHighlight,
      onDismiss: this.handleDismissDownloadHighlight,
      dispatch: this.props.dispatch
    }, /*#__PURE__*/external_React_default().createElement(DownloadMobilePromoHighlight
    // Var B layout has the weather right-aligned
    , {
      position: `${layoutsVariantBEnabled ? "inset-inline-start" : "inset-inline-end"} inset-block-end`,
      dispatch: this.props.dispatch
    })))), /*#__PURE__*/external_React_default().createElement("div", {
      className: outerClassName,
      onClick: this.closeCustomizationMenu
    }, /*#__PURE__*/external_React_default().createElement("main", {
      className: "newtab-main",
      style: this.state.fixedNavStyle
    }, prefs.showSearch && /*#__PURE__*/external_React_default().createElement("div", {
      className: "non-collapsible-section"
    }, /*#__PURE__*/external_React_default().createElement(ErrorBoundary, null, /*#__PURE__*/external_React_default().createElement(Search_Search, Base_extends({
      showLogo: noSectionsEnabled || prefs["logowordmark.alwaysVisible"],
      handoffEnabled: searchHandoffEnabled
    }, props.Search)))), !prefs.showSearch && layoutsVariantAorB && !noSectionsEnabled && /*#__PURE__*/external_React_default().createElement(Logo, null), /*#__PURE__*/external_React_default().createElement("div", {
      className: `body-wrapper${initialized ? " on" : ""}`
    }, isDiscoveryStream ? /*#__PURE__*/external_React_default().createElement(ErrorBoundary, {
      className: "borderless-error"
    }, /*#__PURE__*/external_React_default().createElement(DiscoveryStreamBase, {
      locale: props.App.locale,
      mayHaveSponsoredStories: mayHaveSponsoredStories,
      firstVisibleTimestamp: this.state.firstVisibleTimestamp
    })) : /*#__PURE__*/external_React_default().createElement(Sections_Sections, null)), /*#__PURE__*/external_React_default().createElement(ConfirmDialog, null), wallpapersV2Enabled && this.renderWallpaperAttribution()), /*#__PURE__*/external_React_default().createElement("aside", null, this.props.Notifications?.showNotifications && /*#__PURE__*/external_React_default().createElement(ErrorBoundary, null, /*#__PURE__*/external_React_default().createElement(Notifications_Notifications, {
      dispatch: this.props.dispatch
    }))), mayShowTopicSelection && pocketEnabled && /*#__PURE__*/external_React_default().createElement(TopicSelection, {
      supportUrl: supportUrl
    })));
  }
}
BaseContent.defaultProps = {
  document: __webpack_require__.g.document
};
const Base = (0,external_ReactRedux_namespaceObject.connect)(state => ({
  App: state.App,
  Prefs: state.Prefs,
  Sections: state.Sections,
  DiscoveryStream: state.DiscoveryStream,
  Messages: state.Messages,
  Notifications: state.Notifications,
  Search: state.Search,
  Wallpapers: state.Wallpapers,
  Weather: state.Weather
}))(_Base);
;// CONCATENATED MODULE: ./content-src/lib/detect-user-session-start.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */




const detect_user_session_start_VISIBLE = "visible";
const detect_user_session_start_VISIBILITY_CHANGE_EVENT = "visibilitychange";

class DetectUserSessionStart {
  constructor(store, options = {}) {
    this._store = store;
    // Overrides for testing
    this.document = options.document || globalThis.document;
    this._perfService = options.perfService || perfService;
    this._onVisibilityChange = this._onVisibilityChange.bind(this);
  }

  /**
   * sendEventOrAddListener - Notify immediately if the page is already visible,
   *                    or else set up a listener for when visibility changes.
   *                    This is needed for accurate session tracking for telemetry,
   *                    because tabs are pre-loaded.
   */
  sendEventOrAddListener() {
    if (this.document.visibilityState === detect_user_session_start_VISIBLE) {
      // If the document is already visible, to the user, send a notification
      // immediately that a session has started.
      this._sendEvent();
    } else {
      // If the document is not visible, listen for when it does become visible.
      this.document.addEventListener(
        detect_user_session_start_VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
  }

  /**
   * _sendEvent - Sends a message to the main process to indicate the current
   *              tab is now visible to the user, includes the
   *              visibility_event_rcvd_ts time in ms from the UNIX epoch.
   */
  _sendEvent() {
    this._perfService.mark("visibility_event_rcvd_ts");

    try {
      let visibility_event_rcvd_ts =
        this._perfService.getMostRecentAbsMarkStartByName(
          "visibility_event_rcvd_ts"
        );

      this._store.dispatch(
        actionCreators.AlsoToMain({
          type: actionTypes.SAVE_SESSION_PERF_DATA,
          data: {
            visibility_event_rcvd_ts,
            window_inner_width: window.innerWidth,
            window_inner_height: window.innerHeight,
          },
        })
      );
    } catch (ex) {
      // If this failed, it's likely because the `privacy.resistFingerprinting`
      // pref is true.  We should at least not blow up.
    }
  }

  /**
   * _onVisibilityChange - If the visibility has changed to visible, sends a notification
   *                      and removes the event listener. This should only be called once per tab.
   */
  _onVisibilityChange() {
    if (this.document.visibilityState === detect_user_session_start_VISIBLE) {
      this._sendEvent();
      this.document.removeEventListener(
        detect_user_session_start_VISIBILITY_CHANGE_EVENT,
        this._onVisibilityChange
      );
    }
  }
}

;// CONCATENATED MODULE: external "Redux"
const external_Redux_namespaceObject = Redux;
;// CONCATENATED MODULE: ./content-src/lib/init-store.mjs
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/remote-page */


// We disable import checking here as redux is installed via the npm packages
// at the newtab level, rather than in the top-level package.json.
// eslint-disable-next-line import/no-unresolved


const MERGE_STORE_ACTION = "NEW_TAB_INITIAL_STATE";
const OUTGOING_MESSAGE_NAME = "ActivityStream:ContentToMain";
const INCOMING_MESSAGE_NAME = "ActivityStream:MainToContent";

/**
 * A higher-order function which returns a reducer that, on MERGE_STORE action,
 * will return the action.data object merged into the previous state.
 *
 * For all other actions, it merely calls mainReducer.
 *
 * Because we want this to merge the entire state object, it's written as a
 * higher order function which takes the main reducer (itself often a call to
 * combineReducers) as a parameter.
 *
 * @param  {function} mainReducer reducer to call if action != MERGE_STORE_ACTION
 * @return {function}             a reducer that, on MERGE_STORE_ACTION action,
 *                                will return the action.data object merged
 *                                into the previous state, and the result
 *                                of calling mainReducer otherwise.
 */
function mergeStateReducer(mainReducer) {
  return (prevState, action) => {
    if (action.type === MERGE_STORE_ACTION) {
      return { ...prevState, ...action.data };
    }

    return mainReducer(prevState, action);
  };
}

/**
 * messageMiddleware - Middleware that looks for SentToMain type actions, and sends them if necessary
 */
const messageMiddleware = () => next => action => {
  const skipLocal = action.meta && action.meta.skipLocal;
  if (actionUtils.isSendToMain(action)) {
    RPMSendAsyncMessage(OUTGOING_MESSAGE_NAME, action);
  }
  if (!skipLocal) {
    next(action);
  }
};

const rehydrationMiddleware = ({ getState }) => {
  // NB: The parameter here is MiddlewareAPI which looks like a Store and shares
  // the same getState, so attached properties are accessible from the store.
  getState.didRehydrate = false;
  getState.didRequestInitialState = false;
  return next => action => {
    if (getState.didRehydrate || window.__FROM_STARTUP_CACHE__) {
      // Startup messages can be safely ignored by the about:home document
      // stored in the startup cache.
      if (
        window.__FROM_STARTUP_CACHE__ &&
        action.meta &&
        action.meta.isStartup
      ) {
        return null;
      }
      return next(action);
    }

    const isMergeStoreAction = action.type === MERGE_STORE_ACTION;
    const isRehydrationRequest = action.type === actionTypes.NEW_TAB_STATE_REQUEST;

    if (isRehydrationRequest) {
      getState.didRequestInitialState = true;
      return next(action);
    }

    if (isMergeStoreAction) {
      getState.didRehydrate = true;
      return next(action);
    }

    // If init happened after our request was made, we need to re-request
    if (getState.didRequestInitialState && action.type === actionTypes.INIT) {
      return next(actionCreators.AlsoToMain({ type: actionTypes.NEW_TAB_STATE_REQUEST }));
    }

    if (
      actionUtils.isBroadcastToContent(action) ||
      actionUtils.isSendToOneContent(action) ||
      actionUtils.isSendToPreloaded(action)
    ) {
      // Note that actions received before didRehydrate will not be dispatched
      // because this could negatively affect preloading and the the state
      // will be replaced by rehydration anyway.
      return null;
    }

    return next(action);
  };
};

/**
 * initStore - Create a store and listen for incoming actions
 *
 * @param  {object} reducers An object containing Redux reducers
 * @param  {object} intialState (optional) The initial state of the store, if desired
 * @return {object}          A redux store
 */
function initStore(reducers, initialState) {
  const store = (0,external_Redux_namespaceObject.createStore)(
    mergeStateReducer((0,external_Redux_namespaceObject.combineReducers)(reducers)),
    initialState,
    globalThis.RPMAddMessageListener &&
      (0,external_Redux_namespaceObject.applyMiddleware)(rehydrationMiddleware, messageMiddleware)
  );

  if (globalThis.RPMAddMessageListener) {
    globalThis.RPMAddMessageListener(INCOMING_MESSAGE_NAME, msg => {
      try {
        store.dispatch(msg.data);
      } catch (ex) {
        console.error("Content msg:", msg, "Dispatch error: ", ex);
        dump(
          `Content msg: ${JSON.stringify(msg)}\nDispatch error: ${ex}\n${
            ex.stack
          }`
        );
      }
    });
  }

  return store;
}

;// CONCATENATED MODULE: external "ReactDOM"
const external_ReactDOM_namespaceObject = ReactDOM;
var external_ReactDOM_default = /*#__PURE__*/__webpack_require__.n(external_ReactDOM_namespaceObject);
;// CONCATENATED MODULE: ./content-src/activity-stream.jsx
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */









const NewTab = ({
  store
}) => /*#__PURE__*/external_React_default().createElement(external_ReactRedux_namespaceObject.Provider, {
  store: store
}, /*#__PURE__*/external_React_default().createElement(Base, null));
function renderWithoutState() {
  const store = initStore(reducers);
  new DetectUserSessionStart(store).sendEventOrAddListener();

  // If this document has already gone into the background by the time we've reached
  // here, we can deprioritize requesting the initial state until the event loop
  // frees up. If, however, the visibility changes, we then send the request.
  let didRequest = false;
  let requestIdleCallbackId = 0;
  function doRequest() {
    if (!didRequest) {
      if (requestIdleCallbackId) {
        cancelIdleCallback(requestIdleCallbackId);
      }
      didRequest = true;
      store.dispatch(actionCreators.AlsoToMain({
        type: actionTypes.NEW_TAB_STATE_REQUEST
      }));
    }
  }
  if (document.hidden) {
    requestIdleCallbackId = requestIdleCallback(doRequest);
    addEventListener("visibilitychange", doRequest, {
      once: true
    });
  } else {
    doRequest();
  }
  external_ReactDOM_default().hydrate( /*#__PURE__*/external_React_default().createElement(NewTab, {
    store: store
  }), document.getElementById("root"));
}
function renderCache(initialState) {
  const store = initStore(reducers, initialState);
  new DetectUserSessionStart(store).sendEventOrAddListener();
  external_ReactDOM_default().hydrate( /*#__PURE__*/external_React_default().createElement(NewTab, {
    store: store
  }), document.getElementById("root"));
}
NewtabRenderUtils = __webpack_exports__;
/******/ })()
;