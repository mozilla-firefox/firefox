/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  JsonSchemaValidator:
    "resource://gre/modules/components-utils/JsonSchemaValidator.sys.mjs",
  Policies: "resource:///modules/policies/Policies.sys.mjs",
  WindowsGPOParser: "resource://gre/modules/policies/WindowsGPOParser.sys.mjs",
  macOSPoliciesParser:
    "resource://gre/modules/policies/macOSPoliciesParser.sys.mjs",
  SitePolicyUtils: "resource://gre/modules/SitePolicyUtils.sys.mjs",
});

// This is the file that will be searched for in the
// ${InstallDir}/distribution folder.
const POLICIES_FILENAME = "policies.json";

// When true browser policy is loaded per-user from
// /run/user/$UID/appname
const PREF_PER_USER_DIR = "toolkit.policies.perUserDir";
// For easy testing, modify the helpers/sample.json file,
// and set PREF_ALTERNATE_PATH in firefox.js as:
// /your/repo/browser/components/enterprisepolicies/helpers/sample.json
const PREF_ALTERNATE_PATH = "browser.policies.alternatePath";
// For testing GPO, you can set an alternate location in testing
const PREF_ALTERNATE_GPO = "browser.policies.alternateGPO";

// For testing, we may want to set PREF_ALTERNATE_PATH to point to a file
// relative to the test root directory. In order to enable this, the string
// below may be placed at the beginning of that preference value and it will
// be replaced with the path to the test root directory.
const MAGIC_TEST_ROOT_PREFIX = "<test-root>";
const PREF_TEST_ROOT = "mochitest.testRoot";

const PREF_LOGLEVEL = "browser.policies.loglevel";

// To allow for cleaning up old policies
const PREF_POLICIES_APPLIED = "browser.policies.applied";

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  return new ConsoleAPI({
    prefix: "Enterprise Policies",
    // tip: set maxLogLevel to "debug" and use log.debug() to create detailed
    // messages during development. See LOG_LEVELS in Console.sys.mjs for details.
    maxLogLevel: "error",
    maxLogLevelPref: PREF_LOGLEVEL,
  });
});

const isXpcshell = Services.env.exists("XPCSHELL_TEST_PROFILE_DIR");

// On Nightly under a test harness, ignore real system/user policies so a
// developer's local policies.json or registry entries don't leak into tests.
// Restricted to Nightly so release builds never expose a way to bypass
// enterprise policies via a test env var.
function shouldIgnoreLocalPolicies() {
  return AppConstants.NIGHTLY_BUILD && (Cu.isInAutomation || isXpcshell);
}

// We're only testing for empty objects, not
// empty strings or empty arrays.
function isEmptyObject(obj) {
  if (typeof obj != "object" || Array.isArray(obj)) {
    return false;
  }
  for (let key of Object.keys(obj)) {
    if (!isEmptyObject(obj[key])) {
      return false;
    }
  }
  return true;
}

export function EnterprisePoliciesManager() {
  Services.obs.addObserver(this, "profile-after-change", true);
  Services.obs.addObserver(this, "final-ui-startup", true);
  Services.obs.addObserver(this, "sessionstore-windows-restored", true);
  Services.obs.addObserver(this, "EnterprisePolicies:Restart", true);
  Services.obs.addObserver(this, "distribution-customization-complete", true);
}

