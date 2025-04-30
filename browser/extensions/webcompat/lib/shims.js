/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* globals browser, module, onMessageFromTab */

// To grant shims access to bundled logo images without risking
// exposing our moz-extension URL, we have the shim request them via
// nonsense URLs which we then redirect to the actual files (but only
// on tabs where a shim using a given logo happens to be active).
const LogosBaseURL = "https://smartblock.firefox.etp/";

const releaseBranchPromise = browser.appConstants.getReleaseBranch();

const platformPromise = browser.runtime.getPlatformInfo().then(info => {
  return info.os === "android" ? "android" : "desktop";
});

let debug = async function () {
  if ((await releaseBranchPromise) !== "release_or_beta") {
    console.debug.apply(this, arguments);
  }
};
let error = async function () {
  if ((await releaseBranchPromise) !== "release_or_beta") {
    console.error.apply(this, arguments);
  }
};
let warn = async function () {
  if ((await releaseBranchPromise) !== "release_or_beta") {
    console.warn.apply(this, arguments);
  }
};

class Shim {
  constructor(opts, manager) {
    this.manager = manager;

    const { contentScripts, matches, unblocksOnOptIn } = opts;

    this.branches = opts.branches;
    this.bug = opts.bug;
    this.isGoogleTrendsDFPIFix = opts.custom == "google-trends-dfpi-fix";
    this.file = opts.file;
    this.hiddenInAboutCompat = opts.hiddenInAboutCompat;
    this.hosts = opts.hosts;
    this.id = opts.id;
    this.logos = opts.logos || [];
    this.matches = [];
    this.name = opts.name;
    this.notHosts = opts.notHosts;
    this.onlyIfBlockedByETP = opts.onlyIfBlockedByETP;
    this.onlyIfDFPIActive = opts.onlyIfDFPIActive;
    this.onlyIfPrivateBrowsing = opts.onlyIfPrivateBrowsing;
    this._options = opts.options || {};
    this.webExposedShimHelpers = opts.webExposedShimHelpers;
    this.needsShimHelpers = opts.needsShimHelpers;
    this.platform = opts.platform || "all";
    this.runFirst = opts.runFirst;
    this.unblocksOnOptIn = unblocksOnOptIn;
    this.requestStorageAccessForRedirect = opts.requestStorageAccessForRedirect;
    this.shouldUseScriptingAPI =
      browser.aboutConfigPrefs.getBoolPrefSync("useScriptingAPI");
    this.isSmartblockEmbedShim = opts.isSmartblockEmbedShim || false;
    debug(
      `WebCompat Shim ${this.id} will be injected using ${
        this.shouldUseScriptingAPI ? "scripting" : "contentScripts"
      } API`
    );

    this._hostOptIns = new Set();
    this._pBModeHostOptIns = new Set();

    this._disabledByConfig = opts.disabled;
    this._disabledGlobally = false;
    this._disabledForSession = false;
    this._disabledByPlatform = false;
    this._disabledByReleaseBranch = false;
    this._disabledBySmartblockEmbedPref = false;

    this._activeOnTabs = new Set();
    this._showedOptInOnTabs = new Set();

    const pref = `disabled_shims.${this.id}`;

    this.redirectsRequests = !!this.file && matches?.length;

    // NOTE: _contentScriptRegistrations is an array of string ids when
    // shouldUseScriptingAPI is true and an array of script handles returned
    // by contentScripts.register otherwise.
    this._contentScriptRegistrations = [];

    this.contentScripts = contentScripts || [];
    for (const script of this.contentScripts) {
      if (typeof script.css === "string") {
        script.css = [
          this.shouldUseScriptingAPI
            ? `/shims/${script.css}`
            : { file: `/shims/${script.css}` },
        ];
      }
      if (typeof script.js === "string") {
        script.js = [
          this.shouldUseScriptingAPI
            ? `/shims/${script.js}`
            : { file: `/shims/${script.js}` },
        ];
      }
    }

    for (const match of matches || []) {
      if (!match.types) {
        this.matches.push({ patterns: [match], types: ["script"] });
      } else {
        this.matches.push(match);
      }
      if (match.target) {
        this.redirectsRequests = true;
      }
    }

    browser.aboutConfigPrefs.onPrefChange.addListener(async () => {
      const value = await browser.aboutConfigPrefs.getPref(pref);
      this._disabledPrefValue = value;
      this._onEnabledStateChanged({ alsoClearResourceCache: true });
    }, pref);

    this.ready = Promise.all([
      browser.aboutConfigPrefs.getPref(pref),
      platformPromise,
      releaseBranchPromise,
    ]).then(([disabledPrefValue, platform, branch]) => {
      this._disabledPrefValue = disabledPrefValue;

      this._disabledByPlatform =
        this.platform !== "all" && this.platform !== platform;

      this._disabledByReleaseBranch = false;
      for (const supportedBranchAndPlatform of this.branches || []) {
        const [supportedBranch, supportedPlatform] =
          supportedBranchAndPlatform.split(":");
        if (
          (!supportedPlatform || supportedPlatform == platform) &&
          supportedBranch != branch
        ) {
          this._disabledByReleaseBranch = true;
        }
      }

      this._preprocessOptions(platform, branch);
      this._onEnabledStateChanged();
    });
  }

