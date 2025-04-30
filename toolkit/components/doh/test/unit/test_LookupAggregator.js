/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

add_task(setup);

async function helper_SuccessfulLookupAggregator(
  networkUnstable = false,
  captivePortal = false
) {
  Services.fog.testResetFOG();
  let deferred = Promise.withResolvers();
  let aggregator = new LookupAggregator(() => deferred.resolve(), trrList);
  // The aggregator's domain list should correctly reflect our set
  // prefs for number of random subdomains (2) and the list of
  // popular domains.
  Assert.equal(aggregator.domains[0], null);
  Assert.equal(aggregator.domains[1], null);
  Assert.equal(aggregator.domains[2], "foo.example.com.");
  Assert.equal(aggregator.domains[3], "bar.example.com.");
  Assert.equal(aggregator.totalLookups, 8); // 2 TRRs * 4 domains.

  if (networkUnstable) {
    aggregator.markUnstableNetwork();
  }
  if (captivePortal) {
    aggregator.markCaptivePortal();
  }
  aggregator.run();
  await deferred.promise;
  Assert.ok(!aggregator.aborted);
  Assert.equal(aggregator.networkUnstable, networkUnstable);
  Assert.equal(aggregator.captivePortal, captivePortal);
  Assert.equal(aggregator.results.length, aggregator.totalLookups);

  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  Assert.ok(events);
  info(events);

  events = events.filter(e => e.category == "security.doh.trr_performance");
  Assert.equal(events.length, aggregator.totalLookups);

  for (let event of events) {
    info(JSON.stringify(event));
    Assert.equal(event.category, "security.doh.trr_performance");
    Assert.equal(event.name, "resolved_record");
    Assert.equal(event.extra.value, "success");
  }

  // We only need to check the payload of each event from here on.
  events = events.map(e => e.extra);

  for (let trr of [trrServer1, trrServer2]) {
    // There should be two results for random subdomains.
    let results = aggregator.results.filter(r => {
      return r.trr == trr && r.domain.endsWith(".firefox-dns-perf-test.net.");
    });
    Assert.equal(results.length, 2);

    for (let result of results) {
      Assert.ok(result.domain.endsWith(".firefox-dns-perf-test.net."));
      Assert.equal(result.trr, trr);
      Assert.ok(Components.isSuccessCode(result.status));
      Assert.greater(result.time, 0);
      Assert.equal(result.retryCount, 1);

      let matchingEvents = events.filter(
        e => e.domain == result.domain && e.trr == result.trr
      );
      Assert.equal(matchingEvents.length, 1);
      let e = matchingEvents.pop();
      for (let key of Object.keys(result)) {
        Assert.equal(e[key], result[key].toString());
      }
      Assert.equal(e.networkUnstable, networkUnstable.toString());
      Assert.equal(e.captivePortal, captivePortal.toString());
    }

    // There should be two results for the popular domains.
    results = aggregator.results.filter(r => {
      return r.trr == trr && !r.domain.endsWith(".firefox-dns-perf-test.net.");
    });
    Assert.equal(results.length, 2);

    Assert.ok(
      [results[0].domain, results[1].domain].includes("foo.example.com.")
    );
    Assert.ok(
      [results[0].domain, results[1].domain].includes("bar.example.com.")
    );
    for (let result of results) {
      Assert.equal(result.trr, trr);
      Assert.equal(result.status, Cr.NS_OK);
      Assert.greater(result.time, 0);
      Assert.equal(result.retryCount, 1);

      let matchingEvents = events.filter(
        e => e.domain == result.domain && e.trr == result.trr
      );
      Assert.equal(matchingEvents.length, 1);
      let e = matchingEvents.pop();
      for (let key of Object.keys(result)) {
        Assert.equal(e[key], result[key].toString());
      }
      Assert.equal(e.networkUnstable, networkUnstable.toString());
      Assert.equal(e.captivePortal, captivePortal.toString());
    }
  }

  Services.fog.testResetFOG();
}

add_task(async function test_SuccessfulLookupAggregator() {
  await helper_SuccessfulLookupAggregator(false, false);
  await helper_SuccessfulLookupAggregator(false, true);
  await helper_SuccessfulLookupAggregator(true, false);
  await helper_SuccessfulLookupAggregator(true, true);
});

add_task(async function test_AbortedLookupAggregator() {
  Services.fog.testResetFOG();
  let deferred = Promise.withResolvers();
  let aggregator = new LookupAggregator(() => deferred.resolve(), trrList);
  // The aggregator's domain list should correctly reflect our set
  // prefs for number of random subdomains (2) and the list of
  // popular domains.
  Assert.equal(aggregator.domains[0], null);
  Assert.equal(aggregator.domains[1], null);
  Assert.equal(aggregator.domains[2], "foo.example.com.");
  Assert.equal(aggregator.domains[3], "bar.example.com.");
  Assert.equal(aggregator.totalLookups, 8); // 2 TRRs * 4 domains.

  // The aggregator should never call the onComplete callback. To test
  // this, race the deferred promise with a 3 second timeout. The timeout
  // should win, since the deferred promise should never resolve.
  let timeoutPromise = new Promise(resolve => {
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    setTimeout(() => resolve("timeout"), 3000);
  });
  aggregator.run();
  aggregator.abort();
  let winner = await Promise.race([deferred.promise, timeoutPromise]);
  Assert.equal(winner, "timeout");
  Assert.ok(aggregator.aborted);
  Assert.ok(!aggregator.networkUnstable);
  Assert.ok(!aggregator.captivePortal);

  // Ensure we send no telemetry for an aborted run!
  let events =
    await Glean.securityDohTrrPerformance.resolvedRecord.testGetValue();
  Assert.ok(!events || !events.length);
});
