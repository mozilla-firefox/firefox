/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization([
      "branding/brand.ftl",
      "toolkit/main-window/autocomplete.ftl",
    ])
);

const AUTH_TIMEOUT_MS = 5 * 60 * 1000;

// Record type -> { browser, tabBrowser, innerWindowID, expiresAt } of the last
// authorized delete. browser and tabBrowser are WeakRefs.
const gRecentAuth = new Map();

// Drops the auth when its page reloads or navigates, also from the bfcache.
const gNavigationListener = {
  onLocationChange(browser, webProgress, request, location, flags) {
    if (
      webProgress.isTopLevel &&
      !(flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) &&
      gRecentAuth.values().some(auth => auth.browser.deref() === browser)
    ) {
      forgetAuth();
    }
  },
};

function forgetAuth() {
  for (const auth of gRecentAuth.values()) {
    auth.tabBrowser.deref()?.removeTabsProgressListener(gNavigationListener);
  }
  gRecentAuth.clear();
}

const TITLE_L10N_IDS = {
  password: "autocomplete-delete-password-title",
  address: "autocomplete-delete-address-title",
  payment: "autocomplete-delete-payment-method-title",
};

export const AutocompleteRemoveRecord = {
  /**
   * Like about:logins, a delete authorized in the last 5 minutes from the
   * same page skips the next prompt, until the user switches tabs.
   *
   * @param {string} recordType
   * @param {Element} browser The top <browser> of the tab.
   * @returns {number?} When the authorization expires, or null if none.
   */
  authExpirationTime(recordType, browser) {
    const auth = gRecentAuth.get(recordType);
    if (
      !auth ||
      auth.browser.deref() !== browser ||
      auth.innerWindowID !== browser.innerWindowID ||
      Date.now() >= auth.expiresAt
    ) {
      return null;
    }
    return auth.expiresAt;
  },

  /**
   * @param {string} recordType
   * @param {Element} browser The top <browser> of the tab.
   * @param {number} innerWindowID The top page's id, read before the prompt.
   */
  noteAuthorized(recordType, browser, innerWindowID) {
    const tabBrowser = browser?.getTabBrowser?.();
    if (!tabBrowser?.tabContainer) {
      return;
    }
    gRecentAuth.set(recordType, {
      browser: new WeakRef(browser),
      tabBrowser: new WeakRef(tabBrowser),
      innerWindowID,
      expiresAt: Date.now() + AUTH_TIMEOUT_MS,
    });
    // Adding the same listener again is a no-op.
    tabBrowser.tabContainer.addEventListener("TabSelect", forgetAuth, {
      once: true,
    });
    // Tabs progress listeners are not deduplicated.
    tabBrowser.removeTabsProgressListener(gNavigationListener);
    tabBrowser.addTabsProgressListener(gNavigationListener);
  },

  resetAuthForTesting() {
    forgetAuth();
  },

  async passwordOSAuthStrings() {
    const platform = AppConstants.platform;
    const ids = [{ id: "autocomplete-remove-password-os-auth-dialog-caption" }];
    if (platform == "win" || platform == "macosx") {
      ids.push({
        id: `autocomplete-remove-password-os-auth-dialog-message-${platform}`,
      });
    }
    const [caption, message = null] = await lazy.l10n.formatValues(ids);
    return { message, caption };
  },

  /**
   * @param {ChromeWindow} chromeWindow
   * @param {string} recordType
   * @param {string} [flowId] The flow id of the dropdown the removal came from.
   */
  async confirmRemoval(chromeWindow, recordType, flowId = "") {
    const [title, message, confirmButton] = await lazy.l10n.formatValues([
      { id: TITLE_L10N_IDS[recordType] },
      { id: "autocomplete-remove-record-message" },
      { id: "autocomplete-delete-record-button" },
    ]);

    const flags =
      Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0 +
      Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1;

    const result = await Services.prompt.asyncConfirmEx(
      chromeWindow.browsingContext,
      Services.prompt.MODAL_TYPE_INTERNAL_WINDOW,
      title,
      message,
      flags,
      confirmButton,
      null,
      null,
      null,
      false,
      {}
    );

    const confirmed =
      result.QueryInterface(Ci.nsIPropertyBag2).get("buttonNumClicked") == 0;
    Glean.autocomplete.recordDialogInteraction.record({
      record_type: recordType,
      interaction_type: confirmed ? "confirm" : "cancel",
      update_type: "delete",
      flow_id: flowId,
    });
    return confirmed;
  },
};
