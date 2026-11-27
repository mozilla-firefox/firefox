/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

async function connectAndClose(port) {
  const socket = new TCPSocket("127.0.0.1", port, {
    useSecureTransport: true,
  });
  await new Promise((resolve, reject) => {
    socket.onopen = resolve;
    socket.onerror = reject;
  });
  Assert.equal(socket.readyState, "open");
  await new Promise(resolve => {
    socket.onclose = resolve;
    socket.close();
  });
}

// The TLS handshake completing on the socket thread before the main thread
// handles STATUS_CONNECTED_TO must still lead to the "open" event.
add_task(async function test_handshake_done_before_connected_status() {
  do_get_profile();
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  const mock = Cc["@mozilla.org/network/mock-network-controller;1"].getService(
    Ci.nsIMockNetworkLayerController
  );
  const cert = await getTestServerCertificate();
  const server = Cc["@mozilla.org/network/tls-server-socket;1"].createInstance(
    Ci.nsITLSServerSocket
  );
  server.init(-1, true, -1);
  server.serverCert = cert;
  server.setSessionTickets(false);
  let serverTransport;
  let copier;
  registerCleanupFunction(() => {
    copier?.cancel(Cr.NS_BINDING_ABORTED);
    serverTransport?.close(Cr.NS_BINDING_ABORTED);
    server.close();
    mock.clearPausedTCPWrite();
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
  });
  server.asyncListen({
    onSocketAccepted(_, transport) {
      serverTransport = transport;
      copier = Cc["@mozilla.org/network/async-stream-copier;1"].createInstance(
        Ci.nsIAsyncStreamCopier
      );
      copier.init(
        transport.openInputStream(0, 0, 0),
        transport.openOutputStream(0, 0, 0),
        Cc["@mozilla.org/network/socket-transport-service;1"].getService(),
        true,
        true,
        4096,
        true,
        true
      );
      mock.clearPausedTCPWrite();
      copier.asyncCopy(null, null);
      // Keep the main thread busy while the TLS handshake completes.
      for (let now = Date.now(), end = now + 300; now < end; ) {
        now = Date.now();
      }
    },
  });
  Cc["@mozilla.org/security/certoverride;1"]
    .getService(Ci.nsICertOverrideService)
    .rememberValidityOverride("127.0.0.1", server.port, {}, cert, false);
  mock.pauseTCPWrite(mock.createScriptableNetAddr("127.0.0.1", server.port));

  await connectAndClose(server.port);
});

// Clearing a mock write pause doesn't wake the socket thread's poll. Starting a
// copy on the socket thread does, so it notices the connection right away.
function wakeSocketThread() {
  const source = Cc["@mozilla.org/io/string-input-stream;1"].createInstance(
    Ci.nsIStringInputStream
  );
  source.setByteStringData("x");
  const storage = Cc["@mozilla.org/storagestream;1"].createInstance(
    Ci.nsIStorageStream
  );
  storage.init(16, 16);
  NetUtil.asyncCopy(source, storage.getOutputStream(0));
}

function busyWait(ms) {
  for (let now = Date.now(), end = now + ms; now < end; ) {
    now = Date.now();
  }
}

// WeakRef.deref() keeps its target alive until the end of the current job, so
// every GC has to happen in a job that didn't call it.
async function isCollected(weakRef) {
  for (let i = 0; i < 10; i++) {
    await new Promise(resolve => do_timeout(50, resolve));
    Cu.forceGC();
    Cu.forceCC();
    await new Promise(resolve => do_timeout(50, resolve));
    if (!weakRef.deref()) {
      return true;
    }
  }
  return false;
}

// Closing the socket before the main thread handles STATUS_CONNECTED_TO must
// not leave a TLS handshake listener behind that keeps the socket alive.
add_task(async function test_close_before_connected_status() {
  Services.prefs.setBoolPref("network.socket.attach_mock_network_layer", true);
  const mock = Cc["@mozilla.org/network/mock-network-controller;1"].getService(
    Ci.nsIMockNetworkLayerController
  );
  const server = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  server.init(-1, true, -1);
  const state = { socket: null, serverTransport: null };
  registerCleanupFunction(() => {
    state.serverTransport?.close(Cr.NS_BINDING_ABORTED);
    server.close();
    mock.clearPausedTCPWrite();
    Services.prefs.clearUserPref("network.socket.attach_mock_network_layer");
  });
  const closed = new Promise(resolve => {
    server.asyncListen({
      onSocketAccepted(_, transport) {
        state.serverTransport = transport;
        mock.clearPausedTCPWrite();
        wakeSocketThread();
        busyWait(300);
        state.socket.onclose = () => resolve();
        state.socket.close();
        state.socket = null;
      },
    });
  });
  mock.pauseTCPWrite(mock.createScriptableNetAddr("127.0.0.1", server.port));

  const weakRef = (() => {
    state.socket = new TCPSocket("127.0.0.1", server.port, {
      useSecureTransport: true,
    });
    return new WeakRef(state.socket);
  })();
  await closed;

  Assert.ok(await isCollected(weakRef), "The closed socket can be collected");
});
