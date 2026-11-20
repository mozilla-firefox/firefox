/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let h3Port;
let trrServer;

const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);

const { NodeHTTPServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

add_setup(async function setup() {
  h3Port = Services.env.get("MOZHTTP3_PORT_PROXY");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  trr_test_setup();

  Services.prefs.setBoolPref("network.dns.upgrade_with_https_rr", true);
  Services.prefs.setBoolPref("network.dns.use_https_rr_as_altsvc", true);

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.dns.upgrade_with_https_rr");
    Services.prefs.clearUserPref("network.dns.use_https_rr_as_altsvc");
    Services.prefs.clearUserPref("network.dns.disablePrefetch");
    await trrServer.stop();
  });
});

function makeChan(url) {
  let chan = NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
  return chan;
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    function finish(req, buffer) {
      certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
        false
      );
      resolve([req, buffer]);
    }
    certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
      true
    );
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

// Use NodeHTTPServer to create an HTTP server and test if the Http/3 server
// can act as a reverse proxy.
add_task(async function testHttp3ServerAsReverseProxy() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("test.h3_example.com", "HTTPS", {
    answers: [
      {
        name: "test.h3_example.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.h3_example.com",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3Port },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("test.h3_example.com", "A", {
    answers: [
      {
        name: "test.h3_example.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await new TRRDNSListener("test.h3_example.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let server = new NodeHTTPServer();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerPathHandler("/test", (req, resp) => {
    if (req.method === "GET") {
      resp.writeHead(200);
      resp.end("got GET request");
    } else if (req.method === "POST") {
      let received = "";
      req.on("data", function receivePostData(chunk) {
        received += chunk.toString();
      });
      req.on("end", function finishPost() {
        resp.writeHead(200);
        resp.end(received);
      });
    }
  });

  // Tell the Http/3 server which port to forward requests.
  let chan = makeChan(`https://test.h3_example.com/port?${server.port()}`);
  await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);

  // Test GET method.
  chan = makeChan(`https://test.h3_example.com/test`);
  let [req, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.protocolVersion, "h3");
  Assert.equal(buf, "got GET request");

  var stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  stream.setByteStringData("b".repeat(500));

  // Test POST method.
  chan = makeChan(`https://test.h3_example.com/test`);
  chan
    .QueryInterface(Ci.nsIUploadChannel)
    .setUploadStream(stream, "text/plain", stream.available());
  chan.requestMethod = "POST";

  [req, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.protocolVersion, "h3");
  Assert.equal(buf, stream.data);

  await trrServer.stop();
});
