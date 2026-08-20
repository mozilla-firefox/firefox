/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { BrowserUtils } from "resource://gre/modules/BrowserUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const APPLE_COPY_LINK = "com.apple.share.CopyLink.invite";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
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
    item.classList.add("menuitem-iconic", "share-copy-link");
    item.setAttribute("image", "chrome://global/skin/icons/link.svg");
    return item;
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

    win.gBrowser
      .getTabDialogBox(browser)
      .open(
        "chrome://browser/content/qrcode/qrcode-dialog.html",
        { features: "resizable=no", allowDuplicateDialogs: false },
        params
      );
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

    // If we can't share the current URL, we display the items disabled,
    // but enable the "more..." item at the bottom on macOS, to allow the
    // user to change sharing preferences in the system dialog.
    let shouldEnable = !!urlToShare;

    let shareableCount;
    if (isMultiTab) {
      shareableCount = this.getLinksToShare(node).length;
    } else {
      shareableCount = shouldEnable ? 1 : 0;
    }
    let copyLinkEnabled = shareableCount > 0;

    // On macOS, query native services. We need this list to check whether
    // Apple already provides a "Copy Link" service.
    let services = [];
    if (AppConstants.platform == "macosx") {
      if (!urlToShare) {
        // Fake it so we can ask the sharing service for services:
        urlToShare = "https://mozilla.org/";
      }
      services = lazy.MacSharingService.getSharingProviders(urlToShare);
    }

    // Copy Link(s) - all platforms. Apple seems reluctant to provide copy
    // link as a feature, so add it if it's not already in the services list.
    if (
      AppConstants.platform != "macosx" ||
      !services.some(s => s.name == APPLE_COPY_LINK)
    ) {
      let copyItem = this.#createCopyLinkMenuItem(document, shareableCount);
      if (!copyLinkEnabled) {
        copyItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(copyItem);
    }

    // QR code - all platforms
    if (Services.prefs.getBoolPref("browser.shareqrcode.enabled", false)) {
      let qrCodeItem = document.createXULElement("menuitem");
      qrCodeItem.classList.add("menuitem-iconic", "share-qrcode-item");
      document.l10n.setAttributes(qrCodeItem, "menu-file-share-qrcode");
      qrCodeItem.setAttribute("image", "chrome://browser/skin/qrcode.svg");
      if (!shouldEnable || isMultiTab) {
        qrCodeItem.setAttribute("disabled", "true");
      }
      menuPopup.appendChild(qrCodeItem);
    }

    // macOS: native sharing services + "More..."
    if (AppConstants.platform == "macosx") {
      if (services.length) {
        menuPopup.appendChild(document.createXULElement("menuseparator"));
      }
      // Share service items
      services.forEach(share => {
        let item = document.createXULElement("menuitem");
        item.classList.add("menuitem-iconic");
        item.setAttribute("label", share.menuItemTitle);
        item.setAttribute("data-share-name", share.name);
        item.setAttribute("image", ChromeUtils.encodeURIForSrcset(share.image));
        if (!shouldEnable) {
          item.setAttribute("disabled", "true");
        }
        menuPopup.appendChild(item);
      });
      menuPopup.appendChild(document.createXULElement("menuseparator"));

      // More item
      let moreItem = document.createXULElement("menuitem");
      document.l10n.setAttributes(moreItem, "menu-share-more");
      moreItem.classList.add("menuitem-iconic", "share-more-button");
      moreItem.setAttribute("data-share-name", "share_macosx_more");
      menuPopup.appendChild(moreItem);
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
  }

  #onCommand(event) {
    let node = event.currentTarget.parentNode;
    if (event.target.classList.contains("share-qrcode-item")) {
      let { urlToShare: url } = this.getLinkToShare(node);
      let browser = node.contextBrowserToShare?.get();
      if (url && browser) {
        this.showQRCodePanel(node.documentGlobal, browser, url);
      }
    } else if (event.target.classList.contains("share-more-button")) {
      this.openMacSharePreferences();
    } else if (event.target.classList.contains("share-copy-link")) {
      this.copyLink(node);
    } else if (event.target.classList.contains("share-windows-item")) {
      this.shareOnWindows(node);
    } else if (event.target.dataset.shareName) {
      this.shareOnMac(node, event.target.dataset.shareName);
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

  shareOnMac(node, serviceName) {
    let { urlToShare, titleToShare } = this.getLinkToShare(node);
    if (!urlToShare) {
      return;
    }

    lazy.MacSharingService.shareUrl(serviceName, urlToShare, titleToShare);
  }

  openMacSharePreferences() {
    lazy.MacSharingService.openSharingPreferences();
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
