/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Privacy } from "content-src/components/Widgets/Privacy/Privacy";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.system.privacy.enabled": true,
      "widgets.privacy.enabled": true,
      "widgets.privacy.size": "medium",
    },
  },
};

function WrapWithProvider({ children, state = INITIAL_STATE }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function stateWithTrackers(trackersToday, sitesToday = 9) {
  return {
    ...mockState,
    PrivacyWidget: {
      ...INITIAL_STATE.PrivacyWidget,
      initialized: true,
      trackersToday,
      sitesToday,
    },
  };
}

function stateWithMessage(message, trackersToday = 87, sitesToday = 9) {
  return {
    ...mockState,
    PrivacyWidget: {
      ...INITIAL_STATE.PrivacyWidget,
      initialized: true,
      trackersToday,
      sitesToday,
      ...message,
    },
  };
}

function renderPrivacy(dispatch = jest.fn(), props = {}, state = mockState) {
  const tree = currentState => (
    <WrapWithProvider state={currentState}>
      <Privacy
        dispatch={dispatch}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  const { container, unmount, rerender } = render(tree(state));
  return {
    container,
    unmount,
    dispatch,
    rerenderWithState: nextState => rerender(tree(nextState)),
  };
}

// Returns the WIDGETS_USER_EVENT action dispatched for a given user_action.
function findUserEvent(dispatch, userAction) {
  return dispatch.mock.calls
    .map(([action]) => action)
    .find(
      action =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data.user_action === userAction
    );
}

describe("Privacy widget", () => {
  // The impression-time telemetry now waits for the widget to be seen. The
  // default jest IntersectionObserver mock never fires, so install one that
  // reports the element as intersecting the moment it's observed.
  let originalIntersectionObserver;
  beforeEach(() => {
    originalIntersectionObserver = global.IntersectionObserver;
    global.IntersectionObserver = class {
      constructor(callback) {
        this.callback = callback;
      }
      observe(el) {
        this.callback([{ isIntersecting: true, target: el }]);
      }
      unobserve() {}
      disconnect() {}
    };
  });
  afterEach(() => {
    global.IntersectionObserver = originalIntersectionObserver;
  });

  it("renders the widget at the resolved size", () => {
    const { container } = renderPrivacy();
    const root = container.querySelector("article.privacy");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it("names the widget region for screen readers", () => {
    // The widget has no visible title, so the article carries the accessible
    // name itself via an attribute-only Fluent message.
    const { container } = renderPrivacy();
    const root = container.querySelector("article.privacy");
    expect(root.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-widget-label"
    );
  });

  it("fires widgets_impression once when the widget scrolls into view", () => {
    // beforeEach installs a firing IntersectionObserver, so the hook's
    // impression goes out on observe. This is the trigger the impression-time
    // telemetry piggybacks on, so proving it fires anchors the timing fix.
    const dispatch = jest.fn();
    renderPrivacy(dispatch);
    const impressions = dispatch.mock.calls
      .map(([action]) => action)
      .filter(action => action.type === at.WIDGETS_IMPRESSION);
    expect(impressions).toHaveLength(1);
    expect(impressions[0].data.widget_name).toBe("privacy");
  });

  it("hides the widget by setting its enabled pref to false", () => {
    const dispatch = jest.fn();
    const { container } = renderPrivacy(dispatch);
    const hide = container.querySelector(
      '[data-l10n-id="newtab-widget-menu-hide"]'
    );
    fireEvent.click(hide);
    const setPref = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.SET_PREF &&
        action.data?.name === "widgets.privacy.enabled"
    );
    expect(setPref).toBeTruthy();
    expect(setPref[0].data.value).toBe(false);
  });

  it("shows no metric state until the feed has initialized", () => {
    // Default mockState has PrivacyWidget.initialized = false.
    const { container } = renderPrivacy();
    const root = container.querySelector("article.privacy");
    expect(root).toBeTruthy();
    expect(root.className).not.toContain("is-empty");
    expect(container.querySelector(".privacy-empty")).toBeFalsy();
    expect(container.querySelector(".privacy-count")).toBeFalsy();
  });

  it("shows the empty state when no trackers are blocked today", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(0));
    expect(container.querySelector("article.privacy").className).toContain(
      "is-empty"
    );
    expect(container.querySelector(".privacy-empty-message")).toBeTruthy();
    expect(container.querySelector(".privacy-count")).toBeFalsy();
  });

  it("keeps the count but drops the across-sites line at zero sites", () => {
    // Bug 2063207: the count is real, so only the "Across 0 sites" line goes.
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithTrackers(34, 0)
    );
    expect(container.querySelector("article.privacy").className).not.toContain(
      "is-empty"
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "34"
    );
    expect(container.querySelector(".privacy-count-label")).toBeTruthy();
    expect(container.querySelector(".privacy-count-sites")).toBeFalsy();
  });

  it("leaves the empty state once the count climbs, even with a stale empty variant", () => {
    // A SYSTEM_TICK refreshes trackersToday without touching `variant`, so a
    // tab that opened at zero can carry variant "empty" with a non-zero count.
    // trackersToday must win (Dré, jsx:82).
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({ variant: "empty", icon: "shield" }, 12)
    );
    expect(container.querySelector("article.privacy").className).not.toContain(
      "is-empty"
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "12"
    );
  });

  it("uses the shield icon in the empty state, ignoring a stale decision icon", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({ variant: "blank", icon: "shieldCheck" }, 0)
    );
    const img = container.querySelector(".privacy-empty .privacy-image-icon");
    expect(img.getAttribute("src")).toContain("widget-privacy-shield.svg");
    expect(img.getAttribute("src")).not.toContain("shield-check");
  });

  it("shows today's blocked-tracker count", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(42));
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "42"
    );
    expect(container.querySelector("article.privacy").className).not.toContain(
      "is-empty"
    );
  });

  it("ceilings the readout at 999+ by default", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(1200));
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "999+"
    );
  });

  it("shows the real count past the daily-cap threshold (no 100+ ceiling)", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(250));
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "250"
    );
  });

  it("caps at the widgets.privacy.maxDisplayCount pref when set", () => {
    const base = stateWithTrackers(75);
    const state = {
      ...base,
      Prefs: {
        ...base.Prefs,
        values: { ...base.Prefs.values, "widgets.privacy.maxDisplayCount": 50 },
      },
    };
    const { container } = renderPrivacy(jest.fn(), {}, state);
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "50+"
    );
  });

  it("lets trainhopConfig.widgets.privacyMaxDisplayCount override the pref", () => {
    const base = stateWithTrackers(75);
    const state = {
      ...base,
      Prefs: {
        ...base.Prefs,
        values: {
          ...base.Prefs.values,
          "widgets.privacy.maxDisplayCount": 200,
          trainhopConfig: { widgets: { privacyMaxDisplayCount: 50 } },
        },
      },
    };
    const { container } = renderPrivacy(jest.fn(), {}, state);
    // trainhop (50) wins over the pref (200): 75 > 50 caps to "50+".
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "50+"
    );
  });

  it("lets the dedicated widgetPrivacy namespace win over the shared widgets key", () => {
    const base = stateWithTrackers(75);
    const state = {
      ...base,
      Prefs: {
        ...base.Prefs,
        values: {
          ...base.Prefs.values,
          "widgets.privacy.maxDisplayCount": 200,
          trainhopConfig: {
            widgetPrivacy: { maxDisplayCount: 50 },
            widgets: { privacyMaxDisplayCount: 100 },
          },
        },
      },
    };
    const { container } = renderPrivacy(jest.fn(), {}, state);
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "50+"
    );
  });

  it("caps the readout to countCeiling+ on the daily-cap render", () => {
    // The daily-cap decision carries countCeiling (100); the real count is 137
    // but this one render shows "100+".
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-daily-cap",
          countCeiling: 100,
        },
        137
      )
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "100+"
    );
  });

  it("shows the exact count just below the ceiling", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(87));
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "87"
    );
  });

  it("passes the site count to the across-sites line", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithTrackers(42, 7)
    );
    const sites = container.querySelector(".privacy-count-sites");
    expect(sites).toBeTruthy();
    expect(sites.getAttribute("data-l10n-args")).toBe(
      JSON.stringify({ count: 7 })
    );
  });

  it("passes the numeric count (not the ceiling string) to the label plural", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(250));
    const label = container.querySelector(".privacy-count-label");
    expect(label.getAttribute("data-l10n-args")).toBe(
      JSON.stringify({ count: 250 })
    );
  });

  it("renders the blank variant as count only (no tip, no divider)", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({ variant: "blank", icon: "shieldCheck" })
    );
    expect(container.querySelector(".privacy-count")).toBeTruthy();
    expect(container.querySelector(".privacy-divider")).toBeFalsy();
    expect(container.querySelector(".privacy-tip")).toBeFalsy();
    expect(container.querySelector(".privacy-streak")).toBeFalsy();
  });

  it("renders a View protections CTA in the blank state (info-1 label)", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "blank",
        icon: "shieldCheck",
        cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
      })
    );
    const button = container.querySelector(".privacy-cta");
    expect(button).toBeTruthy();
    expect(button.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-message-info-1-cta"
    );
    // Still no tip/divider — it's the count-only layout plus the CTA.
    expect(container.querySelector(".privacy-tip")).toBeFalsy();
    expect(container.querySelector(".privacy-divider")).toBeFalsy();
  });

  it("renders the tip variant via its l10n id and mapped icon", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "tip",
        messageId: "newtab-privacy-message-info-4",
        icon: "planet",
      })
    );
    expect(container.querySelector(".privacy-divider")).toBeTruthy();
    const tip = container.querySelector(".privacy-tip-message");
    expect(tip.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-message-info-4"
    );
    const img = container.querySelector(".privacy-image-icon");
    expect(img.getAttribute("src")).toContain("widget-privacy-planet.svg");
  });

  it("renders the streak variant with a divider and its message", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "streak",
        messageId: "newtab-privacy-message-streak",
        icon: "kit",
        countArg: { count: 5 },
      })
    );
    expect(container.querySelector("article.privacy").className).toContain(
      "has-streak"
    );
    expect(container.querySelector(".privacy-divider")).toBeTruthy();
    const streak = container.querySelector(
      ".privacy-streak .privacy-tip-message"
    );
    expect(streak.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-message-streak"
    );
  });

  it("resolves messageId as an l10n id with count args", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "tip",
        messageId: "newtab-privacy-message-milestone-week",
        icon: "kit",
        countArg: { count: 120 },
      })
    );
    const tip = container.querySelector(".privacy-tip-message");
    expect(tip.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-message-milestone-week"
    );
    expect(tip.getAttribute("data-l10n-args")).toBe(
      JSON.stringify({ count: 120 })
    );
  });

  it("renders a CTA button labelled from the message's -cta id", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "tip",
        messageId: "newtab-privacy-message-info-1",
        icon: "shield",
        cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
      })
    );
    const button = container.querySelector(".privacy-cta");
    expect(button).toBeTruthy();
    expect(button.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-message-info-1-cta"
    );
  });

  it("renders no CTA button when the decision has no cta", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({
        variant: "tip",
        messageId: "newtab-privacy-message-info-1",
        icon: "shield",
        cta: null,
      })
    );
    expect(container.querySelector(".privacy-cta")).toBeFalsy();
  });

  it("dispatches WIDGETS_PRIVACY_CTA with the action on CTA click", () => {
    const dispatch = jest.fn();
    const cta = { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } };
    const { container } = renderPrivacy(
      dispatch,
      {},
      stateWithMessage({
        variant: "tip",
        messageId: "newtab-privacy-message-info-1",
        icon: "shield",
        cta,
      })
    );
    fireEvent.click(container.querySelector(".privacy-cta"));
    const ctaAction = dispatch.mock.calls.find(
      ([action]) => action.type === at.WIDGETS_PRIVACY_CTA
    );
    expect(ctaAction).toBeTruthy();
    expect(ctaAction[0].data.action).toEqual(cta);
    expect(ctaAction[0].data.message_id).toBe("newtab-privacy-message-info-1");
  });

  it("makes a blank-state CTA click traceable via its impression", () => {
    const dispatch = jest.fn();
    const { container } = renderPrivacy(
      dispatch,
      {},
      stateWithMessage({
        variant: "blank",
        icon: "shieldCheck",
        cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
      })
    );
    // Blank logs a message_impression under the stable blank id, so the click
    // (whose action_value is the destination) can be joined back to "blank".
    const impression = findUserEvent(dispatch, "message_impression");
    expect(impression.data.action_value).toBe("newtab-privacy-blank");
    fireEvent.click(container.querySelector(".privacy-cta"));
    const ctaAction = dispatch.mock.calls.find(
      ([action]) => action.type === at.WIDGETS_PRIVACY_CTA
    );
    expect(ctaAction[0].data.message_id).toBe("newtab-privacy-blank");
    const event = findUserEvent(dispatch, "message_cta_click");
    expect(event.data.action_value).toBe("about:protections");
  });
  it("opens about:protections when the count block is clicked", () => {
    const dispatch = jest.fn();
    const { container } = renderPrivacy(dispatch, {}, stateWithTrackers(42));
    const link = container.querySelector("a.privacy-count");
    expect(link).toBeTruthy();
    expect(link.getAttribute("href")).toBe("about:protections");
    // The count number lives inside the same anchor, so it's clickable too.
    expect(link.querySelector(".privacy-count-number")).toBeTruthy();
    fireEvent.click(link);
    const action = dispatch.mock.calls
      .map(([call]) => call)
      .find(call => call.type === at.WIDGETS_PRIVACY_CTA);
    expect(action).toBeTruthy();
    expect(action.data.action).toEqual({
      type: "OPEN_ABOUT_PAGE",
      data: { args: "protections", where: "tab" },
    });
    // The click is also logged as a tracking_message_click user event.
    const clickEvent = findUserEvent(dispatch, "tracking_message_click");
    expect(clickEvent).toBeTruthy();
    expect(clickEvent.data.widget_source).toBe("widget");
  });

  it("opens about:protections when the empty-state message is clicked", () => {
    const dispatch = jest.fn();
    const { container } = renderPrivacy(dispatch, {}, stateWithTrackers(0));
    const link = container.querySelector("a.privacy-empty-details");
    expect(link).toBeTruthy();
    expect(link.getAttribute("href")).toBe("about:protections");
    fireEvent.click(link);
    const action = dispatch.mock.calls
      .map(([call]) => call)
      .find(call => call.type === at.WIDGETS_PRIVACY_CTA);
    expect(action).toBeTruthy();
    expect(action.data.action).toEqual({
      type: "OPEN_ABOUT_PAGE",
      data: { args: "protections", where: "tab" },
    });
    const clickEvent = findUserEvent(dispatch, "tracking_message_click");
    expect(clickEvent).toBeTruthy();
    expect(clickEvent.data.widget_source).toBe("widget");
  });

  describe("telemetry", () => {
    it("logs the trackers_blocked impression with 'blocked' when trackers are blocked", () => {
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage({ variant: "blank", icon: "shieldCheck" }, 42)
      );
      const event = findUserEvent(dispatch, "trackers_blocked_impression");
      expect(event).toBeTruthy();
      expect(event.data.widget_name).toBe("privacy");
      expect(event.data.widget_source).toBe("widget");
      expect(event.data.action_value).toBe("blocked");
    });

    it("logs the trackers_blocked impression with 'none' when nothing was blocked", () => {
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage({ variant: "empty", icon: "shield" }, 0)
      );
      const event = findUserEvent(dispatch, "trackers_blocked_impression");
      expect(event).toBeTruthy();
      expect(event.data.action_value).toBe("none");
    });

    it("waits for the widget to be seen before logging impressions", () => {
      // With no IntersectionObserver, the widget is never "seen" and the
      // impression-time signals stay unsent (preloaded / never-viewed tab).
      const savedObserver = global.IntersectionObserver;
      global.IntersectionObserver = class {
        observe() {}
        unobserve() {}
        disconnect() {}
      };
      try {
        const dispatch = jest.fn();
        renderPrivacy(
          dispatch,
          {},
          stateWithMessage({ variant: "blank", icon: "shieldCheck" }, 42)
        );
        const firedImpression = dispatch.mock.calls
          .map(([action]) => action)
          .some(action => action.type === at.WIDGETS_IMPRESSION);
        // Never seen: neither widgets_impression nor the impression-time
        // user events go out.
        expect(firedImpression).toBe(false);
        expect(
          findUserEvent(dispatch, "trackers_blocked_impression")
        ).toBeUndefined();
      } finally {
        global.IntersectionObserver = savedObserver;
      }
    });

    it("logs a message impression carrying the messageId when a message shows", () => {
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage({
          variant: "tip",
          messageId: "newtab-privacy-message-promo-vpn-1",
          icon: "star",
        })
      );
      const event = findUserEvent(dispatch, "message_impression");
      expect(event.data.widget_source).toBe("message");
      expect(event.data.action_value).toBe(
        "newtab-privacy-message-promo-vpn-1"
      );
    });

    it("does not log a message impression in the empty state", () => {
      // The selector sets messageId to "newtab-privacy-empty" in the empty
      // state, so the fixture mirrors that — otherwise this passes vacuously and
      // wouldn't catch a spurious empty-state impression (Dré).
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage(
          {
            variant: "empty",
            messageId: "newtab-privacy-empty",
            icon: "shield",
          },
          0
        )
      );
      expect(findUserEvent(dispatch, "message_impression")).toBeUndefined();
    });

    it("logs a blank message impression under the stable blank id", () => {
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage({ variant: "blank", icon: "shieldCheck" })
      );
      const event = findUserEvent(dispatch, "message_impression");
      expect(event).toBeTruthy();
      expect(event.data.action_value).toBe("newtab-privacy-blank");
    });

    it("does not log impressions until the feed is initialized", () => {
      // Default mockState has PrivacyWidget.initialized = false.
      const dispatch = jest.fn();
      renderPrivacy(dispatch);
      expect(findUserEvent(dispatch, "message_impression")).toBeUndefined();
      expect(
        findUserEvent(dispatch, "trackers_blocked_impression")
      ).toBeUndefined();
    });

    it("does not log impressions on a counts-only update before a message is picked", () => {
      // A SYSTEM_TICK refresh flips `initialized` with counts but no variant;
      // the one-shot must not fire yet, or the message that renders next never
      // logs its impression. stateWithTrackers sets no variant.
      const dispatch = jest.fn();
      renderPrivacy(dispatch, {}, stateWithTrackers(42));
      expect(
        findUserEvent(dispatch, "trackers_blocked_impression")
      ).toBeUndefined();
      expect(findUserEvent(dispatch, "message_impression")).toBeUndefined();
    });

    it("records widgets_enabled(false) from the context menu when hidden", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(dispatch);
      fireEvent.click(
        container.querySelector('[data-l10n-id="newtab-widget-menu-hide"]')
      );
      const enabled = dispatch.mock.calls
        .map(([action]) => action)
        .find(action => action.type === at.WIDGETS_ENABLED);
      expect(enabled).toBeTruthy();
      expect(enabled.data.widget_name).toBe("privacy");
      expect(enabled.data.widget_source).toBe("context_menu");
      expect(enabled.data.enabled).toBe(false);
    });

    it("records a learn_more user event from the context menu", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(dispatch);
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-privacy-menu-learn-more"]'
        )
      );
      const event = findUserEvent(dispatch, "learn_more");
      expect(event).toBeTruthy();
      expect(event.data.widget_source).toBe("context_menu");
    });

    it("records a change_size user event when a size is chosen", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(dispatch);
      const largeItem = container.querySelector(
        '#privacy-size-submenu panel-item[data-size="large"]'
      );
      fireEvent.click(largeItem);
      const event = findUserEvent(dispatch, "change_size");
      expect(event).toBeTruthy();
      expect(event.data.action_value).toBe("large");
      expect(event.data.widget_size).toBe("large");
    });

    it("records a message_cta_click carrying the about: destination", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage({
          variant: "tip",
          messageId: "newtab-privacy-message-info-1",
          cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
        })
      );
      fireEvent.click(container.querySelector(".privacy-cta"));
      const event = findUserEvent(dispatch, "message_cta_click");
      expect(event).toBeTruthy();
      expect(event.data.widget_source).toBe("message");
      expect(event.data.action_value).toBe("about:protections");
    });

    it("records the CTA's URL (with UTM params) as the message_cta_click value", () => {
      // Monitor/Relay CTAs carry UTM params (Bug 2061524); the whole tagged URL
      // is logged so campaign attribution survives.
      const dispatch = jest.fn();
      const taggedUrl =
        "https://monitor.mozilla.org/?utm_medium=referral&utm_source=firefox-desktop&utm_campaign=widget&utm_content=get-breach-alerts-global";
      const { container } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage({
          variant: "tip",
          messageId: "newtab-privacy-message-promo-monitor-1",
          cta: { type: "OPEN_URL", data: { args: taggedUrl } },
        })
      );
      fireEvent.click(container.querySelector(".privacy-cta"));
      const event = findUserEvent(dispatch, "message_cta_click");
      expect(event.data.action_value).toBe(taggedUrl);
    });

    it("falls back to the action type for CTAs with no URL", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage({
          variant: "tip",
          messageId: "newtab-privacy-message-info-9",
          cta: { type: "SET_DEFAULT_BROWSER" },
        })
      );
      fireEvent.click(container.querySelector(".privacy-cta"));
      const event = findUserEvent(dispatch, "message_cta_click");
      expect(event.data.action_value).toBe("SET_DEFAULT_BROWSER");
    });
  });
});

