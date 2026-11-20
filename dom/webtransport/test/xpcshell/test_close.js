//
//  Simple WebTransport test
//

"use strict";

var h3Port;
var host;

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
});

var { NetUtil } = ChromeUtils.importESModule(
  "resource://gre/modules/NetUtil.sys.mjs"
);

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

add_task(async function setup() {
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");

  h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");
  host = "foo.example.com:" + h3Port;
  do_get_profile();

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  // `../unit/` so that unit_ipc tests can use as well
  addCertFromFile(
    certdb,
    "../../../../netwerk/test/unit/http2-ca.pem",
    "CTu,u,u"
  );
});

add_task(async function test_webtransport_create() {
  const wt = newWebTransport("https://" + host + "/success");
  await wt.ready;
  dump("**** ready\n");

  wt.close();
});

// bug 1840626 - cancel and then close
add_task(async function test_wt_stream_create_bidi_cancel_close() {
  let wt = newWebTransport("https://" + host + "/success");
  await wt.ready;

  await wt.createBidirectionalStream();
  await wt.incomingBidirectionalStreams.cancel(undefined);
  wt.close();
});
