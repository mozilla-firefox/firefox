"use strict";

const triggeringPrincipal_base64 = E10SUtils.SERIALIZED_SYSTEMPRINCIPAL;

const MAX_CONCURRENT_TABS = "browser.engagement.max_concurrent_tab_count";
const TAB_EVENT_COUNT = "browser.engagement.tab_open_event_count";
const MAX_CONCURRENT_WINDOWS = "browser.engagement.max_concurrent_window_count";
const WINDOW_OPEN_COUNT = "browser.engagement.window_open_event_count";
const TOTAL_URI_COUNT = "browser.engagement.total_uri_count";
const UNFILTERED_URI_COUNT = "browser.engagement.unfiltered_uri_count";
const UNIQUE_DOMAINS_COUNT = "browser.engagement.unique_domains_count";
const TOTAL_URI_COUNT_NORMAL_AND_PRIVATE_MODE =
  "browser.engagement.total_uri_count_normal_and_private_mode";

BrowserUsageTelemetry._onTabsOpenedTask._idleTimeoutMs = 0;
registerCleanupFunction(() => {
  BrowserUsageTelemetry._onTabsOpenedTask._idleTimeoutMs = undefined;
});

function promiseBrowserStateRestored() {
  return new Promise(resolve => {
    Services.obs.addObserver(function observer() {
      Services.obs.removeObserver(
        observer,
        "sessionstore-browser-state-restored"
      );
      resolve();
    }, "sessionstore-browser-state-restored");
  });
}

function resetTelemetry() {
  Services.telemetry.clearScalars();
  Services.fog.testResetFOG();
  BrowserUsageTelemetry.maxTabCount = 0;
  BrowserUsageTelemetry.maxTabPinnedCount = 0;
  BrowserUsageTelemetry.maxWindowCount = 0;
}

add_task(async function test_privateMode() {
  // Let's reset the counts.
  resetTelemetry();

  // Open a private window and load a website in it.
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await BrowserTestUtils.firstBrowserLoaded(privateWin);
  BrowserTestUtils.startLoadingURIString(
    privateWin.gBrowser.selectedBrowser,
    "https://example.com/"
  );
  await BrowserTestUtils.browserLoaded(
    privateWin.gBrowser.selectedBrowser,
    false,
    "https://example.com/"
  );

  // Check that tab and window count is recorded.
  const scalars = TelemetryTestUtils.getProcessScalars("parent");

  ok(
    !(TOTAL_URI_COUNT in scalars),
    "We should not track URIs in private mode."
  );
  ok(
    !(UNFILTERED_URI_COUNT in scalars),
    "We should not track URIs in private mode."
  );
  ok(
    !(UNIQUE_DOMAINS_COUNT in scalars),
    "We should not track unique domains in private mode."
  );
  is(
    scalars[TAB_EVENT_COUNT],
    1,
    "The number of open tab event count must match the expected value."
  );
  is(
    scalars[MAX_CONCURRENT_TABS],
    2,
    "The maximum tab count must match the expected value."
  );
  is(
    scalars[WINDOW_OPEN_COUNT],
    1,
    "The number of window open event count must match the expected value."
  );
  is(
    scalars[MAX_CONCURRENT_WINDOWS],
    2,
    "The maximum window count must match the expected value."
  );
  is(
    scalars[TOTAL_URI_COUNT_NORMAL_AND_PRIVATE_MODE],
    1,
    "We should include URIs in private mode as part of the actual total URI count."
  );
  is(
    Glean.browserEngagement.uriCount.testGetValue(),
    1,
    "We should record the URI count in Glean as well."
  );

  // Clean up.
  await BrowserTestUtils.closeWindow(privateWin);
});

add_task(async function test_sessionRestore() {
  const PREF_RESTORE_ON_DEMAND = "browser.sessionstore.restore_on_demand";
  Services.prefs.setBoolPref(PREF_RESTORE_ON_DEMAND, false);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(PREF_RESTORE_ON_DEMAND);
  });

  // Let's reset the counts.
  resetTelemetry();

  // The first window will be put into the already open window and the second
  // window will be opened with _openWindowWithState, which is the source of the problem.
  const state = {
    windows: [
      {
        tabs: [
          {
            entries: [
              { url: "http://example.org", triggeringPrincipal_base64 },
            ],
            extData: { uniq: 3785 },
          },
        ],
        selected: 1,
      },
    ],
  };

  // Save the current session.
  let { SessionStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs"
  );

  // Load the custom state and wait for SSTabRestored, as we want to make sure
  // that the URI counting code was hit.
  let tabRestored = BrowserTestUtils.waitForEvent(
    gBrowser.tabContainer,
    "SSTabRestored"
  );
  SessionStore.setBrowserState(JSON.stringify(state));
  await tabRestored;

  // Check that the URI is not recorded.
  const scalars = TelemetryTestUtils.getProcessScalars("parent");

  ok(
    !(TOTAL_URI_COUNT in scalars),
    "We should not track URIs from restored sessions."
  );
  ok(
    !(UNFILTERED_URI_COUNT in scalars),
    "We should not track URIs from restored sessions."
  );
  ok(
    !(UNIQUE_DOMAINS_COUNT in scalars),
    "We should not track unique domains from restored sessions."
  );

  // Restore the original session and cleanup.
  let sessionRestored = promiseBrowserStateRestored();
  SessionStore.setBrowserState(JSON.stringify(state));
  await sessionRestored;
});
