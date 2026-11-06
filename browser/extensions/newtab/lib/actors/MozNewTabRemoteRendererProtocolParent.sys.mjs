/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  logConsole: () =>
    console.createInstance({
      logModule: "MozNewTabRemoteRendererProtocolParent",
      // @backward-compat { version 162 }
      // Older versions ignore logModule and name the module after the prefix.
      // Remove prefix: once 162 reaches Release.
      prefix: "MozNewTabRemoteRendererProtocolParent",
      maxLogLevel: Services.prefs.getBoolPref(
        "browser.newtabpage.activity-stream.remote-renderer.log",
        false
      )
        ? "Debug"
        : "Warn",
    }),
});

/**
 * This is the parent actor for communications that are occurring from the
 * privileged about content process using the moz-newtab-remote-renderer://
 * protocol. That protocol is used to load the remote renderer scripts and
 * styles from either the local bundle, or the cached version in the HTTP
 * cache that was downloaded from RemoteSettings.
 */
export class MozNewTabRemoteRendererProtocolParent extends JSWindowActorParent {
  #assignedRenderer = null;

  async assignRenderer() {
    if (this.#assignedRenderer) {
      lazy.logConsole.warn(
        "There is already a renderer assigned to this window global."
      );
      return this.#assignedRenderer.appProps;
    }

    this.#assignedRenderer =
      await lazy.AboutNewTab.activityStream.remoteRenderer.assign();
    return this.#assignedRenderer.appProps;
  }

  receiveMessage(message) {
    if (
      !this.manager.isInProcess &&
      this.manager.remoteType !== lazy.E10SUtils.PRIVILEGEDABOUT_REMOTE_TYPE
    ) {
      return Promise.reject(new Error("Process type mismatch"));
    }

    switch (message.name) {
      case "GetInputStream": {
        return this.#getInputStream(message.data.uriString);
      }
    }
    return null;
  }

  async #getInputStream(uriString) {
    let uri;
    try {
      uri = Services.io.newURI(uriString);

      if (uri.scheme !== "moz-newtab-remote-renderer") {
        throw new Error(
          `Expected scheme moz-newtab-remote-renderer, but got ${uri.scheme}`
        );
      }
    } catch (e) {
      lazy.logConsole.error("Failed to get input stream: ", e);
      return {
        success: false,
      };
    }

    const resourceType = uri.host;
    if (!this.#assignedRenderer) {
      lazy.logConsole.error(
        "Somehow got an input stream request before the renderer was assigned."
      );
      return {
        success: false,
      };
    }

    switch (resourceType) {
      case "script": {
        return this.getScriptResource();
      }
      case "style": {
        return this.getStyleResource();
      }
    }

    return {
      success: false,
    };
  }

  async getScriptResource() {
    if (!this.#assignedRenderer) {
      lazy.logConsole.error(
        "Cannot get script resource without an assigned renderer."
      );
      return {
        success: false,
      };
    }

    return lazy.AboutNewTab.activityStream.remoteRenderer.getScriptResource(
      this.#assignedRenderer
    );
  }

  async getStyleResource() {
    if (!this.#assignedRenderer) {
      lazy.logConsole.error(
        "Cannot get style resource without an assigned renderer."
      );
      return {
        success: false,
      };
    }

    return lazy.AboutNewTab.activityStream.remoteRenderer.getStyleResource(
      this.#assignedRenderer
    );
  }
}
