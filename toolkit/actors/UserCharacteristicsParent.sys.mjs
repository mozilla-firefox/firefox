/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "UserCharacteristicsPage",
    maxLogLevelPref: "toolkit.telemetry.user_characteristics_ping.logLevel",
  });
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "UserCharacteristicsPageService",
  "@mozilla.org/user-characteristics-page;1",
  Ci.nsIUserCharacteristicsPageService
);

function mapToObjectReplacer(_key, value) {
  if (value instanceof Map) {
    return Object.fromEntries(value);
  }
  return value;
}

class UserCharacteristicsParent extends JSWindowActorParent {
  receiveMessage(aMessage) {
    lazy.console.debug("Actor Parent: Got ", aMessage.name);
    switch (aMessage.name) {
      case "UserCharacteristics::PageReady":
        lazy.console.debug("Actor Parent: Got pageReady");
        lazy.UserCharacteristicsPageService.pageLoaded(
          this.browsingContext,
          aMessage.data
        );
        break;
      case "ScreenInfo:Populated":
        Services.obs.notifyObservers(
          null,
          "user-characteristics-screen-info-done",
          JSON.stringify(aMessage.data, mapToObjectReplacer)
        );
        break;
      case "PointerInfo:Populated":
        Services.obs.notifyObservers(
          null,
          "user-characteristics-pointer-info-done",
          JSON.stringify(aMessage.data, mapToObjectReplacer)
        );
        break;
      case "WindowInfo::Done":
        Services.obs.notifyObservers(
          null,
          "user-characteristics-window-info-done",
          aMessage.data
        );
        break;
    }
  }
}

export {
  UserCharacteristicsParent,
  UserCharacteristicsParent as UserCharacteristicsWindowInfoParent,
  UserCharacteristicsParent as UserCharacteristicsCanvasRenderingParent,
};
