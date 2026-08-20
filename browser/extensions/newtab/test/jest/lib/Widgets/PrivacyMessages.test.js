/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  selectPrivacyMessage,
  CATEGORY,
  PRIVACY_MESSAGES,
} from "lib/Widgets/PrivacyMessages.sys.mjs";

// A fixed Monday noon UTC so period/day math is stable across runs.
const NOW = Date.UTC(2026, 5, 15, 12, 0, 0);
const HOUR = 60 * 60 * 1000;

// Deterministic rand: returns each queued value once, then 0.
function rand(values = []) {
  let i = 0;
  return () => (i < values.length ? values[i++] : 0);
}

function ctx(over = {}) {
  return {
    trackersToday: 10,
    sitesToday: 5,
    weekTotal: 0,
    monthTotal: 0,
    yearTotal: 0,
    allTimeTotal: 0,
    streakDays: 1,
    maxCount: 100,
    blankChance: 0.4,
    // Match the shipping default: VPN promos gated off. The fixture models a
    // VPN-eligible user (vpnEnabled) whose promos are simply not opted in; the
    // two gates are exercised explicitly in the showVpnMessages tests below.
    showVpnMessages: false,
    vpnEnabled: true,
    profileCreatedMs: 0, // old profile -> normal caps
    features: {},
    ...over,
  };
}

// firstProtectionShown defaults true so tests aren't all caught by the
// once-ever first-protection rung.
function state(over = {}) {
  return { firstProtectionShown: true, ...over };
}

function catOf(messageId) {
  return PRIVACY_MESSAGES.find(m => m.id === messageId)?.category;
}