describe("Privacy widget celebration", () => {
  const anAward = (fromCount, toCount, awardedAt = 1000) => ({
    awardedAt,
    fromCount,
    toCount,
  });

  const markCalls = dispatch =>
    dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_PRIVACY_MARK_CELEBRATED
    );

  // Only that the count-up starts from fromCount. The reason it runs in a
  // layout effect — so the first *painted* frame isn't the final number — is
  // not covered: RTL flushes passive effects inside act(), and jsdom never
  // paints, so this passes under useEffect too. Needs a real browser.
  it("starts the count-up at fromCount, not the final count", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "100"
    );
  });

  it("hides the animating count from AT and exposes a stable one", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );

    const visible = container.querySelector(".privacy-count-number");
    const accessible = container.querySelector(".privacy-count-number-a11y");

    expect(visible.getAttribute("aria-hidden")).toBe("true");
    expect(visible.textContent).toBe("100");
    // Mid-animation the visible number is stale; AT still gets the true count.
    expect(accessible.textContent).toBe("137");
    expect(accessible.getAttribute("aria-hidden")).toBeNull();
  });

  it("keeps the accessible count capped in step with the visible one", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-daily-cap",
          category: "dailyCap",
          countCeiling: 100,
          celebration: anAward(100, 137),
        },
        137
      )
    );

    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "100+"
    );
    expect(
      container.querySelector(".privacy-count-number-a11y").textContent
    ).toBe("100+");
  });

  it("plays the celebration when the feed awards one", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );
    expect(container.querySelector(".privacy-celebration")).toBeTruthy();
    expect(
      container.querySelectorAll(".privacy-celebration-sparkle").length
    ).toBeGreaterThan(0);
  });

  it("acknowledges the award so a later tab can't replay it", () => {
    const dispatch = jest.fn();
    renderPrivacy(
      dispatch,
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137, 777) },
        137
      )
    );
    const marks = markCalls(dispatch);
    expect(marks.length).toBe(1);
    expect(marks[0][0].data).toBe(777);
  });

  it("does not celebrate without an award", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage({ variant: "blank" }, 4000)
    );
    expect(container.querySelector(".privacy-celebration")).toBeNull();
  });

  it("uses the loud tier with a ring only on the daily-cap render", () => {
    const plain = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );
    expect(
      plain.container.querySelector(".privacy-celebration-ring")
    ).toBeNull();

    const cap = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-daily-cap",
          category: "dailyCap",
          countCeiling: 100,
          celebration: anAward(100, 137),
        },
        137
      )
    );
    expect(
      cap.container.querySelector(".privacy-celebration-ring")
    ).toBeTruthy();
    expect(
      cap.container.querySelectorAll(".privacy-celebration-sparkle")
    ).toHaveLength(12);
  });

  it("keeps the tier and ring it started with when the message changes mid-animation", () => {
    const { container, rerenderWithState } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-daily-cap",
          category: "dailyCap",
          icon: "kit",
          countCeiling: 100,
          celebration: anAward(100, 137),
        },
        137
      )
    );
    expect(
      container.querySelectorAll(".privacy-celebration-sparkle")
    ).toHaveLength(12);
    expect(container.querySelector(".privacy-celebration-ring")).toBeTruthy();

    // A refresh swaps an ordinary tip in under the running animation. Same
    // award, so nothing re-triggers — the look must not downgrade mid-flight.
    rerenderWithState(
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-info-5",
          category: "info",
          icon: "kit",
          celebration: anAward(100, 137),
        },
        137
      )
    );

    expect(
      container.querySelectorAll(".privacy-celebration-sparkle")
    ).toHaveLength(12);
    expect(container.querySelector(".privacy-celebration-ring")).toBeTruthy();
    expect(container.querySelector("article.privacy").className).toContain(
      "is-major-celebration"
    );
  });
});

