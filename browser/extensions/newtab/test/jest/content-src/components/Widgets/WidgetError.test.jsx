/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, act } from "@testing-library/react";
import { WidgetError } from "content-src/components/Widgets/WidgetError";

describe("WidgetError", () => {
  let recordError;
  let observerCallbacks;
  let observerInstances;
  // One target shared by every fake intersection entry.
  const mockTarget = {};

  beforeEach(() => {
    recordError = jest.fn();
    observerCallbacks = [];
    observerInstances = [];
    jest.spyOn(global, "IntersectionObserver").mockImplementation(cb => {
      const instance = {
        observe: jest.fn(),
        unobserve: jest.fn(),
        disconnect: jest.fn(),
      };
      observerCallbacks.push(cb);
      observerInstances.push(instance);
      return instance;
    });
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  function renderError(props = {}) {
    return render(
      <WidgetError
        recordError={recordError}
        title="Daily Crossword"
        {...props}
      />
    );
  }

  function fireIntersection() {
    const cb = observerCallbacks[observerCallbacks.length - 1];
    act(() => {
      cb([{ isIntersecting: true, target: mockTarget }]);
    });
  }

  it("shows the generic message", () => {
    const { container } = renderError();
    expect(container.querySelector(".widget-error-text")).toHaveAttribute(
      "data-l10n-id",
      "newtab-widget-error-not-available"
    );
  });

  it("puts itself in the slot it is given", () => {
    const { container } = renderError({ slot: "error" });
    expect(container.querySelector(".widget-error")).toHaveAttribute(
      "slot",
      "error"
    );
  });

  it("shows a widget's own message instead when it has one", () => {
    const { container } = renderError({
      l10nId: "newtab-stocks-error-not-available",
    });
    expect(container.querySelector(".widget-error-text")).toHaveAttribute(
      "data-l10n-id",
      "newtab-stocks-error-not-available"
    );
  });

  it("announces the name but keeps it out of sight when a header shows it", () => {
    const { container } = renderError();
    const name = container.querySelector(".widget-error-title");
    expect(name).toHaveTextContent("Daily Crossword");
    expect(name).toHaveClass("sr-only");
    expect(container.querySelector(".widget-error-titled")).toBeNull();
  });

  it("shows the name for a widget with no header of its own", () => {
    const { container } = renderError({
      title: "Picture of the day",
      headerless: true,
    });
    const name = container.querySelector(".widget-error-title");
    expect(name).toHaveTextContent("Picture of the day");
    expect(name).not.toHaveClass("sr-only");
    expect(container.querySelector(".widget-error-titled")).not.toBeNull();
  });

  it("marks the message as an alert for screen readers", () => {
    const { container } = renderError();
    expect(container.querySelector(".widget-error").getAttribute("role")).toBe(
      "alert"
    );
  });

  it("records a load_error once when the message is seen", () => {
    renderError();
    fireIntersection();
    expect(recordError).toHaveBeenCalledTimes(1);
    expect(recordError).toHaveBeenCalledWith("load_error");
  });

  it("records the error type it is given", () => {
    renderError({ errorType: "render_failed" });
    fireIntersection();
    expect(recordError).toHaveBeenCalledTimes(1);
    expect(recordError).toHaveBeenCalledWith("render_failed");
  });

  it("records nothing while the message is off screen", () => {
    const { container } = renderError();
    expect(observerInstances[0].observe).toHaveBeenCalledWith(
      container.querySelector(".widget-error")
    );
    expect(recordError).not.toHaveBeenCalled();
  });

  it("records the error only once if the observer reports twice", () => {
    renderError();
    fireIntersection();
    fireIntersection();
    expect(recordError).toHaveBeenCalledTimes(1);
  });
});
