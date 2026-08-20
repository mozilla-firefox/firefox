/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PanelMultiView:
    "moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs",
  SharingUtils: "moz-src:///browser/components/sharing/SharingUtils.sys.mjs",
});

const ENABLED_PREF = "browser.urlbar.share-button.enabled";
const BUTTON_ID = "share-button";
const PANEL_ID = "share-panel";
const TEMPLATE_ID = "template-share-panel";
const COPY_LINK_BUTTON_ID = "share-panel-copy-link";
const OS_SHARE_BUTTON_ID = "share-panel-os-share";
const MAIL_SHARE_BUTTON_ID = "share-panel-mail";
const SCREENSHOTS_BUTTON_ID = "share-panel-screenshot";
const QR_CODE_BUTTON_ID = "share-panel-qr-code";
const DEVICE_SHARE_BUTTON_ID = "share-panel-send-to-device";

function shouldButtonBeVisible(buttonId, isShareable) {
  switch (buttonId) {
    case SCREENSHOTS_BUTTON_ID: {
      return true;
    }
    case OS_SHARE_BUTTON_ID: {
      return AppConstants.platform !== "linux" && isShareable;
    }
    case MAIL_SHARE_BUTTON_ID: {
      return AppConstants.platform === "linux" && isShareable;
    }
    case COPY_LINK_BUTTON_ID:
    case QR_CODE_BUTTON_ID:
    case DEVICE_SHARE_BUTTON_ID: {
      return isShareable;
    }
  }

  return false;
}

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "SHARE_BUTTON_ENABLED",
  ENABLED_PREF,
  false,
  () => {
    SharePageAction.updateGlobalButtonVisibility();
  }
);

/**
 * Manages the share panel anchored to the share button in the URL bar. The
 * panel markup lives in browser/components/sharing/content/sharePanel.inc.xhtml
 * and is only instantiated the first time the panel is opened.
 */
class SharePageActionClass {
  /**
   * Sets up the share button for a browser window. Called for every window at
   * delayed startup through the browser-window-delayed-startup category.
   *
   * @param {DOMWindow} window
   *   The browser window.
   */
  init(window) {
    if (!window.toolbar.visible) {
      // Popup windows don't get page actions.
      return;
    }

    this.updateButtonVisibility(window);

    let button = window.document.getElementById(BUTTON_ID);
    if (!button) {
      return;
    }

    button.addEventListener("click", this, true);
  }

  updateGlobalButtonVisibility() {
    for (let window of Services.wm.getEnumerator("navigator:browser")) {
      this.updateButtonVisibility(window);
    }
  }

  updateButtonVisibility(window) {
    let button = window.document.getElementById(BUTTON_ID);
    if (!button) {
      return;
    }

    button.hidden = !lazy.SHARE_BUTTON_ENABLED;
  }

  handleEvent(event) {
    switch (event.type) {
      case "click": {
        this.togglePanel(event);
        break;
      }
      case "command": {
        this.handleCommand(event);
        break;
      }
      case "popupshowing": {
        this.handlePopupShowing(event);
        break;
      }
    }
  }

  handleCommand(event) {
    let panel = event.currentTarget;
    let hintL10nId;

    switch (event.target.id) {
      case COPY_LINK_BUTTON_ID: {
        hintL10nId = this.#copyLink(panel);
        break;
      }
      case SCREENSHOTS_BUTTON_ID: {
        Services.obs.notifyObservers(
          event.target.documentGlobal,
          "menuitem-screenshot",
          "SharePanel"
        );
        break;
      }
      case QR_CODE_BUTTON_ID: {
        this.#showQRCode(panel);
        break;
      }
      case OS_SHARE_BUTTON_ID: {
        this.#handleOsShare(panel);
        break;
      }
      case MAIL_SHARE_BUTTON_ID: {
        lazy.SharingUtils.sendEmail(panel);
        break;
      }
      default: {
        return;
      }
    }

    if (hintL10nId) {
      this.#showConfirmationHint(panel, hintL10nId);
    }

    lazy.PanelMultiView.hidePopup(panel);
  }

  /**
   * Copies the current page URL to the clipboard.
   *
   * @param {Element} panel
   * @returns {string|null} The l10n id of the confirmation hint to show, or
   *   null if nothing was copied.
   */
  #copyLink(panel) {
    if (!lazy.SharingUtils.getLinkToShare(panel).urlToShare) {
      return null;
    }

    lazy.SharingUtils.copyLink(panel);

    return "confirmation-hint-link-copied";
  }

  /**
   * Open the QR code panel for a given url
   *
   * @param {Element} panel The share panel
   */
  #showQRCode(panel) {
    let { urlToShare } = lazy.SharingUtils.getLinkToShare(panel);
    if (!urlToShare) {
      return;
    }

    let window = panel.documentGlobal;
    let browser = panel.contextBrowserToShare?.get();

    lazy.SharingUtils.showQRCodePanel(window, browser, urlToShare);
  }

  #handleOsShare(panel) {
    if (AppConstants.platform === "win") {
      lazy.SharingUtils.shareOnWindows(panel);
    }
  }

  /**
   * Shows a confirmation hint on the share button once the panel has closed.
   *
   * @param {Element} panel
   * @param {string} l10nId
   */
  #showConfirmationHint(panel, l10nId) {
    let window = panel.documentGlobal;
    let button = window.document.getElementById(BUTTON_ID);
    panel.addEventListener(
      "popuphidden",
      () => {
        window.ConfirmationHint.show(button, l10nId);
      },
      { once: true }
    );
  }

  handlePopupShowing(event) {
    const panel = event.target;
    let isShareable = !!lazy.SharingUtils.getLinkToShare(panel).urlToShare;

    for (let button of panel.querySelectorAll(
      "#share-panel-mainView toolbarbutton"
    )) {
      button.hidden = !shouldButtonBeVisible(button.id, isShareable);
    }
  }

  togglePanel(event) {
    if (event.button !== 0) {
      return;
    }

    let window = event.target.documentGlobal;
    let button = window.document.getElementById(BUTTON_ID);
    let panel = this.#ensurePanel(window);

    if (panel.state === "open" || panel.state === "showing") {
      lazy.PanelMultiView.hidePopup(panel);
      return;
    }

    panel.contextBrowserToShare = Cu.getWeakReference(
      window.gBrowser.selectedBrowser
    );

    lazy.PanelMultiView.openPopup(panel, button, {
      position: "bottomright topright",
      triggerEvent: event,
    });
  }

  /**
   * Turns the panel template into DOM the first time it is needed.
   *
   * @param {DOMWindow} window
   *   The browser window.
   * @returns {Element}
   *   The panel element for this window.
   */
  #ensurePanel(window) {
    let panel = window.document.getElementById(PANEL_ID);
    if (!panel) {
      let template = window.document.getElementById(TEMPLATE_ID);
      panel = template.content.firstElementChild;
      template.replaceWith(template.content);

      panel.addEventListener("command", this);
      panel.addEventListener("popupshowing", this);
    }

    return panel;
  }
}

export const SharePageAction = new SharePageActionClass();
