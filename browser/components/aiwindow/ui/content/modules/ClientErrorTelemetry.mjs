/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Shared vocabulary and helpers for the smart_window.client_error event.
 *
 * The event is recorded in the parent process by
 * SmartWindowTelemetry.recordClientError(), but the Smart Window UI that
 * reports failures runs in two documents: the chrome document, which can call
 * Glean directly, and the about:aichatcontent document, which lives in a
 * content process and has to relay through AIChatContentChild/Parent. Keeping
 * the key sets, the field extraction and the message normalization here means
 * both sides describe a failure the same way, whichever route it takes.
 */

const CLIENT_ERROR_EVENT = "AIChatContent:ClientError";

// Error objects already reported, so a single failure that is both explicitly
// captured (e.g. a markdown render error) and rethrown into the window
// "error"/"unhandledrejection" listeners is only emitted once.
const reportedErrors = new WeakSet();

/**
 * Reporting surfaces a failure can be attributed to. Each one is a place the
 * Smart Window UI installs a report hook: Lit's update cycle (recognised from
 * the stack), the markdown setHTML() call, the actor message path, and
 * everything else that reaches a window "error"/"unhandledrejection"
 * listener.
 */
export const CLIENT_ERROR_SOURCES = new Set([
  "lit-render",
  "markdown",
  "message-data",
  "uncaught",
]);

/**
 * Every message key the event can carry. Raw exception text is never
 * recorded, so a failure has to map onto one of:
 *   - a surface with a single obvious failure mode (lit_render_failed,
 *     markdown_render_failed, invalid_message_data, message_dispatch_failed)
 *   - the engine message texts we hit often enough to want split out
 *     (property_read_failure, not_a_function, not_iterable)
 *   - runtime_error for anything else. The error type is not lost, it stays
 *     in the event's `name` extra.
 */
export const CLIENT_ERROR_MESSAGES = new Set([
  "invalid_message_data",
  "lit_render_failed",
  "markdown_render_failed",
  "message_dispatch_failed",
  "not_a_function",
  "not_iterable",
  "property_read_failure",
  "runtime_error",
]);

const SOURCE_MESSAGE_KEYS = {
  "lit-render": "lit_render_failed",
  markdown: "markdown_render_failed",
  "message-data": "invalid_message_data",
};

/**
 * @param {unknown} value
 * @returns {string} The value when it is a string, "" otherwise.
 */
export function asString(value) {
  return typeof value === "string" ? value : "";
}

/**
 * Pull the event's fields off a thrown value. Anything can be thrown,
 * including plain strings and objects from another realm, so nothing here
 * assumes an Error. The property names match the event's extra keys so these
 * fields can travel from a content process to the parent without being
 * renamed on the way.
 *
 * @param {unknown} error
 * @returns {{name: string, message: string, filename: string, lineno: number}}
 */
export function extractClientErrorFields(error) {
  if (typeof error === "string") {
    return { name: "", message: error, filename: "", lineno: 0 };
  }
  if (!error || typeof error !== "object") {
    return { name: "", message: "", filename: "", lineno: 0 };
  }
  return {
    name: asString(error.name),
    message: asString(error.message),
    filename: asString(error.fileName),
    lineno: Number.isFinite(error.lineNumber) ? error.lineNumber : 0,
  };
}

/**
 * Resolve the stable message key for a failure. Raw exception text only ever
 * feeds the heuristics below, it is never returned.
 *
 * @param {object} options
 * @param {string} options.source
 *   A member of CLIENT_ERROR_SOURCES.
 * @param {string} [options.messageKey]
 *   Key the reporter already settled on, for failures whose meaning is only
 *   known where they were caught (the actor's message_dispatch_failed).
 * @param {string} [options.message]
 *   Raw exception message, matched against known engine texts.
 * @returns {string} A member of CLIENT_ERROR_MESSAGES.
 */
