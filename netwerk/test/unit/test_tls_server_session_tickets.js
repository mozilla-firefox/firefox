/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

async function connectAndCheckResumed(port) {
  const socket = new TCPSocket("127.0.0.1", port, {
    useSecureTransport: true,
  });
  await new Promise((resolve, reject) => {
    socket.onopen = () => socket.send("x");
    socket.ondata = () => resolve();
    socket.onerror = e => reject(new Error(e.name + ": " + e.message));
  });
  const securityInfo =
    await socket.transport.tlsSocketControl.asyncGetSecurityInfo();
  await new Promise(resolve => {
    socket.onclose = () => resolve();
    socket.close();
  });
  return securityInfo.resumed;
}

// Issuing a session ticket makes NSS create a server wrapping key that is
// never freed, which makes NSS_Shutdown fail, so TLSServerSocket must not
// issue tickets unless asked to.
add_task(async function test_no_session_ticket_by_default() {
  do_get_profile();
  const cert = await getTestServerCertificate();
  const server = Cc["@mozilla.org/network/tls-server-socket;1"].createInstance(
    Ci.nsITLSServerSocket
  );
  server.init(-1, true, -1);
  server.serverCert = cert;
  const transports = [];
  registerCleanupFunction(() => {
    for (const transport of transports) {
      transport.close(Cr.NS_BINDING_ABORTED);
    }
    server.close();
  });
  server.asyncListen({
    onSocketAccepted(_, transport) {
      transports.push(transport);
      const copier = Cc[
        "@mozilla.org/network/async-stream-copier;1"
      ].createInstance(Ci.nsIAsyncStreamCopier);
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
      copier.asyncCopy(null, null);
    },
  });
  Cc["@mozilla.org/security/certoverride;1"]
    .getService(Ci.nsICertOverrideService)
    .rememberValidityOverride("127.0.0.1", server.port, {}, cert, false);

  Assert.ok(!(await connectAndCheckResumed(server.port)), "First connection");
  Assert.ok(
    !(await connectAndCheckResumed(server.port)),
    "No ticket was issued to resume the session with"
  );
});
