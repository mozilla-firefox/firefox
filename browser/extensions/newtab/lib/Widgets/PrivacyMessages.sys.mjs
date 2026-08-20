/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Privacy widget message scheduling — catalog + selector.
 *
 * The widget always shows today's tracker count. Below it, it shows ONE
 * secondary message — or nothing ("blank"). This module owns the catalog of
 * messages and the pure logic that picks which one to show on each new tab.
 * `PrivacyFeed` gathers the inputs (counts, streak, feature flags, persisted
 * state), calls `selectPrivacyMessage`, persists the returned state, and
 * broadcasts the decision to the component (Bug 2050954).
 *
 * ───────────────────────────── BUSINESS RULES ─────────────────────────────
 *
 * ZERO STATE
 *   No trackers blocked today → the empty state (shield icon, no count, no tip).
 *   A zero *site* count is not a zero state; the component drops that line instead.
 *
 * MESSAGE FAMILIES (the `category` field)
 *   info          Education about tracking / Firefox protections. shield/planet/
 *                 bolt icon by sub-theme.
 *   promo         Suggest adopting another Firefox feature (passwords, VPN,
 *                 Relay masks, sign-in, Monitor). star icon. `feature` tags it
 *                 so we can suppress promos for features already in use.
 *   firstProtection / dailyCap / streak / milestone{Week,Month,Year,Total}
 *                 Earned "celebration" moments. kit icon.
 *
 * CELEBRATIONS
 *   firstProtection  The very first time the count goes above zero (once ever).
 *   dailyCap         Today's count reaches the display cap (the "100+" ceiling).
 *   streak           Protected 3, 5, or 7 days in a row (count-block layout,
 *                    like blank but with a message + kit icon). Each of 3/5/7
 *                    fires once per streak; a broken streak re-arms them.
 *   milestone*       Cumulative total crosses a tier (see MILESTONE_TIERS):
 *                      week  100
 *                      month 250 / 500 / 1K / 2K
 *                      year  5K / 10K / 25K
 *                      total 10K / 25K
 *                    Fires on the next new tab after crossing, at most once per
 *                    day. The highest crossed tier wins; the watermark prevents
 *                    re-firing until the calendar period rolls (week/month/year)
 *                    or never (total).
 *
 * FREQUENCY (ordinary info + promo only — celebrations bypass all of this)
 *   Normal profile:        ≤ 5/day, ≤ 1/hour.       (day = UTC day, see below)
 *   New profile (< 48h):   ≤ 10/day, ≤ 1/15min. Anchored to profile age.
 *   Over a limit → blank. A blank never consumes a slot (only real messages do).
 *   "Seen" = rendered: a message counts the moment it is selected for a new tab.
 *
 * BLANK CHANCE
 *   Even when an info message is eligible, show blank `blankChance` of the time
 *   (default 0.4, pref-controlled) for a calm, high-signal experience. Promos
 *   are not subject to the random blank.
 *
 * PROMO LIMITS (on top of the frequency rules)
 *   ≤ 1 promo/day; the same promo repeats ≤ once/month; and promos are skipped
 *   for features the user already uses (sign-in/Sync, saved logins, Relay masks
 *   are detectable; VPN and Monitor have no local signal so they still rotate).
 *
 * PRECEDENCE (when several qualify on the same new tab)
 *   1. empty (count 0)
 *   2. firstProtection (once ever)
 *   3. streak (3/5/7)
 *   4. milestone (≤ once/day)
 *   5. dailyCap (100+)
 *   6. otherwise an ordinary info/promo message subject to the limits — or blank
 *
 * DAY BOUNDARIES are UTC (matching how the tracker count is stored; same as
 * about:protections), so week/month/year totals reset on UTC boundaries.
 *
 * NOTE (Bug 2048389): the catalog `id`s are real Fluent message ids; the
 * component resolves each as an l10n id with `countArg`. The scheduling logic
 * keys off `category`/`icon`/`countSource`, never the id.
 */

