/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

/**
 * @import { TypedArray } from "../ml.d.ts"
 */

const lazy = {};
const IN_WORKER = typeof importScripts !== "undefined";
const ES_MODULES_OPTIONS = IN_WORKER ? { global: "current" } : {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    BLOCK_WORDS_ENCODED: "chrome://global/content/ml/BlockWords.sys.mjs",
    ModelHub: "chrome://global/content/ml/ModelHub.sys.mjs",
    MLEngine: "moz-src:///toolkit/components/ml/actors/MLEngineParent.sys.mjs",
    EngineProcess: "chrome://global/content/ml/EngineProcess.sys.mjs",
    RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
    TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
    FEATURES: "chrome://global/content/ml/EngineProcess.sys.mjs",
    PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  },
  ES_MODULES_OPTIONS
);

/**
 * Log level set by the pipeline.
 *
 * @type {string}
 */
let logLevel = "Error";

/**
 * Sets the log level.
 *
 * @param {string} level - The log level.
 */
export function setLogLevel(level) {
  logLevel = level;
}

if (IN_WORKER) {
  ChromeUtils.defineLazyGetter(lazy, "console", () => {
    return console.createInstance({
      maxLogLevel: logLevel, // we can't use maxLogLevelPref in workers.
      logModule: "GeckoMLUtils",
    });
  });
} else {
  ChromeUtils.defineLazyGetter(lazy, "console", () => {
    return console.createInstance({
      maxLogLevelPref: "browser.ml.logLevel",
      logModule: "GeckoMLUtils",
    });
  });
}

/** The name of the remote settings collection holding block list */
const RS_BLOCK_LIST_COLLECTION = "ml-inference-words-block-list";

/**
 * Enumeration for the progress status text.
 */
export const ProgressStatusText = Object.freeze({
  // The value of the status text indicating that an operation is started.
  INITIATE: "initiate",
  // The value of the status text indicating an estimate for the size of the operation.
  SIZE_ESTIMATE: "size_estimate",
  // The value of the status text indicating that an operation is in progress.
  IN_PROGRESS: "in_progress",
  // The value of the status text indicating that an operation has completed.
  DONE: "done",
});

/**
 * Enumeration for type of progress operations.
 */
export const ProgressType = Object.freeze({
  // The value of the operation type for a remote downloading.
  DOWNLOAD: "downloading",
  // The value of the operation type when loading from cache
  LOAD_FROM_CACHE: "loading_from_cache",
  // The value of the operation type when running the model
  INFERENCE: "running_inference",
});

/**
 * This class encapsulates the parameters supported by a progress and status callback.
 */
export class ProgressAndStatusCallbackParams {
  // Params for progress callback

  /**
   * A float indicating the percentage of data loaded. Note that
   * 100% does not necessarily mean the operation is complete.
   *
   * @type {?float}
   */
  progress = null;

  /**
   * A float indicating the total amount of data loaded so far.
   * In particular, this is the sum of currentLoaded across all call of the callback.
   *
   * @type {?float}
   */
  totalLoaded = null;

  /**
   * The amount of data loaded in the current callback call.
   *
   * @type {?float}
   */
  currentLoaded = null;

  /**
   * A float indicating an estimate of the total amount of data to be loaded.
   * Do not rely on this number as this is an estimate and the true total could be
   * either lower or higher.
   *
   * @type {?float}
   */
  total = null;

  /**
   * The units in which the amounts are reported.
   *
   * @type {?string}
   */
  units = null;

  // Params for status callback
  /**
   * The name of the operation being tracked.
   *
   * @type {?string}
   */
  type = null;

  /**
   * A message indicating the status of the tracked operation.
   *
   * @type {?string}
   */
  statusText = null;

  /**
   * An ID uniquely identifying the object/file being tracked.
   *
   * @type {?string}
   */
  id = null;

  /**
   * A boolean indicating if the operation was successful.
   * true means we have a successful operation.
   *
   * @type {?boolean}
   */
  ok = null;

  /**
   * Any additional metadata for the operation being tracked.
   *
   * @type {?object}
   */
  metadata = null;

  constructor(params = {}) {
    this.update(params);
  }

  update(params = {}) {
    const allowedKeys = new Set(Object.keys(this));
    const invalidKeys = Object.keys(params).filter(x => !allowedKeys.has(x));
    if (invalidKeys.length) {
      throw new Error(`Received Invalid option: ${invalidKeys}`);
    }
    for (const key of allowedKeys) {
      if (key in params) {
        this[key] = params[key];
      }
    }
  }
}

/**
 * Creates the file URL from the organization, model, and version.
 *
 * @param {object} config - The configuration object to be updated.
 * @param {string} config.model - model name
 * @param {string} config.revision - model revision
 * @param {string} config.file - filename
 * @param {string} config.rootUrl - root url of the model hub
 * @param {string} config.urlTemplate - url template of the model hub
 * @param {boolean} [config.addDownloadParams] - Whether to add a download query parameter.
 * @returns {string} The full URL
 */
export function createFileUrl({
  model,
  revision,
  file,
  rootUrl,
  urlTemplate,
  addDownloadParams = false,
}) {
  const baseUrl = new URL(rootUrl);

  if (!baseUrl.pathname.endsWith("/")) {
    baseUrl.pathname += "/";
  }
  // Replace placeholders in the URL template with the provided data.
  // If some keys are missing in the data object, the placeholder is left as is.
  // If the placeholder is not found in the data object, it is left as is.
  const data = {
    model,
    revision,
  };
  let path = urlTemplate.replace(
    /\{(\w+)\}/g,
    (match, key) => data[key] || match
  );
  path = `${path}/${file}`;

  const fullPath = `${baseUrl.pathname}${
    path.startsWith("/") ? path.slice(1) : path
  }`;

  const urlObject = new URL(fullPath, baseUrl.origin);
  if (addDownloadParams) {
    urlObject.searchParams.append("download", "true");
  }

  return urlObject.toString();
}

/**
 * Read and track progress when reading a Response object
 *
 * @param {any} response The Response object to read
 * @param {?function(ProgressAndStatusCallbackParams):void} progressCallback The function to call with progress updates
 *
 * @returns {Promise<Uint8Array>} A Promise that resolves with the Uint8Array buffer
 */
