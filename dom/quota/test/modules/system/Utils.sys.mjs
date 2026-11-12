/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

import {
  getCachedUsageForOrigin,
  resetStorage,
} from "resource://testing-common/dom/quota/test/modules/StorageUtils.sys.mjs";

// Ensure NSS is initialized on the main thread. Quota clients that use NSS
// for cipher key generation (e.g. private browsing localStorage) dispatch
// synchronously to the main thread from the QuotaManager IO thread; calling
// EnsureNSSInitializedChromeOrContent from the IO thread would deadlock with
// DoRequestSynchronously. In a real browser session NSS is always initialized
// during startup; this is only needed for xpcshell.
export function ensureNSSInitialized() {
  let psm = Cc["@mozilla.org/psm;1"];
  if (psm) {
    psm.getService(Ci.nsISupports);
  }
}

export const Utils = {
  async getCachedOriginUsage() {
    const principal = Cc["@mozilla.org/systemprincipal;1"].createInstance(
      Ci.nsIPrincipal
    );
    const result = await getCachedUsageForOrigin(principal);
    return result;
  },

  async isPrivateBrowsing() {
    const principal = Cc["@mozilla.org/systemprincipal;1"].createInstance(
      Ci.nsIPrincipal
    );
    // Always false by construction for the system principal.
    return principal.isInPrivateBrowsing;
  },

  async shrinkStorageSize(size) {
    Services.prefs.setIntPref(
      "dom.quotaManager.temporaryStorage.fixedLimit",
      size
    );

    const result = await resetStorage();
    return result;
  },

  async restoreStorageSize() {
    Services.prefs.clearUserPref(
      "dom.quotaManager.temporaryStorage.fixedLimit"
    );

    const result = await resetStorage();
    return result;
  },
};
