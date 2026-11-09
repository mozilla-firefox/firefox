/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render, waitFor } from "@testing-library/react";
import { AddClockForm } from "content-src/components/Widgets/Clocks/AddClockForm";
import {
  CLOCK_CITIES,
  clockCityFluentId,
} from "content-src/components/Widgets/Clocks/ClockCityRegistry.mjs";

const SUPPORTED_TIME_ZONES = [
  "Europe/Berlin",
  "Australia/Sydney",
  "America/New_York",
  "America/Los_Angeles",
  "Asia/Tokyo",
];

// Renders without flushing useCuratedCityNames, for tests that assert what
// shows before the localized names resolve.
const renderFormSync = (overrides = {}) => {
  const props = {
    isEditing: false,
    initialClock: null,
    canAddClock: true,
    supportedTimeZones: SUPPORTED_TIME_ZONES,
    onSave: jest.fn(),
    onCancel: jest.fn(),
    ...overrides,
  };
  const result = render(<AddClockForm {...props} />);
  return { ...result, props };
};

async function renderForm(overrides = {}) {
  const rendered = renderFormSync(overrides);
  // Flush the useCuratedCityNames effect so the localized city names are in
  // place before the test queries the DOM.
  await act(async () => {});
  return rendered;
}

function setSearchValue(container, value) {
  const input = container.querySelector(".clocks-search-location-input");
  Object.defineProperty(input, "value", {
    configurable: true,
    writable: true,
    value,
  });
  fireEvent.input(input);
  return input;
}

// Per-zone Intl.DateTimeFormat mock for render tests that need
// different localized names across multiple time zones in one run.
// The inner stub must stay a `function` expression — arrow functions
// can't be invoked with `new`, which production code does.
const installMockedIntl = nameByTimeZone => {
  const Original = Intl.DateTimeFormat;
  Intl.DateTimeFormat = function (locale, options = {}) {
    const tz = options.timeZone;
    const value = tz && nameByTimeZone[tz];
    return {
      format: () => "",
      formatToParts: () => (value ? [{ type: "timeZoneName", value }] : []),
      resolvedOptions: () => ({ timeZone: tz }),
    };
  };
  return () => {
    Intl.DateTimeFormat = Original;
  };
};

// document.l10n.formatValues mock resolving the given { fluentId: name } map.
const l10nResolving = names =>
  jest.fn(async ids => ids.map(({ id }) => names[id] ?? null));

// jsdom has no document.l10n; about:newtab always does. Resolve every curated
// city's Fluent id to its en-US name, as the browser would. Tests that need a
// different locale's spelling override formatValues with l10nResolving.
const CITY_NAME_BY_FLUENT_ID = new Map(
  CLOCK_CITIES.map(city => [clockCityFluentId(city.id), city.fallbackName])
);
const mockDocumentL10n = () => {
  document.l10n = {
    formatValues: jest.fn(async ids =>
      ids.map(({ id }) => CITY_NAME_BY_FLUENT_ID.get(id) ?? null)
    ),
  };
};

