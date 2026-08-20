/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { parseWatchlist } from "common/StocksWatchlist.mjs";
import { Stocks } from "content-src/components/Widgets/Stocks/Stocks";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.system.stocks.enabled": true,
      "widgets.stocks.enabled": true,
      "widgets.stocks.size": "medium",
    },
  },
};

// A single Markets ticker; the watchlist tests save its symbol ("SPY").
const WATCHLIST_SAMPLE = [
  {
    ticker: "SPY",
    name: "SPDR S&P 500 ETF Trust",
    last_price: "$559.44 USD",
    todays_change_perc: "+0.2",
  },
];

const WL_ROWS = [
  {
    ticker: "AAPL",
    name: "Apple Inc",
    last_price: "$1 USD",
    todays_change_perc: "+0.1",
  },
  {
    ticker: "MSFT",
    name: "Microsoft",
    last_price: "$2 USD",
    todays_change_perc: "+0.2",
  },
  {
    ticker: "AMZN",
    name: "Amazon",
    last_price: "$3 USD",
    todays_change_perc: "-0.1",
  },
  {
    ticker: "NVDA",
    name: "Nvidia",
    last_price: "$4 USD",
    todays_change_perc: "+0.3",
  },
  {
    ticker: "SPYX",
    name: "SpyxCo",
    last_price: "$5 USD",
    todays_change_perc: "+0.1",
  },
  {
    ticker: "TSLA",
    name: "Tesla",
    last_price: "$6 USD",
    todays_change_perc: "+0.1",
  },
];

