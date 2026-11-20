// META: global=window,worker
// META: script=resources/webtransport-test-helpers.sub.js

// Write datagrams until the producer receives the AbortSignal.
async function write_datagrams(writer, signal) {
  const encoder = new TextEncoder();
  let counter = 0;
  const sentTokens = [];
  const aborted = new Promise((resolve) => {
    signal.addEventListener('abort', resolve);
  });
  while (true) {
    await Promise.race([writer.ready, aborted]);
    if (signal.aborted) {
      break;
    }
    var token = counter.toString();
    sentTokens.push(token);
    writer.write(encoder.encode(token)).catch(() => {});
    counter++;
  }
  return sentTokens;
}

// Write N datagrams without waiting, then wait for them
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

// Read datagrams until the consumer has received enough i.e. N datagrams. Call
// abort() after reading.
async function read_datagrams(reader, controller, N) {
  const decoder = new TextDecoder();
  const receivedTokens = [];
  while (receivedTokens.length < N) {
    const { value: token, done } = await reader.read();
    assert_false(done);
    receivedTokens.push(decoder.decode(token));
  }
  controller.abort();
  return receivedTokens;
}

// Write numbers until the producer receives the AbortSignal.
async function write_numbers(writer, signal) {
  let counter = 0;
  const sentNumbers = [];
  const aborted =
    new Promise((resolve) => signal.addEventListener('abort', resolve));
  // Counter should be less than 256 because reader stores numbers in Uint8Array.
  while (counter < 256) {
    await Promise.race([writer.ready, aborted])
    if (signal.aborted) {
      break;
    }
    sentNumbers.push(counter);
    chunk = new Uint8Array(1);
    chunk[0] = counter;
    writer.write(chunk).catch(() => {});
    counter++;
  }
  return sentNumbers;
}

// Write large datagrams of size 10 until the producer receives the AbortSignal.
async function write_large_datagrams(writer, signal) {
  const aborted = new Promise((resolve) => {
    signal.addEventListener('abort', resolve);
  });
  while (true) {
    await Promise.race([writer.ready, aborted]);
    if (signal.aborted) {
      break;
    }
    writer.write(new Uint8Array(10)).catch(() => {});
  }
}

// Read datagrams with BYOB reader until the consumer has received enough i.e. N
// datagrams. Call abort() after reading.
async function read_numbers_byob(reader, controller, N) {
  let buffer = new ArrayBuffer(N);
  buffer = await readInto(reader, buffer);
  controller.abort();
  return Array.from(new Uint8Array(buffer));
}

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader();

  const controller = new AbortController();
  const signal = controller.signal;

  // Write and read datagrams.
  const N = 5;
  const [sentTokens, receivedTokens] = await Promise.all([
      write_datagrams(writer, signal),
      read_datagrams(reader, controller, N)
  ]);

  // Check receivedTokens is a subset of sentTokens.
  const subset = receivedTokens.every(token => sentTokens.includes(token));
  assert_true(subset);
}, 'Datagrams are echoed successfully');

promise_test(async t => {
  // Establish a WebTransport session. datagramsReadableType: "bytes" opts
  // into a byte stream, which BYOB reads require.
  const wt = new WebTransport(webtransport_url('echo.py'),
                              { datagramsReadableType: 'bytes' });
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader({ mode: 'byob' });

  const controller = new AbortController();
  const signal = controller.signal;

  // Write and read datagrams.
  // Numbers are less than 256, consider N to be a small number.
  const N = 5;
  const [sentNumbers, receiveNumbers] = await Promise.all([
    write_numbers(writer, signal),
    read_numbers_byob(reader, controller, N)
  ]);

  // No duplicated numbers received.
  assert_equals((new Set(receiveNumbers)).size, N);

  // Check receiveNumbers is a subset of sentNumbers.
  const subset = receiveNumbers.every(token => sentNumbers.includes(token));
  assert_true(subset);
}, 'Successfully reading datagrams with BYOB reader.');

promise_test(async t => {
  // https://w3c.github.io/webtransport/#web-transport-configuration: without
  // datagramsReadableType: "bytes", datagrams.readable must be a default
  // (non-byte) stream, so a BYOB reader request must be rejected.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;
  assert_throws_js(TypeError, () => { wt.datagrams.readable.getReader({ mode: 'byob' }) });
}, 'Reading datagrams with BYOB reader without datagramsReadableType should fail.');

