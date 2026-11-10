/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { readFileSync } from "fs";
import path from "path";
import { CONTENT_MESSAGE_TYPE } from "common/Actions.mjs";
import { WIDGET_REGISTRY } from "common/WidgetsRegistry.mjs";
import { DEFAULT_SITES } from "lib/DefaultSites.sys.mjs";
import {
  ActivityStream,
  MARKET_PREF_FALLBACKS,
  PREFS_CONFIG,
  csvPrefHasValue,
} from "lib/ActivityStream.sys.mjs";
import { mockServices, stubGlobals } from "test/jest/test-utils";

// Stand-ins for the lazily loaded feed classes. The real feed modules cannot be
// imported here (they statically import resource://gre/modules/*), and the
// assertions below only care that the factory registered for each pref returns
// a feed.
function feedStubs() {
  return {
    AboutPreferences: class AboutPreferences {},
    DiscoveryStreamFeed: class DiscoveryStreamFeed {},
    FaviconFeed: class FaviconFeed {},
    HighlightsFeed: class HighlightsFeed {},
    NewTabInit: class NewTabInit {},
    PlacesFeed: class PlacesFeed {},
    PrefsFeed: class PrefsFeed {},
    SectionsFeed: class SectionsFeed {},
    SystemTickFeed: class SystemTickFeed {},
    TelemetryFeed: class TelemetryFeed {},
    TopSitesFeed: class TopSitesFeed {},
    TopStoriesFeed: class TopStoriesFeed {},
  };
}

function FakeStore() {
  return { init: jest.fn(), uninit: jest.fn(), feeds: { get: () => {} } };
}

// Enough of DefaultPrefs for the dynamic pref computation: it reads back
// whatever default was last written for a pref.
class FakeDefaultPrefs {
  constructor(config) {
    this._config = config;
    this._values = new Map();
    this.init = jest.fn();
  }

  get(name) {
    return this._values.get(name);
  }

  set(name, value) {
    this._values.set(name, value);
  }
}

/**
 * A jest.fn() with sinon's withArgs semantics: the first matching argument
 * prefix wins, and anything unmatched falls through to `fallback`.
 *
 * @param {Function} fallback Implementation for unmatched calls.
 * @returns {Function} jest.fn() with an extra whenCalledWith(...args) helper.
 */
function argsStub(fallback = () => undefined) {
  const behaviors = [];
  const fn = jest.fn((...args) => {
    const behavior = behaviors.find(({ expected }) =>
      expected.every((value, i) => value === args[i])
    );
    return behavior ? behavior.value : fallback(...args);
  });
  fn.whenCalledWith = (...expected) => ({
    returns(value) {
      behaviors.unshift({ expected, value });
      return fn;
    },
  });
  return fn;
}

const MARKET_GATE_PREF =
  "browser.newtabpage.activity-stream.widgets.marketGate.enabled";

const STORIES_REGION_LOCALE_PREF =
  "browser.newtabpage.activity-stream.discoverystream.stories-region-locale-config";

