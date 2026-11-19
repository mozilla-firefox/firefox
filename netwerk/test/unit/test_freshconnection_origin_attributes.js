/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A LOAD_FRESH_CONNECTION | LOAD_INITIAL_DOCUMENT_URI load must only reset
// the connections and Alt-Svc cache entries that share its origin attributes.

const { NodeHTTPServer, NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

let server;
let h2Server;
let h2Port;

const ORIGIN_HOST = "foo.example.com";
const ALT_HOST = "alt1.example.com";

add_setup(async function setup() {
  do_get_profile();
  Services.prefs.setBoolPref("network.http.altsvc.enabled", true);
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    `${ORIGIN_HOST},${ALT_HOST}`
  );
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("network.http.altsvc.enabled");
    Services.prefs.clearUserPref("network.dns.localDomains");
  });
  server = new NodeHTTPServer();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });
  await server.registerPathHandler("/", (req, resp) => {
    resp.setHeader("x-client-port", req.socket.remotePort);
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("ok");
  });

  h2Server = new NodeHTTP2Server();
  await h2Server.start();
  registerCleanupFunction(async () => {
    await h2Server.stop();
  });
  h2Port = h2Server.port();
  await h2Server.registerPathHandler("/", (req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("ok");
  });
  await h2Server.registerPathHandler("/altsvc-test", (req, resp) => {
    resp.setHeader("Cache-Control", "no-cache");
    resp.setHeader("Alt-Svc", "h2=" + req.headers["x-altsvc"]);
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("ok");
  });
});

const FRESH_FLAGS =
  Ci.nsIRequest.LOAD_FRESH_CONNECTION | Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;

function makeChan(uri, originAttributes) {
  let chan = NetUtil.newChannel({
    uri,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  chan.loadInfo.originAttributes = originAttributes;
  return chan;
}

async function getClientPort(originAttributes, loadFlags = 0) {
  let chan = makeChan(`http://localhost:${server.port()}/`, originAttributes);
  chan.loadFlags = loadFlags;
  let [req] = await new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (request, buffer) => resolve([request, buffer]),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
  Assert.equal(req.responseStatus, 200);
  return parseInt(req.getResponseHeader("x-client-port"));
}

async function testFreshConnectionScoping(oa1, oa2) {
  let port1 = await getClientPort(oa1);
  Assert.equal(await getClientPort(oa1), port1, "Connection is reused");

  let port2 = await getClientPort(oa2, FRESH_FLAGS);
  Assert.notEqual(port2, port1, "Different OA uses a new connection");

  Assert.equal(
    await getClientPort(oa1),
    port1,
    "Fresh load with another OA keeps this OA's connection"
  );

  let port1Fresh = await getClientPort(oa1, FRESH_FLAGS);
  Assert.notEqual(port1Fresh, port1, "Fresh load opens a new connection");
  Assert.notEqual(
    await getClientPort(oa1),
    port1,
    "Fresh load closed the old connection with the same OA"
  );
}

add_task(async function test_fresh_connection_scoped_to_user_context_id() {
  await testFreshConnectionScoping({ userContextId: 1 }, { userContextId: 2 });
});

add_task(async function test_fresh_connection_scoped_to_partition_key() {
  await testFreshConnectionScoping(
    { partitionKey: "(https,example.com)" },
    { partitionKey: "(https,example.org)" }
  );
});

async function loadAltSvcOrigin(originAttributes, path, loadFlags = 0) {
  let chan = makeChan(
    `https://${ORIGIN_HOST}:${h2Port}${path}`,
    originAttributes
  );
  chan.loadFlags = loadFlags;
  chan.setRequestHeader("x-altsvc", `${ALT_HOST}:${h2Port}`, false);
  let [req] = await new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (request, buffer) => resolve([request, buffer]),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
  Assert.equal(req.responseStatus, 200);
}

function hasAltSvcMapping(originAttributes) {
  let suffix = ChromeUtils.originAttributesToSuffix(originAttributes);
  return Services.io
    .getProtocolHandler("http")
    .QueryInterface(Ci.nsIHttpProtocolHandler)
    .altSvcCacheKeys.some(key =>
      key.startsWith(`https:${ORIGIN_HOST}:${h2Port}:.:${suffix}:`)
    );
}

async function testFreshConnectionScopingAltSvc(oa1, oa2) {
  await loadAltSvcOrigin(oa1, "/altsvc-test");
  await TestUtils.waitForCondition(
    () => hasAltSvcMapping(oa1),
    "Alt-Svc mapping was stored"
  );

  await loadAltSvcOrigin(oa2, "/altsvc-test", FRESH_FLAGS);
  Assert.ok(
    hasAltSvcMapping(oa1),
    "Fresh load with different OA keeps first OA's Alt-Svc mapping"
  );

  await loadAltSvcOrigin(oa1, "/", FRESH_FLAGS);
  Assert.ok(!hasAltSvcMapping(oa1), "Fresh load should remove Alt-Svc mapping");
}

add_task(
  async function test_fresh_connection_altsvc_scoped_to_user_context_id() {
    await testFreshConnectionScopingAltSvc(
      { userContextId: 1 },
      { userContextId: 2 }
    );
  }
);

add_task(async function test_fresh_connection_altsvc_scoped_to_partition_key() {
  await testFreshConnectionScopingAltSvc(
    { partitionKey: "(https,example.com)" },
    { partitionKey: "(https,example.org)" }
  );
});
