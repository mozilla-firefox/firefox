/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const actionTypes = {
  // manifest substate
  FETCH_MANIFEST_FAILURE: "FETCH_MANIFEST_FAILURE",
  FETCH_MANIFEST_START: "FETCH_MANIFEST_START",
  FETCH_MANIFEST_SUCCESS: "FETCH_MANIFEST_SUCCESS",
  RESET_MANIFEST: "RESET_MANIFEST",
  // page substate
  UPDATE_DOMAIN: "UPDATE_DOMAIN",
  // ui substate
  UPDATE_SELECTED_PAGE: "UPDATE_SELECTED_PAGE",
  // workers substate
  START_WORKER: "START_WORKER",
  UNREGISTER_WORKER: "UNREGISTER_WORKER",
  UPDATE_CAN_DEBUG_WORKERS: "UPDATE_CAN_DEBUG_WORKERS",
  UPDATE_WORKERS: "UPDATE_WORKERS",
  // session history substate
  SET_AVAILABLE_SESSION_HISTORY: "SET_AVAILABLE_SESSION_HISTORY",
  UPDATE_SESSION_HISTORY: "UPDATE_SESSION_HISTORY",
  UPDATE_SESSION_HISTORY_ENTRY: "UPDATE_SESSION_HISTORY_ENTRY",
  DISABLE_SESSION_HISTORY: "DISABLE_SESSION_HISTORY",
};

// NOTE: these const values are used as part of CSS selectors - be mindful of the characters used
const PAGE_TYPES = {
  MANIFEST: "manifest",
  SERVICE_WORKERS: "service-workers",
  SESSION_HISTORY: "session-history",
};

const DEFAULT_PAGE = PAGE_TYPES.SERVICE_WORKERS;

const MANIFEST_CATEGORIES = {
  IDENTITY: "identity",
  PRESENTATION: "presentation",
  ICONS: "icons",
};

const MANIFEST_MEMBER_VALUE_TYPES = {
  COLOR: "color",
  ICON: "icon",
  STRING: "string",
  URL: "url",
};

const MANIFEST_ISSUE_LEVELS = {
  ERROR: "error",
  WARNING: "warning",
};

// flatten constants
module.exports = Object.assign(
  {},
  {
    DEFAULT_PAGE,
    PAGE_TYPES,
    MANIFEST_CATEGORIES,
    MANIFEST_ISSUE_LEVELS,
    MANIFEST_MEMBER_VALUE_TYPES,
  },
  actionTypes
);