EnterprisePoliciesManager.prototype = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIObserver",
    "nsISupportsWeakReference",
    "nsIEnterprisePolicies",
  ]),

  _initialize() {
    if (Services.prefs.getBoolPref(PREF_POLICIES_APPLIED, false)) {
      if ("_cleanup" in lazy.Policies) {
        let policyImpl = lazy.Policies._cleanup;

        for (let timing of Object.keys(this._callbacks)) {
          let policyCallback = policyImpl[timing];
          if (policyCallback) {
            this._schedulePolicyCallback(
              timing,
              policyCallback.bind(
                policyImpl,
                this /* the EnterprisePoliciesManager */
              )
            );
          }
        }
      }
      Services.prefs.clearUserPref(PREF_POLICIES_APPLIED);
    }

    let provider = this._chooseProvider();

    if (provider.failed) {
      this.status = Ci.nsIEnterprisePolicies.FAILED;
      return;
    }

    if (!provider.hasPolicies) {
      this.status = Ci.nsIEnterprisePolicies.INACTIVE;
      return;
    }

    // Because security.enterprise_roots.enabled is true by default, we can
    // ignore attempts by Antivirus to try to set it via policy.
    // We have to explicitly check for true or 1 because this happens before
    // policy is parsed against the schema, so the value could be coming
    // from the registry.
    if (
      Object.keys(provider.policies).length === 1 &&
      provider.policies.Certificates &&
      Object.keys(provider.policies.Certificates).length === 1 &&
      (provider.policies.Certificates.ImportEnterpriseRoots === true ||
        provider.policies.Certificates.ImportEnterpriseRoots === 1)
    ) {
      this.status = Ci.nsIEnterprisePolicies.INACTIVE;
      return;
    }

    this.status = Ci.nsIEnterprisePolicies.ACTIVE;
    this._parsedPolicies = {};
    this._activatePolicies(provider.policies);

    Services.prefs.setBoolPref(PREF_POLICIES_APPLIED, true);
  },

  _reportEnterpriseTelemetry() {
    Glean.policies.count.set(Object.keys(this._parsedPolicies || {}).length);
    Glean.policies.isEnterprise.set(this.isEnterprise);
  },

  _chooseProvider() {
    let platformProvider = null;
    if (AppConstants.platform == "win" && AppConstants.MOZ_SYSTEM_POLICIES) {
      platformProvider = new WindowsGPOPoliciesProvider();
    } else if (
      AppConstants.platform == "macosx" &&
      AppConstants.MOZ_SYSTEM_POLICIES
    ) {
      platformProvider = new macOSPoliciesProvider();
    }
    let jsonProvider = new JSONPoliciesProvider();
    if (platformProvider && platformProvider.hasPolicies) {
      if (jsonProvider.hasPolicies) {
        return new CombinedProvider(platformProvider, jsonProvider);
      }
      return platformProvider;
    }
    return jsonProvider;
  },

  _activatePolicies(unparsedPolicies) {
    let { schema } = ChromeUtils.importESModule(
      "resource:///modules/policies/schema.sys.mjs"
    );

    for (let policyName of Object.keys(unparsedPolicies)) {
      let policySchema = schema.properties[policyName];
      let policyParameters = unparsedPolicies[policyName];

      if (!policySchema) {
        lazy.log.error(`Unknown policy: ${policyName}`);
        continue;
      }

      let { valid: parametersAreValid, parsedValue: parsedParameters } =
        lazy.JsonSchemaValidator.validate(policyParameters, policySchema, {
          allowAdditionalProperties: true,
        });

      if (!parametersAreValid) {
        lazy.log.error(`Invalid parameters specified for ${policyName}.`);
        continue;
      }

      let policyImpl = lazy.Policies[policyName];

      if (!policyImpl) {
        // This means there is an entry in the schema, but no implementaton.
        // We only do this when we deprecate policies.
        lazy.log.info(`${policyName} has been deprecated.`);
        continue;
      }

      if (policyImpl.validate && !policyImpl.validate(parsedParameters)) {
        lazy.log.error(
          `Parameters for ${policyName} did not validate successfully.`
        );
        continue;
      }

      this._parsedPolicies[policyName] = parsedParameters;

      for (let timing of Object.keys(this._callbacks)) {
        let policyCallback = policyImpl[timing];
        if (policyCallback) {
          this._schedulePolicyCallback(
            timing,
            policyCallback.bind(
              policyImpl,
              this /* the EnterprisePoliciesManager */,
              parsedParameters
            )
          );
        }
      }
    }
  },

  _callbacks: {
    // The earliest that a policy callback can run. This will
    // happen right after the Policy Engine itself has started,
    // and before the Add-ons Manager has started.
    onBeforeAddons: [],

    // This happens after all the initialization related to
    // the profile has finished (prefs, places database, etc.).
    onProfileAfterChange: [],

    // Just before the first browser window gets created.
    onBeforeUIStartup: [],

    // Called after all windows from the last session have been
    // restored (or the default window and homepage tab, if the
    // session is not being restored).
    // The content of the tabs themselves have not necessarily
    // finished loading.
    onAllWindowsRestored: [],
  },

  _schedulePolicyCallback(timing, callback) {
    this._callbacks[timing].push(callback);
  },

  _runPoliciesCallbacks(timing) {
    let callbacks = this._callbacks[timing];
    while (callbacks.length) {
      let callback = callbacks.shift();
      try {
        callback();
      } catch (ex) {
        lazy.log.error("Error running ", callback, `for ${timing}:`, ex);
      }
    }
  },

  async _restart() {
    DisallowedFeatures = {};
    SitePolicies = [];

    Services.ppmm.sharedData.delete("EnterprisePolicies:Status");
    Services.ppmm.sharedData.delete("EnterprisePolicies:DisallowedFeatures");
    Services.ppmm.sharedData.delete("EnterprisePolicies:SitePolicies");

    this._status = Ci.nsIEnterprisePolicies.UNINITIALIZED;
    this._parsedPolicies = undefined;
    for (let timing of Object.keys(this._callbacks)) {
      this._callbacks[timing] = [];
    }

    // Simulate the startup process. This step-by-step is a bit ugly but it
    // tries to emulate the same behavior as of a normal startup.
    let notifyTopicOnIdle = topic =>
      new Promise(resolve => {
        ChromeUtils.idleDispatch(() => {
          this.observe(null, topic, "");
          resolve();
        });
      });
    await notifyTopicOnIdle("policies-startup");
    await notifyTopicOnIdle("profile-after-change");
    await notifyTopicOnIdle("final-ui-startup");
    await notifyTopicOnIdle("sessionstore-windows-restored");
    await notifyTopicOnIdle("distribution-customization-complete");
  },

  // nsIObserver implementation
  observe: function BG_observe(subject, topic) {
    switch (topic) {
      case "policies-startup":
        // Before the first set of policy callbacks runs, we must
        // initialize the service.
        this._initialize();

        this._runPoliciesCallbacks("onBeforeAddons");
        break;

      case "profile-after-change":
        this._runPoliciesCallbacks("onProfileAfterChange");
        break;

      case "final-ui-startup":
        this._runPoliciesCallbacks("onBeforeUIStartup");
        break;

      case "sessionstore-windows-restored":
        this._runPoliciesCallbacks("onAllWindowsRestored");
        break;

      case "EnterprisePolicies:Restart":
        this._restart().then(null, console.error);
        break;

      case "distribution-customization-complete":
        this._reportEnterpriseTelemetry();

        // Notify the test observer when the last message
        // is received.
        Services.obs.notifyObservers(
          null,
          "EnterprisePolicies:AllPoliciesApplied"
        );

        break;
    }
  },

  disallowFeature(feature, neededOnContentProcess = false) {
    DisallowedFeatures[feature] = neededOnContentProcess;

    // NOTE: For optimization purposes, only features marked as needed
    // on content process will be passed onto the child processes.
    if (neededOnContentProcess) {
      Services.ppmm.sharedData.set(
        "EnterprisePolicies:DisallowedFeatures",
        new Set(
          Object.keys(DisallowedFeatures).filter(key => DisallowedFeatures[key])
        )
      );
    }
  },

  updateSitePolicies(policies) {
    SitePolicies = policies;

    let clonable = policies.map(policy => ({
      match: policy.match.patterns.map(p => p.pattern),
      exceptions: policy.exceptions.patterns.map(p => p.pattern),
      features: policy.features,
    }));

    Services.ppmm.sharedData.set("EnterprisePolicies:SitePolicies", clonable);
  },

  // ------------------------------
  // public nsIEnterprisePolicies members
  // ------------------------------

  _status: Ci.nsIEnterprisePolicies.UNINITIALIZED,

  set status(val) {
    this._status = val;
    if (val != Ci.nsIEnterprisePolicies.INACTIVE) {
      Services.ppmm.sharedData.set("EnterprisePolicies:Status", val);
    }
  },

  get status() {
    return this._status;
  },

  isAllowed(feature) {
    return !(feature in DisallowedFeatures);
  },

  isAllowedForURI(feature, uri) {
    return lazy.SitePolicyUtils.isAllowedForURI(
      this,
      SitePolicies,
      feature,
      uri
    );
  },

  getActivePolicies() {
    return this._parsedPolicies;
  },

  setSupportMenu(supportMenu) {
    SupportMenu = supportMenu;
  },

  getSupportMenu() {
    return SupportMenu;
  },

  setExtensionPolicies(extensionPolicies) {
    ExtensionPolicies = extensionPolicies;
  },

  getExtensionPolicy(extensionID) {
    if (ExtensionPolicies && extensionID in ExtensionPolicies) {
      return ExtensionPolicies[extensionID];
    }
    return null;
  },

  setExtensionSettings(extensionSettings) {
    ExtensionSettings = extensionSettings;
    if (
      "*" in extensionSettings &&
      "install_sources" in extensionSettings["*"]
    ) {
      InstallSources = new MatchPatternSet(
        extensionSettings["*"].install_sources
      );
    }
  },

  getExtensionSettings(extensionID) {
    if (!ExtensionSettings) {
      return null;
    }
    if (extensionID in ExtensionSettings) {
      const settings = ExtensionSettings[extensionID];
      if (
        settings.installation_mode === "force_installed" &&
        !("updates_disabled" in settings)
      ) {
        return { ...settings, updates_disabled: false };
      }
      return settings;
    }
    if ("*" in ExtensionSettings) {
      return ExtensionSettings["*"];
    }
    return null;
  },

  isAddonRequiredByPolicy(addonID) {
    const policySettings = this.getExtensionSettings(addonID);
    const legacyLockedSettings =
      this.getActivePolicies()?.Extensions?.Locked ?? [];
    return (
      ["force_installed", "normal_installed"].includes(
        policySettings?.installation_mode
      ) || legacyLockedSettings.includes(addonID)
    );
  },

  mayInstallAddon(addon) {
    // See https://dev.chromium.org/administrators/policy-list-3/extension-settings-full
    if (!ExtensionSettings) {
      return true;
    }
    if (addon.id in ExtensionSettings) {
      if ("installation_mode" in ExtensionSettings[addon.id]) {
        switch (ExtensionSettings[addon.id].installation_mode) {
          case "blocked":
            return false;
          default:
            return true;
        }
      }
    }
    if ("*" in ExtensionSettings) {
      if (
        ExtensionSettings["*"].installation_mode &&
        ExtensionSettings["*"].installation_mode == "blocked"
      ) {
        return false;
      }
      if ("allowed_types" in ExtensionSettings["*"]) {
        return ExtensionSettings["*"].allowed_types.includes(addon.type);
      }
    }
    return true;
  },

  allowedInstallSource(uri) {
    return InstallSources ? InstallSources.matches(uri) : true;
  },

  isExemptExecutableExtension(url, extension) {
    let urlObject = URL.parse(url);
    if (!urlObject) {
      return false;
    }
    let { hostname } = urlObject;
    let exemptArray =
      this.getActivePolicies()
        ?.ExemptDomainFileTypePairsFromFileTypeDownloadWarnings;
    if (!hostname || !extension || !exemptArray) {
      return false;
    }
    extension = extension.toLowerCase();
    let domains = exemptArray
      .filter(item => item.file_extension.toLowerCase() == extension)
      .map(item => item.domains)
      .flat();
    for (let domain of domains) {
      if (Services.eTLD.hasRootDomain(hostname, domain)) {
        return true;
      }
    }
    return false;
  },

  get isEnterprise() {
    let excludedDistributionIDs = [
      "mozilla-mac-eol-esr115",
      "mozilla-win-eol-esr115",
    ];
    let distroId = Services.prefs
      .getDefaultBranch(null)
      .getCharPref("distribution.id", "");

    let policiesLength = Object.keys(this._parsedPolicies || {}).length;

    let isEnterprise =
      // As we migrate folks to ESR for other reasons (deprecating an OS),
      // we need to add checks here for distribution IDs.
      (AppConstants.IS_ESR && !excludedDistributionIDs.includes(distroId)) ||
      // If there are policies then its enterprise.
      policiesLength > 0;

    return isEnterprise;
  },
};

