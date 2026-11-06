/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, renderHook } from "@testing-library/react";
import {
  FRAMED_WIDGET_LOAD_TIMEOUT_MS,
  useFramedWidgetMessages,
  useFramedWidgetReady,
  useFramedWidgetSize,
} from "content-src/components/Widgets/useFramedWidgetMessages";

const ORIGIN = "https://widget.example.mozgcp.net";
const SRC = `${ORIGIN}/1.0.0/index.html`;
const WIDGET = {
  id: "travel",
  telemetryName: "travel",
  validSizes: ["medium", "large"],
};

let iframe;
let iframeRef;

beforeEach(() => {
  iframe = document.createElement("iframe");
  document.body.appendChild(iframe);
  iframeRef = { current: iframe };
});

afterEach(() => {
  iframe.remove();
});

// jsdom does not honor `source` from the MessageEvent init dict, so it is
// forced onto the instance.
function post(data, { origin = ORIGIN, source } = {}) {
  const event = new MessageEvent("message", { data, origin });
  Object.defineProperty(event, "source", {
    value: source === undefined ? iframe.contentWindow : source,
  });
  act(() => {
    window.dispatchEvent(event);
  });
}

function message(type, payload = {}, extra = {}) {
  return { channel: "travel_widget", type, payload, sentAt: "", ...extra };
}

const action = (name, payload) => message("action", payload, { message: name });

describe("useFramedWidgetMessages", () => {
  function renderMessages() {
    const onMessage = jest.fn();
    renderHook(() =>
      useFramedWidgetMessages({
        widget: WIDGET,
        src: SRC,
        iframeRef,
        onMessage,
      })
    );
    return onMessage;
  }

  it("passes on valid messages from the widget's frame, origin and channel", () => {
    const onMessage = renderMessages();
    post(action("explore_travel", { a: 1 }));
    post(message("widget_ready", { protocol: 1 }));
    expect(onMessage.mock.calls).toEqual([
      ["action", "explore_travel", { a: 1 }],
      ["widget_ready", undefined, { protocol: 1 }],
    ]);
  });

  it("ignores messages from elsewhere or that break the protocol", () => {
    const onMessage = renderMessages();
    post(action("explore_travel"), { origin: "https://evil.example.com" });
    post(action("explore_travel"), { source: window });
    post({ ...action("explore_travel"), channel: "crossword_widget" });
    post(action("Explore Travel"));
    post({ ...action("explore_travel"), payload: null });
    post(message("unknown_type"));
    post(message("widget_ready", { protocol: 1, extra: true }));
    post(message("widget_error", { reason: "Oops", terminal: true }));
    post(message("widget_error", { terminal: true }));
    post(message("action"));
    expect(onMessage).not.toHaveBeenCalled();
  });
});

describe("useFramedWidgetReady", () => {
  beforeEach(() => jest.useFakeTimers());
  afterEach(() => jest.useRealTimers());

  function renderReady() {
    const onError = jest.fn();
    const hook = renderHook(() =>
      useFramedWidgetReady({ widget: WIDGET, src: SRC, iframeRef, onError })
    );
    return { ...hook, onError };
  }

  it("is ready after widget_ready, with no timeout", () => {
    const { result, onError } = renderReady();
    expect(result.current).toBe(false);
    post(message("widget_ready", { protocol: 1 }));
    expect(result.current).toBe(true);
    act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
    expect(onError).not.toHaveBeenCalled();
  });

  it("reports a terminal load_timeout without widget_ready", () => {
    const { result, onError } = renderReady();
    act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
    expect(result.current).toBe(false);
    expect(onError).toHaveBeenCalledWith("load_timeout", true);
  });

  it("doesn't time out without a document", () => {
    const onError = jest.fn();
    const { result } = renderHook(() =>
      useFramedWidgetReady({ widget: WIDGET, src: "", iframeRef, onError })
    );
    act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
    expect(result.current).toBe(false);
    expect(onError).not.toHaveBeenCalled();
  });

  it("reports each non-terminal reason once and only the first terminal error", () => {
    const { onError } = renderReady();
    post(message("widget_error", { reason: "net_error", terminal: false }));
    post(message("widget_error", { reason: "net_error", terminal: false }));
    post(message("widget_error", { reason: "net_error", terminal: true }));
    post(message("widget_error", { reason: "render_failed", terminal: true }));
    post(message("widget_error", { reason: "other", terminal: false }));
    act(() => jest.advanceTimersByTime(FRAMED_WIDGET_LOAD_TIMEOUT_MS));
    expect(onError.mock.calls).toEqual([
      ["net_error", false],
      ["net_error", true],
    ]);
  });

  it("reports widget_error with its terminal flag", () => {
    const { onError } = renderReady();
    post(message("widget_error", { reason: "render_failed", terminal: false }));
    expect(onError).toHaveBeenCalledWith("render_failed", false);
  });
});

describe("useFramedWidgetSize", () => {
  function renderSize(widgetSize = "medium") {
    const onAction = jest.fn();
    const hook = renderHook(
      props =>
        useFramedWidgetSize({
          widget: WIDGET,
          src: SRC,
          iframeRef,
          onAction,
          ...props,
        }),
      { initialProps: { widgetSize } }
    );
    return { ...hook, onAction };
  }

  it("returns a valid size the widget asks for", () => {
    const { result, onAction } = renderSize();
    expect(result.current.requestedSize).toBeNull();
    post(action("change_size", { size: "large" }));
    expect(result.current.requestedSize).toBe("large");
    expect(onAction).toHaveBeenCalledWith("change_size", { size: "large" });
  });

  it("ignores sizes outside validSizes and unexpected payload keys", () => {
    const { result } = renderSize();
    post(action("change_size", { size: "xlarge" }));
    post(action("change_size", { size: "large", extra: true }));
    expect(result.current.requestedSize).toBeNull();
  });

  it("drops the request when cleared", () => {
    const { result } = renderSize();
    post(action("change_size", { size: "large" }));
    act(() => result.current.clearRequestedSize());
    expect(result.current.requestedSize).toBeNull();
  });

  it("drops the request when the widget's own size changes", () => {
    const { result, rerender } = renderSize("medium");
    post(action("change_size", { size: "large" }));
    expect(result.current.requestedSize).toBe("large");
    rerender({ widgetSize: "large" });
    expect(result.current.requestedSize).toBeNull();
  });
});
