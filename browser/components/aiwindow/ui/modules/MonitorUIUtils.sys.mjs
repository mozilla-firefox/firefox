/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AGENT_UI_TYPES:
    "moz-src:///browser/components/aiwindow/ui/modules/AgentUI.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CHAT_ROLE_LABELS:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs",
  isAllowedWatchUrl:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  isBareAgentCommand:
    "chrome://browser/content/aiwindow/modules/AgentCommands.mjs",
  MAX_CHAT_HISTORY_LENGTH:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs",
  MESSAGE_ROLE:
    "moz-src:///browser/components/aiwindow/models/Conversation.sys.mjs",
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  MonitorSetup:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorSetup.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  TOTAL_NUM_URLS_IN_MONITOR:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
  RegionLocaleMap: "moz-src:///toolkit/modules/RegionLocaleMap.sys.mjs",
  SCHEDULE_TYPES:
    "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

const localization = new Localization(
  [
    "preview/aiWindow.ftl",
    "branding/brand.ftl",
    "toolkit/branding/brandings.ftl",
  ],
  true
);

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const SUPPORTED_REGIONS_DEFAULT = [
  ["US", ["*"]],
  ["CA", ["en-*"]],
  ["GB", ["en-*"]],
];

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "monitorSupportedRegions",
  "browser.smartwindow.agent.supportedRegions",
  JSON.stringify(SUPPORTED_REGIONS_DEFAULT),
  null,
  json =>
    lazy.RegionLocaleMap.fromJSON(json, {
      fallback: SUPPORTED_REGIONS_DEFAULT,
      onInvalid: value =>
        console.warn("Invalid JSON in supportedRegions pref:", value),
    })
);

/**
 * Titles of the pages open in non-private windows.
 *
 * @returns {Map<string, string>} Map of url -> page title
 */
function getOpenTabTitles() {
  const titles = new Map();
  for (const win of lazy.BrowserWindowTracker.getOrderedWindows({
    private: false,
  })) {
    for (const { linkedBrowser } of win.gBrowser.tabs) {
      const url = linkedBrowser.currentURI?.spec;
      if (url && linkedBrowser.contentTitle && !titles.has(url)) {
        titles.set(url, linkedBrowser.contentTitle);
      }
    }
  }
  return titles;
}

export const DAILY_SCHEDULE_L10N_ID = "ai-tasks-alert-schedule-daily-at";

// Indexed by the weekday values used by the scheduler (0 = Sunday)
export const WEEKLY_SCHEDULE_L10N_IDS = Object.freeze([
  "ai-tasks-alert-schedule-weekly-sunday",
  "ai-tasks-alert-schedule-weekly-monday",
  "ai-tasks-alert-schedule-weekly-tuesday",
  "ai-tasks-alert-schedule-weekly-wednesday",
  "ai-tasks-alert-schedule-weekly-thursday",
  "ai-tasks-alert-schedule-weekly-friday",
  "ai-tasks-alert-schedule-weekly-saturday",
]);

/**
 * A monitor as the chat model is told about it: English, with no ids or
 * check history. Not the stored monitor object. Users see monitors as
 * "tasks", so this is named for that rather than for the monitor.
 *
 * @typedef {object} WatchTask
 * @property {"watching"|"paused"|"expired"|"deleted"} status - Whether the
 *   monitor is running
 * @property {string} name - The task name
 * @property {string[]} watchUrls - Pages the monitor checks
 * @property {string} condition - What the user asked to be notified about
 * @property {string} schedule - When it checks, e.g. "weekly on Monday at 08:30"
 */

const englishWeekdayFormat = new Intl.DateTimeFormat("en-US", {
  weekday: "long",
  timeZone: "UTC",
});

/**
 * Describes a stored schedule in English, e.g. "weekly on Monday at 08:30"
 *
 * @param {?object} schedule - A serialized IntervalSchedule, DailySchedule or
 *   WeeklySchedule
 * @returns {string}
 */
function describeScheduleForModel(schedule) {
  const pad = n => String(n ?? 0).padStart(2, "0");
  const time = `${pad(schedule?.hour)}:${pad(schedule?.minute)}`;
  switch (schedule?.type) {
    case lazy.SCHEDULE_TYPES.INTERVAL:
      return schedule.hours === 1
        ? "every hour"
        : `every ${schedule.hours} hours`;
    case lazy.SCHEDULE_TYPES.DAILY:
      return `daily at ${time}`;
    case lazy.SCHEDULE_TYPES.WEEKLY: {
      // January 1, 2023 was a Sunday, matching the scheduler's weekday 0
      const weekday = englishWeekdayFormat.format(
        Date.UTC(2023, 0, 1 + schedule.weekday)
      );
      return `weekly on ${weekday} at ${time}`;
    }
    default:
      return "unknown";
  }
}

