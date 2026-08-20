/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable mozilla/no-arbitrary-setTimeout */

/**
 * Testing search suggestions from SearchSuggestionController.sys.mjs.
 */

"use strict";

const { FormHistory } = ChromeUtils.importESModule(
  "resource://gre/modules/FormHistory.sys.mjs"
);
const { SearchSuggestionController } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/SearchSuggestionController.sys.mjs"
);

const { ConfigSearchEngine } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/search/ConfigSearchEngine.sys.mjs"
);
let getEngine;
let postEngine;
let unresolvableEngine;
let alternateJSONEngine;
let thirdPartyEngine;

add_setup(async function () {
  Services.fog.initializeFOG();

  Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
  // These tests intentionally test broken connections.
  consoleAllowList = consoleAllowList.concat([
    "Non-200 status or empty HTTP response: 404",
    "Non-200 status or empty HTTP response: 500",
    "SearchSuggestionController found an unexpected string value",
    "HTTP request timeout",
    "HTTP error",
  ]);

  let server = useHttpServer();
  server.registerContentType("sjs", "sjs");

  const ENGINE_DATA = [
    {
      id: "get-engine",
      baseURL: `${gHttpURL}/sjs/`,
      name: "GET suggestion engine",
      method: "GET",
      telemetrySuffix: "suffix",
    },
    {
      id: "post-engine",
      baseURL: `${gHttpURL}/sjs/`,
      name: "POST suggestion engine",
      method: "POST",
    },
    {
      id: "offline-engine",
      baseURL: "http://example.invalid/",
      name: "Offline suggestion engine",
      method: "GET",
    },
    {
      id: "alternative-json-engine",
      baseURL: `${gHttpURL}/sjs/`,
      name: "Alternative JSON suggestion type",
      method: "GET",
      alternativeJSONType: true,
    },
  ];

  SearchTestUtils.setRemoteSettingsConfig(
    ENGINE_DATA.map(data => {
      return {
        identifier: data.id,
        base: {
          name: data.name,
          urls: {
            suggestions: {
              base: data.baseURL + "searchSuggestions.sjs",
              searchTermParamName: "q",
            },
          },
        },
        variants: [
          {
            environment: {
              allRegionsAndLocales: true,
            },
            telemetrySuffix: data.telemetrySuffix,
          },
        ],
      };
    })
  );
  await SearchService.init();

  let thirdPartyData = {
    baseURL: `${gHttpURL}/sjs/`,
    name: "Third Party",
    method: "GET",
  };
  thirdPartyEngine = await SearchTestUtils.installOpenSearchEngine({
    url: `${gHttpURL}/sjs/engineMaker.sjs?${JSON.stringify(thirdPartyData)}`,
  });

  getEngine = SearchService.getEngineById("get-engine");
  postEngine = SearchService.getEngineById("post-engine");
  unresolvableEngine = SearchService.getEngineById("offline-engine");
  alternateJSONEngine = SearchService.getEngineById("alternative-json-engine");

  registerCleanupFunction(async () => {
    // Remove added form history entries
    await updateSearchHistory("remove", null);
    Services.prefs.clearUserPref("browser.search.suggest.enabled");
  });
});

// Begin tests

add_task(async function simple_no_result_promise() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "no remote",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "no remote");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(getEngine, true);
});

add_task(async function simple_remote_no_local_result() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "Mozilla");
  Assert.equal(result.remote[1].value, "modern");
  Assert.equal(result.remote[2].value, "mom");

  assertLatencyCollection(getEngine, true);
});

add_task(async function simple_third_party_remote_no_local_result() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: thirdPartyEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "Mozilla");
  Assert.equal(result.remote[1].value, "modern");
  Assert.equal(result.remote[2].value, "mom");

  // Data is not recorded for third-party engines.
  assertLatencyCollection(thirdPartyEngine, false);
});

add_task(async function simple_remote_no_local_result_alternative_type() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: alternateJSONEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "Mozilla");
  Assert.equal(result.remote[1].value, "modern");
  Assert.equal(result.remote[2].value, "mom");
});