export async function readResponse(response, progressCallback) {
  const contentLength = response.headers.get("Content-Length");
  if (!contentLength) {
    console.warn(
      "Unable to determine content-length from response headers. Will expand buffer when needed."
    );
  }
  let total = parseInt(contentLength ?? "0");

  progressCallback?.(
    new ProgressAndStatusCallbackParams({
      progress: 0,
      totalLoaded: 0,
      currentLoaded: 0,
      total,
      units: "bytes",
    })
  );

  let buffer = new Uint8Array(total);
  let loaded = 0;

  for await (const value of response.body) {
    let newLoaded = loaded + value.length;
    if (newLoaded > total) {
      total = newLoaded;

      // Adding the new data will overflow buffer.
      // In this case, we extend the buffer
      // Happened when the content-length is lower than the actual lenght
      let newBuffer = new Uint8Array(total);

      // copy contents
      newBuffer.set(buffer);

      buffer = newBuffer;
    }
    buffer.set(value, loaded);
    loaded = newLoaded;

    const progress = (loaded / total) * 100;

    progressCallback?.(
      new ProgressAndStatusCallbackParams({
        progress,
        totalLoaded: loaded,
        currentLoaded: value.length,
        total,
        units: "bytes",
      })
    );
  }

  // Ensure that buffer is not bigger than loaded
  // Sometimes content length is larger than the actual size
  buffer = buffer.slice(0, loaded);

  return buffer;
}

/**
 * Class for watching the progress bar of multiple events and combining
 * then into a single progress bar.
 */
export class MultiProgressAggregator {
  /**
   * A function to call with the aggregated statistics.
   *
   * @type {?function(ProgressAndStatusCallbackParams):void}
   */
  progressCallback = null;

  /**
   * The name of the key that contains status information.
   *
   * @type {Set<string>}
   */
  watchedTypes;

  /**
   * The number of operations that are yet to be completed.
   *
   * @type {float}
   */
  #remainingEvents = 0;

  /**
   * The type of operation seen so far.
   *
   * @type {Set<string>}
   */
  #seenTypes;

  /**
   * Total number of objects seen, irrespective of method
   *
   * @type {integer}
   */
  #totalObjectsSeen = 0;

  /**
   * The status of text seen so far.
   *
   * @type {Set<string>}
   */
  #seenStatus;

  /**
   * Info about each object.
   *
   * @type {Dict<string, integer>}
   */
  #downloadObjects;

  /**
   * @param {object} config
   * @param {?function(ProgressAndStatusCallbackParams):void} config.progressCallback - A function to call with the aggregated statistics.
   * @param {Iterable<string>} config.watchedTypes - The types to watch for aggregation
   */
  constructor({ progressCallback, watchedTypes = [ProgressType.DOWNLOAD] }) {
    this.progressCallback = progressCallback;
    this.watchedTypes = new Set(watchedTypes);

    this.#seenTypes = new Set();
    this.#seenStatus = new Set();
    this.#downloadObjects = {};
  }

  /**
   * Callback function that will combined data from different objects/files.
   *
   * @param {ProgressAndStatusCallbackParams} data - object containing the data
   */
  aggregateCallback(data) {
    if (this.watchedTypes.has(data.type)) {
      this.#seenTypes.add(data.type);
      this.#seenStatus.add(data.statusText);
      if (data.statusText == ProgressStatusText.INITIATE) {
        this.#remainingEvents += 1;
      }

      if (data.statusText == ProgressStatusText.SIZE_ESTIMATE) {
        if (data.type != ProgressType.LOAD_FROM_CACHE) {
          // We consider a downloaded object seen when we have the size estimate (object started downloading)
          this.#totalObjectsSeen += 1;
          this.#downloadObjects[data.id] = {
            expected: data.total,
            curTotal: 0,
          };
        }
      }

      const curDownload = this.#downloadObjects[data.id] || {};

      if (data.statusText == ProgressStatusText.DONE) {
        this.#remainingEvents -= 1;
        if (data.type == ProgressType.LOAD_FROM_CACHE) {
          // We consider a cached (not downloaded) object seen when loaded
          this.#totalObjectsSeen += 1;
        } else {
          curDownload.curTotal = curDownload.expected; // Make totals match
        }
      }

      if ("curTotal" in curDownload) {
        curDownload.curTotal += data.currentLoaded;
        if (curDownload.curTotal > curDownload.expected) {
          // Make sure we don't go over 100%. Due to compression, sometimes the numbers don't add up as expected.
          curDownload.curTotal = curDownload.expected;
        }
      }

      if (this.progressCallback) {
        let statusText = data.statusText;
        if (this.#seenStatus.has(ProgressStatusText.IN_PROGRESS)) {
          statusText = ProgressStatusText.IN_PROGRESS;
        }

        if (this.#remainingEvents == 0) {
          statusText = ProgressStatusText.DONE;
        }
        const combinedLoadedManual = Object.keys(this.#downloadObjects).reduce(
          (acc, key) => acc + this.#downloadObjects[key].curTotal,
          0
        );
        const combinedTotalManual =
          Object.keys(this.#downloadObjects).reduce(
            (acc, key) => acc + this.#downloadObjects[key].expected,
            0
          ) || 1;
        data = { ...data, totalObjectsSeen: this.#totalObjectsSeen };
        this.progressCallback(
          new ProgressAndStatusCallbackParams({
            type: data.type,
            statusText,
            id: data.id,
            total: combinedTotalManual,
            currentLoaded: data.currentLoaded,
            totalLoaded: combinedLoadedManual,
            progress: (combinedLoadedManual / combinedTotalManual) * 100,
            ok: data.ok,
            units: data.units,
            metadata: data,
          })
        );
      }
    }
  }
}

/**
 * Fetches a URL and returns the response if the request is successful (status 2xx).
 * Throws an error if the response status indicates failure.
 *
 * @async
 * @function fetchUrl
 * @param {string | URL} url - The URL to fetch.
 * @param {RequestInit} [options] - Optional fetch options (method, headers, body, etc.).
 * @returns {Promise<Response>} The fetch `Response` object.
 * @throws {Error} If the response status is not in the 200–299 range.
 */
export async function fetchUrl(url, options) {
  const response = await fetch(url, options);

  if (!response.ok) {
    throw new Error(
      `HTTP error! Status: ${response.status} ${response.statusText}`
    );
  }

  return response;
}

/**
 * Reads the body of a fetch `Response` object and writes it to a provided `WritableStream`,
 * tracking progress and reporting it via a callback.
 *
 * @param {object}  params - Parameters object.
 * @param {Response} params.response - The fetch `Response` object containing the body to read.
 * @param {WritableStream} params.writableStream - The destination stream where the response body
 *                                          will be written.
 * @param {?function(ProgressAndStatusCallbackParams):void} params.progressCallback The function to call with progress updates.
 * @param {?AbortSignal} params.abortSignal - AbortSignal to cancel the read.
 */
