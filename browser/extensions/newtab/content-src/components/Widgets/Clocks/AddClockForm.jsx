/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { CLOCK_CITIES } from "./ClockCityRegistry.mjs";
import {
  buildClockSearchIndex,
  buildClockZone,
  filterCustomZoneResults,
  formatCustomZoneLabel,
  getCityFromTimeZone,
  getClockFormDerivedState,
  getDefaultTimeZones,
  getRandomLabelColor,
  isValidTimeZone,
} from "./ClocksHelpers";
import { useCuratedCityNames } from "./useCuratedCityNames";

const MAX_NICKNAME_LENGTH = 11;
const MAX_CITY_LENGTH = 32;

// moz-input-search doesn't support <datalist>, so the suggestions are a bespoke listbox and need their own keyboard navigation
function handleListboxArrowKeys(event) {
  const listbox = event.currentTarget.querySelector('[role="listbox"]');
  if (!listbox || (event.key !== "ArrowDown" && event.key !== "ArrowUp")) {
    return;
  }
  const options = [...listbox.querySelectorAll('[role="option"]')];
  const index = options.indexOf(event.target);
  let next = null;
  if (event.key === "ArrowDown") {
    // Wrap from the last option back to the first.
    next = options[index + 1] ?? options[0];
  } else if (index === 0) {
    const field = event.currentTarget.querySelector("moz-input-search");
    next = field?.inputEl ?? field;
  } else if (index > 0) {
    next = options[index - 1];
  }
  // On an option, always prevent the default so the page never scrolls, even
  // if focus has nowhere to move.
  if (next || index >= 0) {
    event.preventDefault();
  }
  next?.focus();
}

/**
 * Add/edit form for a single clock. Owns its own form state — the parent
 * only knows whether the form is open (mount/unmount toggle), the clock
 * being edited (if any), and what to do with the saved zone.
 *
 * @param {object} props
 * @param {boolean} props.isEditing
 * @param {object|null} props.initialClock Pre-fill values when editing.
 * @param {boolean} props.canAddClock
 * @param {string[]} props.supportedTimeZones
 * @param {string} [props.locale] Locale for localized zone names.
 * @param {(zone: object) => void} props.onSave
 * @param {() => void} props.onCancel
 */