function WrapWithProvider({ children, state = INITIAL_STATE }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function renderStocks(dispatch = jest.fn(), props = {}) {
  const { container, unmount } = render(
    <WrapWithProvider state={mockState}>
      <Stocks
        dispatch={dispatch}
        handleUserInteraction={jest.fn()}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  return { container, unmount, dispatch };
}

function renderStocksState({
  size = "medium",
  tickers = [],
  error = false,
  watchlist = "",
  watchlistTickers = [],
  watchlistReconciledSymbols = parseWatchlist(watchlist),
  lastUpdated = 1,
  dispatch = jest.fn(),
} = {}) {
  const state = {
    ...mockState,
    Prefs: {
      ...mockState.Prefs,
      values: {
        ...mockState.Prefs.values,
        "widgets.stocks.size": size,
        "widgets.stocks.watchlist": watchlist,
      },
    },
    Stocks: {
      tickers,
      lastUpdated,
      error,
      watchlistTickers,
      watchlistReconciledSymbols,
    },
  };
  const store = createStore(combineReducers(reducers), state);
  const view = render(
    <Provider store={store}>
      <Stocks
        dispatch={dispatch}
        handleUserInteraction={jest.fn()}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
      />
    </Provider>
  );
  return { ...view, store, dispatch };
}

// Simulate the main-process pref broadcast that follows a SET_PREF, so a test can deliver
// an external pref change or the acknowledgement of the component's own write.
function broadcastPref(store, name, value) {
  act(() => {
    store.dispatch({ type: at.PREF_CHANGED, data: { name, value } });
  });
}

describe("Stocks widget", () => {
  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("renders the widget at the resolved size", () => {
    const { container } = renderStocks();
    const root = container.querySelector("article.stocks");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it.each(["small", "medium", "large"])(
    "with no watchlist, labels the region with a visible <h2> Stocks heading and no dropdown at %s size",
    size => {
      const { container } = renderStocksState({ size });
      const article = container.querySelector("article.stocks");
      expect(article.getAttribute("aria-labelledby")).toBe(
        "stocks-widget-label"
      );
      const label = container.querySelector("#stocks-widget-label");
      expect(label.tagName).toBe("H2");
      expect(label.getAttribute("data-l10n-id")).toBe(
        "newtab-stocks-widget-title"
      );
      expect(label.hasAttribute("hidden")).toBe(false);
      expect(label.classList.contains("sr-only")).toBe(false);
      expect(container.querySelector(".stocks-list-button")).toBeNull();
    }
  );

  it("at large with a non-empty watchlist, shows the dropdown (defaulted to Watchlist) and hides the heading", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "SPY",
    });
    const button = container.querySelector(".stocks-list-button");
    expect(button).toBeTruthy();
    expect(button.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-list-watchlist"
    );
    expect(
      container
        .querySelector("#stocks-widget-label")
        .classList.contains("sr-only")
    ).toBe(true);
  });

  it("keeps the Watchlist selection across large, medium, and small", () => {
    const { container, store } = renderStocksState({
      size: "large",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "SPY",
      watchlistTickers: WATCHLIST_SAMPLE,
    });
    const buttonL10n = () =>
      container
        .querySelector(".stocks-list-button")
        ?.getAttribute("data-l10n-id");
    expect(buttonL10n()).toBe("newtab-stocks-list-watchlist");
    broadcastPref(store, "widgets.stocks.size", "medium");
    expect(buttonL10n()).toBe("newtab-stocks-list-watchlist");
    expect(container.querySelector("ul.stocks-grid").textContent).toContain(
      "SPY"
    );
    broadcastPref(store, "widgets.stocks.size", "small");
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    broadcastPref(store, "widgets.stocks.size", "large");
    expect(buttonL10n()).toBe("newtab-stocks-list-watchlist");
  });

  it("medium with a watchlist shows the dropdown (Watchlist) and a grid of the saved rows", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT",
      watchlistTickers: WL_ROWS.slice(0, 2),
    });
    const button = container.querySelector(".stocks-list-button");
    expect(button).toBeTruthy();
    expect(button.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-list-watchlist"
    );
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid).toBeTruthy();
    expect(grid.querySelectorAll(".stock-ticker").length).toBe(2);
    expect(grid.textContent).toContain("AAPL");
    expect(grid.textContent).toContain("MSFT");
  });

  it("medium watchlist caps at the first four saved rows in order", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT,AMZN,NVDA,SPYX,TSLA",
      watchlistTickers: WL_ROWS,
    });
    expect(
      container
        .querySelector(".stocks-list-button")
        .getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-list-watchlist");
    const grid = container.querySelector("ul.stocks-grid");
    expect(
      [...grid.querySelectorAll(".stock-ticker-symbol")].map(n => n.textContent)
    ).toEqual(["AAPL", "MSFT", "AMZN", "NVDA"]);
  });

  it("switches the medium list between Watchlist and Markets via the dropdown", () => {
    const { container } = renderStocksState({
      size: "medium",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "AAPL,MSFT",
      watchlistTickers: WL_ROWS.slice(0, 2),
    });
    const buttonL10n = () =>
      container
        .querySelector(".stocks-list-button")
        .getAttribute("data-l10n-id");
    const gridText = () =>
      container.querySelector("ul.stocks-grid").textContent;
    expect(buttonL10n()).toBe("newtab-stocks-list-watchlist");
    expect(gridText()).toContain("AAPL");
    expect(gridText()).not.toContain("SPY");
    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="markets"]'
      )
    );
    expect(buttonL10n()).toBe("newtab-stocks-list-markets");
    expect(gridText()).toContain("SPY");
    expect(gridText()).not.toContain("AAPL");
    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="watchlist"]'
      )
    );
    expect(buttonL10n()).toBe("newtab-stocks-list-watchlist");
    expect(gridText()).toContain("AAPL");
    expect(gridText()).not.toContain("SPY");
  });

  it("medium watchlist rows are view-only (no add/remove control)", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT",
      watchlistTickers: WL_ROWS.slice(0, 2),
    });
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid.textContent).toContain("AAPL");
    expect(grid.querySelector(".stock-ticker-action")).toBeNull();
    expect(grid.querySelector(".stock-ticker-added")).toBeNull();
  });

  it("medium with an empty watchlist shows Markets and no dropdown", () => {
    const { container } = renderStocksState({
      size: "medium",
      tickers: WATCHLIST_SAMPLE,
    });
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(
      container.querySelectorAll(".stocks-grid .stock-ticker").length
    ).toBeGreaterThan(0);
  });

  it("medium watchlist while loading with nothing resolved: aria-busy, dimmed, capped at four", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT,AMZN,NVDA,SPYX,TSLA",
      watchlistTickers: [],
      watchlistReconciledSymbols: [],
    });
    const button = container.querySelector(".stocks-list-button");
    expect(button).toBeTruthy();
    expect(button.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-list-watchlist"
    );
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid.getAttribute("aria-busy")).toBe("true");
    expect(grid.className).toContain("stocks-grid--loading");
    expect(grid.querySelectorAll(".stock-ticker").length).toBe(4);
  });

  it("medium watchlist partially resolved: undimmed, rows plus placeholders capped at four", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT,AMZN,NVDA,SPYX,TSLA",
      watchlistTickers: WL_ROWS.slice(0, 1),
      watchlistReconciledSymbols: ["AAPL"],
    });
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid.getAttribute("aria-busy")).toBe("true");
    expect(grid.className).not.toContain("stocks-grid--loading");
    expect(grid.textContent).toContain("AAPL");
    expect(grid.querySelectorAll(".stock-ticker").length).toBe(4);
  });

  it("medium watchlist drops a symbol that resolved to no data once ready", () => {
    const { container } = renderStocksState({
      size: "medium",
      watchlist: "AAPL,MSFT,AMZN,NVDA",
      watchlistTickers: [WL_ROWS[0], WL_ROWS[2], WL_ROWS[3]],
    });
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid.getAttribute("aria-busy")).toBe("false");
    expect(grid.querySelectorAll(".stock-ticker").length).toBe(3);
    expect(grid.textContent).not.toContain("Microsoft");
  });

  it("medium returns to Markets when every saved symbol fails to resolve", () => {
    const { container } = renderStocksState({
      size: "medium",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "AAPL,MSFT",
      watchlistTickers: [],
      watchlistReconciledSymbols: ["AAPL", "MSFT"],
    });
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(container.querySelector("ul.stocks-grid").textContent).toContain(
      "SPY"
    );
  });

  it("medium watchlist collapses to Markets when the watchlist is emptied after display", () => {
    const { container, store } = renderStocksState({
      size: "medium",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "AAPL,MSFT",
      watchlistTickers: WL_ROWS.slice(0, 2),
    });
    expect(
      container
        .querySelector(".stocks-list-button")
        .getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-list-watchlist");
    broadcastPref(store, "widgets.stocks.watchlist", "");
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(container.querySelector("ul.stocks-grid").textContent).toContain(
      "SPY"
    );
  });

  it("medium watchlist stays visible when the default feed errored", () => {
    const { container } = renderStocksState({
      size: "medium",
      error: true,
      tickers: [],
      watchlist: "AAPL,MSFT",
      watchlistTickers: WL_ROWS.slice(0, 2),
    });
    expect(container.querySelector(".stocks-error")).toBeNull();
    const grid = container.querySelector("ul.stocks-grid");
    expect(grid.textContent).toContain("AAPL");
    expect(grid.querySelectorAll(".stock-ticker").length).toBe(2);
  });

  it("offers small, medium, and large sizes", () => {
    const { container } = renderStocks();
    const items = [
      ...container.querySelectorAll(
        "#stocks-size-submenu panel-item[type='checkbox']"
      ),
    ];
    const sizes = items.map(el => el.getAttribute("data-size"));
    expect(sizes).toEqual(["small", "medium", "large"]);
    expect(items.every(el => !el.hasAttribute("disabled"))).toBe(true);
  });

  it("records change_size with action_value and widget_size 'small'", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    fireEvent.click(
      container.querySelector(
        '#stocks-size-submenu panel-item[data-size="small"]'
      )
    );
    const evt = dispatch.mock.calls.find(
      ([a]) =>
        a.type === at.WIDGETS_USER_EVENT &&
        a.data?.user_action === "change_size" &&
        a.data?.action_value === "small"
    );
    expect(evt).toBeTruthy();
    expect(evt[0].data.widget_size).toBe("small");
  });

  it("hides the widget by setting its enabled pref to false", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    const hide = container.querySelector(
      '[data-l10n-id="newtab-widget-menu-hide"]'
    );
    fireEvent.click(hide);
    const setPref = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.SET_PREF &&
        action.data?.name === "widgets.stocks.enabled"
    );
    expect(setPref).toBeTruthy();
    expect(setPref[0].data.value).toBe(false);
  });

  it("records a telemetry event for the ticker-search stub", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    const search = container.querySelector(
      '[data-l10n-id="newtab-stocks-menu-search-stocks"]'
    );
    fireEvent.click(search);
    const evt = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "search_tickers"
    );
    expect(evt).toBeTruthy();
    expect(evt[0].data).toMatchObject({
      widget_name: "stocks",
      widget_source: "context_menu",
      widget_size: "medium",
    });
    expect(evt[0].data.action_value).toBeUndefined();
  });

  it("records the change_size user event with the new size", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    fireEvent.click(
      container.querySelector(
        '#stocks-size-submenu panel-item[data-size="large"]'
      )
    );
    const evt = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "change_size"
    );
    expect(evt).toBeTruthy();
    expect(evt[0].data).toMatchObject({
      widget_name: "stocks",
      widget_source: "context_menu",
      user_action: "change_size",
      action_value: "large",
      widget_size: "large",
    });
  });

  describe("interaction pref", () => {
    function renderWithInteraction() {
      const handleUserInteraction = jest.fn();
      const { container } = renderStocks(jest.fn(), { handleUserInteraction });
      return { container, handleUserInteraction };
    }

    it("flips the interaction pref on change_size", () => {
      const { container, handleUserInteraction } = renderWithInteraction();
      fireEvent.click(
        container.querySelector(
          '#stocks-size-submenu panel-item[data-size="large"]'
        )
      );
      expect(handleUserInteraction).toHaveBeenCalledWith("stocks");
    });

    it("flips the interaction pref on search_tickers", () => {
      const { container, handleUserInteraction } = renderWithInteraction();
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-stocks-menu-search-stocks"]'
        )
      );
      expect(handleUserInteraction).toHaveBeenCalledWith("stocks");
    });

    it("flips the interaction pref on learn_more", () => {
      const { container, handleUserInteraction } = renderWithInteraction();
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-stocks-menu-learn-more"]'
        )
      );
      expect(handleUserInteraction).toHaveBeenCalledWith("stocks");
    });

    it("does not flip the interaction pref when hiding the widget", () => {
      const { container, handleUserInteraction } = renderWithInteraction();
      fireEvent.click(
        container.querySelector('[data-l10n-id="newtab-widget-menu-hide"]')
      );
      expect(handleUserInteraction).not.toHaveBeenCalled();
    });
  });

  function renderStocksWithTickers(tickers, size = "medium") {
    const state = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: { ...mockState.Prefs.values, "widgets.stocks.size": size },
      },
      Stocks: { tickers, lastUpdated: 1 },
    };
    return render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
  }

  // Matches what Merino actually returns: last_price ends with " USD" and
  // todays_change_perc has no "%". The widget formats both for display.
  const SAMPLE = [
    {
      ticker: "SPY",
      name: "SPDR S&P 500 ETF Trust",
      last_price: "$559.44 USD",
      todays_change_perc: "+0.2",
    },
    {
      ticker: "ONEQ",
      name: "Fidelity Nasdaq Composite",
      last_price: "$70.10 USD",
      todays_change_perc: "-0.21",
    },
    {
      ticker: "DIA",
      name: "SPDR Dow Jones ETF",
      last_price: "$430.00 USD",
      todays_change_perc: "0.00",
    },
    {
      ticker: "IWM",
      name: "iShares Russell 2000 ETF",
      last_price: "$220.00 USD",
      todays_change_perc: "+1.0",
    },
  ];

  it("renders one card per ticker in the medium grid with formatted price and change", () => {
    const { container } = renderStocksWithTickers(SAMPLE, "medium");
    expect(
      container.querySelectorAll(".stocks-grid .stock-ticker").length
    ).toBe(4);
    expect(container.querySelector(".stock-ticker-symbol").textContent).toBe(
      "SPY"
    );
    expect(container.querySelector(".stock-ticker-price").textContent).toBe(
      "$559.44"
    );
    expect(container.querySelector(".stock-ticker-change").textContent).toBe(
      "+0.2%"
    );
  });

  it("renders placeholder cards before data arrives", () => {
    const { container } = renderStocksWithTickers([], "medium");
    expect(container.querySelector(".stocks-grid--loading")).toBeTruthy();
    expect(
      container.querySelectorAll(".stocks-grid .stock-ticker").length
    ).toBe(4);
    expect(container.querySelectorAll(".stock-ticker-sr").length).toBe(0);
  });

  it("renders the large size as a vertical list showing full names", () => {
    const { container } = renderStocksWithTickers(SAMPLE, "large");
    expect(container.querySelector(".stocks-grid")).toBeNull();
    expect(
      container.querySelectorAll(".stocks-list .stock-ticker--large").length
    ).toBe(4);
    expect(container.querySelector(".stock-ticker-name").textContent).toBe(
      "SPDR S&P 500 ETF Trust"
    );
    expect(container.querySelector(".stock-ticker-price").textContent).toBe(
      "$559.44"
    );
    expect(container.querySelector(".stock-ticker-change").textContent).toBe(
      "+0.2%"
    );
  });

  it("renders placeholder cards before data arrives at the large size", () => {
    const { container } = renderStocksWithTickers([], "large");
    expect(container.querySelector(".stocks-list--loading")).toBeTruthy();
    expect(
      container.querySelectorAll(".stocks-list .stock-ticker--large").length
    ).toBe(4);
    expect(container.querySelectorAll(".stock-ticker-sr").length).toBe(0);
  });

  function renderStocksWithError(size) {
    const state = {
      Stocks: { tickers: [], lastUpdated: null, error: true },
      Prefs: { values: { "widgets.stocks.size": size } },
    };
    return render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
  }

  it("renders the error box (icon + message) at medium when error and no data", () => {
    const { container } = renderStocksWithError("medium");
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(
      container.querySelector(".stocks-error .icon-info-warning")
    ).toBeTruthy();
    expect(
      container.querySelector(
        "[data-l10n-id='newtab-stocks-error-not-available']"
      )
    ).toBeTruthy();
    expect(container.querySelector(".stocks-grid")).toBeNull();
  });

  it("renders the error box at large when error and no data", () => {
    const { container } = renderStocksWithError("large");
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(container.querySelector(".stocks-list")).toBeNull();
  });

  it("shows data, not the error box, when tickers exist even if error is set", () => {
    const state = {
      Stocks: { tickers: SAMPLE, lastUpdated: 1, error: true },
      Prefs: { values: { "widgets.stocks.size": "medium" } },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".stocks-error")).toBeNull();
    expect(
      container.querySelectorAll(".stocks-grid .stock-ticker").length
    ).toBe(4);
  });

  it("does not render the error box when error is false", () => {
    const state = {
      Stocks: { tickers: [], lastUpdated: null, error: false },
      Prefs: { values: { "widgets.stocks.size": "medium" } },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".stocks-error")).toBeNull();
  });

  it("clears the error box and shows ticker cards once data arrives", () => {
    const { container, rerender } = render(
      <WrapWithProvider
        state={{
          Stocks: { tickers: [], lastUpdated: null, error: true },
          Prefs: { values: { "widgets.stocks.size": "medium" } },
        }}
      >
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".stocks-error")).toBeTruthy();

    rerender(
      <WrapWithProvider
        state={{
          Stocks: { tickers: SAMPLE, lastUpdated: 1, error: true },
          Prefs: { values: { "widgets.stocks.size": "medium" } },
        }}
      >
        <Stocks
          dispatch={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );

    expect(container.querySelector(".stocks-error")).toBeNull();
    expect(
      container.querySelectorAll(".stocks-grid .stock-ticker").length
    ).toBe(4);
  });
});