export async function readResponseToWriter({
  response,
  writableStream,
  progressCallback,
  abortSignal,
} = {}) {
  // Attempts to retrieve the `Content-Length` header from the response to estimate total size.
  const contentLength = response.headers.get("Content-Length");
  if (!contentLength) {
    console.warn(
      "Unable to determine content-length from response headers. Progress percentage will be approximated."
    );
  }
  let totalSize = parseInt(contentLength ?? "0");

  let loadedSize = 0;

  // Creates a `TransformStream` to monitor the transfer progress of each chunk.
  const progressStream = new TransformStream({
    transform(chunk, controller) {
      controller.enqueue(chunk); // Pass the chunk along to the writable stream
      loadedSize += chunk.length;
      totalSize = Math.max(totalSize, loadedSize);

      // Reports progress updates via the `progressCallback` function if provided.
      progressCallback?.(
        new ProgressAndStatusCallbackParams({
          progress: (loadedSize / totalSize) * 100,
          totalLoaded: loadedSize,
          currentLoaded: chunk.length,
          total: totalSize,
          units: "bytes",
        })
      );
    },
  });

  // Pipes the response body through the progress stream into the writable stream and close the stream on completion/error.
  await response.body
    .pipeThrough(progressStream, { signal: abortSignal })
    .pipeTo(writableStream, { signal: abortSignal });
}

// Create a "namespace" to make it easier to import multiple names.
export var Progress = Progress || {};
Progress.ProgressAndStatusCallbackParams = ProgressAndStatusCallbackParams;
Progress.ProgressStatusText = ProgressStatusText;
Progress.ProgressType = ProgressType;
Progress.readResponse = readResponse;
Progress.readResponseToWriter = readResponseToWriter;
Progress.fetchUrl = fetchUrl;

/**
 * @param {?string} [processType] - "inference", "hwInference", or null for
 * either.
 */
export async function getInferenceProcessInfo(processType = null) {
  let info = await ChromeUtils.requestProcInfo();

  for (const child of info.children) {
    let type = null;
    if (child.type === "inference") {
      type = "inference";
    } else if (
      child.type.startsWith("utility") &&
      child.utilityActors.some(actor => actor.actorName === "hwInference")
    ) {
      type = "hwInference";
    }
    if (type && (!processType || processType === type)) {
      return {
        pid: child.pid,
        memory: child.memory,
        cpuTime: child.cpuTime,
        cpuCycleCount: child.cpuCycleCount,
      };
    }
  }
  return {};
}

const ALWAYS_ALLOWED_HUBS = [
  "chrome://",
  "resource://",
  "http://localhost/",
  "https://localhost/",
];

/**
 * Enum for URL rejection types.
 *
 * Defines the type of rejection for a URL:
 *
 * - "DENIED" is for URLs explicitly disallowed by the deny list.
 * - "NONE" is for URLs allowed by the allow list.
 * - "DISALLOWED" is for URLs not matching any entry in either list.
 *
 * @readonly
 * @enum {string}
 */
export const RejectionType = {
  DENIED: "DENIED",
  NONE: "NONE",
  DISALLOWED: "DISALLOWED",
};

/**
 * Class for checking URLs against allow and deny lists.
 */
export class URLChecker {
  /**
   * Creates an instance of URLChecker.
   *
   * @param {Array<{filter: 'ALLOW'|'DENY', urlPrefix: string}>} allowDenyList - Array of URL patterns with filters.
   */
  constructor(allowDenyList = null) {
    if (allowDenyList) {
      this.allowList = allowDenyList
        .filter(entry => entry.filter === "ALLOW")
        .map(entry => entry.urlPrefix.toLowerCase());

      this.denyList = allowDenyList
        .filter(entry => entry.filter === "DENY")
        .map(entry => entry.urlPrefix.toLowerCase());
    } else {
      this.allowList = [];
      this.denyList = [];
    }

    // Always allowed
    for (const url of ALWAYS_ALLOWED_HUBS) {
      this.allowList.push(url);
    }
  }

  /**
   * Normalizes localhost URLs to ignore user info, port, and path details.
   *
   * @param {string} url - The URL to normalize.
   * @returns {string} - Normalized URL.
   */
  normalizeLocalhost(url) {
    const parsedURL = URL.parse(url);
    if (parsedURL?.hostname === "localhost") {
      // Normalize to only scheme and localhost without port or user info
      return `${parsedURL.protocol}//localhost/`;
    }
    return url;
  }

  /**
   * Checks if a given URL is allowed based on allowList and denyList patterns.
   *
   * @param {string} url - The URL to check.
   * @returns {{ allowed: boolean, rejectionType: string }} - Returns an object with:
   *    - `allowed`: true if the URL is allowed, otherwise false.
   *    - `rejectionType`:
   *       - "DENIED" if the URL matches an entry in the denyList,
   *       - "NONE" if the URL matches an entry in the allowList,
   *       - "DISALLOWED" if the URL does not match any entry in either list.
   */
  allowedURL(url) {
    const normalizedURL = this.normalizeLocalhost(url).toLowerCase();

    // Check if the URL is denied by any entry in the denyList
    if (this.denyList.some(prefix => normalizedURL.startsWith(prefix))) {
      return { allowed: false, rejectionType: RejectionType.DENIED };
    }

    // Check if the URL is allowed by any entry in the allowList
    if (this.allowList.some(prefix => normalizedURL.startsWith(prefix))) {
      return { allowed: true, rejectionType: RejectionType.NONE };
    }

    // If no matches, return a default rejectionType
    return { allowed: false, rejectionType: RejectionType.DISALLOWED };
  }
}

/**
 * Returns the optimal CPU concurrency for ML
 *
 * @returns {number} The number of threads we should be using
 */
export function getOptimalCPUConcurrency() {
  let mlUtils = Cc["@mozilla.org/ml-utils;1"].createInstance(Ci.nsIMLUtils);
  return mlUtils.getOptimalCPUConcurrency();
}

/**
 * A class to check if some text belongs to a blocked list of n-grams.
 *
 */
export class BlockListManager {
  /**
   * The set of blocked word n-grams.
   *
   * This set contains the n-grams (combinations of words) that are considered blocked.
   * The n-grams are decoded from base64 to strings.
   *
   * @type {Set<string>}
   */
  blockNgramSet = null;