describe("Privacy widget celebration on a preloaded tab", () => {
  const anAward = (fromCount, toCount, awardedAt = 1000) => ({
    awardedAt,
    fromCount,
    toCount,
  });

  const markCalls = dispatch =>
    dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_PRIVACY_MARK_CELEBRATED
    );

  const setVisibility = value =>
    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      value,
    });

  const showTab = () => {
    setVisibility("visible");
    fireEvent(document, new Event("visibilitychange"));
  };

  afterEach(() => setVisibility("visible"));

  it("does not spend the award while the tab is still hidden", () => {
    setVisibility("hidden");
    const dispatch = jest.fn();
    const { container } = renderPrivacy(
      dispatch,
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );

    expect(markCalls(dispatch)).toHaveLength(0);
    expect(container.querySelector(".privacy-celebration")).toBeNull();
    // Held at the pre-award number, so showing the tab can't flash the final
    // count before the climb starts.
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "100"
    );
  });

  it("plays and acknowledges once the tab is shown", () => {
    setVisibility("hidden");
    const dispatch = jest.fn();
    const { container } = renderPrivacy(
      dispatch,
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137, 777) },
        137
      )
    );

    showTab();

    const marks = markCalls(dispatch);
    expect(marks).toHaveLength(1);
    expect(marks[0][0].data).toBe(777);
    expect(container.querySelector(".privacy-celebration")).toBeTruthy();
  });

  it("does not strand the readout when the animation is skipped", () => {
    const originalMatchMedia = globalThis.matchMedia;
    globalThis.matchMedia = () => ({ matches: true });
    setVisibility("hidden");

    try {
      const { container } = renderPrivacy(
        jest.fn(),
        {},
        stateWithMessage(
          { variant: "blank", celebration: anAward(100, 137) },
          137
        )
      );
      expect(container.querySelector(".privacy-count-number").textContent).toBe(
        "100"
      );

      showTab();

      // Reduced motion skips the count-up, so the hold taken while hidden has
      // to be dropped rather than leaving the pre-award number on screen.
      expect(container.querySelector(".privacy-count-number").textContent).toBe(
        "137"
      );
    } finally {
      globalThis.matchMedia = originalMatchMedia;
    }
  });

  it("holds an earned moment with no award until the tab is shown", () => {
    setVisibility("hidden");
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-daily-cap",
          category: "dailyCap",
          icon: "kit",
          countCeiling: 100,
        },
        137
      )
    );
    expect(container.querySelector(".privacy-celebration")).toBeNull();

    showTab();
    expect(container.querySelector(".privacy-celebration")).toBeTruthy();
    expect(container.querySelector(".privacy-celebration-ring")).toBeTruthy();
  });
});