export function normalizeClientErrorMessage({ source, messageKey, message }) {
  const key = asString(messageKey);
  if (key) {
    if (CLIENT_ERROR_MESSAGES.has(key)) {
      return key;
    }
    console.warn(
      `ClientErrorTelemetry: unknown message key ${JSON.stringify(key)}, deriving one instead`
    );
  }

  if (SOURCE_MESSAGE_KEYS[source]) {
    return SOURCE_MESSAGE_KEYS[source];
  }

  const text = asString(message).toLowerCase();
  if (
    text.includes("cannot read properties of undefined") ||
    text.includes("cannot read properties of null") ||
    text.includes("can't access property")
  ) {
    return "property_read_failure";
  }
  if (text.includes("is not a function")) {
    return "not_a_function";
  }
  if (text.includes("is not iterable")) {
    return "not_iterable";
  }

  return "runtime_error";
}

/**
 * Classify a thrown value as a Lit render failure or a generic uncaught error
 * based on whether its stack passes through the Lit library.
 *
 * @param {unknown} error
 * @returns {"lit-render" | "uncaught"}
 */
export function classifyClientErrorSource(error) {
  return error &&
    typeof error.stack === "string" &&
    error.stack.includes("lit.all.mjs")
    ? "lit-render"
    : "uncaught";
}

/**
 * Report the failures that reach a window: uncaught exceptions and unhandled
 * rejections. Both Smart Window documents want these, but they report them
 * differently, so the caller supplies the reporter.
 *
 * @param {Window} target
 *   The window to listen on.
 * @param {Function} report
 *   Called with (error, source) for each failure.
 * @returns {Function} Removes both listeners.
 */
export function installClientErrorListeners(target, report) {
  const reportSafely = error => {
    try {
      report(error, classifyClientErrorSource(error));
    } catch (e) {
      // Never let reporting a failure cause another one.
      console.warn("Could not report Smart Window client error:", e);
    }
  };
  const onError = event => {
    // ErrorEvent shape: { error, message, filename, lineno }. When `error` is
    // null (cross-realm or stack-stripped), the event's own fields are all
    // there is to go on.
    reportSafely(
      event.error ?? {
        name: "Error",
        message: event.message ?? "",
        fileName: event.filename ?? "",
        lineNumber: event.lineno ?? 0,
      }
    );
  };
  const onUnhandledRejection = event => reportSafely(event.reason);

  target.addEventListener("error", onError);
  target.addEventListener("unhandledrejection", onUnhandledRejection);

  return () => {
    target.removeEventListener("error", onError);
    target.removeEventListener("unhandledrejection", onUnhandledRejection);
  };
}

/**
 * Build the detail payload sent up the AIChatContent:ClientError event chain.
 *
 * @param {unknown} error
 * @param {string} source
 *   A member of CLIENT_ERROR_SOURCES.
 * @param {string} [messageKey]
 *   A member of CLIENT_ERROR_MESSAGES, when the reporter knows what the
 *   failure means and does not want the parent to derive a key from the
 *   source or the message text.
 * @returns {{source: string, messageKey: string, name: string, message: string, filename: string, lineno: number}}
 */
export function serializeClientErrorDetail(error, source, messageKey = "") {
  return { source, messageKey, ...extractClientErrorFields(error) };
}

/**
 * Dispatch a client-error event from a target inside the AI Chat Content
 * document. Bubbles + composes through shadow roots so the actor's top-level
 * listener catches it.
 *
 * @param {EventTarget} target
 * @param {unknown} error
 * @param {string} source
 */
export function dispatchClientError(target, error, source) {
  if (error && typeof error === "object") {
    if (reportedErrors.has(error)) {
      return;
    }
    reportedErrors.add(error);
  }
  target.dispatchEvent(
    new CustomEvent(CLIENT_ERROR_EVENT, {
      bubbles: true,
      composed: true,
      detail: serializeClientErrorDetail(error, source),
    })
  );
}
