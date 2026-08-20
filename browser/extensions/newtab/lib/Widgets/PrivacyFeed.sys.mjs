/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  PrivacyMetricsService:
    "moz-src:///browser/components/protections/PrivacyMetricsService.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  FirefoxRelay: "resource://gre/modules/FirefoxRelay.sys.mjs",
  ProfileAge: "resource://gre/modules/ProfileAge.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

import {
  actionTypes as at,
  actionCreators as ac,
} from "resource://newtab/common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  isWidgetEnabled,
  resolvePrivacyMaxCount,
  resolvePrivacyBlankChance,
  resolvePrivacyShowVpnMessages,
  PREF_PRIVACY_MESSAGE_STATE,
  PREF_PRIVACY_FORCE_MESSAGE_ID,
  PREF_PRIVACY_CELEBRATION_STATE,
  PREF_PRIVACY_FORCE_CELEBRATION,
  resolvePrivacyCelebrationThreshold,
} from "resource://newtab/common/WidgetsRegistry.mjs";
import {
  selectPrivacyMessage,
  periodBounds,
} from "resource://newtab/lib/Widgets/PrivacyMessages.sys.mjs";

const PREF_WIDGETS_ENABLED = "widgets.enabled";
const DAY_MS = 24 * 60 * 60 * 1000;
const STREAK_LOOKBACK_DAYS = 10;

// An awarded count-up celebration older than this is dropped unplayed rather
// than firing on a tab opened long after the trackers were blocked.
const CELEBRATION_WINDOW_MS = 10 * 60 * 1000;

// How far below the live count a forced (debug) celebration counts up from.
const FORCED_COUNT_UP_SPAN = 25;

// UTC to match both readouts, which key off the tracking DB's UTC date.
const utcDayKey = () => new Date().toISOString().slice(0, 10);

const PRIVACY_ENTRY = WIDGET_REGISTRY.find(w => w.id === "privacy");

// Prefs that can flip the widget's registry enablement (incl. trainhop config).
// A change to any of these should re-evaluate whether to start fetching.
const ENABLEMENT_PREFS = new Set([
  PREF_WIDGETS_ENABLED,
  PRIVACY_ENTRY.enabledPref,
  PRIVACY_ENTRY.systemEnabledPref,
  "trainhopConfig",
  // Synthetic value from PrefsFeed; turning history back on resumes fetching.
  "recordsHistory",
]);

/**
 * Feed for the Privacy widget. Runs in the parent process. Reads the daily
 * tracker-blocked count from PrivacyMetricsService (same process, no IPC) for
 * the live readout, and on each new tab runs the message scheduler
 * (PrivacyMessages.sys.mjs) — period/streak totals come straight from
 * TrackingDBService so the whole feature stays trainhoppable (Bug 2050954).
 */
export class PrivacyFeed {
  constructor() {
    // Cached once per session (holds the in-flight/resolved promise, so a
    // failed lookup resolves to null and is NOT retried on every new tab).
    this._profileCreatedMs = null;
    // Serializes updateMessage across concurrent new tabs (see updateMessage).
    this._messageQueue = null;
  }

  get enabled() {
    const prefs = this.store.getState()?.Prefs.values;
    // Share the registry enablement logic the UI uses so trainhop rollouts
    // (trainhopConfig.widgets.privacyEnabled) start the feed even when the
    // system pref defaults false. Otherwise the widget renders but the counter
    // stays stuck at the empty state.
    return isWidgetEnabled(PRIVACY_ENTRY, prefs, prefs?.[PREF_WIDGETS_ENABLED]);
  }

  /**
   * Count of distinct sites visited today, read from the Places history DB.
   *
   * NOTE: this is "sites visited today", not "sites where a tracker was
   * blocked" — the tracking DB records no per-site data, so this is a proxy
   * (see Bug 2048384 discussion). Private-browsing visits are not recorded in
   * Places, so they're inherently excluded.
   *
   * @returns {Promise<number>}
   */
  async getSitesVisitedToday() {
    const db = await lazy.PlacesUtils.promiseDBConnection();
    // Count non-hidden pages only — hidden = 1 covers redirect sources, embeds,
    // and framed visits, so e.g. amazon.com -> www.amazon.com counts as one
    // site, not two.
    //
    // Day boundary is UTC midnight to match the tracker count, which keys on
    // TrackingDBService's UTC date and so can't be local. RISK: both "today"
    // readouts therefore reset at UTC midnight — mid-day for users far from GMT.
    // Inherent to the tracking DB (about:protections behaves the same); a true
    // local-day "today" would need event-level timestamps in TrackingDBService.
    const rows = await db.execute(
      `SELECT COUNT(DISTINCT p.origin_id) AS count
       FROM moz_historyvisits v
       JOIN moz_places p ON v.place_id = p.id
       WHERE p.hidden = 0
       AND v.visit_date >= (strftime('%s','now','start of day') * 1000000)`
    );
    return rows[0]?.getResultByName("count") ?? 0;
  }

