/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { AuthProviderImpl } from "moz-src:///browser/components/aiwindow/models/auth/AuthProviderImpl.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const PREF_TOKEN = "browser.smartwindow.auth.guest.token";
const PREF_MODE = "browser.smartwindow.auth.guest.mode";
const VERIFY_PATH = "/verify/v1/guest";
const SUCCESS_PATH = "/verify/v1/guest/success";

const lazy = XPCOMUtils.declareLazy({
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  ToUSpotlight:
    "moz-src:///browser/components/aiwindow/ui/modules/ToUSpotlight.sys.mjs",
  token: {
    pref: PREF_TOKEN,
    default: "",
  },
  mode: {
    pref: PREF_MODE,
    default: "fetch",
  },
});

/**
 * AuthProviderImpl that obtains a guest token from MLPA, without an account.
 */
export class GuestAuthProviderImpl extends AuthProviderImpl {
  static cleanup() {
    Services.prefs.clearUserPref(PREF_TOKEN);
  }

  get providerName() {
    return "guest";
  }

  async getToken() {
    return lazy.token || null;
  }

  async invalidateToken(token) {
    if (token === lazy.token) {
      Services.prefs.clearUserPref(PREF_TOKEN);
      this.dispatchEvent(
        new CustomEvent("AuthProviderImpl:FallbackNeeded", {
          detail: { reason: "token_rejected" },
        })
      );
    }
  }

  async isSignedIn() {
    return !!lazy.token;
  }

  async canSignIn() {
    return true;
  }

  async signIn(browser) {
    if (
      !lazy.AuthProvider.hasToUConsent &&
      !(await lazy.ToUSpotlight.show(browser))
    ) {
      return false;
    }

    const origin = new URL(lazy.openAIEngine.endpoint).origin;
    const state = crypto.randomUUID();
    const url = `${origin}${VERIFY_PATH}?state=${encodeURIComponent(state)}`;

    if (lazy.mode === "tab") {
      return this.#signInWithTab(browser, url, origin, state);
    }
    try {
      return await this.#signInWithFetch(url);
    } catch (error) {
      await lazy.ToUSpotlight.showUnavailable(browser).catch(e =>
        console.error("Error showing unavailable modal:", e)
      );
      throw error;
    }
  }

  async #signInWithFetch(url) {
    const response = await fetch(url, {
      credentials: "omit",
      headers: { Accept: "application/json" },
    });
    if (!response.ok) {
      throw new Error(`Guest token request failed: ${response.status}`);
    }

    const { access_token: token, token_type: tokenType } =
      await response.json();
    if (typeof token !== "string" || !token || tokenType !== "Bearer") {
      throw new Error("Invalid guest token response");
    }

    Services.prefs.setStringPref(PREF_TOKEN, token);
    return true;
  }

  async #signInWithTab(browser, url, origin, state) {
    const guestBrowser = await new Promise(resolve => {
      browser.documentGlobal.openLinkIn(url, "tab", {
        private: false,
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
        resolveOnContentBrowserCreated: resolve,
        forceForeground: true,
      });
    });

    const gBrowser = guestBrowser.getTabBrowser();
    const guestTab = gBrowser.getTabForBrowser(guestBrowser);

    const token = await new Promise(resolve => {
      const controller = new AbortController();
      const { signal } = controller;
      const finish = value => {
        controller.abort();
        resolve(value);
      };

      const progressListener = {
        QueryInterface: ChromeUtils.generateQI([
          Ci.nsIWebProgressListener,
          Ci.nsISupportsWeakReference,
        ]),

        onLocationChange(webProgress, _request, location) {
          if (
            !webProgress.isTopLevel ||
            location.prePath !== origin ||
            location.filePath !== SUCCESS_PATH
          ) {
            return;
          }
          const params = new URLSearchParams(location.ref);
          if (params.get("state") !== state) {
            return;
          }
          finish(params.get("access_token"));
        },
      };

      guestTab.addEventListener("TabClose", () => finish(null), {
        once: true,
        signal,
      });
      guestTab.documentGlobal.addEventListener("unload", () => finish(null), {
        once: true,
        signal,
      });

      guestBrowser.addProgressListener(
        progressListener,
        Ci.nsIWebProgress.NOTIFY_LOCATION
      );
      signal.addEventListener(
        "abort",
        () => guestBrowser.removeProgressListener(progressListener),
        { once: true }
      );
    });

    if (!token) {
      return false;
    }

    Services.prefs.setStringPref(PREF_TOKEN, token);

    if (
      Services.prefs.getBoolPref(
        "browser.smartwindow.firstrun.hasCompleted",
        false
      )
    ) {
      gBrowser.removeTab(guestTab);
    }

    return true;
  }
}
