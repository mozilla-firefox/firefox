/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PREF_MODE = "browser.speed.mode";
const PREF_APPLIED = "browser.speed.applied";

// Pref bundles per mode. Performance favors aggressive speculative networking;
// Fortress favors blocking. Only prefs listed here are managed by SpeedMode —
// anything the user set manually outside this list is left alone, and managed
// prefs a user overwrote after we applied them are not reclaimed.
const MODE_BUNDLES = {
  // 0 = Performance
  0: {
    "network.http.speculative-parallel-limit": 40,
    "network.http.max-persistent-connections-per-server": 10,
    "network.dnsCacheExpiration": 300,
    "network.predictor.enable-hover-on-ssl": true,
    "dom.prefetch_dns_for_anchor_https_document": true,
    "dom.speculation_rules.enabled": true,
    "dom.speculation_rules.hover_prefetch_all": true,
  },
  // 1 = Balanced — no managed prefs; restores defaults for anything we set.
  1: {},
  // 2 = Fortress — strict tracking protection incl. cookie-banner handling.
  2: {
    "browser.contentblocking.category": "strict",
  },
};

function readApplied() {
  try {
    return JSON.parse(Services.prefs.getStringPref(PREF_APPLIED, "{}"));
  } catch {
    return {};
  }
}

function setPref(name, value) {
  switch (typeof value) {
    case "boolean":
      Services.prefs.setBoolPref(name, value);
      break;
    case "number":
      Services.prefs.setIntPref(name, value);
      break;
    default:
      Services.prefs.setStringPref(name, value);
  }
}

export const SpeedMode = {
  init() {
    this.apply();
    Services.prefs.addObserver(PREF_MODE, () => this.apply());
  },

  apply() {
    const mode = Services.prefs.getIntPref(PREF_MODE, 1);
    const bundle = MODE_BUNDLES[mode];
    if (!bundle) {
      return;
    }

    const applied = readApplied();

    // Clear prefs we set previously that the new mode doesn't manage, unless
    // the user has since overwritten them.
    for (const [name, value] of Object.entries(applied)) {
      if (!(name in bundle) && Services.prefs.prefHasUserValue(name)) {
        // Only clear if the current value still matches what we wrote;
        // a different user value means the user took it over.
        const current = Services.prefs.getPrefType(name)
          ? this.getPref(name)
          : value;
        if (current === value) {
          Services.prefs.clearUserPref(name);
        }
        delete applied[name];
      }
    }

    for (const [name, value] of Object.entries(bundle)) {
      // Don't clobber a pref the user customized on top of our value.
      if (
        Services.prefs.prefHasUserValue(name) &&
        !(name in applied) &&
        this.getPref(name) !== value
      ) {
        continue;
      }
      setPref(name, value);
      applied[name] = value;
    }

    Services.prefs.setStringPref(PREF_APPLIED, JSON.stringify(applied));
  },

  getPref(name) {
    switch (Services.prefs.getPrefType(name)) {
      case Services.prefs.PREF_BOOL:
        return Services.prefs.getBoolPref(name);
      case Services.prefs.PREF_INT:
        return Services.prefs.getIntPref(name);
      default:
        return Services.prefs.getStringPref(name);
    }
  },
};
