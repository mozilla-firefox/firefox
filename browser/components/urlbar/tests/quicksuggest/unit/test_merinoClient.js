/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Test for MerinoClient.

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExperimentFakes: "resource://testing-common/NimbusTestUtils.sys.mjs",
  MerinoClient: "resource:///modules/MerinoClient.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
});

// Set the `merino.timeoutMs` pref to a large value so that the client will not
// inadvertently time out during fetches. This is especially important on CI and
// when running this test in verify mode. Tasks that specifically test timeouts
// may need to set a more reasonable value for their duration.
const TEST_TIMEOUT_MS = 30000;

// The expected suggestion objects returned from `MerinoClient.fetch()`.
const EXPECTED_MERINO_SUGGESTIONS = [];

const { SEARCH_PARAMS } = MerinoClient;

let gClient;

add_setup(async function init() {
  UrlbarPrefs.set("merino.timeoutMs", TEST_TIMEOUT_MS);
  registerCleanupFunction(() => {
    UrlbarPrefs.clear("merino.timeoutMs");
  });

  gClient = new MerinoClient();
  await MerinoTestUtils.server.start();

  for (let suggestion of MerinoTestUtils.server.response.body.suggestions) {
    EXPECTED_MERINO_SUGGESTIONS.push({
      ...suggestion,
      request_id: MerinoTestUtils.server.response.body.request_id,
      source: "merino",
    });
  }
});

// Checks client names.
add_task(async function name() {
  Assert.equal(
    gClient.name,
    "anonymous",
    "gClient name is 'anonymous' since it wasn't given a name"
  );

  let client = new MerinoClient("New client");
  Assert.equal(client.name, "New client", "newClient name is correct");
});