describe("selectPrivacyMessage", () => {
  it("shows the empty state when no trackers blocked today", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ trackersToday: 0 }),
      state(),
      NOW,
      rand()
    );
    expect(decision.variant).toBe("empty");
  });

  it("still picks a message when no sites were visited today", () => {
    // A zero site count is not a zero state, so the scheduler keeps running.
    const { decision } = selectPrivacyMessage(
      ctx({ trackersToday: 34, sitesToday: 0 }),
      state(),
      NOW,
      rand()
    );
    expect(["blank", "streak", "tip"]).toContain(decision.variant);
  });

  it("shows first-protection once, then records it", () => {
    const first = selectPrivacyMessage(
      ctx(),
      { firstProtectionShown: false },
      NOW,
      rand()
    );
    expect(catOf(first.decision.messageId)).toBe(CATEGORY.FIRST_PROTECTION);
    expect(first.nextState.firstProtectionShown).toBe(true);
    expect(first.decision.icon).toBe("kit");
  });

  it("fires the streak at 3, not again at 3, then at 5", () => {
    const day1 = selectPrivacyMessage(
      ctx({ streakDays: 3 }),
      state(),
      NOW,
      rand()
    );
    expect(day1.decision.variant).toBe("streak");
    expect(day1.decision.countArg).toEqual({ count: 3 });
    expect(day1.nextState.streakFiredAt).toBe(3);

    // Same streak length on a later day must not refire.
    const again = selectPrivacyMessage(
      ctx({ streakDays: 3 }),
      state({ streakFiredAt: 3, lastCelebrationDay: "2000-01-01" }),
      NOW,
      rand()
    );
    expect(again.decision.variant).not.toBe("streak");

    const day5 = selectPrivacyMessage(
      ctx({ streakDays: 5 }),
      state({ streakFiredAt: 3, lastCelebrationDay: "2000-01-01" }),
      NOW,
      rand()
    );
    expect(day5.decision.variant).toBe("streak");
    expect(day5.nextState.streakFiredAt).toBe(5);
  });

  it("fires a week milestone on crossing, once per period", () => {
    const crossed = selectPrivacyMessage(
      ctx({ weekTotal: 120 }),
      state(),
      NOW,
      rand()
    );
    expect(catOf(crossed.decision.messageId)).toBe(CATEGORY.MILESTONE_WEEK);
    expect(crossed.decision.countArg).toEqual({ count: 120 });

    // Re-run a later day with the watermark in place: no refire.
    const noRefire = selectPrivacyMessage(
      ctx({ weekTotal: 140 }),
      { ...crossed.nextState, lastCelebrationDay: "2000-01-01" },
      NOW,
      rand()
    );
    expect(catOf(noRefire.decision.messageId)).not.toBe(
      CATEGORY.MILESTONE_WEEK
    );
  });

  it("picks the highest crossed month tier", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ monthTotal: 600 }),
      state(),
      NOW,
      rand()
    );
    expect(catOf(decision.messageId)).toBe(CATEGORY.MILESTONE_MONTH);
    // 600 crosses 250 and 500 -> watermark advances to 500.
  });

  it("fires the daily-cap celebration at the cap and caps the readout", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ trackersToday: 137, maxCount: 100 }),
      state(),
      NOW,
      rand()
    );
    expect(catOf(decision.messageId)).toBe(CATEGORY.DAILY_CAP);
    // Only this render caps the count display to "maxCount+".
    expect(decision.countCeiling).toBe(100);
  });

  it("leaves countCeiling null for non-daily-cap decisions", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ streakDays: 5 }),
      state(),
      NOW,
      rand()
    );
    expect(decision.countCeiling).toBeNull();
  });

  it("celebrations bypass the frequency caps", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ streakDays: 5 }),
      state({ shownToday: 99, lastShownMs: NOW - 1000 }),
      NOW,
      rand()
    );
    expect(decision.variant).toBe("streak");
  });

  it("blanks ordinary messages within the hourly interval", () => {
    const { decision } = selectPrivacyMessage(
      ctx(),
      state({
        lastCelebrationDay: "2026-06-15",
        lastShownMs: NOW - 10 * 60 * 1000,
        shownToday: 1,
      }),
      NOW,
      rand([0])
    );
    expect(decision.variant).toBe("blank");
  });

  it("blanks ordinary messages once the daily limit is hit", () => {
    const { decision } = selectPrivacyMessage(
      ctx(),
      state({
        lastCelebrationDay: "2026-06-15",
        shownToday: 5,
        dayStamp: "2026-06-15",
      }),
      NOW,
      rand([0])
    );
    expect(decision.variant).toBe("blank");
  });

  it("uses the 10/day, 15-min cadence in a new profile's first 48h", () => {
    const base = state({
      lastCelebrationDay: "2026-06-15",
      dayStamp: "2026-06-15",
    });
    // 5 shown + 20 min since last would blank on a normal profile...
    const newProfile = selectPrivacyMessage(
      ctx({ profileCreatedMs: NOW - HOUR }),
      { ...base, shownToday: 5, lastShownMs: NOW - 20 * 60 * 1000 },
      NOW,
      rand([0, 0.9])
    );
    expect(newProfile.decision.variant).toBe("tip");
  });

  it("rolls the 40% blank on info without consuming a slot", () => {
    const base = state({
      lastCelebrationDay: "2026-06-15",
      dayStamp: "2026-06-15",
      shownToday: 0,
    });
    // rand[0]=0 -> first pool item (an info message); rand[1]=0.1 < 0.4 -> blank.
    const blanked = selectPrivacyMessage(ctx(), base, NOW, rand([0, 0.1]));
    expect(blanked.decision.variant).toBe("blank");
    expect(blanked.nextState.shownToday).toBe(0);

    // rand[1]=0.9 >= 0.4 -> the info message shows and consumes a slot.
    const shown = selectPrivacyMessage(ctx(), base, NOW, rand([0, 0.9]));
    expect(shown.decision.variant).toBe("tip");
    expect(catOf(shown.decision.messageId)).toBe(CATEGORY.INFO);
    expect(shown.nextState.shownToday).toBe(1);
  });

  it("suppresses promos for features already in use", () => {
    const base = state({
      lastCelebrationDay: "2026-06-15",
      dayStamp: "2026-06-15",
    });
    // rand near 1 selects the last pool entry. With no suppression that's a
    // promo; with the logins feature in use the logins promo is filtered out.
    const withLogins = selectPrivacyMessage(
      ctx({ features: { signedIn: true, hasLogins: true, relayMasks: true } }),
      base,
      NOW,
      rand([0.999, 0.9])
    );
    // vpn/monitor promos have no signal, so a promo can still appear; just
    // assert no suppressible promo (logins/relay/signin) is shown.
    const msg = PRIVACY_MESSAGES.find(
      m => m.id === withLogins.decision.messageId
    );
    if (msg?.category === CATEGORY.PROMO) {
      expect(["vpn", "monitor", "private-window"]).toContain(msg.feature);
    }
  });

  // Sample the promo pool across the whole rand range (selection is random and
  // buildPool isn't exported) and collect which promo `feature`s can appear.
  const sampledPromoFeatures = context => {
    const base = state({
      dayStamp: "2026-06-15",
      lastCelebrationDay: "2026-06-15",
    });
    const feats = new Set();
    for (let i = 0; i < 100; i++) {
      const r = i / 100;
      const { decision } = selectPrivacyMessage(
        context,
        base,
        NOW,
        rand([r, r])
      );
      const msg = PRIVACY_MESSAGES.find(m => m.id === decision.messageId);
      if (msg?.category === CATEGORY.PROMO) {
        feats.add(msg.feature);
      }
    }
    return feats;
  };

  it("excludes VPN promos when showVpnMessages is false", () => {
    const feats = sampledPromoFeatures(ctx({ showVpnMessages: false }));
    expect(feats.has("vpn")).toBe(false);
    // other promos still rotate
    expect(feats.size).toBeGreaterThan(0);
  });

  it("includes VPN promos when showVpnMessages is true", () => {
    const feats = sampledPromoFeatures(ctx({ showVpnMessages: true }));
    expect(feats.has("vpn")).toBe(true);
  });

  it("excludes VPN promos when the VPN isn't available, even if opted in", () => {
    // showVpnMessages on but browser.ipProtection.enabled off -> no VPN promos.
    const feats = sampledPromoFeatures(
      ctx({ showVpnMessages: true, vpnEnabled: false })
    );
    expect(feats.has("vpn")).toBe(false);
    // other promos still rotate
    expect(feats.size).toBeGreaterThan(0);
  });

  it("limits promos to one per day", () => {
    const base = state({
      lastCelebrationDay: "2026-06-15",
      dayStamp: "2026-06-15",
      promoLastDay: "2026-06-15",
    });
    // Even selecting the last pool entry, promos are excluded today -> info.
    const { decision } = selectPrivacyMessage(
      ctx(),
      base,
      NOW,
      rand([0.999, 0.9])
    );
    expect(catOf(decision.messageId)).toBe(CATEGORY.INFO);
  });

  it("passes the count as an l10n arg and tags the category", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ streakDays: 7 }),
      state(),
      NOW,
      rand()
    );
    expect(catOf(decision.messageId)).toBe(CATEGORY.STREAK);
    expect(decision.countArg).toEqual({ count: 7 });
    expect(decision.category).toBe(CATEGORY.STREAK);
  });

  it("carries the message's cta descriptor on the decision", () => {
    const { decision } = selectPrivacyMessage(
      ctx({ trackersToday: 0 }),
      state(),
      NOW,
      rand()
    );
    // Empty state has no cta.
    expect(decision.cta).toBeNull();

    const withCta = selectPrivacyMessage(
      ctx({ streakDays: 7 }),
      state(),
      NOW,
      rand()
    ).decision;
    const msg = PRIVACY_MESSAGES.find(m => m.id === withCta.messageId);
    expect(withCta.cta).toEqual(msg.cta);
    expect(withCta.cta.type).toBe("OPEN_ABOUT_PAGE");
  });

  it("gives the blank decision a View protections cta", () => {
    // Within the hourly interval -> blank, but it still carries the protections
    // action so the widget can show a CTA (the component supplies the label).
    const base = state({ lastShownMs: NOW - 5 * 60 * 1000, shownToday: 1 });
    const { decision } = selectPrivacyMessage(ctx(), base, NOW, rand());
    expect(decision.variant).toBe("blank");
    expect(decision.messageId).toBeNull();
    expect(decision.cta).toEqual({
      type: "OPEN_ABOUT_PAGE",
      data: { args: "protections", where: "tab" },
    });
  });

  describe("forceMessageId debug override", () => {
    it("pins the widget to the requested message, bypassing the ladder", () => {
      const { decision } = selectPrivacyMessage(
        ctx({
          trackersToday: 0, // would otherwise be the empty state
          forceMessageId: "newtab-privacy-message-promo-relay-1",
        }),
        state(),
        NOW,
        rand()
      );
      expect(decision.messageId).toBe("newtab-privacy-message-promo-relay-1");
      expect(decision.variant).toBe("tip");
      expect(decision.cta.type).toBe("OPEN_URL");
    });

    it("forces the empty message by id", () => {
      const { decision } = selectPrivacyMessage(
        ctx({ forceMessageId: "newtab-privacy-empty" }),
        state(),
        NOW,
        rand()
      );
      expect(decision.variant).toBe("empty");
      expect(decision.messageId).toBe("newtab-privacy-empty");
    });

    it("does not mutate scheduler state", () => {
      const prev = state({ shownToday: 3, dayStamp: "2026-06-15" });
      const { nextState } = selectPrivacyMessage(
        ctx({ forceMessageId: "newtab-privacy-message-info-1" }),
        prev,
        NOW,
        rand()
      );
      expect(nextState.shownToday).toBe(3);
    });

    it("ignores an unknown id and falls through to normal scheduling", () => {
      const { decision } = selectPrivacyMessage(
        ctx({ trackersToday: 0, forceMessageId: "does-not-exist" }),
        state(),
        NOW,
        rand()
      );
      expect(decision.variant).toBe("empty");
    });
  });
});

