/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { Privacy } from "content-src/components/Widgets/Privacy/Privacy";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.enabled": true,
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

function stateWithEtpOff(trackersToday = 0, extra = {}) {
  return {
    ...mockState,
    PrivacyWidget: {
      ...INITIAL_STATE.PrivacyWidget,
      initialized: true,
      etpOff: true,
      trackersToday,
      sitesToday: 9,
      ...extra,
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

function atLargeSize(state) {
  return {
    ...state,
    Prefs: {
      ...state.Prefs,
      values: { ...state.Prefs.values, "widgets.privacy.size": "large" },
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

const findImpressions = dispatch =>
  dispatch.mock.calls
    .map(([action]) => action)
    .filter(action => action.type === at.WIDGETS_IMPRESSION);

// Swaps in an observer that only reports when the returned function is called
// with an on-screen ratio (default fully on screen).
const installManualObserver = () => {
  const observers = [];
  global.IntersectionObserver = class {
    constructor(callback) {
      this.callback = callback;
      observers.push(this);
    }
    observe(el) {
      this.el = el;
    }
    unobserve() {}
    disconnect() {}
  };
  return (intersectionRatio = 1) =>
    act(() => {
      for (const observer of observers) {
        observer.callback([
          {
            isIntersecting: intersectionRatio > 0,
            intersectionRatio,
            target: observer.el,
          },
        ]);
      }
    });
};

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
  // The impression waits for the widget to be seen. The default jest
  // IntersectionObserver mock never fires, so install one that reports the
  // element fully on screen the moment it's observed.
  let originalIntersectionObserver;
  beforeEach(() => {
    originalIntersectionObserver = global.IntersectionObserver;
    global.IntersectionObserver = class {
      constructor(callback) {
        this.callback = callback;
      }
      observe(el) {
        this.callback([
          { isIntersecting: true, intersectionRatio: 1, target: el },
        ]);
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

  it("names the context menu button for screen readers", () => {
    const { container } = renderPrivacy();
    const menuButton = container.querySelector(".privacy-context-menu-button");
    expect(menuButton.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-widget-open-menu-button"
    );
  });

  it("fires widgets_impression once when the widget scrolls into view", () => {
    const dispatch = jest.fn();
    renderPrivacy(
      dispatch,
      {},
      stateWithMessage({ variant: "blank", icon: "shieldCheck" })
    );
    const impressions = findImpressions(dispatch);
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

  describe("is-count-only state class", () => {
    const rootClassName = state =>
      renderPrivacy(jest.fn(), {}, state).container.querySelector(
        "article.privacy"
      ).className;

    it("is set on a count with no tip or streak copy", () => {
      expect(rootClassName(stateWithTrackers(42))).toContain("is-count-only");
    });

    it("is set on the blank variant, which is a count plus a CTA", () => {
      expect(
        rootClassName(
          stateWithMessage({
            variant: "blank",
            icon: "shieldCheck",
            cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
          })
        )
      ).toContain("is-count-only");
    });

    it("is not set when a tip shares the card", () => {
      expect(
        rootClassName(
          stateWithMessage({
            variant: "tip",
            messageId: "newtab-privacy-message-info-4",
            icon: "planet",
          })
        )
      ).not.toContain("is-count-only");
    });

    it("is not set when a streak shares the card", () => {
      expect(
        rootClassName(
          stateWithMessage({
            variant: "streak",
            messageId: "newtab-privacy-message-streak",
            icon: "kit",
            countArg: { count: 5 },
          })
        )
      ).not.toContain("is-count-only");
    });

    it("is not set in the empty state", () => {
      expect(rootClassName(stateWithTrackers(0))).not.toContain(
        "is-count-only"
      );
    });

    it("is not set when ETP is off", () => {
      expect(rootClassName(stateWithEtpOff())).not.toContain("is-count-only");
    });

    it("is not set before the feed has initialized", () => {
      // Needs a non-zero count: at the default of 0 the empty state would
      // clear the class on its own and the initialized guard would go untested.
      const pending = stateWithTrackers(42);
      expect(
        rootClassName({
          ...pending,
          PrivacyWidget: { ...pending.PrivacyWidget, initialized: false },
        })
      ).not.toContain("is-count-only");
    });
  });

  it("shows the empty state when no trackers are blocked today", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithTrackers(0));
    expect(container.querySelector("article.privacy").className).toContain(
      "is-empty"
    );
    const messages = container.querySelectorAll(
      ".privacy-empty-message-wrapper p"
    );
    expect([...messages].map(p => p.getAttribute("data-l10n-id"))).toEqual([
      "newtab-privacy-empty-state",
      "newtab-privacy-empty-state-tally",
    ]);
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

  it("shows the ETP-off card when protection is turned off", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithEtpOff());
    expect(container.querySelector("article.privacy").className).toContain(
      "is-etp-off"
    );
    // The card carries two lines: a bold headline above the call to action.
    const headline = container.querySelector(".privacy-etp-off-bold");
    expect(headline).toBeTruthy();
    expect(headline.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-etp-off-faster-browsing"
    );
    const message = container.querySelector(".privacy-etp-off-message");
    expect(message).toBeTruthy();
    expect(message.getAttribute("data-l10n-id")).toBe(
      "newtab-privacy-etp-off-turn-on-tracking"
    );
    const img = container.querySelector(".privacy-etp-off .privacy-image-icon");
    expect(img.getAttribute("src")).toContain("widget-privacy-etp-off.svg");
  });

  it("replaces the count and any message while ETP is off", () => {
    // Trackers blocked earlier today (and the decision picked for them) are
    // still in state when protection is turned off — the card outranks both.
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      stateWithEtpOff(87, {
        variant: "tip",
        messageId: "newtab-privacy-message-info-1",
        icon: "shield",
        cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
      })
    );
    const root = container.querySelector("article.privacy");
    expect(root.className).toContain("is-etp-off");
    expect(root.className).not.toContain("has-tip-msg");
    expect(container.querySelector(".privacy-count")).toBeFalsy();
    expect(container.querySelector(".privacy-tip-message")).toBeFalsy();
  });

  it("shows the ETP-off card instead of the empty state at zero", () => {
    const { container } = renderPrivacy(jest.fn(), {}, stateWithEtpOff(0));
    const root = container.querySelector("article.privacy");
    expect(root.className).toContain("is-etp-off");
    expect(root.className).not.toContain("is-empty");
    expect(container.querySelector(".privacy-empty")).toBeFalsy();
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
    expect(button.getAttribute("size")).toBe("small");
    // Still no tip/divider — it's the count-only layout plus the CTA.
    expect(container.querySelector(".privacy-tip")).toBeFalsy();
    expect(container.querySelector(".privacy-divider")).toBeFalsy();
  });

  it("renders the blank state CTA at its default size when the widget is large", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      atLargeSize(
        stateWithMessage({
          variant: "blank",
          icon: "shieldCheck",
          cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
        })
      )
    );
    const button = container.querySelector(".privacy-cta");
    expect(button).toBeTruthy();
    expect(button.hasAttribute("size")).toBe(false);
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
    expect(button.getAttribute("size")).toBe("small");
  });

  it("renders the tip CTA at its default size when the widget is large", () => {
    const { container } = renderPrivacy(
      jest.fn(),
      {},
      atLargeSize(
        stateWithMessage({
          variant: "tip",
          messageId: "newtab-privacy-message-info-1",
          icon: "shield",
          cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
        })
      )
    );
    const button = container.querySelector(".privacy-cta");
    expect(button).toBeTruthy();
    expect(button.hasAttribute("size")).toBe(false);
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
    // Blank uses a stable id, so its impression and click share a variant.
    const [impression] = findImpressions(dispatch);
    expect(impression.data.widget_variant).toBe("newtab-privacy-blank");
    fireEvent.click(container.querySelector(".privacy-cta"));
    const ctaAction = dispatch.mock.calls.find(
      ([action]) => action.type === at.WIDGETS_PRIVACY_CTA
    );
    expect(ctaAction[0].data.message_id).toBe("newtab-privacy-blank");
    const event = findUserEvent(dispatch, "message_cta_click");
    expect(event.data.action_value).toBe("about:protections");
    expect(event.data.widget_variant).toBe("newtab-privacy-blank");
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

  it("opens the ETP section of the privacy pane when the ETP-off message is clicked", () => {
    // about:protections can't turn protection back on, so this card is the one
    // link in the widget that goes to settings rather than the dashboard.
    const dispatch = jest.fn();
    const { container } = renderPrivacy(dispatch, {}, stateWithEtpOff());
    const link = container.querySelector("a.privacy-etp-off-details");
    expect(link).toBeTruthy();
    expect(link.getAttribute("href")).toBe(
      "about:preferences#privacy-trackingprotection"
    );
    fireEvent.click(link);
    const action = dispatch.mock.calls
      .map(([call]) => call)
      .find(call => call.type === at.WIDGETS_PRIVACY_CTA);
    expect(action).toBeTruthy();
    expect(action.data.action).toEqual({
      type: "OPEN_PREFERENCES_PAGE",
      data: { category: "privacy-trackingprotection" },
    });
    const clickEvent = findUserEvent(dispatch, "tracking_message_click");
    expect(clickEvent).toBeTruthy();
    expect(clickEvent.data.widget_source).toBe("widget");
    // The destination distinguishes this click from the dashboard links, which
    // share the same user_action but record no action_value.
    expect(clickEvent.data.action_value).toBe(
      "about:preferences#privacy-trackingprotection"
    );
  });

  describe("telemetry", () => {
    const TIP_A = {
      variant: "tip",
      messageId: "newtab-privacy-message-info-1",
      icon: "shield",
      cta: { type: "OPEN_ABOUT_PAGE", data: { args: "protections" } },
    };
    const TIP_B = {
      variant: "tip",
      messageId: "newtab-privacy-message-promo-vpn-1",
      icon: "star",
      cta: { type: "OPEN_URL", data: { args: "https://example.com/vpn" } },
    };

    it("records the shown message ID as widget_variant on the impression", () => {
      const dispatch = jest.fn();
      renderPrivacy(dispatch, {}, stateWithMessage(TIP_B));
      const impressions = findImpressions(dispatch);
      expect(impressions).toHaveLength(1);
      expect(impressions[0].data.widget_variant).toBe(
        "newtab-privacy-message-promo-vpn-1"
      );
    });

    it("records the blank state under the stable blank id", () => {
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage({ variant: "blank", icon: "shieldCheck" })
      );
      expect(findImpressions(dispatch)[0].data.widget_variant).toBe(
        "newtab-privacy-blank"
      );
    });

    it("records no_trackers_blocked in the empty state", () => {
      // The selector sets messageId to "newtab-privacy-empty-state" here, so
      // the fixture mirrors it to prove that ID is never reported as shown.
      const dispatch = jest.fn();
      renderPrivacy(
        dispatch,
        {},
        stateWithMessage(
          {
            variant: "empty",
            messageId: "newtab-privacy-empty-state",
            icon: "shield",
          },
          0
        )
      );
      expect(findImpressions(dispatch)[0].data.widget_variant).toBe(
        "no_trackers_blocked"
      );
    });

    it("records etp_off while ETP is off, even with a message picked", () => {
      const dispatch = jest.fn();
      renderPrivacy(dispatch, {}, stateWithEtpOff(87, TIP_A));
      expect(findImpressions(dispatch)[0].data.widget_variant).toBe("etp_off");
    });

    it("no longer records impressions as user events", () => {
      const dispatch = jest.fn();
      renderPrivacy(dispatch, {}, stateWithMessage(TIP_A));
      expect(findUserEvent(dispatch, "message_impression")).toBeUndefined();
      expect(
        findUserEvent(dispatch, "trackers_blocked_impression")
      ).toBeUndefined();
    });

    it("waits for the widget to be seen before recording the impression", () => {
      installManualObserver();
      const dispatch = jest.fn();
      renderPrivacy(dispatch, {}, stateWithMessage(TIP_A));
      expect(findImpressions(dispatch)).toHaveLength(0);
    });

    it("doesn't count a widget in a peeked space as seen", () => {
      const dispatch = jest.fn();
      render(
        <WrapWithProvider state={stateWithMessage(TIP_A)}>
          <SpaceActiveContext.Provider value={false}>
            <Privacy
              dispatch={dispatch}
              widgetsMayBeMaximized={true}
              widgetEnabledMap={{}}
            />
          </SpaceActiveContext.Provider>
        </WrapWithProvider>
      );
      expect(findImpressions(dispatch)).toHaveLength(0);
    });

    it("holds the impression until a message is picked, then records it", () => {
      // A SYSTEM_TICK refresh flips `initialized` with counts but no variant.
      const dispatch = jest.fn();
      const { rerenderWithState } = renderPrivacy(
        dispatch,
        {},
        stateWithTrackers(42)
      );
      expect(findImpressions(dispatch)).toHaveLength(0);

      rerenderWithState(stateWithMessage(TIP_A, 42));
      const impressions = findImpressions(dispatch);
      expect(impressions).toHaveLength(1);
      expect(impressions[0].data.widget_variant).toBe(
        "newtab-privacy-message-info-1"
      );
    });

    it("waits to record a message that arrives after the widget scrolled away", () => {
      const report = installManualObserver();
      const dispatch = jest.fn();
      const { rerenderWithState } = renderPrivacy(
        dispatch,
        {},
        stateWithTrackers(42)
      );
      report(1);
      report(0);

      rerenderWithState(stateWithMessage(TIP_A, 42));
      expect(findImpressions(dispatch)).toHaveLength(0);

      report(1);
      const impressions = findImpressions(dispatch);
      expect(impressions).toHaveLength(1);
      expect(impressions[0].data.widget_variant).toBe(
        "newtab-privacy-message-info-1"
      );
    });

    it("waits to record a message that arrives while the tab is hidden", () => {
      // Opening another tab is what usually broadcasts the pick, and it hides
      // this one.
      const setVisibility = value => {
        Object.defineProperty(document, "visibilityState", {
          configurable: true,
          value,
        });
        fireEvent(document, new Event("visibilitychange"));
      };
      try {
        const dispatch = jest.fn();
        const { rerenderWithState } = renderPrivacy(
          dispatch,
          {},
          stateWithTrackers(42)
        );
        setVisibility("hidden");

        rerenderWithState(stateWithMessage(TIP_A, 42));
        expect(findImpressions(dispatch)).toHaveLength(0);

        setVisibility("visible");
        expect(findImpressions(dispatch)).toHaveLength(1);
      } finally {
        setVisibility("visible");
      }
    });

    it("records no impression, no error and no widget_variant if the feed never sends anything", () => {
      // Default mockState has PrivacyWidget.initialized = false, as when the
      // count read fails and nothing reaches the tab.
      const dispatch = jest.fn();
      const { container } = renderPrivacy(dispatch);
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-privacy-menu-learn-more"]'
        )
      );

      expect(findImpressions(dispatch)).toHaveLength(0);
      expect(
        dispatch.mock.calls.some(([action]) => action.type === at.WIDGETS_ERROR)
      ).toBe(false);
      expect(findUserEvent(dispatch, "learn_more").data).not.toHaveProperty(
        "widget_variant"
      );
    });

    it("gives a message CTA click the same widget_variant as the impression", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage(TIP_A)
      );
      fireEvent.click(container.querySelector(".privacy-cta"));
      expect(
        findUserEvent(dispatch, "message_cta_click").data.widget_variant
      ).toBe(findImpressions(dispatch)[0].data.widget_variant);
    });

    it("adds the current widget_variant to widgets_enabled when hidden from the menu", () => {
      const dispatch = jest.fn();
      const { container } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage(TIP_A)
      );
      fireEvent.click(
        container.querySelector('[data-l10n-id="newtab-widget-menu-hide"]')
      );
      const enabled = dispatch.mock.calls
        .map(([action]) => action)
        .find(action => action.type === at.WIDGETS_ENABLED);
      expect(enabled.data.widget_variant).toBe("newtab-privacy-message-info-1");
    });

    describe("after the impression", () => {
      it("keeps this tab's message when another tab picks a new one", () => {
        const dispatch = jest.fn();
        const { container, rerenderWithState } = renderPrivacy(
          dispatch,
          {},
          stateWithMessage(TIP_A, 87)
        );

        // PrivacyFeed broadcasts each new tab's pick to every open tab.
        rerenderWithState(stateWithMessage(TIP_B, 120));

        expect(
          container.querySelector(
            '[data-l10n-id="newtab-privacy-message-info-1"]'
          )
        ).toBeTruthy();
        expect(
          container.querySelector(
            '[data-l10n-id="newtab-privacy-message-promo-vpn-1"]'
          )
        ).toBeFalsy();
        expect(
          container.querySelector(".privacy-count-number").textContent
        ).toBe("120");

        fireEvent.click(container.querySelector(".privacy-cta"));
        expect(findImpressions(dispatch)).toHaveLength(1);
        expect(
          findUserEvent(dispatch, "message_cta_click").data.widget_variant
        ).toBe("newtab-privacy-message-info-1");
      });

      it("takes the next real pick once an empty-state tab's count goes up", () => {
        const empty = {
          variant: "empty",
          messageId: "newtab-privacy-empty-state",
          icon: "shield",
        };
        const dispatch = jest.fn();
        const { container, rerenderWithState } = renderPrivacy(
          dispatch,
          {},
          stateWithMessage(empty, 0)
        );
        expect(findImpressions(dispatch)[0].data.widget_variant).toBe(
          "no_trackers_blocked"
        );

        // Counts-only update: still the empty pick, so a count with no message.
        rerenderWithState(stateWithMessage(empty, 42));
        expect(container.querySelector(".privacy-cta")).toBeFalsy();

        rerenderWithState(stateWithMessage(TIP_A, 42));
        expect(
          container.querySelector(
            '[data-l10n-id="newtab-privacy-message-info-1"]'
          )
        ).toBeTruthy();

        // Locked onto the new pick, so later rotation doesn't move it.
        rerenderWithState(stateWithMessage(TIP_B, 50));
        expect(
          container.querySelector(
            '[data-l10n-id="newtab-privacy-message-promo-vpn-1"]'
          )
        ).toBeFalsy();
        fireEvent.click(container.querySelector(".privacy-cta"));
        expect(
          findUserEvent(dispatch, "message_cta_click").data.widget_variant
        ).toBe("newtab-privacy-message-info-1");
        expect(findImpressions(dispatch)).toHaveLength(1);
      });

      it("ignores another tab's daily-cap ceiling", () => {
        const { container, rerenderWithState } = renderPrivacy(
          jest.fn(),
          {},
          stateWithMessage(TIP_A, 87)
        );

        rerenderWithState(
          stateWithMessage(
            {
              variant: "tip",
              messageId: "newtab-privacy-message-daily-cap",
              category: "dailyCap",
              icon: "shield",
              countCeiling: 100,
              celebration: { awardedAt: 1000, fromCount: 100, toCount: 137 },
            },
            137
          )
        );

        expect(
          container.querySelector(".privacy-count-number").textContent
        ).not.toContain("+");
        expect(container.querySelector(".privacy-celebration-ring")).toBeNull();
      });

      it("shows the ETP-off card right away, and later events say etp_off", () => {
        // Not kept stale: a mismatch with the impression marks the visit.
        const dispatch = jest.fn();
        const { container, rerenderWithState } = renderPrivacy(
          dispatch,
          {},
          stateWithMessage(TIP_A)
        );

        rerenderWithState(stateWithEtpOff(87, TIP_A));
        fireEvent.click(
          container.querySelector(
            '[data-l10n-id="newtab-privacy-menu-learn-more"]'
          )
        );

        expect(container.querySelector(".privacy-etp-off")).toBeTruthy();
        expect(findImpressions(dispatch)[0].data.widget_variant).toBe(
          "newtab-privacy-message-info-1"
        );
        expect(findUserEvent(dispatch, "learn_more").data.widget_variant).toBe(
          "etp_off"
        );
      });
    });

    it("holds the impression for a count with no message until a real pick arrives", () => {
      // A tab preloaded at zero keeps its empty pick; counts-only updates don't
      // re-run the selector. Once seen, it shows a count and no message.
      const report = installManualObserver();
      const empty = {
        variant: "empty",
        messageId: "newtab-privacy-empty-state",
        icon: "shield",
      };
      const dispatch = jest.fn();
      const { container, rerenderWithState } = renderPrivacy(
        dispatch,
        {},
        stateWithMessage(empty, 0)
      );
      rerenderWithState(stateWithMessage(empty, 42));
      report(1);

      expect(container.querySelector(".privacy-count-number").textContent).toBe(
        "42"
      );
      expect(findImpressions(dispatch)).toHaveLength(0);

      rerenderWithState(stateWithMessage(TIP_A, 42));
      const impressions = findImpressions(dispatch);
      expect(impressions).toHaveLength(1);
      expect(impressions[0].data.widget_variant).toBe(
        "newtab-privacy-message-info-1"
      );
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

  const visibleCalls = dispatch =>
    dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_PRIVACY_VISIBLE
    );

  // The global mock never fires. Install one reporting a given ratio, so "on
  // screen", "scrolled past" and "only a sliver showing" are all reachable.
  const withObserver = (ratio, run) => {
    const saved = global.IntersectionObserver;
    global.IntersectionObserver = class {
      constructor(callback) {
        this.callback = callback;
      }
      observe(el) {
        this.callback([
          { isIntersecting: ratio > 0, intersectionRatio: ratio, target: el },
        ]);
      }
      unobserve() {}
      disconnect() {}
    };
    try {
      run();
    } finally {
      global.IntersectionObserver = saved;
    }
  };

  it("reports on screen once the tab is shown", () => {
    setVisibility("hidden");
    const dispatch = jest.fn();
    withObserver(1, () => {
      renderPrivacy(dispatch, {}, stateWithTrackers(100));

      // Nothing while preloaded: the user cannot see it yet.
      expect(visibleCalls(dispatch)).toHaveLength(0);

      showTab();

      expect(visibleCalls(dispatch)).toHaveLength(1);
    });
  });

  it("stays quiet while the widget is scrolled out of view", () => {
    // A widget below the fold, or dropped from a collapsed row, is never seen,
    // so it must not cost a count re-read.
    const dispatch = jest.fn();
    withObserver(0, () => {
      renderPrivacy(dispatch, {}, stateWithTrackers(100));
      expect(visibleCalls(dispatch)).toHaveLength(0);
    });
  });

  it("stays quiet when only a sliver of the widget is showing", () => {
    // isIntersecting is true for any non-zero overlap, so the ratio is what has
    // to clear ON_SCREEN_THRESHOLD.
    const dispatch = jest.fn();
    withObserver(0.1, () => {
      renderPrivacy(dispatch, {}, stateWithTrackers(100));
      expect(visibleCalls(dispatch)).toHaveLength(0);
    });
  });

  it("asks once per reveal, not once per render", () => {
    // The ref callback is an inline arrow, so React reattaches it every render.
    // If that churned rootEl the observer would be rebuilt — and an observer
    // fires on observe() — so a plain re-render would re-ask.
    const dispatch = jest.fn();
    withObserver(1, () => {
      const { rerenderWithState } = renderPrivacy(
        dispatch,
        {},
        stateWithTrackers(100)
      );
      expect(visibleCalls(dispatch)).toHaveLength(1);
      rerenderWithState(stateWithTrackers(101));
      rerenderWithState(stateWithTrackers(102));
      expect(visibleCalls(dispatch)).toHaveLength(1);
    });
  });

  it("reports on screen for a tab that was visible from the start", () => {
    const dispatch = jest.fn();
    withObserver(1, () => {
      renderPrivacy(dispatch, {}, stateWithTrackers(100));
      expect(visibleCalls(dispatch)).toHaveLength(1);
    });
  });

  it("drops the hold when the award is withdrawn while the tab is hidden", () => {
    setVisibility("hidden");
    const { container, rerenderWithState } = renderPrivacy(
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

    // Another tab played the award and acknowledged it, so the feed cleared it
    // and the next broadcast carries no celebration. Nothing will ever pay off
    // the hold now, so it has to be dropped here (Bug 2063963).
    rerenderWithState(
      stateWithMessage({ variant: "blank", celebration: null }, 150)
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "150"
    );

    // Showing the tab must not resurrect the stale number either.
    showTab();
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "150"
    );
  });

  it("keeps tracking the count after a withdrawn award, without a reload", () => {
    setVisibility("hidden");
    const { container, rerenderWithState } = renderPrivacy(
      jest.fn(),
      {},
      stateWithMessage(
        { variant: "blank", celebration: anAward(100, 137) },
        137
      )
    );

    rerenderWithState(
      stateWithMessage({ variant: "blank", celebration: null }, 150)
    );
    showTab();
    // A later count refresh still has to move the readout: the freeze used to
    // survive for the life of the document, so only a reload fixed it.
    rerenderWithState(
      stateWithMessage({ variant: "blank", celebration: null }, 162)
    );
    expect(container.querySelector(".privacy-count-number").textContent).toBe(
      "162"
    );
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
