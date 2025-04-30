/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(setup);

add_task(async function test_SuccessfulRandomDNSLookup() {
  let deferred = Promise.withResolvers();
  let lookup = new DNSLookup(
    null,
    trrServer1,
    (request, record, status, usedDomain, retryCount) => {
      deferred.resolve({ request, record, status, usedDomain, retryCount });
    }
  );
  lookup.doLookup();
  let result = await deferred.promise;
  Assert.ok(result.usedDomain.endsWith(".firefox-dns-perf-test.net."));
  Assert.equal(result.status, Cr.NS_OK);
  Assert.ok(result.record.QueryInterface(Ci.nsIDNSAddrRecord));
  Assert.ok(result.record.IsTRR());
  Assert.greater(result.record.trrFetchDuration, 0);
  Assert.equal(result.retryCount, 1);
});

add_task(async function test_SuccessfulSpecifiedDNSLookup() {
  let deferred = Promise.withResolvers();
  let lookup = new DNSLookup(
    "foo.example.com",
    trrServer1,
    (request, record, status, usedDomain, retryCount) => {
      deferred.resolve({ request, record, status, usedDomain, retryCount });
    }
  );
  lookup.doLookup();
  let result = await deferred.promise;
  Assert.equal(result.usedDomain, "foo.example.com");
  Assert.equal(result.status, Cr.NS_OK);
  Assert.ok(result.record.QueryInterface(Ci.nsIDNSAddrRecord));
  Assert.ok(result.record.IsTRR());
  Assert.greater(result.record.trrFetchDuration, 0);
  Assert.equal(result.retryCount, 1);
});

add_task(async function test_FailedDNSLookup() {
  let deferred = Promise.withResolvers();
  let lookup = new DNSLookup(
    null,
    `https://foo.example.com:${h2Port}/doh?responseIP=none`,
    (request, record, status, usedDomain, retryCount) => {
      deferred.resolve({ request, record, status, usedDomain, retryCount });
    }
  );
  lookup.doLookup();
  let result = await deferred.promise;
  Assert.ok(result.usedDomain.endsWith(".firefox-dns-perf-test.net."));
  Assert.notEqual(result.status, Cr.NS_OK);
  Assert.equal(result.record, null);
  Assert.equal(result.retryCount, 3);
});