  /**
   * Per-period block totals plus the current day-streak, in a single pass over
   * TrackingDBService. week ⊂ month ⊂ year ⊂ all-time, so one
   * getEventsByDateRange(0, now) — bucketed by its stored UTC day string —
   * yields every total (and the streak) instead of a query per range.
   * getEventsByDateRange day-truncates both bounds to a UTC date, and every
   * recorded row is one of the blocked categories, so summing row counts
   * matches the totals PrivacyMetricsService reports.
   *
   * @returns {Promise<{weekTotal: number, monthTotal: number,
   *   yearTotal: number, allTimeTotal: number, streakDays: number}>}
   */
  async getPeriodTotals(now) {
    const bounds = periodBounds(now);
    const rows = await lazy.TrackingDBService.getEventsByDateRange(0, now);
    const perDay = new Map();
    let allTimeTotal = 0;
    for (const row of rows) {
      // timestamp is the stored UTC date string "YYYY-MM-DD".
      const day = row.getResultByName("timestamp");
      const count = row.getResultByName("count");
      perDay.set(day, (perDay.get(day) || 0) + count);
      allTimeTotal += count;
    }

    // Sum days on/after each period's UTC start. periodBounds already gives
    // day-truncated starts, so the >= comparison is exact.
    let weekTotal = 0;
    let monthTotal = 0;
    let yearTotal = 0;
    for (const [day, count] of perDay) {
      const dayMs = Date.parse(`${day}T00:00:00.000Z`);
      if (dayMs >= bounds.year.startMs) {
        yearTotal += count;
      }
      if (dayMs >= bounds.month.startMs) {
        monthTotal += count;
      }
      if (dayMs >= bounds.week.startMs) {
        weekTotal += count;
      }
    }

    // Consecutive days (ending today, UTC) with at least one block — the
    // 3/5/7-day streak celebration.
    let streakDays = 0;
    for (let i = 0; i < STREAK_LOOKBACK_DAYS; i++) {
      const key = new Date(now - i * DAY_MS).toISOString().slice(0, 10);
      if ((perDay.get(key) || 0) > 0) {
        streakDays++;
      } else {
        break;
      }
    }

    return { weekTotal, monthTotal, yearTotal, allTimeTotal, streakDays };
  }

  /**
   * Best-effort flags for promo suppression. Each is guarded: an unavailable
   * signal fails open (false → we don't suppress). VPN and Monitor have no
   * reliable local signal, so their promos are never suppressed.
   *
   * @returns {Promise<{signedIn: boolean, hasLogins: boolean, relayMasks: boolean}>}
   */
  async getFeatureFlags() {
    let signedIn = false;
    let hasLogins = false;
    let relayMasks = false;
    try {
      signedIn = lazy.UIState.get().status === lazy.UIState.STATUS_SIGNED_IN;
    } catch (e) {}
    try {
      hasLogins = (await Services.logins.countLoginsAsync("", "", "")) > 0;
    } catch (e) {}
    try {
      const profile = await lazy.FirefoxRelay.getRelayProfileInfo();
      relayMasks = (profile?.masksCount || 0) > 0;
    } catch (e) {}
    return { signedIn, hasLogins, relayMasks };
  }

  getProfileCreatedMs() {
    this._profileCreatedMs ??= (async () => {
      try {
        const accessor = await lazy.ProfileAge();
        return await accessor.created;
      } catch (e) {
        return null;
      }
    })();
    return this._profileCreatedMs;
  }

  // skipBroadcast JSON pref — read from / written to the parent store only.
  readMessageState() {
    const raw =
      this.store.getState()?.Prefs.values?.[PREF_PRIVACY_MESSAGE_STATE];
    if (!raw) {
      return {};
    }
    try {
      return JSON.parse(raw);
    } catch (e) {
      return {};
    }
  }

  writeMessageState(state) {
    this.store.dispatch(
      ac.SetPref(PREF_PRIVACY_MESSAGE_STATE, JSON.stringify(state))
    );
  }

