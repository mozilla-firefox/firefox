/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* import-globals-from head_http3.js */

// Starts the HTTP/2 origin that advertises the HTTP/3 server via Alt-Svc and
// serves the requests that fall back from HTTP/3. Returns its port.
async function create_h3_fallback_origin() {
  let server = new HeadNodeHTTP2Server();
  // VersionFallback retries over HTTP/1.1.
  server.allowHTTP1 = true;
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.registerPathHandler("/http3-test", (req, res) => {
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Alt-Svc", "h3=" + req.headers["x-altsvc"]);
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end("ok");
  });
  for (let path of ["/UnknownReset", "/VersionFallback"]) {
    await server.registerPathHandler(path, (req, res) => {
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end("ok");
    });
  }
  return server.port();
}

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

// Promise-backed Http3CheckListener
class Http3CheckListener {
  constructor(
    { expectedStatus = Cr.NS_OK, expectedRoute = "" } = {},
    resolve,
    reject
  ) {
    this.onDataAvailableFired = false;
    this.expectedStatus = expectedStatus;
    this.expectedRoute = expectedRoute;
    this._resolve = resolve;
    this._reject = reject;
  }

  onStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(request.status, this.expectedStatus);
    if (Components.isSuccessCode(this.expectedStatus)) {
      Assert.equal(request.responseStatus, 200);
    }
  }

  onDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    read_stream(stream, cnt);
  }

  onStopRequest(request, status) {
    Assert.equal(status, this.expectedStatus);

    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump("routed is " + routed + "\n");

    Assert.equal(routed, this.expectedRoute);

    if (Components.isSuccessCode(this.expectedStatus)) {
      let httpVersion = "";
      try {
        httpVersion = request.protocolVersion;
      } catch (e) {}
      Assert.equal(httpVersion, "h3");
      Assert.equal(this.onDataAvailableFired, true);
      Assert.equal(request.getResponseHeader("X-Firefox-Http3"), "h3");
    }

    this._resolve?.(request);
  }
}

class WaitForHttp3Listener extends Http3CheckListener {
  constructor(
    {
      expectedStatus = Cr.NS_OK,
      expectedRoute = "",
      uri = "",
      h3AltSvc = "",
      retry,
      delayMs = 500,
    } = {},
    resolve,
    reject
  ) {
    super({ expectedStatus, expectedRoute }, resolve, reject);
    this.uri = uri;
    this.h3AltSvc = h3AltSvc;
    this._retry = retry; // function to re-open the request
    this._delayMs = delayMs; // poll interval
  }

  onStopRequest(request, status) {
    Assert.equal(status, this.expectedStatus);

    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump(`routed is ${routed}\n`);

    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}

    if (routed === this.expectedRoute) {
      // (This is where run_next_test() used to be.)
      Assert.equal(routed, this.expectedRoute); // useful log/assert
      Assert.equal(httpVersion, "h3");
      this._resolve?.(request);
      return;
    }

    // Not routed yet: mirror old behavior (log + supportsHTTP3 check + retry)
    dump("poll later for alt-svc mapping\n");
    if (httpVersion === "h2") {
      request.QueryInterface(Ci.nsIHttpChannelInternal);
      Assert.ok(request.supportsHTTP3);
    }

    if (typeof this._retry === "function") {
      // schedule another attempt (replaces do_test_pending/do_timeout recursion)
      do_timeout(this._delayMs, () =>
        this._retry(this.uri, this.expectedRoute, this.h3AltSvc)
      );
    }
    // Promise remains pending until a later attempt matches expectedRoute.
  }
}

// Factory to create { listener, promise }
function createHttp3CheckListener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new Http3CheckListener(options, resolve, reject);
  return { listener, promise };
}

function createWaitForHttp3Listener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new WaitForHttp3Listener(options, resolve, reject);
  return { listener, promise };
}

// --- Async wrapper that does the polling by re-issuing the request ---
async function waitForHttp3Route(
  uri,
  expectedRoute,
  altSvc,
  { delayMs = 500 } = {}
) {
  let listenerRef;

  let firstAttempt = true;
  // Function to (re)open the channel using the same listener instance.
  const retry = () => {
    if (!firstAttempt) {
      // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
      do_timeout(1000, () => {
        Services.obs.notifyObservers(null, "net:cancel-all-connections");
        const chan = makeChan(uri);
        if (altSvc) {
          chan.setRequestHeader("x-altsvc", altSvc, false);
        }
        chan.asyncOpen(listenerRef);
      });
      return;
    }
    firstAttempt = false;
    const chan = makeChan(uri);
    if (altSvc) {
      chan.setRequestHeader("x-altsvc", altSvc, false);
    }
    chan.asyncOpen(listenerRef);
  };

  const { listener, promise } = createWaitForHttp3Listener({
    expectedStatus: Cr.NS_OK,
    expectedRoute,
    uri,
    h3AltSvc: altSvc,
    retry,
    delayMs,
  });
  listenerRef = listener;

  // Kick off first attempt; subsequent attempts are scheduled by the listener.
  retry();

  // Resolves only when routed === expectedRoute
  return promise;
}

