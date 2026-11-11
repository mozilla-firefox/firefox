/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  ADLINK_CHECK_TIMEOUT_MS:
    "resource:///actors/SearchSERPTelemetryChild.sys.mjs",
  BrowserSearchTelemetry:
    "moz-src:///browser/components/search/BrowserSearchTelemetry.sys.mjs",
  CATEGORIZATION_SETTINGS:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  CustomizableUITestUtils:
    "resource://testing-common/CustomizableUITestUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
  SEARCH_TELEMETRY_SHARED:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SearchSERPTelemetry:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SearchSERPTelemetryUtils:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  SearchTestUtils: "resource://testing-common/SearchTestUtils.sys.mjs",
  SearchUITestUtils: "resource://testing-common/SearchUITestUtils.sys.mjs",
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
  SERPCategorizationRecorder:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  SERPDomainToCategoriesMap:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  SPA_ADLINK_CHECK_TIMEOUT_MS:
    "moz-src:///browser/components/search/SearchSERPTelemetry.sys.mjs",
  TELEMETRY_CATEGORIZATION_KEY:
    "moz-src:///browser/components/search/SERPCategorization.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  VISIBILITY_THRESHOLD: "resource:///actors/SearchSERPTelemetryChild.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "SearchbarTestUtils", () => {
  const { SearchbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

ChromeUtils.defineLazyGetter(this, "searchCounts", () => {
  return Services.telemetry.getKeyedHistogramById("SEARCH_COUNTS");
});

ChromeUtils.defineLazyGetter(this, "SEARCH_AD_CLICK_SCALARS", () => {
  const sources = [
    ...Object.keys(BrowserSearchTelemetry.KNOWN_SEARCH_SOURCES),
    "unknown",
  ];
  return [
    ...sources.map(v => `browser.search.withads.${v}`),
    ...sources.map(v => `browser.search.adclicks.${v}`),
  ];
});

ChromeUtils.defineLazyGetter(this, "gCryptoHash", () => {
  return Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
});

// For use with categorization.
const APP_MAJOR_VERSION = parseInt(Services.appinfo.version).toString();
const CHANNEL = SearchUtils.MODIFIED_APP_CHANNEL;
const REGION = Region.home;

let gCUITestUtils = new CustomizableUITestUtils(window);

SearchTestUtils.init(this);
SearchUITestUtils.init(this);

const UUID_REGEX =
  /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function sleep(ms) {
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  return new Promise(resolve => setTimeout(resolve, ms));
}

// sharedData messages are only passed to the child on idle. Therefore
// we wait for a few idles to try and ensure the messages have been able
// to be passed across and handled.
async function waitForIdle() {
  for (let i = 0; i < 10; i++) {
    await new Promise(resolve => Services.tm.idleDispatchToMainThread(resolve));
  }
}

function getPageUrl(useAdPage = false) {
  let page = useAdPage ? "searchTelemetryAd.html" : "searchTelemetry.html";
  return `https://example.org/browser/browser/components/search/test/browser/telemetry/${page}`;
}

function getSERPUrl(page, organic = false) {
  let url =
    getRootDirectory(gTestPath).replace(
      "chrome://mochitests/content",
      "https://example.org"
    ) + page;
  return `${url}?s=test${organic ? "" : "&abc=ff"}`;
}

async function typeInSearchField(browser, text, fieldName) {
  await SpecialPowers.spawn(
    browser,
    [[fieldName, text]],
    async function ([contentFieldName, contentText]) {
      // Put the focus on the search box.
      let searchInput = content.document.getElementById(contentFieldName);
      searchInput.focus();
      searchInput.value = contentText;
    }
  );
}

async function searchInSearchbar(inputText, win = window) {
  await new Promise(r => waitForFocus(r, win));
  let sb = win.document.getElementById("searchbar");
  // Write the search query in the searchbar.
  sb.focus();
  sb.value = inputText;
  sb.textbox.controller.startSearch(inputText);
  // Wait for the popup to show.
  await BrowserTestUtils.waitForEvent(sb.textbox.popup, "popupshown");
  // And then for the search to complete.
  await TestUtils.waitForCondition(
    () =>
      sb.textbox.controller.searchStatus >=
      Ci.nsIAutoCompleteController.STATUS_COMPLETE_NO_MATCH,
    "The search in the searchbar must complete."
  );
  return sb.textbox.popup;
}

// Ad links are processed after a small delay. We need to allow tests to wait
// for that before checking telemetry, otherwise the received values may be
// too small in some cases.
function promiseWaitForAdLinkCheck() {
  return new Promise(resolve =>
    /* eslint-disable-next-line mozilla/no-arbitrary-setTimeout */
    setTimeout(resolve, ADLINK_CHECK_TIMEOUT_MS)
  );
}

async function assertSearchSourcesTelemetry(
  expectedHistograms,
  expectedScalars
) {
  let histSnapshot = {};
  let scalars = {};

  // This used to rely on the implied 100ms initial timer of
  // TestUtils.waitForCondition. See bug 1515466.
  await new Promise(resolve => setTimeout(resolve, 100));

  await TestUtils.waitForCondition(() => {
    histSnapshot = searchCounts.snapshot();
    return (
      Object.getOwnPropertyNames(histSnapshot).length ==
      Object.getOwnPropertyNames(expectedHistograms).length
    );
  }, "should have the correct number of histograms");

  if (Object.entries(expectedScalars).length) {
    await TestUtils.waitForCondition(() => {
      scalars =
        Services.telemetry.getSnapshotForKeyedScalars("main", false).parent ||
        {};
      return Object.getOwnPropertyNames(expectedScalars).every(
        scalar => scalar in scalars
      );
    }, "should have the expected keyed scalars");
  }

  Assert.equal(
    Object.getOwnPropertyNames(histSnapshot).length,
    Object.getOwnPropertyNames(expectedHistograms).length,
    "Should only have one key"
  );

  for (let [key, value] of Object.entries(expectedHistograms)) {
    Assert.ok(
      key in histSnapshot,
      `Histogram should have the expected key: ${key}`
    );
    Assert.equal(
      histSnapshot[key].sum,
      value,
      `Should have counted the correct number of visits for ${key}`
    );
  }

  for (let [name, value] of Object.entries(expectedScalars)) {
    Assert.ok(name in scalars, `Scalar ${name} should have been added.`);
    Assert.deepEqual(
      scalars[name],
      value,
      `Should have counted the correct number of visits for ${name}`
    );
  }

  for (let name of SEARCH_AD_CLICK_SCALARS) {
    Assert.equal(
      name in scalars,
      name in expectedScalars,
      `Should have matched ${name} in scalars and expectedScalars`
    );
  }
}

function resetTelemetry() {
  searchCounts.clear();
  Services.telemetry.clearScalars();
  Services.fog.testResetFOG();
  SERPCategorizationRecorder.testReset();
}

const DEFAULT_IMPRESSION = {
  provider: "example",
  tagged: "true",
  partner_code: "ff",
  source: "unknown",
  is_private: "false",
  is_signed_in: "false",
  is_shopping_page: "false",
  shopping_tab_displayed: "false",
  has_ai_summary: "false",
  prescan: "none_found",
  scan: "complete",
};

/**
 * First checks that we get the correct number of recorded Glean impression events
 * and the recorded Glean impression events have the correct keys and values.
 *
 * Then it checks that there are the the correct engagement events associated with the
 * impression events.
 *
 * @param {Array} expectedEvents The expected impression events whose keys and
 * values we use to validate the recorded Glean impression events.
 */
function assertSERPTelemetry(expectedEvents) {
  // Do a deep copy of impressions in case the input is using constants, as
  // we insert impression id into the expected events to make it easier to
  // run Assert.deepEqual() on the expected and actual result.
  expectedEvents = structuredClone(expectedEvents);

  for (let expectedEvent of expectedEvents) {
    // The prescan finds ads whenever an ad component is expected, unless the
    // test specifies otherwise.
    let hasAds = !!expectedEvent.adImpressions?.some(adImpression =>
      adImpression.component?.startsWith("ad_")
    );
    expectedEvent.impression = {
      ...DEFAULT_IMPRESSION,
      prescan: hasAds ? "found" : "none_found",
      ...expectedEvent.impression,
    };
  }

  // A single test might run assertImpressionEvents more than once
  // so the Set needs to be cleared or else the impression event
  // check will throw.
  const impressionIdsSet = new Set();

  let recordedImpressions = Glean.serp.impression.testGetValue() ?? [];

  Assert.equal(
    recordedImpressions.length,
    expectedEvents.length,
    "Number of impressions matches expected events."
  );

  // Assert the impression events.
  for (let [idx, expectedEvent] of expectedEvents.entries()) {
    let impressionId = recordedImpressions[idx].extra.impression_id;
    Assert.ok(
      UUID_REGEX.test(impressionId),
      "Impression has an impression_id with a valid UUID."
    );

    Assert.ok(
      !impressionIdsSet.has(impressionId),
      "Impression has a unique impression_id."
    );

    impressionIdsSet.add(impressionId);

    // If we want to use deepEqual checks, we have to add the impressionId
    // to each impression since they are randomly generated at runtime.
    expectedEvent.impression.impression_id = impressionId;

    Assert.deepEqual(
      recordedImpressions[idx].extra,
      expectedEvent.impression,
      "Matching SERP impression values."
    );

    // Once the impression check is sufficient, add the impression_id to
    // each of the expected engagements, ad impressions, and abandonments for
    // deep equal checks.
    if (expectedEvent.engagements) {
      for (let expectedEngagment of expectedEvent.engagements) {
        expectedEngagment.impression_id = impressionId;
      }
    }
    if (expectedEvent.adImpressions) {
      for (let adImpression of expectedEvent.adImpressions) {
        adImpression.impression_id = impressionId;
      }
    }
    if (expectedEvent.abandonment) {
      expectedEvent.abandonment.impression_id = impressionId;
    }
  }

  // Group engagement events into separate array fetchable by their
  // impression_id.
  let recordedEngagements = Glean.serp.engagement.testGetValue() ?? [];
  let idToEngagements = new Map();
  let totalExpectedEngagements = 0;

  for (let recordedEngagement of recordedEngagements) {
    let impressionId = recordedEngagement.extra.impression_id;
    Assert.ok(impressionId, "Engagement event has impression_id.");

    let arr = idToEngagements.get(impressionId) ?? [];
    arr.push(recordedEngagement.extra);

    idToEngagements.set(impressionId, arr);
  }

  // Assert the engagement events.
  for (let expectedEvent of expectedEvents) {
    let impressionId = expectedEvent.impression.impression_id;
    let expectedEngagements = expectedEvent.engagements;
    if (expectedEngagements) {
      let recorded = idToEngagements.get(impressionId) ?? [];
      Assert.deepEqual(
        recorded,
        expectedEngagements,
        "Matching engagement value."
      );
      totalExpectedEngagements += expectedEngagements.length;
    }
  }

  Assert.equal(
    recordedEngagements.length,
    totalExpectedEngagements,
    "Number of engagements"
  );

  let recordedAdImpressions = Glean.serp.adImpression.testGetValue() ?? [];
  let idToAdImpressions = new Map();
  let totalExpectedAdImpressions = 0;

  // The list of ad impressions are contained in a flat list. Separate them
  // into arrays organized by impressionId to make it easier to determine if
  // the page load that matches the expected ads on the page.
  for (let recordedAdImpression of recordedAdImpressions) {
    let impressionId = recordedAdImpression.extra.impression_id;
    Assert.ok(impressionId, "Ad impression has impression_id");

    let arr = idToAdImpressions.get(impressionId) ?? [];
    arr.push(recordedAdImpression.extra);
    idToAdImpressions.set(impressionId, arr);
  }

  for (let expectedEvent of expectedEvents) {
    let impressionId = expectedEvent.impression.impression_id;
    let expectedAdImpressions = expectedEvent.adImpressions ?? [];
    if (expectedAdImpressions.length) {
      let recorded = idToAdImpressions.get(impressionId) ?? {};
      Assert.deepEqual(
        recorded,
        expectedAdImpressions,
        "Matching ad impression value."
      );
    }
    totalExpectedAdImpressions += expectedAdImpressions.length;
  }

  Assert.equal(
    recordedAdImpressions.length,
    totalExpectedAdImpressions,
    "Recorded and expected ad impression counts match."
  );

  // Assert abandonment events.
  let recordedAbandonments = Glean.serp.abandonment.testGetValue() ?? [];
  let idTorecordedAbandonments = new Map();
  let totalExpectedrecordedAbandonments = 0;

  for (let recordedAbandonment of recordedAbandonments) {
    let impressionId = recordedAbandonment.extra.impression_id;
    Assert.ok(impressionId, "Abandonment event has an impression_id.");
    idTorecordedAbandonments.set(impressionId, recordedAbandonment.extra);
  }

  for (let expectedEvent of expectedEvents) {
    let impressionId = expectedEvent.impression.impression_id;
    let expectedAbandonment = expectedEvent.abandonment;
    if (expectedAbandonment) {
      let recorded = idTorecordedAbandonments.get(impressionId);
      Assert.deepEqual(
        recorded,
        expectedAbandonment,
        "Matching abandonment value."
      );
    }
    totalExpectedrecordedAbandonments += expectedAbandonment ? 1 : 0;
  }

  Assert.equal(
    recordedAbandonments.length,
    totalExpectedrecordedAbandonments,
    "Recorded and expected abandonment counts match."
  );
}

async function openSerpInNewTab(url, expectedAds = true) {
  let promise;
  if (expectedAds) {
    promise = waitForPageWithAdImpressions();
  }
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  await promise;

  let cleanup = async () => {
    await BrowserTestUtils.removeTab(tab);
    resetTelemetry();
  };

  return { tab, cleanup };
}

async function synthesizePageAction({
  selector,
  event = {},
  tab,
  expectEngagement = true,
} = {}) {
  let promise;
  if (expectEngagement) {
    promise = waitForPageWithAction();
  } else {
    // Wait roughly around how much it might take for a possible page action
    // to be registered in telemetry.
    /* eslint-disable-next-line mozilla/no-arbitrary-setTimeout */
    promise = new Promise(resolve => setTimeout(resolve, 50));
  }
  await BrowserTestUtils.synthesizeMouseAtCenter(
    selector,
    event,
    tab.linkedBrowser
  );

  await promise;
}

function assertCategorizationValues(expectedResults) {
  let actualResults = Glean.serp.categorization.testGetValue() ?? [];

  Assert.equal(
    actualResults.length,
    expectedResults.length,
    "Should have the correct number of categorization impressions."
  );

  if (!expectedResults.length) {
    return;
  }

  // We use keys in the result vs. Assert.deepEqual to make it easier to
  // identify exact discrepancies in comparisons, because it can be tedious to
  // parse a giant list of values.
  let keys = new Set();
  for (let expected of expectedResults) {
    for (let key in expected) {
      keys.add(key);
    }
  }
  for (let actual of actualResults) {
    for (let key in actual.extra) {
      keys.add(key);
    }
  }
  keys = Array.from(keys);

  for (let index = 0; index < expectedResults.length; ++index) {
    info(`Checking categorization at index: ${index}`);
    let expected = expectedResults[index];
    let actual = actualResults[index].extra;

    Assert.lessOrEqual(
      Number(actual?.organic_num_domains),
      CATEGORIZATION_SETTINGS.MAX_DOMAINS_TO_CATEGORIZE,
      "Number of organic domains categorized should not exceed threshold."
    );

    Assert.lessOrEqual(
      Number(actual?.sponsored_num_domains),
      CATEGORIZATION_SETTINGS.MAX_DOMAINS_TO_CATEGORIZE,
      "Number of sponsored domains categorized should not exceed threshold."
    );

    for (let key of keys) {
      Assert.equal(
        actual[key],
        expected[key],
        `Actual and expected values for ${key} should match.`
      );
    }
  }
}

function waitForPageWithAction() {
  return TestUtils.topicObserved("reported-page-with-action");
}

function waitForPageWithImpression() {
  return TestUtils.topicObserved("reported-page-with-impression");
}

function waitForPageWithAdImpressions() {
  return TestUtils.topicObserved("reported-page-with-ad-impressions");
}

function waitForPageWithCategorizedDomains() {
  return TestUtils.topicObserved("reported-page-with-categorized-domains");
}

function waitForSingleCategorizedEvent() {
  return TestUtils.topicObserved("recorded-single-categorization-event");
}

function waitForAllCategorizedEvents() {
  return TestUtils.topicObserved("recorded-all-categorization-events");
}

function waitForDomainToCategoriesUpdate() {
  return TestUtils.topicObserved("domain-to-categories-map-update-complete");
}

function waitForDomainToCategoriesInit() {
  return TestUtils.topicObserved("domain-to-categories-map-init");
}

function waitForDomainToCategoriesUninit() {
  return TestUtils.topicObserved("domain-to-categories-map-uninit");
}

registerCleanupFunction(async () => {
  await PlacesUtils.history.clear();
});

async function mockRecordWithAttachment({
  id,
  version,
  filename,
  mapping,
  includeRegions,
  excludeRegions,
  isDefault = true,
}) {
  // Get the bytes of the file for the hash and size for attachment metadata.
  let buffer = new TextEncoder().encode(JSON.stringify(mapping)).buffer;
  let stream = Cc["@mozilla.org/io/arraybuffer-input-stream;1"].createInstance(
    Ci.nsIArrayBufferInputStream
  );
  stream.setData(buffer, 0, buffer.byteLength);

  // Generate a hash.
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  hasher.init(Ci.nsICryptoHash.SHA256);
  hasher.updateFromStream(stream, -1);
  let hash = hasher.finish(false);
  hash = Array.from(hash, (_, i) =>
    ("0" + hash.charCodeAt(i).toString(16)).slice(-2)
  ).join("");

  let record = {
    id,
    version,
    includeRegions,
    excludeRegions,
    isDefault,
    attachment: {
      hash,
      location: `main-workspace/search-categorization/${filename}`,
      filename,
      size: buffer.byteLength,
      mimetype: "application/json",
    },
  };

  let attachment = {
    record,
    blob: new Blob([buffer]),
  };

  return { record, attachment };
}

async function resetCategorizationCollection(record) {
  const client = RemoteSettings(TELEMETRY_CATEGORIZATION_KEY);
  await client.attachments.cacheImpl.delete(record.id);
  await client.db.clear();
  await client.db.importChanges({}, Date.now());
}

const MOCK_ATTACHMENT_VALUES = {
  "abc.com": [2, 95],
  "abc.org": [4, 90],
  "def.com": [2, 78, 4, 10],
  "def.org": [4, 90],
  "foobar.org": [3, 90],
};

const CONVERTED_ATTACHMENT_VALUES = convertDomainsToHashes(
  MOCK_ATTACHMENT_VALUES
);

function convertDomainsToHashes(domainsToCategories) {
  let newObj = {};
  for (let [key, value] of Object.entries(domainsToCategories)) {
    gCryptoHash.init(gCryptoHash.SHA256);
    let bytes = new TextEncoder().encode(key);
    gCryptoHash.update(bytes, key.length);
    let hash = gCryptoHash.finish(true);
    newObj[hash] = value;
  }
  return newObj;
}

async function insertRecordIntoCollection() {
  const client = RemoteSettings(TELEMETRY_CATEGORIZATION_KEY);
  const db = client.db;

  await db.clear();
  let { record, attachment } = await mockRecordWithAttachment({
    id: "example_id",
    version: 1,
    filename: "domain_category_mappings.json",
    mapping: CONVERTED_ATTACHMENT_VALUES,
    includeRegions: [],
    excludeRegions: [],
    isDefault: true,
  });
  await db.create(record);
  await client.attachments.cacheImpl.set(record.id, attachment);
  await db.importChanges({}, Date.now());

  return { record, attachment };
}

async function insertRecordIntoCollectionAndSync() {
  let { record } = await insertRecordIntoCollection();

  registerCleanupFunction(async () => {
    await resetCategorizationCollection(record);
  });

  await syncCollection(record);
}

async function syncCollection(record) {
  let arrayWithRecord = record ? [record] : [];
  await RemoteSettings(TELEMETRY_CATEGORIZATION_KEY).emit("sync", {
    data: {
      current: arrayWithRecord,
      created: arrayWithRecord,
      updated: [],
      deleted: [],
    },
  });
}

async function initSinglePageAppTest({ baseProviderOverrides = {} } = {}) {
  /* import-globals-from head-spa.js */
  Services.scriptloader.loadSubScript(
    "chrome://mochitests/content/browser/browser/components/search/test/browser/telemetry/head-spa.js",
    this
  );

  const BASE_PROVIDER = {
    telemetryId: "example1",
    searchPageRegexp:
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetrySinglePageApp/,
    queryParamNames: ["s"],
    codeParamName: "abc",
    taggedCodes: ["ff"],
    adServerAttributes: ["mozAttr"],
    extraAdServersRegexps: [
      /^https:\/\/example\.com\/ad/,
      /^https:\/\/example.org\/browser\/browser\/components\/search\/test\/browser\/telemetry\/redirect_ad/,
    ],
    components: [
      {
        included: {
          parent: {
            selector: "#searchbox-container",
          },
          related: {
            selector: "#searchbox-suggestions",
          },
          children: [
            {
              selector: "#searchbox",
            },
          ],
        },
        topDown: true,
        type: SearchSERPTelemetryUtils.COMPONENTS.INCONTENT_SEARCHBOX,
      },
      {
        type: SearchSERPTelemetryUtils.COMPONENTS.AD_LINK,
        default: true,
      },
    ],
    pageTypeParam: {
      enableSPAHandling: true,
      keys: ["page"],
      pageTypes: [
        { name: "web", values: ["web"], enabled: true, isDefault: true },
        { name: "shopping", values: ["shopping"], enabled: true },
        { name: "images", values: ["images"], enabled: false },
      ],
    },
    ...baseProviderOverrides,
  };

  const SPA_PROVIDER_INFO = [
    BASE_PROVIDER,
    {
      ...BASE_PROVIDER,
      telemetryId: "example2",
      // Use example.com instead of example.org so that we have two providers
      // with different TLD's and won't share the same web process.
      searchPageRegexp:
        /^https:\/\/example.com\/browser\/browser\/components\/search\/test\/browser\/telemetry\/searchTelemetrySinglePageApp/,
    },
  ];

  SearchSERPTelemetry.overrideSearchTelemetryForTests(SPA_PROVIDER_INFO);
  await waitForIdle();

  // Shorten delay to avoid potential TV timeouts.
  Services.ppmm.sharedData.set(SEARCH_TELEMETRY_SHARED.SPA_LOAD_TIMEOUT, 100);

  await SpecialPowers.pushPrefEnv({
    set: [["dom.ipc.processCount.webIsolated", 1]],
  });

  registerCleanupFunction(function () {
    Services.ppmm.sharedData.set(
      SEARCH_TELEMETRY_SHARED.SPA_LOAD_TIMEOUT,
      SPA_ADLINK_CHECK_TIMEOUT_MS
    );
  });
}

async function resizeWindow(win, width, height) {
  let promise = BrowserTestUtils.waitForEvent(win, "resize");
  win.resizeTo(width, height);
  await promise;

  // Wait two frames in hopes resizing is done.
  await new Promise(resolve => win.requestAnimationFrame(resolve));
  await new Promise(resolve => win.requestAnimationFrame(resolve));
}
