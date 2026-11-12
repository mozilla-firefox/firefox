/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

export let SerialDeviceSharingHelper = {
  // Keyed by browserId rather than by <browser> element so the count follows
  // the content when tabs are moved between windows.
  _activePortCounts: new Map(),

  observe(subject, topic, _data) {
    if (topic != "serial-device-state-changed") {
      return;
    }

    let props = subject.QueryInterface(Ci.nsIPropertyBag2);
    const browserId = props.getPropertyAsUint64("browserId");
    let connected = props.getPropertyAsBool("connected");
    let count = this._activePortCounts.get(browserId) || 0;
    count = connected ? count + 1 : Math.max(0, count - 1);
    if (count > 0) {
      this._activePortCounts.set(browserId, count);
    } else {
      this._activePortCounts.delete(browserId);
    }

    let bc = BrowsingContext.getCurrentTopByBrowserId(browserId);
    if (!bc) {
      console.warn("BrowsingContext not found for browser ID:", browserId);
      return;
    }
    let browser = bc.embedderElement;
    if (!browser) {
      console.warn("No embedder element for BrowsingContext");
      return;
    }

    browser.documentGlobal?.gBrowser?.updateBrowserSharing(browser, {
      serial: count > 0 ? "serial" : null,
    });
  },

  resetBrowserCount(browser) {
    this._activePortCounts.delete(browser.browserId);
  },
};
