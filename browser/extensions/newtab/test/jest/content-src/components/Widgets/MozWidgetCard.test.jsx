/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { fireEvent, render } from "@testing-library/react";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { MozWidgetCard } from "content-src/components/Widgets/MozWidgetCard";
import { WrapWithProvider } from "test/jest/test-utils";

const ENTRY = {
  id: "extensionWidget",
  telemetryName: "extension_widget",
  enabledPref: "widgets.extensionWidget.enabled",
  sizePref: "widgets.extensionWidget.size",
  validSizes: ["medium", "large"],
  extensionID: "fixture-widget@mozilla.org",
  name: "Tally",
};

function renderCard(
  entry = {},
  prefs = {},
  src = "moz-extension://uuid/widget.html",
  size = "medium",
  displaySize = size,
  props = {}
) {
  const dispatch = jest.fn();
  const state = {
    ...INITIAL_STATE,
    Prefs: { ...INITIAL_STATE.Prefs, values: prefs },
  };
  const { container } = render(
    <WrapWithProvider state={state}>
      <MozWidgetCard
        dispatch={dispatch}
        entry={{ ...ENTRY, ...entry }}
        size={size}
        displaySize={displaySize}
        src={src}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  const card = container.querySelector("moz-widget");
  const header = card.querySelector("moz-widget-header");
  const menu = card.querySelector(':scope > panel-list[slot="menu"]');
  return { card, dispatch, header, menu };
}

function findUserEvent(dispatch, userAction) {
  return dispatch.mock.calls.find(
    ([action]) =>
      action.type === at.WIDGETS_USER_EVENT &&
      action.data.user_action === userAction
  )?.[0];
}

function menuItems(menu) {
  return [...menu.children].map(el => el.dataset.l10nId ?? el.localName);
}

describe("<MozWidgetCard>", () => {
  it("draws the entry's icon before its name in the header", () => {
    const { header } = renderCard({ icon: "chrome://test/content/icon.png" });
    const icon = header.querySelector(':scope > img[slot="leading"]');
    expect(icon.getAttribute("src")).toBe("chrome://test/content/icon.png");
    expect(icon.getAttribute("alt")).toBe("");
  });

  it("shows a placeholder without a document", () => {
    const { card } = renderCard({}, {}, "");
    expect(
      card.querySelector(':scope > p[data-l10n-id="newtab-widget-placeholder"]')
    ).not.toBeNull();
  });

  it("shows no placeholder with a document", () => {
    const { card } = renderCard();
    expect(card.querySelector(":scope > p")).toBeNull();
  });

  it("draws no icon for an entry without one", () => {
    const { header } = renderCard();
    expect(header.querySelector("img")).toBeNull();
  });

  it("renders the shared widget items", () => {
    const { menu } = renderCard();
    expect(menuItems(menu)).toEqual([
      "panel-item",
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("localizes the card and header with the entry's l10nId", () => {
    const { card, header } = renderCard({ l10nId: "tally-name" });
    expect(card).toHaveAttribute("data-l10n-id", "tally-name");
    expect(header).toHaveAttribute("data-l10n-id", "tally-name");
  });

  it("titles the card with the widget's name", () => {
    const { card } = renderCard();
    expect(card).toHaveAttribute("title", "Tally");
  });

  it("renders only Hide and Learn more without sizes", () => {
    const { menu } = renderCard({ validSizes: ["large"] });
    expect(menuItems(menu)).toEqual([
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("writes the size pref and records change_size", () => {
    const { dispatch, menu } = renderCard();
    fireEvent.click(menu.querySelector("panel-item[data-size='large']"));

    expect(dispatch).toHaveBeenCalledWith(
      expect.objectContaining({
        type: at.SET_PREF,
        data: { name: ENTRY.sizePref, value: "large" },
      })
    );
    expect(findUserEvent(dispatch, "change_size").data).toMatchObject({
      widget_name: "extension_widget",
      widget_source: "context_menu",
      action_value: "large",
      widget_size: "large",
    });
  });

  it("records learn_more", () => {
    const { dispatch, menu } = renderCard();
    fireEvent.click(
      menu.querySelector(
        "panel-item[data-l10n-id='newtab-widget-section-menu-learn-more']"
      )
    );

    expect(findUserEvent(dispatch, "learn_more").data).toMatchObject({
      widget_name: "extension_widget",
      widget_source: "context_menu",
    });
  });

  it("draws the card at its display size", () => {
    const { card } = renderCard({}, {}, undefined, "medium", "large");
    expect(card).toHaveAttribute("size", "large");
  });

  it("draws menu items above the shared items", () => {
    const { menu } = renderCard({}, {}, undefined, "medium", "medium", {
      menuItems: <panel-item data-l10n-id="tally-extra" />,
    });
    expect(menuItems(menu)).toEqual([
      "tally-extra",
      "hr",
      "panel-item",
      "newtab-widget-menu-hide",
      "newtab-widget-section-menu-learn-more",
    ]);
  });

  it("tells onChangeSize the size the user chooses", () => {
    const onChangeSize = jest.fn();
    const { menu } = renderCard({}, {}, undefined, "medium", "medium", {
      onChangeSize,
    });
    fireEvent.click(menu.querySelector("panel-item[data-size='large']"));
    expect(onChangeSize).toHaveBeenCalledWith("large");
  });

  it("sets cardRef to the moz-widget", () => {
    const cardRef = { current: null };
    const { card } = renderCard({}, {}, undefined, "medium", "medium", {
      cardRef,
    });
    expect(cardRef.current).toBe(card);
  });

  describe("impression telemetry", () => {
    let originalIntersectionObserver;
    let observerInstances;

    beforeEach(() => {
      observerInstances = [];
      originalIntersectionObserver = global.IntersectionObserver;
      global.IntersectionObserver = class MockIntersectionObserver {
        constructor(callback) {
          this.callback = callback;
          this.observed = [];
          observerInstances.push(this);
        }
        observe(el) {
          this.observed.push(el);
        }
        unobserve() {}
        disconnect() {}
      };
    });

    afterEach(() => {
      global.IntersectionObserver = originalIntersectionObserver;
    });

    it("records an impression of the card", () => {
      const { card, dispatch } = renderCard();
      const [observer] = observerInstances;
      expect(observer.observed).toEqual([card]);

      observer.callback([{ isIntersecting: true, target: card }], observer);

      expect(dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WIDGETS_IMPRESSION,
          data: { widget_name: "extension_widget", widget_size: "medium" },
        })
      );
    });
  });
});
