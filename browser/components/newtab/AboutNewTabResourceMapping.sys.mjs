/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

export const BUILTIN_ADDON_ID = "newtab@mozilla.org";
export const DISABLE_NEWTAB_AS_ADDON_PREF =
  "browser.newtabpage.disableNewTabAsAddon";
export const TRAINHOP_NIMBUS_DEPLOYMENT_FEATURE_ID =
  "newtabTrainhopAddonDeployment";
export const TRAINHOP_NIMBUS_FIRST_STARTUP_FEATURE_ID =
  "newtabTrainhopFirstStartup";
export const TRAINHOP_XPI_BASE_URL_PREF =
  "browser.newtabpage.trainhopAddon.xpiBaseURL";
// Records the highest currently-enrolled train-hop version for the co-enrollment
// deployment feature. Owned exclusively by AboutNewTabResourceMapping. It tracks
// the current winner and can decrease (e.g. when a higher-version rollout ends,
// which is what drives a downgrade).
export const TRAINHOP_XPI_DEPLOYMENT_VERSION_PREF =
  "browser.newtabpage.trainhopAddonDeployment.version";
// "any" is a sentinel value accepted in either of the two version prefs above,
// meaning "entitled to whichever train-hop version happens to be installed".
// Nimbus never writes it; it exists for the callers that install the XPI with
// the traditional extension install mechanism (namely, newtab devs and our CI
// infrastructure).
export const TRAINHOP_ANY_VERSION_SENTINEL = "any";
export const TRAINHOP_SCHEDULED_UPDATE_STATE_DELAY_PREF =
  "browser.newtabpage.trainhopAddon.scheduledUpdateState.delay";
export const TRAINHOP_SCHEDULED_UPDATE_STATE_TIMEOUT_PREF =
  "browser.newtabpage.trainhopAddon.scheduledUpdateState.timeout";

const FLUENT_SOURCE_NAME = "newtab";
const TOPIC_LOCALES_CHANGED = "intl:app-locales-changed";
const TOPIC_SHUTDOWN = "profile-before-change";
const TOPIC_LANGPACK_STARTUP = "webextension-langpack-startup";
const TOPIC_LANGPACK_SHUTDOWN = "webextension-langpack-shutdown";

const lazy = XPCOMUtils.declareLazy({
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonSettings: "resource://gre/modules/addons/AddonSettings.sys.mjs",
  Langpack: "resource://gre/modules/Extension.sys.mjs",
  AboutHomeStartupCache: "resource:///modules/AboutHomeStartupCache.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  NewTabGleanUtils: "resource://newtab/lib/NewTabGleanUtils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",

  resProto: {
    service: "@mozilla.org/network/protocol;1?name=resource",
    iid: Ci.nsISubstitutingProtocolHandler,
  },
  aomStartup: {
    service: "@mozilla.org/addons/addon-manager-startup;1",
    iid: Ci.amIAddonManagerStartup,
  },
  aboutRedirector: {
    service: "@mozilla.org/network/protocol/about;1?what=newtab",
    iid: Ci.nsIAboutModule,
  },

  // NOTE: the following timeout and delay prefs are used to customize the
  // DeferredTask that calls updateTrainhopAddonState, and they are meant to
  // be set for testing, debugging or QA verification.
  trainhopAddonScheduledUpdateDelay: {
    pref: TRAINHOP_SCHEDULED_UPDATE_STATE_DELAY_PREF,
    default: 5000,
  },
  trainhopAddonScheduledUpdateTimeout: {
    pref: TRAINHOP_SCHEDULED_UPDATE_STATE_TIMEOUT_PREF,
    default: -1,
  },
  trainhopAddonXPIBaseURL: {
    pref: TRAINHOP_XPI_BASE_URL_PREF,
    default: "",
  },
  trainhopAddonDeploymentXPIVersion: {
    pref: TRAINHOP_XPI_DEPLOYMENT_VERSION_PREF,
    default: "",
  },
});

/**
 * AboutNewTabResourceMapping is responsible for creating the mapping between
 * the built-in add-on newtab code, and the chrome://newtab and resource://newtab
 * URI prefixes (which are also used by the component mode for newtab, and acts
 * as a compatibility layer).
 *
 * When the built-in add-on newtab is being read in from an XPI, the
 * AboutNewTabResourceMapping is also responsible for doing dynamic Fluent
 * and Glean ping/metric registration.
 */
