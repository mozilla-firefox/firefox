/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

const { searchBrowsingHistory } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/SearchBrowsingHistory.sys.mjs"
);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { sanitizeUntrustedContent } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs"
);

const { getPlacesSemanticHistoryManager } = ChromeUtils.importESModule(
  "resource://gre/modules/PlacesSemanticHistoryManager.sys.mjs"
);

let sb;

// setup
add_task(async function setup() {
  sb = sinon.createSandbox();
  registerCleanupFunction(() => {
    sb.restore();
    Services.prefs.clearUserPref("browser.ml.enable");
    Services.prefs.clearUserPref("places.semanticHistory.featureGate");
    Services.prefs.clearUserPref(
      "places.semanticHistory.smartwindow.featureGate"
    );
    Services.prefs.clearUserPref("browser.search.region");
  });

  Services.prefs.setBoolPref("browser.ml.enable", true);
  Services.prefs.setBoolPref("places.semanticHistory.featureGate", true);
  Services.prefs.setBoolPref(
    "places.semanticHistory.smartwindow.featureGate",
    true
  );
  Services.prefs.setCharPref("browser.search.region", "US");

  await PlacesUtils.history.clear();
});

// test: empty searchTerm, no time window
add_task(async function test_basic_history_fetch_and_shape() {
  await PlacesUtils.history.clear();

  const now = Date.now();

  const seeded = [
    {
      url: "https://www.google.com/search?q=firefox+history",
      title: "Google Search: firefox history",
      visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
    },
    {
      url: "https://developer.mozilla.org/en-US/docs/Web/JavaScript",
      title: "JavaScript | MDN",
      visits: [{ date: new Date(now - 10 * 60 * 1000) }], // 10 min ago
    },
    {
      url: "https://news.ycombinator.com/",
      title: "Hacker News",
      visits: [{ date: new Date(now - 15 * 60 * 1000) }],
    },
    {
      url: "https://search.brave.com/search?q=mozsqlite",
      title: "Brave Search: mozsqlite",
      visits: [{ date: new Date(now - 20 * 60 * 1000) }],
    },
    {
      url: "https://mozilla.org/en-US/",
      title: "Internet for people, not profit — Mozilla",
      visits: [{ date: new Date(now - 25 * 60 * 1000) }],
    },
  ];

  await PlacesUtils.history.insertMany(seeded);

  const allRowsObj = await searchBrowsingHistory({
    searchTerm: "",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  // check count match
  Assert.equal(
    allRowsObj.count,
    seeded.length,
    "Should return all seeded records"
  );

  // check all url match
  const urls = allRowsObj.results.map(r => r.url).sort();
  const expectedUrls = seeded.map(s => s.url).sort();
  Assert.deepEqual(urls, expectedUrls, "Should return all seeded URLs");

  // check title and url match
  const byUrl = new Map(allRowsObj.results.map(r => [r.url, r]));
  for (const { url, title } of seeded) {
    Assert.ok(byUrl.has(url), `Has entry for ${url}`);
    Assert.equal(
      byUrl.get(url).title,
      sanitizeUntrustedContent(title),
      `Title matches for ${url}`
    );
  }

  // check visitDate iso string
  for (const r of allRowsObj.results) {
    Assert.ok(
      !isNaN(Date.parse(r.visitDate)),
      "visitDate is a valid ISO timestamp"
    );
  }
});

// test: startTs only
add_task(async function test_time_range_only_startTs() {
  await PlacesUtils.history.clear();

  const now = Date.now();

  const older = {
    url: "https://example.com/older",
    title: "Older Page",
    visits: [{ date: new Date(now - 60 * 60 * 1000) }], // 60 min ago
  };
  const recent = {
    url: "https://example.com/recent",
    title: "Recent Page",
    visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
  };

  await PlacesUtils.history.insertMany([older, recent]);

  // records after last 10 minutes
  const startTs = new Date(now - 10 * 60 * 1000).toISOString(); // ISO input

  const rows = await searchBrowsingHistory({
    searchTerm: "",
    startTs,
    endTs: null,
    historyLimit: 15,
  });
  const urls = rows.results.map(r => r.url);

  Assert.ok(
    urls.includes(recent.url),
    "Recent entry should be included when only startTs is set"
  );
  Assert.ok(
    !urls.includes(older.url),
    "Older entry should be excluded when only startTs is set"
  );
});

// test: endTs only
add_task(async function test_time_range_only_endTs() {
  await PlacesUtils.history.clear();

  const now = Date.now();

  const older = {
    url: "https://example.com/older",
    title: "Older Page",
    visits: [{ date: new Date(now - 60 * 60 * 1000) }], // 60 min ago
  };
  const recent = {
    url: "https://example.com/recent",
    title: "Recent Page",
    visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
  };

  await PlacesUtils.history.insertMany([older, recent]);

  // Anything before last 10 minutes
  const endTs = new Date(now - 10 * 60 * 1000).toISOString(); // ISO input

  const rows = await searchBrowsingHistory({
    searchTerm: "",
    startTs: null,
    endTs,
    historyLimit: 15,
  });
  const urls = rows.results.map(r => r.url);

  Assert.ok(
    urls.includes(older.url),
    "Older entry should be included when only endTs is set"
  );
  Assert.ok(
    !urls.includes(recent.url),
    "Recent entry should be excluded when only endTs is set"
  );
});

// test: startTs + endTs
add_task(async function test_time_range_start_and_endTs() {
  await PlacesUtils.history.clear();

  const now = Date.now();

  const beforeWindow = {
    url: "https://example.com/before-window",
    title: "Before Window",
    visits: [{ date: new Date(now - 3 * 60 * 60 * 1000) }], // 3h ago
  };
  const inWindow = {
    url: "https://example.com/in-window",
    title: "In Window",
    visits: [{ date: new Date(now - 30 * 60 * 1000) }], // 30 min ago
  };
  const afterWindow = {
    url: "https://example.com/after-window",
    title: "After Window",
    visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
  };

  await PlacesUtils.history.insertMany([beforeWindow, inWindow, afterWindow]);

  // Time window: [45min ago,  15min ago]
  const startTs = new Date(now - 45 * 60 * 1000).toISOString();
  const endTs = new Date(now - 15 * 60 * 1000).toISOString();

  const rows = await searchBrowsingHistory({
    searchTerm: "",
    startTs,
    endTs,
    historyLimit: 15,
  });
  const urls = rows.results.map(r => r.url);

  Assert.ok(urls.includes(inWindow.url), "In window entry should be included");
  Assert.ok(
    !urls.includes(beforeWindow.url),
    "Before window entry should be excluded"
  );
  Assert.ok(
    !urls.includes(afterWindow.url),
    "After window entry should be excluded"
  );
});

/**
 * Test no results behavior: empty history with and without searchTerm.
 *
 * We don't try to force the semantic here (that would require a
 * running ML engine). Instead we just assert the wrapper's messaging
 * when there are no rows.
 */
add_task(async function test_no_results_messages() {
  await PlacesUtils.history.clear();

  // No search term: time range message.
  let output = await searchBrowsingHistory({
    searchTerm: "",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  Assert.equal(output.results.length, 0, "No results when history is empty");
  Assert.ok(
    output.message.includes("requested time range"),
    "Message explains empty time-range search"
  );

  // With search term: search specific message.
  output = await searchBrowsingHistory({
    searchTerm: "mozilla",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  Assert.equal(output.results.length, 0, "No results for semantic search");
  Assert.ok(
    output.message.includes("mozilla"),
    "Message mentions the search term when there are no matches"
  );
});

// test: non-empty searchTerm falls back to basic history search
// when semantic search is disabled via prefs.
add_task(async function test_basic_text_search_when_semantic_disabled() {
  await PlacesUtils.history.clear();

  const now = Date.now();

  const seeded = [
    {
      url: "https://www.mozilla.org/en-US/",
      title: "Internet for people, not profit — Mozilla",
      visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
    },
    {
      url: "https://example.com/other",
      title: "Some Other Site",
      visits: [{ date: new Date(now - 10 * 60 * 1000) }], // 10 min ago
    },
  ];

  await PlacesUtils.history.insertMany(seeded);

  // Disable semantic search so searchBrowsingHistory must fall back
  // to the basic Places history search.
  Services.prefs.setBoolPref("browser.ml.enable", false);
  Services.prefs.setBoolPref("places.semanticHistory.featureGate", false);

  const output = await searchBrowsingHistory({
    searchTerm: "mozilla",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  Assert.equal(output.searchTerm, "mozilla", "searchTerm match");
  Assert.equal(output.results.length, 1, "One history entry is returned");

  const urls = output.results.map(r => r.url);
  Assert.ok(
    urls.includes("https://www.mozilla.org/en-US/"),
    "Basic Places history search should find the Mozilla entry"
  );

  // Restore prefs
  Services.prefs.setBoolPref("browser.ml.enable", true);
  Services.prefs.setBoolPref("places.semanticHistory.featureGate", true);
});

// test: non-empty searchTerm falls back to basic history search
// and still respects the time range when semantic search is disabled.
add_task(
  async function test_basic_text_search_with_time_range_when_semantic_disabled() {
    await PlacesUtils.history.clear();

    const now = Date.now();

    const olderMozilla = {
      url: "https://example.com/mozilla-older",
      title: "Mozilla Older Result",
      visits: [{ date: new Date(now - 60 * 60 * 1000) }], // 60 min ago
    };

    const recentMozilla = {
      url: "https://example.com/mozilla-recent",
      title: "Mozilla Recent Result",
      visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
    };

    const unrelatedRecent = {
      url: "https://example.com/unrelated",
      title: "Unrelated Result",
      visits: [{ date: new Date(now - 3 * 60 * 1000) }], // 3 min ago
    };

    await PlacesUtils.history.insertMany([
      olderMozilla,
      recentMozilla,
      unrelatedRecent,
    ]);

    // Disable semantic search so searchBrowsingHistory must fall back
    // to the basic Places history search.
    Services.prefs.setBoolPref("browser.ml.enable", false);
    Services.prefs.setBoolPref("places.semanticHistory.featureGate", false);

    // Only include results from the last 10 minutes.
    const startTs = new Date(now - 10 * 60 * 1000).toISOString();

    const output = await searchBrowsingHistory({
      searchTerm: "mozilla",
      startTs,
      endTs: null,
      historyLimit: 15,
    });
    const urls = output.results.map(r => r.url);

    Assert.ok(
      urls.includes(recentMozilla.url),
      "Recent matching entry should be included in basic fallback search"
    );
    Assert.ok(
      !urls.includes(olderMozilla.url),
      "Older matching entry should be excluded by the time filter in basic fallback search"
    );
    Assert.ok(
      !urls.includes(unrelatedRecent.url),
      "Non-matching entry should not be returned even if it is in the time window"
    );

    // Restore prefs
    Services.prefs.setBoolPref("browser.ml.enable", true);
    Services.prefs.setBoolPref("places.semanticHistory.featureGate", true);
  }
);

// test: basic fallback search respects endTs when semantic search is disabled.
add_task(
  async function test_basic_text_search_with_endTs_when_semantic_disabled() {
    await PlacesUtils.history.clear();

    const now = Date.now();

    const olderMozilla = {
      url: "https://example.com/mozilla-older-endts",
      title: "Mozilla Older EndTs Result",
      visits: [{ date: new Date(now - 60 * 60 * 1000) }], // 60 min ago
    };

    const recentMozilla = {
      url: "https://example.com/mozilla-recent-endts",
      title: "Mozilla Recent EndTs Result",
      visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
    };

    await PlacesUtils.history.insertMany([olderMozilla, recentMozilla]);

    Services.prefs.setBoolPref("browser.ml.enable", false);
    Services.prefs.setBoolPref("places.semanticHistory.featureGate", false);

    // Only include results before the last 10 minutes.
    const endTs = new Date(now - 10 * 60 * 1000).toISOString();

    const output = await searchBrowsingHistory({
      searchTerm: "mozilla",
      startTs: null,
      endTs,
      historyLimit: 15,
    });
    const urls = output.results.map(r => r.url);

    Assert.ok(
      urls.includes(olderMozilla.url),
      "Older matching entry should be included in basic fallback search"
    );
    Assert.ok(
      !urls.includes(recentMozilla.url),
      "Recent matching entry should be excluded by endTs in basic fallback search"
    );

    Services.prefs.setBoolPref("browser.ml.enable", true);
    Services.prefs.setBoolPref("places.semanticHistory.featureGate", true);
  }
);

// test: region = RO (not supported in semantic search), time range search should still work
add_task(async function test_time_range_search_with_region_ro() {
  await PlacesUtils.history.clear();

  // Switch region to RO for this test.
  Services.prefs.setCharPref("browser.search.region", "RO");

  const now = Date.now();

  const older = {
    url: "https://example.com/older-ro",
    title: "Older RO Page",
    visits: [{ date: new Date(now - 60 * 60 * 1000) }], // 60 min ago
  };
  const inWindow = {
    url: "https://example.com/in-window-ro",
    title: "In Window RO Page",
    visits: [{ date: new Date(now - 30 * 60 * 1000) }], // 30 min ago
  };
  const recent = {
    url: "https://example.com/recent-ro",
    title: "Recent RO Page",
    visits: [{ date: new Date(now - 5 * 60 * 1000) }], // 5 min ago
  };

  await PlacesUtils.history.insertMany([older, inWindow, recent]);

  // Time window: [45min ago, 15min ago]
  const startTs = new Date(now - 45 * 60 * 1000).toISOString();
  const endTs = new Date(now - 15 * 60 * 1000).toISOString();

  const rows = await searchBrowsingHistory({
    searchTerm: "",
    startTs,
    endTs,
    historyLimit: 15,
  });
  const urls = rows.results.map(r => r.url);

  Assert.ok(
    urls.includes(inWindow.url),
    "In window entry should be included when region is RO"
  );
  Assert.ok(
    !urls.includes(older.url),
    "Older entry should be excluded when region is RO"
  );
  Assert.ok(
    !urls.includes(recent.url),
    "Recent entry should be excluded when region is RO"
  );

  // Restore region pref for other tests.
  Services.prefs.setCharPref("browser.search.region", "US");
});

add_task(async function test_hybrid_search_path() {
  await PlacesUtils.history.clear();
  sb.restore();

  const now = Date.now();

  // Fake Places SQL rows.
  await PlacesUtils.history.insertMany([
    {
      url: "https://example.com/mozilla",
      title: "Mozilla From Places",
      visits: [{ date: new Date(now - 5 * 60 * 1000) }],
    },
    {
      url: "https://example.com/firefox",
      title: "Firefox From Places",
      visits: [{ date: new Date(now - 10 * 60 * 1000) }],
    },
  ]);

  const semanticManager = getPlacesSemanticHistoryManager();

  sb.stub(semanticManager, "hasSufficientEntriesForSearching").resolves(true);
  sb.stub(semanticManager, "isEnabledForSmartWindow").value(true);

  sb.stub(semanticManager.embedder, "ensureEngine").resolves();
  sb.stub(semanticManager.embedder, "embed").resolves({
    output: [[0.1, 0.2, 0.3]],
  });

  // Fake semantic SQL rows.
  sb.stub(semanticManager, "getConnection").resolves({
    executeCached: async () => [
      {
        getResultByName(name) {
          const row = {
            title: "Mozilla From Semantic",
            url: "https://example.com/mozilla",
            visit_count: 1,
            distance: 0.1,
            frecency: 10,
            last_visit_date: (now - 60 * 60 * 1000) * 1000,
          };
          return row[name];
        },
      },
      {
        getResultByName(name) {
          const row = {
            title: "Semantic Only",
            url: "https://example.com/semantic-only",
            visit_count: 1,
            distance: 0.2,
            frecency: 5,
            last_visit_date: (now - 30 * 60 * 1000) * 1000,
          };
          return row[name];
        },
      },
    ],
  });

  const output = await searchBrowsingHistory({
    searchTerm: "mozilla",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  Assert.greater(output.results.length, 0, "Hybrid path should return results");

  const byUrl = new Map(output.results.map(r => [r.url, r]));

  Assert.ok(
    byUrl.has("https://example.com/mozilla"),
    "Shared semantic + Places URL should be returned"
  );
  Assert.ok(
    byUrl.has("https://example.com/semantic-only"),
    "Semantic-only URL should be returned"
  );

  Assert.equal(
    byUrl.get("https://example.com/mozilla").title,
    sanitizeUntrustedContent("Mozilla From Places"),
    "Places metadata should win in hybrid merge"
  );
});

add_task(async function test_hybrid_search_rrf_ranking_prefers_shared_result() {
  await PlacesUtils.history.clear();
  sb.restore();

  const now = Date.now();

  const siteA = {
    url: "https://example.com/site-a",
    title: "Site A From Places",
    visits: [{ date: new Date(now - 5 * 60 * 1000) }], // newer
  };

  const siteB = {
    url: "https://example.com/site-b",
    title: "Site B From Places",
    visits: [{ date: new Date(now - 10 * 60 * 1000) }],
  };

  await PlacesUtils.history.insertMany([siteA, siteB]);

  const semanticManager = getPlacesSemanticHistoryManager();

  sb.stub(semanticManager, "hasSufficientEntriesForSearching").resolves(true);
  sb.stub(semanticManager, "isEnabledForSmartWindow").value(true);

  sb.stub(semanticManager.embedder, "ensureEngine").resolves();
  sb.stub(semanticManager.embedder, "embed").resolves({
    output: [[0.1, 0.2, 0.3]],
  });

  sb.stub(semanticManager, "getConnection").resolves({
    executeCached: async () => [
      {
        getResultByName(name) {
          const row = {
            id: 1,
            title: "Site B From Semantic",
            url: siteB.url,
            distance: 0.05,
            visit_count: 1,
            frecency: 100,
            last_visit_date: (now - 60 * 60 * 1000) * 1000,
          };
          return row[name];
        },
      },
    ],
  });

  const output = await searchBrowsingHistory({
    searchTerm: "site",
    startTs: null,
    endTs: null,
    historyLimit: 15,
  });

  Assert.greaterOrEqual(
    output.results.length,
    2,
    "Hybrid search should return both rows"
  );

  //   Places: site A rank 1, site B rank 2.
  // Semantic: site B rank 1.
  // After RRF fusion, site B should rank first because it appears in both lists.
  Assert.equal(
    output.results[0].url,
    siteB.url,
    "Site B should rank first after RRF because it appears in both lists"
  );
  Assert.equal(
    output.results[1].url,
    siteA.url,
    "Site A should rank after site B because it only appears in Places results"
  );

  // Shared URL should still prefer Places metadata.
  Assert.equal(
    output.results[0].title,
    sanitizeUntrustedContent(siteB.title),
    "Hybrid merge should prefer Places metadata for the shared URL"
  );
});
