/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewUtils } from "resource://gre/modules/GeckoViewUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "require", () => {
  const { require } = ChromeUtils.importESModule(
    "resource://devtools/shared/loader/Loader.sys.mjs",
    // Ensure loading all DevTools modules in the dedicated "devtools" global
    // so that it can safely debug all the privileged modules running from the shared system global.
    { global: "devtools" }
  );
  return require;
});

ChromeUtils.defineLazyGetter(lazy, "DevToolsServer", () => {
  const { DevToolsServer } = lazy.require("devtools/server/devtools-server");
  return DevToolsServer;
});

ChromeUtils.defineLazyGetter(lazy, "SocketListener", () => {
  const { SocketListener } = lazy.require("devtools/shared/security/socket");
  return SocketListener;
});

const { debug, warn } = GeckoViewUtils.initLogging("RemoteDebugger");

export var GeckoViewRemoteDebugger = {
  observe(aSubject, aTopic, aData) {
    if (aTopic !== "nsPref:changed") {
      return;
    }

    if (Services.prefs.getBoolPref(aData, false)) {
      this.onEnable();
    } else {
      this.onDisable();
    }
  },

  onInit() {
    debug`onInit`;
    this._isEnabled = false;
    this._usbDebugger = new USBRemoteDebugger();
  },

  onEnable() {
    if (this._isEnabled) {
      return;
    }

    debug`onEnable`;
    lazy.DevToolsServer.init();
    lazy.DevToolsServer.registerAllActors();
    const { createRootActor } = lazy.require(
      "moz-src:///mobile/shared/modules/dbg-browser-actors.js"
    );
    lazy.DevToolsServer.setRootActor(createRootActor);
    lazy.DevToolsServer.allowChromeProcess = true;
    lazy.DevToolsServer.chromeWindowType = "navigator:geckoview";
    // Force the Server to stay alive even if there are no connections at the moment.
    lazy.DevToolsServer.keepAlive = true;

    // Socket address for USB remote debugger expects
    // @{package_name}/firefox-debugger-socket format.
    // In /proc/net/unix, it will be outputed as
    // @org.mozilla.geckoview_example/firefox-debugger-socket
    //
    // If package name isn't available, it will be "@firefox-debugger-socket".

    let packageName = Services.env.get("MOZ_ANDROID_PACKAGE_NAME");
    if (packageName) {
      packageName = packageName + "/";
    } else {
      warn`Missing env MOZ_ANDROID_PACKAGE_NAME. Unable to get package name`;
    }

    this._isEnabled = true;
    this._usbDebugger.stop();

    const portOrPath = packageName + "firefox-debugger-socket";
    this._usbDebugger.start(portOrPath);
  },

  onDisable() {
    if (!this._isEnabled) {
      return;
    }

    debug`onDisable`;
    this._isEnabled = false;
    this._usbDebugger.stop();
  },
};

class USBRemoteDebugger {
  start(aPortOrPath) {
    try {
      const AuthenticatorType =
        lazy.DevToolsServer.Authenticators.get("PROMPT");
      const authenticator = new AuthenticatorType.Server();
      authenticator.allowConnection = this.allowConnection.bind(this);
      const socketOptions = {
        authenticator,
        portOrPath: aPortOrPath,
      };
      this._listener = new lazy.SocketListener(
        lazy.DevToolsServer,
        socketOptions
      );
      this._listener.open();
      debug`USB remote debugger - listening on ${aPortOrPath}`;
    } catch (e) {
      warn`Unable to start USB debugger server: ${e}`;
    }
  }

  stop() {
    if (!this._listener) {
      return;
    }

    try {
      this._listener.close();
      this._listener = null;
    } catch (e) {
      warn`Unable to stop USB debugger server: ${e}`;
    }
  }

  allowConnection(aSession) {
    if (!this._listener) {
      return lazy.DevToolsServer.AuthenticationResult.DENY;
    }

    if (aSession.server.port) {
      return lazy.DevToolsServer.AuthenticationResult.DENY;
    }
    return lazy.DevToolsServer.AuthenticationResult.ALLOW;
  }
}