add_task(async function remote_term_case_mismatch() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "Query Case Mismatch",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "Query Case Mismatch");
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "Query Case Mismatch");
});

add_task(async function simple_local_no_remote_result() {
  await updateSearchHistory("bump", "no remote entries");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "no remote",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "no remote");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "no remote entries");
  Assert.equal(result.remote.length, 0);

  await updateSearchHistory("remove", "no remote entries");
});

add_task(async function simple_non_ascii() {
  await updateSearchHistory("bump", "I ❤️ XUL");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "I ❤️",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "I ❤️");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "I ❤️ XUL");
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "I ❤️ Mozilla");
});

add_task(async function both_local_remote_result_dedupe() {
  await updateSearchHistory("bump", "Mozilla");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "Mozilla");
  Assert.equal(result.remote.length, 2);
  Assert.equal(result.remote[0].value, "modern");
  Assert.equal(result.remote[1].value, "mom");
});

add_task(async function POST_both_local_remote_result_dedupe() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: postEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "Mozilla");
  Assert.equal(result.remote.length, 2);
  Assert.equal(result.remote[0].value, "modern");
  Assert.equal(result.remote[1].value, "mom");
});

add_task(async function both_local_remote_result_dedupe2() {
  await updateSearchHistory("bump", "mom");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 2);
  Assert.equal(result.local[0].value, "mom");
  Assert.equal(result.local[1].value, "Mozilla");
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "modern");
});

add_task(async function both_local_remote_result_dedupe3() {
  // All of the server entries also exist locally
  await updateSearchHistory("bump", "modern");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "mo");
  Assert.equal(result.local.length, 3);
  Assert.equal(result.local[0].value, "modern");
  Assert.equal(result.local[1].value, "mom");
  Assert.equal(result.local[2].value, "Mozilla");
  Assert.equal(result.remote.length, 0);
});

add_task(async function valid_tail_results() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "tail query",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "tail query");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "tail query normal");
  Assert.ok(!result.remote[0].matchPrefix);
  Assert.ok(!result.remote[0].tail);
  Assert.equal(result.remote[1].value, "tail query tail 1");
  Assert.equal(result.remote[1].matchPrefix, "… ");
  Assert.equal(result.remote[1].tail, "tail 1");
  Assert.equal(result.remote[2].value, "tail query tail 2");
  Assert.equal(result.remote[2].matchPrefix, "… ");
  Assert.equal(result.remote[2].tail, "tail 2");
});

add_task(async function alt_tail_results() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "tailalt query",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "tailalt query");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "tailalt query normal");
  Assert.ok(!result.remote[0].matchPrefix);
  Assert.ok(!result.remote[0].tail);
  Assert.equal(result.remote[1].value, "tailalt query tail 1");
  Assert.equal(result.remote[1].matchPrefix, "… ");
  Assert.equal(result.remote[1].tail, "tail 1");
  Assert.equal(result.remote[2].value, "tailalt query tail 2");
  Assert.equal(result.remote[2].matchPrefix, "… ");
  Assert.equal(result.remote[2].tail, "tail 2");
});

add_task(async function invalid_tail_results() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "tailjunk query",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "tailjunk query");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "tailjunk query normal");
  Assert.ok(!result.remote[0].matchPrefix);
  Assert.ok(!result.remote[0].tail);
  Assert.equal(result.remote[1].value, "tailjunk query tail 1");
  Assert.ok(!result.remote[1].matchPrefix);
  Assert.ok(!result.remote[1].tail);
  Assert.equal(result.remote[2].value, "tailjunk query tail 2");
  Assert.ok(!result.remote[2].matchPrefix);
  Assert.ok(!result.remote[2].tail);
});

add_task(async function too_few_tail_results() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "tailjunk few query",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "tailjunk few query");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "tailjunk few query normal");
  Assert.ok(!result.remote[0].matchPrefix);
  Assert.ok(!result.remote[0].tail);
  Assert.equal(result.remote[1].value, "tailjunk few query tail 1");
  Assert.ok(!result.remote[1].matchPrefix);
  Assert.ok(!result.remote[1].tail);
  Assert.equal(result.remote[2].value, "tailjunk few query tail 2");
  Assert.ok(!result.remote[2].matchPrefix);
  Assert.ok(!result.remote[2].tail);
});

