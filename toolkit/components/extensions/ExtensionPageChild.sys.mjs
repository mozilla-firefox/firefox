/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

/**
 * This file handles privileged extension page logic that runs in the
 * child process.
 */

const CATEGORY_EXTENSION_SCRIPTS_ADDON = "webextension-scripts-addon";
const CATEGORY_EXTENSION_SCRIPTS_DEVTOOLS = "webextension-scripts-devtools";

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import {
  ChildAPIManager,
  ExtensionActivityLogChild,
  Messenger,
} from "resource://gre/modules/ExtensionChild.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionChildDevToolsUtils:
    "resource://gre/modules/ExtensionChildDevToolsUtils.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
});

const { getInnerWindowID, promiseEvent } = ExtensionUtils;

const { BaseContext, CanOfAPIs, SchemaAPIManager, redefineGetter } =
  ExtensionCommon;

const initializeBackgroundPage = context => {
  // Override the `alert()` method inside background windows;
  // we alias it to console.log().
  // See: https://bugzilla.mozilla.org/show_bug.cgi?id=1203394
  let alertDisplayedWarning = false;
  const innerWindowID = getInnerWindowID(context.contentWindow);

  /** @param {{ text, filename, lineNumber?, columnNumber? }} options */
  function logWarningMessage({ text, filename, lineNumber, columnNumber }) {
    let consoleMsg = Cc["@mozilla.org/scripterror;1"].createInstance(
      Ci.nsIScriptError
    );
    consoleMsg.initWithWindowID(
      text,
      filename,
      lineNumber,
      columnNumber,
      Ci.nsIScriptError.warningFlag,
      "webextension",
      innerWindowID
    );
    Services.console.logMessage(consoleMsg);
  }

  function ignoredSuspendListener() {
    logWarningMessage({
      text: "Background event page was not terminated on idle because a DevTools toolbox is attached to the extension.",
      filename: context.contentWindow.location.href,
    });
  }

  if (!context.extension.manifest.background.persistent) {
    context.extension.on(
      "background-script-suspend-ignored",
      ignoredSuspendListener
    );
    context.callOnClose({
      close: () => {
        context.extension.off(
          "background-script-suspend-ignored",
          ignoredSuspendListener
        );
      },
    });
  }

  let alertOverwrite = text => {
    const { filename, columnNumber, lineNumber } = Components.stack.caller;

    if (!alertDisplayedWarning) {
      context.childManager.callParentAsyncFunction(
        "runtime.openBrowserConsole",
        []
      );

      logWarningMessage({
        text: "alert() is not supported in background windows; please use console.log instead.",
        filename,
        lineNumber,
        columnNumber,
      });

      alertDisplayedWarning = true;
    }

    logWarningMessage({ text, filename, lineNumber, columnNumber });
  };
  Cu.exportFunction(alertOverwrite, context.contentWindow, {
    defineAs: "alert",
  });
};

var apiManager = new (class extends SchemaAPIManager {
  constructor() {
    super("addon", lazy.Schemas);
    this.initialized = false;
  }

  lazyInit() {
    if (!this.initialized) {
      this.initialized = true;
      this.initGlobal();
      for (let { value } of Services.catMan.enumerateCategory(
        CATEGORY_EXTENSION_SCRIPTS_ADDON
      )) {
        this.loadScript(value);
      }
    }
  }
})();

var devtoolsAPIManager = new (class extends SchemaAPIManager {
  constructor() {
    super("devtools", lazy.Schemas);
    this.initialized = false;
  }

  lazyInit() {
    if (!this.initialized) {
      this.initialized = true;
      this.initGlobal();
      for (let { value } of Services.catMan.enumerateCategory(
        CATEGORY_EXTENSION_SCRIPTS_DEVTOOLS
      )) {
        this.loadScript(value);
      }
    }
  }
})();

export function getContextChildManagerGetter(
  { envType },
  ChildAPIManagerClass = ChildAPIManager
) {
  return function () {
    let apiManager =
      envType === "devtools_parent"
        ? devtoolsAPIManager
        : this.extension.apiManager;

    apiManager.lazyInit();

    let localApis = {};
    let can = new CanOfAPIs(this, apiManager, localApis);

    let childManager = new ChildAPIManagerClass(
      this,
      this.messageManager,
      can,
      {
        envType,
        viewType: this.viewType,
        url: this.uri.spec,
        incognito: this.incognito,
        // Additional data a BaseContext subclass may optionally send
        // as part of the CreateProxyContext request sent to the main process
        // (e.g. WorkerContexChild implements this method to send the service
        // worker descriptor id along with the details send by default here).
        ...this.getCreateProxyContextData?.(),
      }
    );

    this.callOnClose(childManager);

    return childManager;
  };
}