let DisallowedFeatures = {};
let SitePolicies = [];
let SupportMenu = null;
let ExtensionPolicies = null;
let ExtensionSettings = null;
let InstallSources = null;

/*
 * JSON PROVIDER OF POLICIES
 *
 * This is a platform-agnostic provider which looks for
 * policies specified through a policies.json file stored
 * in the installation's distribution folder.
 */

class JSONPoliciesProvider {
  constructor() {
    this._policies = null;
    this._readData();
  }

  get hasPolicies() {
    return this._policies !== null && !isEmptyObject(this._policies);
  }

  get policies() {
    return this._policies;
  }

  get failed() {
    return this._failed;
  }

  _getLocalConfigurationFile() {
    if (shouldIgnoreLocalPolicies()) {
      return null;
    }

    if (AppConstants.platform == "linux" && AppConstants.MOZ_SYSTEM_POLICIES) {
      let systemConfigFile = Services.dirsvc.get("SysConfD", Ci.nsIFile);
      systemConfigFile.append("policies");
      systemConfigFile.append(POLICIES_FILENAME);
      if (systemConfigFile.exists()) {
        return systemConfigFile;
      }
    }

    try {
      let configFile;
      let perUserPath = Services.prefs.getBoolPref(PREF_PER_USER_DIR, false);
      if (perUserPath) {
        configFile = Services.dirsvc.get("XREUserRunTimeDir", Ci.nsIFile);
      } else {
        configFile = Services.dirsvc.get("XREAppDist", Ci.nsIFile);
      }
      configFile.append(POLICIES_FILENAME);
      return configFile;
    } catch (ex) {
      // Getting the correct directory will fail in xpcshell tests. This should
      // be handled the same way as if the configFile simply does not exist.
      return null;
    }
  }

