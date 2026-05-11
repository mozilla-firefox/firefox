/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  backfillClockLabelColors,
  buildClocksRowAriaLabel,
  buildNextClockZones,
  decorateDefaultZones,
  formatDateTimeAttr,
  formatTime,
  getCityAbbreviation,
  getCityFromTimeZone,
  getClockFormDerivedState,
  getDefaultTimeZones,
  getTimeZoneAbbreviation,
  isValidTimeZone,
  isValidPaletteName,
  parseClockZonesPref,
  removeClockZoneAtIndex,
  shouldUse12HourTimeFormat,
} from "content-src/components/Widgets/Clocks/ClocksHelpers";

const originalResolvedOptions = Intl.DateTimeFormat.prototype.resolvedOptions;

function withLocalTz(tz, fn) {
  const stubbed = Intl.DateTimeFormat.prototype.resolvedOptions;
  Intl.DateTimeFormat.prototype.resolvedOptions = function () {
    const opts = originalResolvedOptions.call(this);
    return { ...opts, timeZone: tz };
  };
  try {
    fn();
  } finally {
    Intl.DateTimeFormat.prototype.resolvedOptions = stubbed;
  }
}

describe("getDefaultTimeZones", () => {
  it("returns IANA zone strings without decoration", () => {
    withLocalTz("Europe/Paris", () => {
      const result = getDefaultTimeZones();
      expect(result).toEqual([
        "Europe/Paris",
        "Europe/Berlin",
        "Australia/Sydney",
        "America/New_York",
      ]);
      expect(result.every(z => typeof z === "string")).toBe(true);
    });
  });

  it("dedupes when the local TZ matches a fixed zone (Berlin)", () => {
    withLocalTz("Europe/Berlin", () => {
      expect(getDefaultTimeZones()).toEqual([
        "Europe/Berlin",
        "Australia/Sydney",
        "America/New_York",
        "America/Los_Angeles",
      ]);
    });
  });

  it("dedupes when the local TZ matches a fixed zone (New York)", () => {
    withLocalTz("America/New_York", () => {
      expect(getDefaultTimeZones()).toEqual([
        "America/New_York",
        "Europe/Berlin",
        "Australia/Sydney",
        "America/Los_Angeles",
      ]);
    });
  });

  it("always returns exactly four zones", () => {
    withLocalTz("Asia/Tokyo", () => {
      expect(getDefaultTimeZones()).toHaveLength(4);
    });
    withLocalTz("Europe/Berlin", () => {
      expect(getDefaultTimeZones()).toHaveLength(4);
    });
  });

  it("falls back to the fixed set when Intl throws", () => {
    const original = Intl.DateTimeFormat;
    Intl.DateTimeFormat = function () {
      throw new Error("Intl unavailable");
    };
    try {
      const result = getDefaultTimeZones();
      expect(result).toEqual([
        "Europe/Berlin",
        "Australia/Sydney",
        "America/New_York",
        "America/Los_Angeles",
      ]);
    } finally {
      Intl.DateTimeFormat = original;
    }
  });
});

describe("decorateDefaultZones", () => {
  it("returns null label and labelColor for all zones", () => {
    const decorated = decorateDefaultZones([
      "Europe/Berlin",
      "Australia/Sydney",
    ]);
    expect(decorated).toHaveLength(2);
    expect(decorated[0]).toEqual({
      timeZone: "Europe/Berlin",
      label: null,
      labelColor: null,
    });
    expect(decorated[1]).toEqual({
      timeZone: "Australia/Sydney",
      label: null,
      labelColor: null,
    });
  });

  it("returns an empty array when given an empty array", () => {
    expect(decorateDefaultZones([])).toEqual([]);
  });
});