  _preprocessOptions(platform, branch) {
    // options may be any value, but can optionally be gated for specified
    // platform/branches, if in the format `{value, branches, platform}`
    this.options = {};
    for (const [k, v] of Object.entries(this._options)) {
      if (v?.value) {
        if (
          (!v.platform || v.platform === platform) &&
          (!v.branches || v.branches.includes(branch))
        ) {
          this.options[k] = v.value;
        }
      } else {
        this.options[k] = v;
      }
    }
  }

  get enabled() {
    if (this._disabledGlobally || this._disabledForSession) {
      return false;
    }

    if (this._disabledPrefValue !== undefined) {
      return !this._disabledPrefValue;
    }

    if (this.isSmartblockEmbedShim && this._disabledBySmartblockEmbedPref) {
      return false;
    }

    return (
      !this._disabledByConfig &&
      !this._disabledByPlatform &&
      !this._disabledByReleaseBranch
    );
  }

  get disabledReason() {
    if (this._disabledGlobally) {
      return "globalPref";
    }

    if (this._disabledForSession) {
      return "session";
    }

    if (this._disabledPrefValue !== undefined) {
      if (this._disabledPrefValue === true) {
        return "pref";
      }
      return false;
    }

    if (this.isSmartblockEmbedShim && this._disabledBySmartblockEmbedPref) {
      return "smartblockEmbedDisabledByPref";
    }

    if (this._disabledByConfig) {
      return "config";
    }

    if (this._disabledByPlatform) {
      return "platform";
    }

    if (this._disabledByReleaseBranch) {
      return "releaseBranch";
    }

    return false;
  }

  get disabledBySmartblockEmbedPref() {
    return this._disabledBySmartblockEmbedPref;
  }

  set disabledBySmartblockEmbedPref(value) {
    this._disabledBySmartblockEmbedPref = value;
    this._onEnabledStateChanged({ alsoClearResourceCache: true });
  }

  onAllShimsEnabled() {
    const wasEnabled = this.enabled;
    this._disabledGlobally = false;
    if (!wasEnabled) {
      this._onEnabledStateChanged({ alsoClearResourceCache: true });
    }
  }

  onAllShimsDisabled() {
    const wasEnabled = this.enabled;
    this._disabledGlobally = true;
    if (wasEnabled) {
      this._onEnabledStateChanged({ alsoClearResourceCache: true });
    }
  }

  enableForSession() {
    const wasEnabled = this.enabled;
    this._disabledForSession = false;
    if (!wasEnabled) {
      this._onEnabledStateChanged({ alsoClearResourceCache: true });
    }
  }

  disableForSession() {
    const wasEnabled = this.enabled;
    this._disabledForSession = true;
    if (wasEnabled) {
      this._onEnabledStateChanged({ alsoClearResourceCache: true });
    }
  }

  async _onEnabledStateChanged({ alsoClearResourceCache = false } = {}) {
    this.manager?.onShimStateChanged(this.id);
    if (!this.enabled) {
      await this._unregisterContentScripts();
      await this._revokeRequestsInETP(alsoClearResourceCache);
      return browser.testUtils.shimsInactive();
    }
    await this._registerContentScripts();
    await this._allowRequestsInETP(alsoClearResourceCache);
    return browser.testUtils.shimsActive();
  }

  async _registerContentScripts() {
    if (
      this.contentScripts.length &&
      !this._contentScriptRegistrations.length
    ) {
      const matches = [];
      let idx = 0;
      for (const options of this.contentScripts) {
        matches.push(options.matches);
        if (this.shouldUseScriptingAPI) {
          // Some shims includes more than one script (e.g. Blogger one contains
          // a content script to be run on document_start and one to be run
          // on document_end.
          options.id = `shim-${this.id}-${idx++}`;
          options.persistAcrossSessions = false;
          // Having to call getRegisteredContentScripts each time we are going to
          // register a Shim content script is suboptimal, but avoiding that
          // may require a bit more changes (e.g. rework both Injections, Shim and Shims
          // classes to more easily register all content scripts with a single
          // call to the scripting API methods when the background script page is loading
          // and one per injection or shim being enabled from the AboutCompatBroker).
          // In the short term we call getRegisteredContentScripts and restrict it to
          // the script id we are about to register.
          let isAlreadyRegistered = false;
          try {
            const registeredScripts =
              await browser.scripting.getRegisteredContentScripts({
                ids: [options.id],
              });
            isAlreadyRegistered = !!registeredScripts.length;
          } catch (ex) {
            console.error(
              "Retrieve WebCompat GoFaster registered content scripts failed: ",
              ex
            );
          }
          try {
            if (!isAlreadyRegistered) {
              await browser.scripting.registerContentScripts([options]);
            }
            this._contentScriptRegistrations.push(options.id);
          } catch (ex) {
            console.error(
              "Registering WebCompat Shim content scripts failed: ",
              options,
              ex
            );
          }
        } else {
          const reg = await browser.contentScripts.register(options);
          this._contentScriptRegistrations.push(reg);
        }
      }
      const urls = Array.from(new Set(matches.flat()));
      debug("Enabling content scripts for these URLs:", urls);
    }
  }

