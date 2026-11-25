/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExtensionParent: "resource://gre/modules/ExtensionParent.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",

  // Delay defaults to 500 ms via modules/libpref/init/all.js, for all builds:
  delayBeforeEnablingButtons: { pref: "security.notification_enable_delay" },
});

var { DefaultWeakMap, promiseEvent } = ExtensionUtils;

const { makeWidgetId } = ExtensionCommon;

const POPUP_LOAD_TIMEOUT_MS = 200;

function promisePopupShown(popup) {
  return new Promise(resolve => {
    if (popup.state == "open") {
      resolve();
    } else {
      popup.addEventListener(
        "popupshown",
        function () {
          resolve();
        },
        { once: true }
      );
    }
  });
}

function addPanelHidingHandler(panel) {
  function handleClick(event) {
    if (
      event.target.closest(
        "panel:not(#unified-extensions-panel),#notifications-toolbar"
      )
    ) {
      event.preventDefault();
      event.stopImmediatePropagation();
      Services.console.logStringMessage(
        "Ignored click shortly after extension popup was closed"
      );
    }
  }
  panel.addEventListener(
    "popuphiding",
    () => {
      const window = panel.documentGlobal;
      window.addEventListener("click", handleClick, true);
      window.setTimeout(() => {
        window.removeEventListener("click", handleClick, true);
      }, lazy.delayBeforeEnablingButtons);
    },
    { once: true, capture: true }
  );
}

const REMOTE_PANEL_ID = "webextension-remote-preload-panel";

export class BasePopup {
  constructor(
    extension,
    viewNode,
    popupURL,
    browserStyle,
    fixedWidth = false,
    blockParser = false
  ) {
    this.extension = extension;
    this.popupURL = popupURL;
    this.viewNode = viewNode;
    this.browserStyle = browserStyle;
    this.window = viewNode.documentGlobal;
    this.destroyed = false;
    this.fixedWidth = fixedWidth;
    this.blockParser = blockParser;

    extension.callOnClose(this);

    this.contentReady = new Promise(resolve => {
      this._resolveContentReady = resolve;
    });

    this.window.addEventListener("unload", this);
    this.viewNode.addEventListener(this.DESTROY_EVENT, this);
    this.panel.addEventListener("popuppositioned", this, {
      once: true,
      capture: true,
    });

    this.browser = null;
    this.browserLoaded = new Promise((resolve, reject) => {
      this.browserLoadedDeferred = { resolve, reject };
    });
    this.browserReady = this.createBrowser(viewNode, popupURL);

    BasePopup.instances.get(this.window).set(extension, this);
  }

  static for(extension, window) {
    return BasePopup.instances.get(window).get(extension);
  }

  close() {
    this.closePopup();
  }

  destroy() {
    this.extension.forgetOnClose(this);

    this.window.removeEventListener("unload", this);

    this.destroyed = true;
    this.browserLoadedDeferred.reject(new Error("Popup destroyed"));
    // Ignore unhandled rejections if the "attach" method is not called.
    this.browserLoaded.catch(() => {});
    // Whoever is waiting to show this popup has to be able to resume and see
    // that it is destroyed, even if the content never got as far as reporting
    // its size.
    this._resolveContentReady();

    BasePopup.instances.get(this.window).delete(this.extension);

    return this.browserReady.then(() => {
      if (this.browser) {
        this.destroyBrowser(this.browser, true);
        this.browser.parentNode.remove();
      }
      if (this.stack) {
        this.stack.remove();
      }

      if (this.viewNode) {
        this.viewNode.removeEventListener(this.DESTROY_EVENT, this);
        delete this.viewNode.customRectGetter;
      }

      let { panel } = this;
      if (panel) {
        panel.removeEventListener("popuppositioned", this, { capture: true });
      }
      if (panel && panel.id !== REMOTE_PANEL_ID) {
        panel.style.removeProperty("--panel-background-color");
        panel.style.removeProperty("--panel-border-color");
        panel.removeAttribute("remote");
      }

      this.browser = null;
      this.stack = null;
      this.viewNode = null;
    });
  }