add_task(async function empty_rich_results() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "richempty query",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "richempty query");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[0].value, "richempty query normal");
  Assert.ok(!result.remote[0].matchPrefix);
  Assert.ok(!result.remote[0].tail);
  Assert.equal(result.remote[1].value, "richempty query tail 1");
  Assert.ok(!result.remote[1].matchPrefix);
  Assert.ok(!result.remote[1].tail);
  Assert.equal(result.remote[2].value, "richempty query tail 2");
  Assert.ok(!result.remote[2].matchPrefix);
  Assert.ok(!result.remote[2].tail);
});

add_task(async function tail_offset_index() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "tail tail 1 t",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "tail tail 1 t");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 3);
  Assert.equal(result.remote[1].value, "tail tail 1 t tail 1");
  Assert.equal(result.remote[1].matchPrefix, "… ");
  Assert.equal(result.remote[1].tail, "tail 1");
  Assert.equal(result.remote[1].tailOffsetIndex, 14);
});

add_task(async function fetch_twice_in_a_row() {
  // The previous tests weren't testing telemetry, but this one is, so reset
  // it before use.
  Services.fog.testResetFOG();

  // The second fetch must complete with remote results, but the server delays
  // its response. Raise the remote timeout for this subtest so the response
  // can't be timed out under load. Cleared in the finally below so the
  // slow_timeout tests still use the default timeout.
  Services.prefs.setIntPref("browser.search.suggest.timeout", 5000);
  try {
    // Two entries since the first will match the first fetch but not the second.
    await updateSearchHistory("bump", "delay local");
    await updateSearchHistory("bump", "delayed local");

    let controller = new SearchSuggestionController();
    let resultPromise1 = controller.fetch({
      searchString: "delay",
      inPrivateBrowsing: false,
      engine: getEngine,
    });

    // A second fetch while the server is still waiting to return results leads to an abort.
    let resultPromise2 = controller.fetch({
      searchString: "delayed ",
      inPrivateBrowsing: false,
      engine: getEngine,
    });
    await resultPromise1.then(results => Assert.equal(null, results));

    let result = await resultPromise2;
    Assert.equal(result.term, "delayed ");
    Assert.equal(result.local.length, 1);
    Assert.equal(result.local[0].value, "delayed local");
    Assert.equal(result.remote.length, 1);
    Assert.equal(result.remote[0].value, "delayed ");

    // Only the second fetch's latency should be recorded since the first fetch
    // was aborted and latencies for aborted fetches are not recorded.
    assertLatencyCollection(getEngine, true);
  } finally {
    Services.prefs.clearUserPref("browser.search.suggest.timeout");
  }
});

add_task(async function both_identical_with_more_than_max_results() {
  // Add letters A through Z to form history which will match the server
  for (
    let charCode = "A".charCodeAt();
    charCode <= "Z".charCodeAt();
    charCode++
  ) {
    await updateSearchHistory(
      "bump",
      "letter " + String.fromCharCode(charCode)
    );
  }

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 7,
    maxRemoteResults: 10,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 7);
  for (let i = 0; i < 7; i++) {
    Assert.equal(
      result.local[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + i)
    );
  }
  Assert.equal(result.local.length + result.remote.length, 10);
  for (let i = 0; i < result.remote.length; i++) {
    Assert.equal(
      result.remote[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + 7 + i)
    );
  }
});

add_task(async function noremote_maxLocal() {
  // The previous tests weren't testing telemetry, but this one is, so reset
  // it before use.
  Services.fog.testResetFOG();

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 2, // (should be ignored because no remote results)
    maxRemoteResults: 0,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 26);
  for (let i = 0; i < result.local.length; i++) {
    Assert.equal(
      result.local[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + i)
    );
  }
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(getEngine, false);
});