export var AboutNewTabResourceMapping = {
  initialized: false,
  log: null,
  newTabAsAddonDisabled: false,

  _rootURISpec: null,
  _addonIsXPI: null,
  _addonVersion: null,
  _addonListener: null,
  _builtinVersion: null,
  _updateAddonStateDeferredTask: null,
  _supportedLocales: null,
  _langpackShadowSources: null,
  _inObserveHandler: false,

  /**
   * Returns the version string for whichever version of New Tab is currently
   * being used. This is exposed to Nimbus / Experimenter for advanced targeting
   * based on the currently used newtab version.
   *
   * The reason that we expose this specially and cannot simply use
   * addonsInfo.addons["newtab@mozilla.org"] to do this kind of advanced
   * targeting is documented in bug 1983928 (essentially, the addonsInfo
   * route doesn't take into account that the addon might be disabled or
   * bypassed via the `DISABLE_NEWTAB_AS_ADDON_PREF` pref).
   *
   * @type {string}
   */
  get addonVersion() {
    return this._addonVersion;
  },

  /**
   * Returns true if an train-hopped XPI is in use, or false if we're using
   * the built-in instance of newtab.
   *
   * @type {string}
   */
  get addonIsXPI() {
    return this._addonIsXPI;
  },

  /**
   * This should be called early on in the lifetime of the browser, before any
   * attempt to load a resource from resource://newtab or chrome://newtab.
   *
   * This method is a no-op after the first call.
   */
  init() {
    if (this.initialized) {
      return;
    }

    this.logger.debug("Initializing:");

    // NOTE: this pref is read only once per session on purpose
    // (and it is expected to be used by the resource mapping logic
    // on the next application startup if flipped at runtime, e.g. as
    // part of an emergency pref flip through Nimbus).
    this.newTabAsAddonDisabled = Services.prefs.getBoolPref(
      DISABLE_NEWTAB_AS_ADDON_PREF,
      false
    );
    this.inSafeMode = Services.appinfo.inSafeMode;
    this.getBuiltinAddonVersion();
    this.registerNewTabResources();
    this.addAddonListener();

    this.initialized = true;
    this.logger.debug("Initialized");
  },

  /**
   * Adds an add-on listener to detect postponed installations of the newtab add-on
   * and invalidate the AboutHomeStartupCache. This method is a no-op when the
   * emergency fallback `browser.newtabpage.disableNewTabAsAddon` about:config pref
   * is set to true.
   */
  addAddonListener() {
    if (!this._addonListener && !this.newTabAsAddonDisabled) {
      // The newtab add-on has a background.js script which defers updating until
      // the next restart. We still, however, want to blow away the about:home
      // startup cache when we notice this postponed install, to avoid loading
      // a cache created with another version of newtab.
      const addonInstallListener = {};
      addonInstallListener.onInstallPostponed = install => {
        if (install.addon.id === BUILTIN_ADDON_ID) {
          this.logger.debug(
            "Invalidating AboutHomeStartupCache on detected newly installed newtab resources"
          );
          lazy.AboutHomeStartupCache.clearCacheAndUninit();
        }
      };
      lazy.AddonManager.addInstallListener(addonInstallListener);
      this._addonListener = addonInstallListener;
    }
  },

  /**
   * Retrieves the version of the built-in newtab add-on from AddonManager.
   * If AddonManager.getBuiltinAddonVersion hits an unexpected exception (e.g.
   * if the method is unexpectedly called before AddonManager and XPIProvider
   * are being started), it sets the _builtinVersion property to null and logs
   * a warning message.
   */
  getBuiltinAddonVersion() {
    try {
      this._builtinVersion =
        lazy.AddonManager.getBuiltinAddonVersion(BUILTIN_ADDON_ID);
    } catch (e) {
      this._builtinVersion = null;
      this.logger.warn(
        "Unexpected failure on retrieving builtin addon version",
        e
      );
    }
  },

  isXPIInCurrentProfile(rootURI) {
    try {
      const { file: xpiFile } = rootURI
        .QueryInterface(Ci.nsIJARURI)
        .JARFile.QueryInterface(Ci.nsIFileURL);
      // NOTE: this logic is expecting the XPI file to be located
      // in PROFILE_DIR/extensions/newtab@mozilla.org and so for
      // xpiFile.parent.parent.path to be matching the path
      // returned by PathUtils.profileDir.
      const xpiIsInsideProfile =
        PathUtils.profileDir === xpiFile.parent.parent.path;
      if (!xpiIsInsideProfile) {
        this.logger.warn(
          "Detected newtab XPI as located outside of the current Firefox profile"
        );
      }
      return xpiIsInsideProfile;
    } catch (err) {
      this.logger.error(
        "Unexpected error on verifying newtab XPI path is inside the current Firefox profile",
        err
      );
    }
    // If we failed to confirm the XPIFile is located inside the current Firefox
    // profile, then let's fallback to built-in add-on resources.
    return false;
  },

  /**
   * Gather details from the newtab active addon, used by getPreferredMapping
   * as part of determining if the newtab resource to be mapped should be the
   * one from the built-in or from the train-hop version (and it is stubbed
   * in tests that verify fallback to built-in resources under edge cases like
   * relocated Firefox profiles, see Bug 2007810).
   *
   * @returns {{ version: ?string, rootURI: ?nsIURI, isPrivileged: ?boolean}}
   *   Returns version, rootURI and isPrivileged properties from the newtab
   *   WebExtensionPolicy instance that is currently active.
   */
  getActiveAddonInfo() {
    const policy = WebExtensionPolicy.getByID(BUILTIN_ADDON_ID);
    // Retrieve the mapping url (but fallback to the known url for the
    // newtab resources bundled in the Desktop omni jar if that fails).
    let { version, rootURI, isPrivileged } = policy?.extension ?? {};
    return { version, rootURI, isPrivileged };
  },

  /**
   * Gets the preferred mapping for newtab resources. This method tries to retrieve
   * the rootURI from the WebExtensionPolicy instance of the newtab add-on, or falling
   * back to the URI of the newtab resources bundled in the Desktop omni jar if not found.
   * The newtab resources bundled in the Desktop omni jar are instead always preferred
   * while running in safe mode or if the emergency fallback about:config pref
   * (`browser.newtabpage.disableNewTabAsAddon`) is set to true.
   *
   * @returns {{version: ?string, rootURI: nsIURI}}
   *   Returns the preferred newtab root URI for resource://newtab and chrome://newtab,
   *   along with add-on version if using the newtab add-on root URI, or null
   *   when the newtab add-on root URI was not selected as the preferred one.
   */
  getPreferredMapping() {
    const { inSafeMode, newTabAsAddonDisabled } = this;
    // Retrieve the mapping url (but fallback to the known url for the
    // newtab resources bundled in the Desktop omni jar if that fails).
    let { version, rootURI, isPrivileged } = this.getActiveAddonInfo();
    let isXPI = rootURI?.spec.endsWith(".xpi!/");

    // If we failed to retrieve the builtin add-on version, avoid mapping
    // XPI resources as an additional safety measure, because later it
    // wouldn't be possible to check if the builtin version is more recent
    // than the train-hop add-on version that may be already installed.
    if (isXPI && this._builtinVersion === null) {
      rootURI = null;
      isXPI = false;
    }

    // If the XPI path is not inside the current Firefox instance profile,
    // fallback to use the resources bundled with Firefox itself (see Bug 2007810)
    if (isXPI && !this.isXPIInCurrentProfile(rootURI)) {
      rootURI = null;
      isXPI = false;
    }

    // Do not use XPI resources to prepare to uninstall the train-hop add-on xpi
    // later in the current application session from updateTrainhopAddonState, if:
    //
    // - the train-hop version pref is empty (the client has been unenrolled
    //   from from the deployment feature in the previous browsing session and
    //   so we fallback to the resources bundled in the Desktop omni jar). The
    //   pref being set means the client is still entitled to a train-hop version.
    // - the builtin add-on version is equal or greater than the train-hop add-on
    //   version (and so the application has been updated and the old train-hop
    //   add-on is obsolete and can be uninstalled)
    // - the installed train-hop add-on version is higher than the version the
    //   client is currently entitled to (e.g. the higher-version rollout has
    //   ended while a lower-version one is still active); we stop using it so the
    //   highest currently-enrolled version can take over on the next startups.
    //   This comparison is skipped when the entitled version is
    //   TRAINHOP_ANY_VERSION_SENTINEL.
    // - the train-hop add-on xpi is not system-signed (as specifically required for
    //   newtab xpi being installed in the `extensions` profile subdirectory by
    //   the custom install logic provided by the _installTrainhopAddon method).
    const entitledVersion = lazy.trainhopAddonDeploymentXPIVersion;
    const entitledToAnyVersion =
      entitledVersion === TRAINHOP_ANY_VERSION_SENTINEL;
    const shouldUninstallXPI = isXPI
      ? entitledVersion === "" ||
        Services.vc.compare(this._builtinVersion, version) >= 0 ||
        (!entitledToAnyVersion &&
          Services.vc.compare(version, entitledVersion) > 0) ||
        (lazy.AddonSettings.REQUIRE_SIGNING && !isPrivileged)
      : false;

    if (!rootURI || inSafeMode || newTabAsAddonDisabled || shouldUninstallXPI) {
      const builtinAddonsURI = lazy.resProto.getSubstitution("builtin-addons");
      rootURI = Services.io.newURI("newtab/", null, builtinAddonsURI);
      version = this._builtinVersion;
      isXPI = false;
    }
    return { isXPI, version, rootURI };
  },

  /**
   * Registers the resource://newtab and chrome://newtab resources, and also
   * kicks off dynamic Fluent and Glean registration if the add-on is installed
   * via an XPI.
   */
  registerNewTabResources() {
    const RES_PATH = "newtab";
    try {
      const { isXPI, version, rootURI } = this.getPreferredMapping();
      this._rootURISpec = rootURI.spec;
      this._addonVersion = version;
      this._addonIsXPI = isXPI;
      this.logger.log(
        this.newTabAsAddonDisabled
          ? `Train-hopping disabled - mapping newtab resources from ${rootURI.spec}`
          : `Mapping newtab resources from ${isXPI ? "XPI" : "built-in add-on"} version ${version} ` +
              `on application version ${AppConstants.MOZ_APP_VERSION_DISPLAY}`
      );
      lazy.resProto.setSubstitutionWithFlags(
        RES_PATH,
        rootURI,
        Ci.nsISubstitutingProtocolHandler.ALLOW_CONTENT_ACCESS
      );
      const manifestURI = Services.io.newURI("manifest.json", null, rootURI);
      this._chromeHandle = lazy.aomStartup.registerChrome(manifestURI, [
        ["content", "newtab", "data/content", "contentaccessible=yes"],
      ]);

      if (isXPI) {
        // We must be a train-hopped XPI running in this app. This means we
        // may have Fluent files or Glean pings/metrics to register dynamically.
        this.registerFluentSources(rootURI);
        this.registerMetricsFromJson();
        this.reevaluateNimbusRecipes();
      }
      lazy.aboutRedirector.wrappedJSObject.notifyBuiltInAddonInitialized();
      Glean.newtab.addonReadySuccess.set(true);
      Glean.newtab.addonXpiUsed.set(isXPI);
      this.logger.debug("Newtab resource mapping completed successfully");
    } catch (e) {
      this.logger.error("Failed to complete resource mapping: ", e);
      Glean.newtab.addonReadySuccess.set(false);
      throw e;
    }
  },

  /**
   * Registers Fluent strings contained within the XPI.
   *
   * @param {nsIURI} rootURI
   *   The rootURI for the newtab add-on.
   * @returns {Promise<undefined>}
   *   Resolves once the Fluent strings have been registered, or even if a
   *   failure to register them has occurred (which will log the error).
   */
  async registerFluentSources(rootURI) {
    try {
      // Read in the list of locales included with the XPI. This will prevent
      // us from accidentally registering a L10nFileSource that wasn't included.
      this._supportedLocales = new Set(
        await fetch(rootURI.resolve("/locales/supported-locales.json")).then(
          r => r.json()
        )
      );

      this._langpackShadowSources = new Set();

      // Set up observers so that if the user changes the list of available
      // locales, we'll re-register.
      Services.obs.addObserver(this, TOPIC_LOCALES_CHANGED);
      Services.obs.addObserver(this, TOPIC_SHUTDOWN);
      Services.obs.addObserver(this, TOPIC_LANGPACK_STARTUP);
      Services.obs.addObserver(this, TOPIC_LANGPACK_SHUTDOWN);
      // Register the primary newtab source in the "app" metasource so that
      // its strings are available wherever app strings are resolved (notably
      // the en-US fallback chain when no langpack is active).
      this._updateFluentSourcesRegistration();

      // For each already-active langpack, also register a shadow newtab
      // source inside that langpack's metasource. This lets the L10nRegistry
      // solver produce a bundle where the langpack provides every other
      // resource in the active locale while the newtab XPI provides its own
      // (possibly newer) newtab.ftl entries. Without this, train-hop-only
      // strings (added in the XPI but not yet in any langpack) would never
      // appear in a non-English locale: the (locale, app) bundle can't be
      // built because the rest of the app sources are en-US only, and the
      // (locale, langpack) bundle would only contain the langpack's older
      // newtab.ftl. See Bug 2046945.
      //
      // Batch the registration into a single registerSources call so we
      // emit one intl:l10n-sources-changed / intl:app-locales-changed
      // broadcast for the whole set of active langpacks rather than one
      // per langpack. Profiles in Bug 2049845 showed the per-langpack
      // path multiplying observer cascades at startup on installations
      // with multiple langpacks (Flatpak/Snap/MSIX).
      const shadowSources = [];
      for (const langpackId of lazy.Langpack.activeLangpackIds) {
        if (this._langpackShadowSources.has(langpackId)) {
          continue;
        }
        const sourceName = `${FLUENT_SOURCE_NAME}-${langpackId}`;
        shadowSources.push(this._buildNewtabFileSource(sourceName, langpackId));
        this._langpackShadowSources.add(langpackId);
      }
      if (shadowSources.length) {
        L10nRegistry.getInstance().registerSources(shadowSources);
      }
    } catch (e) {
      // TODO: consider if we should collect this in telemetry.
      this.logger.error(
        `Error on registering fluent files from ${rootURI.spec}:`,
        e
      );
    }
  },

  /**
   * This is run during `init` after the addonVersion has been updated, and only
   * if an XPI is being used. We tell Nimbus to re-evaluate any recipes in case
   * the XPI is being used for an experiment.
   *
   * @returns {Promise<void>}
   */
  async reevaluateNimbusRecipes() {
    // Tell Experimenter to re-evaluate experiment recipe targeting in case we
    // just installed an XPI.
    try {
      await lazy.ExperimentAPI.updateRecipesForFeatures("newtab-trainhop", [
        "newtabTrainhop",
      ]);
    } catch (e) {
      // TODO: consider if we should collect this in telemetry.
      this.logger.error("Error when re-evaluating Nimbus recipes:", e);
    }
  },

  /**
   * Sets up the L10nFileSource for the newtab Fluent files included in the
   * XPI that are in the available locales for the app. If a pre-existing
   * registration exists, it will be updated.
   */
  _updateFluentSourcesRegistration() {
    let availableLocales = new Set(Services.locale.availableLocales);
    let availableSupportedLocales =
      this._supportedLocales.intersection(availableLocales);

    const newtabFileSource = this._buildNewtabFileSource(
      FLUENT_SOURCE_NAME,
      "app"
    );

    let registry = L10nRegistry.getInstance();
    if (registry.hasSource(FLUENT_SOURCE_NAME)) {
      registry.updateSources([newtabFileSource]);
      this.logger.debug(
        "Newtab strings updated for ",
        Array.from(availableSupportedLocales)
      );
    } else {
      registry.registerSources([newtabFileSource]);
      this.logger.debug(
        "Newtab strings registered for ",
        Array.from(availableSupportedLocales)
      );
    }
  },

  /**
   * Builds an L10nFileSource for the newtab Fluent files with a given name
   * and metasource, using the current intersection of the XPI's supported
   * locales and the locales known to the L10nRegistry.
   *
   * @param {string} name
   *   The source name. Must be unique per metasource.
   * @param {string} metasource
   *   The metasource the source belongs to. "app" for the primary source,
   *   a langpack id for shadow sources.
   * @returns {L10nFileSource}
   */
  _buildNewtabFileSource(name, metasource) {
    let availableLocales = new Set(Services.locale.availableLocales);
    let availableSupportedLocales =
      this._supportedLocales.intersection(availableLocales);
    return new L10nFileSource(
      name,
      metasource,
      [...availableSupportedLocales],
      `resource://newtab/locales/{locale}/`
    );
  },

  /**
   * Registers a shadow L10nFileSource for the newtab XPI inside a langpack's
   * metasource. This lets the L10nRegistry solver produce a bundle where the
   * langpack provides every required resource in the active locale except
   * browser/newtab/newtab.ftl, which is satisfied by the train-hopped XPI's
   * (possibly newer) copy. Without this, train-hop-only strings — those
   * present in the XPI but not yet in any langpack — are unreachable in any
   * non-English locale and fall through to en-US. No-op if a shadow is
   * already registered for this langpack. See Bug 2046945.
   *
   * @param {string} langpackId
   *   The langpack's L10nRegistry metasource string, as stored on
   *   `Langpack.langpackId`. This is NOT the langpack add-on's `id` —
   *   its shape is `langpack-${manifest.langpack_id}-${productCodeName}`,
   *   e.g. "langpack-es-ES-browser" on desktop Firefox.
   */
  _registerLangpackShadow(langpackId) {
    if (this._langpackShadowSources.has(langpackId)) {
      return;
    }
    const sourceName = `${FLUENT_SOURCE_NAME}-${langpackId}`;
    const shadowSource = this._buildNewtabFileSource(sourceName, langpackId);
    L10nRegistry.getInstance().registerSources([shadowSource]);
    this._langpackShadowSources.add(langpackId);
    this.logger.debug(
      `Registered newtab shadow source in metasource ${langpackId}`
    );
  },

  /**
   * Removes a previously-registered shadow source for the given langpack.
   * Called when a langpack shuts down (is uninstalled or disabled) so we
   * don't leave an orphan source keeping the langpack's now-empty metasource
   * alive. No-op if no shadow was registered for this langpack.
   *
   * @param {string} langpackId
   *   The langpack metasource string whose shadow source should be removed.
   *   Same shape as the argument to `_registerLangpackShadow`.
   */
  _unregisterLangpackShadow(langpackId) {
    if (!this._langpackShadowSources.has(langpackId)) {
      return;
    }
    const sourceName = `${FLUENT_SOURCE_NAME}-${langpackId}`;
    L10nRegistry.getInstance().removeSources([sourceName]);
    this._langpackShadowSources.delete(langpackId);
    this.logger.debug(
      `Removed newtab shadow source from metasource ${langpackId}`
    );
  },

  /**
   * Re-issues every currently-registered shadow source against the current
   * intersection of the XPI's supported locales and the L10nRegistry's known
   * available locales. Called on `intl:app-locales-changed` so shadow sources
   * track locale-set changes the same way the primary "app" source does.
   */
  _updateLangpackShadows() {
    let registry = L10nRegistry.getInstance();
    for (const langpackId of this._langpackShadowSources) {
      const sourceName = `${FLUENT_SOURCE_NAME}-${langpackId}`;
      registry.updateSources([
        this._buildNewtabFileSource(sourceName, langpackId),
      ]);
    }
  },

  observe(subject, topic, _data) {
    // The TOPIC_LOCALES_CHANGED, TOPIC_LANGPACK_STARTUP, and
    // TOPIC_LANGPACK_SHUTDOWN handlers all call L10nRegistry.{register,
    // update,remove}Sources, and each of those calls fires
    // intl:l10n-sources-changed (and, because LocaleService::
    // SetAvailableLocales compares as an order-sensitive nsTArray against
    // an array built from a Rust HashSet, spuriously fires
    // intl:app-locales-changed too). Without this guard, those broadcasts can
    // re-enter observe synchronously and the resulting cascade can pump
    // unbounded source mutations through the registry at startup,
    // exploding memory on installations with downloaded langpacks
    // (Flatpak/Snap/MSIX). The first invocation does its work; any
    // synchronous re-entries during that work are coalesced away. See
    // Bug 2049845.
    if (this._inObserveHandler) {
      return;
    }
    this._inObserveHandler = true;
    try {
      switch (topic) {
        case TOPIC_LOCALES_CHANGED: {
          this._updateFluentSourcesRegistration();
          this._updateLangpackShadows();
          break;
        }
        case TOPIC_LANGPACK_STARTUP: {
          const langpackId = subject?.wrappedJSObject?.langpack?.langpackId;
          if (langpackId) {
            this._registerLangpackShadow(langpackId);
          }
          break;
        }
        case TOPIC_LANGPACK_SHUTDOWN: {
          const langpackId = subject?.wrappedJSObject?.langpack?.langpackId;
          if (langpackId) {
            this._unregisterLangpackShadow(langpackId);
          }
          break;
        }
        case TOPIC_SHUTDOWN: {
          Services.obs.removeObserver(this, TOPIC_LOCALES_CHANGED);
          Services.obs.removeObserver(this, TOPIC_SHUTDOWN);
          Services.obs.removeObserver(this, TOPIC_LANGPACK_STARTUP);
          Services.obs.removeObserver(this, TOPIC_LANGPACK_SHUTDOWN);
          break;
        }
      }
    } finally {
      this._inObserveHandler = false;
    }
  },

  /**
   * Registers any dynamic Glean metrics that have been included with the XPI
   * version of the add-on.
   */
  registerMetricsFromJson() {
    // The metrics we need to process were placed in webext-glue/metrics/runtime-metrics-<version>.json
    // That file will be generated by build script getting implemented with Bug 1960111
    const version = AppConstants.MOZ_APP_VERSION.match(/\d+/)[0];
    const metricsPath = `resource://newtab/webext-glue/metrics/runtime-metrics-${version}.json`;
    this.logger.debug(`Registering FOG Glean metrics from ${metricsPath}`);
    lazy.NewTabGleanUtils.registerMetricsAndPings(metricsPath);
  },

  /**
   * Returns true once the application has started to shut down.
   *
   * Train-hop installs must not be started or continued past this point.
   *
   * @returns {boolean}
   */
  _isAppShuttingDown() {
    return (
      Services.startup.attemptingQuit ||
      Services.startup.isInOrBeyondShutdownPhase(
        Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
      )
    );
  },

  scheduleUpdateTrainhopAddonState() {
    if (!this._updateAddonStateDeferredTask) {
      this.logger.debug("creating _updateAddonStateDeferredTask");
      const delayMs = lazy.trainhopAddonScheduledUpdateDelay;
      const idleTimeoutMs = lazy.trainhopAddonScheduledUpdateTimeout;
      this._updateAddonStateDeferredTask = new lazy.DeferredTask(
        async () => {
          if (this._isAppShuttingDown()) {
            this.logger.warn(
              "updateAddonStateDeferredTask cancelled on application shutdown"
            );
            return;
          }
          this.logger.debug("_updateAddonStateDeferredTask running");
          await this.updateTrainhopAddonState().catch(e => {
            this.logger.warn("updateAddonStateDeferredTask failure", e);
          });
          this.logger.debug("_updateAddonStateDeferredTask completed");
        },
        delayMs,
        idleTimeoutMs
      );
      lazy.NimbusFeatures[TRAINHOP_NIMBUS_DEPLOYMENT_FEATURE_ID].onUpdate(() =>
        this._updateAddonStateDeferredTask.arm()
      );
    }
    this.logger.debug("re-arming _updateAddonStateDeferredTask");
    this._updateAddonStateDeferredTask.arm();
  },

  /**
   * Updates the state of the train-hop add-on based on the Nimbus feature variables.
   *
   * @returns {Promise<void>}
   *   Resolves once the train-hop add-on has been staged to be installed or uninstalled (e.g.
   *   when the client has been unenrolled from the Nimbus feature), or after it has been
   *   determined that no action was needed (e.g. while running in safemode, or if the same
   *   or an higher add-on version than the train-hop add-on version is already in use,
   *   installed or pending to be installed). Rejects on failures or unexpected cancellations
   *   during installation or uninstallation process.
   *
   *   The winning enrollment is chosen across all active co-enrollments of the
   *   newtabTrainhopAddonDeployment feature, keeping the highest addon_version. The
   *   front-end records the winning version in its own pref
   *   (browser.newtabpage.trainhopAddonDeployment.version), and early startup keeps
   *   the train-hop XPI while that pref is non-empty.
   *
   *   The highest currently-enrolled version always wins, including downgrades: if
   *   the highest-version rollout ends while a lower-version one is still active,
   *   the higher XPI is uninstalled and the lower version is installed instead (this
   *   is applied over the following startups, since the switch is decided at early
   *   startup). If nothing remains, the built-in takes over. This intentionally lets
   *   a rollout be rolled back by ending it (see Bug 1995391).
   */
  async updateTrainhopAddonState(forceRestartlessInstall = false) {
    if (this.inSafeMode) {
      this.logger.debug(
        "train-hop add-on update state disabled while running in SafeMode"
      );
      return;
    }

    // Pick the active co-enrollment of the deployment feature with the highest
    // addon_version.
    const feature = lazy.NimbusFeatures[TRAINHOP_NIMBUS_DEPLOYMENT_FEATURE_ID];
    await feature.ready();
    let winningEnrollment = null;
    for (const enrollment of feature.getAllEnrollments()) {
      const { addon_version } = enrollment.value;
      if (!addon_version) {
        continue;
      }
      if (
        !winningEnrollment ||
        Services.vc.compare(
          addon_version,
          winningEnrollment.value.addon_version
        ) > 0
      ) {
        winningEnrollment = enrollment;
      }
    }

    const addon_version = winningEnrollment?.value.addon_version ?? null;
    const xpi_download_path =
      winningEnrollment?.value.xpi_download_path ?? null;

    // Record the winning version in the deployment pref (owned exclusively by the
    // front-end). It only ever holds an empty string or a version that came from
    // a Nimbus train-hop enrollment, never the built-in add-on version. This pref
    // tracks the current winner and can decrease, which is what drives a downgrade
    // when a higher-version rollout ends.
    if (addon_version) {
      Services.prefs.setCharPref(
        TRAINHOP_XPI_DEPLOYMENT_VERSION_PREF,
        addon_version
      );
    } else if (
      lazy.trainhopAddonDeploymentXPIVersion !== TRAINHOP_ANY_VERSION_SENTINEL
    ) {
      Services.prefs.clearUserPref(TRAINHOP_XPI_DEPLOYMENT_VERSION_PREF);
    }

    this.logger.debug("Force restartless install: ", forceRestartlessInstall);
    this.logger.debug("Received addon version:", addon_version);
    this.logger.debug("Received XPI download path:", xpi_download_path);

    let addon = await lazy.AddonManager.getAddonByID(BUILTIN_ADDON_ID);

    // Uninstall train-hop add-on xpi if its resources are not currently
    // being used and the client has been unenrolled from the
    // newtabTrainhopAddonDeployment Nimbus feature.
    if (!this._addonIsXPI && addon) {
      let changed = false;
      if (addon_version === null && xpi_download_path === null) {
        changed ||= await this.uninstallAddon({
          uninstallReason:
            "uninstalling train-hop add-on version on Nimbus feature unenrolled",
        });
        return;
      }

      if (
        this._builtinVersion &&
        Services.vc.compare(this._builtinVersion, addon.version) >= 0
      ) {
        changed ||= await this.uninstallAddon({
          uninstallReason:
            "uninstalling train-hop add-on version on builtin add-on with equal or higher version",
        });
      }

      // Downgrade: the installed train-hop XPI is higher than the version the
      // client is currently entitled to (the higher-version rollout ended while a
      // lower-version one is still active). Uninstall it so the lower winning
      // version can be installed below (or the built-in takes over if none remains).
      if (
        addon_version !== null &&
        Services.vc.compare(addon.version, addon_version) > 0
      ) {
        changed ||= await this.uninstallAddon({
          uninstallReason:
            "uninstalling higher train-hop add-on version to downgrade to the highest currently-enrolled version",
        });
      }

      if (
        lazy.AddonSettings.REQUIRE_SIGNING &&
        addon.signedState !== lazy.AddonManager.SIGNEDSTATE_SYSTEM
      ) {
        changed ||= await this.uninstallAddon({
          uninstallReason: `uninstall train-hop add-on xpi on unexpected signedState ${addon.signedState} (expected ${lazy.AddonManager.SIGNEDSTATE_SYSTEM})`,
        });
      }

      // Retrieve the new add-on wrapper if the xpi version has been uninstalled.
      if (changed) {
        addon = await lazy.AddonManager.getAddonByID(BUILTIN_ADDON_ID);

        this.logger.debug(
          "Invalidating AboutHomeStartupCache after train-hop uninstall"
        );
        lazy.AboutHomeStartupCache.clearCacheAndUninit();
      }
    }

    // Record the Nimbus exposure event if NewTab is currently using the
    // resources from the train-hop add-on version.
    if (
      this._addonIsXPI &&
      this._addonVersion === addon_version &&
      winningEnrollment
    ) {
      this.logger.debug(
        `train-hop add-on version ${addon_version} already in use`
      );
      feature.recordExposureEvent({
        once: true,
        slug: winningEnrollment.meta.slug,
      });
      return;
    }

    // Verify if the train-hop add-on version is already installed.
    if (addon?.version === addon_version) {
      this.logger.warn(
        `train-hop add-on version ${addon_version} already installed but not in use`
      );
      return;
    }

    if (!lazy.trainhopAddonXPIBaseURL) {
      this.logger.debug(
        "train-hop add-on download disabled on empty download base URL"
      );
      return;
    }

    if (addon_version == null && xpi_download_path == null) {
      this.logger.debug("train-hop cancelled: client not enrolled");
      return;
    }

    if (addon_version == null) {
      this.logger.warn("train-hop failure: missing mandatory addon_version");
      return;
    }

    if (xpi_download_path == null) {
      this.logger.warn(
        "train-hop failure: missing mandatory xpi_download_path"
      );
      return;
    }

    const xpiDownloadURL = `${lazy.trainhopAddonXPIBaseURL}${xpi_download_path}`;
    await this._installTrainhopAddon({
      trainhopAddonVersion: addon_version,
      xpiDownloadURL,
      forceRestartlessInstall,
    });
  },

  /**
   * Downloads and installs the newtab train-hop add-on version based on Nimbus feature configuration,
   * or record the Nimbus feature exposure event if the newtab train-hop add-on version is already in use.
   *
   * @param {object} params
   * @param {string} params.trainhopAddonVersion - The version of the train-hop add-on to install.
   * @param {string} params.xpiDownloadURL - The URL from which to download the XPI file.
   * @param {boolean} params.forceRestartlessInstall
   *   After the XPI is downloaded, attempt to complete a restartless install. Note that if
   *   AboutNewTabResourceMapping.init has been called by the time the XPI has finished
   *   downloading, this directive is ignored, and we fallback to installing on the next
   *   restart.
   *
   * @returns {Promise<void>}
   *   Resolves when the train-hop add-on installation is completed or not needed, or rejects
   *   on failures or unexpected cancellations hit during the installation process.
   */
  async _installTrainhopAddon({
    trainhopAddonVersion,
    xpiDownloadURL,
    forceRestartlessInstall,
  }) {
    if (
      this._builtinVersion &&
      Services.vc.compare(this._builtinVersion, trainhopAddonVersion) >= 0
    ) {
      this.logger.warn(
        `cancel xpi download on train-hop add-on version ${trainhopAddonVersion} on equal or higher builtin version ${this._builtinVersion}`
      );
      return;
    }

    let addon = await lazy.AddonManager.getAddonByID(BUILTIN_ADDON_ID);
    if (
      addon?.version &&
      Services.vc.compare(addon.version, trainhopAddonVersion) >= 0
    ) {
      this.logger.warn(
        `cancel xpi download on train-hop add-on version ${trainhopAddonVersion} on equal or higher version ${addon.version} already installed`
      );
      return;
    }

    // Verify if there is already a pending install for the same or higher add-on version
    // (in case of multiple pending installations for the same add-on id, the last one wins).
    let pendingInstall = (await lazy.AddonManager.getAllInstalls())
      .filter(
        install =>
          install.addon?.id === BUILTIN_ADDON_ID &&
          install.state === lazy.AddonManager.STATE_POSTPONED
      )
      .pop();
    if (
      pendingInstall &&
      Services.vc.compare(pendingInstall.addon.version, trainhopAddonVersion) >=
        0
    ) {
      this.logger.debug(
        `cancel xpi download on train-hop add-on version ${trainhopAddonVersion} on equal or higher versions ${pendingInstall.addon.version} install already in progress`
      );
      return;
    }
    this.logger.log(
      `downloading train-hop add-on version ${trainhopAddonVersion} from ${xpiDownloadURL}`
    );
    try {
      if (this._isAppShuttingDown()) {
        this.logger.warn(
          "cancel xpi download on application shutdown already initiated"
        );
        return;
      }
      let newInstall = await lazy.AddonManager.getInstallForURL(
        xpiDownloadURL,
        {
          id: BUILTIN_ADDON_ID,
          type: "extension",
          version: trainhopAddonVersion,
          // Categorizes the install as coming from the Nimbus newtab train-hop
          // mechanism. This source id is kept stable for historical continuity
          // (it predates the co-enrollment newtabTrainhopAddonDeployment feature).
          telemetryInfo: { source: "nimbus-newtabTrainhopAddon" },
        }
      );
      const deferred = Promise.withResolvers();
      newInstall.addListener({
        onDownloadEnded: () => {
          if (
            newInstall.addon.id !== BUILTIN_ADDON_ID ||
            newInstall.addon.version !== trainhopAddonVersion
          ) {
            deferred.reject(
              new Error(
                `train-hop add-on install cancelled on mismatching add-on version` +
                  `(actual ${newInstall.addon.version}, expected ${trainhopAddonVersion})`
              )
            );
            newInstall.cancel();
          }

          if (
            lazy.AddonSettings.REQUIRE_SIGNING &&
            newInstall.addon.signedState !==
              lazy.AddonManager.SIGNEDSTATE_SYSTEM
          ) {
            deferred.reject(
              new Error(
                `trainhop add-on install cancelled on invalid signed state` +
                  `(actual ${newInstall.addon.signedState}, expected ${lazy.AddonManager.SIGNEDSTATE_SYSTEM})`
              )
            );
            newInstall.cancel();
          }

          this.logger.debug("Train-hop download ended");
        },
        onInstallPostponed: () => {
          this.logger.debug("Train-hop install postponed, as expected");
          const isAppShuttingDown = this._isAppShuttingDown();
          if (
            forceRestartlessInstall &&
            !this.initialized &&
            !isAppShuttingDown
          ) {
            this.logger.debug("Forcing restartless install of train-hop");
            newInstall.continuePostponedInstall();
          } else {
            this.logger.debug("Not forcing restartless install");
            if (forceRestartlessInstall) {
              this.logger.warn(
                isAppShuttingDown
                  ? "Application shutdown already initiated, leaving the train-hop install staged."
                  : "We must have initialized before the XPI finished downloading."
              );
            }
            deferred.resolve();
          }
        },
        onInstallEnded: () => {
          this.logger.debug("Train-hop restartless install ended");
          if (forceRestartlessInstall) {
            this.logger.debug("Resolving train-hop install promise");
            deferred.resolve();
          }
        },
        onDownloadCancelled: () => {
          deferred.reject(
            new Error(
              `Unexpected download cancelled while downloading xpi from ${xpiDownloadURL}`
            )
          );
        },
        onDownloadFailed: () => {
          deferred.reject(
            new Error(`Failed to download xpi from ${xpiDownloadURL}`)
          );
        },
        onInstallCancelled: () => {
          deferred.reject(
            new Error(
              `Unexpected install cancelled while installing xpi from ${xpiDownloadURL}`
            )
          );
        },
        onInstallFailed: () => {
          deferred.reject(
            new Error(`Failed to install xpi from ${xpiDownloadURL}`)
          );
        },
      });
      newInstall.install();
      await deferred.promise;

      if (forceRestartlessInstall) {
        this.logger.debug(
          `train-hop add-on ${trainhopAddonVersion} downloaded and we will attempt a restartless install`
        );
      } else {
        this.logger.debug(
          `train-hop add-on ${trainhopAddonVersion} downloaded and pending install on next startup`
        );
      }
    } catch (e) {
      this.logger.error(`train-hop add-on install failure: ${e}`);
    }
  },

  /**
   * Uninstalls the newtab add-on, if it exists and has the PERM_CAN_UNINSTALL permission,
   * optionally logs a reason for the add-on being uninstalled.
   *
   * @param {object} params
   * @param {string} [params.uninstallReason]
   *   Reason for uninstalling the add-on to log along with uninstalling
   *   the add-on.
   *
   * @returns {Promise<boolean>}
   *   Resolves once the add-on is uninstalled, if it was found and had the
   *   PERM_CAN_UNINSTALL permission, with a boolean set to true if the
   *   add-on was found and uninstalled.
   */
  async uninstallAddon({ uninstallReason } = {}) {
    let addon = await lazy.AddonManager.getAddonByID(BUILTIN_ADDON_ID);
    if (addon && addon.permissions & lazy.AddonManager.PERM_CAN_UNINSTALL) {
      if (uninstallReason) {
        this.logger.info(uninstallReason);
      }
      await addon.uninstall();
      return true;
    }
    return false;
  },

  /**
   * This is registered to be called on first startup for new profiles on
   * Windows. It is expected to be called very early on in the lifetime of
   * new profiles, such that the AboutNewTabResourceMapping.init routine has
   * not yet had a chance to run.
   *
   * @returns {Promise<void>}
   */
  async firstStartupNewProfile() {
    if (this.initialized) {
      this.logger.error(
        "firstStartupNewProfile is being run after AboutNewTabResourceMapping initializes, so we're too late."
      );
      return;
    }
    this.logger.debug(
      "First startup with a new profile. Checking for any train-hops to perform restartless install."
    );
    await lazy.ExperimentAPI.ready();

    const nimbusFeature =
      lazy.NimbusFeatures[TRAINHOP_NIMBUS_FIRST_STARTUP_FEATURE_ID];
    await nimbusFeature.ready();
    const { enabled } = nimbusFeature.getAllVariables({
      defaultValues: { enabled: true },
    });
    if (!enabled) {
      // We've been configured to bypass the FirstStartup install for
      // train-hops, so exit now.
      this.logger.debug(
        "Not forcing install of any newtab XPIs, as we're currently configured not to."
      );
      return;
    }

    await lazy.AddonManager.readyPromise;
    await this.updateTrainhopAddonState(true /* forceRestartlessInstall */);

    this.logger.debug("First startup - new profile done");
  },
};

AboutNewTabResourceMapping.logger = console.createInstance({
  logModule: "AboutNewTabResourceMapping",
  maxLogLevel: Services.prefs.getBoolPref(
    "browser.newtabpage.resource-mapping.log",
    false
  )
    ? "Debug"
    : "Warn",
});
