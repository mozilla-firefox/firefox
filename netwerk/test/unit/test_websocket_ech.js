/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Checks that a secure WebSocket uses the ECH config advertised in an HTTPS RR
// (bug 2074787), with Happy Eyeballs enabled and disabled.
//
// The ECH test server runs as a reverse proxy: it terminates TLS (with ECH) and
// relays the decrypted stream to a plaintext WebSocket echo server, so each
// test opens a real WebSocket and exchanges a message over it.

"use strict";

/* import-globals-from head_trr.js */

const { NodeWebSocketPlainServer } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const ECH_HOST = "ech-private.example.com";
const ECH_PORT = 8443;
const ECH_CONFIG =
  "AEn+DQBFTQAgACCKB1Y5SfrGIyk27W82xPpzWTDs3q72c04xSurDWlb9CgAEAAEAA2QWZWNoLXB1YmxpYy5leGFtcGxlLmNvbQAA";
// The server rejects this config and sends back its real one as retry config.
const ECH_CONFIG_TRUSTED_RETRY =
  "AEn+DQBFTQAgACCKB1Y5SfrGIyk27W82xPpzWTDs3q72c04xSurDWlb9CgAEAAMAA2QWZWNoLXB1YmxpYy5leGFtcGxlLmNvbQAA";

const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);

let trrServer;

add_setup(async function setup() {
  trr_test_setup();

  Services.prefs.setBoolPref("network.dns.upgrade_with_https_rr", true);
  Services.prefs.setBoolPref("network.dns.use_https_rr_as_altsvc", true);
  Services.prefs.setBoolPref("network.dns.echconfig.enabled", true);
  // The URLs use the server's real port, so look up the HTTPS RR under the
  // plain host name.
  Services.prefs.setBoolPref("network.dns.port_prefixed_qname_https_rr", false);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
  // Connections are torn down between runs, so don't back off reconnects.
  Services.prefs.setBoolPref(
    "network.websocket.delay-failed-reconnects",
    false
  );
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await setHTTPSRR([{ port: ECH_PORT, echConfig: ECH_CONFIG }]);
  await trrServer.registerDoHAnswers(ECH_HOST, "A", {
    answers: [
      {
        name: ECH_HOST,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  let wsServer = new NodeWebSocketPlainServer();
  await wsServer.start();
  await wsServer.registerMessageHandler((data, ws) => {
    ws.send(data);
  });

  Services.env.set("MOZ_TLS_ECH_PROXY_PORT", `${wsServer.port()}`);
  await asyncStartTLSTestServer(
    "EncryptedClientHelloServer",
    "../../../security/manager/ssl/tests/unit/test_encrypted_client_hello"
  );
  Services.env.set("MOZ_TLS_ECH_PROXY_PORT", "");

  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
    true
  );

  registerCleanupFunction(async () => {
    certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
      false
    );
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.trr.mode");
    Services.prefs.clearUserPref("network.trr.uri");
    Services.prefs.clearUserPref("network.dns.upgrade_with_https_rr");
    Services.prefs.clearUserPref("network.dns.use_https_rr_as_altsvc");
    Services.prefs.clearUserPref("network.dns.echconfig.enabled");
    Services.prefs.clearUserPref("network.dns.port_prefixed_qname_https_rr");
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.websocket.delay-failed-reconnects");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_upgrade_enabled");
    Services.prefs.clearUserPref("network.http.upgrade_use_https_rr");
    await wsServer.stop();
    await trrServer.stop();
  });
});

// Serves one HTTPS RR per entry of aRecords, in priority order.
async function setHTTPSRR(aRecords) {
  await trrServer.registerDoHAnswers(ECH_HOST, "HTTPS", {
    answers: aRecords.map(({ port, echConfig }, i) => ({
      name: ECH_HOST,
      ttl: 55,
      type: "HTTPS",
      flush: false,
      data: {
        priority: i + 1,
        name: ECH_HOST,
        values: [
          { key: "alpn", value: "http/1.1" },
          { key: "port", value: port },
          { key: "echconfig", value: echConfig, needBase64Decode: true },
        ],
      },
    })),
  });
}

function getClosedPort() {
  let socket = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  socket.init(-1, true, -1);
  let port = socket.port;
  socket.close();
  return port;
}

// Opens a WebSocket as a page would (a system principal on a non-document load
// disables HTTPS RR), sends a message and waits for the echo. Resolves with
// whether the echo arrived and whether the connection accepted ECH. The ECH
// state comes from the HTTP channel that performed the upgrade, as the
// WebSocket channel doesn't expose its connection's security info.
function openWebSocket() {
  let chan = Cc["@mozilla.org/network/protocol;1?name=wss"]
    .getService(Ci.nsIWebSocketProtocolHandler)
    .newWebSocketChannel();
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(`https://${ECH_HOST}`),
    {}
  );
  chan.initLoadInfo(
    null,
    principal,
    principal,
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    Ci.nsIContentPolicy.TYPE_WEBSOCKET
  );

  const msg = "hello over ECH";
  let url = `wss://${ECH_HOST}:${ECH_PORT}/`;
  let acceptedEch = false;
  let upgradeObserver = {
    observe(aSubject) {
      let httpChan = aSubject.QueryInterface(Ci.nsIHttpChannel);
      if (httpChan.URI.host == ECH_HOST && httpChan.responseStatus == 101) {
        acceptedEch = httpChan.securityInfo.isAcceptedEch;
      }
    },
  };
  Services.obs.addObserver(upgradeObserver, "http-on-examine-response");
  return new Promise(resolve => {
    let echoed = false;
    let onMessage = aMsg => {
      echoed = aMsg == msg;
      chan.close(Ci.nsIWebSocketChannel.CLOSE_NORMAL, "");
    };
    chan.asyncOpen(
      Services.io.newURI(url),
      url,
      {},
      0,
      {
        QueryInterface: ChromeUtils.generateQI(["nsIWebSocketListener"]),
        onAcknowledge() {},
        // The `ws` library echoes the data back as a binary frame.
        onBinaryMessageAvailable(aContext, aMsg) {
          onMessage(aMsg);
        },
        onMessageAvailable(aContext, aMsg) {
          onMessage(aMsg);
        },
        onServerClose() {},
        onStart() {
          chan.sendMsg(msg);
        },
        onStop(aContext, aStatusCode) {
          info(`WebSocket onStop status 0x${aStatusCode.toString(16)}`);
          Services.obs.removeObserver(
            upgradeObserver,
            "http-on-examine-response"
          );
          resolve({ echoed, acceptedEch });
        },
      },
      null
    );
  });
}