  _getConfigurationFile() {
    let configFile = this._getLocalConfigurationFile();

    let alternatePath = Services.prefs.getStringPref(PREF_ALTERNATE_PATH, "");

    // Check if we are in automation *before* we use the synchronous
    // nsIFile.exists() function or allow the config file to be overriden
    // An alternate policy path can also be used in Nightly builds (for
    // testing purposes), but the Background Update Agent will be unable to
    // detect the alternate policy file so the DisableAppUpdate policy may not
    // work as expected.
    if (
      alternatePath &&
      (Cu.isInAutomation || AppConstants.NIGHTLY_BUILD || isXpcshell) &&
      (!configFile || !configFile.exists())
    ) {
      if (alternatePath.startsWith(MAGIC_TEST_ROOT_PREFIX)) {
        // Intentionally not using a default value on this pref lookup. If no
        // test root is set, we are not currently testing and this function
        // should throw rather than returning something.
        let testRoot = Services.prefs.getStringPref(PREF_TEST_ROOT);
        let relativePath = alternatePath.substring(
          MAGIC_TEST_ROOT_PREFIX.length
        );
        if (AppConstants.platform == "win") {
          relativePath = relativePath.replace(/\//g, "\\");
        }
        alternatePath = testRoot + relativePath;
      }

      configFile = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      configFile.initWithPath(alternatePath);
    }

    return configFile;
  }

  _readData() {
    let configFile = this._getConfigurationFile();
    if (!configFile) {
      // Do nothing, _policies will remain null
      return;
    }
    try {
      let data = Cu.readUTF8File(configFile);
      if (data) {
        lazy.log.debug(`policies.json path = ${configFile.path}`);
        lazy.log.debug(`policies.json content = ${data}`);
        this._policies = JSON.parse(data).policies;

        if (!this._policies) {
          lazy.log.error("Policies file doesn't contain a 'policies' object");
          this._policies = null;
          this._failed = true;
        }
      }
    } catch (ex) {
      if (
        ex instanceof Components.Exception &&
        ex.result == Cr.NS_ERROR_FILE_NOT_FOUND
      ) {
        // Do nothing, _policies will remain null
      } else if (ex instanceof SyntaxError) {
        lazy.log.error(`Error parsing JSON file: ${ex}`);
        this._failed = true;
      } else {
        lazy.log.error(`Error reading JSON file: ${ex}`);
        this._failed = true;
      }
    }
  }
}

class WindowsGPOPoliciesProvider {
  constructor() {
    this._policies = null;

    let wrk = Cc["@mozilla.org/windows-registry-key;1"].createInstance(
      Ci.nsIWindowsRegKey
    );

    // Machine policies override user policies, so we read
    // user policies first and then replace them if necessary.
    this._readData(wrk, wrk.ROOT_KEY_CURRENT_USER);
    // We don't access machine policies in testing
    if (!Cu.isInAutomation && !isXpcshell) {
      this._readData(wrk, wrk.ROOT_KEY_LOCAL_MACHINE);
    }
  }

