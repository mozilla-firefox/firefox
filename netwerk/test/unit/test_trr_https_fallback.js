/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { NodeWebSocketHttp2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

let h2Port;
let h3Port;
let h3NoResponsePort;
let trrServer;

const certOverrideService = Cc[
  "@mozilla.org/security/certoverride;1"
].getService(Ci.nsICertOverrideService);

add_setup(async function setup() {
  trr_test_setup();

  let h2Server = await create_h2_server({
    "/server-timing": (req, res) => {
      const body = "data reached";
      res.setHeader("X-Connection-Http2", "yes");
      res.setHeader("Content-Length", body.length);
      res.writeHead(200, { "Content-Type": "text/plain" });
      res.end(body);
    },
  });
  h2Port = h2Server.port();

  h3Port = Services.env.get("MOZHTTP3_PORT");
  Assert.notEqual(h3Port, null);
  Assert.notEqual(h3Port, "");

  h3NoResponsePort = Services.env.get("MOZHTTP3_PORT_NO_RESPONSE");
  Assert.notEqual(h3NoResponsePort, null);
  Assert.notEqual(h3NoResponsePort, "");

  Services.prefs.setBoolPref("network.http.happy_eyeballs_enabled", true);
  Services.prefs.setBoolPref("network.dns.upgrade_with_https_rr", true);
  Services.prefs.setBoolPref("network.dns.use_https_rr_as_altsvc", true);
  Services.prefs.setBoolPref("network.dns.echconfig.enabled", true);
  Services.prefs.setBoolPref(
    "network.dns.https_rr.check_record_with_cname",
    false
  );

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    Services.prefs.clearUserPref("network.dns.upgrade_with_https_rr");
    Services.prefs.clearUserPref("network.dns.use_https_rr_as_altsvc");
    Services.prefs.clearUserPref("network.dns.echconfig.enabled");
    Services.prefs.clearUserPref(
      "network.dns.echconfig.fallback_to_origin_when_all_failed"
    );
    Services.prefs.clearUserPref("network.dns.httpssvc.reset_exclustion_list");
    Services.prefs.clearUserPref("network.http.http3.enable");
    Services.prefs.clearUserPref(
      "network.dns.httpssvc.http3_fast_fallback_timeout"
    );
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.dns.localDomains");
    Services.prefs.clearUserPref("network.dns.http3_echconfig.enabled");
    Services.prefs.clearUserPref(
      "network.dns.https_rr.check_record_with_cname"
    );
    if (trrServer) {
      await trrServer.stop();
    }
  });

  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRFIRST);
});

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
      certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
        false
      );
    }
    certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
      true
    );
    chan.asyncOpen(new ChannelListener(finish, null, flags));
  });
}