  async _unregisterContentScripts() {
    if (this.shouldUseScriptingAPI) {
      const ids = this._contentScriptRegistrations;
      await browser.scripting.unregisterContentScripts({ ids });
    } else {
      for (const registration of this._contentScriptRegistrations) {
        registration.unregister();
      }
    }
    this._contentScriptRegistrations = [];
  }

  async _allowRequestsInETP(alsoClearResourceCache) {
    let modified = false;
    const matches = this.matches.map(m => m.patterns).flat();
    if (matches.length) {
      // ensure requests shimmed in both PB and non-PB modes
      await browser.trackingProtection.shim(this.id, matches);
      modified = true;
    }

    if (this._hostOptIns.size) {
      const optIns = this.getApplicableOptIns();
      if (optIns.length) {
        await browser.trackingProtection.allow(
          this.id,
          this._optInPatterns,
          false,
          Array.from(this._hostOptIns)
        );
        modified = true;
      }
    }

    if (this._pBModeHostOptIns.size) {
      const optIns = this.getApplicableOptIns();
      if (optIns.length) {
        await browser.trackingProtection.allow(
          this.id,
          this._optInPatterns,
          true,
          Array.from(this._pBModeHostOptIns)
        );
        modified = true;
      }
    }

    if (this._haveCheckedEnabledPrefs && alsoClearResourceCache && modified) {
      this.clearResourceCache();
    }
  }

  async _revokeRequestsInETP(alsoClearResourceCache) {
    await browser.trackingProtection.revoke(this.id);
    if (this._haveCheckedEnabledPrefs && alsoClearResourceCache) {
      this.clearResourceCache();
    }
  }

  setActiveOnTab(tabId, active = true) {
    if (active) {
      this._activeOnTabs.add(tabId);
    } else {
      this._activeOnTabs.delete(tabId);
      this._showedOptInOnTabs.delete(tabId);
    }
  }

  isActiveOnTab(tabId) {
    return this._activeOnTabs.has(tabId);
  }

  meantForHost(host) {
    const { hosts, notHosts } = this;
    if (hosts || notHosts) {
      if (
        (notHosts && notHosts.includes(host)) ||
        (hosts && !hosts.includes(host))
      ) {
        return false;
      }
    }
    return true;
  }

  async unblocksURLOnOptIn(url) {
    if (!this._optInPatterns) {
      this._optInPatterns = await this.getApplicableOptIns();
    }

    if (!this._optInMatcher) {
      this._optInMatcher = browser.matchPatterns.getMatcher(
        Array.from(this._optInPatterns)
      );
    }

    return this._optInMatcher.matches(url);
  }

  isTriggeredByURLAndType(url, type) {
    for (const entry of this.matches || []) {
      if (!entry.types.includes(type)) {
        continue;
      }
      if (!entry.matcher) {
        entry.matcher = browser.matchPatterns.getMatcher(
          Array.from(entry.patterns)
        );
      }
      if (entry.matcher.matches(url)) {
        return entry;
      }
    }

    return undefined;
  }

  async getApplicableOptIns() {
    if (this._applicableOptIns) {
      return this._applicableOptIns;
    }
    const optins = [];
    for (const unblock of this.unblocksOnOptIn || []) {
      if (typeof unblock === "string") {
        optins.push(unblock);
        continue;
      }
      const { branches, patterns, platforms } = unblock;
      if (platforms?.length) {
        const platform = await platformPromise;
        if (platform !== "all" && !platforms.includes(platform)) {
          continue;
        }
      }
      if (branches?.length) {
        const branch = await releaseBranchPromise;
        if (!branches.includes(branch)) {
          continue;
        }
      }
      optins.push.apply(optins, patterns);
    }
    this._applicableOptIns = optins;
    return optins;
  }

  async onUserOptIn(host, isPrivateMode) {
    const optins = await this.getApplicableOptIns();
    const activeHostOptIns = isPrivateMode
      ? this._pBModeHostOptIns
      : this._hostOptIns;
    if (optins.length) {
      activeHostOptIns.add(host);
      await browser.trackingProtection.allow(
        this.id,
        optins,
        isPrivateMode,
        Array.from(activeHostOptIns)
      );
      this.clearResourceCache();
    }
  }

  hasUserOptedInAlready(host, isPrivateMode) {
    const activeHostOptIns = isPrivateMode
      ? this._pBModeHostOptIns
      : this._hostOptIns;
    return activeHostOptIns.has(host);
  }

