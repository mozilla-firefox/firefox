/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A complete HTTP/2 response that is still buffered for a suspended consumer
// must be delivered intact, even when the request body is still being
// uploaded and the stream is serviced for writing again.

"use strict";

/* import-globals-from head_channels.js */

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const RESPONSE_SIZE = 4 * 1024 * 1024;
const UPLOAD_SIZE = 1024 * 1024;
const PACED_UPLOAD_SIZE = 16 * 1024 * 1024;
const FLOW_UPLOAD_SIZE = 4 * 1024 * 1024;
const SESSION_WINDOW_UPLOAD_SIZE = 32 * 1024 * 1024;
const RESET_AFTER = 256 * 1024;
const INITIAL_WINDOW = 65535;
const LARGE_STREAM_WINDOW = 24 * 1024 * 1024;
const ITERATIONS = 10;
const HALF_CLOSED_LOCAL = 5;

let server;

add_setup(async function () {
  do_get_profile();
  server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.execute(`(() => {
    global.responseBody = Buffer.alloc(${RESPONSE_SIZE});
    for (let i = 0; i < global.responseBody.length; i++) {
      global.responseBody[i] = i % 251;
    }
    global.LARGE_STREAM_WINDOW = ${LARGE_STREAM_WINDOW};
  })()`);
  let respond = (res, length = true) => {
    global.responseStream = res.stream;
    res.writeHead(
      200,
      length ? { "content-length": global.responseBody.length } : {}
    );
    res.end(global.responseBody);
  };
  let handleUpload = (req, res, length) => {
    let state = { received: 0, ended: false, closed: false, rstCode: null };
    global.upload = state;
    // Paces the upload so that it outlasts the delivery of the response.
    req.on("data", chunk => {
      state.received += chunk.length;
      req.pause();
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      setTimeout(() => req.resume(), 1);
    });
    req.on("end", () => {
      state.ended = true;
      state.endedAt = Date.now();
    });
    // Holds the upload on flow control until the client resumes.
    req.pause();
    global.uploadReq = req;
    res.stream.on("close", () => {
      state.closed = true;
      state.rstCode = res.stream.rstCode;
    });
    global.respond(res, length);
  };
  await server.execute(`global.respond = ${respond.toString()}`);
  await server.execute(`global.handleUpload = ${handleUpload.toString()}`);
  await server.registerPathHandler("/get", (req, res) => {
    global.respond(res);
  });
  await server.registerPathHandler("/upload-held", (req, res) => {
    // Keeps the request stream window closed and stops node from resetting
    // the stream after the response.
    req.pause();
    global.uploadReq = req;
    global.respond(res);
  });
  await server.registerPathHandler("/upload-rst", (req, res) => {
    // Not reading the request body makes node reset the stream with NO_ERROR
    // once the response is complete (RFC 9113 section 8.1).
    global.respond(res);
  });
  await server.registerPathHandler("/release", (req, res) => {
    global.uploadReq.once("data", () => res.end("ok"));
    global.uploadReq.resume();
  });
  await server.registerPathHandler("/upload", (req, res) => {
    global.handleUpload(req, res, true);
  });
  await server.registerPathHandler("/upload-no-length", (req, res) => {
    global.handleUpload(req, res, false);
  });
  // Resets the stream with NO_ERROR as soon as the response is complete.
  await server.registerPathHandler("/upload-no-length-reset", (req, res) => {
    global.handleUpload(req, res, false);
    const state = global.upload;
    const s = res.stream;
    global.uploadSession = s.session;
    (function poll() {
      if (s.state.localClose === 1) {
        state.streamWindowAtReset = s.state.localWindowSize;
        state.endedAtReset = state.ended;
        s.close(require("http2").constants.NGHTTP2_NO_ERROR);
        // Node emits "close" only once the paused request body is drained.
        req.resume();
      } else {
        // eslint-disable-next-line no-undef
        setImmediate(poll);
      }
    })();
  });
  await server.registerPathHandler("/upload-flow", (req, res) => {
    global.flowSession = req.stream.session;
    req.on("data", chunk => (global.flowReceived += chunk.length));
    req.on("end", () => res.end("ok"));
  });
  // Raises the stream send windows above the session send window, and
  // answers once the client has acknowledged the change.
  await server.registerPathHandler("/large-stream-window", (req, res) => {
    req.stream.session.settings(
      { initialWindowSize: global.LARGE_STREAM_WINDOW },
      () => res.end("ok")
    );
  });
  await server.registerPathHandler("/sync", (req, res) => {
    res.end("ok");
  });
});