  destroyBrowser(browser, finalize = false) {
    let mm = browser.messageManager;
    // If the browser has already been removed from the document, because the
    // popup was closed externally, there will be no message manager here, so
    // just replace our receiveMessage method with a stub.
    if (mm) {
      mm.removeMessageListener("Extension:BrowserBackgroundChanged", this);
      mm.removeMessageListener("Extension:BrowserContentLoaded", this);
      mm.removeMessageListener("Extension:BrowserResized", this);
    } else if (finalize) {
      this.receiveMessage = () => {};
    }
    browser.removeEventListener("pagetitlechanged", this);
    browser.removeEventListener("DOMWindowClose", this);
    browser.removeEventListener("DoZoomEnlarge", this);
    browser.removeEventListener("DoZoomReduce", this);
  }

  // Returns the name of the event fired on `viewNode` when the popup is being
  // destroyed. This must be implemented by every subclass.
  get DESTROY_EVENT() {
    throw new Error("Not implemented");
  }

  get STYLESHEETS() {
    let sheets = [];

    if (this.browserStyle) {
      sheets.push("chrome://browser/content/extension.css");
    }
    if (!this.fixedWidth) {
      sheets.push("chrome://browser/content/extension-popup-panel.css");
    }

    return sheets;
  }

  get panel() {
    let panel = this.viewNode;
    while (panel && panel.localName != "panel") {
      panel = panel.parentNode;
    }
    return panel;
  }

  receiveMessage({ name, data }) {
    switch (name) {
      case "Extension:BrowserBackgroundChanged":
        this.setBackground(data.background);
        break;

      case "Extension:BrowserContentLoaded":
        this.browserLoadedDeferred.resolve();
        break;

      case "Extension:BrowserResized":
        this._resolveContentReady();
        if (this.ignoreResizes) {
          this.dimensions = data;
        } else {
          this.resizeBrowser(data);
        }
        break;
    }
  }

  handleEvent(event) {
    switch (event.type) {
      case "unload":
      case this.DESTROY_EVENT:
        if (!this.destroyed) {
          this.destroy();
        }
        break;
      case "popuppositioned":
        if (!this.destroyed) {
          this.browserLoaded
            .then(() => {
              if (this.destroyed) {
                return;
              }
              // Wait the reflow before asking the popup panel to grab the focus, otherwise
              // `nsFocusManager::SetFocus` may ignore out request because the panel view
              // visibility is still set to `ViewVisibility::Hide` (waiting the document
              // to be fully flushed makes us sure that when the popup panel grabs the focus
              // nsMenuPopupFrame::LayoutPopup has already been colled and set the frame
              // visibility to `ViewVisibility::Show`).
              return this.browser.documentGlobal.promiseDocumentFlushed(
                () => {}
              );
            })
            .then(() => {
              if (this.destroyed) {
                return;
              }
              // The window was deactivated before the popup finished opening,
              // so nothing will roll it up. Grabbing the focus would raise the
              // window back. Keep the popup open if auto-hide is disabled for
              // debugging.
              if (Services.focus.activeWindow != this.window) {
                if (!Services.prefs.getBoolPref("ui.popup.disable_autohide")) {
                  this.closePopup();
                }
                return;
              }
              this.browser.messageManager.sendAsyncMessage(
                "Extension:GrabFocus",
                {}
              );
            })
            .catch(() => {
              // If the panel closes too fast an exception is raised here and tests will fail.
            });
        }
        break;

      case "pagetitlechanged":
        this.viewNode.setAttribute("aria-label", this.browser.contentTitle);
        break;

      case "DOMWindowClose":
        this.closePopup();
        break;

      case "DoZoomEnlarge": {
        const browser = event.target;
        let { ZoomManager } = browser.documentGlobal;
        let zoom = this.browser.fullZoom;
        zoom += 0.1;
        if (zoom > ZoomManager.MAX) {
          zoom = ZoomManager.MAX;
        }
        browser.fullZoom = zoom;
        break;
      }

      case "DoZoomReduce": {
        const browser = event.target;
        let { ZoomManager } = browser.documentGlobal;
        let zoom = browser.fullZoom;
        zoom -= 0.1;
        if (zoom < ZoomManager.MIN) {
          zoom = ZoomManager.MIN;
        }
        browser.fullZoom = zoom;
        break;
      }
    }
  }

