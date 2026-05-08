/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
});

/**
 * Engines created via this runner are tracked here so callers can refer to
 * them by `engineId`. MLEngine.getInstance is module-private in MC, so we
 * keep our own registry instead of reaching into MLEngineParent internals.
 *
 * @type {Map<string, object>}
 */
const engines = new Map();

/**
 * Realm-agnostic check for built-in tagged objects (Map, Set, ArrayBuffer).
 * `instanceof` is unreliable when the value crosses sandbox boundaries.
 */
function tag(value) {
  return Object.prototype.toString.call(value);
}

/**
 * Convert a value into something that survives structured-clone over Marionette
 * or postMessage. Binary buffers are projected to plain arrays so consumers in
 * Python land can deserialize without bespoke handling.
 *
 * @param {*} value
 * @returns {*}
 */
export function serializeEntry(value) {
  if (value === null || value === undefined) {
    return value;
  }
  const t = tag(value);
  if (t === "[object ArrayBuffer]") {
    return Array.from(new Uint8Array(value));
  }
  if (ArrayBuffer.isView(value)) {
    return Array.from(/** @type {Uint8Array} */ (value));
  }
  if (Array.isArray(value)) {
    return value.map(serializeEntry);
  }
  if (t === "[object Map]") {
    const out = {};
    for (const [k, v] of value.entries()) {
      out[String(k)] = serializeEntry(v);
    }
    return out;
  }
  if (t === "[object Set]") {
    return Array.from(value, serializeEntry);
  }
  if (typeof value === "object") {
    const out = {};
    for (const [k, v] of Object.entries(value)) {
      out[k] = serializeEntry(v);
    }
    return out;
  }
  return value;
}

/**
 * Make an inference response uniformly shaped for consumers.
 *
 *   - Scalars round-trip unchanged.
 *   - Single objects are returned as-is (after entry-serialization).
 *   - Arrays are mapped element-by-element.
 *   - `metrics` (if present) is recursively serialized so PerformanceEntry-like
 *     objects don't trip structured-clone.
 *
 * @param {*} response
 * @returns {*}
 */
export function serializeInferenceResponse(response) {
  if (response === null || response === undefined) {
    return response;
  }
  if (typeof response !== "object") {
    return response;
  }
  if (Array.isArray(response)) {
    return response.map(serializeEntry);
  }
  return serializeEntry(response);
}

/**
 * @param {string} engineId
 * @returns {object}
 */
export function getExistingEngine(engineId) {
  if (!engineId) {
    throw new Error("engineId is required");
  }
  const engine = engines.get(engineId);
  if (!engine) {
    throw new Error(`Engine ${engineId} not found`);
  }
  return engine;
}

/**
 * @param {object} [options]
 * @returns {Promise<{ engineId: string, status: string }>}
 */
export async function createMlEngine(options = {}) {
  const engine = await lazy.createEngine(options);
  engines.set(engine.engineId, engine);
  return {
    engineId: engine.engineId,
    status: engine.engineStatus,
  };
}

/**
 * @param {string} engineId
 * @param {object} [request]
 * @returns {Promise<*>}
 */
export async function runMlEngine(engineId, request = {}) {
  const engine = getExistingEngine(engineId);
  const response = await engine.run(request);
  return serializeInferenceResponse(response);
}

/**
 * @param {string} engineId
 * @param {{ shutdown?: boolean }} [options]
 * @returns {Promise<{ engineId: string, status: string }>}
 */
export async function destroyMlEngine(engineId, { shutdown = true } = {}) {
  const engine = getExistingEngine(engineId);
  await engine.terminate(shutdown, false);
  engines.delete(engineId);
  return { engineId, status: engine.engineStatus };
}

/**
 * Test-only: clear the engine registry between mochitest add_task runs.
 */
export function _resetEnginesForTesting() {
  engines.clear();
}
