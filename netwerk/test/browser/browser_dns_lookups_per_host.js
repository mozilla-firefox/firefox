/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Loading a page should resolve each host it uses only once per record type,
// whatever made the lookup: the document load, a speculative connection, an
// HTTPS record prefetch or a subresource load (bug 2074097).
// test_dns_lookups_per_host.js checks the same with DoH.

"use strict";

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

// Each test uses its own hosts, so that no lookup is served from the cache.
// The label changes on every run, as the test can run repeatedly in the same
// browser.
const RUN_LABEL = Math.random().toString(36).slice(2, 10);
const HOSTS = {
  he: {
    page: `page.he.${RUN_LABEL}.dnscount.test`,
    subresource: `sub.he.${RUN_LABEL}.dnscount.test`,
  },
  noHe: {
    page: `page.nohe.${RUN_LABEL}.dnscount.test`,
    subresource: `sub.nohe.${RUN_LABEL}.dnscount.test`,
  },
};
const HOST_COUNT = 2;
const FAMILIES = ["ipv4", "ipv6", "unspec", "https"];

let server;
let override;

add_setup(async function () {
  override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
    Ci.nsINativeDNSResolverOverride
  );
  let hostnames = Object.values(HOSTS).flatMap(h => [h.page, h.subresource]);
  for (let host of hostnames) {
    override.addIPOverride(host, "127.0.0.1");
  }

  server = new NodeHTTP2Server();
  await server.start(0, hostnames);
  // The page loads subresources from its own host and from the host in the
  // query string.
  await server.registerPathHandler("/", (req, res) => {
    let subHost = new URL(req.url, "https://localhost").searchParams.get("sub");
    let sub = `https://${subHost}:${req.socket.localPort}`;
    res.setHeader("Content-Type", "text/html");
    res.end(`<!DOCTYPE html>
      <img src="${sub}/image">
      <script src="${sub}/script"></script>
      <img src="/image">`);
  });
  await server.registerPathHandler("/image", (req, res) => {
    res.setHeader("Content-Type", "image/png");
    res.end();
  });
  await server.registerPathHandler("/script", (req, res) => {
    res.setHeader("Content-Type", "text/javascript");
    res.end("");
  });

  // Use the native resolver, also in the DoH variant of the test harness, and
  // no proxy: with the harness's PAC proxy the channel doesn't prefetch DNS.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.trr.mode", Ci.nsIDNSService.MODE_TRROFF],
      ["network.proxy.type", 0],
    ],
  });

  registerCleanupFunction(async () => {
    override.clearOverrides();
    await server.stop();
  });
});

// The number of lookups that went to the network, per record family.
async function lookupCounts() {
  await Services.fog.testFlushAllChildren();
  let counts = {};
  for (let family of FAMILIES) {
    counts[family] =
      Glean.dns.cacheMissReason.get(family, "absent").testGetValue() ?? 0;
  }
  return counts;
}

async function loadPage(happyEyeballs, { page, subresource }) {
  await SpecialPowers.pushPrefEnv({
    set: [["network.http.happy_eyeballs_enabled", happyEyeballs]],
  });
  // Compare with the counts before the load rather than resetting Glean, as
  // Services.fog.testResetFOG() leaks a thread in browser tests.
  let before = await lookupCounts();

  await BrowserTestUtils.withNewTab(
    `https://${page}:${server.port()}/?sub=${subresource}`,
    async () => {}
  );

  let after = await lookupCounts();
  await SpecialPowers.popPrefEnv();
  let counts = {};
  for (let family of FAMILIES) {
    counts[family] = after[family] - before[family];
  }
  return counts;
}

// Each host is looked up once for its addresses: with Happy Eyeballs, once per
// address family. An HTTPS record lookup isn't made for every load, so only
// check that a host isn't looked up for one twice.
function checkCounts(counts, expected) {
  info(`lookups: ${JSON.stringify(counts)}`);
  for (let family of ["ipv4", "ipv6", "unspec"]) {
    Assert.equal(counts[family], expected[family], `${family} lookups`);
  }
  Assert.lessOrEqual(counts.https, HOST_COUNT, "HTTPS record lookups");
}

add_task(async function test_happy_eyeballs_enabled() {
  checkCounts(await loadPage(true, HOSTS.he), {
    ipv4: HOST_COUNT,
    ipv6: HOST_COUNT,
    unspec: 0,
  });
});

add_task(async function test_happy_eyeballs_disabled() {
  checkCounts(await loadPage(false, HOSTS.noHe), {
    ipv4: 0,
    ipv6: 0,
    unspec: HOST_COUNT,
  });
});
