/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// END_STREAM on a HEADERS frame applies to the whole header block, which can
// continue in CONTINUATION frames (RFC 9113, section 6.2). The server sends
// header blocks larger than the 16KB frame size Firefox advertises, so they
// are split into HEADERS (with END_STREAM) followed by CONTINUATION frames.

"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let server;

add_setup(async function () {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  addCertFromFile(certdb, "http2-ca.pem", "CTu,u,u");

  server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  // Headers that HPACK can't shrink below the frame size.
  await server.execute(`
    global.bigHeaders = function (prefix) {
      let headers = {};
      let seed = 7;
      for (let i = 0; i < 32; i++) {
        let value = "";
        for (let j = 0; j < 1024; j++) {
          seed = (seed * 1103515245 + 12345) % 2147483648;
          value += String.fromCharCode(97 + (seed % 26));
        }
        headers[prefix + i] = value;
      }
      return headers;
    };
  `);

  await server.registerPathHandler("/empty", (req, res) => {
    res.stream.respond(
      { ":status": 200, ...global.bigHeaders("x-big-header-") },
      { endStream: true }
    );
  });

  await server.registerPathHandler("/trailers", (req, res) => {
    res.writeHead(200, { "content-type": "text/plain" });
    res.addTrailers(global.bigHeaders("x-big-trailer-"));
    res.end("body");
  });
});

function openChan(path) {
  let chan = NetUtil.newChannel({
    uri: `https://localhost:${server.port()}${path}`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (req, buffer) => resolve({ req, buffer }),
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

add_task(async function test_empty_response_end_stream_on_continued_headers() {
  let { req, buffer } = await openChan("/empty");
  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(req.QueryInterface(Ci.nsIHttpChannel).responseStatus, 200);
  Assert.equal(req.protocolVersion, "h2");
  Assert.equal(buffer, "");
});

add_task(async function test_trailers_end_stream_on_continued_headers() {
  let { req, buffer } = await openChan("/trailers");
  Assert.equal(req.status, Cr.NS_OK);
  Assert.equal(req.QueryInterface(Ci.nsIHttpChannel).responseStatus, 200);
  Assert.equal(req.protocolVersion, "h2");
  Assert.equal(buffer, "body");
});