  // Count-up celebration bookkeeping (HNT-2845), kept in its own JSON pref
  // rather than messageState: selectPrivacyMessage() round-trips that blob
  // through normalizeState(), which would strip any keys it doesn't know.
  readCelebrationState() {
    const raw =
      this.store.getState()?.Prefs.values?.[PREF_PRIVACY_CELEBRATION_STATE];
    // Hand-edited prefs are the documented way to test this, so treat anything
    // that isn't a well-formed object as "no state yet" — throwing here would
    // take the count broadcast down with it.
    let parsed = null;
    if (raw) {
      try {
        parsed = JSON.parse(raw);
      } catch (e) {
        parsed = null;
      }
    }
    if (!parsed || typeof parsed !== "object") {
      parsed = {};
    }
    return {
      date: parsed.date ?? null,
      baselineCount: parsed.baselineCount ?? 0,
      pending: parsed.pending ?? null,
    };
  }

  writeCelebrationState(state) {
    this.store.dispatch(
      ac.SetPref(PREF_PRIVACY_CELEBRATION_STATE, JSON.stringify(state))
    );
  }

  /**
   * Awards a count-up celebration once the count has climbed by the threshold
   * since the last one. The baseline is persisted because trackersToday
   * hydrates from 0 on every tab mount — an in-memory comparison would
   * celebrate on essentially every new tab.
   *
   * @param {number} trackersToday Current blocked count.
   * @returns {object|null} Celebration to broadcast, or null.
   */
  resolveCelebration(trackersToday) {
    const prefs = this.store.getState().Prefs.values;

    // Debug lever (QA/design): force a celebration on every parent count
    // refresh without touching the persisted baseline. Value picks the tier.
    // Drives the animation only — the kit icon follows forceMessageId.
    const forcedTier = prefs[PREF_PRIVACY_FORCE_CELEBRATION];
    if (forcedTier) {
      return {
        awardedAt: Date.now(),
        // Wide enough a span that the count-up is clearly visible, rather than
        // the bare threshold, which is over almost before it starts.
        fromCount: Math.max(0, trackersToday - FORCED_COUNT_UP_SPAN),
        toCount: trackersToday,
        forcedTier,
      };
    }

    const state = this.readCelebrationState();
    const today = utcDayKey();
    // A count below the baseline means the UTC day rolled over between polls
    // or the user cleared history; re-seed instead of celebrating the rebound.
    if (state.date !== today || trackersToday < state.baselineCount) {
      this.writeCelebrationState({
        date: today,
        baselineCount: trackersToday,
        pending: null,
      });
      return null;
    }

    const threshold = resolvePrivacyCelebrationThreshold(prefs);
    if (trackersToday - state.baselineCount >= threshold) {
      const pending = {
        awardedAt: Date.now(),
        fromCount: state.baselineCount,
        toCount: trackersToday,
      };
      this.writeCelebrationState({
        date: today,
        baselineCount: trackersToday,
        pending,
      });
      return pending;
    }

    // Drop an award nothing ever played rather than surfacing it much later.
    if (
      state.pending &&
      Date.now() - state.pending.awardedAt >= CELEBRATION_WINDOW_MS
    ) {
      this.writeCelebrationState({ ...state, pending: null });
      return null;
    }

    return state.pending;
  }

  // The headline count is the total blocked across all categories (cookies,
  // trackers, fingerprinters, cryptominers, social) — the same total
  // about:protections shows — not just the "trackers" slice.
  async fetchTodayCounts() {
    const [stats, sitesToday] = await Promise.all([
      lazy.PrivacyMetricsService.getTodayStats(),
      this.getSitesVisitedToday(),
    ]);
    return {
      trackersToday: stats.total,
      sitesToday,
      lastUpdated: stats.lastUpdated,
    };
  }