describe("Stocks error telemetry across a size change", () => {
  let observerCallbacks;

  beforeEach(() => {
    observerCallbacks = [];
    jest.spyOn(global, "IntersectionObserver").mockImplementation(cb => {
      observerCallbacks.push(cb);
      return {
        observe: jest.fn(),
        unobserve: jest.fn(),
        disconnect: jest.fn(),
      };
    });
  });

  afterEach(() => {
    jest.restoreAllMocks();
  });

  // Trigger every observer that has been created so far. The error and
  // impression observers both fire; only WIDGETS_ERROR is counted below.
  function fireAllIntersections() {
    act(() => {
      observerCallbacks.forEach(cb =>
        cb([{ isIntersecting: true, target: {} }])
      );
    });
  }

  it("reports WIDGETS_ERROR once even if the size changes while the error box is shown", () => {
    const dispatch = jest.fn();
    const store = createStore(combineReducers(reducers), {
      ...mockState,
      Stocks: { tickers: [], lastUpdated: null, error: true },
      Prefs: {
        ...mockState.Prefs,
        values: { ...mockState.Prefs.values, "widgets.stocks.size": "medium" },
      },
    });
    render(
      <Provider store={store}>
        <Stocks
          dispatch={dispatch}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </Provider>
    );

    // Error box visible at medium: WIDGETS_ERROR fires once.
    fireAllIntersections();

    // Switching size must not rebuild the error box, or it reports the error to telemetry twice.
    ["small", "large"].forEach(value => {
      act(() => {
        store.dispatch({
          type: at.PREF_CHANGED,
          data: { name: "widgets.stocks.size", value },
        });
      });
      fireAllIntersections();
    });

    const errorCalls = dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_ERROR
    );
    expect(errorCalls).toHaveLength(1);
  });

  it("reports WIDGETS_ERROR once across a feed-error to chosen-unavailable transition", () => {
    const dispatch = jest.fn();
    const store = createStore(combineReducers(reducers), {
      ...mockState,
      Stocks: { tickers: [], lastUpdated: null, error: true },
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.stocks.size": "small",
          "widgets.stocks.watchlist": "VOO",
        },
      },
    });
    render(
      <Provider store={store}>
        <Stocks
          dispatch={dispatch}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </Provider>
    );
    fireAllIntersections();
    act(() => {
      store.dispatch({
        type: at.WIDGETS_STOCKS_UPDATE,
        data: {
          tickers: [
            {
              ticker: "QQQ",
              name: "Invesco QQQ Trust",
              last_price: "$480.00 USD",
              todays_change_perc: "+2.1",
            },
          ],
          lastUpdated: 1,
          error: false,
        },
      });
    });
    fireAllIntersections();
    const errorCalls = dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_ERROR
    );
    expect(errorCalls).toHaveLength(1);
  });

  it("reports WIDGETS_ERROR once even when switching lists during an error", () => {
    const dispatch = jest.fn();
    const { container } = renderStocksState({
      size: "large",
      tickers: [],
      error: true,
      watchlist: "VOO",
      watchlistReconciledSymbols: [], // pending, so the dropdown stays available
      dispatch,
    });
    fireAllIntersections();
    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="markets"]'
      )
    );
    fireAllIntersections();
    const errorCalls = dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_ERROR
    );
    expect(errorCalls).toHaveLength(1);
  });

  it("records the error with the current size after a resize", () => {
    const dispatch = jest.fn();
    const store = createStore(combineReducers(reducers), {
      ...mockState,
      Stocks: { tickers: [], lastUpdated: null, error: true },
      Prefs: {
        ...mockState.Prefs,
        values: { ...mockState.Prefs.values, "widgets.stocks.size": "medium" },
      },
    });
    render(
      <Provider store={store}>
        <Stocks
          dispatch={dispatch}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </Provider>
    );

    // Change to large before the error box is ever seen, so the first
    // intersection reports the error at the new size.
    act(() => {
      store.dispatch({
        type: at.PREF_CHANGED,
        data: { name: "widgets.stocks.size", value: "large" },
      });
    });
    fireAllIntersections();

    const errorCalls = dispatch.mock.calls.filter(
      ([action]) => action.type === at.WIDGETS_ERROR
    );
    expect(errorCalls).toHaveLength(1);
    expect(errorCalls[0][0].data).toMatchObject({
      widget_name: "stocks",
      error_type: "load_error",
      widget_size: "large",
    });
    expect(errorCalls[0][0].meta).toEqual(
      expect.objectContaining({ to: "ActivityStream:Main" })
    );
  });
});