  /**
   * Word segmenter for identifying word boundaries in the text.
   *
   * Used to segment the input text into words and ensure that n-grams are checked at word boundaries.
   *
   * @type {Intl.Segmenter}
   */
  wordSegmenter = null;

  /**
   * The unique lengths of the blocked n-grams.
   *
   * This set stores the lengths of the blocked n-grams, allowing for efficient length-based checks.
   * For example, if the blocked n-grams are "apple" (5 characters) and "orange" (6 characters),
   * this set will store lengths {5, 6}.
   *
   * @type {Set<number>}
   */
  blockNgramLengths = null;

  /**
   * Create an instance of the block list manager.
   *
   * @param {object} options - Configuration object.
   * @param {string} options.language - A string with a BCP 47 language tag for the language of the blocked n-grams.
   *                                    Example: "en" for English, "fr" for French.
   *                                    See https://en.wikipedia.org/wiki/IETF_language_tag.
   * @param {Array<string>} options.blockNgrams - Base64-encoded blocked n-grams.
   */
  constructor({ blockNgrams, language = "en" } = {}) {
    const blockNgramList = blockNgrams.map(base64Str =>
      BlockListManager.decodeBase64(base64Str)
    );
    // TODO: Can be optimized by grouping the set by the word n-gram lenghts.
    this.blockNgramSet = new Set(blockNgramList);

    this.blockNgramLengths = new Set(blockNgramList.map(k => k.length)); // unique lengths

    this.wordSegmenter = new Intl.Segmenter(language, { granularity: "word" });
  }

  /**
   * Initialize the block list manager from the default list.
   *
   * @param {object} options - Configuration object.
   * @param {string} options.language - A string with a BCP 47 language tag for the language of the blocked n-grams.
   *                                    Example: "en" for English, "fr" for French.
   *                                    See https://en.wikipedia.org/wiki/IETF_language_tag.
   *
   * @returns {BlockListManager} A new BlockListManager instance.
   */
  static initializeFromDefault({ language = "en" } = {}) {
    return new BlockListManager({
      blockNgrams: lazy.BLOCK_WORDS_ENCODED[language],
      language,
    });
  }

  /**
   * Initialize the block list manager from remote settings
   *
   * @param {object} options - Configuration object.
   * @param {string} options.blockListName - Name of the block list within the remote setting collection.
   * @param {string} options.language - A string with a BCP 47 language tag for the language of the blocked n-grams.
   *                                    Example: "en" for English, "fr" for French.
   *                                    See https://en.wikipedia.org/wiki/IETF_language_tag.
   * @param {boolean} options.fallbackToDefault - Whether to fall back to the default block list if the remote settings retrieval fails.
   * @param {number} options.majorVersion - The target version of the block list in remote settings.
   * @param {number} options.collectionName - The remote settings collection holding the block list.
   *
   * @returns {Promise<BlockListManager>} A promise to a new BlockListManager instance.
   */
  static async initializeFromRemoteSettings({
    blockListName,
    language = "en",
    fallbackToDefault = true,
    majorVersion = 1,
    collectionName = RS_BLOCK_LIST_COLLECTION,
  } = {}) {
    try {
      const record = await RemoteSettingsManager.getRemoteData({
        collectionName,
        filters: { name: blockListName, language },
        majorVersion,
      });

      if (!record) {
        throw new Error(
          `No block list record found for ${JSON.stringify({ language, majorVersion, blockListName })}`
        );
      }

      return new BlockListManager({
        blockNgrams: record.blockList,
        language,
      });
    } catch (error) {
      if (fallbackToDefault) {
        lazy.console.debug(
          "Error when retrieving list from remote settings. Falling back to in-source list"
        );
        return BlockListManager.initializeFromDefault({ language });
      }

      throw error;
    }
  }

  /**
   * Decode a base64 encoded string to its original representation.
   *
   * @param {string} base64Str - The base64 encoded string to decode.
   * @returns {string} The decoded string.
   */
  static decodeBase64(base64Str) {
    const binary = atob(base64Str); // binary string

    // Convert binary string to byte array
    const bytes = Uint8Array.from(binary, c => c.charCodeAt(0));

    // Decode bytes to Unicode string
    return new TextDecoder().decode(bytes);
  }

  /**
   * Encode a string to base64.
   *
   * @param {string} str - The string to encode.
   * @returns {string} The base64 encoded string.
   */
  static encodeBase64(str) {
    // Convert Unicode string to bytes
    const bytes = new TextEncoder().encode(str); // Uint8Array

    // Convert bytes to binary string
    const binary = String.fromCharCode(...bytes);

    // Encode binary string to base64
    return btoa(binary);
  }

  /**
   * Check if blocked n-grams are present at word boundaries in the given text.
   *
   * This method checks the text at word boundaries (using the word segmenter) for any n-grams that are blocked.
   *
   * @param {object} options - Configuration object.
   * @param {string} options.text - The text to check for blocked n-grams.
   * @returns {boolean} True if the text contains a blocked word n-gram, false otherwise.
   *
   * @example
   * const result = blockListManager.matchAtWordBoundary({ text: "this is spam text" });
   * console.log(result); // true if 'spam' is a blocked n-gram.
   * const result2 = blockListManager.matchAtWordBoundary({ text: "this isspam text" });
   * console.log(result2); // false even if spam is a blocked n-gram.
   */
  matchAtWordBoundary({ text }) {
    const isTextOffsetAtEndOfWordBoundary = new Array(text.length).fill(false);

    // Keep hold of the index of the first character of each word in the text
    const startWordIndices = Array.from(
      this.wordSegmenter.segment(text),
      segment => {
        if (segment.index > 0) {
          // segment.index returns start of word. Subtracting one for end of word.
          isTextOffsetAtEndOfWordBoundary[segment.index - 1] = true;
        }

        return segment.index;
      }
    );
    // End of text always at word boundary
    isTextOffsetAtEndOfWordBoundary[text.length - 1] = true;

    for (const startTextOffset of startWordIndices) {
      // Check if there is a word starting at offset startTextOffset and matching a blocked n-gram words of given length
      for (const blockLength of this.blockNgramLengths) {
        const endTextOffset = startTextOffset + blockLength;

        if (
          // Skip checking when the pattern to check does not end at word boundary.
          isTextOffsetAtEndOfWordBoundary[endTextOffset - 1] &&
          // check if we have this word in the block list
          this.blockNgramSet.has(text.slice(startTextOffset, endTextOffset))
        ) {
          return true;
        }
      }
    }
    return false;
  }

