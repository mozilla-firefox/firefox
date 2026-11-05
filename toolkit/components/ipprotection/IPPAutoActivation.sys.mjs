/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ERRORS: "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPPrincipalRules:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPSiteRuleManager:
    "moz-src:///toolkit/components/ipprotection/IPPSiteRuleManager.sys.mjs",
  IPPMode:
    "moz-src:///toolkit/components/ipprotection/IPPChannelFilter.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPPProxyStates:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
  IPProtectionServerlist:
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () =>
  console.createInstance({
    logModule: "IPP_AutoActivation",
    maxLogLevel: Services.prefs.getBoolPref("browser.ipProtection.log", false)
      ? "Debug"
      : "Warn",
  })
);

const AUTOSTART_FEATURE_PREF = "browser.ipProtection.features.autoStart";
const AUTOSTART_PREF = "browser.ipProtection.autoStartEnabled";
const AUTO_RESTORE_PREF = "browser.ipProtection.autoRestoreEnabled";
const USER_ENABLED_PREF = "browser.ipProtection.userEnabled";
const RESTORING_ON_STARTUP = "sessionstore-restoring-on-startup";
const OFFLINE_STATUS_CHANGED = "network:offline-status-changed";

/**
 * Brings the proxy up without the user asking: auto-start, auto-restore, and
 * a narrow connection for the sites marked as always using the VPN.
 */
export class IPPAutoActivationSingleton {
  #initialized = false;
  #fullProtectionOwed = false;
  #hasRestoringObserver = false;
  #willRestore = false;
  /** @type {{ mode: import("./IPPChannelFilter.sys.mjs").IPPProxyMode, promise: Promise } | null} */
  #activation = null;

  /**
   * Set by the embedder to decide whether locked egress locations may be
   * used. Without it, auto-activation never routes through a locked one.
   *
   * @type {(() => Promise<boolean>) | null}
   */
  isPremium = null;

