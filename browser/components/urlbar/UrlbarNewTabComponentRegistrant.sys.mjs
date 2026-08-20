/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AboutNewTabComponentRegistry,
  BaseAboutNewTabComponentRegistrant,
} from "moz-src:///browser/components/newtab/AboutNewTabComponents.sys.mjs";
import { UrlbarPrefs } from "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs";

const FEATURE_GATE_PREF = "newtab.featureGate";

/**
 * A registrant that adds `<moz-urlbar>` to about:newtab / about:home while
 * `browser.urlbar.newtab.featureGate` is enabled. It supersedes the handoff
 * search bar, which stands down for the same pref.
 */
export class UrlbarNewTabComponentRegistrant extends BaseAboutNewTabComponentRegistrant {
  constructor() {
    super();
    // Held weakly, so this instance must outlive the registration -- New Tab's
    // component registry keeps it alive.
    UrlbarPrefs.addObserver(this);
  }

  destroy() {
    UrlbarPrefs.removeObserver(this);
  }

  onPrefChanged(pref) {
    if (pref == FEATURE_GATE_PREF) {
      this.updated();
    }
  }

  getComponents() {
    if (!UrlbarPrefs.get(FEATURE_GATE_PREF)) {
      return [];
    }

    return [
      {
        type: AboutNewTabComponentRegistry.TYPES.SEARCH,
        l10nURLs: [
          "browser/browser.ftl",
          "browser/search.ftl",
          "preview/enUS-searchFeatures.ftl",
        ],
        componentURL: "chrome://browser/content/urlbar/UrlbarInput.mjs",
        tagName: "moz-urlbar",
        attributes: {
          class: "urlbar",
          pageproxystate: "invalid",
          popover: "manual",
          "sap-name": "newtab_searchbar",
          "unifiedsearchbutton-available": "",
        },
      },
    ];
  }
}