describe("ActivityStream", () => {
  let as;
  let restoreGlobals;
  let services;
  let region;
  let nimbusFeatures;
  let proxyService;

  beforeEach(() => {
    services = mockServices([
      "appinfo",
      "locale",
      "obs",
      "prefs",
      "urlFormatter",
    ]);
    // Market gating ships off; turn it on so the gating tests exercise it.
    services.prefs.getBoolPref = argsStub(
      (_pref, defaultValue) => defaultValue
    );
    services.prefs.getBoolPref.whenCalledWith(MARKET_GATE_PREF).returns(true);
    region = { home: "US", REGION_TOPIC: "browser-region-updated" };
    nimbusFeatures = {
      pocketNewtab: { getVariable: argsStub() },
      newtabTrainhop: { getAllEnrollments: jest.fn(() => []) },
    };
    proxyService = {
      registerChannelFilter: jest.fn(),
      unregisterChannelFilter: jest.fn(),
      newProxyInfo: jest.fn(),
    };

    restoreGlobals = stubGlobals({
      Services: services,
      Region: region,
      NimbusFeatures: nimbusFeatures,
      ProxyService: proxyService,
      AboutNewTabParent: { loadedTabs: new Set() },
      Store: FakeStore,
      DefaultPrefs: FakeDefaultPrefs,
      DEFAULT_SITES,
      NewTabActorRegistry: { init: jest.fn(), uninit: jest.fn() },
      ...feedStubs(),
    });

    as = new ActivityStream();
    PREFS_CONFIG.get("feeds.system.topstories").value = undefined;
  });

  afterEach(() => {
    restoreGlobals();
  });

  it("should exist", () => {
    expect(ActivityStream).toBeTruthy();
  });
  it("should initialize with .initialized=false", () => {
    expect(as.initialized).toBe(false);
  });
  it("should have a null createdInstant if not constructed with one", () => {
    const noCreatedInstantAS = new ActivityStream();
    expect(noCreatedInstantAS.createdInstant).toBeNull();
  });
  it("should have a createdInstant value exposed if constructed with one", () => {
    // The Node environment does not know what Temporal is, but we can pretend
    // that a Date is a temporal, since ActivityStream isn't really doing any
    // type-checking here - it's just holding onto whatever it was constructed
    // with, and exposing it with a getter.
    const instant = new Date();
    const createdInstantAS = new ActivityStream(instant);
    expect(createdInstantAS.createdInstant).toBe(instant);
  });
  describe("#init", () => {
    beforeEach(() => {
      as.init();
    });
    it("should initialize default prefs", () => {
      expect(as._defaultPrefs.init).toHaveBeenCalledTimes(1);
    });
    it("should set .initialized to true", () => {
      expect(as.initialized).toBe(true);
    });
    it("should call .store.init", () => {
      expect(as.store.init).toHaveBeenCalledTimes(1);
    });
    it("should pass to Store an INIT event for content", () => {
      const [[, action]] = as.store.init.mock.calls;
      expect(action.meta.to).toBe(CONTENT_MESSAGE_TYPE);
    });
    it("should pass to Store an UNINIT event", () => {
      const [[, , action]] = as.store.init.mock.calls;
      expect(action.type).toBe("UNINIT");
    });
    it("should call addObserver for the app locales", () => {
      as.init();
      expect(services.obs.addObserver).toHaveBeenCalledWith(
        as,
        "intl:app-locales-changed"
      );
    });
    it("should call addObserver for the market gate pref", () => {
      expect(services.prefs.addObserver).toHaveBeenCalledWith(
        MARKET_GATE_PREF,
        as
      );
    });
  });
  describe("#uninit", () => {
    beforeEach(() => {
      as.init();
      as.uninit();
    });
    it("should set .initialized to false", () => {
      expect(as.initialized).toBe(false);
    });
    it("should call .store.uninit", () => {
      expect(as.store.uninit).toHaveBeenCalledTimes(1);
    });
    it("should call removeObserver for the region", () => {
      as.geo = "";
      as.uninit();
      expect(services.obs.removeObserver).toHaveBeenCalledWith(
        as,
        region.REGION_TOPIC
      );
    });
    it("should call removeObserver for the app locales", () => {
      as.uninit();
      expect(services.obs.removeObserver).toHaveBeenCalledWith(
        as,
        "intl:app-locales-changed"
      );
    });
    it("should call removeObserver for the market gate pref", () => {
      expect(services.prefs.removeObserver).toHaveBeenCalledWith(
        MARKET_GATE_PREF,
        as
      );
    });
  });
  describe("#observe", () => {
    it("should call _updateDynamicPrefs from observe", () => {
      const updateStub = jest
        .spyOn(as, "_updateDynamicPrefs")
        .mockImplementation(() => {});
      as.observe(undefined, region.REGION_TOPIC);
      expect(updateStub).toHaveBeenCalledTimes(1);
    });
  });
  describe("feeds", () => {
    it("should create a NewTabInit feed", () => {
      const feed = as.feeds.get("feeds.newtabinit")();
      expect(feed).toBeTruthy();
    });
    it("should create a Places feed", () => {
      const feed = as.feeds.get("feeds.places")();
      expect(feed).toBeTruthy();
    });
    it("should create a TopSites feed", () => {
      const feed = as.feeds.get("feeds.system.topsites")();
      expect(feed).toBeTruthy();
    });
    it("should create a Telemetry feed", () => {
      const feed = as.feeds.get("feeds.telemetry")();
      expect(feed).toBeTruthy();
    });
    it("should create a Prefs feed", () => {
      const feed = as.feeds.get("feeds.prefs")();
      expect(feed).toBeTruthy();
    });
    it("should create a HighlightsFeed feed", () => {
      const feed = as.feeds.get("feeds.section.highlights")();
      expect(feed).toBeTruthy();
    });
    it("should create a TopStoriesFeed feed", () => {
      const feed = as.feeds.get("feeds.system.topstories")();
      expect(feed).toBeTruthy();
    });
    it("should create a AboutPreferences feed", () => {
      const feed = as.feeds.get("feeds.aboutpreferences")();
      expect(feed).toBeTruthy();
    });
    it("should create a SectionsFeed", () => {
      const feed = as.feeds.get("feeds.sections")();
      expect(feed).toBeTruthy();
    });
    it("should create a SystemTick feed", () => {
      const feed = as.feeds.get("feeds.systemtick")();
      expect(feed).toBeTruthy();
    });
    it("should create a Favicon feed", () => {
      const feed = as.feeds.get("feeds.favicon")();
      expect(feed).toBeTruthy();
    });
    it("should create a DiscoveryStreamFeed feed", () => {
      const feed = as.feeds.get("feeds.discoverystreamfeed")();
      expect(feed).toBeTruthy();
    });
  });
  describe("_migratePref", () => {
    it("should migrate a pref if the user has set a custom value", () => {
      services.prefs.prefHasUserValue.mockReturnValue(true);
      services.prefs.getPrefType.mockReturnValue(services.prefs.PREF_INT);
      services.prefs.getIntPref.mockReturnValue(10);
      const callback = jest.fn();
      as._migratePref("oldPrefName", callback);
      expect(callback).toHaveBeenCalledWith(10);
    });
    it("should not migrate a pref if the user has not set a custom value", () => {
      // we bailed out early so we don't check the pref type later
      services.prefs.prefHasUserValue.mockReturnValue(false);
      as._migratePref("oldPrefName");
      expect(services.prefs.getPrefType).not.toHaveBeenCalled();
    });
    it("should use the proper pref getter for each type", () => {
      services.prefs.prefHasUserValue.mockReturnValue(true);

      // Integer
      services.prefs.getPrefType.mockReturnValue(services.prefs.PREF_INT);
      as._migratePref("oldPrefName", () => {});
      expect(services.prefs.getIntPref).toHaveBeenCalledWith("oldPrefName");

      // Boolean
      services.prefs.getPrefType.mockReturnValue(services.prefs.PREF_BOOL);
      as._migratePref("oldPrefName", () => {});
      expect(services.prefs.getBoolPref).toHaveBeenCalledWith("oldPrefName");

      // String
      services.prefs.getPrefType.mockReturnValue(services.prefs.PREF_STRING);
      as._migratePref("oldPrefName", () => {});
      expect(services.prefs.getStringPref).toHaveBeenCalledWith("oldPrefName");
    });
    it("should clear the old pref after setting the new one", () => {
      services.prefs.prefHasUserValue.mockReturnValue(true);
      services.prefs.getPrefType.mockReturnValue(services.prefs.PREF_INT);
      as._migratePref("oldPrefName", () => {});
      expect(services.prefs.clearUserPref).toHaveBeenCalledWith("oldPrefName");
    });
  });
  describe("csvPrefHasValue", () => {
    let getStringPrefStub;
    beforeEach(() => {
      getStringPrefStub = argsStub();
      services.prefs.getStringPref = getStringPrefStub;
      region.home = "CA";
      services.locale.appLocaleAsBCP47 = "en-CA";
    });
    it("throws an error when the pref argument is not a string", () => {
      expect(() => csvPrefHasValue(0, "foo")).toThrow(
        new Error("The stringPrefName argument is not a string")
      );
    });
    it("returns true if pref contains test value", () => {
      getStringPrefStub.whenCalledWith("example.csvPref").returns("foo,bar");
      const featureCheck = csvPrefHasValue("example.csvPref", "foo");
      expect(featureCheck).toBe(true);
    });
    it("returns false if pref contains test value", () => {
      getStringPrefStub.whenCalledWith("example.csvPref").returns("foo,bar");
      const featureCheck = csvPrefHasValue("example.csvPref", "baz");
      expect(featureCheck).toBe(false);
    });
    it("returns false if pref value returns empty", () => {
      getStringPrefStub.whenCalledWith("example.csvPref").returns("");
      const featureCheck = csvPrefHasValue("example.csvPref", "foo");
      expect(featureCheck).toBe(false);
    });
    it("returns false if test value is blank", () => {
      getStringPrefStub.whenCalledWith("example.csvPref").returns("foo,bar");
      const featureCheck = csvPrefHasValue("example.csvPref", "");
      expect(featureCheck).toBe(false);
    });
  });
  describe("showWeather", () => {
    let getStringPrefStub;
    const FEATURE_ENABLED_PREF = "system.showWeather";
    const REGION_WEATHER_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.region-weather-config";
    const LOCALE_WEATHER_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.locale-weather-config";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";

      getStringPrefStub = argsStub();
      services.prefs.getStringPref = getStringPrefStub;

      // Set default regions
      getStringPrefStub.whenCalledWith(REGION_WEATHER_CONFIG).returns("US, CA");

      // Set default locales
      getStringPrefStub
        .whenCalledWith(LOCALE_WEATHER_CONFIG)
        .returns("en-US,en-GB,en-CA");
    });
    it("should turn off when region and locale are not set", () => {
      region.home = "";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region is not set", () => {
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when region is supported", () => {
      region.home = "US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when region is not supported", () => {
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when locale is not set", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when locale is supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "en-US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when locale is not supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region and locale are both not supported", () => {
      region.home = "FR";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
  });
  describe("marketGate", () => {
    let getStringPrefStub;
    const AVAILABLE_PREF = "widgets.system.enabled";
    const ENABLED_PREF = "widgets.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it("should be on everywhere when no lists are set", () => {
      region.home = "KE";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(true);
    });
    it("should turn off a blocked region", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-block`)
        .returns("CN,JP,RU");
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it("should let the block list win over the allow list", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-config`)
        .returns("JP,CZ");
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-block`)
        .returns("JP");
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it("should turn off a region absent from a populated allow list", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-config`)
        .returns("CZ");
      region.home = "KE";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it("should ignore locale when only the region lists are set", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-config`)
        .returns("CZ");
      region.home = "CZ";
      services.locale.appLocaleAsBCP47 = "cs";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
    });
    it("should turn off a blocked locale", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.locale-block`)
        .returns("en-US");
      region.home = "CZ";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it("should wait for the region when a list restricts it", () => {
      // Bug 2063361: showing the widget and then taking it away is worse than
      // a brief absence, so an unresolved region loses to a populated list.
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-block`)
        .returns("CN,JP,RU");
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it("should not wait for the region when no list restricts it", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-block`)
        .returns("");
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(true);
    });
    it("should ignore the lists on Nightly", () => {
      const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
      globalThis.AppConstants.NIGHTLY_BUILD = true;
      try {
        getStringPrefStub
          .whenCalledWith(`${BRANCH}widgets.system.region-block`)
          .returns("CN,JP,RU");
        region.home = "JP";
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
      } finally {
        globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
      }
    });
  });
  describe("marketGate - lists", () => {
    let getStringPrefStub;
    const CONTAINER_PREF = "widgets.system.enabled";
    const AVAILABLE_PREF = "widgets.system.lists.enabled";
    const ENABLED_PREF = "widgets.lists.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each(["CZ", "US"])(
      "should be available but off by default in %s",
      geo => {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      }
    );
    it("should not follow the container block list", () => {
      // Blocking the container does not narrow the widget's own prefs; the
      // container gate is applied separately by isWidgetsContainerVisible.
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.region-block`)
        .returns("JP");
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(CONTAINER_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
    });
    it("should be unavailable and off in PL", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.lists.region-block`)
        .returns("PL");
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
    });
  });
  describe("marketGate - focus timer", () => {
    let getStringPrefStub;
    const AVAILABLE_PREF = "widgets.system.focusTimer.enabled";
    const ENABLED_PREF = "widgets.focusTimer.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each(["CZ", "US"])(
      "should be available but off by default in %s",
      geo => {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      }
    );
    it("should be unavailable and off in PL", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.focusTimer.region-block`)
        .returns("PL");
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
    });
  });
  describe("marketGate - clocks", () => {
    let getStringPrefStub;
    const AVAILABLE_PREF = "widgets.system.clocks.enabled";
    const ENABLED_PREF = "widgets.clocks.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each(["CZ", "US"])(
      "should be available but off by default in %s",
      geo => {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      }
    );
    it("should be unavailable and off in PL", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.clocks.region-block`)
        .returns("PL");
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
    });
  });
  describe("marketGate - picture of the day", () => {
    let getStringPrefStub;
    const AVAILABLE_PREF = "widgets.system.pictureOfTheDay.enabled";
    const ENABLED_PREF = "widgets.pictureOfTheDay.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each(["CZ", "US"])(
      "should be available but off by default in %s",
      geo => {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      }
    );
    it("should be unavailable and off in PL", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.pictureOfTheDay.region-block`)
        .returns("PL");
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
    });
  });
  describe("market-gated widgets on Nightly", () => {
    const BRANCH = "browser.newtabpage.activity-stream.";
    it.each([
      ["lists", false, true],
      ["focusTimer", false, true],
      ["clocks", false, true],
      ["pictureOfTheDay", false, true],
      ["crossword", false, true],
      ["lists", true, false],
      ["focusTimer", true, false],
      ["clocks", true, false],
      ["pictureOfTheDay", true, false],
      ["crossword", true, false],
    ])(
      "should make %s available on Nightly (enforceOnNightly=%s, on=%s)",
      (widget, enforce, expected) => {
        const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
        globalThis.AppConstants.NIGHTLY_BUILD = true;
        services.prefs.getStringPref = argsStub(
          (_pref, defaultValue) => defaultValue
        );
        services.prefs.getBoolPref
          .whenCalledWith(`${BRANCH}widgets.marketGate.enforceOnNightly`, false)
          .returns(enforce);
        try {
          region.home = "US";
          services.locale.appLocaleAsBCP47 = "en-US";
          as._updateDynamicPrefs();
          expect(
            PREFS_CONFIG.get(`widgets.system.${widget}.enabled`).value
          ).toBe(true);
          expect(PREFS_CONFIG.get(`widgets.${widget}.enabled`).value).toBe(
            expected
          );
        } finally {
          globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
        }
      }
    );
  });
  describe("privacy widget defaults", () => {
    const AVAILABLE_PREF = "widgets.system.privacy.enabled";
    const ENABLED_PREF = "widgets.privacy.enabled";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      services.prefs.getStringPref = argsStub(
        (_pref, defaultValue) => defaultValue
      );
    });
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
    });
    it("should stay unavailable and off in Nightly", () => {
      const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
      globalThis.AppConstants.NIGHTLY_BUILD = true;
      try {
        region.home = "US";
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      } finally {
        globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
      }
    });
  });
  describe("marketGate - crossword", () => {
    const AVAILABLE_PREF = "widgets.system.crossword.enabled";
    const ENABLED_PREF = "widgets.crossword.enabled";
    const BRANCH = "browser.newtabpage.activity-stream.";
    beforeEach(() => {
      const getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.crossword.locale-config`)
        .returns("en-CA,en-GB,en-US");
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.crossword.region-block`)
        .returns("PL");
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each([
      ["US", "en-US"],
      ["DE", "en-GB"],
      ["JP", "en-CA"],
    ])(
      "should be available but off by default in %s with %s",
      (geo, locale) => {
        region.home = geo;
        services.locale.appLocaleAsBCP47 = locale;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(true);
        expect(PREFS_CONFIG.get(ENABLED_PREF).value).toBe(false);
      }
    );
    it.each(["de", "fr", "ja"])(
      "should be unavailable in a non-English locale (%s)",
      locale => {
        region.home = "US";
        services.locale.appLocaleAsBCP47 = locale;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
      }
    );
    it("should be unavailable in PL even in an English locale", () => {
      region.home = "PL";
      services.locale.appLocaleAsBCP47 = "en-US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(false);
    });
    it.each([
      [false, true],
      [true, false],
    ])(
      "on Nightly with enforceOnNightly=%s, availability in de is %s",
      (enforce, expected) => {
        const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
        globalThis.AppConstants.NIGHTLY_BUILD = true;
        services.prefs.getBoolPref = argsStub(
          (_pref, defaultValue) => defaultValue
        );
        services.prefs.getBoolPref
          .whenCalledWith(`${BRANCH}widgets.marketGate.enforceOnNightly`, false)
          .returns(enforce);
        services.prefs.getBoolPref
          .whenCalledWith(MARKET_GATE_PREF)
          .returns(true);
        try {
          region.home = "US";
          services.locale.appLocaleAsBCP47 = "de";
          as._updateDynamicPrefs();
          expect(PREFS_CONFIG.get(AVAILABLE_PREF).value).toBe(expected);
        } finally {
          globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
        }
      }
    );
  });
  describe("market gating on by default", () => {
    it("should make widgets available when nothing sets the pref", () => {
      services.prefs.getBoolPref = argsStub(
        (_pref, defaultValue) => defaultValue
      );
      services.prefs.getStringPref = argsStub(
        (_pref, defaultValue) => defaultValue
      );
      region.home = "CZ";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.marketGate.enabled").value).toBe(true);
      expect(PREFS_CONFIG.get("widgets.system.lists.enabled").value).toBe(true);
    });
  });
  describe("market gating turned off", () => {
    const SYSTEM_PREFS = [
      "widgets.system.enabled",
      "widgets.system.lists.enabled",
      "widgets.system.focusTimer.enabled",
      "widgets.system.clocks.enabled",
      "widgets.system.pictureOfTheDay.enabled",
      "widgets.system.crossword.enabled",
    ];
    const USER_PREFS = [
      "widgets.enabled",
      "widgets.lists.enabled",
      "widgets.focusTimer.enabled",
      "widgets.clocks.enabled",
      "widgets.pictureOfTheDay.enabled",
      "widgets.privacy.enabled",
      "widgets.crossword.enabled",
      "widgets.stocks.enabled",
      "widgets.recentSearches.enabled",
      "widgets.horoscopes.enabled",
      "widgets.travel.enabled",
      "widgets.solitaire.enabled",
    ];
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      services.prefs.getStringPref = argsStub(
        (_pref, defaultValue) => defaultValue
      );
      services.prefs.getBoolPref = argsStub(
        (_pref, defaultValue) => defaultValue
      );
      services.prefs.getBoolPref
        .whenCalledWith(MARKET_GATE_PREF)
        .returns(false);
      region.home = "US";
    });
    it.each([false, true])(
      "should restore the pre-gating defaults (Nightly: %s)",
      nightly => {
        const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
        globalThis.AppConstants.NIGHTLY_BUILD = nightly;
        try {
          as._updateDynamicPrefs();
          for (const pref of SYSTEM_PREFS) {
            expect([pref, PREFS_CONFIG.get(pref).value]).toEqual([pref, false]);
          }
          for (const pref of USER_PREFS) {
            expect([pref, PREFS_CONFIG.get(pref).value]).toEqual([pref, true]);
          }
        } finally {
          globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
        }
      }
    );
    it("should rewrite the default branch each time the pref flips", () => {
      let gated = true;
      services.prefs.getBoolPref = jest.fn((pref, defaultValue) =>
        pref === MARKET_GATE_PREF ? gated : defaultValue
      );
      const wasNightly = globalThis.AppConstants.NIGHTLY_BUILD;
      globalThis.AppConstants.NIGHTLY_BUILD = false;
      const defaults = () => [
        as._defaultPrefs.get("widgets.system.lists.enabled"),
        as._defaultPrefs.get("widgets.lists.enabled"),
        as._defaultPrefs.get("widgets.privacy.enabled"),
      ];
      try {
        as._updateDynamicPrefs();

        gated = false;
        as.observe(null, "nsPref:changed", MARKET_GATE_PREF);
        expect(defaults()).toEqual([false, true, true]);

        gated = true;
        as.observe(null, "nsPref:changed", MARKET_GATE_PREF);
        expect(defaults()).toEqual([true, false, false]);
      } finally {
        globalThis.AppConstants.NIGHTLY_BUILD = wasNightly;
      }
    });
  });
  describe("market gating on a host without the firefox.js prefs", () => {
    const BRANCH = "browser.newtabpage.activity-stream.";
    let getStringPrefStub;
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";
      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;
    });
    it.each(["CN", "JP", "RU"])(
      "should make widgets available and on in %s",
      geo => {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(PREFS_CONFIG.get("widgets.system.enabled").value).toBe(true);
        expect(PREFS_CONFIG.get("widgets.enabled").value).toBe(true);
      }
    );
    it("should keep Lists available but off in the US", () => {
      region.home = "US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.system.lists.enabled").value).toBe(true);
      expect(PREFS_CONFIG.get("widgets.lists.enabled").value).toBe(false);
    });
    it("should make Lists unavailable in PL", () => {
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.system.lists.enabled").value).toBe(
        false
      );
    });
    it("should keep Crossword English-only", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "de";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.system.crossword.enabled").value).toBe(
        false
      );
    });
    it("should have a fallback matching every widget market pref in firefox.js", () => {
      const firefoxJs = readFileSync(
        path.resolve(__dirname, "../../../../../app/profile/firefox.js"),
        "utf8"
      );
      const prefs = [
        ...firefoxJs.matchAll(
          /^pref\("(browser\.newtabpage\.activity-stream\.widgets\.[^"]+\.(?:region|locale)-(?:block|config))",\s*"([^"]*)"\);/gm
        ),
      ].map(([, name, value]) => [name, value]);
      expect(prefs.length).toBeGreaterThan(0);
      for (const [name, value] of prefs) {
        expect([name, MARKET_PREF_FALLBACKS.get(name)]).toEqual([name, value]);
      }
    });
    it("should let a pref that exists win over the fallback", () => {
      getStringPrefStub
        .whenCalledWith(`${BRANCH}widgets.system.lists.region-block`)
        .returns("");
      region.home = "PL";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.system.lists.enabled").value).toBe(true);
    });
  });
  describe("stocks widget defaults", () => {
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.stocks.enabled").value).toBe(false);
      expect(PREFS_CONFIG.get("widgets.system.stocks.enabled").value).toBe(
        false
      );
    });
  });
  describe("recent searches widget defaults", () => {
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.recentSearches.enabled").value).toBe(
        false
      );
      expect(
        PREFS_CONFIG.get("widgets.system.recentSearches.enabled").value
      ).toBe(false);
    });
  });
  describe("horoscopes widget defaults", () => {
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.horoscopes.enabled").value).toBe(false);
      expect(PREFS_CONFIG.get("widgets.system.horoscopes.enabled").value).toBe(
        false
      );
    });
  });
  describe("travel widget defaults", () => {
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.travel.enabled").value).toBe(false);
      expect(PREFS_CONFIG.get("widgets.system.travel.enabled").value).toBe(
        false
      );
    });
  });
  describe("solitaire widget defaults", () => {
    it("should be off everywhere by default", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("widgets.solitaire.enabled").value).toBe(false);
      expect(PREFS_CONFIG.get("widgets.system.solitaire.enabled").value).toBe(
        false
      );
    });
  });
  describe("getWeatherWidgetSize", () => {
    let getBoolPrefStub;
    let getStringPrefStub;
    const PREF = "widgets.weather.size";
    const FORECAST_PREF =
      "browser.newtabpage.activity-stream.widgets.system.weatherForecast.enabled";
    const MAXIMIZED_PREF =
      "browser.newtabpage.activity-stream.widgets.maximized";
    const DISPLAY_PREF = "browser.newtabpage.activity-stream.weather.display";

    beforeEach(() => {
      getBoolPrefStub = argsStub();
      services.prefs.getBoolPref = getBoolPrefStub;
      getBoolPrefStub.whenCalledWith(FORECAST_PREF, false).returns(false);
      getBoolPrefStub.whenCalledWith(MAXIMIZED_PREF, true).returns(true);
      getStringPrefStub = argsStub((_pref, defaultVal) => defaultVal);
      services.prefs.getStringPref = getStringPrefStub;
      getStringPrefStub
        .whenCalledWith(DISPLAY_PREF, "detailed")
        .returns("detailed");
    });

    it("should return small when forecast system pref is disabled", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(PREF).value).toBe("small");
    });

    it("should return small when forecast is enabled but display is not detailed", () => {
      getBoolPrefStub.whenCalledWith(FORECAST_PREF, false).returns(true);
      getStringPrefStub
        .whenCalledWith(DISPLAY_PREF, "detailed")
        .returns("simple");
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(PREF).value).toBe("small");
    });

    it("should return large when forecast is enabled and widgets are maximized", () => {
      getBoolPrefStub.whenCalledWith(FORECAST_PREF, false).returns(true);
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(PREF).value).toBe("large");
    });

    it("should return medium when forecast is enabled but widgets are not maximized", () => {
      getBoolPrefStub.whenCalledWith(FORECAST_PREF, false).returns(true);
      getBoolPrefStub.whenCalledWith(MAXIMIZED_PREF, true).returns(false);
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(PREF).value).toBe("medium");
    });
  });
  describe("newtabWallpapers.customWallpaper.library.enabled", () => {
    // firefox.js turns the library on from 158. Turning it on here as well
    // would reach a train-hopped host whose backup keeps one wallpaper.
    it("should default to false", () => {
      expect(
        PREFS_CONFIG.get("newtabWallpapers.customWallpaper.library.enabled")
          .value
      ).toBe(false);
    });
  });
  describe("showTopicsSelection", () => {
    let getStringPrefStub;
    const FEATURE_ENABLED_PREF = "discoverystream.topicSelection.enabled";
    const REGION_TOPICS_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.topicSelection.region-topics-config";
    const LOCALE_TOPICS_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.topicSelection.locale-topics-config";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";

      getStringPrefStub = argsStub();
      services.prefs.getStringPref = getStringPrefStub;

      // Set default regions
      getStringPrefStub.whenCalledWith(REGION_TOPICS_CONFIG).returns("US, CA");

      // Set default locales
      getStringPrefStub
        .whenCalledWith(LOCALE_TOPICS_CONFIG)
        .returns("en-US,en-GB,en-CA");
    });
    it("should turn off when region and locale are not set", () => {
      region.home = "";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region is not set", () => {
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when region is supported", () => {
      region.home = "US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when region is not supported", () => {
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when locale is not set", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when locale is supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "en-US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when locale is not supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region and locale are both not supported", () => {
      region.home = "FR";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
  });
  describe("showTopicLabels", () => {
    let getStringPrefStub;
    const FEATURE_ENABLED_PREF = "discoverystream.topicLabels.enabled";
    const REGION_TOPIC_LABEL_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.topicLabels.region-topic-label-config";
    const LOCALE_TOPIC_LABEL_CONFIG =
      "browser.newtabpage.activity-stream.discoverystream.topicLabels.locale-topic-label-config";
    beforeEach(() => {
      services.locale.appLocaleAsBCP47 = "en-US";

      getStringPrefStub = argsStub();
      services.prefs.getStringPref = getStringPrefStub;

      // Set default regions
      getStringPrefStub
        .whenCalledWith(REGION_TOPIC_LABEL_CONFIG)
        .returns("US, CA");

      // Set default locales
      getStringPrefStub
        .whenCalledWith(LOCALE_TOPIC_LABEL_CONFIG)
        .returns("en-US,en-GB,en-CA");
    });
    it("should turn off when region and locale are not set", () => {
      region.home = "";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region is not set", () => {
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when region is supported", () => {
      region.home = "US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when region is not supported", () => {
      region.home = "JP";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when locale is not set", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn on when locale is supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "en-US";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(true);
    });
    it("should turn off when locale is not supported", () => {
      region.home = "US";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
    it("should turn off when region and locale are both not supported", () => {
      region.home = "FR";
      services.locale.appLocaleAsBCP47 = "fr";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(FEATURE_ENABLED_PREF).value).toBe(false);
    });
  });
  describe("discoverystream.region-basic-layout config", () => {
    let getStringPrefStub;
    beforeEach(() => {
      getStringPrefStub = argsStub();
      services.prefs.getStringPref = getStringPrefStub;
      region.home = "CA";
      services.locale.appLocaleAsBCP47 = "en-CA";
    });
    it("should enable 7 row layout pref if no basic config is set and no geo is set", () => {
      getStringPrefStub
        .whenCalledWith(
          "browser.newtabpage.activity-stream.discoverystream.region-basic-config"
        )
        .returns("");
      region.home = "";

      as._updateDynamicPrefs();

      expect(
        PREFS_CONFIG.get("discoverystream.region-basic-layout").value
      ).toBe(false);
    });
    it("should enable 1 row layout pref based on region layout pref", () => {
      getStringPrefStub
        .whenCalledWith(
          "browser.newtabpage.activity-stream.discoverystream.region-basic-config"
        )
        .returns("CA");

      as._updateDynamicPrefs();

      expect(
        PREFS_CONFIG.get("discoverystream.region-basic-layout").value
      ).toBe(true);
    });
    it("should enable 7 row layout pref based on region layout pref", () => {
      getStringPrefStub
        .whenCalledWith(
          "browser.newtabpage.activity-stream.discoverystream.region-basic-config"
        )
        .returns("");

      as._updateDynamicPrefs();

      expect(
        PREFS_CONFIG.get("discoverystream.region-basic-layout").value
      ).toBe(false);
    });
  });
  describe("_updateDynamicPrefs topstories default value", () => {
    let getVariableStub;
    let getBoolPrefStub;
    let getStringPrefStub;
    beforeEach(() => {
      getVariableStub = argsStub();
      nimbusFeatures.pocketNewtab.getVariable = getVariableStub;

      getStringPrefStub = argsStub((_pref, defaultValue) => defaultValue);
      services.prefs.getStringPref = getStringPrefStub;

      getBoolPrefStub = argsStub();
      services.prefs.getBoolPref = getBoolPrefStub;
      getBoolPrefStub
        .whenCalledWith(
          "browser.newtabpage.activity-stream.feeds.section.topstories"
        )
        .returns(true);

      services.locale.appLocaleAsBCP47 = "en-US";

      region.home = "US";

      getStringPrefStub.whenCalledWith(STORIES_REGION_LOCALE_PREF).returns(
        JSON.stringify([
          ["US", ["en-*"]],
          ["CA", ["en-*"]],
        ])
      );
    });
    it("should be false with no geo/locale", () => {
      services.locale.appLocaleAsBCP47 = "";
      region.home = "";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false with no geo but an allowed locale", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      region.home = "";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false with unexpected geo", () => {
      region.home = "NOGEO";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false with expected geo and unexpected locale", () => {
      services.locale.appLocaleAsBCP47 = "no-LOCALE";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be true with expected geo and locale", () => {
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be false after expected geo and locale then unexpected", () => {
      region.home = "US";
      as._updateDynamicPrefs();

      region.home = "NOGEO";
      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be true with updated pref change", () => {
      services.locale.appLocaleAsBCP47 = "en-GB";
      region.home = "GB";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["GB", ["en-*"]]]));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be true with a locale matched by a pattern", () => {
      services.locale.appLocaleAsBCP47 = "en-GB";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be true in any region with a wildcard region entry", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      region.home = "MX";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["*", ["en-*"]]]));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be false for a locale the wildcard region does not list", () => {
      services.locale.appLocaleAsBCP47 = "es-MX";
      region.home = "MX";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["*", ["en-*"]]]));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be true for a region added alongside a wildcard entry", () => {
      services.locale.appLocaleAsBCP47 = "es-MX";
      region.home = "MX";
      getStringPrefStub.whenCalledWith(STORIES_REGION_LOCALE_PREF).returns(
        JSON.stringify([
          ["*", ["en-*"]],
          ["MX", ["es-*"]],
        ])
      );

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be false with a blocked region despite a wildcard entry", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      region.home = "MX";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["*", ["en-*"]]]));
      getVariableStub.whenCalledWith("regionStoriesBlock").returns("MX");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should not match a variant against an exact pattern", () => {
      services.locale.appLocaleAsBCP47 = "es-MX";
      region.home = "ES";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["ES", ["es-ES"]]]));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should ignore casing in regions and locales", () => {
      services.locale.appLocaleAsBCP47 = "EN-us";
      region.home = "us";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["us", ["EN-*"]]]));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should keep only the last entry for a region", () => {
      services.locale.appLocaleAsBCP47 = "de";
      region.home = "BE";
      getStringPrefStub.whenCalledWith(STORIES_REGION_LOCALE_PREF).returns(
        JSON.stringify([
          ["BE", ["de"]],
          ["BE", ["fr"]],
        ])
      );

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false with a geo but no locale", () => {
      services.locale.appLocaleAsBCP47 = "";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should fall back when the config parses to something else", () => {
      services.locale.appLocaleAsBCP47 = "es-MX";
      region.home = "MX";
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify({ MX: ["es-*"] }));

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false everywhere with an emptied config", () => {
      getStringPrefStub.whenCalledWith(STORIES_REGION_LOCALE_PREF).returns("");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should fall back to the built-in list with an invalid config", () => {
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns("not json");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be true for a trainhop region/locale pair", () => {
      services.locale.appLocaleAsBCP47 = "es-MX";
      region.home = "MX";
      nimbusFeatures.newtabTrainhop.getAllEnrollments = jest.fn(() => [
        {
          value: {
            type: "multi-payload",
            payload: [
              {
                type: "storiesRegionLocale",
                payload: { config: [["MX", ["es-*"]]] },
              },
            ],
          },
          meta: { isRollout: true },
        },
      ]);

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should let the trainhop payload replace the shipped pairs", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      nimbusFeatures.newtabTrainhop.getAllEnrollments = jest.fn(() => [
        {
          value: {
            type: "storiesRegionLocale",
            payload: { config: [["MX", ["es-*"]]] },
          },
          meta: { isRollout: true },
        },
      ]);

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should prefer an experiment payload over a rollout payload", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      nimbusFeatures.newtabTrainhop.getAllEnrollments = jest.fn(() => [
        {
          value: {
            type: "storiesRegionLocale",
            payload: { config: [["MX", ["es-*"]]] },
          },
          meta: { isRollout: true },
        },
        {
          value: {
            type: "storiesRegionLocale",
            payload: { config: [["US", ["en-*"]]] },
          },
          meta: { isRollout: false },
        },
      ]);

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be true for a legacy region/locale pair", () => {
      services.locale.appLocaleAsBCP47 = "pl";
      region.home = "PL";
      getVariableStub
        .whenCalledWith("regionStoriesConfig")
        .returns("US,DE,CA,GB,IE,CH,AT,BE,IN,FR,IT,ES,PL");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should be false for a legacy region without its locale", () => {
      services.locale.appLocaleAsBCP47 = "en-US";
      region.home = "PL";
      getVariableStub
        .whenCalledWith("regionStoriesConfig")
        .returns("US,DE,CA,GB,IE,CH,AT,BE,IN,FR,IT,ES,PL");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false for a legacy region left out of the config", () => {
      services.locale.appLocaleAsBCP47 = "pl";
      region.home = "PL";
      getVariableStub
        .whenCalledWith("regionStoriesConfig")
        .returns("US,DE,CA,GB,IE,CH,AT,BE,IN,FR,IT,ES");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should be false with a blocked region in the legacy config", () => {
      services.locale.appLocaleAsBCP47 = "pl";
      region.home = "PL";
      getVariableStub.whenCalledWith("regionStoriesConfig").returns("PL");
      getVariableStub.whenCalledWith("regionStoriesBlock").returns("PL");

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should use the built-in list with no config set", () => {
      getStringPrefStub
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(undefined);
      services.locale.appLocaleAsBCP47 = "de";
      region.home = "DE";

      as._updateDynamicPrefs();

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
  });
  describe("_updateDynamicPrefs topstories delayed default value", () => {
    const notifyRegionUpdated = () =>
      services.obs.notifyObservers("US", "browser-region-updated");

    beforeEach(() => {
      jest.useFakeTimers();

      // Have addObserver cause prefHasUserValue to now return true then observe
      services.obs.addObserver.mockImplementation(() => {
        setTimeout(notifyRegionUpdated);
      });
    });
    afterEach(() => jest.useRealTimers());

    it("should set false with unexpected geo", () => {
      region.home = "NOGEO";

      as._updateDynamicPrefs();

      jest.advanceTimersByTime(1);

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should set true with expected geo and locale", () => {
      services.prefs.getStringPref = argsStub();
      services.prefs.getStringPref
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["US", ["en-*"]]]));

      services.prefs.getBoolPref = jest.fn(() => true);
      services.locale.appLocaleAsBCP47 = "en-US";

      as._updateDynamicPrefs();
      jest.advanceTimersByTime(1);

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(true);
    });
    it("should not change default even with expected geo and locale", () => {
      as._defaultPrefs.set("feeds.system.topstories", false);
      services.prefs.getStringPref = argsStub();
      services.prefs.getStringPref
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["US", ["en-*"]]]));

      services.locale.appLocaleAsBCP47 = "en-US";

      as._updateDynamicPrefs();
      jest.advanceTimersByTime(1);

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
    it("should set false with geo blocked", () => {
      const getVariableStub = argsStub();
      nimbusFeatures.pocketNewtab.getVariable = getVariableStub;
      services.prefs.getStringPref = argsStub();
      services.prefs.getStringPref
        .whenCalledWith(STORIES_REGION_LOCALE_PREF)
        .returns(JSON.stringify([["US", ["en-*"]]]));
      getVariableStub.whenCalledWith("regionStoriesBlock").returns("US");

      services.prefs.getBoolPref = jest.fn(() => true);
      services.locale.appLocaleAsBCP47 = "en-US";

      as._updateDynamicPrefs();
      jest.advanceTimersByTime(1);

      expect(PREFS_CONFIG.get("feeds.system.topstories").value).toBe(false);
    });
  });
  describe("searchs shortcuts shouldPin pref", () => {
    const SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF =
      "improvesearch.topSiteSearchShortcuts.searchEngines";

    it("should be an empty string when no geo is available", () => {
      region.home = "";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF).value).toBe(
        ""
      );
    });

    it("should be 'baidu' in China", () => {
      region.home = "CN";
      as._updateDynamicPrefs();
      expect(PREFS_CONFIG.get(SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF).value).toBe(
        "baidu"
      );
    });

    it("should be 'yandex' in Russia, Belarus, Kazakhstan, and Turkey", () => {
      const geos = ["BY", "KZ", "RU", "TR"];
      for (const geo of geos) {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(
          PREFS_CONFIG.get(SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF).value
        ).toBe("yandex");
      }
    });

    it("should be 'google,amazon' in Germany, France, the UK, Japan, Italy, and the US", () => {
      const geos = ["DE", "FR", "GB", "IT", "JP", "US"];
      for (const geo of geos) {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(
          PREFS_CONFIG.get(SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF).value
        ).toBe("google,amazon");
      }
    });

    it("should be 'google' elsewhere", () => {
      // A selection of other geos
      const geos = ["BR", "CA", "ES", "ID", "IN"];
      for (const geo of geos) {
        region.home = geo;
        as._updateDynamicPrefs();
        expect(
          PREFS_CONFIG.get(SEARCH_SHORTCUTS_SEARCH_ENGINES_PREF).value
        ).toBe("google");
      }
    });
  });

  describe("proxying images", () => {
    let registerStub;
    let unregisterStub;

    beforeEach(() => {
      registerStub = jest
        .spyOn(as, "registerNetworkProxy")
        .mockImplementation(() => {});
      unregisterStub = jest
        .spyOn(as, "unregisterNetworkProxy")
        .mockImplementation(() => {});
    });

    describe("#init", () => {
      it("should call registerNetworkProxy during init", () => {
        as.init();
        expect(registerStub).toHaveBeenCalledTimes(1);
      });
    });

    describe("#uninit", () => {
      it("should call unregisterNetworkProxy during uninit", () => {
        as.init();
        as.uninit();
        expect(unregisterStub).toHaveBeenCalledTimes(1);
      });
    });

    describe("#getImageProxyConfig", () => {
      beforeEach(() => {
        as.initialized = true;
      });

      it("should return null when proxy config is missing", () => {
        as.store = {
          getState: () => ({
            Prefs: {
              values: {},
            },
          }),
        };

        const config = as.getImageProxyConfig();
        expect(config).toBeNull();
      });

      it("should return null when proxy is disabled", () => {
        as.store = {
          getState: () => ({
            Prefs: {
              values: {
                trainhopConfig: {
                  imageProxy: {
                    enabled: false,
                    proxyHost: "proxy.example.com",
                    proxyPort: 443,
                    proxyAuthHeader: "auth",
                  },
                },
                "discoverystream.sections.personalization.inferred.enabled": true,
              },
            },
          }),
        };

        const config = as.getImageProxyConfig();
        expect(config).toBeNull();
      });

      it("should return null when required fields are missing", () => {
        as.store = {
          getState: () => ({
            Prefs: {
              values: {
                trainhopConfig: {
                  imageProxy: {
                    enabled: true,
                    proxyHost: "proxy.example.com",
                  },
                },
                "discoverystream.sections.personalization.inferred.enabled": true,
              },
            },
          }),
        };

        const config = as.getImageProxyConfig();
        expect(config).toBeNull();
      });

      it("should return null when inferred personalization is disabled", () => {
        as.store = {
          getState: () => ({
            Prefs: {
              values: {
                trainhopConfig: {
                  imageProxy: {
                    enabled: true,
                    proxyHost: "proxy.example.com",
                    proxyPort: 443,
                    proxyAuthHeader: "auth",
                  },
                },
                "discoverystream.sections.personalization.inferred.enabled": false,
                "discoverystream.imageProxy.enabled": true,
              },
            },
          }),
        };

        const config = as.getImageProxyConfig();
        expect(config).toBeNull();
      });

      it("should return valid config when properly configured", () => {
        as.store = {
          getState: () => ({
            Prefs: {
              values: {
                trainhopConfig: {
                  imageProxy: {
                    enabled: true,
                    proxyHost: "host",
                    proxyPort: 1124,
                    proxyAuthHeader: "123",
                    connectionIsolationKey: "isolation-key",
                    failoverProxy: "failover.example.com",
                    imageProxyHosts: "host1.com,host2.com,host3.com",
                  },
                },
                "discoverystream.sections.personalization.inferred.enabled": true,
                "discoverystream.imageProxy.enabled": true,
              },
            },
          }),
        };

        const config = as.getImageProxyConfig();
        expect(config).toBeTruthy();
      });
    });

    describe("#applyFilter", () => {
      let mockChannel;
      let mockCallback;
      let mockProxyInfo;
      let mockBrowser;
      let mockCustomProxyInfo;
      let getConfigStub;
      let restoreFilterGlobals;

      beforeEach(() => {
        mockCallback = { onProxyFilterResult: jest.fn() };
        mockProxyInfo = {};
        mockCustomProxyInfo = {};
        mockBrowser = {};

        mockChannel = {
          URI: { host: "example.com", scheme: "https" },
          loadInfo: {
            browsingContext: { top: { embedderElement: mockBrowser } },
          },
        };

        getConfigStub = jest
          .spyOn(as, "getImageProxyConfig")
          .mockImplementation(() => {});

        proxyService = {
          newProxyInfo: jest.fn(() => mockCustomProxyInfo),
        };
        restoreFilterGlobals = stubGlobals({
          AboutNewTabParent: { loadedTabs: new Set([mockBrowser]) },
          ProxyService: proxyService,
        });
      });

      afterEach(() => {
        restoreFilterGlobals();
      });

      it("should pass through original proxy when config is null", () => {
        getConfigStub.mockReturnValue(null);

        as.applyFilter(mockChannel, mockProxyInfo, mockCallback);

        expect(mockCallback.onProxyFilterResult).toHaveBeenCalledTimes(1);
        expect(mockCallback.onProxyFilterResult).toHaveBeenCalledWith(
          mockProxyInfo
        );
      });

      it("should apply proxy for matching HTTPS host from newtab", () => {
        const config = {
          imageProxyHosts: ["example.com", "other.com"],
          proxyHost: "proxy.example.com",
          proxyPort: 443,
          proxyAuthHeader: "Bearer token",
          connectionIsolationKey: "key",
          failoverProxy: "failover.example.com",
        };
        getConfigStub.mockReturnValue(config);

        as.applyFilter(mockChannel, mockProxyInfo, mockCallback);

        expect(proxyService.newProxyInfo).toHaveBeenCalledTimes(1);
        expect(proxyService.newProxyInfo).toHaveBeenCalledWith(
          "https",
          config.proxyHost,
          config.proxyPort,
          config.proxyAuthHeader,
          config.connectionIsolationKey,
          0,
          5000,
          config.failoverProxy
        );

        expect(mockCallback.onProxyFilterResult).toHaveBeenCalledTimes(1);
        expect(mockCallback.onProxyFilterResult).toHaveBeenCalledWith(
          mockCustomProxyInfo
        );
      });
    });
  });
});

describe("WIDGET_REGISTRY pref coverage", () => {
  for (const widget of WIDGET_REGISTRY) {
    it(`should have enabledPref registered for widget "${widget.id}"`, () => {
      expect(PREFS_CONFIG.has(widget.enabledPref)).toBe(true);
    });

    it(`should have sizePref registered for widget "${widget.id}"`, () => {
      expect(PREFS_CONFIG.has(widget.sizePref)).toBe(true);
    });
  }
});