// Does a successful fetch.
add_task(async function success() {
  await fetchAndCheckSuggestions({
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  Assert.equal(
    gClient.lastFetchStatus,
    "success",
    "The request successfully finished"
  );
});

// Does a successful fetch that doesn't return any suggestions.
add_task(async function noSuggestions() {
  let { suggestions } = MerinoTestUtils.server.response.body;
  MerinoTestUtils.server.response.body.suggestions = [];

  await fetchAndCheckSuggestions({
    expected: [],
  });

  Assert.equal(
    gClient.lastFetchStatus,
    "no_suggestion",
    "The request successfully finished without suggestions"
  );

  MerinoTestUtils.server.response.body.suggestions = suggestions;
});

// Tests `MerinoClient`'s simplistic caching mechanism.
add_task(async function cache() {
  MerinoTestUtils.enableClientCache(true);

  // Stub `MerinoClient`'s `Date.now()` so we can artificially set the date.
  let sandbox = sinon.createSandbox();
  let dateNowStub = sandbox.stub(
    Cu.getGlobalForObject(MerinoClient).Date,
    "now"
  );
  let startDateMs = Date.now();
  dateNowStub.returns(startDateMs);

  // We'll do fetches with this client and pass it this provider.
  let provider = "test-provider";
  let client = new MerinoClient("cache-test", { cachePeriodMs: 60 * 1000 });

  // We'll do each of these fetches in order.
  let fetches = [
    {
      timeOffsetMs: 0,
      query: "aaa",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 30 * 1000,
      query: "aaa",
      shouldCallMerino: false,
    },
    // This call is exactly when the cache period expires, so the cache should
    // not be used.
    {
      timeOffsetMs: 60 * 1000,
      query: "aaa",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 90 * 1000,
      query: "aaa",
      shouldCallMerino: false,
    },
    // This call is well past the previous cache period (which expired at an
    // offset of 120), so the cache should not be used.
    {
      timeOffsetMs: 200 * 1000,
      query: "aaa",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 259 * 1000,
      query: "aaa",
      shouldCallMerino: false,
    },
    // This call is exactly when the cache period expires, so the cache should
    // not be used.
    {
      timeOffsetMs: 260 * 1000,
      query: "aaa",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 261 * 1000,
      query: "aaa",
      shouldCallMerino: false,
    },
    // A different query creates a different request URL, so the cache should
    // not be used even though this request is within the current cache period.
    {
      timeOffsetMs: 262 * 1000,
      query: "bbb",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 263 * 1000,
      query: "bbb",
      shouldCallMerino: false,
    },
    {
      timeOffsetMs: 264 * 1000,
      query: "aaa",
      shouldCallMerino: true,
    },
    {
      timeOffsetMs: 265 * 1000,
      query: "aaa",
      shouldCallMerino: false,
    },
  ];

  // Do each fetch one after another.
  for (let i = 0; i < fetches.length; i++) {
    let fetch = fetches[i];
    info(`Fetch ${i} start: ` + JSON.stringify(fetch));

    let { timeOffsetMs, query, shouldCallMerino } = fetch;

    // Set the date forward.
    dateNowStub.returns(startDateMs + timeOffsetMs);

    // Do the fetch.
    let callsByProvider = await doFetchAndGetCalls(client, {
      query,
      providers: [provider],
    });
    info(`Fetch ${i} callsByProvider: ` + JSON.stringify(callsByProvider));

    // Stringify each `URLSearchParams` in the `calls` array in each
    // `[provider, calls]` for easier comparison.
    let actualCalls = Object.entries(callsByProvider).map(([p, calls]) => [
      p,
      calls.map(params => {
        params.delete(MerinoClient.SEARCH_PARAMS.SESSION_ID);
        params.delete(MerinoClient.SEARCH_PARAMS.SEQUENCE_NUMBER);
        return params.toString();
      }),
    ]);

    if (!shouldCallMerino) {
      Assert.deepEqual(
        actualCalls,
        [],
        `Fetch ${i} should not have called Merino`
      );
    } else {
      // Build the expected params.
      let expectedParams = new URLSearchParams();
      expectedParams.set(MerinoClient.SEARCH_PARAMS.PROVIDERS, provider);
      expectedParams.set(MerinoClient.SEARCH_PARAMS.QUERY, query);
      expectedParams.sort();

      Assert.deepEqual(
        actualCalls,
        [[provider, [expectedParams.toString()]]],
        `Fetch ${i} should have called Merino`
      );
    }
  }

  sandbox.restore();
  MerinoTestUtils.enableClientCache(false);
});

async function doFetchAndGetCalls(client, fetchArgs) {
  let callsByProvider = {};

  MerinoTestUtils.server.requestHandler = req => {
    let params = new URLSearchParams(req.queryString);
    params.sort();
    let provider = params.get("providers");
    callsByProvider[provider] ||= [];
    callsByProvider[provider].push(params);
    return {
      body: {
        request_id: "request_id",
        suggestions: [{ foo: "bar" }],
      },
    };
  };

  await client.fetch(fetchArgs);

  MerinoTestUtils.server.requestHandler = null;
  return callsByProvider;
}

// Checks a 204 "No content" response.
add_task(async function noContent() {
  MerinoTestUtils.server.response = { status: 204 };
  await fetchAndCheckSuggestions({ expected: [] });

  Assert.equal(
    gClient.lastFetchStatus,
    "no_suggestion",
    "The request should have been recorded as no_suggestion"
  );

  MerinoTestUtils.server.reset();
});

// Checks a response that's valid but also has some unexpected properties.
add_task(async function unexpectedResponseProperties() {
  MerinoTestUtils.server.response.body.unexpectedString = "some value";
  MerinoTestUtils.server.response.body.unexpectedArray = ["a", "b", "c"];
  MerinoTestUtils.server.response.body.unexpectedObject = { foo: "bar" };

  await fetchAndCheckSuggestions({
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  Assert.equal(
    gClient.lastFetchStatus,
    "success",
    "The request successfully finished"
  );
});

// Checks some responses with unexpected response bodies.
add_task(async function unexpectedResponseBody() {
  let responses = [
    { body: {} },
    { body: { bogus: [] } },
    { body: { suggestions: {} } },
    { body: { suggestions: [] } },
    { body: "" },
    { body: "bogus", contentType: "text/html" },
  ];

  for (let r of responses) {
    info("Testing response: " + JSON.stringify(r));

    MerinoTestUtils.server.response = r;
    await fetchAndCheckSuggestions({ expected: [] });

    Assert.equal(
      gClient.lastFetchStatus,
      "no_suggestion",
      "The request successfully finished without suggestions"
    );
  }

  MerinoTestUtils.server.reset();
});

// Tests with a network error.
add_task(async function networkError() {
  // This promise will be resolved when the client processes the network error.
  let responsePromise = gClient.waitForNextResponse();

  await MerinoTestUtils.server.withNetworkError(async () => {
    await fetchAndCheckSuggestions({ expected: [] });
  });

  // The client should have nulled out the timeout timer before `fetch()`
  // returned.
  Assert.strictEqual(
    gClient._test_timeoutTimer,
    null,
    "timeoutTimer does not exist after fetch finished"
  );

  // Wait for the client to process the network error.
  await responsePromise;

  Assert.equal(
    gClient.lastFetchStatus,
    "network_error",
    "The request failed with a network error"
  );
});

// Tests with an HTTP error.
add_task(async function httpError() {
  MerinoTestUtils.server.response = { status: 500 };
  await fetchAndCheckSuggestions({ expected: [] });

  Assert.equal(
    gClient.lastFetchStatus,
    "http_error",
    "The request failed with an HTTP error"
  );

  MerinoTestUtils.server.reset();
});

// Tests a client timeout.
add_task(async function clientTimeout() {
  await doClientTimeoutTest({
    prefTimeoutMs: 200,
    responseDelayMs: 400,
  });
});

// Tests a client timeout followed by an HTTP error. Only the timeout should be
// recorded.
add_task(async function clientTimeoutFollowedByHTTPError() {
  MerinoTestUtils.server.response = { status: 500 };
  await doClientTimeoutTest({
    prefTimeoutMs: 200,
    responseDelayMs: 400,
    expectedResponseStatus: 500,
  });
});

// Tests a client timeout when a timeout value is passed to `fetch()`, which
// should override the value in the `merino.timeoutMs` pref.
add_task(async function timeoutPassedToFetch() {
  // Set up a timeline like this:
  //
  //     1ms: The timeout passed to `fetch()` elapses
  //   400ms: Merino returns a response
  // 30000ms: The timeout in the pref elapses
  //
  // The expected behavior is that the 1ms timeout is hit, the request fails
  // with a timeout, and Merino later returns a response. If the 1ms timeout is
  // not hit, then Merino will return a response before the 30000ms timeout
  // elapses and the request will complete successfully.

  await doClientTimeoutTest({
    prefTimeoutMs: 30000,
    responseDelayMs: 400,
    fetchArgs: { query: "search", timeoutMs: 1 },
  });
});

async function doClientTimeoutTest({
  prefTimeoutMs,
  responseDelayMs,
  fetchArgs = { query: "search" },
  expectedResponseStatus = 200,
} = {}) {
  let originalPrefTimeoutMs = UrlbarPrefs.get("merino.timeoutMs");
  UrlbarPrefs.set("merino.timeoutMs", prefTimeoutMs);

  // Make the server return a delayed response so the client times out waiting
  // for it.
  MerinoTestUtils.server.response.delay = responseDelayMs;

  let responsePromise = gClient.waitForNextResponse();
  await fetchAndCheckSuggestions({ args: fetchArgs, expected: [] });

  Assert.equal(gClient.lastFetchStatus, "timeout", "The request timed out");

  // The client should have nulled out the timeout timer.
  Assert.strictEqual(
    gClient._test_timeoutTimer,
    null,
    "timeoutTimer does not exist after fetch finished"
  );

  // The fetch controller should still exist because the fetch should remain
  // ongoing.
  Assert.ok(
    gClient._test_fetchController,
    "fetchController still exists after fetch finished"
  );
  Assert.ok(
    !gClient._test_fetchController.signal.aborted,
    "fetchController is not aborted"
  );

  // Wait for the client to receive the response.
  let httpResponse = await responsePromise;
  Assert.ok(httpResponse, "Response was received");
  Assert.equal(httpResponse.status, expectedResponseStatus, "Response status");

  // The client should have nulled out the fetch controller.
  Assert.ok(!gClient._test_fetchController, "fetchController no longer exists");

  MerinoTestUtils.server.reset();
  UrlbarPrefs.set("merino.timeoutMs", originalPrefTimeoutMs);
}

// By design, when a fetch times out, the client allows it to finish so we can
// record its latency. But when a second fetch starts before the first finishes,
// the client should abort the first so that there is at most one fetch at a
// time.
add_task(async function newFetchAbortsPrevious() {
  // Make the server return a very delayed response so that it would time out
  // and we can start a second fetch that will abort the first fetch.
  MerinoTestUtils.server.response.delay =
    100 * UrlbarPrefs.get("merino.timeoutMs");

  // Do the first fetch.
  await fetchAndCheckSuggestions({ expected: [] });

  // At this point, the timeout timer has fired, causing our `fetch()` call to
  // return. However, the client's internal fetch should still be ongoing.

  Assert.equal(gClient.lastFetchStatus, "timeout", "The request timed out");

  // The client should have nulled out the timeout timer.
  Assert.strictEqual(
    gClient._test_timeoutTimer,
    null,
    "timeoutTimer does not exist after first fetch finished"
  );

  // The fetch controller should still exist because the fetch should remain
  // ongoing.
  Assert.ok(
    gClient._test_fetchController,
    "fetchController still exists after first fetch finished"
  );
  Assert.ok(
    !gClient._test_fetchController.signal.aborted,
    "fetchController is not aborted"
  );

  // Do the second fetch. This time don't delay the response.
  delete MerinoTestUtils.server.response.delay;
  await fetchAndCheckSuggestions({
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  Assert.equal(
    gClient.lastFetchStatus,
    "success",
    "The request finished successfully"
  );

  // The fetch was successful, so the client should have nulled out both
  // properties.
  Assert.ok(
    !gClient._test_fetchController,
    "fetchController does not exist after second fetch finished"
  );
  Assert.strictEqual(
    gClient._test_timeoutTimer,
    null,
    "timeoutTimer does not exist after second fetch finished"
  );

  MerinoTestUtils.server.reset();
});

// The client should not include the `clientVariants` and `providers` search
// params when they are not set.
add_task(async function clientVariants_providers_notSet() {
  UrlbarPrefs.set("merino.clientVariants", "");
  UrlbarPrefs.set("merino.providers", "");

  await fetchAndCheckSuggestions({
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  MerinoTestUtils.server.checkAndClearRequests([
    {
      params: {
        [SEARCH_PARAMS.QUERY]: "search",
        [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
      },
    },
  ]);

  UrlbarPrefs.clear("merino.clientVariants");
  UrlbarPrefs.clear("merino.providers");
});

// The client should include the `clientVariants` and `providers` search params
// when they are set using preferences.
add_task(async function clientVariants_providers_preferences() {
  UrlbarPrefs.set("merino.clientVariants", "green");
  UrlbarPrefs.set("merino.providers", "pink");

  await fetchAndCheckSuggestions({
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  MerinoTestUtils.server.checkAndClearRequests([
    {
      params: {
        [SEARCH_PARAMS.QUERY]: "search",
        [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
        [SEARCH_PARAMS.CLIENT_VARIANTS]: "green",
        [SEARCH_PARAMS.PROVIDERS]: "pink",
      },
    },
  ]);

  UrlbarPrefs.clear("merino.clientVariants");
  UrlbarPrefs.clear("merino.providers");
});

// The client should include the `providers` search param when it's set by
// passing in the `providers` argument to `fetch()`. The argument should
// override the pref. This tests a single provider.
add_task(async function providers_arg_single() {
  UrlbarPrefs.set("merino.providers", "prefShouldNotBeUsed");

  await fetchAndCheckSuggestions({
    args: { query: "search", providers: ["argShouldBeUsed"] },
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  MerinoTestUtils.server.checkAndClearRequests([
    {
      params: {
        [SEARCH_PARAMS.QUERY]: "search",
        [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
        [SEARCH_PARAMS.PROVIDERS]: "argShouldBeUsed",
      },
    },
  ]);

  UrlbarPrefs.clear("merino.providers");
});

// The client should include the `providers` search param when it's set by
// passing in the `providers` argument to `fetch()`. The argument should
// override the pref. This tests multiple providers.
add_task(async function providers_arg_many() {
  UrlbarPrefs.set("merino.providers", "prefShouldNotBeUsed");

  await fetchAndCheckSuggestions({
    args: { query: "search", providers: ["one", "two", "three"] },
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  MerinoTestUtils.server.checkAndClearRequests([
    {
      params: {
        [SEARCH_PARAMS.QUERY]: "search",
        [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
        [SEARCH_PARAMS.PROVIDERS]: "one,two,three",
      },
    },
  ]);

  UrlbarPrefs.clear("merino.providers");
});

// The client should include the `providers` search param when it's set by
// passing in the `providers` argument to `fetch()` even when it's an empty
// array. The argument should override the pref.
add_task(async function providers_arg_empty() {
  UrlbarPrefs.set("merino.providers", "prefShouldNotBeUsed");

  await fetchAndCheckSuggestions({
    args: { query: "search", providers: [] },
    expected: EXPECTED_MERINO_SUGGESTIONS,
  });

  MerinoTestUtils.server.checkAndClearRequests([
    {
      params: {
        [SEARCH_PARAMS.QUERY]: "search",
        [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
        [SEARCH_PARAMS.PROVIDERS]: "",
      },
    },
  ]);

  UrlbarPrefs.clear("merino.providers");
});

// Passes invalid `providers` arguments to `fetch()`.
add_task(async function providers_arg_invalid() {
  let providersValues = ["", "nonempty", {}];

  for (let providers of providersValues) {
    info("Calling fetch() with providers: " + JSON.stringify(providers));

    // `Assert.throws()` doesn't seem to work with async functions...
    let error;
    try {
      await gClient.fetch({ providers, query: "search" });
    } catch (e) {
      error = e;
    }
    Assert.ok(error, "fetch() threw an error");
    Assert.equal(
      error.message,
      "providers must be an array if given",
      "Expected error was thrown"
    );
  }
});

// Tests setting the endpoint URL and query parameters via Nimbus.
add_task(async function nimbus() {
  // Clear the endpoint pref so we know the URL is not being fetched from it.
  let originalEndpointURL = UrlbarPrefs.get("merino.endpointURL");
  UrlbarPrefs.set("merino.endpointURL", "");

  await UrlbarTestUtils.initNimbusFeature();

  // First, with the endpoint pref set to an empty string, make sure no Merino
  // suggestion are returned.
  await fetchAndCheckSuggestions({ expected: [] });

  // Now install an experiment that sets the endpoint and other Merino-related
  // variables. Make sure a suggestion is returned and the request includes the
  // correct query params.

  // `param`: The param name in the request URL
  // `value`: The value to use for the param
  // `variable`: The name of the Nimbus variable corresponding to the param
  let expectedParams = [
    {
      param: SEARCH_PARAMS.CLIENT_VARIANTS,
      value: "test-client-variants",
      variable: "merinoClientVariants",
    },
    {
      param: SEARCH_PARAMS.PROVIDERS,
      value: "test-providers",
      variable: "merinoProviders",
    },
  ];

  // Set up the Nimbus variable values to create the experiment with.
  let experimentValues = {
    merinoEndpointURL: MerinoTestUtils.server.url.toString(),
  };
  for (let { variable, value } of expectedParams) {
    experimentValues[variable] = value;
  }

  await withExperiment(experimentValues, async () => {
    await fetchAndCheckSuggestions({ expected: EXPECTED_MERINO_SUGGESTIONS });

    let params = {
      [SEARCH_PARAMS.QUERY]: "search",
      [SEARCH_PARAMS.SEQUENCE_NUMBER]: 0,
    };
    for (let { param, value } of expectedParams) {
      params[param] = value;
    }
    MerinoTestUtils.server.checkAndClearRequests([{ params }]);
  });

  UrlbarPrefs.set("merino.endpointURL", originalEndpointURL);
});

async function fetchAndCheckSuggestions({
  expected,
  args = {
    query: "search",
  },
}) {
  let actual = await gClient.fetch(args);
  Assert.deepEqual(actual, expected, "Expected suggestions");
  gClient.resetSession();
}

async function withExperiment(values, callback) {
  const doExperimentCleanup = await ExperimentFakes.enrollWithFeatureConfig(
    {
      featureId: NimbusFeatures.urlbar.featureId,
      value: {
        enabled: true,
        ...values,
      },
    },
    {
      slug: "mock-experiment",
      branchSlug: "treatment",
    }
  );
  await callback();
  doExperimentCleanup();
}
