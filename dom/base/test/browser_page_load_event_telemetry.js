"use strict";

const { TelemetryTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TelemetryTestUtils.sys.mjs"
);

const ALL_CHANNELS = Ci.nsITelemetry.DATASET_ALL_CHANNELS;

add_task(async function () {
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    waitForLoad: true,
  });

  let browser = tab.linkedBrowser;

  // Reset event counts.
  Services.telemetry.clearEvents();
  TelemetryTestUtils.assertNumberOfEvents(0);

  // Check for pageload ping and pageload event
  await GleanPings.pageload.testSubmission(
    reason => {
      Assert.equal(reason, "threshold");
      let record = Glean.perf.pageLoad.testGetValue();

      // Ensure the events in the pageload ping are reasonable.
      record.forEach(entry => {
        Assert.equal(entry.name, "page_load");
        Assert.greater(parseInt(entry.extra.load_time), 0);
        Assert.ok(
          entry.extra.using_webdriver,
          "Webdriver field should be set to true."
        );
        Assert.ok(
          "is_active_client" in entry.extra,
          "Active client field should be recorded."
        );
      });
    },
    async () => {
      // Perform page load 30 times to trigger the ping being sent
      for (let i = 0; i < 30; i++) {
        BrowserTestUtils.startLoadingURIString(browser, "https://example.com");
        await BrowserTestUtils.browserLoaded(browser);
      }
      BrowserTestUtils.removeTab(tab);
    },
    // The ping itself is submitted via idle dispatch, so we need to specify a
    // timeout.
    1000
  );
});
