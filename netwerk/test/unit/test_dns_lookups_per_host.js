/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Loads to a host should resolve it only once per record type: the channel's
// lookups and the connection's lookups must share their results (bug 2074097).

"use strict";

/* import-globals-from head_trr.js */

const { NodeHTTP2Server } = ChromeUtils.importESModule(
  "resource://testing-common/NodeServer.sys.mjs"
);

const TRR_HOST = "foo.example.com";
// Each test uses its own hosts, so that no lookup is served from the cache.
const HOSTS = {
  he: { document: "doc.he.dnscount.test", subresource: "sub.he.dnscount.test" },
  noHe: {
    document: "doc.nohe.dnscount.test",
    subresource: "sub.nohe.dnscount.test",
  },
};

let trrServer;
let h2Server;

add_setup(async function () {
  trrServer = new TRRServer();
  await trrServer.start();
  trr_test_setup();
  Services.prefs.setIntPref("network.trr.mode", Ci.nsIDNSService.MODE_TRRONLY);
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://${TRR_HOST}:${trrServer.port()}/dns-query`
  );
  // Speculative connections would add their own lookups.
  Services.prefs.setIntPref("network.http.speculative-parallel-limit", 0);

  h2Server = new NodeHTTP2Server();
  await h2Server.start(
    0,
    Object.values(HOSTS).flatMap(h => [h.document, h.subresource])
  );
  await h2Server.registerPathHandler("/", (req, res) => res.end("ok"));

  registerCleanupFunction(async () => {
    trr_clear_prefs();
    Services.prefs.clearUserPref("network.http.speculative-parallel-limit");
    Services.prefs.clearUserPref("network.http.happy_eyeballs_enabled");
    await trrServer.stop();
    await h2Server.stop();
  });
});

function httpsRecordName(host) {
  return `_${h2Server.port()}._https.${host}`;
}

async function registerAnswers(host) {
  await trrServer.registerDoHAnswers(host, "A", {
    answers: [
      { name: host, ttl: 55, type: "A", flush: false, data: "127.0.0.1" },
    ],
  });
  // Answer AAAA too: a failed AAAA lookup also queries A, which this test
  // isn't about.
  await trrServer.registerDoHAnswers(host, "AAAA", {
    answers: [{ name: host, ttl: 55, type: "AAAA", flush: false, data: "::1" }],
  });
  await trrServer.registerDoHAnswers(httpsRecordName(host), "HTTPS", {
    answers: [],
  });
}

// A load from a web page: HTTPS records aren't used for a non-document load
// with the system principal, see nsHttpChannel::OnBeforeConnect.
function load(host, path, isDocument) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(`https://${host}/`),
    {}
  );
  let chan = NetUtil.newChannel({
    uri: `https://${host}:${h2Server.port()}${path}`,
    loadingPrincipal: principal,
    triggeringPrincipal: principal,
    securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    contentPolicyType: isDocument
      ? Ci.nsIContentPolicy.TYPE_DOCUMENT
      : Ci.nsIContentPolicy.TYPE_OTHER,
  }).QueryInterface(Ci.nsIHttpChannel);
  if (isDocument) {
    chan.loadFlags |= Ci.nsIChannel.LOAD_INITIAL_DOCUMENT_URI;
  }
  return new Promise(resolve => {
    chan.asyncOpen(
      new ChannelListener(
        req => {
          Assert.equal(req.status, Cr.NS_OK, `loaded ${req.URI.spec}`);
          resolve();
        },
        null,
        CL_ALLOW_UNKNOWN_CL
      )
    );
  });
}

async function requestCounts(host) {
  return {
    A: await trrServer.requestCount(host, "A"),
    AAAA: await trrServer.requestCount(host, "AAAA"),
    HTTPS: await trrServer.requestCount(httpsRecordName(host), "HTTPS"),
  };
}

// Loads a document and its subresources, some of them from a second host, and
// checks how often each host was looked up.
async function checkLookups(happyEyeballs, { document, subresource }) {
  Services.prefs.setBoolPref(
    "network.http.happy_eyeballs_enabled",
    happyEyeballs
  );
  await registerAnswers(document);
  await registerAnswers(subresource);

  await load(document, "/", true);
  await Promise.all([
    load(document, "/?a", false),
    load(document, "/?b", false),
    load(subresource, "/?c", false),
    load(subresource, "/?d", false),
  ]);

  for (let host of [document, subresource]) {
    Assert.deepEqual(
      await requestCounts(host),
      { A: 1, AAAA: 1, HTTPS: 1 },
      `${host} was looked up once per record type, happy eyeballs ${happyEyeballs}`
    );
  }
}

add_task(async function test_happy_eyeballs_enabled() {
  await checkLookups(true, HOSTS.he);
});

add_task(async function test_happy_eyeballs_disabled() {
  await checkLookups(false, HOSTS.noHe);
});
