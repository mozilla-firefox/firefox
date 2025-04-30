/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  applyMiddleware,
  createStore,
} = require("resource://devtools/client/shared/vendor/redux.js");

const {
  waitUntilService,
} = require("resource://devtools/client/shared/redux/middleware/wait-service.js");

const {
  MIN_COLUMN_WIDTH,
  DEFAULT_COLUMN_WIDTH,
} = require("resource://devtools/client/netmonitor/src/constants.js");

// Middleware
const batching = require("resource://devtools/client/netmonitor/src/middleware/batching.js");
const prefs = require("resource://devtools/client/netmonitor/src/middleware/prefs.js");
const {
  thunk,
} = require("resource://devtools/client/shared/redux/middleware/thunk.js");
const throttling = require("resource://devtools/client/netmonitor/src/middleware/throttling.js");
const eventTelemetry = require("resource://devtools/client/netmonitor/src/middleware/event-telemetry.js");
const requestBlocking = require("resource://devtools/client/netmonitor/src/middleware/request-blocking.js");

// Reducers
const rootReducer = require("resource://devtools/client/netmonitor/src/reducers/index.js");
const {
  FilterTypes,
  Filters,
} = require("resource://devtools/client/netmonitor/src/reducers/filters.js");
const {
  Requests,
} = require("resource://devtools/client/netmonitor/src/reducers/requests.js");
const {
  Sort,
} = require("resource://devtools/client/netmonitor/src/reducers/sort.js");
const {
  TimingMarkers,
} = require("resource://devtools/client/netmonitor/src/reducers/timing-markers.js");
const {
  UI,
  Columns,
  ColumnsData,
} = require("resource://devtools/client/netmonitor/src/reducers/ui.js");
const {
  Messages,
  getMessageDefaultColumnsState,
} = require("resource://devtools/client/netmonitor/src/reducers/messages.js");
const {
  Search,
} = require("resource://devtools/client/netmonitor/src/reducers/search.js");

/**
 * Configure state and middleware for the Network monitor tool.
 */
function configureStore(connector, commands, telemetry) {
  // Prepare initial state.
  const initialState = {
    filters: new Filters(getFiltersState()),
    requests: new Requests(),
    sort: new Sort(),
    timingMarkers: new TimingMarkers(),
    ui: UI({
      columns: getColumnState(),
      columnsData: getColumnsData(),
    }),
    messages: Messages({
      columns: getMessageColumnState(),
    }),
    search: new Search(),
  };

  // Prepare middleware.
  const middleware = applyMiddleware(
    requestBlocking(commands),
    thunk({ connector, commands }),
    prefs,
    batching,
    throttling(connector),
    eventTelemetry(connector, telemetry),
    waitUntilService
  );

  return createStore(rootReducer, initialState, middleware);
}

// Helpers

/**
 * Get column state from preferences.
 */
function getColumnState() {
  const columns = Columns();
  const visibleColumns = getJsonPref("devtools.netmonitor.visibleColumns");

  const state = {};
  for (const col in columns) {
    state[col] = visibleColumns.includes(col);
  }

  return state;
}

/**
 * Get column state of Messages from preferences.
 */
function getMessageColumnState() {
  const columns = getMessageDefaultColumnsState();
  const visibleColumns = getJsonPref("devtools.netmonitor.msg.visibleColumns");

  const state = {};
  for (const col in columns) {
    state[col] = visibleColumns.includes(col);
  }

  return state;
}

/**
 * Get columns data (width, min-width)
 */
function getColumnsData() {
  const columnsData = getJsonPref("devtools.netmonitor.columnsData");
  if (!columnsData.length) {
    return ColumnsData();
  }

  const newMap = new Map();
  columnsData.forEach(col => {
    if (col.name) {
      col.minWidth = col.minWidth ? col.minWidth : MIN_COLUMN_WIDTH;
      col.width = col.width ? col.width : DEFAULT_COLUMN_WIDTH;
      newMap.set(col.name, col);
    }
  });

  return newMap;
}

/**
 * Get filter state from preferences.
 */
function getFiltersState() {
  const activeFilters = {};
  const filters = getJsonPref("devtools.netmonitor.filters");
  filters.forEach(filter => {
    activeFilters[filter] = true;
  });

  return {
    requestFilterTypes: new FilterTypes(activeFilters),
    requestFilterText: Services.prefs.getCharPref(
      "devtools.netmonitor.requestfilter"
    ),
  };
}

/**
 * Get json data from preferences
 */

function getJsonPref(pref) {
  try {
    return JSON.parse(Services.prefs.getCharPref(pref));
  } catch (_) {
    return [];
  }
}

exports.configureStore = configureStore;
