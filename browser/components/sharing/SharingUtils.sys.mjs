/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { BrowserUtils } from "resource://gre/modules/BrowserUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  QRCodeGenerator:
    "moz-src:///browser/components/qrcode/QRCodeGenerator.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  WindowsUIUtils: ["@mozilla.org/windows-ui-utils;1", Ci.nsIWindowsUIUtils],
});

// Use a non-caching getter so tests can swap out the service via MockRegistrar.
Object.defineProperty(lazy, "MacSharingService", {
  get() {
    return Cc["@mozilla.org/widget/macsharingservice;1"].getService(
      Ci.nsIMacSharingService
    );
  },
});

Object.defineProperty(lazy, "ExternalProtocolService", {
  configurable: true,
  get() {
    return Cc["@mozilla.org/uriloader/external-protocol-service;1"].getService(
      Ci.nsIExternalProtocolService
    );
  },
});
// Build a JS object satisfying nsIMacShareCustomItem. The `handler` arrow
// function is wrapped by XPConnect into an nsIMacShareCustomItemHandler.
const sMacShareCustomItemQI = ChromeUtils.generateQI(["nsIMacShareCustomItem"]);
function makeMacShareCustomItem({ label, icon = "", handler }) {
  return {
    label,
    icon,
    handler,
    QueryInterface: sMacShareCustomItemQI,
  };
}

/**
 * Class that populates and handles various sharing options
 */
class SharingUtilsCls {
  /**
   * Ensures a "Share" submenu exists in a given menu, creating it if necessary.
   *
   * @param {MozBrowser} contextBrowser
   *   The browser of the right-clicked (context) tab.
   * @param {MozBrowser[]|null} browsers
   *   All selected browsers in tab strip order. Pass null for single URL sharing.
   * @param {Element} insertAfterEl
   *   The menu item after which the share item is inserted.
   */
  ensureShareMenu(contextBrowser, browsers, insertAfterEl) {
    if (!Services.prefs.getBoolPref("browser.menu.share_url.allow", true)) {
      return;
    }

    let hasShareableURL =
      browsers !== null
        ? browsers.some(b => BrowserUtils.getShareableURL(b.currentURI))
        : !!BrowserUtils.getShareableURL(contextBrowser.currentURI);

    let shareMenu;
    let oldElement = insertAfterEl.nextElementSibling;

    if (oldElement?.matches(".share-tab-url-item")) {
      shareMenu = oldElement;
    } else {
      shareMenu = this.#createShareMenu(insertAfterEl);
    }

    shareMenu.contextBrowserToShare = Cu.getWeakReference(contextBrowser);
    shareMenu.browsersToShare =
      browsers?.map(b => Cu.getWeakReference(b)) ?? null;

    if (AppConstants.platform != "macosx") {
      // On macOS, we keep the item visible and handle enable/disable
      // inside the menupopup.
      shareMenu.hidden = !hasShareableURL;
    }
  }

  /**
   * Creates and inserts the "Share" <menu> element with a <menupopup> child.
   * All platforms now use a submenu.
   *
   * @param {Element} insertAfterEl
   * @returns {Element} A newly created menu
   */
  #createShareMenu(insertAfterEl) {
    let parentMenu = insertAfterEl.parentNode;
    let document = insertAfterEl.ownerDocument;

    let l10nID =
      parentMenu.id == "tabContextMenu"
        ? "tab-context-share-url"
        : "menu-file-share-url";

    let menu = document.createXULElement("menu");
    let menuPopup = document.createXULElement("menupopup");
    menuPopup.setAttribute("accesskey-conflicts-bug", "2073899");
    menuPopup.addEventListener("popupshowing", this);
    menu.appendChild(menuPopup);

    document.l10n.setAttributes(menu, l10nID);
    menu.classList.add("share-tab-url-item");

