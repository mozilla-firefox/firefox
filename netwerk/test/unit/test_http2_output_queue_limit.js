/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// When the socket can't be written to, request headers pile up in the HTTP/2
// session's output queue. Once the queue would grow beyond
// network.http.http2.max_output_queue_size the session must fail instead.

"use strict";

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const mockController = Cc[
  "@mozilla.org/network/mock-network-controller;1"
].getService(Ci.nsIMockNetworkLayerController);

const MAX_OUTPUT_QUEUE_SIZE = 1024 * 1024;
// "&" has an 8-bit Huffman code, so the header block is as large as the value.
// One fits in the output queue, but two exceed the limit. Writes are paused,
// so the first request's headers stay in the queue when the second one is
// added.
const HEADER_VALUE = "&".repeat(768 * 1024);

let server;

add_setup(async function () {
  Services.prefs.setIntPref(
    "network.http.http2.max_output_queue_size",
    MAX_OUTPUT_QUEUE_SIZE
  );
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);

  server = new NodeHTTP2Server();
  await server.start();
  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref("network.http.http2.max_output_queue_size");
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
    mockController.clearPausedTCPWrite();
    await server.stop();
  });

  await server.registerPathHandler("/", (req, res) => {
    res.end("ok");
  });
});

function makeChannel(path, bigHeader = false) {
  let chan = NetUtil.newChannel({
    uri: `https://localhost:${server.port()}${path}`,
    loadUsingSystemPrincipal: true,
  }).QueryInterface(Ci.nsIHttpChannel);
  if (bigHeader) {
    chan.setRequestHeader("X-Big", HEADER_VALUE, false);
  }
  return chan;
}

function openChannel(chan, flags = 0) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        (req, buffer) => resolve({ status: req.status, buffer }),
        null,
        flags | CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

// Resolves once the channel's request has been handed to the HTTP/2 session.
function waitForRequestQueued(chan) {
  return new Promise(resolve => {
    chan.notificationCallbacks = {
      QueryInterface: ChromeUtils.generateQI([
        "nsIInterfaceRequestor",
        "nsIProgressEventSink",
      ]),
      getInterface(iid) {
        return this.QueryInterface(iid);
      },
      onProgress() {},
      onStatus(request, status) {
        if (status == Ci.nsISocketTransport.STATUS_WAITING_FOR) {
          resolve();
        }
      },
    };
  });
}

add_task(async function test_output_queue_limit() {
  let { status, buffer } = await openChannel(makeChannel("/"));
  Assert.equal(status, Cr.NS_OK);
  Assert.equal(buffer, "ok");

  for (let host of ["127.0.0.1", "::1"]) {
    mockController.pauseTCPWrite(
      mockController.createScriptableNetAddr(host, server.port())
    );
  }

  let first = makeChannel("/?first", true);
  let firstQueued = waitForRequestQueued(first);
  let firstDone = openChannel(first, CL_EXPECT_FAILURE);
  await firstQueued;

  let secondDone = false;
  let second = openChannel(
    makeChannel("/?second", true),
    CL_EXPECT_FAILURE
  ).then(result => {
    secondDone = true;
    return result;
  });
  first.cancel(Cr.NS_BINDING_ABORTED);
  await firstDone;

  // The socket doesn't become writable, so the session only reads more of the
  // second request when something else makes it write. Adding a stream does:
  // Http2Session::AddStream calls ReadSegments right away, without waiting for
  // the socket. There is no event for the second request's progress, so open
  // and cancel requests until it finishes. It takes a few of them.
  const MAX_NUDGES = 100;
  for (let i = 0; i < MAX_NUDGES && !secondDone; i++) {
    let chan = makeChannel("/?nudge");
    let done = openChannel(chan, CL_EXPECT_FAILURE);
    chan.cancel(Cr.NS_BINDING_ABORTED);
    await done;
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  Assert.ok(secondDone, "the second request finished");

  ({ status } = await second);
  Assert.equal(
    status,
    Cr.NS_ERROR_OUT_OF_MEMORY,
    "the output queue limit was reached"
  );

  // A new session works.
  mockController.clearPausedTCPWrite();
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  ({ status, buffer } = await openChannel(makeChannel("/")));
  Assert.equal(status, Cr.NS_OK);
  Assert.equal(buffer, "ok");
});
