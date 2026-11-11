/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import { createEngine } from "chrome://global/content/ml/EngineProcess.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
});

export const INTENT_MODEL_ENGLISH_FRENCH =
  "mozilla/query-intent-detection-en-fr";
export const DEFAULT_INTENT_MODEL = "mozilla/mobilebert-query-intent-detection";

const INTENT_FEATURE = "smart-intent";
const INTENT_FEATURE_ENGLISH_FRENCH = "smart-intent-en-fr";

const FORCED_CHAT_PHRASES = [
  "amuse me",
  "are we alone",
  "are you alive",
  "are you gpt",
  "are you human",
  "are you real",
  "bark like dog",
  "cheer me up",
  "comfort me",
  "count numbers",
  "curse me",
  "do aliens exist",
  "do we matter",
  "do you dream",
  "do you think",
  "does fate exist",
  "dream meaning",
  "drop wisdom",
  "encourage me",
  "entertain me",
  "explain yourself",
  "flip coin",
  "give blessing",
  "give wisdom",
  "good morning",
  "good night",
  "guess number",
  "hallo",
  "hello",
  "hey",
  "hi",
  "hola",
  "how are you",
  "inspire me",
  "invent a word",
  "invent holiday",
  "invent joke",
  "is god real",
  "life advice",
  "life purpose",
  "list animals",
  "list capitals",
  "list colors",
  "list countries",
  "list elements",
  "list fruits",
  "list metals",
  "list oceans",
  "list planets",
  "list shapes",
  "meaning of life",
  "meow like cat",
  "motivate me",
  "now you are",
  "play a game",
  "pretend alien",
  "pretend child",
  "pretend detective",
  "pretend ghost",
  "pretend pirate",
  "pretend robot",
  "pretend superhero",
  "pretend teacher",
  "pretend wizard",
  "random fact",
  "random number",
  "roll dice",
  "goodbye",
  "simulate chat",
  "simulate future",
  "simulate past",
  "sing like robot",
  "sing lullaby",
  "sing rap",
  "sup",
  "surprise me",
  "teach me",
  "tell bedtime story",
  "tell fortune",
  "tell joke",
  "tell prophecy",
  "tell riddle",
  "tell story",
  "what is art",
  "what is beauty",
  "what is death",
  "what is freedom",
  "what is justice",
  "what is love",
  "what is mind",
  "what is reality",
  "what is right",
  "what is self",
  "what is soul",
  "what is time",
  "what is truth",
  "what is wrong",
  "what model are you",
  "what version",
  "what’s up",
  "which model are you",
  "who am i",
  "who are you",
  "who made you",
  "why are we",
  "write a poem",
  "write a song",
  "write haiku",
  "write quote",
  "your model is",
];

// Short forced-chat allowlist for French. Appended to FORCED_CHAT_PHRASES when
// the French/English intent model is in use.
const FRENCH_FORCED_CHAT_PHRASES = [
  "amuse-moi",
  "au revoir",
  "bonjour",
  "bonne nuit",
  "bonsoir",
  "comment ça va",
  "comment vas-tu",
  "coucou",
  "donne un conseil",
  "écris un poème",
  "écris une chanson",
  "es-tu humain",
  "es-tu réel",
  "es-tu une ia",
  "le sens de la vie",
  "motive-moi",
  "quel modèle es-tu",
  "qui es-tu",
  "raconte une blague",
  "raconte une histoire",
  "salut",
  "surprends-moi",
  "ça va",
];

export function normalizeTextForChatAllowlist(s) {
  // Fold diacritics (NFD + strip combining marks) so accent-insensitive input
  // still matches, e.g. "ecris un poeme" matches "écris un poème". French input
  // is frequently typed without accents.
  return s
    .normalize("NFKC")
    .toLowerCase()
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .replace(/\s+/g, " ")
    .trim();
}

// Split on non-word chars; letters/numbers/_ are "word" characters
export function tokenizeTextForChatAllowlist(s) {
  return normalizeTextForChatAllowlist(s)
    .split(/[^\p{L}\p{N}_]+/u)
    .filter(Boolean);
}

export function buildChatAllowlist(phrases) {
  const byLen = new Map(); // len -> Set("tok tok ...")
  for (const p of phrases) {
    const key = tokenizeTextForChatAllowlist(p).join(" ");
    if (!key) {
      continue;
    }
    const k = key.split(" ").length;
    if (!byLen.has(k)) {
      byLen.set(k, new Set());
    }
    byLen.get(k).add(key);
  }
  return byLen;
}