  constructor() {
    this.handleEvent = this.#handleEvent.bind(this);
    this.handleProxyEvent = this.#handleProxyEvent.bind(this);
    this.handleRuleEvent = this.#handleRuleEvent.bind(this);

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "autoStartFeaturePref",
      AUTOSTART_FEATURE_PREF,
      false
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "autoStartPref",
      AUTOSTART_PREF,
      false,
      (_pref, _oldVal, enabled) => {
        if (enabled) {
          this.init();
        } else {
          this.uninit();
        }
      }
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "autoRestorePref",
      AUTO_RESTORE_PREF,
      false
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "userEnabled",
      USER_ENABLED_PREF,
      false
    );
  }

  get autoStart() {
    return this.autoStartFeaturePref && this.autoStartPref;
  }

  /**
   * Only applies when auto-start is not already bringing the proxy up.
   *
   * @returns {boolean}
   */
  get autoRestore() {
    return this.autoRestorePref && !this.autoStartPref && this.userEnabled;
  }

  /** @returns {boolean} Whether a restore is held open for this session. */
  get willRestore() {
    return this.#willRestore;
  }

  get inclusionWanted() {
    return lazy.IPPSiteRuleManager.hasRule(lazy.IPPPrincipalRules.INCLUDED);
  }

  /**
   * The mode an idle proxy should come up in, or null to leave it alone. Full
   * protection wins over anything narrower.
   *
   * @returns {import("./IPPChannelFilter.sys.mjs").IPPProxyMode | null}
   */
  desiredMode() {
    if (this.#fullProtectionOwed) {
      return lazy.IPPMode.MODE_FULL;
    }
    if (this.inclusionWanted) {
      return lazy.IPPMode.MODE_INCLUSION;
    }
    return null;
  }

  init() {
    if (this.#initialized) {
      return;
    }
    this.#initialized = true;

    lazy.IPProtectionService.addEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.addEventListener(
      "IPPProxyManager:StateChanged",
      this.handleProxyEvent
    );
    lazy.IPPSiteRuleManager.addEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleRuleEvent
    );
    Services.obs.addObserver(this, OFFLINE_STATUS_CHANGED);

    if (this.autoStart) {
      this.#fullProtectionOwed = true;
    } else if (this.autoRestore) {
      Services.obs.addObserver(this, RESTORING_ON_STARTUP);
      this.#hasRestoringObserver = true;
    }
    this.#maybeActivate();
  }

  initOnStartupCompleted() {
    // The activation started for the restore completes on its own.
    this.#willRestore = false;
    this.#stopObservingRestore();
  }

  uninit() {
    if (!this.#initialized) {
      return;
    }
    this.#initialized = false;
    this.#fullProtectionOwed = false;

    if (this.#willRestore || this.#activation) {
      this.#willRestore = false;
      lazy.IPPProxyManager.stop(false);
    }

    lazy.IPProtectionService.removeEventListener(
      "IPProtectionService:StateChanged",
      this.handleEvent
    );
    lazy.IPPProxyManager.removeEventListener(
      "IPPProxyManager:StateChanged",
      this.handleProxyEvent
    );
    lazy.IPPSiteRuleManager.removeEventListener(
      "SiteRuleManager:RuleChanged",
      this.handleRuleEvent
    );
    Services.obs.removeObserver(this, OFFLINE_STATUS_CHANGED);
    this.#stopObservingRestore();
  }

  observe(_subject, topic) {
    if (topic === OFFLINE_STATUS_CHANGED) {
      if (Services.io.offline) {
        return;
      }
      // A connection for included sites that failed on going offline is
      // brought back up rather than left holding their traffic.
      if (
        lazy.IPPProxyManager.state === lazy.IPPProxyStates.ERROR &&
        lazy.IPPProxyManager.mode === lazy.IPPMode.MODE_INCLUSION &&
        lazy.IPPProxyManager.errorType === lazy.ERRORS.NETWORK
      ) {
        lazy.IPPProxyManager.stop(false).then(() => this.#maybeActivate());
        return;
      }
      this.#maybeActivate();
      return;
    }
    if (topic !== RESTORING_ON_STARTUP) {
      return;
    }
    this.#stopObservingRestore();

    // Only worth holding traffic if the carried-over state says it could work.
    if (lazy.IPProtectionService.state !== lazy.IPProtectionStates.READY) {
      return;
    }

    this.#willRestore = true;
    this.#fullProtectionOwed = true;
    this.#maybeActivate();
  }

  #stopObservingRestore() {
    if (this.#hasRestoringObserver) {
      Services.obs.removeObserver(this, RESTORING_ON_STARTUP);
      this.#hasRestoringObserver = false;
    }
  }

  #maybeActivate() {
    const mode = this.desiredMode();
    // Included sites can wait for the network to come back.
    if (
      mode === null ||
      (mode === lazy.IPPMode.MODE_INCLUSION && Services.io.offline)
    ) {
      return;
    }

    if (this.#activation) {
      // Full protection replaces a narrower activation still in flight.
      if (mode === lazy.IPPMode.MODE_FULL && this.#activation.mode !== mode) {
        lazy.IPPProxyManager.stop(false).then(() => this.#maybeActivate());
      }
      return;
    }

    // A busy proxy is either already doing this or waiting on the user.
    const state = lazy.IPPProxyManager.state;
    if (
      state !== lazy.IPPProxyStates.NOT_READY &&
      state !== lazy.IPPProxyStates.READY
    ) {
      return;
    }

    // Cleared on the ask, not on success, so neither a failure nor the user
    // turning it off is retried.
    if (mode === lazy.IPPMode.MODE_FULL) {
      this.#fullProtectionOwed = false;
    }

    lazy.logConsole.info("Starting the proxy, mode:", mode);
    const promise = lazy.IPPProxyManager.start(
      false,
      PrivateBrowsingUtils.permanentPrivateBrowsing,
      lazy.IPProtectionServerlist.resolveEgressCountry(this.isPremium),
      mode
    );
    this.#activation = { mode, promise };
    promise.finally(() => {
      if (this.#activation?.promise === promise) {
        this.#activation = null;
      }
    });
  }

  #handleProxyEvent(event) {
    switch (lazy.IPPProxyManager.state) {
      case lazy.IPPProxyStates.ACTIVE:
        this.#activation = null;
        break;

      case lazy.IPPProxyStates.READY:
        // Only after a working connection goes away: a failed activation, or
        // the user stopping a failed connection, is not retried.
        if (
          event.detail?.prevState === lazy.IPPProxyStates.ACTIVE ||
          event.detail?.prevState === lazy.IPPProxyStates.PAUSED
        ) {
          // Starting dispatches ACTIVATING, so let every listener see READY
          // first.
          Promise.resolve().then(() => {
            if (this.#initialized) {
              this.#maybeActivate();
            }
          });
        }
        break;

      default:
        break;
    }
  }

  #handleRuleEvent() {
    // The last inclusion going away takes a narrow connection down.
    if (
      this.desiredMode() === null &&
      lazy.IPPProxyManager.mode === lazy.IPPMode.MODE_INCLUSION
    ) {
      lazy.IPPProxyManager.stop(false);
      return;
    }
    this.#maybeActivate();
  }

  #handleEvent() {
    switch (lazy.IPProtectionService.state) {
      case lazy.IPProtectionStates.UNINITIALIZED:
      case lazy.IPProtectionStates.UNAVAILABLE:
      case lazy.IPProtectionStates.UNAUTHENTICATED:
        // The restore has missed its window; auto-start is owed one again.
        if (this.#willRestore) {
          this.#willRestore = false;
          lazy.IPPProxyManager.stop(false);
        }
        this.#fullProtectionOwed = this.autoStart;
        break;

      case lazy.IPProtectionStates.READY:
        this.#maybeActivate();
        break;

      default:
        break;
    }
  }
}

export const IPPAutoActivation = new IPPAutoActivationSingleton();
