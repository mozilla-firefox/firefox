/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { NodeHTTP2Server, HTTP3Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const HOST = "foo.example.com";

let h2Server;
let h3Server;

add_setup(async function () {
  Services.fog.initializeFOG();

  let h3ServerPath = Services.env.get("MOZ_HTTP3_SERVER_PATH");
  let h3DBPath = Services.env.get("MOZ_HTTP3_CERT_DB_PATH");

  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setIntPref("network.http.http3.max_pto", 1);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  h3Server = new HTTP3Server();
  await h3Server.start(h3ServerPath, h3DBPath);

  h2Server = new NodeHTTP2Server();
  await h2Server.start();
  await h2Server.registerPathHandler("/", (_req, resp) => {
    resp.writeHead(200, { "Content-Type": "text/plain" });
    resp.end("h2");
  });

  override.addIPOverride(HOST, "127.0.0.1");
  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    `${HOST};h3=:${h3Server.port()}`
  );

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref("network.http.http3.max_pto");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref(
      "network.http.http3.alt-svc-mapping-for-testing"
    );
    override.clearOverrides();
    await h3Server.stop();
    await h2Server.stop();
  });
});

function openChan(path = "/", isPost = false, expectFailure = false) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: `https://${HOST}:${h2Server.port()}${path}`,
      loadUsingSystemPrincipal: true,
    }).QueryInterface(Ci.nsIHttpChannel);
    chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
    if (isPost) {
      let stream = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
        Ci.nsIStringInputStream
      );
      stream.setByteStringData("foo");
      chan
        .QueryInterface(Ci.nsIUploadChannel)
        .setUploadStream(stream, "application/x-www-form-urlencoded", -1);
      chan.requestMethod = "POST";
    }
    chan.asyncOpen(
      new ChannelListener(
        (request, buffer) => {
          let httpChannel = request.QueryInterface(Ci.nsIHttpChannel);
          resolve({
            status: request.status,
            httpVersion: httpChannel.protocolVersion,
            connectionHash:
              !isPost && httpChannel.protocolVersion == "h3"
                ? httpChannel.getResponseHeader("x-http3-conn-hash")
                : null,
            buffer,
          });
        },
        null,
        CL_ALLOW_UNKNOWN_CL | (expectFailure ? CL_EXPECT_FAILURE : 0)
      )
    );
  });
}

function tooManyPtosCount() {
  return (
    Glean.networking.http3ConnectionCloseReason.TooManyPtos.testGetValue() ?? 0
  );
}

add_task(async function test_blackholed_h3_safe_transaction_is_retried() {
  let warmup = await openChan();
  Assert.equal(warmup.status, Cr.NS_OK, "Warm-up request succeeds");
  Assert.equal(warmup.httpVersion, "h3", "Warm-up request uses HTTP/3");

  let post = await openChan("/post", true);
  Assert.equal(post.status, Cr.NS_OK, "Post request succeeds");
  Assert.equal(post.httpVersion, "h3", "Post request uses HTTP/3");

  let initialTooManyPtosCount = tooManyPtosCount();
  let blackhole = await openChan("/SetBlackhole");
  Assert.equal(blackhole.status, Cr.NS_OK, "Blackhole request succeeds");
  Assert.equal(
    blackhole.connectionHash,
    warmup.connectionHash,
    "Blackhole request uses the warm-up connection"
  );

  let [getRequest, postRequest] = await Promise.all([
    openChan(),
    openChan("/post", true, true),
  ]);
  Assert.equal(
    getRequest.status,
    Cr.NS_OK,
    "Blackholed GET request is retried"
  );
  Assert.equal(getRequest.httpVersion, "h3", "Retry still uses HTTP/3");
  Assert.notEqual(
    warmup.connectionHash,
    getRequest.connectionHash,
    "Blackholed GET request is restarted on a different connection"
  );
  Assert.equal(
    postRequest.status,
    Cr.NS_ERROR_NET_RESET,
    "Blackholed post request is not retried"
  );

  Assert.equal(
    tooManyPtosCount(),
    initialTooManyPtosCount + 1,
    "TooManyPtos is recorded once"
  );
});
