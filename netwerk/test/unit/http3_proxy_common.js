/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* import-globals-from head_http3.js */

const {
  Http3ProxyFilter,
  with_node_servers,
  NodeHTTPServer,
  NodeHTTPSServer,
  NodeHTTP2Server,
  NodeHTTP2ProxyServer,
  NodeWebSocketHttp2Server,
  WebSocketConnection,
  NodeTCPEchoServer,
  NodeTLSEchoServer,
} = ChromeUtils.importESModule("resource://testing-common/NodeServer.sys.mjs");

function makeChan(uri) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

let pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
let proxyHost;
let proxyPort;
let proxyAuth;
let proxyFilter;

/**
 * Sets up proxy filter to MASQUE H3 proxy
 */
async function setup_http3_proxy() {
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setBoolPref("network.dns.disableIPv6", true);
  Services.prefs.setIntPref("network.webtransport.datagram_size", 1500);
  Services.prefs.setCharPref("network.dns.localDomains", "foo.example.com");
  Services.prefs.setIntPref("network.http.http3.max_gso_segments", 1); // TODO: fix underflow
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");
  addCertFromFile(certdb, "proxy-ca.pem", "CTu,u,u");

  proxyHost = "foo.example.com";
  ({ masqueProxyPort: proxyPort } = await create_masque_proxy_server());
  proxyAuth = "";

  Assert.notEqual(proxyPort, null);
  Assert.notEqual(proxyPort, "");

  // A dummy request to make sure AltSvcCache::mStorage is ready.
  let chan = makeChan(`https://localhost`);
  await channelOpenPromise(chan, CL_EXPECT_FAILURE);

  proxyFilter = new Http3ProxyFilter(
    proxyHost,
    proxyPort,
    0,
    "/.well-known/masque/udp/{target_host}/{target_port}/",
    proxyAuth
  );
  pps.registerFilter(proxyFilter, 10);

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
  });
}

/**
 * Tests HTTP connect through H3 proxy to HTTP, HTTPS and H2 servers
 * Makes multiple requests. Expects success.
 */
function checkProxyType(req, expected) {
  Assert.equal(
    req.QueryInterface(Ci.nsIHttpChannelInternal).proxyType,
    expected,
    "The proxy type of the connection the response came from"
  );
}

async function test_http_connect() {
  info("Running test_http_connect");
  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      info(`Proxying to ${server.constructor.name} server`);
      await server.registerPathHandler("/first", (req, resp) => {
        resp.writeHead(200);
        resp.end("first");
      });
      await server.registerPathHandler("/second", (req, resp) => {
        resp.writeHead(200);
        resp.end("second");
      });
      await server.registerPathHandler("/third", (req, resp) => {
        resp.writeHead(200);
        resp.end("third");
      });
      let chan = makeChan(
        `${server.protocol()}://alt1.example.com:${server.port()}/first`
      );
      let [req, buf] = await channelOpenPromise(
        chan,
        CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
      );
      Assert.equal(req.status, Cr.NS_OK);
      Assert.equal(buf, "first");
      checkProxyType(req, Ci.nsIHttpChannelInternal.PROXY_TYPE_MASQUE);
      chan = makeChan(
        `${server.protocol()}://alt1.example.com:${server.port()}/second`
      );
      [req, buf] = await channelOpenPromise(
        chan,
        CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
      );
      Assert.equal(req.status, Cr.NS_OK);
      Assert.equal(buf, "second");

      chan = makeChan(
        `${server.protocol()}://alt1.example.com:${server.port()}/third`
      );
      [req, buf] = await channelOpenPromise(
        chan,
        CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
      );
      Assert.equal(req.status, Cr.NS_OK);
      Assert.equal(buf, "third");
    }
  );
}

/**
 * Test HTTP CONNECT authentication failure - tests behavior when proxy
 * authentication is required but not provided or incorrect
 */