// How much of the chat a monitor setup suggestion is told about
const CHAT_HISTORY_MAX_MESSAGES = 10;
const CHAT_HISTORY_MAX_MESSAGE_LENGTH = 2000;

/**
 * Whether a message is something the user or the assistant said. Text the
 * app put in the conversation is told apart by how the message is marked,
 * never by what it says.
 *
 * @param {ChatMessage} message
 * @returns {boolean}
 */
function isRealChatMessage({ role, content, isActiveBranch, toolUIData }) {
  if (
    isActiveBranch === false ||
    content?.type !== "text" ||
    typeof content.body !== "string" ||
    !content.body.trim()
  ) {
    return false;
  }
  if (role === lazy.MESSAGE_ROLE.USER) {
    return !lazy.isBareAgentCommand(content.body);
  }
  return (
    role === lazy.MESSAGE_ROLE.ASSISTANT &&
    !content.l10nId &&
    !content.monitorLog &&
    !content.watchTask &&
    toolUIData?.uiType !== lazy.AGENT_UI_TYPES.MONITOR_ITEM
  );
}

/**
 * The recent chat as context for a monitor setup suggestion: the last few
 * things the user and the assistant said, oldest first, one "User: ..." or
 * "Assistant: ..." line each. When they don't all fit, the oldest are left
 * out.
 *
 * @param {?ChatConversation} conversation
 * @returns {string} "" when the conversation has nothing to tell
 */
export function formatChatHistoryForSuggestion(conversation) {
  const labels = new Map([
    [lazy.MESSAGE_ROLE.USER, lazy.CHAT_ROLE_LABELS.user],
    [lazy.MESSAGE_ROLE.ASSISTANT, lazy.CHAT_ROLE_LABELS.assistant],
  ]);
  const lines = (conversation?.messages ?? [])
    .filter(isRealChatMessage)
    .slice(-CHAT_HISTORY_MAX_MESSAGES)
    .map(
      ({ role, content }) =>
        `${labels.get(role)}: ${content.body
          .replace(/\s+/g, " ")
          .trim()
          .slice(0, CHAT_HISTORY_MAX_MESSAGE_LENGTH)}`
    );

  const kept = [];
  let length = 0;
  for (const line of lines.reverse()) {
    length += line.length + (kept.length ? 1 : 0);
    if (length > lazy.MAX_CHAT_HISTORY_LENGTH) {
      break;
    }
    kept.unshift(line);
  }
  return kept.join("\n");
}

/**
 * Shared utilities for monitor UI operations
 */
