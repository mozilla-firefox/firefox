/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// On average, each token represents about 4 characters. A factor of 3.5 is used
// instead of 4 to account for edge cases.
const CHARACTERS_PER_TOKEN = 3.5;
// On average, one token corresponds to approximately 4 characters, meaning 0.25
// times the character count would suffice under normal conditions. To ensure
// robustness and handle edge cases, we use a more conservative factor of 0.69.
const CONTEXT_SIZE_MULTIPLIER = 0.69;
const DEFAULT_INPUT_SENTENCES = 6;
const MIN_SENTENCE_LENGTH = 14;
const MIN_WORD_COUNT = 5;
const DEFAULT_INPUT_PROMPT =
  "You're an AI assistant for text re-writing and summarization. Rewrite the input text focusing on the main key point in at most three very short sentences.";

// All tokens taken from the model's vocabulary at https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct/raw/main/vocab.json
// Token id for end of text
const END_OF_TEXT_TOKEN = 0;
// Token id for beginning of sequence
const BOS_TOKEN = 1;
// Token id for end of sequence
const EOS_TOKEN = 2;

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
  Progress: "chrome://global/content/ml/Utils.sys.mjs",
  BlockListManager: "chrome://global/content/ml/Utils.sys.mjs",
  RemoteSettingsManager: "chrome://global/content/ml/Utils.sys.mjs",
});
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "config",
  "browser.ml.linkPreview.config",
  "{}"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "inputSentences",
  "browser.ml.linkPreview.inputSentences"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "outputSentences",
  "browser.ml.linkPreview.outputSentences"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "prompt",
  "browser.ml.linkPreview.prompt"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "blockListEnabled",
  "browser.ml.linkPreview.blockListEnabled"
);

