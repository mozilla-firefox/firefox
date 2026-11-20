/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

/* import-globals-from head_cache.js */
/* import-globals-from head_cookies.js */
/* import-globals-from head_channels.js */

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
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

let h2Port;
let h3Port;
let trrServer;

add_setup(async function setup() {
  await http3_setup_tests("h3", true);
  Services.prefs.setBoolPref(
    "network.http.http3.disable_when_third_party_roots_found",
    true
  );

  registerCleanupFunction(async () => {
    http3_clear_prefs();

    Services.prefs.clearUserPref(
      "network.http.http3.disable_when_third_party_roots_found"
    );
    Services.prefs.clearUserPref(
      "network.http.http3.has_third_party_roots_found_in_automation"
    );
    Services.obs.notifyObservers(null, "network:reset_third_party_roots_check");

    if (trrServer) {
      await trrServer.stop();
    }
  });

  h2Port = (await create_h2_server()).port();

  h3Port = Services.env.get("MOZHTTP3_PORT");
});

async function do_test(host, expectedVersion) {
  let nssComponent = Cc["@mozilla.org/network/ssl-tokens-cache;1"].getService(
    Ci.nsISSLTokensCache
  );
  await nssComponent.asyncClearSSLExternalAndInternalSessionCache();

  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 3000));
  Services.obs.notifyObservers(null, "network:reset-http3-excluded-list");

  let chan = makeChan(`https://${host}:${h2Port}/`);
  let [req] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.protocolVersion, expectedVersion);
}

add_task(async function test_http3_with_third_party_roots() {
  Services.prefs.setBoolPref(
    "network.http.http3.has_third_party_roots_found_in_automation",
    true
  );

  await do_test("foo.example.com", "h2");
});

add_task(async function test_http3_with_no_third_party_roots() {
  Services.prefs.setBoolPref(
    "network.http.http3.has_third_party_roots_found_in_automation",
    false
  );

  await do_test("foo.example.com", "h3");
});

async function setup_trr_server() {
  http3_clear_prefs();

  Services.prefs.setBoolPref("network.dns.port_prefixed_qname_https_rr", false);
  trr_test_setup();

  trrServer = new TRRServer();
  await trrServer.start();

  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("alt1.example.com", "HTTPS", {
    answers: [
      {
        name: "alt1.example.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "alt1.example.com",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3Port },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("alt1.example.com", "A", {
    answers: [
      {
        name: "alt1.example.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  let { inStatus } = await new TRRDNSListener("alt1.example.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });
  Assert.ok(Components.isSuccessCode(inStatus), `${inStatus} should work`);
}

// Similar to the previous test, but the difference is that we test this with
// HTTPS RR.
add_task(async function test_http3_with_third_party_roots_1() {
  await setup_trr_server();

  Services.prefs.setBoolPref(
    "network.http.http3.has_third_party_roots_found_in_automation",
    true
  );

  await do_test("alt1.example.com", "h2");
});

add_task(async function test_http3_with_no_third_party_roots_1() {
  Services.prefs.setBoolPref(
    "network.http.http3.has_third_party_roots_found_in_automation",
    false
  );

  await do_test("alt1.example.com", "h3");
});
