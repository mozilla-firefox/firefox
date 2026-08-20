/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { useState } from "react";
import {
  StockTicker,
  getDirection,
  formatPrice,
  formatChange,
} from "content-src/components/Widgets/Stocks/StockTicker";

describe("getDirection", () => {
  it("returns up/down/flat from the change sign", () => {
    expect(getDirection("+2.1")).toBe("up");
    expect(getDirection("-0.11")).toBe("down");
    expect(getDirection("0.00")).toBe("flat");
    expect(getDirection(undefined)).toBe("flat");
  });
});

describe("formatPrice", () => {
  it("formats the parsed price as US currency for an en-US locale", () => {
    expect(formatPrice("$559.44 USD", "en-US")).toBe("$559.44");
  });

  it("keeps thousands separators", () => {
    expect(formatPrice("$1,234.56 USD", "en-US")).toBe("$1,234.56");
  });

  it("uses the locale's decimal separator and symbol placement", () => {
    // French uses a comma decimal separator and places the symbol after the amount.
    const fr = formatPrice("$559.44 USD", "fr-FR");
    expect(fr).toContain("559,44");
    expect(fr).not.toContain("559.44");
  });

  it("returns the raw string when there is no number to parse", () => {
    expect(formatPrice("", "en-US")).toBe("");
    expect(formatPrice(undefined, "en-US")).toBe("");
  });
});

describe("formatChange", () => {
  it("formats the change as a signed percent for an en-US locale", () => {
    expect(formatChange("+0.2", "en-US")).toBe("+0.2%");
    expect(formatChange("-0.21", "en-US")).toBe("-0.21%");
  });

  it("shows no sign for a flat change and keeps the source precision", () => {
    expect(formatChange("0.00", "en-US")).toBe("0.00%");
  });

  it("formats an integer change with no decimal places", () => {
    expect(formatChange("+1", "en-US")).toBe("+1%");
  });

  it("uses the locale's decimal separator", () => {
    const fr = formatChange("+0.2", "fr-FR");
    expect(fr).toContain("0,2");
    expect(fr).not.toContain("0.2");
  });

  it("returns the raw string when there is no number to parse", () => {
    expect(formatChange("", "en-US")).toBe("");
  });
});