  showOptInWarningOnce(tabId, origin) {
    if (this._showedOptInOnTabs.has(tabId)) {
      return Promise.resolve();
    }
    this._showedOptInOnTabs.add(tabId);

    const { bug, name } = this;
    const warning = `${name} is allowed on ${origin} for this browsing session due to user opt-in. See https://bugzilla.mozilla.org/show_bug.cgi?id=${bug} for details.`;
    return browser.tabs
      .executeScript(tabId, {
        code: `console.warn(${JSON.stringify(warning)})`,
        runAt: "document_start",
      })
      .catch(() => {});
  }

  async onUserOptOut(host, isPrivateMode) {
    const optIns = await this.getApplicableOptIns();
    const activeHostOptIns = isPrivateMode
      ? this._pBModeHostOptIns
      : this._hostOptIns;
    if (optIns.length) {
      activeHostOptIns.delete(host);
      await browser.trackingProtection.allow(
        this.id,
        optIns,
        isPrivateMode,
        Array.from(activeHostOptIns)
      );
      this.clearResourceCache();
    }
  }

  async clearUserOptIns(forPrivateMode) {
    const optIns = await this.getApplicableOptIns();
    const activeHostOptIns = forPrivateMode
      ? this._pBModeHostOptIns
      : this._hostOptIns;
    if (optIns.length) {
      activeHostOptIns.clear();
      await browser.trackingProtection.allow(
        this.id,
        optIns,
        forPrivateMode,
        Array.from(activeHostOptIns)
      );
      this.clearResourceCache();
    }
  }

  clearResourceCache() {
    return browser.trackingProtection.clearResourceCache();
  }
}

class Shims {
  constructor(availableShims) {
    browser.testUtils.shimsInactive();

    if (!browser.trackingProtection) {
      console.error("Required experimental add-on APIs for shims unavailable");
      return;
    }

    this._registerShims(availableShims);

    onMessageFromTab(this._onMessageFromShim.bind(this));

    this.ENABLED_PREF = "enable_shims";
    browser.aboutConfigPrefs.onPrefChange.addListener(() => {
      this._checkEnabledPref();
    }, this.ENABLED_PREF);

    this.SMARTBLOCK_EMBEDS_ENABLED_PREF = `smartblockEmbeds.enabled`;
    browser.aboutConfigPrefs.onPrefChange.addListener(() => {
      this._checkSmartblockEmbedsEnabledPref();
    }, this.SMARTBLOCK_EMBEDS_ENABLED_PREF);

    // NOTE: Methods that uses the prefs should await
    //       _haveCheckedEnabledPrefsPromise, in order to make sure the
    //       prefs are all read.
    //       Methods that potentially clears the resource cache should check
    //       _haveCheckedEnabledPrefs, in order to avoid clearing the
    //       resource cache during the startup.
    this._haveCheckedEnabledPrefs = false;
    this._haveCheckedEnabledPrefsPromise = Promise.all([
      this._checkEnabledPref(),
      this._checkSmartblockEmbedsEnabledPref(),
    ]);
    this._haveCheckedEnabledPrefsPromise.then(() => {
      this._haveCheckedEnabledPrefs = true;
    });

    // handles unblock message coming in from protections panel
    browser.trackingProtection.onSmartBlockEmbedUnblock.addListener(
      async (tabId, shimId, hostname) => {
        const shim = this.shims.get(shimId);
        if (!shim) {
          console.warn("Smartblock shim not found", { tabId, shimId });
          return;
        }
        const isPB = (await browser.tabs.get(tabId)).incognito;
        await shim.onUserOptIn(hostname, isPB);

        // send request to shim to remove placeholders and replace with original embeds
        await browser.tabs.sendMessage(tabId, {
          shimId,
          topic: "smartblock:unblock-embed",
        });
      }
    );

    // handles reblock message coming in from protections panel
    browser.trackingProtection.onSmartBlockEmbedReblock.addListener(
      async (tabId, shimId, hostname) => {
        const shim = this.shims.get(shimId);
        if (!shim) {
          console.warn("Smartblock shim not found", { tabId, shimId });
          return;
        }
        const isPB = (await browser.tabs.get(tabId)).incognito;
        await shim.onUserOptOut(hostname, isPB);

        // a browser reload is required to reload the shim in the case where the shim gets unloaded
        // i.e. after user unblocks, then closes and revisits the page while shim is still allowed
        browser.tabs.reload(tabId);
      }
    );

    // handles data clearing on private browsing mode end
    browser.trackingProtection.onPrivateSessionEnd.addListener(() => {
      for (const shim of this.shims.values()) {
        shim.clearUserOptIns(true);
      }
    });
  }

  bindAboutCompatBroker(broker) {
    this._aboutCompatBroker = broker;
  }

  getShimInfoForAboutCompat(shim) {
    const { bug, disabledReason, hiddenInAboutCompat, id, name } = shim;
    const type = "smartblock";
    return { bug, disabledReason, hidden: hiddenInAboutCompat, id, name, type };
  }

  disableShimForSession(id) {
    const shim = this.shims.get(id);
    shim?.disableForSession();
  }