  get hasPolicies() {
    return this._policies !== null && !isEmptyObject(this._policies);
  }

  get policies() {
    return this._policies;
  }

  get failed() {
    return this._failed;
  }

  _readData(wrk, root) {
    try {
      let regLocation = "SOFTWARE\\Policies";
      if (Cu.isInAutomation || isXpcshell) {
        let altLocation = Services.prefs.getStringPref(PREF_ALTERNATE_GPO, "");
        if (altLocation) {
          regLocation = altLocation;
        } else if (shouldIgnoreLocalPolicies()) {
          return;
        }
      }
      wrk.open(root, regLocation, wrk.ACCESS_READ);
      if (wrk.hasChild("Mozilla\\" + Services.appinfo.name)) {
        lazy.log.debug(
          `root = ${
            root == wrk.ROOT_KEY_CURRENT_USER
              ? "HKEY_CURRENT_USER"
              : "HKEY_LOCAL_MACHINE"
          }`
        );
        this._policies = lazy.WindowsGPOParser.readPolicies(
          wrk,
          this._policies
        );
      }
      wrk.close();
    } catch (e) {
      lazy.log.error("Unable to access registry - ", e);
    }
  }
}

class macOSPoliciesProvider {
  constructor() {
    this._policies = null;
    let prefReader = Cc["@mozilla.org/mac-preferences-reader;1"].createInstance(
      Ci.nsIMacPreferencesReader
    );
    if (!prefReader.policiesEnabled()) {
      return;
    }
    this._policies = lazy.macOSPoliciesParser.readPolicies(prefReader);
  }

  get hasPolicies() {
    return this._policies !== null && Object.keys(this._policies).length;
  }

  get policies() {
    return this._policies;
  }

  get failed() {
    return this._failed;
  }
}

class CombinedProvider {
  constructor(primaryProvider, secondaryProvider) {
    // Combine policies with primaryProvider taking precedence.
    // We only do this for top level policies.
    this._policies = primaryProvider._policies;
    for (let policyName of Object.keys(secondaryProvider.policies)) {
      if (!(policyName in this._policies)) {
        this._policies[policyName] = secondaryProvider.policies[policyName];
      }
    }
  }

  get hasPolicies() {
    // Combined provider always has policies.
    return true;
  }

  get policies() {
    return this._policies;
  }

  get failed() {
    // Combined provider never fails.
    return false;
  }
}
