/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useCallback, useEffect, useMemo, useRef, useState } from "react";

// Protocol v1 rules from the framed-widgets host integration doc.
const TOKEN = /^[a-z][a-z0-9_]{0,63}$/;
const VALUE = /^[a-z0-9][a-z0-9_]{0,31}$/;
// RegExp#test coerces its argument, so undefined would pass as "undefined".
const isToken = x => typeof x === "string" && TOKEN.test(x);
const isValue = x => typeof x === "string" && VALUE.test(x);
const onlyKeys = (payload, keys) =>
  Object.keys(payload).every(key => keys.includes(key));

// Payload checks by message type. Messages of any other type, or that fail
// their check, are dropped.
const PAYLOAD_VALIDATORS = {
  widget_ready: p =>
    onlyKeys(p, ["protocol", "variant"]) &&
    Number.isInteger(p.protocol) &&
    (p.variant === undefined || isValue(p.variant)),
  widget_error: p =>
    onlyKeys(p, ["reason", "terminal"]) &&
    isToken(p.reason) &&
    typeof p.terminal === "boolean",
  action: (p, message) => isToken(message),
};

// Without widget_ready by then, the widget is treated as failed. The iframe
// itself always loads, even when its URL is wrong, so widget_ready is the
// only sign that the widget's document works.
export const FRAMED_WIDGET_LOAD_TIMEOUT_MS = 12000;

/**
 * Calls onMessage for every valid message a framed widget's document posts.
 * The frame posts with targetOrigin "*" (about:newtab is opaque), so a message
 * only counts when it comes from the frame's window and origin on the
 * widget's channel, `<telemetryName>_widget`.
 *
 * @param {object} options
 * @param {object} options.widget - a WIDGET_REGISTRY entry
 * @param {string} options.src - URL of the widget's document
 * @param {object} options.iframeRef - ref to the widget's iframe
 * @param {Function} options.onMessage - called with (type, message, payload)
 */
export function useFramedWidgetMessages({ widget, src, iframeRef, onMessage }) {
  const onMessageRef = useRef(onMessage);
  onMessageRef.current = onMessage;
  const channel = `${widget.telemetryName}_widget`;
  const frameOrigin = useMemo(() => {
    try {
      return new URL(src).origin;
    } catch {
      return null;
    }
  }, [src]);

  useEffect(() => {
    if (!frameOrigin) {
      return undefined;
    }
    function handleMessage(event) {
      const { data } = event;
      if (
        event.origin !== frameOrigin ||
        event.source !== iframeRef.current?.contentWindow ||
        data?.channel !== channel ||
        typeof data.payload !== "object" ||
        data.payload === null ||
        !PAYLOAD_VALIDATORS[data.type]?.(data.payload, data.message)
      ) {
        return;
      }
      onMessageRef.current(data.type, data.message, data.payload);
    }
    window.addEventListener("message", handleMessage);
    return () => window.removeEventListener("message", handleMessage);
  }, [frameOrigin, channel, iframeRef]);
}

/**
 * Returns whether a framed widget's document has sent widget_ready, so the
 * frame can stay hidden until it has. Calls onError with (reason, terminal)
 * for widget_errors, and with ("load_timeout", true) when widget_ready doesn't
 * arrive in time. So a frame can't flood telemetry, each load reports a
 * non-terminal reason once and nothing after its first terminal error. A
 * terminal error means the widget is unusable.
 *
 * @param {object} options - see useFramedWidgetMessages, without onMessage
 * @param {Function} options.onError - called with (reason, terminal)
 * @returns {boolean}
 */
export function useFramedWidgetReady({ widget, src, iframeRef, onError }) {
  const [ready, setReady] = useState(false);
  const onErrorRef = useRef(onError);
  onErrorRef.current = onError;
  const [reportedReasons] = useState(() => new Set());
  const failedRef = useRef(false);

  useEffect(() => {
    setReady(false);
    reportedReasons.clear();
    failedRef.current = false;
  }, [src, reportedReasons]);

  useEffect(() => {
    // Without a document there is nothing to wait for.
    if (ready || !src) {
      return undefined;
    }
    const timer = setTimeout(() => {
      if (!failedRef.current) {
        failedRef.current = true;
        onErrorRef.current("load_timeout", true);
      }
    }, FRAMED_WIDGET_LOAD_TIMEOUT_MS);
    return () => clearTimeout(timer);
  }, [src, ready]);

  useFramedWidgetMessages({
    widget,
    src,
    iframeRef,
    onMessage(type, message, payload) {
      if (type === "widget_ready") {
        setReady(true);
      } else if (type === "widget_error") {
        if (
          failedRef.current ||
          (!payload.terminal && reportedReasons.has(payload.reason))
        ) {
          return;
        }
        reportedReasons.add(payload.reason);
        failedRef.current = payload.terminal;
        onErrorRef.current(payload.reason, payload.terminal);
      }
    },
  });

  return ready;
}

/**
 * Returns the size a framed widget's document last asked for with a
 * `change_size` action (or null), and a function that drops it. Only the
 * widget's validSizes are accepted. The request is never written to the
 * widget's size pref. It is dropped when the widget's URL or its own size
 * changes; call clearRequestedSize when the user picks a size, so their
 * choice wins even when it matches the size pref.
 *
 * @param {object} options - see useFramedWidgetMessages, without onMessage
 * @param {string} options.widgetSize - the widget's size from its prefs
 * @param {Function} [options.onAction] - called with (message, payload) for
 *   every action, change_size included
 * @returns {{requestedSize: string|null, clearRequestedSize: Function}}
 */
export function useFramedWidgetSize({
  widget,
  src,
  iframeRef,
  widgetSize,
  onAction,
}) {
  const [requestedSize, setRequestedSize] = useState(null);

  useEffect(() => setRequestedSize(null), [src, widgetSize]);

  useFramedWidgetMessages({
    widget,
    src,
    iframeRef,
    onMessage(type, message, payload) {
      if (type !== "action") {
        return;
      }
      onAction?.(message, payload);
      if (
        message === "change_size" &&
        onlyKeys(payload, ["size"]) &&
        widget.validSizes.includes(payload.size)
      ) {
        setRequestedSize(payload.size);
      }
    },
  });

  const clearRequestedSize = useCallback(() => setRequestedSize(null), []);

  return { requestedSize, clearRequestedSize };
}