  /**
   * Check if blocked n-grams are present anywhere in the text.
   *
   * This method checks the entire text (not limited to word boundaries) for any n-grams that are blocked.
   *
   * @param {object} options - Configuration object.
   * @param {string} options.text - The text to check for blocked n-grams.
   * @returns {boolean} True if the text contains a blocked word n-gram, false otherwise.
   *
   * @example
   * const result = blockListManager.matchAnywhere({ text: "this is spam text" });
   * console.log(result); // true if 'spam' is a blocked n-gram.
   * const result2 = blockListManager.matchAnywhere({ text: "this isspam text" });
   * console.log(result2); // true if 'spam' is a blocked n-gram.
   * const result3 = blockListManager.matchAnywhere({ text: "this is s_p_a_m text" });
   * console.log(result3); // false even if 'spam' is a blocked n-gram.
   */
  matchAnywhere({ text }) {
    for (
      let startTextOffset = 0;
      startTextOffset < text.length;
      startTextOffset++
    ) {
      for (const blockLength of this.blockNgramLengths) {
        if (
          this.blockNgramSet.has(
            text.slice(startTextOffset, startTextOffset + blockLength)
          )
        ) {
          return true;
        }
      }
    }

    return false;
  }
}

/**
 * A class to retrieve data from remote setting
 *
 */
export class RemoteSettingsManager {
  /**
   * The cached remote settings clients that downloads the data.
   *
   * @type {Record<string, RemoteSettingsClient>}
   */
  static #remoteClients = {};

  /**
   * Remote settings isn't available in tests, so provide mocked clients.
   *
   * @param {Record<string, RemoteSettingsClient>} remoteClients
   */
  static mockRemoteSettings(remoteClients) {
    lazy.console.log("Mocking remote settings in RemoteSettingsManager.");
    RemoteSettingsManager.#remoteClients = remoteClients;
  }

  /**
   * Remove anything that could have been mocked.
   */
  static removeMocks() {
    lazy.console.log("Removing mocked remote client in RemoteSettingsManager.");
    RemoteSettingsManager.#remoteClients = {};
  }

  /**
   * Lazily initialize the remote settings client responsible for downloading the data.
   *
   * @param {string} collectionName - The name of the collection to use.
   * @returns {RemoteSettingsClient}
   */
  static getRemoteClient(collectionName) {
    if (RemoteSettingsManager.#remoteClients[collectionName]) {
      return RemoteSettingsManager.#remoteClients[collectionName];
    }

    /** @type {RemoteSettingsClient} */
    const client = lazy.RemoteSettings(collectionName, {
      bucketName: "main",
    });

    RemoteSettingsManager.#remoteClients[collectionName] = client;

    client.on("sync", async ({ data: { created, updated, deleted } }) => {
      lazy.console.debug(`"sync" event for ${collectionName}`, {
        created,
        updated,
        deleted,
      });

      // Remove all the deleted records.
      for (const record of deleted) {
        await client.attachments.deleteDownloaded(record);
      }

      // Remove any updated records, and download the new ones.
      for (const { old: oldRecord } of updated) {
        await client.attachments.deleteDownloaded(oldRecord);
      }

      // Do nothing for the created records.
    });

    return client;
  }

  /**
   * Gets data from remote settings.
   *
   * @param {object} options - Configuration object
   * @param {string} options.collectionName - The name of the remote settings collection.
   * @param {object} options.filters - The filters to use where key should match the schema in remote settings.
   * @param {number|null} options.majorVersion - The target version or null if no version is supported.
   * @param {Function} [options.lookupKey=(record => record.name)]
   *     The function to use to extract a lookup key from each record when versionning is supported..
   *     This function should take a record as input and return a string that represents the lookup key for the record.
   * @returns {Promise<object|null>}
   */

  static async getRemoteData({
    collectionName,
    filters,
    majorVersion,
    lookupKey = record => record.name,
  } = {}) {
    const client = RemoteSettingsManager.getRemoteClient(collectionName);

    let records = [];

    if (majorVersion) {
      records = await lazy.TranslationsParent.getMaxSupportedVersionRecords(
        client,
        {
          filters,
          minSupportedMajorVersion: majorVersion,
          maxSupportedMajorVersion: majorVersion,
          lookupKey,
        }
      );
    } else {
      records = await client.get({ filters });
    }

    // Handle case where multiple records exist
    if (records.length > 1) {
      throw new Error(
        `Found more than one record in '${collectionName}' for filters ${JSON.stringify(filters)}. Double-check your filters.`
      );
    }

    // If still no records, return null
    if (records.length === 0) {
      return null;
    }

    return records[0];
  }
}

const ADDON_PREFIX = "ML-ENGINE-";

/**
 * Check if an engine id is for an addon
 *
 * @param {string} engineId - The engine id to check
 * @returns {boolean} True if the engine id is for an addon
 */
export function isAddonEngineId(engineId) {
  return engineId.startsWith(ADDON_PREFIX);
}

/**
 * Converts an addon id to an engine id
 *
 * @param {string} addonId - The addon id to convert
 * @returns {string} The engine id
 */
export function addonIdToEngineId(addonId) {
  return `${ADDON_PREFIX}${addonId}`;
}

/**
 * Converts an engine Id into an addon id
 *
 * @param {string} engineId - The engine id to convert
 * @returns {string|null} The addon id. null if the engine id is invalid
 */
export function engineIdToAddonId(engineId) {
  if (!engineId.startsWith(ADDON_PREFIX)) {
    return null;
  }
  return engineId.substring(ADDON_PREFIX.length);
}

/**
 * Converts a feature engine id to a fluent id
 *
 * @param {string} engineId
 * @returns {string|null}
 */
export function featureEngineIdToFluentId(engineId) {
  for (const config of Object.values(lazy.FEATURES)) {
    if (config.engineId === engineId) {
      return config.fluentId;
    }
  }
  return null;
}

/**
 * Looks up display info (a human-readable name and a homepage URL) for one
 * of a downloaded model's files, from the feature registered for one of
 * engineIds. Features register their file display info via
 * FEATURES[...].fileDisplayInfoModule, a module whose default export maps
 * file name to {name, hfUrl}.
 *
 * @param {Array<string>} [engineIds] A model with no recorded engineIds has no
 *   feature to look up, and gets no display info.
 * @param {Array<string>} files
 * @returns {Promise<{name: string, hfUrl: string}|null>}
 */
