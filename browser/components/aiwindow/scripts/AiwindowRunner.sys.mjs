/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  // Chat handlers
  setRealTimeInfo:
    "moz-src:///browser/components/aiwindow/scripts/ChatRunner.sys.mjs",
  initConversation:
    "moz-src:///browser/components/aiwindow/scripts/ChatRunner.sys.mjs",
  submitTurn:
    "moz-src:///browser/components/aiwindow/scripts/ChatRunner.sys.mjs",

  // Memories handlers
  setMemories:
    "moz-src:///browser/components/aiwindow/scripts/MemoriesRunner.sys.mjs",
  getAllMemories:
    "moz-src:///browser/components/aiwindow/scripts/MemoriesRunner.sys.mjs",
  getRelevantMemories:
    "moz-src:///browser/components/aiwindow/scripts/MemoriesRunner.sys.mjs",
  clearMemories:
    "moz-src:///browser/components/aiwindow/scripts/MemoriesRunner.sys.mjs",

  // Browsing history
  searchBrowsingHistory:
    "moz-src:///browser/components/aiwindow/scripts/SearchBrowsingHistoryRunner.sys.mjs",
});

const TOOLKIT_RUNNER_URL =
  "resource://gre/modules/firefox_inference/Runner.sys.mjs";

/**
 * Dispatch a single named operation. Smart-Window handlers run locally;
 * everything else is forwarded to the toolkit Runner.sys.mjs dispatcher
 * (ML engine, translations, page extractor).
 *
 * @param {string} name
 * @param {...*} args
 */
export async function handleMessage(name, ...args) {
  switch (name) {
    case "set_real_time_info":
      return lazy.setRealTimeInfo(args[0]);
    case "init_conversation":
      return lazy.initConversation();
    case "submit_turn":
      return lazy.submitTurn(args[0], args[1] ?? {});
    case "set_memories":
      return lazy.setMemories(args[0] ?? []);
    case "get_all_memories":
      return lazy.getAllMemories();
    case "get_relevant_memories":
      return lazy.getRelevantMemories(args[0], args[1], args[2]);
    case "clear_memories":
      return lazy.clearMemories();
    case "search_browsing_history":
      return lazy.searchBrowsingHistory(args[0] ?? {});
    default: {
      const { handleMessage: toolkitHandleMessage } =
        ChromeUtils.importESModule(TOOLKIT_RUNNER_URL);
      return toolkitHandleMessage(name, ...args);
    }
  }
}

/**
 * Wire `handleMessage` into the Marionette / executeAsyncScript callback
 * convention: invoke `done` with either a success envelope or an error
 * envelope so Python-side consumers can deserialize uniformly. Envelope
 * shape is identical to toolkit Runner.sys.mjs#run.
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
 * Make any thrown value JSON-friendly. Mirrors toolkit Runner.serializeError
 * so the wire envelope stays bit-identical regardless of which dispatcher
 * handled the call.
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