  enableShimForSession(id) {
    const shim = this.shims.get(id);
    shim?.enableForSession();
  }

  onShimStateChanged(id) {
    if (!this._aboutCompatBroker) {
      return;
    }

    const shim = this.shims.get(id);
    if (!shim) {
      return;
    }

    const shimsChanged = [this.getShimInfoForAboutCompat(shim)];
    this._aboutCompatBroker.portsToAboutCompatTabs.broadcast({ shimsChanged });
  }

  getAvailableShims() {
    const shims = Array.from(this.shims.values()).map(
      this.getShimInfoForAboutCompat
    );
    shims.sort((a, b) => a.name.localeCompare(b.name));
    return shims;
  }

  _registerShims(shims) {
    if (this.shims) {
      throw new Error("_registerShims has already been called");
    }

    this.shims = new Map();
    for (const shimOpts of shims) {
      const { id } = shimOpts;
      if (!this.shims.has(id)) {
        this.shims.set(shimOpts.id, new Shim(shimOpts, this));
      }
    }

    // Register onBeforeRequest listener which handles storage access requests
    // on matching redirects.
    let redirectTargetUrls = Array.from(shims.values())
      .filter(shim => shim.requestStorageAccessForRedirect)
      .flatMap(shim => shim.requestStorageAccessForRedirect)
      .map(([, dstUrl]) => dstUrl);

    // Unique target urls.
    redirectTargetUrls = Array.from(new Set(redirectTargetUrls));

    if (redirectTargetUrls.length) {
      debug("Registering redirect listener for requestStorageAccess helper", {
        redirectTargetUrls,
      });
      browser.webRequest.onBeforeRequest.addListener(
        this._onRequestStorageAccessRedirect.bind(this),
        { urls: redirectTargetUrls, types: ["main_frame"] },
        ["blocking"]
      );
    }

    function addTypePatterns(type, patterns, set) {
      if (!set.has(type)) {
        set.set(type, { patterns: new Set() });
      }
      const allSet = set.get(type).patterns;
      for (const pattern of patterns) {
        allSet.add(pattern);
      }
    }

    const allMatchTypePatterns = new Map();
    const allHeaderChangingMatchTypePatterns = new Map();
    const allLogos = [];
    for (const shim of this.shims.values()) {
      const { logos, matches } = shim;
      allLogos.push(...logos);
      for (const { patterns, target, types } of matches || []) {
        for (const type of types) {
          if (shim.isGoogleTrendsDFPIFix) {
            addTypePatterns(type, patterns, allHeaderChangingMatchTypePatterns);
          }
          if (target || shim.file || shim.runFirst) {
            addTypePatterns(type, patterns, allMatchTypePatterns);
          }
        }
      }
    }

    if (allLogos.length) {
      const urls = Array.from(new Set(allLogos)).map(l => {
        return `${LogosBaseURL}${l}`;
      });
      debug("Allowing access to these logos:", urls);
      const unmarkShimsActive = tabId => {
        for (const shim of this.shims.values()) {
          shim.setActiveOnTab(tabId, false);
        }
      };
      browser.tabs.onRemoved.addListener(unmarkShimsActive);
      browser.tabs.onUpdated.addListener((tabId, changeInfo) => {
        if (changeInfo.discarded || changeInfo.url) {
          unmarkShimsActive(tabId);
        }
      });
      browser.webRequest.onBeforeRequest.addListener(
        this._redirectLogos.bind(this),
        { urls, types: ["image"] },
        ["blocking"]
      );
    }

    if (allHeaderChangingMatchTypePatterns) {
      for (const [
        type,
        { patterns },
      ] of allHeaderChangingMatchTypePatterns.entries()) {
        const urls = Array.from(patterns);
        debug("Shimming these", type, "URLs:", urls);
        browser.webRequest.onBeforeSendHeaders.addListener(
          this._onBeforeSendHeaders.bind(this),
          { urls, types: [type] },
          ["blocking", "requestHeaders"]
        );
        browser.webRequest.onHeadersReceived.addListener(
          this._onHeadersReceived.bind(this),
          { urls, types: [type] },
          ["blocking", "responseHeaders"]
        );
      }
    }

    if (!allMatchTypePatterns.size) {
      debug("Skipping shims; none enabled");
      return;
    }

    for (const [type, { patterns }] of allMatchTypePatterns.entries()) {
      const urls = Array.from(patterns);
      debug("Shimming these", type, "URLs:", urls);

      browser.webRequest.onBeforeRequest.addListener(
        this._ensureShimForRequestOnTab.bind(this),
        { urls, types: [type] },
        ["blocking"]
      );
    }
  }

  async _checkEnabledPref() {
    await browser.aboutConfigPrefs.getPref(this.ENABLED_PREF).then(value => {
      if (value === undefined) {
        browser.aboutConfigPrefs.setPref(this.ENABLED_PREF, true);
      } else if (value === false) {
        this.enabled = false;
      } else {
        this.enabled = true;
      }
    });
  }