  createBrowser(viewNode, popupURL = null) {
    let document = viewNode.ownerDocument;

    let stack = document.createXULElement("stack");
    stack.setAttribute("class", "webextension-popup-stack");

    let browser = document.createXULElement("browser");
    browser.setAttribute("type", "content");
    browser.setAttribute("disableglobalhistory", "true");
    browser.setAttribute("messagemanagergroup", "webext-browsers");
    browser.setAttribute("class", "webextension-popup-browser");
    browser.setAttribute("webextension-view-type", "popup");
    browser.setAttribute("tooltip", "aHTMLTooltip");
    browser.setAttribute("contextmenu", "contentAreaContextMenu");
    browser.setAttribute("autocompletepopup", "PopupAutoComplete");
    browser.setAttribute("constrainpopups", "false");

    // Ensure the browser will initially load in the same group as other
    // browsers from the same extension.
    browser.setAttribute(
      "initialBrowsingContextGroupId",
      this.extension.policy.browsingContextGroupId
    );

    if (this.extension.remote) {
      browser.setAttribute("remote", "true");
      browser.setAttribute("remoteType", this.extension.remoteType);
      browser.setAttribute("maychangeremoteness", "true");
    }

    // We only need flex sizing for the sake of the slide-in sub-views of the
    // main menu panel, so that the browser occupies the full width of the view,
    // and also takes up any extra height that's available to it.
    browser.setAttribute("flex", "1");
    stack.setAttribute("flex", "1");

    // Note: When using noautohide panels, the popup manager will add width and
    // height attributes to the panel, breaking our resize code, if the browser
    // starts out smaller than 30px by 10px. This isn't an issue now, but it
    // will be if and when we popup debugging.

    this.browser = browser;
    this.stack = stack;

    let readyPromise;
    if (this.extension.remote) {
      readyPromise = promiseEvent(browser, "XULFrameLoaderCreated");
    } else {
      readyPromise = promiseEvent(browser, "load");
    }

    stack.appendChild(browser);
    viewNode.appendChild(stack);

    if (!this.extension.remote) {
      // FIXME: bug 1494029 - this code used to rely on the browser binding
      // accessing browser.contentWindow. This is a stopgap to continue doing
      // that, but we should get rid of it in the long term.
      browser.contentWindow; // eslint-disable-line no-unused-expressions
    }

    let setupBrowser = browser => {
      let mm = browser.messageManager;
      mm.addMessageListener("Extension:BrowserBackgroundChanged", this);
      mm.addMessageListener("Extension:BrowserContentLoaded", this);
      mm.addMessageListener("Extension:BrowserResized", this);
      browser.addEventListener("pagetitlechanged", this);
      browser.addEventListener("DOMWindowClose", this);
      browser.addEventListener("DoZoomEnlarge", this, true); // eslint-disable-line mozilla/balanced-listeners
      browser.addEventListener("DoZoomReduce", this, true); // eslint-disable-line mozilla/balanced-listeners

      lazy.ExtensionParent.apiManager.emit(
        "extension-browser-inserted",
        browser
      );
      return browser;
    };

    const initBrowser = () => {
      setupBrowser(browser);
      let mm = browser.messageManager;

      mm.loadFrameScript(
        "chrome://extensions/content/ext-browser-content.js",
        false,
        true
      );

      mm.sendAsyncMessage("Extension:InitBrowser", {
        allowScriptsToClose: true,
        blockParser: this.blockParser,
        fixedWidth: this.fixedWidth,
        maxWidth: 800,
        maxHeight: 600,
        stylesheets: this.STYLESHEETS,
      });
    };

    browser.addEventListener("DidChangeBrowserRemoteness", initBrowser); // eslint-disable-line mozilla/balanced-listeners

    if (!popupURL) {
      // For remote browsers, we can't do any setup until the frame loader is
      // created. Non-remote browsers get a message manager immediately, so
      // there's no need to wait for the load event.
      if (this.extension.remote) {
        return readyPromise.then(() => setupBrowser(browser));
      }
      return setupBrowser(browser);
    }

    return readyPromise.then(() => {
      initBrowser();
      browser.fixupAndLoadURIString(popupURL, {
        triggeringPrincipal: this.extension.principal,
      });
    });
  }