describe("<AddClockForm>", () => {
  beforeEach(mockDocumentL10n);
  afterEach(() => {
    delete document.l10n;
  });

  describe("rendering", () => {
    it("renders an empty form when no initialClock is supplied", async () => {
      const { container } = await renderForm();
      expect(container.querySelector(".clocks-add-form")).toBeInTheDocument();
      expect(
        container
          .querySelector(".clocks-search-location-input")
          .getAttribute("value")
      ).toBe("");
      expect(
        container.querySelector(".clocks-nickname-input").getAttribute("value")
      ).toBe("");
    });

    it("pre-fills inputs from initialClock when editing", async () => {
      const { container } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
      });
      expect(
        container
          .querySelector(".clocks-search-location-input")
          .getAttribute("value")
      ).toBe("Boston");
      expect(
        container.querySelector(".clocks-nickname-input").getAttribute("value")
      ).toBe("Office");
    });

    it("falls back to deriving the city from the timezone when initialClock has no city", async () => {
      const { container } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "America/Los_Angeles",
          label: null,
          labelColor: null,
        },
      });
      expect(
        container
          .querySelector(".clocks-search-location-input")
          .getAttribute("value")
      ).toBe("Los Angeles");
    });

    it("uses the add-clock l10n id in add mode", async () => {
      const { container } = await renderForm();
      expect(
        container.querySelector(".clocks-add-form").getAttribute("data-l10n-id")
      ).toBe("newtab-clock-widget-add-clock-form");
      expect(
        container
          .querySelector(".clocks-form-submit")
          .getAttribute("data-l10n-id")
      ).toBe("newtab-clock-widget-button-add-clock");
    });

    it("uses the edit-clock l10n id in edit mode", async () => {
      const { container } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "Europe/Berlin",
          label: null,
          labelColor: null,
        },
      });
      expect(
        container.querySelector(".clocks-add-form").getAttribute("data-l10n-id")
      ).toBe("newtab-clock-widget-edit-clock-form");
      expect(
        container
          .querySelector(".clocks-form-submit")
          .getAttribute("data-l10n-id")
      ).toBe("newtab-clock-widget-button-save");
    });
  });

  describe("search dropdown", () => {
    it("shows matching results as the user types", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Ber");
      const results = container.querySelectorAll(".clocks-search-result");
      expect(results.length).toBeGreaterThan(0);
      expect(
        results[0].querySelector(".clocks-search-result-city").textContent
      ).toBe("Berlin");
    });

    it("renders and searches by the localized zone name built from Intl", async () => {
      const restore = installMockedIntl({
        "Europe/Berlin": "Central European Time",
        "Australia/Sydney": "Eastern Australia Time",
        "America/New_York": "Eastern Time",
        "America/Los_Angeles": "Pacific Time",
        "Asia/Tokyo": "Japan Time",
      });
      try {
        const { container } = await renderForm();
        setSearchValue(container, "Tok");
        const result = container.querySelector(".clocks-search-result");
        expect(
          result.querySelector(".clocks-search-result-timezone").textContent
        ).toBe("Japan Time");
        setSearchValue(container, "Pacific");
        const cities = Array.from(
          container.querySelectorAll(".clocks-search-result-city")
        ).map(el => el.textContent);
        expect(cities).toContain("Los Angeles");
      } finally {
        restore();
      }
    });

    it("renders results as div role='option' (not buttons) per ARIA listbox pattern", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Berlin");
      const result = container.querySelector(".clocks-search-result");
      expect(result.tagName).toBe("DIV");
      expect(result.getAttribute("role")).toBe("option");
      expect(result.getAttribute("tabIndex")).toBe("-1");
    });

    it("moves through results with the arrow keys", async () => {
      const { container } = await renderForm();
      const input = setSearchValue(container, "San");
      const results = container.querySelectorAll(
        "#clocks-search-results [role='option']"
      );
      expect(results.length).toBeGreaterThan(1);

      fireEvent.keyDown(input, { key: "ArrowDown" });
      expect(document.activeElement).toBe(results[0]);
      fireEvent.keyDown(results[0], { key: "ArrowDown" });
      expect(document.activeElement).toBe(results[1]);
      fireEvent.keyDown(results[1], { key: "ArrowUp" });
      expect(document.activeElement).toBe(results[0]);
    });

    it("closes the results when focus leaves the field and its list", async () => {
      const { container } = await renderForm();
      const input = setSearchValue(container, "San");
      const firstResult = container.querySelector(".clocks-search-result");

      fireEvent.focusOut(input, { relatedTarget: firstResult });
      expect(
        container.querySelector("#clocks-search-results")
      ).toBeInTheDocument();

      fireEvent.focusOut(firstResult, {
        relatedTarget: container.querySelector(".clocks-nickname-input"),
      });
      expect(
        container.querySelector("#clocks-search-results")
      ).not.toBeInTheDocument();

      setSearchValue(container, "Sant");
      expect(
        container.querySelector("#clocks-search-results")
      ).toBeInTheDocument();
    });

    it("wraps from the last result to the first without scrolling the page", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "San");
      const results = container.querySelectorAll(
        "#clocks-search-results [role='option']"
      );
      const lastResult = results[results.length - 1];
      lastResult.focus();
      // fireEvent returns false when the handler prevented the default.
      expect(fireEvent.keyDown(lastResult, { key: "ArrowDown" })).toBe(false);
      expect(document.activeElement).toBe(results[0]);
    });

    it("keeps the results list open on a full city name and closes it only after a row is clicked", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Berlin");
      expect(
        container.querySelector("#clocks-search-results")
      ).toBeInTheDocument();
      const result = container.querySelector(".clocks-search-result");
      expect(result).toBeInTheDocument();
      fireEvent.click(result);
      expect(
        container.querySelector("#clocks-search-results")
      ).not.toBeInTheDocument();
    });

    it("selects a timezone when a result is clicked, replacing the search value with the city", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tok");
      fireEvent.click(container.querySelector(".clocks-search-result"));
      expect(
        container.querySelector(".clocks-search-location-input").value
      ).toBe("Tokyo");
    });

    it("selects a result via Enter on the option", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tok");
      const result = container.querySelector(".clocks-search-result");
      fireEvent.keyDown(result, { key: "Enter" });
      expect(
        container.querySelector(".clocks-search-location-input").value
      ).toBe("Tokyo");
    });

    it("never sets aria-activedescendant (no active-descendant navigation)", async () => {
      const { container } = await renderForm();
      const input = container.querySelector(".clocks-search-location-input");
      setSearchValue(container, "Ber");
      expect(input.hasAttribute("aria-activedescendant")).toBe(false);
      fireEvent.click(container.querySelector(".clocks-search-result"));
      expect(input.hasAttribute("aria-activedescendant")).toBe(false);
    });

    it("shows the add-custom option inside the listbox when the query matches nothing", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "zzz");
      const listbox = container.querySelector("#clocks-search-results");
      expect(listbox).toBeInTheDocument();
      expect(listbox.getAttribute("role")).toBe("listbox");
      const addCustom = listbox.querySelector(".clocks-add-custom");
      expect(addCustom).toBeInTheDocument();
      expect(addCustom.getAttribute("role")).toBe("option");
      expect(addCustom.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-add-custom"
      );
      expect(JSON.parse(addCustom.getAttribute("data-l10n-args"))).toEqual({
        city: "zzz",
      });
    });

    it("hides the add-custom option when the query matches a city", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Berlin");
      expect(
        container.querySelector(".clocks-search-result")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".clocks-add-custom")
      ).not.toBeInTheDocument();
    });

    it("shows the add-custom option alongside partial matches when no match is exact", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Berl");
      // "Berl" prefix-matches Berlin but is not an exact city name.
      expect(
        container.querySelector(".clocks-search-result")
      ).toBeInTheDocument();
      const addCustom = container.querySelector(".clocks-add-custom");
      expect(addCustom).toBeInTheDocument();
      expect(JSON.parse(addCustom.getAttribute("data-l10n-args"))).toEqual({
        city: "Berl",
      });
    });
  });

  describe("submit", () => {
    it("does nothing when no timezone is resolved", async () => {
      const { container, props } = await renderForm();
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      expect(props.onSave).not.toHaveBeenCalled();
    });

    it("calls onSave with a built zone when a city is matched and submit is clicked", async () => {
      const { container, props } = await renderForm();
      setSearchValue(container, "Berlin");
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      expect(props.onSave).toHaveBeenCalledTimes(1);
      expect(props.onSave).toHaveBeenCalledWith({
        timeZone: "Europe/Berlin",
        city: "Berlin",
        cityId: "de-berlin",
        label: null,
        labelColor: null,
      });
    });

    it("trims the nickname and assigns a random labelColor when one is added", async () => {
      const randomStub = jest.spyOn(Math, "random").mockReturnValue(0);
      try {
        const { container, props } = await renderForm();
        setSearchValue(container, "Berlin");
        const nicknameInput = container.querySelector(".clocks-nickname-input");
        Object.defineProperty(nicknameInput, "value", {
          configurable: true,
          writable: true,
          value: "  Office  ",
        });
        fireEvent.input(nicknameInput);
        fireEvent.click(container.querySelector(".clocks-form-submit"));
        const [[savedZone]] = props.onSave.mock.calls;
        expect(savedZone.label).toBe("Office");
        expect(savedZone.labelColor).toBe("cyan"); // First non-neutral palette entry.
      } finally {
        randomStub.mockRestore();
      }
    });

    it("trims an over-long label coming in via initialClock to MAX_NICKNAME_LENGTH", async () => {
      // Defensive cap for legacy/external pref values that may exceed the
      // limit the input enforces interactively.
      const { container, props } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: "Way too long nickname here",
          labelColor: "purple",
        },
      });
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      const [[savedZone]] = props.onSave.mock.calls;
      expect(savedZone.label).toBe("Way too lon"); // 11 chars
      expect(savedZone.label.length).toBe(11);
    });

    it("counts down the characters left as the nickname is typed", async () => {
      const { container } = await renderForm();
      const nicknameInput = container.querySelector(".clocks-nickname-input");
      const countLeft = () =>
        JSON.parse(nicknameInput.getAttribute("data-l10n-args")).count;
      expect(nicknameInput.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-input-nickname2"
      );
      expect(countLeft()).toBe(11);
      Object.defineProperty(nicknameInput, "value", {
        configurable: true,
        writable: true,
        value: "Home",
      });
      fireEvent.input(nicknameInput);
      expect(countLeft()).toBe(7);
    });

    it("shows a saved nickname over the limit cut to what will be saved", async () => {
      const { container } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: "Way too long nickname here",
          labelColor: "purple",
        },
      });
      const nicknameInput = container.querySelector(".clocks-nickname-input");
      expect(nicknameInput.getAttribute("value")).toBe("Way too lon");
      expect(
        JSON.parse(nicknameInput.getAttribute("data-l10n-args")).count
      ).toBe(0);
    });

    it("preserves the existing labelColor when editing the same zone", async () => {
      const { container, props } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: "Home",
          labelColor: "purple",
        },
      });
      // Submit unchanged — labelColor should be preserved.
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      const [[savedZone]] = props.onSave.mock.calls;
      expect(savedZone.labelColor).toBe("purple");
    });

    it("drops the stale cityId when the same zone resolves to a different city", async () => {
      const { container, props } = await renderForm({
        isEditing: true,
        initialClock: {
          timeZone: "America/Los_Angeles",
          city: "San Francisco",
          cityId: "us-san-francisco",
          label: null,
          labelColor: null,
        },
      });
      // Same zone via its IANA id resolves to the exemplar city (Los Angeles),
      // which has no cityId here, so the old cityId must not be inherited.
      setSearchValue(container, "America/Los_Angeles");
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      const [[savedZone]] = props.onSave.mock.calls;
      expect(savedZone.city).toBe("Los Angeles");
      expect("cityId" in savedZone).toBe(false);
    });

    it("adopts the current localized city name when editing a curated clock", async () => {
      document.l10n.formatValues = l10nResolving({
        "newtab-clock-city-de-munich": "München",
      });
      const { container, props } = renderFormSync({
        isEditing: true,
        initialClock: {
          timeZone: "Europe/Berlin",
          city: "Munich",
          cityId: "de-munich",
          label: null,
          labelColor: null,
        },
      });
      const input = container.querySelector(".clocks-search-location-input");
      expect(input.getAttribute("value")).toBe("Munich");
      // Once l10n resolves, the field adopts the current localized name so a
      // save no longer persists the stale locale spelling.
      await waitFor(() => expect(input.getAttribute("value")).toBe("München"));
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      const [[savedZone]] = props.onSave.mock.calls;
      expect(savedZone.city).toBe("München");
      expect(savedZone.cityId).toBe("de-munich");
    });

    it("disables the submit button until a valid timezone is resolved", async () => {
      const { container } = await renderForm();
      const submit = container.querySelector(".clocks-form-submit");
      expect(submit.hasAttribute("disabled")).toBe(true);
      setSearchValue(container, "Berlin");
      expect(submit.hasAttribute("disabled")).toBe(false);
    });

    it("submits via Enter on the form when not focused on a result or button", async () => {
      const { container, props } = await renderForm();
      setSearchValue(container, "Berlin");
      const nicknameInput = container.querySelector(".clocks-nickname-input");
      fireEvent.keyDown(nicknameInput, { key: "Enter" });
      expect(props.onSave).toHaveBeenCalled();
    });

    it("does not submit when Enter is pressed on the Cancel or Submit button", async () => {
      // Enter on a focused button should fire that button's own click via
      // native HTML semantics, not the form's onKeyDown=Enter submit path.
      const { container, props } = await renderForm();
      setSearchValue(container, "Berlin");
      const cancelButton = container.querySelector(
        "moz-button[data-l10n-id='newtab-clock-widget-button-cancel']"
      );
      fireEvent.keyDown(cancelButton, { key: "Enter" });
      const submitButton = container.querySelector(".clocks-form-submit");
      fireEvent.keyDown(submitButton, { key: "Enter" });
      expect(props.onSave).not.toHaveBeenCalled();
    });
  });

  describe("cancellation", () => {
    it("calls onCancel when the cancel button is clicked", async () => {
      const { container, props } = await renderForm();
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-button-cancel']"
        )
      );
      expect(props.onCancel).toHaveBeenCalled();
    });

    it("calls onCancel when the back button is clicked", async () => {
      const { container, props } = await renderForm();
      const backButton = container.querySelector(".clocks-search-back");
      expect(backButton).toHaveAttribute(
        "data-l10n-id",
        "newtab-clock-widget-search-back-button"
      );
      fireEvent.click(backButton);
      expect(props.onCancel).toHaveBeenCalled();
    });

    it("calls onCancel on Escape inside the form", async () => {
      const { container, props } = await renderForm();
      const input = container.querySelector(".clocks-search-location-input");
      fireEvent.keyDown(input, { key: "Escape" });
      expect(props.onCancel).toHaveBeenCalled();
    });

    it("does not call onCancel when focus moves outside the form", async () => {
      const { container, props } = await renderForm();
      const form = container.querySelector(".clocks-add-form");
      const outside = document.createElement("button");
      document.body.appendChild(outside);
      try {
        fireEvent.blur(form, { relatedTarget: outside });
        expect(props.onCancel).not.toHaveBeenCalled();
      } finally {
        outside.remove();
      }
    });
  });

  describe("custom-add flow", () => {
    const setValue = (el, value) => {
      Object.defineProperty(el, "value", {
        configurable: true,
        writable: true,
        value,
      });
      fireEvent.input(el);
    };

    it("enters custom mode with the typed name and a searchable zone picker", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      const cityInput = container.querySelector(".clocks-custom-city-input");
      expect(cityInput).toBeInTheDocument();
      expect(cityInput.getAttribute("value")).toBe("Tacoma");
      expect(
        container.querySelector(".clocks-custom-timezone-input")
      ).toBeInTheDocument();
      // The city search is replaced by the custom view.
      expect(
        container.querySelector(".clocks-search-location-input")
      ).not.toBeInTheDocument();
    });

    it("saves a custom clock bound to the zone picked in the searchable list", async () => {
      const { container, props } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      setValue(
        container.querySelector(".clocks-custom-timezone-input"),
        "Los Angeles"
      );
      fireEvent.click(
        container.querySelector(
          "#clocks-custom-zone-results .clocks-search-result"
        )
      );
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      expect(props.onSave).toHaveBeenCalledWith({
        timeZone: "America/Los_Angeles",
        city: "Tacoma",
        label: null,
        labelColor: null,
      });
    });

    it("shows a no-matches message when the zone search finds nothing", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      setValue(
        container.querySelector(".clocks-custom-timezone-input"),
        "zzzzzzzz"
      );
      const noResults = container.querySelector(".clocks-search-no-results");
      expect(noResults).toBeInTheDocument();
      expect(noResults.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-custom-zone-no-results"
      );
      expect(
        container.querySelectorAll(
          "#clocks-custom-zone-results [role='option']"
        ).length
      ).toBe(0);
    });

    it("returns to the city search when Back is clicked", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      fireEvent.click(
        container.querySelector(
          "moz-button[data-l10n-id='newtab-clock-widget-custom-back']"
        )
      );
      expect(
        container.querySelector(".clocks-search-location-input")
      ).toBeInTheDocument();
    });

    it("caps the custom city name at MAX_CITY_LENGTH on save", async () => {
      const { container, props } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      setValue(
        container.querySelector(".clocks-custom-timezone-input"),
        "Los Angeles"
      );
      fireEvent.click(
        container.querySelector(
          "#clocks-custom-zone-results .clocks-search-result"
        )
      );
      setValue(
        container.querySelector(".clocks-custom-city-input"),
        "x".repeat(50)
      );
      fireEvent.click(container.querySelector(".clocks-form-submit"));
      const [[savedZone]] = props.onSave.mock.calls;
      expect(savedZone.timeZone).toBe("America/Los_Angeles");
      expect(savedZone.city.length).toBe(32);
    });
  });

  // Protects the ARIA/DOM contract the screen reader relies on. jsdom cannot
  // verify what a screen reader actually speaks, so the announcement and
  // focus-order pass stays manual.
  describe("accessibility (screen reader contract)", () => {
    const setValue = (el, value) => {
      Object.defineProperty(el, "value", {
        configurable: true,
        writable: true,
        value,
      });
      fireEvent.input(el);
    };

    it("labels the location search and exposes results as a listbox", async () => {
      // moz-input-search can't wire up a combobox (aria stays on the host and
      // IDREFs can't cross its shadow boundary), so the contract is a labeled
      // search field plus a reachable light-DOM listbox that toggles on query.
      const { container } = await renderForm();
      const input = container.querySelector(".clocks-search-location-input");
      expect(input.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-search-location-input2"
      );
      expect(
        container.querySelector("#clocks-search-results")
      ).not.toBeInTheDocument();
      setSearchValue(container, "Ber");
      const listbox = container.querySelector("#clocks-search-results");
      expect(listbox.getAttribute("role")).toBe("listbox");
      expect(
        listbox.querySelectorAll('[role="option"]').length
      ).toBeGreaterThan(0);
    });

    it("exposes results and the add-custom row as listbox options", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Berl");
      const listbox = container.querySelector("#clocks-search-results");
      expect(listbox.getAttribute("role")).toBe("listbox");
      const options = listbox.querySelectorAll('[role="option"]');
      expect(options.length).toBeGreaterThan(0);
      expect(
        container.querySelector(".clocks-add-custom").getAttribute("role")
      ).toBe("option");
    });

    it("activates a result with Space as well as Enter", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tok");
      const result = container.querySelector(".clocks-search-result");
      fireEvent.keyDown(result, { key: " " });
      expect(
        container.querySelector(".clocks-search-location-input").value
      ).toBe("Tokyo");
    });

    it("keeps dir=auto on user-supplied text in the result rows", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Ber");
      expect(
        container
          .querySelector(".clocks-search-result-city")
          .getAttribute("dir")
      ).toBe("auto");
      expect(
        container
          .querySelector(".clocks-search-result-timezone")
          .getAttribute("dir")
      ).toBe("auto");
    });

    it("labels the custom city and time-zone fields", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      const cityInput = container.querySelector(".clocks-custom-city-input");
      const zoneInput = container.querySelector(
        ".clocks-custom-timezone-input"
      );
      expect(cityInput.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-custom-city-input"
      );
      expect(cityInput.getAttribute("dir")).toBe("auto");
      expect(zoneInput.getAttribute("data-l10n-id")).toBe(
        "newtab-clock-widget-custom-timezone-input"
      );
    });

    it("opens the zone listbox with option semantics once the query changes", async () => {
      const { container } = await renderForm();
      setSearchValue(container, "Tacoma");
      fireEvent.click(container.querySelector(".clocks-add-custom"));
      setValue(
        container.querySelector(".clocks-custom-timezone-input"),
        "Los Angeles"
      );
      const listbox = container.querySelector("#clocks-custom-zone-results");
      expect(listbox.getAttribute("role")).toBe("listbox");
      expect(
        listbox.querySelectorAll('[role="option"]').length
      ).toBeGreaterThan(0);
    });
  });

  describe("back button direction", () => {
    afterEach(() => {
      document.dir = "";
    });

    it("points the back arrow left in left-to-right layouts", async () => {
      const { container } = await renderForm();
      expect(
        container.querySelector(".clocks-search-back").getAttribute("iconSrc")
      ).toBe("chrome://global/skin/icons/arrow-left.svg");
    });

    it("points the back arrow right in right-to-left layouts", async () => {
      document.dir = "rtl";
      const { container } = await renderForm();
      expect(
        container.querySelector(".clocks-search-back").getAttribute("iconSrc")
      ).toBe("chrome://global/skin/icons/arrow-right.svg");
    });
  });
});
