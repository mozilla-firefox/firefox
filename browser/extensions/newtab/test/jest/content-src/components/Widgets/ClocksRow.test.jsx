/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { render } from "@testing-library/react";
import { ClocksRow } from "content-src/components/Widgets/Clocks/ClocksRow";

function renderRow(overrides = {}) {
  const props = {
    clock: {
      timeZone: "Europe/Berlin",
      label: "Home",
      labelColor: "cyan",
    },
    locale: "en-US",
    now: new Date("2026-04-20T13:44:00Z"),
    shouldAbbreviate: false,
    showLabel: false,
    use12HourFormat: false,
    ...overrides,
  };
  return render(
    <ul>
      <ClocksRow {...props} />
    </ul>
  );
}

describe("<ClocksRow>", () => {
  it("renders the full city name when shouldAbbreviate is false", () => {
    const { container } = renderRow({
      clock: { timeZone: "America/New_York", label: null, labelColor: null },
      shouldAbbreviate: false,
    });
    expect(container.querySelector(".clocks-city").textContent).toBe(
      "New York"
    );
  });

  it("renders the saved city when one is provided", () => {
    const { container } = renderRow({
      clock: {
        timeZone: "America/New_York",
        city: "Boston",
        label: null,
        labelColor: null,
      },
      shouldAbbreviate: false,
    });
    expect(container.querySelector(".clocks-city").textContent).toBe("Boston");
    expect(
      container.querySelector(".clocks-row").getAttribute("aria-label")
    ).toMatch(/^Boston, /);
  });

  it("renders the IATA abbreviation when shouldAbbreviate is true", () => {
    const { container } = renderRow({
      clock: { timeZone: "America/New_York", label: null, labelColor: null },
      shouldAbbreviate: true,
    });
    expect(container.querySelector(".clocks-city").textContent).toBe("NYC");
  });

  it("renders the label chip only when showLabel is true AND clock.label is set", () => {
    const { container: withChip } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
      showLabel: true,
    });
    expect(withChip.querySelector(".clocks-label-chip")).toBeInTheDocument();

    const { container: noLabel } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: null, labelColor: null },
      showLabel: true,
    });
    expect(noLabel.querySelector(".clocks-label-chip")).not.toBeInTheDocument();

    const { container: labelOff } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
      showLabel: false,
    });
    expect(
      labelOff.querySelector(".clocks-label-chip")
    ).not.toBeInTheDocument();
  });

  it("applies the clocks-chip-<palette> modifier class for the chip colour", () => {
    const { container } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: "Home", labelColor: "violet" },
      showLabel: true,
    });
    const chip = container.querySelector(".clocks-label-chip");
    expect(chip.classList.contains("clocks-chip-violet")).toBe(true);
  });

  it("ignores unknown labelColor values (no injected classes)", () => {
    // Allow-list guard: a malformed labelColor should fall back to neutral,
    // not smuggle extra tokens into the DOM.
    const { container } = renderRow({
      clock: {
        timeZone: "Europe/Berlin",
        label: "Home",
        labelColor: "violet extra-class",
      },
      showLabel: true,
    });
    const chip = container.querySelector(".clocks-label-chip");
    expect(Array.from(chip.classList)).toEqual([
      "clocks-label-chip",
      "clocks-chip-neutral",
    ]);
  });

  it("keeps native listitem semantics and sets an aria-label with city, TZ, and time", () => {
    // Row must stay an <li> with no explicit role so screen readers retain
    // list counting from the parent <ul>. The aria-label is only the
    // accessible name override.
    const { container } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: null, labelColor: null },
      now: new Date("2026-04-20T13:44:00Z"),
    });
    const li = container.querySelector(".clocks-row");
    expect(li.tagName).toBe("LI");
    expect(li.hasAttribute("role")).toBe(false);
    expect(li.getAttribute("aria-label")).toMatch(/^Berlin, /);
  });

  it("includes the visible label in the aria-label", () => {
    const { container } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
      showLabel: true,
    });
    const li = container.querySelector(".clocks-row");
    expect(li.getAttribute("aria-label")).toMatch(/^Home, Berlin, /);
  });

  it("includes the label in the aria-label even when the chip is hidden", () => {
    // Screen-reader users still need the label to disambiguate two clocks
    // for the same zone (e.g. NY "Office" vs NY "Family") on sizes where
    // the chip isn't rendered.
    const { container } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: "Home", labelColor: "cyan" },
      showLabel: false,
    });
    const li = container.querySelector(".clocks-row");
    expect(li.getAttribute("aria-label")).toMatch(/^Home, Berlin, /);
    expect(container.querySelector(".clocks-label-chip")).toBeNull();
  });

  it("renders an empty time when now is null (pre-tick)", () => {
    const { container } = renderRow({
      clock: { timeZone: "Europe/Berlin", label: null, labelColor: null },
      now: null,
    });
    expect(container.querySelector(".clocks-time").textContent).toBe("");
  });

  it("sets the time datetime attribute in the clock time zone", () => {
    const { container } = renderRow({
      clock: { timeZone: "Asia/Tokyo", label: null, labelColor: null },
      now: new Date("2026-04-20T13:44:00Z"),
    });
    expect(container.querySelector(".clocks-time").dateTime).toBe(
      "2026-04-20T22:44"
    );
  });

  it("has no tabIndex when showInlineActions is false", () => {
    const { container } = renderRow({ showInlineActions: false });
    expect(
      container.querySelector(".clocks-row").hasAttribute("tabindex")
    ).toBe(false);
  });

  it("has tabIndex=0 when showInlineActions is true so keyboard focus reveals the actions", () => {
    const { container } = renderRow({
      showInlineActions: true,
      onEdit: () => {},
    });
    expect(
      container.querySelector(".clocks-row").getAttribute("tabindex")
    ).toBe("0");
  });
});
