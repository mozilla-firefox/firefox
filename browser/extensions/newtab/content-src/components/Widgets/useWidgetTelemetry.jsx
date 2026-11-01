/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { useCallback, useContext, useEffect, useRef } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { SpaceActiveContext } from "content-src/lib/SpaceActiveContext";
import { WidgetLocationContext } from "./WidgetPlacement";

const IMPRESSION_THRESHOLD = 0.3;

/**
 * Shared telemetry hook for function-component widgets. Returns recorders
 * for the four widget telemetry actions (impression, user event, enabled,
 * error) so call sites don't hand-build payloads. `widget` is a
 * WIDGET_REGISTRY entry; `widget.telemetryName` becomes `widget_name`.
 *
 *   const {
 *     impressionRef,
 *     recordImpression,
 *     recordUserAction,
 *     recordEnabled,
 *     recordError,
 *   } = useWidgetTelemetry({ dispatch, widget, widgetSize });
 *
 *   <article ref={impressionRef}>...</article>
 *   recordUserAction("learn_more", { source: "context_menu" });
 *
 * Optional `widgetVariant` is sent as `widget_variant` on all four events, and
 * left off while it's undefined.
 *
 * Per-call options on `recordUserAction`: `value` (action_value), `size`
 * (overrides widgetSize), `alsoToMain: true` (routes via AlsoToMain), and
 * `legacy: true` (co-dispatches `legacyUserEventType` with `{ userAction }`).
 *
 * `recordImpression()` is a manual one-shot fire that shares the observer's
 * impressionFired guard; useful when a widget needs to record an impression
 * outside the IntersectionObserver path.
 *
 * Constructor `legacyImpressionTypes` (array) and `legacyUserEventType`
 * bridge the Bug 2012779 transition: while WIDGETS_TIMER_* / WIDGETS_LISTS_*
 * legacy events still exist alongside the unified events, FocusTimer and
 * Lists pass the matching legacy action types so the hook emits both. Both
 * co-dispatches fire legacy first, unified second.
 */
export const useWidgetTelemetry = ({
  dispatch,
  widget,
  widgetSize,
  widgetVariant,
  legacyImpressionTypes,
  legacyUserEventType,
}) => {
  const { telemetryName } = widget;

  const sizeRef = useRef(widgetSize);
  useEffect(() => {
    sizeRef.current = widgetSize;
  }, [widgetSize]);

  const variantRef = useRef(widgetVariant);
  useEffect(() => {
    variantRef.current = widgetVariant;
  }, [widgetVariant]);

  // Legacy bridge types are fixed per call site, so capture once at mount;
  // refs keep them out of the recorder callbacks' dependency arrays.
  const legacyImpressionTypesRef = useRef(legacyImpressionTypes);
  const legacyUserEventTypeRef = useRef(legacyUserEventType);

  const buildPayload = useCallback(
    ({ size, rest } = {}) => ({
      widget_name: telemetryName,
      widget_size: size ?? sizeRef.current,
      ...(variantRef.current !== undefined && {
        widget_variant: variantRef.current,
      }),
      ...rest,
    }),
    [telemetryName]
  );

  // Engagement events say where on the page this copy renders.
  const widgetLocation = useContext(WidgetLocationContext);
  const locationRef = useRef(null);
  locationRef.current = widgetLocation
    ? { widget_location: widgetLocation }
    : null;

  const impressionFired = useRef(false);
  const fireImpression = useCallback(
    size => {
      if (impressionFired.current) {
        return;
      }
      impressionFired.current = true;
      const data = buildPayload({ size, rest: locationRef.current });
      const legacyTypes = legacyImpressionTypesRef.current;
      if (legacyTypes && legacyTypes.length) {
        // Legacy first, then unified, matching the pre-hook dispatch order
        // in FocusTimer / Lists so existing tests don't need to flip.
        for (const type of legacyTypes) {
          dispatch(ac.AlsoToMain({ type }));
        }
        dispatch(ac.AlsoToMain({ type: at.WIDGETS_IMPRESSION, data }));
      } else {
        dispatch(ac.AlsoToMain({ type: at.WIDGETS_IMPRESSION, data }));
      }
    },
    [dispatch, buildPayload]
  );

  // The observer owns observation directly so the callback ref can attach to
  // elements that mount after the initial render (e.g. widgets that gate
  // rendering on a Redux pref or async data).
  const observerRef = useRef(null);
  const observedEl = useRef(null);
  const spaceActive = useContext(SpaceActiveContext);

  useEffect(() => {
    if (typeof IntersectionObserver === "undefined" || !spaceActive) {
      return undefined;
    }
    const observer = new IntersectionObserver(
      entries => {
        // Filter to the currently-observed element so a queued callback for a
        // previously-unobserved ref target doesn't fire a stale impression.
        if (
          entries.some(e => e.isIntersecting && e.target === observedEl.current)
        ) {
          fireImpression();
          observer.disconnect();
        }
      },
      { threshold: IMPRESSION_THRESHOLD }
    );
    observerRef.current = observer;
    if (observedEl.current && !impressionFired.current) {
      observer.observe(observedEl.current);
    }
    return () => {
      observer.disconnect();
      observerRef.current = null;
    };
  }, [fireImpression, spaceActive]);

  const impressionRef = useCallback(el => {
    if (observedEl.current === el) {
      return;
    }
    const observer = observerRef.current;
    if (observedEl.current && observer) {
      observer.unobserve(observedEl.current);
    }
    observedEl.current = el;
    if (el && observer && !impressionFired.current) {
      observer.observe(el);
    }
  }, []);

  const recordImpression = useCallback(
    ({ size } = {}) => {
      fireImpression(size);
    },
    [fireImpression]
  );

  const recordUserAction = useCallback(
    (userAction, { source, value, size, alsoToMain, legacy } = {}) => {
      const route = alsoToMain ? ac.AlsoToMain : ac.OnlyToMain;
      const rest = {
        widget_source: source,
        user_action: userAction,
        ...locationRef.current,
      };
      if (value !== undefined) {
        rest.action_value = value;
      }
      const data = buildPayload({ size, rest });
      const main = route({ type: at.WIDGETS_USER_EVENT, data });
      const legacyType = legacy ? legacyUserEventTypeRef.current : null;
      if (legacyType) {
        // Legacy first, then unified, matching the pre-hook dispatch order
        // in FocusTimer / Lists so existing tests don't need to flip.
        const legacyAction = route({
          type: legacyType,
          data: { userAction },
        });
        dispatch(legacyAction);
        dispatch(main);
      } else {
        dispatch(main);
      }
    },
    [dispatch, buildPayload]
  );

  const recordEnabled = useCallback(
    (enabled, { source, size } = {}) => {
      const data = buildPayload({
        size,
        rest: { widget_source: source, enabled },
      });
      dispatch(ac.OnlyToMain({ type: at.WIDGETS_ENABLED, data }));
    },
    [dispatch, buildPayload]
  );

  const recordError = useCallback(
    (errorType, { size } = {}) => {
      const data = buildPayload({ size, rest: { error_type: errorType } });
      dispatch(ac.AlsoToMain({ type: at.WIDGETS_ERROR, data }));
    },
    [dispatch, buildPayload]
  );

  return {
    impressionRef,
    recordImpression,
    recordUserAction,
    recordEnabled,
    recordError,
  };
};