// Promise-backed MultipleListener
class MultipleListener {
  constructor(
    {
      number_of_parallel_requests = 0,
      expectedRoute = "",
      with_error = Cr.NS_OK, // NS_OK means we expect success for all
    } = {},
    resolve,
    reject
  ) {
    this.number_of_parallel_requests = number_of_parallel_requests;
    this.expectedRoute = expectedRoute;
    this.with_error = with_error;

    this.count_of_done_requests = 0;
    this.error_found_onstart = false;
    this.error_found_onstop = false;
    this.need_cancel_found = false;

    this._resolve = resolve;
    this._reject = reject;
  }

  onStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);

    // Optional cancel behavior via header "CancelMe"
    let need_cancel = "";
    try {
      need_cancel = request.getRequestHeader("CancelMe");
    } catch (_) {}
    if (need_cancel !== "") {
      this.need_cancel_found = true;
      request.cancel(Cr.NS_ERROR_ABORT);
      return;
    }

    // Original logic: either 200 OK for success, or exactly one failure
    if (Components.isSuccessCode(request.status)) {
      Assert.equal(request.responseStatus, 200);
    } else if (this.error_found_onstart) {
      // Fail fast: more than one failing request on start
      this._reject?.(
        new Error("We should have only one request failing (onStart).")
      );
    } else {
      Assert.equal(request.status, this.with_error);
      this.error_found_onstart = true;
    }
  }

  onDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  }

  onStopRequest(request) {
    // Check Alt-Used routing matches expectation
    let routed = "";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (_) {}
    Assert.equal(routed, this.expectedRoute);

    // If success, ensure HTTP/3
    if (Components.isSuccessCode(request.status)) {
      let httpVersion = "";
      try {
        httpVersion = request.protocolVersion;
      } catch (_) {}
      Assert.equal(httpVersion, "h3");
    }

    // Track/validate failures (at most one)
    if (!Components.isSuccessCode(request.status)) {
      if (this.error_found_onstop) {
        this._reject?.(
          new Error("We should have only one request failing (onStop).")
        );
        return;
      }
      Assert.equal(request.status, this.with_error);
      this.error_found_onstop = true;
    }

    // Count completion and maybe resolve
    this.count_of_done_requests++;
    if (this.count_of_done_requests === this.number_of_parallel_requests) {
      if (Components.isSuccessCode(this.with_error)) {
        // All were expected to succeed
        Assert.equal(this.error_found_onstart, false);
        Assert.equal(this.error_found_onstop, false);
      } else {
        // One failure was expected OR a cancel path was exercised
        Assert.ok(this.error_found_onstart || this.need_cancel_found);
        Assert.equal(this.error_found_onstop, true);
      }
      this._resolve?.();
    }
  }
}

// Factory to create { listener, promise }
function createMultipleListener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new MultipleListener(options, resolve, reject);
  return { listener, promise };
}

async function do_test_multiple_requests(
  number_of_parallel_requests,
  h3Route,
  httpsOrigin
) {
  dump("test_multiple_requests()\n");

  const { listener, promise } = createMultipleListener({
    number_of_parallel_requests,
    expectedRoute: h3Route,
    with_error: Cr.NS_OK,
  });

  for (let i = 0; i < number_of_parallel_requests; i++) {
    const chan = makeChan(httpsOrigin + "20000");
    chan.asyncOpen(listener);
  }

  await promise;
}

async function do_test_request_cancelled_by_server(h3Route, httpsOrigin) {
  dump("do_test_request_cancelled_by_server()\n");

  const { listener, promise } = createHttp3CheckListener({
    expectedStatus: Cr.NS_ERROR_NET_INTERRUPT,
    expectedRoute: h3Route,
  });

  const chan = makeChan(httpsOrigin + "RequestCancelled");
  chan.asyncOpen(listener);

  // Resolves at the point where run_next_test() used to be called
  await promise;
}