export async function fileDisplayInfoForEngineIds(engineIds, files) {
  for (const config of Object.values(lazy.FEATURES)) {
    if (
      !config.fileDisplayInfoModule ||
      !engineIds?.includes(config.engineId)
    ) {
      continue;
    }
    const { default: info } = await ChromeUtils.importESModule(
      config.fileDisplayInfoModule
    );
    for (const file of files) {
      if (info[file]) {
        return info[file];
      }
    }
  }
  return null;
}

/**
 * Generates a random uuid to use where Services.uuid is not available,
 * for instance pipelines
 *
 * @returns {string}
 */
export function generateUUID() {
  lazy.console.debug("generating uuid");
  return crypto.randomUUID();
}

/**
 * Checks if we are in private browsing mode
 *
 * @returns {boolean} True if we are in private browsing mode
 */
export function isPrivateBrowsing() {
  const win = Services.wm.getMostRecentBrowserWindow() ?? null;
  return lazy.PrivateBrowsingUtils.isWindowPrivate(win);
}

/**
 * Helpers used to collect telemetry related to the mlmodel management UI
 * (used by about:addons)
 */

function baseRecordData(modelAddonWrapper) {
  const { usedByAddonIds, usedByFirefoxFeatures, model, version } =
    modelAddonWrapper;
  return {
    extension_ids: usedByAddonIds.join(","),
    feature_ids: usedByFirefoxFeatures.join(","),
    model,
    version,
  };
}

export function recordRemoveConfirmationTelemetry(modelAddonWrapper, confirm) {
  Glean.modelManagement.removeConfirmation.record({
    ...baseRecordData(modelAddonWrapper),
    action: confirm ? "remove" : "cancel",
  });
}

export function recordListItemManageTelemetry(modelAddonWrapper) {
  Glean.modelManagement.listItemManage.record({
    ...baseRecordData(modelAddonWrapper),
  });
}

function convertDateToHours(date) {
  const now = Date.now();
  return Math.floor((now - date.getTime()) / 1000 / 60 / 60); // hours
}

export function recordRemoveInitiatedTelemetry(modelAddonWrapper, source) {
  const { lastUsed, updateDate, totalSize } = modelAddonWrapper;
  Glean.modelManagement.removeInitiated.record({
    ...baseRecordData(modelAddonWrapper),
    source,
    size: totalSize,
    last_used: convertDateToHours(lastUsed),
    last_install: convertDateToHours(updateDate),
  });
}

export function recordModelCardLinkTelemetry(modelAddonWrapper) {
  Glean.modelManagement.modelCardLink.record({
    ...baseRecordData(modelAddonWrapper),
  });
}

export function recordListViewTelemetry(qty) {
  Glean.modelManagement.listView.record({
    models: qty,
  });
}

export function recordDetailsViewTelemetry(modelAddonWrapper) {
  Glean.modelManagement.detailsView.record({
    ...baseRecordData(modelAddonWrapper),
  });
}

/**
 * Converts a binary string (where each character represents a byte) into a hexadecimal string.
 *
 * @param {string} binaryStr - The binary string to convert.
 * @returns {string} The resulting hexadecimal string.
 */
export function binaryToHex(binaryStr) {
  return Array.from(binaryStr)
    .map(c => c.charCodeAt(0).toString(16).padStart(2, "0"))
    .join("");
}

/**
 * Computes a cryptographic hash of a Blob using the specified algorithm and output format.
 *
 * @param {Blob} blob - The Blob to hash.
 * @param {("md5"|"sha1"|"sha256"|"sha384"|"sha512")} [algorithm="sha256"] - The hashing algorithm to use.
 * @param {("hex"|"binary"|"base64")} [outputFormat="hex"] - The output format of the hash.
 * @returns {Promise<string>} The computed hash as a string in the specified format.
 */
export async function computeHash(
  blob,
  algorithm = "sha256",
  outputFormat = "hex"
) {
  let hasher = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  hasher.initWithString(algorithm);

  const hashingTransform = new TransformStream({
    transform(chunk, controller) {
      hasher.update(chunk, chunk.length);
      controller.enqueue(chunk); // pass through
    },
  });

  const sink = new WritableStream({
    write() {
      /* discard */
    },
  });

  await blob.stream().pipeThrough(hashingTransform).pipeTo(sink);

  const base64 = outputFormat === "base64";

  let hash = hasher.finish(/* base64 */ base64);

  if (outputFormat === "hex") {
    hash = binaryToHex(hash);
  }

  return hash;
}

// Utils operations
export var MLUtils = MLUtils || {};
MLUtils.fetchUrl = fetchUrl;

/**
 * Safely stringify any value for logging/debugging.
 *
 * This function guarantees a string is returned and will never throw,
 * even for values that JSON.stringify cannot handle (BigInt, Symbols,
 * circular references, proxies with throwing getters, etc).
 *
 * It tries JSON.stringify first with a safe replacer, then falls back to
 * a bounded inspection that handles depth, length, and property limits.
 *
 * @param {*} value - The value to stringify for logging (any type).
 * @param {object} [options] - Optional limits to control output.
 * @param {number} [options.maxDepth=3] - Maximum recursion depth for nested objects.
 * @param {number} [options.maxKeysPerLevel=50] - Maximum number of keys per object or items per array to include.
 * @param {number} [options.maxOutputLength=20000] - Maximum number of characters in the final output string.
 *
 * @returns {string} A safe string representation of the input, never throwing.
 *
 * @example
 * lazy.console.debug(`Chunk received ${stringifyForLog(chunk.metadata)}`);
 *
 * @example
 * const txt = stringifyForLog({ a: 1n, b: new Map([["x", 42]]) });
 * // → '{"a":"1","b":{"x":42}}'
 */