describe("promo CTA attribution (Bug 2061524)", () => {
  it.each([
    [
      "newtab-privacy-message-promo-monitor-1",
      "https://monitor.mozilla.org/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=get-breach-alerts-global",
    ],
    [
      "newtab-privacy-message-promo-monitor-2",
      "https://monitor.mozilla.org/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=protect-your-info-global",
    ],
    [
      "newtab-privacy-message-promo-relay-1",
      "https://relay.firefox.com/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=use-email-mask-global",
    ],
    [
      "newtab-privacy-message-promo-relay-2",
      "https://relay.firefox.com/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=protect-your-inbox-global",
    ],
    [
      "newtab-privacy-message-promo-relay-3",
      "https://relay.firefox.com/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=50-free-email-mask-global",
    ],
  ])("%s opens a tagged url", (id, url) => {
    const msg = PRIVACY_MESSAGES.find(m => m.id === id);
    expect(msg.cta).toEqual({
      type: "OPEN_URL",
      data: { args: url, where: "tab" },
    });
  });

  // The cases above name each promo explicitly, so a future Monitor/Relay promo
  // could ship untagged with CI still green. This asserts the whole family stays
  // tagged: every Monitor/Relay promo opens a url carrying all four utm params.
  it("tags every Monitor/Relay promo CTA with all four utm params", () => {
    const attributedPromos = PRIVACY_MESSAGES.filter(
      m => m.feature === "monitor" || m.feature === "relay"
    );
    // Guard against the filter silently matching nothing (e.g. a `feature`
    // rename) and the assertions below vacuously passing.
    expect(attributedPromos.length).toBeGreaterThan(0);
    const contents = [];
    for (const msg of attributedPromos) {
      expect(msg.cta.type).toBe("OPEN_URL");
      const params = new URL(msg.cta.data.args).searchParams;
      expect(params.get("utm_medium")).toBe("referral");
      expect(params.get("utm_source")).toBe("firefox-desktop");
      expect(params.get("utm_campaign")).toBe("widget");
      expect(params.get("utm_content")).toBeTruthy();
      contents.push(params.get("utm_content"));
    }
    // utm_content is what distinguishes one message's signups from another's, so
    // a duplicate slug would silently merge two messages' attribution.
    expect(new Set(contents).size).toBe(contents.length);
  });
});
