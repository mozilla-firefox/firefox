import { _ASRouter, MessageLoaderUtils } from "modules/ASRouter.sys.mjs";
import { QueryCache } from "modules/ASRouterTargeting.sys.mjs";
import {
  FAKE_LOCAL_MESSAGES,
  FAKE_LOCAL_PROVIDER,
  FAKE_LOCAL_PROVIDERS,
  FAKE_REMOTE_MESSAGES,
  FAKE_REMOTE_PROVIDER,
  FAKE_REMOTE_SETTINGS_PROVIDER,
} from "./constants";
import {
  ASRouterPreferences,
  TARGETING_PREFERENCES,
} from "modules/ASRouterPreferences.sys.mjs";
import { ASRouterTriggerListeners } from "modules/ASRouterTriggerListeners.sys.mjs";
import { GlobalOverrider } from "tests/unit/utils";
import { PanelTestProvider } from "modules/PanelTestProvider.sys.mjs";
import ProviderResponseSchema from "content-src/schemas/provider-response.schema.json";

const MESSAGE_PROVIDER_PREF_NAME =
  "browser.newtabpage.activity-stream.asrouter.providers.cfr";
const FAKE_PROVIDERS = [
  FAKE_LOCAL_PROVIDER,
  FAKE_REMOTE_PROVIDER,
  FAKE_REMOTE_SETTINGS_PROVIDER,
];
const ONE_DAY_IN_MS = 24 * 60 * 60 * 1000;
const SIX_MONTHS_IN_MS = (24 * 60 * 60 * 365 * 1000) / 2;
const FAKE_RESPONSE_HEADERS = { get() {} };
const FAKE_BUNDLE = [FAKE_LOCAL_MESSAGES[1], FAKE_LOCAL_MESSAGES[2]];

const USE_REMOTE_L10N_PREF =
  "browser.newtabpage.activity-stream.asrouter.useRemoteL10n";

