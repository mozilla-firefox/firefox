import {
  EventEmitter,
  FakePrefs,
  FakensIPrefService,
  GlobalOverrider,
  FakeConsoleAPI,
  FakeLogger,
} from "test/unit/utils";
import Adapter from "enzyme-adapter-react-16";
import { chaiAssertions } from "test/schemas/pings";
import enzyme from "enzyme";

enzyme.configure({ adapter: new Adapter() });

// Cause React warnings to make tests that trigger them fail
const origConsoleError = console.error;
console.error = function (msg, ...args) {
  origConsoleError.apply(console, [msg, ...args]);

  if (
    /(Invalid prop|Failed prop type|Check the render method|React Intl)/.test(
      msg
    )
  ) {
    throw new Error(msg);
  }
};

const req = require.context(".", true, /\.test\.jsx?$/);
const files = req.keys();

// This exposes sinon assertions to chai.assert
sinon.assert.expose(assert, { prefix: "" });

chai.use(chaiAssertions);

const overrider = new GlobalOverrider();

const RemoteSettings = name => ({
  get: () => {
    if (name === "attachment") {
      return Promise.resolve([{ attachment: {} }]);
    }
    return Promise.resolve([]);
  },
  on: () => {},
  off: () => {},
});
RemoteSettings.pollChanges = () => {};

class JSWindowActorParent {
  sendAsyncMessage(name, data) {
    return { name, data };
  }
}

class JSWindowActorChild {
  sendAsyncMessage(name, data) {
    return { name, data };
  }

  sendQuery(name, data) {
    return Promise.resolve({ name, data });
  }

  get contentWindow() {
    return {
      Promise,
    };
  }
}

// Detect plain object passed to lazy getter APIs, and set its prototype to
// global object, and return the global object for further modification.
// Returns the object if it's not plain object.
//
// This is a workaround to make the existing testharness and testcase keep
// working even after lazy getters are moved to plain `lazy` object.
const cachedPlainObject = new Set();
function updateGlobalOrObject(object) {
  // Given this function modifies the prototype, and the following
  // condition doesn't meet on the second call, cache the result.
  if (cachedPlainObject.has(object)) {
    return global;
  }

  if (Object.getPrototypeOf(object).constructor.name !== "Object") {
    return object;
  }

  cachedPlainObject.add(object);
  Object.setPrototypeOf(object, global);
  return global;
}

