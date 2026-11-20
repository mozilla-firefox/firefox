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

var { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

var h3Port;
var host;

function readFile(file) {
  let fstream = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
    Ci.nsIFileInputStream
  );
  fstream.init(file, -1, 0, 0);
  let data = NetUtil.readInputStreamToString(fstream, fstream.available());
  fstream.close();
  return data;
}

function addCertFromFile(certdb, filename, trustString) {
  let certFile = do_get_file(filename, false);
  let pem = readFile(certFile)
    .replace(/-----BEGIN CERTIFICATE-----/, "")
    .replace(/-----END CERTIFICATE-----/, "")
    .replace(/[\r\n]/g, "");
  certdb.addCertFromBase64(pem, trustString);
}

// Points the WebTransport tests at the local HTTP/3 server and trusts its
// certificate.
function setupWebTransportServer() {
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");

  h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");
  host = "foo.example.com:" + h3Port;
  do_get_profile();

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(
    certdb,
    "../../../../netwerk/test/unit/http2-ca.pem",
    "CTu,u,u"
  );
}