export class ExtensionBaseContextChild extends BaseContext {
  /**
   * This ExtensionBaseContextChild represents an addon execution environment
   * that is running in an addon or devtools child process.
   *
   * @param {ExtensionChild} extension This context's owner.
   * @param {object} params
   * @param {string} params.envType One of "addon_child" or "devtools_child".
   * @param {nsIDOMWindow} params.contentWindow The window where the addon runs.
   * @param {string} params.viewType One of "background", "popup", "tab",
   *   "sidebar", "devtools_page" or "devtools_panel".
   * @param {number} [params.tabId] This tab's ID, used if viewType is "tab".
   * @param {nsIURI} [params.uri] The URI of the page.
   */
  constructor(extension, params) {
    if (!params.envType) {
      throw new Error("Missing envType");
    }

    super(params.envType, extension);
    let { viewType = "tab", uri, contentWindow, tabId } = params;
    this.viewType = viewType;
    this.uri = uri || extension.baseURI;

    this.setContentWindow(contentWindow);
    this.browsingContextId = contentWindow.docShell.browsingContext.id;

    if (viewType == "tab") {
      Object.defineProperty(this, "tabId", {
        value: tabId,
        enumerable: true,
        configurable: true,
      });
    }

    lazy.Schemas.exportLazyGetter(contentWindow, "browser", () => {
      return this.browserObj;
    });

    lazy.Schemas.exportLazyGetter(contentWindow, "chrome", () => {
      // For MV3 and later, this is just an alias for browser.
      if (extension.manifestVersion > 2) {
        return this.browserObj;
      }
      // Chrome compat is only used with MV2
      let chromeApiWrapper = Object.create(this.childManager);
      chromeApiWrapper.isChromeCompat = true;

      let chromeObj = Cu.createObjectIn(contentWindow);
      chromeApiWrapper.inject(chromeObj);
      return chromeObj;
    });
  }

  get browserObj() {
    const browserObj = Cu.createObjectIn(this.contentWindow);
    this.childManager.inject(browserObj);
    return redefineGetter(this, "browserObj", browserObj);
  }

  logActivity(type, name, data) {
    ExtensionActivityLogChild.log(this, type, name, data);
  }

  get cloneScope() {
    return this.contentWindow;
  }

  get principal() {
    return this.contentWindow.document.nodePrincipal;
  }

  get tabId() {
    // Will be overwritten in the constructor if necessary.
    return -1;
  }

  // Called when the extension shuts down.
  shutdown() {
    if (this.contentWindow) {
      this.contentWindow.close();
    }

    this.unload();
  }

  // This method is called when an extension page navigates away or
  // its tab is closed.
  unload() {
    // Note that without this guard, we end up running unload code
    // multiple times for tab pages closed by the "page-unload" handlers
    // triggered below.
    if (this.unloaded) {
      return;
    }

    super.unload();
  }

  get messenger() {
    return redefineGetter(this, "messenger", new Messenger(this));
  }

  /** @type {ReturnType<ReturnType<getContextChildManagerGetter>>} */
  get childManager() {
    throw new Error("childManager getter must be overridden");
  }
}

class ExtensionPageContextChild extends ExtensionBaseContextChild {
  /**
   * This ExtensionPageContextChild represents a privileged addon
   * execution environment that has full access to the WebExtensions
   * APIs (provided that the correct permissions have been requested).
   *
   * This is the child side of the ExtensionPageContextParent class
   * defined in ExtensionParent.sys.mjs.
   *
   * @param {ExtensionChild} extension This context's owner.
   * @param {object} params
   * @param {nsIDOMWindow} params.contentWindow The window where the addon runs.
   * @param {string} params.viewType One of "background", "popup", "sidebar" or "tab".
   *     "background", "sidebar" and "tab" are used by `browser.extension.getViews`.
   *     "popup" is only used internally to identify page action and browser
   *     action popups and options_ui pages.
   * @param {number} [params.tabId] This tab's ID, used if viewType is "tab".
   * @param {nsIURI} [params.uri] The URI of the page.
   */
  constructor(extension, params) {
    super(extension, Object.assign(params, { envType: "addon_child" }));

    if (this.viewType == "background") {
      initializeBackgroundPage(this);
    }

    this.extension.views.add(this);
  }

  unload() {
    super.unload();
    this.extension.views.delete(this);
  }

  get childManager() {
    const childManager = getContextChildManagerGetter({
      envType: "addon_parent",
    }).call(this);
    return redefineGetter(this, "childManager", childManager);
  }
}

