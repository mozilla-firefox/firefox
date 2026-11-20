/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Basic tests for WebTransport session API features:
// * protocol attribute
// * draining promise
// * createSendGroup()
// * createWritable()
// * exportKeyingMaterial()
// * getStats()
// Very basic tests; most of this is tested via WPT tests

"use strict";

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref("network.dns.localDomains");
  Services.prefs.clearUserPref("network.webtransport.enabled");
});

add_setup(async function setup() {
  Services.prefs.setBoolPref("network.webtransport.enabled", true);
  setupWebTransportServer();
});

async function openSession() {
  const wt = newWebTransport(`https://${host}/success`);
  await wt.ready;
  return wt;
}

add_task(async function test_protocol_attribute() {
  const wt = await openSession();

  // [[Protocol]] is initially the empty string, and no protocols were requested.
  Assert.equal(wt.protocol, "");

  wt.close();
  await wt.closed;
});

add_task(async function test_draining_promise() {
  const wt = await openSession();

  // The promise belongs to the window's global, so check its class rather than
  // using instanceof against this scope's Promise.
  Assert.equal(ChromeUtils.getClassName(wt.draining), "Promise");

  wt.close();
  await wt.closed;
});

add_task(async function test_createSendGroup() {
  const wt = await openSession();

  // Create a send group
  const sendGroup = wt.createSendGroup();
  Assert.ok(sendGroup instanceof webTransportWindow().WebTransportSendGroup);

  wt.close();
  await wt.closed;
});

add_task(async function test_createWritable() {
  const wt = await openSession();

  // Create a writable datagram stream
  const writable = wt.datagrams.createWritable();
  Assert.ok(
    writable instanceof webTransportWindow().WebTransportDatagramsWritable
  );

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
  const wt = await openSession();

  // Create a send group
  const sendGroup = wt.createSendGroup();

  // Create a writable with options
  const writable = wt.datagrams.createWritable({
    sendOrder: 100,
    sendGroup,
  });
  Assert.ok(
    writable instanceof webTransportWindow().WebTransportDatagramsWritable
  );
  Assert.equal(writable.sendOrder, 100);
  Assert.equal(writable.sendGroup, sendGroup);

  wt.close();
  await wt.closed;
});

add_task(async function test_exportKeyingMaterial() {
  const wt = await openSession();

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
  const wt = await openSession();

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
  const wt1 = await openSession();
  const wt2 = await openSession();

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
  const wt = await openSession();
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
  const wt = await openSession();
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