// eslint-disable-next-line max-statements
describe("ASRouter", () => {
  let Router;
  let globals;
  let sandbox;
  let initParams;
  let messageBlockList;
  let providerBlockList;
  let messageImpressions;
  let groupImpressions;
  let previousSessionEnd;
  let fetchStub;
  let clock;
  let fakeAttributionCode;
  let fakeTargetingContext;
  let FakeToolbarBadgeHub;
  let FakeMomentsPageHub;
  let FakeSpecialMessageActions;
  let FakeMessagingSystemAllowlists;
  let ASRouterTargeting;
  let gBrowser;
  let screenImpressions;
  let multiProfileMessageImpressions;
  let multiProfileMessageBlocklist;

  function setMessageProviderPref(value) {
    sandbox.stub(ASRouterPreferences, "providers").get(() => value);
  }

  function initASRouter(router) {
    const getStub = sandbox.stub();
    getStub.returns(Promise.resolve());
    getStub
      .withArgs("messageBlockList")
      .returns(Promise.resolve(messageBlockList));
    getStub
      .withArgs("multiProfileMessageBlocklist")
      .returns(Promise.resolve(multiProfileMessageBlocklist));
    getStub
      .withArgs("providerBlockList")
      .returns(Promise.resolve(providerBlockList));
    getStub
      .withArgs("messageImpressions")
      .returns(Promise.resolve(messageImpressions));
    getStub.withArgs("groupImpressions").resolves(groupImpressions);
    getStub
      .withArgs("previousSessionEnd")
      .returns(Promise.resolve(previousSessionEnd));
    getStub
      .withArgs("screenImpressions")
      .returns(Promise.resolve(screenImpressions));
    initParams = {
      storage: {
        get: getStub,
        set: sandbox.stub().returns(Promise.resolve()),
        setSharedMessageImpressions: sandbox.stub(),
        getSharedMessageImpressions: sandbox
          .stub()
          .resolves(multiProfileMessageImpressions),
        setSharedMessageBlocked: sandbox.stub(),
        getSharedMessageBlocklist: sandbox
          .stub()
          .resolves(multiProfileMessageBlocklist),
        resetSharedMessageStorage: sandbox.stub().resolves(),
      },
      sendTelemetry: sandbox.stub().resolves(),
      clearChildMessages: sandbox.stub().resolves(),
      clearChildProviders: sandbox.stub().resolves(),
      updateAdminState: sandbox.stub().resolves(),
      dispatchCFRAction: sandbox.stub().resolves(),
    };
    MessageLoaderUtils._recordedReachIds = new Set();
    sandbox.stub(router, "loadMessagesFromAllProviders").callThrough();
    return router.init(initParams);
  }

  async function createRouterAndInit(providers = FAKE_PROVIDERS) {
    setMessageProviderPref(providers);
    // `.freeze` to catch any attempts at modifying the object
    Router = new _ASRouter(Object.freeze(FAKE_LOCAL_PROVIDERS));
    await initASRouter(Router);
  }

  beforeEach(async () => {
    globals = new GlobalOverrider();
    messageBlockList = [];
    providerBlockList = [];
    messageImpressions = {};
    groupImpressions = {};
    previousSessionEnd = 100;
    screenImpressions = {};
    multiProfileMessageImpressions = {};
    multiProfileMessageBlocklist = [];
    sandbox = sinon.createSandbox();
    ASRouterTargeting = {
      isMatch: sandbox.stub(),
      findMatchingMessage: sandbox.stub(),
      getMessageTriggers: message => {
        if (Array.isArray(message.triggers)) {
          return message.triggers;
        }
        return message.trigger ? [message.trigger] : [];
      },
      Environment: {
        locale: "en-US",
        localeLanguageCode: "en",
        browserSettings: {
          update: {
            channel: "default",
            enabled: true,
            autoDownload: true,
          },
        },
        attributionData: {},
        currentDate: "2000-01-01T10:00:0.001Z",
        profileAgeCreated: {},
        profileAgeReset: {},
        usesFirefoxSync: false,
        isFxAEnabled: true,
        isFxASignedIn: false,
        sync: {
          desktopDevices: 0,
          mobileDevices: 0,
          totalDevices: 0,
        },
        xpinstallEnabled: true,
        addonsInfo: {},
        searchEngines: {},
        isDefaultBrowser: false,
        devToolsOpenedCount: 5,
        topFrecentSites: {},
        recentBookmarks: {},
        pinnedSites: [
          {
            url: "https://amazon.com",
            host: "amazon.com",
            searchTopSite: true,
          },
        ],
        providerCohorts: {
          onboarding: "",
          cfr: "",
          "message-groups": "",
          "messaging-experiments": "",
        },
        totalBookmarksCount: {},
        firefoxVersion: 80,
        region: "US",
        needsUpdate: {},
        hasPinnedTabs: false,
        hasAccessedFxAPanel: false,
        userPrefs: {
          cfrFeatures: true,
          cfrAddons: true,
        },
        totalBlockedCount: {},
        blockedCountByType: {},
        attachedFxAOAuthClients: [],
        platformName: "macosx",
        scores: {},
        scoreThreshold: 5000,
        userId: "adsf",
        currentProfileId: "1",
        canCreateSelectableProfiles: false,
        hasSelectableProfiles: false,
      },
    };
    gBrowser = {
      selectedBrowser: {
        constructor: { name: "MozBrowser" },
        get documentGlobal() {
          return { gBrowser };
        },
      },
    };

    ASRouterPreferences.specialConditions = {
      someCondition: true,
    };
    sandbox.stub(ASRouterPreferences, "_maybeSetMessagingProfileID").resolves();
    sandbox.spy(ASRouterPreferences, "init");
    sandbox.spy(ASRouterPreferences, "uninit");
    sandbox.spy(ASRouterPreferences, "addListener");
    sandbox.spy(ASRouterPreferences, "removeListener");

    clock = sandbox.useFakeTimers();
    fetchStub = sandbox
      .stub(global, "fetch")
      .withArgs("http://fake.com/endpoint")
      .resolves({
        ok: true,
        status: 200,
        json: () => Promise.resolve({ messages: FAKE_REMOTE_MESSAGES }),
        headers: FAKE_RESPONSE_HEADERS,
      });
    sandbox.stub(global.Services.prefs, "getStringPref");

    fakeAttributionCode = {
      allowedCodeKeys: ["foo", "bar", "baz"],
      _clearCache: () => sinon.stub(),
      getAttrDataAsync: () => Promise.resolve({ content: "addonID" }),
      deleteFileAsync: () => Promise.resolve(),
      writeAttributionFile: () => Promise.resolve(),
      getCachedAttributionData: sinon.stub(),
    };
    FakeToolbarBadgeHub = {
      init: sandbox.stub(),
      uninit: sandbox.stub(),
      registerBadgeNotificationListener: sandbox.stub(),
    };
    FakeMomentsPageHub = {
      init: sandbox.stub(),
      uninit: sandbox.stub(),
      executeAction: sandbox.stub(),
    };
    fakeTargetingContext = {
      combineContexts: sandbox.stub(),
      evalWithDefault: sandbox.stub().resolves(),
    };
    let fakeNimbusFeatures = [
      "cfr",
      "infobar",
      "spotlight",
      "moments-page",
      "pbNewtab",
      "fxms-message-15",
    ].reduce((features, featureId) => {
      features[featureId] = {
        getAllEnrollments: sandbox.stub().returns([
          {
            meta: {
              slug: "experiment-slug",
              branch: "experiment-branch-slug",
              isRollout: false,
            },
            value: undefined,
          },
        ]),
        recordExposureEvent: sandbox.stub(),
      };
      return features;
    }, {});
    globals.set({
      // Testing framework doesn't know how to `defineESModuleGetters` so we're
      // importing these modules into the global scope ourselves.
      GroupsConfigurationProvider: { getMessages: () => Promise.resolve([]) },
      ASRouterPreferences,
      TARGETING_PREFERENCES,
      ASRouterTargeting,
      ASRouterTriggerListeners,
      QueryCache,
      gBrowser,
      gURLBar: {},
      isSeparateAboutWelcome: true,
      AttributionCode: fakeAttributionCode,
      PanelTestProvider,
      MacAttribution: { applicationPath: "" },
      ToolbarBadgeHub: FakeToolbarBadgeHub,
      MomentsPageHub: FakeMomentsPageHub,
      UnstoredDownloader: class {
        download() {
          return Promise.resolve({ buffer: "fake buffer" });
        }
      },
      NimbusFeatures: fakeNimbusFeatures,
      ExperimentAPI: {
        getAllBranches: sandbox.stub().resolves([]),
        ready: sandbox.stub().resolves(),
      },
      SpecialMessageActions: (FakeSpecialMessageActions = {
        handleAction: sandbox.stub(),
      }),
      MessagingSystemAllowlists: (FakeMessagingSystemAllowlists = {
        ensureInit: sandbox.stub().resolves(),
        getActionOnlyActions: sandbox.stub().returns([]),
      }),
      TargetingContext: class {
        static combineContexts(...args) {
          return fakeTargetingContext.combineContexts.apply(sandbox, args);
        }

        evalWithDefault(expr) {
          return fakeTargetingContext.evalWithDefault(expr);
        }
      },
      RemoteL10n: {
        // This is just a subset of supported locales that happen to be used in
        // the test.
        isLocaleSupported: locale => ["en-US", "ja-JP-mac"].includes(locale),
        // PathUtils.join() is mocked in `unit-entry.js`, only filenames count.
        cfrFluentFileDir: "ms-language-packs",
        cfrFluentFilePath: "asrouter.ftl",
      },
    });
    await createRouterAndInit();
  });
  afterEach(() => {
    Router.uninit();
    ASRouterPreferences.uninit();
    sandbox.restore();
    globals.restore();
  });

  describe(".state", () => {
    it("should throw if an attempt to set .state was made", () => {
      assert.throws(() => {
        Router.state = {};
      });
    });
  });

  describe("#init", () => {
    it("should only be called once", async () => {
      Router = new _ASRouter();
      let state = await initASRouter(Router);

      assert.equal(state, Router.state);

      assert.isNull(await Router.init({}));
    });
    it("should only be called once", async () => {
      Router = new _ASRouter();
      initASRouter(Router);
      let secondCall = await Router.init({});

      assert.isNull(
        secondCall,
        "Should not init twice, it should exit early with null"
      );
    });
    it("should set state.messageBlockList to the block list in persistent storage", async () => {
      messageBlockList = ["foo"];
      Router = new _ASRouter();
      await initASRouter(Router);

      assert.deepEqual(Router.state.messageBlockList, ["foo"]);
    });
    it("should initialize all the hub providers", async () => {
      // ASRouter init called in `beforeEach` block above

      assert.calledOnce(FakeToolbarBadgeHub.init);
      assert.calledOnce(FakeMomentsPageHub.init);

      assert.calledWithExactly(
        FakeToolbarBadgeHub.init,
        Router.waitForInitialized,
        {
          handleMessageRequest: Router.handleMessageRequest,
          addImpression: Router.addImpression,
          blockMessageById: Router.blockMessageById,
          sendTelemetry: Router.sendTelemetry,
          unblockMessageById: Router.unblockMessageById,
        }
      );

      assert.calledWithExactly(
        FakeMomentsPageHub.init,
        Router.waitForInitialized,
        {
          handleMessageRequest: Router.handleMessageRequest,
          addImpression: Router.addImpression,
          blockMessageById: Router.blockMessageById,
          sendTelemetry: Router.sendTelemetry,
        }
      );
    });
    it("should set state.messageImpressions to the messageImpressions object in persistent storage", async () => {
      // Note that messageImpressions are only kept if a message exists in router and has a .frequency property,
      // otherwise they will be cleaned up by .cleanupImpressions()
      const testMessage = { id: "foo", frequency: { lifetimeCap: 10 } };
      messageImpressions = { foo: [0, 1, 2] };
      setMessageProviderPref([
        { id: "onboarding", type: "local", messages: [testMessage] },
      ]);
      Router = new _ASRouter();
      await initASRouter(Router);

      assert.deepEqual(Router.state.messageImpressions, messageImpressions);
    });
    it("should set state.screenImpressions to the screenImpressions object in persistent storage", async () => {
      screenImpressions = { test: 123 };

      Router = new _ASRouter();
      await initASRouter(Router);

      assert.deepEqual(Router.state.screenImpressions, screenImpressions);
    });
    it("should clear impressions for groups that are not active once they are more than six months old", async () => {
      groupImpressions = { foo: [Date.now() - SIX_MONTHS_IN_MS - 1] };
      Router = new _ASRouter();
      await initASRouter(Router);

      assert.notProperty(Router.state.groupImpressions, "foo");
    });
    it("should keep impressions for groups that are active", async () => {
      Router = new _ASRouter();
      await initASRouter(Router);
      await Router.setState(() => {
        return {
          groups: [
            {
              id: "foo",
              enabled: true,
              frequency: {
                custom: [{ period: ONE_DAY_IN_MS, cap: 10 }],
                lifetime: Infinity,
              },
            },
          ],
          groupImpressions: { foo: [Date.now()] },
        };
      });
      Router.cleanupImpressions();

      assert.property(Router.state.groupImpressions, "foo");
      assert.lengthOf(Router.state.groupImpressions.foo, 1);
    });
    it("should remove old impressions for a group", async () => {
      Router = new _ASRouter();
      await initASRouter(Router);
      await Router.setState(() => {
        return {
          groups: [
            {
              id: "foo",
              enabled: true,
              frequency: {
                custom: [{ period: ONE_DAY_IN_MS, cap: 10 }],
              },
            },
          ],
          groupImpressions: {
            foo: [Date.now() - ONE_DAY_IN_MS - 1, Date.now()],
          },
        };
      });
      Router.cleanupImpressions();

      assert.property(Router.state.groupImpressions, "foo");
      assert.lengthOf(Router.state.groupImpressions.foo, 1);
    });
    it("should await .loadMessagesFromAllProviders() and add messages from providers to state.messages", async () => {
      Router = new _ASRouter(Object.freeze(FAKE_LOCAL_PROVIDERS));

      await initASRouter(Router);

      assert.calledOnce(Router.loadMessagesFromAllProviders);
      assert.isArray(Router.state.messages);
      assert.lengthOf(
        Router.state.messages,
        FAKE_LOCAL_MESSAGES.length + FAKE_REMOTE_MESSAGES.length
      );
    });
    it("should set state.previousSessionEnd from IndexedDB", async () => {
      previousSessionEnd = 200;
      await createRouterAndInit();

      assert.equal(Router.state.previousSessionEnd, previousSessionEnd);
    });
    it("should assign ASRouterPreferences.specialConditions to state", async () => {
      assert.isTrue(ASRouterPreferences.specialConditions.someCondition);
      assert.isTrue(Router.state.someCondition);
    });
    it("should add observer for `intl:app-locales-changed`", async () => {
      sandbox.spy(global.Services.obs, "addObserver");
      await createRouterAndInit();

      assert.calledWithExactly(
        global.Services.obs.addObserver,
        Router._onLocaleChanged,
        "intl:app-locales-changed"
      );
    });
    it("should add a pref observer", async () => {
      sandbox.spy(global.Services.prefs, "addObserver");
      await createRouterAndInit();

      assert.calledOnce(global.Services.prefs.addObserver);
      assert.calledWithExactly(
        global.Services.prefs.addObserver,
        USE_REMOTE_L10N_PREF,
        Router
      );
    });
    describe("lazily loading local test providers", () => {
      let justIdAndContent = ({ id, content }) => ({ id, content });
      afterEach(() => Router.uninit());

      it("should add the local test providers on init if devtools are enabled", async () => {
        sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);

        await createRouterAndInit();

        assert.property(Router._localProviders, "PanelTestProvider");
      });
      it("should not add the local test providers on init if devtools are disabled", async () => {
        sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => false);

        await createRouterAndInit();

        assert.notProperty(Router._localProviders, "PanelTestProvider");
      });
      it("should flatten experiment translated messages from local test providers if devtools are enabled...", async () => {
        sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);

        await createRouterAndInit();

        assert.property(Router._localProviders, "PanelTestProvider");

        expect(
          Router.state.messages.map(justIdAndContent)
        ).to.deep.include.members([
          { id: "experimentL10n", content: { text: "UniqueText" } },
        ]);
      });
      it("...but not if devtools are disabled", async () => {
        sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => false);

        await createRouterAndInit();

        assert.notProperty(Router._localProviders, "PanelTestProvider");

        let justIdAndContentMessages =
          Router.state.messages.map(justIdAndContent);
        expect(justIdAndContentMessages).not.to.deep.include.members([
          { id: "experimentL10n", content: { text: "UniqueText" } },
        ]);
        expect(justIdAndContentMessages).to.deep.include.members([
          {
            id: "experimentL10n",
            content: { text: { $l10n: { text: "UniqueText" } } },
          },
        ]);
      });
    });

    it("should load shared message impressions and blocklist when selectable profiles are enabled", async () => {
      const testMessage = { id: "msg1", frequency: { lifetimeCap: 10 } };
      setMessageProviderPref([
        { id: "onboarding", type: "local", messages: [testMessage] },
      ]);

      const testMultiProfileImpressions = { msg1: [123, 456] };
      const testMultiProfileBlocklist = ["blocked1", "blocked2"];
      const getSharedMessageImpressions = sandbox
        .stub()
        .resolves(testMultiProfileImpressions);
      const getSharedMessageBlocklist = sandbox
        .stub()
        .resolves(testMultiProfileBlocklist);

      sandbox
        .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
        .value(true);
      sandbox
        .stub(ASRouterTargeting.Environment, "hasSelectableProfiles")
        .value(true);

      Router = new _ASRouter();
      const getStub = sandbox.stub();

      const testInitParams = {
        storage: {
          get: getStub,
          set: sandbox.stub().returns(Promise.resolve()),
          getSharedMessageImpressions,
          getSharedMessageBlocklist,
        },
      };

      await Router.init(testInitParams);

      assert.calledOnce(getSharedMessageImpressions);
      assert.calledOnce(getSharedMessageBlocklist);
      assert.deepEqual(
        Router.state.multiProfileMessageImpressions,
        testMultiProfileImpressions
      );
      assert.deepEqual(
        Router.state.multiProfileMessageBlocklist,
        testMultiProfileBlocklist
      );
    });

    it("should not load shared data when selectable profiles are disabled", async () => {
      const getSharedMessageImpressions = sandbox.stub();
      const getSharedMessageBlocklist = sandbox.stub();

      sandbox
        .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
        .value(false);
      sandbox
        .stub(ASRouterTargeting.Environment, "hasSelectableProfiles")
        .value(false);

      Router = new _ASRouter();
      const getStub = sandbox.stub();

      const testInitParams = {
        storage: {
          get: getStub,
          set: sandbox.stub().returns(Promise.resolve()),
          getSharedMessageImpressions,
          getSharedMessageBlocklist,
        },
      };

      await Router.init(testInitParams);

      assert.notCalled(getSharedMessageImpressions);
      assert.notCalled(getSharedMessageBlocklist);
    });

    it("should add observer for multiprofile data updates", async () => {
      sandbox.spy(global.Services.obs, "addObserver");
      await createRouterAndInit();

      assert.calledWithExactly(
        global.Services.obs.addObserver,
        Router._updateMultiprofileData,
        "sps-profiles-updated"
      );
    });
  });

  describe("preference changes", () => {
    it("should call ASRouterPreferences.init and add a listener on init", () => {
      assert.calledOnce(ASRouterPreferences.init);
      assert.calledWith(ASRouterPreferences.addListener, Router.onPrefChange);
    });
    it("should call ASRouterPreferences.uninit and remove the listener on uninit", () => {
      Router.uninit();
      assert.calledOnce(ASRouterPreferences.uninit);
      assert.calledWith(
        ASRouterPreferences.removeListener,
        Router.onPrefChange
      );
    });
    it("should call clearChildMessages (does nothing, see bug 1899028)", async () => {
      const messageTargeted = {
        id: "1",
        campaign: "foocampaign",
        targeting: "true",
        groups: ["cfr"],
        provider: "cfr",
      };
      const messageNotTargeted = {
        id: "2",
        campaign: "foocampaign",
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({
        messages: [messageTargeted, messageNotTargeted],
        providers: [{ id: "cfr" }],
      });
      fakeTargetingContext.evalWithDefault.resolves(false);

      await Router.onPrefChange("services.sync.username");

      assert.calledOnce(initParams.clearChildMessages);
      assert.calledWith(initParams.clearChildMessages, [messageTargeted.id]);
    });
    it("should call loadMessagesFromAllProviders on pref change", () => {
      ASRouterPreferences.observe(null, null, MESSAGE_PROVIDER_PREF_NAME);
      assert.calledOnce(Router.loadMessagesFromAllProviders);
    });
    it("should update groups state if a user pref changes", async () => {
      await Router.setState({
        groups: [{ id: "foo", userPreferences: ["bar"], enabled: true }],
      });
      sandbox.stub(ASRouterPreferences, "getUserPreference");

      await Router.onPrefChange(MESSAGE_PROVIDER_PREF_NAME);

      assert.calledWithExactly(ASRouterPreferences.getUserPreference, "bar");
    });
    it("should update the list of providers on pref change", async () => {
      const modifiedRemoteProvider = Object.assign({}, FAKE_REMOTE_PROVIDER, {
        url: "baz.com",
      });
      setMessageProviderPref([
        FAKE_LOCAL_PROVIDER,
        modifiedRemoteProvider,
        FAKE_REMOTE_SETTINGS_PROVIDER,
      ]);

      const { length } = Router.state.providers;

      ASRouterPreferences.observe(null, null, MESSAGE_PROVIDER_PREF_NAME);
      await Router._updateMessageProviders();

      const provider = Router.state.providers.find(p => p.url === "baz.com");
      assert.lengthOf(Router.state.providers, length);
      assert.isDefined(provider);
    });
    it("should clear disabled providers on pref change", async () => {
      const TEST_PROVIDER_ID = "some_provider_id";
      await Router.setState({
        providers: [{ id: TEST_PROVIDER_ID }],
      });
      const modifiedRemoteProvider = Object.assign({}, FAKE_REMOTE_PROVIDER, {
        id: TEST_PROVIDER_ID,
        enabled: false,
      });
      setMessageProviderPref([
        FAKE_LOCAL_PROVIDER,
        modifiedRemoteProvider,
        FAKE_REMOTE_SETTINGS_PROVIDER,
      ]);
      await Router.onPrefChange(MESSAGE_PROVIDER_PREF_NAME);

      assert.calledOnce(initParams.clearChildProviders);
      assert.calledWith(initParams.clearChildProviders, [TEST_PROVIDER_ID]);
    });
  });

  describe("setState", () => {
    it("should broadcast a message to update the admin tool on a state change if the asrouter.devtoolsEnabled pref is", async () => {
      sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);
      sandbox.stub(Router, "getTargetingParameters").resolves({});
      const state = await Router.setState({ foo: 123 });

      assert.calledOnce(initParams.updateAdminState);
      assert.deepEqual(state.providerPrefs, ASRouterPreferences.providers);
      assert.deepEqual(
        state.userPrefs,
        ASRouterPreferences.getAllUserPreferences()
      );
      assert.deepEqual(state.targetingParameters, {});
      assert.deepEqual(state.errors, Router.errors);
    });
    it("should not send a message on a state change asrouter.devtoolsEnabled pref is on", async () => {
      sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => false);
      await Router.setState({ foo: 123 });

      assert.notCalled(initParams.updateAdminState);
    });
  });

  describe("getTargetingParameters", () => {
    it("should return the targeting parameters", async () => {
      const stub = sandbox.stub().resolves("foo");
      const obj = { foo: 1 };
      sandbox.stub(obj, "foo").get(stub);
      const result = await Router.getTargetingParameters(obj, obj);

      assert.calledTwice(stub);
      assert.propertyVal(result, "foo", "foo");
    });
  });

  describe("evaluateExpression", () => {
    it("should call ASRouterTargeting to evaluate", async () => {
      fakeTargetingContext.evalWithDefault.resolves("foo");
      const response = await Router.evaluateExpression({});
      assert.equal(response.evaluationStatus.result, "foo");
      assert.isTrue(response.evaluationStatus.success);
    });
    it("should catch evaluation errors", async () => {
      fakeTargetingContext.evalWithDefault.returns(
        Promise.reject(new Error("fake error"))
      );
      const response = await Router.evaluateExpression({});
      assert.isFalse(response.evaluationStatus.success);
    });
  });

  describe("#routeCFRMessage", () => {
    let browser;
    beforeEach(() => {
      browser = {};
    });
    it("should route moments messages to the right hub", () => {
      Router.routeCFRMessage({ template: "update_action" }, browser, "", true);

      assert.calledOnce(FakeMomentsPageHub.executeAction);
      assert.notCalled(FakeToolbarBadgeHub.registerBadgeNotificationListener);
    });
    it("should route toolbar_badge message to the right hub", () => {
      Router.routeCFRMessage({ template: "toolbar_badge" }, browser);

      assert.calledOnce(FakeToolbarBadgeHub.registerBadgeNotificationListener);
      assert.notCalled(FakeMomentsPageHub.executeAction);
    });
    it("should route default to sending to content", () => {
      Router.routeCFRMessage(
        { template: "some_other_template" },
        browser,
        {},
        true
      );

      assert.notCalled(FakeToolbarBadgeHub.registerBadgeNotificationListener);
      assert.notCalled(FakeMomentsPageHub.executeAction);
    });
    describe("action template", () => {
      let addImpressionStub;
      let blockMessageByIdStub;
      const allowedMessage = {
        id: "TEST_ACTION",
        template: "action_only",
        content: { action: { type: "CONFIRM_LAUNCH_ON_LOGIN" } },
      };
      beforeEach(() => {
        addImpressionStub = sandbox.stub(Router, "addImpression");
        blockMessageByIdStub = sandbox.stub(Router, "blockMessageById");
      });
      it("calls handleAction and records an impression without blocking for an allowed action", async () => {
        Router.routeCFRMessage(allowedMessage, browser, {});
        await Promise.resolve(); // let dispatchCFRAction's async wrapper resolve

        assert.calledOnceWithExactly(
          FakeSpecialMessageActions.handleAction,
          allowedMessage.content.action,
          browser
        );
        assert.calledWithMatch(initParams.dispatchCFRAction, {
          type: "ACTION_ONLY_TELEMETRY",
          data: {
            action: "action_only_user_event",
            message_id: allowedMessage.id,
            event: "IMPRESSION",
          },
        });
        assert.calledWithMatch(initParams.dispatchCFRAction, {
          type: "IMPRESSION",
          data: allowedMessage,
        });
        assert.notCalled(addImpressionStub);
        assert.notCalled(blockMessageByIdStub);
      });
      it("allowlists the SET_DEFAULT_BROWSER_OPEN_WITH action", () => {
        assert.isTrue(
          Router._isAllowedActionOnlyMessageAction({
            type: "SET_DEFAULT_BROWSER_OPEN_WITH",
          }),
          "SET_DEFAULT_BROWSER_OPEN_WITH is an allowed action-only action"
        );
      });
      it("allowlists ENABLE_CLOSED_BROWSER_NOTIFICATIONS but not its opposite", () => {
        assert.isTrue(
          Router._isAllowedActionOnlyMessageAction({
            type: "ENABLE_CLOSED_BROWSER_NOTIFICATIONS",
          }),
          "the enabling action can fire from an action_only message"
        );
        assert.isFalse(
          Router._isAllowedActionOnlyMessageAction({
            type: "DISABLE_CLOSED_BROWSER_NOTIFICATIONS",
          }),
          "the disabling action only ever fires from UI the user clicked"
        );
      });
      it("does nothing for a non-allowlisted action", () => {
        const badMessage = {
          id: "BAD",
          template: "action_only",
          content: { action: { type: "OPEN_URL" } },
        };
        Router.routeCFRMessage(badMessage, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
        assert.notCalled(blockMessageByIdStub);
      });
      it("allows MULTI_ACTION when every nested action is allowlisted", async () => {
        const multiMessage = {
          id: "MULTI",
          template: "action_only",
          content: {
            action: {
              type: "MULTI_ACTION",
              data: {
                actions: [
                  { type: "CONFIRM_LAUNCH_ON_LOGIN" },
                  { type: "PIN_FIREFOX_TO_TASKBAR" },
                ],
              },
            },
          },
        };
        Router.routeCFRMessage(multiMessage, browser, {});
        await Promise.resolve(); // let dispatchCFRAction's async wrapper resolve

        assert.calledOnce(FakeSpecialMessageActions.handleAction);
        assert.calledWithMatch(initParams.dispatchCFRAction, {
          type: "IMPRESSION",
          data: multiMessage,
        });
        assert.notCalled(addImpressionStub);
      });
      it("rejects MULTI_ACTION when any nested action is not allowlisted", () => {
        const badMulti = {
          id: "BAD_MULTI",
          template: "action_only",
          content: {
            action: {
              type: "MULTI_ACTION",
              data: {
                actions: [
                  { type: "CONFIRM_LAUNCH_ON_LOGIN" },
                  { type: "OPEN_URL" },
                ],
              },
            },
          },
        };
        Router.routeCFRMessage(badMulti, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
      });
      it("rejects a nested MULTI_ACTION even if MULTI_ACTION is allowlisted", () => {
        // MULTI_ACTION is not blocklisted, so Remote Settings can grant it. It
        // is still only valid as the top level action.
        FakeMessagingSystemAllowlists.getActionOnlyActions.returns([
          "MULTI_ACTION",
        ]);
        const nestedMulti = {
          id: "NESTED_MULTI",
          template: "action_only",
          content: {
            action: {
              type: "MULTI_ACTION",
              data: {
                actions: [
                  {
                    type: "MULTI_ACTION",
                    data: { actions: [{ type: "OPEN_URL" }] },
                  },
                ],
              },
            },
          },
        };
        Router.routeCFRMessage(nestedMulti, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
      });
      it("rejects a MULTI_ACTION with no nested actions", () => {
        const emptyMulti = {
          id: "EMPTY_MULTI",
          template: "action_only",
          content: {
            action: { type: "MULTI_ACTION", data: { actions: [] } },
          },
        };
        Router.routeCFRMessage(emptyMulti, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
      });
      it("does nothing when the message has no action", () => {
        const noAction = {
          id: "NO_ACTION",
          template: "action_only",
          content: {},
        };
        Router.routeCFRMessage(noAction, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
        assert.notCalled(blockMessageByIdStub);
      });
      it("allows an action supplied only by the Remote Settings allowlist", async () => {
        FakeMessagingSystemAllowlists.getActionOnlyActions.returns([
          "FOO_ACTION",
        ]);
        const rsMessage = {
          id: "RS_ACTION",
          template: "action_only",
          content: { action: { type: "FOO_ACTION" } },
        };
        Router.routeCFRMessage(rsMessage, browser, {});
        await Promise.resolve(); // let dispatchCFRAction's async wrapper resolve

        assert.calledOnceWithExactly(
          FakeSpecialMessageActions.handleAction,
          rsMessage.content.action,
          browser
        );
        assert.notCalled(addImpressionStub);
        assert.calledWithMatch(initParams.dispatchCFRAction, {
          type: "IMPRESSION",
          data: rsMessage,
        });
      });
      it("rejects an action absent from both baseline and Remote Settings", () => {
        FakeMessagingSystemAllowlists.getActionOnlyActions.returns([
          "FOO_ACTION",
        ]);
        const badMessage = {
          id: "BAR",
          template: "action_only",
          content: { action: { type: "BAR_ACTION" } },
        };
        Router.routeCFRMessage(badMessage, browser, {});

        assert.notCalled(FakeSpecialMessageActions.handleAction);
        assert.notCalled(addImpressionStub);
      });
      describe("promptOnly pin actions", () => {
        it("sets promptOnly on a top level PIN_FIREFOX_TO_TASKBAR action", () => {
          const message = {
            id: "PIN",
            template: "action_only",
            content: {
              action: {
                type: "PIN_FIREFOX_TO_TASKBAR",
                data: { fireAndForget: true },
              },
            },
          };
          const original = structuredClone(message);
          Router.routeCFRMessage(message, browser, {});

          assert.calledOnceWithExactly(
            FakeSpecialMessageActions.handleAction,
            {
              type: "PIN_FIREFOX_TO_TASKBAR",
              data: { fireAndForget: true, promptOnly: true },
            },
            browser
          );
          assert.deepEqual(message, original, "message is not modified");
        });
        it("sets promptOnly on a PIN_FIREFOX_TO_TASKBAR action with no data", () => {
          const message = {
            id: "PIN_NO_DATA",
            template: "action_only",
            content: { action: { type: "PIN_FIREFOX_TO_TASKBAR" } },
          };
          Router.routeCFRMessage(message, browser, {});

          assert.calledOnceWithExactly(
            FakeSpecialMessageActions.handleAction,
            { type: "PIN_FIREFOX_TO_TASKBAR", data: { promptOnly: true } },
            browser
          );
          assert.deepEqual(message.content.action, {
            type: "PIN_FIREFOX_TO_TASKBAR",
          });
        });
        it("sets promptOnly on a PIN_AND_DEFAULT action", () => {
          FakeMessagingSystemAllowlists.getActionOnlyActions.returns([
            "PIN_AND_DEFAULT",
          ]);
          const message = {
            id: "PIN_AND_DEFAULT",
            template: "action_only",
            content: { action: { type: "PIN_AND_DEFAULT" } },
          };
          Router.routeCFRMessage(message, browser, {});

          assert.calledOnceWithExactly(
            FakeSpecialMessageActions.handleAction,
            { type: "PIN_AND_DEFAULT", data: { promptOnly: true } },
            browser
          );
          assert.deepEqual(message.content.action, {
            type: "PIN_AND_DEFAULT",
          });
        });
        it("sets promptOnly on pin actions nested in a MULTI_ACTION", () => {
          const message = {
            id: "MULTI_PIN",
            template: "action_only",
            content: {
              action: {
                type: "MULTI_ACTION",
                data: {
                  orderedExecution: true,
                  actions: [
                    { type: "CONFIRM_LAUNCH_ON_LOGIN" },
                    {
                      type: "PIN_FIREFOX_TO_TASKBAR",
                      data: { privatePin: true },
                    },
                  ],
                },
              },
            },
          };
          const original = structuredClone(message);
          Router.routeCFRMessage(message, browser, {});

          assert.calledOnceWithExactly(
            FakeSpecialMessageActions.handleAction,
            {
              type: "MULTI_ACTION",
              data: {
                orderedExecution: true,
                actions: [
                  { type: "CONFIRM_LAUNCH_ON_LOGIN" },
                  {
                    type: "PIN_FIREFOX_TO_TASKBAR",
                    data: { privatePin: true, promptOnly: true },
                  },
                ],
              },
            },
            browser
          );
          assert.deepEqual(message, original, "message is not modified");
        });
        it("does not set promptOnly on other actions", () => {
          Router.routeCFRMessage(allowedMessage, browser, {});

          assert.calledOnceWithExactly(
            FakeSpecialMessageActions.handleAction,
            { type: "CONFIRM_LAUNCH_ON_LOGIN" },
            browser
          );
        });
      });
      it("allows MULTI_ACTION mixing baseline and Remote Settings actions", async () => {
        FakeMessagingSystemAllowlists.getActionOnlyActions.returns([
          "FOO_ACTION",
        ]);
        const multiMessage = {
          id: "MIXED_MULTI",
          template: "action_only",
          content: {
            action: {
              type: "MULTI_ACTION",
              data: {
                actions: [
                  { type: "CONFIRM_LAUNCH_ON_LOGIN" },
                  { type: "FOO_ACTION" },
                ],
              },
            },
          },
        };
        Router.routeCFRMessage(multiMessage, browser, {});
        await Promise.resolve(); // let dispatchCFRAction's async wrapper resolve

        assert.calledOnceWithExactly(
          FakeSpecialMessageActions.handleAction,
          multiMessage.content.action,
          browser
        );
        assert.notCalled(addImpressionStub);
        assert.calledWithMatch(initParams.dispatchCFRAction, {
          type: "IMPRESSION",
          data: multiMessage,
        });
      });
    });
    it("should tag the message with panel_local_testing when force is true", () => {
      const msg = {
        template: "feature_callout",
        provider: "cfr",
        targeting: "true",
      };
      const { message } = Router.routeCFRMessage(msg, browser, {}, true);
      assert.equal(message.provider, "panel_local_testing");
      assert.include(
        message.targeting,
        'providerCohorts.panel_local_testing == "SHOW_TEST"'
      );
    });
    it("should not modify the message provider when force is false", () => {
      const msg = {
        template: "feature_callout",
        provider: "cfr",
        targeting: "true",
      };
      const { message } = Router.routeCFRMessage(msg, browser, {}, false);
      assert.equal(message.provider, "cfr");
    });
  });

  describe("#loadMessagesFromAllProviders", () => {
    function assertRouterContainsMessages(messages) {
      const messageIdsInRouter = Router.state.messages.map(m => m.id);
      for (const message of messages) {
        assert.include(messageIdsInRouter, message.id);
      }
    }

    it("should not trigger an update if not enough time has passed for a provider", async () => {
      await createRouterAndInit([
        {
          id: "remotey",
          type: "remote",
          enabled: true,
          url: "http://fake.com/endpoint",
          updateCycleInMs: 300,
        },
      ]);

      const previousState = Router.state;

      // Since we've previously gotten messages during init and we haven't advanced our fake timer,
      // no updates should be triggered.
      await Router.loadMessagesFromAllProviders();
      assert.deepEqual(Router.state, previousState);
    });
    it("should not trigger an update if we only have local providers", async () => {
      await createRouterAndInit([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: FAKE_LOCAL_MESSAGES,
        },
      ]);

      const previousState = Router.state;
      const stub = sandbox.stub(MessageLoaderUtils, "loadMessagesForProvider");

      clock.tick(300);

      await Router.loadMessagesFromAllProviders();

      assert.deepEqual(Router.state, previousState);
      assert.notCalled(stub);
    });
    it("should update messages for a provider if enough time has passed, without removing messages for other providers", async () => {
      const NEW_MESSAGES = [{ id: "new_123" }];
      await createRouterAndInit([
        {
          id: "remotey",
          type: "remote",
          url: "http://fake.com/endpoint",
          enabled: true,
          updateCycleInMs: 300,
        },
        {
          id: "alocalprovider",
          type: "local",
          enabled: true,
          messages: FAKE_LOCAL_MESSAGES,
        },
      ]);
      fetchStub.withArgs("http://fake.com/endpoint").resolves({
        ok: true,
        status: 200,
        json: () => Promise.resolve({ messages: NEW_MESSAGES }),
        headers: FAKE_RESPONSE_HEADERS,
      });

      clock.tick(301);
      await Router.loadMessagesFromAllProviders();

      // These are the new messages
      assertRouterContainsMessages(NEW_MESSAGES);
      // These are the local messages that should not have been deleted
      assertRouterContainsMessages(FAKE_LOCAL_MESSAGES);
    });
    it("should parse the triggers in the messages and register the trigger listeners", async () => {
      sandbox.spy(ASRouterTriggerListeners.get("openURL"), "init");

      await createRouterAndInit([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: [
            {
              id: "foo",
              template: "simple_template",
              trigger: { id: "firstRun" },
              content: { title: "Foo", body: "Foo123" },
            },
            {
              id: "bar1",
              template: "simple_template",
              trigger: {
                id: "openURL",
                params: ["www.mozilla.org", "www.mozilla.com"],
              },
              content: { title: "Bar1", body: "Bar123" },
            },
            {
              id: "bar2",
              template: "simple_template",
              trigger: { id: "openURL", params: ["www.example.com"] },
              content: { title: "Bar2", body: "Bar123" },
            },
          ],
        },
      ]);
      assert.calledTwice(ASRouterTriggerListeners.get("openURL").init);
      assert.calledWithExactly(
        ASRouterTriggerListeners.get("openURL").init,
        Router._triggerHandler,
        ["www.mozilla.org", "www.mozilla.com"],
        undefined, // patterns
        undefined // regexPatterns
      );
      assert.calledWithExactly(
        ASRouterTriggerListeners.get("openURL").init,
        Router._triggerHandler,
        ["www.example.com"],
        undefined, // patterns
        undefined // regexPatterns
      );
    });
    it("should register a listener for each trigger of a multi-trigger message", async () => {
      sandbox.spy(ASRouterTriggerListeners.get("openURL"), "init");
      sandbox.spy(ASRouterTriggerListeners.get("frequentVisits"), "init");

      await createRouterAndInit([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: [
            {
              id: "multi",
              template: "simple_template",
              triggers: [
                { id: "openURL", params: ["www.mozilla.org"] },
                { id: "frequentVisits", params: ["www.example.com"] },
              ],
              content: { title: "Multi", body: "Multi123" },
            },
          ],
        },
      ]);

      assert.calledWithExactly(
        ASRouterTriggerListeners.get("openURL").init,
        Router._triggerHandler,
        ["www.mozilla.org"],
        undefined, // patterns
        undefined // regexPatterns
      );
      assert.calledWithExactly(
        ASRouterTriggerListeners.get("frequentVisits").init,
        Router._triggerHandler,
        ["www.example.com"],
        undefined, // patterns
        undefined // regexPatterns
      );
    });
    it("should parse the message's messagesLoaded trigger and immediately fire trigger", async () => {
      setMessageProviderPref([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: [
            {
              id: "foo",
              template: "simple_template",
              trigger: { id: "messagesLoaded" },
              content: { title: "Foo", body: "Bar123" },
            },
          ],
        },
      ]);
      Router = new _ASRouter(Object.freeze(FAKE_LOCAL_PROVIDERS));
      sandbox.spy(Router, "sendTriggerMessage");
      await initASRouter(Router);
      assert.calledOnce(Router.sendTriggerMessage);
      assert.calledWith(
        Router.sendTriggerMessage,
        sandbox.match({ id: "messagesLoaded" }),
        true
      );
    });
    it("should not register a trigger listener in automation for a message with skip_in_tests", async () => {
      sandbox.spy(ASRouterTriggerListeners.get("openURL"), "init");
      await createRouterAndInit([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: [
            {
              id: "foo",
              template: "simple_template",
              trigger: { id: "openURL" },
              content: { title: "Foo", body: "Foo123" },
              skip_in_tests: "testing",
            },
          ],
        },
      ]);
      assert.notCalled(ASRouterTriggerListeners.get("openURL").init);
    });
    it("should gracefully handle messages loading before a window or browser exists", async () => {
      sandbox.stub(global, "gBrowser").value(undefined);
      sandbox
        .stub(global.Services.wm, "getMostRecentBrowserWindow")
        .returns(undefined);
      setMessageProviderPref([
        {
          id: "foo",
          type: "local",
          enabled: true,
          messages: [
            "toolbar_badge",
            "update_action",
            "infobar",
            "spotlight",
            "toast_notification",
          ].map((template, i) => {
            return {
              id: `foo${i}`,
              template,
              trigger: { id: "messagesLoaded" },
              content: { title: `Foo${i}`, body: "Bar123" },
            };
          }),
        },
      ]);
      Router = new _ASRouter(Object.freeze(FAKE_LOCAL_PROVIDERS));
      sandbox.spy(Router, "sendTriggerMessage");
      await initASRouter(Router);
      assert.calledWith(
        Router.sendTriggerMessage,
        sandbox.match({ id: "messagesLoaded" }),
        true
      );
    });
    it("should gracefully handle RemoteSettings blowing up and dispatch undesired event", async () => {
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsMessages")
        .rejects("fake error");
      await createRouterAndInit();
      assert.calledWith(initParams.dispatchCFRAction, {
        type: "AS_ROUTER_TELEMETRY_USER_EVENT",
        data: {
          action: "asrouter_undesired_event",
          message_id: "n/a",
          event: "ASR_RS_ERROR",
          event_context: "remotey-settingsy",
        },
      });
    });
    it("should dispatch undesired event if RemoteSettings returns no messages", async () => {
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsMessages")
        .resolves([]);
      assert.calledWith(initParams.dispatchCFRAction, {
        type: "AS_ROUTER_TELEMETRY_USER_EVENT",
        data: {
          action: "asrouter_undesired_event",
          message_id: "n/a",
          event: "ASR_RS_NO_MESSAGES",
          event_context: "remotey-settingsy",
        },
      });
    });
    it("should download the attachment if RemoteSettings returns some messages", async () => {
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsMessages")
        .resolves([{ id: "message_1" }]);
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsLanguagePackRecord")
        .resolves({ attachment: { size: 123 } });
      sandbox.stub(global.IOUtils, "exists").resolves(false);
      const spy = sandbox.spy();
      global.UnstoredDownloader.prototype.download = spy;
      const provider = {
        id: "cfr",
        enabled: true,
        type: "remote-settings",
        collection: "cfr",
      };
      await createRouterAndInit([provider]);

      assert.calledOnce(spy);
    });
    it("should dispatch undesired event if the ms-language-packs returns no messages", async () => {
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsMessages")
        .resolves([{ id: "message_1" }]);
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsLanguagePackRecord")
        .resolves(null);
      const provider = {
        id: "cfr",
        enabled: true,
        type: "remote-settings",
        collection: "cfr",
      };
      await createRouterAndInit([provider]);

      assert.calledWith(initParams.dispatchCFRAction, {
        type: "AS_ROUTER_TELEMETRY_USER_EVENT",
        data: {
          action: "asrouter_undesired_event",
          message_id: "n/a",
          event: "ASR_RS_NO_MESSAGES",
          event_context: "ms-language-packs",
        },
      });
    });
  });

  describe("#_updateMessageProviders", () => {
    it("should correctly replace %STARTPAGE_VERSION% in remote provider urls", async () => {
      // If this test fails, you need to update the constant STARTPAGE_VERSION in
      // ASRouter.sys.mjs to match the `version` property of provider-response-schema.json
      const expectedStartpageVersion = ProviderResponseSchema.version;
      const provider = {
        id: "foo",
        enabled: true,
        type: "remote",
        url: "https://www.mozilla.org/%STARTPAGE_VERSION%/",
      };
      setMessageProviderPref([provider]);
      await Router._updateMessageProviders();
      assert.equal(
        Router.state.providers[0].url,
        `https://www.mozilla.org/${parseInt(expectedStartpageVersion, 10)}/`
      );
    });
    it("should replace other params in remote provider urls by calling Services.urlFormater.formatURL", async () => {
      const url = "https://www.example.com/";
      const replacedUrl = "https://www.foo.bar/";
      const stub = sandbox
        .stub(global.Services.urlFormatter, "formatURL")
        .withArgs(url)
        .returns(replacedUrl);
      const provider = { id: "foo", enabled: true, type: "remote", url };
      setMessageProviderPref([provider]);
      await Router._updateMessageProviders();
      assert.calledOnce(stub);
      assert.calledWithExactly(stub, url);
      assert.equal(Router.state.providers[0].url, replacedUrl);
    });
    it("should only add the providers that are enabled", async () => {
      const providers = [
        {
          id: "foo",
          enabled: false,
          type: "remote",
          url: "https://www.foo.com/",
        },
        {
          id: "bar",
          enabled: true,
          type: "remote",
          url: "https://www.bar.com/",
        },
      ];
      setMessageProviderPref(providers);
      await Router._updateMessageProviders();
      assert.equal(Router.state.providers.length, 1);
      assert.equal(Router.state.providers[0].id, providers[1].id);
    });
  });

  describe("#handleMessageRequest", () => {
    beforeEach(async () => {
      await Router.setState(() => ({
        providers: [{ id: "cfr" }, { id: "badge" }],
      }));
    });
    it("should return no messages if shouldShowMessagesToProfile returns false", async () => {
      sandbox.stub(Router, "shouldShowMessagesToProfile").returns(false);
      await Router.setState(() => ({
        messages: [
          { id: "foo", provider: "cfr", groups: ["cfr"] },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
      }));
      const result = await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.isNull(result);
    });
    it("should return messages if shouldShowMessagesToProfile returns true", async () => {
      sandbox.stub(Router, "shouldShowMessagesToProfile").returns(true);
      await Router.setState(() => ({
        messages: [
          { id: "foo", provider: "cfr", groups: ["cfr"] },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
      }));
      const result = await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.isNotNull(result);
      assert.calledWithMatch(ASRouterTargeting.findMatchingMessage, {
        messages: [
          { id: "foo", provider: "cfr", groups: ["cfr"] },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
      });
    });
    it("should not return a blocked message", async () => {
      // Block all messages except the first
      await Router.setState(() => ({
        messages: [
          { id: "foo", provider: "cfr", groups: ["cfr"] },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
        messageBlockList: ["foo"],
      }));
      await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.calledWithMatch(ASRouterTargeting.findMatchingMessage, {
        messages: [{ id: "bar", provider: "cfr", groups: ["cfr"] }],
      });
    });
    it("should not return a message from a disabled group", async () => {
      ASRouterTargeting.findMatchingMessage.callsFake(
        ({ messages }) => messages[0]
      );
      // Block all messages except the first
      await Router.setState(() => ({
        messages: [
          { id: "foo", provider: "cfr", groups: ["cfr"] },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
        groups: [{ id: "cfr", enabled: false }],
      }));
      const result = await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.isNull(result);
    });
    it("should not return a message from a blocked campaign", async () => {
      // Block all messages except the first
      await Router.setState(() => ({
        messages: [
          {
            id: "foo",
            provider: "cfr",
            campaign: "foocampaign",
            groups: ["cfr"],
          },
          { id: "bar", provider: "cfr", groups: ["cfr"] },
        ],
        messageBlockList: ["foocampaign"],
      }));

      await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.calledWithMatch(ASRouterTargeting.findMatchingMessage, {
        messages: [{ id: "bar", provider: "cfr", groups: ["cfr"] }],
      });
    });
    it("should not return a message excluded by the provider", async () => {
      // There are only two providers; block the FAKE_LOCAL_PROVIDER, leaving
      // only FAKE_REMOTE_PROVIDER unblocked, which provides only one message
      await Router.setState(() => ({
        providers: [{ id: "cfr", exclude: ["foo"] }],
      }));

      await Router.setState(() => ({
        messages: [{ id: "foo", provider: "cfr" }],
        messageBlockList: ["foocampaign"],
      }));

      const result = await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.isNull(result);
    });
    it("should not return a message if the frequency cap has been hit", async () => {
      sandbox.stub(Router, "isBelowFrequencyCaps").returns(false);
      await Router.setState(() => ({
        messages: [{ id: "foo", provider: "cfr" }],
      }));
      const result = await Router.handleMessageRequest({
        provider: "cfr",
      });
      assert.isNull(result);
    });
    it("should get unblocked messages that match the trigger", async () => {
      const message1 = {
        id: "1",
        campaign: "foocampaign",
        trigger: { id: "foo" },
        groups: ["cfr"],
        provider: "cfr",
      };
      const message2 = {
        id: "2",
        campaign: "foocampaign",
        trigger: { id: "bar" },
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({ messages: [message2, message1] });
      // Just return the first message provided as arg
      ASRouterTargeting.findMatchingMessage.callsFake(
        ({ messages }) => messages[0]
      );

      const result = Router.handleMessageRequest({ triggerId: "foo" });

      assert.deepEqual(result, message1);
    });
    it("should get unblocked messages that match trigger and template", async () => {
      const message1 = {
        id: "1",
        campaign: "foocampaign",
        template: "badge",
        trigger: { id: "foo" },
        groups: ["badge"],
        provider: "badge",
      };
      const message2 = {
        id: "2",
        campaign: "foocampaign",
        template: "test_template",
        trigger: { id: "foo" },
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({ messages: [message2, message1] });
      // Just return the first message provided as arg
      ASRouterTargeting.findMatchingMessage.callsFake(
        ({ messages }) => messages[0]
      );

      const result = Router.handleMessageRequest({
        triggerId: "foo",
        template: "badge",
      });

      assert.deepEqual(result, message1);
    });
    it("should match a message with multiple triggers against any of its trigger ids", async () => {
      const message = {
        id: "MULTI",
        campaign: "foocampaign",
        triggers: [{ id: "foo" }, { id: "bar" }],
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({ messages: [message] });
      ASRouterTargeting.findMatchingMessage.callsFake(
        ({ messages }) => messages[0] || null
      );

      assert.deepEqual(
        await Router.handleMessageRequest({ triggerId: "foo" }),
        message
      );
      assert.deepEqual(
        await Router.handleMessageRequest({ triggerId: "bar" }),
        message
      );
    });
    it("should filter out a multi-trigger message when no trigger id matches", async () => {
      const message = {
        id: "MULTI",
        campaign: "foocampaign",
        triggers: [{ id: "foo" }, { id: "bar" }],
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({ messages: [message] });
      ASRouterTargeting.findMatchingMessage.callsFake(
        ({ messages }) => messages[0] || null
      );

      const result = await Router.handleMessageRequest({ triggerId: "baz" });

      assert.isNull(result);
    });
    it("should forward trigger param info", async () => {
      const trigger = {
        triggerId: "foo",
        triggerParam: "bar",
        triggerContext: "context",
      };
      const message1 = {
        id: "1",
        campaign: "foocampaign",
        trigger: { id: "foo" },
        groups: ["cfr"],
        provider: "cfr",
      };
      const message2 = {
        id: "2",
        campaign: "foocampaign",
        trigger: { id: "bar" },
        groups: ["badge"],
        provider: "badge",
      };
      await Router.setState({ messages: [message2, message1] });
      // Just return the first message provided as arg

      Router.handleMessageRequest(trigger);

      assert.calledOnce(ASRouterTargeting.findMatchingMessage);

      const [options] = ASRouterTargeting.findMatchingMessage.firstCall.args;
      assert.propertyVal(options.trigger, "id", trigger.triggerId);
      assert.propertyVal(options.trigger, "param", trigger.triggerParam);
      assert.propertyVal(options.trigger, "context", trigger.triggerContext);
    });
    it("should not cache badge messages", async () => {
      const trigger = {
        triggerId: "bar",
        triggerParam: "bar",
        triggerContext: "context",
      };
      const message1 = {
        id: "1",
        provider: "cfr",
        campaign: "foocampaign",
        trigger: { id: "foo" },
        groups: ["cfr"],
      };
      const message2 = {
        id: "2",
        campaign: "foocampaign",
        trigger: { id: "bar" },
        groups: ["badge"],
        provider: "badge",
      };
      await Router.setState({ messages: [message2, message1] });
      // Just return the first message provided as arg

      Router.handleMessageRequest(trigger);

      assert.calledOnce(ASRouterTargeting.findMatchingMessage);

      const [options] = ASRouterTargeting.findMatchingMessage.firstCall.args;
      assert.propertyVal(options, "shouldCache", false);
    });
    it("should filter out messages without a trigger (or different) when a triggerId is defined", async () => {
      const trigger = { triggerId: "foo" };
      const message1 = {
        id: "1",
        campaign: "foocampaign",
        trigger: { id: "foo" },
        groups: ["cfr"],
        provider: "cfr",
      };
      const message2 = {
        id: "2",
        campaign: "foocampaign",
        trigger: { id: "bar" },
        groups: ["cfr"],
        provider: "cfr",
      };
      const message3 = {
        id: "3",
        campaign: "bazcampaign",
        groups: ["cfr"],
        provider: "cfr",
      };
      await Router.setState({
        messages: [message2, message1, message3],
        groups: [{ id: "cfr", enabled: true }],
      });
      // Just return the first message provided as arg
      ASRouterTargeting.findMatchingMessage.callsFake(args => args.messages);

      const result = Router.handleMessageRequest(trigger);

      assert.lengthOf(result, 1);
      assert.deepEqual(result[0], message1);
    });
    it("should filter out messages with skip_in_tests when in automation", async () => {
      await Router.setState(() => ({
        messages: [
          { id: "foo", provider: "cfr", skip_in_tests: "testing", groups: [] },
        ],
      }));
      const result = await Router.handleMessageRequest({ provider: "cfr" });
      assert.isNull(result);
    });
  });

  describe("#uninit", () => {
    it("should unregister the trigger listeners", () => {
      for (const listener of ASRouterTriggerListeners.values()) {
        sandbox.spy(listener, "uninit");
      }

      Router.uninit();

      for (const listener of ASRouterTriggerListeners.values()) {
        assert.calledOnce(listener.uninit);
      }
    });
    it("should set .dispatchCFRAction to null", () => {
      Router.uninit();
      assert.isNull(Router.dispatchCFRAction);
      assert.isNull(Router.clearChildMessages);
      assert.isNull(Router.sendTelemetry);
    });
    it("should save previousSessionEnd", () => {
      Router.uninit();

      assert.calledOnce(Router._storage.set);
      assert.calledWithExactly(
        Router._storage.set,
        "previousSessionEnd",
        sinon.match.number
      );
    });
    it("should remove the observer for `intl:app-locales-changed`", () => {
      sandbox.spy(global.Services.obs, "removeObserver");
      Router.uninit();

      assert.calledWithExactly(
        global.Services.obs.removeObserver,
        Router._onLocaleChanged,
        "intl:app-locales-changed"
      );
    });
    it("should remove the pref observer for `USE_REMOTE_L10N_PREF`", async () => {
      sandbox.spy(global.Services.prefs, "removeObserver");
      Router.uninit();

      // Grab the last call as #uninit() also involves multiple calls of `Services.prefs.removeObserver`.
      const call = global.Services.prefs.removeObserver.lastCall;
      assert.calledWithExactly(call, USE_REMOTE_L10N_PREF, Router);
    });
    it("should remove observer for multiprofile data updates", () => {
      sandbox.spy(global.Services.obs, "removeObserver");
      Router.uninit();

      assert.calledWithExactly(
        global.Services.obs.removeObserver,
        Router._updateMultiprofileData,
        "sps-profiles-updated"
      );
    });
  });

  describe("#setMessageById", async () => {
    it("should send an empty message if provided id did not resolve to a message", async () => {
      let response = await Router.setMessageById({ id: -1 }, true, {});
      assert.deepEqual(response.message, {});
    });
  });

  describe("#isUnblockedMessage", () => {
    it("should block a message if the group is blocked", async () => {
      const msg = { id: "msg1", groups: ["foo"], provider: "unit-test" };
      await Router.setState({
        groups: [{ id: "foo", enabled: false }],
        messages: [msg],
        providers: [{ id: "unit-test" }],
      });
      assert.isFalse(Router.isUnblockedMessage(msg));

      await Router.setState({ groups: [{ id: "foo", enabled: true }] });

      assert.isTrue(Router.isUnblockedMessage(msg));
    });
    it("should block a message if at least one group is blocked", async () => {
      const msg = {
        id: "msg1",
        groups: ["foo", "bar"],
        provider: "unit-test",
      };
      await Router.setState({
        groups: [
          { id: "foo", enabled: false },
          { id: "bar", enabled: false },
        ],
        messages: [msg],
        providers: [{ id: "unit-test" }],
      });
      assert.isFalse(Router.isUnblockedMessage(msg));

      await Router.setState({
        groups: [
          { id: "foo", enabled: true },
          { id: "bar", enabled: false },
        ],
      });

      assert.isFalse(Router.isUnblockedMessage(msg));
    });
    it("should not exclude a message with no provider", async () => {
      const msg = { id: "msg1", groups: [] };
      await Router.setState({ messages: [msg], providers: [] });
      assert.isTrue(Router.isUnblockedMessage(msg));
    });
  });

  describe("#hasMessageForTrigger", () => {
    const provider = { id: "unit-test" };

    it("should return true for a loaded message that matches the trigger, is unblocked, and is below its frequency cap", async () => {
      const msg = {
        id: "msg1",
        groups: [],
        provider: "unit-test",
        trigger: { id: "lastWindowClose" },
      };
      await Router.setState({ messages: [msg], providers: [provider] });

      assert.isTrue(Router.hasMessageForTrigger("lastWindowClose"));
    });

    it("should return false if no loaded message has a matching trigger", async () => {
      const msg = {
        id: "msg1",
        groups: [],
        provider: "unit-test",
        trigger: { id: "openURL" },
      };
      await Router.setState({ messages: [msg], providers: [provider] });

      assert.isFalse(Router.hasMessageForTrigger("lastWindowClose"));
    });

    it("should return false if the only matching message is blocked by its group", async () => {
      const msg = {
        id: "msg1",
        groups: ["foo"],
        provider: "unit-test",
        trigger: { id: "lastWindowClose" },
      };
      await Router.setState({
        messages: [msg],
        providers: [provider],
        groups: [{ id: "foo", enabled: false }],
      });

      assert.isFalse(Router.hasMessageForTrigger("lastWindowClose"));
    });

    it("should return false if the only matching message is over its frequency cap", async () => {
      const msg = {
        id: "msg1",
        groups: [],
        provider: "unit-test",
        trigger: { id: "lastWindowClose" },
        frequency: { lifetime: 1 },
      };
      await Router.setState(state => ({
        messages: [msg],
        providers: [provider],
        messageImpressions: {
          ...state.messageImpressions,
          msg1: [0],
        },
      }));

      assert.isFalse(Router.hasMessageForTrigger("lastWindowClose"));
    });

    it("should return true if at least one matching message is eligible, even if another matching one is blocked", async () => {
      const blockedMsg = {
        id: "blocked",
        groups: ["foo"],
        provider: "unit-test",
        trigger: { id: "lastWindowClose" },
      };
      const eligibleMsg = {
        id: "eligible",
        groups: [],
        provider: "unit-test",
        trigger: { id: "lastWindowClose" },
      };
      await Router.setState({
        messages: [blockedMsg, eligibleMsg],
        providers: [provider],
        groups: [{ id: "foo", enabled: false }],
      });

      assert.isTrue(Router.hasMessageForTrigger("lastWindowClose"));
    });
  });

  describe("#blockMessageById", () => {
    it("should add the id to the messageBlockList", async () => {
      await Router.blockMessageById("foo");
      assert.isTrue(Router.state.messageBlockList.includes("foo"));
    });
    it("should add the campaign to the messageBlockList instead of id if .campaign is specified and not select messages of that campaign again", async () => {
      await Router.setState({
        messages: [
          { id: "1", campaign: "foocampaign" },
          { id: "2", campaign: "foocampaign" },
        ],
      });
      await Router.blockMessageById("1");

      assert.isTrue(Router.state.messageBlockList.includes("foocampaign"));
      assert.isEmpty(Router.state.messages.filter(Router.isUnblockedMessage));
    });
    it("should be able to add multiple items to the messageBlockList", async () => {
      await Router.blockMessageById(FAKE_BUNDLE.map(b => b.id));
      assert.isTrue(Router.state.messageBlockList.includes(FAKE_BUNDLE[0].id));
      assert.isTrue(Router.state.messageBlockList.includes(FAKE_BUNDLE[1].id));
    });
    it("should save the messageBlockList", async () => {
      await Router.blockMessageById(FAKE_BUNDLE.map(b => b.id));
      assert.calledWithExactly(Router._storage.set, "messageBlockList", [
        FAKE_BUNDLE[0].id,
        FAKE_BUNDLE[1].id,
      ]);
    });
  });

  describe("#unblockMessageById", () => {
    it("should remove the id from the messageBlockList", async () => {
      await Router.blockMessageById("foo");
      assert.isTrue(Router.state.messageBlockList.includes("foo"));
      await Router.unblockMessageById("foo");
      assert.isFalse(Router.state.messageBlockList.includes("foo"));
    });
    it("should remove the campaign from the messageBlockList if it is defined", async () => {
      await Router.setState({ messages: [{ id: "1", campaign: "foo" }] });
      await Router.blockMessageById("1");
      assert.isTrue(
        Router.state.messageBlockList.includes("foo"),
        "blocklist has campaign id"
      );
      await Router.unblockMessageById("1");
      assert.isFalse(
        Router.state.messageBlockList.includes("foo"),
        "campaign id removed from blocklist"
      );
    });
    it("should save the messageBlockList", async () => {
      await Router.unblockMessageById("foo");
      assert.calledWithExactly(Router._storage.set, "messageBlockList", []);
    });
  });

  describe("#routeCFRMessage", () => {
    it("should allow for echoing back message modifications", () => {
      const message = { somekey: "some value" };
      const data = { content: message };
      const browser = {};
      let msg = Router.routeCFRMessage(data.content, browser, data, false);
      assert.deepEqual(msg.message, message);
    });
  });

  describe("#updateTargetingParameters", () => {
    it("should return an object containing the whole state", async () => {
      sandbox.stub(Router, "getTargetingParameters").resolves({});
      let msg = await Router.updateTargetingParameters();
      let expected = Object.assign({}, Router.state, {
        providerPrefs: ASRouterPreferences.providers,
        userPrefs: ASRouterPreferences.getAllUserPreferences(),
        targetingParameters: {},
        errors: Router.errors,
        devtoolsEnabled: ASRouterPreferences.devtoolsEnabled,
      });

      assert.deepEqual(msg, expected);
    });
  });

  describe("#reachEvent", () => {
    let experimentAPIStub;
    let featureIds = ["cfr", "moments-page", "infobar", "spotlight"];
    beforeEach(() => {
      let getAllBranchesStub = sandbox.stub();
      featureIds.forEach(feature => {
        global.NimbusFeatures[feature].getAllEnrollments.returns([
          {
            meta: {
              slug: `slug-${feature}`,
              branch: `branch-${feature}`,
              isRollout: false,
            },
            value: { id: `message-${feature}`, recordReach: true },
          },
        ]);
        getAllBranchesStub.withArgs(`slug-${feature}`).resolves([
          {
            slug: `other-branch-${feature}`,
            [feature]: { value: { trigger: "unit-test", recordReach: true } },
          },
        ]);
      });
      experimentAPIStub = {
        getAllBranches: getAllBranchesStub,
      };
      globals.set("ExperimentAPI", experimentAPIStub);
    });
    afterEach(() => {
      sandbox.restore();
    });
    it("should tag `_reachId` for all the expected message types", async () => {
      // This should match the `providers.messaging-experiments`
      let response = await MessageLoaderUtils.loadMessagesForProvider({
        type: "remote-experiments",
        featureIds,
      });

      // 1 message for reach 1 for expose
      assert.property(response, "messages");
      assert.lengthOf(response.messages, featureIds.length * 2);
      assert.lengthOf(
        response.messages.filter(m => m._reachId),
        featureIds.length
      );
    });
  });

  describe("#sendTriggerMessage", () => {
    it("should pass the trigger to ASRouterTargeting when sending trigger message", async () => {
      await Router.setState({
        messages: [
          {
            id: "foo1",
            provider: "onboarding",
            template: "onboarding",
            trigger: { id: "firstRun" },
            content: { title: "Foo1", body: "Foo123-1" },
            groups: ["onboarding"],
          },
        ],
        providers: [{ id: "onboarding" }],
      });

      Router.loadMessagesFromAllProviders.resetHistory();
      Router.loadMessagesFromAllProviders.onFirstCall().resolves();

      await Router.sendTriggerMessage({
        browser: gBrowser.selectedBrowser,
        id: "firstRun",
      });

      const [{ trigger }] =
        ASRouterTargeting.findMatchingMessage.firstCall.args;

      assert.calledOnce(ASRouterTargeting.findMatchingMessage);
      assert.strictEqual(trigger.id, "firstRun");
      assert.strictEqual(trigger.param, undefined);
      assert.isObject(trigger.context);
      assert.strictEqual(trigger.context.isAIWindow, false);
      assert.strictEqual(trigger.context.browserIsSelected, true);
      assert.strictEqual(trigger.context.onThirdPartyPage, true);
      assert.deepEqual(
        ASRouterTargeting.findMatchingMessage.firstCall.args[0].trigger,
        {
          id: "firstRun",
          param: undefined,
          context: {
            isAIWindow: false,
            browserIsSelected: true,
            onThirdPartyPage: true,
          },
        }
      );
    });
    it("should record telemetry information", async () => {
      const fakeTimerId = 42;
      const start = sandbox
        .stub(global.Glean.messagingSystem.messageRequestTime, "start")
        .returns(fakeTimerId);
      const stopAndAccumulate = sandbox.stub(
        global.Glean.messagingSystem.messageRequestTime,
        "stopAndAccumulate"
      );

      await Router.sendTriggerMessage({
        browser: {},
        id: "firstRun",
      });

      assert.calledOnce(start);
      assert.calledWithExactly(start);
      assert.calledOnce(stopAndAccumulate);
      assert.calledWithExactly(stopAndAccumulate, fakeTimerId);
    });
    it("should record the Reach event if found any", async () => {
      let messages = [
        {
          id: "foo1",
          recordReach: true,
          _reachId: "foo1",
          _nimbusFeature: "cfr",
          _nimbusSlug: "exp01",
          _branchSlug: "branch01",
          template: "simple_template",
          trigger: { id: "foo" },
          content: { title: "Foo1", body: "Foo123-1" },
        },
        {
          id: "foo2",
          template: "simple_template",
          trigger: { id: "bar" },
          content: { title: "Foo2", body: "Foo123-2" },
          provider: "onboarding",
        },
        {
          id: "foo3",
          recordReach: true,
          _reachId: "foo3",
          _nimbusFeature: "cfr",
          _nimbusSlug: "exp02",
          _branchSlug: "branch02",
          template: "simple_template",
          trigger: { id: "foo" },
          content: { title: "Foo1", body: "Foo123-1" },
        },
      ];
      sandbox.stub(Router, "handleMessageRequest").resolves(messages);
      sandbox.spy(Glean.messagingExperiments.reachCfr, "record");

      await Router.sendTriggerMessage({
        browser: {},
        id: "foo",
      });

      assert.calledTwice(Glean.messagingExperiments.reachCfr.record);
    });
    it("should not record the Reach event if it's already sent", async () => {
      let messages = [
        {
          id: "foo1",
          recordReach: true,
          _reachId: "foo1",
          _nimbusFeature: "cfr",
          _nimbusSlug: "exp01",
          _branchSlug: "branch01",
          template: "simple_template",
          trigger: { id: "foo" },
          content: { title: "Foo1", body: "Foo123-1" },
        },
      ];
      MessageLoaderUtils._recordedReachIds.add("foo1");
      sandbox.stub(Router, "handleMessageRequest").resolves(messages);
      sandbox.spy(Glean.messagingExperiments.reachCfr, "record");

      await Router.sendTriggerMessage({
        browser: {},
        id: "foo",
      });
      assert.notCalled(Glean.messagingExperiments.reachCfr.record);
    });
    // XXX this next test set (ie the single `it` that tries to generate
    // four tests with `forEach`) doesn't work, because it will always
    // pass, so don't use it as a pattern to write other tests. Bug 1967593
    it("should record the Exposure event for each valid feature", async () => {
      ["feature_callout", "update_action", "infobar", "spotlight"].forEach(
        async template => {
          let featureMap = {
            feature_callout: "cfr",
            spotlight: "spotlight",
            infobar: "infobar",
            update_action: "moments-page",
          };
          assert.notCalled(
            global.NimbusFeatures[featureMap[template]].recordExposureEvent
          );

          let messages = [
            {
              id: "foo1",
              template,
              trigger: { id: "foo" },
              content: { title: "Foo1", body: "Foo123-1" },
            },
          ];
          sandbox.stub(Router, "handleMessageRequest").resolves(messages);

          await Router.sendTriggerMessage({
            browser: {},
            id: "foo",
          });

          assert.calledOnce(
            global.NimbusFeatures[featureMap[template]].recordExposureEvent
          );
        }
      );
    });

    it("should send Exposure and route messages if recording reach fails", async () => {
      const template = "feature_callout";
      const featureId = "fxms-message-15";
      const invalidFeatureIdForReach = "FxmsMessage15";
      let messages = [
        {
          _nimbusFeature: invalidFeatureIdForReach,
          recordReach: true,
          _reachId: "foo1",
          id: "foo1",
          template,
          trigger: { id: "fakeTrigger" },
          content: { title: "Foo1", body: "Foo123-1" },
        },
        {
          _nimbusFeature: featureId,
          id: "foo2",
          template,
          trigger: { id: "fakeTrigger" },
          content: { title: "Foo2", body: "Foo123-2" },
        },
      ];
      sandbox.stub(Router, "handleMessageRequest").resolves(messages);
      sandbox.spy(Router, "routeCFRMessage");
      sandbox
        .stub(
          Glean.messagingExperiments[`reach${invalidFeatureIdForReach}`],
          "record"
        )
        .throws(new Error("stuff"));
      assert.notCalled(global.NimbusFeatures[featureId].recordExposureEvent);

      await Router.sendTriggerMessage(
        { browser: {}, id: "fakeTrigger" },
        true // skipMessagesLoaded to avoid irrelevant calls spy/stub calls
      );

      assert.calledOnce(global.NimbusFeatures[featureId].recordExposureEvent);
      assert.calledOnce(Router.routeCFRMessage);
    });
  });

  describe("forceAttribution", () => {
    let setAttributionString;
    beforeEach(() => {
      setAttributionString = sandbox.spy(Router, "setAttributionString");
      sandbox.stub(global.Services.env, "set");
    });
    afterEach(() => {
      sandbox.reset();
    });
    it("should double encode on windows", async () => {
      sandbox.stub(fakeAttributionCode, "writeAttributionFile");

      Router.forceAttribution({ foo: "FOO!", eh: "NOPE", bar: "BAR?" });

      assert.notCalled(setAttributionString);
      assert.calledWithMatch(
        fakeAttributionCode.writeAttributionFile,
        "foo%3DFOO!%26bar%3DBAR%253F"
      );
    });
    it("should set attribution string on mac", async () => {
      sandbox.stub(global.AppConstants, "platform").value("macosx");

      Router.forceAttribution({ foo: "FOO!", eh: "NOPE", bar: "BAR?" });

      assert.calledOnce(setAttributionString);
      assert.calledWithMatch(
        setAttributionString,
        "foo%3DFOO!%26bar%3DBAR%253F"
      );
    });
  });

  describe("_triggerHandler", () => {
    it("should call #sendTriggerMessage with the correct trigger", () => {
      const getter = sandbox.stub();
      getter.returns(false);
      sandbox.stub(global.BrowserHandler, "kiosk").get(getter);
      sinon.spy(Router, "sendTriggerMessage");
      const browser = {};
      const trigger = { id: "FAKE_TRIGGER", param: "some fake param" };
      Router._triggerHandler(browser, trigger);
      assert.calledOnce(Router.sendTriggerMessage);
      assert.calledWith(
        Router.sendTriggerMessage,
        sandbox.match({
          id: "FAKE_TRIGGER",
          param: "some fake param",
        })
      );
    });
  });

  describe("_triggerHandler_kiosk", () => {
    it("should not call #sendTriggerMessage", () => {
      const getter = sandbox.stub();
      getter.returns(true);
      sandbox.stub(global.BrowserHandler, "kiosk").get(getter);
      sinon.spy(Router, "sendTriggerMessage");
      const browser = {};
      const trigger = { id: "FAKE_TRIGGER", param: "some fake param" };
      Router._triggerHandler(browser, trigger);
      assert.notCalled(Router.sendTriggerMessage);
    });
  });

  describe("valid preview endpoint", () => {
    it("should report an error if url protocol is not https", () => {
      sandbox.stub(console, "error");

      assert.equal(false, Router._validPreviewEndpoint("http://foo.com"));
      assert.calledTwice(console.error);
    });
  });

  describe("impressions", () => {
    describe("#addImpression for groups", () => {
      it("should save an impression in each group-with-frequency in a message", async () => {
        const fooMessageImpressions = [0];
        const aGroupImpressions = [0, 1, 2];
        const bGroupImpressions = [3, 4, 5];
        const cGroupImpressions = [6, 7, 8];

        const message = {
          id: "foo",
          provider: "bar",
          groups: ["a", "b", "c"],
        };
        const groups = [
          { id: "a", frequency: { lifetime: 3 } },
          { id: "b", frequency: { lifetime: 4 } },
          { id: "c", frequency: { lifetime: 5 } },
        ];
        await Router.setState(state => {
          // Add provider
          const providers = [...state.providers];
          // Add fooMessageImpressions
          // eslint-disable-next-line no-shadow
          const messageImpressions = Object.assign(
            {},
            state.messageImpressions
          );
          let gImpressions = {};
          gImpressions.a = aGroupImpressions;
          gImpressions.b = bGroupImpressions;
          gImpressions.c = cGroupImpressions;
          messageImpressions.foo = fooMessageImpressions;
          return {
            providers,
            messageImpressions,
            groups,
            groupImpressions: gImpressions,
          };
        });

        await Router.addImpression(message);

        assert.deepEqual(
          Router.state.groupImpressions.a,
          [0, 1, 2, 0],
          "a impressions"
        );
        assert.deepEqual(
          Router.state.groupImpressions.b,
          [3, 4, 5, 0],
          "b impressions"
        );
        assert.deepEqual(
          Router.state.groupImpressions.c,
          [6, 7, 8, 0],
          "c impressions"
        );
      });
    });

    describe("#isBelowFrequencyCaps", () => {
      it("should call #_isBelowItemFrequencyCap for the message and for the provider with the correct impressions and arguments", async () => {
        sinon.spy(Router, "_isBelowItemFrequencyCap");

        const MAX_MESSAGE_LIFETIME_CAP = 100; // Defined in ASRouter
        const fooMessageImpressions = [0, 1];
        const barGroupImpressions = [0, 1, 2];

        const message = {
          id: "foo",
          provider: "bar",
          groups: ["bar"],
          frequency: { lifetime: 3 },
        };
        const groups = [{ id: "bar", frequency: { lifetime: 5 } }];

        await Router.setState(state => {
          // Add provider
          const providers = [...state.providers];
          // Add fooMessageImpressions
          // eslint-disable-next-line no-shadow
          const messageImpressions = Object.assign(
            {},
            state.messageImpressions
          );
          let gImpressions = {};
          gImpressions.bar = barGroupImpressions;
          messageImpressions.foo = fooMessageImpressions;
          return {
            providers,
            messageImpressions,
            groups,
            groupImpressions: gImpressions,
          };
        });

        await Router.isBelowFrequencyCaps(message);

        assert.calledTwice(Router._isBelowItemFrequencyCap);
        assert.calledWithExactly(
          Router._isBelowItemFrequencyCap,
          message,
          fooMessageImpressions,
          MAX_MESSAGE_LIFETIME_CAP
        );
        assert.calledWithExactly(
          Router._isBelowItemFrequencyCap,
          groups[0],
          barGroupImpressions
        );
      });
    });

    describe("#_isBelowItemFrequencyCap", () => {
      it("should return false if the # of impressions exceeds the maxLifetimeCap", () => {
        const item = { id: "foo", frequency: { lifetime: 5 } };
        const impressions = [0, 1];
        const maxLifetimeCap = 1;
        const result = Router._isBelowItemFrequencyCap(
          item,
          impressions,
          maxLifetimeCap
        );
        assert.isFalse(result);
      });

      describe("lifetime frequency caps", () => {
        it("should return true if .frequency is not defined on the item", () => {
          const item = { id: "foo" };
          const impressions = [0, 1];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isTrue(result);
        });
        it("should return true if there are no impressions", () => {
          const item = {
            id: "foo",
            frequency: {
              lifetime: 10,
              custom: [{ period: ONE_DAY_IN_MS, cap: 2 }],
            },
          };
          const impressions = [];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isTrue(result);
        });
        it("should return true if the # of impressions is less than .frequency.lifetime of the item", () => {
          const item = { id: "foo", frequency: { lifetime: 3 } };
          const impressions = [0, 1];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isTrue(result);
        });
        it("should return false if the # of impressions is equal to .frequency.lifetime of the item", async () => {
          const item = { id: "foo", frequency: { lifetime: 3 } };
          const impressions = [0, 1, 2];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isFalse(result);
        });
        it("should return false if the # of impressions is greater than .frequency.lifetime of the item", async () => {
          const item = { id: "foo", frequency: { lifetime: 3 } };
          const impressions = [0, 1, 2, 3];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isFalse(result);
        });
      });

      describe("custom frequency caps", () => {
        it("should return true if impressions in the time period < the cap and total impressions < the lifetime cap", () => {
          clock.tick(ONE_DAY_IN_MS + 10);
          const item = {
            id: "foo",
            frequency: {
              custom: [{ period: ONE_DAY_IN_MS, cap: 2 }],
              lifetime: 3,
            },
          };
          const impressions = [0, ONE_DAY_IN_MS + 1];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isTrue(result);
        });
        it("should return false if impressions in the time period > the cap and total impressions < the lifetime cap", () => {
          clock.tick(200);
          const item = {
            id: "msg1",
            frequency: { custom: [{ period: 100, cap: 2 }], lifetime: 3 },
          };
          const impressions = [0, 160, 161];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isFalse(result);
        });
        it("should return false if impressions in one of the time periods > the cap and total impressions < the lifetime cap", () => {
          clock.tick(ONE_DAY_IN_MS + 200);
          const itemTrue = {
            id: "msg2",
            frequency: { custom: [{ period: 100, cap: 2 }] },
          };
          const itemFalse = {
            id: "msg1",
            frequency: {
              custom: [
                { period: 100, cap: 2 },
                { period: ONE_DAY_IN_MS, cap: 3 },
              ],
            },
          };
          const impressions = [
            0,
            ONE_DAY_IN_MS + 160,
            ONE_DAY_IN_MS - 100,
            ONE_DAY_IN_MS - 200,
          ];
          assert.isTrue(Router._isBelowItemFrequencyCap(itemTrue, impressions));
          assert.isFalse(
            Router._isBelowItemFrequencyCap(itemFalse, impressions)
          );
        });
        it("should return false if impressions in the time period < the cap and total impressions > the lifetime cap", () => {
          clock.tick(ONE_DAY_IN_MS + 10);
          const item = {
            id: "msg1",
            frequency: {
              custom: [{ period: ONE_DAY_IN_MS, cap: 2 }],
              lifetime: 3,
            },
          };
          const impressions = [0, 1, 2, 3, ONE_DAY_IN_MS + 1];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isFalse(result);
        });
        it("should return true if daily impressions < the daily cap and there is no lifetime cap", () => {
          clock.tick(ONE_DAY_IN_MS + 10);
          const item = {
            id: "msg1",
            frequency: { custom: [{ period: ONE_DAY_IN_MS, cap: 2 }] },
          };
          const impressions = [0, 1, 2, 3, ONE_DAY_IN_MS + 1];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isTrue(result);
        });
        it("should return false if daily impressions > the daily cap and there is no lifetime cap", () => {
          clock.tick(ONE_DAY_IN_MS + 10);
          const item = {
            id: "msg1",
            frequency: { custom: [{ period: ONE_DAY_IN_MS, cap: 2 }] },
          };
          const impressions = [
            0,
            1,
            2,
            3,
            ONE_DAY_IN_MS + 1,
            ONE_DAY_IN_MS + 2,
            ONE_DAY_IN_MS + 3,
          ];
          const result = Router._isBelowItemFrequencyCap(item, impressions);
          assert.isFalse(result);
        });
      });
    });

    describe("#getLongestPeriod", () => {
      it("should return the period if there is only one definition", () => {
        const message = {
          id: "foo",
          frequency: { custom: [{ period: 200, cap: 2 }] },
        };
        assert.equal(Router.getLongestPeriod(message), 200);
      });
      it("should return the longest period if there are more than one definitions", () => {
        const message = {
          id: "foo",
          frequency: {
            custom: [
              { period: 1000, cap: 3 },
              { period: ONE_DAY_IN_MS, cap: 5 },
              { period: 100, cap: 2 },
            ],
          },
        };
        assert.equal(Router.getLongestPeriod(message), ONE_DAY_IN_MS);
      });
      it("should return null if there are is no .frequency", () => {
        const message = { id: "foo" };
        assert.isNull(Router.getLongestPeriod(message));
      });
      it("should return null if there are is no .frequency.custom", () => {
        const message = { id: "foo", frequency: { lifetime: 10 } };
        assert.isNull(Router.getLongestPeriod(message));
      });
    });

    describe("cleanup on init", () => {
      it("should clear old messageImpressions for missing messages", async () => {
        const messages = [
          {
            id: "recentImpressionForActiveMessage",
            frequency: { lifetime: 10 },
          },
          { id: "oldImpressionForActiveMessage", frequency: { lifetime: 10 } },
        ];
        let now = Date.now();
        let old = now - SIX_MONTHS_IN_MS - 1;
        messageImpressions = {
          recentImpressionForActiveMessage: [now],
          oldImpressionForActiveMessage: [old],
          recentImpressionForInactiveMessage: [now],
          oldImpressionForInactiveMessage: [old],
          mixedImpressionsForInactiveMessage: [now, old],
        };
        // Impressions older than six months for nonexistent messages should be
        // cleared. If that results in an empty impressions array, the key
        // should be removed altogether.
        const expectedResult = {
          recentImpressionForActiveMessage: [now],
          oldImpressionForActiveMessage: [old],
          recentImpressionForInactiveMessage: [now],
          mixedImpressionsForInactiveMessage: [now],
        };

        await createRouterAndInit([
          { id: "onboarding", type: "local", messages, enabled: true },
        ]);
        assert.calledWith(
          Router._storage.set,
          "messageImpressions",
          expectedResult
        );
        assert.deepEqual(Router.state.messageImpressions, expectedResult);
      });
      it("should clear messageImpressions if they are not properly formatted", async () => {
        const messages = [{ id: "foo", frequency: { lifetime: 10 } }];
        // this is impromperly formatted since messageImpressions are supposed to be an array
        messageImpressions = { foo: 0, bar: undefined, baz: [] };
        const result = {};

        await createRouterAndInit([
          { id: "onboarding", type: "local", messages, enabled: true },
        ]);
        assert.calledWith(Router._storage.set, "messageImpressions", result);
        assert.deepEqual(Router.state.messageImpressions, result);
      });
      it("should clear groupImpressions older than the longest period if no lifetime impression cap is included", async () => {
        const CURRENT_TIME = ONE_DAY_IN_MS * 2;
        clock.tick(CURRENT_TIME);
        const groups = [
          {
            id: "foo",
            frequency: {
              custom: [
                { period: ONE_DAY_IN_MS, cap: 5 },
                { period: 100, cap: 2 },
              ],
            },
          },
        ];
        groupImpressions = { foo: [0, 1, CURRENT_TIME - 10] };
        // Only 0 and 1 are more than 24 hours before CURRENT_TIME
        const result = { foo: [CURRENT_TIME - 10] };

        Router = new _ASRouter();
        await initASRouter(Router);
        await Router.setState({
          groups,
          groupImpressions,
        });
        Router.cleanupImpressions();
        assert.calledWith(Router._storage.set, "groupImpressions", result);
        assert.deepEqual(Router.state.groupImpressions, result);
      });
    });
  });

  describe("#_onLocaleChanged", () => {
    it("should update the l10n attachment if the locale was changed", async () => {
      const getter = sandbox.stub();
      getter.onFirstCall().returns("en-US");
      getter.onSecondCall().returns("fr");
      sandbox.stub(global.Services.locale, "appLocaleAsBCP47").get(getter);
      const provider = {
        id: "cfr",
        enabled: true,
        type: "remote-settings",
        collection: "cfr",
      };
      await createRouterAndInit([provider]);
      sandbox.spy(Router, "setState");
      Router.loadMessagesFromAllProviders.resetHistory();

      await Router._onLocaleChanged();

      assert.calledWith(Router.setState, {
        localeInUse: "fr",
        providers: [
          {
            id: "cfr",
            enabled: true,
            type: "remote-settings",
            collection: "cfr",
            lastUpdated: undefined,
            errors: [],
          },
        ],
      });
      assert.calledOnce(Router.loadMessagesFromAllProviders);
    });
    it("should not update the l10n attachment if the provider doesn't need l10n attachment", async () => {
      const getter = sandbox.stub();
      getter.onFirstCall().returns("en-US");
      getter.onSecondCall().returns("fr");
      sandbox.stub(global.Services.locale, "appLocaleAsBCP47").get(getter);
      const provider = {
        id: "localProvider",
        enabled: true,
        type: "local",
      };
      await createRouterAndInit([provider]);
      Router.loadMessagesFromAllProviders.resetHistory();
      sandbox.spy(Router, "setState");

      await Router._onLocaleChanged();

      assert.notCalled(Router.setState);
      assert.notCalled(Router.loadMessagesFromAllProviders);
    });
  });
  describe("#observe", () => {
    let reloadL10nStub;
    beforeEach(() => {
      reloadL10nStub = sandbox.stub();
      globals.set("RemoteL10n", { reloadL10n: reloadL10nStub });
    });
    it("should reload l10n when the `USE_REMOTE_L10N_PREF` pref is changed", () => {
      Router.observe("", "", USE_REMOTE_L10N_PREF);

      assert.calledOnce(reloadL10nStub);
    });
    it("should not react to other pref changes", () => {
      Router.observe("", "", "foo");

      assert.notCalled(reloadL10nStub);
    });
  });
  describe("#loadAllMessageGroups", () => {
    it("should disable the group if the pref is false", async () => {
      sandbox.stub(ASRouterPreferences, "getUserPreference").returns(false);
      sandbox.stub(MessageLoaderUtils, "_getRemoteSettingsMessages").resolves([
        {
          id: "provider-group",
          enabled: true,
          type: "remote",
          userPreferences: ["cfrAddons"],
        },
      ]);
      await Router.setState({
        providers: [
          {
            id: "message-groups",
            enabled: true,
            collection: "collection",
            type: "remote-settings",
          },
        ],
      });

      await Router.loadAllMessageGroups();

      const group = Router.state.groups.find(g => g.id === "provider-group");

      assert.ok(group);
      assert.propertyVal(group, "enabled", false);
    });
    it("should enable the group if at least one pref is true", async () => {
      sandbox
        .stub(ASRouterPreferences, "getUserPreference")
        .withArgs("cfrAddons")
        .returns(false)
        .withArgs("cfrFeatures")
        .returns(true);
      sandbox.stub(MessageLoaderUtils, "_getRemoteSettingsMessages").resolves([
        {
          id: "provider-group",
          enabled: true,
          type: "remote",
          userPreferences: ["cfrAddons", "cfrFeatures"],
        },
      ]);
      await Router.setState({
        providers: [
          {
            id: "message-groups",
            enabled: true,
            collection: "collection",
            type: "remote-settings",
          },
        ],
      });

      await Router.loadAllMessageGroups();

      const group = Router.state.groups.find(g => g.id === "provider-group");

      assert.ok(group);
      assert.propertyVal(group, "enabled", true);
    });
    it("should be keep the group disabled if disabled is true", async () => {
      sandbox.stub(ASRouterPreferences, "getUserPreference").returns(true);
      sandbox.stub(MessageLoaderUtils, "_getRemoteSettingsMessages").resolves([
        {
          id: "provider-group",
          enabled: false,
          type: "remote",
          userPreferences: ["cfrAddons"],
        },
      ]);
      await Router.setState({
        providers: [
          {
            id: "message-groups",
            enabled: true,
            collection: "collection",
            type: "remote-settings",
          },
        ],
      });

      await Router.loadAllMessageGroups();

      const group = Router.state.groups.find(g => g.id === "provider-group");

      assert.ok(group);
      assert.propertyVal(group, "enabled", false);
    });
    it("should keep local groups unchanged if provider doesn't require an update", async () => {
      sandbox.stub(MessageLoaderUtils, "shouldProviderUpdate").returns(false);
      sandbox.stub(MessageLoaderUtils, "_loadDataForProvider");
      await Router.setState({
        groups: [
          {
            id: "cfr",
            enabled: true,
            collection: "collection",
            type: "remote-settings",
          },
        ],
      });

      await Router.loadAllMessageGroups();

      const group = Router.state.groups.find(g => g.id === "cfr");

      assert.ok(group);
      assert.propertyVal(group, "enabled", true);
      // Because it should not have updated
      assert.notCalled(MessageLoaderUtils._loadDataForProvider);
    });
    it("should update local groups on pref change (no RS update)", async () => {
      sandbox.stub(MessageLoaderUtils, "shouldProviderUpdate").returns(false);
      sandbox.stub(ASRouterPreferences, "getUserPreference").returns(false);
      await Router.setState({
        groups: [
          {
            id: "cfr",
            enabled: true,
            collection: "collection",
            type: "remote-settings",
            userPreferences: ["cfrAddons"],
          },
        ],
      });

      await Router.loadAllMessageGroups();

      const group = Router.state.groups.find(g => g.id === "cfr");

      assert.ok(group);
      // Pref changed, updated the group state
      assert.propertyVal(group, "enabled", false);
    });
  });
  describe("unblockAll", () => {
    it("Clears the message block list and returns the state value", async () => {
      await Router.setState({ messageBlockList: ["one", "two", "three"] });
      assert.equal(Router.state.messageBlockList.length, 3);
      const state = await Router.unblockAll();
      assert.equal(Router.state.messageBlockList.length, 0);
      assert.equal(state.messageBlockList.length, 0);
    });
  });
  describe("#loadMessagesForProvider", () => {
    it("should fetch messages from the ExperimentAPI", async () => {
      const args = {
        type: "remote-experiments",
        featureIds: ["spotlight"],
      };

      await MessageLoaderUtils.loadMessagesForProvider(args);

      assert.calledOnce(global.NimbusFeatures.spotlight.getAllEnrollments);
    });
    it("should handle the case of no experiments in the ExperimentAPI", async () => {
      const args = {
        type: "remote-experiments",
        featureIds: ["infobar"],
      };

      const result = await MessageLoaderUtils.loadMessagesForProvider(args);

      assert.lengthOf(result.messages, 0);
    });
    it("should normally load ExperimentAPI messages", async () => {
      const args = {
        type: "remote-experiments",
        featureIds: ["infobar"],
      };
      const enrollment = {
        slug: "enrollment01",
        branch: {
          slug: "branch01",
          infobar: {
            featureId: "infobar",
            value: { id: "id01", trigger: { id: "openURL" } },
          },
        },
      };

      global.NimbusFeatures.infobar.getAllEnrollments.returns([
        {
          meta: {
            slug: enrollment.slug,
            branch: enrollment.branch.slug,
            isRollout: false,
          },
          value: enrollment.branch.infobar.value,
        },
      ]);
      global.ExperimentAPI.getAllBranches.returns([
        enrollment.branch,
        {
          slug: "control",
          infobar: {
            featureId: "infobar",
            value: null,
          },
        },
      ]);

      const result = await MessageLoaderUtils.loadMessagesForProvider(args);

      assert.lengthOf(result.messages, 1);
    });
    it("should load branches for reach", async () => {
      const args = {
        type: "remote-experiments",
        featureIds: ["cfr"],
      };
      const enrollment = {
        slug: "exp01",
        branch: {
          slug: "branch01",
          cfr: {
            featureId: "cfr",
            value: { id: "id01", trigger: { id: "openURL" } },
          },
        },
      };

      global.NimbusFeatures.cfr.getAllEnrollments.returns([
        {
          meta: {
            slug: enrollment.slug,
            branch: enrollment.branch.slug,
            isRollout: false,
          },
          value: enrollment.branch.cfr.value,
        },
      ]);
      global.ExperimentAPI.getAllBranches.resolves([
        enrollment.branch,
        {
          slug: "branch02",
          cfr: {
            featureId: "cfr",
            value: {
              id: "id02",
              trigger: { id: "openURL" },
              recordReach: true,
            },
          },
        },
        {
          // This branch should not be loaded as it doesn't have a trigger
          slug: "branch03",
          cfr: {
            featureId: "cfr",
            value: { id: "id03", recordReach: true },
          },
        },
        {
          // This branch should not be loaded as it doesn't have recordReach
          slug: "branch03",
          cfr: {
            featureId: "cfr",
            value: { id: "id04" },
          },
        },
      ]);

      const result = await MessageLoaderUtils.loadMessagesForProvider(args);

      assert.equal(result.messages.length, 2);
      assert.equal(result.messages[0].id, "id01");
      assert.equal(result.messages[1].id, "id02");
      assert.equal(result.messages[1]._nimbusSlug, "exp01");
      assert.equal(result.messages[1]._branchSlug, "branch02");
      assert.equal(result.messages[1]._nimbusFeature, "cfr");
    });
  });
  describe("#_remoteSettingsLoader", () => {
    let provider;
    let downloadSpy;
    beforeEach(() => {
      provider = {
        id: "cfr",
        collection: "cfr",
      };
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsMessages")
        .resolves([{ id: "message_1" }]);
      sandbox
        .stub(MessageLoaderUtils, "_getRemoteSettingsLanguagePackRecord")
        .resolves({ attachment: { size: 42 } });
      sandbox.stub(global.IOUtils, "exists").resolves(false);
      downloadSpy = sandbox.spy(global.UnstoredDownloader.prototype.download);
      global.UnstoredDownloader.prototype.download = downloadSpy;
    });
    it("should be called with the expected dir path", async () => {
      const writeSpy = sandbox.spy(global.IOUtils, "write");
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.calledOnce(downloadSpy);
      assert.calledWithMatch(
        writeSpy,
        "asrouter.ftl", // PathUtils.join() is mocked in `unit-entry.js` and only returns the filename.
        sinon.match.any,
        { tmpPath: "asrouter.ftl.tmp" }
      );
    });
    it("should download if local file has different size", async () => {
      global.IOUtils.exists.resolves(true);
      sandbox.stub(global.IOUtils, "stat").resolves({ size: 1337 });
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.calledOnce(downloadSpy);
    });
    it("should not download if local file has same size", async () => {
      global.IOUtils.exists.resolves(true);
      sandbox.stub(global.IOUtils, "stat").resolves({ size: 42 });
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.notCalled(downloadSpy);
    });
    it("should allow fetch for known locales", async () => {
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "en-US");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.calledOnce(downloadSpy);
    });
    it("should fallback to 'en-US' for locale 'und' ", async () => {
      sandbox.stub(global.Services.locale, "appLocaleAsBCP47").get(() => "und");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.calledWithMatch(
        MessageLoaderUtils._getRemoteSettingsLanguagePackRecord,
        sinon.match("en-US")
      );
      assert.calledOnce(downloadSpy);
    });
    it("should fallback to 'ja-JP-mac' for locale 'ja-JP-macos'", async () => {
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "ja-JP-macos");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.calledWithMatch(
        MessageLoaderUtils._getRemoteSettingsLanguagePackRecord,
        sinon.match("ja-JP-mac")
      );
      assert.calledOnce(downloadSpy);
    });
    it("should not allow fetch for unsupported locales", async () => {
      sandbox
        .stub(global.Services.locale, "appLocaleAsBCP47")
        .get(() => "unkown");

      await MessageLoaderUtils._remoteSettingsLoader(provider, {});

      assert.notCalled(downloadSpy);
    });
  });
  describe("#resetMessageState", () => {
    it("should reset all message impressions", async () => {
      await Router.setState({
        messages: [{ id: "1" }, { id: "2" }],
      });
      await Router.setState({
        messageImpressions: { 1: [0, 1, 2], 2: [0, 1, 2] },
      });
      assert.equal(
        Object.values(Router.state.messageImpressions).filter(i => i.length)
          .length,
        2
      );

      Router.resetMessageState();
      assert.isEmpty(
        Object.values(Router.state.messageImpressions).filter(i => i.length)
      );
    });
  });
  describe("#resetGroupsState", () => {
    it("should reset all group impressions", async () => {
      await Router.setState({
        groups: [{ id: "1" }, { id: "2" }],
      });
      await Router.setState({
        groupImpressions: { 1: [0, 1, 2], 2: [0, 1, 2] },
      });
      assert.equal(
        Object.values(Router.state.groupImpressions).filter(i => i.length)
          .length,
        2
      );

      Router.resetGroupsState();
      assert.isEmpty(
        Object.values(Router.state.groupImpressions).filter(i => i.length)
      );
    });
  });
  describe("#resetScreenImpressions", () => {
    it("should reset all screen impressions", async () => {
      await Router.setState({ screenImpressions: { 1: 1, 2: 2 } });
      assert.equal(
        Object.values(Router.state.screenImpressions).filter(i => i).length,
        2
      );

      Router.resetScreenImpressions();
      assert.isEmpty(
        Object.values(Router.state.screenImpressions).filter(i => i)
      );
    });
  });
  describe("#editState", () => {
    it("should update message impressions", async () => {
      sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);
      await Router.setState({ messages: [{ id: "1" }, { id: "2" }] });
      await Router.setState({
        messageImpressions: { 1: [0, 1, 2], 2: [0, 1, 2] },
      });
      assert.equal(
        Object.values(Router.state.messageImpressions).filter(i => i.length)
          .length,
        2
      );

      Router.editState("messageImpressions", {
        1: [],
        2: [],
        3: [0, 1, 2],
      });

      // The original messages now have zero impressions
      assert.isEmpty(Router.state.messageImpressions["1"]);
      assert.isEmpty(Router.state.messageImpressions["2"]);
      // A new impression array was added for the new message
      assert.equal(Router.state.messageImpressions["3"].length, 3);
      assert.calledWithExactly(Router._storage.set, "messageImpressions", {
        1: [],
        2: [],
        3: [0, 1, 2],
      });
    });
    it("should update multiprofile message impressions in shared storage", async () => {
      sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);
      await Router.setState({
        multiProfileMessageImpressions: { 1: [0, 1, 2], 2: [0, 1, 2] },
      });

      await Router.editState("multiProfileMessageImpressions", {
        1: [3, 4],
        3: [0, 1, 2],
      });

      // State reflects the edited value
      assert.deepEqual(Router.state.multiProfileMessageImpressions, {
        1: [3, 4],
        3: [0, 1, 2],
      });
      // Added/changed entries are persisted to shared storage
      assert.calledWithExactly(
        Router._storage.setSharedMessageImpressions,
        "1",
        [3, 4]
      );
      assert.calledWithExactly(
        Router._storage.setSharedMessageImpressions,
        "3",
        [0, 1, 2]
      );
      // Removed entry is deleted from shared storage
      assert.calledWithExactly(
        Router._storage.setSharedMessageImpressions,
        "2",
        undefined
      );
      // Regular storage is not used for multiprofile impressions
      assert.neverCalledWith(
        Router._storage.set,
        "multiProfileMessageImpressions"
      );
    });
    it("should throw for invalid state key", async () => {
      sandbox.stub(ASRouterPreferences, "devtoolsEnabled").get(() => true);
      let error;
      try {
        await Router.editState("notARealKey", {});
      } catch (e) {
        error = e;
      }
      assert.instanceOf(error, Error);
      assert.equal(error.message, "Invalid state key");
    });
  });

  describe("multiprofile messages", () => {
    describe("#_updateMultiprofileData", () => {
      it("should update multiprofile data state from storage with event source remote", async () => {
        const testImpressions = { test_msg: [111, 222] };
        const testBlocklist = ["blocked_msg"];

        Router._storage.getSharedMessageImpressions = sandbox
          .stub()
          .resolves(testImpressions);
        Router._storage.getSharedMessageBlocklist = sandbox
          .stub()
          .resolves(testBlocklist);

        await Router._updateMultiprofileData(
          null,
          "sps-profiles-updated",
          "remote"
        );

        assert.calledOnce(Router._storage.getSharedMessageImpressions);
        assert.calledOnce(Router._storage.getSharedMessageBlocklist);
        assert.deepEqual(
          Router.state.multiProfileMessageImpressions,
          testImpressions
        );
        assert.deepEqual(
          Router.state.multiProfileMessageBlocklist,
          testBlocklist
        );
      });
      it("should not update multiprofile data from storage with event source local", async () => {
        const testImpressions = { test_msg: [111, 222] };
        const testBlocklist = ["blocked_msg"];

        await Router.setState(() => {
          return {
            multiProfileMessageBlocklist: testBlocklist,
            multiProfileMessageImpressions: testImpressions,
          };
        });

        Router._storage.getSharedMessageImpressions = sandbox.stub();
        Router._storage.getSharedMessageBlocklist = sandbox.stub();

        await Router._updateMultiprofileData(
          null,
          "sps-profiles-updated",
          "local"
        );

        assert.notCalled(Router._storage.getSharedMessageImpressions);
        assert.notCalled(Router._storage.getSharedMessageBlocklist);
        assert.deepEqual(
          Router.state.multiProfileMessageImpressions,
          testImpressions
        );
        assert.deepEqual(
          Router.state.multiProfileMessageBlocklist,
          testBlocklist
        );
      });
    });

    describe("multiprofile #addImpression", () => {
      describe("addImpression when multiprofile is enabled", () => {
        beforeEach(() => {
          sandbox
            .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
            .value(true);
        });
        it("should add impression data when profileScope is set", async () => {
          const message = {
            id: "foo",
            provider: "bar",
            frequency: { lifetime: 3 },
            profileScope: "single",
          };
          await Router.addImpression(message);
          assert.deepEqual(
            Router.state.multiProfileMessageImpressions.foo,
            [0],
            "foo message shared multiprofile impressions"
          );
          assert.deepEqual(
            Router.state.messageImpressions.foo,
            [0],
            "foo message impressions"
          );
        });
        it("should not add profileImpressions when profileScope is not set", async () => {
          const message = {
            id: "foo",
            provider: "bar",
            frequency: { lifetime: 3 },
          };
          await Router.addImpression(message);
          assert.deepEqual(
            Router.state.multiProfileMessageImpressions.foo,
            undefined,
            "foo message shared multiprofile impressions"
          );
          assert.deepEqual(
            Router.state.messageImpressions.foo,
            [0],
            "foo message impressions"
          );
        });
      });
      describe("addImpression when multiprofile is not enabled", () => {
        it("should not add shared multiprofile impression even when profileScope is set", async () => {
          sandbox
            .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
            .value(false);

          const message = {
            id: "foo",
            provider: "bar",
            frequency: { lifetime: 3 },
            profileScope: "single",
          };
          await Router.addImpression(message);
          assert.deepEqual(
            Router.state.multiProfileMessageImpressions.foo,
            undefined,
            "foo message shared multiprofile impressions"
          );
          assert.deepEqual(
            Router.state.messageImpressions.foo,
            [0],
            "foo message impressions"
          );
        });
      });
    });

    describe("multiprofile #cleanupImpressions", () => {
      beforeEach(() => {
        Router._storage.setSharedMessageImpressions = sandbox.stub();
        sandbox
          .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
          .value(true);
      });
      it("should remove impressions from shared multiprofile impressions if the message is not in state & is older than six months", async () => {
        await Router.setState(() => ({
          multiProfileMessageImpressions: {
            deadMessage: [Date.now() - SIX_MONTHS_IN_MS - 1, Date.now()],
          },
          messageImpressions: {
            deadMessage: [Date.now() - SIX_MONTHS_IN_MS - 1, Date.now()],
          },
        }));

        Router.cleanupImpressions();

        assert.property(
          Router.state.multiProfileMessageImpressions,
          "deadMessage"
        );
        assert.lengthOf(
          Router.state.multiProfileMessageImpressions.deadMessage,
          1
        );
      });
    });

    describe("multiprofile #hasValidProfileScope", () => {
      it("should not filter messages when profile scope not set", async () => {
        const message1 = {
          id: "foo",
          provider: "cfr",
          groups: [],
        };
        const result = await Router.hasValidProfileScope(message1);
        assert.isTrue(result);
      });
      it("should not filter when profile scope set and has both message and shared profile impression", async () => {
        const message1 = {
          id: "foo",
          provider: "cfr",
          profileScope: "single",
          groups: [],
        };
        await Router.setState(() => ({
          messages: [message1],
          multiProfileMessageImpressions: { foo: [111, 222] },
          messageImpressions: { foo: [111, 222] },
        }));

        const result = await Router.hasValidProfileScope(message1);
        assert.isTrue(result);
      });
      it("should filter when profile scope set and has just shared profile impression", async () => {
        const message1 = {
          id: "foo",
          provider: "cfr",
          profileScope: "single",
          groups: [],
        };
        await Router.setState(() => ({
          messages: [message1],
          multiProfileMessageImpressions: { foo: [111, 222] },
        }));

        const result = await Router.hasValidProfileScope(message1);
        assert.isFalse(result);
      });
    });

    describe("multiprofile #blockMessageById", () => {
      beforeEach(() => {
        sandbox
          .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
          .value(true);
      });

      it("should add the id to the shared messageBlockList if the profile scope is single", async () => {
        await Router.setState({
          messages: [
            { id: "foo", provider: "cfr", profileScope: "single", groups: [] },
          ],
        });

        await Router.blockMessageById("foo");
        assert.isTrue(Router.state.messageBlockList.includes("foo"));
        assert.isTrue(
          Router.state.multiProfileMessageBlocklist.includes("foo")
        );
        assert.calledOnce(Router._storage.setSharedMessageBlocked);
      });

      it("should not add the id to the shared messageBlockList if there is no profile scope", async () => {
        await Router.setState({
          messages: [{ id: "bar", provider: "cfr", groups: [] }],
        });

        await Router.blockMessageById("bar");
        assert.isTrue(Router.state.messageBlockList.includes("bar"));
        assert.isFalse(
          Router.state.multiProfileMessageBlocklist.includes("bar")
        );
        assert.notCalled(Router._storage.setSharedMessageBlocked);
      });
    });

    describe("multiprofile #unblockMessageById", () => {
      beforeEach(() => {
        sandbox
          .stub(ASRouterTargeting.Environment, "canCreateSelectableProfiles")
          .value(true);
      });

      it("should remove the id from the messageBlockList", async () => {
        await Router.setState({
          messages: [
            { id: "foo", provider: "cfr", profileScope: "single", groups: [] },
          ],
        });
        await Router.blockMessageById("foo");
        assert.isTrue(Router.state.messageBlockList.includes("foo"));
        assert.isTrue(
          Router.state.multiProfileMessageBlocklist.includes("foo")
        );
        assert.calledWithExactly(
          Router._storage.setSharedMessageBlocked,
          "foo"
        );

        await Router.unblockMessageById("foo");
        assert.isFalse(Router.state.messageBlockList.includes("foo"));
        assert.isFalse(
          Router.state.multiProfileMessageBlocklist.includes("foo")
        );
        // multiprofile uses the same function for block and unblock
        assert.calledWithExactly(
          Router._storage.setSharedMessageBlocked,
          "foo",
          false
        );
      });
    });

    describe("multiprofile #handleMessageRequest", () => {
      beforeEach(async () => {
        await Router.setState(() => ({
          providers: [{ id: "cfr" }],
        }));

        sandbox.stub(Router, "shouldShowMessagesToProfile").returns(true);
      });
      it("should hide message when not a valid multi profile scope", async () => {
        await Router.setState(() => ({
          messages: [
            { id: "foo", provider: "cfr", profileScope: "single", groups: [] },
          ],
          multiProfileMessageImpressions: { foo: [111, 222] },
          messageImpressions: {},
        }));
        const result = await Router.handleMessageRequest({ provider: "cfr" });
        assert.isNull(result);
        assert.notCalled(ASRouterTargeting.findMatchingMessage);
      });

      it("should show message for valid multi profile scope", async () => {
        const message1 = {
          id: "foo",
          provider: "cfr",
          profileScope: "single",
          groups: [],
        };
        await Router.setState(() => ({
          messages: [message1],
          multiProfileMessageImpressions: { foo: [111, 222] },
          messageImpressions: { foo: [111, 222] },
        }));

        await Router.handleMessageRequest({ provider: "cfr" });
        assert.calledWithMatch(ASRouterTargeting.findMatchingMessage, {
          messages: [
            { id: "foo", provider: "cfr", groups: [], profileScope: "single" },
          ],
        });
      });

      it("should show messages when profile scope is not set", async () => {
        await Router.setState(() => ({
          messages: [
            { id: "foo", provider: "cfr", profileScope: "", groups: [] },
          ],
          messageImpressions: { foo: [111, 222] },
        }));

        await Router.handleMessageRequest({ provider: "cfr" });
        assert.calledWithMatch(ASRouterTargeting.findMatchingMessage, {
          messages: [
            { id: "foo", provider: "cfr", groups: [], profileScope: "" },
          ],
        });
      });
    });
  });
});
