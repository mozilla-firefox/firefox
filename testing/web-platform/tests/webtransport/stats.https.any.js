// META: global=window,worker
// META: script=resources/webtransport-test-helpers.sub.js

function validate_rtt_stats(stats) {
  assert_true('datagrams' in stats, 'datagrams is required');
  assert_equals(typeof stats.datagrams, 'object');
  assert_not_equals(stats.datagrams, null);
  assert_true('estimatedSendRate' in stats,
              'estimatedSendRate defaults to null');
  if (stats.estimatedSendRate !== null) {
    assert_greater_than_equal(stats.estimatedSendRate, 0, 'estimatedSendRate');
  }
  // The assumption below is that the RTT to localhost is under 5 seconds,
  // which is fairly generous.
  if ("minRtt" in stats) {
    assert_greater_than(stats.minRtt, 0, "minRtt");
    assert_less_than(stats.minRtt, 5 * 1000, "minRtt");
  }
  if ("smoothedRtt" in stats) {
    assert_greater_than(stats.smoothedRtt, 0, "smoothedRtt");
    assert_less_than(stats.smoothedRtt, 5 * 1000, "smoothedRtt");
  }

  if ("rttVariation" in stats) {
    assert_greater_than_equal(stats.rttVariation, 0, "rttVariation");
  }
}

const COUNTER_STATS = ["bytesSent", "bytesSentOverhead", "bytesAcknowledged",
                       "packetsSent", "bytesLost", "packetsLost",
                       "bytesReceived", "packetsReceived"];

// getStats() only sets the members the user agent wishes to expose, and
// unavailable stats are absent, so most members are optional.  The exceptions
// are "datagrams", which is required, and "estimatedSendRate" and
// "atSendCapacity", which have default values; WebIDL guarantees all three are
// always present.
function validate_connection_stats(stats) {
  validate_rtt_stats(stats);
  for (const member of COUNTER_STATS) {
    if (member in stats) {
      assert_greater_than_equal(stats[member], 0, member);
    }
  }
  assert_equals(typeof stats.datagrams, "object", "datagrams");
  assert_true("estimatedSendRate" in stats, "estimatedSendRate is present");
  if (stats.estimatedSendRate !== null) {
    assert_greater_than(stats.estimatedSendRate, 0, "estimatedSendRate when not null");
  }
  assert_equals(typeof stats.atSendCapacity, "boolean", "atSendCapacity");
}

function assert_not_decreased(before, after, members) {
  for (const member of members) {
    if (member in before && member in after) {
      assert_greater_than_equal(after[member], before[member],
                                `${member} should not decrease`);
    }
  }
}

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;
  const stats = await wt.getStats();
  validate_connection_stats(stats);

  if ("expiredOutgoing" in stats.datagrams) {
    assert_equals(stats.datagrams.expiredOutgoing, 0);
  }
  if ("droppedIncoming" in stats.datagrams) {
    assert_equals(stats.datagrams.droppedIncoming, 0);
  }
  if ('expiredIncoming' in stats.datagrams) {
    assert_equals(stats.datagrams.expiredIncoming, 0);
  }
  if ("lostOutgoing" in stats.datagrams) {
    assert_equals(stats.datagrams.lostOutgoing, 0);
  }
  if ("expiredIncoming" in stats.datagrams) {
    assert_equals(stats.datagrams.expiredIncoming, 0);
  }

  // The handshake has completed, so traffic has flowed in both directions.
  for (const member of ["bytesSent", "bytesReceived", "packetsSent", "packetsReceived"]) {
    if (member in stats) {
      assert_greater_than(stats[member], 0, `${member} after handshake`);
    }
  }
}, "WebTransport client should be able to provide stats after connection has been established");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;
  wt.close();

  const stats = await wt.getStats();
  validate_rtt_stats(stats);
}, "WebTransport client should be able to provide stats after connection has been closed");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('server-close.py?code=42'));
  await wt.ready;
  const {closeCode:code} = await wt.closed;
  assert_equals(code, 42);
  wt.close();

  const wt2 = new WebTransport(webtransport_url('server-close.py?code=0'));
  await wt2.ready;
  const {closeCode: code2} = await wt2.closed;
  assert_equals(code2, 0);
  wt2.close();

  // Server closes immediately, so RTT stats may be 0. Handshake bytes are
  // always exchanged though, regardless of close code or timing, so if the
  // byte counts are exposed assert strictly greater than 0 rather than the
  // always-true >= 0.
  for (const [name, transport] of [["wt", wt], ["wt2", wt2]]) {
    const stats = await transport.getStats();
    for (const member of ["bytesSent", "bytesReceived"]) {
      if (member in stats) {
        assert_greater_than(stats[member], 0, `${name} ${member}`);
      }
    }
  }
}, "WebTransport client should be able to provide stats after server closes connection");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;
  const statsPromise = wt.getStats();
  wt.close();

  const stats = await statsPromise;
  validate_rtt_stats(stats);
}, "WebTransport client should be able to provide stats requested right before connection has been closed");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  // getStats() must return a Promise without throwing, even while CONNECTING.
  const p = wt.getStats();
  assert_true(p instanceof Promise, 'getStats() during CONNECTING must return a Promise');

  // getStats() must not resolve before ready: step 4.1 waits for the connecting
  // state to change (which is when ready resolves) before queueing the resolve.
  const order = [];
  const ready = wt.ready.then(() => { order.push('ready'); });
  const stats = p.then(() => { order.push('stats'); });
  await Promise.all([ready, stats]);
  assert_equals(order[0], 'ready', 'getStats() must not resolve before ready');

  // The promise must resolve with valid stats once the connection is established.
  validate_rtt_stats(await p);
}, 'getStats() during CONNECTING returns a Promise that resolves once connected');

