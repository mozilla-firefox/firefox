/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  // ML engine
  createMlEngine:
    "resource://gre/modules/firefox_inference/MlEngineRunner.sys.mjs",
  runMlEngine:
    "resource://gre/modules/firefox_inference/MlEngineRunner.sys.mjs",
  destroyMlEngine:
    "resource://gre/modules/firefox_inference/MlEngineRunner.sys.mjs",

  // Translations
  createTranslationsSession:
    "resource://gre/modules/firefox_inference/TranslationsRunner.sys.mjs",
  runTranslationsSession:
    "resource://gre/modules/firefox_inference/TranslationsRunner.sys.mjs",
  destroyTranslationsSession:
    "resource://gre/modules/firefox_inference/TranslationsRunner.sys.mjs",

  // Page extractor
  getPageText:
    "resource://gre/modules/firefox_inference/PageExtractorRunner.sys.mjs",
  getPageInfo:
    "resource://gre/modules/firefox_inference/PageExtractorRunner.sys.mjs",
  getReaderModeContent:
    "resource://gre/modules/firefox_inference/PageExtractorRunner.sys.mjs",
  getSelectionText:
    "resource://gre/modules/firefox_inference/PageExtractorRunner.sys.mjs",
  runHeadlessExtractor:
    "resource://gre/modules/firefox_inference/PageExtractorRunner.sys.mjs",
});

/**
 * Dispatch a single named operation to the appropriate runner module.
 *
 * @param {string} name
 * @param {...*} args
 */
export async function handleMessage(name, ...args) {
  switch (name) {
    case "create_ml_engine":
      return lazy.createMlEngine(args[0]);
    case "run_ml_engine":
      return lazy.runMlEngine(args[0], args[1]);
    case "destroy_ml_engine":
      return lazy.destroyMlEngine(args[0], args[1]);
    case "create_translations_session":
      return lazy.createTranslationsSession(args[0] ?? {});
    case "run_translations_session":
      return lazy.runTranslationsSession(args[0], args[1] ?? {});
    case "destroy_translations_session":
      return lazy.destroyTranslationsSession(args[0], args[1] ?? {});
    case "get_page_text":
      return lazy.getPageText(args[0] ?? {});
    case "get_reader_mode_content":
      return lazy.getReaderModeContent(args[0]);
    case "get_page_info":
      return lazy.getPageInfo(args[0] ?? {});
    case "get_selection_text":
      return lazy.getSelectionText(args[0] ?? {});
    case "get_headless_page_text":
      return lazy.runHeadlessExtractor(args[0], args[1] ?? {});
    default:
      throw new Error(`Unknown message: ${name}`);
  }
}

/**
 * Wire `handleMessage` into the Marionette / executeAsyncScript callback
 * convention: invoke `done` with either a success envelope or an error
 * envelope so Python-side consumers can deserialize uniformly.
 *
 * @param {(envelope: object) => void} done
 * @param {Array<*>} args
 */
export async function run(done, args) {
  try {
    done({ name: "success", result: await handleMessage(...args) });
  } catch (error) {
    done({ name: "error", error: serializeError(error) });
  }
}

/**
 * Make any thrown value JSON-friendly. Errors collapse to
 * `{ message, name, stack }`; primitives become `{ message: String(value) }`.
 *
 * @param {unknown} error
 * @returns {{ message?: string; name?: string; stack?: string }}
 */
export function serializeError(error) {
  if (error && typeof error === "object") {
    const { message, name, stack } = /** @type {Error} */ (error);
    return { message: message ?? JSON.stringify(error), name, stack };
  }
  return { message: String(error) };
}
