/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef } from "react";
import { getCityFromTimeZone } from "./ClocksHelpers";

export function EditClocksPanel({
  clockZones,
  canAddClock,
  onShowAddClock,
  onEditClock,
  onRemoveClock,
  onClose,
}) {
  const backButtonRef = useRef(null);

  // Focus the back button when the panel opens. Double-rAF so this fires
  // one frame after closeContextMenu's blur, which is scheduled in the
  // same event handler when opening from the context menu.
  useEffect(() => {
    let outerId = 0;
    let innerId = 0;
    outerId = requestAnimationFrame(() => {
      innerId = requestAnimationFrame(() => {
        backButtonRef.current?.focus?.();
      });
    });
    return () => {
      cancelAnimationFrame(outerId);
      cancelAnimationFrame(innerId);
    };
  }, []);

  return (
    <section
      className="clocks-panel clocks-edit-panel"
      aria-labelledby="clocks-edit-title"
      onKeyDown={e => {
        if (e.key === "Escape") {
          onClose();
        }
      }}
    >
      <div className="clocks-edit-header">
        <div className="clocks-edit-title-group">
          <moz-button
            className="clocks-edit-back-button"
            type="icon ghost"
            size="small"
            iconSrc="chrome://global/skin/icons/arrow-left.svg"
            data-l10n-id="newtab-clock-widget-button-back"
            onClick={onClose}
            ref={backButtonRef}
          />
          <h3
            id="clocks-edit-title"
            className="clocks-edit-title"
            data-l10n-id="newtab-clock-widget-label-your-clocks"
          />
        </div>
        {canAddClock && (
          <moz-button
            className="clocks-edit-add-button"
            type="icon primary"
            size="small"
            iconSrc="chrome://global/skin/icons/plus.svg"
            data-l10n-id="newtab-clock-widget-button-add"
            onClick={onShowAddClock}
          />
        )}
      </div>
      <ul className="clocks-edit-list">
        {clockZones.map((clock, i) => (
          <li
            className="clocks-edit-item"
            key={`${clock.timeZone}-${i}`}
            tabIndex={0}
          >
            <div className="clocks-edit-top-row">
              <span className="clocks-edit-city">
                {clock.city || getCityFromTimeZone(clock.timeZone)}
              </span>
              <div className="clocks-edit-item-actions">
                <moz-button
                  className="clocks-edit-item-button clocks-edit-item-edit-button"
                  type="icon ghost"
                  size="small"
                  iconSrc="chrome://global/skin/icons/edit-outline.svg"
                  data-l10n-id="newtab-clock-widget-button-edit-clock"
                  onClick={() => onEditClock(i)}
                />
                {clockZones.length > 1 && (
                  <moz-button
                    className="clocks-edit-item-button clocks-edit-item-remove-button"
                    type="icon ghost"
                    size="small"
                    iconSrc="chrome://global/skin/icons/delete.svg"
                    data-l10n-id="newtab-clock-widget-button-remove-clock"
                    onClick={() => onRemoveClock(i)}
                  />
                )}
              </div>
            </div>
            <span
              aria-hidden={!clock.label}
              className="clocks-edit-subtitle"
              data-l10n-id={
                clock.label
                  ? "newtab-clock-widget-label-nickname-with-value"
                  : undefined
              }
              data-l10n-args={
                clock.label
                  ? JSON.stringify({
                      nickname: clock.label,
                    })
                  : undefined
              }
            >
              {clock.label ? null : " "}
            </span>
          </li>
        ))}
      </ul>
    </section>
  );
}
