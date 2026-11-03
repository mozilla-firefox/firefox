/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { render } from "@testing-library/react";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { Widgets } from "content-src/components/Widgets/Widgets";

// Replace the row-widget registry with one widget that throws on render
// (lists) and one that renders normally (clocks). This lets us assert that a
// crash in a single widget is contained by its own ErrorBoundary and doesn't
// tear down its neighbor or the whole widgets section. Bug 2045049.
jest.mock("content-src/components/Widgets/WidgetsComponentRegistry.jsx", () => {
  const ReactLib = require("react");
  const ThrowingWidget = () => {
    throw new Error("widget boom");
  };
  const HealthyWidget = () =>
    ReactLib.createElement("div", { className: "healthy-widget" }, "ok");
  return {
    WIDGET_ROW_COMPONENTS: { lists: ThrowingWidget, clocks: HealthyWidget },
    WIDGET_SIDEBAR_COMPONENTS: {},
  };
});

const TWO_WIDGET_STATE = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "nova.enabled": true,
      "widgets.enabled": true,
      "widgets.lists.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.clocks.enabled": true,
      "widgets.system.clocks.enabled": true,
    },
  },
};

describe("<Widgets> per-widget error boundary (bug 2045049)", () => {
  let errorSpy;
  beforeEach(() => {
    // React logs every error caught by a boundary to console.error; silence it
    // so the deliberate throw doesn't spam the test output.
    errorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
  });
  afterEach(() => {
    errorSpy.mockRestore();
  });

  function renderWidgets() {
    const store = createStore(combineReducers(reducers), TWO_WIDGET_STATE);
    return render(
      <Provider store={store}>
        <Widgets />
      </Provider>
    );
  }

  it("contains a crashing widget without tearing down the section or its neighbor", () => {
    const { container } = renderWidgets();
    // The widgets section still renders (the throw didn't propagate up).
    expect(container.querySelector(".widgets-wrapper")).toBeInTheDocument();
    // The healthy neighbor widget still renders.
    expect(container.querySelector(".healthy-widget")).toBeInTheDocument();
    // The crashing widget shows the error fallback in its place.
    expect(
      container.querySelector(".widget-error-fallback")
    ).toBeInTheDocument();
  });
});
