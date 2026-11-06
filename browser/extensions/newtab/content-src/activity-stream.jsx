/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { Base } from "content-src/components/Base/Base";
import { DetectUserSessionStart } from "content-src/lib/detect-user-session-start";
import { initStore } from "content-src/lib/init-store";
import { Provider } from "react-redux";
import React from "react";
import ReactDOM from "react-dom";
import { reducers } from "common/Reducers.sys.mjs";

export const NewTab = ({ store }) => (
  <Provider store={store}>
    <Base />
  </Provider>
);

function doRequestWhenReady() {
  // If this document has already gone into the background by the time we've reached
  // here, we can deprioritize the request until the event loop
  // frees up. If, however, the visibility changes, we then send the request.
  const doRequestPromise = new Promise(resolve => {
    let didRequest = false;
    let requestIdleCallbackId = 0;
    function doRequest() {
      if (!didRequest) {
        if (requestIdleCallbackId) {
          cancelIdleCallback(requestIdleCallbackId);
        }
        didRequest = true;
        resolve();
      }
    }

    if (document.hidden) {
      requestIdleCallbackId = requestIdleCallback(doRequest);
      addEventListener("visibilitychange", doRequest, { once: true });
    } else {
      resolve();
    }
  });

  return doRequestPromise;
}

// Infinite loading animations are gated on this class so they don't keep the
// refresh driver running in a hidden document, such as a preloaded newtab.
function syncPageVisibleClass() {
  const sync = () =>
    document.body.classList.toggle("page-visible", !document.hidden);
  sync();
  document.addEventListener("visibilitychange", sync);
}

export function renderWithoutState() {
  syncPageVisibleClass();
  const store = initStore(reducers);
  new DetectUserSessionStart(store).sendEventOrAddListener();

  doRequestWhenReady().then(() => {
    // If state events happened before we got here, we can request state again.
    store.dispatch(ac.AlsoToMain({ type: at.NEW_TAB_STATE_REQUEST }));
    // If we rendered without state, we don't need the startup cache.
    store.dispatch(
      ac.OnlyToMain({ type: at.NEW_TAB_STATE_REQUEST_WITHOUT_STARTUPCACHE })
    );
  });

  const container = document.getElementById("root");
  ReactDOM.createRoot(container).render(<NewTab store={store} />);
}

export function renderCache(initialState) {
  syncPageVisibleClass();
  if (initialState) {
    initialState.App.isForStartupCache.App = false;
  }
  const store = initStore(reducers, initialState);
  new DetectUserSessionStart(store).sendEventOrAddListener();

  doRequestWhenReady().then(() => {
    // If state events happened before we got here,
    // we can notify main that we need updates.
    // The individual feeds know what state is not cached.
    store.dispatch(
      ac.OnlyToMain({ type: at.NEW_TAB_STATE_REQUEST_STARTUPCACHE })
    );
  });

  // Use createRoot so stale startup cache DOM doesn't cause hydration mismatches. (Issue with React 19)
  const container = document.getElementById("root");
  ReactDOM.createRoot(container).render(<NewTab store={store} />);
}