add_task(async function someremote_maxLocal() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 2,
    maxRemoteResults: 4,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 2);
  for (let i = 0; i < result.local.length; i++) {
    Assert.equal(
      result.local[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + i)
    );
  }
  Assert.equal(result.remote.length, 2);
  // "A" and "B" will have been de-duped, start at C for remote results
  for (let i = 0; i < result.remote.length; i++) {
    Assert.equal(
      result.remote[i].value,
      "letter " + String.fromCharCode("C".charCodeAt() + i)
    );
  }

  assertLatencyCollection(getEngine, true);
});

add_task(async function one_of_each() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 1,
    maxRemoteResults: 2,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "letter A");
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "letter B");
});

add_task(async function local_result_returned_remote_result_disabled() {
  // The previous tests weren't testing telemetry, but this one is, so reset
  // it before use.
  Services.fog.testResetFOG();

  Services.prefs.setBoolPref("browser.search.suggest.enabled", false);
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 1,
    maxRemoteResults: 1,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 26);
  for (let i = 0; i < 26; i++) {
    Assert.equal(
      result.local[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + i)
    );
  }
  Assert.equal(result.remote.length, 0);
  assertLatencyCollection(getEngine, false);
  Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
});

add_task(
  async function local_result_returned_remote_result_disabled_after_creation_of_controller() {
    let controller = new SearchSuggestionController();
    Services.prefs.setBoolPref("browser.search.suggest.enabled", false);
    let result = await controller.fetch({
      searchString: "letter ",
      inPrivateBrowsing: false,
      engine: getEngine,
      maxLocalResults: 1,
      maxRemoteResults: 1,
    });
    Assert.equal(result.term, "letter ");
    Assert.equal(result.local.length, 26);
    for (let i = 0; i < 26; i++) {
      Assert.equal(
        result.local[i].value,
        "letter " + String.fromCharCode("A".charCodeAt() + i)
      );
    }
    Assert.equal(result.remote.length, 0);
    assertLatencyCollection(getEngine, false);
    Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
  }
);

add_task(
  async function one_of_each_disabled_before_creation_enabled_after_creation_of_controller() {
    Services.prefs.setBoolPref("browser.search.suggest.enabled", false);
    let controller = new SearchSuggestionController();
    Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
    let result = await controller.fetch({
      searchString: "letter ",
      inPrivateBrowsing: false,
      engine: getEngine,
      maxLocalResults: 1,
      maxRemoteResults: 2,
    });
    Assert.equal(result.term, "letter ");
    Assert.equal(result.local.length, 1);
    Assert.equal(result.local[0].value, "letter A");
    Assert.equal(result.remote.length, 1);
    Assert.equal(result.remote[0].value, "letter B");

    assertLatencyCollection(getEngine, true);

    Services.prefs.setBoolPref("browser.search.suggest.enabled", true);
  }
);

add_task(async function one_local_zero_remote() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 1,
    maxRemoteResults: 0,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 26);
  for (let i = 0; i < 26; i++) {
    Assert.equal(
      result.local[i].value,
      "letter " + String.fromCharCode("A".charCodeAt() + i)
    );
  }
  Assert.equal(result.remote.length, 0);
  assertLatencyCollection(getEngine, false);
});

add_task(async function zero_local_one_remote() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "letter ",
    inPrivateBrowsing: false,
    engine: getEngine,
    maxLocalResults: 0,
    maxRemoteResults: 1,
  });
  Assert.equal(result.term, "letter ");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "letter A");
  assertLatencyCollection(getEngine, true);
});

add_task(async function stop_search() {
  let controller = new SearchSuggestionController();
  let resultPromise = controller.fetch({
    searchString: "mo",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  controller.stop();
  await resultPromise.then(result => {
    Assert.equal(null, result);
  });
  assertLatencyCollection(getEngine, false);
});

add_task(async function empty_searchTerm() {
  // Empty searches don't go to the server but still get form history.
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "");
  Assert.ok(!!result.local.length);
  Assert.equal(result.remote.length, 0);
  assertLatencyCollection(getEngine, false);
});

add_task(async function slow_timeout() {
  // Make the server return suggestions on a delay longer than the timeout of
  // the suggestion controller.
  let delayMs = 3 * SearchSuggestionController.REMOTE_TIMEOUT_DEFAULT;
  let searchString = `delay${delayMs} `;

  // Add a local result.
  let localValue = searchString + " local result";
  await updateSearchHistory("bump", localValue);

  // Do a search. The remote fetch should time out but the local result should
  // be returned.
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString,
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, searchString);
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, localValue);
  Assert.equal(result.remote.length, 0);

  // The remote fetch isn't done yet, so the latency histogram should not be
  // updated.
  assertLatencyCollection(getEngine, false);

  // Wait for the remote fetch to finish and record its latency.
  await TestUtils.waitForCondition(
    () => Glean.searchSuggestions.latency[getEngine.id].testGetValue() != null,
    "Waiting for the remote fetch latency to be recorded"
  );

  // Now the latency histogram should be updated.
  assertLatencyCollection(getEngine, true);
});

