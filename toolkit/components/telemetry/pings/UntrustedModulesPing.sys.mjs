/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This module periodically sends a Telemetry ping containing information
 * about untrusted module loads on Windows.
 *
 * https://firefox-source-docs.mozilla.org/toolkit/components/telemetry/telemetry/data/third-party-modules-ping.html
 */

import { Log } from "resource://gre/modules/Log.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TelemetryUtils: "resource://gre/modules/TelemetryUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  UpdateTimerManager: [
    "@mozilla.org/updates/timer-manager;1",
    Ci.nsIUpdateTimerManager,
  ],
});

const DEFAULT_INTERVAL_SECONDS = 24 * 60 * 60; // 1 day

const LOGGER_NAME = "Toolkit.Telemetry";
const LOGGER_PREFIX = "TelemetryUntrustedModulesPing::";
const TIMER_NAME = "telemetry_untrustedmodules_ping";

export var TelemetryUntrustedModulesPing = Object.freeze({
  _log: Log.repository.getLoggerWithMessagePrefix(LOGGER_NAME, LOGGER_PREFIX),

  start() {
    lazy.UpdateTimerManager.registerTimer(
      TIMER_NAME,
      this,
      Services.prefs.getIntPref(
        lazy.TelemetryUtils.Preferences.UntrustedModulesPingFrequency,
        DEFAULT_INTERVAL_SECONDS
      )
    );
  },

  notify() {
    try {
      Services.telemetry.submitAndGetUntrustedModulePayload().catch(ex => {
        // NS_ERROR_NOT_AVAILABLE means no untrusted module load event was
        // collected since the last ping.
        if (ex.result != Cr.NS_ERROR_NOT_AVAILABLE) {
          this._log.error("submitAndGetUntrustedModulePayload failed", ex);
        }
      });
    } catch (ex) {
      this._log.error("notify() caught an exception", ex);
    }
  },
});
