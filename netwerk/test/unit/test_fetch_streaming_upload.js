/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// End-to-end tests for fetch() streaming upload bodies (ReadableStream + duplex)
// against every transport from plaintext HTTP/1.1 through HTTP/3, using the
// shared NodeServer helpers (and the prebuilt http3server binary for HTTP/3).
//
// Streaming uploads are only supported on HTTP/2 and HTTP/3; sending one over
// HTTP/1.x is rejected with a TypeError. That restriction is not mandated by
// the Fetch spec, but it matches other browsers and is asserted by WPT
// (fetch/api/basic/request-upload.any.js, "Streaming upload shouldn't work on
// Http/1.1.").

const { NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server, HTTP3Server } =
  ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const ECHO_HANDLER = function echoHandler(req, resp) {
  let chunks = [];
  req.on("data", c => chunks.push(c));
  req.on("end", () => {
    let body = Buffer.concat(chunks);
    resp.writeHead(200, { "Content-Type": "application/octet-stream" });
    resp.end(body);
  });
};

// Counts what actually reached the server, so a rejected upload can be shown
// to have been refused before anything was sent.
const COUNTING_HANDLER = function countingHandler(req, resp) {
  global.requestCount = (global.requestCount || 0) + 1;
  let chunks = [];
  req.on("data", c => chunks.push(c));
  req.on("end", () => {
    global.bodyBytes = (global.bodyBytes || 0) + Buffer.concat(chunks).length;
    resp.writeHead(200, { "Content-Type": "application/octet-stream" });
    resp.end("ok");
  });
};

function makeStream(...chunks) {
  return new ReadableStream({
    start(controller) {
      const encoder = new TextEncoder();
      for (const chunk of chunks) {
        controller.enqueue(
          typeof chunk === "string" ? encoder.encode(chunk) : chunk
        );
      }
      controller.close();
    },
  });
}

// Async producer: enqueues each chunk lazily via the pull callback so the
// body genuinely arrives over time rather than all upfront. Lets us verify
// the channel keeps the request open and forwards data as it shows up.
function makeAsyncStream(...chunks) {
  let i = 0;
  return new ReadableStream({
    async pull(controller) {
      await Promise.resolve();
      if (i >= chunks.length) {
        controller.close();
        return;
      }
      const chunk = chunks[i++];
      const encoder = new TextEncoder();
      controller.enqueue(
        typeof chunk === "string" ? encoder.encode(chunk) : chunk
      );
    },
  });
}

const STREAM_TESTS = [
  {
    name: "empty stream",
    body: () => makeStream(),
    expected: "",
  },
  {
    name: "single small chunk",
    body: () => makeStream("Test"),
    expected: "Test",
  },
  {
    name: "multiple chunks (sync)",
    body: () => makeStream("Hello", " ", "world"),
    expected: "Hello world",
  },
  {
    name: "multiple chunks (async)",
    body: () => makeAsyncStream("alpha", "-", "beta", "-", "gamma"),
    expected: "alpha-beta-gamma",
  },
  {
    name: "large body across chunks",
    body: () => makeAsyncStream("a".repeat(4096), "b".repeat(4096)),
    expected: "a".repeat(4096) + "b".repeat(4096),
  },
];

async function fetchStream(url, body) {
  return fetch(url, { method: "POST", body, duplex: "half" });
}

async function runStreamingShouldSucceed(url, label) {
  for (const t of STREAM_TESTS) {
    info(`${label}: ${t.name}`);
    let resp = await fetchStream(url, t.body());
    Assert.equal(resp.status, 200, `${label} ${t.name}: status`);
    let text = await resp.text();
    Assert.equal(text.length, t.expected.length, `${label} ${t.name}: length`);
    Assert.equal(text, t.expected, `${label} ${t.name}: body`);
  }
}

// An HTTP/1.x streaming upload must be refused before the request goes out,
// not after the server has already received the whole body.
async function runNothingIsUploaded(server, label) {
  await server.execute("global.requestCount = 0; global.bodyBytes = 0;");
  let url = `${server.origin()}/count`;
  await Assert.rejects(
    fetchStream(url, makeStream("should", "never", "arrive")),
    TypeError,
    `${label}: streaming upload must reject`
  );
  Assert.equal(
    await server.execute("global.bodyBytes"),
    0,
    `${label}: no body bytes should have reached the server`
  );
  Assert.equal(
    await server.execute("global.requestCount"),
    0,
    `${label}: the request should not have been sent at all`
  );
}

async function runStreamingShouldFail(url, label) {
  for (const t of STREAM_TESTS) {
    info(`${label} (must reject): ${t.name}`);
    let threw = false;
    try {
      await fetchStream(url, t.body());
    } catch (e) {
      threw = true;
      Assert.ok(
        e instanceof TypeError,
        `${label} ${t.name}: expected TypeError, got ${e}`
      );
    }
    Assert.ok(
      threw,
      `${label} ${t.name}: streaming upload over HTTP/1.x must reject`
    );
  }
}

async function withServer(ServerClass, fn) {
  let server = new ServerClass();
  await server.start();
  try {
    await server.registerPathHandler("/echo", ECHO_HANDLER);
    await server.registerPathHandler("/count", COUNTING_HANDLER);
    await fn(server);
  } finally {
    await server.stop();
  }
}

add_setup(async function setup() {
  do_get_profile(); // installCert needs a profile.
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.http3.enable");
  });
});

