/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  tabTracker: () => lazy.ExtensionParent.apiManager.global.tabTracker,
});

var { DefaultMap } = ExtensionUtils;

const MSG_SET_ENABLED = "Extension:ActivityLog:SetEnabled";
const MSG_LOG = "Extension:ActivityLog:DoLog";

export const ExtensionActivityLog = {
  initialized: false,

  // id => Set(callbacks)
  listeners: new DefaultMap(() => new Set()),
  watchedIds: new Set(),

  init() {
    if (this.initialized) {
      return;
    }

    this.initialized = true;

    Services.ppmm.sharedData.set("extensions/logging", this.watchedIds);

    Services.ppmm.addMessageListener(MSG_LOG, this);
  },

  /**
   * Notify all listeners of an extension activity.
   *
   * @param {string} id The ID of the extension that caused the activity.
   * @param {string} viewType The view type the activity is in.
   * @param {string} type The type of the activity.
   * @param {string} name The API name or path.
   * @param {object} data Activity specific data.
   * @param {Date} [timeStamp] The timestamp for the activity.
   */
  log(id, viewType, type, name, data, timeStamp) {
    if (!this.initialized) {
      return;
    }
    let callbacks = this.listeners.get(id);
    if (callbacks) {
      if (!timeStamp) {
        timeStamp = new Date();
      }

      for (let callback of callbacks) {
        try {
          callback({ id, viewType, timeStamp, type, name, data });
        } catch (e) {
          Cu.reportError(e);
        }
      }
    }
  },

  addListener(id, callback) {
    this.init();
    let callbacks = this.listeners.get(id);
    if (callbacks.size === 0) {
      this.watchedIds.add(id);
      Services.ppmm.sharedData.set("extensions/logging", this.watchedIds);
      Services.ppmm.sharedData.flush();
      Services.ppmm.broadcastAsyncMessage(MSG_SET_ENABLED, { id, value: true });
    }
    callbacks.add(callback);
  },

  removeListener(id, callback) {
    let callbacks = this.listeners.get(id);
    if (callbacks.size > 0) {
      callbacks.delete(callback);
      if (callbacks.size === 0) {
        this.watchedIds.delete(id);
        Services.ppmm.sharedData.set("extensions/logging", this.watchedIds);
        Services.ppmm.sharedData.flush();
        Services.ppmm.broadcastAsyncMessage(MSG_SET_ENABLED, {
          id,
          value: false,
        });
      }
    }
  },

  receiveMessage({ name, data }) {
    if (name === MSG_LOG) {
      let { viewType, browsingContextId } = data;
      if (browsingContextId && (!viewType || viewType == "tab")) {
        let browser =
          BrowsingContext.get(browsingContextId).top.embedderElement;
        let browserData = lazy.tabTracker.getBrowserData(browser);
        if (browserData && browserData.tabId !== undefined) {
          data.data.tabId = browserData.tabId;
        }
      }
      this.log(
        data.id,
        data.viewType,
        data.type,
        data.name,
        data.data,
        new Date(data.timeStamp)
      );
    }
  },
};