// Server resets request stream with an unrecognized application error code.
// The transaction must retry via H2/H1 and succeed.
async function do_test_unknown_reset(httpsOrigin) {
  dump("do_test_unknown_reset()\n");

  const chan = makeChan(httpsOrigin + "UnknownReset");
  const promise = new Promise(resolve => {
    const listener = {
      onStartRequest(request) {
        Assert.ok(request instanceof Ci.nsIHttpChannel);
        Assert.equal(request.status, Cr.NS_OK);
        Assert.equal(request.responseStatus, 200);
      },
      onDataAvailable(request, stream, off, cnt) {
        read_stream(stream, cnt);
      },
      onStopRequest(request, status) {
        Assert.equal(status, Cr.NS_OK);
        let httpVersion = "";
        try {
          httpVersion = request.protocolVersion;
        } catch (e) {}
        Assert.notEqual(httpVersion, "h3");
        resolve(request);
      },
    };
    chan.asyncOpen(listener);
  });

  await promise;
}

// Promise-backed Http3CheckListener must already exist:
//   createHttp3CheckListener({ expectedStatus, expectedRoute })

class CancelRequestListener extends Http3CheckListener {
  constructor({ expectedRoute = "" } = {}, resolve, reject) {
    super(
      { expectedStatus: Cr.NS_ERROR_ABORT, expectedRoute },
      resolve,
      reject
    );
  }

  onStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(Components.isSuccessCode(request.status), true);
    // Cancel the request immediately (simulate Necko cancelling)
    request.cancel(Cr.NS_ERROR_ABORT);
  }
}

function createCancelRequestListener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new CancelRequestListener(options, resolve, reject);
  return { listener, promise };
}

// Cancel stream after OnStartRequest.
async function do_test_stream_cancelled_by_necko(h3Route, httpsOrigin) {
  dump("do_test_stream_cancelled_by_necko()\n");

  const { listener, promise } = createCancelRequestListener({
    expectedRoute: h3Route,
  });

  const chan = makeChan(httpsOrigin + "20000");
  chan.asyncOpen(listener);

  // Resolves at the end of onStopRequest (where run_next_test() used to be)
  await promise;
}

async function do_test_multiple_request_one_is_cancelled(
  number_of_parallel_requests,
  h3Route,
  httpsOrigin
) {
  dump("do_test_multiple_request_one_is_cancelled()\n");

  const { listener, promise } = createMultipleListener({
    number_of_parallel_requests,
    expectedRoute: h3Route,
    with_error: Cr.NS_ERROR_NET_INTERRUPT, // one request is expected to fail (server-cancelled)
  });

  for (let i = 0; i < number_of_parallel_requests; i++) {
    let uri = httpsOrigin + "20000";
    if (i === 4) {
      // Add a request that will be cancelled by the server.
      uri = httpsOrigin + "RequestCancelled";
    }
    const chan = makeChan(uri);
    chan.asyncOpen(listener);
  }

  // Resolves when all parallel requests complete and invariants are checked
  await promise;
}

async function do_test_multiple_request_one_is_cancelled_by_necko(
  number_of_parallel_requests,
  h3Route,
  httpsOrigin
) {
  dump("do_test_multiple_request_one_is_cancelled_by_necko()\n");

  const { listener, promise } = createMultipleListener({
    number_of_parallel_requests,
    expectedRoute: h3Route,
    with_error: Cr.NS_ERROR_ABORT,
  });

  for (let i = 0; i < number_of_parallel_requests; i++) {
    let chan = makeChan(httpsOrigin + "20000");
    if (i === 4) {
      // MultipleListener will cancel request with this header.
      chan.setRequestHeader("CancelMe", "true", false);
    }
    chan.asyncOpen(listener);
  }

  // Resolves when all parallel requests complete and invariants are checked
  await promise;
}

// Promise-backed Http3CheckListener assumed available:
// function createHttp3CheckListener({ expectedStatus, expectedRoute })

class PostListener extends Http3CheckListener {
  constructor(opts = {}, resolve, reject) {
    super(opts, resolve, reject);
  }
  onDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    read_stream(stream, cnt);
  }
}

// Factory for PostListener
function createPostListener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new PostListener(options, resolve, reject);
  return { listener, promise };
}

// Helper to perform a POST (or any method with a body)
function openWithBody(
  content,
  chan,
  method = "POST",
  contentType = "text/plain"
) {
  const stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stream.setByteStringData(content);

  const uchan = chan.QueryInterface(Ci.nsIUploadChannel);
  uchan.setUploadStream(stream, contentType, stream.available());
  chan.requestMethod = method;
  return chan;
}

// Generate a post with known pre-calculated md5 sum.
function generateContent(size) {
  let content = "";
  for (let i = 0; i < size; i++) {
    content += "0";
  }
  return content;
}