const TEST_GLOBAL = {
  JSWindowActorParent,
  JSWindowActorChild,
  AboutReaderParent: {
    addMessageListener: (_messageName, _listener) => {},
    removeMessageListener: (_messageName, _listener) => {},
  },
  AboutWelcomeTelemetry: class {
    submitGleanPingForPing() {}
  },
  AddonManager: {
    getActiveAddons() {
      return Promise.resolve({ addons: [], fullData: false });
    },
  },
  AppConstants: {
    MOZILLA_OFFICIAL: true,
    MOZ_APP_VERSION: "69.0a1",
    isPlatformAndVersionAtMost() {
      return false;
    },
    platform: "win",
  },
  BrowserUtils: {
    sendToDeviceEmailsSupported() {
      return true;
    },
  },
  UpdateUtils: { getUpdateChannel() {} },
  BasePromiseWorker: class {
    constructor() {
      this.ExceptionHandlers = [];
    }
    post() {}
  },
  browserSearchRegion: "US",
  BrowserWindowTracker: { getTopWindow() {} },
  ChromeUtils: {
    defineLazyGetter(object, name, f) {
      updateGlobalOrObject(object)[name] = f();
    },
    defineESModuleGetters: updateGlobalOrObject,
    generateQI() {
      return {};
    },
    importESModule() {
      return global;
    },
  },
  ClientEnvironment: {
    get userId() {
      return "foo123";
    },
  },
  Components: {
    Constructor(classId) {
      switch (classId) {
        case "@mozilla.org/referrer-info;1":
          return function (referrerPolicy, sendReferrer, originalReferrer) {
            this.referrerPolicy = referrerPolicy;
            this.sendReferrer = sendReferrer;
            this.originalReferrer = originalReferrer;
          };
      }
      return function () {};
    },
    isSuccessCode: () => true,
  },
  ConsoleAPI: FakeConsoleAPI,
  // NB: These are functions/constructors
  // eslint-disable-next-line object-shorthand
  ContentSearchUIController: function () {},
  // eslint-disable-next-line object-shorthand
  ContentSearchHandoffUIController: function () {},
  Cc: {
    "@mozilla.org/browser/nav-bookmarks-service;1": {
      addObserver() {},
      getService() {
        return this;
      },
      removeObserver() {},
      SOURCES: {},
      TYPE_BOOKMARK: {},
    },
    "@mozilla.org/browser/nav-history-service;1": {
      addObserver() {},
      executeQuery() {},
      getNewQuery() {},
      getNewQueryOptions() {},
      getService() {
        return this;
      },
      insert() {},
      markPageAsTyped() {},
      removeObserver() {},
    },
    "@mozilla.org/io/string-input-stream;1": {
      createInstance() {
        return {};
      },
    },
    "@mozilla.org/updates/update-checker;1": { createInstance() {} },
    "@mozilla.org/widget/useridleservice;1": {
      getService() {
        return {
          idleTime: 0,
          addIdleObserver() {},
          removeIdleObserver() {},
        };
      },
    },
    "@mozilla.org/streamConverters;1": {
      getService() {
        return this;
      },
    },
    "@mozilla.org/network/stream-loader;1": {
      createInstance() {
        return {};
      },
    },
  },
  Ci: {
    nsICryptoHash: {},
    nsIReferrerInfo: { UNSAFE_URL: 5 },
    nsITimer: { TYPE_ONE_SHOT: 1 },
    nsIWebProgressListener: { LOCATION_CHANGE_SAME_DOCUMENT: 1 },
    nsIDOMWindow: Object,
    nsITrackingDBService: {
      TRACKERS_ID: 1,
      TRACKING_COOKIES_ID: 2,
      CRYPTOMINERS_ID: 3,
      FINGERPRINTERS_ID: 4,
      SOCIAL_ID: 5,
    },
    nsICookieBannerService: {
      MODE_DISABLED: 0,
      MODE_REJECT: 1,
      MODE_REJECT_OR_ACCEPT: 2,
      MODE_UNSET: 3,
    },
  },
  Cu: {
    importGlobalProperties() {},
    now: () => window.performance.now(),
    cloneInto: o => JSON.parse(JSON.stringify(o)),
  },
  console: {
    ...console,
    error() {},
  },
  dump() {},
  EveryWindow: {
    registerCallback: (_id, _init, _uninit) => {},
    unregisterCallback: _id => {},
  },
  setTimeout: window.setTimeout.bind(window),
  clearTimeout: window.clearTimeout.bind(window),
  fetch() {},
  // eslint-disable-next-line object-shorthand
  Image: function () {}, // NB: This is a function/constructor
  IOUtils: {
    writeJSON() {
      return Promise.resolve(0);
    },
    readJSON() {
      return Promise.resolve({});
    },
    read() {
      return Promise.resolve(new Uint8Array());
    },
    makeDirectory() {
      return Promise.resolve(0);
    },
    write() {
      return Promise.resolve(0);
    },
    exists() {
      return Promise.resolve(0);
    },
    remove() {
      return Promise.resolve(0);
    },
    stat() {
      return Promise.resolve(0);
    },
  },
  NewTabUtils: {
    activityStreamProvider: {
      getTopFrecentSites: () => [],
      executePlacesQuery: async (sql, options) => ({ sql, options }),
    },
    shortHostname() {},
  },
  OS: {
    File: {
      writeAtomic() {},
      makeDir() {},
      stat() {},
      Error: {},
      read() {},
      exists() {},
      remove() {},
      removeEmptyDir() {},
    },
    Path: {
      join() {
        return "/";
      },
    },
    Constants: {
      Path: {
        localProfileDir: "/",
      },
    },
  },
  PathUtils: {
    join(...parts) {
      return parts[parts.length - 1];
    },
    joinRelative(...parts) {
      return parts[parts.length - 1];
    },
    getProfileDir() {
      return Promise.resolve("/");
    },
    getLocalProfileDir() {
      return Promise.resolve("/");
    },
  },
  PlacesUtils: {
    get bookmarks() {
      return TEST_GLOBAL.Cc["@mozilla.org/browser/nav-bookmarks-service;1"];
    },
    get history() {
      return TEST_GLOBAL.Cc["@mozilla.org/browser/nav-history-service;1"];
    },
    observers: {
      addListener() {},
      removeListener() {},
    },
  },
  Preferences: FakePrefs,
  PrivateBrowsingUtils: {
    isBrowserPrivate: () => false,
    isWindowPrivate: () => false,
    permanentPrivateBrowsing: false,
  },
  DownloadsViewUI: {
    getDisplayName: () => "filename.ext",
    getSizeWithUnits: () => "1.5 MB",
  },
  FileUtils: {
    // eslint-disable-next-line object-shorthand
    File: function () {}, // NB: This is a function/constructor
  },
  Region: {
    home: "US",
    REGION_TOPIC: "browser-region-updated",
  },
  Services: {
    dirsvc: {
      get: () => ({ parent: { parent: { path: "appPath" } } }),
    },
    env: {
      set: () => undefined,
    },
    locale: {
      get appLocaleAsBCP47() {
        return "en-US";
      },
      negotiateLanguages() {},
    },
    urlFormatter: { formatURL: str => str, formatURLPref: str => str },
    mm: {
      addMessageListener: (_msg, _cb) => this.receiveMessage(),
      removeMessageListener() {},
    },
    obs: {
      addObserver() {},
      removeObserver() {},
      notifyObservers() {},
    },
    uuid: {
      generateUUID() {
        return "{foo-123-foo}";
      },
    },
    console: { logStringMessage: () => {} },
    prefs: new FakensIPrefService(),
    tm: {
      dispatchToMainThread: cb => cb(),
      idleDispatchToMainThread: cb => cb(),
    },
    eTLD: {
      getBaseDomain({ spec }) {
        return spec.match(/\/([^/]+)/)[1];
      },
      getBaseDomainFromHost(host) {
        return host.match(/.*?(\w+\.\w+)$/)[1];
      },
      getPublicSuffix() {},
    },
    io: {
      newURI: spec => ({
        mutate: () => ({
          setRef: ref => ({
            finalize: () => ({
              ref,
              spec,
              schemeIs: scheme => spec.startsWith(scheme),
            }),
          }),
        }),
        spec,
      }),
    },
    search: {
      init() {
        return Promise.resolve();
      },
      getVisibleEngines: () =>
        Promise.resolve([{ identifier: "google" }, { identifier: "bing" }]),
      defaultEngine: {
        identifier: "google",
        aliases: ["@google"],
      },
      defaultPrivateEngine: {
        identifier: "bing",
        aliases: ["@bing"],
      },
      getEngineByAlias: async () => null,
    },
    scriptSecurityManager: {
      createNullPrincipal() {},
      getSystemPrincipal() {},
    },
    wm: {
      getMostRecentWindow: () => window,
      getMostRecentBrowserWindow: () => window,
      getEnumerator: () => [],
    },
    ww: { registerNotification() {}, unregisterNotification() {} },
    appinfo: { appBuildID: "20180710100040", version: "69.0a1" },
    scriptloader: { loadSubScript: () => {} },
    startup: {
      getStartupInfo() {
        return {
          process: {
            getTime() {
              return 1588010448000;
            },
          },
        };
      },
    },
  },
  XPCOMUtils: {
    defineLazyGlobalGetters: updateGlobalOrObject,
    defineLazyServiceGetter: updateGlobalOrObject,
    defineLazyServiceGetters: updateGlobalOrObject,
    defineLazyPreferenceGetter(object, name) {
      updateGlobalOrObject(object)[name] = "";
    },
    generateQI() {
      return {};
    },
  },
  EventEmitter,
  ShellService: {
    doesAppNeedPin: () => false,
    isDefaultBrowser: () => true,
  },
  FilterExpressions: {
    eval() {
      return Promise.resolve(false);
    },
  },
  RemoteSettings,
  Localization: class {
    async formatMessages(stringsIds) {
      return Promise.resolve(
        stringsIds.map(({ id, args }) => ({ value: { string_id: id, args } }))
      );
    }
    async formatValue(stringId) {
      return Promise.resolve(stringId);
    }
  },
  FxAccountsConfig: {
    promiseConnectAccountURI(id) {
      return Promise.resolve(id);
    },
  },
  FX_MONITOR_OAUTH_CLIENT_ID: "fake_client_id",
  ExperimentAPI: {
    getExperimentMetaData() {},
    getRolloutMetaData() {},
  },
  NimbusFeatures: {
    glean: {
      getVariable() {},
    },
    newtab: {
      getVariable() {},
      getAllVariables() {},
      onUpdate() {},
      offUpdate() {},
    },
    pocketNewtab: {
      getVariable() {},
      getAllVariables() {},
      onUpdate() {},
      offUpdate() {},
    },
    cookieBannerHandling: {
      getVariable() {},
    },
  },
  TelemetryEnvironment: {
    setExperimentActive() {},
    currentEnvironment: {
      profile: {
        creationDate: 16587,
      },
      settings: {},
    },
  },
  Sampling: {
    ratioSample(_seed, _ratios) {
      return Promise.resolve(0);
    },
  },
  BrowserHandler: {
    get kiosk() {
      return false;
    },
  },
  TelemetrySession: {
    getMetadata(reason) {
      return {
        reason,
        sessionId: "fake_session_id",
      };
    },
  },
  PageThumbs: {
    addExpirationFilter() {},
    removeExpirationFilter() {},
  },
  Logger: FakeLogger,
  LinksCache: class {},
  FaviconFeed: class {},

  getFxAccountsSingleton() {},
  AboutNewTab: {},
  Glean: {
    activityStream: {
      eventClick: {
        record() {},
      },
      endSession: {
        record() {},
      },
    },
    newtab: {
      opened: {
        record() {},
      },
      closed: {
        record() {},
      },
      locale: {
        set() {},
      },
      newtabCategory: {
        set() {},
      },
      homepageCategory: {
        set() {},
      },
      blockedSponsors: {
        set() {},
      },
      sovAllocation: {
        set() {},
      },
    },
    newtabSearch: {
      enabled: {
        set() {},
      },
    },
    newtabHandoffPreference: {
      enabled: {
        set() {},
      },
    },
    pocket: {
      enabled: {
        set() {},
      },
      impression: {
        record() {},
      },
      isSignedIn: {
        set() {},
      },
      sponsoredStoriesEnabled: {
        set() {},
      },
      click: {
        record() {},
      },
      save: {
        record() {},
      },
      topicClick: {
        record() {},
      },
      shim: {
        set() {},
      },
    },
    topsites: {
      enabled: {
        set() {},
      },
      sponsoredEnabled: {
        set() {},
      },
      impression: {
        record() {},
      },
      click: {
        record() {},
      },
      rows: {
        set() {},
      },
      showPrivacyClick: {
        record() {},
      },
      dismiss: {
        record() {},
      },
      prefChanged: {
        record() {},
      },
      sponsoredTilesConfigured: {
        set() {},
      },
      sponsoredTilesReceived: {
        set() {},
      },
    },
    topSites: {
      pingType: {
        set() {},
      },
      position: {
        set() {},
      },
      source: {
        set() {},
      },
      tileId: {
        set() {},
      },
      reportingUrl: {
        set() {},
      },
      advertiser: {
        set() {},
      },
      contextId: {
        set() {},
      },
    },
  },
  GleanPings: {
    newtab: {
      submit() {},
    },
    topSites: {
      submit() {},
    },
    spoc: {
      submit() {},
    },
  },
  Utils: {
    SERVER_URL: "bogus://foo",
  },
};
overrider.set(TEST_GLOBAL);

describe("activity-stream", () => {
  after(() => overrider.restore());
  files.forEach(file => req(file));
});
