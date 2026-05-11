/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module. This
// is because the Karma test environment already stubs out
// XPCOMUtils, and overrides importESModule to be a no-op (which
// can't be done for a static import statement).

// eslint-disable-next-line mozilla/use-static-import
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

import {
  actionCreators as ac,
  actionTypes as at,
} from "resource://newtab/common/Actions.mjs";
import { Prefs } from "resource://newtab/lib/ActivityStreamPrefs.sys.mjs";
import {
  PREF_DEFAULT_VALUE_TOPSTORIES_ENABLED,
  PREF_DEFAULT_VALUE_TOPSITES_ENABLED,
} from "resource://newtab/lib/ActivityStream.sys.mjs";

// eslint-disable-next-line mozilla/use-static-import
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
});

const ACTIVATION_WINDOW_VARIANT_PREF = "activationWindow.variant";
const ACTIVATION_WINDOW_ENTER_MESSAGE_ID_PREF =
  "activationWindow.enterMessageID";
const ACTIVATION_WINDOW_EXIT_MESSAGE_ID_PREF = "activationWindow.exitMessageID";
const TOP_SITES_ENABLED_PREF = "feeds.topsites";
const TOP_STORIES_ENABLED_PREF = "feeds.section.topstories";
const TOP_SITES_USER_VALUE_TEMP_PREF =
  "activationWindow.temp.topSitesUserValue";
const TOP_STORIES_USER_VALUE_TEMP_PREF =
  "activationWindow.temp.topStoriesUserValue";
const PREF_DEFAULTS = [
  { type: "bool", key: "logowordmark.alwaysVisible", defaultValue: false },
  { type: "bool", key: "feeds.section.topstories", defaultValue: false },
  { type: "bool", key: "discoverystream.enabled", defaultValue: false },
  {
    type: "bool",
    key: "discoverystream.hardcoded-basic-layout",
    defaultValue: false,
  },
  { type: "string", key: "discoverystream.spocs-endpoint", defaultValue: "" },
  {
    type: "string",
    key: "discoverystream.spocs-endpoint-query",
    defaultValue: "",
  },
  {
    type: "string",
    key: "discoverystream.sections.personalization.inferred.debug.override",
    defaultValue: "",
  },
  { type: "string", key: "newNewtabExperience.colors", defaultValue: "" },
];

ChromeUtils.defineLazyGetter(lazy, "logConsole", function () {
  return console.createInstance({
    prefix: "PrefsFeed",
    maxLogLevel: Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.activationWindow.log",
      false
    )
      ? "Debug"
      : "Warn",
  });
});