promise_test(async t => {
  // Establish a WebTransport session. datagramsReadableType: "bytes" opts
  // into a byte stream, which BYOB reads require.
  const wt = new WebTransport(webtransport_url('echo.py'),
                              { datagramsReadableType: 'bytes' });
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader({ mode: 'byob' });

  const controller = new AbortController();
  const signal = controller.signal;

  // Write datagrams of size 10, but only 1 byte buffer is provided for BYOB
  // reader. To avoid splitting a datagram, stream will be errored.
  const buffer = new ArrayBuffer(1);
  const [error, _] = await Promise.all([
    reader.read(new Uint8Array(buffer)).catch(e => {
      controller.abort();
      return e;
    }),
    write_large_datagrams(writer, signal)
  ]);
  assert_equals(error.name, 'RangeError');
}, 'Reading datagrams with insufficient buffer should be rejected.');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo_datagram_length.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader();

  // Write and read max-size datagram.
  const maxDatagramSize = wt.datagrams.maxDatagramSize;
  await writer.write(new Uint8Array(maxDatagramSize));

  // the server should echo the datagram length encoded in JSON
  const { value: token, done } = await reader.read();
  assert_false(done);

  const decoder = new TextDecoder();
  const datagramStr = decoder.decode(token);
  const jsonObject = JSON.parse(datagramStr);
  assert_equals(jsonObject['length'], maxDatagramSize);
}, 'Transfer max-size datagram');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo_datagram_length.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader();

  let maxDatagramSize = wt.datagrams.maxDatagramSize;

  while (true) {
    // Write and read max-size datagram.
    await writer.write(new Uint8Array(maxDatagramSize + 1));
    // This should resolve with no datagram sent, which is hard to test for.
    // Wait for incoming datagrams to arrive, and if they do, fail.
    const result = await Promise.race([reader.read(), wait(500)]);
    if (result === undefined) {
      return; // Success, no datagram received, exit the test early.
    }

    // Maybe QUIC's PMTUD increased the max-size datagram in the meantime. If
    // so, try again.
    const currentMaxDatagramSize = wt.datagrams.maxDatagramSize;
    assert_greater_than(currentMaxDatagramSize, maxDatagramSize);
    maxDatagramSize = currentMaxDatagramSize;
  }
}, 'Fail to transfer max-size+1 datagram, handle PMTUD increases');

promise_test(async t => {
  // Make a WebTransport connection, but session is not necessarily established.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());

  const writer = wt.datagrams.createWritable().getWriter();
  const reader = wt.datagrams.readable.getReader();

  const controller = new AbortController();
  const signal = controller.signal;

  // Write and read datagrams.
  const N = 5;
  wt.datagrams.outgoingMaxBufferedDatagrams = N;
  const [sentTokens, receivedTokens] = await Promise.all([
      write_N_datagrams(writer, N),
      read_datagrams(reader, controller, N)
  ]);

  // Check receivedTokens is a subset of sentTokens.
  const subset = receivedTokens.every(token => sentTokens.includes(token));
  assert_true(subset);

  // Make sure WebTransport session is established.
  await wt.ready;
}, 'Sending and receiving datagrams is ready to use before session is established');