describe("Privacy widget daily-cap celebration", () => {
  const capState = (extra = {}) =>
    stateWithMessage(
      {
        variant: "tip",
        messageId: "newtab-privacy-message-daily-cap",
        category: "dailyCap",
        icon: "kit",
        countCeiling: 100,
        ...extra,
      },
      137
    );

  it("fires the full celebration with the ring on its own, with no +10 award", () => {
    const { container } = renderPrivacy(jest.fn(), {}, capState());
    expect(container.querySelector(".privacy-celebration")).toBeTruthy();
    expect(container.querySelector(".privacy-celebration-ring")).toBeTruthy();
  });

  it("does not dispatch an ack when there was no award to acknowledge", () => {
    const dispatch = jest.fn();
    renderPrivacy(dispatch, {}, capState());
    const marks = dispatch.mock.calls.filter(
      ([a]) => a.type === at.WIDGETS_PRIVACY_MARK_CELEBRATED
    );
    expect(marks.length).toBe(0);
  });

  it("uses the longer sparkle tier for any major moment, not just the cap", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-milestone-month",
          category: "milestoneMonth",
          icon: "kit",
        },
        2000
      )
    );
    // Fires on its own, and with no ring: the ring is the daily cap's alone.
    expect(container.querySelector(".privacy-celebration")).toBeTruthy();
    expect(container.querySelector(".privacy-celebration-ring")).toBeNull();
    expect(container.querySelector("article.privacy").className).toContain(
      "is-major-celebration"
    );
  });

  it("tilts the kit on an earned moment with no count-up award", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "streak",
          messageId: "newtab-privacy-message-streak",
          category: "streak",
          icon: "kit",
        },
        87
      )
    );
    const icon = container.querySelector(".privacy-image-icon");
    expect(icon.getAttribute("src")).toContain("kit-circle-animated.svg");
  });

  it("leaves the kit static on an ordinary tip", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        {
          variant: "tip",
          messageId: "newtab-privacy-message-info-5",
          category: "info",
          icon: "kit",
        },
        87
      )
    );
    const icon = container.querySelector(".privacy-image-icon");
    expect(icon.getAttribute("src")).toContain("widget-privacy-kit.svg");
    expect(icon.getAttribute("src")).not.toContain("animated");
  });
});

