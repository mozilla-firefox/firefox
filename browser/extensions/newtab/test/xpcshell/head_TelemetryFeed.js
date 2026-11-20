/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { updateAppInfo } = ChromeUtils.importESModule(
  "resource://testing-common/AppInfo.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AboutNewTabParent: "resource:///actors/AboutNewTabParent.sys.mjs",
  AdsClient: "resource://newtab/lib/AdsClient.sys.mjs",
  ContextId: "moz-src:///browser/modules/ContextId.sys.mjs",
  actionCreators: "resource://newtab/common/Actions.mjs",
  actionTypes: "resource://newtab/common/Actions.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  GleanSessionType: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  laplaceNoise: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  NewTabContentPing: "resource://newtab/lib/NewTabContentPing.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  TelemetryController: "resource://gre/modules/TelemetryController.sys.mjs",
  TelemetryFeed: "resource://newtab/lib/TelemetryFeed.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  WIDGET_REGISTRY: "resource://newtab/common/WidgetsRegistry.mjs",
});

const FAKE_UUID = "{foo-123-foo}";
const PREF_IMPRESSION_ID = "browser.newtabpage.activity-stream.impressionId";
const PREF_TELEMETRY = "browser.newtabpage.activity-stream.telemetry";
const PREF_NEWTAB_PING_ENABLED = "browser.newtabpage.ping.enabled";
const PREF_PRIVATE_PING_ENABLED =
  "browser.newtabpage.activity-stream.telemetry.privatePing.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_ENABLED =
  "browser.newtabpage.activity-stream.telemetry.privatePing.dwellTime.enabled";
const PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE =
  "browser.newtabpage.activity-stream.telemetry.privatePing.dwellTime.noiseScale";
const PREF_IS_MERINO_FEED_EXPERIMENT =
  "browser.newtabpage.activity-stream.discoverystream.merino-feed-experiment";
const PREF_ENDPOINTS =
  "browser.newtabpage.activity-stream.discoverystream.endpoints";
const PREF_UNIFIED_ADS_TILES_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.tiles.enabled";
const PREF_UNIFIED_ADS_SPOCS_ENABLED =
  "browser.newtabpage.activity-stream.unifiedAds.spocs.enabled";

add_setup(async function setup() {
  do_get_profile();
  // FOG needs to be initialized in order for data to flow.
  Services.fog.initializeFOG();

  await TelemetryController.testReset();

  updateAppInfo({
    name: "XPCShell",
    ID: "xpcshell@tests.mozilla.org",
    version: "122",
    platformVersion: "122",
  });

  let sandbox = sinon.createSandbox();
  sandbox.stub(ContextId, "requestSynchronously").returns(FAKE_UUID);

  registerCleanupFunction(() => {
    sandbox.restore();
  });
});

const USER_INTERACTION_ACTIVE = "user-interaction-active-non-synthesized";
const USER_INTERACTION_INACTIVE = "user-interaction-inactive-non-synthesized";
// EventStateManager's default notification cadence. The feed takes its cutoff
// from the notifications themselves, so this only paces the fake ones.
const USER_INTERACTION_INTERVAL_MS = 5000;
// testGetValue().sum is in nanoseconds, whatever the time_unit.
const NS_PER_MS = 1000000;

/**
 * Build a TelemetryFeed with the clock and the foreground check under the
 * test's control, and telemetry on so the newtab ping is submitted.
 *
 * @param {object} sandbox a sinon sandbox
 * @returns {object} the instance and a setTime(ms) helper
 */
function setupDwellFeed(sandbox) {
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");
  sandbox.stub(instance, "isDwellTargetInForeground").returns(true);
  let clock = sandbox.stub(instance, "now").returns(0);

  return { instance, setTime: ms => clock.returns(ms) };
}

/**
 * Undo setupDwellFeed.
 *
 * @param {object} sandbox the sinon sandbox it was given
 */
function teardownDwellTest(sandbox) {
  sandbox.restore();
  Services.prefs.clearUserPref(PREF_TELEMETRY);
  // Owned here rather than by each test, so setupContentDwell setting them
  // cannot leak into whatever runs next.
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_ENABLED);
  Services.prefs.clearUserPref(PREF_PRIVATE_PING_DWELL_TIME_NOISE_SCALE);
  Services.fog.testResetFOG();
}

/**
 * A URI the way the dwell code reads one off a <browser>.
 *
 * @param {string} spec
 * @returns {object}
 */
function fakeURI(spec) {
  return {
    spec,
    specIgnoringRef: spec.split("#")[0],
    scheme: spec.split(":")[0],
  };
}

// Window globals are identified by a process-wide id in the real thing, so
// every document a test creates gets a distinct one.
let gNextInnerWindowId = 1;