describe("StockTicker", () => {
  function renderCard(props = {}) {
    const { container } = render(
      <StockTicker
        name="SPDR S&P 500 ETF Trust"
        ticker="SPY"
        price="$559.44 USD"
        changePercent="+2.1"
        {...props}
      />
    );
    return container;
  }

  it("renders ticker, formatted price and formatted change as visual (aria-hidden) text", () => {
    const container = renderCard();
    expect(container.querySelector(".stock-ticker-symbol").textContent).toBe(
      "SPY"
    );
    expect(container.querySelector(".stock-ticker-price").textContent).toBe(
      "$559.44"
    );
    expect(container.querySelector(".stock-ticker-change").textContent).toBe(
      "+2.1%"
    );
  });

  it("applies the direction modifier to the indicator and change", () => {
    const up = renderCard();
    expect(up.querySelector(".stock-indicator--up")).toBeTruthy();
    expect(up.querySelector(".stock-ticker-change--up")).toBeTruthy();

    const down = render(
      <StockTicker name="n" ticker="IWM" price="$1 USD" changePercent="-0.21" />
    ).container;
    expect(down.querySelector(".stock-indicator--down")).toBeTruthy();
    expect(down.querySelector(".stock-ticker-change--down")).toBeTruthy();
  });

  it("falls through to the base color when the change is flat", () => {
    const flat = render(
      <StockTicker name="n" ticker="IWM" price="$1 USD" changePercent="0.00" />
    ).container;
    expect(flat.querySelector(".stock-indicator--flat")).toBeTruthy();
    expect(flat.querySelector(".stock-ticker-change--up")).toBeNull();
    expect(flat.querySelector(".stock-ticker-change--down")).toBeNull();
  });

  it("sets the flat arrow inline and mirrors it for right-to-left locales", () => {
    const ltr = render(
      <StockTicker name="n" ticker="DIA" price="$1 USD" changePercent="0.00" />
    ).container;
    expect(
      ltr.querySelector(".stock-indicator--flat").style.backgroundImage
    ).toContain("shaft-arrow-right.svg");

    document.dir = "rtl";
    try {
      const rtl = render(
        <StockTicker
          name="n"
          ticker="DIA"
          price="$1 USD"
          changePercent="0.00"
        />
      ).container;
      expect(
        rtl.querySelector(".stock-indicator--flat").style.backgroundImage
      ).toContain("shaft-arrow-left.svg");
    } finally {
      document.dir = "";
    }
  });

  it("marks the visual rows aria-hidden", () => {
    const container = renderCard();
    expect(
      container.querySelector(".stock-indicator").getAttribute("aria-hidden")
    ).toBe("true");
    expect(
      container.querySelector(".stock-ticker-label").getAttribute("aria-hidden")
    ).toBe("true");
  });

  it("exposes a localized screen-reader label with the formatted change and price", () => {
    const container = renderCard();
    const sr = container.querySelector(".stock-ticker-sr");
    expect(sr.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-ticker-status-up"
    );
    const args = JSON.parse(sr.getAttribute("data-l10n-args"));
    expect(args).toEqual({
      name: "SPDR S&P 500 ETF Trust",
      change: "+2.1%",
      price: "$559.44",
    });
  });

  it("uses the direction-specific screen-reader message id", () => {
    const down = render(
      <StockTicker name="n" ticker="IWM" price="$1 USD" changePercent="-0.21" />
    ).container;
    expect(
      down.querySelector(".stock-ticker-sr").getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-ticker-status-down");
    const flat = render(
      <StockTicker name="n" ticker="DIA" price="$1 USD" changePercent="0.00" />
    ).container;
    expect(
      flat.querySelector(".stock-ticker-sr").getAttribute("data-l10n-id")
    ).toBe("newtab-stocks-ticker-status-flat");
  });

  it("renders as an aria-hidden placeholder with no screen-reader label while loading", () => {
    const { container } = render(<StockTicker loading={true} />);
    expect(container.querySelector(".stock-ticker-sr")).toBeNull();
    expect(
      container.querySelector("li.stock-ticker").getAttribute("aria-hidden")
    ).toBe("true");
  });

  it("falls back to the ticker symbol as the screen-reader subject when name is missing, and is not aria-hidden", () => {
    const { container } = render(
      <StockTicker
        name=""
        ticker="SPY"
        price="$559.44 USD"
        changePercent="+2.1"
      />
    );
    const sr = container.querySelector(".stock-ticker-sr");
    expect(sr.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-ticker-status-up"
    );
    const args = JSON.parse(sr.getAttribute("data-l10n-args"));
    expect(args.name).toBe("SPY");
    expect(
      container.querySelector("li.stock-ticker").getAttribute("aria-hidden")
    ).toBeNull();
  });

  it("uses the medium layout with no name by default", () => {
    const container = renderCard();
    expect(container.querySelector(".stock-ticker--medium")).toBeTruthy();
    expect(container.querySelector(".stock-ticker-name")).toBeNull();
  });

  it("renders the large layout: full name plus ticker, then change and price", () => {
    const { container } = render(
      <StockTicker
        size="large"
        name="SPDR S&P 500 ETF Trust"
        ticker="SPY"
        price="$559.44 USD"
        changePercent="+2.1"
      />
    );
    expect(container.querySelector(".stock-ticker--large")).toBeTruthy();
    expect(container.querySelector(".stock-ticker-name").textContent).toBe(
      "SPDR S&P 500 ETF Trust"
    );
    expect(container.querySelector(".stock-ticker-symbol").textContent).toBe(
      "SPY"
    );
    expect(container.querySelector(".stock-ticker-price").textContent).toBe(
      "$559.44"
    );
    const args = JSON.parse(
      container.querySelector(".stock-ticker-sr").getAttribute("data-l10n-args")
    );
    expect(args).toEqual({
      name: "SPDR S&P 500 ETF Trust",
      change: "+2.1%",
      price: "$559.44",
    });
  });
});

describe("StockTicker watchlist control (large only)", () => {
  const base = {
    size: "large",
    name: "Vanguard S&P 500 ETF",
    ticker: "VOO",
    price: "$559.44 USD",
    changePercent: "-0.11",
  };
  const renderRow = props =>
    render(
      <ul>
        <StockTicker {...base} {...props} />
      </ul>
    );

  it("add: shows '+' with the add label, fires the toggle, not aria-hidden", () => {
    const onToggle = jest.fn();
    const { container } = renderRow({
      watchlistState: "add",
      onWatchlistToggle: onToggle,
    });
    const btn = container.querySelector("moz-button.stock-ticker-action");
    expect(btn.getAttribute("iconSrc")).toContain("plus.svg");
    expect(btn.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-add-to-watchlist"
    );
    expect(JSON.parse(btn.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
    });
    expect(btn.closest("[aria-hidden='true']")).toBeNull();
    fireEvent.click(btn);
    expect(onToggle).toHaveBeenCalledWith("VOO", "Vanguard S&P 500 ETF");
  });

  it("remove: shows '-' with the remove label and fires the toggle", () => {
    const onToggle = jest.fn();
    const { container } = renderRow({
      watchlistState: "remove",
      onWatchlistToggle: onToggle,
    });
    const btn = container.querySelector("moz-button.stock-ticker-action");
    expect(btn.getAttribute("iconSrc")).toContain("minus.svg");
    expect(btn.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-remove-from-watchlist"
    );
    expect(JSON.parse(btn.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
    });
    fireEvent.click(btn);
    expect(onToggle).toHaveBeenCalledWith("VOO", "Vanguard S&P 500 ETF");
  });

  it("added: a persistent decorative checkmark + visually-hidden in-watchlist text, no button", () => {
    const onToggle = jest.fn();
    const { container } = renderRow({
      watchlistState: "added",
      onWatchlistToggle: onToggle,
    });
    expect(
      container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
    expect(
      container.querySelector(".stock-ticker-added[aria-hidden='true']")
    ).toBeTruthy();
    const sr = container.querySelector(".stock-ticker-in-watchlist");
    expect(sr.getAttribute("data-l10n-id")).toBe("newtab-stocks-in-watchlist");
    expect(JSON.parse(sr.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
    });
  });

  it("added renders even without a handler (it is just an indicator)", () => {
    const { container } = renderRow({ watchlistState: "added" });
    expect(container.querySelector(".stock-ticker-added")).toBeTruthy();
  });

  it("renders no actionable control for add/remove without a handler", () => {
    const add = renderRow({ watchlistState: "add" });
    expect(
      add.container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
    const remove = renderRow({ watchlistState: "remove" });
    expect(
      remove.container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
  });

  it("renders no control without watchlistState", () => {
    const { container } = renderRow({});
    expect(
      container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
    expect(container.querySelector(".stock-ticker-added")).toBeNull();
  });

  it("renders no control in medium", () => {
    const { container } = renderRow({
      size: "medium",
      watchlistState: "add",
      onWatchlistToggle: jest.fn(),
    });
    expect(
      container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
  });

  it("after adding, marks the checkmark as confirming for a moment, then clears it", () => {
    jest.useFakeTimers();
    function ControlledRow() {
      const [state, setState] = useState("add");
      return (
        <ul>
          <StockTicker
            {...base}
            watchlistState={state}
            onWatchlistToggle={() => setState("added")}
          />
        </ul>
      );
    }
    const { container } = render(<ControlledRow />);
    fireEvent.click(container.querySelector("moz-button.stock-ticker-action"));
    expect(
      container.querySelector(".stock-ticker-added--confirming")
    ).toBeTruthy();
    act(() => {
      jest.advanceTimersByTime(1999);
    });
    expect(
      container.querySelector(".stock-ticker-added--confirming")
    ).toBeTruthy();
    act(() => {
      jest.advanceTimersByTime(1);
    });
    expect(container.querySelector(".stock-ticker-added")).toBeTruthy();
    expect(
      container.querySelector(".stock-ticker-added--confirming")
    ).toBeNull();
    jest.useRealTimers();
  });

  it("disables the add button when disabled is set", () => {
    const { container } = renderRow({
      watchlistState: "add",
      onWatchlistToggle: jest.fn(),
      disabled: true,
    });
    expect(
      container
        .querySelector("moz-button.stock-ticker-action")
        .getAttribute("disabled")
    ).not.toBeNull();
  });

  it("marks a search-variant row with the result class", () => {
    const { container } = renderRow({ variant: "search" });
    expect(container.querySelector("li.stock-ticker--result")).toBeTruthy();
  });
});

describe("StockTicker small size", () => {
  const base = {
    size: "small",
    name: "Vanguard S&P 500 ETF",
    ticker: "VOO",
    price: "$559.44 USD",
    changePercent: "-0.11",
  };
  const renderSmall = props =>
    render(
      <ul>
        <StockTicker {...base} {...props} />
      </ul>
    );

  it("renders the indicator, change and price, and no name/symbol line", () => {
    const { container } = renderSmall();
    expect(container.querySelector(".stock-indicator")).toBeTruthy();
    expect(container.querySelector(".stock-ticker-change").textContent).toBe(
      "-0.11%"
    );
    expect(container.querySelector(".stock-ticker-price").textContent).toBe(
      "$559.44"
    );
    expect(container.querySelector(".stock-ticker-name")).toBeNull();
    expect(container.querySelector(".stock-ticker-symbol")).toBeNull();
    expect(container.querySelector(".stock-ticker-dot")).toBeNull();
  });

  it("keeps the screen-reader status text (name / change / price, no symbol)", () => {
    const { container } = renderSmall();
    const sr = container.querySelector(".stock-ticker-sr");
    expect(sr.getAttribute("data-l10n-id")).toBe(
      "newtab-stocks-ticker-status-down"
    );
    expect(JSON.parse(sr.getAttribute("data-l10n-args"))).toEqual({
      name: "Vanguard S&P 500 ETF",
      change: "-0.11%",
      price: "$559.44",
    });
  });

  it("renders no add/remove button and no added indicator at small", () => {
    const add = renderSmall({
      watchlistState: "add",
      onWatchlistToggle: jest.fn(),
    });
    expect(
      add.container.querySelector("moz-button.stock-ticker-action")
    ).toBeNull();
    // "added" would show the indicator at large; small must suppress it too.
    const added = renderSmall({ watchlistState: "added" });
    expect(added.container.querySelector(".stock-ticker-added")).toBeNull();
    expect(
      added.container.querySelector(".stock-ticker-in-watchlist")
    ).toBeNull();
  });

  it("renders a hidden placeholder when loading", () => {
    const { container } = renderSmall({ loading: true });
    const li = container.querySelector(".stock-ticker--small");
    expect(li.getAttribute("aria-hidden")).toBe("true");
    expect(container.querySelector(".stock-ticker-sr")).toBeNull();
  });
});