describe("backfillClockLabelColors", () => {
  it("adds colors only for labeled clocks that are missing one", () => {
    const randomStub = jest.spyOn(Math, "random").mockReturnValue(0);
    try {
      expect(
        backfillClockLabelColors([
          {
            timeZone: "America/New_York",
            city: "Boston",
            label: "Office",
            labelColor: null,
          },
          {
            timeZone: "Europe/Berlin",
            city: "Berlin",
            label: "Home",
            labelColor: "purple",
          },
          {
            timeZone: "Asia/Tokyo",
            city: "Tokyo",
            label: null,
            labelColor: null,
          },
        ])
      ).toEqual([
        {
          timeZone: "America/New_York",
          city: "Boston",
          label: "Office",
          labelColor: "cyan",
        },
        {
          timeZone: "Europe/Berlin",
          city: "Berlin",
          label: "Home",
          labelColor: "purple",
        },
        {
          timeZone: "Asia/Tokyo",
          city: "Tokyo",
          label: null,
          labelColor: null,
        },
      ]);
    } finally {
      randomStub.mockRestore();
    }
  });
});

describe("isValidTimeZone", () => {
  it("accepts valid IANA time zones and rejects invalid values", () => {
    expect(isValidTimeZone("Europe/Berlin")).toBe(true);
    expect(isValidTimeZone("Invalid/Zone")).toBe(false);
    expect(isValidTimeZone("")).toBe(false);
    expect(isValidTimeZone(null)).toBe(false);
  });
});

describe("getClockFormDerivedState", () => {
  const supportedTimeZones = [
    "Europe/Berlin",
    "Australia/Sydney",
    "America/New_York",
  ];

  it("resolves a selected time zone and enables save when a slot is open", () => {
    expect(
      getClockFormDerivedState({
        canAddClock: true,
        clockSearchQuery: "Ber",
        clockSelectedTimeZone: "Europe/Berlin",
        isEditingClock: false,
        supportedTimeZones,
      })
    ).toMatchObject({
      canAddSelectedClock: true,
      resolvedClockTimeZone: "Europe/Berlin",
      showLocationDropdown: false,
    });
  });

  it("resolves exact city queries without requiring a selected result", () => {
    expect(
      getClockFormDerivedState({
        canAddClock: true,
        clockSearchQuery: "new york",
        clockSelectedTimeZone: "",
        isEditingClock: false,
        supportedTimeZones,
      })
    ).toMatchObject({
      canAddSelectedClock: true,
      resolvedClockTimeZone: "America/New_York",
      showLocationDropdown: false,
    });
  });

  it("shows filtered results for unresolved partial queries", () => {
    expect(
      getClockFormDerivedState({
        canAddClock: true,
        clockSearchQuery: "syd",
        clockSelectedTimeZone: "",
        isEditingClock: false,
        supportedTimeZones,
      })
    ).toMatchObject({
      canAddSelectedClock: false,
      filteredTimeZones: ["Australia/Sydney"],
      resolvedClockTimeZone: "",
      showLocationDropdown: true,
    });
  });

  it("allows edits even when no add slots are open", () => {
    expect(
      getClockFormDerivedState({
        canAddClock: false,
        clockSearchQuery: "Berlin",
        clockSelectedTimeZone: "",
        isEditingClock: true,
        supportedTimeZones,
      }).canAddSelectedClock
    ).toBe(true);
  });
});

describe("buildNextClockZones", () => {
  const berlin = { timeZone: "Europe/Berlin", label: null, labelColor: null };
  const sydney = {
    timeZone: "Australia/Sydney",
    label: null,
    labelColor: null,
  };
  const tokyo = { timeZone: "Asia/Tokyo", label: null, labelColor: null };

  it("appends a new zone when editingClockIndex is null", () => {
    expect(buildNextClockZones([berlin, sydney], null, tokyo)).toEqual([
      berlin,
      sydney,
      tokyo,
    ]);
  });

  it("replaces the zone at the given index", () => {
    expect(buildNextClockZones([berlin, sydney], 0, tokyo)).toEqual([
      tokyo,
      sydney,
    ]);
    expect(buildNextClockZones([berlin, sydney], 1, tokyo)).toEqual([
      berlin,
      tokyo,
    ]);
  });

  it("does not mutate the original array", () => {
    const zones = [berlin, sydney];
    buildNextClockZones(zones, null, tokyo);
    expect(zones).toHaveLength(2);
  });
});