export const CATEGORY = {
  EMPTY: "empty",
  INFO: "info",
  PROMO: "promo",
  FIRST_PROTECTION: "firstProtection",
  DAILY_CAP: "dailyCap",
  STREAK: "streak",
  MILESTONE_WEEK: "milestoneWeek",
  MILESTONE_MONTH: "milestoneMonth",
  MILESTONE_YEAR: "milestoneYear",
  MILESTONE_TOTAL: "milestoneTotal",
};

// Cumulative tiers per period. Highest crossed tier wins; ascending order.
export const MILESTONE_TIERS = {
  week: [100],
  month: [250, 500, 1000, 2000],
  year: [5000, 10000, 25000],
  total: [10000, 25000],
};

// Consecutive-day streak lengths that fire a celebration.
export const STREAK_DAYS = [3, 5, 7];

const DAY_MS = 24 * 60 * 60 * 1000;
const NEW_PROFILE_WINDOW_MS = 48 * 60 * 60 * 1000;
const PROMO_REPEAT_MS = 30 * DAY_MS;
const CAPS = {
  normal: { perDay: 5, intervalMs: 60 * 60 * 1000 },
  newProfile: { perDay: 10, intervalMs: 15 * 60 * 1000 },
};

// The empty-state message (count 0). Fixed, not part of the rotation pool.
export const EMPTY_MESSAGE = {
  id: "newtab-privacy-empty",
  category: CATEGORY.EMPTY,
  icon: "shield",
  countSource: "none",
};

// CTA actions are SpecialMessageAction descriptors (see
// SpecialMessageActions.sys.mjs). The component dispatches them to the parent
// (PrivacyFeed) on click, which calls SpecialMessageActions.handleAction. A
// message with no `cta` renders no button. Small builders keep the catalog
// readable; the destinations come from the Phase 1 message spec.
const ctaAboutPage = args => ({
  type: "OPEN_ABOUT_PAGE",
  data: { args, where: "tab" },
});
const ctaUrl = url => ({ type: "OPEN_URL", data: { args: url, where: "tab" } });
const CTA_PROTECTIONS = ctaAboutPage("protections");
const CTA_LOGINS = ctaAboutPage("logins");
const CTA_USER_PRIVACY = ctaUrl("https://www.firefox.com/en-US/user-privacy/");
const CTA_MISSION = ctaUrl("https://www.mozilla.org/en-US/mission/");
// Monitor/Relay are external properties: tag the referral so they can attribute
// signups to this widget (Bug 2061524). utm_content identifies the message.
// `base` must end in "/" since the query string is appended directly to it.
const ctaAttributedUrl = (base, utmContent) =>
  ctaUrl(
    `${base}?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=${utmContent}`
  );
const MONITOR = "https://monitor.mozilla.org/";
const RELAY = "https://relay.firefox.com/";
const CTA_SET_DEFAULT = { type: "SET_DEFAULT_BROWSER" };
const CTA_SIGNIN = {
  type: "FXA_SIGNIN_FLOW",
  data: { entrypoint: "newtab-privacy-widget" },
};
const CTA_VPN = { type: "IPPROTECTION_ENROLL" };
const CTA_PRIVATE_WINDOW = { type: "OPEN_PRIVATE_BROWSER_WINDOW" };

/**
 * Message catalog. Each entry:
 *   id          the Fluent message id (Bug 2048389); the component resolves it
 *               with `countArg` as the l10n args. The `-cta` companion string
 *               shares this id stem.
 *   category    one of CATEGORY — drives scheduling, NOT inferred from the id
 *   icon        shield | shieldCheck | planet | bolt | star | kit
 *   countSource none | today | week | month | year | total | streak | sites
 *   feature     promos only: signin | relay | vpn | monitor | private-window
 *               (only signin/relay are locally suppressible — see isFeatureInUse)
 *   cta         optional SpecialMessageAction descriptor for the CTA button; the
 *               button's label is the `-cta` companion Fluent id. Omitted → no
 *               button (the zero state and the random blank have none).
 */