  // INIT/SYSTEM_TICK/enablement: keep the live count fresh without re-running
  // the scheduler (no message fields → the reducer keeps the current message).
  async updateCounts() {
    const counts = await this.fetchTodayCounts();
    // Clear countCeiling: it's a one-render display cap set by the daily-cap
    // message. Without this, a tab showing "100+" stays stuck there across
    // count refreshes until its next NEW_TAB_INIT re-runs the scheduler.
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WIDGETS_PRIVACY_UPDATE,
        data: {
          ...counts,
          countCeiling: null,
          celebration: this.resolveCelebration(counts.trackersToday),
        },
      })
    );
  }

  // NEW_TAB_INIT: refresh the count AND run the scheduler to pick this tab's
  // secondary message (or blank). "Seen" = rendered, so a selection counts.
  //
  // Serialized: the store middleware doesn't await onAction, so two tabs
  // opening together would otherwise read the same messageState and the last
  // write would clobber the first.
  //
  // NOTE: the decision is broadcast to every tab, not targeted at the tab that
  // opened. That's deliberate given the preload/rehydration model:
  // NEW_TAB_INIT fires while the tab is a preloaded, not-yet-rehydrated
  // browser, and content's rehydrationMiddleware drops any content-bound action
  // received before rehydration (init-store.mjs). A one-shot OnlyToOneContent
  // aimed at that port is therefore dropped and never re-sent, so the message
  // never appears. A broadcast reaches each tab on a later new-tab/tick once it
  // has rehydrated. Making the decision truly per-tab (Dré, D309610) requires
  // delivering it after the tab rehydrates (e.g. off NEW_TAB_STATE_REQUEST) —
  // tracked with the preload/view-time follow-up. The same preload timing means
  // the throttle can also commit before a tab is viewed.
  updateMessage() {
    this._messageQueue = (this._messageQueue ?? Promise.resolve())
      .catch(() => {})
      .then(() => this._runMessageSelection());
    return this._messageQueue;
  }

  async _runMessageSelection() {
    const now = Date.now();
    const [counts, totals, features, profileCreatedMs] = await Promise.all([
      this.fetchTodayCounts(),
      this.getPeriodTotals(now),
      this.getFeatureFlags(),
      this.getProfileCreatedMs(),
    ]);

    const prefs = this.store.getState().Prefs.values;
    const ctx = {
      trackersToday: counts.trackersToday,
      sitesToday: counts.sitesToday,
      weekTotal: totals.weekTotal,
      monthTotal: totals.monthTotal,
      yearTotal: totals.yearTotal,
      allTimeTotal: totals.allTimeTotal,
      streakDays: totals.streakDays,
      maxCount: resolvePrivacyMaxCount(prefs),
      blankChance: resolvePrivacyBlankChance(prefs),
      // Operator/experiment gate for VPN promos...
      showVpnMessages: resolvePrivacyShowVpnMessages(prefs),
      // ...and the per-user availability gate: only promote the built-in VPN
      // when it's actually available to this user. Read the platform pref
      // directly (not via IPProtection.sys.mjs) to stay train-hop-safe;
      // fails closed if the pref is absent on an older channel.
      vpnEnabled: Services.prefs.getBoolPref(
        "browser.ipProtection.enabled",
        false
      ),
      forceMessageId: prefs[PREF_PRIVACY_FORCE_MESSAGE_ID] || null,
      profileCreatedMs,
      features,
    };

    const prevState = this.readMessageState();
    const { decision, nextState } = selectPrivacyMessage(
      ctx,
      prevState,
      now,
      Math.random
    );
    if (JSON.stringify(nextState) !== JSON.stringify(prevState)) {
      this.writeMessageState(nextState);
    }

    // Broadcast the count + this selection to every tab (see updateMessage for
    // why this can't be a one-shot per-tab send under the preload model).
    this.store.dispatch(
      ac.BroadcastToContent({
        type: at.WIDGETS_PRIVACY_UPDATE,
        data: {
          ...counts,
          variant: decision.variant,
          messageId: decision.messageId,
          category: decision.category,
          icon: decision.icon,
          countArg: decision.countArg,
          cta: decision.cta,
          countCeiling: decision.countCeiling,
          // Orthogonal to the message decision: the count-up animates whatever
          // number is on screen, whichever message the scheduler picked.
          celebration: this.resolveCelebration(counts.trackersToday),
        },
      })
    );
  }

  // A CTA button was clicked in the widget. The message's SpecialMessageAction
  // descriptor rides along in the action; run it against the tab that fired it.
  handleCtaAction(action) {
    const smaAction = action.data?.action;
    const browser = action._target?.browser;
    if (!smaAction || !browser) {
      return;
    }
    lazy.SpecialMessageActions.handleAction(smaAction, browser);
  }

  async onAction(action) {
    switch (action.type) {
      // INIT/SYSTEM_TICK keep the count fresh across the session.
      case at.INIT:
      case at.SYSTEM_TICK:
        if (this.enabled) {
          await this.updateCounts();
        }
        break;
      // A new tab is a fresh impression: refresh the count and pick the message.
      case at.NEW_TAB_INIT:
        if (this.enabled) {
          await this.updateMessage();
        }
        break;
      case at.PREF_CHANGED:
        // Enablement can flip on after startup (e.g. a trainhop rollout lands
        // its config); fetch as soon as it does.
        if (ENABLEMENT_PREFS.has(action.data?.name) && this.enabled) {
          await this.updateCounts();
        }
        break;
      case at.WIDGETS_PRIVACY_CTA:
        this.handleCtaAction(action);
        break;
      // Content played a celebration — clear it so a tab opened afterwards
      // doesn't replay it. Tabs already open have played it, so no rebroadcast.
      case at.WIDGETS_PRIVACY_MARK_CELEBRATED: {
        const state = this.readCelebrationState();
        if (state.pending?.awardedAt === action.data) {
          this.writeCelebrationState({ ...state, pending: null });
        }
        break;
      }
    }
  }
}
