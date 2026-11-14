/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/**
 * MLSuggest helps with ML based suggestions around intents and location.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

/**
 * @import {
 *   EngineRequests,
 *   EngineResponses,
 *   EngineFeatureIds,
 *   EngineCreateOptions,
 *   EngineOptions
 * } from "../../../../toolkit/components/ml/ml.d.ts"
 * @import { MLEngine } from "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs"
 * @typedef {Awaited<ReturnType<typeof lazy.createEngine>>} MLEngine
 */

// List of prepositions used in subject cleaning.
const PREPOSITIONS = ["in", "at", "on", "for", "to", "near"];

const MAX_QUERY_LENGTH = 200;
const NAME_PUNCTUATION = [".", "-", "'"];
const NAME_PUNCTUATION_EXCEPT_DOT = NAME_PUNCTUATION.filter(p => p !== ".");

/**
 * Class for handling ML-based suggestions using intent and NER models.
 *
 * @class
 */
class _MLSuggest {
  /**
   * @type {Partial<{ [K in EngineFeatureIds]: MLEngine<K> }>}
   */
  #modelEngines = {};

  INTENT_OPTIONS = {
    taskName: /** @type {const} */ ("text-classification"),
    featureId: /** @type {const} */ ("suggest-intent-classification"),
    numThreads: 2,
    backend: "best-onnx",
  };

  NER_OPTIONS = {
    taskName: /** @type {const} */ ("token-classification"),
    featureId: /** @type {const} */ ("suggest-NER"),
    numThreads: 2,
    backend: "best-onnx",
  };

  /**
   * Helper to wrap createEngine for testing purposes.
   *
   * @template {EngineFeatureIds} FeatureId
   *
   * @param {EngineOptions<FeatureId>} options
   *   Configuration options for the ML engine.
   */
  createEngine(options) {
    return lazy.createEngine(options);
  }

  /**
   * Initializes the intent and NER models.
   */
  async initialize() {
    await Promise.all([
      this.#initializeModelEngine(this.INTENT_OPTIONS),
      this.#initializeModelEngine(this.NER_OPTIONS),
    ]);
  }

  /**
   * @typedef {object} MLSuggestResult
   * @property {string} intent
   *   The predicted intent label of the query. Possible values include:
   *   - 'information_intent': For queries seeking general information.
   *   - 'yelp_intent': For queries related to local businesses or services.
   *   - 'navigation_intent': For queries with navigation-related actions.
   *   - 'travel_intent': For queries showing travel-related interests.
   *   - 'purchase_intent': For queries with purchase or shopping intent.
   *   - 'weather_intent': For queries asking about weather or forecasts.
   *   - 'translation_intent': For queries seeking translations.
   *   - 'unknown': When the intent cannot be classified with confidence.
   *   - '' (empty string): Returned when model probabilities for all intents
   *     are below the intent threshold.
   * @property {?{city: ?string, state: ?string}} location
   *   The detected location from the query.
   * @property {string} subject
   *   The subject of the query after location is removed.
   * @property {{intent: object, ner: object}} metrics
   *   The combined metrics from NER model results, representing additional
   *   information about the model's performance.
   */

  /**
   * Generates ML-based suggestions by finding intent, detecting entities, and
   * combining locations.
   *
   * @param {string} query
   *   The user's input query.
   * @returns {Promise<?MLSuggestResult>}
   *   The suggestion result including intent, location, and subject, or null if
   *   an error occurs or query length > MAX_QUERY_LENGTH
   */
  async makeSuggestions(query) {
    // avoid bunch of work for very long strings
    if (query.length > MAX_QUERY_LENGTH) {
      return null;
    }

    let intentRes, nerResult;
    try {
      [intentRes, nerResult] = await Promise.all([
        this._findIntent(query),
        this._findNER(query),
      ]);
    } catch (error) {
      return null;
    }

    if (!intentRes || !nerResult) {
      return null;
    }

    const locationResVal = this.#combineLocations(
      nerResult,
      lazy.UrlbarPrefs.get("nerThreshold")
    );