describe("Stocks impression telemetry", () => {
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

  it("fires WIDGETS_IMPRESSION once when the widget is seen", () => {
    const dispatch = jest.fn();
    renderStocks(dispatch);
    const [observer] = observerInstances;
    const [target] = observer.observed;

    observer.callback([{ isIntersecting: true, target }], observer);
    observer.callback([{ isIntersecting: true, target }], observer);

    const impressions = dispatch.mock.calls.filter(
      ([action]) => action?.type === at.WIDGETS_IMPRESSION
    );
    expect(impressions).toHaveLength(1);
    expect(impressions[0][0].data).toMatchObject({
      widget_name: "stocks",
      widget_size: "medium",
    });
    expect(impressions[0][0].meta).toEqual(
      expect.objectContaining({ to: "ActivityStream:Main" })
    );
  });
});

describe("Stocks new badge", () => {
  const SAMPLE_TICKER = [
    {
      ticker: "SPY",
      name: "SPDR S&P 500 ETF Trust",
      last_price: "$559.44 USD",
      todays_change_perc: "+0.2",
    },
  ];

  function renderStocksBadge({
    tickers = SAMPLE_TICKER,
    hasInteracted = false,
  } = {}) {
    const state = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.stocks.interaction": hasInteracted,
        },
      },
      Stocks: { tickers, lastUpdated: 1, error: false },
    };
    return render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          handleUserInteraction={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
  }

  it("shows the New badge when tickers are present and the user has not interacted", () => {
    const { container } = renderStocksBadge();
    const badge = container.querySelector(".stocks-new-badge");
    expect(badge).toBeTruthy();
    expect(badge.getAttribute("data-l10n-id")).toBe(
      "newtab-widget-lists-label-new"
    );
  });

  it("hides the New badge once the interaction pref is set", () => {
    const { container } = renderStocksBadge({ hasInteracted: true });
    expect(container.querySelector(".stocks-new-badge")).toBeNull();
  });

  it("hides the New badge while there are no tickers (loading)", () => {
    const { container } = renderStocksBadge({ tickers: [] });
    expect(container.querySelector(".stocks-new-badge")).toBeNull();
  });

  it("hides the New badge in the error state", () => {
    const state = {
      Stocks: { tickers: [], lastUpdated: null, error: true },
      Prefs: { values: { "widgets.stocks.size": "medium" } },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          handleUserInteraction={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".stocks-new-badge")).toBeNull();
  });

  it("shows the New badge with stale tickers even when error is set", () => {
    const state = {
      ...mockState,
      Stocks: { tickers: SAMPLE_TICKER, lastUpdated: 1, error: true },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={jest.fn()}
          handleUserInteraction={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    // The badge follows ticker presence, not the error flag.
    expect(container.querySelector(".stocks-new-badge")).toBeTruthy();
  });
});

describe("Stocks list dropdown", () => {
  const SAMPLE = [
    {
      ticker: "SPY",
      name: "SPDR S&P 500 ETF Trust",
      last_price: "$559.44 USD",
      todays_change_perc: "+0.2",
    },
  ];

  function renderWithTickers({
    tickers = SAMPLE,
    error = false,
    watchlist = "SPY",
  } = {}) {
    const state = {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.stocks.size": "large",
          "widgets.stocks.watchlist": watchlist,
        },
      },
      Stocks: { tickers, lastUpdated: 1, error },
    };
    const dispatch = jest.fn();
    const handleUserInteraction = jest.fn();
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={dispatch}
          handleUserInteraction={handleUserInteraction}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    return { container, dispatch, handleUserInteraction };
  }

  function listItem(container, list) {
    return container.querySelector(
      `#stocks-list-menu panel-item[data-list="${list}"]`
    );
  }

  function buttonL10n(container) {
    return container
      .querySelector(".stocks-list-button")
      .getAttribute("data-l10n-id");
  }

  it("defaults to Watchlist and shows the saved rows", () => {
    const { container } = renderWithTickers();
    expect(buttonL10n(container)).toBe("newtab-stocks-list-watchlist");
    expect(listItem(container, "watchlist").hasAttribute("checked")).toBe(true);
    expect(listItem(container, "markets").hasAttribute("checked")).toBe(false);
    expect(
      container.querySelectorAll(".stocks-list .stock-ticker").length
    ).toBe(1);
  });

  it("switches to Markets, updating the button and checked item", () => {
    const { container } = renderWithTickers();
    fireEvent.click(listItem(container, "markets"));
    expect(buttonL10n(container)).toBe("newtab-stocks-list-markets");
    expect(listItem(container, "markets").hasAttribute("checked")).toBe(true);
    expect(listItem(container, "watchlist").hasAttribute("checked")).toBe(
      false
    );
    expect(
      container.querySelectorAll(".stocks-list .stock-ticker").length
    ).toBe(1);
  });

  it("shows loading rows on the Watchlist while the feed loads, and the error box on failure", () => {
    const loading = renderWithTickers({ tickers: [] });
    expect(
      loading.container.querySelector(".stocks-list--loading")
    ).toBeTruthy();

    const errored = renderWithTickers({ tickers: [], error: true });
    expect(
      errored.container.querySelector(
        '[data-l10n-id="newtab-stocks-error-not-available"]'
      )
    ).toBeTruthy();
  });

  it("records exactly one change_list event and flips the interaction pref on a switch", () => {
    const { container, dispatch, handleUserInteraction } = renderWithTickers();
    fireEvent.click(listItem(container, "markets"));
    const evts = dispatch.mock.calls.filter(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "change_list"
    );
    expect(evts).toHaveLength(1);
    expect(evts[0][0].data).toMatchObject({
      widget_name: "stocks",
      widget_source: "widget",
      user_action: "change_list",
      action_value: "markets",
    });
    expect(handleUserInteraction).toHaveBeenCalledWith("stocks");
  });

  it("records a change_list event on each genuine switch", () => {
    const { container, dispatch } = renderWithTickers();
    fireEvent.click(listItem(container, "markets"));
    fireEvent.click(listItem(container, "watchlist"));
    const evts = dispatch.mock.calls.filter(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "change_list"
    );
    expect(evts).toHaveLength(2);
    expect(evts.map(e => e[0].data.action_value)).toEqual([
      "markets",
      "watchlist",
    ]);
  });

  it("does nothing when the current list is reselected", () => {
    const { container, dispatch, handleUserInteraction } = renderWithTickers();
    fireEvent.click(listItem(container, "watchlist"));
    const evt = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "change_list"
    );
    expect(evt).toBeUndefined();
    expect(handleUserInteraction).not.toHaveBeenCalled();
  });
});

