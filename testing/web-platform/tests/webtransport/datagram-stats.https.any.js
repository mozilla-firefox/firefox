// META: global=window,worker
// META: script=resources/webtransport-test-helpers.sub.js

// Outgoing-datagram stats, kept out of the Interop 2026 scored stats.https.any.js.

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;

  // Small enough that a datagram is already too old by the time it reaches the
  // transport, so every one of them expires instead of being sent. Outgoing
  // datagrams can expire either before or after reaching the transport's queue,
  // and expiredOutgoing has to account for both.
  wt.datagrams.outgoingMaxAge = 0.001;

  const numDatagrams = 32;
  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();
  const promises = [];
  while (promises.length < numDatagrams) {
    promises.push(writer.write(encoder.encode(promises.length.toString())));
  }
  // Expired datagrams still resolve their write(): datagrams are best-effort,
  // so writeDatagrams never rejects on loss.
  await Promise.all(promises);

  const maxAttempts = 40;
  let stats;
  for (let i = 0; i < maxAttempts; i++) {
    await wait(50);
    stats = await wt.getStats();
    if (stats.datagrams.expiredOutgoing > 0) {
      break;
    }
  }
  assert_greater_than(stats.datagrams.expiredOutgoing, 0);
  assert_less_than_equal(stats.datagrams.expiredOutgoing, numDatagrams);
}, "WebTransport client should count datagrams that outlive outgoingMaxAge in expiredOutgoing");

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  await wt.ready;

  // outgoingMaxBufferedDatagrams bounds [[OutgoingDatagramsQueue]] through
  // backpressure: writeDatagrams withholds its promise at the limit, so further
  // write()s wait in the WritableStream's queue rather than being dropped.  A
  // value of 1 therefore must not make datagrams expire; only outgoingMaxAge
  // does that, and it is generous here.
  wt.datagrams.outgoingMaxBufferedDatagrams = 1;
  wt.datagrams.outgoingMaxAge = 30000;

  const numDatagrams = 32;
  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();
  const promises = [];
  while (promises.length < numDatagrams) {
    promises.push(writer.write(encoder.encode(promises.length.toString())));
  }
  await Promise.all(promises);

  const stats = await wt.getStats();
  assert_equals(stats.datagrams.expiredOutgoing, 0);
}, "WebTransport client should not expire datagrams because of outgoingMaxBufferedDatagrams");
