/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
});

/**
 * Module-level session registry. Keyed by sessionId.
 *
 * @type {Map<string, object>}
 */
const translationSessions = new Map();
let nextTranslationSessionId = 1;

function takeNextTranslationSessionId() {
  return `translation-session-${nextTranslationSessionId++}`;
}

/**
 * @returns {{
 *   promise: Promise<*>,
 *   resolve: (value: *) => void,
 *   reject: (error: *) => void,
 *   isSettled: () => boolean
 * }}
 */
export function createDeferred() {
  let settled = false;
  let resolveFn;
  let rejectFn;

  const promise = new Promise((resolve, reject) => {
    resolveFn = value => {
      if (!settled) {
        settled = true;
        resolve(value);
      }
    };
    rejectFn = error => {
      if (!settled) {
        settled = true;
        reject(error);
      }
    };
  });

  return {
    promise,
    resolve: resolveFn,
    reject: rejectFn,
    isSettled: () => settled,
  };
}

/**
 * @param {{ sourceLanguage?: *, targetLanguage?: * }} [pair]
 * @returns {{ sourceLanguage: string, targetLanguage: string }}
 */
export function normalizeLanguagePair(pair = {}) {
  const { sourceLanguage, targetLanguage } = pair;
  if (!sourceLanguage || !targetLanguage) {
    throw new Error(
      "Translations languagePair requires both sourceLanguage and targetLanguage"
    );
  }
  return {
    sourceLanguage: String(sourceLanguage),
    targetLanguage: String(targetLanguage),
  };
}

function attachPortHandlers(session) {
  const { port } = session;

  port.onmessage = event => {
    const data = event.data || {};
    switch (data.type) {
      case "TranslationsPort:GetEngineStatusResponse": {
        session.status = data.status;
        if (data.status === "ready") {
          session.ready.resolve({ status: data.status });
        } else {
          const error = new Error(
            data.error || "Translations engine failed to initialize"
          );
          session.ready.reject(error);
          failTranslationsSession(session, error);
        }
        break;
      }
      case "TranslationsPort:TranslationResponse": {
        const pending = session.pendingRequests.get(data.translationId);
        if (pending) {
          session.pendingRequests.delete(data.translationId);
          pending.resolve({
            translationId: data.translationId,
            targetText: data.targetText,
          });
        }
        break;
      }
      case "TranslationsPort:EngineTerminated": {
        failTranslationsSession(
          session,
          new Error("Translations engine terminated unexpectedly")
        );
        break;
      }
      default:
        console.error("Unknown translations port message", data);
    }
  };

  port.onmessageerror = () => {
    failTranslationsSession(
      session,
      new Error("Translations port failed to deserialize a message")
    );
  };
}

function failTranslationsSession(session, error) {
  cleanupTranslationSession(session, error);
  translationSessions.delete(session.id);
}

function cleanupTranslationSession(session, error) {
  if (session.port) {
    try {
      session.port.onmessage = null;
      session.port.onmessageerror = null;
      session.port.close();
    } catch (closeError) {
      console.error("Failed to close translations port", closeError);
    }
    session.port = null;
  }

  if (error) {
    if (!session.ready.isSettled()) {
      session.ready.reject(error);
    }
    for (const pending of session.pendingRequests.values()) {
      pending.reject(error);
    }
  } else {
    for (const pending of session.pendingRequests.values()) {
      pending.reject(new Error("Translations session was destroyed"));
    }
  }
  session.pendingRequests.clear();
}

/**
 * @param {object} [options]
 * @returns {Promise<{ sessionId: string, status: string, languagePair: object }>}
 */
export async function createTranslationsSession(options = {}) {
  const languagePair = normalizeLanguagePair(options.languagePair || options);
  const port =
    await lazy.TranslationsParent.requestTranslationsPort(languagePair);
  if (!port) {
    throw new Error("Failed to acquire a translations port from Firefox");
  }

  const sessionId = takeNextTranslationSessionId();
  const session = {
    id: sessionId,
    languagePair,
    port,
    ready: createDeferred(),
    status: "initializing",
    pendingRequests: new Map(),
    nextTranslationId: 1,
  };

  translationSessions.set(sessionId, session);
  attachPortHandlers(session);
  if (typeof port.start === "function") {
    port.start();
  }

  port.postMessage({ type: "TranslationsPort:GetEngineStatusRequest" });
  await session.ready.promise;

  return {
    sessionId,
    status: session.status,
    languagePair,
  };
}

/**
 * @param {string} sessionId
 * @param {object} [request]
 * @returns {Promise<{ sessionId: string, translationId: number, targetText: string, languagePair: object }>}
 */
export async function runTranslationsSession(sessionId, request = {}) {
  const session = translationSessions.get(sessionId);
  if (!session) {
    throw new Error(`Translations session not found: ${sessionId}`);
  }

  await session.ready.promise;

  const text =
    typeof request.text === "string"
      ? request.text
      : typeof request.sourceText === "string"
        ? request.sourceText
        : null;

  if (text === null) {
    throw new Error("Translations request missing text/sourceText field");
  }

  const translationId = session.nextTranslationId++;
  const pending = createDeferred();
  session.pendingRequests.set(translationId, pending);

  session.port.postMessage({
    type: "TranslationsPort:TranslationRequest",
    translationId,
    sourceText: text,
    isHTML: Boolean(request.isHTML || request.isHtml),
  });

  const response = await pending.promise;
  return {
    sessionId,
    translationId: response.translationId,
    targetText: response.targetText,
    languagePair: session.languagePair,
  };
}

/**
 * @param {string} sessionId
 * @param {{ discardTranslations?: boolean }} [options]
 * @returns {{ sessionId: string, destroyed: boolean }}
 */
export function destroyTranslationsSession(sessionId, options = {}) {
  const session = translationSessions.get(sessionId);
  if (!session) {
    return { sessionId, destroyed: false };
  }

  if (options.discardTranslations !== false && session.port) {
    session.port.postMessage({
      type: "TranslationsPort:DiscardTranslations",
    });
  }

  cleanupTranslationSession(session);
  translationSessions.delete(sessionId);

  return {
    sessionId,
    destroyed: true,
  };
}

/**
 * Test-only: clears any in-flight session state. Mochitests call this between
 * runs to avoid leaking sessions across `add_task` boundaries.
 */
export function _resetTranslationSessionsForTesting() {
  for (const session of translationSessions.values()) {
    cleanupTranslationSession(session);
  }
  translationSessions.clear();
  nextTranslationSessionId = 1;
}