export const PRIVACY_MESSAGES = [
  // — Celebrations —
  {
    id: "newtab-privacy-message-first-protection",
    category: CATEGORY.FIRST_PROTECTION,
    icon: "kit",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-daily-cap",
    category: CATEGORY.DAILY_CAP,
    icon: "kit",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-streak",
    category: CATEGORY.STREAK,
    icon: "kit",
    countSource: "streak",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-milestone-week",
    category: CATEGORY.MILESTONE_WEEK,
    icon: "kit",
    countSource: "week",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-milestone-month",
    category: CATEGORY.MILESTONE_MONTH,
    icon: "kit",
    countSource: "month",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-milestone-year",
    category: CATEGORY.MILESTONE_YEAR,
    icon: "kit",
    countSource: "year",
    cta: CTA_PROTECTIONS,
  },
  {
    id: "newtab-privacy-message-milestone-total",
    category: CATEGORY.MILESTONE_TOTAL,
    icon: "kit",
    countSource: "total",
    cta: CTA_PROTECTIONS,
  },

  // — Info (shield: blocking trackers; planet: values/choice; bolt: data/speed) —
  // gist comments name the message; copy lives in newtab.ftl under the id.
  {
    // blocks trackers automatically as you browse
    id: "newtab-privacy-message-info-1",
    category: CATEGORY.INFO,
    icon: "shield",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // tracker blocking helps stop companies following you
    id: "newtab-privacy-message-info-2",
    category: CATEGORY.INFO,
    icon: "shield",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // many sites have trackers
    id: "newtab-privacy-message-info-3",
    category: CATEGORY.INFO,
    icon: "shield",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // choosing Firefox = protection by default
    id: "newtab-privacy-message-info-4",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // blocked trackers = fewer companies can follow
    id: "newtab-privacy-message-info-5",
    category: CATEGORY.INFO,
    icon: "shield",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // we never sell your data
    id: "newtab-privacy-message-info-6",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_USER_PRIVACY,
  },
  {
    // see which trackers Firefox blocked
    id: "newtab-privacy-message-info-7",
    category: CATEGORY.INFO,
    icon: "shield",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // browsing with Firefox supports Mozilla's mission
    id: "newtab-privacy-message-info-8",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_MISSION,
  },
  {
    // make Firefox your default browser
    id: "newtab-privacy-message-info-9",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_SET_DEFAULT,
  },
  {
    // save passwords in Firefox
    id: "newtab-privacy-message-info-10",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_LOGINS,
  },
  {
    // find out how Firefox helps keep browsing private
    id: "newtab-privacy-message-info-11",
    category: CATEGORY.INFO,
    icon: "planet",
    countSource: "none",
    cta: CTA_USER_PRIVACY,
  },
  {
    // save bandwidth on limited data plans
    id: "newtab-privacy-message-info-12",
    category: CATEGORY.INFO,
    icon: "bolt",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },
  {
    // freeing bandwidth for smoother streaming
    id: "newtab-privacy-message-info-13",
    category: CATEGORY.INFO,
    icon: "bolt",
    countSource: "none",
    cta: CTA_PROTECTIONS,
  },

  // — Promo (star icon; `feature` drives suppression) —
  {
    // Monitor: find out if your info is in a data breach
    id: "newtab-privacy-message-promo-monitor-1",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "monitor",
    cta: ctaAttributedUrl(MONITOR, "get-breach-alerts-global"),
  },
  {
    // Monitor: free data breach monitoring, up to 20 emails
    id: "newtab-privacy-message-promo-monitor-2",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "monitor",
    cta: ctaAttributedUrl(MONITOR, "protect-your-info-global"),
  },
  {
    // sign in: encrypt bookmarks/passwords/tabs across devices
    id: "newtab-privacy-message-promo-signin-1",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "signin",
    cta: CTA_SIGNIN,
  },
  {
    // VPN: public Wi-Fi
    id: "newtab-privacy-message-promo-vpn-1",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "vpn",
    cta: CTA_VPN,
  },
  {
    // VPN: airport Wi-Fi
    id: "newtab-privacy-message-promo-vpn-2",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "vpn",
    cta: CTA_VPN,
  },
  {
    // VPN: keep location more private
    id: "newtab-privacy-message-promo-vpn-3",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "vpn",
    cta: CTA_VPN,
  },
  {
    // private window: shared computer
    id: "newtab-privacy-message-promo-private-window-1",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "private-window",
    cta: CTA_PRIVATE_WINDOW,
  },
  {
    // Relay: email mask for sign-ups
    id: "newtab-privacy-message-promo-relay-1",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "relay",
    cta: ctaAttributedUrl(RELAY, "use-email-mask-global"),
  },
  {
    // Relay: protect real address from spam
    id: "newtab-privacy-message-promo-relay-2",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "relay",
    cta: ctaAttributedUrl(RELAY, "protect-your-inbox-global"),
  },
  {
    // Relay: 50 free email masks
    id: "newtab-privacy-message-promo-relay-3",
    category: CATEGORY.PROMO,
    icon: "star",
    countSource: "none",
    feature: "relay",
    cta: ctaAttributedUrl(RELAY, "50-free-email-mask-global"),
  },
];