async function test_http_connect_auth_failure() {
  info("Running test_http_connect_auth_failure");
  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      info(`Testing auth failure with ${server.constructor.name} server`);
      // Register a handler that requires authentication
      await server.registerPathHandler("/auth-required", (req, resp) => {
        const auth = req.headers.authorization;
        if (!auth || auth !== "Basic dGVzdDp0ZXN0") {
          resp.writeHead(401, {
            "WWW-Authenticate": 'Basic realm="Test Realm"',
            "Content-Type": "text/plain",
          });
          resp.end("");
        } else {
          resp.writeHead(200);
          resp.end("Authenticated");
        }
      });

      let chan = makeChan(
        `${server.protocol()}://alt1.example.com:${server.port()}/auth-required`
      );
      let [req] = await channelOpenPromise(
        chan,
        CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
      );

      // Should receive 401 Unauthorized through the tunnel
      Assert.equal(req.status, Cr.NS_OK);
      Assert.equal(req.QueryInterface(Ci.nsIHttpChannel).responseStatus, 401);
    }
  );
}

/**
 * Test HTTP CONNECT with large request/response data - ensures the tunnel
 * can handle substantial data transfer without corruption or truncation
 */
async function test_http_connect_large_data() {
  info("Running test_http_connect_large_data");
  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      info(
        `Testing large data transfer with ${server.constructor.name} server`
      );
      // Create a large response payload (1MB of data)
      const largeData = "x".repeat(1024 * 1024);
      await server.registerPathHandler("/large", (req, resp) => {
        const largeData = "x".repeat(1024 * 1024);
        resp.writeHead(200, { "Content-Type": "text/plain" });
        resp.end(largeData);
      });

      let chan = makeChan(
        `${server.protocol()}://alt1.example.com:${server.port()}/large`
      );
      let [req, buf] = await channelOpenPromise(
        chan,
        CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
      );

      Assert.equal(req.status, Cr.NS_OK);
      Assert.equal(buf.length, largeData.length);
      Assert.equal(buf, largeData);
    }
  );
}

/**
 * Test HTTP CONNECT tunnel connection refused - simulates target server
 * being unreachable or refusing connections
 */
async function test_http_connect_connection_refused() {
  info("Running test_http_connect_connection_refused");
  // Test connecting to a port that's definitely not in use
  let chan = makeChan(`http://alt1.example.com:667/refused`);
  let [req] = await channelOpenPromise(chan, CL_EXPECT_FAILURE);

  // Should fail to establish tunnel connection
  Assert.notEqual(req.status, Cr.NS_OK);
  info(`Connection refused status: ${req.status}`);
}

/**
 * Test HTTP CONNECT with invalid target host - verifies proper error handling
 * when trying to tunnel to a non-existent hostname
 */
async function test_http_connect_invalid_host() {
  info("Running test_http_connect_invalid_host");
  let chan = makeChan(`http://nonexistent.invalid.example/test`);
  let [req] = await channelOpenPromise(chan, CL_EXPECT_FAILURE);

  // Should fail DNS resolution for invalid hostname
  Assert.notEqual(req.status, Cr.NS_OK);
  info(`Invalid host status: ${req.status}`);
}

/**
 * Test concurrent HTTP CONNECT tunnels - ensures multiple simultaneous
 * requests can be established and used independently through the same H3 proxy
 */
async function test_concurrent_http_connect_tunnels() {
  info("Running test_concurrent_http_connect_tunnels");
  await with_node_servers(
    [NodeHTTPServer, NodeHTTPSServer, NodeHTTP2Server],
    async server => {
      info(`Testing concurrent tunnels with ${server.constructor.name} server`);

      // Register multiple endpoints
      await server.registerPathHandler("/concurrent1", (req, resp) => {
        resp.writeHead(200);
        resp.end("response1");
      });
      await server.registerPathHandler("/concurrent2", (req, resp) => {
        resp.writeHead(200);
        resp.end("response2");
      });
      await server.registerPathHandler("/concurrent3", (req, resp) => {
        resp.writeHead(200);
        resp.end("response3");
      });

      // Create multiple concurrent requests through the tunnel
      const promises = [];
      for (let i = 1; i <= 3; i++) {
        let chan = makeChan(
          `${server.protocol()}://alt1.example.com:${server.port()}/concurrent${i}`
        );
        promises.push(
          channelOpenPromise(chan, CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL)
        );
      }

      const results = await Promise.all(promises);

      // Verify all requests succeeded with correct responses
      for (let i = 0; i < 3; i++) {
        const [req, buf] = results[i];
        Assert.equal(req.status, Cr.NS_OK);
        Assert.equal(buf, `response${i + 1}`);
      }
      info("All concurrent tunnels completed successfully");
    }
  );
}