/**
 * A stand-in for a <browser> in a tab, with the surface the dwell code
 * touches. Like the real thing, it tells its progress listeners about each
 * top-level location change as it happens, and its tab fires TabClose.
 *
 * - goTo() loads a new document at a new address.
 * - goToInDocument() is same-document navigation, which keeps the window
 *   global it already had.
 * - goToInFrame() is a frame inside the page loading something.
 * - replaceDocument() loads a new document at the same address, the way a
 *   reload or a page that replaces itself does.
 * - failLoad() shows an error page, which keeps the failed address.
 * - closeTab() closes the tab.
 * - closeWindow() closes the tab's window, which fires no TabClose.
 *
 * @param {string} [spec] where the tab starts out
 * @param {object} [options]
 * @param {boolean} [options.listeners] false for a browser whose progress
 *  listeners never fire, so only the activity notifications see changes
 * @returns {object} the fake browser
 */
function makeFakeBrowser(spec = "about:newtab", { listeners = true } = {}) {
  const progressListeners = new Set();
  const tabListeners = new Set();
  const windowListeners = new Set();
  const tab = {
    addEventListener(type, listener) {
      if (type === "TabClose") {
        tabListeners.add(listener);
      }
    },
    removeEventListener(type, listener) {
      if (type === "TabClose") {
        tabListeners.delete(listener);
      }
    },
  };
  const browser = {
    permanentKey: {},
    isConnected: true,
    progressListeners,
    tabListeners,
    windowListeners,
    browsingContext: {
      currentWindowGlobal: {
        documentURI: fakeURI(spec),
        innerWindowId: gNextInnerWindowId++,
      },
    },
    documentGlobal: {
      gBrowser: { getTabForBrowser: () => tab },
      addEventListener(type, listener) {
        if (type === "unload") {
          windowListeners.add(listener);
        }
      },
      removeEventListener(type, listener) {
        if (type === "unload") {
          windowListeners.delete(listener);
        }
      },
    },
    getAttribute() {
      return "";
    },
    addProgressListener(listener) {
      progressListeners.add(listener);
    },
    removeProgressListener(listener) {
      progressListeners.delete(listener);
    },
    notifyLocationChange(uri, flags = 0) {
      if (!listeners) {
        return;
      }
      for (const listener of [...progressListeners]) {
        listener.onLocationChange({ isTopLevel: true }, null, uri, flags);
      }
    },
    goTo(newSpec) {
      this.browsingContext.currentWindowGlobal = {
        documentURI: fakeURI(newSpec),
        innerWindowId: gNextInnerWindowId++,
      };
      this.notifyLocationChange(fakeURI(newSpec));
    },
    goToInDocument(newSpec) {
      this.browsingContext.currentWindowGlobal.documentURI = fakeURI(newSpec);
      this.notifyLocationChange(
        fakeURI(newSpec),
        Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT
      );
    },
    goToInFrame(frameSpec) {
      for (const listener of [...progressListeners]) {
        listener.onLocationChange(
          { isTopLevel: false },
          null,
          fakeURI(frameSpec),
          0
        );
      }
    },
    replaceDocument() {
      this.goTo(this.browsingContext.currentWindowGlobal.documentURI.spec);
    },
    failLoad(failedSpec) {
      this.browsingContext.currentWindowGlobal = {
        documentURI: fakeURI("about:neterror?e=connectionFailure"),
        innerWindowId: gNextInnerWindowId++,
      };
      this.notifyLocationChange(
        fakeURI(failedSpec),
        Ci.nsIWebProgressListener.LOCATION_CHANGE_ERROR_PAGE
      );
    },
    closeTab() {
      for (const listener of [...tabListeners]) {
        listener();
      }
      this.isConnected = false;
    },
    closeWindow() {
      for (const listener of [...windowListeners]) {
        listener();
      }
      this.isConnected = false;
    },
  };
  return browser;
}

/**
 * Open a link from the newtab, the way PlacesFeed reports it.
 *
 * @param {object} instance a TelemetryFeed
 * @param {object} browser the <browser> that receives the load
 * @param {object} [data] the dwell_label the link opted in with
 */
function openLinkFrom(instance, browser, data = {}) {
  instance.handleDwellLinkOpened({
    data: { browser, dwell_label: "story_organic", ...data },
  });
}

/**
 * The newtab.opened_page_dwell_time distribution for one label, or null if
 * none.
 *
 * @param {string} label one of the four labels the metric declares
 * @returns {object|null}
 */
function readOpenedPageDwell(label) {
  return Glean.newtab.openedPageDwellTime[label].testGetValue("metrics");
}