export class PrefsFeed {
  constructor(prefMap) {
    this._prefMap = prefMap;
    this._prefs = new Prefs();
    this.onExperimentUpdated = this.onExperimentUpdated.bind(this);
    this.onTrainhopExperimentUpdated =
      this.onTrainhopExperimentUpdated.bind(this);
    this.onPocketExperimentUpdated = this.onPocketExperimentUpdated.bind(this);
    this.onSmartShortcutsExperimentUpdated =
      this.onSmartShortcutsExperimentUpdated.bind(this);
    this.onWidgetsUpdated = this.onWidgetsUpdated.bind(this);
    this.onOhttpImagesUpdated = this.onOhttpImagesUpdated.bind(this);
    this.onInferredPersonalizationExperimentUpdated =
      this.onInferredPersonalizationExperimentUpdated.bind(this);
    this.onAdsBackendUpdated = this.onAdsBackendUpdated.bind(this);

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "inActivationWindowState",
      this._prefs._branchStr + ACTIVATION_WINDOW_VARIANT_PREF,
      ""
    );
  }

  /**
   * Handles preference changes by broadcasting them to content processes and
   * tracking user changes during the activation window.
   *
   * @param {string} name - The preference name
   * @param {boolean | number | string} value - The new preference value
   * @param {boolean} [isUserChange=true] - Whether this change originated from
   *   a user action (true) or programmatic state transition (false). Only user
   *   changes are tracked during the activation window.
   */
  onPrefChanged(name, value, isUserChange = true) {
    const prefItem = this._prefMap.get(name);
    if (prefItem) {
      let action = "BroadcastToContent";
      if (prefItem.skipBroadcast) {
        action = "OnlyToMain";
        if (prefItem.alsoToPreloaded) {
          action = "AlsoToPreloaded";
        }
      }

      this.store.dispatch(
        ac[action]({
          type: at.PREF_CHANGED,
          data: { name, value },
        })
      );
    }

    if (isUserChange && this.inActivationWindowState) {
      this.trackActivationWindowPrefChange(name, value);
    }
  }

  /**
   * Tracks user preference changes during the activation window.
   *
   * @param {string} name - The preference name
   * @param {boolean | number | string} value - The new preference value
   */
  trackActivationWindowPrefChange(name, value) {
    if (name === TOP_SITES_ENABLED_PREF) {
      this._prefs.set(TOP_SITES_USER_VALUE_TEMP_PREF, value);
      lazy.logConsole.debug(
        `User set top sites to ${value} during activation window`
      );
    } else if (name === TOP_STORIES_ENABLED_PREF) {
      this._prefs.set(TOP_STORIES_USER_VALUE_TEMP_PREF, value);
      lazy.logConsole.debug(
        `User set top stories to ${value} during activation window`
      );
    }
  }

  _setStringPref(values, key, defaultValue) {
    this._setPref(values, key, defaultValue, Services.prefs.getStringPref);
  }

  _setBoolPref(values, key, defaultValue) {
    this._setPref(values, key, defaultValue, Services.prefs.getBoolPref);
  }

  _setIntPref(values, key, defaultValue) {
    this._setPref(values, key, defaultValue, Services.prefs.getIntPref);
  }

  _setPref(values, key, defaultValue, getPrefFunction) {
    let value = getPrefFunction(
      `browser.newtabpage.activity-stream.${key}`,
      defaultValue
    );
    values[key] = value;
    this._prefMap.set(key, { value });
  }

  /**
   * Handler for when experiment data updates.
   */
  onExperimentUpdated() {
    const value = lazy.NimbusFeatures.newtab.getAllVariables() || {};
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "featureConfig",
          value,
        },
      })
    );
  }

  /**
   * Computes the trainhop config by processing all enrollments.
   * Supports two formats:
   * - Single payload: { type: "feature", payload: { "enabled": true, ... }}
   * - Multi-payload: { type: "multi-payload", payload: [{ type: "feature", payload: { "enabled": true, ... }}] }
   * Both formats output the same structure: { "feature": { "enabled": true, ... }}
   */
  _getTrainhopConfig() {
    const allEnrollments =
      lazy.NimbusFeatures.newtabTrainhop.getAllEnrollments() || [];

    let enrollmentsToProcess = [];

    allEnrollments.forEach(enrollment => {
      if (
        enrollment?.value?.type === "multi-payload" &&
        Array.isArray(enrollment?.value?.payload)
      ) {
        enrollment.value.payload.forEach(item => {
          if (item?.type && item?.payload) {
            enrollmentsToProcess.push({
              value: {
                type: item.type,
                payload: item.payload,
              },
              meta: enrollment.meta,
            });
          }
        });
      } else if (enrollment?.value?.type) {
        enrollmentsToProcess.push(enrollment);
      }
    });

    const valueObj = {};
    enrollmentsToProcess.reduce((accumulator, currentValue) => {
      if (currentValue?.value?.type) {
        if (
          !accumulator[currentValue.value.type] ||
          (accumulator[currentValue.value.type].meta.isRollout &&
            !currentValue.meta.isRollout)
        ) {
          accumulator[currentValue.value.type] = currentValue;
          valueObj[currentValue.value.type] = currentValue.value.payload;
        }
      }
      return accumulator;
    }, {});

    // Bug 2021055: Write weather.display to the default branch so Nimbus sets
    // the initial value without overriding an explicit user choice (user branch
    // always takes precedence over the default branch).
    if (valueObj.widgets?.weatherForecastEnabled && valueObj.weather?.display) {
      Services.prefs
        .getDefaultBranch(this._prefs._branchStr)
        .setStringPref("weather.display", valueObj.weather.display);
    }

    // Write widgets.weather.size to the default branch so trainhop overrides
    // the migration default computed by getWeatherWidgetSize() while a user's
    // explicit size pick (user branch) still wins.
    if (
      typeof valueObj.widgets?.weatherSize === "string" &&
      valueObj.widgets.weatherSize
    ) {
      Services.prefs
        .getDefaultBranch(this._prefs._branchStr)
        .setStringPref("widgets.weather.size", valueObj.widgets.weatherSize);
    }

    // Write topSitesRows to the default branch to enable experiments with
    // the default row count without overriding an explicit user choice.
    if (valueObj.topSites?.topSitesRows) {
      Services.prefs
        .getDefaultBranch(this._prefs._branchStr)
        .setIntPref("topSitesRows", valueObj.topSites.topSitesRows);
    }

    // Write initialWallpaper to the user branch so it persists after the
    // experiment ends. The guard prevents overwriting an existing value or a
    // user's explicit wallpaper choice that has already cleared it.
    if (
      valueObj.wallpaper?.initialWallpaper &&
      !this._prefs.get("newtabWallpapers.initialWallpaper")
    ) {
      this._prefs.set(
        "newtabWallpapers.initialWallpaper",
        valueObj.wallpaper.initialWallpaper
      );
    }

    return valueObj;
  }

  /**
   * Computes the adsBackend features by processing all enrollments.
   * This only looks at the flags variable within the adsBackend feature
   * and merges each flags object, preferring an experiment over a rollout.
   *
   */
  _getAdsBackendFeatures() {
    const allEnrollments =
      lazy.NimbusFeatures.adsBackend.getAllEnrollments() || [];

    const valueObj = {};
    allEnrollments.reduce((accumulator, currentValue) => {
      if (currentValue?.value?.flags) {
        for (const [key, value] of Object.entries(currentValue.value.flags)) {
          if (
            !accumulator[key] ||
            (accumulator[key].meta.isRollout && !currentValue.meta.isRollout)
          ) {
            accumulator[key] = currentValue;
            valueObj[key] = value;
          }
        }
      }
      return accumulator;
    }, {});

    return valueObj;
  }

  /**
   * Handler for when experiment data updates.
   */
  onTrainhopExperimentUpdated() {
    const valueObj = this._getTrainhopConfig();

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "trainhopConfig",
          value: valueObj,
        },
      })
    );
  }

  /**
   * Handler for Pocket specific experiment data updates.
   */
  onPocketExperimentUpdated(event, reason) {
    const value = lazy.NimbusFeatures.pocketNewtab.getAllVariables() || {};

    if (
      value.currentWallpaper &&
      !this._prefs.get("newtabWallpapers.initialWallpaper")
    ) {
      this._prefs.set(
        "newtabWallpapers.initialWallpaper",
        value.currentWallpaper
      );
    }

    // Loaded experiments are set up inside init()
    if (
      reason !== "feature-experiment-loaded" &&
      reason !== "feature-rollout-loaded"
    ) {
      this.store.dispatch(
        ac.BroadcastToContent({
          type: at.PREF_CHANGED,
          data: {
            name: "pocketConfig",
            value,
          },
        })
      );
    }
  }

  /**
   * Handler for when smart shortcuts experiment data updates.
   */
  onSmartShortcutsExperimentUpdated() {
    const value =
      lazy.NimbusFeatures.newtabSmartShortcuts.getAllVariables() || {};
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "smartShortcutsConfig",
          value,
        },
      })
    );
  }

  /**
   * Handler for when inferred personalization experiment config values update.
   */
  onInferredPersonalizationExperimentUpdated() {
    const value =
      lazy.NimbusFeatures.newtabInferredPersonalization.getAllVariables() || {};
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "inferredPersonalizationConfig",
          value,
        },
      })
    );
  }

  /**
   * Handler for when widget experiment data updates.
   */
  onWidgetsUpdated() {
    const value = lazy.NimbusFeatures.newtabWidgets.getAllVariables() || {};
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "widgetsConfig",
          value,
        },
      })
    );
  }

  /**
   * Handler for when OHTTP images experiment data updates.
   */
  onOhttpImagesUpdated() {
    const value = lazy.NimbusFeatures.newtabOhttpImages.getAllVariables() || {};
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "ohttpImagesConfig",
          value,
        },
      })
    );
  }

  /**
   * Handler for when adsBackend experiment data updates.
   */
  onAdsBackendUpdated() {
    const valueObj = this._getAdsBackendFeatures();

    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREF_CHANGED,
        data: {
          name: "adsBackendConfig",
          value: valueObj,
        },
      })
    );
  }

  init() {
    this._prefs.observeBranch(this);
    lazy.NimbusFeatures.newtab.onUpdate(this.onExperimentUpdated);
    lazy.NimbusFeatures.newtabTrainhop.onUpdate(
      this.onTrainhopExperimentUpdated
    );
    lazy.NimbusFeatures.pocketNewtab.onUpdate(this.onPocketExperimentUpdated);
    lazy.NimbusFeatures.newtabSmartShortcuts.onUpdate(
      this.onSmartShortcutsExperimentUpdated
    );
    lazy.NimbusFeatures.newtabInferredPersonalization.onUpdate(
      this.onInferredPersonalizationExperimentUpdated
    );
    lazy.NimbusFeatures.newtabWidgets.onUpdate(this.onWidgetsUpdated);
    lazy.NimbusFeatures.newtabOhttpImages.onUpdate(this.onOhttpImagesUpdated);
    lazy.NimbusFeatures.adsBackend.onUpdate(this.onAdsBackendUpdated);

    // Get the initial value of each activity stream pref
    const values = {};
    for (const name of this._prefMap.keys()) {
      values[name] = this._prefs.get(name);
    }

    // These are not prefs, but are needed to determine stuff in content that can only be
    // computed in main process
    values.isPrivateBrowsingEnabled = lazy.PrivateBrowsingUtils.enabled;
    values.platform = AppConstants.platform;

    // Save the geo pref if we have it
    if (lazy.Region.home) {
      values.region = lazy.Region.home;
      this.geo = values.region;
    } else if (this.geo !== "") {
      // Watch for geo changes and use a dummy value for now
      Services.obs.addObserver(this, lazy.Region.REGION_TOPIC);
      this.geo = "";
    }

    // Get the firefox accounts url for links and to send firstrun metrics to.
    values.fxa_endpoint = Services.prefs.getStringPref(
      "browser.newtabpage.activity-stream.fxaccounts.endpoint",
      "https://accounts.firefox.com"
    );

    // Get the firefox update channel with values as default, nightly, beta or release
    values.appUpdateChannel = Services.prefs.getStringPref(
      "app.update.channel",
      ""
    );

    // Read the pref for search shortcuts top sites experiment from firefox.js and store it
    // in our internal list of prefs to watch
    let searchTopSiteExperimentPrefValue = Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.improvesearch.topSiteSearchShortcuts"
    );
    values["improvesearch.topSiteSearchShortcuts"] =
      searchTopSiteExperimentPrefValue;
    this._prefMap.set("improvesearch.topSiteSearchShortcuts", {
      value: searchTopSiteExperimentPrefValue,
    });

    values.mayHaveSponsoredTopSites = Services.prefs.getBoolPref(
      "browser.topsites.useRemoteSetting"
    );

    // Add experiment values and default values
    values.featureConfig = lazy.NimbusFeatures.newtab.getAllVariables() || {};
    values.pocketConfig =
      lazy.NimbusFeatures.pocketNewtab.getAllVariables() || {};
    if (
      values.pocketConfig.currentWallpaper &&
      !this._prefs.get("newtabWallpapers.initialWallpaper")
    ) {
      this._prefs.set(
        "newtabWallpapers.initialWallpaper",
        values.pocketConfig.currentWallpaper
      );
    }
    values.smartShortcutsConfig =
      lazy.NimbusFeatures.newtabSmartShortcuts.getAllVariables() || {};
    values.widgetsConfig =
      lazy.NimbusFeatures.newtabWidgets.getAllVariables() || {};
    values.trainhopConfig = this._getTrainhopConfig();
    values.adsBackendConfig = this._getAdsBackendFeatures();
    for (const { type, key, defaultValue } of PREF_DEFAULTS) {
      if (type === "bool") {
        this._setBoolPref(values, key, defaultValue);
      } else if (type === "string") {
        this._setStringPref(values, key, defaultValue);
      }
    }

    // Set the initial state of all prefs in redux
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.PREFS_INITIAL_VALUES,
        data: values,
        meta: {
          isStartup: true,
        },
      })
    );

    this.checkForActivationWindow(Temporal.Now.instant(), /* isStartup */ true);
  }

  uninit() {
    this.removeListeners();
  }

  removeListeners() {
    this._prefs.ignoreBranch(this);
    lazy.NimbusFeatures.newtab.offUpdate(this.onExperimentUpdated);
    lazy.NimbusFeatures.newtabTrainhop.offUpdate(
      this.onTrainhopExperimentUpdated
    );
    lazy.NimbusFeatures.pocketNewtab.offUpdate(this.onPocketExperimentUpdated);
    lazy.NimbusFeatures.newtabSmartShortcuts.offUpdate(
      this.onSmartShortcutsExperimentUpdated
    );
    lazy.NimbusFeatures.newtabInferredPersonalization.offUpdate(
      this.onInferredPersonalizationExperimentUpdated
    );
    lazy.NimbusFeatures.newtabWidgets.offUpdate(this.onWidgetsUpdated);
    lazy.NimbusFeatures.newtabOhttpImages.offUpdate(this.onOhttpImagesUpdated);
    lazy.NimbusFeatures.adsBackend.offUpdate(this.onAdsBackendUpdated);

    if (this.geo === "") {
      Services.obs.removeObserver(this, lazy.Region.REGION_TOPIC);
    }
  }

  /**
   * Checks whether the current profile is within the activation window and
   * updates the activation window state accordingly.
   *
   * @param {Temporal.Instant} [now=Temporal.Now.instant()] - The current time
   * @param {boolean} [isStartup=false] - Whether this is being called during
   *   browser startup. When true, forces re-application of default branch
   *   prefs even if already in the activation window state, since default
   *   branch prefs are not persisted across restarts.
   */
  checkForActivationWindow(now = Temporal.Now.instant(), isStartup = false) {
    const state = this.store.getState();
    if (!state || !state.Prefs) {
      return;
    }

    const { values } = state.Prefs;

    const {
      enabled = false,
      maxProfileAgeInHours = 48,
      disableTopSites = false,
      disableTopStories = false,
      variant = "",
      enterActivationWindowMessageID = "",
      exitActivationWindowMessageID = "",
    } = values?.trainhopConfig?.activationWindowBehavior ?? {};

    const { createdInstant } = lazy.AboutNewTab.activityStream;

    if (
      !enabled ||
      !createdInstant ||
      !variant ||
      lazy.SelectableProfileService.hasCreatedSelectableProfiles()
    ) {
      lazy.logConsole.log("Activation window evaluation skipped.");
      lazy.logConsole.debug(
        `enabled:${enabled}, createdInstant:${createdInstant}, variant: ${variant}`
      );
      if (this.inActivationWindowState) {
        lazy.logConsole.log(
          "Exiting activation window state because evaluation skipped."
        );
        this.exitActivationWindowState();
      }
      return;
    }

    // Are we within the maxProfileAgeInHours period? We compare against the
    // current instant and check to see if the creation time is before now,
    // but after maxProfileAgeInHours hours ago.
    const withinMaxProfileAgeInHours =
      Temporal.Instant.compare(createdInstant, now) === -1 &&
      Temporal.Instant.compare(
        createdInstant,
        now.subtract({ hours: maxProfileAgeInHours })
      ) === 1;

    if (withinMaxProfileAgeInHours) {
      lazy.logConsole.log(
        `Within activation window range (${maxProfileAgeInHours} hours)`
      );
      this.enterActivationWindowState(
        variant,
        disableTopSites,
        disableTopStories,
        enterActivationWindowMessageID,
        isStartup
      );
    } else if (this.inActivationWindowState) {
      this.exitActivationWindowState(exitActivationWindowMessageID);
    }
  }

  /**
   * Enters the activation window state by setting default branch prefs and
   * broadcasting the changes.
   *
   * @param {string} variant - The activation window variant identifier
   * @param {boolean} disableTopSites - Whether to disable top sites by default
   * @param {boolean} disableTopStories - Whether to disable top stories by default
   * @param {string} enterActivationWindowMessageID - Message ID to display when entering activation window
   * @param {boolean} [isStartup=false] - Whether this is being called during
   *   browser startup. When true, skips the idempotent check to ensure default
   *   branch prefs are reapplied.
   */
  enterActivationWindowState(
    variant,
    disableTopSites,
    disableTopStories,
    enterActivationWindowMessageID,
    isStartup = false
  ) {
    if (!isStartup && this.inActivationWindowState === variant) {
      lazy.logConsole.debug(
        `Already in activation window state for variant: ${variant}`
      );
      return;
    }

    lazy.logConsole.log("Entering activation window state");
    lazy.logConsole.debug(`variant: ${variant}`);

    // Clear the variant pref first to ensure we're not in activation window
    // state when the preference observer fires (even asynchronously) from the
    // default branch changes below.
    this._prefs.reset(ACTIVATION_WINDOW_VARIANT_PREF);

    const defaultBranch = Services.prefs.getDefaultBranch(
      this._prefs._branchStr
    );

    if (disableTopSites) {
      lazy.logConsole.log("Disabling top sites by default.");
      defaultBranch.setBoolPref(TOP_SITES_ENABLED_PREF, false);
      // This is a programmatic change, not a user action, so don't track it
      this.onPrefChanged(
        TOP_SITES_ENABLED_PREF,
        false,
        /* isUserChange */ false
      );
    }

    if (disableTopStories) {
      lazy.logConsole.log("Disabling top stories by default.");
      defaultBranch.setBoolPref(TOP_STORIES_ENABLED_PREF, false);
      // This is a programmatic change, not a user action, so don't track it
      this.onPrefChanged(
        TOP_STORIES_ENABLED_PREF,
        false,
        /* isUserChange */ false
      );
    }

    // Set the variant pref last, after default branch changes and broadcasts.
    // This prevents the preference observer from incorrectly tracking the
    // default branch changes as user actions.
    this._prefs.set(ACTIVATION_WINDOW_VARIANT_PREF, variant);

    // In the unlikely event that we've exited the activation window in the
    // past but somehow re-entered it, clear away the exit message pref so
    // that we don't accidentally show any exit messages.
    this._prefs.set(ACTIVATION_WINDOW_EXIT_MESSAGE_ID_PREF, "");

    // Set the enter message ID pref for ASRouter message targeting
    if (enterActivationWindowMessageID) {
      this._prefs.set(
        ACTIVATION_WINDOW_ENTER_MESSAGE_ID_PREF,
        enterActivationWindowMessageID
      );
    }

    lazy.logConsole.log("Activation window enter complete");
  }

  /**
   * Exits the activation window state by resetting default branch prefs and
   * restoring any user preference changes made during the window.
   *
   * @param {string} [exitActivationWindowMessageID=""] - Message ID to display when exiting activation window
   */
  exitActivationWindowState(exitActivationWindowMessageID = "") {
    lazy.logConsole.log("Exiting activation window state.", new Error().stack);
    this._prefs.reset(ACTIVATION_WINDOW_VARIANT_PREF);

    // Always reset defaults back to true first
    const defaultBranch = Services.prefs.getDefaultBranch(
      this._prefs._branchStr
    );
    lazy.logConsole.debug("Resetting default branch prefs to true");
    defaultBranch.setBoolPref(
      TOP_SITES_ENABLED_PREF,
      PREF_DEFAULT_VALUE_TOPSITES_ENABLED
    );
    defaultBranch.setBoolPref(
      TOP_STORIES_ENABLED_PREF,
      PREF_DEFAULT_VALUE_TOPSTORIES_ENABLED
    );

    // Check temp pref status
    const hasTopSitesTempPref = this._prefs.isSet(
      TOP_SITES_USER_VALUE_TEMP_PREF
    );
    const hasTopStoriesTempPref = this._prefs.isSet(
      TOP_STORIES_USER_VALUE_TEMP_PREF
    );
    lazy.logConsole.debug(
      `Temp prefs: topSites was disabled=${hasTopSitesTempPref}, topStories was disabled=${hasTopStoriesTempPref}`
    );

    // Broadcast the default changes (if user hasn't set these prefs, they'll see true)
    // If user has set them, setting user values below will trigger another broadcast
    if (!hasTopSitesTempPref) {
      lazy.logConsole.debug("Broadcasting top sites default change (true)");
      // This is a programmatic change, not a user action, so don't track it
      this.onPrefChanged(
        TOP_SITES_ENABLED_PREF,
        true,
        /* isUserChange */ false
      );
    } else {
      lazy.logConsole.debug(
        "Skipping top sites broadcast - will restore user value"
      );
    }
    if (!hasTopStoriesTempPref) {
      lazy.logConsole.debug("Broadcasting top stories default change (true)");
      // This is a programmatic change, not a user action, so don't track it
      this.onPrefChanged(
        TOP_STORIES_ENABLED_PREF,
        true,
        /* isUserChange */ false
      );
    } else {
      lazy.logConsole.debug(
        "Skipping top stories broadcast - will restore user value"
      );
    }

    // Then check if user made changes during activation window and apply them
    // Note: _prefs.set() will automatically trigger onPrefChanged() via the observer
    if (hasTopSitesTempPref) {
      const userValue = this._prefs.get(TOP_SITES_USER_VALUE_TEMP_PREF);
      lazy.logConsole.log(
        `Restoring user's top sites preference to ${userValue}`
      );
      this._prefs.set(TOP_SITES_ENABLED_PREF, userValue);
      this._prefs.reset(TOP_SITES_USER_VALUE_TEMP_PREF);
      lazy.logConsole.debug(
        "Top sites user value restored and temp pref cleared"
      );
    }

    if (hasTopStoriesTempPref) {
      const userValue = this._prefs.get(TOP_STORIES_USER_VALUE_TEMP_PREF);
      lazy.logConsole.log(
        `Restoring user's top stories preference to ${userValue}`
      );
      this._prefs.set(TOP_STORIES_ENABLED_PREF, userValue);
      this._prefs.reset(TOP_STORIES_USER_VALUE_TEMP_PREF);
      lazy.logConsole.debug(
        "Top stories user value restored and temp pref cleared"
      );
    }

    // Clear the enter message ID pref and set exit message ID for ASRouter
    this._prefs.set(ACTIVATION_WINDOW_ENTER_MESSAGE_ID_PREF, "");
    if (exitActivationWindowMessageID) {
      this._prefs.set(
        ACTIVATION_WINDOW_EXIT_MESSAGE_ID_PREF,
        exitActivationWindowMessageID
      );
    } else {
      this._prefs.set(ACTIVATION_WINDOW_EXIT_MESSAGE_ID_PREF, "");
    }

    lazy.logConsole.log("Activation window exit complete");
  }

  observe(subject, topic) {
    switch (topic) {
      case lazy.Region.REGION_TOPIC:
        this.store.dispatch(
          ac.BroadcastToContent({
            type: at.PREF_CHANGED,
            data: { name: "region", value: lazy.Region.home },
          })
        );
        break;
    }
  }

  onAction(action) {
    switch (action.type) {
      case at.INIT:
        this.init();
        break;
      case at.UNINIT:
        this.uninit();
        break;
      case at.CLEAR_PREF:
        Services.prefs.clearUserPref(this._prefs._branchStr + action.data.name);
        break;
      case at.SET_PREF:
        this._prefs.set(action.data.name, action.data.value);
        break;
      case at.NEW_TAB_STATE_REQUEST: {
        this.checkForActivationWindow();
        break;
      }
    }
  }
}
