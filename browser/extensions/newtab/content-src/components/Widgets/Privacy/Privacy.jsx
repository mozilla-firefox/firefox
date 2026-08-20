/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { useSelector, batch } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useSizeSubmenu } from "../../../lib/utils";
import {
  WIDGET_REGISTRY,
  resolveWidgetSize,
  resolvePrivacyDisplayCount,
} from "common/WidgetsRegistry.mjs";
import { MoveSubmenu } from "../MoveSubmenu";
import { WidgetCelebration, CelebrationSparkles } from "../WidgetCelebration";
import { useWidgetCelebration } from "../useWidgetCelebration";
import { useCountUp } from "../useCountUp";
import { usePageVisible } from "../usePageVisible";
import { useWidgetTelemetry } from "../useWidgetTelemetry";

const USER_ACTION_TYPES = {
  CHANGE_SIZE: "change_size",
  LEARN_MORE: "learn_more",
  MESSAGE_CTA_CLICK: "message_cta_click",
  MESSAGE_IMPRESSION: "message_impression",
  TRACKERS_BLOCKED_IMPRESSION: "trackers_blocked_impression",
  TRACKING_MESSAGE_CLICK: "tracking_message_click",
};

// action_value for the trackers-blocked impression: whether the widget was
// shown with any tracking activity blocked today.
const TRACKERS_BLOCKED_VALUE = {
  BLOCKED: "blocked",
  NONE: "none",
};

// Per design: the brief sparkle rides ordinary +10 count-ups and the smaller
// earned moments; the longer, denser one is reserved for the count milestones
// and the daily cap. Durations are tiered in _Privacy.scss.
const CELEBRATION_TIERS = {
  brief: { sparkleCount: 5, sparkleStaggerMs: 65 },
  major: { sparkleCount: 12, sparkleStaggerMs: 60 },
};

// Count-up duration. Slower than the 750ms prototype so the number is
// readable while it climbs.
const COUNT_UP_DURATION_MS = 1100;

const PRIVACY_ENTRY = WIDGET_REGISTRY.find(w => w.id === "privacy");

const ICON_BASE_URL = "chrome://newtab/content/data/content/assets/";

// Icon key (from the message decision / PrivacyMessages.sys.mjs) -> asset.
const ICON_ASSETS = {
  shield: "widget-privacy-shield.svg",
  shieldCheck: "widget-privacy-shield-check.svg",
  planet: "widget-privacy-planet.svg",
  bolt: "widget-privacy-bolt.svg",
  star: "widget-privacy-star.svg",
  kit: "widget-privacy-kit.svg",
};

// The kit head-tilt loops on its own (CSS animation inside the SVG), so it is
// swapped in only during an earned moment. Its reduced-motion guard lives in
// the asset: loaded via <img>, the page's JS check can't reach it.
const KIT_ANIMATED_ASSET = "kit-circle-animated.svg";

// The longer, denser sparkle is reserved for the count milestones — the
// "100+ type" moments.
const MAJOR_CELEBRATION_CATEGORIES = new Set([
  "dailyCap",
  "milestoneWeek",
  "milestoneMonth",
  "milestoneYear",
  "milestoneTotal",
]);

// Every scheduler "earned moment" fires a celebration and tilts the kit. A
// first block happens minutes into a new profile and a streak is a days count,
// so those two get the brief sparkle rather than the loud one above.
const CELEBRATION_CATEGORIES = new Set([
  ...MAJOR_CELEBRATION_CATEGORIES,
  "firstProtection",
  "streak",
]);

// Renders a widget icon by icon key. The wrapper div is the alignment hook.
// `animated` swaps the kit for its head-tilt variant during a celebration.
const privacyImage = (iconKey, animated = false) => {
  const asset =
    animated && iconKey === "kit"
      ? KIT_ANIMATED_ASSET
      : ICON_ASSETS[iconKey] || ICON_ASSETS.shieldCheck;
  return (
    <div className="privacy-image">
      <img
        className="privacy-image-icon"
        src={`${ICON_BASE_URL}${asset}`}
        alt=""
      />
    </div>
  );
};

