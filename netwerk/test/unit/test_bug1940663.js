/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

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
      resolve([req, buffer]);
    }
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

// The whole response is received over HTTP/2 before the server resets the
// stream with HTTP_1_1_REQUIRED, so the response must be used as is.
add_task(async function test_http_1_1() {
  let server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    await server.stop();
  });

  await server.registerPathHandler("/h11required_with_content", (req, res) => {
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Content-Length", "ok".length);
    res.writeHead(200);
    // Only reset the stream once the data has been sent, otherwise the reset
    // could reach the client first.
    res.write("ok", () => {
      const http2 = require("http2");
      res.stream.close(http2.constants.NGHTTP2_HTTP_1_1_REQUIRED);
    });
  });

  let chan = makeChan(`${server.origin()}/h11required_with_content`);
  let [req, resp] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h2");
  Assert.equal(resp, "ok");
});
