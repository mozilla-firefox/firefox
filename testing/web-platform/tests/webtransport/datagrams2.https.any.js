// META: global=window,worker
// META: script=resources/webtransport-test-helpers.sub.js
// META: timeout=long

// Datagram tests that are not part of the Interop 2026 scored set; the scored
// subtests live in datagrams.https.any.js.

async function write_N_datagrams(writer, n) {
  const encoder = new TextEncoder();
  const sentTokens = [];
  const promises = [];
  while (sentTokens.length < n) {
    const token = sentTokens.length.toString();
    sentTokens.push(token);
    promises.push(writer.write(encoder.encode(token)));
  }
  await Promise.all(promises);
  return sentTokens;
}

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const N = 5;
  wt.datagrams.incomingMaxBufferedDatagrams = N;

  // Unlike 'Datagrams read is less than or equal to
  // incomingMaxBufferedDatagrams' in datagrams.https.any.js, keep reading for
  // the whole burst. That test lets the queue overflow on purpose, so it cannot
  // tell apart a queue doing its job from a burst that evicted itself before
  // anything could read it. With a reader attached,
  // incomingMaxBufferedDatagrams bounds how much may sit buffered while nothing
  // reads it, not how much a consumer that keeps reading is allowed to
  // receive.
  const writer = wt.datagrams.createWritable().getWriter();
  const decoder = new TextDecoder();

  const totalDatagrams = 10 * N;

  // Start consuming before the burst is written, and keep a read() outstanding
  // throughout it. Draining only after the burst has landed would instead
  // measure what the queue happened to retain, which the no-reader test
  // already covers.
  const received = [];
  const readLoop = (async () => {
    for await (const value of wt.datagrams.readable) {
      received.push(parseInt(decoder.decode(value)));
      if (received.length == totalDatagrams) {
        return;
      }
    }
  })();

  await write_N_datagrams(writer, totalDatagrams);
  // Datagrams are unreliable, so don't wait for every one of them.
  await Promise.race([readLoop, wait(1000)]);

  // Datagrams are unreliable, so don't require every one to arrive. Receiving
  // no more than the queue bound out of 10x that many is the signature of the
  // burst evicting itself: arrivals are delivered inline from IPC dispatch, so
  // if the pending read() cannot resolve between messages, each datagram
  // displaces the one before it and only the last queueful survives.
  assert_greater_than(received.length, N,
    'A consumer that keeps reading should receive more than ' +
    'incomingMaxBufferedDatagrams datagrams');

  // Sharper than the count: if the burst had evicted itself, the survivors
  // would all come from its tail, as the no-reader test asserts. Seeing
  // anything from before the first queueful proves the reader was served while
  // the burst was still arriving.
  assert_less_than(Math.min(...received), N,
    'Delivery should begin while the burst is still arriving, not only ' +
    'after it ends');
}, 'Datagrams read with a reader attached are not capped by ' +
   'incomingMaxBufferedDatagrams');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('server-flood-datagrams.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const N = 5;
  wt.datagrams.incomingMaxBufferedDatagrams = N;

  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  // Unlike the echoing handlers, this burst is not paced by anything the client
  // did, so the datagrams can genuinely pile up on the receive path.
  const requested = 500;

  const received = [];
  const readLoop = (async () => {
    for await (const value of wt.datagrams.readable) {
      received.push(parseInt(decoder.decode(value)));
      if (received.length == requested) {
        return;
      }
    }
  })();

  await writer.write(encoder.encode(requested.toString()));
  await Promise.race([readLoop, wait(1000)]);

  // Datagrams are unreliable and a burst this size may legitimately overrun
  // buffers, so don't require a particular yield. What must hold is that the
  // burst isn't collapsed to a single queueful: that would mean the consumer
  // never got to run while the burst was arriving.
  assert_greater_than(received.length, N,
    'A server-initiated burst should not be reduced to one queueful');
}, 'Server-initiated datagram bursts are not reduced to incomingMaxBufferedDatagrams');

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  // Several times more bytes than an implementation is likely to buffer for a
  // session, written as fast as writer.ready allows, so the outgoing queue
  // backs up.  The user agent may drop datagrams to cope, but every write()
  // has to settle and the writable has to keep accepting writes.
  const burst = 2000;
  const size = 1000;
  const writer = wt.datagrams.createWritable().getWriter();

  const writes = [];
  for (let i = 0; i < burst; i++) {
    await writer.ready;
    writes.push(writer.write(new Uint8Array(size).fill(i & 0xff)));
  }
  await Promise.all(writes);
  await writer.ready;
  await writer.write(new Uint8Array([0]));
}, 'Every write() of a datagram burst that overruns the outgoing queue settles');

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  // Large enough that outgoingMaxBufferedDatagrams does not pace the burst, and
  // outgoingMaxAge left at its default of Infinity, so nothing may leave the
  // queue except by being sent. The burst holds more bytes than an
  // implementation is likely to buffer for a session, so it has to pace the
  // writes instead of dropping datagrams.
  const burst = 4000;
  wt.datagrams.outgoingMaxBufferedDatagrams = burst * 2;
  wt.datagrams.incomingMaxBufferedDatagrams = burst;

  let received = 0;
  const readLoop = (async () => {
    for await (const value of wt.datagrams.readable) {
      if (++received == burst) {
        return;
      }
    }
  })();

  const writer = wt.datagrams.createWritable().getWriter();
  const writes = [];
  for (let i = 0; i < burst; i++) {
    writes.push(writer.write(new Uint8Array(15).fill(i & 0xff)));
  }
  assert_array_equals(await Promise.all(writes), new Array(burst).fill(undefined),
    'every write() should resolve with undefined');

  // Datagrams are unreliable, so tolerate a little loss on the way, but not the
  // dozens a queue evicting its oldest datagrams loses.
  await Promise.race([readLoop, wait(5000)]);
  assert_greater_than_equal(received, burst - 10,
    'Datagrams written faster than they can be sent should not be dropped');

  const stats = await wt.getStats();
  assert_equals(stats.datagrams.expiredOutgoing, 0,
    'Datagrams should not expire with an unlimited outgoingMaxAge');

  await writer.ready;
  await writer.write(new Uint8Array([0]));
}, 'A burst of datagrams larger than the transport buffers is paced, not dropped');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  // The default outgoingMaxBufferedDatagrams is implementation-defined, so
  // set it well below the burst size.
  const maxBuffered = 10;
  wt.datagrams.outgoingMaxBufferedDatagrams = maxBuffered;
  const writer = wt.datagrams.writable.getWriter();
  const datagram = new Uint8Array(1);

  // A burst larger than outgoingMaxBufferedDatagrams. Written all in the same
  // task, with no await between writes, so every promise's resolution state
  // reflects only what the implementation itself decided, not test timing.
  const N = maxBuffered * 10;
  const resolved = new Array(N).fill(false);
  const promises = [];
  for (let i = 0; i < N; i++) {
    const p = writer.write(datagram);
    p.then(() => { resolved[i] = true; });
    promises.push(p);
  }

  // Once outgoingMaxBufferedDatagrams datagrams are queued, writeDatagrams
  // withholds the next write() promise until sendDatagrams dequeues one,
  // which happens in a network task and so cannot complete within this
  // microtask turn.
  await Promise.resolve();
  const resolvedImmediately = resolved.filter(Boolean).length;
  assert_less_than(resolvedImmediately, N,
    'a burst this size must not have every write() promise resolve in the same microtask turn');

  // All of them must still eventually settle.
  await Promise.all(promises);
}, 'A burst larger than outgoingMaxBufferedDatagrams paces write() resolution instead of resolving it all immediately');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  await wt.ready;

  // The default outgoingMaxBufferedDatagrams is implementation-defined; set
  // it well below N so some of these writes are still withheld when close()
  // runs.
  const maxBuffered = 5;
  wt.datagrams.outgoingMaxBufferedDatagrams = maxBuffered;
  const writer = wt.datagrams.writable.getWriter();
  const datagram = new Uint8Array(1);

  const N = maxBuffered * 10;
  const promises = [];
  for (let i = 0; i < N; i++) {
    promises.push(writer.write(datagram));
  }

  // Close immediately, before any network task can have dequeued a
  // datagram, so every withheld promise depends on cleanup settling it.
  // Cleanup resolves the withheld write and errors the writable, which
  // rejects the writes queued behind it.
  wt.close();

  const results = await Promise.allSettled(promises);
  const firstRejected = results.findIndex(r => r.status == 'rejected');
  for (const result of results.slice(firstRejected == -1 ? N : firstRejected)) {
    assert_equals(result.status, 'rejected',
                  'no write() resolves after the first rejected one');
    assert_throws_dom('AbortError', () => { throw result.reason; });
  }
}, 'Closing a session settles every deferred outgoing datagram write() promise');
