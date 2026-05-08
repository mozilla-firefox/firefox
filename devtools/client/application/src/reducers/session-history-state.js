/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  SET_AVAILABLE_SESSION_HISTORY,
  DISABLE_SESSION_HISTORY,
  UPDATE_SESSION_HISTORY,
  UPDATE_SESSION_HISTORY_ENTRY,
} = require("resource://devtools/client/application/src/constants.js");

function SessionHistory() {
  return {
    current: 0,
    diagrams: [],
    entriesByKey: {},
    disabled: false,
  };
}

function sessionHistoryReducer(state = SessionHistory(), action) {
  switch (action.type) {
    case SET_AVAILABLE_SESSION_HISTORY:
    case UPDATE_SESSION_HISTORY: {
      const { sessionHistory } = action;
      return Object.assign({}, state, {
        current: sessionHistory.current,
        diagrams: sessionHistory.diagrams,
        entriesByKey: sessionHistory.entriesByKey,
      });
    }
    case UPDATE_SESSION_HISTORY_ENTRY: {
      const { sessionHistoryEntry } = action;
      const entryKey = sessionHistoryEntry.key;
      const entry = state.entriesByKey[entryKey];
      if (!entry) {
        return state;
      }
      // Updates happen when we're mutating the existing session history,
      // without neither adding or traversing. This happens when a document
      // updates its title, a window changes its name, or through calls to
      // History.replaceState.
      return {
        ...state,
        entriesByKey: {
          ...state.entriesByKey,
          [entryKey]: {
            ...entry,
            url: sessionHistoryEntry.url,
            title: sessionHistoryEntry.title,
            name: sessionHistoryEntry.name,
          },
        },
      };
    }
    case DISABLE_SESSION_HISTORY: {
      return Object.assign({}, state, {
        disabled: true,
      });
    }
    default:
      return state;
  }
}

module.exports = {
  SessionHistory,
  sessionHistoryReducer,
};