let post = generateContent(10);

// Test a simple POST (async)
async function do_test_post(httpsOrigin, h3Route) {
  dump("do_test_post()\n");

  const chan = makeChan(httpsOrigin + "post");
  openWithBody(post, chan, "POST");

  const { listener, promise } = createPostListener({
    expectedStatus: Cr.NS_OK,
    expectedRoute: h3Route,
  });

  chan.asyncOpen(listener);
  await promise; // resolves at end of onStopRequest in Http3CheckListener
}

// Test a simple PATCH
async function do_test_patch(httpsOrigin, h3Route) {
  dump("do_test_post()\n");

  const chan = makeChan(httpsOrigin + "patch");
  openWithBody(post, chan, "PATCH");

  const { listener, promise } = createPostListener({
    expectedStatus: Cr.NS_OK,
    expectedRoute: h3Route,
  });

  chan.asyncOpen(listener);
  await promise;
}

// Promise-backed base assumed available:
//   class Http3CheckListener { ... }
//   function createHttp3CheckListener(opts) { return { listener, promise }; }

class SlowReceiverListener extends Http3CheckListener {
  constructor(
    {
      expectedStatus = Cr.NS_OK,
      expectedRoute = "",
      expectedBytes = 10_000_000,
    } = {},
    resolve,
    reject
  ) {
    super({ expectedStatus, expectedRoute }, resolve, reject);
    this.count = 0;
    this.expectedBytes = expectedBytes;
  }

  onDataAvailable(request, stream, off, cnt) {
    this.onDataAvailableFired = true;
    this.count += cnt;
    read_stream(stream, cnt);
  }

  onStopRequest(request, status) {
    Assert.equal(status, this.expectedStatus);
    Assert.equal(this.count, this.expectedBytes);

    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump(`routed is ${routed}\n`);
    Assert.equal(routed, this.expectedRoute);

    if (Components.isSuccessCode(this.expectedStatus)) {
      let httpVersion = "";
      try {
        httpVersion = request.protocolVersion;
      } catch (e) {}
      Assert.equal(httpVersion, "h3");
      Assert.equal(this.onDataAvailableFired, true);
    }

    // Resolve where run_next_test() used to be
    this._resolve?.(request);
  }
}

function createSlowReceiverListener(options = {}) {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new SlowReceiverListener(options, resolve, reject);
  return { listener, promise };
}

// Test: slow receiver (suspend, then resume)
async function do_test_slow_receiver(httpsOrigin, h3Route) {
  dump("do_test_slow_receiver()\n");

  const chan = makeChan(httpsOrigin + "10000000");

  const { listener, promise } = createSlowReceiverListener({
    expectedStatus: Cr.NS_OK,
    expectedRoute: h3Route,
    expectedBytes: 10_000_000,
  });

  chan.asyncOpen(listener);

  // Suspend immediately, then resume after 1s (replaces do_test_pending/do_timeout)
  chan.suspend();
  await new Promise(r => do_timeout(1000, r));
  chan.resume();

  // Wait for completion (used to be run_next_test/do_test_finished)
  await promise;
}

// Promise-backed listener for version fallback checks
class CheckFallbackListener {
  constructor(resolve, reject) {
    this._resolve = resolve;
    this._reject = reject;
  }

  onStartRequest(request) {
    Assert.ok(request instanceof Ci.nsIHttpChannel);
    Assert.equal(request.status, Cr.NS_OK);
    Assert.equal(request.responseStatus, 200);
  }

  onDataAvailable(request, stream, off, cnt) {
    read_stream(stream, cnt);
  }

  onStopRequest(request, status) {
    Assert.equal(status, Cr.NS_OK);

    let routed = "NA";
    try {
      routed = request.getRequestHeader("Alt-Used");
    } catch (e) {}
    dump(`routed is ${routed}\n`);
    Assert.equal(routed, "0");

    let httpVersion = "";
    try {
      httpVersion = request.protocolVersion;
    } catch (e) {}
    Assert.equal(httpVersion, "http/1.1");

    // Resolve where run_next_test() used to be called
    this._resolve?.(request);
  }
}

// Factory to create { listener, promise }
function createCheckFallbackListener() {
  let resolve, reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const listener = new CheckFallbackListener(resolve, reject);
  return { listener, promise };
}

// Server cancels request with VersionFallback.
async function do_test_version_fallback(httpsOrigin) {
  dump("do_test_version_fallback()\n");

  const chan = makeChan(httpsOrigin + "VersionFallback");
  const { listener, promise } = createCheckFallbackListener();

  chan.asyncOpen(listener);

  await promise;
}
