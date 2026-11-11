/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * This test ensures we are correctly extracting domains from a SERP.
 */

ChromeUtils.defineESModuleGetters(this, {
  SearchUtils: "moz-src:///toolkit/components/search/SearchUtils.sys.mjs",
});

// The search provider's name is provided to ensure we can extract domains
// from relative links, e.g. /url?=https://www.foobar.com
const SEARCH_PROVIDER_NAME = "example";

const TESTS = [
  {
    title: "Extract domain from href (absolute URL) - one link.",
    extractorInfos: [
      {
        selectors:
          '#test1 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
    ],
    expectedDomains: ["foobar.com"],
  },
  {
    title: "Extract domain from href (absolute URL) - multiple links.",
    extractorInfos: [
      {
        selectors:
          '#test2 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
    ],
    expectedDomains: ["foo.com", "bar.com", "baz.com", "qux.com"],
  },
  {
    title: "Extract domain from href (relative URL / URL matching provider)",
    extractorInfos: [
      {
        selectors:
          '#test3 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Extract domain from data attribute - one link.",
    extractorInfos: [
      {
        selectors: "#test4 [data-dtld]",
        method: "dataAttribute",
        options: {
          dataAttributeKey: "dtld",
        },
      },
    ],
    expectedDomains: ["abc.com"],
  },
  {
    title: "Extract domain from data attribute - multiple links.",
    extractorInfos: [
      {
        selectors: "#test5 [data-dtld]",
        method: "dataAttribute",
        options: {
          dataAttributeKey: "dtld",
        },
      },
    ],
    expectedDomains: ["foo.com", "bar.com", "baz.com", "qux.com"],
  },
  {
    title: "Extract domain from an href's query param value.",
    extractorInfos: [
      {
        selectors:
          '#test6 .js-carousel-item-title, #test6 [data-layout="ad"] [data-testid="result-title-a"]',
        method: "href",
        options: {
          queryParamKey: "ad_domain",
        },
      },
    ],
    expectedDomains: ["def.com", "bar.com", "baz.com"],
  },
  {
    title:
      "Extract domain from an href's query param value containing an href.",
    extractorInfos: [
      {
        selectors: "#test7 a",
        method: "href",
        options: {
          queryParamKey: "ad_domain",
          queryParamValueIsHref: true,
        },
      },
    ],
    expectedDomains: ["def.com"],
  },
  {
    title:
      "The param value contains an invalid href while queryParamValueIsHref enabled.",
    extractorInfos: [
      {
        selectors: "#test8 a",
        method: "href",
        options: {
          queryParamKey: "ad_domain",
          queryParamValueIsHref: true,
        },
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Param value is missing from the href.",
    extractorInfos: [
      {
        selectors: "#test9 a",
        method: "href",
        options: {
          queryParamKey: "ad_domain",
          queryParamValueIsHref: true,
        },
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Extraction preserves order of domains within the page.",
    extractorInfos: [
      {
        selectors:
          '#test10 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
      {
        selectors: "#test10 [data-dtld]",
        method: "dataAttribute",
        options: {
          dataAttributeKey: "dtld",
        },
      },
      {
        selectors:
          '#test10 .js-carousel-item-title, #test7 [data-layout="ad"] [data-testid="result-title-a"]',
        method: "href",
        options: {
          queryParamKey: "ad_domain",
        },
      },
    ],
    expectedDomains: ["foobar.com", "abc.com", "def.com"],
  },
  {
    title: "No elements match the selectors.",
    extractorInfos: [
      {
        selectors:
          '#test11 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Data attribute is present, but value is missing.",
    extractorInfos: [
      {
        selectors: "#test12 [data-dtld]",
        method: "dataAttribute",
        options: {
          dataAttributeKey: "dtld",
        },
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Query param is present, but value is missing.",
    extractorInfos: [
      {
        selectors: '#test13 [data-layout="ad"] [data-testid="result-title-a"]',
        method: "href",
        options: {
          queryParamKey: "ad_domain",
        },
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Non-standard URL scheme.",
    extractorInfos: [
      {
        selectors:
          '#test14 [data-layout="organic"] a[data-testid="result-title-a"]',
        method: "href",
      },
    ],
    expectedDomains: [],
  },
  {
    title: "Second-level domains to a top-level domain.",
    extractorInfos: [
      {
        selectors: "#test15 a",
        method: "href",
      },
    ],
    expectedDomains: [
      "foobar.gc.ca",
      "foobar.gov.uk",
      "foobar.co.uk",
      "foobar.co.il",
    ],
  },
  {
    title: "URL with a long subdomain.",
    extractorInfos: [
      {
        selectors: "#test16 a",
        method: "href",
      },
    ],
    expectedDomains: ["foobar.com"],
  },
  {
    title: "URLs with the same top level domain.",
    extractorInfos: [
      {
        selectors: "#test17 a",
        method: "href",
      },
    ],
    expectedDomains: ["foobar.com"],
  },
  {
    title: "Maximum domains extracted from a single selector.",
    extractorInfos: [
      {
        selectors: "#test18 a",
        method: "href",
      },
    ],
    expectedDomains: [
      "foobar1.com",
      "foobar2.com",
      "foobar3.com",
      "foobar4.com",
      "foobar5.com",
      "foobar6.com",
      "foobar7.com",
      "foobar8.com",
      "foobar9.com",
      "foobar10.com",
    ],
  },
  {
    // This is just in case we use multiple selectors meant for separate SERPs
    // and the provider switches to re-using their markup.
    title: "Maximum domains extracted from multiple matching selectors.",
    extractorInfos: [
      {
        selectors: "#test19 a.foo",
        method: "href",
      },
      {
        selectors: "#test19 a.baz",
        method: "href",
      },
    ],
    expectedDomains: [
      "foobar1.com",
      "foobar2.com",
      "foobar3.com",
      "foobar4.com",
      "foobar5.com",
      "foobar6.com",
      "foobar7.com",
      "foobar8.com",
      "foobar9.com",
      // This is from the second selector.
      "foobaz1.com",
    ],
  },
  {
    title: "Bing organic result.",
    extractorInfos: [
      {
        selectors: "#test20 #b_results .b_algo .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["organic.com"],
  },
  {
    title: "Bing sponsored result.",
    extractorInfos: [
      {
        selectors: "#test21 #b_results .b_ad .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["sponsored.com"],
  },
  {
    title: "Bing carousel result.",
    extractorInfos: [
      {
        selectors: "#test22 .adsMvCarousel cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["fixedupfromthecarousel.com"],
  },
  {
    title: "Bing sidebar result.",
    extractorInfos: [
      {
        selectors: "#test23 aside cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["fixedupfromthesidebar.com"],
  },
  {
    title: "Extraction threshold respected using text content method.",
    extractorInfos: [
      {
        selectors: "#test24 #b_results .b_ad .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: [
      "sponsored1.com",
      "sponsored2.com",
      "sponsored3.com",
      "sponsored4.com",
      "sponsored5.com",
      "sponsored6.com",
      "sponsored7.com",
      "sponsored8.com",
      "sponsored9.com",
      "sponsored10.com",
    ],
  },
  {
    title: "Bing organic result with no protocol.",
    extractorInfos: [
      {
        selectors: "#test25 #b_results .b_algo .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["organic.com"],
  },
  {
    title: "Bing organic result with a path in the URL.",
    extractorInfos: [
      {
        selectors: "#test26 #b_results .b_algo .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["organic.com"],
  },
  {
    title: "Bing organic result with a path and query param in the URL.",
    extractorInfos: [
      {
        selectors: "#test27 #b_results .b_algo .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["organic.com"],
  },
  {
    title:
      "Bing organic result with a path in the URL, but protocol appears in separate HTML element.",
    extractorInfos: [
      {
        selectors: "#test28 #b_results .b_algo .b_attribution cite",
        method: "textContent",
      },
    ],
    expectedDomains: ["wikipedia.org"],
  },
];

add_setup(async function () {
  await SearchSERPTelemetry.init();

  registerCleanupFunction(async () => {
    resetTelemetry();
  });
});

add_task(async function test_domain_extraction_heuristics() {
  resetTelemetry();
  let url = getSERPUrl("searchTelemetryDomainExtraction.html");
  info(
    "Load a sample SERP where domains need to be extracted in different ways."
  );
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  for (let currentTest of TESTS) {
    if (currentTest.title) {
      info(currentTest.title);
    }
    let expectedDomains = new Set(currentTest.expectedDomains);
    let actualDomains = await SpecialPowers.spawn(
      gBrowser.selectedBrowser,
      [currentTest.extractorInfos, SEARCH_PROVIDER_NAME],
      (extractorInfos, searchProviderName) => {
        const { domainExtractor } = ChromeUtils.importESModule(
          "resource:///actors/SearchSERPTelemetryChild.sys.mjs"
        );
        return domainExtractor.extractDomainsFromDocument(
          content.document,
          extractorInfos,
          searchProviderName
        );
      }
    );

    Assert.deepEqual(
      Array.from(actualDomains),
      Array.from(expectedDomains),
      "Domains should have been extracted correctly."
    );
  }

  BrowserTestUtils.removeTab(tab);
});