export function stringifyForLog(
  value,
  { maxDepth = 3, maxKeysPerLevel = 50, maxOutputLength = 20_000 } = {}
) {
  // 1) Fast path: JSON with a safe replacer
  try {
    const seen = new WeakSet();

    // Small type-aware helper that keeps output concise and stable
    const toStringish = v => {
      if (typeof v === "bigint" || typeof v === "symbol") {
        return String(v);
      }
      if (v instanceof Date) {
        return v.toISOString();
      } // stable
      if (v instanceof RegExp) {
        return v.toString();
      } // /re/flags
      return undefined; // signal "no change"
    };

    const txt = JSON.stringify(value, (_, v) => {
      // cheap string-ification for a few tricky primitives/objects
      const s = toStringish(v);
      if (s !== undefined) {
        return s;
      }

      if (typeof v === "function") {
        // Avoid dumping source
        return `[Function ${v.name || "anonymous"}]`;
      }

      if (v instanceof Error) {
        // Keep the useful fields
        return { name: v.name, message: v.message, stack: v.stack };
      }

      if (v instanceof Map) {
        return Object.fromEntries([...v.entries()].slice(0, maxKeysPerLevel));
      }

      if (v instanceof Set) {
        return [...v.values()].slice(0, maxKeysPerLevel);
      }

      if (ArrayBuffer.isView(v)) {
        return `${v.constructor.name}(${v.byteLength} bytes)`;
      }

      if (v instanceof ArrayBuffer) {
        return `ArrayBuffer(${v.byteLength} bytes)`;
      }

      if (v && typeof v === "object") {
        if (seen.has(v)) {
          return "[Circular]";
        }
        seen.add(v);
      }

      return v;
    });

    if (typeof txt === "string") {
      return txt.length > maxOutputLength
        ? txt.slice(0, maxOutputLength) + "…[truncated]"
        : txt;
    }
  } catch (_) {
    // fall through to slow path
  }

  // 2) Slow path: guarded, shallow-ish serializer
  //
  // Why we need this:
  // - JSON.stringify can still fail or be unhelpful even with a replacer,
  //   for example top-level BigInt, exotic proxies with throwing getters,
  //   or values that JSON reduces to "{}" while a human-readable preview
  //   would be more useful.
  // - We also want bounded, readable output when JSON would be massive.
  //
  // How it works and why it is safe:
  // - Never throws: every property access is try/catch protected so getters
  //   that throw or proxy traps cannot break logging.
  // - Bounded traversal: depth is capped by maxDepth and the number of
  //   keys or items per level is capped by maxKeysPerLevel.
  // - Cycle safe: a WeakSet tracks seen objects and prints "[Circular]".
  // - Type-aware summaries: Dates use ISO, RegExp uses "/re/flags",
  //   Errors are "Name: message", TypedArrays and ArrayBuffer show sizes,
  //   Arrays show a preview with a possible "…" tail.
  // - Constructor tag: for non-plain objects we prefix with the class name
  //   to keep helpful context without full expansion.
  // - Final guard: the final string is truncated to maxOutputLength.

  const seen2 = new WeakSet();

  function safeDescribe(x, depth = 0) {
    if (x === null) {
      return "null";
    }

    const t = typeof x;
    if (t === "bigint" || t === "symbol") {
      return String(x);
    }
    if (t === "function") {
      return `[Function ${x.name || "anonymous"}]`;
    }
    if (t !== "object") {
      // Handles number, string, boolean, undefined
      try {
        return JSON.stringify(x);
      } catch {
        // Fallback for weird host objects
        return String(x);
      }
    }

    if (seen2.has(x)) {
      return "[Circular]";
    }
    seen2.add(x);

    if (x instanceof Date) {
      return `Date(${isNaN(x.getTime()) ? "Invalid" : x.toISOString()})`;
    }
    if (x instanceof RegExp) {
      return x.toString();
    }
    if (x instanceof Error) {
      return `${x.name}: ${x.message}`;
    }

    if (Array.isArray(x)) {
      if (depth >= maxDepth) {
        return `[Array(${x.length})]`;
      }
      const items = [];
      for (let i = 0; i < Math.min(x.length, maxKeysPerLevel); i++) {
        try {
          items.push(safeDescribe(x[i], depth + 1));
        } catch (e) {
          items.push(`[Thrown: ${(e && e.message) || e}]`);
        }
      }
      if (x.length > maxKeysPerLevel) {
        items.push("…");
      }
      return `[${items.join(", ")}]`;
    }

    if (ArrayBuffer.isView(x)) {
      return `${x.constructor.name}(${x.byteLength} bytes)`;
    }
    if (x instanceof ArrayBuffer) {
      return `ArrayBuffer(${x.byteLength} bytes)`;
    }
    if (x instanceof Map) {
      return `Map(${x.size})`;
    }
    if (x instanceof Set) {
      return `Set(${x.size})`;
    }

    if (depth >= maxDepth) {
      return `[Object ${(x && x.constructor && x.constructor.name) || "Object"}]`;
    }

    const out = [];
    let names = [];
    try {
      names = [
        ...new Set([
          ...Object.keys(x),
          ...Object.getOwnPropertyNames(x).filter(k => !k.startsWith("#")),
        ]),
      ];
    } catch (e) {
      return `[Uninspectable: ${(e && e.message) || e}]`;
    }

    for (const key of names.slice(0, maxKeysPerLevel)) {
      try {
        const val = x[key];
        out.push(`${JSON.stringify(key)}: ${safeDescribe(val, depth + 1)}`);
      } catch (e) {
        out.push(`${JSON.stringify(key)}: [Thrown: ${(e && e.message) || e}]`);
      }
    }
    if (names.length > maxKeysPerLevel) {
      out.push(`"…": "more properties omitted"`);
    }

    const tag =
      x &&
      x.constructor &&
      x.constructor.name &&
      x.constructor.name !== "Object"
        ? x.constructor.name
        : "";
    return tag ? `${tag} { ${out.join(", ")} }` : `{ ${out.join(", ")} }`;
  }

  let s = safeDescribe(value);
  if (typeof s !== "string") {
    try {
      s = JSON.stringify(s);
    } catch {
      s = String(s);
    }
  }
  if (s.length > maxOutputLength) {
    s = s.slice(0, maxOutputLength) + "…[truncated]";
  }
  return s;
}

/**
 * Reads into an ArrayBuffer keeping track of the offsets.
 */
class ByteReader {
  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
    this.offset = 0;
    this.buffer = buffer;
    this.view = new DataView(buffer);
  }

  /**
   * @returns {number}
   */
  uint8() {
    return this.view.getUint8(this.offset++);
  }

  /**
   * @param {"little" | "big"} endianess
   */
  uint16(endianess) {
    const value = this.view.getUint16(this.offset, endianess == "little");
    this.offset += 2;
    return value;
  }

  /**
   * @param {number} length
   * @returns {string}
   */
  latin1(length) {
    const bytes = new Uint8Array(this.buffer, this.offset, length);
    this.offset += length;
    const decoder = new TextDecoder("latin1");
    return decoder.decode(bytes);
  }

  /**
   * Return the remaining data.
   */
  sliceRemaining() {
    return this.buffer.slice(this.offset);
  }
}

