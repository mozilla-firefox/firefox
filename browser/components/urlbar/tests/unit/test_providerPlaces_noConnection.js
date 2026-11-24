/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_no_connection() {
  const TEST_URL = "https://history.mozilla.org/";
  await PlacesTestUtils.addVisits([{ uri: TEST_URL, title: "Test history" }]);

  let stub = sinon
    .stub(PlacesUtils, "promiseLargeCacheDBConnection")
    .resolves();
  registerCleanupFunction(() => stub.restore());

  let controller = UrlbarTestUtils.mockChildController();
  let context = createContext("^Hist", {
    isPrivate: false,
    providers: ["UrlbarProviderPlaces"],
  });
  await controller.startQuery(context);
  Assert.equal(context.results.length, 0, "No results without a connection");

  let pendingConn = Promise.withResolvers();
  stub.resetHistory();
  stub.returns(pendingConn.promise);
  let firstContext = createContext("^Hist", {
    isPrivate: false,
    providers: ["UrlbarProviderPlaces"],
  });
  let first = controller.startQuery(firstContext);
  await TestUtils.waitForCondition(
    () => stub.called,
    "Wait for the first query to request the connection"
  );
  stub.resolves();
  let secondContext = createContext("^Hist", {
    isPrivate: false,
    providers: ["UrlbarProviderPlaces"],
  });
  await controller.startQuery(secondContext);
  pendingConn.resolve();
  await first;
  Assert.equal(
    firstContext.results.length,
    0,
    "The canceled query has no results"
  );
  Assert.equal(
    secondContext.results.length,
    0,
    "An overlapping query completes without a connection"
  );

  stub.restore();
  context = createContext("^Hist", {
    isPrivate: false,
    providers: ["UrlbarProviderPlaces"],
  });
  await controller.startQuery(context);
  Assert.equal(context.results.length, 1, "Results are back with a connection");
  Assert.equal(context.results[0].payload.url, TEST_URL, "Check result URL");

  await PlacesUtils.history.clear();
});
