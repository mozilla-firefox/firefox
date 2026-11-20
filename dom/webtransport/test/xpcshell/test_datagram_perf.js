/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Benchmarks WebTransport datagram send and receive rates and round-trip
// latency through the DOM API, using small and maximum-sized datagrams echoed
// by the local HTTP/3 server, both loaded and one at a time, and receive rates
// and time to the first datagram for datagrams the server sends as fast as
// its outgoing queue allows.

"use strict";

/* exported perfMetadata */
var perfMetadata = {
  owner: "Network Team",
  name: "WebTransport datagrams",
  description:
    "XPCShell benchmark of WebTransport datagram send, echo receive and flood receive rates and latency against a local server",
  options: {
    default: {
      perfherder: true,
      perfherder_metrics: [
        { name: "small_send", unit: "datagrams/s", lowerIsBetter: false },
        { name: "small_receive", unit: "datagrams/s", lowerIsBetter: false },
        { name: "small_delivered", unit: "%", lowerIsBetter: false },
        { name: "small_latency_first", unit: "ms", lowerIsBetter: true },
        { name: "small_latency_median", unit: "ms", lowerIsBetter: true },
        { name: "small_latency_p95", unit: "ms", lowerIsBetter: true },
        { name: "large_send", unit: "datagrams/s", lowerIsBetter: false },
        { name: "large_receive", unit: "datagrams/s", lowerIsBetter: false },
        { name: "large_delivered", unit: "%", lowerIsBetter: false },
        { name: "large_latency_first", unit: "ms", lowerIsBetter: true },
        { name: "large_latency_median", unit: "ms", lowerIsBetter: true },
        { name: "large_latency_p95", unit: "ms", lowerIsBetter: true },
        {
          name: "flood_small_receive",
          unit: "datagrams/s",
          lowerIsBetter: false,
        },
        { name: "flood_small_delivered", unit: "%", lowerIsBetter: false },
        { name: "flood_small_first", unit: "ms", lowerIsBetter: true },
        {
          name: "flood_large_receive",
          unit: "datagrams/s",
          lowerIsBetter: false,
        },
        { name: "flood_large_delivered", unit: "%", lowerIsBetter: false },
        { name: "flood_large_first", unit: "ms", lowerIsBetter: true },
        { name: "ping_small_median", unit: "ms", lowerIsBetter: true },
        { name: "ping_small_p95", unit: "ms", lowerIsBetter: true },
        { name: "ping_large_median", unit: "ms", lowerIsBetter: true },
        { name: "ping_large_p95", unit: "ms", lowerIsBetter: true },
      ],
      xpcshell_cycles: 13,
      verbose: true,
      try_platform: ["linux", "mac"],
    },
  },
  tags: ["network", "http3", "quic", "webtransport"],
};

const SMALL_DATAGRAM_SIZE = 64;
const DATAGRAM_COUNT = 10000;
// Datagrams still outstanding after this long without a new one are counted
// as lost.
const RECEIVE_IDLE_TIMEOUT_MS = 1000;
const IDLE_POLL_MS = 50;
const PING_COUNT = 1000;
// An echo not back within this long is counted as lost.
const PING_TIMEOUT_MS = 200;

var { clearTimeout, setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

let gMetrics = {};

registerCleanupFunction(() => {
  Services.prefs.clearUserPref("network.dns.localDomains");
});

add_setup(function () {
  setupWebTransportServer();
});

// Collects the previous case's garbage up front, so its GC slices don't land
// in the middle of this one's measurements.
function collectGarbage() {
  Cu.forceGC();
  Cu.forceCC();
}

// The default outgoingMaxAge is short enough for a loaded test machine to
// exceed, which would count scheduling delay as loss and leave no echo to time.
function disableOutgoingDatagramExpiry(wt) {
  wt.datagrams.outgoingMaxAge = Infinity;
}

function percentile(sorted, fraction) {
  return sorted[
    Math.min(sorted.length - 1, Math.floor(sorted.length * fraction))
  ];
}

// A null size takes the size of the first datagram, for floods of the
// server's maximum.  onDatagram, if given, is called with each datagram and
// its arrival time.
async function receiveDatagrams(wt, size, count, onDatagram) {
  const reader = wt.datagrams.readable.getReader();
  let received = 0;
  let lastTime = ChromeUtils.now();
  let firstTime = null;
  let finished = false;

  const reading = (async () => {
    while (received < count) {
      const { value, done } = await reader.read();
      if (done) {
        break;
      }
      if (size === null) {
        size = value.byteLength;
      } else if (value.byteLength != size) {
        Assert.equal(value.byteLength, size, "received datagram size");
      }
      received++;
      lastTime = ChromeUtils.now();
      if (firstTime === null) {
        firstTime = lastTime;
      }
      if (onDatagram) {
        onDatagram(value, lastTime);
      }
    }
    finished = true;
  })();

  while (!finished && ChromeUtils.now() - lastTime < RECEIVE_IDLE_TIMEOUT_MS) {
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, IDLE_POLL_MS));
  }
  await reader.cancel();
  await reading;
  return { received, firstTime, lastTime, size };
}