describe("removeClockZoneAtIndex", () => {
  const berlin = { timeZone: "Europe/Berlin", label: null, labelColor: null };
  const sydney = {
    timeZone: "Australia/Sydney",
    label: null,
    labelColor: null,
  };
  const tokyo = { timeZone: "Asia/Tokyo", label: null, labelColor: null };

  it("removes the element at the given index", () => {
    expect(removeClockZoneAtIndex([berlin, sydney, tokyo], 0)).toEqual([
      sydney,
      tokyo,
    ]);
    expect(removeClockZoneAtIndex([berlin, sydney, tokyo], 1)).toEqual([
      berlin,
      tokyo,
    ]);
    expect(removeClockZoneAtIndex([berlin, sydney, tokyo], 2)).toEqual([
      berlin,
      sydney,
    ]);
  });

  it("does not mutate the original array", () => {
    const zones = [berlin, sydney];
    removeClockZoneAtIndex(zones, 0);
    expect(zones).toHaveLength(2);
  });
});

describe("parseClockZonesPref", () => {
  it("preserves stored clock order, labels, colors, and duplicate zones", () => {
    const prefValue = JSON.stringify([
      {
        timeZone: "America/New_York",
        city: "Boston",
        label: "Office",
        labelColor: "cyan",
      },
      {
        timeZone: "America/New_York",
        label: "Family",
        labelColor: "green",
      },
      {
        timeZone: "Asia/Tokyo",
        label: "",
        labelColor: "not-a-palette",
      },
    ]);

    expect(parseClockZonesPref(prefValue)).toEqual([
      {
        timeZone: "America/New_York",
        city: "Boston",
        label: "Office",
        labelColor: "cyan",
      },
      {
        timeZone: "America/New_York",
        label: "Family",
        labelColor: "green",
      },
      {
        timeZone: "Asia/Tokyo",
        label: null,
        labelColor: null,
      },
    ]);
  });

  it("accepts string time zone entries", () => {
    expect(parseClockZonesPref(JSON.stringify(["Europe/Berlin"]))).toEqual([
      {
        timeZone: "Europe/Berlin",
        label: null,
        labelColor: null,
      },
    ]);
  });

  it("drops invalid entries and caps the result at four clocks", () => {
    const prefValue = JSON.stringify([
      { timeZone: "Invalid/NotAZone" },
      { timeZone: "Europe/Berlin" },
      { timeZone: "Australia/Sydney" },
      { timeZone: "America/New_York" },
      { timeZone: "America/Los_Angeles" },
      { timeZone: "Asia/Tokyo" },
    ]);

    expect(parseClockZonesPref(prefValue).map(clock => clock.timeZone)).toEqual(
      [
        "Europe/Berlin",
        "Australia/Sydney",
        "America/New_York",
        "America/Los_Angeles",
      ]
    );
  });

  it("returns null for missing, malformed, or empty pref data", () => {
    expect(parseClockZonesPref("")).toBeNull();
    expect(parseClockZonesPref("{")).toBeNull();
    expect(
      parseClockZonesPref(JSON.stringify({ timeZone: "Europe/Berlin" }))
    ).toBeNull();
    expect(parseClockZonesPref(JSON.stringify([{ timeZone: "" }]))).toBeNull();
  });
});