describe("Stocks watchlist add/remove", () => {
  const SAMPLE = [
    {
      ticker: "VOO",
      name: "Vanguard S&P 500 ETF",
      last_price: "$559.44 USD",
      todays_change_perc: "-0.11",
    },
    {
      ticker: "QQQ",
      name: "Invesco QQQ Trust",
      last_price: "$480.00 USD",
      todays_change_perc: "+2.1",
    },
  ];

  const firstAddButton = container =>
    container.querySelector(
      '.stocks-list moz-button.stock-ticker-action[data-l10n-id="newtab-stocks-add-to-watchlist"]'
    );

  it("empty watchlist at large: plain heading, no dropdown, Markets rows show add", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "",
    });
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(
      container
        .querySelector("#stocks-widget-label")
        .classList.contains("sr-only")
    ).toBe(false);
    expect(firstAddButton(container)).toBeTruthy();
  });

  it("adding a ticker writes the pref, reveals the dropdown, stays on Markets, marks the row added", () => {
    const dispatch = jest.fn();
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "",
      dispatch,
    });
    fireEvent.click(firstAddButton(container));
    const setPref = dispatch.mock.calls.find(
      ([a]) =>
        a.type === at.SET_PREF && a.data?.name === "widgets.stocks.watchlist"
    );
    expect(setPref[0].data.value).toBe("VOO");
    expect(container.querySelector(".stocks-list-button")).toBeTruthy();
    expect(
      container
        .querySelector(".stocks-list-button")
        .getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-list-markets");
    expect(
      container.querySelector(".stocks-list .stock-ticker-in-watchlist")
    ).toBeTruthy();
  });

  it("records add_ticker (Markets) and remove_ticker (Watchlist), source row, no value, flips interaction", () => {
    const dispatch = jest.fn();
    const handleUserInteraction = jest.fn();
    const store = createStore(combineReducers(reducers), {
      ...mockState,
      Prefs: {
        ...mockState.Prefs,
        values: {
          ...mockState.Prefs.values,
          "widgets.stocks.size": "large",
          "widgets.stocks.watchlist": "",
        },
      },
      Stocks: { tickers: SAMPLE, lastUpdated: 1, error: false },
    });
    const { container } = render(
      <Provider store={store}>
        <Stocks
          dispatch={dispatch}
          handleUserInteraction={handleUserInteraction}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </Provider>
    );
    fireEvent.click(firstAddButton(container));
    const addEvents = dispatch.mock.calls.filter(
      ([a]) =>
        a.type === at.WIDGETS_USER_EVENT && a.data?.user_action === "add_ticker"
    );
    expect(addEvents).toHaveLength(1);
    expect(addEvents[0][0].data).toEqual({
      widget_name: "stocks",
      widget_size: "large",
      widget_source: "row",
      user_action: "add_ticker",
    });
    expect(handleUserInteraction).toHaveBeenCalledWith("stocks");

    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="watchlist"]'
      )
    );
    fireEvent.click(
      container.querySelector(".stocks-list moz-button.stock-ticker-action")
    );
    const removeEvents = dispatch.mock.calls.filter(
      ([a]) =>
        a.type === at.WIDGETS_USER_EVENT &&
        a.data?.user_action === "remove_ticker"
    );
    expect(removeEvents).toHaveLength(1);
    expect(removeEvents[0][0].data).toEqual({
      widget_name: "stocks",
      widget_size: "large",
      widget_source: "row",
      user_action: "remove_ticker",
    });
  });

  it("announces add (Markets) and remove (Watchlist) in the live region", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "",
    });
    fireEvent.click(firstAddButton(container));
    let live = container.querySelector(".stocks-sr-status");
    expect(live.getAttribute("role")).toBe("status");
    expect(live.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-added-to-watchlist"
    );
    expect(JSON.parse(live.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
    });
    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="watchlist"]'
      )
    );
    fireEvent.click(
      container.querySelector(".stocks-list moz-button.stock-ticker-action")
    );
    live = container.querySelector(".stocks-sr-status");
    expect(live.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-removed-from-watchlist"
    );
    expect(JSON.parse(live.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
    });
  });

  it("watchlist tab lists saved tickers in saved order with remove controls", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "QQQ,VOO",
    });
    const symbols = [
      ...container.querySelectorAll(".stocks-list .stock-ticker-symbol"),
    ].map(n => n.textContent);
    expect(symbols).toEqual(["QQQ", "VOO"]);
    const removes = container.querySelectorAll(
      ".stocks-list moz-button.stock-ticker-action"
    );
    expect(removes).toHaveLength(2);
    expect(removes[0].getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-remove-from-watchlist"
    );
  });

  it("removing the last ticker reverts to the plain heading", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO",
    });
    fireEvent.click(
      container.querySelector(".stocks-list moz-button.stock-ticker-action")
    );
    expect(container.querySelector(".stocks-list-button")).toBeNull();
  });

  it("after emptying the watchlist, a fresh add starts on Markets (not the Watchlist tab)", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO",
    });
    fireEvent.click(
      container.querySelector(".stocks-list moz-button.stock-ticker-action")
    );
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    fireEvent.click(firstAddButton(container));
    expect(
      container
        .querySelector(".stocks-list-button")
        .getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-list-markets");
  });

  it("reconciliation ignores a stale ack, accepts the latest, then adopts an external change", () => {
    const { container, store } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "",
    });
    fireEvent.click(firstAddButton(container));
    // VOO is now "added", so the first Markets add button is QQQ.
    fireEvent.click(firstAddButton(container));
    fireEvent.click(
      container.querySelector(
        '#stocks-list-menu panel-item[data-list="watchlist"]'
      )
    );
    const symbols = () =>
      [...container.querySelectorAll(".stocks-list .stock-ticker-symbol")].map(
        n => n.textContent
      );
    broadcastPref(store, "widgets.stocks.watchlist", "VOO"); // out-of-date, ignored
    expect(symbols()).toEqual(["VOO", "QQQ"]);
    broadcastPref(store, "widgets.stocks.watchlist", "VOO,QQQ"); // our write, acknowledged
    expect(symbols()).toEqual(["VOO", "QQQ"]);
    broadcastPref(store, "widgets.stocks.watchlist", "QQQ"); // external change adopted
    expect(symbols()).toEqual(["QQQ"]);
  });

  it("adopts a non-canonical external pref without an update loop", () => {
    const { container, store } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO",
    });
    broadcastPref(store, "widgets.stocks.watchlist", " voo , qqq ,,VOO");
    const symbols = [
      ...container.querySelectorAll(".stocks-list .stock-ticker-symbol"),
    ].map(n => n.textContent);
    expect(symbols).toEqual(["VOO", "QQQ"]);
  });

  it("matches a saved symbol against a lower-case feed ticker", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: [
        {
          ticker: "voo",
          name: "Vanguard S&P 500 ETF",
          last_price: "$559.44 USD",
          todays_change_perc: "-0.11",
        },
      ],
      watchlist: "VOO",
    });
    const symbols = [
      ...container.querySelectorAll(".stocks-list .stock-ticker-symbol"),
    ].map(n => n.textContent);
    expect(symbols).toEqual(["voo"]);
    expect(container.querySelector(".stocks-list-button")).toBeTruthy();
  });

  it("hides a saved symbol the feed no longer returns without rewriting the pref", () => {
    const dispatch = jest.fn();
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO,GONE",
      dispatch,
    });
    const symbols = [
      ...container.querySelectorAll(".stocks-list .stock-ticker-symbol"),
    ].map(n => n.textContent);
    expect(symbols).toEqual(["VOO"]);
    expect(container.querySelector(".stocks-list-button")).toBeTruthy();
    const wroteWatchlist = dispatch.mock.calls.some(
      ([a]) =>
        a.type === at.SET_PREF && a.data?.name === "widgets.stocks.watchlist"
    );
    expect(wroteWatchlist).toBe(false);
  });

  it("reverts to the plain heading when no saved symbol is in the feed", () => {
    const dispatch = jest.fn();
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "GONE,ALSOGONE",
      dispatch,
    });
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(
      container
        .querySelector("#stocks-widget-label")
        .classList.contains("sr-only")
    ).toBe(false);
    const wroteWatchlist = dispatch.mock.calls.some(
      ([a]) =>
        a.type === at.SET_PREF && a.data?.name === "widgets.stocks.watchlist"
    );
    expect(wroteWatchlist).toBe(false);
  });

  it("keeps the dropdown while the feed is still loading", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: [],
      watchlist: "VOO",
      watchlistReconciledSymbols: [], // still loading: not reconciled yet
    });
    expect(container.querySelector(".stocks-list-button")).toBeTruthy();
  });

  const spyOnFocus = focusedEls =>
    jest.spyOn(HTMLElement.prototype, "focus").mockImplementation(function () {
      focusedEls.push(this);
    });
  const focusedSymbol = el =>
    el?.closest(".stock-ticker")?.querySelector(".stock-ticker-symbol")
      ?.textContent;

  it("after removing a non-last row, focus moves to the connected remaining row", () => {
    const focusedEls = [];
    const focusSpy = spyOnFocus(focusedEls);
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO,QQQ",
    });
    const removes = container.querySelectorAll(
      ".stocks-list moz-button.stock-ticker-action"
    );
    focusedEls.length = 0;
    fireEvent.click(removes[0]);
    const focused = focusedEls[focusedEls.length - 1];
    expect(focused?.classList.contains("stock-ticker-action")).toBe(true);
    expect(focused.isConnected).toBe(true);
    expect(focusedSymbol(focused)).toBe("QQQ");
    focusSpy.mockRestore();
  });

  it("after removing the last-position row, focus clamps to the previous row", () => {
    const focusedEls = [];
    const focusSpy = spyOnFocus(focusedEls);
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO,QQQ",
    });
    const removes = container.querySelectorAll(
      ".stocks-list moz-button.stock-ticker-action"
    );
    focusedEls.length = 0;
    fireEvent.click(removes[1]);
    expect(focusedSymbol(focusedEls[focusedEls.length - 1])).toBe("VOO");
    focusSpy.mockRestore();
  });

  it("after removing the sole row, focus moves to the widget menu button", () => {
    const focusedEls = [];
    const focusSpy = spyOnFocus(focusedEls);
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "VOO",
    });
    focusedEls.length = 0;
    fireEvent.click(
      container.querySelector(".stocks-list moz-button.stock-ticker-action")
    );
    const focused = focusedEls[focusedEls.length - 1];
    expect(focused?.classList.contains("stocks-context-menu-button")).toBe(
      true
    );
    focusSpy.mockRestore();
  });

  it("adding a ticker moves focus to the next add button", () => {
    const focusedEls = [];
    const focusSpy = spyOnFocus(focusedEls);
    const { container } = renderStocksState({
      size: "large",
      tickers: SAMPLE,
      watchlist: "",
    });
    focusedEls.length = 0;
    fireEvent.click(firstAddButton(container));
    const movedToAddButton = focusedEls.some(el =>
      el.classList?.contains("stock-ticker-action")
    );
    expect(movedToAddButton).toBe(true);
    focusSpy.mockRestore();
  });
});