// Factory: returns a fast checker for “does query contain any isolated phrase?”
export function makeIsolatedPhraseChecker(phrases) {
  const byLen = buildChatAllowlist(phrases);
  const cache = new Map();

  return function containsIsolatedPhrase(query) {
    const qNorm = normalizeTextForChatAllowlist(query);
    if (cache.has(qNorm)) {
      return cache.get(qNorm);
    }

    const toks = qNorm.split(/[^\p{L}\p{N}_]+/u).filter(Boolean);
    for (const [k, set] of byLen) {
      for (let i = 0; i + k <= toks.length; i++) {
        if (set.has(toks.slice(i, i + k).join(" "))) {
          cache.set(qNorm, true);
          return true;
        }
      }
    }
    cache.set(qNorm, false);
    return false;
  };
}

/**
 * Returns the intent model and its feature id appropriate for the current
 * region, falling back to the default (English) model.
 *
 * @returns {{ modelId: string, featureId: string }}
 */
function getIntentModelInfoForLocale() {
  if (lazy.Region.home == "FR") {
    return {
      modelId: INTENT_MODEL_ENGLISH_FRENCH,
      featureId: INTENT_FEATURE_ENGLISH_FRENCH,
    };
  }
  return {
    modelId: DEFAULT_INTENT_MODEL,
    featureId: INTENT_FEATURE,
  };
}

/**
 * Returns the forced-chat phrase list for a given intent model. The French/
 * English model gets the French phrases appended to the English ones.
 *
 * @param {string} modelId
 * @returns {string[]}
 */
function getForcedChatPhrasesForModel(modelId) {
  if (modelId === INTENT_MODEL_ENGLISH_FRENCH) {
    return [...FORCED_CHAT_PHRASES, ...FRENCH_FORCED_CHAT_PHRASES];
  }
  return FORCED_CHAT_PHRASES;
}

/**
 * Intent Classifier Engine
 */
export const IntentClassifier = {
  /**
   * Exposing createEngine for testing purposes.
   */
  _createEngine: createEngine,

  /**
   * Smart bar pin from the smartWindow feature. "search" or "chat" skips the
   * classifier. Null or empty keeps it. Nimbus enforces the enum.
   */
  get smartbarIntent() {
    return lazy.NimbusFeatures.smartWindow.getVariable("smartbarIntent");
  },

  /**
   * Lazily-built forced-chat checkers, keyed by model id so each locale's
   * allowlist is only compiled once. Kept as a property for easy stubbing.
   */
  _forcedChatCheckers: new Map(),

  /**
   * Returns whether the query is on the forced-chat allowlist for the model.
   *
   * @param {string} query
   * @param {string} modelId
   * @returns {boolean}
   */
  _isForcedChat(query, modelId) {
    let checker = this._forcedChatCheckers.get(modelId);
    if (!checker) {
      checker = makeIsolatedPhraseChecker(
        getForcedChatPhrasesForModel(modelId)
      );
      this._forcedChatCheckers.set(modelId, checker);
    }
    return checker(query);
  },

  /**
   * Gets the intent of the prompt using a text classification model.
   *
   * @param {string} query
   * @returns {string} "search" | "chat"
   */
  async getPromptIntent(query) {
    if (this.smartbarIntent) {
      return this.smartbarIntent;
    }
    try {
      const modelInfo = getIntentModelInfoForLocale();
      const cleanedQuery = this._preprocessQuery(query);
      if (this._isForcedChat(cleanedQuery, modelInfo.modelId)) {
        return "chat";
      }
      const engine = await this._createEngine({
        backend: "best-onnx",
        ...modelInfo,
        taskName: "text-classification",
      });
      const threshold = 0.8;
      const resp = await engine.run({ args: [[cleanedQuery]] });
      // resp example: [{ label: "chat", score: 0.05 }, { label: "search", score: 0.94 }]
      if (
        resp[0].label.toLowerCase() === "search" &&
        resp[0].score >= threshold
      ) {
        return "search";
      }
      return "chat";
    } catch (error) {
      console.error("Error using intent detection model:", error);
      throw error;
    }
  },

  // Helper function for preprocessing text input
  _preprocessQuery(query) {
    if (typeof query !== "string") {
      throw new TypeError(
        `Expected a string for query preprocessing, but received ${typeof query}`
      );
    }
    return query.replace(/\?/g, "").trim();
  },
};