function startOfUTCDay(ms) {
  const d = new Date(ms);
  return Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate());
}

function utcDayKey(ms) {
  return new Date(startOfUTCDay(ms)).toISOString().slice(0, 10);
}

/**
 * UTC period windows for the milestone totals. The feed queries each total over
 * `startMs..now`; the selector uses `key` to reset a period's watermark when the
 * calendar period rolls. Computed in one place so the two never drift.
 *
 * @param {number} now - Timestamp in ms.
 * @returns {{week: object, month: object, year: object}}
 */
export function periodBounds(now) {
  const d = new Date(now);
  const dayStart = startOfUTCDay(now);
  const mondayOffset = (d.getUTCDay() + 6) % 7; // 0 = Sun -> 6 days back to Mon
  const weekStartMs = dayStart - mondayOffset * DAY_MS;
  const year = d.getUTCFullYear();
  const month = d.getUTCMonth() + 1;
  return {
    week: { startMs: weekStartMs, key: utcDayKey(weekStartMs) },
    month: {
      startMs: Date.UTC(year, d.getUTCMonth(), 1),
      key: `${year}-${month}`,
    },
    year: { startMs: Date.UTC(year, 0, 1), key: `${year}` },
  };
}

function normalizeState(prev) {
  const s = prev || {};
  return {
    firstProtectionShown: !!s.firstProtectionShown,
    lastShownMs: s.lastShownMs || 0,
    dayStamp: s.dayStamp || null,
    shownToday: s.shownToday || 0,
    lastCelebrationDay: s.lastCelebrationDay || null,
    streakFiredAt: s.streakFiredAt || 0,
    milestoneWatermark: { ...(s.milestoneWatermark || {}) },
    promoLastDay: s.promoLastDay || null,
    messageLastShown: { ...(s.messageLastShown || {}) },
    recentId: s.recentId || null,
  };
}

function countArgFor(msg, ctx) {
  switch (msg.countSource) {
    case "today":
      return { count: ctx.trackersToday };
    case "week":
      return { count: ctx.weekTotal };
    case "month":
      return { count: ctx.monthTotal };
    case "year":
      return { count: ctx.yearTotal };
    case "total":
      return { count: ctx.allTimeTotal };
    case "streak":
      return { count: ctx.streakDays };
    case "sites":
      return { count: ctx.sitesToday };
    default:
      return null;
  }
}

function toDecision(variant, msg, countArg, countCeiling = null) {
  return {
    variant,
    messageId: msg.id,
    // The message family (CATEGORY) so the component can tell a celebration
    // from an ordinary tip without matching ids.
    category: msg.category,
    icon: msg.icon,
    countArg,
    // SpecialMessageAction the CTA button dispatches; null → no button.
    cta: msg.cta ?? null,
    // When set, the count readout shows "{countCeiling}+" instead of the real
    // number — only the daily-cap render caps the display (see Privacy.jsx).
    countCeiling,
  };
}

function blankDecision() {
  return {
    variant: "blank",
    messageId: null,
    category: null,
    icon: "shieldCheck",
    countArg: null,
    // Blank still offers a "View protections" CTA. messageId is null (no tip
    // copy), so the component supplies the label id; the action rides here.
    // TODO(follow-up): give the blank state its own CTA string.
    cta: CTA_PROTECTIONS,
    countCeiling: null,
  };
}

function firstByCategory(category) {
  return PRIVACY_MESSAGES.find(m => m.category === category) || null;
}