  get enabled() {
    return this._enabled;
  }

  set enabled(enabled) {
    if (enabled === this._enabled) {
      return;
    }

    this._enabled = enabled;

    for (const shim of this.shims.values()) {
      if (enabled) {
        shim.onAllShimsEnabled();
      } else {
        shim.onAllShimsDisabled();
      }
    }
  }

  async _checkSmartblockEmbedsEnabledPref() {
    await browser.aboutConfigPrefs
      .getPref(this.SMARTBLOCK_EMBEDS_ENABLED_PREF)
      .then(value => {
        if (value === undefined) {
          browser.aboutConfigPrefs.setPref(
            this.SMARTBLOCK_EMBEDS_ENABLED_PREF,
            true
          );
        } else if (value === false) {
          this.smartblockEmbedsEnabled = false;
        } else {
          this.smartblockEmbedsEnabled = true;
        }
      });
  }

  get smartblockEmbedsEnabled() {
    return this._smartblockEmbedsEnabled;
  }

  set smartblockEmbedsEnabled(value) {
    if (value === this._smartblockEmbedsEnabled) {
      return;
    }

    this._smartblockEmbedsEnabled = value;

    for (const shim of this.shims.values()) {
      if (shim.isSmartblockEmbedShim) {
        shim.disabledBySmartblockEmbedPref = !this._smartblockEmbedsEnabled;
      }
    }
  }

  async _onRequestStorageAccessRedirect({
    originUrl: srcUrl,
    url: dstUrl,
    tabId,
  }) {
    debug("Detected redirect", { srcUrl, dstUrl, tabId });

    // Check if a shim needs to request storage access for this redirect. This
    // handler is called when the *source url* matches a shims redirect pattern,
    // but we still need to check if the *destination url* matches.
    const matchingShims = Array.from(this.shims.values()).filter(shim => {
      const { enabled, requestStorageAccessForRedirect } = shim;

      if (!enabled || !requestStorageAccessForRedirect) {
        return false;
      }

      return requestStorageAccessForRedirect.some(
        ([srcPattern, dstPattern]) =>
          browser.matchPatterns.getMatcher([srcPattern]).matches(srcUrl) &&
          browser.matchPatterns.getMatcher([dstPattern]).matches(dstUrl)
      );
    });

    // For each matching shim, find out if its enabled in regard to dFPI state.
    const bugNumbers = new Set();
    let isDFPIActive = null;
    await Promise.all(
      matchingShims.map(async shim => {
        if (shim.onlyIfDFPIActive) {
          // Only get the dFPI state for the first shim which requires it.
          if (isDFPIActive === null) {
            const tabIsPB = (await browser.tabs.get(tabId)).incognito;
            isDFPIActive =
              await browser.trackingProtection.isDFPIActive(tabIsPB);
          }
          if (!isDFPIActive) {
            return;
          }
        }
        bugNumbers.add(shim.bug);
      })
    );

    // If there is no shim which needs storage access for this redirect src/dst
    // pair, resume it.
    if (!bugNumbers.size) {
      return;
    }

    // Inject the helper to call requestStorageAccessForOrigin on the document.
    await browser.tabs.executeScript(tabId, {
      file: "/lib/requestStorageAccess_helper.js",
      runAt: "document_start",
    });

    const bugUrls = Array.from(bugNumbers)
      .map(bugNo => `https://bugzilla.mozilla.org/show_bug.cgi?id=${bugNo}`)
      .join(", ");
    const warning = `Firefox calls the Storage Access API for ${dstUrl} on behalf of ${srcUrl}. See the following bugs for details: ${bugUrls}`;

    // Request storage access for the origin of the destination url of the
    // redirect.
    const { origin: requestStorageAccessOrigin } = new URL(dstUrl);

    // Wait for the requestStorageAccess request to finish before resuming the
    // redirect.
    const { success } = await browser.tabs.sendMessage(tabId, {
      requestStorageAccessOrigin,
      warning,
    });
    debug("requestStorageAccess callback", {
      success,
      requestStorageAccessOrigin,
      srcUrl,
      dstUrl,
      bugNumbers,
    });
  }

