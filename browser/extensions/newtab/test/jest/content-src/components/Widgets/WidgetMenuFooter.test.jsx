/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { WidgetMenuFooter } from "content-src/components/Widgets/WidgetMenuFooter";
import { actionTypes as at } from "common/Actions.mjs";
import { PREF_WIDGETS_ORDER } from "common/WidgetsRegistry.mjs";
import { WIDGET_PLACEMENTS } from "common/PageLayoutVariants.mjs";
import { WidgetPlacementContext } from "content-src/components/Widgets/WidgetPlacement";

function renderFooter(props = {}) {
  const dispatch = jest.fn();
  const { container } = render(
    <WrapWithProvider>
      <panel-list>
        <WidgetMenuFooter
          dispatch={dispatch}
          widgetName="clocks"
          enabledPref="widgets.clocks.enabled"
          widgetSize="large"
          learnMoreL10nId="newtab-clock-widget-menu-learn-more"
          {...props}
        />
      </panel-list>
    </WrapWithProvider>
  );
  return { container, dispatch };
}

describe("<WidgetMenuFooter>", () => {
  it("renders a divider, then Hide and Learn more in order", () => {
    const { container } = renderFooter();
    const children = Array.from(container.querySelector("panel-list").children);
    // No sizeSubmenu passed and MoveSubmenu renders nothing without a movable
    // widget order, so only the divider + the two fixed items remain.
    expect(children.map(c => c.tagName.toLowerCase())).toEqual([
      "hr",
      "panel-item",
      "panel-item",
    ]);
    expect(children[1].getAttribute("data-l10n-id")).toBe(
      "newtab-widget-menu-hide"
    );
    expect(children[2].getAttribute("data-l10n-id")).toBe(
      "newtab-clock-widget-menu-learn-more"
    );
  });

  it("omits the divider when showDivider is false", () => {
    const { container } = renderFooter({ showDivider: false });
    const children = Array.from(container.querySelector("panel-list").children);
    expect(children.map(c => c.tagName.toLowerCase())).toEqual([
      "panel-item",
      "panel-item",
    ]);
    expect(container.querySelector("hr")).not.toBeInTheDocument();
  });

  it("renders the passed size submenu right after the divider, before Hide", () => {
    const { container } = renderFooter({
      sizeSubmenu: (
        <panel-item data-testid="size" submenu="clocks-size-submenu" />
      ),
    });
    const children = Array.from(container.querySelector("panel-list").children);
    expect(children[0].tagName.toLowerCase()).toBe("hr");
    expect(children[1].getAttribute("data-testid")).toBe("size");
    expect(children[2].getAttribute("data-l10n-id")).toBe(
      "newtab-widget-menu-hide"
    );
  });

  it("orders divider, size, Move, Hide, Learn more when movement is available", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          [PREF_WIDGETS_ORDER]: "clocks,weather",
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <panel-list>
          <WidgetMenuFooter
            dispatch={jest.fn()}
            widgetId="clocks"
            widgetEnabledMap={{ clocks: true, weather: true }}
            widgetName="clocks"
            enabledPref="widgets.clocks.enabled"
            widgetSize="large"
            learnMoreL10nId="newtab-clock-widget-menu-learn-more"
            sizeSubmenu={
              <panel-item data-testid="size" submenu="clocks-size-submenu" />
            }
          />
        </panel-list>
      </WrapWithProvider>
    );
    const children = Array.from(container.querySelector("panel-list").children);
    expect(children[0].tagName.toLowerCase()).toBe("hr");
    expect(children[1].getAttribute("data-testid")).toBe("size");
    expect(children[2].getAttribute("submenu")).toBe("clocks-move-submenu");
    expect(children[3].getAttribute("data-l10n-id")).toBe(
      "newtab-widget-menu-hide"
    );
    expect(children[4].getAttribute("data-l10n-id")).toBe(
      "newtab-clock-widget-menu-learn-more"
    );
  });

  it("dispatches SET_PREF(false) and WIDGETS_ENABLED telemetry on hide", () => {
    const onAfterHide = jest.fn();
    const { container, dispatch } = renderFooter({
      widgetName: "weather",
      enabledPref: "showWeather",
      widgetSize: "medium",
      onAfterHide,
    });
    fireEvent.click(
      container.querySelector(
        "panel-item[data-l10n-id='newtab-widget-menu-hide']"
      )
    );

    expect(dispatch).toHaveBeenCalledTimes(2);
    expect(dispatch.mock.calls[0][0]).toMatchObject({
      type: at.SET_PREF,
      data: { name: "showWeather", value: false },
    });
    expect(dispatch.mock.calls[1][0]).toMatchObject({
      type: at.WIDGETS_ENABLED,
      data: {
        widget_name: "weather",
        widget_source: "context_menu",
        enabled: false,
        widget_size: "medium",
      },
    });
    expect(onAfterHide).toHaveBeenCalledTimes(1);
  });

  it("opens Learn more in a new tab and runs onLearnMore after it", () => {
    const onLearnMore = jest.fn();
    const { container, dispatch } = renderFooter({ onLearnMore });
    fireEvent.click(
      container.querySelector(
        "panel-item[data-l10n-id='newtab-clock-widget-menu-learn-more']"
      )
    );

    expect(dispatch).toHaveBeenCalledTimes(1);
    expect(dispatch.mock.calls[0][0]).toMatchObject({
      type: at.OPEN_LINK,
      data: { where: "tab" },
    });
    expect(onLearnMore).toHaveBeenCalledTimes(1);
  });

  it("offers Move > Home alongside Hide in the widget library", () => {
    const dispatch = jest.fn();
    const { container } = render(
      <WrapWithProvider>
        <WidgetPlacementContext.Provider value={WIDGET_PLACEMENTS.LIBRARY}>
          <panel-list>
            <WidgetMenuFooter
              dispatch={dispatch}
              widgetId="clocks"
              widgetName="clocks"
              enabledPref="widgets.clocks.enabled"
              widgetSize="large"
              learnMoreL10nId="newtab-clock-widget-menu-learn-more"
            />
          </panel-list>
        </WidgetPlacementContext.Provider>
      </WrapWithProvider>
    );
    expect(
      container.querySelector(
        "panel-item[data-l10n-id='newtab-widget-menu-move-to-home']"
      )
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        "panel-item[data-l10n-id='newtab-widget-menu-hide']"
      )
    ).toBeInTheDocument();
  });
});
