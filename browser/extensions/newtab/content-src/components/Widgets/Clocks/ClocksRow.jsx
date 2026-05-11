/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React from "react";
import {
  buildClocksRowAriaLabel,
  formatDateTimeAttr,
  formatTime,
  getCityAbbreviation,
  getCityFromTimeZone,
  getTimeZoneAbbreviation,
  isValidPaletteName,
} from "./ClocksHelpers";

/**
 * Single row for the Clocks widget; parent pre-computes per-row flags.
 *
 * @param {object} props
 * @param {{timeZone: string, city?: string, label: string|null, labelColor: string|null}} props.clock
 * @param {string} [props.locale]
 * @param {Date|null} props.now Null before the first tick.
 * @param {Function|null} [props.onEdit]
 * @param {Function|null} [props.onRemove]
 * @param {boolean} [props.hideTimeOnInlineActions]
 * @param {boolean} props.shouldAbbreviate
 * @param {boolean} props.showLabel
 * @param {boolean} [props.showInlineActions]
 * @param {boolean} [props.use12HourFormat] Overrides locale default.
 */
export function ClocksRow({
  clock,
  locale,
  now,
  onEdit,
  onRemove,
  hideTimeOnInlineActions,
  shouldAbbreviate,
  showLabel,
  showInlineActions,
  use12HourFormat,
}) {
  const city = clock.city || getCityFromTimeZone(clock.timeZone);
  const cityDisplay = shouldAbbreviate ? getCityAbbreviation(city) : city;
  // Pass `now` so the TZ label and time resolve from the same instant;
  // otherwise they can disagree across a DST boundary.
  const tzLabel = getTimeZoneAbbreviation(
    clock.timeZone,
    locale,
    now ?? undefined
  );
  const timeDisplay = now
    ? formatTime(now, clock.timeZone, locale, use12HourFormat)
    : "";

  // aria-label uses the full city name even when the UI abbreviates, and
  // always includes the label so screen readers can disambiguate two
  // clocks for the same zone even on sizes where the chip is hidden.
  const ariaLabel = buildClocksRowAriaLabel(
    city,
    tzLabel,
    timeDisplay,
    clock.label
  );

  // Allow-list labelColor before interpolating; otherwise a malformed
  // value could inject unintended classes into the DOM.
  const chipClassName = isValidPaletteName(clock.labelColor)
    ? `clocks-label-chip clocks-chip-${clock.labelColor}`
    : "clocks-label-chip clocks-chip-neutral";

  return (
    <li
      className={`clocks-row${showInlineActions ? " has-inline-actions" : ""}${
        hideTimeOnInlineActions ? " hides-time-on-inline-actions" : ""
      }`}
      data-timezone={clock.timeZone}
      aria-label={ariaLabel}
      tabIndex={showInlineActions ? 0 : undefined}
    >
      <div className="clocks-meta" aria-hidden="true">
        {showLabel && !!clock.label && (
          <span className={chipClassName}>{clock.label}</span>
        )}
        <div className="clocks-label">
          <span className="clocks-city">{cityDisplay}</span>
          <span className="clocks-timezone">{tzLabel}</span>
        </div>
      </div>
      <time
        className="clocks-time"
        aria-hidden="true"
        dateTime={now ? formatDateTimeAttr(now, clock.timeZone) : undefined}
      >
        {timeDisplay}
      </time>
      {showInlineActions && (
        <div className="clocks-row-actions">
          <moz-button
            className="clocks-row-action-button clocks-row-edit-button"
            type="icon ghost"
            size="small"
            iconSrc="chrome://global/skin/icons/edit-outline.svg"
            data-l10n-id="newtab-clock-widget-button-edit-clock"
            onClick={onEdit ?? undefined}
          />
          {onRemove && (
            <moz-button
              className="clocks-row-action-button clocks-row-remove-button"
              type="icon ghost"
              size="small"
              iconSrc="chrome://global/skin/icons/delete.svg"
              data-l10n-id="newtab-clock-widget-button-remove-clock"
              onClick={onRemove}
            />
          )}
        </div>
      )}
    </li>
  );
}