    const intentLabel = this.#applyIntentThreshold(
      intentRes,
      lazy.UrlbarPrefs.get("intentThreshold")
    );

    return {
      intent: intentLabel,
      location: locationResVal,
      subject: this.#findSubjectFromQuery(query, locationResVal),
      metrics: { intent: intentRes.metrics, ner: nerResult.metrics },
    };
  }

  /**
   * Shuts down all initialized engines.
   */
  async shutdown() {
    for (const [key, engine] of Object.entries(this.#modelEngines)) {
      try {
        await engine.terminate?.();
      } finally {
        // Remove each engine after termination
        delete this.#modelEngines[key];
      }
    }
  }

  /**
   * Initializes a engine model.
   *
   * @template {EngineFeatureIds} FeatureId
   *
   * @param {EngineCreateOptions[FeatureId]} options
   *   Configuration options for the ML engine.
   * @returns {Promise<?MLEngine<any>>}
   *   The engine, or null if it could not be created.
   */
  async #initializeModelEngine(options) {
    /** @type {EngineFeatureIds} */
    const featureId = options.featureId;

    // uses cache if engine was used
    let engine = this.#modelEngines[featureId];
    if (engine) {
      return engine;
    }

    try {
      engine = await this.createEngine(options);
    } catch (e) {
      // Engine creation is best effort: "best-onnx" already covers falling
      // back to the wasm backend, so anything reaching here (no model, not
      // enough memory, crashed inference process) leaves us without
      // suggestions rather than being fatal for the caller.
      console.error(`Failed to create the ${featureId} engine: ${e}`);
      return null;
    }

    // Cache the engine. Cast the featureId to "any" here since there's not an easy
    // way to assert
    this.#modelEngines[featureId] = /** @type {MLEngine<any>} */ (engine);
    return engine;
  }

  /**
   * Finds the intent of the query using the intent classification model.
   * (This has been made public to enable testing)
   *
   * @param {string} query
   *   The user's input query.
   * @param {EngineRequests["suggest-intent-classification"]["options"]} [options]
   *   The options for the engine pipeline
   */
  async _findIntent(query, options = {}) {
    const engineIntentClassifier =
      this.#modelEngines[this.INTENT_OPTIONS.featureId];
    if (!engineIntentClassifier) {
      return null;
    }

    /** @type {Awaited<ReturnType<typeof engineIntentClassifier["run"]>>} */
    let res;
    try {
      res = await engineIntentClassifier.run({
        args: [query],
        options,
      });
    } catch (error) {
      // The engine could have timed out or crashed, so drop it from the cache
      // and recreate it for the next query.
      delete this.#modelEngines[this.INTENT_OPTIONS.featureId];
      this.#initializeModelEngine(this.INTENT_OPTIONS);
      return null;
    }
    return res;
  }

  /**
   * Finds named entities in the query using the NER model.
   * (This has been made public to enable testing)
   *
   * @param {string} query
   *   The user's input query.
   * @param {EngineRequests["suggest-intent-classification"]["options"]} options
   *   The options for the engine pipeline
   */
  async _findNER(query, options = {}) {
    const engineNER = this.#modelEngines[this.NER_OPTIONS.featureId];
    try {
      return await engineNER?.run({ args: [query], options });
    } catch (error) {
      // The engine could have timed out or crashed, so drop it from the cache
      // and recreate it for the next query.
      delete this.#modelEngines[this.NER_OPTIONS.featureId];
      this.#initializeModelEngine(this.NER_OPTIONS);
      return null;
    }
  }

  /**
   * Applies a confidence threshold to determine the intent label.
   *
   * If the highest-scoring intent in the result exceeds the threshold, its label
   * is returned; otherwise, the label defaults to 'unknown'.
   *
   * @param {EngineResponses["suggest-intent-classification"]} intentResult
   *   The result of the intent classification model, where each item includes
   *   a `label` and `score`.
   * @param {number} intentThreshold
   *   The confidence threshold for accepting the intent label.
   * @returns {string}
   *   The determined intent label or 'unknown' if the threshold is not met.
   */
  #applyIntentThreshold(intentResult, intentThreshold) {
    return intentResult[0]?.score > intentThreshold
      ? intentResult[0].label
      : "";
  }

  /**
   * Combines location tokens detected by NER into separate city and state
   * components. This method processes city, state, and combined city-state
   * entities, returning an object with `city` and `state` fields.
   *
   * Handles the following entity types:
   * - B-CITY, I-CITY: Identifies city tokens.
   * - B-STATE, I-STATE: Identifies state tokens.
   * - B-CITYSTATE, I-CITYSTATE: Identifies tokens that represent a combined
   *   city and state.
   *
   * @param {EngineResponses["suggest-NER"]} nerResult
   *   The NER results containing tokens and their corresponding entity labels.
   * @param {number} nerThreshold
   *   The confidence threshold for including entities. Tokens with a confidence
   *   score below this threshold will be ignored.
   */
  #combineLocations(nerResult, nerThreshold) {
    let cityResult = [];
    let stateResult = [];
    let cityStateResult = [];

    for (let i = 0; i < nerResult.length; i++) {
      const res = nerResult[i];
      if (res.entity === "B-CITY" || res.entity === "I-CITY") {
        this.#processNERToken(res, cityResult, nerThreshold);
      } else if (res.entity === "B-STATE" || res.entity === "I-STATE") {
        this.#processNERToken(res, stateResult, nerThreshold);
      } else if (res.entity === "B-CITYSTATE" || res.entity === "I-CITYSTATE") {
        this.#processNERToken(res, cityStateResult, nerThreshold);
      }
    }

    // Handle city_state as combined and split into city and state
    if (cityStateResult.length && !cityResult.length && !stateResult.length) {
      let cityStateSplit = cityStateResult.join(" ").split(",");
      cityResult =
        cityStateSplit[0]
          ?.trim?.()
          .split(",")
          .filter(item => item.trim() !== "") || [];
      stateResult =
        cityStateSplit[1]
          ?.trim?.()
          .split(",")
          .filter(item => item.trim() !== "") || [];
    }

    // Remove trailing punctuation from the last cityResult element if present
    this.#removePunctFromEndIfPresent(cityResult);
    this.#removePunctFromEndIfPresent(stateResult);

    // Return city and state as separate components if detected
    return {
      city: cityResult.join(" ").trim() || null,
      state: stateResult.join(" ").trim() || null,
    };
  }

  /**
   * Processes a token from the NER results, appending it to the provided result
   * array while handling wordpieces (e.g., "##"), punctuation, and
   * multi-token entities.
   *
   * - Appends wordpieces (starting with "##") to the last token in the array.
   * - Handles punctuation tokens like ".", "-", or "'".
   * - Ensures continuity for entities split across multiple tokens.
   *
   * @param {EngineResponses["suggest-NER"][number]} res
   *   The NER result token to process. Should include:
   *   - {string} word: The word or token from the NER output.
   *   - {number} score: The confidence score for the token.
   *   - {string} entity: The entity type label (e.g., "B-CITY", "I-STATE").
   * @param {string[]} resultArray
   *   The array to append the processed token. Typically `cityResult`,
   *   `stateResult`, or `cityStateResult`.
   * @param {number} nerThreshold
   *   The confidence threshold for including tokens. Tokens with a score below
   *   this threshold will be ignored.
   */
  #processNERToken(res, resultArray, nerThreshold) {
    // Skip low-confidence tokens
    if (res.score <= nerThreshold) {
      return;
    }

    const lastTokenIndex = resultArray.length - 1;
    // "##" prefix indicates that a token is continuation of a word
    // rather than a start of a new word.
    // reference -> https://github.com/google-research/bert/blob/master/tokenization.py#L314-L316
    if (res.word.startsWith("##") && resultArray.length) {
      resultArray[lastTokenIndex] += res.word.slice(2);
    } else if (
      resultArray.length &&
      (NAME_PUNCTUATION.includes(res.word) ||
        NAME_PUNCTUATION_EXCEPT_DOT.includes(
          resultArray[lastTokenIndex].slice(-1)
        ))
    ) {
      // Special handling for punctuation like ".", "-", or "'"
      resultArray[lastTokenIndex] += res.word;
    } else {
      resultArray.push(res.word);
    }
  }

  /**
   * Removes trailing punctuation from the last element in the result array
   * if the last character matches any punctuation in `NAME_PUNCTUATION`.
   *
   * This method is useful for cleaning up city or state tokens that may
   * contain unwanted punctuation after processing NER results.
   *
   * @param {string[]} resultArray
   *   An array of strings representing detected entities (e.g., cities or states).
   *   The array is modified in place if the last element ends with punctuation.
   */
  #removePunctFromEndIfPresent(resultArray) {
    const lastTokenIndex = resultArray.length - 1;
    if (
      resultArray.length &&
      NAME_PUNCTUATION.includes(resultArray[lastTokenIndex].slice(-1))
    ) {
      resultArray[lastTokenIndex] = resultArray[lastTokenIndex].slice(0, -1);
    }
  }

  /**
   * Finds the subject from the query, removing the city and location words.
   *
   * @param {string} query
   * @param {{city: ?string, state: ?string}} location
   */
  #findSubjectFromQuery(query, location) {
    // If location is null or no city/state, return the entire query
    if (!location || (!location.city && !location.state)) {
      return query;
    }

    // Remove the city and state values from the query
    let locValues = Object.values(location)
      .map(loc => loc?.replace(/\W+/g, " "))
      .filter(loc => loc?.trim());

    // Regular expression to remove locations
    // This handles single & multi-worded cities/states
    let locPattern = locValues.map(loc => `\\b${loc}\\b`).join("|");
    let locRegex = new RegExp(locPattern, "g");

    // Remove locations, trim whitespace, and split words
    let words = query
      .replace(/\W+/g, " ")
      .replace(locRegex, "")
      .split(/\W+/)
      .filter(word => !!word.length);

    let subjectWords = this.#cleanSubject(words);
    return subjectWords.join(" ");
  }

  /**
   * Remove trailing prepositions from the list of words
   *
   * @param {string[]} words
   */
  #cleanSubject(words) {
    while (words.length && PREPOSITIONS.includes(words[words.length - 1])) {
      words.pop();
    }
    return words;
  }
}

// Export the singleton instance
export var MLSuggest = new _MLSuggest();