promise_test(async t => {
  const wt = new WebTransport("https://webtransport.invalid/");
  wt.ready.catch(e => {});
  wt.closed.catch(e => {});
  const error = await wt.getStats().catch(e => e);
  assert_equals(error.code, DOMException.INVALID_STATE_ERR);
  const error2 = await wt.getStats().catch(e => e);
  assert_equals(error2.code, DOMException.INVALID_STATE_ERR);
}, "WebTransport client should throw an error when stats are requested for a failed connection");

// Test spec step 4.2: getStats() called while still CONNECTING must reject
// (not resolve) once the connection fails.
promise_test(async t => {
  const wt = new WebTransport(webtransport_url('custom-response.py?:status=404'));
  // Intentionally called before ready, so the transport is CONNECTING.
  const p = wt.getStats();
  assert_true(p instanceof Promise, 'getStats() during CONNECTING must return a Promise');

  // getStats() must reject *after* ready rejects and closed settles: the spec
  // queues the rejection task only once [[State]] has become "failed", which is
  // also when ready/closed settle.
  const order = [];
  const ready = wt.ready.catch(() => { order.push('ready'); });
  const closed = wt.closed.catch(() => { order.push('closed'); });
  const stats = p.catch(() => { order.push('stats'); });
  await Promise.all([ready, closed, stats]);
  assert_equals(order[order.length - 1], 'stats',
                'getStats() must reject after ready and closed settle');

  await promise_rejects_dom(t, 'InvalidStateError', p);
}, 'getStats() called while CONNECTING rejects with InvalidStateError when connection fails');