  async _onMessageFromShim(payload, sender) {
    const { tab, frameId } = sender;
    const { id, url } = tab;
    const { shimId, message } = payload;

    // Ignore unknown messages (for instance, from about:compat).
    if (
      message !== "getOptions" &&
      message !== "optIn" &&
      message !== "embedClicked" &&
      message !== "smartblockEmbedReplaced" &&
      message !== "smartblockGetFluentString" &&
      message !== "checkFacebookLoginStatus"
    ) {
      return undefined;
    }

    if (sender.id !== browser.runtime.id || id === -1) {
      throw new Error("not allowed");
    }

    // Important! It is entirely possible for sites to spoof
    // these messages, due to shims allowing web pages to
    // communicate with the extension.

    const shim = this.shims.get(shimId);
    if (!shim?.needsShimHelpers?.includes(message)) {
      throw new Error("not allowed");
    }

    if (message === "getOptions") {
      return Object.assign(
        {
          platform: await platformPromise,
          releaseBranch: await releaseBranchPromise,
        },
        shim.options
      );
    } else if (message === "optIn") {
      try {
        await shim.onUserOptIn(new URL(url).hostname, tab.incognito);
        const origin = new URL(tab.url).origin;
        warn(
          "** User opted in for",
          shim.name,
          "shim on",
          origin,
          "on tab",
          id,
          "frame",
          frameId
        );
        await shim.showOptInWarningOnce(id, origin);
      } catch (err) {
        console.error(err);
        throw new Error("error");
      }
    } else if (message === "embedClicked") {
      browser.trackingProtection.openProtectionsPanel(id);
    } else if (message === "smartblockEmbedReplaced") {
      browser.trackingProtection.incrementSmartblockEmbedShownTelemetry();
    } else if (message === "smartblockGetFluentString") {
      return await browser.trackingProtection.getSmartBlockEmbedFluentString(
        id,
        shimId,
        new URL(url).hostname
      );
    } else if (message === "checkFacebookLoginStatus") {
      // Verify that the user is logged in to Facebook by checking the c_user
      // cookie.
      let cookie = await browser.cookies.get({
        url: "https://www.facebook.com",
        name: "c_user",
      });

      // If the cookie is found, the user is logged in to Facebook.
      return cookie != null;
    }

    return undefined;
  }

  async _redirectLogos(details) {
    await this._haveCheckedEnabledPrefsPromise;

    if (!this.enabled) {
      return { cancel: true };
    }

    const { tabId, url } = details;
    const logo = new URL(url).pathname.slice(1);

    for (const shim of this.shims.values()) {
      await shim.ready;

      if (!shim.enabled) {
        continue;
      }

      if (shim.onlyIfDFPIActive) {
        const isPB = (await browser.tabs.get(details.tabId)).incognito;
        if (!(await browser.trackingProtection.isDFPIActive(isPB))) {
          continue;
        }
      }

      if (!shim.logos.includes(logo)) {
        continue;
      }

      if (shim.isActiveOnTab(tabId)) {
        return { redirectUrl: browser.runtime.getURL(`shims/${logo}`) };
      }
    }

    return { cancel: true };
  }

  async _onHeadersReceived(details) {
    await this._haveCheckedEnabledPrefsPromise;

    for (const shim of this.shims.values()) {
      await shim.ready;

      if (!shim.enabled) {
        continue;
      }

      if (shim.onlyIfDFPIActive) {
        const isPB = (await browser.tabs.get(details.tabId)).incognito;
        if (!(await browser.trackingProtection.isDFPIActive(isPB))) {
          continue;
        }
      }

      if (shim.isGoogleTrendsDFPIFix) {
        if (shim.GoogleNidCookieToUse) {
          continue;
        }

        for (const header of details.responseHeaders) {
          if (header.name == "set-cookie") {
            shim.GoogleNidCookieToUse = header.value;
            return { redirectUrl: details.url };
          }
        }
      }
    }

    return undefined;
  }

  async _onBeforeSendHeaders(details) {
    await this._haveCheckedEnabledPrefsPromise;

    const { frameId, requestHeaders, tabId } = details;

    if (!this.enabled) {
      return { requestHeaders };
    }

    for (const shim of this.shims.values()) {
      await shim.ready;

      if (!shim.enabled) {
        continue;
      }

      if (shim.isGoogleTrendsDFPIFix) {
        const value = shim.GoogleNidCookieToUse;

        if (!value) {
          continue;
        }

        let found;
        for (let header of requestHeaders) {
          if (header.name.toLowerCase() === "cookie") {
            header.value = value;
            found = true;
          }
        }
        if (!found) {
          requestHeaders.push({ name: "Cookie", value });
        }

        browser.tabs
          .get(tabId)
          .then(({ url }) => {
            debug(
              `Google Trends dFPI fix used on tab ${tabId} frame ${frameId} (${url})`
            );
          })
          .catch(() => {});

        const warning = `Working around Google Trends tracking protection breakage. See https://bugzilla.mozilla.org/show_bug.cgi?id=${shim.bug} for details.`;
        browser.tabs
          .executeScript(tabId, {
            code: `console.warn(${JSON.stringify(warning)})`,
            runAt: "document_start",
          })
          .catch(() => {});
      }
    }

    return { requestHeaders };
  }