  unblockParser() {
    this.browserReady.then(() => {
      if (this.destroyed) {
        return;
      }
      // Only block the parser for the preloaded browser, initBrowser will be
      // called again when the browserAction popup is navigated and we should
      // not block the parser in that case, otherwise the navigating the popup
      // to another extension page will never complete and the popup will
      // stay stuck on the previous extension page. See Bug 1747813.
      this.blockParser = false;
      this.browser.messageManager.sendAsyncMessage("Extension:UnblockParser");
    });
  }

  resizeBrowser({ width, height, detail }) {
    if (this.fixedWidth) {
      // Figure out how much extra space we have on the side of the panel
      // opposite the arrow.
      let side = this.panel.getAttribute("side") == "top" ? "bottom" : "top";
      let maxHeight = this.viewHeight + this.extraHeight[side];

      height = Math.min(height, maxHeight);
      this.browser.style.height = `${height}px`;

      // Used by the panelmultiview code to figure out sizing without reparenting
      // (which would destroy the browser and break us).
      this.lastCalculatedInViewHeight = Math.max(height, this.viewHeight);
    } else {
      this.browser.style.width = `${width}px`;
      this.browser.style.minWidth = `${width}px`;
      this.browser.style.height = `${height}px`;
      this.browser.style.minHeight = `${height}px`;
    }

    let event = new this.window.CustomEvent("WebExtPopupResized", { detail });
    this.browser.dispatchEvent(event);
  }

  setBackground(background) {
    // Panels inherit the applied theme (light, dark, etc) and there is a high
    // likelihood that most extension authors will not have tested with a dark theme.
    // If they have not set a background-color, we force it to white to ensure visibility
    // of the extension content. Passing `null` should be treated the same as no argument,
    // which is why we can't use default parameters here.
    if (!background) {
      background = "#fff";
    }
    if (this.panel.id != "widget-overflow") {
      this.panel.style.setProperty("--panel-background-color", background);
    }
    if (background == "#fff") {
      // Set a usable default color that work with the default background-color.
      this.panel.style.setProperty(
        "--panel-border-color",
        "hsla(210,4%,10%,.15)"
      );
    }
    this.background = background;
  }
}

/**
 * A map of active popups for a given browser window.
 *
 * WeakMap[window -> WeakMap[Extension -> BasePopup]]
 */
BasePopup.instances = new DefaultWeakMap(() => new WeakMap());

export class PanelPopup extends BasePopup {
  constructor(extension, document, popupURL, browserStyle) {
    let panel = document.createXULElement("panel");
    panel.setAttribute("id", makeWidgetId(extension.id) + "-panel");
    panel.setAttribute("class", "browser-extension-panel panel-no-padding");
    panel.setAttribute("tabspecific", "true");
    panel.setAttribute("type", "arrow");
    panel.setAttribute("role", "group");
    if (extension.remote) {
      panel.setAttribute("remote", "true");
    }
    panel.setAttribute("neverhidden", "true");

    document.getElementById("mainPopupSet").appendChild(panel);

    panel.addEventListener(
      "popupshowing",
      () => {
        let event = new this.window.CustomEvent("WebExtPopupLoaded", {
          bubbles: true,
          detail: { extension },
        });
        this.browser.dispatchEvent(event);
      },
      { once: true }
    );
    addPanelHidingHandler(panel);

    super(extension, panel, popupURL, browserStyle);
  }

