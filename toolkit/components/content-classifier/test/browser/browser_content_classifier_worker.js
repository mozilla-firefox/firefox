"use strict";

// Bug 2088304: requests from a worker have no top-level window. The engine
// must still see the page's site as the source, so $domain options apply.

async function fetchFromWorker(browser, domain) {
  let url =
    domain +
    "browser/toolkit/components/antitracking/test/browser/raptor.jpg?" +
    Math.random();
  return SpecialPowers.spawn(browser, [url], async fetchURL => {
    let source = `fetch(${JSON.stringify(fetchURL)}, { mode: "no-cors" }).then(
      () => postMessage(true),
      () => postMessage(false)
    );`;
    let blobURL = content.URL.createObjectURL(
      new content.Blob([source], { type: "text/javascript" })
    );
    let worker = new content.Worker(blobURL);
    return new content.Promise(resolve => {
      worker.onmessage = e => {
        worker.terminate();
        resolve(e.data);
      };
    });
  });
}

add_task(async function test_worker_excluded_domain_applies() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||example.org^$domain=~example.net"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  await syncAndWaitForLists(client, records);

  ok(
    await fetchFromWorker(tab.linkedBrowser, TEST_BLOCKED_3RD_PARTY_DOMAIN),
    "$domain=~example.net lets a worker on an example.net page fetch example.org"
  );
  await assertImageLoaded(
    tab.linkedBrowser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "$domain=~example.net lets the example.net page load example.org"
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_worker_included_domain_applies() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||example.org^$domain=example.net"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  await syncAndWaitForLists(client, records);

  ok(
    !(await fetchFromWorker(tab.linkedBrowser, TEST_BLOCKED_3RD_PARTY_DOMAIN)),
    "$domain=example.net blocks a worker on an example.net page fetching example.org"
  );
  await assertImageBlocked(
    tab.linkedBrowser,
    TEST_BLOCKED_3RD_PARTY_DOMAIN,
    "$domain=example.net blocks the example.net page loading example.org"
  );

  BrowserTestUtils.removeTab(tab);
});