// Test if we can fallback to the last record sucessfully.
add_task(async function testFallbackToTheLastRecord() {
  trrServer = new TRRServer();
  await trrServer.start();

  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  // Only the last record is valid to use.
  await trrServer.registerDoHAnswers(
    `_${h2Port}._https.test.fallback.com`,
    "HTTPS",
    {
      answers: [
        {
          name: `_${h2Port}._https.test.fallback.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 1,
            name: "test.fallback1.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "echconfig", value: "123..." },
            ],
          },
        },
        {
          name: `_${h2Port}._https.test.fallback.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 4,
            name: "foo.example.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "port", value: h2Port },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
        {
          name: `_${h2Port}._https.test.fallback.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 3,
            name: "test.fallback3.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
        {
          name: `_${h2Port}._https.test.fallback.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 2,
            name: "test.fallback2.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
      ],
    }
  );

  await new TRRDNSListener("test.fallback.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
    port: h2Port,
  });

  let chan = makeChan(`https://test.fallback.com:${h2Port}/server-timing`);
  let [req] = await channelOpenPromise(chan);
  // Test if this request is done by h2.
  Assert.equal(req.getResponseHeader("x-connection-http2"), "yes");

  await trrServer.stop();
});

add_task(async function testFallbackToTheOrigin() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setBoolPref(
    "network.dns.echconfig.fallback_to_origin_when_all_failed",
    true
  );
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  // All records are not able to use to connect, so we fallback to the origin
  // one.
  await trrServer.registerDoHAnswers("test.foo.com", "HTTPS", {
    answers: [
      {
        name: "test.foo.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.foo1.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "123..." },
          ],
        },
      },
      {
        name: "test.foo.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 3,
          name: "test.foo3.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
      {
        name: "test.foo.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 2,
          name: "test.foo2.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("test.foo.com", "A", {
    answers: [
      {
        name: "test.foo.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await new TRRDNSListener("test.foo.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://test.foo.com:${h2Port}/server-timing`);
  let [req] = await channelOpenPromise(chan);
  // Test if this request is done by h2.
  Assert.equal(req.getResponseHeader("x-connection-http2"), "yes");

  await trrServer.stop();
});

// Test when all records are failed and network.dns.echconfig.fallback_to_origin
// is false. In this case, the connection is always failed.
add_task(async function testAllRecordsFailed() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref(
    "network.dns.echconfig.fallback_to_origin_when_all_failed",
    false
  );

  await trrServer.registerDoHAnswers("test.bar.com", "HTTPS", {
    answers: [
      {
        name: "test.bar.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.bar1.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "123..." },
          ],
        },
      },
      {
        name: "test.bar.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 3,
          name: "test.bar3.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
      {
        name: "test.bar.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 2,
          name: "test.bar2.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await new TRRDNSListener("test.bar.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  // This channel should be failed.
  let chan = makeChan(`https://test.bar.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.stop();
});

// Test when all records have no echConfig, we directly fallback to the origin
// one.
add_task(async function testFallbackToTheOrigin2() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("test.example.com", "HTTPS", {
    answers: [
      {
        name: "test.example.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.example1.com",
          values: [{ key: "alpn", value: ["h2", "h3-26"] }],
        },
      },
      {
        name: "test.example.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 3,
          name: "test.example3.com",
          values: [{ key: "alpn", value: ["h2", "h3-26"] }],
        },
      },
    ],
  });

  await new TRRDNSListener("test.example.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://test.example.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.registerDoHAnswers("test.example.com", "A", {
    answers: [
      {
        name: "test.example.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  Services.dns.clearCache(true);

  chan = makeChan(`https://test.example.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan);

  await trrServer.stop();
});

// Test when some records have echConfig and some not, we directly fallback to
// the origin one.
add_task(async function testFallbackToTheOrigin3() {
  Services.dns.clearCache(true);

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("vulnerable.com", "A", {
    answers: [
      {
        name: "vulnerable.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await trrServer.registerDoHAnswers("vulnerable.com", "HTTPS", {
    answers: [
      {
        name: "vulnerable.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "vulnerable1.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
      {
        name: "vulnerable.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 2,
          name: "vulnerable2.com",
          values: [
            { key: "alpn", value: ["h2", "h3-26"] },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
      {
        name: "vulnerable.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 3,
          name: "vulnerable3.com",
          values: [{ key: "alpn", value: ["h2", "h3-26"] }],
        },
      },
    ],
  });

  await new TRRDNSListener("vulnerable.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://vulnerable.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan);

  await trrServer.stop();
});

add_task(async function testResetExclusionList() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref(
    "network.dns.httpssvc.reset_exclustion_list",
    false
  );

  await trrServer.registerDoHAnswers(
    `_${h2Port}._https.test.reset.com`,
    "HTTPS",
    {
      answers: [
        {
          name: `_${h2Port}._https.test.reset.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 1,
            name: "test.reset1.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "port", value: h2Port },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
        {
          name: `_${h2Port}._https.test.reset.com`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 2,
            name: "test.reset2.com",
            values: [
              { key: "alpn", value: ["h2", "h3-26"] },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
      ],
    }
  );

  await new TRRDNSListener("test.reset.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
    port: h2Port,
  });

  // After this request, test.reset1.com and test.reset2.com should be both in
  // the exclusion list.
  let chan = makeChan(`https://test.reset.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  // This request should be also failed, because all records are excluded.
  chan = makeChan(`https://test.reset.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.registerDoHAnswers("test.reset1.com", "A", {
    answers: [
      {
        name: "test.reset1.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  Services.prefs.setBoolPref(
    "network.dns.httpssvc.reset_exclustion_list",
    true
  );

  Services.dns.clearCache(true);

  // After enable network.dns.httpssvc.reset_exclustion_list and register
  // A record for test.reset1.com, this request should be succeeded.
  chan = makeChan(`https://test.reset.com:${h2Port}/server-timing`);
  await channelOpenPromise(chan);

  await trrServer.stop();
});

// Simply test if we can connect to H3 server.
add_task(async function testH3Connection() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);

  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    100
  );

  await trrServer.registerDoHAnswers("test.h3.com", "HTTPS", {
    answers: [
      {
        name: "test.h3.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "www.h3.com",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3Port },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("www.h3.com", "A", {
    answers: [
      {
        name: "www.h3.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await new TRRDNSListener("test.h3.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://test.h3.com`);
  let [req] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h3");
  let internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(internal.remotePort, h3Port);

  await trrServer.stop();
});

add_task(async function testFastfallbackToH2() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  // Use a short timeout to make sure the fast fallback timer will be triggered.
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    1
  );
  Services.prefs.setCharPref(
    "network.dns.localDomains",
    "test.fastfallback1.com"
  );

  await trrServer.registerDoHAnswers("test.fastfallback.com", "HTTPS", {
    answers: [
      {
        name: "test.fastfallback.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.fastfallback1.com",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3NoResponsePort },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
      {
        name: "test.fastfallback.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 2,
          name: "test.fastfallback2.com",
          values: [
            { key: "alpn", value: "h2" },
            { key: "port", value: h2Port },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("test.fastfallback2.com", "A", {
    answers: [
      {
        name: "test.fastfallback2.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await new TRRDNSListener("test.fastfallback.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://test.fastfallback.com/server-timing`);
  let [req] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h2");
  let internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(internal.remotePort, h2Port);

  // Use a longer timeout to test the case that the timer is canceled.
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    5000
  );

  chan = makeChan(`https://test.fastfallback.com/server-timing`);
  [req] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h2");
  internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(internal.remotePort, h2Port);

  await trrServer.stop();
});

// Test when we fail to establish H3 connection.
add_task(async function testFailedH3Connection() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    0
  );

  await trrServer.registerDoHAnswers("test.h3.org", "HTTPS", {
    answers: [
      {
        name: "test.h3.org",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "www.h3.org",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3Port },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await new TRRDNSListener("test.h3.org", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  let chan = makeChan(`https://test.h3.org`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.stop();
});

// Test we don't use the service mode record whose domain is in
// http3 excluded list.
add_task(async function testHttp3ExcludedList() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    0
  );

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    "www.h3_fail.org;h3=:" + h3Port
  );

  // This will fail because there is no address record for www.h3_fail.org.
  let chan = makeChan(`https://www.h3_fail.org`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  // Now www.h3_fail.org should be already excluded, so the second record
  // foo.example.com will be selected.
  await trrServer.registerDoHAnswers("test.h3_excluded.org", "HTTPS", {
    answers: [
      {
        name: "test.h3_excluded.org",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "www.h3_fail.org",
          values: [
            { key: "alpn", value: "h3" },
            { key: "no-default-alpn" },
            { key: "port", value: h3Port },
          ],
        },
      },
      {
        name: "test.h3_excluded.org",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 2,
          name: "foo.example.com",
          values: [
            { key: "alpn", value: "h3" },
            { key: "port", value: h3Port },
          ],
        },
      },
    ],
  });

  await new TRRDNSListener("test.h3_excluded.org", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  chan = makeChan(`https://test.h3_excluded.org`);
  let [req] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h3");
  let internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(internal.remotePort, h3Port);

  await trrServer.stop();
});

add_task(async function testAllRecordsInHttp3ExcludedList() {
  // This test exercises the legacy (non-Happy-Eyeballs-v3) behavior where a
  // request is not retried against the origin when all HTTPS RRs are in the
  // HTTP/3 excluded list. HE-v3 handles this case differently and the load
  // succeeds, so the test only applies to the legacy path.
  if (
    Services.prefs.getBoolPref("network.http.happy_eyeballs_enabled", false)
  ) {
    info("Skipping testAllRecordsInHttp3ExcludedList: not applicable to HE-v3");
    return;
  }

  trrServer = new TRRServer();
  await trrServer.start();
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setBoolPref("network.dns.http3_echconfig.enabled", true);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    0
  );

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    "_${h2Port}._https.www.h3_fail1.org;h3=:" + h3Port
  );

  await trrServer.registerDoHAnswers(`www.h3_all_excluded.org`, "A", {
    answers: [
      {
        name: `www.h3_all_excluded.org`,
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  // Test we can connect to www.h3_all_excluded.org sucessfully.
  let chan = makeChan(
    `https://www.h3_all_excluded.org:${h2Port}/server-timing`
  );

  let [req] = await channelOpenPromise(chan);

  // Test if this request is done by h2.
  Assert.equal(req.getResponseHeader("x-connection-http2"), "yes");

  // This will fail because there is no address record for www.h3_fail1.org.
  chan = makeChan(`https://www.h3_fail1.org`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  Services.prefs.setCharPref(
    "network.http.http3.alt-svc-mapping-for-testing",
    "_${h2Port}._https.www.h3_fail2.org;h3=:" + h3Port
  );

  // This will fail because there is no address record for www.h3_fail2.org.
  chan = makeChan(`https://www.h3_fail2.org`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.registerDoHAnswers(
    `_${h2Port}._https.www.h3_all_excluded.org`,
    "HTTPS",
    {
      answers: [
        {
          name: `_${h2Port}._https.www.h3_all_excluded.org`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 1,
            name: "www.h3_fail1.org",
            values: [
              { key: "alpn", value: "h3" },
              { key: "no-default-alpn" },
              { key: "port", value: h3Port },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
        {
          name: `_${h2Port}._https.www.h3_all_excluded.org`,
          ttl: 55,
          type: "HTTPS",
          flush: false,
          data: {
            priority: 2,
            name: "www.h3_fail2.org",
            values: [
              { key: "alpn", value: "h3" },
              { key: "port", value: h3Port },
              { key: "echconfig", value: "456..." },
            ],
          },
        },
      ],
    }
  );

  await new TRRDNSListener("www.h3_all_excluded.org", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
    port: h2Port,
  });

  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);
  Services.obs.notifyObservers(null, "net:prune-all-connections");

  // All HTTPS RRs are in http3 excluded list and all records are failed to
  // connect, so don't fallback to the origin one.
  chan = makeChan(`https://www.h3_all_excluded.org:${h2Port}/server-timing`);
  await channelOpenPromise(chan, CL_EXPECT_LATE_FAILURE | CL_ALLOW_UNKNOWN_CL);

  await trrServer.registerDoHAnswers("www.h3_fail1.org", "A", {
    answers: [
      {
        name: "www.h3_fail1.org",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  // The the case that when all records are in http3 excluded list, we still
  // give the first record one more shot.
  chan = makeChan(`https://www.h3_all_excluded.org:${h2Port}`);
  [req] = await channelOpenPromise(chan);
  Assert.equal(req.protocolVersion, "h3");
  let internal = req.QueryInterface(Ci.nsIHttpChannelInternal);
  Assert.equal(internal.remotePort, h3Port);

  await trrServer.stop();
});

WebSocketListener.prototype = {
  onAcknowledge() {},
  onBinaryMessageAvailable() {},
  onMessageAvailable() {},
  onServerClose() {},
  onStart() {
    this.finish();
  },
  onStop() {},
};

add_task(async function testUpgradeNotUsingHTTPSRR() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );

  await trrServer.registerDoHAnswers("test.ws.com", "HTTPS", {
    answers: [
      {
        name: "test.ws.com",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.ws1.com",
          values: [{ key: "port", value: ["8888"] }],
        },
      },
    ],
  });

  await new TRRDNSListener("test.ws.com", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  await trrServer.registerDoHAnswers("test.ws.com", "A", {
    answers: [
      {
        name: "test.ws.com",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  let wsServer = new NodeWebSocketHttp2Server();
  await wsServer.start();
  registerCleanupFunction(async () => {
    await wsServer.stop();
  });
  let wssUri = "wss://test.ws.com:" + wsServer.port() + "/websocket";
  let chan = Cc["@mozilla.org/network/protocol;1?name=wss"]
    .getService(Ci.nsIWebSocketProtocolHandler)
    .newWebSocketChannel();
  chan.initLoadInfo(
    null, // aLoadingNode
    Services.scriptSecurityManager.getSystemPrincipal(),
    null, // aTriggeringPrincipal
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    Ci.nsIContentPolicy.TYPE_DOCUMENT
  );

  var uri = Services.io.newURI(wssUri);
  var wsListener = new WebSocketListener();
  certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
    false
  );
  await new Promise(resolve => {
    wsListener.finish = resolve;
    chan.asyncOpen(uri, wssUri, {}, 0, wsListener, null);
    certOverrideService.setDisableAllSecurityChecksAndLetAttackersInterceptMyData(
      true
    );
  });

  await trrServer.stop();
});

// Test if we fallback to h2 with echConfig.
add_task(async function testFallbackToH2WithEchConfig() {
  trrServer = new TRRServer();
  await trrServer.start();
  Services.dns.clearCache(true);
  Services.prefs.setIntPref("network.trr.mode", 3);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  Services.prefs.setBoolPref("network.http.http3.enable", true);
  Services.prefs.setIntPref(
    "network.dns.httpssvc.http3_fast_fallback_timeout",
    0
  );

  await trrServer.registerDoHAnswers("test.fallback.org", "HTTPS", {
    answers: [
      {
        name: "test.fallback.org",
        ttl: 55,
        type: "HTTPS",
        flush: false,
        data: {
          priority: 1,
          name: "test.fallback.org",
          values: [
            { key: "alpn", value: ["h2", "h3"] },
            { key: "port", value: h2Port },
            { key: "echconfig", value: "456..." },
          ],
        },
      },
    ],
  });

  await trrServer.registerDoHAnswers("test.fallback.org", "A", {
    answers: [
      {
        name: "test.fallback.org",
        ttl: 55,
        type: "A",
        flush: false,
        data: "127.0.0.1",
      },
    ],
  });

  await new TRRDNSListener("test.fallback.org", {
    type: Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC,
  });

  await new TRRDNSListener("test.fallback.org", "127.0.0.1");

  let chan = makeChan(`https://test.fallback.org/server-timing`);
  let [req] = await channelOpenPromise(chan);
  // Test if this request is done by h2.
  Assert.equal(req.getResponseHeader("x-connection-http2"), "yes");

  await trrServer.stop();
});
