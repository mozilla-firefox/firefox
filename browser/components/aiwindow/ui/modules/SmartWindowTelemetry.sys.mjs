/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* This singleton is for telemetry events that benefit from shared state management.
 * Simple events to be handled with inline Glean calls */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import {
  asString,
  CLIENT_ERROR_SOURCES,
  extractClientErrorFields,
  normalizeClientErrorMessage,
} from "chrome://browser/content/aiwindow/modules/ClientErrorTelemetry.mjs";

const ONE_HOUR_MS = 60 * 60 * 1000;
// Per-session cap on identical client_error emissions. Prevents a hot render
// loop from flooding Glean with thousands of copies of the same bug; the cap
// is per (source, name, message) tuple because file/line drifts across builds
// while the bug class stays the same.
const CLIENT_ERROR_EMIT_CAP = 20;
const clientErrorEmitCounts = new Map();
const PREF_MODEL_CHOICE = "browser.smartwindow.firstrun.modelChoice";
const PREF_MEMORIES_FROM_CONVERSATION =
  "browser.smartwindow.memories.generateFromConversation";
const PREF_MEMORIES_FROM_HISTORY =
  "browser.smartwindow.memories.generateFromHistory";
const PREF_IS_DEFAULT_WINDOW = "browser.smartwindow.isDefaultWindow";
const PREF_SMARTWINDOW_ENABLED = "browser.smartwindow.enabled";
const PREF_SIDEBAR_OPEN_BY_DEFAULT =
  "browser.smartwindow.sidebar.openByDefault";
const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "modelChoice",
  PREF_MODEL_CHOICE,
  "",
  () => SmartWindowTelemetry.updateModelMetric()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "memoriesFromConversation",
  PREF_MEMORIES_FROM_CONVERSATION,
  false,
  () => SmartWindowTelemetry.updateMemoriesFromConversationMetric()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "memoriesFromHistory",
  PREF_MEMORIES_FROM_HISTORY,
  false,
  () => SmartWindowTelemetry.updateMemoriesFromHistoryMetric()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "isDefaultWindow",
  PREF_IS_DEFAULT_WINDOW,
  false,
  () => SmartWindowTelemetry.updateSetDefaultOptinMetric()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "smartWindowEnabled",
  PREF_SMARTWINDOW_ENABLED,
  false,
  () => SmartWindowTelemetry.updateEnabledMetric()
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarOpenByDefault",
  PREF_SIDEBAR_OPEN_BY_DEFAULT,
  true,
  () => SmartWindowTelemetry.recordSidebarOpenByDefaultChange()
);

ChromeUtils.defineESModuleGetters(lazy, {
  AuthProvider:
    "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs",
  getModelForChoice:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
});