describe("Stocks small size", () => {
  const SAMPLE = [
    {
      ticker: "VOO",
      name: "Vanguard S&P 500 ETF",
      last_price: "$559.44 USD",
      todays_change_perc: "-0.11",
    },
    {
      ticker: "QQQ",
      name: "Invesco QQQ Trust",
      last_price: "$480.00 USD",
      todays_change_perc: "+2.1",
    },
  ];
  const smallSymbol = container =>
    container.querySelector(".stocks-small-symbol")?.textContent;
  // Check the rendered row without relying on locale-dependent number formatting.
  const srStatus = container => {
    const sr = container.querySelector(".stocks-list--small .stock-ticker-sr");
    return {
      id: sr?.getAttribute("data-l10n-id"),
      name: JSON.parse(sr?.getAttribute("data-l10n-args") ?? "{}").name,
    };
  };

  it("empty watchlist, loaded feed: shows the first feed row in valid ul/li structure", () => {
    const { container } = renderStocksState({ size: "small", tickers: SAMPLE });
    expect(
      container.querySelector("ul.stocks-list--small > li.stock-ticker--small")
    ).toBeTruthy();
    expect(smallSymbol(container)).toBe("VOO");
    expect(srStatus(container)).toEqual({
      id: "newtab-stocks-ticker-status-down",
      name: "Vanguard S&P 500 ETF",
    });
  });

  it("watchlist present: shows savedSymbols[0], even when a later saved symbol is also present", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "QQQ,VOO",
    });
    expect(smallSymbol(container)).toBe("QQQ");
    expect(srStatus(container)).toEqual({
      id: "newtab-stocks-ticker-status-up",
      name: "Invesco QQQ Trust",
    });
  });

  it("matches a saved symbol against a lower-case feed ticker", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [
        {
          ticker: "voo",
          name: "Vanguard S&P 500 ETF",
          last_price: "$559.44 USD",
          todays_change_perc: "-0.11",
        },
      ],
      watchlist: "VOO",
    });
    expect(container.querySelectorAll(".stock-ticker--small")).toHaveLength(1);
    expect(srStatus(container).name).toBe("Vanguard S&P 500 ETF");
    expect(smallSymbol(container)).toBe("voo");
  });

  it("errors when the first saved symbol is absent, even if a later saved symbol is in the feed", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "GONE,QQQ",
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(container.querySelector(".stock-ticker--small")).toBeNull();
  });

  it("not loaded with a watchlist: hidden loading placeholder, saved symbol in the header", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      watchlist: "VOO",
      watchlistReconciledSymbols: [], // not loaded yet: pending, not reconciled
      lastUpdated: null,
    });
    const li = container.querySelector(
      "ul.stocks-list--small.stocks-list--loading > li.stock-ticker--small"
    );
    expect(li).toBeTruthy();
    expect(li.getAttribute("aria-hidden")).toBe("true");
    expect(container.querySelector(".stock-ticker-sr")).toBeNull();
    expect(smallSymbol(container)).toBe("VOO");
  });

  it("not loaded, empty watchlist: loading placeholder with the Stocks heading and no symbol", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      lastUpdated: null,
    });
    expect(
      container.querySelector(".stocks-list--small.stocks-list--loading")
    ).toBeTruthy();
    expect(container.querySelector(".stocks-small-symbol")).toBeNull();
    expect(
      container
        .querySelector("#stocks-widget-label")
        .classList.contains("sr-only")
    ).toBe(false);
  });

  it("loaded-empty snapshot: error, not indefinite loading", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      lastUpdated: 1,
      error: false,
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(container.querySelector(".stocks-list--small")).toBeNull();
  });

  it("feed error with no rows: error box", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      lastUpdated: null,
      error: true,
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
  });

  it("chosen saved symbol absent from a loaded feed: unavailable error", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "GONE",
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(container.querySelector(".stock-ticker--small")).toBeNull();
  });

  it("stale refresh (error + rows), chosen resolves: shows the chosen row", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "VOO",
      error: true,
    });
    expect(container.querySelector(".stock-ticker--small")).toBeTruthy();
    expect(container.querySelector(".stocks-error")).toBeNull();
    expect(smallSymbol(container)).toBe("VOO");
  });

  it("stale refresh (error + rows), empty watchlist: shows the first stale row", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      error: true,
    });
    expect(container.querySelector(".stock-ticker--small")).toBeTruthy();
    expect(container.querySelector(".stocks-error")).toBeNull();
    expect(smallSymbol(container)).toBe("VOO");
  });

  it("stale refresh (error + rows), chosen saved symbol absent: unavailable error", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "GONE",
      error: true,
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
    expect(container.querySelector(".stock-ticker--small")).toBeNull();
  });

  it("region stays labelled Stocks and the symbol is readable text (not aria-hidden)", () => {
    const { container } = renderStocksState({ size: "small", tickers: SAMPLE });
    const article = container.querySelector("article.stocks");
    expect(article.getAttribute("aria-labelledby")).toBe("stocks-widget-label");
    const symbol = container.querySelector(".stocks-small-symbol");
    expect(symbol.closest("[aria-hidden='true']")).toBeNull();
  });

  it("shows no New badge, dropdown, or watchlist control at small", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: SAMPLE,
      watchlist: "VOO",
    });
    expect(container.querySelector(".stocks-new-badge")).toBeNull();
    expect(container.querySelector(".stocks-list-button")).toBeNull();
    expect(
      container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
  });
});