// Polls the node expression `condition` and resolves with the value of the
// node expression `value` once it holds.
function waitFor(condition, value = condition) {
  return server.execute(`new Promise(resolve => {
    (function poll() {
      if (${condition}) {
        resolve(${value});
      } else {
        setTimeout(poll, 1);
      }
    })();
  })`);
}

// Resolves once node has sent END_STREAM on the latest response stream.
function waitForResponseSent() {
  const s = "global.responseStream";
  return waitFor(
    `${s}.destroyed || ${s}.state.localClose === 1`,
    `${s}.destroyed ? "destroyed" : ${s}.state.state`
  );
}

async function uploadState() {
  return JSON.parse(await server.execute(`JSON.stringify(global.upload)`));
}

// With pauseAt, the listener consumes exactly pauseAt bytes, then suspends the
// request and resolves result.paused.
function openChannel(
  path,
  { suspend = false, upload = null, pauseAt = null } = {}
) {
  let chan = NetUtil.newChannel({
    uri: `${server.origin()}${path}`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags =
    Ci.nsIRequest.LOAD_BYPASS_CACHE | Ci.nsIRequest.INHIBIT_CACHING;
  if (upload) {
    let stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
      Ci.nsIStringInputStream
    );
    stream.setByteStringData(upload);
    chan
      .QueryInterface(Ci.nsIUploadChannel)
      .setUploadStream(stream, "text/plain", upload.length);
    chan.requestMethod = "POST";
  }

  let result = {};
  let resolvePaused;
  result.paused = new Promise(resolve => (resolvePaused = resolve));
  let consumed = 0;
  result.started = new Promise(resolveStart => {
    result.stopped = new Promise(resolveStop => {
      let data = [];
      chan.asyncOpen({
        QueryInterface: ChromeUtils.generateQI([
          "nsIStreamListener",
          "nsIRequestObserver",
        ]),
        onStartRequest(request) {
          if (suspend) {
            request.suspend();
          }
          resolveStart(request);
        },
        onDataAvailable(request, stream, offset, count) {
          if (
            pauseAt !== null &&
            consumed < pauseAt &&
            consumed + count >= pauseAt
          ) {
            let n = pauseAt - consumed;
            data.push(NetUtil.readInputStreamToString(stream, n));
            consumed += n;
            request.suspend();
            resolvePaused(request);
            return;
          }
          data.push(NetUtil.readInputStreamToString(stream, count));
          consumed += count;
        },
        onStopRequest(request, status) {
          resolveStop({
            request,
            status,
            body: data.join(""),
            stoppedAt: Date.now(),
          });
        },
      });
    });
  });
  return result;
}

function firstMismatch(body) {
  for (let i = 0; i < body.length; i++) {
    if (body.charCodeAt(i) !== i % 251) {
      return i;
    }
  }
  return -1;
}

async function fetchSync() {
  let { status, request } = await openChannel("/sync").stopped;
  Assert.equal(status, Cr.NS_OK);
  Assert.equal(request.protocolVersion, "h2");
}

function checkResponse({ request, status, body }) {
  info(`status=0x${(status >>> 0).toString(16)} length=${body.length}`);
  Assert.equal(status, Cr.NS_OK);
  Assert.equal(request.protocolVersion, "h2");
  Assert.equal(body.length, RESPONSE_SIZE);
  Assert.equal(firstMismatch(body), -1);
}

function pipeSize() {
  return (
    Services.prefs.getIntPref("network.buffer.cache.size") *
    Services.prefs.getIntPref("network.buffer.cache.count")
  );
}

add_task(async function test_get_suspended_until_complete() {
  let chan = openChannel("/get", { suspend: true });
  let request = await chan.started;
  await waitForResponseSent();
  // Frames arrive in order on the session, so END_STREAM of /get has been
  // processed once /sync completes.
  await fetchSync();
  request.resume();
  checkResponse(await chan.stopped);
});

add_task(async function test_upload_suspended_until_complete() {
  let chan = openChannel("/upload-held", {
    suspend: true,
    upload: "x".repeat(UPLOAD_SIZE),
  });
  let request = await chan.started;
  Assert.equal(await waitForResponseSent(), HALF_CLOSED_LOCAL);
  // node sends the WINDOW_UPDATE for /upload-held before the /release
  // response.
  let release = await openChannel("/release").stopped;
  Assert.equal(release.status, Cr.NS_OK);
  // /upload-held was queued for writing before /sync, so it has been serviced
  // once /sync completes.
  await fetchSync();
  request.resume();
  checkResponse(await chan.stopped);
});

add_task(async function test_upload_reset_no_error_suspended_until_complete() {
  let chan = openChannel("/upload-rst", {
    suspend: true,
    upload: "x".repeat(UPLOAD_SIZE),
  });
  let request = await chan.started;
  await waitForResponseSent();
  // Wait until node has reset the stream with NO_ERROR.
  const s = "global.responseStream";
  const rstCode = await waitFor(
    `${s}.destroyed || ${s}.closed`,
    `${s}.rstCode`
  );
  Assert.equal(rstCode, 0);
  // The RST_STREAM has been processed once /sync completes.
  await fetchSync();
  request.resume();
  checkResponse(await chan.stopped);
});

// With fillPipe, the listener leaves one pipe's worth unread, so the stream's
// buffer drains into a full pipe and its cleanup stays pending while node
// opens the stream window again.
async function uploadWhileResumed(path, { fillPipe = false } = {}) {
  let upload = "x".repeat(PACED_UPLOAD_SIZE);
  let pauseAt = fillPipe ? RESPONSE_SIZE - pipeSize() : null;
  for (let i = 0; i < ITERATIONS; i++) {
    let chan = openChannel(path, { suspend: true, upload, pauseAt });
    let request = await chan.started;
    Assert.equal(await waitForResponseSent(), HALF_CLOSED_LOCAL);
    await fetchSync();
    let atResume = await uploadState();
    if (fillPipe) {
      request.resume();
      await chan.paused;
    }
    await server.execute(
      `global.upload.resumedAt = Date.now(); global.uploadReq.resume()`
    );
    if (fillPipe) {
      // Node has consumed the initial stream window and sent WINDOW_UPDATE,
      // which has been processed once /sync completes.
      await waitFor(`global.upload.received >= ${INITIAL_WINDOW}`);
      await fetchSync();
    }
    request.resume();
    let result = await chan.stopped;
    let atStop = await uploadState();
    info(
      `iteration ${i}: stoppedAt=${result.stoppedAt} ` +
        `atResume=${JSON.stringify(atResume)} atStop=${JSON.stringify(atStop)}`
    );
    Assert.ok(!atResume.ended && !atResume.closed, "upload running at resume");
    Assert.ok(
      !atStop.ended || atStop.endedAt > result.stoppedAt,
      "upload running at stop"
    );
    checkResponse(result);
  }
}

add_task(async function test_upload_running_when_resumed() {
  await uploadWhileResumed("/upload");
});

add_task(async function test_upload_running_when_resumed_no_length() {
  await uploadWhileResumed("/upload-no-length", { fillPipe: true });
});

// Lets the upload run, then resets the stream with NO_ERROR while the client
// is still sending the request body.
async function resetWhileUploading() {
  return JSON.parse(
    await server.execute(`new Promise(resolve => {
      const state = global.upload;
      const s = global.responseStream;
      const start = state.received;
      s.on("close", () => resolve(JSON.stringify(state)));
      // Resets right after consuming a chunk, so that the reset follows the
      // WINDOW_UPDATE that lets the client send more.
      global.uploadReq.on("data", function onData() {
        if (state.received - start >= ${RESET_AFTER}) {
          global.uploadReq.off("data", onData);
          state.receivedAtReset = state.received;
          state.endedAtReset = state.ended;
          s.close(require("http2").constants.NGHTTP2_NO_ERROR);
        }
      });
      global.uploadReq.resume();
    })`)
  );
}

add_task(async function test_upload_reset_no_error_when_resumed_no_length() {
  let upload = "x".repeat(PACED_UPLOAD_SIZE);
  for (let i = 0; i < ITERATIONS; i++) {
    let chan = openChannel("/upload-no-length", { suspend: true, upload });
    let request = await chan.started;
    Assert.equal(await waitForResponseSent(), HALF_CLOSED_LOCAL);
    let atReset = await resetWhileUploading();
    await fetchSync();
    request.resume();
    let result = await chan.stopped;
    info(
      `iteration ${i}: stoppedAt=${result.stoppedAt} ` +
        `atReset=${JSON.stringify(atReset)}`
    );
    Assert.equal(atReset.rstCode, 0, "stream reset with NO_ERROR");
    Assert.ok(
      !atReset.endedAtReset && atReset.receivedAtReset < PACED_UPLOAD_SIZE,
      "upload running at reset"
    );
    checkResponse(result);
  }
});

// Keeps uploads to /upload-flow running back to back until stopped.
function startFlow() {
  let upload = "y".repeat(FLOW_UPLOAD_SIZE);
  let running = true;
  let done = (async () => {
    while (running) {
      let { status, body } = await openChannel("/upload-flow", { upload })
        .stopped;
      Assert.equal(status, Cr.NS_OK);
      Assert.equal(body, "ok");
    }
  })();
  return () => {
    running = false;
    return done;
  };
}

function largeStreamWindow() {
  return openChannel("/large-stream-window").stopped.then(
    ({ status, body }) => {
      Assert.equal(status, Cr.NS_OK);
      Assert.equal(body, "ok");
    }
  );
}

// With stream send windows larger than the session send window, A's upload
// stops on the exhausted session window, which node does not refill while it
// holds A's request body. A's complete response stays buffered while A is
// suspended, and node resets A with NO_ERROR. A's listener then consumes all
// but one pipe's worth and suspends, so the rest fills the pipe exactly and the
// stream's buffer is empty while its cleanup is pending. Then uploads to
// /upload-flow run the session send window down to 0, and node refills it with
// connection WINDOW_UPDATEs.
add_task(async function test_reset_session_window_update_in_gap() {
  let upload = "x".repeat(SESSION_WINDOW_UPLOAD_SIZE);
  await largeStreamWindow();
  await server.execute(`global.flowReceived = 0`);
  for (let i = 0; i < ITERATIONS; i++) {
    let chan = openChannel("/upload-no-length-reset", {
      suspend: true,
      upload,
      pauseAt: RESPONSE_SIZE - pipeSize(),
    });
    let request = await chan.started;
    await waitFor("global.upload.closed");
    await fetchSync();
    request.resume();
    await chan.paused;
    let stopFlow = startFlow();
    let flowStart = await server.execute(`global.flowReceived`);
    await waitFor(`global.flowReceived - ${flowStart} >= ${FLOW_UPLOAD_SIZE}`);
    let sameSession = await server.execute(
      `global.flowSession === global.uploadSession`
    );
    let stateA = await uploadState();
    await stopFlow();
    request.resume();
    let result = await chan.stopped;
    info(`iteration ${i}: stateA=${JSON.stringify(stateA)}`);
    Assert.ok(sameSession, "A on the session of the other uploads");
    Assert.strictEqual(stateA.rstCode, 0, "A reset with NO_ERROR");
    Assert.strictEqual(stateA.endedAtReset, false, "upload A unfinished");
    Assert.greater(stateA.streamWindowAtReset, 0, "A has stream window");
    checkResponse(result);
  }
});