/**
 * Test HTTP CONNECT tunnel stream closure handling - verifies proper cleanup
 * when the tunnel connection is closed unexpectedly
 */
// eslint-disable-next-line no-unused-vars
async function test_http_connect_stream_closure() {
  info("Running test_http_connect_stream_closure");
  await with_node_servers([NodeHTTPServer], async server => {
    info(`Testing stream closure with ${server.constructor.name} server`);

    await server.registerPathHandler("/close", (req, resp) => {
      // Send partial response then close connection abruptly
      resp.writeHead(200, { "Content-Type": "text/plain" });
      resp.write("partial");
      // Simulate connection closure
      resp.destroy();
    });

    let chan = makeChan(
      `${server.protocol()}://alt1.example.com:${server.port()}/close`
    );
    let [req] = await channelOpenPromise(chan, CL_EXPECT_FAILURE);

    // Should handle connection closure gracefully
    Assert.notEqual(req.status, Cr.NS_OK);
    info(`Stream closure status: ${req.status}`);
  });
}

/**
 * Test connect-udp - SUCCESS case.
 * Will use h3 proxy to connect to h3 server.
 */
async function test_connect_udp() {
  info("Running test_connect_udp");
  let h3Port = Services.env.get("MOZHTTP3_PORT");
  info(`h3Port = ${h3Port}`);

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `alt1.example.com;h3=:${h3Port}`
  );

  {
    let chan = makeChan(`https://alt1.example.com:${h3Port}/no_body`);
    let [req] = await channelOpenPromise(
      chan,
      CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
    );
    Assert.equal(req.protocolVersion, "h3");
    Assert.equal(req.status, Cr.NS_OK);
    Assert.equal(req.responseStatus, 200);
    checkProxyType(req, Ci.nsIHttpChannelInternal.PROXY_TYPE_MASQUE);
  }
}

async function test_http_connect_fallback() {
  info("Running test_http_connect_fallback");
  pps.unregisterFilter(proxyFilter);

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    ""
  );

  let proxy = new NodeHTTP2ProxyServer();
  await proxy.startWithNoResponseUdpPort(() => proxy.startWithoutProxyFilter());
  dump(`proxy port=${proxy.port()}\n`);

  let server = new NodeHTTP2Server();
  await server.start();

  // Register multiple endpoints
  await server.registerPathHandler("/concurrent1", (req, resp) => {
    resp.writeHead(200);
    resp.end("response1");
  });
  await server.registerPathHandler("/concurrent2", (req, resp) => {
    resp.writeHead(200);
    resp.end("response2");
  });
  await server.registerPathHandler("/concurrent3", (req, resp) => {
    resp.writeHead(200);
    resp.end("response3");
  });

  let filter = new Http3ProxyFilter(
    proxyHost,
    proxy.port(),
    0,
    "/.well-known/masque/udp/{target_host}/{target_port}/",
    proxyAuth
  );
  pps.registerFilter(filter, 10);

  registerCleanupFunction(async () => {
    await proxy.stop();
    await server.stop();
  });

  // Create multiple concurrent requests through the tunnel
  const promises = [];
  for (let i = 1; i <= 3; i++) {
    let chan = makeChan(
      `${server.protocol()}://alt1.example.com:${server.port()}/concurrent${i}`
    );
    promises.push(channelOpenPromise(chan, CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL));
  }

  const results = await Promise.all(promises);

  // Verify all requests succeeded with correct responses
  for (let i = 0; i < 3; i++) {
    const [req, buf] = results[i];
    Assert.equal(req.status, Cr.NS_OK);
    Assert.equal(buf, `response${i + 1}`);
    // The proxy is reached over HTTP/2 instead of HTTP/3.
    checkProxyType(req, Ci.nsIHttpChannelInternal.PROXY_TYPE_HTTPS);
  }

  let h3Port = server.port();
  console.log(`h3Port = ${h3Port}`);

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `alt1.example.com;h3=:${h3Port}`
  );

  let chan = makeChan(`https://alt1.example.com:${h3Port}/concurrent1`);
  let [req] = await channelOpenPromise(
    chan,
    CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
  );
  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(req.responseStatus, 200);

  await proxy.stop();
  pps.unregisterFilter(filter);
  await server.stop();
}