promise_test(async t => {
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const N = 5;
  wt.datagrams.outgoingMaxBufferedDatagrams = N;

  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();

  // The writable keeps the default highWaterMark (1); outgoingMaxBufferedDatagrams
  // only makes writeDatagrams withhold the write promise once the queue
  // reaches it: https://w3c.github.io/webtransport/#writedatagrams
  assert_equals(writer.desiredSize, 1,
                'outgoing datagrams writable uses the default highWaterMark (1)');

  const settled = [];
  const writes = [];
  for (let i = 0; i < N; i++) {
    writes.push(writer.write(encoder.encode(i.toString()))
                    .then(() => { settled[i] = true; }));
  }

  // Drain microtasks without yielding a task, so the datagrams cannot have
  // left the queue yet.
  for (let i = 0; i < 50; i++) {
    await Promise.resolve();
  }
  assert_array_equals(settled.slice(0, N - 1), new Array(N - 1).fill(true),
                      'writes below the limit resolve promptly');
  assert_equals(settled[N - 1], undefined,
                'the write that reaches the limit is withheld');

  await Promise.all(writes);
}, "Datagram's outgoingMaxBufferedDatagrams correctly regulates written datagrams");

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  const N = 5;
  wt.datagrams.incomingMaxBufferedDatagrams = N;

  const writer = wt.datagrams.createWritable().getWriter();
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  // Write 10*N datagrams with incrementing sequence numbers.
  const totalDatagrams = 10 * N;
  for (let counter = 0; counter < totalDatagrams; counter++) {
    writer.write(encoder.encode(counter.toString())).catch(() => {});
    await writer.ready;
  }

  // Wait for incoming datagrams to arrive and for the queue to stabilize under
  // HWM. No reader is active during this time so HWM enforcement takes effect.
  await wait(2000);

  // Read only what is already buffered. A short per-read timeout detects when
  // the queue is empty so we don't sit waiting for new datagrams to arrive.
  const reader = wt.datagrams.readable.getReader();
  const receivedSequenceNumbers = [];
  while (true) {
    const result = await Promise.race([reader.read(), wait(100)]);
    if (result === undefined) break;
    receivedSequenceNumbers.push(parseInt(decoder.decode(result.value)));
  }

  // Check that at least some datagrams were actually delivered, so a
  // regression that breaks delivery entirely doesn't vacuously pass the
  // checks below (Math.min(...[]) is Infinity, which satisfies both).
  assert_greater_than(receivedSequenceNumbers.length, 0);

  // Check that the receivedDatagrams is less than or equal to
  // incomingMaxBufferedDatagrams.
  assert_less_than_equal(receivedSequenceNumbers.length, N);

  // Check that the received datagrams are from the end of the sequence.
  // If datagrams are being dropped from the beginning, the received sequence
  // numbers should all be from the later part of the sequence.
  // We won't assume that every datagram sent was received or dropped to avoid
  // timing issues.  Just verify that the received datagrams don't come from the
  // start of the sequence.
  const minReceivedSequence = Math.min(...receivedSequenceNumbers);
  assert_greater_than_equal(minReceivedSequence, N-1,
    'Dropped datagrams should be from the beginning of the sequence');
}, 'Datagrams read is less than or equal to incomingMaxBufferedDatagrams');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  assert_equals(wt.datagrams.incomingMaxAge, null);
  assert_equals(wt.datagrams.outgoingMaxAge, null);

  wt.datagrams.incomingMaxAge = 5;
  assert_equals(wt.datagrams.incomingMaxAge, 5);
  wt.datagrams.outgoingMaxAge = 5;
  assert_equals(wt.datagrams.outgoingMaxAge, 5);

  assert_throws_js(RangeError, () => { wt.datagrams.incomingMaxAge = -1; });
  assert_throws_js(RangeError, () => { wt.datagrams.outgoingMaxAge = -1; });
  assert_throws_js(RangeError, () => { wt.datagrams.incomingMaxAge = NaN; });
  assert_throws_js(RangeError, () => { wt.datagrams.outgoingMaxAge = NaN; });

  wt.datagrams.incomingMaxAge = 0;
  assert_equals(wt.datagrams.incomingMaxAge, null);
  wt.datagrams.outgoingMaxAge = 0;
  assert_equals(wt.datagrams.outgoingMaxAge, null);
}, 'Datagram MaxAge getters/setters work correctly');

promise_test(async t => {
  // Establish a WebTransport session.
  const wt = new WebTransport(webtransport_url('echo.py'));
  wt.closed.catch(() => {});
  t.add_cleanup(() => wt.close());
  await wt.ready;

  // Initial values are implementation-defined
  assert_greater_than_equal(wt.datagrams.incomingMaxBufferedDatagrams, 1);
  assert_greater_than_equal(wt.datagrams.outgoingMaxBufferedDatagrams, 1);
  wt.datagrams.incomingMaxBufferedDatagrams = 5;
  assert_equals(wt.datagrams.incomingMaxBufferedDatagrams, 5);
  wt.datagrams.outgoingMaxBufferedDatagrams = 5;
  assert_equals(wt.datagrams.outgoingMaxBufferedDatagrams, 5);

  // With unsigned long type, -1 coerces to 2^32 - 1 (4294967295)
  wt.datagrams.incomingMaxBufferedDatagrams = -1;
  assert_equals(wt.datagrams.incomingMaxBufferedDatagrams, 4294967295);
  wt.datagrams.outgoingMaxBufferedDatagrams = -1;
  assert_equals(wt.datagrams.outgoingMaxBufferedDatagrams, 4294967295);

  // NaN coerces to 0, then clamped to 1
  wt.datagrams.incomingMaxBufferedDatagrams = NaN;
  assert_equals(wt.datagrams.incomingMaxBufferedDatagrams, 1);
  wt.datagrams.outgoingMaxBufferedDatagrams = NaN;
  assert_equals(wt.datagrams.outgoingMaxBufferedDatagrams, 1);

  wt.datagrams.incomingMaxBufferedDatagrams = 0.5;
  assert_equals(wt.datagrams.incomingMaxBufferedDatagrams, 1);
  wt.datagrams.outgoingMaxBufferedDatagrams = 0.5;
  assert_equals(wt.datagrams.outgoingMaxBufferedDatagrams, 1);
  wt.datagrams.incomingMaxBufferedDatagrams = 0;
  assert_equals(wt.datagrams.incomingMaxBufferedDatagrams, 1);
  wt.datagrams.outgoingMaxBufferedDatagrams = 0;
  assert_equals(wt.datagrams.outgoingMaxBufferedDatagrams, 1);
}, 'Datagram MaxBufferedDatagrams getters/setters work correctly');
