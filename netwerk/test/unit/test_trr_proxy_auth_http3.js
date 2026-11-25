/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { HTTP3Server, Http3ProxyFilter } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */
/* import-globals-from head_http3.js */

trr_test_setup();
registerCleanupFunction(async () => {
  trr_clear_prefs();
  Services.prefs.clearUserPref("network.http.http3.enable");
  Services.prefs.clearUserPref("network.proxy.allow_hijacking_localhost");
  Services.prefs.clearUserPref("network.dns.localDomains");
});

// URIs of the channels credentials were requested for.
let promptedURIs = [];

function AuthPrompt() {}

AuthPrompt.prototype = {
  user: "guest",
  pass: "guest",

  QueryInterface: ChromeUtils.generateQI(["nsIAuthPrompt2"]),

  promptAuth: function ap_promptAuth(channel, level, authInfo) {
    promptedURIs.push(channel.URI.spec);
    authInfo.username = this.user;
    authInfo.password = this.pass;

    return true;
  },

  asyncPromptAuth: function ap_async() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },
};

function Requestor() {}

Requestor.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIInterfaceRequestor"]),

  getInterface: function requestor_gi(iid) {
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      if (!this.prompt) {
        this.prompt = new AuthPrompt();
      }
      return this.prompt;
    }

    throw Components.Exception("", Cr.NS_ERROR_NO_INTERFACE);
  },

  prompt: null,
};

let trrServer;

function dohURI() {
  return `https://foo.example.com:${trrServer.port()}/dns-query`;
}

add_setup(async function setup() {
  trrServer = new TRRServer();
  await trrServer.start();
  registerCleanupFunction(async () => {
    await trrServer.stop();
  });
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref("network.trr.uri", dohURI());

  await trrServer.registerDoHAnswers("test.proxy.com", "A", {
    answers: [
      {
        name: "test.proxy.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "3.3.3.3",
      },
    ],
  });

  await new TRRDNSListener("test.proxy.com", "3.3.3.3");

  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setBoolPref("network.proxy.allow_hijacking_localhost", true);
  Services.prefs.setCharPref("network.dns.localDomains", "alt1.example.com");
});

// Routes all requests through a new HTTP/3 proxy until the returned function
// is called.
async function useHttp3Proxy(env) {
  let proxy = new HTTP3Server();
  await proxy.start(
    Services.env.get("MOZ_HTTP3_SERVER_PATH"),
    Services.env.get("MOZ_HTTP3_CERT_DB_PATH"),
    env
  );
  let pps = Cc["@mozilla.org/network/protocol-proxy-service;1"].getService();
  let proxyFilter = new Http3ProxyFilter(
    "alt1.example.com",
    proxy.masque_proxy_port(),
    0,
    "/.well-known/masque/udp/{target_host}/{target_port}/",
    ""
  );
  pps.registerFilter(proxyFilter, 10);
  return async () => {
    pps.unregisterFilter(proxyFilter);
    await proxy.stop();
  };
}

// TRR requests made on the socket thread should work through a CONNECT tunnel
// over an HTTP/3 proxy.
add_task(async function test_trr_http3_proxy_no_auth() {
  let stopProxy = await useHttp3Proxy();

  Services.dns.clearCache(true);
  await new TRRDNSListener("test.proxy.com", "3.3.3.3");
  Assert.deepEqual(promptedURIs, [], "No credentials should be requested");

  await stopProxy();
});

// Same as test_trr_proxy_auth.js, but the CONNECT tunnel to the TRR server
// goes through an HTTP/3 proxy that requires authentication.
add_task(async function test_trr_proxy_auth_http3() {
  let stopProxy = await useHttp3Proxy({ MOZ_HTTP3_PROXY_REQUIRE_AUTH: "1" });

  let observer = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
    observe(aSubject, aTopic) {
      if (aTopic == "http-on-examine-response") {
        Services.obs.removeObserver(observer, "http-on-examine-response");
        let channel = aSubject.QueryInterface(Ci.nsIChannel);
        channel.notificationCallbacks = new Requestor();
      }
    },
  };
  Services.obs.addObserver(observer, "http-on-examine-response");

  Services.dns.clearCache(true);
  await new TRRDNSListener("test.proxy.com", "3.3.3.3");
  Assert.ok(
    promptedURIs.some(spec => spec.startsWith(dohURI())),
    `Proxy credentials should be requested for the DoH channel: ${promptedURIs}`
  );

  await stopProxy();
});