describe("shouldUse12HourTimeFormat", () => {
  it("returns true when prefValue is '12'", () => {
    expect(
      shouldUse12HourTimeFormat({ prefValue: "12", locale: "en-GB" })
    ).toBe(true);
  });

  it("returns false when prefValue is '24'", () => {
    expect(
      shouldUse12HourTimeFormat({ prefValue: "24", locale: "en-US" })
    ).toBe(false);
  });

  it("falls back to locale default when prefValue is empty", () => {
    expect(shouldUse12HourTimeFormat({ prefValue: "", locale: "en-US" })).toBe(
      true
    );
    expect(shouldUse12HourTimeFormat({ prefValue: "", locale: "en-GB" })).toBe(
      false
    );
  });

  it("treats undefined prefValue the same as empty", () => {
    expect(
      shouldUse12HourTimeFormat({ prefValue: undefined, locale: "en-US" })
    ).toBe(true);
  });
});

describe("getCityFromTimeZone", () => {
  it("returns the last IANA segment with underscores as spaces", () => {
    expect(getCityFromTimeZone("America/Los_Angeles")).toBe("Los Angeles");
    expect(getCityFromTimeZone("Europe/Berlin")).toBe("Berlin");
    expect(getCityFromTimeZone("Asia/Tokyo")).toBe("Tokyo");
  });

  it("handles single-segment and empty inputs", () => {
    expect(getCityFromTimeZone("UTC")).toBe("UTC");
    expect(getCityFromTimeZone("")).toBe("");
    expect(getCityFromTimeZone(null)).toBe("");
    expect(getCityFromTimeZone(undefined)).toBe("");
  });
});

describe("getCityAbbreviation", () => {
  it("returns the IATA code for cities in the curated map", () => {
    expect(getCityAbbreviation("New York")).toBe("NYC");
    expect(getCityAbbreviation("Los Angeles")).toBe("LAX");
    expect(getCityAbbreviation("Tokyo")).toBe("TYO");
    expect(getCityAbbreviation("Hong Kong")).toBe("HKG");
    expect(getCityAbbreviation("Zurich")).toBe("ZRH");
    // Regression: Seoul's IATA *city* code is SEL; ICN is Incheon airport
    // only. Mapping to ICN would display the airport code for a Seoul clock.
    expect(getCityAbbreviation("Seoul")).toBe("SEL");
  });

  it("falls back to first-3-chars-uppercased for unknown cities", () => {
    expect(getCityAbbreviation("Berlin")).toBe("BER");
    expect(getCityAbbreviation("Paris")).toBe("PAR");
    expect(getCityAbbreviation("Sydney")).toBe("SYD");
    expect(getCityAbbreviation("Ulaanbaatar")).toBe("ULA");
  });

  it("strips whitespace from multi-word cities before slicing", () => {
    // Ensures multi-word IANA cities not in the map don't emit a trailing
    // space (e.g. "St Johns" would slice to "ST " without this guard).
    expect(getCityAbbreviation("St Johns")).toBe("STJ");
    expect(getCityAbbreviation("Addis Ababa")).toBe("ADD");
    expect(getCityAbbreviation("Cape Town")).toBe("CAP");
  });

  it("resolves both legacy and canonical IANA city names to the same IATA code", () => {
    // tzdata renamed Kiev->Kyiv, Calcutta->Kolkata, Saigon->Ho Chi Minh;
    // depending on OS tzdata version, either spelling can surface from
    // Intl.DateTimeFormat().resolvedOptions().timeZone, so the map has both.
    expect(getCityAbbreviation("Kiev")).toBe("IEV");
    expect(getCityAbbreviation("Kyiv")).toBe("IEV");
    expect(getCityAbbreviation("Calcutta")).toBe("CCU");
    expect(getCityAbbreviation("Kolkata")).toBe("CCU");
    expect(getCityAbbreviation("Saigon")).toBe("SGN");
    expect(getCityAbbreviation("Ho Chi Minh")).toBe("SGN");
  });

  it("handles short and empty inputs", () => {
    expect(getCityAbbreviation("Rio")).toBe("RIO");
    expect(getCityAbbreviation("NY")).toBe("NY");
    expect(getCityAbbreviation("")).toBe("");
    expect(getCityAbbreviation(null)).toBe("");
  });
});

