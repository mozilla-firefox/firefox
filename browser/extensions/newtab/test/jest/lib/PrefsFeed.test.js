/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  actionCreators as ac,
  actionTypes as at,
  actionUtils as au,
} from "common/Actions.mjs";
import { mockServices, stubGlobals } from "test/jest/test-utils";
import { PrefsFeed } from "lib/PrefsFeed.sys.mjs";
import { PREFS_CONFIG } from "lib/ActivityStream.sys.mjs";

const NIMBUS_FEATURES = [
  "adsBackend",
  "newtab",
  "newtabInferredPersonalization",
  "newtabOhttpImages",
  "newtabSmartShortcuts",
  "newtabTrainhop",
  "newtabWidgets",
  "pocketNewtab",
];

function mockNimbusFeatures() {
  return Object.fromEntries(
    NIMBUS_FEATURES.map(name => [
      name,
      {
        getAllVariables: jest.fn(),
        getAllEnrollments: jest.fn(),
        getVariable: jest.fn(),
        onUpdate: jest.fn(),
        offUpdate: jest.fn(),
      },
    ])
  );
}

// sinon's calledWith matches a prefix of the recorded arguments, so the karma
// original could assert on the pref name alone. jest compares every argument,
// so name-only assertions look at the recorded names instead.
function calledNames(mockFn) {
  return mockFn.mock.calls.map(([name]) => name);
}