    parentMenu.insertBefore(menu, insertAfterEl.nextSibling);
    return menu;
  }

  /**
   * Return a menuitem that copies the link(s) to the clipboard.
   *
   * @param {Document} document
   * @param {number} shareableCount
   * @returns {Element} A newly created menuitem
   */
  #createCopyLinkMenuItem(document, shareableCount) {
    let item = document.createXULElement("menuitem");
    document.l10n.setAttributes(item, "menu-share-copy-links", {
      // shareableCount can be zero when about:blank tabs are selected but no
      // "real" tabs are. Clamp to 1 so the localized string is correct.
      count: Math.max(1, shareableCount),
    });
    item.classList.add("share-copy-link");
    return item;
  }

  /**
   * Show the QR code panel for the shareable URL carried by a DOM node.
   *
   * @param {Node} node Carries contextBrowserToShare.
   */
  showQRCode(node) {
    let { urlToShare } = this.getLinkToShare(node);
    let browser = node.contextBrowserToShare?.get();
    if (urlToShare && browser) {
      this.showQRCodePanel(node.documentGlobal, browser, urlToShare);
    }
  }

  async showQRCodePanel(win, browser, url) {
    let tab = win.gBrowser.getTabForBrowser(browser);
    if (tab && win.gBrowser.selectedTab !== tab) {
      let wait = null;
      if (!tab.linkedPanel) {
        wait = this.#waitForTabRestored(tab);
      }
      win.gBrowser.selectedTab = tab;
      if (wait) {
        if (win.gBrowser.selectedTab === tab) {
          await wait.promise;
        } else {
          wait.cancel();
        }
      }
    }

    if (!tab || !tab.linkedBrowser || tab.closing) {
      return;
    }

    Glean.qrcode.opened.add(1);

    let qrCodeDataURI = null;
    try {
      qrCodeDataURI = await lazy.QRCodeGenerator.generateQRCode(url);
    } catch (error) {
      console.error("Failed to generate QR code:", error);
    }

    let params = {
      url,
      qrCodeDataURI,
    };

    let { closedPromise, dialog } = win.gBrowser
      .getTabDialogBox(browser)
      .open(
        "chrome://browser/content/qrcode/qrcode-dialog.html",
        { features: "resizable=no", allowDuplicateDialogs: false },
        params
      );
    if (dialog) {
      await closedPromise;
    }
  }

  #waitForTabRestored(tab) {
    let { promise, resolve } = Promise.withResolvers();
    let cleanup = () => {
      tab.removeEventListener("SSTabRestored", cleanup);
      tab.removeEventListener("TabClose", cleanup);
      resolve();
    };
    tab.addEventListener("SSTabRestored", cleanup, { once: true });
    tab.addEventListener("TabClose", cleanup, { once: true });
    return { promise, cancel: cleanup };
  }

  /**
   * Get the sharing data for the context browser on a DOM node.
   *
   * @param {Node} node DOM node
   * @returns {object} An object containing the url and title to share
   */
  getLinkToShare(node) {
    let browser = node.contextBrowserToShare?.get();
    let urlToShare = null;
    let titleToShare = null;

    if (browser) {
      let maybeToShare = BrowserUtils.getShareableURL(browser.currentURI);
      if (maybeToShare) {
        let { gURLBar } = node.documentGlobal;
        urlToShare = gURLBar.makeURIReadable(maybeToShare).displaySpec;
        titleToShare = browser.contentTitle;
      }
    }
    return { urlToShare, titleToShare };
  }

  /**
   * Get the link data for all browsers stored on a DOM node.
   *
   * @param {Node} node DOM node
   * @returns {Array<{url: string, title: string}>}
   */
  getLinksToShare(node) {
    let browsers = node.browsersToShare ?? [node.contextBrowserToShare];
    let links = [];
    for (let weakRef of browsers) {
      let browser = weakRef.get();
      if (!browser) {
        continue;
      }
      let maybeToShare = BrowserUtils.getShareableURL(browser.currentURI);
      if (maybeToShare) {
        let { gURLBar } = node.documentGlobal;
        links.push({
          url: gURLBar.makeURIReadable(maybeToShare).displaySpec,
          title: browser.contentTitle,
        });
      }
    }
    return links;
  }

  #initSharePopup(menuPopup) {
    this.populateSharePopup(menuPopup);

    menuPopup.parentNode
      .closest("menupopup")
      .addEventListener("popuphiding", this);
  }

  /**
   * Populates the share submenu popup with platform-appropriate items.
   *
   * @param {Element} menuPopup The share menu element
   */
  populateSharePopup(menuPopup) {
    // Ensure the command listener is registered.
    menuPopup.addEventListener("command", this);

    while (menuPopup.firstChild) {
      menuPopup.firstChild.remove();
    }

    let document = menuPopup.ownerDocument;
    let node = menuPopup.parentNode;
    let isMultiTab = node.browsersToShare !== null;

    let { urlToShare } = this.getLinkToShare(node);

    // If we can't share the current URL, we display the items disabled.
    let shouldEnable = !!urlToShare;

    let shareableCount;
    if (isMultiTab) {
      shareableCount = this.getLinksToShare(node).length;
    } else {
      shareableCount = shouldEnable ? 1 : 0;
    }
    let copyLinkEnabled = shareableCount > 0;

    let copyItem = this.#createCopyLinkMenuItem(document, shareableCount);
    if (!copyLinkEnabled) {
      copyItem.setAttribute("disabled", "true");
    }
    menuPopup.appendChild(copyItem);

    // QR code
    if (Services.prefs.getBoolPref("browser.shareqrcode.enabled", false)) {
      let qrCodeItem = document.createXULElement("menuitem");
      qrCodeItem.classList.add("share-qrcode-item");
      document.l10n.setAttributes(qrCodeItem, "menu-file-share-qrcode3");
      if (!shouldEnable || isMultiTab) {
        qrCodeItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(qrCodeItem);
    }

    // Windows: native share dialog
    if (AppConstants.platform == "win") {
      menuPopup.appendChild(document.createXULElement("menuseparator"));
      let winShareItem = document.createXULElement("menuitem");
      winShareItem.classList.add("share-windows-item");
      document.l10n.setAttributes(winShareItem, "menu-share-windows");
      if (!shouldEnable || isMultiTab) {
        winShareItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(winShareItem);
    }

    if (AppConstants.platform == "macosx") {
      menuPopup.appendChild(document.createXULElement("menuseparator"));
      let macPickerItem = document.createXULElement("menuitem");
      macPickerItem.classList.add("share-mac-picker-item");
      document.l10n.setAttributes(
        macPickerItem,
        shareableCount > 1
          ? "menu-share-mac-picker-multiple"
          : "menu-share-mac-picker-single"
      );
      if (!shouldEnable) {
        macPickerItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(macPickerItem);
    }
  }

  #onCommand(event) {
    let target = event.target;
    let node =
      target.closest(".share-tab-url-item") ?? event.currentTarget.parentNode;
    if (target.classList.contains("share-qrcode-item")) {
      this.showQRCode(node);
    } else if (target.classList.contains("share-copy-link")) {
      this.copyLink(node);
    } else if (target.classList.contains("share-windows-item")) {
      this.shareOnWindows(node);
    } else if (target.classList.contains("share-mac-picker-item")) {
      this.shareOnMacPicker(node);
    }
  }

  onPopupHiding(event) {
    // We don't want to rebuild the contents of the "Share" menupopup if only its submenu is
    // hidden. So bail if this isn't the top menupopup in the DOM tree:
    if (event.target.parentNode.closest("menupopup")) {
      return;
    }
    let menupopup = event.target.querySelector(
      ".share-tab-url-item"
    )?.menupopup;
    menupopup?.removeEventListener("command", this);
    event.target.removeEventListener("popuphiding", this);
  }

  onPopupShowing(event) {
    this.#initSharePopup(event.target);
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "command":
        this.#onCommand(aEvent);
        break;
      case "popuphiding":
        this.onPopupHiding(aEvent);
        break;
      case "popupshowing":
        this.onPopupShowing(aEvent);
        break;
    }
  }

  copyLink(node) {
    let links = this.getLinksToShare(node);
    if (links.length) {
      BrowserUtils.copyLinks(links);
    }
  }

  openPairingFlow(window) {
    window.gSync.openPairDevice(null, "share-panel");
  }

  sendToDevice(panel, deviceId) {
    let window = panel.documentGlobal;
    let target = window.gSync
      .getSendTabTargets()
      .find(device => device.id == deviceId);
    if (!target) {
      return;
    }

    let browser = panel.contextBrowserToShare?.get();
    let uri = browser && BrowserUtils.getShareableURL(browser.currentURI);
    if (!uri) {
      return;
    }

    window.gSync.sendTabsAndConfirm(
      [
        {
          url: uri.spec,
          title: browser.contentTitle,
          private: lazy.PrivateBrowsingUtils.isBrowserPrivate(browser),
        },
      ],
      [target]
    );
  }

  shareOnWindows(node) {
    let { urlToShare, titleToShare } = this.getLinkToShare(node);
    if (!urlToShare) {
      return;
    }

    lazy.WindowsUIUtils.shareUrl(urlToShare, titleToShare);
  }

  sendEmail(node) {
    let { urlToShare, titleToShare } = this.getLinkToShare(node);
    if (!urlToShare) {
      return;
    }

    let mailtoUrl =
      "mailto:?body=" +
      encodeURIComponent(urlToShare) +
      "&subject=" +
      encodeURIComponent(titleToShare ?? "");

    lazy.ExternalProtocolService.loadURI(
      Services.io.newURI(mailtoUrl),
      Services.scriptSecurityManager.getSystemPrincipal()
    );
  }

  /**
   * Open the macOS system share picker.
   *
   * @param {Node} node - Carries contextBrowserToShare and browsersToShare.
   * @param {object} [options]
   * @param {boolean} [options.injectQR] - Prepend a QR Code entry inside the
   *   picker.
   */
  async shareOnMacPicker(node, { injectQR = false } = {}) {
    let anchor = this.#resolvePickerAnchor(node);
    if (!anchor) {
      return;
    }

    let isMultiTab = node.browsersToShare !== null;
    let links;
    let customItems = [];

    if (isMultiTab) {
      links = this.getLinksToShare(node);
    } else {
      let { urlToShare, titleToShare } = this.getLinkToShare(node);
      links = urlToShare ? [{ url: urlToShare, title: titleToShare }] : [];
      if (
        injectQR &&
        links.length &&
        Services.prefs.getBoolPref("browser.shareqrcode.enabled", false)
      ) {
        customItems.push(await this.#makeQRCodeCustomItem(node, links[0].url));
      }
    }
    if (!links.length) {
      return;
    }

    let urls = links.map(l => l.url);
    let titles = links.map(l => l.title ?? "");
    let shareTitle = isMultiTab
      ? await this.#formatLabel(node, "menu-share-links", {
          count: links.length,
        })
      : titles[0];

    // Override Apple's Copy Link service so it copies our multiple
    // clipboard representations and shows the correct "Copy Link" /
    // "Copy N Links" label, while keeping Apple's native icon/identity.
    let copyItem = makeMacShareCustomItem({
      label: await this.#formatLabel(node, "menu-share-copy-links", {
        count: links.length,
      }),
      handler: () => BrowserUtils.copyLinks(links),
    });

    lazy.MacSharingService.shareUrlWithPicker(
      anchor,
      urls,
      titles,
      shareTitle,
      customItems,
      copyItem
    );
  }

  async #formatLabel(node, l10nId, args) {
    let [msg] = await node.ownerDocument.l10n.formatMessages([
      { id: l10nId, args },
    ]);
    return msg?.attributes?.find(a => a.name === "label")?.value ?? "";
  }

  async #makeQRCodeCustomItem(node, url) {
    let label = await this.#formatLabel(node, "menu-file-share-qrcode3");
    let win = node.documentGlobal;
    let browser = node.contextBrowserToShare?.get();
    return makeMacShareCustomItem({
      label,
      icon: "qrcode",
      handler: () => this.showQRCodePanel(win, browser, url),
    });
  }

  #resolvePickerAnchor(node) {
    // Resolve gBrowser/gURLBar through the menu's own chrome window
    // rather than browser.ownerGlobal, which can be null for remote browsers
    // during menu teardown.
    let win = node.documentGlobal;
    if (node.classList.contains("share-toolbar-picker")) {
      return node;
    }
    // Tab right-click: anchor to the tab that was right-clicked.
    if (node.closest("#tabContextMenu")) {
      let browser = node.contextBrowserToShare?.get();
      let tab = browser && win.gBrowser?.getTabForBrowser(browser);
      if (tab) {
        return tab;
      }
    }
    return win.gURLBar?.inputField ?? null;
  }

  testOnlyMockUIUtils(mock) {
    if (!Cu.isInAutomation) {
      throw new Error("Can only mock utils in automation.");
    }
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(lazy, "WindowsUIUtils", {
      get() {
        if (mock) {
          return mock;
        }
        return Cc["@mozilla.org/windows-ui-utils;1"].getService(
          Ci.nsIWindowsUIUtils
        );
      },
    });
  }

  testOnlyMockExternalProtocolService(mock) {
    if (!Cu.isInAutomation) {
      throw new Error("Can only mock utils in automation.");
    }
    // eslint-disable-next-line mozilla/valid-lazy
    Object.defineProperty(lazy, "ExternalProtocolService", {
      configurable: true,
      get() {
        if (mock) {
          return mock;
        }
        return Cc[
          "@mozilla.org/uriloader/external-protocol-service;1"
        ].getService(Ci.nsIExternalProtocolService);
      },
    });
  }
}

export let SharingUtils = new SharingUtilsCls();
