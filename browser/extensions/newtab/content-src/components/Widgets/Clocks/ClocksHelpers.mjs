/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// Fixed-order palette; each name needs a matching `.clocks-chip-<name>` in
// _Clocks.scss and drives isValidPaletteName's allow-list.
const LABEL_PALETTE = [
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
const RANDOM_LABEL_PALETTE = LABEL_PALETTE.filter(
  colorName => colorName !== "neutral"
);

/**
 * Allow-list for `clock.labelColor` before interpolating it into a
 * `clocks-chip-<name>` class, so a malformed value can't inject classes.
 */
export function isValidPaletteName(paletteName) {
  return typeof paletteName === "string" && LABEL_PALETTE.includes(paletteName);
}

export function getRandomLabelColor() {
  return RANDOM_LABEL_PALETTE[
    Math.floor(Math.random() * RANDOM_LABEL_PALETTE.length)
  ];
}

const FIXED_DEFAULT_ZONES = [
  "Europe/Berlin",
  "Australia/Sydney",
  "America/New_York",
  "America/Los_Angeles",
];
export const MAX_CLOCK_COUNT = 4;

// IATA city codes for cities where the code differs from slice(0,3).
// Cities whose code matches that slice (e.g. Sydney -> SYD, Berlin ->
// BER) are omitted; getCityAbbreviation falls back to the slice.
// Both legacy and canonical spellings (Kiev/Kyiv, Calcutta/Kolkata,
// Saigon/Ho Chi Minh) are present — the user's OS may report either,
// depending on its tzdata version.
const CITY_IATA_CODES = {
  // North America
  Detroit: "DTW",
  Halifax: "YHZ",
  Honolulu: "HNL",
  "Los Angeles": "LAX",
  "New York": "NYC",
  Phoenix: "PHX",
  "San Francisco": "SFO",
  Toronto: "YTO",
  Vancouver: "YVR",
  // South America
  Santiago: "SCL",
  // Europe
  Copenhagen: "CPH",
  Geneva: "GVA",
  Kiev: "IEV",
  Kyiv: "IEV",
  Moscow: "MOW",
  Prague: "PRG",
  Warsaw: "WAW",
  Zurich: "ZRH",
  // Asia
  Bangkok: "BKK",
  Beijing: "BJS",
  Beirut: "BEY",
  Calcutta: "CCU",
  Kolkata: "CCU",
  Colombo: "CMB",
  Dhaka: "DAC",
  Dubai: "DXB",
  "Ho Chi Minh": "SGN",
  "Hong Kong": "HKG",
  Jakarta: "JKT",
  Jerusalem: "JRS",
  Karachi: "KHI",
  "Kuala Lumpur": "KUL",
  Manila: "MNL",
  Riyadh: "RUH",
  Saigon: "SGN",
  Seoul: "SEL",
  Taipei: "TPE",
  Tehran: "THR",
  "Tel Aviv": "TLV",
  Tokyo: "TYO",
  // Africa
  Johannesburg: "JNB",
  Lagos: "LOS",
  Nairobi: "NBO",
  // Australia & Pacific
  Adelaide: "ADL",
  Auckland: "AKL",
  Brisbane: "BNE",
};

function is12HourLocale(locale) {
  try {
    const opts = new Intl.DateTimeFormat(locale, {
      hour: "numeric",
    }).resolvedOptions();
    if (typeof opts.hour12 === "boolean") {
      return opts.hour12;
    }
    // On older platforms `hour12` may be missing; derive it from `hourCycle`.
    return opts.hourCycle === "h11" || opts.hourCycle === "h12";
  } catch (e) {
    return false;
  }
}

/**
 * Resolves 12h vs 24h. Pref ("12"/"24") wins over locale default.
 */
export function shouldUse12HourTimeFormat({ prefValue, locale }) {
  if (prefValue === "12") {
    return true;
  }
  if (prefValue === "24") {
    return false;
  }
  return is12HourLocale(locale);
}

/**
 * Read-only landing zones: local first, then fixed samples, deduped, cap 4.
 */
export function getDefaultTimeZones() {
  let localTz = null;
  try {
    localTz = new Intl.DateTimeFormat().resolvedOptions().timeZone;
  } catch (e) {
    // Some environments can't resolve the local zone; fall back to the fixed set.
  }
  const result = [];
  const seen = new Set();
  if (localTz) {
    result.push(localTz);
    seen.add(localTz);
  }
  for (const tz of FIXED_DEFAULT_ZONES) {
    if (result.length >= 4) {
      break;
    }
    if (!seen.has(tz)) {
      result.push(tz);
      seen.add(tz);
    }
  }
  return result;
}

export function decorateDefaultZones(timeZones) {
  return timeZones.map(timeZone => ({
    timeZone,
    label: null,
    labelColor: null,
  }));
}

/**
 * Convenience wrapper returning the decorated default zones ready to render.
 */
export function buildDefaultZones() {
  return decorateDefaultZones(getDefaultTimeZones());
}

export const isValidTimeZone = timeZone => {
  if (typeof timeZone !== "string" || !timeZone) {
    return false;
  }
  try {
    new Intl.DateTimeFormat(undefined, { timeZone }).format(new Date(0));
    return true;
  } catch (e) {
    return false;
  }
};

export const getSupportedTimeZones = () => {
  try {
    if (typeof Intl.supportedValuesOf === "function") {
      const timeZones = Intl.supportedValuesOf("timeZone");
      if (timeZones.length) {
        return timeZones;
      }
    }
  } catch (e) {
    // Fall through to the fixed defaults below.
  }
  return FIXED_DEFAULT_ZONES;
};

const normalizeClockZone = clock => {
  const normalizedClock =
    typeof clock === "string" ? { timeZone: clock } : clock;
  if (!normalizedClock || !isValidTimeZone(normalizedClock.timeZone)) {
    return null;
  }
  const label =
    typeof normalizedClock.label === "string" && normalizedClock.label.trim()
      ? normalizedClock.label.trim()
      : null;
  const labelColor = isValidPaletteName(normalizedClock.labelColor)
    ? normalizedClock.labelColor
    : null;
  const city =
    typeof normalizedClock.city === "string" && normalizedClock.city.trim()
      ? normalizedClock.city.trim()
      : undefined;
  return {
    timeZone: normalizedClock.timeZone,
    ...(city !== undefined && { city }),
    label,
    labelColor,
  };
};

export const parseClockZonesPref = prefValue => {
  if (!prefValue) {
    return null;
  }
  try {
    const parsed =
      typeof prefValue === "string" ? JSON.parse(prefValue) : prefValue;
    if (!Array.isArray(parsed)) {
      return null;
    }
    const clocks = parsed
      .map(normalizeClockZone)
      .filter(Boolean)
      .slice(0, MAX_CLOCK_COUNT);
    return clocks.length ? clocks : null;
  } catch (e) {
    return null;
  }
};

/**
 * Derives a human-readable city from an IANA zone id
 * (e.g. "America/Los_Angeles" -> "Los Angeles").
 */
export function getCityFromTimeZone(tz) {
  if (!tz) {
    return "";
  }
  const segments = tz.split("/");
  const last = segments[segments.length - 1];
  return last.replace(/_/g, " ");
}

/**
 * Builds a fresh clock-zone object for a newly-added or zone-changed
 * clock. Seeds `city` from the IANA id so the manage panel and aria
 * label have a display name before any user customization; label and
 * color start null and are filled in later only if the user adds a
 * nickname.
 */
export const buildClockZone = timeZone => ({
  timeZone,
  city: getCityFromTimeZone(timeZone),
  label: null,
  labelColor: null,
});

export const backfillClockLabelColors = clockZones =>
  clockZones.map(clock =>
    clock.label && !clock.labelColor
      ? {
          ...clock,
          labelColor: getRandomLabelColor(),
        }
      : clock
  );

export const getClockFormDerivedState = ({
  canAddClock,
  clockSearchQuery,
  clockSelectedTimeZone,
  isEditingClock,
  supportedTimeZones,
}) => {
  let resolvedClockTimeZone = "";
  const query = clockSearchQuery.trim().toLowerCase();
  if (clockSelectedTimeZone && isValidTimeZone(clockSelectedTimeZone)) {
    resolvedClockTimeZone = clockSelectedTimeZone;
  } else if (query) {
    resolvedClockTimeZone =
      supportedTimeZones.find(timeZone => {
        const city = getCityFromTimeZone(timeZone).toLowerCase();
        return timeZone.toLowerCase() === query || city === query;
      }) ?? "";
  }

  const filteredTimeZones = query
    ? supportedTimeZones
        .filter(timeZone => {
          const city = getCityFromTimeZone(timeZone).toLowerCase();
          return timeZone.toLowerCase().includes(query) || city.includes(query);
        })
        .slice(0, 8)
    : [];

  return {
    canAddSelectedClock:
      (isEditingClock || canAddClock) && !!resolvedClockTimeZone,
    filteredTimeZones,
    resolvedClockTimeZone,
    showLocationDropdown: !!(query && !resolvedClockTimeZone),
  };
};

export const buildNextClockZones = (clockZones, editingClockIndex, zone) =>
  editingClockIndex === null
    ? [...clockZones, zone]
    : clockZones.map((clock, index) =>
        index === editingClockIndex ? zone : clock
      );

export const removeClockZoneAtIndex = (clockZones, indexToRemove) =>
  clockZones.filter((_, index) => index !== indexToRemove);

/**
 * IATA code for known cities, else first 3 non-whitespace chars upcased.
 * Stripping whitespace avoids trailing space on multi-word names.
 */
export function getCityAbbreviation(cityName) {
  if (!cityName) {
    return "";
  }
  if (CITY_IATA_CODES[cityName]) {
    return CITY_IATA_CODES[cityName];
  }
  return cityName.replace(/\s/g, "").slice(0, 3).toUpperCase();
}

/**
 * Returns the short name for a time zone at a given moment, like "CET"
 * or "EST". Pass the same `date` you use for formatTime: DST-observing
 * zones flip between two abbreviations (CET/CEST, EST/EDT) at the
 * transition boundary, and using a mismatched date can leave the
 * displayed time and the label out of sync. Falls back to the zone id
 * (e.g. "Europe/Berlin") if the platform can't produce a short name.
 */
export function getTimeZoneAbbreviation(tz, locale, date = new Date()) {
  try {
    const parts = new Intl.DateTimeFormat(locale, {
      timeZone: tz,
      timeZoneName: "short",
    }).formatToParts(date);
    const part = parts.find(p => p.type === "timeZoneName");
    return part?.value ?? tz;
  } catch (e) {
    return tz;
  }
}

/**
 * Formats Date as a local datetime string (YYYY-MM-DDTHH:mm) in the given
 * timezone, suitable for <time>'s datetime attribute. Falls back to the UTC
 * ISO string if the platform can't format the zone.
 */
export function formatDateTimeAttr(date, tz) {
  try {
    const parts = new Intl.DateTimeFormat(undefined, {
      timeZone: tz,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      hourCycle: "h23",
    }).formatToParts(date);
    const get = type => parts.find(p => p.type === type)?.value ?? "00";
    return `${get("year")}-${get("month")}-${get("day")}T${get("hour")}:${get("minute")}`;
  } catch (e) {
    return date.toISOString();
  }
}

/**
 * Formats Date as hh:mm in a zone; "" if the zone can't be formatted.
 */
export function formatTime(date, tz, locale, hour12) {
  try {
    const opts = {
      timeZone: tz,
      hour: "numeric",
      minute: "2-digit",
    };
    if (typeof hour12 === "boolean") {
      opts.hour12 = hour12;
    }
    return new Intl.DateTimeFormat(locale, opts).format(date);
  } catch (e) {
    return "";
  }
}

/**
 * Screen-reader label. Prepends label when present; omits the time until
 * it becomes available.
 */
export const buildClocksRowAriaLabel = (city, tzLabel, timeDisplay, label) => {
  const parts = label ? [label, city, tzLabel] : [city, tzLabel];
  if (timeDisplay) {
    parts.push(timeDisplay);
  }
  return parts.join(", ");
};