export const LinkPreviewModel = {
  /**
   * Manager for the block list. If null, no block list is applied.
   *
   * @type {BlockListManager}
   */
  blockListManager: null,

  /**
   * Blocked token list
   *
   * @returns {Array<number>} block token list
   */
  getBlockTokenList() {
    // Tokens with newlines for the link preview model, based on the model's vocab: https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct/raw/main/vocab.json
    const tokensWithNewLines = [
      198, 448, 466, 472, 629, 945, 1004, 1047, 1116, 1410, 1927, 2367, 2738,
      2830, 2953, 3136, 3299, 3337, 3354, 3558, 3717, 3805, 3914, 4602, 4767,
      5952, 7116, 7209, 7338, 7396, 8301, 8500, 8821, 8866, 9198, 9225, 9343,
      9694, 10459, 11181, 11259, 11539, 11813, 12350, 13002, 13272, 13280,
      13596, 13617, 13809, 14436, 14446, 15111, 15182, 15290, 15537, 16140,
      16299, 16390, 16506, 16871, 16980, 16997, 18682, 18850, 18864, 19014,
      19145, 19993, 20098, 20370, 20793, 21193, 21377, 21941, 22342, 22369,
      23004, 23386, 23499, 23799, 24112, 24205, 25457, 25576, 26675, 26886,
      26925, 27536, 27924, 28577, 29306, 29866, 30314, 30544, 30799, 31464,
      32057, 32315, 32829, 34344, 34356, 35163, 35988, 36176, 36286, 36328,
      36489, 36496, 36804, 37468, 38028, 38031, 39014, 39843, 39892, 40677,
      40944, 42057, 42617, 43784, 43902, 44064, 46778, 47213, 47647, 48259,
      48279, 48818,
    ];
    return tokensWithNewLines;
  },
  /**
   * Extracts sentences from a given text.
   *
   * @param {string} text text to process
   * @returns {Array<string>} sentences
   */
  getSentences(text) {
    const abbreviations = [
      "Mr.",
      "Mrs.",
      "Ms.",
      "Dr.",
      "Prof.",
      "Inc.",
      "Ltd.",
      "Jr.",
      "Sr.",
      "St.",
      "e.g.",
      "i.e.",
      "U.S.A",
      "D.C.",
      "U.K.",
      "etc.",
      "a.m.",
      "p.m.",
      "D.",
      "Mass.",
      "Sen.",
      "Rep.",
      "No.",
      "Fig.",
      "vs.",
      "Mx.",
      "Ph.D.",
      "M.D.",
      "D.D.S.",
      "B.A.",
      "M.A.",
      "LL.B.",
      "LL.M.",
      "J.D.",
      "D.O.",
      "D.V.M.",
      "Psy.D.",
      "Ed.D.",
      "Eng.",
      "Co.",
      "Corp.",
      "Mt.",
      "Ft.",
      "U.S.",
      "U.S.A.",
      "E.U.",
      "et al.",
      "Nos.",
      "pp.",
      "Vol.",
      "Rev.",
      "Gen.",
      "Lt.",
      "Col.",
      "Maj.",
      "Capt.",
      "Sgt.",
      "Cpl.",
      "Pvt.",
      "Adm.",
      "Cmdr.",
      "Ave.",
      "Blvd.",
      "Rd.",
      "Ln.",
      "Jan.",
      "Feb.",
      "Mar.",
      "Apr.",
      "May.",
      "Jun.",
      "Jul.",
      "Aug.",
      "Sep.",
      "Sept.",
      "Oct.",
      "Nov.",
      "Dec.",
      "Mon.",
      "Tue.",
      "Tues.",
      "Wed.",
      "Thu.",
      "Thur.",
      "Thurs.",
      "Fri.",
      "Sat.",
      "Sun.",
      "Dept.",
      "Univ.",
      "Est.",
      "Calif.",
      "Fla.",
      "N.Y.",
      "Conn.",
      "Va.",
      "Ill.",
      "Assoc.",
      "Bros.",
      "Dist.",
      "Msgr.",
      "S.P.",
      "P.S.",
      "U.S.S.R.",
      "Mlle.",
      "Mme.",
      "Hon.",
      "Messrs.",
      "Mmes.",
      "v.",
      "vs.",
    ];

    // Replace periods in abbreviations with a placeholder.
    let modifiedText = text;
    const placeholder = "∯";

    abbreviations.forEach(abbrev => {
      const escapedAbbrev = abbrev
        .replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
        .replace(/\\\./g, "\\.");
      const regex = new RegExp(escapedAbbrev, "g");
      const abbrevWithPlaceholder = abbrev.replace(/\./g, placeholder);
      modifiedText = modifiedText.replace(regex, abbrevWithPlaceholder);
    });

    const segmenter = new Intl.Segmenter("en", {
      granularity: "sentence",
    });
    const segments = segmenter.segment(modifiedText);
    let sentences = Array.from(segments, segment => segment.segment);

    // Restore the periods in abbreviations.
    return sentences.map(sentence =>
      sentence.replace(new RegExp(placeholder, "g"), ".")
    );
  },

  /**
   * Clean up text for text generation AI.
   *
   * @param {string} text to process
   * @param {number} maxNumSentences - Max number of sentences to return.
   * @returns {string} cleaned up text
   */
  preprocessText(
    text,
    maxNumSentences = lazy.inputSentences ?? DEFAULT_INPUT_SENTENCES
  ) {
    return (
      this.getSentences(text)
        .map(s =>
          // trim and replace consecutive blank by a single one.
          s.trim().replace(
            /(\s*\n\s*)|\s{2,}/g,
            // (\s*\n\s*)  -> Matches a newline (`\n`) surrounded by optional whitespace.
            // \s{2,}      -> Matches two or more consecutive spaces.
            // g           -> Global flag to replace all occurrences in the string.

            (_, newline) => (newline ? "\n" : " ")
            // Callback function:
            // `_`         -> First argument (full match) is ignored.
            // `newline`   -> If the first capturing group (\s*\n\s*) matched, `newline` is truthy.
            // If `newline` exists, it replaces the match with a single newline ("\n").
            // Otherwise, it replaces the match (extra spaces) with a single space (" ").
          )
        )
        // Remove sentences that are too short without punctuation.
        .filter(
          s =>
            s.length >= MIN_SENTENCE_LENGTH &&
            s.split(" ").length >= MIN_WORD_COUNT &&
            /\p{P}$/u.test(s)
        )
        .slice(0, maxNumSentences)
        .join(" ")
    );
  },

  /**
   * Creates a new ML engine instance with the provided options for link preview.
   *
   * @param {object} options - Configuration options for the ML engine.
   * @param {?function(ProgressAndStatusCallbackParams):void} notificationsCallback A function to call to indicate notifications.
   * @returns {Promise<MLEngine>} - A promise that resolves to the ML engine instance.
   */
  async createEngine(options, notificationsCallback = null) {
    return lazy.createEngine(options, notificationsCallback);
  },

  /**
   * Generate summary text using AI.
   *
   * @param {string} inputText
   * @param {object} callbacks for progress and error
   * @param {Function} callbacks.onDownload optional for download active
   * @param {Function} callbacks.onText optional for text chunks
   * @param {Function} callbacks.onError optional for error
   */
  async generateTextAI(inputText, { onDownload, onText, onError } = {}) {
    // Get updated options from remote settings. No failure if no record exists
    const remoteRequestRecord = await lazy.RemoteSettingsManager.getRemoteData({
      collectionName: "ml-inference-request-options",
      filters: { featureId: "link-preview" },
      majorVersion: 1,
    }).catch(() => {
      console.error(
        "Error retrieving request options from remote settings, will use default options."
      );
      return { options: "{}" };
    });

    let remoteRequestOptions = {};

    try {
      remoteRequestOptions = remoteRequestRecord?.options
        ? JSON.parse(remoteRequestRecord.options)
        : {};
    } catch (error) {
      console.error(
        "Error parsing the remote settings request options, will use default options.",
        error
      );
    }

    // TODO: Unit test that order of preference is correctly respected.
    const processedInput = this.preprocessText(
      inputText,
      lazy.inputSentences ??
        remoteRequestOptions?.inputSentences ??
        DEFAULT_INPUT_SENTENCES
    );

    // Asssume generated text is approximately the same length as the input.
    const nPredict = Math.ceil(processedInput.length / CHARACTERS_PER_TOKEN);
    const systemPrompt =
      lazy.prompt ?? remoteRequestOptions?.systemPrompt ?? DEFAULT_INPUT_PROMPT;
    // Estimate an upper bound for the required number of tokens. This estimate
    // must be large enough to include prompt tokens, input tokens, and
    // generated tokens.
    const numContext =
      Math.ceil(
        (processedInput.length + systemPrompt.length) * CONTEXT_SIZE_MULTIPLIER
      ) + nPredict;

    let engine;
    try {
      engine = await this.createEngine(
        {
          backend: "wllama",
          engineId: "wllamapreview",
          kvCacheDtype: "q8_0",
          modelFile: "smollm2-360m-instruct-q8_0.gguf",
          modelHubRootUrl: "https://model-hub.mozilla.org",
          modelHubUrlTemplate: "{model}/{revision}",
          modelId: "HuggingFaceTB/SmolLM2-360M-Instruct-GGUF",
          modelRevision: "main",
          numBatch: numContext,
          numContext,
          numUbatch: numContext,
          runtimeFilename: "wllama.wasm",
          taskName: "wllama-text-generation",
          timeoutMS: -1,
          useMlock: false,
          useMmap: true,
          ...JSON.parse(lazy.config),
        },
        data => {
          if (data.type == lazy.Progress.ProgressType.DOWNLOAD) {
            onDownload?.(
              data.statusText != lazy.Progress.ProgressStatusText.DONE,
              Math.round((100 * data.totalLoaded) / data.total)
            );
          }
        }
      );

      const postProcessor = await SentencePostProcessor.initialize();
      const blockedTokens = this.getBlockTokenList();
      for await (const val of engine.runWithGenerator({
        nPredict,
        stopTokens: [END_OF_TEXT_TOKEN, BOS_TOKEN, EOS_TOKEN],
        logit_bias_toks: blockedTokens,
        logit_bias_vals: Array(blockedTokens.length).fill(-Infinity),
        prompt: [
          { role: "system", content: systemPrompt },
          { role: "user", content: processedInput },
        ],
      })) {
        const { sentence, abort } = postProcessor.put(val.text);
        if (sentence) {
          onText?.(sentence);
        } else if (!val.text) {
          const remaining = postProcessor.flush();
          if (remaining) {
            onText?.(remaining);
          }
        }

        if (abort) {
          break;
        }
      }
    } catch (error) {
      onError?.(error);
    } finally {
      await engine?.terminate();
    }
  },
};