describe("Privacy widget celebration tiers", () => {
  const sparkleCount = container =>
    container.querySelectorAll(".privacy-celebration-sparkle").length;

  const withCategory = (category, messageId) =>
    stateWithMessage({ variant: "tip", messageId, category, icon: "kit" }, 500);

  it("gives count milestones the longer sparkle", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      withCategory("milestoneMonth", "newtab-privacy-message-milestone-month")
    );
    expect(sparkleCount(container)).toBe(12);
    expect(container.querySelector("article.privacy").className).toContain(
      "is-major-celebration"
    );
  });

  it("gives a first block the brief sparkle, not the milestone one", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      withCategory("firstProtection", "newtab-privacy-message-first-protection")
    );
    expect(sparkleCount(container)).toBe(5);
    expect(container.querySelector("article.privacy").className).not.toContain(
      "is-major-celebration"
    );
  });

  it("gives a streak the brief sparkle too", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      withCategory("streak", "newtab-privacy-message-streak")
    );
    expect(sparkleCount(container)).toBe(5);
  });

  it("still tilts the kit on the brief-tier earned moments", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      withCategory("streak", "newtab-privacy-message-streak")
    );
    expect(
      container.querySelector(".privacy-image-icon").getAttribute("src")
    ).toContain("kit-circle-animated.svg");
  });
});
