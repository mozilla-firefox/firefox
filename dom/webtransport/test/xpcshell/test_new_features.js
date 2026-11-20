/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests for new WebTransport features:
// * protocol attribute
// * draining promise
// * createSendGroup()
// * createWritable()
// * exportKeyingMaterial()
// * getStats()
// Very basic tests; most of this is tested via WPT tests

"use strict";

var h3Port;
var host;

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.webtransport.enabled");
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
  Services.prefs.setBoolPref("network.webtransport.enabled", true);

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
});

add_task(async function test_protocol_attribute() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Protocol attribute should be empty string when no protocols specified
  Assert.equal(wt.protocol, "");

  wt.close();
  await wt.closed;
});

add_task(async function test_draining_promise() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Draining promise should exist
  Assert.notEqual(wt.draining, null);
  Assert.equal(typeof wt.draining.then, "function");

  wt.close();
  await wt.closed;
});

add_task(async function test_createSendGroup() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Create a send group
  const sendGroup = wt.createSendGroup();
  Assert.notEqual(sendGroup, null);

  wt.close();
  await wt.closed;
});

add_task(async function test_createWritable() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Create a writable datagram stream
  const writable = wt.datagrams.createWritable();
  Assert.notEqual(writable, null);

  // Writable should have sendGroup and sendOrder attributes
  Assert.equal(writable.sendGroup, null);
  Assert.equal(writable.sendOrder, 0);

  // Set sendOrder
  writable.sendOrder = 42;
  Assert.equal(writable.sendOrder, 42);

  wt.close();
  await wt.closed;
});

add_task(async function test_createWritable_with_options() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Create a send group
  const sendGroup = wt.createSendGroup();

  // Create a writable with options
  const writable = wt.datagrams.createWritable({
    sendOrder: 100,
    sendGroup,
  });
  Assert.notEqual(writable, null);
  Assert.equal(writable.sendOrder, 100);
  Assert.equal(writable.sendGroup, sendGroup);

  wt.close();
  await wt.closed;
});

add_task(async function test_exportKeyingMaterial() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  // Export keying material with an empty context
  const label = new TextEncoder().encode("test-label");
  const keyMaterial = await wt.exportKeyingMaterial(
    label,
    new Uint8Array(0),
    32
  );
  Assert.notEqual(keyMaterial, null);
  Assert.equal(ArrayBuffer.isView(keyMaterial), true);

  // Export with a non-empty context
  const context = new TextEncoder().encode("test-context");
  const keyMaterial2 = await wt.exportKeyingMaterial(label, context, 32);
  Assert.notEqual(keyMaterial2, null);
  Assert.equal(ArrayBuffer.isView(keyMaterial2), true);

  wt.close();
  await wt.closed;
});

add_task(async function test_getStats() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;

  const stats = await wt.getStats();
  Assert.notEqual(stats, null);

  // Numeric connection stats
  Assert.equal(typeof stats.bytesSent, "number");
  // bytesSentOverhead is omitted: Firefox cannot decompose bytesSent into
  // payload vs. framing/retransmission overhead (see bug 2051624), and the
  // spec requires unavailable stats to be absent from the dictionary.
  Assert.equal(typeof stats.bytesSentOverhead, "undefined");
  Assert.equal(typeof stats.bytesAcknowledged, "number");
  Assert.equal(typeof stats.packetsSent, "number");
  Assert.equal(typeof stats.bytesLost, "number");
  Assert.equal(typeof stats.packetsLost, "number");
  Assert.equal(typeof stats.bytesReceived, "number");
  Assert.equal(typeof stats.packetsReceived, "number");

  // RTT fields
  Assert.equal(typeof stats.smoothedRtt, "number");
  Assert.equal(typeof stats.rttVariation, "number");
  Assert.equal(typeof stats.minRtt, "number");

  // estimatedSendRate is nullable
  Assert.ok(
    stats.estimatedSendRate === null ||
      typeof stats.estimatedSendRate === "number"
  );

  // Boolean capacity indicator
  Assert.equal(typeof stats.atSendCapacity, "boolean");

  // Datagram sub-stats (required field)
  Assert.notEqual(stats.datagrams, null);
  Assert.equal(typeof stats.datagrams.droppedIncoming, "number");
  Assert.equal(typeof stats.datagrams.expiredIncoming, "number");
  Assert.equal(typeof stats.datagrams.expiredOutgoing, "number");
  Assert.equal(typeof stats.datagrams.lostOutgoing, "number");

  wt.close();
  await wt.closed;
});

add_task(async function test_sendGroup_validation() {
  const wt1 = newWebTransport(`https://${host}/success`);
  const wt2 = newWebTransport(`https://${host}/success`);
  await wt1.ready;
  await wt2.ready;

  // Create send group on wt1
  const sendGroup = wt1.createSendGroup();

  // Try to use send group from wt1 on wt2's stream - should fail
  try {
    await wt2.createUnidirectionalStream({ sendGroup });
    Assert.ok(false, "Should have thrown InvalidStateError");
  } catch (e) {
    Assert.equal(e.name, "InvalidStateError");
  }

  wt1.close();
  wt2.close();
  await wt1.closed;
  await wt2.closed;
});

add_task(async function test_getStats_after_close() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;
  wt.close();
  await wt.closed;

  // Per spec, getStats() on a closed transport returns the last cached stats.
  const stats = await wt.getStats();
  Assert.notEqual(stats, null);
  Assert.equal(typeof stats.bytesSent, "number");
  Assert.equal(typeof stats.bytesReceived, "number");
  Assert.notEqual(stats.datagrams, null);
});

add_task(async function test_closed_state_errors() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;
  wt.close();
  await wt.closed;

  try {
    await wt.exportKeyingMaterial(
      new Uint8Array([1, 2, 3]),
      new Uint8Array(0),
      32
    );
    Assert.ok(false, "exportKeyingMaterial should have thrown");
  } catch (e) {
    Assert.equal(e.name, "InvalidStateError");
  }
});
