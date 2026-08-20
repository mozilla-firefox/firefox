/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, renderHook } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { useStockSearch } from "content-src/components/Widgets/Stocks/useStockSearch";

function setup() {
  const dispatch = jest.fn();
  const recordUserAction = jest.fn();
  const menuButtonRef = { current: { focus: jest.fn() } };
  const view = renderHook(() =>
    useStockSearch({ dispatch, recordUserAction, menuButtonRef })
  );
  return { dispatch, recordUserAction, menuButtonRef, ...view };
}

const actionsOfType = (dispatch, type) =>
  dispatch.mock.calls.map(c => c[0]).filter(a => a.type === type);

describe("useStockSearch", () => {
  it("starts inactive", () => {
    const { result } = setup();
    expect(result.current.active).toBe(false);
  });

  it("open() activates the panel and clears any previous search", () => {
    const { result, dispatch } = setup();
    act(() => result.current.open());
    expect(result.current.active).toBe(true);
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_CLEAR)
    ).toHaveLength(1);
  });

  it("close() deactivates, clears the search, and refocuses the menu button", () => {
    const { result, dispatch, menuButtonRef } = setup();
    act(() => result.current.open());
    act(() => result.current.close());
    expect(result.current.active).toBe(false);
    expect(menuButtonRef.current.focus).toHaveBeenCalledTimes(1);
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_CLEAR)
    ).toHaveLength(2);
  });

  it("does not focus the menu button on mount", () => {
    const { menuButtonRef } = setup();
    expect(menuButtonRef.current.focus).not.toHaveBeenCalled();
  });

  it("submit() sends a content-only started action and a request to main, with telemetry", () => {
    const { result, dispatch, recordUserAction } = setup();
    act(() => result.current.submit("  aapl  "));
    const [started] = actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED);
    const [request] = actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_REQUEST);
    expect(started.data.query).toBe("aapl");
    expect(started.meta).toBeUndefined();
    expect(request.data.query).toBe("aapl");
    expect(request.data.requestId).toBe(started.data.requestId);
    expect(request.meta).toBeTruthy();
    expect(recordUserAction).toHaveBeenCalledWith("search_submit", {
      source: "search",
    });
  });

  it("submit() ignores an empty or whitespace query", () => {
    const { result, dispatch, recordUserAction } = setup();
    act(() => result.current.submit("   "));
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED)
    ).toHaveLength(0);
    expect(recordUserAction).not.toHaveBeenCalled();
  });

  it("uses a unique request id across separate mounts", () => {
    const first = setup();
    act(() => first.result.current.submit("AAPL"));
    first.unmount();
    const second = setup();
    act(() => second.result.current.submit("AAPL"));
    const idOf = dispatch =>
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED)[0].data
        .requestId;
    expect(idOf(first.dispatch)).not.toBe(idOf(second.dispatch));
  });
});