add_task(async function test_http1_plain() {
  await withServer(NodeHTTPServer, async server => {
    await runStreamingShouldFail(`${server.origin()}/echo`, "HTTP/1.1 plain");
    await runNothingIsUploaded(server, "HTTP/1.1 plain");
  });
});

add_task(async function test_http1_tls() {
  await withServer(NodeHTTPSServer, async server => {
    await runStreamingShouldFail(`${server.origin()}/echo`, "HTTP/1.1 TLS");
    await runNothingIsUploaded(server, "HTTP/1.1 TLS");
  });
});

add_task(async function test_http2() {
  await withServer(NodeHTTP2Server, async server => {
    await runStreamingShouldSucceed(`${server.origin()}/echo`, "HTTP/2");
  });
});

// REQUEST_BODY_SENT is driven by NS_NET_STATUS_WAITING_FOR, which HTTP/2 fires
// off the request body byte count a streaming upload never has.
add_task(async function test_http2_request_body_sent_is_reported() {
  const BODY_SENT =
    Ci.nsIHttpActivityObserver.ACTIVITY_SUBTYPE_REQUEST_BODY_SENT;

  await withServer(NodeHTTP2Server, async server => {
    let url = `${server.origin()}/echo`;
    let sequence = [];
    let distributor = Cc[
      "@mozilla.org/network/http-activity-distributor;1"
    ].getService(Ci.nsIHttpActivityDistributor);
    let observer = {
      observeActivity(aChannel, aActivityType, aActivitySubtype) {
        if (
          aActivityType !==
          Ci.nsIHttpActivityObserver.ACTIVITY_TYPE_HTTP_TRANSACTION
        ) {
          return;
        }
        try {
          if (aChannel.QueryInterface(Ci.nsIChannel).URI.spec === url) {
            sequence.push(aActivitySubtype);
          }
        } catch (e) {}
      },
    };
    distributor.addObserver(observer);

    try {
      let resp = await fetchStream(url, makeAsyncStream("alpha", "-", "beta"));
      Assert.equal(resp.status, 200, "status");
      Assert.equal(await resp.text(), "alpha-beta", "body echoed back");

      // The notifications are marshalled to the main thread, so they are still
      // in flight when the fetch resolves.
      await TestUtils.waitForCondition(
        () =>
          sequence.includes(
            Ci.nsIHttpActivityObserver.ACTIVITY_SUBTYPE_TRANSACTION_CLOSE
          ),
        "the transaction was reported closed"
      );
      info(`activity: ${sequence.join(",")}`);
      Assert.ok(
        sequence.includes(BODY_SENT),
        "the streaming request body is reported sent"
      );
    } finally {
      distributor.removeObserver(observer);
    }
  });
});

// The http3server's /post endpoint reads the entire request body and
// replies with a constant body plus an x-data-received-length header
// telling us exactly how many body bytes it saw.
async function setupHttp3Server() {
  const server = new HTTP3Server();
  await server.start(
    Services.env.get("MOZ_HTTP3_SERVER_PATH"),
    Services.env.get("MOZ_HTTP3_CERT_DB_PATH")
  );

  // http3server uses the same CA as the HTTP/2 node server.
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "../unit/http2-ca.pem", "CTu,u,u");

  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `foo.example.com;h3=:${server.port()}`
  );

  // Prime alt-svc. The first request lands on regular HTTPS (which fails
  // because only the H3 port is listening), but the alt-svc pref above
  // is enough for the next request to go straight over H3. Retry the
  // warm-up until nsHttpChannel reports that the connection actually used
  // the advertised alternate.
  let h3Route = `foo.example.com:${server.port()}`;
  const maxAttempts = 20;
  await new Promise((resolve, reject) => {
    let attempts = 0;
    let attempt = () => {
      if (++attempts > maxAttempts) {
        reject(
          new Error(
            `alt-svc warm-up did not route to ${h3Route} after ${maxAttempts} attempts`
          )
        );
        return;
      }
      let chan = makeChan("https://foo.example.com/");
      chan.asyncOpen({
        onStartRequest() {},
        onDataAvailable(request, stream, offset, count) {
          read_stream(stream, count);
        },
        onStopRequest(request) {
          let routed = "NA";
          try {
            routed = request.getRequestHeader("Alt-Used");
          } catch (e) {
            info(`Alt-Used not present yet: ${e}`);
          }
          if (routed == h3Route) {
            resolve();
          } else {
            attempt();
          }
        },
      });
    };
    attempt();
  });

  return server;
}

add_task(
  {
    skip_if: () =>
      mozinfo.os == "android" ||
      !Services.env.get("MOZ_HTTP3_SERVER_PATH") ||
      !Services.env.get("MOZ_HTTP3_CERT_DB_PATH"),
  },
  async function test_http3() {
    Services.prefs.setBoolPref("network.http.http3.enable", true);
    let server = await setupHttp3Server();
    registerCleanupFunction(async () => {
      await server.stop();
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      Services.prefs.clearUserPref("network.dns.localDomains");
      Services.prefs.clearUserPref(
        "network.http.http3.alt-svc-mapping-for-testing"
      );
    });

    let url = "https://foo.example.com/post";
    for (const t of STREAM_TESTS) {
      info(`HTTP/3: ${t.name}`);
      let resp = await fetchStream(url, t.body());
      Assert.equal(resp.status, 200, `HTTP/3 ${t.name}: status`);
      Assert.equal(
        resp.headers.get("x-data-received-length"),
        String(t.expected.length),
        `HTTP/3 ${t.name}: server received correct number of bytes`
      );
      // Drain the response.
      await resp.text();
    }
  }
);
