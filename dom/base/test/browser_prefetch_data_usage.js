"use strict";

const BASE = "https://example.com/browser/dom/base/test/";

function targetURL(token) {
  // A fresh URL per trial, so trials cannot share a cache entry.
  return `${BASE}file_pageload_prefetch_target.sjs?v=${token}-${Date.now()}`;
}

function sourceURL(file, target) {
  return `${BASE}${file}?target=${encodeURIComponent(target)}`;
}

// One sample per record, so count is how many records ended with that label.
function transferred(label) {
  return (
    Glean.speculationRules.prefetchTransferSize[label].testGetValue() ?? {
      sum: 0,
      count: 0,
    }
  );
}

async function openSourceAndPrefetch(file, target, expectPrefetch) {
  // The prefetch can finish before the source page fires load.
  const prefetched = expectPrefetch
    ? TestUtils.topicObserved(
        "http-on-stop-request",
        subject => subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === target
      )
    : Promise.resolve();

  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: sourceURL(file, target),
    waitForLoad: true,
  });
  await prefetched;
  return tab;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.speculation_rules.enabled", true]],
  });
});

add_task(async function test_activated_prefetch_is_used() {
  Services.fog.testResetFOG();

  const target = targetURL("used");
  const tab = await openSourceAndPrefetch(
    "file_pageload_prefetch_source.html",
    target,
    true
  );

  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, target);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, target);
  // A record is only reported once discarded, which happens with its document.
  BrowserTestUtils.removeTab(tab);

  await TestUtils.waitForCondition(
    () => transferred("used").count === 1,
    "A prefetch a navigation activated is reported as used."
  );
  Assert.equal(
    transferred("unused").count,
    0,
    "It is not also counted as unused."
  );
  Assert.greater(
    transferred("used").sum,
    0,
    "Its network bytes are attributed to the used label."
  );
});

add_task(async function test_prefetch_nothing_navigated_to_is_unused() {
  Services.fog.testResetFOG();

  const target = targetURL("unused");
  const tab = await openSourceAndPrefetch(
    "file_pageload_prefetch_source.html",
    target,
    true
  );
  // Discard the source document without ever navigating to the target.
  BrowserTestUtils.removeTab(tab);

  await TestUtils.waitForCondition(
    () => transferred("unused").count === 1,
    "A prefetch no navigation came for is reported as unused."
  );
  Assert.equal(transferred("used").count, 0, "It is not counted as used.");
  Assert.greater(
    transferred("unused").sum,
    0,
    "Its wasted network bytes are attributed to the unused label."
  );
});

async function assertIncompletePrefetch() {
  await TestUtils.waitForCondition(
    () => transferred("incomplete").count === 1,
    "The prefetch is reported exactly once as incomplete."
  );
  Assert.equal(transferred("used").count, 0, "It is not counted as used.");
  Assert.equal(transferred("unused").count, 0, "It is not counted as unused.");
}

add_task(async function test_canceled_prefetch_is_incomplete() {
  Services.fog.testResetFOG();

  const target = `${targetURL("canceled")}&slow=10000`;
  const requested = TestUtils.topicObserved(
    "http-on-modify-request",
    subject => subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === target
  );
  const tab = await openSourceAndPrefetch(
    "file_pageload_prefetch_source.html",
    target,
    false
  );
  await requested;
  BrowserTestUtils.removeTab(tab);

  await assertIncompletePrefetch();
  Assert.equal(
    transferred("incomplete").sum,
    0,
    "Discarding a prefetch before its response records zero bytes."
  );
});

add_task(async function test_server_failure_is_incomplete() {
  Services.fog.testResetFOG();

  const target = `${targetURL("failure")}&slow=1&status=503`;
  const stopped = TestUtils.topicObserved(
    "http-on-stop-request",
    subject => subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === target
  );
  const tab = await openSourceAndPrefetch(
    "file_pageload_prefetch_source.html",
    target,
    false
  );
  const [channel] = await stopped;
  Assert.equal(
    channel.QueryInterface(Ci.nsIHttpChannel).responseStatus,
    503,
    "The server returned a failure response."
  );
  BrowserTestUtils.removeTab(tab);

  await assertIncompletePrefetch();
  Assert.greater(
    transferred("incomplete").sum,
    0,
    "The failed response's network bytes are attributed to incomplete."
  );
});

add_task(async function test_timed_out_prefetch_is_incomplete() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["network.http.response.timeout", 1],
      ["network.http.tcp_keepalive.short_lived_connections", false],
      ["network.http.tcp_keepalive.long_lived_connections", false],
    ],
  });
  Services.fog.testResetFOG();

  const { HttpServer } = ChromeUtils.importESModule(
    "resource://testing-common/httpd.sys.mjs"
  );
  const server = new HttpServer();
  let pendingResponse;
  server.registerPathHandler("/target", (request, response) => {
    response.processAsync();
    pendingResponse = response;
  });
  server.registerPathHandler("/source", (request, response) => {
    response.setHeader("Content-Type", "text/html");
    response.write(
      `<!DOCTYPE html><script type="speculationrules">${JSON.stringify({
        prefetch: [
          { source: "list", urls: ["/target"], eagerness: "immediate" },
        ],
      })}</script>`
    );
  });
  server.start(-1);
  const base = `http://localhost:${server.identity.primaryPort}`;
  const stopped = TestUtils.topicObserved(
    "http-on-stop-request",
    subject =>
      subject.QueryInterface(Ci.nsIHttpChannel).URI.spec === `${base}/target`
  );
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: `${base}/source`,
    waitForLoad: true,
  });
  try {
    const [channel] = await stopped;
    Assert.equal(
      channel.QueryInterface(Ci.nsIRequest).status,
      Cr.NS_ERROR_NET_TIMEOUT,
      "The prefetch timed out waiting for the server's response."
    );
  } finally {
    BrowserTestUtils.removeTab(tab);
    pendingResponse?.finish();
    await new Promise(resolve => server.stop(resolve));
    await SpecialPowers.popPrefEnv();
  }

  await assertIncompletePrefetch();
  Assert.equal(
    transferred("incomplete").sum,
    0,
    "A timeout before the response transfers no response bytes."
  );
});

add_task(async function test_navigation_without_rules_records_nothing() {
  Services.fog.testResetFOG();

  const target = targetURL("none");
  const tab = await openSourceAndPrefetch(
    // Same flow, but the source page carries no speculation rules.
    "file_pageload_prefetch_plain.html",
    target,
    false
  );
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, target);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, target);
  BrowserTestUtils.removeTab(tab);

  for (const label of ["used", "unused", "incomplete"]) {
    Assert.equal(
      transferred(label).count,
      0,
      `A navigation with no speculation rules reports no ${label} prefetch.`
    );
  }
});
