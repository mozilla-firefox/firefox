/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "REFERRALS_ENABLED",
  "browser.referrals.enabled",
  false
);

const REFERRAL_CODE_PREF = "browser.referrals.code";
const REFERRAL_CODE_LENGTH = 16;
// Crockford base32
const REFERRAL_CODE_CHARSET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/**
 * Manages the Firefox Referral program's per-profile referral code.
 */
class ReferralsClass {
  maybeLockPref() {
    if (Services.prefs.prefIsLocked(REFERRAL_CODE_PREF)) {
      return;
    }

    // Lock the pref if it exists so that it can't be changed. Locking the pref
    // will reset the value to the default. If the pref doesn't exist yet,
    // locking the pref will cause the default pref value to be an empty string
    // meaning a new code will be generated every time.
    let code = Services.prefs.getStringPref(REFERRAL_CODE_PREF, "");
    if (code.length) {
      Services.prefs.lockPref(REFERRAL_CODE_PREF);
    }
  }

  get isEnabled() {
    return lazy.REFERRALS_ENABLED;
  }

  /**
   * Returns the profile's referral code, generating and storing one on first
   * access. Returns an empty string when referrals are disabled.
   *
   * @returns {string} The referral code || ""
   */
  getReferralCode() {
    if (!this.isEnabled) {
      return "";
    }

    // A locked pref reads its default, so unlock to read the user-branch code.
    if (Services.prefs.prefIsLocked(REFERRAL_CODE_PREF)) {
      Services.prefs.unlockPref(REFERRAL_CODE_PREF);
    }

    let code = Services.prefs.getStringPref(REFERRAL_CODE_PREF, "");
    if (!code) {
      code = this.#generateCode();
      Services.prefs.setStringPref(REFERRAL_CODE_PREF, code);
    }

    Services.prefs.lockPref(REFERRAL_CODE_PREF);
    return code;
  }

  /**
   * Opens about:referrals in a new tab. No-op when referrals are disabled.
   *
   * @param {Window} window The browser window to open the tab in.
   * @param {string} entrypoint
   *   The UI surface the tab was opened from. One of: app_menu,
   *   accounts_menu, preferences, help_menu, about_dialog.
   */
  openReferralsTab(window, entrypoint) {
    if (!this.isEnabled) {
      return;
    }
    Glean.referrals.entrypointClicked.record({ entrypoint });
    let referralCode = this.getReferralCode();
    let aboutPageURL = new URL(`about:referrals`);

    aboutPageURL.searchParams.set("ref_key", referralCode);

    window.openTrustedLinkIn(aboutPageURL.toString(), "tab");
  }

  /**
   * Generates a Referral code that will persist and be saved per-profile
   *
   * @returns {string} A newly generated referral code.
   */
  #generateCode() {
    const bytes = new Uint8Array(REFERRAL_CODE_LENGTH);
    crypto.getRandomValues(bytes);

    let code = "";
    for (const byte of bytes) {
      code += REFERRAL_CODE_CHARSET[byte % REFERRAL_CODE_CHARSET.length];
    }
    return code;
  }
}

export const Referrals = new ReferralsClass();
