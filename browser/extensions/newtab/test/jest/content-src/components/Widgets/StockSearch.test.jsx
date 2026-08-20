/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent } from "@testing-library/react";
import { StockSearch } from "content-src/components/Widgets/Stocks/StockSearch";
import { MAX_STOCKS_WATCHLIST } from "common/StocksWatchlist.mjs";

const AAPL = {
  ticker: "AAPL",
  name: "Apple Inc",
  last_price: "$1 USD",
  todays_change_perc: "+0.1",
};

function renderSearch(props = {}) {
  const handlers = {
    onSubmit: jest.fn(),
    onAdd: jest.fn(),
    onClose: jest.fn(),
  };
  const view = render(
    <StockSearch
      searchStatus="idle"
      searchResults={[]}
      savedSymbols={[]}
      atWatchlistLimit={false}
      {...handlers}
      {...props}
    />
  );
  return { ...view, ...handlers };
}

function messageId(container) {
  return container
    .querySelector(".stocks-search-message span")
    ?.getAttribute("data-l10n-id");
}

function messageArgs(container) {
  const raw = container
    .querySelector(".stocks-search-message span")
    ?.getAttribute("data-l10n-args");
  return raw ? JSON.parse(raw) : null;
}

describe("StockSearch", () => {
  it("idle: shows the input and an empty status region, no message or rows", () => {
    const { container } = renderSearch();
    expect(container.querySelector("moz-input-search")).toBeTruthy();
    const region = container.querySelector(".stocks-search-message");
    expect(region).toBeTruthy();
    expect(region.getAttribute("role")).toBe("status");
    expect(messageId(container)).toBeUndefined();
    expect(container.querySelector(".stock-ticker")).toBeNull();
  });

  it("keeps the same status region mounted when a message appears", () => {
    const { container, rerender } = renderSearch();
    const before = container.querySelector(".stocks-search-message");
    rerender(
      <StockSearch
        searchStatus="loading"
        searchResults={[]}
        savedSymbols={[]}
        atWatchlistLimit={false}
        onSubmit={jest.fn()}
        onAdd={jest.fn()}
        onClose={jest.fn()}
      />
    );
    expect(container.querySelector(".stocks-search-message")).toBe(before);
    expect(messageId(container)).toBe("newtab-stocks-search-loading");
  });

  it("names the input and results list and links them with aria-controls", () => {
    const { container } = renderSearch();
    expect(
      container.querySelector("moz-input-search").getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-search-input");
    const list = container.querySelector("ul.stocks-search-results");
    expect(list.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-search-results"
    );
    expect(
      container.querySelector("moz-input-search").getAttribute("aria-controls")
    ).toBe(list.id);
  });

  it("loading: announces a status and marks the list busy", () => {
    const { container } = renderSearch({ searchStatus: "loading" });
    expect(messageId(container)).toBe("newtab-stocks-search-loading");
    const list = container.querySelector("ul.stocks-search-results");
    expect(list.getAttribute("aria-busy")).toBe("true");
    expect(
      container.querySelector(".stocks-search-message").getAttribute("role")
    ).toBe("status");
  });

  it("empty: shows the no-results message", () => {
    const { container } = renderSearch({ searchStatus: "empty" });
    expect(messageId(container)).toBe("newtab-stocks-search-no-results");
  });

  it("empty: echoes the submitted query in the no-results message", () => {
    const { container, rerender } = renderSearch();
    const input = container.querySelector("moz-input-search");
    input.value = "amzn";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    rerender(
      <StockSearch
        searchStatus="empty"
        searchResults={[]}
        savedSymbols={[]}
        atWatchlistLimit={false}
        onSubmit={jest.fn()}
        onAdd={jest.fn()}
        onClose={jest.fn()}
      />
    );
    expect(messageId(container)).toBe("newtab-stocks-search-no-results");
    expect(messageArgs(container)).toEqual({ query: "amzn" });
  });

  it("error: shows the search-specific error message", () => {
    const { container } = renderSearch({ searchStatus: "error" });
    expect(messageId(container)).toBe("newtab-stocks-search-error");
  });

  it("success: renders a result row per ticker as an add row", () => {
    const { container } = renderSearch({
      searchStatus: "success",
      searchResults: [AAPL],
    });
    const row = container.querySelector(".stock-ticker--result");
    expect(row).toBeTruthy();
    const btn = row.querySelector("moz-button.stock-ticker-action");
    expect(btn.getAttribute("iconSrc")).toContain("plus.svg");
    expect(messageId(container)).toBeUndefined();
  });

  it("success: an already-saved result shows the added check, not an add button", () => {
    const { container } = renderSearch({
      searchStatus: "success",
      searchResults: [AAPL],
      savedSymbols: ["AAPL"],
    });
    expect(
      container.querySelector(".stock-ticker--result .stock-ticker-added")
    ).toBeTruthy();
    expect(
      container.querySelector(
        ".stock-ticker--result moz-button.stock-ticker-action"
      )
    ).toBeNull();
  });

  it("success: a not-yet-saved result is disabled and explained when the watchlist is full", () => {
    const { container } = renderSearch({
      searchStatus: "success",
      searchResults: [AAPL],
      atWatchlistLimit: true,
    });
    expect(
      container
        .querySelector(".stock-ticker--result moz-button.stock-ticker-action")
        .getAttribute("disabled")
    ).not.toBeNull();
    expect(messageId(container)).toBe("newtab-stocks-watchlist-full");
    expect(messageArgs(container)).toEqual({ limit: MAX_STOCKS_WATCHLIST });
  });

  it("adds a result through onAdd with the symbol and name", () => {
    const { container, onAdd } = renderSearch({
      searchStatus: "success",
      searchResults: [AAPL],
    });
    fireEvent.click(
      container.querySelector(
        ".stock-ticker--result moz-button.stock-ticker-action"
      )
    );
    expect(onAdd).toHaveBeenCalledWith("AAPL", "Apple Inc");
  });

  it("submits the trimmed query on Enter", () => {
    const { container, onSubmit } = renderSearch();
    const input = container.querySelector("moz-input-search");
    input.value = "  AAPL  ";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    expect(onSubmit).toHaveBeenCalledWith("AAPL");
  });

  it("does not submit an empty or whitespace-only query", () => {
    const { container, onSubmit } = renderSearch();
    const input = container.querySelector("moz-input-search");
    input.value = "   ";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    expect(onSubmit).not.toHaveBeenCalled();
  });

  it("closes from the back button and on Escape anywhere in the panel", () => {
    const { container, onClose } = renderSearch();
    fireEvent.click(container.querySelector("moz-button.stocks-search-back"));
    expect(onClose).toHaveBeenCalledTimes(1);
    // Escape from the input bubbles up to the panel-root handler.
    fireEvent.keyDown(container.querySelector("moz-input-search"), {
      key: "Escape",
    });
    expect(onClose).toHaveBeenCalledTimes(2);
  });
});