export const SmartWindowTelemetry = {
  _initialized: false,
  lastUriLoadTimestamp: 0,

  init() {
    if (this._initialized) {
      return;
    }
    this._initialized = true;

    this.updateModelMetric().catch(console.error);
    this.updateMemoriesFromConversationMetric();
    this.updateMemoriesFromHistoryMetric();
    this.updateSetDefaultOptinMetric();
    this.updateEnabledMetric();
    this.updateSidebarOpenByDefaultMetric();
    this.initAuthTelemetry();
  },

  /**
   * Listens for the events AuthProvider dispatches, and sets auth_impl if a
   * provider was chosen before init.
   */
  initAuthTelemetry() {
    for (const type of [
      "ImplChanged",
      "ImplSwitched",
      "SignInFlowStarted",
      "SignInFlowCompleted",
    ]) {
      lazy.AuthProvider.addEventListener(
        `AuthProvider:${type}`,
        this.onAuthProviderEvent
      );
    }
    if (lazy.AuthProvider.currentProviderName) {
      Glean.smartWindow.authImpl.set(lazy.AuthProvider.currentProviderName);
    }
  },

  /**
   * Records the Glean data for an AuthProvider event. The sign-in events get
   * the provider from AuthProvider.currentProviderName, as the flow does not
   * report it.
   *
   * @param {CustomEvent} event
   */
  onAuthProviderEvent({ type, detail }) {
    const provider = lazy.AuthProvider.currentProviderName ?? undefined;
    switch (type) {
      case "AuthProvider:ImplChanged":
        Glean.smartWindow.authImpl.set(detail.providerName);
        break;
      case "AuthProvider:ImplSwitched":
        Glean.smartWindow.authImplSwitched.record(detail);
        break;
      case "AuthProvider:SignInFlowStarted":
        Glean.smartWindow.signinFlowStarted.record({
          ...detail,
          auth_provider: provider,
        });
        break;
      case "AuthProvider:SignInFlowCompleted":
        Glean.smartWindow.signinFlowCompleted.record({
          ...detail,
          auth_provider: provider,
        });
        break;
    }
  },

  updateMemoriesFromConversationMetric() {
    const memoriesFromConversation = lazy.memoriesFromConversation;
    Glean.smartWindow.memoriesOptin.generate_from_conversation.set(
      memoriesFromConversation
    );
  },

  updateMemoriesFromHistoryMetric() {
    const memoriesFromHistory = lazy.memoriesFromHistory;
    Glean.smartWindow.memoriesOptin.generate_from_history.set(
      memoriesFromHistory
    );
  },

  updateSetDefaultOptinMetric() {
    Glean.smartWindow.setDefaultOptin.set(lazy.isDefaultWindow);
  },

  updateEnabledMetric() {
    Glean.smartWindow.enabled.set(lazy.smartWindowEnabled);
  },

  updateSidebarOpenByDefaultMetric() {
    Glean.smartWindow.sidebarOpenByDefault.set(lazy.sidebarOpenByDefault);
  },

  recordSidebarOpenByDefaultChange() {
    this.updateSidebarOpenByDefaultMetric();
    Glean.smartWindow.sidebarOpenByDefaultChange.record({
      enabled: lazy.sidebarOpenByDefault,
    });
  },

  async updateModelMetric() {
    const modelInfo = await lazy.getModelForChoice(lazy.modelChoice);
    Glean.smartWindow.model.set(modelInfo?.model ?? "unset");
  },

  /**
   * Records a Smart Window client-side runtime/render failure to telemetry.
   *
   * Forgiving by design: anything thrown can flow through here, including
   * plain strings and DOMExceptions from a different realm, so callers do not
   * need to pre-normalize.
   *
   * @param {unknown} error
   *   The thrown value. Error name and source location are pulled from it.
   * @param {object} [opts]
   * @param {string} opts.source
   *   A member of CLIENT_ERROR_SOURCES.
   * @param {string} [opts.messageKey]
   *   Message key the caller has already settled on, for failures whose
   *   meaning is only known where they were caught.
   * @param {object} [opts.context]
   *   Optional correlation extras already resolved on the caller side
   *   (location, chat_id, message_seq, model). Useful when the caller has
   *   them in hand and we don't want this module to reach across processes
   *   to find the active conversation.
   */
  recordClientError(error, { source, messageKey, context } = {}) {
    this.recordClientErrorDetail(
      { source, messageKey, ...extractClientErrorFields(error) },
      context
    );
  },

  /**
   * Records a failure whose fields have already been extracted. This is the
   * shape AIChatContentParent relays from the chat content document, where the
   * thrown object itself cannot cross the process boundary.
   *
   * @param {object} detail
   *   As built by ClientErrorTelemetry.serializeClientErrorDetail():
   *   {source, messageKey, name, message, filename, lineno}.
   * @param {object} [context]
   *   Correlation extras, as for recordClientError().
   */
  recordClientErrorDetail(detail, context = {}) {
    // A relayed detail comes from another process, so treat its fields as
    // input rather than assuming the shape survived intact.
    const name = asString(detail?.name);
    const filename = asString(detail?.filename);
    const lineno = Number.isFinite(detail?.lineno) ? detail.lineno : 0;

    let source = detail?.source;
    if (!CLIENT_ERROR_SOURCES.has(source)) {
      console.warn(
        `SmartWindowTelemetry: unknown client_error source ${JSON.stringify(source)}, bucketing as "uncaught"`
      );
      source = "uncaught";
    }

    const message = normalizeClientErrorMessage({
      source,
      messageKey: detail?.messageKey,
      message: detail?.message,
    });

    const dedupKey = `${source}|${name}|${message}`;
    const count = (clientErrorEmitCounts.get(dedupKey) ?? 0) + 1;
    clientErrorEmitCounts.set(dedupKey, count);
    if (count > CLIENT_ERROR_EMIT_CAP) {
      return;
    }

    Glean.smartWindow.clientError.record({
      location: context.location ?? "",
      chat_id: context.chat_id ?? "",
      message_seq: context.message_seq ?? 0,
      model: context.model ?? "",
      source,
      name,
      message,
      filename,
      lineno,
    });
  },

  _resetClientErrorDedupForTests() {
    clientErrorEmitCounts.clear();
  },

  async recordUriLoad() {
    const now = Date.now();

    // Throttle to once per hour to capture activity at event time rather than
    // relying on daily metric submission, while avoiding duplicate events.
    if (now - this.lastUriLoadTimestamp < ONE_HOUR_MS) {
      return false;
    }

    this.lastUriLoadTimestamp = now;

    const modelInfo = await lazy.getModelForChoice(lazy.modelChoice);
    Glean.smartWindow.uriLoad.record({
      model: modelInfo?.model ?? "unset",
    });

    return true;
  },
};