async function resetConnections() {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1000));
  Services.dns.clearCache(true);
}

function applyPrefs(aPrefs) {
  Services.prefs.clearUserPref("network.http.happy_eyeballs_upgrade_enabled");
  Services.prefs.clearUserPref("network.http.upgrade_use_https_rr");
  for (let [pref, value] of Object.entries(aPrefs)) {
    Services.prefs.setBoolPref(pref, value);
  }
}

async function checkWebSocket(aConfig, aExpectEch, aWhat) {
  applyPrefs(aConfig.prefs);
  await resetConnections();
  let ws = await openWebSocket();
  info(`${aConfig.name}: ${aWhat}: ${JSON.stringify(ws)}`);
  Assert.ok(ws.echoed, `${aConfig.name}: ${aWhat}: WebSocket echoed`);
  Assert.equal(
    ws.acceptedEch,
    aExpectEch,
    `${aConfig.name}: ${aWhat}: WebSocket accepted ECH`
  );
}

// network.http.upgrade_use_https_rr is left at its default unless a config
// sets it.
const HE_OFF = { "network.http.happy_eyeballs_enabled": false };
const HE_ON_NO_UPGRADE = {
  "network.http.happy_eyeballs_enabled": true,
  "network.http.happy_eyeballs_upgrade_enabled": false,
};
const HE_ON_UPGRADE = {
  "network.http.happy_eyeballs_enabled": true,
  "network.http.happy_eyeballs_upgrade_enabled": true,
};

const DEFAULT_CONFIGS = [
  { name: "HEv3 disabled", prefs: HE_OFF },
  { name: "HEv3 enabled, upgrades disabled", prefs: HE_ON_NO_UPGRADE },
  { name: "HEv3 enabled, upgrades enabled", prefs: HE_ON_UPGRADE },
];

add_task(async function test_upgrade_use_https_rr_default() {
  Assert.ok(
    Services.prefs.getBoolPref("network.http.upgrade_use_https_rr"),
    "upgrades use HTTPS RR by default"
  );
});

add_task(async function test_websocket_ech() {
  for (let config of DEFAULT_CONFIGS) {
    await checkWebSocket(config, true, "ECH");
  }
});

// Without Happy Eyeballs, upgrades only use HTTPS RR, and so ECH, when
// network.http.upgrade_use_https_rr is enabled.
add_task(async function test_websocket_ech_pref_disabled() {
  let configs = [
    { name: "HEv3 disabled", expectEch: false, prefs: HE_OFF },
    {
      name: "HEv3 enabled, upgrades disabled",
      expectEch: false,
      prefs: HE_ON_NO_UPGRADE,
    },
    {
      name: "HEv3 enabled, upgrades enabled",
      expectEch: true,
      prefs: HE_ON_UPGRADE,
    },
  ];
  for (let config of configs) {
    await checkWebSocket(
      {
        name: `${config.name}, upgrade_use_https_rr disabled`,
        prefs: { ...config.prefs, "network.http.upgrade_use_https_rr": false },
      },
      config.expectEch,
      "ECH"
    );
  }
});

// Without Happy Eyeballs, the HTTPS RR fallback restarts the transaction, which
// network.http.upgrade_use_https_rr allows for a sticky upgrade transaction.
add_task(async function test_websocket_ech_retry() {
  await setHTTPSRR([{ port: ECH_PORT, echConfig: ECH_CONFIG_TRUSTED_RETRY }]);
  for (let config of DEFAULT_CONFIGS) {
    await checkWebSocket(config, true, "ECH retry");
  }
  await setHTTPSRR([{ port: ECH_PORT, echConfig: ECH_CONFIG }]);
});

add_task(async function test_websocket_https_rr_next_record() {
  await setHTTPSRR([
    { port: getClosedPort(), echConfig: ECH_CONFIG },
    { port: ECH_PORT, echConfig: ECH_CONFIG },
  ]);
  for (let config of DEFAULT_CONFIGS) {
    await checkWebSocket(config, true, "next HTTPS RR record");
  }
  await setHTTPSRR([{ port: ECH_PORT, echConfig: ECH_CONFIG }]);
});