add_task(async function slow_timeout_2() {
  // Make the server return suggestions on a delay longer the timeout of the
  // suggestion controller.
  let delayMs = 3 * SearchSuggestionController.REMOTE_TIMEOUT_DEFAULT;
  let searchString = `delay${delayMs} `;

  // Add a local result.
  let localValue = searchString + " local result";
  await updateSearchHistory("bump", localValue);

  // Do two searches using the same controller. Both times, the remote fetches
  // should time out and only the local result should be returned. The second
  // search should abort the remote fetch of the first search, and the remote
  // fetch of the second search should be ongoing when the second search
  // finishes.
  let controller = new SearchSuggestionController();
  for (let i = 0; i < 2; i++) {
    let result = await controller.fetch({
      searchString,
      inPrivateBrowsing: false,
      engine: getEngine,
    });
    Assert.equal(result.term, searchString);
    Assert.equal(result.local.length, 1);
    Assert.equal(result.local[0].value, localValue);
    Assert.equal(result.remote.length, 0);
  }

  // The remote fetch of the second search isn't done yet, so the latency
  // histogram should not be updated.
  assertLatencyCollection(getEngine, false);

  // Wait for the second remote fetch to finish and record its latency.
  await TestUtils.waitForCondition(
    () => Glean.searchSuggestions.latency[getEngine.id].testGetValue() != null,
    "Waiting for the remote fetch latency to be recorded"
  );

  // Now the latency histogram should be updated, and only the remote fetch of
  // the second search should be recorded.
  assertLatencyCollection(getEngine, true);
});

add_task(async function slow_stop() {
  // Make the server return suggestions on a delay longer the timeout of the
  // suggestion controller.
  let delayMs = 3 * SearchSuggestionController.REMOTE_TIMEOUT_DEFAULT;
  let searchString = `delay${delayMs} `;

  // Do a search but stop it before it finishes. Wait a tick before stopping it
  // to better simulate the real world.
  let controller = new SearchSuggestionController();
  let resultPromise = controller.fetch({
    searchString,
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  await TestUtils.waitForTick();
  controller.stop();
  let result = await resultPromise;
  Assert.equal(result, null, "No result should be returned");

  // The remote fetch should have been aborted by stopping the controller, but
  // wait for the timeout period just to make sure it's done.
  await new Promise(r => setTimeout(r, delayMs));

  // Since the latencies of aborted fetches are not recorded, the latency
  // histogram should not be updated.
  assertLatencyCollection(getEngine, false);
});

// Error handling

add_task(async function remote_term_mismatch() {
  await updateSearchHistory("bump", "Query Mismatch Entry");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "Query Mismatch",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "Query Mismatch");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "Query Mismatch Entry");
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(getEngine, true);
});

add_task(async function http_404() {
  await updateSearchHistory("bump", "HTTP 404 Entry");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "HTTP 404",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "HTTP 404");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "HTTP 404 Entry");
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(getEngine, true);
});

add_task(async function http_500() {
  await updateSearchHistory("bump", "HTTP 500 Entry");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "HTTP 500",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "HTTP 500");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "HTTP 500 Entry");
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(getEngine, true);
});

add_task(async function invalid_response_does_not_throw() {
  let controller = new SearchSuggestionController();
  // Although the server will return invalid json, the error is handled by
  // the suggestion controller, and so we receive no results.
  let result = await controller.fetch({
    searchString: "invalidJSON",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "invalidJSON");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 0);
});