// Layout variant a message renders as (see Privacy.jsx). Empty and streak have
// their own layouts; everything else is a "tip".
function variantForCategory(category) {
  if (category === "empty") {
    return "empty";
  }
  if (category === CATEGORY.STREAK) {
    return "streak";
  }
  return "tip";
}

// Debug override result for widgets.privacy.forceMessageId: the forced message's
// decision (bypassing the ladder/caps, no state mutation), or null when unset or
// the id is unknown (falls through to normal scheduling).
function forcedResult(ctx, state) {
  if (!ctx.forceMessageId) {
    return null;
  }
  const forced =
    ctx.forceMessageId === EMPTY_MESSAGE.id
      ? EMPTY_MESSAGE
      : PRIVACY_MESSAGES.find(m => m.id === ctx.forceMessageId);
  if (!forced) {
    return null;
  }
  return {
    decision: toDecision(
      variantForCategory(forced.category),
      forced,
      countArgFor(forced, ctx)
    ),
    nextState: state,
  };
}

function isFeatureInUse(feature, features = {}) {
  switch (feature) {
    case "signin":
      return !!features.signedIn;
    case "logins":
      return !!features.hasLogins;
    case "relay":
      return !!features.relayMasks;
    // vpn / monitor: no reliable local signal — never suppressed.
    default:
      return false;
  }
}

// Highest un-fired milestone crossing, scanned widest scope first. Advances the
// watermark for the period it fires (and resets a rolled period's watermark).
function detectMilestone(ctx, bounds, state) {
  const checks = [
    ["total", CATEGORY.MILESTONE_TOTAL, ctx.allTimeTotal, null],
    ["year", CATEGORY.MILESTONE_YEAR, ctx.yearTotal, bounds.year.key],
    ["month", CATEGORY.MILESTONE_MONTH, ctx.monthTotal, bounds.month.key],
    ["week", CATEGORY.MILESTONE_WEEK, ctx.weekTotal, bounds.week.key],
  ];
  for (const [period, category, total, key] of checks) {
    const wm = state.milestoneWatermark[period] || { key: null, tier: 0 };
    const rolled = key !== null && wm.key !== key;
    const baseTier = rolled ? 0 : wm.tier;
    const crossed = MILESTONE_TIERS[period].filter(
      t => total >= t && t > baseTier
    );
    if (crossed.length) {
      state.milestoneWatermark[period] = {
        key,
        tier: crossed[crossed.length - 1],
      };
      return category;
    }
    if (rolled) {
      state.milestoneWatermark[period] = { key, tier: 0 };
    }
  }
  return null;
}

function buildPool(ctx, state, today, now) {
  const recent = state.recentId;
  const pool = PRIVACY_MESSAGES.filter(
    m => m.category === CATEGORY.INFO && m.id !== recent
  );
  if (state.promoLastDay !== today) {
    for (const m of PRIVACY_MESSAGES) {
      if (
        m.category === CATEGORY.PROMO &&
        m.id !== recent &&
        !isFeatureInUse(m.feature, ctx.features) &&
        // VPN promos need both gates: showVpnMessages (operator/experiment
        // opt-in, pref or trainhopConfig) AND vpnEnabled (the per-user VPN
        // availability signal, browser.ipProtection.enabled). Don't promote a
        // feature the user can't actually use.
        (m.feature !== "vpn" || (ctx.showVpnMessages && ctx.vpnEnabled)) &&
        now - (state.messageLastShown[m.id] || 0) >= PROMO_REPEAT_MS
      ) {
        pool.push(m);
      }
    }
  }
  return pool;
}

/**
 * Pick the message (or blank) to show on a new tab. Pure: never reads prefs or
 * the clock — `ctx`, `now`, and `rand` carry everything. Returns the render
 * decision plus the next persisted scheduler state.
 *
 * @param {object} ctx - trackersToday, sitesToday, weekTotal, monthTotal,
 *   yearTotal, allTimeTotal, streakDays, maxCount, blankChance,
 *   showVpnMessages, profileCreatedMs, features {signedIn, hasLogins,
 *   relayMasks}, and optional forceMessageId (debug override).
 * @param {object} prevState - Persisted scheduler state (or null/{}).
 * @param {number} now - Timestamp in ms.
 * @param {function(): number} rand - Returns 0..1 (Math.random in prod).
 * @returns {{decision: object, nextState: object}}
 */
