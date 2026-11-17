/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { clearTimeout, setTimeout } from "resource://gre/modules/Timer.sys.mjs";

const WORKER_URL = "resource://gre/modules/translations/cld-worker.js";

/**
 * @import {DetectionResult} from "./translations.d.ts"
 */

/**
 * An internal class to manage communicating to the worker, and managing its lifecycle.
 * It's initialized once below statically to the module.
 */
class WorkerManager {
  // Since Emscripten can handle heap growth, but not heap shrinkage, we need to refresh
  // the worker after we've processed a particularly large string in order to prevent
  // unnecessary resident memory growth.
  //
  // These values define the cut-off string length and the idle timeout (in milliseconds)
  // before destroying a worker. Once a string of the maximum size has been processed,
  // the worker is marked for destruction, and is terminated as soon as it has been idle
  // for the given timeout.
  //
  // 1.5MB. This is the approximate string length that forces heap growth for a 2MB heap.
  LARGE_STRING = 1.5 * 1024 * 1024;
  IDLE_TIMEOUT = 10_000;

  /**
   * Resolvers for the detection queue.
   *
   * @type {Array<(result: DetectionResult) => void>}
   */
  detectionQueue = [];

  /**
   * @type {Worker | null}
   */
  worker = null;

  /**
   * @type {Promise<Worker> | null}
   */
  workerPromise = null;

  /**
   * Holds the ID of the current pending idle cleanup setTimeout.
   *
   * @type {number | null}
   */
  idleTimeoutId = null;

  /**
   * @param {DetectionOptions} options
   * @returns {Promise<DetectionResult>}
   */
  async detectLanguage(options) {
    const worker = await this.getWorker();

    const result = await new Promise(resolve => {
      this.detectionQueue.push(resolve);
      worker.postMessage(options);
    });

    // We have our asynchronous result from the worker.
    //
    // Determine if our input was large enough to trigger heap growth,
    // or if we're already waiting to destroy the worker when it's
    // idle. If so, schedule termination after the idle timeout.
    if (
      options.text.length >= this.LARGE_STRING ||
      this.idleTimeoutId != null
    ) {
      this.flushWorker();
    }

    return result;
  }

  /**
   * @returns {Promise<Worker>}
   */
  getWorker() {
    if (!this.workerPromise) {
      this.workerPromise = new Promise(resolve => {
        let worker = new Worker(WORKER_URL);
        worker.onmessage = message => {
          if (message.data == "ready") {
            resolve(worker);
          } else {
            /** @type {DetectionResult} */
            const detectionResult = message.data;

            const resolver = this.detectionQueue.shift();
            resolver(detectionResult);
          }
        };
        this.worker = worker;
      });
    }

    return this.workerPromise;
  }

  /**
   * Schedule the current worker to be terminated after the idle timeout.
   */
  flushWorker() {
    if (this.idleTimeoutId != null) {
      clearTimeout(this.idleTimeoutId);
    }

    this.idleTimeoutId = setTimeout(() => {
      if (this.detectionQueue.length) {
        // Reschedule the termination as something else was added to the queue.
        this.flushWorker();
      } else {
        // Terminate the worker.
        if (this.worker) {
          this.worker.terminate();
        }

        this.worker = null;
        this.workerPromise = null;
        this.idleTimeoutId = null;
      }
    }, this.IDLE_TIMEOUT);
  }
}

/**
 * The worker manager is static to this module. Exported it for unit testing.
 */
export const workerManager = new WorkerManager();

/**
 * This class provides the ability to identify the language of text using
 * the CLD2 language-detection algorithm.
 */
export class LanguageDetector {
  /**
   * Detect the language of a given string.
   *
   * @param {DetectionOptions | string} options - Either the text to analyze,
   *     or the options.
   * @returns {Promise<DetectionResult>}
   */
  static async detectLanguage(options) {
    if (typeof options === "string") {
      options = { text: options };
    }

    const result = await workerManager.detectLanguage(options);

    // Some language tags are not supported by CLD2
    result.language = this.maybeRefineMacroLanguageTag(result.language);
    result.languages.forEach(lng => {
      lng.languageCode = this.maybeRefineMacroLanguageTag(lng.languageCode);
    });

    return result;
  }

  /**
   * Attempts to make the language tag more specific if it is a supported macro language tag.
   * If no special cases apply, the provided language tag is returned as-is.
   *
   * @param {string} langTag - A BCP-47 language tag to evaluate and possibly refine.
   * @returns {string} - The refined language tag
   */
  static maybeRefineMacroLanguageTag(langTag) {
    if (langTag === "no") {
      // Choose "Norwegian Bokmål" over "Norwegian Nynorsk" as it is more widely used.
      //
      // https://en.wikipedia.org/wiki/Norwegian_language#Bokm%C3%A5l_and_Nynorsk
      //
      //   > A 2005 poll indicates that 86.3% use primarily Bokmål as their daily
      //   > written language, 5.5% use both Bokmål and Nynorsk, and 7.5% use
      //   > primarily Nynorsk.
      return "nb";
    }

    // No special cases were handled above, so pass the langTag through.
    return langTag;
  }
}
