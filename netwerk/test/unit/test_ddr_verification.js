/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests RFC 9462 DDR verification: DoHDiscovery.verify() must only accept a
// designated resolver whose TLS certificate contains one of the unencrypted
// resolver IPs in a subjectAltName and that answers a test query.

"use strict";

const { DoHDiscovery } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/doh/DoHDiscovery.sys.mjs"
);

let trrServer;
let dohUrl;

async function checkVerify(aURI, aResolverAddrs, aExpected) {
  let verified = await DoHDiscovery.verify({
    uri: aURI,
    resolverAddrs: aResolverAddrs,
  });
  Assert.equal(verified, aExpected, `verify(${aURI}, ${aResolverAddrs})`);
}

const MODES = [Ci.nsIDNSService.MODE_NATIVEONLY, Ci.nsIDNSService.MODE_TRRONLY];

add_setup(async function setup() {
  trr_test_setup();

  trrServer = new TRRServer();
  // Mint a leaf cert (signed by the test CA) that covers the DoH hostname as a
  // DNS SAN and 127.0.0.1 as an iPAddress SAN.
  await trrServer.installCert(["foo.example.com", "127.0.0.1"]);
  await trrServer.start();

  Services.prefs.setCharPref("network.trr.confirmationNS", "skip");

  dohUrl = `https://foo.example.com:${trrServer.port()}/doh?responseIP=3.3.3.3`;

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    await trrServer.stop();
  });
});

add_task(async function test_matching_ip_is_accepted() {
  for (let mode of MODES) {
    Services.prefs.setIntPref("network.trr.mode", mode);
    await checkVerify(dohUrl, ["127.0.0.1"], true);
  }
});

add_task(async function test_any_matching_ip_is_accepted() {
  // Any of the configured resolver IPs matching the cert is enough: the SVCB
  // query may have been answered by any of them.
  await checkVerify(dohUrl, ["1.2.3.4", "127.0.0.1"], true);
});

add_task(async function test_mismatching_ip_is_rejected() {
  for (let mode of MODES) {
    Services.prefs.setIntPref("network.trr.mode", mode);
    await checkVerify(dohUrl, ["1.2.3.4", "5.6.7.8"], false);
  }
});

add_task(async function test_empty_resolver_addrs_is_rejected() {
  // Without the unencrypted resolver addresses the designated resolver can't
  // be verified, so it must be rejected without making a query.
  await checkVerify(dohUrl, [], false);
});

add_task(async function test_http_error() {
  // The certificate matches but the server answers with an HTTP error.
  await trrServer.registerPathHandler("/doh-error", (req, res) => {
    res.setHeader("Content-Type", "application/dns-message");
    res.writeHead(500);
    res.end("");
  });
  await checkVerify(
    `https://foo.example.com:${trrServer.port()}/doh-error`,
    ["127.0.0.1"],
    false
  );
});

add_task(async function test_redirect_is_not_followed() {
  // The certificate check must only cover the designated resolver itself, so a
  // redirect to another server is rejected without being followed.
  await trrServer.registerPathHandler("/doh-redirect", (req, res) => {
    res.writeHead(302, { Location: "/doh-redirect-target" });
    res.end("");
  });
  await trrServer.registerPathHandler("/doh-redirect-target", (req, res) => {
    global.redirectTargetHits = (global.redirectTargetHits || 0) + 1;
    res.writeHead(404);
    res.end("");
  });
  await checkVerify(
    `https://foo.example.com:${trrServer.port()}/doh-redirect`,
    ["127.0.0.1"],
    false
  );
  Assert.equal(
    await trrServer.execute("global.redirectTargetHits || 0"),
    0,
    "the redirect target was not requested"
  );
});

add_task(async function test_not_a_doh_endpoint() {
  // The certificate matches but the server isn't a DoH server.
  await checkVerify(
    `https://foo.example.com:${trrServer.port()}/not-a-doh-endpoint`,
    ["127.0.0.1"],
    false
  );
});

add_task(async function test_no_answers() {
  // A resolver that filters the test domain still proves the certificate
  // matched and that it answers DNS queries.
  await trrServer.registerDoHAnswers("firefox.com", "A", { answers: [] });
  await checkVerify(
    `https://foo.example.com:${trrServer.port()}/dns-query`,
    ["127.0.0.1"],
    true
  );
});

add_task(async function test_no_connection() {
  // Nothing listens on the port of a server that was stopped.
  let stopped = new TRRServer();
  await stopped.start();
  let port = stopped.port();
  await stopped.stop();
  await checkVerify(
    `https://foo.example.com:${port}/dns-query`,
    ["127.0.0.1"],
    false
  );
});

add_task(async function test_blocked_port() {
  // Necko refuses to connect to port 1 at all.
  await checkVerify(
    "https://foo.example.com:1/dns-query",
    ["127.0.0.1"],
    false
  );
});