/**
 * Helper function to open a WebSocket connection
 */
async function wsChannelOpenPromise(url, msg) {
  let conn = new WebSocketConnection();
  let statusObj = await Promise.race([conn.open(url), conn.finished()]);
  if (statusObj && statusObj.status != Cr.NS_OK) {
    return [statusObj.status, ""];
  }
  let finalStatusPromise = conn.finished();
  conn.send(msg);
  let res = await conn.receiveMessages();
  conn.close();
  let finalStatus = await finalStatusPromise;
  return [finalStatus.status, res];
}

/**
 * Test WebSocket through H3 proxy using H2 WebSocket server
 */
async function test_http_connect_websocket() {
  info("Running test_http_connect_websocket");
  Services.prefs.setBoolPref("network.http.http2.websockets", true);

  let wss = new NodeWebSocketHttp2Server();
  await wss.start();

  registerCleanupFunction(async () => {
    await wss.stop();
  });

  Assert.notEqual(wss.port(), null);
  await wss.registerMessageHandler((data, ws) => {
    ws.send(data);
  });

  let url = `wss://alt1.example.com:${wss.port()}`;
  const msg = "test h2 websocket through h3 proxy";
  let [status, res] = await wsChannelOpenPromise(url, msg);
  Assert.equal(status, Cr.NS_OK);
  Assert.deepEqual(res, [msg]);

  // Test multiple messages
  let conn = new WebSocketConnection();
  await conn.open(url);
  conn.send("message1");
  let mess1 = await conn.receiveMessages();
  Assert.deepEqual(mess1, ["message1"]);

  conn.send("message2");
  conn.send("message3");
  let mess2 = [];
  while (mess2.length < 2) {
    mess2 = mess2.concat(await conn.receiveMessages());
  }
  Assert.deepEqual(mess2, ["message2", "message3"]);

  conn.close();
  let { status: finalStatus } = await conn.finished();
  Assert.equal(finalStatus, Cr.NS_OK);

  await wss.stop();
}

async function test_inner_connection_fallback(ServerClass) {
  info("Running test_inner_connection_fallback");

  // Register the connect-udp proxy.
  pps.registerFilter(proxyFilter, 10);

  // The H3 attempt must get no response while the TCP fallback reaches server.
  let server = new ServerClass();
  await server.startWithNoResponseUdpPort();
  let h3Port = server.port();
  info(`h3Port = ${h3Port}`);

  // Register multiple endpoints
  await server.registerPathHandler("/concurrent1", (req, resp) => {
    resp.writeHead(200);
    resp.end("fallback1");
  });
  await server.registerPathHandler("/concurrent2", (req, resp) => {
    resp.writeHead(200);
    resp.end("fallback2");
  });
  await server.registerPathHandler("/concurrent3", (req, resp) => {
    resp.writeHead(200);
    resp.end("fallback3");
  });
  registerCleanupFunction(async () => {
    await server.stop();
  });

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `alt1.example.com;h3=:${h3Port}`
  );

  // Create multiple concurrent requests through the tunnel
  const promises = [];
  for (let i = 1; i <= 3; i++) {
    let chan = makeChan(
      `${server.protocol()}://alt1.example.com:${h3Port}/concurrent${i}`
    );
    promises.push(channelOpenPromise(chan, CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL));
  }

  const results = await Promise.all(promises);

  // Verify all requests succeeded with correct responses
  for (let i = 0; i < 3; i++) {
    const [req, buf] = results[i];
    Assert.equal(req.status, Cr.NS_OK);
    Assert.equal(buf, `fallback${i + 1}`);
  }
  await server.stop();

  pps.unregisterFilter(proxyFilter);
}

