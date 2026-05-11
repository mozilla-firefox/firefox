import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { GlobalOverrider } from "test/unit/utils";
import { PrefsFeed } from "lib/PrefsFeed.sys.mjs";

let overrider = new GlobalOverrider();

describe("PrefsFeed", () => {
  let feed;
  let FAKE_PREFS;
  let sandbox;
  let ServicesStub;
  let SelectableProfileServiceStub;
  beforeEach(() => {
    sandbox = sinon.createSandbox();
    FAKE_PREFS = new Map([
      ["foo", 1],
      ["bar", 2],
      ["baz", { value: 1, skipBroadcast: true }],
      ["qux", { value: 1, skipBroadcast: true, alsoToPreloaded: true }],
    ]);
    feed = new PrefsFeed(FAKE_PREFS);
    ServicesStub = {
      prefs: {
        clearUserPref: sinon.spy(),
        getStringPref: sinon.spy(),
        getIntPref: sinon.spy(),
        getBoolPref: sinon.spy(),
      },
      obs: {
        removeObserver: sinon.spy(),
        addObserver: sinon.spy(),
      },
    };
    SelectableProfileServiceStub = {
      hasCreatedSelectableProfiles() {
        return false;
      },
    };
    sinon.spy(feed, "_setPref");
    feed.store = {
      dispatch: sinon.spy(),
      getState() {
        return this.state;
      },
    };
    // Setup for tests that don't call `init`
    feed._prefs = {
      get: sinon.spy(item => FAKE_PREFS.get(item)),
      set: sinon.spy((name, value) => FAKE_PREFS.set(name, value)),
      observe: sinon.spy(),
      observeBranch: sinon.spy(),
      ignore: sinon.spy(),
      ignoreBranch: sinon.spy(),
      reset: sinon.stub(),
      _branchStr: "branch.str.",
    };
    overrider.set({
      PrivateBrowsingUtils: { enabled: true },
      Services: ServicesStub,
      SelectableProfileService: SelectableProfileServiceStub,
    });
  });
  afterEach(() => {
    overrider.restore();
    sandbox.restore();
  });

  it("should set a pref when a SET_PREF action is received", () => {
    feed.onAction(ac.SetPref("foo", 2));
    assert.calledWith(feed._prefs.set, "foo", 2);
  });
  it("should call clearUserPref with action CLEAR_PREF", () => {
    feed.onAction({ type: at.CLEAR_PREF, data: { name: "pref.test" } });
    assert.calledWith(ServicesStub.prefs.clearUserPref, "branch.str.pref.test");
  });
  it("should dispatch PREFS_INITIAL_VALUES on init with pref values and .isPrivateBrowsingEnabled", () => {
    feed.onAction({ type: at.INIT });
    assert.calledOnce(feed.store.dispatch);
    assert.equal(
      feed.store.dispatch.firstCall.args[0].type,
      at.PREFS_INITIAL_VALUES
    );
    const [{ data }] = feed.store.dispatch.firstCall.args;
    assert.equal(data.foo, 1);
    assert.equal(data.bar, 2);
    assert.isTrue(data.isPrivateBrowsingEnabled);
  });
  it("should dispatch PREFS_INITIAL_VALUES with a .featureConfig", () => {
    sandbox.stub(global.NimbusFeatures.newtab, "getAllVariables").returns({
      prefsButtonIcon: "icon-foo",
    });
    feed.onAction({ type: at.INIT });
    assert.equal(
      feed.store.dispatch.firstCall.args[0].type,
      at.PREFS_INITIAL_VALUES
    );
    const [{ data }] = feed.store.dispatch.firstCall.args;
    assert.deepEqual(data.featureConfig, { prefsButtonIcon: "icon-foo" });
  });
  it("should dispatch PREFS_INITIAL_VALUES with trainhopConfig", () => {
    const testObject = {
      meta: { isRollout: false },
      value: {
        type: "testExperiment",
        payload: { enabled: true },
      },
    };
    sandbox
      .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
      .returns([testObject]);

    feed.onAction({ type: at.INIT });

    assert.equal(
      feed.store.dispatch.firstCall.args[0].type,
      at.PREFS_INITIAL_VALUES
    );
    const [{ data }] = feed.store.dispatch.firstCall.args;
    assert.deepEqual(data.trainhopConfig, {
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
    sandbox
      .stub(global.NimbusFeatures.adsBackend, "getAllEnrollments")
      .returns([testObject]);

    feed.onAction({ type: at.INIT });

    assert.equal(
      feed.store.dispatch.firstCall.args[0].type,
      at.PREFS_INITIAL_VALUES
    );
    const [{ data }] = feed.store.dispatch.firstCall.args;
    assert.deepEqual(data.adsBackendConfig, {
      feature1: true,
    });
  });
  it("should dispatch PREFS_INITIAL_VALUES with an empty object if no experiment is returned", () => {
    sandbox.stub(global.NimbusFeatures.newtab, "getAllVariables").returns(null);
    feed.onAction({ type: at.INIT });
    assert.equal(
      feed.store.dispatch.firstCall.args[0].type,
      at.PREFS_INITIAL_VALUES
    );
    const [{ data }] = feed.store.dispatch.firstCall.args;
    assert.deepEqual(data.featureConfig, {});
  });
  it("should add one branch observer on init", () => {
    feed.onAction({ type: at.INIT });
    assert.calledOnce(feed._prefs.observeBranch);
    assert.calledWith(feed._prefs.observeBranch, feed);
  });
  it("should handle region on init", () => {
    feed.init();
    assert.equal(feed.geo, "US");
  });
  it("should add region observer on init", () => {
    sandbox.stub(global.Region, "home").get(() => "");
    feed.init();
    assert.equal(feed.geo, "");
    assert.calledWith(
      ServicesStub.obs.addObserver,
      feed,
      global.Region.REGION_TOPIC
    );
  });
  it("should remove the branch observer on uninit", () => {
    feed.onAction({ type: at.UNINIT });
    assert.calledOnce(feed._prefs.ignoreBranch);
    assert.calledWith(feed._prefs.ignoreBranch, feed);
  });
  it("should call removeObserver", () => {
    feed.geo = "";
    feed.uninit();
    assert.calledWith(
      ServicesStub.obs.removeObserver,
      feed,
      global.Region.REGION_TOPIC
    );
  });
  it("should send a PREF_CHANGED action when onPrefChanged is called", () => {
    feed.onPrefChanged("foo", 2);
    assert.calledWith(
      feed.store.dispatch,
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: { name: "foo", value: 2 },
      })
    );
  });
  it("should send a PREF_CHANGED actions when onPocketExperimentUpdated is called", () => {
    sandbox
      .stub(global.NimbusFeatures.pocketNewtab, "getAllVariables")
      .returns({
        prefsButtonIcon: "icon-new",
      });
    feed.onPocketExperimentUpdated();
    assert.calledWith(
      feed.store.dispatch,
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
    sandbox
      .stub(global.NimbusFeatures.pocketNewtab, "getAllVariables")
      .returns({
        prefsButtonIcon: "icon-new",
      });
    feed.onPocketExperimentUpdated({}, "feature-experiment-loaded");
    assert.notCalled(feed.store.dispatch);
    feed.onPocketExperimentUpdated({}, "feature-rollout-loaded");
    assert.notCalled(feed.store.dispatch);
  });
  it("should set initialWallpaper when currentWallpaper is set and initialWallpaper is unset", () => {
    sandbox
      .stub(global.NimbusFeatures.pocketNewtab, "getAllVariables")
      .returns({
        currentWallpaper: "celestial",
      });
    feed.onPocketExperimentUpdated();
    assert.calledWith(
      feed._prefs.set,
      "newtabWallpapers.initialWallpaper",
      "celestial"
    );
  });
  it("should not overwrite initialWallpaper if it is already set", () => {
    FAKE_PREFS.set("newtabWallpapers.initialWallpaper", "celestial");
    sandbox
      .stub(global.NimbusFeatures.pocketNewtab, "getAllVariables")
      .returns({
        currentWallpaper: "celestial",
      });
    feed.onPocketExperimentUpdated();
    assert.neverCalledWith(
      feed._prefs.set,
      "newtabWallpapers.initialWallpaper",
      sinon.match.any
    );
  });
  it("should send a PREF_CHANGED actions when onExperimentUpdated is called", () => {
    sandbox.stub(global.NimbusFeatures.newtab, "getAllVariables").returns({
      prefsButtonIcon: "icon-new",
    });
    feed.onExperimentUpdated();
    assert.calledWith(
      feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([testObject]);
      feed.onTrainhopExperimentUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([testObject1, testObject2, testObject3, testObject4]);
      feed.onTrainhopExperimentUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([testObject]);
      feed.onTrainhopExperimentUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([testObject]);
      feed.onTrainhopExperimentUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      const setStringPref = sinon.spy();
      ServicesStub.prefs.getDefaultBranch = sinon
        .stub()
        .returns({ setStringPref });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { weatherSize: "large" },
        },
      };
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([enrollment]);

      feed.onTrainhopExperimentUpdated();

      assert.calledWith(setStringPref, "widgets.weather.size", "large");
    });

    it("should not write widgets.weather.size when weatherSize is missing", () => {
      const setStringPref = sinon.spy();
      ServicesStub.prefs.getDefaultBranch = sinon
        .stub()
        .returns({ setStringPref });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { enabled: true },
        },
      };
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([enrollment]);

      feed.onTrainhopExperimentUpdated();

      assert.neverCalledWith(
        setStringPref,
        "widgets.weather.size",
        sinon.match.any
      );
    });

    it("should not write widgets.weather.size when weatherSize is empty string", () => {
      const setStringPref = sinon.spy();
      ServicesStub.prefs.getDefaultBranch = sinon
        .stub()
        .returns({ setStringPref });
      const enrollment = {
        meta: { isRollout: false },
        value: {
          type: "widgets",
          payload: { weatherSize: "" },
        },
      };
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([enrollment]);

      feed.onTrainhopExperimentUpdated();

      assert.neverCalledWith(
        setStringPref,
        "widgets.weather.size",
        sinon.match.any
      );
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
      sandbox
        .stub(global.NimbusFeatures.newtabTrainhop, "getAllEnrollments")
        .returns([rollout, experiment]);
      feed.onTrainhopExperimentUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.adsBackend, "getAllEnrollments")
        .returns([testObject]);
      feed.onAdsBackendUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.adsBackend, "getAllEnrollments")
        .returns([testObject1, testObject2, testObject3]);
      feed.onAdsBackendUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.adsBackend, "getAllEnrollments")
        .returns([testObject1, testObject2, testObject3, testObject4]);
      feed.onAdsBackendUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
      sandbox
        .stub(global.NimbusFeatures.adsBackend, "getAllEnrollments")
        .returns([]);
      feed.onAdsBackendUpdated();
      assert.calledWith(
        feed.store.dispatch,
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
    sandbox
      .stub(global.NimbusFeatures.newtabWidgets, "getAllVariables")
      .returns({
        enabled: true,
        listsEnabled: true,
        timerEnabled: false,
      });

    feed.onWidgetsUpdated();

    assert.calledWith(
      feed.store.dispatch,
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
    sandbox.spy(global.NimbusFeatures.pocketNewtab, "offUpdate");
    sandbox.spy(global.NimbusFeatures.newtab, "offUpdate");
    sandbox.spy(global.NimbusFeatures.newtabTrainhop, "offUpdate");
    sandbox.spy(global.NimbusFeatures.adsBackend, "offUpdate");
    feed.removeListeners();
    assert.calledWith(
      global.NimbusFeatures.pocketNewtab.offUpdate,
      feed.onPocketExperimentUpdated
    );
    assert.calledWith(
      global.NimbusFeatures.newtab.offUpdate,
      feed.onExperimentUpdated
    );
    assert.calledWith(
      global.NimbusFeatures.newtabTrainhop.offUpdate,
      feed.onTrainhopExperimentUpdated
    );
    assert.calledWith(
      global.NimbusFeatures.adsBackend.offUpdate,
      feed.onAdsBackendUpdated
    );
    assert.calledWith(
      ServicesStub.obs.removeObserver,
      feed,
      global.Region.REGION_TOPIC
    );
  });
  it("should send OnlyToMain pref update if config for pref has skipBroadcast: true", async () => {
    feed.onPrefChanged("baz", { value: 2, skipBroadcast: true });
    assert.calledWith(
      feed.store.dispatch,
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
    assert.calledWith(
      feed.store.dispatch,
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
      feed.observe(undefined, global.Region.REGION_TOPIC);
      assert.calledOnce(feed.store.dispatch);
    });
  });
  describe("#_setStringPref", () => {
    it("should call _setPref and getStringPref from _setStringPref", () => {
      feed._setStringPref({}, "fake.pref", "default");
      assert.calledOnce(feed._setPref);
      assert.calledWith(
        feed._setPref,
        { "fake.pref": undefined },
        "fake.pref",
        "default"
      );
      assert.calledOnce(ServicesStub.prefs.getStringPref);
      assert.calledWith(
        ServicesStub.prefs.getStringPref,
        "browser.newtabpage.activity-stream.fake.pref",
        "default"
      );
    });
  });
  describe("#_setBoolPref", () => {
    it("should call _setPref and getBoolPref from _setBoolPref", () => {
      feed._setBoolPref({}, "fake.pref", false);
      assert.calledOnce(feed._setPref);
      assert.calledWith(
        feed._setPref,
        { "fake.pref": undefined },
        "fake.pref",
        false
      );
      assert.calledOnce(ServicesStub.prefs.getBoolPref);
      assert.calledWith(
        ServicesStub.prefs.getBoolPref,
        "browser.newtabpage.activity-stream.fake.pref",
        false
      );
    });
  });
  describe("#_setIntPref", () => {
    it("should call _setPref and getIntPref from _setIntPref", () => {
      feed._setIntPref({}, "fake.pref", 1);
      assert.calledOnce(feed._setPref);
      assert.calledWith(
        feed._setPref,
        { "fake.pref": undefined },
        "fake.pref",
        1
      );
      assert.calledOnce(ServicesStub.prefs.getIntPref);
      assert.calledWith(
        ServicesStub.prefs.getIntPref,
        "browser.newtabpage.activity-stream.fake.pref",
        1
      );
    });
  });
  describe("#_setPref", () => {
    it("should set pref value with _setPref", () => {
      const getPrefFunctionSpy = sinon.spy();
      const values = {};
      feed._setPref(values, "fake.pref", "default", getPrefFunctionSpy);
      assert.deepEqual(values, { "fake.pref": undefined });
      assert.calledOnce(getPrefFunctionSpy);
      assert.calledWith(
        getPrefFunctionSpy,
        "browser.newtabpage.activity-stream.fake.pref",
        "default"
      );
    });
  });

  describe("Activation Window Evaluation", () => {
    let mockCreatedInstant;
    let mockNowInstant;
    let defaultBranch;

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
        subtract: sinon.stub().returns({
          toString: () => "2023-12-30T00:00:00Z",
        }),
      };

      global.Temporal = {
        Instant: {
          compare: sinon.stub(),
        },
        Now: {
          instant: sinon.stub().returns(mockNowInstant),
        },
      };

      global.AboutNewTab = {
        activityStream: {
          createdInstant: mockCreatedInstant,
        },
      };

      defaultBranch = {
        setBoolPref: sinon.spy(),
      };
      ServicesStub.prefs.getDefaultBranch = sinon.stub().returns(defaultBranch);

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
                variant: TEST_VARIANT,
                enterActivationWindowMessageID: "",
                exitActivationWindowMessageID: "",
              },
            },
          },
        },
      };

      sinon.spy(feed, "enterActivationWindowState");
      sinon.spy(feed, "exitActivationWindowState");
    });

    afterEach(() => {
      delete global.Temporal;
      delete global.AboutNewTab;
    });

    describe("#checkForActivationWindow", () => {
      it("should enter activation window state when profile is within window", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        feed.checkForActivationWindow(mockNowInstant);

        assert.calledOnce(feed.enterActivationWindowState);
        assert.calledWith(
          feed.enterActivationWindowState,
          TEST_VARIANT,
          true,
          true
        );
      });

      it("should exit activation window state when profile is outside window", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed._prefs.isSet = sinon.stub().returns(false);

        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant < (now - 48 hours) (comparison returns -1, meaning too old)
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(-1);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.calledOnce(feed.exitActivationWindowState);
      });

      it("should not enter activation window when profile is in the future", () => {
        // First call: createdInstant > now (comparison returns 1)
        global.Temporal.Instant.compare.onFirstCall().returns(1);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should not enter activation window when profile is exactly at boundary", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant === (now - 48 hours) (comparison returns 0)
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(0);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
      });

      it("should not evaluate when config is disabled", () => {
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.enabled = false;

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should not enter the activation window if 1 or more selectable profiles have been created", () => {
        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        sandbox
          .stub(SelectableProfileServiceStub, "hasCreatedSelectableProfiles")
          .returns(true);
        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should exit activation window state when config is disabled but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.enabled = false;
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.calledOnce(feed.exitActivationWindowState);
      });

      it("should not evaluate when createdInstant is missing", () => {
        global.AboutNewTab.activityStream.createdInstant = null;

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should exit activation window state when createdInstant is missing but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        global.AboutNewTab.activityStream.createdInstant = null;
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.calledOnce(feed.exitActivationWindowState);
      });

      it("should not evaluate when variant is missing", () => {
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.variant =
          "";

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should exit activation window state when variant is missing but currently in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;
        feed.store.state.Prefs.values.trainhopConfig.activationWindowBehavior.variant =
          "";
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.calledOnce(feed.exitActivationWindowState);
      });

      it("should return early when store state is missing", () => {
        feed.store.state = null;

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should return early when Prefs state is missing", () => {
        feed.store.state = { Prefs: null };

        feed.checkForActivationWindow(mockNowInstant);

        assert.notCalled(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should call enterActivationWindowState even if already in state", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        // First call: createdInstant < now (comparison returns -1)
        // Second call: createdInstant > (now - 48 hours) (comparison returns 1)
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        feed.checkForActivationWindow(mockNowInstant);

        assert.calledOnce(feed.enterActivationWindowState);
        assert.notCalled(feed.exitActivationWindowState);
      });

      it("should use default now instant when not provided", () => {
        // Set up for within window
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        feed.checkForActivationWindow();

        assert.calledOnce(global.Temporal.Now.instant);
        assert.calledOnce(feed.enterActivationWindowState);
      });

      it("should pass isStartup=true to enterActivationWindowState when called with isStartup=true", () => {
        // Set up for within window
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        feed.checkForActivationWindow(mockNowInstant, /* isStartup */ true);

        assert.calledOnce(feed.enterActivationWindowState);
        assert.calledWith(
          feed.enterActivationWindowState,
          TEST_VARIANT,
          true,
          true,
          "",
          true
        );
      });

      it("should pass isStartup=false to enterActivationWindowState when called without isStartup", () => {
        // Set up for within window
        global.Temporal.Instant.compare.onFirstCall().returns(-1);
        global.Temporal.Instant.compare.onSecondCall().returns(1);

        feed.checkForActivationWindow(mockNowInstant);

        assert.calledOnce(feed.enterActivationWindowState);
        assert.calledWith(
          feed.enterActivationWindowState,
          TEST_VARIANT,
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
        setBoolPref: sinon.spy(),
      };
      ServicesStub.prefs.getDefaultBranch = sinon.stub().returns(defaultBranch);
      sinon.spy(feed, "onPrefChanged");
    });

    describe("#enterActivationWindowState", () => {
      it("should broadcast pref changes when entering activation window", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, true, true, "");

        assert.calledTwice(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", false);
        assert.calledWith(
          feed.onPrefChanged,
          "feeds.section.topstories",
          false
        );
      });

      it("should not broadcast when already in the same variant", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(TEST_VARIANT, true, true, "");

        assert.notCalled(feed.onPrefChanged);
        assert.notCalled(defaultBranch.setBoolPref);
      });

      it("should broadcast when entering with different variant", () => {
        feed.inActivationWindowState = "variant-a";

        feed.enterActivationWindowState("variant-b", true, true, "");

        assert.calledTwice(feed.onPrefChanged);
      });

      it("should only broadcast for top sites if only disabling top sites", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, true, false, "");

        assert.calledOnce(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", false);
      });

      it("should only broadcast for top stories if only disabling top stories", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, false, true, "");

        assert.calledOnce(feed.onPrefChanged);
        assert.calledWith(
          feed.onPrefChanged,
          "feeds.section.topstories",
          false
        );
      });

      it("should not broadcast if not disabling anything", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(TEST_VARIANT, false, false, "");

        assert.notCalled(feed.onPrefChanged);
      });

      it("should reapply prefs on startup even when already in the same variant", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(
          TEST_VARIANT,
          true,
          true,
          "",
          /* isStartup */ true
        );

        assert.calledTwice(defaultBranch.setBoolPref);
        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", false);
        assert.calledWith(
          defaultBranch.setBoolPref,
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
          "",
          /* isStartup */ true
        );

        assert.calledTwice(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", false);
        assert.calledWith(
          feed.onPrefChanged,
          "feeds.section.topstories",
          false
        );
      });

      it("should skip idempotent check when isStartup=true", () => {
        feed.inActivationWindowState = TEST_VARIANT;

        feed.enterActivationWindowState(
          TEST_VARIANT,
          true,
          false,
          "",
          /* isStartup */ true
        );

        assert.calledOnce(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", false);
      });

      it("should set enter message ID pref when provided", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState(
          "test-variant",
          true,
          true,
          "ENTER_MESSAGE_ID"
        );

        assert.calledWith(
          feed._prefs.set,
          "activationWindow.enterMessageID",
          "ENTER_MESSAGE_ID"
        );
      });

      it("should not set enter message ID pref when empty string", () => {
        feed.inActivationWindowState = "";

        feed.enterActivationWindowState("test-variant", true, true, "");

        assert.neverCalledWith(
          feed._prefs.set,
          "activationWindow.enterMessageID"
        );
      });
    });

    describe("#exitActivationWindowState", () => {
      beforeEach(() => {
        feed._prefs.isSet = sinon.stub();
      });

      it("should broadcast pref changes when no user values were set", () => {
        feed._prefs.isSet.returns(false);

        feed.exitActivationWindowState();

        assert.calledTwice(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", true);
        assert.calledWith(feed.onPrefChanged, "feeds.section.topstories", true);
      });

      it("should only broadcast for top stories if top sites had user value", () => {
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(true);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(false);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        assert.calledOnce(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.section.topstories", true);
        assert.neverCalledWith(feed.onPrefChanged, "feeds.topsites", true);
      });

      it("should only broadcast for top sites if top stories had user value", () => {
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(false);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(true);
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);

        feed.exitActivationWindowState();

        assert.calledOnce(feed.onPrefChanged);
        assert.calledWith(feed.onPrefChanged, "feeds.topsites", true);
        assert.neverCalledWith(
          feed.onPrefChanged,
          "feeds.section.topstories",
          true
        );
      });

      it("should not broadcast if both prefs had user values", () => {
        feed._prefs.isSet.returns(true);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);

        feed.exitActivationWindowState();

        assert.notCalled(feed.onPrefChanged);
      });

      it("should clear enter message ID pref on exit", () => {
        feed._prefs.isSet.returns(false);

        feed.exitActivationWindowState();

        assert.calledWith(
          feed._prefs.set,
          "activationWindow.enterMessageID",
          ""
        );
      });

      it("should set exit message ID pref when provided", () => {
        feed._prefs.isSet.returns(false);

        feed.exitActivationWindowState("EXIT_MESSAGE_ID");

        assert.calledWith(
          feed._prefs.set,
          "activationWindow.exitMessageID",
          "EXIT_MESSAGE_ID"
        );
      });

      it("should clear exit message ID pref when not provided", () => {
        feed._prefs.isSet.returns(false);

        feed.exitActivationWindowState();

        assert.calledWith(
          feed._prefs.set,
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

        assert.calledWith(
          feed.store.dispatch,
          sinon.match({
            type: at.PREF_CHANGED,
            data: { name: "feeds.topsites", value: false },
          })
        );
        assert.calledWith(
          feed.store.dispatch,
          sinon.match({
            type: at.PREF_CHANGED,
            data: { name: "feeds.section.topstories", value: false },
          })
        );
      });

      it("should dispatch PREF_CHANGED actions when exiting activation window", () => {
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.exitActivationWindowState();

        assert.calledWith(
          feed.store.dispatch,
          sinon.match({
            type: at.PREF_CHANGED,
            data: { name: "feeds.topsites", value: true },
          })
        );
        assert.calledWith(
          feed.store.dispatch,
          sinon.match({
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
        assert.calledWith(
          feed._prefs.set,
          "activationWindow.temp.topSitesUserValue",
          false
        );
      });

      it("should track top stories user value when changed during activation window", () => {
        feed.trackActivationWindowPrefChange("feeds.section.topstories", true);
        assert.calledWith(
          feed._prefs.set,
          "activationWindow.temp.topStoriesUserValue",
          true
        );
      });

      it("should not track changes for other prefs", () => {
        feed.trackActivationWindowPrefChange("some.other.pref", false);
        assert.neverCalledWith(
          feed._prefs.set,
          "activationWindow.temp.topSitesUserValue"
        );
        assert.neverCalledWith(
          feed._prefs.set,
          "activationWindow.temp.topStoriesUserValue"
        );
      });
    });

    describe("#onPrefChanged with activation window tracking", () => {
      beforeEach(() => {
        feed.inActivationWindowState = "variant-a";
        sinon.spy(feed, "trackActivationWindowPrefChange");
      });

      it("should call trackActivationWindowPrefChange when in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false);
        assert.calledOnce(feed.trackActivationWindowPrefChange);
        assert.calledWith(
          feed.trackActivationWindowPrefChange,
          "feeds.topsites",
          false
        );
      });

      it("should not call trackActivationWindowPrefChange when not in activation window", () => {
        feed.inActivationWindowState = "";
        feed.onPrefChanged("feeds.topsites", false);
        assert.notCalled(feed.trackActivationWindowPrefChange);
      });

      it("should not track when isUserChange=false even if in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false, /* isUserChange */ false);
        assert.notCalled(feed.trackActivationWindowPrefChange);
      });

      it("should track when isUserChange=true (default) and in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false, /* isUserChange */ true);
        assert.calledOnce(feed.trackActivationWindowPrefChange);
      });

      it("should track when isUserChange not specified and in activation window", () => {
        feed.onPrefChanged("feeds.topsites", false);
        assert.calledOnce(feed.trackActivationWindowPrefChange);
      });
    });

    describe("#exitActivationWindowState", () => {
      let defaultBranch;
      beforeEach(() => {
        defaultBranch = {
          setBoolPref: sinon.spy(),
        };
        ServicesStub.prefs.getDefaultBranch = sinon
          .stub()
          .returns(defaultBranch);
      });

      it("should reset defaults to true and restore user's top sites value", () => {
        feed._prefs.isSet = sinon.stub();
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(true);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(false);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", true);
        assert.calledWith(
          defaultBranch.setBoolPref,
          "feeds.section.topstories",
          true
        );
        assert.calledWith(feed._prefs.set, "feeds.topsites", false);
        assert.calledWith(
          feed._prefs.reset,
          "activationWindow.temp.topSitesUserValue"
        );
      });

      it("should reset defaults to true and restore user's top stories value", () => {
        feed._prefs.isSet = sinon.stub();
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(false);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(true);
        FAKE_PREFS.set("activationWindow.temp.topStoriesUserValue", true);

        feed.exitActivationWindowState();

        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", true);
        assert.calledWith(
          defaultBranch.setBoolPref,
          "feeds.section.topstories",
          true
        );
        assert.calledWith(feed._prefs.set, "feeds.section.topstories", true);
        assert.calledWith(
          feed._prefs.reset,
          "activationWindow.temp.topStoriesUserValue"
        );
      });

      it("should only reset defaults when no user changes were made", () => {
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.exitActivationWindowState();

        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", true);
        assert.calledWith(
          defaultBranch.setBoolPref,
          "feeds.section.topstories",
          true
        );
        assert.neverCalledWith(feed._prefs.set, "feeds.topsites");
        assert.neverCalledWith(feed._prefs.set, "feeds.section.topstories");
      });

      it("should clear activation window variant pref", () => {
        feed._prefs.isSet = sinon.stub().returns(false);

        feed.exitActivationWindowState();

        assert.calledWith(feed._prefs.reset, "activationWindow.variant");
      });

      it("should handle user disabling top sites during activation window", () => {
        feed._prefs.isSet = sinon.stub();
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(true);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(false);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", false);

        feed.exitActivationWindowState();

        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", true);
        assert.calledWith(feed._prefs.set, "feeds.topsites", false);
      });

      it("should handle user enabling top sites during activation window", () => {
        feed._prefs.isSet = sinon.stub();
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topSitesUserValue")
          .returns(true);
        feed._prefs.isSet
          .withArgs("activationWindow.temp.topStoriesUserValue")
          .returns(false);
        FAKE_PREFS.set("activationWindow.temp.topSitesUserValue", true);

        feed.exitActivationWindowState();

        assert.calledWith(defaultBranch.setBoolPref, "feeds.topsites", true);
        assert.calledWith(feed._prefs.set, "feeds.topsites", true);
      });
    });
  });
});