describe("PrefsFeed", () => {
  let feed;
  let FAKE_PREFS;
  let services;
  let nimbusFeatures;
  let region;
  let selectableProfileService;
  let restoreGlobals;
  beforeEach(() => {
    FAKE_PREFS = new Map([
      ["foo", 1],
      ["bar", 2],
      ["baz", { value: 1, skipBroadcast: true }],
      ["qux", { value: 1, skipBroadcast: true, alsoToPreloaded: true }],
    ]);
    services = mockServices(["prefs", "obs"]);
    nimbusFeatures = mockNimbusFeatures();
    region = { home: "US", REGION_TOPIC: "browser-region-updated" };
    selectableProfileService = {
      hasCreatedSelectableProfiles: jest.fn(() => false),
    };
    restoreGlobals = stubGlobals({
      NimbusFeatures: nimbusFeatures,
      PrivateBrowsingUtils: { enabled: true },
      Region: region,
      SelectableProfileService: selectableProfileService,
      Services: services,
      // jsdom has no Temporal; init() reads Temporal.Now.instant().
      Temporal: {
        Instant: { compare: jest.fn() },
        Now: { instant: jest.fn() },
      },
    });
    feed = new PrefsFeed(FAKE_PREFS);
    jest.spyOn(feed, "_setPref");
    feed.store = {
      dispatch: jest.fn(),
      getState() {
        return this.state;
      },
    };
    // Setup for tests that don't call `init`
    feed._prefs = {
      get: jest.fn(item => FAKE_PREFS.get(item)),
      set: jest.fn((name, value) => FAKE_PREFS.set(name, value)),
      observe: jest.fn(),
      observeBranch: jest.fn(),
      ignore: jest.fn(),
      ignoreBranch: jest.fn(),
      locked: jest.fn(() => false),
      reset: jest.fn(),
      _branchStr: "branch.str.",
    };
  });
  afterEach(() => {
    restoreGlobals();
    jest.restoreAllMocks();
  });

  it("should set a pref when a SET_PREF action is received", () => {
    feed.onAction(ac.SetPref("foo", 2));
    expect(feed._prefs.set).toHaveBeenCalledWith("foo", 2);
  });
  it("should set every pref when a SET_MULTIPLE_PREFS action is received", () => {
    feed.onAction(ac.SetMultiplePrefs({ foo: 2, bar: 3 }));
    expect(feed._prefs.set).toHaveBeenCalledWith("foo", 2);
    expect(feed._prefs.set).toHaveBeenCalledWith("bar", 3);
  });
  it("should coalesce SET_MULTIPLE_PREFS into one content MULTIPLE_PREFS_CHANGED while still notifying feeds per pref", () => {
    // The branch observer fires onPrefChanged synchronously per _prefs.set.
    feed._prefs.set = jest.fn((name, value) => feed.onPrefChanged(name, value));
    feed.onAction(ac.SetMultiplePrefs({ foo: 2, bar: 3 }));

    const dispatched = feed.store.dispatch.mock.calls.map(([action]) => action);

    // Content gets exactly one combined MULTIPLE_PREFS_CHANGED broadcast.
    const prefsChanged = dispatched.filter(
      a => a.type === at.MULTIPLE_PREFS_CHANGED
    );
    expect(prefsChanged.length).toBe(1);
    expect(prefsChanged[0].data.values).toEqual({ foo: 2, bar: 3 });
    expect(au.isBroadcastToContent(prefsChanged[0])).toBe(true);

    // Feeds still get per-pref PREF_CHANGED, but main-only (not re-broadcast
    // to content, which would re-stagger the resize).
    const prefChanged = dispatched.filter(a => a.type === at.PREF_CHANGED);
    expect(prefChanged.length).toBe(2);
    prefChanged.forEach(a => expect(au.isBroadcastToContent(a)).toBe(false));
  });
  it("should still route skipBroadcast prefs individually during a SET_MULTIPLE_PREFS transaction", () => {
    feed._prefs.set = jest.fn((name, value) => feed.onPrefChanged(name, value));
    feed.onAction(ac.SetMultiplePrefs({ foo: 2, baz: 5 }));

    const dispatched = feed.store.dispatch.mock.calls.map(([action]) => action);
    const prefsChanged = dispatched.filter(
      a => a.type === at.MULTIPLE_PREFS_CHANGED
    );
    expect(prefsChanged.length).toBe(1);
    expect(prefsChanged[0].data.values).toEqual({ foo: 2 });

    const bazChange = dispatched.find(
      a => a.type === at.PREF_CHANGED && a.data.name === "baz"
    );
    expect(bazChange).toBeTruthy(); // baz should be dispatched individually
    expect(bazChange.data.value).toBe(5);
  });
  it("should call clearUserPref with action CLEAR_PREF", () => {
    feed.onAction({ type: at.CLEAR_PREF, data: { name: "pref.test" } });
    expect(services.prefs.clearUserPref).toHaveBeenCalledWith(
      "branch.str.pref.test"
    );
  });
  it("should dispatch PREFS_INITIAL_VALUES on init with pref values and .isPrivateBrowsingEnabled", () => {
    feed.onAction({ type: at.INIT });
    expect(feed.store.dispatch).toHaveBeenCalledTimes(1);
    expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
      at.PREFS_INITIAL_VALUES
    );
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.foo).toBe(1);
    expect(data.bar).toBe(2);
    expect(data.isPrivateBrowsingEnabled).toBe(true);
  });
  it("should dispatch PREFS_INITIAL_VALUES with a .featureConfig", () => {
    nimbusFeatures.newtab.getAllVariables.mockReturnValue({
      prefsButtonIcon: "icon-foo",
    });
    feed.onAction({ type: at.INIT });
    expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
      at.PREFS_INITIAL_VALUES
    );
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.featureConfig).toEqual({ prefsButtonIcon: "icon-foo" });
  });
  it("should dispatch PREFS_INITIAL_VALUES with trainhopConfig", () => {
    const testObject = {
      meta: { isRollout: false },
      value: {
        type: "testExperiment",
        payload: { enabled: true },
      },
    };
    nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
      testObject,
    ]);

    feed.onAction({ type: at.INIT });

    expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
      at.PREFS_INITIAL_VALUES
    );
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.trainhopConfig).toEqual({
      testExperiment: { enabled: true },
    });
  });
  it("should dispatch PREFS_INITIAL_VALUES with adsBackendConfig", () => {
    const testObject = {
      meta: { isRollout: false },
      value: {
        flags: {
          feature1: true,
        },
      },
    };
    nimbusFeatures.adsBackend.getAllEnrollments.mockReturnValue([testObject]);

    feed.onAction({ type: at.INIT });

    expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
      at.PREFS_INITIAL_VALUES
    );
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.adsBackendConfig).toEqual({
      feature1: true,
    });
  });
  it("should dispatch PREFS_INITIAL_VALUES with an empty object if no experiment is returned", () => {
    nimbusFeatures.newtab.getAllVariables.mockReturnValue(null);
    feed.onAction({ type: at.INIT });
    expect(feed.store.dispatch.mock.calls[0][0].type).toBe(
      at.PREFS_INITIAL_VALUES
    );
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.featureConfig).toEqual({});
  });
  describe("locked prefs", () => {
    it("should dispatch PREFS_INITIAL_VALUES with the locked prefs", () => {
      feed._prefs.locked = jest.fn(name => name === "bar");
      feed.onAction({ type: at.INIT });
      const [[action]] = feed.store.dispatch.mock.calls;
      expect(action.type).toBe(at.PREFS_INITIAL_VALUES);
      expect(action.data.lockedPrefs).toEqual(["bar"]);
    });
    it("should broadcast the locked prefs when a pref's lock state changes", () => {
      feed.onAction({ type: at.INIT });
      feed.store.dispatch.mockClear();
      feed._prefs.locked = jest.fn(name => name === "foo");

      feed.onPrefChanged("foo", 2);

      const action = feed.store.dispatch.mock.calls
        .map(([a]) => a)
        .find(a => a.type === at.PREF_CHANGED && a.data.name === "lockedPrefs");
      expect(action.data.value).toEqual(["foo"]);
      expect(au.isBroadcastToContent(action)).toBe(true);
    });
    it("should not re-broadcast the locked prefs when nothing was locked or unlocked", () => {
      feed.onAction({ type: at.INIT });
      feed.store.dispatch.mockClear();

      feed.onPrefChanged("foo", 2);

      expect(
        feed.store.dispatch.mock.calls
          .map(([a]) => a)
          .find(
            a => a.type === at.PREF_CHANGED && a.data.name === "lockedPrefs"
          )
      ).toBeUndefined();
    });
  });
  it("should add one branch observer on init", () => {
    feed.onAction({ type: at.INIT });
    expect(feed._prefs.observeBranch).toHaveBeenCalledTimes(1);
    expect(feed._prefs.observeBranch).toHaveBeenCalledWith(feed);
  });
  it("should handle region on init", () => {
    feed.init();
    expect(feed.geo).toBe("US");
  });
  it("should leave region out of the initial values when it is unresolved", () => {
    region.home = "";
    feed.onAction({ type: at.INIT });
    const [[{ data }]] = feed.store.dispatch.mock.calls;
    expect(data.region).toBeUndefined();
  });
  it("should add region observer on init", () => {
    region.home = "";
    feed.init();
    expect(feed.geo).toBe("");
    expect(services.obs.addObserver).toHaveBeenCalledWith(
      feed,
      region.REGION_TOPIC
    );
  });
  it("should remove the branch observer on uninit", () => {
    feed.onAction({ type: at.UNINIT });
    expect(feed._prefs.ignoreBranch).toHaveBeenCalledTimes(1);
    expect(feed._prefs.ignoreBranch).toHaveBeenCalledWith(feed);
  });
  it("should call removeObserver", () => {
    feed.geo = "";
    feed.uninit();
    expect(services.obs.removeObserver).toHaveBeenCalledWith(
      feed,
      region.REGION_TOPIC
    );
  });
  describe("browserNovaEnabled", () => {
    it("should add a browser.nova.enabled observer on init", () => {
      feed.init();
      expect(services.prefs.addObserver).toHaveBeenCalledWith(
        "browser.nova.enabled",
        feed
      );
    });
    it("should remove the browser.nova.enabled observer on uninit", () => {
      feed.uninit();
      expect(services.prefs.removeObserver).toHaveBeenCalledWith(
        "browser.nova.enabled",
        feed
      );
    });
    it("should broadcast browserNovaEnabled when browser.nova.enabled changes", () => {
      services.prefs.getBoolPref.mockReturnValue(true);
      feed.observe(null, "nsPref:changed", "browser.nova.enabled");
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "browserNovaEnabled", value: true },
        })
      );
    });
    it("broadcasts browserNovaEnabled false when browser.nova.enabled is off", () => {
      services.prefs.getBoolPref.mockReturnValue(false);
      feed.observe(null, "nsPref:changed", "browser.nova.enabled");
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "browserNovaEnabled", value: false },
        })
      );
    });
  });
  describe("recordsHistory", () => {
    // The initial values are what the first new tab of a session reads, so the
    // widget-hiding depends on this being present before any pref changes.
    it("is included in the initial values when history is on", () => {
      services.prefs.getBoolPref = jest.fn(() => true);
      feed.onAction({ type: at.INIT });
      const [[{ data }]] = feed.store.dispatch.mock.calls;
      expect(data.recordsHistory).toBe(true);
    });
    it("is false in the initial values when history is off", () => {
      services.prefs.getBoolPref = jest.fn(
        pref => pref !== "places.history.enabled"
      );
      feed.onAction({ type: at.INIT });
      const [[{ data }]] = feed.store.dispatch.mock.calls;
      expect(data.recordsHistory).toBe(false);
    });
    it("observes both history prefs on init and drops them on uninit", () => {
      feed.init();
      feed.uninit();
      for (const pref of [
        "places.history.enabled",
        "browser.privatebrowsing.autostart",
      ]) {
        expect(services.prefs.addObserver).toHaveBeenCalledWith(pref, feed);
        expect(services.prefs.removeObserver).toHaveBeenCalledWith(pref, feed);
      }
    });
    it("broadcasts the value when places.history.enabled changes", () => {
      services.prefs.getBoolPref = jest.fn(() => false);
      feed.observe(null, "nsPref:changed", "places.history.enabled");
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "recordsHistory", value: false },
        })
      );
    });
    it("ignores a pref write that leaves the derived value alone", () => {
      // Two prefs collapse into one boolean, so moving autostart while
      // places.history.enabled is already false changes nothing.
      services.prefs.getBoolPref = jest.fn(
        pref => pref !== "places.history.enabled"
      );
      feed.init();
      feed.store.dispatch.mockClear();

      feed.observe(null, "nsPref:changed", "browser.privatebrowsing.autostart");
      expect(feed.store.dispatch).not.toHaveBeenCalled();
    });
    it("broadcasts once per flip, not once per write", () => {
      services.prefs.getBoolPref = jest.fn(() => true);
      feed.init();
      feed.store.dispatch.mockClear();

      services.prefs.getBoolPref = jest.fn(
        pref => pref !== "places.history.enabled"
      );
      feed.observe(null, "nsPref:changed", "places.history.enabled");
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "recordsHistory", value: false },
        })
      );

      // A second write with the same resulting value is a no-op.
      feed.store.dispatch.mockClear();
      feed.observe(null, "nsPref:changed", "places.history.enabled");
      expect(feed.store.dispatch).not.toHaveBeenCalled();
    });
    it("is false in permanent private browsing", () => {
      // Places records no visits there, so the pref alone is not enough.
      globalThis.PrivateBrowsingUtils.permanentPrivateBrowsing = true;
      services.prefs.getBoolPref = jest.fn(() => true);
      feed.observe(null, "nsPref:changed", "browser.privatebrowsing.autostart");
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "recordsHistory", value: false },
        })
      );
    });
  });
  it("should send a PREF_CHANGED action when onPrefChanged is called", () => {
    feed.onPrefChanged("foo", 2);
    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: { name: "foo", value: 2 },
      })
    );
  });
  it("should send a PREF_CHANGED actions when onPocketExperimentUpdated is called", () => {
    nimbusFeatures.pocketNewtab.getAllVariables.mockReturnValue({
      prefsButtonIcon: "icon-new",
    });
    feed.onPocketExperimentUpdated();
    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "pocketConfig",
          value: {
            prefsButtonIcon: "icon-new",
          },
        },
      })
    );
  });
  it("should not send a PREF_CHANGED actions when onPocketExperimentUpdated is called during startup", () => {
    nimbusFeatures.pocketNewtab.getAllVariables.mockReturnValue({
      prefsButtonIcon: "icon-new",
    });
    feed.onPocketExperimentUpdated({}, "feature-experiment-loaded");
    expect(feed.store.dispatch).not.toHaveBeenCalled();
    feed.onPocketExperimentUpdated({}, "feature-rollout-loaded");
    expect(feed.store.dispatch).not.toHaveBeenCalled();
  });
  it("should set initialWallpaper when currentWallpaper is set and initialWallpaper is unset", () => {
    nimbusFeatures.pocketNewtab.getAllVariables.mockReturnValue({
      currentWallpaper: "celestial",
    });
    feed.onPocketExperimentUpdated();
    expect(feed._prefs.set).toHaveBeenCalledWith(
      "newtabWallpapers.initialWallpaper",
      "celestial"
    );
  });
  it("should not overwrite initialWallpaper if it is already set", () => {
    FAKE_PREFS.set("newtabWallpapers.initialWallpaper", "celestial");
    nimbusFeatures.pocketNewtab.getAllVariables.mockReturnValue({
      currentWallpaper: "celestial",
    });
    feed.onPocketExperimentUpdated();
    expect(calledNames(feed._prefs.set)).not.toContain(
      "newtabWallpapers.initialWallpaper"
    );
  });
  it("should send a PREF_CHANGED actions when onExperimentUpdated is called", () => {
    nimbusFeatures.newtab.getAllVariables.mockReturnValue({
      prefsButtonIcon: "icon-new",
    });
    feed.onExperimentUpdated();
    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "featureConfig",
          value: {
            prefsButtonIcon: "icon-new",
          },
        },
      })
    );
  });
  describe("spaces opt-out mirror", () => {
    beforeEach(() => {
      FAKE_PREFS.set("pageLayouts.variant", "spaces-buttons-bottom");
    });

    it("should opt the space out when its pref is turned off", () => {
      feed.onPrefChanged("feeds.section.topstories", false);

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.storiesOptOut",
        true
      );
    });

    it("should opt back in when the pref is turned on again", () => {
      FAKE_PREFS.set("spaces.storiesOptOut", true);

      feed.onPrefChanged("feeds.section.topstories", true);

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.storiesOptOut",
        false
      );
    });

    it("should mirror every space the same way", () => {
      feed.onPrefChanged("widgets.enabled", false);
      feed.onPrefChanged("feeds.section.highlights", false);

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.widgetsOptOut",
        true
      );
      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.activityOptOut",
        true
      );
    });

    it("should mirror a change made outside the newtab page", () => {
      // about:preferences writes through the Preferences binding rather than
      // SET_PREF, so only the branch observer sees it.
      feed.observe(null, "nsPref:changed", "feeds.section.topstories");
      feed.onPrefChanged("feeds.section.topstories", false);

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.storiesOptOut",
        true
      );
    });

    it("should not write when the mirror already matches", () => {
      FAKE_PREFS.set("spaces.storiesOptOut", true);

      feed.onPrefChanged("feeds.section.topstories", false);

      expect(calledNames(feed._prefs.set)).not.toContain(
        "spaces.storiesOptOut"
      );
    });

    it("should not mirror outside the experiment", () => {
      FAKE_PREFS.set("pageLayouts.variant", "nova-full-width");

      feed.onPrefChanged("feeds.section.topstories", false);

      expect(calledNames(feed._prefs.set)).not.toContain(
        "spaces.storiesOptOut"
      );
    });

    // Bug 2068165: the control reads an override, so it shows the space as on
    // while the pref is already off. Switching it off writes the value the pref
    // already holds, the branch observer never fires, and without mirroring
    // from the write itself the override survives and the control goes inert.
    it("should opt out when SET_PREF writes the value the pref already holds", () => {
      FAKE_PREFS.set("feeds.section.topstories", false);

      feed.onAction({
        type: at.SET_PREF,
        data: { name: "feeds.section.topstories", value: false },
      });

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.storiesOptOut",
        true
      );
    });

    it("should opt out when SET_MULTIPLE_PREFS writes an unchanged value", () => {
      FAKE_PREFS.set("widgets.enabled", false);

      feed.onAction({
        type: at.SET_MULTIPLE_PREFS,
        data: { values: { "widgets.enabled": false } },
      });

      expect(feed._prefs.set).toHaveBeenCalledWith(
        "spaces.widgetsOptOut",
        true
      );
    });

    it("should not mirror a pref that is not a space", () => {
      feed.onPrefChanged("feeds.topsites", false);

      expect(
        calledNames(feed._prefs.set).some(name => name.startsWith("spaces."))
      ).toBe(false);
    });
  });

  describe("newtabTrainhop", () => {
    it("should send a PREF_CHANGED actions when onTrainhopExperimentUpdated is called", () => {
      const testObject = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "testExperiment",
          payload: {
            enabled: true,
          },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        testObject,
      ]);
      feed.onTrainhopExperimentUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "trainhopConfig",
            value: {
              testExperiment: testObject.value.payload,
            },
          },
        })
      );
    });
    it("should handle and dedupe multiple experiments and rollouts", () => {
      const testObject1 = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "testExperiment1",
          payload: {
            enabled: true,
          },
        },
      };
      const testObject2 = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "testExperiment1",
          payload: {
            enabled: false,
          },
        },
      };
      const testObject3 = {
        meta: {
          isRollout: true,
        },
        value: {
          type: "testExperiment2",
          payload: {
            enabled: true,
          },
        },
      };
      const testObject4 = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "testExperiment2",
          payload: {
            enabled: false,
          },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        testObject1,
        testObject2,
        testObject3,
        testObject4,
      ]);
      feed.onTrainhopExperimentUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "trainhopConfig",
            value: {
              testExperiment1: testObject1.value.payload,
              testExperiment2: testObject4.value.payload,
            },
          },
        })
      );
    });
    it("should handle multi-payload format with single enrollment", () => {
      const testObject = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "multi-payload",
          payload: [
            {
              type: "testExperiment",
              payload: {
                enabled: true,
                name: "test-name",
              },
            },
          ],
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        testObject,
      ]);
      feed.onTrainhopExperimentUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "trainhopConfig",
            value: {
              testExperiment: {
                enabled: true,
                name: "test-name",
              },
            },
          },
        })
      );
    });
    it("should handle multi-payload format with multiple items in single enrollment", () => {
      const testObject = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "multi-payload",
          payload: [
            {
              type: "testExperiment1",
              payload: {
                enabled: true,
              },
            },
            {
              type: "testExperiment2",
              payload: {
                enabled: false,
              },
            },
          ],
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        testObject,
      ]);
      feed.onTrainhopExperimentUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "trainhopConfig",
            value: {
              testExperiment1: {
                enabled: true,
              },
              testExperiment2: {
                enabled: false,
              },
            },
          },
        })
      );
    });
    it("should write trainhop widgets.weatherSize to the default branch", () => {
      const setStringPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setStringPref,
        setBoolPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { weatherSize: "large" },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(setStringPref).toHaveBeenCalledWith(
        "widgets.weather.size",
        "large"
      );
    });

    it("should write widgetsSettings default-enabled values to the default branch", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetsSettings",
          payload: { listsEnabled: false, focusTimerEnabled: true },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(setBoolPref).toHaveBeenCalledWith("widgets.lists.enabled", false);
      expect(setBoolPref).toHaveBeenCalledWith(
        "widgets.focusTimer.enabled",
        true
      );
    });

    it.each([
      ["widgets", { enabled: true }],
      ["widgetsSettings", { enabled: true }],
    ])(
      "should write the widgets.enabled container default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        // Turns widgets back on in a region the region/locale gate leaves off.
        expect(setBoolPref).toHaveBeenCalledWith("widgets.enabled", true);
      }
    );

    it.each([
      ["widgets", { privacyEnabled: true }],
      ["widgetsSettings", { privacyEnabled: true }],
    ])(
      "should turn on the preffed-off privacy default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.privacy.enabled",
          true
        );
      }
    );

    it.each([
      ["widgetsSettings", { listsEnabled: false }],
      ["widgets", { listsEnabled: false }],
    ])(
      "should write a widget's default enabled value from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.lists.enabled",
          false
        );
      }
    );

    it.each([
      ["widgets", { stocksEnabled: true }],
      ["widgetsSettings", { stocksEnabled: true }],
    ])(
      "should turn on the preffed-off stocks default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.stocks.enabled",
          true
        );
      }
    );

    it.each([
      ["widgets", { crosswordEnabled: true }],
      ["widgetsSettings", { crosswordEnabled: true }],
      ["widgetCrossword", { enabled: true }],
    ])(
      "should turn on the preffed-off crossword default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.crossword.enabled",
          true
        );
      }
    );

    it.each([
      ["widgets", { recentSearchesEnabled: true }],
      ["widgetsSettings", { recentSearchesEnabled: true }],
      ["widgetRecentSearches", { enabled: true }],
    ])(
      "should turn on the preffed-off recent searches default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.recentSearches.enabled",
          true
        );
      }
    );

    it.each([
      ["widgets", { horoscopesEnabled: true }],
      ["widgetsSettings", { horoscopesEnabled: true }],
      ["widgetHoroscopes", { enabled: true }],
    ])(
      "should turn on the preffed-off horoscopes default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.horoscopes.enabled",
          true
        );
      }
    );

    it.each([
      ["widgets", { travelEnabled: true }],
      ["widgetsSettings", { travelEnabled: true }],
      ["widgetTravel", { enabled: true }],
    ])(
      "should turn on the preffed-off travel default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.travel.enabled",
          true
        );
      }
    );

    it.each([
      ["widgets", { solitaireEnabled: true }],
      ["widgetsSettings", { solitaireEnabled: true }],
      ["widgetSolitaire", { enabled: true }],
    ])(
      "should turn on the preffed-off solitaire default from %s",
      (type, payload) => {
        const setBoolPref = jest.fn();
        services.prefs.getDefaultBranch.mockReturnValue({
          setBoolPref,
          setStringPref: jest.fn(),
        });
        nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
          { meta: { isRollout: false }, value: { type, payload } },
        ]);

        feed.onTrainhopExperimentUpdated();

        expect(setBoolPref).toHaveBeenCalledWith(
          "widgets.solitaire.enabled",
          true
        );
      }
    );

    it("should let widgetsSettings win over widgets for a widget default", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "multi-payload",
            payload: [
              { type: "widgets", payload: { listsEnabled: false } },
              { type: "widgetsSettings", payload: { listsEnabled: true } },
            ],
          },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(setBoolPref).toHaveBeenCalledWith("widgets.lists.enabled", true);
      expect(setBoolPref).not.toHaveBeenCalledWith(
        "widgets.lists.enabled",
        false
      );
    });

    it("should let widgetsSettings.enabled win over widgets.enabled", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "multi-payload",
            payload: [
              { type: "widgets", payload: { enabled: false } },
              { type: "widgetsSettings", payload: { enabled: true } },
            ],
          },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(setBoolPref).toHaveBeenCalledWith("widgets.enabled", true);
      expect(setBoolPref).not.toHaveBeenCalledWith("widgets.enabled", false);
    });

    it("should not write widgets.enabled when no payload carries it", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "widgetsSettings",
            payload: { listsEnabled: false },
          },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(setBoolPref).not.toHaveBeenCalledWith(
        "widgets.enabled",
        expect.anything()
      );
    });

    it("should write the Recent Activity default for the spaces experiment", () => {
      // Scoped to the experiment, so an unrelated train-hop config leaves it be.
      FAKE_PREFS.set("pageLayouts.variant", "spaces-buttons-bottom");
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: { type: "highlights", payload: { enabled: true } },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      // The default branch, so a user who has switched Recent Activity off
      // keeps it off: the user branch always wins.
      expect(setBoolPref).toHaveBeenCalledWith(
        "feeds.section.highlights",
        true
      );
    });

    it("should put the Recent Activity default back on unenrollment", () => {
      FAKE_PREFS.set("pageLayouts.variant", "spaces-buttons-bottom");
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const { getAllEnrollments } = nimbusFeatures.newtabTrainhop;

      getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: { type: "highlights", payload: { enabled: true } },
        },
      ]);
      feed.onTrainhopExperimentUpdated();

      // Unenrolling takes the variant with it, so the revert has to run when
      // spaces is no longer assigned -- which is the whole point of it.
      getAllEnrollments.mockReturnValue([]);
      FAKE_PREFS.set("pageLayouts.variant", "nova-full-width");
      feed.onTrainhopExperimentUpdated();

      // Reverted in the same update rather than at the next restart, but only
      // because this profile was enrolled.
      expect(setBoolPref).toHaveBeenCalledWith(
        "feeds.section.highlights",
        false
      );
    });

    it("should not opt the space out when unenrolling reverts its default", () => {
      // The revert writes feeds.section.highlights, which fires the branch
      // observer. Its handler has to see the fresh config, or it reads our own
      // write as the user turning Recent Activity off.
      const setBoolPref = jest.fn((name, value) =>
        feed.onPrefChanged(name, value)
      );
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const { getAllEnrollments } = nimbusFeatures.newtabTrainhop;

      FAKE_PREFS.set("pageLayouts.variant", "spaces-buttons-bottom");
      getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "multi-payload",
            payload: [
              {
                type: "pageLayouts",
                payload: { variant: "spaces-buttons-bottom" },
              },
              { type: "highlights", payload: { enabled: true } },
            ],
          },
        },
      ]);
      feed.onTrainhopExperimentUpdated();

      getAllEnrollments.mockReturnValue([]);
      FAKE_PREFS.set("pageLayouts.variant", "nova-full-width");
      feed.onTrainhopExperimentUpdated();

      expect(feed._prefs.set).not.toHaveBeenCalledWith(
        "spaces.activityOptOut",
        true
      );
    });

    it("should not revert a default this profile was never given", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([]);

      FAKE_PREFS.set("pageLayouts.variant", "nova-full-width");
      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain(
        "feeds.section.highlights"
      );
    });

    it("should not touch the Recent Activity default for an unrelated config", () => {
      FAKE_PREFS.set("pageLayouts.variant", "spaces-buttons-bottom");
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: { type: "highlights", payload: {} },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain(
        "feeds.section.highlights"
      );
    });

    it("should not write a widget default when its widgetsSettings key is absent", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetsSettings",
          payload: { listsEnabled: false },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain("widgets.clocks.enabled");
    });

    it("should write widgetPictureOfTheDay.enabled to the user pref default branch, not the system pref", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetPictureOfTheDay",
          payload: {
            enabled: true,
            setAsWallpaperEnabled: true,
            size: "large",
          },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      // `enabled` overrides the user-facing enabled pref's default; `visible`
      // (not present here) would reveal the widget separately; size and
      // setAsWallpaperEnabled are read directly from trainhopConfig.
      expect(setBoolPref).toHaveBeenCalledWith(
        "widgets.pictureOfTheDay.enabled",
        true
      );
      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.system.pictureOfTheDay.enabled"
      );
      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.pictureOfTheDay.setAsWallpaper.enabled"
      );
    });

    it("should not write the POTD enabled default when widgetPictureOfTheDay.enabled is absent", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetPictureOfTheDay",
          payload: { size: "large" },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.pictureOfTheDay.enabled"
      );
    });

    it("should write widgetCrossword.enabled to the user pref default branch, not the system pref", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetCrossword",
          payload: {
            enabled: true,
            endpoint: "https://example.com/crossword/index.html",
            size: "large",
          },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      // `enabled` overrides the user-facing enabled pref's default; `visible`
      // (not present here) would reveal the widget separately; size and
      // endpoint are read directly from trainhopConfig.
      expect(setBoolPref).toHaveBeenCalledWith(
        "widgets.crossword.enabled",
        true
      );
      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.system.crossword.enabled"
      );
    });

    it("should write widgetPrivacy.enabled to the user pref default branch, not the system pref", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "widgetPrivacy",
            payload: {
              enabled: true,
              showVpnMessages: true,
              maxDisplayCount: 50,
              size: "large",
            },
          },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      // `enabled` overrides the user-facing enabled pref's default; `visible`
      // (not present here) would reveal the widget separately; size and the
      // message-scheduling keys are read directly from trainhopConfig.
      expect(setBoolPref).toHaveBeenCalledWith("widgets.privacy.enabled", true);
      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.system.privacy.enabled"
      );
    });

    it("should not write the privacy enabled default when widgetPrivacy.enabled is absent", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        {
          meta: { isRollout: false },
          value: {
            type: "widgetPrivacy",
            payload: { showVpnMessages: true },
          },
        },
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain("widgets.privacy.enabled");
    });

    it("should not write the crossword enabled default when widgetCrossword.enabled is absent", () => {
      const setBoolPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setBoolPref,
        setStringPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgetCrossword",
          payload: { size: "large" },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setBoolPref)).not.toContain(
        "widgets.crossword.enabled"
      );
    });

    it("should not write widgets.weather.size when weatherSize is missing", () => {
      const setStringPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setStringPref,
        setBoolPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { enabled: true },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setStringPref)).not.toContain("widgets.weather.size");
    });

    it("should not write widgets.weather.size when weatherSize is empty string", () => {
      const setStringPref = jest.fn();
      services.prefs.getDefaultBranch.mockReturnValue({
        setStringPref,
        setBoolPref: jest.fn(),
      });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { weatherSize: "" },
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        enrollment,
      ]);

      feed.onTrainhopExperimentUpdated();

      expect(calledNames(setStringPref)).not.toContain("widgets.weather.size");
    });

    it("should dedupe multi-payload format with experiment taking precedence over rollout", () => {
      const rollout = {
        meta: {
          isRollout: true,
        },
        value: {
          type: "multi-payload",
          payload: [
            {
              type: "testExperiment",
              payload: {
                enabled: false,
                name: "rollout-name",
              },
            },
          ],
        },
      };
      const experiment = {
        meta: {
          isRollout: false,
        },
        value: {
          type: "multi-payload",
          payload: [
            {
              type: "testExperiment",
              payload: {
                enabled: true,
                name: "experiment-name",
              },
            },
          ],
        },
      };
      nimbusFeatures.newtabTrainhop.getAllEnrollments.mockReturnValue([
        rollout,
        experiment,
      ]);
      feed.onTrainhopExperimentUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "trainhopConfig",
            value: {
              testExperiment: {
                enabled: true,
                name: "experiment-name",
              },
            },
          },
        })
      );
    });
  });
  describe("adsBackend", () => {
    it("should send a PREF_CHANGED action when onAdsBackendUpdated is called", () => {
      const testObject = {
        meta: { isRollout: false },
        value: {
          flags: {
            feature1: true,
          },
        },
      };
      nimbusFeatures.adsBackend.getAllEnrollments.mockReturnValue([testObject]);
      feed.onAdsBackendUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "adsBackendConfig",
            value: {
              feature1: true,
            },
          },
        })
      );
    });
    it("should prefer experiments over rollouts for individual flags", () => {
      const testObject1 = {
        meta: { isRollout: false },
        value: {
          flags: {
            feature1: true,
          },
        },
      };
      const testObject2 = {
        meta: { isRollout: true },
        value: {
          flags: {
            feature1: false,
            feature2: true,
          },
        },
      };
      const testObject3 = {
        meta: { isRollout: false },
        value: {
          flags: {
            feature2: false,
          },
        },
      };
      nimbusFeatures.adsBackend.getAllEnrollments.mockReturnValue([
        testObject1,
        testObject2,
        testObject3,
      ]);
      feed.onAdsBackendUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "adsBackendConfig",
            value: {
              feature1: true,
              feature2: false,
            },
          },
        })
      );
    });
    it("should handle and merge multiple experiments and rollouts", () => {
      const testObject1 = {
        meta: { isRollout: false },
        value: {
          flags: {
            feature1: true,
            feature2: true,
          },
        },
      };
      const testObject2 = {
        meta: { isRollout: true },
        value: {
          flags: {
            feature1: false,
          },
        },
      };
      const testObject3 = {
        meta: { isRollout: true },
        value: {
          flags: {
            feature3: true,
          },
        },
      };
      const testObject4 = {
        meta: { isRollout: false },
        value: {
          flags: {
            feature3: false,
            feature4: true,
          },
        },
      };
      nimbusFeatures.adsBackend.getAllEnrollments.mockReturnValue([
        testObject1,
        testObject2,
        testObject3,
        testObject4,
      ]);
      feed.onAdsBackendUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "adsBackendConfig",
            value: {
              feature1: true,
              feature2: true,
              feature3: false,
              feature4: true,
            },
          },
        })
      );
    });
    it("should handle no active experiments and rollouts", () => {
      nimbusFeatures.adsBackend.getAllEnrollments.mockReturnValue([]);
      feed.onAdsBackendUpdated();
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "adsBackendConfig",
            value: {},
          },
        })
      );
    });
  });
  it("should dispatch PREF_CHANGED when onWidgetsUpdated is called", () => {
    nimbusFeatures.newtabWidgets.getAllVariables.mockReturnValue({
      enabled: true,
      listsEnabled: true,
      timerEnabled: false,
    });

    feed.onWidgetsUpdated();

    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "widgetsConfig",
          value: {
            enabled: true,
            listsEnabled: true,
            timerEnabled: false,
          },
        },
      })
    );
  });
  it("should remove all events on removeListeners", () => {
    feed.geo = "";
    feed.removeListeners();
    expect(nimbusFeatures.pocketNewtab.offUpdate).toHaveBeenCalledWith(
      feed.onPocketExperimentUpdated
    );
    expect(nimbusFeatures.newtab.offUpdate).toHaveBeenCalledWith(
      feed.onExperimentUpdated
    );
    expect(nimbusFeatures.newtabTrainhop.offUpdate).toHaveBeenCalledWith(
      feed.onTrainhopExperimentUpdated
    );
    expect(nimbusFeatures.adsBackend.offUpdate).toHaveBeenCalledWith(
      feed.onAdsBackendUpdated
    );
    expect(services.obs.removeObserver).toHaveBeenCalledWith(
      feed,
      region.REGION_TOPIC
    );
  });
  it("should send OnlyToMain pref update if config for pref has skipBroadcast: true", async () => {
    feed.onPrefChanged("baz", { value: 2, skipBroadcast: true });
    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.OnlyToMain({
        type: at.PREF_CHANGED,
        data: { name: "baz", value: { value: 2, skipBroadcast: true } },
      })
    );
  });
  it("should send AlsoToPreloaded pref update if config for pref has skipBroadcast: true and alsoToPreloaded: true", async () => {
    feed.onPrefChanged("qux", {
      value: 2,
      skipBroadcast: true,
      alsoToPreloaded: true,
    });
    expect(feed.store.dispatch).toHaveBeenCalledWith(
      ac.AlsoToPreloaded({
        type: at.PREF_CHANGED,
        data: {
          name: "qux",
          value: { value: 2, skipBroadcast: true, alsoToPreloaded: true },
        },
      })
    );
  });
  describe("#observe", () => {
    it("should call dispatch from observe", () => {
      feed.observe(undefined, region.REGION_TOPIC);
      expect(feed.store.dispatch).toHaveBeenCalledTimes(1);
    });

    it("should broadcast the resolved region to content", () => {
      region.home = "FR";
      feed.observe(undefined, region.REGION_TOPIC);
      expect(feed.store.dispatch).toHaveBeenCalledWith(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: { name: "region", value: region.home },
        })
      );
    });
  });
  describe("#_setStringPref", () => {
    it("should call _setPref and getStringPref from _setStringPref", () => {
      // The shared stub returns the caller's default; this case asserts the
      // pass-through of an unset pref, so make it read as unset.
      services.prefs.getStringPref.mockReturnValueOnce(undefined);
      feed._setStringPref({}, "fake.pref", "default");
      expect(feed._setPref).toHaveBeenCalledTimes(1);
      expect(feed._setPref).toHaveBeenCalledWith(
        { "fake.pref": undefined },
        "fake.pref",
        "default",
        services.prefs.getStringPref
      );
      expect(services.prefs.getStringPref).toHaveBeenCalledTimes(1);
      expect(services.prefs.getStringPref).toHaveBeenCalledWith(
        "browser.newtabpage.activity-stream.fake.pref",
        "default"
      );
    });
  });
  describe("#_setBoolPref", () => {
    it("should call _setPref and getBoolPref from _setBoolPref", () => {
      // The shared stub returns the caller's default; this case asserts the
      // pass-through of an unset pref, so make it read as unset.
      services.prefs.getBoolPref.mockReturnValueOnce(undefined);
      feed._setBoolPref({}, "fake.pref", false);
      expect(feed._setPref).toHaveBeenCalledTimes(1);
      expect(feed._setPref).toHaveBeenCalledWith(
        { "fake.pref": undefined },
        "fake.pref",
        false,
        services.prefs.getBoolPref
      );
      expect(services.prefs.getBoolPref).toHaveBeenCalledTimes(1);
      expect(services.prefs.getBoolPref).toHaveBeenCalledWith(
        "browser.newtabpage.activity-stream.fake.pref",
        false
      );
    });
  });
  describe("#_setIntPref", () => {
    it("should call _setPref and getIntPref from _setIntPref", () => {
      // The shared stub returns the caller's default; this case asserts the
      // pass-through of an unset pref, so make it read as unset.
      services.prefs.getIntPref.mockReturnValueOnce(undefined);
      feed._setIntPref({}, "fake.pref", 1);
      expect(feed._setPref).toHaveBeenCalledTimes(1);
      expect(feed._setPref).toHaveBeenCalledWith(
        { "fake.pref": undefined },
        "fake.pref",
        1,
        services.prefs.getIntPref
      );
      expect(services.prefs.getIntPref).toHaveBeenCalledTimes(1);
      expect(services.prefs.getIntPref).toHaveBeenCalledWith(
        "browser.newtabpage.activity-stream.fake.pref",
        1
      );
    });
  });
  describe("#_setPref", () => {
    it("should set pref value with _setPref", () => {
      const getPrefFunctionSpy = jest.fn();
      const values = {};
      feed._setPref(values, "fake.pref", "default", getPrefFunctionSpy);
      expect(values).toEqual({ "fake.pref": undefined });
      expect(getPrefFunctionSpy).toHaveBeenCalledTimes(1);
      expect(getPrefFunctionSpy).toHaveBeenCalledWith(
        "browser.newtabpage.activity-stream.fake.pref",
        "default"
      );
    });
  });

  describe("Activation Window Evaluation", () => {
    let mockCreatedInstant;
    let mockNowInstant;
    let defaultBranch;
    let temporal;
    let aboutNewTab;
    let restoreActivationGlobals;

    const TEST_VARIANT = "a";

    beforeEach(() => {
      // Mock Temporal.Instant for time control
      // Create a mock instant representing profile creation time (Jan 1, 2024)
      mockCreatedInstant = {
        toString: () => "2024-01-01T00:00:00Z",
      };

      // Mock "now" as 24 hours after creation
      mockNowInstant = {
        toString: () => "2024-01-02T00:00:00Z",
        subtract: jest.fn(() => ({
          toString: () => "2023-12-30T00:00:00Z",
        })),
      };

      temporal = {
        Instant: {
          compare: jest.fn(),
        },
        Now: {
          instant: jest.fn(() => mockNowInstant),
        },
      };

      aboutNewTab = {
        activityStream: {
          createdInstant: mockCreatedInstant,
        },
      };

      restoreActivationGlobals = stubGlobals({
        AboutNewTab: aboutNewTab,
        Temporal: temporal,
      });

      defaultBranch = {
        setBoolPref: jest.fn(),
      };
      services.prefs.getDefaultBranch.mockReturnValue(defaultBranch);

      // Setup store state with a fake activation window config
      feed.store.state = {
        Prefs: {
          values: {
            trainhopConfig: {
              activationWindowBehavior: {
                enabled: true,
                maxProfileAgeInHours: 48,
                disableTopSites: true,
                disableTopStories: true,
                disableWidgets: true,
                variant: TEST_VARIANT,
                enterActivationWindowMessageID: "",
                exitActivationWindowMessageID: "",
              },
            },
          },
        },
      };

      jest.spyOn(feed, "enterActivationWindowState");
      jest.spyOn(feed, "exitActivationWindowState");
    });

    afterEach(() => {
      restoreActivationGlobals();
    });

    describe("#checkForActivationWindow", () => {
      it("should enter activation window state when profile is within window", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).toHaveBeenCalledTimes(1);
        expect(feed.enterActivationWindowState).toHaveBeenCalledWith(
          TEST_VARIANT,
          true,
          true,
          true,
          "",
          false
        );
      });

      it("should exit activation window state when profile is outside window", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed._prefs.isSet = jest.fn(() => false);

        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant < (now - 48 hours) (comparison returns -1, meaning too old)
        temporal.Instant.compare
          .mockReturnValueOnce(-1)
          .mockReturnValueOnce(-1);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).toHaveBeenCalledTimes(1);
      });

      it("should not enter activation window when profile is in the future", () => {
        // First call: createdInstant > now (comparison returns 1)
        temporal.Instant.compare.mockReturnValueOnce(1);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should not enter activation window when profile is exactly at boundary", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant === (now - 48 hours) (comparison returns 0)
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(0);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
      });

      it("should not evaluate when config is disabled", () => {
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.enabled = false;

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should not enter the activation window if 1 or more selectable profiles have been created", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        selectableProfileService.hasCreatedSelectableProfiles.mockReturnValue(
          true
        );
        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should exit activation window state when config is disabled but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.enabled = false;
        feed._prefs.isSet = jest.fn(() => false);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).toHaveBeenCalledTimes(1);
      });

      it("should not evaluate when createdInstant is missing", () => {
        aboutNewTab.activityStream.createdInstant = null;

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should exit activation window state when createdInstant is missing but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        aboutNewTab.activityStream.createdInstant = null;
        feed._prefs.isSet = jest.fn(() => false);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).toHaveBeenCalledTimes(1);
      });

      it("should not evaluate when variant is missing", () => {
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.variant =
          "";

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should exit activation window state when variant is missing but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.variant =
          "";
        feed._prefs.isSet = jest.fn(() => false);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).toHaveBeenCalledTimes(1);
      });

      it("should return early when store state is missing", () => {
        feed.store.state = null;

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should return early when Prefs state is missing", () => {
        feed.store.state = { Prefs: null };

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).not.toHaveBeenCalled();
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should call enterActivationWindowState even if already in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).toHaveBeenCalledTimes(1);
        expect(feed.exitActivationWindowState).not.toHaveBeenCalled();
      });

      it("should use default now instant when not provided", () => {
        // Set up for within window
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        feed.checkForActivationWindow();

        expect(temporal.Now.instant).toHaveBeenCalledTimes(1);
        expect(feed.enterActivationWindowState).toHaveBeenCalledTimes(1);
      });

      it("should pass isStartup=true to enterActivationWindowState when called with isStartup=true", () => {
        // Set up for within window
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        feed.checkForActivationWindow(mockNowInstant, /* isStartup */ true);

        expect(feed.enterActivationWindowState).toHaveBeenCalledTimes(1);
        expect(feed.enterActivationWindowState).toHaveBeenCalledWith(
          TEST_VARIANT,
          true,
          true,
          true,
          "",
          true
        );
      });

      it("should pass isStartup=false to enterActivationWindowState when called without isStartup", () => {
        // Set up for within window
        temporal.Instant.compare.mockReturnValueOnce(-1).mockReturnValueOnce(1);

        feed.checkForActivationWindow(mockNowInstant);

        expect(feed.enterActivationWindowState).toHaveBeenCalledTimes(1);
        expect(feed.enterActivationWindowState).toHaveBeenCalledWith(
          TEST_VARIANT,
          true,
          true,
          true,
          "",
          false
        );
      });
    });
  });

  describe("Activation Window Broadcasting", () => {
    const TEST_VARIANT = "a";

    let defaultBranch;
    beforeEach(() => {
      defaultBranch = {
        setBoolPref: jest.fn(),
      };
      services.prefs.getDefaultBranch.mockReturnValue(defaultBranch);
      jest.spyOn(feed, "onPrefChanged");
    });

    describe("#enterActivationWindowState", () => {
      it("should broadcast pref changes when entering activation window", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, true, true, false, "");

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(2);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          false,
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.section.topstories",
          false,
          false
        );
      });

      it("should not broadcast when already in the same variant", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(TEST_VARIANT, true, true, false, "");

        expect(feed.onPrefChanged).not.toHaveBeenCalled();
        expect(defaultBranch.setBoolPref).not.toHaveBeenCalled();
      });

      it("should broadcast when entering with different variant", () => {
        feed.inActivationWindowState = "variant-a";

        feed.enterActivationWindowState("variant-b", true, true, false, "");

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(2);
      });

      it("should only broadcast for top sites if only disabling top sites", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, true, false, false, "");

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(1);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          false,
          false
        );
      });

      it("should only broadcast for top stories if only disabling top stories", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, false, true, false, "");

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(1);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.section.topstories",
          false,
          false
        );
      });

      it("should only broadcast for widgets if only disabling widgets", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, false, false, true, "");

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "widgets.enabled",
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledTimes(1);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "widgets.enabled",
          false,
          false
        );
      });

      it("should not broadcast if not disabling anything", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, false, false, false, "");

        expect(feed.onPrefChanged).not.toHaveBeenCalled();
      });

      it("should reapply prefs on startup even when already in the same variant", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(
          TEST_VARIANT,
          true,
          true,
          false,
          "",
          /* isStartup */ true
        );

        expect(defaultBranch.setBoolPref).toHaveBeenCalledTimes(2);
        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          false
        );
        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.section.topstories",
          false
        );
      });

      it("should broadcast on startup even when already in the same variant", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(
          TEST_VARIANT,
          true,
          true,
          false,
          "",
          /* isStartup */ true
        );

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(2);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          false,
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.section.topstories",
          false,
          false
        );
      });

      it("should skip idempotent check when isStartup=true", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(
          TEST_VARIANT,
          true,
          false,
          false,
          "",
          /* isStartup */ true
        );

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(1);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          false,
          false
        );
      });

      it("should set enter message ID pref when provided", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(
          "test-variant",
          true,
          true,
          false,
          "ENTER_MESSAGE_ID"
        );

        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.enterMessageID",
          "ENTER_MESSAGE_ID"
        );
      });

      it("should not set enter message ID pref when empty string", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState("test-variant", true, true, false, "");

        expect(calledNames(feed._prefs.set)).not.toContain(
          "activationWindow.enterMessageID"
        );
      });
    });

    describe("#exitActivationWindowState", () => {
      beforeEach(() => {
        feed._prefs.isSet = jest.fn();
      });

      it("should broadcast pref changes when no user values were set", () => {
        feed._prefs.isSet.mockReturnValue(false);
        jest.spyOn(PREFS_CONFIG, "get").mockReturnValueOnce({ value: true });

        feed.exitActivationWindowState();

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(3);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "widgets.enabled",
          true,
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          true,
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true,
          false
        );
      });

      it("should only broadcast for top stories if top sites had user value", () => {
        feed._prefs.isSet.mockImplementation(
          name => name === "activationWindow.temp.topSitesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(2);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true,
          false
        );
        expect(feed.onPrefChanged).not.toHaveBeenCalledWith(
          "feeds.topsites",
          true,
          false
        );
      });

      it("should only broadcast for top sites if top stories had user value", () => {
        feed._prefs.isSet.mockImplementation(
          name => name === "activationWindow.temp.topStoriesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);

        feed.exitActivationWindowState();

        expect(feed.onPrefChanged).toHaveBeenCalledTimes(2);
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "feeds.topsites",
          true,
          false
        );
        expect(feed.onPrefChanged).not.toHaveBeenCalledWith(
          "feeds.section.topstories",
          true,
          false
        );
      });

      it("should ignore a non-boolean trainhop widgets value", () => {
        feed._prefs.isSet.mockReturnValue(false);
        feed._trainhopConfig = { widgetsSettings: { enabled: "true" } };
        jest.spyOn(PREFS_CONFIG, "get").mockReturnValueOnce({ value: false });

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "widgets.enabled",
          false
        );
      });

      it("should reset widgets default to the trainhop value when set", () => {
        feed._prefs.isSet.mockReturnValue(false);
        feed._trainhopConfig = { widgetsSettings: { enabled: false } };

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "widgets.enabled",
          false
        );
        expect(feed.onPrefChanged).toHaveBeenCalledWith(
          "widgets.enabled",
          false,
          false
        );
      });

      it("should not broadcast if all prefs had user values", () => {
        feed._prefs.isSet.mockReturnValue(true);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);
        FAKE_PREFS.set("activationWindow.temp.widgetsUserValue", false);

        feed.exitActivationWindowState();

        expect(feed.onPrefChanged).not.toHaveBeenCalled();
      });

      it("should clear enter message ID pref on exit", () => {
        feed._prefs.isSet.mockReturnValue(false);

        feed.exitActivationWindowState();

        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.enterMessageID",
          ""
        );
      });

      it("should set exit message ID pref when provided", () => {
        feed._prefs.isSet.mockReturnValue(false);

        feed.exitActivationWindowState("EXIT_MESSAGE_ID");

        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.exitMessageID",
          "EXIT_MESSAGE_ID"
        );
      });

      it("should clear exit message ID pref when not provided", () => {
        feed._prefs.isSet.mockReturnValue(false);

        feed.exitActivationWindowState();

        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.exitMessageID",
          ""
        );
      });
    });

    describe("store.dispatch integration", () => {
      beforeEach(() => {
        feed.inActivationWindowState = "";
        FAKE_PREFS.set("feeds.topsites", { value: true });
        FAKE_PREFS.set("feeds.section.topstories", { value: true });
      });

      it("should dispatch PREF_CHANGED actions when entering activation window", () => {
        feed.enterActivationWindowState(TEST_VARIANT, true, true);

        expect(feed.store.dispatch).toHaveBeenCalledWith(
          expect.objectContaining({
            type: at.PREF_CHANGED,
            data: { name: "feeds.topsites", value: false },
          })
        );
        expect(feed.store.dispatch).toHaveBeenCalledWith(
          expect.objectContaining({
            type: at.PREF_CHANGED,
            data: { name: "feeds.section.topstories", value: false },
          })
        );
      });

      it("should dispatch PREF_CHANGED actions when exiting activation window", () => {
        feed._prefs.isSet = jest.fn(() => false);

        feed.exitActivationWindowState();

        expect(feed.store.dispatch).toHaveBeenCalledWith(
          expect.objectContaining({
            type: at.PREF_CHANGED,
            data: { name: "feeds.topsites", value: true },
          })
        );
        expect(feed.store.dispatch).toHaveBeenCalledWith(
          expect.objectContaining({
            type: at.PREF_CHANGED,
            data: { name: "feeds.section.topstories", value: true },
          })
        );
      });
    });
  });

  describe("Activation Window User Preference Tracking", () => {
    describe("#trackActivationWindowPrefChange", () => {
      it("should track top sites user value when changed during activation window", () => {
        feed.trackActivationWindowPrefChange("feeds.topsites", false);
        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.temp.topSitesUserValue",
          false
        );
      });

      it("should track top stories user value when changed during activation window", () => {
        feed.trackActivationWindowPrefChange("feeds.section.topstories", true);
        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.temp.topStoriesUserValue",
          true
        );
      });

      it("should track widgets user value when changed during activation window", () => {
        feed.trackActivationWindowPrefChange("widgets.enabled", true);
        expect(feed._prefs.set).toHaveBeenCalledWith(
          "activationWindow.temp.widgetsUserValue",
          true
        );
      });

      it("should not track changes for other prefs", () => {
        feed.trackActivationWindowPrefChange("some.other.pref", false);
        expect(calledNames(feed._prefs.set)).not.toContain(
          "activationWindow.temp.topSitesUserValue"
        );
        expect(calledNames(feed._prefs.set)).not.toContain(
          "activationWindow.temp.topStoriesUserValue"
        );
        expect(calledNames(feed._prefs.set)).not.toContain(
          "activationWindow.temp.widgetsUserValue"
        );
      });
    });

    describe("#onPrefChanged with activation window tracking", () => {
      beforeEach(() => {
        feed.inActivationWindowState = "variant-a";
        jest.spyOn(feed, "trackActivationWindowPrefChange");
      });

      it("should call trackActivationWindowPrefChange when in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false);
        expect(feed.trackActivationWindowPrefChange).toHaveBeenCalledTimes(1);
        expect(feed.trackActivationWindowPrefChange).toHaveBeenCalledWith(
          "feeds.topsites",
          false
        );
      });

      it("should not call trackActivationWindowPrefChange when not in activation window", () => {
        feed.inActivationWindowState = "";
        feed.onPrefChanged("feeds.topsites", false);
        expect(feed.trackActivationWindowPrefChange).not.toHaveBeenCalled();
      });

      it("should not track when isUserChange=false even if in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false, /* isUserChange */ false);
        expect(feed.trackActivationWindowPrefChange).not.toHaveBeenCalled();
      });

      it("should track when isUserChange=true (default) and in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false, /* isUserChange */ true);
        expect(feed.trackActivationWindowPrefChange).toHaveBeenCalledTimes(1);
      });

      it("should track when isUserChange not specified and in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false);
        expect(feed.trackActivationWindowPrefChange).toHaveBeenCalledTimes(1);
      });
    });

    describe("#exitActivationWindowState", () => {
      let defaultBranch;
      beforeEach(() => {
        defaultBranch = {
          setBoolPref: jest.fn(),
        };
        services.prefs.getDefaultBranch.mockReturnValue(defaultBranch);
      });

      it("should reset defaults to true and restore user's top sites value", () => {
        feed._prefs.isSet = jest.fn(
          name => name === "activationWindow.temp.topSitesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          true
        );
        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true
        );
        expect(feed._prefs.set).toHaveBeenCalledWith("feeds.topsites", false);
        expect(feed._prefs.reset).toHaveBeenCalledWith(
          "activationWindow.temp.topSitesUserValue"
        );
      });

      it("should reset defaults to true and restore user's top stories value", () => {
        feed._prefs.isSet = jest.fn(
          name => name === "activationWindow.temp.topStoriesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          true
        );
        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true
        );
        expect(feed._prefs.set).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true
        );
        expect(feed._prefs.reset).toHaveBeenCalledWith(
          "activationWindow.temp.topStoriesUserValue"
        );
      });

      it("should restore user's widgets value", () => {
        feed._prefs.isSet = jest.fn(
          name => name === "activationWindow.temp.widgetsUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.widgetsUserValue", false);

        feed.exitActivationWindowState();

        expect(feed._prefs.set).toHaveBeenCalledWith("widgets.enabled", false);
        expect(feed._prefs.reset).toHaveBeenCalledWith(
          "activationWindow.temp.widgetsUserValue"
        );
      });

      it("should only reset defaults when no user changes were made", () => {
        feed._prefs.isSet = jest.fn(() => false);

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          true
        );
        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.section.topstories",
          true
        );
        expect(calledNames(feed._prefs.set)).not.toContain("feeds.topsites");
        expect(calledNames(feed._prefs.set)).not.toContain(
          "feeds.section.topstories"
        );
      });

      it("should clear activation window variant pref", () => {
        feed._prefs.isSet = jest.fn(() => false);

        feed.exitActivationWindowState();

        expect(feed._prefs.reset).toHaveBeenCalledWith(
          "activationWindow.variant"
        );
      });

      it("should handle user disabling top sites during activation window", () => {
        feed._prefs.isSet = jest.fn(
          name => name === "activationWindow.temp.topSitesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          true
        );
        expect(feed._prefs.set).toHaveBeenCalledWith("feeds.topsites", false);
      });

      it("should handle user enabling top sites during activation window", () => {
        feed._prefs.isSet = jest.fn(
          name => name === "activationWindow.temp.topSitesUserValue"
        );
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", true);

        feed.exitActivationWindowState();

        expect(defaultBranch.setBoolPref).toHaveBeenCalledWith(
          "feeds.topsites",
          true
        );
        expect(feed._prefs.set).toHaveBeenCalledWith("feeds.topsites", true);
      });
    });
  });
});