add_task(async function invalid_content_type_treated_as_json() {
  let controller = new SearchSuggestionController();
  // An invalid content type is overridden as we expect all the responses to
  // be JSON.
  let result = await controller.fetch({
    searchString: "invalidContentType",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "invalidContentType");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 1);
  Assert.equal(result.remote[0].value, "invalidContentType response");
});

add_task(async function unresolvable_server() {
  // The previous tests weren't testing telemetry, but this one is, so reset
  // it before use.
  Services.fog.testResetFOG();

  await updateSearchHistory("bump", "Unresolvable Server Entry");

  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "Unresolvable Server",
    inPrivateBrowsing: false,
    engine: unresolvableEngine,
  });
  Assert.equal(result.term, "Unresolvable Server");
  Assert.equal(result.local.length, 1);
  Assert.equal(result.local[0].value, "Unresolvable Server Entry");
  Assert.equal(result.remote.length, 0);

  assertLatencyCollection(unresolvableEngine, true);
});

// Exception handling

add_task(async function missing_pb() {
  Assert.throws(() => {
    let controller = new SearchSuggestionController();
    controller.fetch({ searchString: "No privacy" });
  }, /priva/i);
});

add_task(async function missing_engine() {
  Assert.throws(() => {
    let controller = new SearchSuggestionController();
    controller.fetch({ searchString: "No engine", inPrivateBrowsing: false });
  }, /engine/i);
});

add_task(async function invalid_engine() {
  Assert.throws(() => {
    let controller = new SearchSuggestionController();
    controller.fetch({
      searchString: "invalid engine",
      inPrivateBrowsing: false,
      engine: {},
    });
  }, /engine/i);
});

add_task(async function no_results_requested() {
  Assert.throws(() => {
    let controller = new SearchSuggestionController();
    controller.fetch({
      searchString: "No results requested",
      inPrivateBrowsing: false,
      engine: getEngine,
      maxLocalResults: 0,
      maxRemoteResults: 0,
    });
  }, /result/i);
});

add_task(async function minus_one_results_requested() {
  Assert.throws(() => {
    let controller = new SearchSuggestionController();
    controller.fetch({
      searchString: "-1 results requested",
      inPrivateBrowsing: false,
      engine: getEngine,
      maxLocalResults: -1,
    });
  }, /result/i);
});

add_task(async function test_userContextId() {
  let controller = new SearchSuggestionController();
  controller._fetchRemote = function (
    searchTerm,
    engine,
    inPrivateBrowsing,
    userContextId
  ) {
    Assert.equal(userContextId, 1);
    return Promise.withResolvers();
  };

  controller.fetch({
    searchString: "test",
    inPrivateBrowsing: false,
    engine: getEngine,
    userContextId: 1,
  });
});

// Non-English characters

add_task(async function suggestions_contain_escaped_unicode() {
  let controller = new SearchSuggestionController();
  let result = await controller.fetch({
    searchString: "stü",
    inPrivateBrowsing: false,
    engine: getEngine,
  });
  Assert.equal(result.term, "stü");
  Assert.equal(result.local.length, 0);
  Assert.equal(result.remote.length, 2);
  Assert.equal(result.remote[0].value, "stühle");
  Assert.equal(result.remote[1].value, "stüssy");
});

// Helpers

function updateSearchHistory(operation, value) {
  return FormHistory.update({
    op: operation,
    fieldname: "searchbar-history",
    value,
  });
}

function assertLatencyCollection(engine, shouldRecord) {
  Assert.ok(
    engine instanceof ConfigSearchEngine || !shouldRecord,
    "shouldRecord should only be true for configuration search engines"
  );

  if (shouldRecord) {
    Assert.equal(
      Glean.searchSuggestions.latency[engine.id].testGetValue().count,
      1,
      "Should have recorded a latency count"
    );
  } else {
    Assert.equal(
      Glean.searchSuggestions.latency[engine.id].testGetValue(),
      null,
      "Should not have recorded a latency count"
    );
  }

  Services.fog.testResetFOG();
}