  get DESTROY_EVENT() {
    return "popuphidden";
  }

  destroy() {
    super.destroy();
    this.viewNode.remove();
    this.viewNode = null;
  }

  closePopup() {
    if (this.viewNode.state == "closed") {
      // The popup has not started opening, so there is no popupshown event
      // coming and nothing will ever hide it: tear it down instead, and let
      // whoever was about to show it notice that it is destroyed.
      this.destroy();
      return;
    }
    promisePopupShown(this.viewNode).then(() => {
      // Make sure we're not already destroyed, or removed from the DOM.
      if (this.viewNode && this.viewNode.hidePopup) {
        this.viewNode.hidePopup();
      }
    });
  }
}

export class ViewPopup extends BasePopup {
  constructor(
    extension,
    window,
    popupURL,
    browserStyle,
    fixedWidth,
    blockParser
  ) {
    let document = window.document;

    let createPanel = remote => {
      let panel = document.createXULElement("panel");
      panel.setAttribute("type", "arrow");
      if (remote) {
        panel.setAttribute("remote", "true");
      }
      panel.setAttribute("neverhidden", "true");

      document.getElementById("mainPopupSet").appendChild(panel);
      return panel;
    };

    // Create a temporary panel to hold the browser while it pre-loads its
    // content. This panel will never be shown, but the browser's docShell will
    // be swapped with the browser in the real panel when it's ready. For remote
    // extensions, this popup is shared between all extensions.
    let panel;
    if (extension.remote) {
      panel = document.getElementById(REMOTE_PANEL_ID);
      if (!panel) {
        panel = createPanel(true);
        panel.id = REMOTE_PANEL_ID;
      }
    } else {
      panel = createPanel();
    }

    super(extension, panel, popupURL, browserStyle, fixedWidth, blockParser);

    this.ignoreResizes = true;

    this.attached = false;
    this.shown = false;
    this.tempPanel = panel;
    this.tempBrowser = this.browser;

    // NOTE: this class is added to the preload browser and never removed because
    // the preload browser is then switched with a new browser once we are about to
    // make the popup visible (this class is not actually used anywhere but it may
    // be useful to keep it around to be able to identify the preload buffer while
    // investigating issues).
    this.browser.classList.add("webextension-preload-browser");
  }