async function runDatagramBenchmark(label, sizeFor) {
  collectGarbage();
  const wt = newWebTransport(`https://${host}/echo_datagrams`);
  await wt.ready;
  disableOutgoingDatagramExpiry(wt);

  const size = sizeFor(wt);
  Assert.greaterOrEqual(size, 4, "datagram size");

  // Each datagram carries its index, so its echo can be matched to the time
  // it was written.
  const sendTimes = new Float64Array(DATAGRAM_COUNT);
  const latencies = [];
  const receiving = receiveDatagrams(wt, size, DATAGRAM_COUNT, (value, now) => {
    const index = new DataView(value.buffer, value.byteOffset).getUint32(0);
    latencies.push(now - sendTimes[index]);
    if (index == 0) {
      gMetrics[`${label}_latency_first`] = now - sendTimes[0];
    }
  });

  // One buffer serves every write: the writable's highWaterMark is 1, so
  // writer.ready only resolves once the previous chunk has been copied out.
  const data = new Uint8Array(size);
  const dataView = new DataView(data.buffer);
  const writer = wt.datagrams.writable.getWriter();
  const start = ChromeUtils.now();
  let lastWrite;
  for (let i = 0; i < DATAGRAM_COUNT; i++) {
    await writer.ready;
    dataView.setUint32(0, i);
    sendTimes[i] = ChromeUtils.now();
    lastWrite = writer.write(data);
  }
  await lastWrite;
  const sendTime = ChromeUtils.now() - start;

  const { received, lastTime } = await receiving;
  const receiveTime = lastTime - start;

  const sendRate = (DATAGRAM_COUNT * 1000) / sendTime;
  const receiveRate = received ? (received * 1000) / receiveTime : 0;
  const delivered = (received * 100) / DATAGRAM_COUNT;
  info(
    `${label} (${size} bytes): sent ${DATAGRAM_COUNT} in ${sendTime.toFixed(1)}ms ` +
      `(${sendRate.toFixed(0)}/s, ${((sendRate * size * 8) / 1e6).toFixed(1)} Mbps), ` +
      `received ${received} in ${receiveTime.toFixed(1)}ms ` +
      `(${receiveRate.toFixed(0)}/s, ${((receiveRate * size * 8) / 1e6).toFixed(1)} Mbps)`
  );

  // Lost datagrams leave no sample, so a metric with none is left unreported.
  if (latencies.length) {
    latencies.sort((a, b) => a - b);
    const mean = latencies.reduce((a, b) => a + b, 0) / latencies.length;
    gMetrics[`${label}_latency_median`] = percentile(latencies, 0.5);
    gMetrics[`${label}_latency_p95`] = percentile(latencies, 0.95);
    const first = gMetrics[`${label}_latency_first`];
    info(
      `${label} round trip: first ${first === undefined ? "lost" : `${first.toFixed(3)}ms`}, ` +
        `median ${gMetrics[`${label}_latency_median`].toFixed(3)}ms, ` +
        `mean ${mean.toFixed(3)}ms, p95 ${gMetrics[`${label}_latency_p95`].toFixed(3)}ms`
    );
  }

  gMetrics[`${label}_send`] = sendRate;
  gMetrics[`${label}_receive`] = receiveRate;
  gMetrics[`${label}_delivered`] = delivered;

  writer.releaseLock();
  wt.close();
  await wt.closed;
}

