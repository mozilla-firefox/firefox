/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FunComputerAccounts: "resource:///modules/FunComputerAccounts.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", function () {
  return console.createInstance({
    prefix: "AIWindowAccountAuth",
    maxLogLevelPref: Services.prefs.getBoolPref(
      "browser.smartwindow.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "hasAIWindowToSConsent",
  "browser.smartwindow.tos.consentTime",
  0
);

export const AIWindowAccountAuth = {
  get hasToSConsent() {
    return !!lazy.hasAIWindowToSConsent;
  },

  set hasToSConsent(value) {
    const nowSeconds = Math.floor(Date.now() / 1000);

    Services.prefs.setIntPref(
      "browser.smartwindow.tos.consentTime",
      value ? nowSeconds : 0
    );
  },

  async isSignedIn() {
    try {
      return !!(await lazy.FunComputerAccounts.getSession());
    } catch (error) {
      lazy.log.error("Error checking sign-in status:", error);
      return false;
    }
  },

  async canAccessAIWindow() {
    if (!this.hasToSConsent) {
      this.hasToSConsent = true;
    }
    return true;
  },

  async promptSignIn(browser) {
    try {
      if (await this.isSignedIn()) {
        this.hasToSConsent = true;
        return true;
      }

      const win =
        browser?.ownerGlobal ||
        browser?.documentGlobal ||
        Services.wm.getMostRecentWindow("navigator:browser");
      if (!win?.gBrowser) {
        return false;
      }

      const url = Services.prefs.getStringPref(
        "funcomputer.accounts.homeUrl",
        "about:funcomputer"
      );
      let tab = win.gBrowser.tabs.find(t =>
        t.linkedBrowser?.currentURI?.spec?.startsWith("about:funcomputer")
      );
      if (!tab) {
        tab = win.gBrowser.addTrustedTab(url, { relatedToCurrent: true });
      }
      win.gBrowser.selectedTab = tab;

      return await new Promise(resolve => {
        let done = false;
        const finish = ok => {
          if (done) {
            return;
          }
          done = true;
          try {
            Services.obs.removeObserver(
              observer,
              lazy.FunComputerAccounts.TOPIC
            );
          } catch {}
          tab.removeEventListener("TabClose", onClose);
          if (ok) {
            this.hasToSConsent = true;
          }
          resolve(ok);
        };
        const observer = {
          async observe() {
            if (await lazy.FunComputerAccounts.getSession()) {
              finish(true);
            }
          },
        };
        const onClose = () => finish(false);
        Services.obs.addObserver(observer, lazy.FunComputerAccounts.TOPIC);
        tab.addEventListener("TabClose", onClose, { once: true });
      });
    } catch (error) {
      lazy.log.error("Error prompting sign-in:", error);
      throw error;
    }
  },

  async ensureAIWindowAccess(_browser) {
    if (!this.hasToSConsent) {
      this.hasToSConsent = true;
    }
    return true;
  },
};