describe("isValidPaletteName", () => {
  it("accepts all known palette names", () => {
    const knownNames = [
      "cyan",
      "green",
      "yellow",
      "purple",
      "red",
      "orange",
      "blue",
      "pink",
      "violet",
      "neutral",
    ];
    knownNames.forEach(name => {
      expect(isValidPaletteName(name)).toBe(true);
    });
  });

  it("rejects unknown, malformed, or non-string inputs", () => {
    expect(isValidPaletteName("not-a-palette")).toBe(false);
    expect(isValidPaletteName("violet extra-class")).toBe(false);
    expect(isValidPaletteName("")).toBe(false);
    expect(isValidPaletteName(null)).toBe(false);
    expect(isValidPaletteName(undefined)).toBe(false);
    expect(isValidPaletteName(42)).toBe(false);
  });
});

describe("getTimeZoneAbbreviation", () => {
  it("falls back to the zone id for invalid zones", () => {
    const bogus = "Invalid/NotAZone";
    expect(getTimeZoneAbbreviation(bogus, "en-US")).toBe(bogus);
  });

  it("honours the provided date for DST-observing zones", () => {
    // Berlin is CET (winter) and CEST (summer). ICU versions may render
    // either the literal "CET/CEST" or "GMT+1/GMT+2"; accept both.
    const winter = new Date("2026-01-15T12:00:00Z");
    const summer = new Date("2026-07-15T12:00:00Z");
    expect(getTimeZoneAbbreviation("Europe/Berlin", "en-US", winter)).toMatch(
      /CET|GMT\+1/
    );
    expect(getTimeZoneAbbreviation("Europe/Berlin", "en-US", summer)).toMatch(
      /CEST|GMT\+2/
    );
  });
});

describe("formatDateTimeAttr", () => {
  it("formats the datetime value in the clock time zone", () => {
    expect(
      formatDateTimeAttr(new Date("2026-04-20T13:44:00Z"), "Asia/Tokyo")
    ).toBe("2026-04-20T22:44");
  });

  it("falls back to an ISO string when the time zone cannot be formatted", () => {
    const date = new Date("2026-04-20T13:44:00Z");
    expect(formatDateTimeAttr(date, "Invalid/Zone")).toBe(date.toISOString());
  });
});

describe("formatTime", () => {
  it("honours an explicit hour12=false override", () => {
    const date = new Date("2026-04-20T13:44:00Z");
    // en-US defaults to 12h; forcing 24h should produce "15" (Berlin = +2).
    const result = formatTime(date, "Europe/Berlin", "en-US", false);
    expect(result).toMatch(/^15/);
    expect(result).not.toMatch(/AM|PM/i);
  });

  it("honours an explicit hour12=true override", () => {
    const date = new Date("2026-04-20T13:44:00Z");
    // en-GB defaults to 24h; forcing 12h should produce an AM/PM string.
    const result = formatTime(date, "Europe/Berlin", "en-GB", true);
    expect(result).toMatch(/AM|PM|am|pm/i);
  });

  it("returns an empty string on invalid time zone", () => {
    const date = new Date("2026-04-20T13:44:00Z");
    expect(formatTime(date, "Invalid/NotAZone", "en-US")).toBe("");
  });
});

describe("buildClocksRowAriaLabel", () => {
  it("joins city, TZ, and time when all three are present", () => {
    expect(buildClocksRowAriaLabel("Berlin", "CET", "14:44")).toBe(
      "Berlin, CET, 14:44"
    );
  });

  it("drops only the time field when empty (pre-tick render)", () => {
    expect(buildClocksRowAriaLabel("Berlin", "CET", "")).toBe("Berlin, CET");
  });

  it("includes the label when present", () => {
    expect(buildClocksRowAriaLabel("Berlin", "CET", "14:44", "Home")).toBe(
      "Home, Berlin, CET, 14:44"
    );
  });
});
