/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
const lazy = {};
const IN_WORKER = typeof importScripts !== "undefined";
const ES_MODULES_OPTIONS = IN_WORKER ? { global: "current" } : {};

ChromeUtils.defineESModuleGetters(
  lazy,
  {
    BLOCK_WORDS_ENCODED: "chrome://global/content/ml/BlockWords.sys.mjs",
    RemoteSettings: "resource://services-settings/remote-settings.sys.mjs",
    TranslationsParent: "resource://gre/actors/TranslationsParent.sys.mjs",
    OPFS: "chrome://global/content/ml/OPFS.sys.mjs",
  },
  ES_MODULES_OPTIONS
);

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevelPref: IN_WORKER ? "Error" : "browser.ml.logLevel",
    prefix: "ML:Utils",
  });
});

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

/** Creates the file URL from the organization, model, and version.
 *
 * @param {object} config - The configuration object to be updated.
 * @param {string} config.model - model name
 * @param {string} config.revision - model revision
 * @param {string} config.file - filename
 * @param {string} config.rootUrl - root url of the model hub
 * @param {string} config.urlTemplate - url template of the model hub
 * @param {boolean} config.addDownloadParams - Whether to add a download query parameter.
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
 * Converts a model and its headers to a Response object.
 *
 * @param {string} modelFilePath - path to the model file in Origin Private FileSystem (OPFS).
 * @param {object|null} headers
 * @returns {Response} The generated Response instance
 */
export async function modelToResponse(modelFilePath, headers) {
  let responseHeaders = {};

  if (headers) {
    // Headers are converted to strings, as the cache may hold int keys like fileSize
    for (let key in headers) {
      if (headers[key] != null) {
        responseHeaders[key] = headers[key].toString();
      }
    }
  }

  const file = await (await lazy.OPFS.getFileHandle(modelFilePath)).getFile();

  return new Response(file.stream(), {
    status: 200,
    headers: responseHeaders,
  });
}

/**
 * Reads the body of a fetch `Response` object and writes it to a provided `WritableStream`,
 * tracking progress and reporting it via a callback.
 *
 * @param {Response} response - The fetch `Response` object containing the body to read.
 * @param {WritableStream} writableStream - The destination stream where the response body
 *                                          will be written.
 * @param {?function(ProgressAndStatusCallbackParams):void} progressCallback The function to call with progress updates.
 */
export async function readResponseToWriter(
  response,
  writableStream,
  progressCallback
) {
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

  // Pipes the response body through the progress stream into the writable stream.
  await response.body.pipeThrough(progressStream).pipeTo(writableStream);
}

// Create a "namespace" to make it easier to import multiple names.
export var Progress = Progress || {};
Progress.ProgressAndStatusCallbackParams = ProgressAndStatusCallbackParams;
Progress.ProgressStatusText = ProgressStatusText;
Progress.ProgressType = ProgressType;
Progress.readResponse = readResponse;
Progress.readResponseToWriter = readResponseToWriter;

export async function getInferenceProcessInfo() {
  // for now we only have a single inference process.
  let info = await ChromeUtils.requestProcInfo();

  for (const child of info.children) {
    if (child.type === "inference") {
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
