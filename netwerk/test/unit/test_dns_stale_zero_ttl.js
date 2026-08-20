/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Regression test for the optimistic-DNS / Happy Eyeballs "got a stale response
// for refresh query" crash (bug 2059597): an answer with a 0 TTL is "born in"
// the cache grace period (SetExpiration puts mGraceStart at now + ttl), so
// deriving nsIDNSAddrRecord.fromStaleCache from the grace status at read time
// misreports a freshly resolved answer -- including a cache-bypassing
// revalidation -- as stale. The flag must instead be captured at serve time:
// true only for a grace-period cache hit, and false for any fresh resolution.
//
// The answer is served by a DoH (TRR) server so it can carry a real TTL of 0,
// and its responses are held back by `delay` so the background renewal that a
// grace hit triggers cannot clear the flag before we read it -- no timing race.

const HOST = "ttl-zero.example.com";
const ADDR = "127.0.0.1";
// Long enough that the renewal a grace hit kicks off cannot complete before we
// read the flag, and that a cache-bypassing lookup must wait for a fresh answer.
const DELAY_MS = 1000;

let trrServer;

function registerA(ttl, delay) {
  return trrServer.registerDoHAnswers(HOST, "A", {
    answers: [{ name: HOST, ttl, type: "A", flush: false, data: ADDR }],
    delay,
  });
}

function resolve(flags) {
  return new Promise((resolve, reject) => {
    let listener = {
      onLookupComplete(inRequest, inRecord, inStatus) {
        if (!Components.isSuccessCode(inStatus)) {
          reject(inStatus);
          return;
        }
        resolve(inRecord.QueryInterface(Ci.nsIDNSAddrRecord));
      },
      QueryInterface: ChromeUtils.generateQI(["nsIDNSListener"]),
    };
    Services.dns.asyncResolve(
      HOST,
      Ci.nsIDNSService.RESOLVE_TYPE_DEFAULT,
      flags,
      null,
      listener,
      Services.tm.currentThread,
      {}
    );
  });
}

add_setup(async function setup() {
  trr_test_setup();
  // TRR_ONLY so every answer comes from the DoH server, keeping native
  // resolution (which answers 127.0.0.1 for everything in TRR tests) out of it.
  Services.prefs.setIntPref("network.trr.mode", 3);

  trrServer = new TRRServer();
  await trrServer.start();
  Services.prefs.setCharPref(
    "network.trr.uri",
    `https://foo.example.com:${trrServer.port()}/dns-query`
  );
  // A single address family keeps the answer unambiguous.
  await trrServer.registerDoHAnswers(HOST, "AAAA", { answers: [] });

  registerCleanupFunction(async () => {
    // Turn TRR off before dropping its URI, so nothing looks up the default
    // (non-local) resolver on the way out.
    Services.prefs.clearUserPref("network.trr.mode");
    Services.prefs.clearUserPref("network.trr.uri");
    trr_clear_prefs();
    try {
      await trrServer.stop();
    } catch (e) {
      info("Error stopping server: " + e);
    }
  });
});

add_task(async function test_stale_cache_lifecycle() {
  // 1. First resolution: a fresh TTL-0 answer. It is born in the grace window,
  //    but as a freshly resolved answer it must not be reported as stale.
  await registerA(0, 0);
  let fresh = await resolve(Ci.nsIDNSService.RESOLVE_DEFAULT_FLAGS);
  Assert.equal(
    fresh.fromStaleCache,
    false,
    "a freshly resolved TTL-0 answer is not stale"
  );

  // Hold subsequent answers back so the renewal a grace hit starts (and, later,
  // the bypass lookup) cannot complete before we read the flag.
  await registerA(0, DELAY_MS);

  // 2. Second resolution served from the grace-period cache: reported as stale.
  let cached = await resolve(Ci.nsIDNSService.RESOLVE_DEFAULT_FLAGS);
  Assert.equal(
    cached.fromStaleCache,
    true,
    "a grace-period cache hit is reported as stale"
  );

  // 3. Cache-bypassing revalidation (what Happy Eyeballs does after a stale
  //    answer): a fresh answer again, so it must not be reported as stale even
  //    though its 0 TTL once more lands it in the grace window.
  let revalidated = await resolve(Ci.nsIDNSService.RESOLVE_BYPASS_CACHE);
  Assert.equal(
    revalidated.fromStaleCache,
    false,
    "a cache-bypassing revalidation of a TTL-0 host is not stale"
  );
});
