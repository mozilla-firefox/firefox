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
