/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
});

export const FUNSHIELD_ID = "funshield@funxplorer";
export const TOPIC = "funxplorer-shield-changed";

async function getAddon() {
  try {
    return await lazy.AddonManager.getAddonByID(FUNSHIELD_ID);
  } catch {
    return null;
  }
}

const listener = {
  onEnabled(addon) {
    if (addon.id === FUNSHIELD_ID) {
      Services.obs.notifyObservers(null, TOPIC);
    }
  },
  onDisabled(addon) {
    if (addon.id === FUNSHIELD_ID) {
      Services.obs.notifyObservers(null, TOPIC);
    }
  },
  onInstalled(addon) {
    if (addon.id === FUNSHIELD_ID) {
      Services.obs.notifyObservers(null, TOPIC);
    }
  },
};

let listening = false;
function ensureListener() {
  if (listening) {
    return;
  }
  listening = true;
  lazy.AddonManager.addAddonListener(listener);
}

export const FunxplorerShield = {
  async isEnabled() {
    ensureListener();
    const addon = await getAddon();
    return !!(addon && !addon.userDisabled && addon.isActive);
  },

  async toggle() {
    const addon = await getAddon();
    if (!addon) {
      return { ok: false, error: "FUNSHIELD is not installed." };
    }
    if (addon.userDisabled) {
      await addon.enable();
    } else {
      await addon.disable();
    }
    Services.obs.notifyObservers(null, TOPIC);
    return { ok: true, enabled: await this.isEnabled() };
  },

  async openDashboard(win) {
    const addon = await getAddon();
    if (!addon) {
      return { ok: false, error: "FUNSHIELD is not installed." };
    }
    const url = addon.optionsURL;
    if (!url || !win?.openTrustedLinkIn) {
      return { ok: false, error: "FUNSHIELD dashboard is unavailable." };
    }
    win.openTrustedLinkIn(url, "tab");
    return { ok: true };
  },
};
