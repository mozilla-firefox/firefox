/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { promiseEvent } from "../aboutaddons-utils.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
});

class InlineOptionsBrowser extends HTMLElement {
  constructor() {
    super();
    // Force the options_ui remote browser to recompute window.mozInnerScreenX
    // and window.mozInnerScreenY when the "addon details" page has been
    // scrolled (See Bug 1390445 for rationale).
    // Also force a repaint to fix an issue where the click location was
    // getting out of sync (see bug 1548687).
    this.updatePositionTask = new lazy.DeferredTask(() => {
      if (this.browser && this.browser.isRemoteBrowser) {
        // Select boxes can appear in the wrong spot after scrolling, this will
        // clear that up. Bug 1390445.
        this.browser.frameLoader.requestUpdatePosition();
      }
    }, 100);

    this._embedderElement = null;
    this._promiseDisconnected = new Promise(
      resolve => (this._resolveDisconnected = resolve)
    );
  }

  connectedCallback() {
    window.addEventListener("scroll", this, true);
    const { embedderElement } = top.browsingContext;
    this._embedderElement = embedderElement;
    embedderElement.addEventListener("FullZoomChange", this);
    embedderElement.addEventListener("TextZoomChange", this);
  }

  disconnectedCallback() {
    this._resolveDisconnected();
    window.removeEventListener("scroll", this, true);
    this._embedderElement?.removeEventListener("FullZoomChange", this);
    this._embedderElement?.removeEventListener("TextZoomChange", this);
    this._embedderElement = null;
  }

  handleEvent(e) {
    switch (e.type) {
      case "scroll":
        return this.updatePositionTask.arm();
      case "FullZoomChange":
      case "TextZoomChange":
        return this.maybeUpdateZoom();
    }
    return undefined;
  }

  maybeUpdateZoom() {
    let bc = this.browser?.browsingContext;
    let topBc = top.browsingContext;
    if (!bc || !topBc) {
      return;
    }
    // Use the same full-zoom as our top window.
    bc.fullZoom = topBc.fullZoom;
    bc.textZoom = topBc.textZoom;
  }

  setAddon(addon) {
    this.addon = addon;
  }

  destroyBrowser() {
    this.textContent = "";
  }

  ensureBrowserCreated() {
    if (this.childElementCount === 0) {
      this.render();
    }
  }

  async render() {
    let { addon } = this;
    if (!addon) {
      throw new Error("addon required to create inline options");
    }

    let browser = document.createXULElement("browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("messagemanagergroup", "webext-browsers");
    browser.setAttribute("id", "addon-inline-options");
    browser.setAttribute("class", "addon-inline-options");
    browser.setAttribute("transparent", "true");
    browser.setAttribute("forcemessagemanager", "true");
    // autocompletepopup="PopupAutoComplete" is the intended effect, except it
    // would trigger a lookup in the current (about:addons) document, while the
    // PopupAutoComplete element is defined in the browser window. Override the
    // getter to return the desired element from the browser window instead.
    Object.defineProperty(browser, "autoCompletePopup", {
      enumerable: true,
      configurable: true,
      get() {
        return this.browsingContext.topChromeWindow.document.getElementById(
          "PopupAutoComplete"
        );
      },
    });

    let { optionsURL, optionsBrowserStyle } = addon;
    if (addon.isWebExtension) {
      let policy = lazy.ExtensionParent.WebExtensionPolicy.getByID(addon.id);
      browser.setAttribute(
        "initialBrowsingContextGroupId",
        policy.browsingContextGroupId
      );
    }

    let readyPromise;
    let remoteSubframes = window.docShell.QueryInterface(
      Ci.nsILoadContext
    ).useRemoteSubframes;
    // For now originAttributes have no effect, which will change if the
    // optionsURL becomes anything but moz-extension* or we start considering
    // OA for extensions.
    var oa = lazy.E10SUtils.predictOriginAttributes({ browser });
    let loadRemote = lazy.E10SUtils.canLoadURIInRemoteType(
      optionsURL,
      remoteSubframes,
      lazy.E10SUtils.EXTENSION_REMOTE_TYPE,
      oa
    );
    if (loadRemote) {
      browser.setAttribute("remote", "true");
      browser.setAttribute("remoteType", lazy.E10SUtils.EXTENSION_REMOTE_TYPE);

      readyPromise = promiseEvent("XULFrameLoaderCreated", browser);
    } else {
      readyPromise = promiseEvent("load", browser, true);
    }

    this.appendChild(browser);
    this.browser = browser;

    // Force bindings to apply synchronously.
    browser.clientTop;

    await readyPromise;

    this.maybeUpdateZoom();

    if (!browser.messageManager) {
      // If the browser.messageManager is undefined, the browser element has
      // been removed from the document in the meantime (e.g. due to a rapid
      // sequence of addon reload), return null.
      return;
    }

    lazy.ExtensionParent.apiManager.emit("extension-browser-inserted", browser);

    await new Promise(resolve => {
      let messageListener = {
        receiveMessage({ name, data }) {
          if (name === "Extension:BrowserResized") {
            browser.style.height = `${data.height}px`;
          } else if (name === "Extension:BrowserContentLoaded") {
            resolve();
          }
        },
      };

      let mm = browser.messageManager;

      if (!mm) {
        // If the browser.messageManager is undefined, the browser element has
        // been removed from the document in the meantime (e.g. due to a rapid
        // sequence of addon reload), return null.
        resolve();
        return;
      }

      mm.loadFrameScript(
        "chrome://extensions/content/ext-browser-content.js",
        false,
        true
      );
      mm.addMessageListener("Extension:BrowserContentLoaded", messageListener);
      mm.addMessageListener("Extension:BrowserResized", messageListener);

      let browserOptions = {
        fixedWidth: true,
        isInline: true,
      };

      if (optionsBrowserStyle) {
        // aboutaddons.js is not used on Android. extension.css is included in
        // Firefox desktop and Thunderbird.
        // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
        browserOptions.stylesheets = ["chrome://browser/content/extension.css"];
      }

      mm.sendAsyncMessage("Extension:InitBrowser", browserOptions);

      if (browser.isConnectedAndReady) {
        this.fixupAndLoadURIString(optionsURL);
      } else {
        // browser custom element does opt-in the delayConnectedCallback
        // behavior (see connectedCallback in the custom element definition
        // from browser-custom-element.mjs) and so calling browser.loadURI
        // would fail if the about:addons document is not yet fully loaded.
        Promise.race([
          promiseEvent("DOMContentLoaded", document),
          this._promiseDisconnected,
        ]).then(() => {
          this.fixupAndLoadURIString(optionsURL);
        });
      }
    });
  }

  fixupAndLoadURIString(uriString) {
    if (!this.browser || !this.browser.isConnectedAndReady) {
      throw new Error("Fail to loadURI");
    }

    this.browser.fixupAndLoadURIString(uriString, {
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });
  }
}
customElements.define("inline-options-browser", InlineOptionsBrowser);