export function selectPrivacyMessage(ctx, prevState, now, rand) {
  const state = normalizeState(prevState);
  const today = utcDayKey(now);
  const bounds = periodBounds(now);

  // Debug override (widgets.privacy.forceMessageId): pin the widget to one
  // catalog message for QA / design review, bypassing the ladder and all caps.
  const forced = forcedResult(ctx, state);
  if (forced) {
    return forced;
  }

  if (state.dayStamp !== today) {
    state.dayStamp = today;
    state.shownToday = 0;
  }

  // 1. EMPTY
  if (!ctx.trackersToday) {
    return {
      decision: toDecision("empty", EMPTY_MESSAGE, null),
      nextState: state,
    };
  }

  // 2. FIRST PROTECTION (once ever) — bypasses caps.
  if (!state.firstProtectionShown) {
    state.firstProtectionShown = true;
    state.lastCelebrationDay = today;
    const msg = firstByCategory(CATEGORY.FIRST_PROTECTION);
    return {
      decision: toDecision("tip", msg, countArgFor(msg, ctx)),
      nextState: state,
    };
  }

  // Celebrations are ≤ 1/day and bypass the rotation caps.
  if (state.lastCelebrationDay !== today) {
    // 3. STREAK (count-block layout). A broken streak re-arms 3/5/7.
    if (ctx.streakDays < state.streakFiredAt) {
      state.streakFiredAt = 0;
    }
    if (
      STREAK_DAYS.includes(ctx.streakDays) &&
      ctx.streakDays > state.streakFiredAt
    ) {
      state.streakFiredAt = ctx.streakDays;
      state.lastCelebrationDay = today;
      const msg = firstByCategory(CATEGORY.STREAK);
      return {
        decision: toDecision("streak", msg, countArgFor(msg, ctx)),
        nextState: state,
      };
    }

    // 4. MILESTONE crossing.
    const milestone = detectMilestone(ctx, bounds, state);
    if (milestone) {
      state.lastCelebrationDay = today;
      const msg = firstByCategory(milestone);
      return {
        decision: toDecision("tip", msg, countArgFor(msg, ctx)),
        nextState: state,
      };
    }

    // 5. DAILY CAP. On this render the count readout caps to "{maxCount}+";
    // subsequent loads show the real number again.
    if (ctx.trackersToday >= ctx.maxCount) {
      state.lastCelebrationDay = today;
      const msg = firstByCategory(CATEGORY.DAILY_CAP);
      return {
        decision: toDecision("tip", msg, countArgFor(msg, ctx), ctx.maxCount),
        nextState: state,
      };
    }
  }

  // 6. ORDINARY info/promo, subject to frequency caps.
  const isNewProfile =
    typeof ctx.profileCreatedMs === "number" &&
    now - ctx.profileCreatedMs < NEW_PROFILE_WINDOW_MS;
  const cap = isNewProfile ? CAPS.newProfile : CAPS.normal;
  if (
    state.shownToday >= cap.perDay ||
    (state.lastShownMs && now - state.lastShownMs < cap.intervalMs)
  ) {
    return { decision: blankDecision(), nextState: state };
  }

  const pool = buildPool(ctx, state, today, now);
  if (!pool.length) {
    return { decision: blankDecision(), nextState: state };
  }
  const pick = pool[Math.floor(rand() * pool.length)];

  // Random blank damper on info (does NOT consume a slot).
  if (pick.category === CATEGORY.INFO && rand() < ctx.blankChance) {
    return { decision: blankDecision(), nextState: state };
  }

  state.shownToday += 1;
  state.lastShownMs = now;
  state.recentId = pick.id;
  state.messageLastShown[pick.id] = now;
  if (pick.category === CATEGORY.PROMO) {
    state.promoLastDay = today;
  }
  return {
    decision: toDecision("tip", pick, countArgFor(pick, ctx)),
    nextState: state,
  };
}
