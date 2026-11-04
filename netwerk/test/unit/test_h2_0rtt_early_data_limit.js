/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Bug 2074493: an HTTP/2 request sent as 0-RTT whose first flight is larger
// than the server's early data allowance. Once the allowance is used up NSS
// accepts no more bytes until the handshake completes; the rest of the request
// must be sent after the handshake instead of the connection being treated as
// closed.
//
// ZeroRttAcceptServer is started with MOZ_ZERORTT_MAX_EARLY_DATA, so its
// session tickets allow only a small amount of early data. The server reports
// each HEADERS frame it receives as "early" (on a connection that accepted
// 0-RTT) or "std" (on a connection that did not).

"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);
var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

// The server accepts 0-RTT for the first host and refuses it for the second.
const ACCEPT_HOST = "0rtt-accept-h2.example.com";
const REJECT_HOST = "0rtt-reject-h2.example.com";
const MAX_EARLY_DATA = 512;

let callbackServer;
let gServerStarted = false;
let earlyCount = 0;
let stdCount = 0;

function callbackHandler(metadata) {
  if (metadata.path === "/callback/request/early") {
    earlyCount++;
  } else if (metadata.path === "/callback/request/std") {
    stdCount++;
  }
}

add_setup(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS,
  },
  async () => {
    callbackServer = new HttpServer();
    callbackServer.registerPrefixHandler("/callback/", callbackHandler);
    callbackServer.start(-1);

    Services.env.set(
      "MOZ_ZERORTT_ACCEPT_CALLBACK_PORT",
      callbackServer.identity.primaryPort
    );
    Services.env.set("MOZ_TLS_SERVER_0RTT", "1");
    Services.env.set("MOZ_ZERORTT_MAX_EARLY_DATA", `${MAX_EARLY_DATA}`);
    const started = await asyncStartTLSTestServer(
      "ZeroRttAcceptServer",
      "../../../security/manager/ssl/tests/unit/test_faulty_server"
    );
    Services.env.set("MOZ_ZERORTT_MAX_EARLY_DATA", "");
    if (!started) {
      return;
    }
    gServerStarted = true;

    Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
    Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
    Services.prefs.setBoolPref("network.ssl_tokens_cache_enabled", true);
    Services.prefs.setBoolPref("network.http.http3.enable", false);
    // The test server listens on IPv4 only; an IPv6 attempt would use up the
    // single-use session ticket.
    Services.prefs.setBoolPref("network.dns.disableIPv6", true);
    Services.prefs.setCharPref(
      "network.dns.localDomains",
      `${ACCEPT_HOST},${REJECT_HOST}`
    );

    registerCleanupFunction(async () => {
      Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
      Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
      Services.prefs.clearUserPref("network.ssl_tokens_cache_enabled");
      Services.prefs.clearUserPref("network.http.http3.enable");
      Services.prefs.clearUserPref("network.dns.disableIPv6");
      Services.prefs.clearUserPref("network.dns.localDomains");
      if (callbackServer) {
        await callbackServer.stop();
      }
    });
  }
);

// A header value HPACK can't compress much, several times the early data
// allowance.
function bigHeaderValue() {
  const chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  let seed = 7;
  let value = "";
  for (let i = 0; i < 4096; i++) {
    seed = (seed * 1103515245 + 12345) % 2147483648;
    value += chars[seed % chars.length];
  }
  return value;
}

function fetch(aHost, aBigHeader) {
  return new Promise(resolve => {
    let chan = NetUtil.newChannel({
      uri: `https://${aHost}:8443/`,
      loadUsingSystemPrincipal: true,
    }).QueryInterface(Ci.nsIHttpChannel);
    chan.loadFlags = Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
    if (aBigHeader) {
      chan.setRequestHeader("X-Big", bigHeaderValue(), false);
    }
    chan.asyncOpen({
      QueryInterface: ChromeUtils.generateQI([
        "nsIStreamListener",
        "nsIRequestObserver",
      ]),
      onStartRequest() {},
      onDataAvailable(request, stream, offset, count) {
        read_stream(stream, count);
      },
      onStopRequest(request, status) {
        let httpChan = request.QueryInterface(Ci.nsIHttpChannel);
        let responseStatus = 0;
        let protocol = "";
        try {
          responseStatus = httpChan.responseStatus;
          protocol = httpChan.protocolVersion;
        } catch (e) {}
        resolve({ status: responseStatus, protocol, result: status });
      },
    });
  });
}

async function closeConnections() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 200));
}

async function runTest(aHost) {
  let tokenCache = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
    Ci.nsISSLTokensCache
  );
  await tokenCache.asyncClearSSLExternalAndInternalSessionCache();
  earlyCount = 0;
  stdCount = 0;

  // Full handshake, which gives us a session ticket allowing 0-RTT.
  let first = await fetch(aHost, false);
  Assert.equal(first.status, 200, "first fetch succeeds");
  Assert.equal(first.protocol, "h2", "first fetch uses h2");
  Assert.equal(stdCount, 1, "first fetch arrived without early data");

  // Let the NewSessionTicket arrive and the anti-replay window pass.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1500));
  await closeConnections();

  // Resumed with 0-RTT; the request does not fit in the early data allowance.
  let second = await fetch(aHost, true);
  info(
    `second fetch: result=0x${second.result.toString(16)} early=${earlyCount} std=${stdCount}`
  );
  Assert.equal(second.result, Cr.NS_OK, "second fetch succeeds");
  Assert.equal(second.status, 200, "second fetch gets a response");
  if (aHost == ACCEPT_HOST) {
    Assert.equal(
      earlyCount,
      1,
      "second fetch completed on the connection that accepted 0-RTT"
    );
    Assert.equal(
      stdCount,
      1,
      "second fetch was not retried on another connection"
    );
  } else {
    // The server refuses the early data, so the whole request is resent once
    // the handshake completes.
    Assert.equal(earlyCount, 0, "no request arrived as 0-RTT");
    Assert.equal(stdCount, 2, "second fetch arrived after the handshake");
  }

  await closeConnections();
}

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_h2_0rtt_early_data_limit_accepted() {
    await runTest(ACCEPT_HOST);
  }
);

add_task(
  {
    skip_if: () => AppConstants.MOZ_SYSTEM_NSS || !gServerStarted,
  },
  async function test_h2_0rtt_early_data_limit_rejected() {
    await runTest(REJECT_HOST);
  }
);
