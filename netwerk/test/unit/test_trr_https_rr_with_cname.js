/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

let h2Port;
let h3Port;
let h3EchConfig;

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

add_setup(async function setup() {
  Services.prefs.setBoolPref("network.dns.port_prefixed_qname_https_rr", false);

  // Happy Eyeballs does not support check CNAME for now.
  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", false);

  trr_test_setup();
  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.dns.port_prefixed_qname_https_rr");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
  });

  h2Port = (await create_h2_server()).port();

  h3Port = Services.env.get("MOZHTTP3_PORT_ECH");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  h3EchConfig = Services.env.get("MOZHTTP3_ECH");
  Assert.notEqual(h3EchConfig, null);
  Assert.notEqual(h3EchConfig, "");
});

async function do_test_https_rr_records(
  host,
  targetName1,
  port1,
  targetName2,
  port2,
  cname,
  expectedVersion
) {
  Services.obs.notifyObservers(null, "net:cancel-all-connections");
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 1000));

  Services.dns.clearCache(true);

  let trrServer = new TRRServer();
  registerCleanupFunction(async () => {
    await trrServer.stop();
  });
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers(host, "HTTPS", {
    answers: [
      {
        name: host,
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: targetName1,
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: port1 },
            {
              key: "echconfig",
              value: h3EchConfig,
              needBase64Decode: true,
            },
          ],
        },
      },
      {
        name: host,
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: targetName2,
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: port2 },
            {
              key: "echconfig",
              value: h3EchConfig,
              needBase64Decode: true,
            },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers(host, "A", {
    answers: [
      {
        name: host,
        ttl: 55,
        type: "CNAME",
        flush: false,
        data: cname,
      },
      {
        name: cname,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await trrServer.registerDoHAnswers(cname, "A", {
    answers: [
      {
        name: cname,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  let { inRecord } = await new TRRDNSListener(host, {
    expectedAnswer: "127.0.0.1",
    flags: Ci.nsIDNSService.RESOLVE_CANONICAL_NAME,
  });
  equal(inRecord.QueryInterface(Ci.nsIDNSAddrRecord).canonicalName, cname);

  let chan = makeChan(`https://${host}:${h2Port}/`);
  let [req] = await channelOpenPromise(chan, CL_ALLOW_UNKNOWN_CL);
  Assert.equal(req.protocolVersion, expectedVersion);
  await trrServer.stop();
}

// Test the case that the pref is off and the cname is not the same as the
// targetName. The expected protocol version being "h3" means that the last
// svcb record is used.
add_task(async function test_https_rr_with_unmatched_cname() {
  Services.prefs.setBoolPref(
    "network.dns.https_rr.check_record_with_cname",
    false
  );
  await do_test_https_rr_records(
    "alt1.example.com",
    "alt1.example.com",
    h3Port,
    "not_used",
    h3Port,
    "test.cname1.com",
    "h3"
  );
});

// Test the case that the pref is on and the cname is not the same as the
// targetName. Since there is no svcb record can be used, we fallback to "h2".
add_task(async function test_https_rr_with_unmatched_cname_1() {
  Services.prefs.setBoolPref(
    "network.dns.https_rr.check_record_with_cname",
    true
  );
  await do_test_https_rr_records(
    "alt1.example.com",
    "alt1.example.com",
    h3Port,
    "not_used",
    h3Port,
    "test.cname1.com",
    "h2"
  );
});

// Test the case that the pref is on and the cname is matched. We failed to
// connect to the first record, but we successfully connect with the second one.
add_task(async function test_https_rr_with_matched_cname() {
  Services.prefs.setBoolPref(
    "network.dns.https_rr.check_record_with_cname",
    true
  );
  await do_test_https_rr_records(
    "alt1.example.com",
    "not_used",
    h3Port,
    "alt2.example.com",
    h3Port,
    "alt2.example.com",
    "h3"
  );
});

// Test the case that the pref is on and both records are failed to connect.
// We can only fallback to "h2" when another pref is on.
add_task(async function test_https_rr_with_matched_cname_1() {
  Services.prefs.setBoolPref(
    "network.dns.echconfig.fallback_to_origin_when_all_failed",
    true
  );
  await do_test_https_rr_records(
    "alt1.example.com",
    "not_used",
    h3Port,
    "alt2.example.com",
    h2Port,
    "alt2.example.com",
    "h2"
  );
});