  // eslint-disable-next-line complexity
  async _ensureShimForRequestOnTab(details) {
    await this._haveCheckedEnabledPrefsPromise;

    if (!this.enabled) {
      return undefined;
    }

    // We only ever reach this point if a request is for a URL which ought to
    // be shimmed. We never get here if a request is blocked, and we only
    // unblock requests if at least one shim matches it.

    const { frameId, originUrl, requestId, tabId, type, url } = details;

    // Ignore requests unrelated to tabs
    if (tabId < 0) {
      return undefined;
    }

    // We need to base our checks not on the frame's host, but the tab's.
    const topHost = new URL((await browser.tabs.get(tabId)).url).hostname;
    const isPB = (await browser.tabs.get(details.tabId)).incognito;
    const unblocked = await browser.trackingProtection.wasRequestUnblocked(
      requestId,
      isPB
    );

    let match;
    let shimToApply;
    for (const shim of this.shims.values()) {
      await shim.ready;

      if (!shim.enabled || (!shim.redirectsRequests && !shim.runFirst)) {
        continue;
      }

      if (shim.onlyIfDFPIActive || shim.onlyIfPrivateBrowsing) {
        if (!isPB && shim.onlyIfPrivateBrowsing) {
          continue;
        }
        if (
          shim.onlyIfDFPIActive &&
          !(await browser.trackingProtection.isDFPIActive(isPB))
        ) {
          continue;
        }
      }

      // Do not apply the shim if it is only meant to apply when strict mode ETP
      // (content blocking) was going to block the request.
      if (!unblocked && shim.onlyIfBlockedByETP) {
        continue;
      }

      if (!shim.meantForHost(topHost)) {
        continue;
      }

      // If this URL and content type isn't meant for this shim, don't apply it.
      match = shim.isTriggeredByURLAndType(url, type);
      if (match) {
        if (!unblocked && match.onlyIfBlockedByETP) {
          continue;
        }

        // If the user has already opted in for this shim, all requests it covers
        // should be allowed; no need for a shim anymore.
        if (shim.hasUserOptedInAlready(topHost, isPB)) {
          warn(
            `Allowing tracking ${type} ${url} on tab ${tabId} frame ${frameId} due to opt-in`
          );
          shim.showOptInWarningOnce(tabId, new URL(originUrl).origin);
          return undefined;
        }
        shimToApply = shim;
        break;
      }
    }

    let runFirst = false;

    if (shimToApply) {
      // Note that sites may request the same shim twice, but because the requests
      // may differ enough for some to fail (CSP/CORS/etc), we always let the request
      // complete via local redirect. Shims should gracefully handle this as well.

      const { target } = match;
      const { bug, file, id, name } = shimToApply;

      // Determine whether we should inject helper scripts into the page.
      // webExposedShimHelpers is an optional list of helpers to provide
      // directly to the website (see script injection below). If not used shims
      // should pass an empty array to disable this functionality.
      const needsShimHelpers =
        shimToApply.webExposedShimHelpers || shimToApply.needsShimHelpers;

      runFirst = shimToApply.runFirst;

      const redirect = target || file;

      warn(
        `Shimming tracking ${type} ${url} on tab ${tabId} frame ${frameId} with ${
          redirect || runFirst
        }`
      );

      const warning = `${name} is being shimmed by Firefox. See https://bugzilla.mozilla.org/show_bug.cgi?id=${bug} for details.`;

      let needConsoleMessage = true;

      if (shimToApply.isSmartblockEmbedShim) {
        try {
          await browser.tabs.executeScript(tabId, {
            file: `/lib/smartblock_embeds_helper.js`,
            frameId,
            runAt: "document_start",
          });
        } catch (_) {}
      }

      if (runFirst) {
        try {
          await browser.tabs.executeScript(tabId, {
            file: `/shims/${runFirst}`,
            frameId,
            runAt: "document_start",
          });
          shimToApply.setActiveOnTab(tabId);
        } catch (_) {}
      }

      // For scripts, we also set up any needed shim helpers.
      if (type === "script" && needsShimHelpers?.length) {
        try {
          await browser.tabs.executeScript(tabId, {
            file: "/lib/shim_messaging_helper.js",
            frameId,
            runAt: "document_start",
          });
          const origin = new URL(originUrl).origin;
          await browser.tabs.sendMessage(
            tabId,
            { origin, shimId: id, needsShimHelpers, warning },
            { frameId }
          );
          needConsoleMessage = false;
          shimToApply.setActiveOnTab(tabId);
        } catch (_) {}
      }

      if (needConsoleMessage) {
        try {
          await browser.tabs.executeScript(tabId, {
            code: `console.warn(${JSON.stringify(warning)})`,
            runAt: "document_start",
          });
        } catch (_) {}
      }

      if (!redirect.indexOf("http://") || !redirect.indexOf("https://")) {
        return { redirectUrl: redirect };
      }

      // If any shims matched the request to replace it, then redirect to the local
      // file bundled with SmartBlock, so the request never hits the network.
      return { redirectUrl: browser.runtime.getURL(`shims/${redirect}`) };
    }

    // Sanity check: if no shims end up handling this request,
    // yet it was meant to be blocked by ETP, then block it now.
    if (unblocked) {
      error(`unexpected: ${url} not shimmed on tab ${tabId} frame ${frameId}`);
      return { cancel: true };
    }

    if (!runFirst) {
      debug(`ignoring ${url} on tab ${tabId} frame ${frameId}`);
    }
    return undefined;
  }
}

if (typeof module !== "undefined") {
  module.exports = Shims;
}
