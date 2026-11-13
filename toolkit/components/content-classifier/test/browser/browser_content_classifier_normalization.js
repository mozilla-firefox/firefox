"use strict";

const UNNORMALIZED_TRACKER = "https://example.org./";

add_task(async function test_probe_matches_normalized_hostname() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: [
        "||tracking.example.com^",
        "||example.org/tracker.js",
        "||sub1.test1.example.org^$domain=example.net",
      ],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  await syncAndWaitForLists(client, records);

  ok(
    await probeHits("https://tracking.example.com./image.jpg"),
    "||tracking.example.com^ matches tracking.example.com."
  );
  ok(
    await probeHits("https://tracking.example.com../image.jpg"),
    "||tracking.example.com^ matches tracking.example.com.."
  );
  ok(
    await probeHits("https://example.org./tracker.js"),
    "||example.org/tracker.js matches example.org./tracker.js"
  );
  ok(
    await probeHits("https://example.org../tracker.js"),
    "||example.org/tracker.js matches example.org../tracker.js"
  );
  ok(
    !(await probeHits("https://example.org./other.js")),
    "||example.org/tracker.js does not match example.org./other.js"
  );
  ok(
    await probeHits(
      "https://sub1.test1.example.org/image.jpg",
      "https://example.net./"
    ),
    "$domain=example.net applies when the source is example.net."
  );
  ok(
    await probeHits(
      "https://sub1.test1.example.org/image.jpg",
      "https://example.net../"
    ),
    "$domain=example.net applies when the source is example.net.."
  );
  ok(
    !(await probeHits(
      "https://sub1.test1.example.org/image.jpg",
      "https://example.com./"
    )),
    "$domain=example.net does not apply when the source is example.com."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_channel_to_unnormalized_hostname_is_blocked() {
  // Resolve the unnormalized host locally so an unblocked load fails on
  // connect instead of reaching the network.
  await SpecialPowers.pushPrefEnv({
    set: [["network.dns.localDomains", "example.org."]],
  });
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||example.org^"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  let browser = tab.linkedBrowser;
  await syncAndWaitForLists(client, records);

  await loadThirdPartyImage(browser, UNNORMALIZED_TRACKER);
  await assertHasBlockingState(
    browser,
    UNNORMALIZED_TRACKER,
    Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT,
    "example.org. is logged as blocked tracking content"
  );

  BrowserTestUtils.removeTab(tab);
});

function openImageChannel(url, loadingUrl) {
  let { NetUtil } = ChromeUtils.importESModule(
    "resource://gre/modules/NetUtil.sys.mjs"
  );
  let channel = NetUtil.newChannel({
    uri: url,
    loadingPrincipal: Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI(loadingUrl),
      {}
    ),
    securityFlags: Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    contentPolicyType: Ci.nsIContentPolicy.TYPE_IMAGE,
  });
  return new Promise(resolve => {
    channel.asyncOpen({
      onStartRequest() {},
      onDataAvailable(request, stream, offset, count) {
        let sis = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
          Ci.nsIScriptableInputStream
        );
        sis.init(stream);
        sis.read(count);
      },
      onStopRequest(request, status) {
        resolve(status);
      },
    });
  });
}

add_task(
  async function test_channel_from_unnormalized_loading_page_is_blocked() {
    let client = getRSClient();
    let records = await populateMultipleRS(client.db, [
      {
        id: "trackers",
        name: "disconnect-tracker-base",
        rules: ["||example.org^"],
      },
    ]);
    await pushEnginePrefs({ protection: "trackers" });
    let tab = await openTestTab();
    await syncAndWaitForLists(client, records);

    let imageURL =
      TEST_BLOCKED_3RD_PARTY_DOMAIN +
      "browser/toolkit/components/antitracking/test/browser/raptor.jpg";
    for (let loadingUrl of [
      "https://example.net/",
      "https://example.net./",
      "https://example.net../",
    ]) {
      is(
        await openImageChannel(imageURL + "?" + Math.random(), loadingUrl),
        Cr.NS_ERROR_TRACKING_URI,
        `example.org is blocked when loaded by ${loadingUrl}`
      );
    }

    BrowserTestUtils.removeTab(tab);
  }
);