export const MonitorUIUtils = {
  /**
   * Show a confirmation prompt before deleting a monitor and delete if confirmed.
   *
   * @param {BrowsingContext} browsingContext - The browsing context for the prompt
   * @param {string} monitorId - The ID of the monitor to delete
   * @param {boolean} skipConfirmation - Skip confirmation dialog (for tests)
   * @returns {Promise<{success: boolean, deleted: boolean, cancelled: boolean}>}
   */
  async deleteMonitorWithConfirmation(
    browsingContext,
    monitorId,
    skipConfirmation = false
  ) {
    try {
      let confirmed = skipConfirmation;

      if (!skipConfirmation) {
        // Localize the prompt strings
        const [title, message, deleteButton] = await localization.formatValues([
          { id: "ai-tasks-alert-delete-confirmation-title" },
          { id: "ai-tasks-alert-delete-confirmation-message" },
          { id: "ai-tasks-alert-delete-confirm-button" },
        ]);

        // Set up the button flags for the prompt
        const flags =
          (Ci.nsIPromptService.BUTTON_TITLE_IS_STRING *
            Ci.nsIPromptService.BUTTON_POS_0) |
          (Ci.nsIPromptService.BUTTON_TITLE_CANCEL *
            Ci.nsIPromptService.BUTTON_POS_1) |
          Ci.nsIPromptService.BUTTON_POS_1_DEFAULT;

        // Show the confirmation prompt
        const result = await Services.prompt.asyncConfirmEx(
          browsingContext,
          Ci.nsIPrompt.MODAL_TYPE_INTERNAL_WINDOW,
          title,
          message,
          flags,
          deleteButton,
          null,
          null,
          null,
          false,
          { useTitle: true }
        );

        // Check if user clicked Delete (button 0)
        confirmed = result.get("buttonNumClicked") === 0;
      }

      if (!confirmed) {
        return {
          success: true,
          deleted: false,
          cancelled: true,
        };
      }

      // User confirmed, proceed with deletion
      const deleted = await lazy.MonitorAgent.deleteMonitor(monitorId);
      return {
        success: true,
        deleted,
        cancelled: false,
      };
    } catch (error) {
      console.error("Failed to delete monitor:", error);
      return {
        success: false,
        deleted: false,
        cancelled: false,
        error: error.message,
      };
    }
  },

  buildMonitorStatus(monitor) {
    // Return the status with kind, let agent-monitor-item handle the localization
    const monitorStatus = monitor.enabled
      ? { kind: "watching" }
      : { kind: "paused" };

    return monitorStatus;
  },

  /**
   * Transforms a monitor object to match the display format.
   *
   * @param {object} monitor - The monitor object from the database
   * @returns {object}
   */
  formatMonitorForDisplay(monitor) {
    const monitorStatus = this.buildMonitorStatus(monitor);

    return {
      id: monitor.id,
      monitorName: monitor.title || "",
      url: monitor.watchUrls?.[0] || "",
      watchUrls: monitor.watchUrls || [],
      condition: monitor.monitorPrompt || "",
      enabled: !!monitor.enabled,
      status: monitorStatus,
      checkLimit: monitor.checkLimit ?? null,
      history: (monitor.history || []).slice().reverse(),
      // Pass the schedule data directly - agent-monitor-item will format it using FTL strings
      schedule: monitor.schedule
        ? {
            frequency: monitor.schedule.type,
            time: `${(monitor.schedule.hour ?? 0)
              .toString()
              .padStart(2, "0")}:${(monitor.schedule.minute ?? 0)
              .toString()
              .padStart(2, "0")}`,
            weekday: monitor.schedule.weekday?.toString(),
          }
        : null,
    };
  },

  /**
   * Turns a stored monitor into the watch task the chat model is told about.
   * In code, "monitor" is the stored object, while users and the model call
   * it a "watch task", so this is the one place that translates between the
   * two. The result is plain data for the model: English rather than
   * localized, so the model's history doesn't depend on the UI locale, and
   * without ids or check history.
   *
   * @param {object} monitor - The monitor object from the database
   * @returns {WatchTask}
   */
  formatMonitorForModel(monitor) {
    let status = this.buildMonitorStatus(monitor).kind;
    if (monitor.expiry) {
      status = "expired";
    }
    return {
      status,
      name: monitor.title || "",
      watchUrls: monitor.watchUrls || [],
      condition: monitor.monitorPrompt || "",
      schedule: describeScheduleForModel(monitor.schedule),
    };
  },

  /**
   * The Fluent string that states when a monitor checks, e.g. "Check daily at
   * 9:00 AM". Callers render it themselves so the string can be localized in
   * whichever document they live in.
   *
   * @param {?object} schedule - A schedule as returned by
   *   formatMonitorForDisplay: { frequency, time: "HH:MM", weekday }
   * @returns {?{id: string, args: object}} Null when there is nothing to state.
   */
  getScheduleL10n(schedule) {
    if (!schedule?.time) {
      return null;
    }
    const [hour, minute] = schedule.time.split(":").map(Number);
    if (!Number.isInteger(hour) || !Number.isInteger(minute)) {
      return null;
    }

    const id =
      schedule.frequency === lazy.SCHEDULE_TYPES.WEEKLY
        ? WEEKLY_SCHEDULE_L10N_IDS[Number(schedule.weekday)]
        : DAILY_SCHEDULE_L10N_ID;
    if (!id) {
      return null;
    }

    // The strings take a datetime, so the time of day is put on today's date.
    const time = new Date();
    time.setHours(hour, minute, 0, 0);
    return { id, args: { time: time.getTime() } };
  },

  /**
   * Open a page a monitor watches, switching to it when it is already open.
   *
   * @param {ChromeWindow} chromeWindow - The window to open the page in
   * @param {string} url - The watched page URL
   * @returns {{success: boolean, error?: string}}
   */
  openMonitorUrl(chromeWindow, url) {
    try {
      if (!url) {
        return { success: false, error: "No URL provided" };
      }

      const uri = Services.io.newURI(url);
      if (uri.scheme !== "http" && uri.scheme !== "https") {
        return { success: false, error: `Unsupported scheme: ${uri.scheme}` };
      }

      if (!chromeWindow) {
        return { success: false, error: "No chrome window" };
      }

      if (
        !lazy.URILoadingHelper.switchToTabHavingURI(
          chromeWindow,
          url,
          false,
          {}
        )
      ) {
        const { userContextId } =
          chromeWindow.gBrowser.selectedBrowser.browsingContext
            .originAttributes;
        lazy.URILoadingHelper.openWebLinkIn(chromeWindow, url, "tab", {
          triggeringPrincipal:
            Services.scriptSecurityManager.createNullPrincipal({
              userContextId,
            }),
          userContextId,
        });
      }

      return { success: true };
    } catch (error) {
      console.error("Failed to open monitor URL:", error);
      return { success: false, error: error.message };
    }
  },

  /**
   * Resolve page titles for watched URLs from open tabs, then Places, so the
   * monitor card can label its chips with the page title.
   *
   * @param {string[]} urls - Watched page URLs
   * @returns {Promise<Record<string, string>>} Map of url → page title for URLs that are open or have a stored title.
   */
  async resolveWatchUrlTitles(urls) {
    const titles = {};
    const openTabTitles = urls?.length ? getOpenTabTitles() : new Map();
    await Promise.all(
      (urls ?? []).map(async url => {
        if (openTabTitles.has(url)) {
          titles[url] = openTabTitles.get(url);
          return;
        }
        try {
          const info = await lazy.PlacesUtils.history.fetch(url);
          if (info?.title) {
            titles[url] = info.title;
          }
        } catch (error) {
          console.error("Places lookup failed for", url, error);
        }
      })
    );
    return titles;
  },

  /**
   * Answers an agent-monitor-item:resolve-titles request from a card hosted in
   * chrome, which can reach Places without going through an actor. Content
   * hosts are answered by AgentMonitorItemChild instead.
   *
   * @param {CustomEvent} event - The request, still being dispatched
   */
  async answerResolveTitles(event) {
    const card = event.composedPath()[0];
    try {
      const titles = await this.resolveWatchUrlTitles(event.detail?.urls);
      card?.dispatchEvent(
        new card.documentGlobal.CustomEvent(
          "agent-monitor-item:titles-resolved",
          { detail: { titles } }
        )
      );
    } catch (error) {
      console.warn("Could not answer watch URL title request", error);
    }
  },

  /**
   * Suggests a name and a condition for a monitor being created, grounded in
   * the pages, their titles, the recent chat and the user's other monitors.
   *
   * @param {object} request
   * @param {string[]} request.watchUrls - Pages in the create form
   * @param {string} [request.chatHistory] - The recent chat, from
   *   formatChatHistoryForSuggestion. Only chat hosts have one.
   * @param {Array<{title: string, condition: string}>} [request.previousDrafts]
   *   - What the card was already given for this form
   * @param {boolean} [request.useMemories] - Whether chat would use memories
   *   for its next message. Only chat hosts have a switch to follow.
   * @returns {Promise<?{title: string, condition: string}>} null when there is
   *   nothing to suggest
   */
  async suggestMonitorSetup({
    watchUrls,
    chatHistory = "",
    previousDrafts = [],
    useMemories = true,
  }) {
    const urls = (Array.isArray(watchUrls) ? watchUrls : [])
      .filter(url => typeof url === "string" && lazy.isAllowedWatchUrl(url))
      .slice(0, lazy.TOTAL_NUM_URLS_IN_MONITOR);
    const [pageTitles, monitors] = await Promise.all([
      this.resolveWatchUrlTitles(urls),
      lazy.MonitorAgent.listMonitors().catch(() => []),
    ]);
    return lazy.MonitorSetup.suggest({
      watchUrls: urls,
      pageTitles,
      chatHistory: typeof chatHistory === "string" ? chatHistory : "",
      existingTitles: monitors.map(monitor => monitor.title),
      previousDrafts,
      useMemories,
    });
  },

  /**
   * Answers an agent-monitor-item:request-suggestion from a card hosted in
   * chrome. The card waits for the answer, so one is always sent, with empty
   * fields when there is nothing to suggest.
   *
   * @param {CustomEvent} event - The request, still being dispatched
   */
  async answerSuggestionRequest(event) {
    const card = event.composedPath()[0];
    const { requestId, watchUrls, previousDrafts } = event.detail ?? {};
    let suggestion = null;
    try {
      suggestion = await this.suggestMonitorSetup({
        watchUrls,
        previousDrafts,
      });
    } catch (error) {
      console.warn("Could not answer monitor suggestion request", error);
    }
    card?.dispatchEvent(
      new card.documentGlobal.CustomEvent("agent-monitor-item:suggestion", {
        detail: {
          requestId,
          title: suggestion?.title ?? "",
          condition: suggestion?.condition ?? "",
        },
      })
    );
  },

  /**
   * Check whether the user's region is allowed to use monitors. The
   * 'browser.smartwindow.agent.supportedRegions' pref pairs each region with
   * the app locales it covers, so a supported region on its own is not enough.
   *
   * @returns {boolean} True if the home region and app locale are supported
   */
  isMonitorRegionSupported() {
    return lazy.monitorSupportedRegions.matches(
      lazy.Region.home,
      Services.locale.appLocaleAsBCP47
    );
  },
};