/**
 * Parse an ArrayBuffer of a .npy file into a typed array and shape.
 *
 * https://numpy.org/doc/stable/reference/generated/numpy.lib.format.html
 *
 * @param {ArrayBuffer} buffer The ArrayBuffer containing the .npy data.
 * @returns {{data: TypedArray, shape: number[], dtype: string}}
 */
export function parseNpy(buffer) {
  const reader = new ByteReader(buffer);
  if (reader.uint8() != 0x93 || reader.latin1(5) != "NUMPY") {
    throw new Error("Not a valid .npy file");
  }
  const majorVersion = reader.uint8();
  reader.uint8(); // minorVersion

  if (majorVersion != 1) {
    throw new Error("Only major version 1 is currently supported.");
  }

  const headerLength = reader.uint16("little");
  let headerText = reader.latin1(headerLength).trim();

  // Header is a Python dict string. Do some text manipulation to make it JSON parseable.
  //
  //  "{'descr': '<f8', 'fortran_order': False, 'shape': (3, 4), }"
  //  "{'descr': '|u1', 'fortran_order': False, 'shape': (63091, 128), }"
  headerText = headerText
    .replace(/'/g, '"') // single to double quotes
    .replace("False", "false")
    .replace("True", "true")
    .replace(/,\s*}/, "}") // trailing commas
    .replace(/,\s*\)/, ")"); // trailing commas in tuple

  const header = JSON.parse(
    headerText.replace(/\((.*?)\)/, (m, inner) => {
      // convert shape tuple into JSON array
      return `[${inner.trim().replace(/, /g, ",")}]`;
    })
  );

  if (header.fortran_order) {
    throw new Error("Unable to parse an array using fortran_order");
  }

  const fullType = header.descr; // e.g. '<f8'
  const littleEndian = fullType[0] === "<" || fullType[0] === "|";
  const dtype = fullType.slice(1);

  const shape = header.shape;
  const dataBuffer = reader.sliceRemaining();

  let typedArray;
  switch (dtype) {
    case "f8": // float64
      typedArray = new Float64Array(dataBuffer);
      break;
    case "f4": // float32
      typedArray = new Float32Array(dataBuffer);
      break;
    case "f2": // float16
      typedArray = new Float16Array(dataBuffer);
      break;
    case "i4": // int32
      typedArray = new Int32Array(dataBuffer);
      break;
    case "i2": // int16
      typedArray = new Int16Array(dataBuffer);
      break;
    case "i1": // int8
      typedArray = new Int8Array(dataBuffer);
      break;
    case "u4": // uint32
      typedArray = new Uint32Array(dataBuffer);
      break;
    case "u2": // uint16
      typedArray = new Uint16Array(dataBuffer);
      break;
    case "u1": // uint8
      typedArray = new Uint8Array(dataBuffer);
      break;
    default:
      throw new Error(`Unsupported dtype: ${fullType}`);
  }

  let expectedLength = 1;
  for (const size of shape) {
    expectedLength *= size;
  }
  if (typedArray.length != expectedLength) {
    throw new Error(
      `The data length (${typedArray.length}) did not match the expected dimensions (${expectedLength}) for shape ${JSON.stringify(shape)}`
    );
  }

  // If endianness doesn't match, swap the bytes.
  if (!littleEndian && typedArray.BYTES_PER_ELEMENT > 1) {
    const u8 = new Uint8Array(typedArray.buffer);
    for (let i = 0; i < u8.length; i += typedArray.BYTES_PER_ELEMENT) {
      u8.subarray(i, i + typedArray.BYTES_PER_ELEMENT).reverse();
    }
  }

  return { data: typedArray, shape, dtype };
}

/**
 * Resolves with all values if all promises succeed, otherwise rejects with all errors.
 *
 * @param {Promise[]} promises Promises to wait for.
 * @returns {Promise<unknown[]>} Fulfilled values in input order.
 * @throws {AggregateError|Error} If one or more promises are rejected.
 */
export async function allSettledOrReject(promises) {
  const results = await Promise.allSettled(promises);

  const errors = results
    .filter(r => r.status === "rejected")
    .map(r => r.reason);

  if (errors.length === 1) {
    throw errors[0];
  }

  if (errors.length) {
    throw new AggregateError(errors, errors.map(e => e.message).join("; "));
  }

  return results.map(r => r.value);
}

/**
 * Utilities for uninstalling ML features or removing all ML-related data.
 *
 * Provides static methods to perform targeted or full uninstalls, with
 * optional reuse of a shared ModelHub instance.
 */
export class MLUninstallService {
  /**
   * Lazily created default ModelHub used when no hub is provided.
   *
   * @type {ModelHub|null}
   * @private
   */
  static #defaultHub = null;

  /**
   * Get or create the default ModelHub instance.
   *
   * @returns {ModelHub}
   * @private
   */
  static #getDefaultHub() {
    return (this.#defaultHub ??= new lazy.ModelHub());
  }

  /**
   * Uninstall a feature by removing all engine instances it uses and deleting
   * all associated files for those engines.
   *
   * The caller passes all engine IDs that belong to the feature being removed.
   *
   * @param {object} params
   * @param {string[]} params.engineIds Engine IDs used by the feature to uninstall.
   * @param {string} [params.actor="other"] Identifier indicating who/what initiated the uninstall.
   * @param {ModelHub} [params.hub] ModelHub instance to use. Use the default if not provided.
   * @returns {Promise<void>}
   * @throws {Error} If removing an engine instance or deleting its associated files fails.
   */
  static async uninstall({ engineIds, actor = "other", hub }) {
    const modelHub = hub ?? this.#getDefaultHub();

    const promises = [];

    for (const engineId of engineIds) {
      promises.push(
        lazy.MLEngine.removeInstance(engineId).then(() =>
          modelHub.deleteFilesByEngine({ engineId, deletedBy: actor })
        )
      );
    }

    await allSettledOrReject(promises);
  }

  /**
   * Completely remove the ML engine and all associated data.
   *
   * This operation destroys all ML-related engine instances and
   * permanently deletes all cached model data.
   *
   * @param {object} params
   * @param {ModelHub} [params.hub] ModelHub instance to use. Use the default if not provided.
   *
   * @throws {Error} If any step of the uninstall process fails.
   */
  static async uninstallAll({ hub } = {}) {
    const modelHub = hub ?? this.#getDefaultHub();

    await lazy.EngineProcess.destroyMLEngine();
    await lazy.EngineProcess.destroyTranslationsEngine();
    await modelHub.purgeDatabase();
  }
}
