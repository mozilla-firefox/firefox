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

function renderCard(entry = {}) {
  const dispatch = jest.fn();
  const { container } = render(
    <WrapWithProvider state={INITIAL_STATE}>
      <MozWidgetCard
        dispatch={dispatch}
        entry={{ ...ENTRY, ...entry }}
        size="medium"
        src="moz-extension://uuid/widget.html"
        widgetEnabledMap={{}}
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
