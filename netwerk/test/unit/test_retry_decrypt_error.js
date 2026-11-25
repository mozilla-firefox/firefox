/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that when a server sends a fatal decrypt_error alert on a TLS session
// resumption attempt (simulating a server whose session ticket encryption key
// has rotated), Firefox automatically retries the connection with a full
// handshake and succeeds.
//
// FaultyServer fires at epoch 1 read (early traffic secret), which fires only
// when the client offers a PSK with early data.  This requires
// MOZ_TLS_SERVER_0RTT so the first connection's NewSessionTicket carries
// maxEarlyDataSize > 0.  The alert is sent before ServerHello at the
// unencrypted record layer, producing SSL_ERROR_DECRYPT_ERROR_ALERT on the
// client — the same error reported in Bug 2033347.
//
// Regression test for Bug 2033073 / Bug 2033347.

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const kHost = "decrypt-error-on-resume.example.com";

var httpServer = null;
let resumeCallbackCount = 0;

add_setup(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async () => {
    httpServer = new HttpServer();
    httpServer.registerPathHandler("/callback/1", () => {
      resumeCallbackCount++;
    });
    httpServer.start(-1);
    registerCleanupFunction(async () => {
      await httpServer.stop();
    });
    await asyncSetupFaultyServer(httpServer);
    Services.prefs.setCharPref("network.dns.localDomains", kHost);
    registerCleanupFunction(() => {
      Services.prefs.clearUserPref("network.dns.localDomains");
    });
  }
);

// Bug 2033073 / Bug 2033347: When a persisted TLS session ticket causes the
// server to send a fatal decrypt_error alert (e.g. server STEK rotation),
// Firefox must automatically retry the connection with a full handshake
// instead of surfacing "Secure Connection Failed" to the user.
add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async function test_retry_on_decrypt_error() {
    // First connection: no cached session ticket → full TLS 1.3 handshake.
    // The server issues a NewSessionTicket (with maxEarlyDataSize > 0 because
    // MOZ_TLS_SERVER_0RTT is set), which Firefox caches in SSLTokensCache.
    // The FaultyServer callback does NOT fire here because the client has no
    // PSK to offer, so no early data is sent and epoch 1 is never derived.
    {
      let beforeCallbacks = resumeCallbackCount;
      let chan = makeChan(`https://${kHost}:8443/`);
      let [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
      ok(buf, "first connection succeeded");
      equal(
        resumeCallbackCount,
        beforeCallbacks,
        "FaultyServer did not fire on fresh handshake"
      );
    }

    // The server has an anti-replay mechanism that prohibits it from accepting
    // 0-RTT connections immediately after issuing a ticket.
    await sleep(1);

    // Second connection: Firefox finds the cached session ticket, offers it as
    // a PSK, and sends early data.  FaultyServer detects epoch 1 (early traffic
    // secret) and sends a fatal decrypt_error alert — simulating a server whose
    // STEK has rotated.  Firefox must automatically retry with a full handshake
    // (MaybeRemoveSSLToken evicts the stale token) and succeed.
    {
      let beforeCallbacks = resumeCallbackCount;
      let chan = makeChan(`https://${kHost}:8443/`);
      let [, buf] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
      ok(buf, "second connection succeeded after decrypt_error retry");
      equal(
        resumeCallbackCount,
        beforeCallbacks + 1,
        "FaultyServer fired exactly once (on the resumption attempt)"
      );
    }
  }
);
