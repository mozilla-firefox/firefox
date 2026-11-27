/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests native decoding of SVCB (type 64) records, including the RFC 9461
// dohpath SvcParam used by RFC 9462 DDR.

"use strict";

const override = Cc["@mozilla.org/network/native-dns-override;1"].getService(
  Ci.nsINativeDNSResolverOverride
);
const defaultOriginAttributes = {};
const mainThread = Services.tm.currentThread;

class Listener {
  constructor() {
    this.promise = new Promise(resolve => {
      this.resolve = resolve;
    });
  }
  onLookupComplete(inRequest, inRecord, inStatus) {
    this.resolve([inRequest, inRecord, inStatus]);
  }
  then() {
    return this.promise.then.apply(this.promise, arguments);
  }
}
Listener.prototype.QueryInterface = ChromeUtils.generateQI(["nsIDNSListener"]);

function makeChan(url) {
  return NetUtil.newChannel({
    uri: url,
    loadUsingSystemPrincipal: true,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
  }).QueryInterface(Ci.nsIHttpChannel);
}

function channelOpenPromise(chan, flags) {
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener((req, buffer) => resolve([req, buffer]), null, flags)
    );
  });
}

function hexToUint8Array(hex) {
  return new Uint8Array(hex.match(/.{1,2}/g).map(byte => parseInt(byte, 16)));
}

add_setup(async function setup() {
  Services.fog.initializeFOG();
  trr_test_setup();
  // Force native resolution.
  Services.prefs.setIntPref(
    "network.trr.mode",
    Ci.nsIDNSService.MODE_NATIVEONLY
  );

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    override.clearOverrides();
  });
});

add_task(async function test_svcb_dohpath_decode() {
  let trrServer = new TRRServer();
  await trrServer.start();
  registerCleanupFunction(async () => {
    await trrServer.stop();
  });

  await trrServer.registerDoHAnswers("_dns.resolver.arpa", "SVCB", {
    answers: [
      {
        name: "_dns.resolver.arpa",
        ttl: 55,
        type: "SVCB",
        flush: false,
        data: {
          priority: 1,
          name: "doh.example.net",
          // SvcParamKeys must be in strictly increasing numeric order:
          // alpn(1), port(3), ipv4hint(4), dohpath(7).
          values: [
            { key: "alpn", value: ["h2", "h3"] },
            { key: "port", value: 8443 },
            { key: "ipv4hint", value: "1.2.3.4" },
            { key: "dohpath", value: "/dns-query{?dns}" },
          ],
        },
      },
    ],
  });

  let chan = makeChan(
    `https://foo.example.com:${trrServer.port()}/dnsAnswer?host=_dns.resolver.arpa&type=SVCB`
  );
  let [, buf] = await channelOpenPromise(chan);
  let rawBuffer = hexToUint8Array(buf);

  override.addHTTPSRecordOverride(
    "_dns.resolver.arpa",
    rawBuffer,
    rawBuffer.length
  );

  let httpsCount0 =
    Glean.networking.dnsNativeCount.https_regular.testGetValue() ?? 0;
  let nativeLookups0 =
    Glean.dns.httpsRrLookupTime.native.testGetValue()?.count ?? 0;

  let listener = new Listener();
  Services.dns.asyncResolve(
    "_dns.resolver.arpa",
    Ci.nsIDNSService.RESOLVE_TYPE_SVCB,
    0,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );

  let [, inRecord, inStatus] = await listener;
  equal(inStatus, Cr.NS_OK, "SVCB lookup succeeded");
  equal(
    inRecord.QueryInterface(Ci.nsIDNSByTypeRecord).type,
    Ci.nsIDNSService.RESOLVE_TYPE_SVCB,
    "the record reports the SVCB type"
  );

  // SVCB lookups must not be counted as HTTPS RR lookups.
  equal(
    Glean.networking.dnsNativeCount.https_regular.testGetValue() ?? 0,
    httpsCount0,
    "dns_native_count.https_regular not incremented"
  );
  equal(
    Glean.dns.httpsRrLookupTime.native.testGetValue()?.count ?? 0,
    nativeLookups0,
    "https_rr_lookup_time.native not recorded"
  );

  let httpsRec = inRecord.QueryInterface(Ci.nsIDNSHTTPSSVCRecord);
  Assert.ok(!httpsRec.IsTRR(), "resolved by Native");

  let records = httpsRec.records;
  equal(records.length, 1, "one SVCB record");
  equal(records[0].priority, 1, "priority");
  equal(records[0].name, "doh.example.net", "target name");
  equal(records[0].dohPath, "/dns-query{?dns}", "dohpath present");

  let alpn = records[0].values
    .find(v => v.type == 1)
    .QueryInterface(Ci.nsISVCParamAlpn).alpn;
  Assert.deepEqual(alpn, ["h2", "h3"], "alpn");

  let port = records[0].values
    .find(v => v.type == 3)
    .QueryInterface(Ci.nsISVCParamPort).port;
  equal(port, 8443, "port");

  await trrServer.stop();
});

async function resolveByType(host, type) {
  let listener = new Listener();
  Services.dns.asyncResolve(
    host,
    type,
    Ci.nsIDNSService.RESOLVE_BYPASS_CACHE,
    null,
    listener,
    mainThread,
    defaultOriginAttributes
  );
  let [, , inStatus] = await listener;
  return inStatus;
}

// An answer of the other type (HTTPS for an SVCB query, or SVCB for an HTTPS
// query) must not be accepted.
add_task(async function test_svcb_https_type_mismatch() {
  let trrServer = new TRRServer();
  await trrServer.start();
  registerCleanupFunction(async () => {
    await trrServer.stop();
  });

  const data = {
    priority: 1,
    name: "target.example.net",
    values: [{ key: "alpn", value: ["h2"] }],
  };

  for (let [answerType, queryType] of [
    ["HTTPS", Ci.nsIDNSService.RESOLVE_TYPE_SVCB],
    ["SVCB", Ci.nsIDNSService.RESOLVE_TYPE_HTTPSSVC],
  ]) {
    let host = `${answerType.toLowerCase()}-mismatch.example.com`;
    await trrServer.registerDoHAnswers(host, answerType, {
      answers: [{ name: host, ttl: 55, type: answerType, flush: false, data }],
    });
    let chan = makeChan(
      `https://foo.example.com:${trrServer.port()}/dnsAnswer?host=${host}&type=${answerType}`
    );
    let [, buf] = await channelOpenPromise(chan);
    let rawBuffer = hexToUint8Array(buf);
    override.addHTTPSRecordOverride(host, rawBuffer, rawBuffer.length);

    let status = await resolveByType(host, queryType);
    Assert.ok(
      !Components.isSuccessCode(status),
      `${answerType} answer is not accepted for query type ${queryType}`
    );
  }

  await trrServer.stop();
});