// Test spec step 3: getStats() called on an already-FAILED transport must
// return a rejected Promise, not throw synchronously.
promise_test(async t => {
  const wt = new WebTransport(webtransport_url('custom-response.py?:status=404'));
  wt.closed.catch(() => {});
  // Wait until the transport has fully transitioned to the FAILED state.
  await wt.ready.catch(() => {});
  // getStats() must return a rejected Promise (not a synchronous throw).
  const p = wt.getStats();
  assert_true(p instanceof Promise, 'getStats() on a FAILED transport must return a Promise');
  await promise_rejects_dom(t, 'InvalidStateError', p);
  // Subsequent calls must also reject consistently.
  await promise_rejects_dom(t, 'InvalidStateError', wt.getStats());
}, 'getStats() on an already-failed transport returns a rejected Promise');

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;
  const stats1 = wt.getStats();
  const stats2 = wt.getStats();
  assert_true(stats1 != stats2, "different promise returned for different getStats() calls");
  validate_rtt_stats(await stats1);
  validate_rtt_stats(await stats2);
}, "WebTransport client should be able to handle multiple concurrent stats requests");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;
  const stats1 = await wt.getStats();
  validate_rtt_stats(stats1);
  const stats2 = await wt.getStats();
  validate_rtt_stats(stats2);
}, "WebTransport client should be able to handle multiple sequential stats requests");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;

  const numDatagrams = 64;
  wt.datagrams.incomingMaxBufferedDatagrams = 4;

  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();
  const promises = [];
  while (promises.length < numDatagrams) {
    const token = promises.length.toString();
    promises.push(writer.write(encoder.encode(token)));
  }
  await Promise.all(promises);

  const maxAttempts = 40;
  let stats;
  for (let i = 0; i < maxAttempts; i++) {
    await wait(50);
    stats = await wt.getStats();
    if ("droppedIncoming" in stats.datagrams && stats.datagrams.droppedIncoming > 0) {
      break;
    }
  }
  if ("droppedIncoming" in stats.datagrams) {
    assert_greater_than(stats.datagrams.droppedIncoming, 0);
    assert_less_than_equal(stats.datagrams.droppedIncoming,
                             numDatagrams - wt.datagrams.incomingMaxBufferedDatagrams);
  }
}, "WebTransport client should be able to provide droppedIncoming values for datagrams");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('stats-data-transfer.py'));
  await wt.ready;

  // Get initial stats
  const initialStats = await wt.getStats();

  // Read the data sent by the server to generate traffic
  const reader = wt.incomingBidirectionalStreams.getReader();
  const { value: stream } = await reader.read();
  reader.releaseLock();

  // Read all data from the stream
  const streamReader = stream.readable.getReader();
  let totalBytesRead = 0;
  while (true) {
    const { done, value } = await streamReader.read();
    if (done) break;
    totalBytesRead += value.length;
  }
  streamReader.releaseLock();

  // Verify we actually read the expected data
  assert_greater_than(totalBytesRead, 4000, "Should have read at least 4KB from stream");

  // Send some data back
  const writer = stream.writable.getWriter();
  const testData = new Uint8Array(5000);
  testData.fill(42);
  await writer.write(testData);
  await writer.close();

  // Wait a bit for stats to update
  await wait(50);

  // Get final stats
  const finalStats = await wt.getStats();

  validate_connection_stats(finalStats);

  // Packet counts should always increase with activity.
  for (const member of ["packetsReceived", "packetsSent"]) {
    if (member in initialStats && member in finalStats) {
      assert_greater_than(finalStats[member], initialStats[member],
                          `${member} should increase`);
    }
  }

  // bytesReceived/bytesSent may increase by less than the application data
  // due to implementation details.
  assert_not_decreased(initialStats, finalStats, ["bytesReceived", "bytesSent"]);

  if ("bytesAcknowledged" in finalStats) {
    assert_greater_than(finalStats.bytesAcknowledged, 0,
                        "bytesAcknowledged should be positive after data transfer");
  }
}, "WebTransport stats should accurately track data transfer");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;

  const stats1 = await wt.getStats();
  const stats2 = await wt.getStats();

  // Sequential stats calls should show monotonically increasing or equal values
  // (equal if no traffic between calls)
  assert_not_decreased(stats1, stats2, ["bytesSent", "bytesReceived", "packetsSent",
                                        "packetsReceived", "bytesAcknowledged"]);
  // bytesLost and packetsLost are not monotonically increasing per spec:
  // packets initially declared lost can subsequently be received.

  // minRtt should not increase (it's the minimum observed)
  if ("minRtt" in stats1 && "minRtt" in stats2) {
    assert_less_than_equal(stats2.minRtt, stats1.minRtt,
                           "minRtt should not increase");
  }
}, "WebTransport stats should maintain monotonicity");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;

  const stats = await wt.getStats();

  const has = (...members) => members.every(m => m in stats);

  // Verify RTT variation is reasonable
  if (has("rttVariation", "smoothedRtt")) {
    assert_less_than(stats.rttVariation, stats.smoothedRtt * 2,
                     "rttVariation should be < 2x smoothedRtt for stable connection");
  }

  // Verify bytesAcknowledged <= bytesSent (with tolerance for timing)
  // Allow up to 20% slack due to timing of stat collection and packets in flight
  if (has("bytesAcknowledged", "bytesSent")) {
    const maxAllowed = stats.bytesSent * 1.2;
    assert_less_than_equal(stats.bytesAcknowledged, maxAllowed,
                           "bytesAcknowledged should not significantly exceed bytesSent");
  }

  // Verify bytesLost <= bytesSent
  if (has("bytesLost", "bytesSent")) {
    assert_less_than_equal(stats.bytesLost, stats.bytesSent,
                           "bytesLost should not exceed bytesSent");
  }

  // Verify packetsLost <= packetsSent
  if (has("packetsLost", "packetsSent")) {
    assert_less_than_equal(stats.packetsLost, stats.packetsSent,
                           "packetsLost should not exceed packetsSent");
  }
}, "WebTransport stats should satisfy invariants");

