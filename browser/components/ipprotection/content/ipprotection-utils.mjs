/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { BANDWIDTH } from "chrome://browser/content/ipprotection/ipprotection-constants.mjs";

const countryDisplayNames = new Intl.DisplayNames(undefined, {
  type: "region",
});

/**
 * Returns the localized country name for a given country code.
 *
 * @param {string} code
 * @returns {string|null}
 */
export function countryName(code) {
  try {
    return countryDisplayNames.of(code) ?? null;
  } catch (_) {
    return null;
  }
}

/**
 * Formats remaining bandwidth bytes into a rounded value with a unit indicator.
 *
 * @param {number} remainingBytes - Remaining bandwidth in bytes.
 * @param {string} [locale] - BCP 47 locale tag; defaults to the runtime locale.
 * @returns {{ value: number|string, useGB: boolean }}
 *   `value` is the remaining amount: a locale-formatted string rounded to 1
 *   decimal place (when >= 1 GB) or a floored integer in MB (when < 1 GB).
 *   `useGB` indicates whether the value is in GB (true) or MB (false).
 */
export function formatRemainingBandwidth(remainingBytes, locale = undefined) {
  const remainingGB = remainingBytes / BANDWIDTH.BYTES_IN_GB;
  if (remainingGB < 1) {
    return {
      value: Math.floor(remainingBytes / BANDWIDTH.BYTES_IN_MB),
      useGB: false,
    };
  }
  return {
    value: new Intl.NumberFormat(locale, {
      maximumFractionDigits: 1,
    }).format(remainingGB),
    useGB: true,
  };
}
