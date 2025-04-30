/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import React from "devtools/client/shared/vendor/react";
import {
  bindActionCreators,
  combineReducers,
} from "devtools/client/shared/vendor/redux";
import ReactDOM from "devtools/client/shared/vendor/react-dom";
const {
  Provider,
} = require("resource://devtools/client/shared/vendor/react-redux.js");

import ToolboxProvider from "devtools/client/framework/store-provider";
import flags from "devtools/shared/flags";
const {
  registerStoreObserver,
} = require("resource://devtools/client/shared/redux/subscriber.js");

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

import { SearchDispatcher } from "../workers/search/index";
import { PrettyPrintDispatcher } from "../workers/pretty-print/index";

import configureStore from "../actions/utils/create-store";
import reducers from "../reducers/index";
import * as selectors from "../selectors/index";
import App from "../components/App";
import { asyncStore, prefs } from "./prefs";
import { persistTabs } from "../utils/tabs";
const {
  sanitizeBreakpoints,
} = require("resource://devtools/client/shared/thread-utils.js");
const {
  visibilityHandlerStore,
} = require("resource://devtools/client/shared/redux/visibilityHandlerStore.js");

let gWorkers;

export function bootstrapStore(client, workers, panel, initialState) {
  const debugJsModules = AppConstants.DEBUG_JS_MODULES == "1";
  const createStore = configureStore({
    log: prefs.logging || flags.testing,
    timing: debugJsModules,
    makeThunkArgs: args => {
      return { ...args, client, ...workers, panel };
    },
  });

  let store = createStore(combineReducers(reducers), initialState);

  // Also wrap the store in order to pause store update notifications while the panel is hidden.
  store = visibilityHandlerStore(store);

  registerStoreObserver(store, updatePrefs);

  const actions = bindActionCreators(
    require("../actions/index").default,
    store.dispatch
  );

  return { store, actions, selectors };
}

export function bootstrapWorkers(panelWorkers) {
  // The panel worker will typically be the source map and parser workers.
  // Both will be managed by the toolbox.
  gWorkers = {
    prettyPrintWorker: new PrettyPrintDispatcher(),
    searchWorker: new SearchDispatcher(),
  };
  return { ...panelWorkers, ...gWorkers };
}

export function teardownWorkers() {
  gWorkers.prettyPrintWorker.stop();
  gWorkers.searchWorker.stop();
}

/**
 * Create and mount the root App component.
 *
 * @param {ReduxStore} store
 * @param {ReduxStore} toolboxStore
 * @param {Object} appComponentAttributes
 * @param {Array} appComponentAttributes.fluentBundles
 * @param {Document} appComponentAttributes.toolboxDoc
 */
export function bootstrapApp(store, toolboxStore, appComponentAttributes = {}) {
  const mount = getMountElement();
  if (!mount) {
    return;
  }

  ReactDOM.render(
    React.createElement(
      Provider,
      { store },
      React.createElement(
        ToolboxProvider,
        { store: toolboxStore },
        React.createElement(App, appComponentAttributes)
      )
    ),
    mount
  );
}

function getMountElement() {
  return document.querySelector("#mount");
}

// This is the opposite of bootstrapApp
export function unmountRoot() {
  ReactDOM.unmountComponentAtNode(getMountElement());
}

function updatePrefs(state, oldState) {
  const hasChanged = selector =>
    selector(oldState) && selector(oldState) !== selector(state);

  if (hasChanged(selectors.getPendingBreakpoints)) {
    asyncStore.pendingBreakpoints = sanitizeBreakpoints(
      selectors.getPendingBreakpoints(state)
    );
  }

  if (
    oldState.eventListenerBreakpoints &&
    oldState.eventListenerBreakpoints !== state.eventListenerBreakpoints
  ) {
    asyncStore.eventListenerBreakpoints = state.eventListenerBreakpoints;
  }

  if (hasChanged(selectors.getTabs)) {
    asyncStore.tabs = persistTabs(selectors.getTabs(state));
  }

  if (hasChanged(selectors.getXHRBreakpoints)) {
    asyncStore.xhrBreakpoints = selectors.getXHRBreakpoints(state);
  }

  if (hasChanged(selectors.getBlackBoxRanges)) {
    asyncStore.blackboxedRanges = selectors.getBlackBoxRanges(state);
  }

  if (hasChanged(selectors.getMainThreadProjectDirectoryRoots)) {
    asyncStore.directoryRoots =
      selectors.getMainThreadProjectDirectoryRoots(state);
  }
}
