/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module re-exports the UrlbarPrefs singleton from the system global.
 * Content modules (.mjs) should import UrlbarPrefs from here instead of
 * UrlbarPrefs.sys.mjs. This ensures that the import works even in child
 * processes where the Urlbar actor is loaded.
 */

/**
 * @import {UrlbarPrefs} from "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs"
 */

function getPrefs() {
  if (typeof ChromeUtils != "undefined") {
    return ChromeUtils.importESModule(
      "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs"
    ).UrlbarPrefs;
  }

  // In child processes the Urlbar actor exposes important UrlbarPrefs methods on
  // the window, as part of the single port it publishes there. To expose more
  // methods, change the Urlbar actor.
  let get = p => window.UrlbarActorPort.getPref(p);
  return /** @type {Pick<typeof UrlbarPrefs, "get" | "getScotchBonnetPref" | "addObserver" | "removeObserver">}*/ ({
    get,
    // Composed from `get` rather than exposed on the port, since that is all the
    // privileged implementation does.
    getScotchBonnetPref: p => get("scotchBonnet.enableOverride") || get(p),
    addObserver: o => window.UrlbarActorPort.addPrefObserver(o),
    removeObserver: o => window.UrlbarActorPort.removePrefObserver(o),
  });
}

export default getPrefs();
