/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  Log: "chrome://remote/content/shared/Log.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

export class MarionetteEventsChild extends JSWindowActorChild {
  get innerWindowId() {
    return this.manager.innerWindowId;
  }

  actorCreated() {
    // Prevent the logger from being created if the current log level
    // isn't set to 'trace'. This is important for a faster content process
    // creation when Marionette is running.
    if (lazy.Log.isTraceLevelOrOrMore) {
      lazy.logger.trace(
        `[${this.browsingContext.id}] MarionetteEvents actor created ` +
          `for window id ${this.innerWindowId}`
      );
    }
  }

  handleEvent({ target, type }) {
    if (!Services.cpmm.sharedData.get("MARIONETTE_EVENTS_ENABLED")) {
      // The parent process will set MARIONETTE_EVENTS_ENABLED to false when
      // the Marionette session ends to avoid unnecessary inter process
      // communications
      return;
    }

    // Ignore invalid combinations of load events and document's readyState.
    if (
      (type === "DOMContentLoaded" && target.readyState != "interactive") ||
      (type === "pageshow" && target.readyState != "complete")
    ) {
      lazy.logger.warn(
        `Ignoring event '${type}' because document has an invalid ` +
          `readyState of '${target.readyState}'.`
      );
      return;
    }

    switch (type) {
      case "beforeunload":
      case "DOMContentLoaded":
      case "hashchange":
      case "pagehide":
      case "pageshow":
      case "popstate":
        this.sendAsyncMessage("MarionetteEventsChild:PageLoadEvent", {
          browsingContext: this.browsingContext,
          documentURI: target.documentURI,
          readyState: target.readyState,
          type,
          windowId: this.innerWindowId,
        });
        break;
    }
  }
}