  /**
   * Attaches the pre-loaded browser to the given view node, and reserves a
   * promise which resolves when the browser is ready.
   *
   * @param {Element} viewNode
   *        The node to attach the browser to.
   * @returns {Promise<boolean>}
   *        Resolves when the browser is ready. Resolves to `false` if the
   *        browser was destroyed before it was fully loaded, and the popup
   *        should be closed, or `true` otherwise.
   */
  async attach(viewNode) {
    if (this.destroyed) {
      return false;
    }
    this.viewNode.removeEventListener(this.DESTROY_EVENT, this);
    this.panel.removeEventListener("popuppositioned", this, {
      once: true,
      capture: true,
    });

    this.viewNode = viewNode;
    this.viewNode.addEventListener(this.DESTROY_EVENT, this);
    this.viewNode.setAttribute("closemenu", "none");

    this.panel.addEventListener("popuppositioned", this, {
      once: true,
      capture: true,
    });
    addPanelHidingHandler(this.panel);
    if (this.extension.remote) {
      this.panel.setAttribute("remote", "true");
    }

    // Wait until the browser element is fully initialized, and give it at least
    // a short grace period to finish loading its initial content, if necessary.
    //
    // In practice, the browser that was created by the mousdown handler should
    // nearly always be ready by this point.
    await Promise.all([
      this.browserReady,
      Promise.race([
        // This promise may be rejected if the popup calls window.close()
        // before it has fully loaded.
        this.browserLoaded.catch(() => {}),
        new Promise(resolve => lazy.setTimeout(resolve, POPUP_LOAD_TIMEOUT_MS)),
      ]),
    ]);

    const { panel } = this;

    if (!this.destroyed && !panel) {
      this.destroy();
    }

    if (this.destroyed) {
      lazy.CustomizableUI.hidePanelForNode(viewNode);
      return false;
    }

    this.attached = true;

    this.setBackground(this.background);

    let flushPromise = this.window.promiseDocumentFlushed(() => {
      let win = this.window;

      // Calculate the extra height available on the screen above and below the
      // menu panel. Use that to calculate the how much the sub-view may grow.
      let popupRect = panel.getBoundingClientRect();
      let screenBottom = win.screen.availTop + win.screen.availHeight;
      let popupBottom = win.mozInnerScreenY + popupRect.bottom;
      let popupTop = win.mozInnerScreenY + popupRect.top;

      // Store the initial height of the view, so that we never resize menu panel
      // sub-views smaller than the initial height of the menu.
      this.viewHeight = viewNode.getBoundingClientRect().height;

      this.extraHeight = {
        bottom: Math.max(0, screenBottom - popupBottom),
        top: Math.max(0, popupTop - win.screen.availTop),
      };
    });

    // Create a new browser in the real popup.
    let browser = this.browser;
    await this.createBrowser(this.viewNode);

    this.browser.swapDocShells(browser);
    this.destroyBrowser(browser);

    await flushPromise;

    // Check if the popup has been destroyed while we were waiting for the
    // document flush promise to be resolve.
    if (this.destroyed) {
      this.closePopup();
      this.destroy();
      return false;
    }

    if (this.dimensions) {
      if (this.fixedWidth) {
        delete this.dimensions.width;
      }
      this.resizeBrowser(this.dimensions);
    }

    this.ignoreResizes = false;

    this.viewNode.customRectGetter = () => {
      return { height: this.lastCalculatedInViewHeight || this.viewHeight };
    };

    this.removeTempPanel();

    this.shown = true;

    if (this.destroyed) {
      this.closePopup();
      this.destroy();
      return false;
    }

    let event = new this.window.CustomEvent("WebExtPopupLoaded", {
      bubbles: true,
      detail: { extension: this.extension },
    });
    this.browser.dispatchEvent(event);

    return true;
  }

  removeTempPanel() {
    if (this.tempPanel) {
      if (this.tempPanel.id !== REMOTE_PANEL_ID) {
        this.tempPanel.remove();
      }
      this.tempPanel = null;
    }
    if (this.tempBrowser) {
      this.tempBrowser.parentNode.remove();
      this.tempBrowser = null;
    }
  }

  destroy() {
    return super.destroy().then(() => {
      this.removeTempPanel();
    });
  }

  get DESTROY_EVENT() {
    return "ViewHiding";
  }

  closePopup() {
    if (this.shown) {
      lazy.CustomizableUI.hidePanelForNode(this.viewNode);
    } else if (this.attached) {
      this.destroyed = true;
    } else {
      this.destroy();
    }
  }
}

// Checks whether there is anything preventing a panel from being opened via
// action.openPopup(), browserAction.openPopup() or pageAction.openPopup().
export function isGloballyBlockingOpenPopup(window) {
  // previewPanel is null until the tab hover preview implementation has been
  // lazily loaded, which only happens once a preview is about to be shown.
  let previewPanel = window.gBrowser.tabContainer.previewPanel;

  // Avoid covering existing menus and panels. We only need to check before
  // opening the extension popup, because any menus/panels that are opened
  // later will render on top of the extension popup.
  for (let elem of window.document.querySelectorAll("menupopup,panel")) {
    if (elem.state !== "closed" && elem.state !== "hiding") {
      // Tab hover previews are tooltip-like panels tied to the pointer being
      // over the tab strip, so they are not UI that the extension popup could
      // be confused with or used to clickjack.
      if (previewPanel?.isHoverPanel(elem)) {
        continue;
      }
      // State "open" or "showing" means that something else is already shown.
      return true;
    }
  }

  return false;
}
