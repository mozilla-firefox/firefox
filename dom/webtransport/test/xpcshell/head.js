/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// WebTransport now requires a ClientInfo, which only real window (and worker)
// globals provide.
let gWebTransportBrowser = Services.appShell.createWindowlessBrowser(true);

function webTransportWindow() {
  return gWebTransportBrowser.document.defaultView;
}

function newWebTransport(...args) {
  let win = webTransportWindow();
  return new win.WebTransport(...args);
}

registerCleanupFunction(() => {
  gWebTransportBrowser.close();
  gWebTransportBrowser = null;
});
