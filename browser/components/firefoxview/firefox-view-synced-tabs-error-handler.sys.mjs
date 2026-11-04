/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This module exports the SyncedTabsErrorHandler singleton, which handles
 * error states for synced tabs.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  UIState: "resource://services-sync/UIState.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "syncUtils", () => {
  return ChromeUtils.importESModule("resource://services-sync/util.sys.mjs")
    .Utils;
});

const FXA_ENABLED = "identity.fxaccounts.enabled";
const NETWORK_STATUS_CHANGED = "network:offline-status-changed";
const SYNC_SERVICE_ERROR = "weave:service:sync:error";
const SYNC_SERVICE_FINISHED = "weave:service:sync:finish";
const TOPIC_DEVICESTATE_CHANGED = "firefox-view.devicestate.changed";

const ErrorType = Object.freeze({
  SYNC_ERROR: "sync-error",
  FXA_ADMIN_DISABLED: "fxa-admin-disabled",
  NETWORK_OFFLINE: "network-offline",
  SYNC_DISCONNECTED: "sync-disconnected",
  PASSWORD_LOCKED: "password-locked",
  SIGNED_OUT: "signed-out",
});

export const SyncedTabsErrorHandler = {
  init() {
    this.networkIsOnline = Services.io.connectivity && !Services.io.offline;
    this.syncIsConnected = lazy.UIState.get().syncEnabled;
    this.syncIsWorking = true;

    Services.obs.addObserver(this, NETWORK_STATUS_CHANGED);
    Services.obs.addObserver(this, lazy.UIState.ON_UPDATE);
    Services.obs.addObserver(this, SYNC_SERVICE_ERROR);
    Services.obs.addObserver(this, SYNC_SERVICE_FINISHED);
    Services.obs.addObserver(this, TOPIC_DEVICESTATE_CHANGED);

    return this;
  },

  get fxaSignedIn() {
    let { UIState } = lazy;
    let syncState = UIState.get();
    return (
      UIState.isReady() &&
      syncState.status === UIState.STATUS_SIGNED_IN &&
      // syncEnabled just checks the "services.sync.username" pref has a value
      syncState.syncEnabled
    );
  },

  getErrorType() {
    // this ordering is important for dealing with multiple errors at once
    const errorStates = {
      [ErrorType.NETWORK_OFFLINE]: !this.networkIsOnline,
      [ErrorType.FXA_ADMIN_DISABLED]: Services.prefs.prefIsLocked(FXA_ENABLED),
      [ErrorType.PASSWORD_LOCKED]: this.isPrimaryPasswordLocked,
      [ErrorType.SIGNED_OUT]:
        lazy.UIState.get().status === lazy.UIState.STATUS_LOGIN_FAILED,
      [ErrorType.SYNC_DISCONNECTED]: !this.syncIsConnected,
      [ErrorType.SYNC_ERROR]: !this.syncIsWorking && !this.syncHasWorked,
    };

    for (let [type, value] of Object.entries(errorStates)) {
      if (value) {
        return type;
      }
    }
    return null;
  },

  getFluentStringsForErrorType(type) {
    const mappings = Services.prefs.getBoolPref("browser.nova.enabled", false)
      ? this._novaErrorStateStringMappings
      : this._errorStateStringMappings;
    return Object.freeze(mappings[type]);
  },

  get isPrimaryPasswordLocked() {
    return lazy.syncUtils.mpLocked();
  },

  isSyncReady() {
    const fxaStatus = lazy.UIState.get().status;
    return (
      this.networkIsOnline &&
      (this.syncIsWorking || this.syncHasWorked) &&
      !Services.prefs.prefIsLocked(FXA_ENABLED) &&
      // it's an error for sync to not be connected if we are signed-in,
      // or for sync to be connected if the FxA status is "login_failed",
      // which can happen if a user updates their password on another device
      ((!this.syncIsConnected && fxaStatus !== lazy.UIState.STATUS_SIGNED_IN) ||
        (this.syncIsConnected &&
          fxaStatus !== lazy.UIState.STATUS_LOGIN_FAILED)) &&
      // We treat a locked primary password as an error if we are signed-in.
      // If the user dismisses the prompt to unlock, they can use the "Try again" button to prompt again
      (!this.isPrimaryPasswordLocked || !this.fxaSignedIn)
    );
  },

  observe(_, topic, data) {
    switch (topic) {
      case NETWORK_STATUS_CHANGED:
        this.networkIsOnline = data == "online";
        break;
      case lazy.UIState.ON_UPDATE:
        this.syncIsConnected = lazy.UIState.get().syncEnabled;
        break;
      case SYNC_SERVICE_ERROR:
        if (lazy.UIState.get().status == lazy.UIState.STATUS_SIGNED_IN) {
          this.syncIsWorking = false;
        }
        break;
      case SYNC_SERVICE_FINISHED:
        if (!this.syncIsWorking) {
          this.syncIsWorking = true;
          this.syncHasWorked = true;
        }
        break;
      case TOPIC_DEVICESTATE_CHANGED:
        this.syncHasWorked = false;
    }
  },

  ErrorType,

  // We map the error state strings to Fluent string IDs so that it's easier
  // to change strings in the future without having to update all of the
  // error state strings.
  _errorStateStringMappings: {
    [ErrorType.SYNC_ERROR]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-sync-error-header",
      description: "firefoxview-tabpickup-generic-sync-error-description",
      buttonLabel: "firefoxview-tabpickup-sync-error-primarybutton",
    },
    [ErrorType.FXA_ADMIN_DISABLED]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-fxa-admin-disabled-header",
      description: "firefoxview-tabpickup-fxa-disabled-by-policy-description",
      // The button is hidden for this errorState, so we don't include the
      // buttonLabel property.
    },
    [ErrorType.NETWORK_OFFLINE]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-network-offline-header",
      description: "firefoxview-tabpickup-network-offline-description",
      buttonLabel: "firefoxview-tabpickup-network-offline-primarybutton",
    },
    [ErrorType.SYNC_DISCONNECTED]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-sync-disconnected-header",
      description: "firefoxview-tabpickup-sync-disconnected-description",
      buttonLabel: "firefoxview-tabpickup-sync-disconnected-primarybutton",
    },
    [ErrorType.PASSWORD_LOCKED]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-password-locked-header",
      description: "firefoxview-tabpickup-password-locked-description",
      buttonLabel: "firefoxview-tabpickup-password-locked-primarybutton",
      link: {
        label: "firefoxview-tabpickup-password-locked-link",
        href:
          Services.urlFormatter.formatURLPref("app.support.baseURL") +
          "primary-password-stored-logins",
      },
    },
    [ErrorType.SIGNED_OUT]: {
      asset: "chrome://browser/content/firefoxview/synced-tabs-error.svg",
      header: "firefoxview-tabpickup-signed-out-header",
      description: "firefoxview-tabpickup-signed-out-description2",
      buttonLabel: "firefoxview-tabpickup-signed-out-primarybutton",
    },
  },

  _novaErrorStateStringMappings: {
    [ErrorType.SYNC_ERROR]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices-error.svg",
      header: "firefoxview-tabpickup-sync-error-header-2",
      description: "firefoxview-tabpickup-generic-sync-error-description-2",
      buttonLabel: "firefoxview-tabpickup-sync-error-primarybutton",
    },
    [ErrorType.FXA_ADMIN_DISABLED]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices-error.svg",
      header: "firefoxview-tabpickup-fxa-admin-disabled-header-2",
      description: "firefoxview-tabpickup-fxa-disabled-by-policy-description-2",
      // The button is hidden for this errorState, so we don't include the
      // buttonLabel property.
    },
    [ErrorType.NETWORK_OFFLINE]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices-error.svg",
      header: "firefoxview-tabpickup-network-offline-header-2",
      description: "firefoxview-tabpickup-network-offline-description-2",
      buttonLabel: "firefoxview-tabpickup-network-offline-primarybutton",
    },
    [ErrorType.SYNC_DISCONNECTED]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices.svg",
      header: "firefoxview-syncedtabs-synctabs-header-2",
      description: "firefoxview-syncedtabs-synctabs-description-2",
      buttonLabel: "firefoxview-tabpickup-synctabs-primarybutton-2",
    },
    [ErrorType.PASSWORD_LOCKED]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices-error.svg",
      header: "firefoxview-tabpickup-password-locked-header-2",
      description: "firefoxview-tabpickup-password-locked-description-2",
      buttonLabel: "firefoxview-tabpickup-password-locked-primarybutton",
      link: {
        label: "firefoxview-tabpickup-password-locked-link",
        href:
          Services.urlFormatter.formatURLPref("app.support.baseURL") +
          "primary-password-stored-logins",
      },
    },
    [ErrorType.SIGNED_OUT]: {
      asset: "chrome://browser/skin/sidebar/kit-tabs-devices-error.svg",
      header: "firefoxview-tabpickup-signed-out-header-2",
      description: "firefoxview-tabpickup-signed-out-description-2",
      buttonLabel: "firefoxview-tabpickup-signed-out-primarybutton",
    },
  },
}.init();
