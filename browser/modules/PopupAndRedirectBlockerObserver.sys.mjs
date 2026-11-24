/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  assignAutoAccessKeys: "chrome://global/content/elements/auto-accesskey.mjs",
});

export var PopupAndRedirectBlockerObserver = {
  /**
   * Check if we are currently in the process of appending a notification.
   * We can't rely on `getNotificationWithValue()`: It returns `null`
   * while `appendNotification()` is resolving, so we keep track of the
   * promise instead.
   */
  mNotificationPromise: null,

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "DOMUpdateBlockedPopups":
        this.onDOMUpdateBlockedPopupsAndRedirect(aEvent);
        break;
      case "DOMUpdateBlockedRedirect":
        this.onDOMUpdateBlockedPopupsAndRedirect(aEvent);
        break;
      case "command":
        this.onCommand(aEvent);
        break;
      case "popupshowing":
        this.onPopupShowing(aEvent);
        break;
      case "popuphiding":
        this.onPopupHiding(aEvent);
        break;
    }
  },

  /**
   * Handles a "DOMUpdateBlockedPopups" or "DOMUpdateBlockedRedirect" event
   * received from the JSWindowActorParent.
   *
   * @param {*} aEvent
   */
  onDOMUpdateBlockedPopupsAndRedirect(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { gBrowser, gPermissionPanel } = window;
    if (aEvent.originalTarget != gBrowser.selectedBrowser) {
      return;
    }

    gPermissionPanel.refreshPermissionIcons();

    const popupCount =
      gBrowser.selectedBrowser.popupAndRedirectBlocker.getBlockedPopupCount();
    const isRedirectBlocked =
      gBrowser.selectedBrowser.popupAndRedirectBlocker.isRedirectBlocked();
    if (!popupCount && !isRedirectBlocked) {
      this.hideNotification(gBrowser);
      return;
    }

    if (Services.prefs.getBoolPref("privacy.popups.showBrowserMessage")) {
      this.ensureInitializedForWindow(window);
      this.showBrowserMessage(gBrowser, popupCount, isRedirectBlocked);
    }
  },

  hideNotification(aBrowser) {
    const notificationBox = aBrowser.getNotificationBox();
    const notification =
      notificationBox.getNotificationWithValue("popup-blocked");
    if (notification) {
      notificationBox.removeNotification(notification);
    }
  },

  ensureInitializedForWindow(aWindow) {
    const popup = aWindow.document.getElementById("blockedPopupOptions");
    // Make sure we don't add the same event handlers multiple times.
    if (popup.getAttribute("initialized")) {
      return;
    }

    popup.setAttribute("initialized", true);
    popup.addEventListener("command", this);
    popup.addEventListener("popupshowing", this);
    popup.addEventListener("popuphiding", this);
  },

  async showBrowserMessage(aBrowser, aPopupCount, aIsRedirectBlocked) {
    const selectedBrowser = aBrowser.selectedBrowser;
    const popupAndRedirectBlocker = selectedBrowser.popupAndRedirectBlocker;

    // Check if the notification was previously shown and then dismissed
    // by the user.
    if (popupAndRedirectBlocker.hasBeenDismissed()) {
      return;
    }

    const l10nId = (() => {
      if (aPopupCount >= this.maxReportedPopups) {
        return aIsRedirectBlocked
          ? "popup-warning-exceeded-with-redirect-message"
          : "popup-warning-exceeded-message";
      }

      return aIsRedirectBlocked
        ? "redirect-warning-with-popup-message"
        : "popup-warning-message";
    })();
    const label = {
      "l10n-id": l10nId,
      "l10n-args": {
        popupCount: aPopupCount,
      },
    };
    const notificationBox = aBrowser.getNotificationBox();
    const notification = this.mNotificationPromise
      ? await this.mNotificationPromise
      : notificationBox.getNotificationWithValue("popup-blocked");
    if (notification) {
      notification.label = label;
      return;
    }

    const image = "chrome://browser/skin/notification-icons/popup.svg";
    const priority = notificationBox.PRIORITY_INFO_MEDIUM;
    const eventCallback = popupAndRedirectBlocker.eventCallback.bind(
      popupAndRedirectBlocker
    );

    this.mNotificationPromise = notificationBox.appendNotification(
      "popup-blocked",
      { label, image, priority, eventCallback },
      [
        {
          "l10n-id": "popup-warning-button",
          popup: "blockedPopupOptions",
          callback: null,
        },
      ]
    );
    await this.mNotificationPromise;
    this.mNotificationPromise = null;
  },

  /**
   * Event handler that is triggered when a user clicks on the "options"
   * button in the notification which opens a popup.
   *
   * @param {*} aEvent
   */
  async onPopupShowing(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { gBrowser, document } = window;

    // We get `uriHost` from the principal whenever possible and fall
    // back to the `spec` for special pages without a host, e.g. "about:".
    const browser = gBrowser.selectedBrowser;
    const uriOrPrincipal = browser.isContentPrincipal
      ? browser.contentPrincipal
      : browser.currentURI;
    const uriHost = uriOrPrincipal.asciiHost
      ? uriOrPrincipal.displayHost
      : uriOrPrincipal.spec;

    // "Allow pop-ups for site..."
    const blockedPopupAllowSite = document.getElementById(
      "blockedPopupAllowSite"
    );
    if (Services.prefs.prefIsLocked("dom.disable_open_during_load")) {
      blockedPopupAllowSite.setAttribute("hidden", "true");
    } else {
      blockedPopupAllowSite.removeAttribute("hidden");
      document.l10n.setAttributes(
        blockedPopupAllowSite,
        "popups-infobar-allow2",
        { uriHost }
      );
    }

    // "Dont show this message when..."
    const blockedPopupDontShowMessage = document.getElementById(
      "blockedPopupDontShowMessage"
    );
    blockedPopupDontShowMessage.removeAttribute("checked");

    gBrowser.selectedBrowser.popupAndRedirectBlocker
      .getBlockedRedirect()
      .then(blockedRedirect => {
        this.onPopupShowingBlockedRedirect(blockedRedirect, window);
      });
    gBrowser.selectedBrowser.popupAndRedirectBlocker
      .getBlockedPopups()
      .then(blockedPopups => {
        this.onPopupShowingBlockedPopups(blockedPopups, window);
      });
  },

  onPopupShowingBlockedRedirect(aBlockedRedirect, aWindow) {
    const { gBrowser, document } = aWindow;
    const browser = gBrowser.selectedBrowser;

    const blockedRedirectSeparator = document.getElementById(
      "blockedRedirectSeparator"
    );
    blockedRedirectSeparator.hidden = !aBlockedRedirect;

    if (!aBlockedRedirect) {
      return;
    }

    // We may end up in a race condition and to avoid showing duplicate
    // items, make sure the list is actually empty.
    const nextElement = blockedRedirectSeparator.nextElementSibling;
    if (nextElement?.hasAttribute("redirectInnerWindowId")) {
      return;
    }

    const menuitem = document.createXULElement("menuitem");
    menuitem.setAttribute("auto-accesskey", "digit");
    document.l10n.setAttributes(menuitem, "popup-trigger-redirect-menuitem", {
      redirectURI: aBlockedRedirect.redirectURISpec,
    });
    menuitem.setAttribute("redirectURISpec", aBlockedRedirect.redirectURISpec);
    // Store the source inner window id, so we can check if the document
    // that triggered the redirect is still the same.
    menuitem.setAttribute(
      "redirectInnerWindowId",
      aBlockedRedirect.innerWindowId
    );
    // Store the browser for the current tab. The active tab may change,
    // so we keep a reference to it.
    menuitem.browser = browser;
    // Same reason as source inner window id, we compare it with the one
    // of the browsing context at the time of unblocking.
    menuitem.browsingContext = aBlockedRedirect.browsingContext;

    blockedRedirectSeparator.after(menuitem);
    lazy.assignAutoAccessKeys(menuitem.parentNode);
  },

  onPopupShowingBlockedPopups(aBlockedPopups, aWindow) {
    const { gBrowser, document } = aWindow;
    const browser = gBrowser.selectedBrowser;

    const blockedPopupsSeparator = document.getElementById(
      "blockedPopupsSeparator"
    );
    blockedPopupsSeparator.hidden = !aBlockedPopups.length;

    if (!aBlockedPopups.length) {
      return;
    }

    // We may end up in a race condition and to avoid showing duplicate
    // items, make sure the list is actually empty.
    const nextElement = blockedPopupsSeparator.nextElementSibling;
    if (nextElement?.hasAttribute("popupInnerWindowId")) {
      return;
    }

    for (let i = 0; i < aBlockedPopups.length; ++i) {
      const blockedPopup = aBlockedPopups[i];

      const menuitem = document.createXULElement("menuitem");
      menuitem.setAttribute("auto-accesskey", "digit");
      document.l10n.setAttributes(menuitem, "popup-show-popup-menuitem", {
        popupURI: blockedPopup.popupWindowURISpec,
      });
      // The report index is the index into the blocked popup list
      // maintained by the window where this popup was blocked.
      menuitem.setAttribute("popupReportIndex", blockedPopup.reportIndex);
      // Store the source inner window id, so we can check if the document
      // that triggered the popup is still the same.
      menuitem.setAttribute("popupInnerWindowId", blockedPopup.innerWindowId);
      // Store the browser for the current tab. The active tab may change,
      // so we keep a reference to it.
      menuitem.browser = browser;
      // Same reason as source inner window id, we compare it with the one
      // of the browsing context at the time of unblocking.
      menuitem.browsingContext = blockedPopup.browsingContext;

      blockedPopupsSeparator.after(menuitem);
    }
    lazy.assignAutoAccessKeys(blockedPopupsSeparator.parentNode);
  },

  /**
   * Event handler that is triggered when the "options" popup of the
   * notification closes.
   *
   * @param {*} aEvent
   */
  onPopupHiding(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { document } = window;

    // Remove the blocked redirect, if any.
    const blockedRedirectSeparator = document.getElementById(
      "blockedRedirectSeparator"
    );
    let item = blockedRedirectSeparator.nextElementSibling;
    if (item?.hasAttribute("redirectInnerWindowId")) {
      item.remove();
    }

    // Remove the blocked popups, if any.
    const blockedPopupsSeparator = document.getElementById(
      "blockedPopupsSeparator"
    );
    let next = null;
    for (
      item = blockedPopupsSeparator.nextElementSibling;
      item?.hasAttribute("popupInnerWindowId");
      item = next
    ) {
      next = item.nextElementSibling;
      item.remove();
    }
  },

  /**
   * Event handler that is triggered when a user clicks on one of the
   * fields in the "options" popup of the notification.
   *
   * @param {*} aEvent
   */
  onCommand(aEvent) {
    if (aEvent.target.hasAttribute("popupReportIndex")) {
      this.showBlockedPopup(aEvent);
      return;
    }

    if (aEvent.target.hasAttribute("redirectURISpec")) {
      this.navigateToBlockedRedirect(aEvent);
      return;
    }

    switch (aEvent.target.id) {
      case "blockedPopupAllowSite":
        this.toggleAllowPopupsForSite(aEvent);
        break;
      case "blockedPopupEdit":
        this.editPopupSettings(aEvent);
        break;
      case "blockedPopupDontShowMessage":
        this.dontShowMessage(aEvent);
        break;
    }
  },

  showBlockedPopup(aEvent) {
    const { browser, browsingContext } = aEvent.target;
    const innerWindowId = aEvent.target.getAttribute("popupInnerWindowId");
    const reportIndex = aEvent.target.getAttribute("popupReportIndex");

    browser.popupAndRedirectBlocker.unblockPopup(
      browsingContext,
      innerWindowId,
      reportIndex
    );
  },

  navigateToBlockedRedirect(aEvent) {
    const { browser, browsingContext } = aEvent.target;
    const innerWindowId = aEvent.target.getAttribute("redirectInnerWindowId");
    const redirectURISpec = aEvent.target.getAttribute("redirectURISpec");

    browser.popupAndRedirectBlocker.unblockRedirect(
      browsingContext,
      innerWindowId,
      redirectURISpec
    );
  },

  async toggleAllowPopupsForSite(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { gBrowser } = window;

    if (Services.prefs.prefIsLocked("dom.disable_open_during_load")) {
      return;
    }

    // The toggle should only be visible (and therefore clickable) if
    // popups are currently blocked.
    Services.perms.addFromPrincipal(
      gBrowser.contentPrincipal,
      "popup",
      Services.perms.ALLOW_ACTION
    );
    gBrowser.getNotificationBox().removeCurrentNotification();

    // The order is important here. We want to unblock all popups of the
    // current document first and then potentially redirect somewhere
    // else.
    await gBrowser.selectedBrowser.popupAndRedirectBlocker.unblockAllPopups();
    await gBrowser.selectedBrowser.popupAndRedirectBlocker.unblockFirstRedirect();
  },

  editPopupSettings(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { openPreferences } = window;

    openPreferences("privacy-permissions-block-popups");
  },

  dontShowMessage(aEvent) {
    const window = aEvent.originalTarget.documentGlobal;
    const { gBrowser } = window;

    Services.prefs.setBoolPref("privacy.popups.showBrowserMessage", false);
    gBrowser.getNotificationBox().removeCurrentNotification();
  },
};

XPCOMUtils.defineLazyPreferenceGetter(
  PopupAndRedirectBlockerObserver,
  "maxReportedPopups",
  "privacy.popups.maxReported"
);
