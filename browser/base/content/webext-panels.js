/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Via webext-panels.xhtml
/* import-globals-from browser.js */
/* global windowRoot */

ChromeUtils.defineESModuleGetters(this, {
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
});

const { ExtensionUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionUtils.sys.mjs"
);

var { promiseEvent } = ExtensionUtils;

function getBrowser(panel) {
  let browser = document.getElementById("webext-panels-browser");
  if (browser) {
    return Promise.resolve(browser);
  }

  if (panel.viewType === "sidebar" && gSidebarRevampEnabled) {
    if (!customElements.get("sidebar-panel-header")) {
      ChromeUtils.importESModule(
        "chrome://browser/content/sidebar/sidebar-panel-header.mjs",
        { global: "current" }
      );
    }
    const heading =
      panel.extension.manifest.sidebar_action.default_title ??
      panel.extension.name;
    document.getElementById("sidebar-panel-header").heading = heading;
  }

  let stack = document.getElementById("webext-panels-stack");
  if (!stack) {
    stack = document.createXULElement("stack");
    stack.setAttribute("flex", "1");
    stack.setAttribute("id", "webext-panels-stack");
    document.documentElement.appendChild(stack);
  }

  browser = document.createXULElement("browser");
  browser.setAttribute("id", "webext-panels-browser");
  browser.setAttribute("type", "content");
  browser.setAttribute("flex", "1");
  browser.setAttribute("disableglobalhistory", "true");
  browser.setAttribute("messagemanagergroup", "webext-browsers");
  browser.setAttribute("webextension-view-type", panel.viewType);
  browser.setAttribute("context", "contentAreaContextMenu");
  browser.setAttribute("tooltip", "aHTMLTooltip");
  browser.setAttribute("autocompletepopup", "PopupAutoComplete");

  if (gAllowTransparentBrowser) {
    browser.setAttribute("transparent", "true");
  }

  // Ensure that the browser is going to run in the same bc group as the other
  // extension pages from the same addon.
  browser.setAttribute(
    "initialBrowsingContextGroupId",
    panel.extension.policy.browsingContextGroupId
  );

  let readyPromise;
  if (panel.extension.remote) {
    browser.setAttribute("remote", "true");
    let oa = E10SUtils.predictOriginAttributes({ browser });
    browser.setAttribute(
      "remoteType",
      E10SUtils.getRemoteTypeForURI(
        panel.uri,
        /* remote */ true,
        /* fission */ false,
        E10SUtils.EXTENSION_REMOTE_TYPE,
        null,
        oa
      )
    );
    browser.setAttribute("maychangeremoteness", "true");

    readyPromise = promiseEvent(browser, "XULFrameLoaderCreated");
  } else {
    readyPromise = Promise.resolve();
  }

  stack.appendChild(browser);

  browser.addEventListener(
    "DoZoomEnlarge",
    () => {
      let { ZoomManager } = browser.documentGlobal;
      let zoom = browser.fullZoom;
      zoom += 0.1;
      if (zoom > ZoomManager.MAX) {
        zoom = ZoomManager.MAX;
      }
      browser.fullZoom = zoom;
    },
    true
  );
  browser.addEventListener(
    "DoZoomReduce",
    () => {
      let { ZoomManager } = browser.documentGlobal;
      let zoom = browser.fullZoom;
      zoom -= 0.1;
      if (zoom < ZoomManager.MIN) {
        zoom = ZoomManager.MIN;
      }
      browser.fullZoom = zoom;
    },
    true
  );
  browser.addEventListener("DOMWindowClose", event => {
    if (panel.viewType == "sidebar") {
      windowRoot.window.SidebarController.hide();
    }
    // Prevent DOMWindowClose events originated from
    // extensions sidebar and devtools panels to bubble up
    // to the gBrowser DOMWindowClose listener and
    // be mistaken as being originated from a tab being closed
    // (See Bug 1926373)
    event.stopPropagation();
  });

  const initBrowser = () => {
    ExtensionParent.apiManager.emit(
      "extension-browser-inserted",
      browser,
      panel.browserInsertedData
    );

    browser.messageManager.loadFrameScript(
      "chrome://extensions/content/ext-browser-content.js",
      false,
      true
    );

    let options = {};
    if (panel.browserStyle) {
      options.stylesheets = ["chrome://browser/content/extension.css"];
    }
    browser.messageManager.sendAsyncMessage("Extension:InitBrowser", options);
    return browser;
  };

  browser.addEventListener("DidChangeBrowserRemoteness", initBrowser);
  return readyPromise.then(initBrowser);
}

// Stub tabbrowser implementation to make sure that links from inside
// extension sidebar panels open in new tabs, see bug 1488055.
var gBrowser = {
  get selectedBrowser() {
    return document.getElementById("webext-panels-browser");
  },

  getTabForBrowser() {
    return null;
  },
};

function updatePosition() {
  // We need both of these to make sure we update the position
  // after any lower level updates have finished.
  requestAnimationFrame(() =>
    setTimeout(() => {
      let browser = document.getElementById("webext-panels-browser");
      if (browser && browser.isRemoteBrowser) {
        browser.frameLoader.requestUpdatePosition();
      }
    }, 0)
  );
}

function loadPanel(extensionId, extensionUrl, browserStyle) {
  let browserEl = document.getElementById("webext-panels-browser");
  if (browserEl) {
    if (browserEl.currentURI.spec === extensionUrl) {
      return;
    }
    // Forces runtime disconnect.  Remove the stack (parent).
    browserEl.parentNode.remove();
  }

  let policy = WebExtensionPolicy.getByID(extensionId);

  let sidebar = {
    uri: extensionUrl,
    extension: policy.extension,
    browserStyle,
    viewType: "sidebar",
  };

  getBrowser(sidebar).then(browser => {
    let uri = Services.io.newURI(policy.getURL());
    let triggeringPrincipal =
      Services.scriptSecurityManager.createContentPrincipal(uri, {});
    browser.fixupAndLoadURIString(extensionUrl, { triggeringPrincipal });
  });
}

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gAllowTransparentBrowser",
  "browser.tabs.allow_transparent_browser",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "gSidebarRevampEnabled",
  "sidebar.revamp",
  false
);