/**
 * A class for processing streaming text to detect and extract complete
 * sentences. It buffers incoming text and periodically checks for new sentences
 * based on punctuation and character count limits.
 *
 * This class is useful for incremental sentence processing in NLP tasks.
 */
export class SentencePostProcessor {
  /**
   * The maximum number of sentences to output before truncating the buffer.
   * Use -1 for unlimited.
   *
   * @type {number}
   */
  maxNumOutputSentences = -1;

  /**
   * Stores the current text being processed.
   *
   * @type {string}
   */
  currentText = "";

  /**
   * Tracks the number of sentences processed so far.
   *
   * @type {number}
   */
  currentNumSentences = 0;

  /**
   * Manager for the block list. If null, no block list is applied.
   *
   * @type {BlockListManager}
   */
  blockListManager = null;

  /**
   * Create an instance of the sentence postprocessor.
   *
   * @param {object} config - Configuration object.
   * @param {number} config.maxNumOutputSentences - The maximum number of sentences to
   * output before truncating the buffer.
   * @param {BlockListManager | null} config.blockListManager - Manager for the block list
   */
  constructor({
    maxNumOutputSentences = lazy.outputSentences,
    blockListManager,
  } = {}) {
    this.maxNumOutputSentences = maxNumOutputSentences;
    this.blockListManager = blockListManager;
  }