describe("Stocks reducer - watchlist data", () => {
  const { Stocks } = reducers;
  it("initializes watchlist fields", () => {
    expect(INITIAL_STATE.Stocks.watchlistTickers).toEqual([]);
    expect(INITIAL_STATE.Stocks.watchlistReconciledSymbols).toEqual([]);
  });
  it("stores a watchlist update without touching default fields", () => {
    const prev = { ...INITIAL_STATE.Stocks, tickers: [{ ticker: "SPY" }] };
    const next = Stocks(prev, {
      type: at.WIDGETS_STOCKS_WATCHLIST_UPDATE,
      data: {
        watchlistTickers: [{ ticker: "AAPL" }],
        reconciledSymbols: ["AAPL"],
      },
    });
    expect(next.watchlistTickers).toEqual([{ ticker: "AAPL" }]);
    expect(next.watchlistReconciledSymbols).toEqual(["AAPL"]);
    expect(next.tickers).toEqual([{ ticker: "SPY" }]);
  });
});

describe("Stocks reducer - search", () => {
  const { Stocks } = reducers;
  it("initializes search fields", () => {
    expect(INITIAL_STATE.Stocks.searchResults).toEqual([]);
    expect(INITIAL_STATE.Stocks.searchStatus).toBe("idle");
    expect(INITIAL_STATE.Stocks.activeRequestId).toBeNull();
    expect(INITIAL_STATE.Stocks.submittedQuery).toBe("");
  });
  it("marks a started search loading and records the request", () => {
    const next = Stocks(INITIAL_STATE.Stocks, {
      type: at.WIDGETS_STOCKS_SEARCH_STARTED,
      data: { requestId: "r1", query: "AAPL" },
    });
    expect(next.searchStatus).toBe("loading");
    expect(next.searchResults).toEqual([]);
    expect(next.activeRequestId).toBe("r1");
    expect(next.submittedQuery).toBe("AAPL");
  });
  it("applies a response whose requestId matches the active one", () => {
    const prev = { ...INITIAL_STATE.Stocks, activeRequestId: "r1" };
    const next = Stocks(prev, {
      type: at.WIDGETS_STOCKS_SEARCH_RESPONSE,
      data: {
        requestId: "r1",
        status: "success",
        values: [{ ticker: "AAPL" }],
      },
    });
    expect(next.searchStatus).toBe("success");
    expect(next.searchResults).toEqual([{ ticker: "AAPL" }]);
  });
  it("drops a response whose requestId no longer matches", () => {
    const prev = {
      ...INITIAL_STATE.Stocks,
      activeRequestId: "r2",
      searchStatus: "loading",
    };
    const next = Stocks(prev, {
      type: at.WIDGETS_STOCKS_SEARCH_RESPONSE,
      data: {
        requestId: "r1",
        status: "success",
        values: [{ ticker: "AAPL" }],
      },
    });
    expect(next).toBe(prev);
  });
  it("clears search back to idle", () => {
    const prev = {
      ...INITIAL_STATE.Stocks,
      searchStatus: "success",
      searchResults: [{ ticker: "AAPL" }],
      activeRequestId: "r1",
      submittedQuery: "AAPL",
    };
    const next = Stocks(prev, { type: at.WIDGETS_STOCKS_SEARCH_CLEAR });
    expect(next.searchStatus).toBe("idle");
    expect(next.searchResults).toEqual([]);
    expect(next.activeRequestId).toBeNull();
    expect(next.submittedQuery).toBe("");
  });
  it("leaves default and watchlist fields untouched", () => {
    const prev = {
      ...INITIAL_STATE.Stocks,
      tickers: [{ ticker: "SPY" }],
      watchlistTickers: [{ ticker: "AAPL" }],
      watchlistReconciledSymbols: ["AAPL"],
    };
    const next = Stocks(prev, {
      type: at.WIDGETS_STOCKS_SEARCH_STARTED,
      data: { requestId: "r1", query: "MSFT" },
    });
    expect(next.tickers).toEqual([{ ticker: "SPY" }]);
    expect(next.watchlistTickers).toEqual([{ ticker: "AAPL" }]);
    expect(next.watchlistReconciledSymbols).toEqual(["AAPL"]);
  });
  it("does not reduce WIDGETS_STOCKS_SEARCH_REQUEST, keeping search out of the main store", () => {
    const prev = INITIAL_STATE.Stocks;
    const next = Stocks(prev, {
      type: at.WIDGETS_STOCKS_SEARCH_REQUEST,
      data: { query: "AAPL", requestId: "r1" },
    });
    expect(next).toBe(prev);
  });
});