async function runFloodBenchmark(label, sizeParam) {
  collectGarbage();
  // The first datagram can arrive before ready resolves, so time it from
  // session creation, which includes the connect.
  const created = ChromeUtils.now();
  const wt = newWebTransport(
    `https://${host}/flood_datagrams/${DATAGRAM_COUNT}/${sizeParam}`
  );
  const receiving = receiveDatagrams(
    wt,
    sizeParam == "max" ? null : sizeParam,
    DATAGRAM_COUNT
  );
  await wt.ready;
  const start = ChromeUtils.now();

  const { received, firstTime, lastTime, size } = await receiving;
  const receiveTime = lastTime - start;
  const receiveRate = received ? (received * 1000) / receiveTime : 0;
  const delivered = (received * 100) / DATAGRAM_COUNT;
  info(
    `${label} (${size} bytes): received ${received} of ${DATAGRAM_COUNT} in ` +
      `${receiveTime.toFixed(1)}ms (${receiveRate.toFixed(0)}/s, ` +
      `${((receiveRate * size * 8) / 1e6).toFixed(1)} Mbps)`
  );

  if (firstTime !== null) {
    const first = firstTime - created;
    info(`${label}: first after ${first.toFixed(1)}ms`);
    gMetrics[`${label}_first`] = first;
  }
  gMetrics[`${label}_receive`] = receiveRate;
  gMetrics[`${label}_delivered`] = delivered;

  wt.close();
  await wt.closed;
}

// Sends one datagram at a time and waits for its echo before sending the
// next, so the round trip carries no queueing behind other datagrams.
async function runPingPongBenchmark(label, sizeFor) {
  collectGarbage();
  const wt = newWebTransport(`https://${host}/echo_datagrams`);
  await wt.ready;
  disableOutgoingDatagramExpiry(wt);

  const size = sizeFor(wt);
  Assert.greaterOrEqual(size, 4, "datagram size");
  const data = new Uint8Array(size);
  const dataView = new DataView(data.buffer);
  const reader = wt.datagrams.readable.getReader();
  const writer = wt.datagrams.writable.getWriter();

  // A read() can't be withdrawn, so one that outlives a lost ping's timeout
  // carries over to the next ping.
  let pendingRead = null;
  const rtts = [];
  let lost = 0;
  for (let i = 0; i < PING_COUNT; i++) {
    await writer.ready;
    dataView.setUint32(0, i);
    const sent = ChromeUtils.now();
    writer.write(data);

    let timer;
    const timeout = new Promise(resolve => {
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      timer = setTimeout(() => resolve(null), PING_TIMEOUT_MS);
    });
    for (;;) {
      if (!pendingRead) {
        pendingRead = reader.read();
      }
      const result = await Promise.race([pendingRead, timeout]);
      if (!result) {
        lost++;
        break;
      }
      pendingRead = null;
      const { value } = result;
      if (new DataView(value.buffer, value.byteOffset).getUint32(0) == i) {
        rtts.push(ChromeUtils.now() - sent);
        break;
      }
    }
    clearTimeout(timer);
  }

  info(`${label} (${size} bytes): ${rtts.length} round trips, ${lost} lost`);
  if (rtts.length) {
    rtts.sort((a, b) => a - b);
    const mean = rtts.reduce((a, b) => a + b, 0) / rtts.length;
    gMetrics[`${label}_median`] = percentile(rtts, 0.5);
    gMetrics[`${label}_p95`] = percentile(rtts, 0.95);
    info(
      `${label} round trip: median ${gMetrics[`${label}_median`].toFixed(3)}ms, ` +
        `mean ${mean.toFixed(3)}ms, p95 ${gMetrics[`${label}_p95`].toFixed(3)}ms`
    );
  }

  await reader.cancel();
  writer.releaseLock();
  wt.close();
  await wt.closed;
}

add_task(async function test_small_datagrams() {
  await runDatagramBenchmark("small", () => SMALL_DATAGRAM_SIZE);
});

add_task(async function test_large_datagrams() {
  await runDatagramBenchmark("large", wt => wt.datagrams.maxDatagramSize);
});

add_task(async function test_flood_small_datagrams() {
  await runFloodBenchmark("flood_small", SMALL_DATAGRAM_SIZE);
});

add_task(async function test_flood_large_datagrams() {
  await runFloodBenchmark("flood_large", "max");
});

add_task(async function test_ping_small_datagrams() {
  await runPingPongBenchmark("ping_small", () => SMALL_DATAGRAM_SIZE);
});

add_task(async function test_ping_large_datagrams() {
  await runPingPongBenchmark("ping_large", wt => wt.datagrams.maxDatagramSize);
});

add_task(function report_metrics() {
  info("perfMetrics", gMetrics);
});
