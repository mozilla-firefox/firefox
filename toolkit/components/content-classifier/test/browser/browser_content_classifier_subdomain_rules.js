"use strict";

// Bug 2072599: rules whose host is below the base domain must match.
// The engine has to see the request hostname, not the schemeless site.

const SUBDOMAIN_TRACKER = "https://tracking.example.com/";
const ORG_SUBDOMAIN_TRACKER = "https://tracking.example.org/";
const BASE_DOMAIN = "https://example.com/";
const ORG_SUB1_HOST = "https://sub1.test1.example.org/";
const ORG_TEST1_HOST = "https://test1.example.org/";
const WWW_HOST = "https://www.example.com/";
const SUBDOMAIN_TOP_PAGE =
  "https://test1.example.com/" + TEST_PATH + "page.html";

add_task(async function test_subdomain_block_rule_matches_only_that_host() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||tracking.example.com^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await syncAndWaitForLists(client, records);

  await assertImageBlocked(
    browser,
    SUBDOMAIN_TRACKER,
    "||tracking.example.com^ blocks tracking.example.com"
  );
  await assertImageLoaded(
    browser,
    BASE_DOMAIN,
    "||tracking.example.com^ does not block example.com"
  );
  await assertHasBlockingState(
    browser,
    SUBDOMAIN_TRACKER,
    Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT,
    "tracking.example.com is logged as blocked tracking content"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_subdomain_exception_rule_allows_that_host() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||example.com^"],
    },
    {
      id: "exception",
      name: "mozilla-major-exceptions",
      rules: ["@@||tracking.example.com^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers,major-exceptions" });
  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await syncAndWaitForLists(client, records);

  await assertImageLoaded(
    browser,
    SUBDOMAIN_TRACKER,
    "@@||tracking.example.com^ allows tracking.example.com against ||example.com^"
  );
  await assertImageBlocked(
    browser,
    WWW_HOST,
    "@@||tracking.example.com^ leaves www.example.com blocked"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_domain_option_matches_subdomain_top_level() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||example.org^$domain=test1.example.com"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    SUBDOMAIN_TOP_PAGE
  );
  await syncAndWaitForLists(client, records);

  await assertImageBlocked(
    tab.linkedBrowser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "$domain=test1.example.com applies on a test1.example.com page"
  );

  let otherTab = await openTestTab();
  await assertImageLoaded(
    otherTab.linkedBrowser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "$domain=test1.example.com does not apply on an example.net page"
  );

  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(tab);
});

add_task(
  async function test_domain_option_exception_matches_subdomain_top_level() {
    let client = getRSClient();
    let records = await populateMultipleRS(client.db, [
      {
        id: "trackers",
        name: "disconnect-tracker-base",
        rules: ["||example.org^"],
      },
      {
        id: "exception",
        name: "mozilla-major-exceptions",
        rules: ["@@||tracking.example.org^$domain=test1.example.com"],
      },
    ]);
    await pushEnginePrefs({ protection: "trackers,major-exceptions" });
    let tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      SUBDOMAIN_TOP_PAGE
    );
    await syncAndWaitForLists(client, records);

    await assertImageLoaded(
      tab.linkedBrowser,
      ORG_SUBDOMAIN_TRACKER,
      "@@||tracking.example.org^$domain=test1.example.com allows tracking.example.org on a test1.example.com page"
    );
    await assertImageBlocked(
      tab.linkedBrowser,
      TEST_BLOCKED_3RD_PARTY_DOMAIN,
      "the exception leaves example.org itself blocked on a test1.example.com page"
    );

    let otherTab = await openTestTab();
    await assertImageBlocked(
      otherTab.linkedBrowser,
      ORG_SUBDOMAIN_TRACKER,
      "@@||tracking.example.org^$domain=test1.example.com does not apply on an example.net page"
    );

    BrowserTestUtils.removeTab(otherTab);
    BrowserTestUtils.removeTab(tab);
  }
);

add_task(async function test_wildcard_subdomain_rule_matches_subdomains() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||*.example.com^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await syncAndWaitForLists(client, records);

  await assertImageBlocked(
    browser,
    SUBDOMAIN_TRACKER,
    "||*.example.com^ blocks tracking.example.com"
  );
  await assertImageBlocked(
    browser,
    WWW_HOST,
    "||*.example.com^ blocks www.example.com"
  );
  await assertImageLoaded(
    browser,
    BASE_DOMAIN,
    "||*.example.com^ does not block example.com"
  );

  // A leading wildcard leaves adblock-rust with no hostname to anchor on, so
  // the rule is a plain URL pattern and also matches ".example.com" followed
  // by a separator anywhere later in the URL.
  let url =
    TEST_BLOCKED_3RD_PARTY_DOMAIN +
    "browser/toolkit/components/antitracking/test/browser/raptor.jpg" +
    "?r=.example.com/";
  let loaded = await SpecialPowers.spawn(browser, [url], async src => {
    let img = new content.Image();
    img.src = src;
    return new content.Promise(resolve => {
      img.onload = () => resolve(true);
      img.onerror = () => resolve(false);
    });
  });
  ok(
    !loaded,
    "||*.example.com^ also matches .example.com/ in the query of an example.org URL"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_middle_wildcard_rule_anchors_on_hostname_prefix() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||sub1.*.example.org^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await syncAndWaitForLists(client, records);

  await assertImageBlocked(
    browser,
    ORG_SUB1_HOST,
    "||sub1.*.example.org^ blocks sub1.test1.example.org"
  );
  await assertImageLoaded(
    browser,
    ORG_TEST1_HOST,
    "||sub1.*.example.org^ does not block test1.example.org"
  );
  await assertImageLoaded(
    browser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "||sub1.*.example.org^ does not block example.org"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_probe_matches_subdomain_rule() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||tracking.example.com^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  await syncAndWaitForLists(client, records);

  ok(
    await probeHits(SUBDOMAIN_TRACKER + "image.jpg"),
    "probe: ||tracking.example.com^ matches tracking.example.com"
  );
  ok(
    !(await probeHits(BASE_DOMAIN + "image.jpg")),
    "probe: ||tracking.example.com^ does not match example.com"
  );

  BrowserTestUtils.removeTab(tab);
});