describe("Stocks watchlist data rendering", () => {
  const AAPL = {
    ticker: "AAPL",
    name: "Apple Inc",
    last_price: "$1.00 USD",
    todays_change_perc: "+0.10",
  };

  it("renders a saved watchlist symbol even when the default feed is empty", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: [],
      watchlist: "AAPL",
      watchlistTickers: [AAPL],
      watchlistReconciledSymbols: ["AAPL"],
    });
    expect(container.textContent).toContain("AAPL");
    // Resolved, so not dimmed as loading.
    expect(container.querySelector(".stocks-list--loading")).toBeNull();
  });

  it("keeps the dropdown and shows placeholders while a newly added symbol is pending", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: WATCHLIST_SAMPLE,
      watchlist: "AAPL",
      watchlistTickers: [],
      watchlistReconciledSymbols: [], // not reconciled for AAPL yet
    });
    expect(container.querySelector(".stocks-list-button")).toBeTruthy();
    const list = container.querySelector("ul.stocks-list--watchlist");
    expect(list.className).toContain("stocks-list--loading");
    expect(list.querySelectorAll(".stock-ticker--large").length).toBe(1);
  });

  it("shows a resolved row plus a placeholder while another saved symbol is pending", () => {
    const { container } = renderStocksState({
      size: "large",
      tickers: [],
      watchlist: "AAPL,MSFT",
      watchlistTickers: [AAPL],
      watchlistReconciledSymbols: ["AAPL"], // AAPL resolved; MSFT still pending
    });
    const ul = container.querySelector("ul.stocks-list");
    expect(ul.getAttribute("aria-busy")).toBe("true");
    // One resolved AAPL row plus one placeholder for the pending MSFT.
    expect(ul.querySelectorAll(".stock-ticker--large").length).toBe(2);
    expect(ul.textContent).toContain("AAPL");
  });

  it("small size shows a placeholder (not an error) while a saved symbol loads", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      watchlist: "AAPL",
      watchlistTickers: [],
      watchlistReconciledSymbols: [],
    });
    expect(container.querySelector(".stocks-error")).toBeNull();
  });

  it("small size keeps a resolved watchlist row when the default feed errored", () => {
    const { container } = renderStocksState({
      size: "small",
      tickers: [],
      error: true,
      watchlist: "AAPL",
      watchlistTickers: [AAPL],
      watchlistReconciledSymbols: ["AAPL"],
    });
    expect(container.textContent).toContain("AAPL");
    expect(container.querySelector(".stocks-error")).toBeNull();
  });

  it("medium size still shows the default error, unaffected by a saved symbol", () => {
    const { container } = renderStocksState({
      size: "medium",
      tickers: [],
      error: true,
      watchlist: "AAPL",
      watchlistTickers: [],
      watchlistReconciledSymbols: [],
    });
    expect(container.querySelector(".stocks-error")).toBeTruthy();
  });
});

describe("Stocks watchlist limit and scroll", () => {
  it("disables the Markets add buttons when the watchlist is full", () => {
    // With no matching watchlist rows, the large widget shows SPY in Markets.
    const saved = Array.from({ length: 10 }, (_, i) => `S${i}`).join(",");
    const { container } = renderStocksState({
      size: "large",
      watchlist: saved,
      tickers: [
        {
          ticker: "SPY",
          name: "SPDR S&P 500 ETF Trust",
          last_price: "$559.44 USD",
          todays_change_perc: "+0.2",
        },
      ],
    });
    const addBtn = container.querySelector("moz-button.stock-ticker-action");
    expect(addBtn.getAttribute("iconSrc")).toContain("plus.svg");
    expect(addBtn.getAttribute("disabled")).not.toBeNull();
  });

  it("adds the scroll modifier to the Watchlist list", () => {
    const { container } = renderStocksState({
      size: "large",
      watchlist: "AAPL",
      watchlistTickers: [
        {
          ticker: "AAPL",
          name: "Apple Inc",
          last_price: "$1 USD",
          todays_change_perc: "+0.1",
        },
      ],
    });
    expect(container.querySelector("ul.stocks-list--watchlist")).toBeTruthy();
  });
});

describe("Stocks ticker search", () => {
  const AAPL = {
    ticker: "AAPL",
    name: "Apple Inc",
    last_price: "$1 USD",
    todays_change_perc: "+0.1",
  };

  function openSearch(container) {
    fireEvent.click(
      container.querySelector(
        '[data-l10n-id="newtab-stocks-menu-search-stocks"]'
      )
    );
  }

  const actionsOfType = (dispatch, type) =>
    dispatch.mock.calls.map(c => c[0]).filter(a => a.type === type);

  afterEach(() => {
    jest.restoreAllMocks();
  });

  it("opens the panel from the menu, expands to large, and keeps the region label", () => {
    const { container } = renderStocks();
    expect(container.querySelector(".stocks-search")).toBeNull();
    openSearch(container);
    expect(container.querySelector(".stocks-search")).toBeTruthy();
    expect(container.querySelector("moz-input-search")).toBeTruthy();
    expect(container.querySelector("article.stocks").className).toContain(
      "large-widget"
    );
    expect(container.querySelector(".stocks-body")).toBeNull();
    expect(container.querySelector("#stocks-widget-label")).toBeTruthy();
  });

  it("closes from the back button, clears search, and reverts the size", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    openSearch(container);
    // Opening also clears; count only what closing dispatches.
    dispatch.mockClear();
    fireEvent.click(container.querySelector("moz-button.stocks-search-back"));
    expect(container.querySelector(".stocks-search")).toBeNull();
    expect(container.querySelector("article.stocks").className).toContain(
      "medium-widget"
    );
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_CLEAR)
    ).toHaveLength(1);
  });

  it("returns focus to the widget menu button when search closes", () => {
    const focusSpy = jest
      .spyOn(HTMLElement.prototype, "focus")
      .mockImplementation(() => {});
    const { container } = renderStocks();
    openSearch(container);
    focusSpy.mockClear();
    fireEvent.click(container.querySelector("moz-button.stocks-search-back"));
    const focused = focusSpy.mock.contexts.at(-1);
    expect(focused?.classList.contains("stocks-context-menu-button")).toBe(
      true
    );
  });

  it("submits: a content-only started action plus a request to main, with telemetry", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    openSearch(container);
    const input = container.querySelector("moz-input-search");
    input.value = "AAPL";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    const [started] = actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED);
    const [request] = actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_REQUEST);
    expect(started.data.query).toBe("AAPL");
    expect(started.meta).toBeUndefined();
    expect(request.data.query).toBe("AAPL");
    expect(request.data.requestId).toBe(started.data.requestId);
    expect(request.meta).toBeTruthy();
    const [evt] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT).filter(
      a => a.data?.user_action === "search_submit"
    );
    expect(evt.data.widget_source).toBe("search");
  });

  it("does not submit an empty query", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    openSearch(container);
    const input = container.querySelector("moz-input-search");
    input.value = "   ";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED)
    ).toHaveLength(0);
  });

  it("uses a fresh requestId on each submit so the latest wins", () => {
    const dispatch = jest.fn();
    const { container } = renderStocks(dispatch);
    openSearch(container);
    const input = container.querySelector("moz-input-search");
    input.value = "AAPL";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    input.value = "MSFT";
    fireEvent.input(input);
    fireEvent.keyDown(input, { key: "Enter" });
    const ids = actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_STARTED).map(
      a => a.data.requestId
    );
    expect(ids).toHaveLength(2);
    expect(ids[0]).not.toBe(ids[1]);
  });

  it("adds a result: writes the pref, switches to Watchlist, keeps the original size, and closes", () => {
    const dispatch = jest.fn();
    const state = {
      ...mockState,
      Stocks: {
        ...INITIAL_STATE.Stocks,
        searchStatus: "success",
        searchResults: [AAPL],
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <Stocks
          dispatch={dispatch}
          handleUserInteraction={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );
    openSearch(container);
    // Opening also clears; count only what adding then closing dispatches.
    dispatch.mockClear();
    fireEvent.click(
      container.querySelector(
        ".stock-ticker--result moz-button.stock-ticker-action"
      )
    );
    const setPref = name =>
      actionsOfType(dispatch, at.SET_PREF).find(a => a.data?.name === name);
    expect(setPref("widgets.stocks.watchlist").data.value).toBe("AAPL");
    // Adding no longer pins the size to large; closing search restores it.
    expect(setPref("widgets.stocks.size")).toBeUndefined();
    const [added] = actionsOfType(dispatch, at.WIDGETS_USER_EVENT).filter(
      a => a.data?.user_action === "add_ticker"
    );
    expect(added.data.widget_source).toBe("search");
    expect(
      actionsOfType(dispatch, at.WIDGETS_STOCKS_SEARCH_CLEAR)
    ).toHaveLength(1);
    expect(container.querySelector(".stocks-search")).toBeNull();
    expect(container.querySelector("article.stocks").className).toContain(
      "medium-widget"
    );
  });
});