export class DevToolsContextChild extends ExtensionBaseContextChild {
  /**
   * This DevToolsContextChild represents a devtools-related addon execution
   * environment that has access to the devtools API namespace and to the same subset
   * of APIs available in a content script execution environment.
   *
   * @param {ExtensionChild} extension This context's owner.
   * @param {object} params
   * @param {nsIDOMWindow} params.contentWindow The window where the addon runs.
   * @param {string} params.viewType One of "devtools_page" or "devtools_panel".
   * @param {object} [params.devtoolsToolboxInfo] This devtools toolbox's information,
   *   used if viewType is "devtools_page" or "devtools_panel".
   * @param {number} [params.tabId] This tab's ID, used if viewType is "tab".
   * @param {nsIURI} [params.uri] The URI of the page.
   */
  constructor(extension, params) {
    super(extension, Object.assign(params, { envType: "devtools_child" }));

    this.devtoolsToolboxInfo = params.devtoolsToolboxInfo;
    lazy.ExtensionChildDevToolsUtils.initThemeChangeObserver(
      params.devtoolsToolboxInfo.themeName,
      this
    );

    this.extension.devtoolsViews.add(this);
  }

  unload() {
    super.unload();
    this.extension.devtoolsViews.delete(this);
  }

  get childManager() {
    const childManager = getContextChildManagerGetter({
      envType: "devtools_parent",
    }).call(this);
    return redefineGetter(this, "childManager", childManager);
  }
}

export var ExtensionPageChild = {
  initialized: false,

  // Map<innerWindowId, ExtensionPageContextChild>
  extensionContexts: new Map(),

  apiManager,

  _init() {
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    Services.obs.addObserver(this, "inner-window-destroyed"); // eslint-ignore-line mozilla/balanced-listeners
  },

  observe(subject, topic) {
    if (topic === "inner-window-destroyed") {
      let windowId = subject.QueryInterface(Ci.nsISupportsPRUint64).data;

      this.destroyExtensionContext(windowId);
    }
  },

  expectViewLoad(global, viewType) {
    promiseEvent(
      global,
      "DOMContentLoaded",
      true,
      /** @param {{target: Window|any}} event */
      event =>
        event.target.location != "about:blank" &&
        // Ignore DOMContentLoaded bubbled from child frames:
        event.target.defaultView === global.content
    ).then(() => {
      let windowId = getInnerWindowID(global.content);
      let context = this.extensionContexts.get(windowId);
      // This initializes ChildAPIManager (and creation of ProxyContextParent)
      // if they don't exist already at this point.
      let childId = context?.childManager.id;
      if (viewType === "background") {
        global.sendAsyncMessage("Extension:BackgroundViewLoaded", { childId });
      }
    });
  },

  /**
   * Create a privileged context at initial-document-element-inserted.
   *
   * @param {ExtensionChild} extension
   *     The extension for which the context should be created.
   * @param {nsIDOMWindow} contentWindow The global of the page.
   */
  initExtensionContext(extension, contentWindow) {
    this._init();

    if (!WebExtensionPolicy.isExtensionProcess) {
      throw new Error(
        "Cannot create an extension page context in current process"
      );
    }

    let windowId = getInnerWindowID(contentWindow);
    let context = this.extensionContexts.get(windowId);
    if (context) {
      if (context.extension !== extension) {
        throw new Error(
          "A different extension context already exists for this frame"
        );
      }
      throw new Error(
        "An extension context was already initialized for this frame"
      );
    }

    let uri = contentWindow.document.documentURIObject;

    let mm = contentWindow.docShell.messageManager;
    let data = mm.sendSyncMessage("Extension:GetFrameData")[0];
    if (!data) {
      let policy = WebExtensionPolicy.getByHostname(uri.host);
      // TODO bug 1749116: Handle this unexpected result, because data
      // (viewType in particular) should never be void for extension documents.
      Cu.reportError(`FrameData missing for ${policy?.id} page ${uri.spec}`);
    }
    let { viewType, tabId, devtoolsToolboxInfo } = data ?? {};

    if (viewType && contentWindow.top === contentWindow) {
      ExtensionPageChild.expectViewLoad(mm, viewType);
    }

    if (devtoolsToolboxInfo) {
      context = new DevToolsContextChild(extension, {
        viewType,
        contentWindow,
        uri,
        tabId,
        devtoolsToolboxInfo,
      });
    } else {
      context = new ExtensionPageContextChild(extension, {
        viewType,
        contentWindow,
        uri,
        tabId,
      });
    }

    this.extensionContexts.set(windowId, context);
  },

  /**
   * Close the ExtensionPageContextChild belonging to the given window, if any.
   *
   * @param {number} windowId The inner window ID of the destroyed context.
   */
  destroyExtensionContext(windowId) {
    let context = this.extensionContexts.get(windowId);
    if (context) {
      context.unload();
      this.extensionContexts.delete(windowId);
    }
  },

  shutdownExtension(extensionId) {
    for (let [windowId, context] of this.extensionContexts) {
      if (context.extension.id == extensionId) {
        context.shutdown();
        this.extensionContexts.delete(windowId);
      }
    }
  },
};
