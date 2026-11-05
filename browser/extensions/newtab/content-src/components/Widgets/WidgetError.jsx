/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import { useIntersectionObserver } from "../../lib/utils";

/**
 * The message shown in place of a widget that could not be loaded. Mount it
 * only while the widget is in its error state. It records the error the first
 * time the message is on screen, so a preloaded tab the user never saw
 * records nothing.
 *
 * @param {object} props
 * @param {Function} props.recordError - recordError from useWidgetTelemetry.
 * @param {string} [props.errorType="load_error"] - the error type to record.
 * @param {string} props.title - the widget's name, already localized.
 *   role="alert" announces the message and nothing else, so the name goes
 *   inside it even when a header already shows it.
 * @param {boolean} [props.headerless] - true for a widget that draws no header
 *   of its own. The name then shows above the message rather than only being
 *   announced.
 * @param {string} [props.l10nId="newtab-widget-error-not-available"] - Fluent
 *   id of the message. A widget with its own wording passes that instead.
 * @param {string} [props.slot] - the slot for the error message.
 */
export function WidgetError({
  recordError,
  errorType = "load_error",
  title,
  headerless = false,
  l10nId = "newtab-widget-error-not-available",
  slot,
}) {
  const handleErrorIntersection = useCallback(
    () => recordError(errorType),
    [recordError, errorType]
  );
  const errorRef = useIntersectionObserver(handleErrorIntersection);

  return (
    // role="alert" so a screen reader announces the failure when it appears,
    // since focus does not move.
    <div
      slot={slot}
      className={`widget-error${headerless ? " widget-error-titled" : ""}`}
      role="alert"
      ref={el => {
        errorRef.current = [el];
      }}
    >
      {/* A heading, so the card is still reachable by heading when the error
          draws the only name on it. */}
      <h2
        className={`newtab-widget-title widget-error-title${
          headerless ? "" : " sr-only"
        }`}
      >
        {title}
      </h2>
      <div className="widget-error-message">
        <span className="icon icon-info-warning" aria-hidden="true" />
        <p className="widget-error-text" data-l10n-id={l10nId} />
      </div>
    </div>
  );
}
