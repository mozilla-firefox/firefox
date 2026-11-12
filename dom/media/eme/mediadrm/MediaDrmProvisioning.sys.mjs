/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

export function MediaDrmProvisioning() {}

MediaDrmProvisioning.prototype = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIMediaDrmProvisioning",
    "nsISupport",
  ]),

  provision(serverUrl, request) {
    // Tests point this at a local server to simulate a provisioning failure.
    const override = Services.prefs.getStringPref(
      "media.eme.mediadrm.provisioning.url-override",
      ""
    );
    const url = (override || serverUrl) + "&signedRequest=" + request;
    return new Promise((resolve, reject) => {
      fetch(url, { method: "POST" })
        .then(response => {
          if (!response.ok) {
            reject(new Error(`Provisioning failed: ${response.status}`));
            return;
          }
          response
            .bytes()
            .then(bytes => {
              resolve(bytes);
            })
            .catch(reject);
        })
        .catch(reject);
    });
  },
};
