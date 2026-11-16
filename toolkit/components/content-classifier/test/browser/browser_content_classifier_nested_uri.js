"use strict";

// A nested URI is matched on its innermost URI, both its hostname and its URL.

add_task(async function test_probe_matches_innermost_uri() {
  let client = getRSClient();
  let records = await populateMultipleRS(client.db, [
    {
      id: "trackers",
      name: "disconnect-tracker-base",
      rules: ["||tracking.example.com^", "||example.org/tracker.js"],
    },
  ]);
  await pushEnginePrefs({ protection: "trackers" });
  let tab = await openTestTab();
  await syncAndWaitForLists(client, records);

  ok(
    await probeHits("view-source:https://tracking.example.com/image.jpg"),
    "||tracking.example.com^ matches view-source:https://tracking.example.com/"
  );
  ok(
    await probeHits("view-source:https://example.org/tracker.js"),
    "||example.org/tracker.js matches view-source:https://example.org/tracker.js"
  );
  ok(
    !(await probeHits("view-source:https://example.org/other.js")),
    "||example.org/tracker.js does not match view-source:https://example.org/other.js"
  );
  ok(
    !(await probeHits("view-source:https://example.com/image.jpg")),
    "||tracking.example.com^ does not match view-source:https://example.com/"
  );

  BrowserTestUtils.removeTab(tab);
});
