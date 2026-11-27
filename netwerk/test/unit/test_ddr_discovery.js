/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests RFC 9462 DDR discovery: DoHDiscovery.discover() should query the
// native resolver for SVCB records at _dns.resolver.arpa and build a DoH URI
// from the highest-priority usable ServiceMode record's target, port and
// dohpath.

"use strict";

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);

const { DoHDiscovery } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/doh/DoHDiscovery.sys.mjs"
);

let trrServer;

function makeChan(url) {
  return NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
}

function channelOpenPromise(chan) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener((req, buffer) => resolve([req, buffer]))
    );
  });
}

function hexToUint8Array(hex) {
  return new Uint8Array(hex.match(/.{1,2}/g).map(byte => parseInt(byte, 16)));
}

add_setup(async function setup() {
  trr_test_setup();
  Services.prefs.setIntPref(
    "network.trr.mode",
    Ci.nsIDNSService.MODE_NATIVEONLY
  );

  trrServer = new TRRServer();
  await trrServer.start();

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    override.clearOverrides();
    await trrServer.stop();
  });
});

// Registers the given SVCB RDATA entries as the native answer for
// _dns.resolver.arpa.
async function registerResolverArpa(...records) {
  await trrServer.registerDoHAnswers("_dns.resolver.arpa", "SVCB", {
    answers: records.map(data => ({
      name: "_dns.resolver.arpa",
      ttl: 55,
      type: "SVCB",
      flush: false,
      data,
    })),
  });

  let chan = makeChan(
    `https://foo.example.com:${trrServer.port()}/dnsAnswer?host=_dns.resolver.arpa&type=SVCB`
  );
  let [, buf] = await channelOpenPromise(chan);
  let rawBuffer = hexToUint8Array(buf);
  override.clearOverrides();
  override.addHTTPSRecordOverride(
    "_dns.resolver.arpa",
    rawBuffer,
    rawBuffer.length
  );
}

function dohRecord(name, dohpath, priority = 1, port = undefined) {
  let values = [{ key: "alpn", value: ["h2"] }];
  if (port !== undefined) {
    values.push({ key: "port", value: port });
  }
  if (dohpath !== undefined) {
    values.push({ key: "dohpath", value: dohpath });
  }
  return { priority, name, values };
}

add_task(async function test_no_svcb_result() {
  // Overriding the host to "N/A" (rather than clearing overrides) keeps the
  // native override service active, so the SVCB lookup deterministically
  // returns "no record" instead of falling back to the real resolver.
  override.clearOverrides();
  override.addIPOverride("_dns.resolver.arpa", "N/A");

  let discovered = await DoHDiscovery.discover();
  Assert.equal(discovered, null, "no SVCB record means no designated resolver");

  override.clearOverrides();
});

add_task(async function test_discover_builds_uri() {
  await registerResolverArpa({
    priority: 1,
    name: "doh.example.net",
    values: [
      { key: "alpn", value: ["h2", "h3"] },
      { key: "port", value: 8443 },
      { key: "dohpath", value: "/dns-query{?dns}" },
    ],
  });

  let discovered = await DoHDiscovery.discover();
  Assert.ok(discovered, "a designated resolver was discovered");
  equal(
    discovered.uri,
    "https://doh.example.net:8443/dns-query",
    "DoH URI built from target, port and dohpath"
  );
});

add_task(async function test_default_port() {
  await registerResolverArpa(
    dohRecord("doh.example.net", "/dns-query{?dns}", 1, 443)
  );
  let discovered = await DoHDiscovery.discover();
  equal(
    discovered?.uri,
    "https://doh.example.net/dns-query",
    "default port is omitted"
  );
});

add_task(async function test_dohpath_with_query() {
  // Literal query parameters before a "{&dns}" expansion are kept; Necko
  // appends "&dns=" to them for GET requests.
  await registerResolverArpa(
    dohRecord("doh.example.net", "/dns-query?client=ff&v=1{&dns}")
  );
  let discovered = await DoHDiscovery.discover();
  equal(
    discovered?.uri,
    "https://doh.example.net/dns-query?client=ff&v=1",
    "literal query parameters are kept"
  );
});

add_task(async function test_dohpath_starting_with_empty_segment() {
  // An empty first path segment is a valid path, and must not be read as a
  // different authority.
  await registerResolverArpa(
    dohRecord("doh.example.net", "//evil.example/dns-query{?dns}")
  );
  let discovered = await DoHDiscovery.discover();
  equal(
    discovered?.uri,
    "https://doh.example.net//evil.example/dns-query",
    "the dohpath is used as the path on the TargetName"
  );
});

add_task(async function test_no_dohpath_is_ignored() {
  // No dohpath: not a usable DoH designated resolver.
  await registerResolverArpa(dohRecord("doh.example.net", undefined));

  let discovered = await DoHDiscovery.discover();
  Assert.equal(discovered, null, "record without dohpath is ignored");
});

add_task(async function test_invalid_dohpath_is_rejected() {
  const invalid = [
    // RFC 9461 requires a relative URI template starting with "/".
    "dns-query{?dns}",
    // Would change the authority of the resulting URI.
    "@evil.example/dns-query{?dns}",
    // No "dns" variable.
    "/dns-query",
    "/dns-query{?foo}",
    // Templates Necko can't express, since it appends "dns=" as the last query
    // parameter.
    "/q/{dns}",
    "/dns-query{?dns,foo}",
    "/a{?dns}/b",
    "/dns-query?x=1{&dns}&y=2",
    // Expansions that don't match whether there is a literal query.
    "/dns-query?x=1{?dns}",
    "/dns-query{&dns}",
    "/dns-query?{&dns}",
    "/dns-query?x={y}{&dns}",
  ];
  for (let dohpath of invalid) {
    await registerResolverArpa(dohRecord("doh.example.net", dohpath));
    let discovered = await DoHDiscovery.discover();
    Assert.equal(discovered, null, `dohpath ${dohpath} is rejected`);
  }
});

add_task(async function test_invalid_target_is_rejected() {
  const invalid = [
    // RFC 9460: "." in ServiceMode means the owner name, which here is
    // _dns.resolver.arpa itself and can't be a designated resolver.
    ".",
    "_dns.resolver.arpa",
    // Characters that would change how the URI is parsed.
    "evil.example/x.doh.example.net",
    "evil.example?x.doh.example.net",
  ];
  for (let target of invalid) {
    await registerResolverArpa(dohRecord(target, "/dns-query{?dns}"));
    let discovered = await DoHDiscovery.discover();
    Assert.equal(discovered, null, `target ${target} is rejected`);
  }
});

add_task(async function test_first_unusable_record_is_skipped() {
  await registerResolverArpa(
    // Higher priority, but its dohpath can't be used.
    dohRecord("bad.example.net", "/q/{dns}", 1),
    dohRecord("good.example.net", "/dns-query{?dns}", 2)
  );
  let discovered = await DoHDiscovery.discover();
  equal(
    discovered?.uri,
    "https://good.example.net/dns-query",
    "the first usable record is selected"
  );

  await registerResolverArpa(
    dohRecord(".", "/dns-query{?dns}", 1),
    dohRecord("good.example.net", "/dns-query{?dns}", 2)
  );
  discovered = await DoHDiscovery.discover();
  equal(
    discovered?.uri,
    "https://good.example.net/dns-query",
    "a record with an unusable target is skipped"
  );
});