const CC = Components.Constructor;
const BinaryInputStream = CC(
  "@mozilla.org/binaryinputstream;1",
  "nsIBinaryInputStream",
  "setInputStream"
);
const BinaryOutputStream = CC(
  "@mozilla.org/binaryoutputstream;1",
  "nsIBinaryOutputStream",
  "setOutputStream"
);

function createUpgradeHandlers({ writePayload = "hello" } = {}) {
  const handler = {
    _resolve: null,

    waitForData() {
      return new Promise(resolve => {
        this._resolve = resolve;
      });
    },

    onInputStreamReady(input) {
      try {
        const bis = new BinaryInputStream(input);
        const n = input.available();
        if (n > 0) {
          const data = bis.readByteArray(n);
          if (this._resolve) {
            this._resolve(data);
            this._resolve = null;
          }
        }
      } catch (e) {
        Assert.ok(false, `onInputStreamReady threw: ${e}`);
      }
    },

    onOutputStreamReady(output) {
      try {
        const bos = new BinaryOutputStream(output);
        bos.writeByteArray(new TextEncoder().encode(writePayload));
      } catch (e) {
        Assert.ok(false, `onOutputStreamReady threw: ${e}`);
      }
    },

    QueryInterface: ChromeUtils.generateQI([
      "nsIInputStreamCallback",
      "nsIOutputStreamCallback",
    ]),
  };

  const listener = {
    onTransportAvailable(transport, socketIn, socketOut) {
      Assert.ok(
        transport && socketIn && socketOut,
        "transport and streams present"
      );
      socketIn.asyncWait(handler, 0, 0, Services.tm.mainThread);
      socketOut.asyncWait(handler, 0, 0, Services.tm.mainThread);
    },
    QueryInterface: ChromeUtils.generateQI(["nsIHttpUpgradeListener"]),
  };

  return { upgradeListener: listener, connectHandler: handler };
}

async function test_http_connect_only() {
  info("Running test_http_connect_only");
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    ""
  );

  let echo = new NodeTCPEchoServer();
  await echo.start();
  registerCleanupFunction(async () => {
    await echo.stop();
  });

  pps.registerFilter(proxyFilter, 10);

  const TEST_DATA = "HelloWorld";
  const { upgradeListener, connectHandler } = createUpgradeHandlers({
    writePayload: TEST_DATA,
  });

  const dataPromise = connectHandler.waitForData();

  let chan = makeChan(`http://localhost:${echo.port()}/`);
  let internal = chan.QueryInterface(Ci.nsIHttpChannelInternal);
  internal.HTTPUpgrade("webrtc", upgradeListener);
  internal.setConnectOnly(true);

  let [req] = await channelOpenPromise(
    chan,
    CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
  );

  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(req.responseStatus, 200);

  const data = await dataPromise;
  Assert.equal(TEST_DATA, String.fromCharCode.apply(String, data));
}

async function test_https_connect_only() {
  info("Running test_http_connect_only");
  pps.unregisterFilter(proxyFilter);

  let echo = new NodeTLSEchoServer();
  await echo.start();
  registerCleanupFunction(async () => {
    await echo.stop();
  });

  pps.registerFilter(proxyFilter, 10);

  const TEST_DATA = "HelloWorld";
  const { upgradeListener, connectHandler } = createUpgradeHandlers({
    writePayload: TEST_DATA,
  });

  const dataPromise = connectHandler.waitForData();

  let chan = makeChan(`https://localhost:${echo.port()}/`);
  let internal = chan.QueryInterface(Ci.nsIHttpChannelInternal);
  internal.HTTPUpgrade("webrtc", upgradeListener);
  internal.setConnectOnly(true);

  let [req] = await channelOpenPromise(
    chan,
    CL_IGNORE_CL | CL_ALLOW_UNKNOWN_CL
  );

  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(req.responseStatus, 200);

  const data = await dataPromise;
  Assert.equal(TEST_DATA, String.fromCharCode.apply(String, data));
}
