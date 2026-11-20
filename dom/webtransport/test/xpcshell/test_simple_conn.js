/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Some basic WebTransport tests for:
// * session rejection and redirection
// * session and stream creation
// * reading from incoming streams (uni)
//

"use strict";

var h3Port;
var host;

/* eslint no-unused-vars: 0 */
const dns = Services.dns;

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.webtransport.redirect.enabled");
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

add_setup(async function setup() {
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setBoolPref("network.webtransport.redirect.enabled", true);

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
  wt.close();
});

add_task(async function test_redirect_wt() {
  let wt = newWebTransport("https://" + host + "/redirect");
  const e1 = await wt.ready.catch(e => e);
  const e2 = await wt.closed.catch(e => e);

  Assert.equal(e1, "WebTransportError: WebTransport connection rejected");
  Assert.equal(e2, "WebTransportError: WebTransport connection rejected");
});

add_task(async function test_reject_wt() {
  let wt = newWebTransport("https://" + host + "/reject");
  const e1 = await wt.ready.catch(e => e);
  const e2 = await wt.closed.catch(e => e);
  Assert.equal(e1, "WebTransportError: WebTransport connection rejected");
  Assert.equal(e2, "WebTransportError: WebTransport connection rejected");
});

add_task(async function test_immediate_server_close() {
  let wt = newWebTransport("https://" + host + "/closeafter0ms");
  await wt.ready;
  await wt.closed;
  Assert.ok(true);
});

add_task(async function test_delayed_server_close() {
  let wt = newWebTransport("https://" + host + "/closeafter100ms");
  await wt.ready;
  await wt.closed;
  Assert.ok(true);
});

add_task(async function test_wt_stream_create_bidi() {
  let wt = newWebTransport("https://" + host + "/success");
  await wt.ready;

  let bds = await wt.createBidirectionalStream();
  await bds.writable.close();
  await bds.readable.cancel();
  Assert.notEqual(bds, null);
  wt.close();
});

add_task(async function test_wt_stream_create_uni() {
  let wt = newWebTransport("https://" + host + "/success");
  await wt.ready;

  let uds = await wt.createUnidirectionalStream();
  Assert.notEqual(uds, null);
  await uds.close();
  wt.close();
});

// TODO: datagram test
// TODO: getStats tests
// TODO: fix the crash discussed in bug 1822154