// Destination for a CTA's SpecialMessageAction, used as the message_cta_click
// action_value: the URL it opens (Monitor/Relay URLs carry UTM params from
// Bug 2061524, whose utm_content identifies the message), an about: page, or
// the action type when there's no navigable target.
function ctaDestination(action) {
  if (!action) {
    return undefined;
  }
  switch (action.type) {
    case "OPEN_URL":
      return action.data?.args;
    case "OPEN_ABOUT_PAGE":
      return action.data?.args ? `about:${action.data.args}` : action.type;
    default:
      return action.type;
  }
}

// eslint-disable-next-line max-statements
function Privacy({ dispatch, widgetsMayBeMaximized, widgetEnabledMap }) {
  const prefs = useSelector(state => state.Prefs.values);
  const privacyData = useSelector(state => state.PrivacyWidget);

  // Size comes from the registry helper: user-set pref > trainhop suggestion
  // > registry defaultSize. Never read the size pref directly.
  const widgetSize = resolveWidgetSize(PRIVACY_ENTRY, prefs);

  // Flipped when the widget is actually scrolled into view (same trigger as
  // widgets_impression), so the impression-time signals below line up with it
  // and skip preloaded tabs that were never seen.
  const [hasBeenSeen, setHasBeenSeen] = useState(false);
  const { impressionRef, recordUserAction, recordEnabled } = useWidgetTelemetry(
    {
      dispatch,
      widget: PRIVACY_ENTRY,
      widgetSize,
      onImpression: () => setHasBeenSeen(true),
    }
  );

  const trackersToday = privacyData?.trackersToday ?? 0;
  const sitesToday = privacyData?.sitesToday ?? 0;
  // Gate the metric UI on a real feed update: before the first broadcast, show
  // no metric state rather than a misleading empty/zero one.
  const initialized = privacyData?.initialized ?? false;

  // Message decision chosen by PrivacyFeed's selector (Bug 2050954).
  const { variant, messageId, category, icon, countArg, cta, countCeiling } =
    privacyData ?? {};
  const isLarge = widgetSize === "large";

  // impressionRef (from useWidgetTelemetry) is a callback ref for the impression
  // observer, so the celebration keeps its own element ref for measurements.
  const celebrationRef = useRef(null);
  const {
    celebrationFrame,
    celebrationId,
    completeCelebration,
    isCelebrating,
    triggerCelebration,
  } = useWidgetCelebration(celebrationRef);
  // displayValue is the resting count except while a celebration counts up.
  const { countUp, displayValue, hold, release } = useCountUp(
    trackersToday,
    COUNT_UP_DURATION_MS
  );

  // A celebration is one-shot, so playing it on a preloaded tab would spend it
  // before the user could see it. Hold until the tab is actually shown.
  const isPageVisible = usePageVisible();

  // Normally show the real count, only ceiling the readout at "{cap}+"
  // (default 999) so it stays a tidy few characters. On the daily-cap render
  // the selector sets countCeiling (100), so that one load shows "100+"; the
  // next load clears it and the real number returns.
  const displayCap = resolvePrivacyDisplayCount(prefs);
  const formatCount = value => {
    if (typeof countCeiling === "number") {
      return `${countCeiling}+`;
    }
    return value > displayCap ? `${displayCap}+` : `${value}`;
  };
  const displayCount = formatCount(displayValue);
  // Same readout without the animation, for the screen-reader copy.
  const stableCount = formatCount(trackersToday);

  // trackersToday === 0 is the sole trigger for the empty layout, so the widget
  // agrees with about:protections: any blocked activity there shows a count
  // here. sitesToday is only a Places history proxy, so it can read 0 while the
  // count stands — that drops its own line below rather than blanking the count
  // (Bug 2063207). It must not also key off `variant === "empty"`: a SYSTEM_TICK
  // refreshes the count without touching `variant`, so a tab opened at zero
  // would stay empty even after its count climbs, until the next tab re-runs
  // the selector.
  const isEmptyState = trackersToday === 0;
  // Streak and tip both use the count + divider + message layout; "blank"
  // shows the count only (plus a CTA).
  const isStreak = !isEmptyState && variant === "streak";
  const isTip = !isEmptyState && variant === "tip";
  const isBlank = !isEmptyState && variant === "blank";
  const hasMessage = (isStreak || isTip) && messageId;
  // Telemetry id for a CTA click. The blank state has no messageId, so give it
  // a stable, distinguishable id — otherwise its clicks report null and the
  // most-shown state can't be attributed (Dré).
  const ctaMessageId = isBlank ? "newtab-privacy-blank" : messageId;
  // The single icon sits beside the count, except in the large tip layout where
  // it sits inside the tip.
  const iconBesideCount = !isEmptyState && !(isTip && isLarge);
  const iconInTip = isTip && isLarge;

  // Count-up celebration (HNT-2845), awarded by PrivacyFeed once the count has
  // climbed by the threshold. Independent of the message decision: it animates
  // whatever number is on screen. The border ring is reserved for the
  // daily-cap render, which is the one the design gives a gradient border.
  const celebration = privacyData?.celebration ?? null;
  const playedCelebrationRef = useRef(null);
  const playedMomentRef = useRef(null);
  // Debug override (widgets.privacy.forceCelebration) wins when set, so QA can
  // see each tier without engineering the scheduler state that produces it.
  const forcedTier = celebration?.forcedTier ?? null;
  const isEarnedMoment = CELEBRATION_CATEGORIES.has(category);
  // Count milestones get the longer sparkle; everything else the brief one.
  const isMajorMoment = forcedTier
    ? forcedTier !== "brief"
    : MAJOR_CELEBRATION_CATEGORIES.has(category);
  const celebrationTier = isMajorMoment
    ? CELEBRATION_TIERS.major
    : CELEBRATION_TIERS.brief;
  // The gradient ring is narrower still: only the design's 100+ card has one.
  const showRing = forcedTier
    ? forcedTier === "cap"
    : typeof countCeiling === "number";
  // Every earned moment tilts the kit. A plain +10 lands on an info message,
  // whose icon is never the kit, so `isCelebrating` alone can't swap it.
  const tiltKit = isEarnedMoment;

  // Snapshot taken when the animation starts: `category` and `countCeiling`
  // can move under it and swap the tier or pull the ring mid-flight.
  const [playingCelebration, setPlayingCelebration] = useState(null);
  const activeCelebration = playingCelebration ?? {
    isMajor: isMajorMoment,
    showRing,
    tier: celebrationTier,
  };

  // Two things can start a celebration, and either alone is enough:
  //   1. a +10 count-up award from PrivacyFeed (HNT-2845), and
  //   2. an earned moment from the scheduler, which the design celebrates in
  //      its own right (streak, milestones, daily cap, first block).
  // Each earned moment carries its own messageId, so that's the dedupe key.
  // Layout effect, not a passive one: the resting value is the *final* count,
  // so starting after paint shows one frame of it before the count-up resets
  // to `fromCount` and climbs back — a visible backward jump.
  useLayoutEffect(() => {
    const awardedAt = celebration?.awardedAt ?? null;
    const isNewAward = awardedAt && playedCelebrationRef.current !== awardedAt;
    const isNewMoment = isEarnedMoment && playedMomentRef.current !== messageId;

    // The empty layout renders no count, so celebrating would burn the one-shot
    // award on an animation nobody sees.
    if (isEmptyState || (!isNewAward && !isNewMoment)) {
      return;
    }

    // Preloaded tab: leave the award and the moment unplayed, and hold the
    // pre-award number so revealing the tab doesn't flash the final count.
    if (!isPageVisible) {
      if (isNewAward) {
        hold(celebration.fromCount);
      }
      return;
    }

    if (isEarnedMoment) {
      playedMomentRef.current = messageId;
    }

    if (isNewAward) {
      playedCelebrationRef.current = awardedAt;
      // Acknowledge even when the animation is skipped (reduced motion), so
      // the award doesn't stay pending and replay on the next tab.
      dispatch(
        ac.AlsoToMain({
          type: at.WIDGETS_PRIVACY_MARK_CELEBRATED,
          data: awardedAt,
        })
      );
    }

    const isPlaying = triggerCelebration();
    if (isPlaying) {
      setPlayingCelebration({
        isMajor: isMajorMoment,
        showRing,
        tier: celebrationTier,
      });
    }

    if (isPlaying && isNewAward) {
      countUp(celebration.fromCount, celebration.toCount);
    } else {
      // Nothing will animate the readout — reduced motion, or a moment with no
      // award — so drop any hold taken while the tab was hidden.
      release();
    }
  }, [
    celebration,
    celebrationTier,
    countUp,
    dispatch,
    hold,
    isEarnedMoment,
    isEmptyState,
    isMajorMoment,
    isPageVisible,
    messageId,
    release,
    showRing,
    triggerCelebration,
  ]);

  // Drop the snapshot with the animation, so the next run can't paint a frame
  // of the previous one's tier.
  const handleCelebrationComplete = useCallback(() => {
    setPlayingCelebration(null);
    completeCelebration();
  }, [completeCelebration]);

  // Impression-time signals, logged once per mount. Gated on two things:
  //   - hasBeenSeen: the widget actually scrolled into view, so these line up
  //     with widgets_impression and don't fire on never-seen preloaded tabs.
  //   - variant != null: the feed has picked a message. A counts-only refresh
  //     flips `initialized` before the decision is made, so gating on
  //     `initialized` alone could burn the one-shot before the message renders,
  //     losing its impression.
  const impressionsLoggedRef = useRef(false);
  useEffect(() => {
    if (!hasBeenSeen || !variant || impressionsLoggedRef.current) {
      return;
    }
    impressionsLoggedRef.current = true;
    // A separate impression per view reporting whether the widget was shown
    // with any tracking activity blocked today. The state rides action_value
    // ("blocked" vs "none"). No count is recorded.
    recordUserAction(USER_ACTION_TYPES.TRACKERS_BLOCKED_IMPRESSION, {
      source: "widget",
      value:
        trackersToday > 0
          ? TRACKERS_BLOCKED_VALUE.BLOCKED
          : TRACKERS_BLOCKED_VALUE.NONE,
    });
    // Impression of the secondary message, keyed by ctaMessageId (blank ->
    // "newtab-privacy-blank"). Gated on !isEmptyState: the selector sets
    // messageId to "newtab-privacy-empty" in the empty state, so a bare
    // ctaMessageId check would log a spurious impression for a state that shows
    // no message/CTA. The blank state still logs, keeping its URL-valued CTA
    // click attributable by joining to this impression on newtab_visit_id.
    if (!isEmptyState && ctaMessageId) {
      recordUserAction(USER_ACTION_TYPES.MESSAGE_IMPRESSION, {
        source: "message",
        value: ctaMessageId,
      });
    }
  }, [
    hasBeenSeen,
    variant,
    trackersToday,
    isEmptyState,
    ctaMessageId,
    recordUserAction,
  ]);

  function handlePrivacyHide() {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.SET_PREF,
          data: { name: PRIVACY_ENTRY.enabledPref, value: false },
        })
      );
      recordEnabled(false, { source: "context_menu" });
    });
  }

  const handleChangeSize = useCallback(
    size => {
      batch(() => {
        dispatch(
          ac.OnlyToMain({
            type: at.SET_PREF,
            data: { name: PRIVACY_ENTRY.sizePref, value: size },
          })
        );
        recordUserAction(USER_ACTION_TYPES.CHANGE_SIZE, {
          source: "context_menu",
          value: size,
          size,
        });
      });
    },
    [dispatch, recordUserAction]
  );

  const sizeSubmenuRef = useSizeSubmenu(handleChangeSize);

  function handleLearnMore() {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.OPEN_LINK,
          data: {
            url: "https://support.mozilla.org/kb/firefox-new-tab-widgets",
          },
        })
      );
      recordUserAction(USER_ACTION_TYPES.LEARN_MORE, {
        source: "context_menu",
      });
    });
  }

  // Runs the message's CTA. The SpecialMessageAction descriptor lives on the
  // decision (`cta`); the parent (PrivacyFeed) executes it — content only
  // forwards it and logs the interaction.
  function handleCtaClick() {
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_PRIVACY_CTA,
          data: { action: cta, message_id: ctaMessageId },
        })
      );
      // action_value is the CTA destination (the click's endpoint, incl. the
      // Bug 2061524 UTM params). Which message drove the click is recovered by
      // joining to this visit's message_impression on newtab_visit_id.
      recordUserAction(USER_ACTION_TYPES.MESSAGE_CTA_CLICK, {
        source: "message",
        value: ctaDestination(cta),
      });
    });
  }

  function handleViewProtections(event) {
    event.preventDefault();
    batch(() => {
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_PRIVACY_CTA,
          data: {
            action: {
              type: "OPEN_ABOUT_PAGE",
              data: { args: "protections", where: "tab" },
            },
          },
        })
      );
      recordUserAction(USER_ACTION_TYPES.TRACKING_MESSAGE_CLICK, {
        source: "widget",
      });
    });
  }

  // The message resolves via its Fluent `messageId` (Bug 2048389); `countArg`
  // feeds the plural/variable l10n args.
  const messageEl = className => (
    <p
      className={className}
      data-l10n-id={messageId}
      data-l10n-args={countArg ? JSON.stringify(countArg) : undefined}
    />
  );

  // CTA button for messages that carry one (`cta`); its label is the message's
  // `-cta` companion Fluent id. Value-only messages render as moz-button text.
  const ctaButton =
    cta && messageId ? (
      <moz-button
        className="privacy-cta"
        data-l10n-id={`${messageId}-cta`}
        onClick={handleCtaClick}
        size="small"
        type="primary"
      />
    ) : null;

  // The blank state has no tip copy (messageId is null) but still shows a
  // "View protections" CTA, borrowing info-1's companion label for now.
  const blankCta =
    isBlank && cta ? (
      <moz-button
        className="privacy-cta"
        data-l10n-id="newtab-privacy-message-info-1-cta"
        onClick={handleCtaClick}
        size="small"
        type="primary"
      />
    ) : null;

  return (
    <article
      data-l10n-id="newtab-privacy-widget-label"
      className={`privacy widget col-4 ${widgetSize}-widget${
        initialized && isEmptyState ? " is-empty" : ""
      }${initialized && isTip ? " has-tip-msg" : ""}${
        initialized && isStreak ? " has-streak" : ""
      }${
        isCelebrating && activeCelebration.isMajor
          ? " is-major-celebration"
          : ""
      }`}
      ref={el => {
        impressionRef(el);
        celebrationRef.current = el;
      }}
    >
      <div className="privacy-title-wrapper">
        <div className="privacy-context-menu-wrapper">
          <moz-button
            className="privacy-context-menu-button"
            iconSrc="chrome://global/skin/icons/more.svg"
            menuId="privacy-context-menu"
            type="ghost"
          />
          <panel-list id="privacy-context-menu">
            {widgetsMayBeMaximized && (
              <panel-item submenu="privacy-size-submenu">
                <span data-l10n-id="newtab-widget-menu-change-size"></span>
                <panel-list
                  ref={sizeSubmenuRef}
                  slot="submenu"
                  id="privacy-size-submenu"
                >
                  {["medium", "large"].map(size => (
                    <panel-item
                      key={size}
                      type="checkbox"
                      checked={widgetSize === size || undefined}
                      data-size={size}
                      data-l10n-id={`newtab-widget-size-${size}`}
                    />
                  ))}
                </panel-list>
              </panel-item>
            )}

            <MoveSubmenu
              widgetId="privacy"
              widgetEnabledMap={widgetEnabledMap}
            />

            <panel-item
              data-l10n-id="newtab-widget-menu-hide"
              onClick={handlePrivacyHide}
            />
            <panel-item
              data-l10n-id="newtab-privacy-menu-learn-more"
              onClick={handleLearnMore}
            />
          </panel-list>
        </div>
      </div>

      <div className="privacy-body">
        {initialized &&
          (isEmptyState ? (
            <div className="privacy-empty">
              {/* Empty state always uses the shield icon — never the decision's
                  `icon`, which may be a stale shieldCheck from a prior tip. */}
              {privacyImage("shield")}
              <a
                className="privacy-empty-details"
                href="about:protections"
                onClick={handleViewProtections}
              >
                <p
                  className="privacy-empty-message"
                  data-l10n-id="newtab-privacy-empty"
                />
              </a>
            </div>
          ) : (
            <>
              <a
                className="privacy-count"
                href="about:protections"
                onClick={handleViewProtections}
              >
                <div className="privacy-count-number-wrapper">
                  {/* The single icon sits beside the count, except in the large
                      tip layout where it moves into the tip. */}
                  {iconBesideCount &&
                    privacyImage(icon || "shieldCheck", tiltKit)}
                  {/* The count-up mutates this text every frame, and
                      newtab-privacy-trackers-blocked-today uses $count for
                      plural selection only — so this span is the sole place
                      the number exists. Hide the animating copy and expose a
                      stable one, or AT can read a transient value. */}
                  <span className="privacy-count-number" aria-hidden="true">
                    {displayCount}
                  </span>
                  <span className="privacy-count-number-a11y">
                    {stableCount}
                  </span>
                  {isCelebrating ? (
                    <CelebrationSparkles
                      classNamePrefix="privacy-celebration"
                      celebrationId={celebrationId}
                      count={activeCelebration.tier.sparkleCount}
                      staggerMs={activeCelebration.tier.sparkleStaggerMs}
                    />
                  ) : null}
                </div>

                <div className="privacy-count-text">
                  <span
                    className="privacy-count-label"
                    data-l10n-id="newtab-privacy-trackers-blocked-today"
                    data-l10n-args={JSON.stringify({ count: trackersToday })}
                  />
                  {sitesToday > 0 && (
                    <span
                      className="privacy-count-sites"
                      data-l10n-id="newtab-privacy-across-sites"
                      data-l10n-args={JSON.stringify({ count: sitesToday })}
                    />
                  )}
                </div>
              </a>
              {isStreak && hasMessage && (
                <>
                  <hr className="privacy-divider" />
                  <div className="privacy-streak">
                    {messageEl("privacy-tip-message")}
                    {ctaButton}
                  </div>
                </>
              )}
              {isTip && hasMessage && (
                <>
                  <hr className="privacy-divider" />
                  <div className="privacy-tip">
                    {iconInTip && privacyImage(icon || "shieldCheck", tiltKit)}
                    <div className="privacy-tip-content">
                      {messageEl("privacy-tip-message")}
                      {ctaButton}
                    </div>
                  </div>
                </>
              )}
              {/* Blank: count only, but still a "View protections" CTA. */}
              {blankCta}
            </>
          ))}
      </div>

      {isCelebrating && celebrationFrame ? (
        <WidgetCelebration
          classNamePrefix="privacy-celebration"
          celebrationFrame={celebrationFrame}
          celebrationId={celebrationId}
          onComplete={handleCelebrationComplete}
          showBorder={false}
          showRing={activeCelebration.showRing}
        />
      ) : null}
    </article>
  );
}

export { Privacy };
