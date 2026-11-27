/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// Tests for `History.countVisits`.

add_setup(async function () {
  let now = Date.now();
  let daysAgo = days => new Date(now - days * 86400000);
  await PlacesTestUtils.addVisits([
    { uri: "https://mozilla.org/a", visitDate: daysAgo(10) },
    { uri: "https://mozilla.org/a", visitDate: daysAgo(5) },
    { uri: "https://mozilla.org/b", visitDate: daysAgo(1) },
    { uri: "https://www.mozilla.org/", visitDate: daysAgo(3) },
    { uri: "https://developer.mozilla.org/", visitDate: daysAgo(20) },
    { uri: "https://notmozilla.org/", visitDate: daysAgo(2) },
    { uri: "http://localhost/", visitDate: daysAgo(4) },
    { uri: "file:///tmp/test.html", visitDate: daysAgo(6) },
    { uri: "http://[::1]/", visitDate: daysAgo(7) },
    { uri: "http://[::1]:8080/page", visitDate: daysAgo(8) },
    { uri: "http://[2001:db8::1]/", visitDate: daysAgo(9) },
    { uri: "http://my_host.intranet/", visitDate: daysAgo(11) },
  ]);
  registerCleanupFunction(() => PlacesUtils.history.clear());
});

add_task(async function test_host() {
  let count = filter => PlacesUtils.history.countVisits(filter);
  Assert.equal(await count({ host: "mozilla.org" }), 3, "Host only");
  Assert.equal(await count({ host: "MOZILLA.org" }), 3, "Case insensitive");
  Assert.equal(await count({ host: ".mozilla.org" }), 5, "With subdomains");
  Assert.equal(await count({ host: "www.mozilla.org" }), 1, "Subdomain");
  Assert.equal(await count({ host: "localhost" }), 1, "localhost");
  Assert.equal(await count({ host: ".localhost" }), 1, "Wildcard localhost");
  Assert.equal(await count({ host: "." }), 1, "Local files");
  Assert.equal(await count({ host: "example.com" }), 0, "No visits");
  Assert.equal(await count({ host: "my_host.intranet" }), 1, "Underscore");
  Assert.equal(await count({ host: "::1" }), 2, "IPv6");
  Assert.equal(await count({ host: ".::1" }), 2, "IPv6 ignores the wildcard");
  Assert.equal(
    await count({ host: "2001:DB8::1" }),
    1,
    "IPv6 case insensitive"
  );
  Assert.equal(
    await count({ host: "2001:0db8:0:0::1" }),
    1,
    "IPv6 non canonical form"
  );
});

add_task(async function test_dates() {
  let now = Date.now();
  let daysAgo = days => new Date(now - days * 86400000);
  let count = filter => PlacesUtils.history.countVisits(filter);
  Assert.equal(
    await count({ host: ".mozilla.org", endDate: daysAgo(4) }),
    3,
    "Before a date"
  );
  Assert.equal(
    await count({ host: ".mozilla.org", beginDate: daysAgo(4) }),
    2,
    "After a date"
  );
  Assert.equal(
    await count({ beginDate: daysAgo(5.5), endDate: daysAgo(1.5) }),
    4,
    "Only dates"
  );
});

add_task(async function test_inclusive_dates() {
  let date = new Date(Date.now() - 30 * 86400000);
  await PlacesTestUtils.addVisits({
    uri: "https://bounds.example.com/",
    visitDate: date,
  });
  Assert.equal(
    await PlacesUtils.history.countVisits({
      host: "bounds.example.com",
      beginDate: date,
      endDate: date,
    }),
    1,
    "Both dates are inclusive"
  );
});

add_task(async function test_hidden() {
  await PlacesTestUtils.addVisits({
    uri: "https://framed.example.com/",
    transition: PlacesUtils.history.TRANSITIONS.FRAMED_LINK,
  });
  let db = await PlacesUtils.promiseDBConnection();
  let rows = await db.execute(
    "SELECT hidden FROM moz_places WHERE url_hash = hash(:url) AND url = :url",
    { url: "https://framed.example.com/" }
  );
  Assert.equal(rows[0].getResultByName("hidden"), 1, "The page is hidden");
  Assert.equal(
    await PlacesUtils.history.countVisits({ host: "framed.example.com" }),
    1,
    "Visits to hidden pages are counted"
  );
});

add_task(async function test_invalid() {
  for (let filter of [
    null,
    {},
    { host: 1 },
    { host: "mozilla..org" },
    { host: "::1/" },
    { host: "192.168.1.1:8080" },
    { host: "..::1" },
    { host: ".mozilla.org", beginDate: "today" },
    { beginDate: new Date(), endDate: new Date(Date.now() - 1000) },
  ]) {
    Assert.throws(
      () => PlacesUtils.history.countVisits(filter),
      /TypeError/,
      `Throws for ${JSON.stringify(filter)}`
    );
  }
});