export function AddClockForm({
  isEditing,
  initialClock,
  canAddClock,
  supportedTimeZones,
  locale,
  onSave,
  onCancel,
}) {
  // Localized display name per curated city id; {} until resolved.
  const curatedNames = useCuratedCityNames();
  const [searchQuery, setSearchQuery] = useState(
    initialClock
      ? initialClock.city || getCityFromTimeZone(initialClock.timeZone)
      : ""
  );
  const [selectedTimeZone, setSelectedTimeZone] = useState(
    initialClock?.timeZone || ""
  );
  const [selectedCity, setSelectedCity] = useState(initialClock?.city || "");
  const [selectedCityId, setSelectedCityId] = useState(
    initialClock?.cityId || ""
  );
  const [nickname, setNickname] = useState(
    initialClock?.label?.slice(0, MAX_NICKNAME_LENGTH) || ""
  );
  const [customMode, setCustomMode] = useState(false);
  const [customCity, setCustomCity] = useState("");
  const [customZone, setCustomZone] = useState("");
  const [customZoneQuery, setCustomZoneQuery] = useState("");
  // Hides the results when focus leaves the field and its list; typing shows
  // them again.
  const [resultsOpen, setResultsOpen] = useState(true);
  const nicknameL10nArgs = JSON.stringify({
    count: MAX_NICKNAME_LENGTH - nickname.length,
  });

  const searchInputRef = useRef(null);
  const customCityRef = useRef(null);

  // One index for both searches, rebuilt only when the zones or localized
  // names change — not on every keystroke.
  const searchIndex = useMemo(
    () =>
      buildClockSearchIndex({
        supportedTimeZones,
        curatedCities: CLOCK_CITIES,
        curatedNames,
        locale,
      }),
    [supportedTimeZones, curatedNames, locale]
  );

  const {
    canAddSelectedClock,
    filteredResults,
    resolvedClockTimeZone,
    resolvedClockCity,
    resolvedClockCityId,
    hasExactMatch,
    showLocationDropdown,
  } = useMemo(
    () =>
      getClockFormDerivedState({
        canAddClock,
        clockSearchQuery: searchQuery,
        clockSelectedTimeZone: selectedTimeZone,
        clockSelectedCity: selectedCity,
        clockSelectedCityId: selectedCityId,
        isEditingClock: isEditing,
        searchIndex,
      }),
    [
      canAddClock,
      searchQuery,
      selectedTimeZone,
      selectedCity,
      selectedCityId,
      isEditing,
      searchIndex,
    ]
  );

  // The custom flow names the city by hand, so it only picks a zone.
  const customZoneResults = useMemo(
    () =>
      customMode ? filterCustomZoneResults(searchIndex, customZoneQuery) : [],
    [customMode, searchIndex, customZoneQuery]
  );
  // A picked zone wins; otherwise let a typed canonical IANA id resolve.
  const effectiveCustomZone =
    customZone ||
    (isValidTimeZone(customZoneQuery.trim()) ? customZoneQuery.trim() : "");
  const showCustomZoneDropdown = !!customZoneQuery.trim() && !customZone;

  // When editing a curated clock the persisted city may be in a stale locale;
  // adopt the current localized name once it resolves, unless the user typed.
  const initialCityId = initialClock?.cityId;
  useEffect(() => {
    const localized = initialCityId && curatedNames[initialCityId];
    if (!localized) {
      return;
    }
    const persisted = initialClock.city || "";
    setSearchQuery(prev => (prev === persisted ? localized : prev));
    setSelectedCity(prev => (prev === persisted ? localized : prev));
  }, [curatedNames, initialCityId, initialClock]);

  // Keep keyboard/AT focus on the active field when the form opens and when
  // switching between city search and the custom-add view, so focus is never
  // lost on a view change. moz-* inputs render their inner input async, so
  // wait for inputEl, then fall back to focusing the host element.
  useEffect(() => {
    let frameId = 0;
    let remainingFrames = 5;

    const focusWhenReady = () => {
      const host = customMode ? customCityRef.current : searchInputRef.current;
      const input = host?.inputEl;
      if (input) {
        input.focus();
        return;
      }
      if (remainingFrames > 0) {
        remainingFrames -= 1;
        frameId = requestAnimationFrame(focusWhenReady);
      } else if (typeof host?.focus === "function") {
        host.focus();
      }
    };

    frameId = requestAnimationFrame(focusWhenReady);
    return () => cancelAnimationFrame(frameId);
  }, [customMode]);

  const handleSelectLocation = useCallback((timeZone, city, cityId = "") => {
    setSearchQuery(city);
    setSelectedTimeZone(timeZone);
    setSelectedCity(city);
    setSelectedCityId(cityId);
  }, []);

  const handleNicknameInput = useCallback(e => {
    setNickname(e.target.value.slice(0, MAX_NICKNAME_LENGTH));
  }, []);

  const handleSubmit = useCallback(() => {
    if (!canAddSelectedClock) {
      return;
    }
    const trimmed = nickname.trim();
    const label = trimmed ? trimmed.slice(0, MAX_NICKNAME_LENGTH) : null;
    // Same zone can now mean a different city (Los Angeles -> San Francisco),
    // so always apply the resolved city/cityId while keeping the color. Only
    // keep the existing cityId when the resolved city is unchanged; otherwise
    // an inherited cityId would mislabel the new city.
    const { cityId: previousCityId, ...editedClock } = initialClock ?? {};
    const editedCityId =
      resolvedClockCityId ||
      (resolvedClockCity === initialClock?.city ? previousCityId : "");
    const baseZone =
      initialClock && initialClock.timeZone === resolvedClockTimeZone
        ? {
            ...editedClock,
            city: resolvedClockCity,
            ...(editedCityId ? { cityId: editedCityId } : {}),
          }
        : buildClockZone(
            resolvedClockTimeZone,
            resolvedClockCity,
            resolvedClockCityId
          );
    onSave({
      ...baseZone,
      label,
      labelColor: label ? baseZone.labelColor || getRandomLabelColor() : null,
    });
  }, [
    canAddSelectedClock,
    nickname,
    initialClock,
    resolvedClockTimeZone,
    resolvedClockCity,
    resolvedClockCityId,
    onSave,
  ]);

  const enterCustomMode = useCallback(() => {
    const localZone = getDefaultTimeZones()[0] || "";
    setCustomMode(true);
    setCustomCity(searchQuery.trim().slice(0, MAX_CITY_LENGTH));
    setCustomZone(localZone);
    setCustomZoneQuery(localZone ? formatCustomZoneLabel(localZone) : "");
  }, [searchQuery]);

  const handleZoneInput = useCallback(value => {
    setCustomZoneQuery(value);
    setCustomZone("");
    setResultsOpen(true);
  }, []);

  const closeResultsOnFocusOut = useCallback(e => {
    if (!e.currentTarget.contains(e.relatedTarget)) {
      setResultsOpen(false);
    }
  }, []);

  const handleZoneSelect = useCallback((timeZone, label) => {
    setCustomZoneQuery(label);
    setCustomZone(timeZone);
  }, []);

  const canAddCustom =
    (isEditing || canAddClock) &&
    !!customCity.trim() &&
    isValidTimeZone(effectiveCustomZone);

  const handleCustomSubmit = useCallback(() => {
    if (!canAddCustom) {
      return;
    }
    const trimmed = nickname.trim();
    const label = trimmed ? trimmed.slice(0, MAX_NICKNAME_LENGTH) : null;
    const base = buildClockZone(effectiveCustomZone, customCity.trim());
    onSave({
      ...base,
      label,
      labelColor: label
        ? initialClock?.labelColor || getRandomLabelColor()
        : null,
    });
  }, [
    canAddCustom,
    effectiveCustomZone,
    customCity,
    nickname,
    initialClock,
    onSave,
  ]);

  const isRTL = typeof document !== "undefined" && document.dir === "rtl";
  const backIconSrc = `chrome://global/skin/icons/arrow-${isRTL ? "right" : "left"}.svg`;

  return (
    <form
      className="clocks-panel clocks-add-form"
      data-l10n-id={
        isEditing
          ? "newtab-clock-widget-edit-clock-form"
          : "newtab-clock-widget-add-clock-form"
      }
      onSubmit={e => {
        e.preventDefault();
        if (customMode) {
          handleCustomSubmit();
        } else {
          handleSubmit();
        }
      }}
      onKeyDown={e => {
        if (e.key === "Escape") {
          onCancel();
        } else if (
          e.key === "Enter" &&
          !e.target.closest(".clocks-search-result") &&
          !e.target.closest("moz-button, button")
        ) {
          e.preventDefault();
          if (customMode) {
            handleCustomSubmit();
          } else {
            handleSubmit();
          }
        }
      }}
    >
      {customMode ? (
        <>
          <moz-input-text
            className="clocks-custom-city-input"
            data-l10n-id="newtab-clock-widget-custom-city-input"
            id="clocks-custom-city-input"
            dir="auto"
            ref={customCityRef}
            value={customCity}
            onInput={e =>
              setCustomCity(e.target.value.slice(0, MAX_CITY_LENGTH))
            }
          />
          <div
            className="clocks-location-wrapper"
            onKeyDown={handleListboxArrowKeys}
            onBlur={closeResultsOnFocusOut}
          >
            {/* Results are a reachable listbox, not a combobox popup:
                moz-input-search can't forward combobox ARIA to its input. */}
            <moz-input-search
              className="clocks-custom-timezone-input"
              data-l10n-id="newtab-clock-widget-custom-timezone-input"
              id="clocks-custom-timezone-input"
              value={customZoneQuery}
              onInput={e => handleZoneInput(e.target.value)}
            />
            {showCustomZoneDropdown && resultsOpen && (
              <div
                id="clocks-custom-zone-results"
                className="clocks-search-results"
                role="listbox"
                data-l10n-id="newtab-clock-widget-custom-zone-results"
              >
                {customZoneResults.map((result, index) => {
                  const label = `${result.city} · ${result.offsetLabel}`;
                  return (
                    <div
                      id={`clocks-custom-zone-result-${index}`}
                      className="clocks-search-result"
                      key={result.timeZone}
                      onClick={() => handleZoneSelect(result.timeZone, label)}
                      onKeyDown={e => {
                        if (e.key === "Enter" || e.key === " ") {
                          e.preventDefault();
                          handleZoneSelect(result.timeZone, label);
                        }
                      }}
                      role="option"
                      aria-selected={result.timeZone === customZone}
                      tabIndex={-1}
                    >
                      <span className="clocks-search-result-city" dir="auto">
                        {result.city}
                      </span>
                      <span
                        className="clocks-search-result-timezone"
                        dir="auto"
                      >
                        {`${result.offsetLabel} · ${result.timeZone}`}
                      </span>
                    </div>
                  );
                })}
                {!customZoneResults.length && (
                  <div
                    className="clocks-search-no-results"
                    role="status"
                    data-l10n-id="newtab-clock-widget-custom-zone-no-results"
                  />
                )}
              </div>
            )}
          </div>
          <moz-input-text
            className="clocks-nickname-input"
            data-l10n-id="newtab-clock-widget-input-nickname2"
            data-l10n-args={nicknameL10nArgs}
            id="clocks-custom-nickname-input"
            value={nickname}
            onInput={handleNicknameInput}
          />
          <moz-button-group className="clocks-add-actions">
            <moz-button
              data-l10n-id="newtab-clock-widget-custom-back"
              onClick={() => setCustomMode(false)}
            />
            <moz-button
              className="clocks-form-submit"
              data-l10n-id="newtab-clock-widget-button-add-clock"
              disabled={!canAddCustom}
              onClick={handleCustomSubmit}
              type="primary"
            />
          </moz-button-group>
        </>
      ) : (
        <>
          <div className="clocks-search-header">
            <moz-button
              className="clocks-search-back"
              type="icon ghost"
              size="small"
              iconSrc={backIconSrc}
              data-l10n-id="newtab-clock-widget-search-back-button"
              onClick={onCancel}
            />
            <div
              className="clocks-location-wrapper"
              onKeyDown={handleListboxArrowKeys}
              onBlur={closeResultsOnFocusOut}
            >
              {/* Reachable listbox, not a combobox popup (see custom-zone note). */}
              <moz-input-search
                className="clocks-search-location-input"
                data-l10n-id="newtab-clock-widget-search-location-input2"
                id="clocks-location-input"
                ref={searchInputRef}
                value={searchQuery}
                onInput={e => {
                  setSearchQuery(e.target.value);
                  setResultsOpen(true);
                  setSelectedTimeZone("");
                  setSelectedCity("");
                  setSelectedCityId("");
                }}
              />
              {showLocationDropdown && resultsOpen && (
                <div
                  id="clocks-search-results"
                  className="clocks-search-results"
                  role="listbox"
                  data-l10n-id="newtab-clock-widget-search-results"
                >
                  {filteredResults.map((result, index) => (
                    <div
                      id={`clocks-result-${index}`}
                      className="clocks-search-result"
                      key={`${result.timeZone}-${result.city}`}
                      onClick={() =>
                        handleSelectLocation(
                          result.timeZone,
                          result.city,
                          result.cityId
                        )
                      }
                      onKeyDown={e => {
                        if (e.key === "Enter" || e.key === " ") {
                          e.preventDefault();
                          handleSelectLocation(
                            result.timeZone,
                            result.city,
                            result.cityId
                          );
                        }
                      }}
                      role="option"
                      aria-selected={
                        result.timeZone === selectedTimeZone &&
                        result.city === selectedCity
                      }
                      tabIndex={-1}
                    >
                      <span className="clocks-search-result-city" dir="auto">
                        {result.city}
                      </span>
                      <span
                        className="clocks-search-result-timezone"
                        dir="auto"
                      >
                        {result.zoneName || result.timeZone}
                      </span>
                    </div>
                  ))}
                  {/* No exact match: the unmatched query becomes an actionable
                    add, shown below any partial matches, not a dead end. */}
                  {!hasExactMatch && (
                    <div
                      id="clocks-add-custom-option"
                      className="clocks-search-result clocks-add-custom"
                      role="option"
                      aria-selected="false"
                      tabIndex={-1}
                      onClick={enterCustomMode}
                      onKeyDown={e => {
                        if (e.key === "Enter" || e.key === " ") {
                          e.preventDefault();
                          enterCustomMode();
                        }
                      }}
                      data-l10n-id="newtab-clock-widget-add-custom"
                      data-l10n-args={JSON.stringify({
                        city: searchQuery.trim(),
                      })}
                    />
                  )}
                </div>
              )}
            </div>
          </div>
          <moz-input-text
            className="clocks-nickname-input"
            data-l10n-id="newtab-clock-widget-input-nickname2"
            data-l10n-args={nicknameL10nArgs}
            id="clocks-nickname-input"
            value={nickname}
            onInput={handleNicknameInput}
          />
          <moz-button-group className="clocks-add-actions">
            <moz-button
              data-l10n-id="newtab-clock-widget-button-cancel"
              onClick={onCancel}
            />
            <moz-button
              className="clocks-form-submit"
              data-l10n-id={
                isEditing
                  ? "newtab-clock-widget-button-save"
                  : "newtab-clock-widget-button-add-clock"
              }
              disabled={!canAddSelectedClock}
              onClick={handleSubmit}
              type="primary"
            />
          </moz-button-group>
        </>
      )}
    </form>
  );
}