  /**
   * @param {object} config - Configuration object.
   * @param {number} config.maxNumOutputSentences - The maximum number of sentences to
   * output before truncating the buffer.
   * @param {boolean} config.blockListEnabled - Wether to enable block list. If enabled, we
   * don't return the sentence that has a blocked word along with any sentences coming after.
   * @returns {SentencePostProcessor} - An instance of SentencePostProcessor
   */
  static async initialize({
    maxNumOutputSentences = lazy.outputSentences,
    blockListEnabled = lazy.blockListEnabled,
  } = {}) {
    if (!blockListEnabled) {
      LinkPreviewModel.blockListManager = null;
    } else if (!LinkPreviewModel.blockListManager) {
      LinkPreviewModel.blockListManager =
        await lazy.BlockListManager.initializeFromRemoteSettings({
          blockListName: "link-preview-test-en",
          language: "en",
          fallbackToDefault: true,
          majorVersion: 1,
        });
    }

    return new SentencePostProcessor({
      maxNumOutputSentences,
      blockListManager: LinkPreviewModel.blockListManager,
    });
  }

  /**
   * Processes incoming text, checking if a full sentence has been completed. If
   * a full sentence is detected, it returns the first complete sentence.
   * Otherwise, it returns an empty string.
   *
   * @param {string} text to process
   * @returns {{ text: string, abort: boolean }} An object containing:
   *          - `{string} sentence`: The first complete sentence if available, otherwise an empty string.
   *          - `{boolean} abort`: `true` if generation should be aborted early, `false` otherwise.
   */
  put(text) {
    if (this.currentNumSentences == this.maxNumOutputSentences) {
      return { sentence: "", abort: true };
    }
    this.currentText += text;

    // We need to ensure that the current sentence is complete and the next
    // has started before reporting that a sentence is ready.
    const sentences = LinkPreviewModel.getSentences(this.currentText);
    let sentence = "";
    let abort = false;
    if (sentences.length >= 2) {
      this.currentText = sentences.slice(1).join("");
      this.currentNumSentences += 1;

      if (this.currentNumSentences == this.maxNumOutputSentences) {
        this.currentText = "";
        abort = true;
      }
      sentence = sentences[0];

      // If the sentence contains a block word, abort
      if (
        this.blockListManager &&
        this.blockListManager.matchAtWordBoundary({
          // Blocklist is always lowercase
          text: sentence.toLowerCase(),
        })
      ) {
        sentence = "";
        abort = true;
        this.currentNumSentences = this.maxNumOutputSentences;
      }
    }

    return { sentence, abort };
  }

  /**
   * Flushes the remaining text buffer. This ensures that any last remaining
   * sentence is returned.
   *
   * @returns {string} remaining text that hasn't been processed yet
   */
  flush() {
    return this.currentText;
  }
}
