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
import {
  buildClockZone,
  getCityFromTimeZone,
  getClockFormDerivedState,
  getRandomLabelColor,
} from "./ClocksHelpers";

const MAX_NICKNAME_LENGTH = 11;

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
 * @param {(zone: object) => void} props.onSave
 * @param {() => void} props.onCancel
 */
export function AddClockForm({
  isEditing,
  initialClock,
  canAddClock,
  supportedTimeZones,
  onSave,
  onCancel,
}) {
  const [searchQuery, setSearchQuery] = useState(
    initialClock
      ? initialClock.city || getCityFromTimeZone(initialClock.timeZone)
      : ""
  );
  const [selectedTimeZone, setSelectedTimeZone] = useState(
    initialClock?.timeZone || ""
  );
  const [nickname, setNickname] = useState(initialClock?.label || "");
  const searchInputRef = useRef(null);

  const {
    canAddSelectedClock,
    filteredTimeZones,
    resolvedClockTimeZone,
    showLocationDropdown,
  } = useMemo(
    () =>
      getClockFormDerivedState({
        canAddClock,
        clockSearchQuery: searchQuery,
        clockSelectedTimeZone: selectedTimeZone,
        isEditingClock: isEditing,
        supportedTimeZones,
      }),
    [canAddClock, searchQuery, selectedTimeZone, isEditing, supportedTimeZones]
  );

  // moz-input-search renders its inner input asynchronously, so focusing
  // the custom element host immediately can throw before inputEl exists.
  useEffect(() => {
    let frameId = 0;
    let remainingFrames = 5;

    const focusWhenReady = () => {
      const input = searchInputRef.current?.inputEl;
      if (input) {
        input.focus();
        return;
      }
      if (remainingFrames > 0) {
        remainingFrames -= 1;
        frameId = requestAnimationFrame(focusWhenReady);
      }
    };

    frameId = requestAnimationFrame(focusWhenReady);
    return () => cancelAnimationFrame(frameId);
  }, []);

  const handleSelectLocation = useCallback(timeZone => {
    setSearchQuery(getCityFromTimeZone(timeZone));
    setSelectedTimeZone(timeZone);
  }, []);

  const handleSubmit = useCallback(() => {
    if (!canAddSelectedClock) {
      return;
    }
    const trimmed = nickname.trim();
    const label = trimmed ? trimmed.slice(0, MAX_NICKNAME_LENGTH) : null;
    // Preserve existing labelColor when editing the same zone so an
    // unchanged labeled clock keeps its color across edits.
    const baseZone =
      initialClock && initialClock.timeZone === resolvedClockTimeZone
        ? { ...initialClock }
        : buildClockZone(resolvedClockTimeZone);
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
    onSave,
  ]);

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
        handleSubmit();
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
          handleSubmit();
        }
      }}
      onBlur={e => {
        if (e.relatedTarget && !e.currentTarget.contains(e.relatedTarget)) {
          onCancel();
        }
      }}
    >
      <div className="clocks-location-wrapper">
        <moz-input-search
          role="combobox"
          aria-haspopup="listbox"
          aria-expanded={showLocationDropdown}
          aria-controls="clocks-search-results"
          aria-activedescendant={
            showLocationDropdown &&
            selectedTimeZone &&
            filteredTimeZones.includes(selectedTimeZone)
              ? `clocks-result-${filteredTimeZones.indexOf(selectedTimeZone)}`
              : undefined
          }
          aria-autocomplete="list"
          className="clocks-search-location-input"
          data-l10n-id="newtab-clock-widget-search-location-input"
          id="clocks-location-input"
          ref={searchInputRef}
          value={searchQuery}
          onInput={e => {
            setSearchQuery(e.target.value);
            setSelectedTimeZone("");
          }}
        />
        {showLocationDropdown && (
          <div
            id="clocks-search-results"
            className="clocks-search-results"
            role="listbox"
            data-l10n-id="newtab-clock-widget-search-results"
          >
            {filteredTimeZones.map((timeZone, index) => (
              <div
                id={`clocks-result-${index}`}
                className="clocks-search-result"
                key={timeZone}
                onClick={() => handleSelectLocation(timeZone)}
                onKeyDown={e => {
                  if (e.key === "Enter" || e.key === " ") {
                    e.preventDefault();
                    handleSelectLocation(timeZone);
                  }
                }}
                role="option"
                aria-selected={timeZone === selectedTimeZone}
                tabIndex={0}
              >
                <span className="clocks-search-result-city">
                  {getCityFromTimeZone(timeZone)}
                </span>
                <span className="clocks-search-result-timezone">
                  {timeZone}
                </span>
              </div>
            ))}
          </div>
        )}
      </div>
      <moz-input-text
        className="clocks-nickname-input"
        data-l10n-id="newtab-clock-widget-input-nickname"
        id="clocks-nickname-input"
        value={nickname}
        onInput={e => setNickname(e.target.value.slice(0, MAX_NICKNAME_LENGTH))}
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
    </form>
  );
}
